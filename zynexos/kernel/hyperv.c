/*
 * Zynex OS - Hyper-V-Eingabe (VMBus): synthetische Tastatur und Maus.
 * Generation-2-Maschinen haben weder PS/2 noch USB; Eingaben kommen nur ueber VMBus-Kanaele.
 * Ablauf: Hypercall-Seite + SynIC einrichten -> VMBus verbinden -> Geraete anfragen -> Tastatur- und Maus-Kanal oeffnen ->
 * Pakete aus den Ringpuffern lesen (Abfrage, ohne Interrupts).
 */
#include "hyperv.h"
#include "usb.h"
#include <stdint.h>

extern void kprint(const char* str);
extern void kprint_dec(uint32_t val);
extern void gfx_size(int* w, int* h);
extern void gfx_begin(void);
extern void gfx_end(void);
extern void gfx_rect(int x, int y, int w, int h, uint32_t c);
extern void gfx_text(int x, int y, const char* s, uint32_t fg, int scale);
extern int  gfx_available(void);
extern void usb_abs_pointer(int x, int y, uint8_t buttons);

#define MSR_GUEST_OS_ID 0x40000000u
#define MSR_HYPERCALL   0x40000001u
#define MSR_VP_INDEX    0x40000002u
#define MSR_SCONTROL    0x40000080u
#define MSR_SIEFP       0x40000082u
#define MSR_SIMP        0x40000083u
#define MSR_EOM         0x40000084u
#define MSR_SINT0       0x40000090u
#define VMBUS_SINT      2
#define SINT_VEKTOR     47            /* unbenutzter Vektor mit vorhandenem Tor (IRQ15); mit Auto-EOI */

static inline void wrmsr(uint32_t m, uint64_t v) { __asm__ volatile ("wrmsr" : : "c"(m), "a"((uint32_t)v), "d"((uint32_t)(v >> 32))); }
static inline uint64_t rdmsr(uint32_t m) { uint32_t lo, hi; __asm__ volatile ("rdmsr" : "=a"(lo), "=d"(hi) : "c"(m)); return ((uint64_t)hi << 32) | lo; }
static inline void cpuid(uint32_t leaf, uint32_t* a, uint32_t* b, uint32_t* c, uint32_t* d) { __asm__ volatile ("cpuid" : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d) : "a"(leaf), "c"(0)); }
static inline void mfence(void) { __asm__ volatile ("mfence" : : : "memory"); }
static void mc(void* d, const void* s, uint32_t n) { volatile uint8_t* a = d; const uint8_t* b = s; for (uint32_t i = 0; i < n; i++) a[i] = b[i]; }
static void mz(void* d, uint32_t n) { volatile uint8_t* a = d; for (uint32_t i = 0; i < n; i++) a[i] = 0; }

/* ---------- Speicher (Identitaetsabbildung: virtuelle = physische Adresse) ---------- */
static uint8_t hcpage[4096] __attribute__((aligned(4096)));
static uint8_t simp[4096] __attribute__((aligned(4096)));
static uint8_t siefp[4096] __attribute__((aligned(4096)));
static uint8_t intpage[4096] __attribute__((aligned(4096)));
static uint8_t monpage[2][4096] __attribute__((aligned(4096)));
static uint8_t postbuf[256] __attribute__((aligned(16)));
#define RING_PAGES 4                       /* je Richtung: 1 Kopfseite + 3 Datenseiten */
#define RING_DATA ((RING_PAGES - 1) * 4096u)

struct kanal {
    int offen; uint32_t relid, connid;
    uint8_t* mem;                          /* Sendering (Gast -> Host) zuerst, dann Empfangsring */
    int art;                               /* 1 Tastatur, 2 Maus */
    int bereit;                            /* Protokoll ausgehandelt */
    uint64_t trans;
};
static uint8_t ringmem[2][2 * RING_PAGES * 4096] __attribute__((aligned(4096)));
static struct kanal kb, ms;

static char status[120] = "";
static int hv_da, hv_show, nachrichten;
static void st_setze(const char* s) { int i = 0; while (s[i] && i < 118) { status[i] = s[i]; i++; } status[i] = 0; if (hv_show) hv_status_zeichnen(); }
static void st_anhaengen(const char* s) { int i = 0; while (status[i]) i++; while (*s && i < 118) status[i++] = *s++; status[i] = 0; if (hv_show) hv_status_zeichnen(); }

const char* hv_status(void) { return status; }
void hv_status_aus(void) { hv_show = 0; }
void hv_status_zeichnen(void) {
    if (!status[0] || !gfx_available()) return;
    hv_show = 1;
    int w, h; gfx_size(&w, &h);
    gfx_begin(); gfx_rect(0, h - 22, w, 22, 0x0A1020); gfx_text(10, h - 19, status, 0x7F8FB0, 1); gfx_end();
}

/* ---------- Hypercalls und SynIC ---------- */
static uint64_t hvcall(uint64_t ctl, uint64_t in, uint64_t out) {
    uint64_t r; register uint64_t r8 __asm__("r8") = out;
    __asm__ volatile ("call *%[pg]" : "=a"(r), "+c"(ctl), "+d"(in), "+r"(r8) : [pg] "r"((uint64_t)(uintptr_t)hcpage) : "memory", "r9", "r10", "r11", "cc");
    return r;
}
static int post_msg(uint32_t conn, const void* payload, uint32_t len) {
    uint32_t* h = (uint32_t*)postbuf;
    for (int versuch = 0; versuch < 200; versuch++) {
        h[0] = conn; h[1] = 0; h[2] = 1; h[3] = len;
        mc(postbuf + 16, payload, len);
        uint64_t r = hvcall(0x5C, (uint64_t)(uintptr_t)postbuf, 0) & 0xFFFF;
        if (r == 0) return 0;
        if (r != 0x13 && r != 0x0B && r != 0x0A) return (int)r;      /* nicht "gleich nochmal" */
        for (volatile int i = 0; i < 200000; i++) {}
    }
    return -1;
}
static int msg_warten(uint8_t* out, long spin) {
    volatile uint32_t* slot = (volatile uint32_t*)(simp + VMBUS_SINT * 256);
    for (long i = 0; i < spin; i++) {
        if (slot[0] != 0) {
            uint32_t n = ((volatile uint8_t*)slot)[4]; uint8_t fl = ((volatile uint8_t*)slot)[5];
            if (n > 240) n = 240;
            if (out) mc(out, (const uint8_t*)slot + 16, n);
            slot[0] = 0; mfence();
            if (fl & 1) wrmsr(MSR_EOM, 0);
            return (int)n;
        }
        __asm__ volatile ("pause");
    }
    return -1;
}
/* wartet auf eine Kanalnachricht eines bestimmten Typs; andere werden verworfen (Angebote nur, wenn gewuenscht ausgewertet) */
static int typ_warten(uint32_t typ, uint8_t* out, long spin) {
    for (int k = 0; k < 64; k++) {
        int n = msg_warten(out, spin);
        if (n < 0) return -1;
        if (*(uint32_t*)out == typ) return n;
    }
    return -1;
}

/* ---------- Ringpuffer ---------- */
struct ring { volatile uint32_t* hdr; uint8_t* data; };
static struct ring ring_sendung(struct kanal* c) { struct ring r = { (volatile uint32_t*)c->mem, c->mem + 4096 }; return r; }
static struct ring ring_empfang(struct kanal* c) { struct ring r = { (volatile uint32_t*)(c->mem + RING_PAGES * 4096), c->mem + RING_PAGES * 4096 + 4096 }; return r; }
static void ring_kopiere_ein(struct ring* r, uint32_t pos, const uint8_t* s, uint32_t n) { for (uint32_t i = 0; i < n; i++) { r->data[pos] = s[i]; if (++pos == RING_DATA) pos = 0; } }
static void ring_kopiere_aus(struct ring* r, uint32_t pos, uint8_t* d, uint32_t n) { for (uint32_t i = 0; i < n; i++) { d[i] = r->data[pos]; if (++pos == RING_DATA) pos = 0; } }

static void signal_host(struct kanal* c) {
    ((volatile uint8_t*)intpage)[2048 + (c->relid >> 3)] |= (uint8_t)(1u << (c->relid & 7));     /* Sende-Ereignisbit */
    hvcall(0x5D | (1u << 16), c->connid, 0);                                                        /* HvSignalEvent (schnell) */
}
static int ring_senden(struct kanal* c, const void* payload, uint32_t len, uint16_t flags) {
    struct ring r = ring_sendung(c);
    uint32_t w = r.hdr[0], rd = r.hdr[1], plen = (len + 7) & ~7u, total = 16 + plen + 8;
    uint32_t frei = rd > w ? rd - w : RING_DATA - (w - rd);
    if (frei <= total) return -1;
    uint8_t pk[16 + 256 + 8]; if (total > sizeof(pk)) return -2;
    mz(pk, total);
    uint16_t* d = (uint16_t*)pk; d[0] = 6; d[1] = 2; d[2] = (uint16_t)((16 + plen) / 8); d[3] = flags;
    uint64_t tid = ++c->trans; mc(pk + 8, &tid, 8);
    mc(pk + 16, payload, len);
    uint64_t prev = (uint64_t)w << 32; mc(pk + 16 + plen, &prev, 8);
    ring_kopiere_ein(&r, w, pk, total);
    mfence();
    r.hdr[0] = (w + total) % RING_DATA; mfence();
    signal_host(c);
    return 0;
}
/* ein Paket lesen: liefert Nutzlast-Laenge oder -1 (nichts da) */
static int ring_lesen(struct kanal* c, uint8_t* out, uint32_t max, uint16_t* typ) {
    struct ring r = ring_empfang(c);
    uint32_t rd = r.hdr[1], w = r.hdr[0];
    if (rd == w) return -1;
    uint8_t dsc[16]; ring_kopiere_aus(&r, rd, dsc, 16);
    uint16_t t = *(uint16_t*)dsc, off8 = *(uint16_t*)(dsc + 2), len8 = *(uint16_t*)(dsc + 4);
    uint32_t total = (uint32_t)len8 * 8 + 8, pl = (uint32_t)(len8 - off8) * 8;
    if (len8 < off8 || total > RING_DATA) { r.hdr[1] = w; return -1; }                    /* defekt: alles verwerfen */
    if (pl > max) pl = max;
    ring_kopiere_aus(&r, (rd + (uint32_t)off8 * 8) % RING_DATA, out, pl);
    mfence();
    r.hdr[1] = (rd + total) % RING_DATA; mfence();
    *typ = t;
    return (int)pl;
}

/* ---------- VMBus ---------- */
static const uint8_t GUID_KBD[16] = { 0x6d, 0xad, 0x12, 0xf9, 0x17, 0x2b, 0xea, 0x48, 0xbd, 0x65, 0xf9, 0x27, 0xa6, 0x1c, 0x76, 0x84 };
static const uint8_t GUID_MAUS[16] = { 0x9e, 0xb6, 0xa8, 0xcf, 0x4a, 0x5b, 0xc0, 0x4c, 0xb9, 0x8b, 0x8b, 0xa1, 0xa1, 0xf3, 0xf9, 0x5a };
static int geq(const uint8_t* a, const uint8_t* b) { for (int i = 0; i < 16; i++) if (a[i] != b[i]) return 0; return 1; }

static uint32_t vp_index, verbunden_version, msg_conn = 1;

static int kanal_oeffnen(struct kanal* c, int nr) {
    c->mem = ringmem[nr]; mz(c->mem, 2 * RING_PAGES * 4096);
    uint32_t gpadl = 0xE1E10 + (uint32_t)nr;
    uint8_t m[256]; mz(m, sizeof(m));
    uint32_t* w = (uint32_t*)m;
    w[0] = 8; w[2] = c->relid; w[3] = gpadl;                                 /* GPADL_HEADER */
    *(uint16_t*)(m + 16) = 8 + 8 * 2 * RING_PAGES; *(uint16_t*)(m + 18) = 1;
    *(uint32_t*)(m + 20) = 2 * RING_PAGES * 4096; *(uint32_t*)(m + 24) = 0;
    for (int i = 0; i < 2 * RING_PAGES; i++) { uint64_t pfn = ((uint64_t)(uintptr_t)c->mem >> 12) + (uint64_t)i; mc(m + 28 + 8 * i, &pfn, 8); }
    if (post_msg(msg_conn, m, 28 + 8 * 2 * RING_PAGES)) return -1;
    uint8_t ant[240];
    if (typ_warten(10, ant, 20000000) < 0) return -2;                          /* GPADL_CREATED */
    if (*(uint32_t*)(ant + 16) != 0) return -3;
    mz(m, sizeof(m)); w[0] = 5; w[2] = c->relid; w[3] = c->relid; w[4] = gpadl; w[5] = vp_index; w[6] = RING_PAGES;   /* OPENCHANNEL */
    if (post_msg(msg_conn, m, 28 + 120)) return -4;
    if (typ_warten(6, ant, 20000000) < 0) return -5;                           /* OPENCHANNEL_RESULT */
    if (*(uint32_t*)(ant + 16) != 0) return -6;
    c->offen = 1; return 0;
}

static int hv_verbinden(void) {
    uint32_t versionen[4] = { 0x00040000, 0x00030000, 0x00020004, 0x00010003 };
    uint8_t m[48], ant[240];
    for (int v = 0; v < 4; v++) {
        mz(m, sizeof(m)); uint32_t* w = (uint32_t*)m;
        w[0] = 14; w[2] = versionen[v]; w[3] = versionen[v] >= 0x00030000 ? vp_index : 0;
        uint64_t a = (uint64_t)(uintptr_t)intpage, b = (uint64_t)(uintptr_t)monpage[0], c = (uint64_t)(uintptr_t)monpage[1];
        mc(m + 16, &a, 8); mc(m + 24, &b, 8); mc(m + 32, &c, 8);
        int pr = post_msg(1, m, 40);
        if (pr) { st_anhaengen(" Senden fehlgeschlagen"); return -1 - pr; }
        if (typ_warten(15, ant, 20000000) < 0) { if (v == 0) return -100; continue; }
        if (ant[8]) { verbunden_version = versionen[v]; return 0; }
    }
    return -101;
}

static int angebote(void) {
    uint8_t m[8] = { 3, 0, 0, 0, 0, 0, 0, 0 }, ant[240];
    if (post_msg(msg_conn, m, 8)) return -1;
    int n = 0;
    for (int k = 0; k < 200; k++) {
        int l = msg_warten(ant, 20000000);
        if (l < 0) return n ? n : -2;
        uint32_t t = *(uint32_t*)ant;
        if (t == 4) return n;                                                   /* ALLOFFERS_DELIVERED */
        if (t != 1 || l < 196) continue;                                        /* OFFERCHANNEL */
        n++;
        uint32_t relid = *(uint32_t*)(ant + 184), conn = *(uint32_t*)(ant + 192);
        if (geq(ant + 8, GUID_KBD)) { kb.relid = relid; kb.connid = conn; kb.art = 1; }
        else if (geq(ant + 8, GUID_MAUS)) { ms.relid = relid; ms.connid = conn; ms.art = 2; }
    }
    return n;
}

/* ---------- Tastatur: XT-Satz 1 -> HID-Nummer ---------- */
static const uint8_t xt2hid[0x59] = {
    0, 0x29, 0x1E, 0x1F, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x2D, 0x2E, 0x2A, 0x2B,
    0x14, 0x1A, 0x08, 0x15, 0x17, 0x1C, 0x18, 0x0C, 0x12, 0x13, 0x2F, 0x30, 0x28, 0, 0x04, 0x16,
    0x07, 0x09, 0x0A, 0x0B, 0x0D, 0x0E, 0x0F, 0x33, 0x34, 0x35, 0, 0x31, 0x1D, 0x1B, 0x06, 0x19,
    0x05, 0x11, 0x10, 0x36, 0x37, 0x38, 0, 0x55, 0, 0x2C, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E,
    0x3F, 0x40, 0x41, 0x42, 0x43, 0, 0, 0x5F, 0x60, 0x61, 0x56, 0x5C, 0x5D, 0x5E, 0x57, 0x59,
    0x5A, 0x5B, 0x62, 0x63, 0, 0, 0x64, 0x44, 0x45 };
static int shift_down, altgr_down;
static void taste(uint16_t make, uint32_t info) {
    if (info & 1) return;                                                       /* Unicode-Zeichen (Eingabemethoden): nicht unterstuetzt */
    int brk = (info >> 1) & 1, e0 = (info >> 2) & 1;
    uint8_t k = 0;
    if (!e0) {
        if (make == 0x2A || make == 0x36) { shift_down = !brk; return; }
        if (make == 0x1D || make == 0x38) return;                              /* Strg, Alt: keine Zeichen */
        if (make < sizeof(xt2hid)) k = xt2hid[make];
    } else {
        if (make == 0x38) { altgr_down = !brk; return; }
        switch (make) {
        case 0x48: k = 0x52; break; case 0x50: k = 0x51; break; case 0x4B: k = 0x50; break; case 0x4D: k = 0x4F; break;
        case 0x47: k = 0x4A; break; case 0x4F: k = 0x4D; break; case 0x49: k = 0x4B; break; case 0x51: k = 0x4E; break;
        case 0x52: k = 0x49; break; case 0x53: k = 0x4C; break; case 0x1C: k = 0x28; break; case 0x35: k = 0x54; break;
        }
    }
    if (!k || brk) return;
    usb_key_event(k, shift_down, altgr_down);
}

static int tasten_gesehen, maus_gesehen, maus_info;
static void kanal_bearbeiten(struct kanal* c) {
    uint8_t p[256]; uint16_t t;
    int n;
    for (int runde = 0; runde < 16 && (n = ring_lesen(c, p, sizeof(p), &t)) >= 0; runde++) {
        if (t != 6) continue;                                                  /* Abschluesse u. a. ignorieren */
        if (c->art == 1 && n >= 4) {
            uint32_t typ = *(uint32_t*)p;
            if (typ == 2) { if (n >= 8 && (*(uint32_t*)(p + 4) & 1)) { c->bereit = 1; st_anhaengen(" Tastatur ok"); } else st_anhaengen(" Tastatur abgelehnt"); }
            else if (typ == 3 && n >= 12) { tasten_gesehen++; taste(*(uint16_t*)(p + 4), *(uint32_t*)(p + 8)); }
        } else if (c->art == 2 && n >= 16) {
            uint32_t typ = *(uint32_t*)(p + 8);
            if (typ == 1) { if (n >= 21 && p[20]) { c->bereit = 1; st_anhaengen(" Maus ok"); } else st_anhaengen(" Maus abgelehnt"); }
            else if (typ == 2) {                                                /* Geraeteinfo: bestaetigen */
                uint8_t ack[17]; mz(ack, sizeof(ack)); *(uint32_t*)ack = 1; *(uint32_t*)(ack + 4) = 9; *(uint32_t*)(ack + 8) = 3; *(uint32_t*)(ack + 12) = 1;
                ring_senden(c, ack, sizeof(ack), 0);
            } else if (typ == 4 && n >= 24) {                                   /* Eingabebericht: [Kennung 1][Tasten][X lo hi][Y lo hi] */
                const uint8_t* r = p + 16; int o = (r[0] == 1) ? 1 : 0;
                int x = r[o + 1] | r[o + 2] << 8, y = r[o + 3] | r[o + 4] << 8;
                if (maus_gesehen == 0) maus_info = r[0];
                maus_gesehen++;
                int w, h; gfx_size(&w, &h);
                if (w > 0 && h > 0) usb_abs_pointer((int)((int64_t)x * (w - 1) / 32767), (int)((int64_t)y * (h - 1) / 32767), (uint8_t)(r[o] & 7));
            }
        }
    }
}

void hv_poll(void) {
    if (!hv_da) return;
    if (msg_warten(0, 1) >= 0) nachrichten++;                                   /* verspaetete Kanalnachrichten verwerfen */
    if (kb.offen) kanal_bearbeiten(&kb);
    if (ms.offen) kanal_bearbeiten(&ms);
}

int hv_init(void) {
    uint32_t a, b, c, d;
    cpuid(1, &a, &b, &c, &d);
    if (!(c & (1u << 31))) return 0;
    cpuid(0x40000000, &a, &b, &c, &d);
    if (b != 0x7263694D || c != 0x666F736F || d != 0x76482074) return 0;       /* "Microsoft Hv" */
    uint32_t maxleaf = a; cpuid(0x40000003, &a, &b, &c, &d);
    if (maxleaf < 0x40000003 || !(a & (1u << 2)) || !(a & (1u << 5))) { st_setze("Hyper-V: Dienste fehlen"); return 1; }
    hv_da = 1;
    st_setze("Hyper-V:");
    wrmsr(MSR_GUEST_OS_ID, ((uint64_t)0x8100 << 48) | (1ULL << 16));
    wrmsr(MSR_HYPERCALL, ((uint64_t)(uintptr_t)hcpage) | 1);
    if (!(rdmsr(MSR_HYPERCALL) & 1)) { st_anhaengen(" Hypercall-Seite nicht aktivierbar"); return 1; }
    vp_index = (uint32_t)rdmsr(MSR_VP_INDEX);
    mz(simp, 4096); mz(siefp, 4096); mz(intpage, 4096); mz(monpage[0], 4096); mz(monpage[1], 4096);
    wrmsr(MSR_SIMP, ((uint64_t)(uintptr_t)simp) | 1);
    wrmsr(MSR_SIEFP, ((uint64_t)(uintptr_t)siefp) | 1);
    wrmsr(MSR_SINT0 + VMBUS_SINT, SINT_VEKTOR | (1u << 17));                   /* Nachricht-Interrupt: nicht maskiert, Auto-EOI */
    wrmsr(MSR_SCONTROL, 1);
    st_anhaengen(" SynIC");
    int r = hv_verbinden();
    if (r) {
        st_anhaengen(" keine VMBus-Verbindung (Code "); char t[12]; int n = 0; int v = r < 0 ? -r : r; if (!v) t[n++] = '0'; while (v) { t[n++] = (char)('0' + v % 10); v /= 10; } char o[12]; int k = 0; while (n) o[k++] = t[--n]; o[k] = 0; st_anhaengen(o); st_anhaengen(")");
        return 1;
    }
    st_anhaengen(" VMBus");
    int ng = angebote();
    if (ng < 0) { st_anhaengen(" keine Geraeteliste"); return 1; }
    st_anhaengen(" Geraete:"); { char z[8]; int n=0,v=ng; char t[8]; if(!v) t[n++]='0'; while(v){t[n++]=(char)('0'+v%10); v/=10;} int k=0; while(n) z[k++]=t[--n]; z[k]=0; st_anhaengen(z);}
    if (!kb.art) st_anhaengen(" keine Tastatur gefunden");
    if (!ms.art) st_anhaengen(" keine Maus gefunden");
    if (kb.art) {
        int e = kanal_oeffnen(&kb, 0);
        if (e) { st_anhaengen(" Tastatur-Kanal Fehler "); char z[3] = { (char)('0' + (-e) % 10), 0, 0 }; st_anhaengen(z); }
        else { uint32_t req[2] = { 1, 0x00010000 }; if (ring_senden(&kb, req, 8, 1)) st_anhaengen(" Tastatur-Anfrage Fehler"); }
    }
    if (ms.art) {
        int e = kanal_oeffnen(&ms, 1);
        if (e) { st_anhaengen(" Maus-Kanal Fehler "); char z[3] = { (char)('0' + (-e) % 10), 0, 0 }; st_anhaengen(z); }
        else { uint32_t req[5] = { 1, 12, 0, 4, 0x00020000 }; if (ring_senden(&ms, req, 20, 1)) st_anhaengen(" Maus-Anfrage Fehler"); }
    }
    for (long i = 0; i < 40000000 && ((kb.offen && !kb.bereit) || (ms.offen && !ms.bereit)); i++) { hv_poll(); __asm__ volatile ("pause"); }
    if (kb.offen && !kb.bereit) { struct ring r = ring_empfang(&kb); struct ring q = ring_sendung(&kb); char z[40]; const char* h = "0123456789ABCDEF"; int k = 0; z[k++]=' '; z[k++]='['; z[k++]=h[(q.hdr[0]>>4)&15]; z[k++]=h[q.hdr[0]&15]; z[k++]='/'; z[k++]=h[(q.hdr[1]>>4)&15]; z[k++]=h[q.hdr[1]&15]; z[k++]='|'; z[k++]=h[(r.hdr[0]>>4)&15]; z[k++]=h[r.hdr[0]&15]; z[k++]='/'; z[k++]=h[(r.hdr[1]>>4)&15]; z[k++]=h[r.hdr[1]&15]; z[k++]=']'; z[k]=0; st_anhaengen(" Tastatur ohne Antwort"); st_anhaengen(z); }
    kprint("[HV] "); kprint(status); kprint("\n");
    return 1;
}
