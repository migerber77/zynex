/*
 * Zynex OS - Hyper-V-Eingabe (VMBus): synthetische Tastatur und Maus.
 * Generation-2-Maschinen haben weder PS/2 noch USB; Eingaben kommen nur ueber VMBus-Kanaele.
 * Ablauf: Hypercall-Seite + SynIC einrichten -> VMBus verbinden -> Geraete anfragen -> Tastatur- und Maus-Kanal oeffnen ->
 * Pakete aus den Ringpuffern lesen (Abfrage, ohne Interrupts).
 */
#include "hyperv.h"
#include "usb.h"
#include "block.h"
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
#define RING_PAGES 12                      /* je Richtung: 1 Kopfseite + 11 Datenseiten (24 Seiten passen in eine GPADL-Kopfnachricht) */
#define RING_DATA ((RING_PAGES - 1) * 4096u)

struct kanal {
    int offen; uint32_t relid, connid;
    uint8_t* mem;                          /* Sendering (Gast -> Host) zuerst, dann Empfangsring */
    int art;                               /* 1 Tastatur, 2 Maus */
    int bereit;                            /* Protokoll ausgehandelt */
    uint64_t trans;
};
static uint8_t ringmem[4][2 * RING_PAGES * 4096] __attribute__((aligned(4096)));
static struct kanal kb, ms;

extern void desktop_splash_info(const char* text);
static char status[400] = "";
static int hv_da, hv_show, nachrichten;
static void st_setze(const char* s) { int i = 0; while (s[i] && i < 398) { status[i] = s[i]; i++; } status[i] = 0; desktop_splash_info(status); }
static void st_anhaengen(const char* s) { int i = 0; while (status[i]) i++; while (*s && i < 398) status[i++] = *s++; status[i] = 0; desktop_splash_info(status); }

int hv_aktiv(void) { return hv_da; }
const char* hv_status(void) { return status; }
void hv_status_aus(void) { hv_show = 0; }
void hv_status_zeichnen(void) { }                    /* Statusleiste ist abgeschaltet; der Text steht im Log und auf der Plattenseite */

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
static uint64_t last_tid;
static int ring_lesen(struct kanal* c, uint8_t* out, uint32_t max, uint16_t* typ) {
    struct ring r = ring_empfang(c);
    uint32_t rd = r.hdr[1], w = r.hdr[0];
    if (rd == w) return -1;
    uint8_t dsc[16]; ring_kopiere_aus(&r, rd, dsc, 16); last_tid = *(uint64_t*)(dsc + 8);
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
static const uint8_t GUID_SCSI[16] = { 0xd9, 0x63, 0x61, 0xba, 0xa1, 0x04, 0x29, 0x4d, 0xb6, 0x05, 0x72, 0xe2, 0xff, 0xb1, 0xdc, 0x7f };
static struct kanal sc;
static const uint8_t GUID_NET[16] = { 0x63, 0x51, 0x61, 0xf8, 0x3e, 0xdf, 0xc5, 0x46, 0x91, 0x3f, 0xf2, 0xd2, 0xf9, 0x65, 0xed, 0x0e };
static struct kanal nv;
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
        else if (geq(ant + 8, GUID_NET)) { nv.relid = relid; nv.connid = conn; nv.art = 4; }
        else if (geq(ant + 8, GUID_SCSI)) { sc.relid = relid; sc.connid = conn; sc.art = 3; }
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
static int tasten_gesehen, maus_gesehen, tasten_geliefert, maus_geliefert;
static uint32_t letzte_make, letzte_info; static uint8_t letzte_hid, letzter_bericht[10];
static void taste(uint16_t make, uint32_t info) {
    letzte_make = make; letzte_info = info;
    if (info & 1) return;                                                       /* Unicode-Zeichen (Eingabemethoden): nicht unterstuetzt */
    int brk = (info >> 1) & 1, e0 = (info >> 2) & 1;
    uint8_t k = 0;
    if (!e0) {
        if (make == 0x2A || make == 0x36) { shift_down = !brk; return; }
        if (make == 0x1D) { usb_ctrl = !brk; return; }
        if (make == 0x38) return;                              /* Strg, Alt: keine Zeichen */
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
    tasten_geliefert++; letzte_hid = k;
    usb_key_event(k, shift_down, altgr_down);
}


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
                const uint8_t* r = p + 16; int o = 0;                                   /* Hyper-V-Maus: kein Report-ID-Byte [Tasten][X lo hi][Y lo hi] */
                int x = r[o + 1] | r[o + 2] << 8, y = r[o + 3] | r[o + 4] << 8;
                for (int q = 0; q < 10; q++) letzter_bericht[q] = r[q];
                maus_gesehen++;
                int w, h; gfx_size(&w, &h);
                if (w > 0 && h > 0) maus_geliefert++;
                if (w > 0 && h > 0) usb_abs_pointer((int)((int64_t)x * (w - 1) / 32767), (int)((int64_t)y * (h - 1) / 32767), (uint8_t)(r[o] & 7));
            }
        }
    }
}

static int zp;
void hv_poll(void) {
    if (!hv_da) return;
    zp++;
    if (msg_warten(0, 1) >= 0) nachrichten++;                                   /* verspaetete Kanalnachrichten verwerfen */
    if (kb.offen) kanal_bearbeiten(&kb);
    if (ms.offen) kanal_bearbeiten(&ms);
}


/* ---------- Hyper-V-Speicher (storvsc): virtuelle SCSI-Platte ueber VMBus ---------- */
extern uint32_t timer_get_ticks(void);
static int zeitquelle;                                               /* 1 = Hyper-V-Referenzzaehler (laeuft auch ohne Timer-Interrupts) */
static uint32_t hv_ticks(void) { if (zeitquelle) { uint32_t lo, hi; __asm__ volatile ("rdmsr" : "=a"(lo), "=d"(hi) : "c"(0x40000020u)); return (uint32_t)((((uint64_t)hi << 32) | lo) / 100000u); } return timer_get_ticks(); }
static uint8_t sbuf[65536] __attribute__((aligned(4096)));           /* Datenpuffer fuer jeden Befehl (physisch zusammenhaengend) */
static uint8_t spk[512];
static uint8_t s_port, s_pfad, s_ziel, s_lun;
static uint32_t s_bs = 512, s_sektoren;
static uint64_t s_tid = 0x1000;
static int s_ok, s_zeitueberschreitungen;

static int ring_senden_roh(struct kanal* c, const uint8_t* pkt, uint32_t len) {
    struct ring r = ring_sendung(c);
    uint32_t w = r.hdr[0], rd = r.hdr[1], total = len + 8;
    uint32_t frei = rd > w ? rd - w : RING_DATA - (w - rd);
    if (frei <= total) return -1;
    ring_kopiere_ein(&r, w, pkt, len);
    uint64_t prev = (uint64_t)w << 32; uint8_t t8[8]; mc(t8, &prev, 8);
    ring_kopiere_ein(&r, (w + len) % RING_DATA, t8, 8);
    mfence(); r.hdr[0] = (w + total) % RING_DATA; mfence();
    signal_host(c);
    return 0;
}
/* Paket: 16-Byte-Kopf [Typ][Versatz][Laenge][Flags][Kennung], dann "extra" (Seitenliste), dann 64 Byte vstor_packet */
static int sc_senden(uint16_t typ, uint16_t flags, const uint8_t* extra, uint32_t el, const uint8_t* pl, uint64_t tid) {
    uint32_t len = (16 + el + 64 + 7) & ~7u;
    if (len > sizeof(spk)) return -2;
    mz(spk, len);
    uint16_t* d = (uint16_t*)spk; d[0] = typ; d[1] = (uint16_t)((16 + el) / 8); d[2] = (uint16_t)(len / 8); d[3] = flags;
    mc(spk + 8, &tid, 8);
    if (el) mc(spk + 16, extra, el);
    mc(spk + 16 + el, pl, 64);
    return ring_senden_roh(&sc, spk, len);
}
static int sc_warte(uint64_t tid, uint8_t* out) {
    uint32_t t0 = hv_ticks(); uint8_t p[128]; uint16_t t;
    for (long spin = 0; spin < 300000000L && hv_ticks() - t0 < 500; spin++) {
        int n = ring_lesen(&sc, p, sizeof(p), &t);
        if (n < 0) { __asm__ volatile ("pause"); continue; }
        if (last_tid == tid && n >= 64) { mc(out, p, 64); return 0; }
    }
    s_zeitueberschreitungen++;
    return -1;
}
static int sc_anfrage(uint32_t op, const uint8_t* arg4, uint8_t* ant) {
    uint8_t pl[64]; mz(pl, 64); *(uint32_t*)pl = op; *(uint32_t*)(pl + 4) = 1;
    if (arg4) mc(pl + 12, arg4, 4);
    uint64_t tid = ++s_tid;
    if (sc_senden(6, 1, 0, 0, pl, tid)) return -1;
    if (sc_warte(tid, ant)) return -2;
    return *(uint32_t*)(ant + 8) ? -3 : 0;
}
/* SCSI-Befehl; richtung 0 = keine Daten, 1 = lesen, 2 = schreiben; Daten liegen in sbuf */
static int sc_cmd(const uint8_t* cdb, int cl, int richtung, uint32_t len) {
    for (int versuch = 0; versuch < 3; versuch++) {
        uint8_t pl[64]; mz(pl, 64);
        *(uint32_t*)pl = 3; *(uint32_t*)(pl + 4) = 1;
        *(uint16_t*)(pl + 12) = 52;
        pl[16] = s_port; pl[17] = s_pfad; pl[18] = s_ziel; pl[19] = s_lun; pl[20] = (uint8_t)cl; pl[21] = 20; pl[22] = richtung == 1;
        *(uint32_t*)(pl + 24) = richtung ? len : 0; mc(pl + 28, cdb, (uint32_t)cl);
        *(uint32_t*)(pl + 52) = richtung == 1 ? 0x40 : richtung == 2 ? 0x80 : 0x10; *(uint32_t*)(pl + 56) = 60;
        uint64_t tid = ++s_tid; int r;
        if (richtung && len) {
            uint32_t np = (len + 4095) / 4096; uint8_t ex[16 + 8 * 16]; mz(ex, sizeof(ex));
            *(uint32_t*)(ex + 4) = 1; *(uint32_t*)(ex + 8) = len; *(uint32_t*)(ex + 12) = 0;
            for (uint32_t i = 0; i < np; i++) { uint64_t pfn = ((uint64_t)(uintptr_t)sbuf >> 12) + i; mc(ex + 16 + 8 * i, &pfn, 8); }
            r = sc_senden(9, 1, ex, 16 + 8 * np, pl, tid);
        } else r = sc_senden(6, 1, 0, 0, pl, tid);
        if (r) return -1;
        uint8_t ant[64];
        if (sc_warte(tid, ant)) return -2;
        if ((ant[14] & 0x3F) == 1 && ant[15] == 0) return 0;
        if (ant[15] == 2) continue;                                   /* Check Condition (z. B. Unit Attention): nochmal */
        return -3 - (ant[14] & 0x3F) * 16;
    }
    return -4;
}
static int sc_blk(uint32_t blk, uint32_t n, int w) {
    uint8_t c[10] = { (uint8_t)(w ? 0x2A : 0x28), 0, (uint8_t)(blk >> 24), (uint8_t)(blk >> 16), (uint8_t)(blk >> 8), (uint8_t)blk, 0, (uint8_t)(n >> 8), (uint8_t)n, 0 };
    return sc_cmd(c, 10, w ? 2 : 1, n * s_bs);
}
static int s_rw(uint32_t lba, uint8_t* buf, uint32_t count, int w) {
    if (!s_ok) return -1;
    uint32_t r = s_bs / 512;
    while (count) {
        if (r == 1) {
            uint32_t n = count > 128 ? 128 : count;
            if (w) mc(sbuf, buf, n * 512);
            if (sc_blk(lba, n, w)) return -1;
            if (!w) mc(buf, sbuf, n * 512);
            lba += n; buf += n * 512; count -= n;
        } else {
            uint32_t blk = lba / r, off = lba % r;
            if (off == 0 && count >= r) {
                uint32_t nb = count / r; if (nb > 16) nb = 16; uint32_t sec = nb * r;
                if (w) mc(sbuf, buf, sec * 512);
                if (sc_blk(blk, nb, w)) return -1;
                if (!w) mc(buf, sbuf, sec * 512);
                lba += sec; buf += sec * 512; count -= sec;
            } else {
                uint32_t n = r - off; if (n > count) n = count;
                if (sc_blk(blk, 1, 0)) return -1;
                if (w) { mc(sbuf + off * 512, buf, n * 512); if (sc_blk(blk, 1, 1)) return -1; }
                else mc(buf, sbuf + off * 512, n * 512);
                lba += n; buf += n * 512; count -= n;
            }
        }
    }
    return 0;
}
static int hv_blk_read(uint32_t lba, void* buf, uint32_t count) { return s_rw(lba, (uint8_t*)buf, count, 0); }
static int hv_blk_write(uint32_t lba, const void* buf, uint32_t count) { return s_rw(lba, (uint8_t*)(uintptr_t)buf, count, 1); }

static void st_zahl(int v) { char t[12], o[12]; int n = 0, k = 0; if (v < 0) { o[k++] = '-'; v = -v; } if (!v) t[n++] = '0'; while (v) { t[n++] = (char)('0' + v % 10); v /= 10; } while (n) o[k++] = t[--n]; o[k] = 0; st_anhaengen(o); }

/* Platte suchen (INQUIRY auf Port/Pfad/Ziel/LUN) und Groesse lesen; liefert 0 bei Erfolg */
static int sc_platte_suchen(uint8_t port_ant) {
    uint8_t ports[2] = { 0, port_ant }; int np = port_ant ? 2 : 1;
    for (int pi = 0; pi < np; pi++)
        for (int z = 0; z < 4; z++)
            for (int l = 0; l < 8; l++) {
                s_port = ports[pi]; s_pfad = 0; s_ziel = (uint8_t)z; s_lun = (uint8_t)l;
                uint8_t inq[6] = { 0x12, 0, 0, 0, 36, 0 };
                int r = sc_cmd(inq, 6, 1, 36);
                if (s_zeitueberschreitungen >= 2) return -10;
                if (r == 0 && (sbuf[0] & 0xE0) == 0 && (sbuf[0] & 0x1F) == 0) return 0;
            }
    return -11;
}
static int sc_kapazitaet(void) {
    uint8_t c10[10] = { 0x25, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    if (sc_cmd(c10, 10, 1, 8)) return -1;
    uint32_t last = (uint32_t)sbuf[0] << 24 | (uint32_t)sbuf[1] << 16 | (uint32_t)sbuf[2] << 8 | sbuf[3];
    uint32_t bs = (uint32_t)sbuf[4] << 24 | (uint32_t)sbuf[5] << 16 | (uint32_t)sbuf[6] << 8 | sbuf[7];
    uint64_t blocks = (uint64_t)last + 1;
    if (last == 0xFFFFFFFFu) {
        uint8_t c16[16] = { 0x9E, 0x10, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 32, 0, 0 };
        if (sc_cmd(c16, 16, 1, 32)) return -2;
        uint64_t l = 0; for (int i = 0; i < 8; i++) l = l << 8 | sbuf[i];
        blocks = l + 1; bs = (uint32_t)sbuf[8] << 24 | (uint32_t)sbuf[9] << 16 | (uint32_t)sbuf[10] << 8 | sbuf[11];
    }
    if (bs != 512 && bs != 4096) return -3;
    s_bs = bs;
    uint64_t sec = blocks * (bs / 512);
    s_sektoren = sec > 0xFFFFFFFFull ? 0xFFFFFFFFu : (uint32_t)sec;
    return 0;
}
static void sc_start(void) {
    if (!sc.art) { st_anhaengen(" | keine SCSI-Platte (Controller fehlt)"); return; }
    st_anhaengen(" | Platte:");
    int e = kanal_oeffnen(&sc, 2);
    if (e) { st_anhaengen(" Kanal Fehler "); st_zahl(e); return; }
    uint8_t ant[64], ver[4];
    if (sc_anfrage(7, 0, ant)) { st_anhaengen(" Start Fehler"); return; }
    static const uint16_t vers[3] = { 0x0602, 0x0600, 0x0501 };
    int vok = 0;
    for (int i = 0; i < 3 && !vok; i++) { ver[0] = (uint8_t)vers[i]; ver[1] = (uint8_t)(vers[i] >> 8); ver[2] = 0; ver[3] = 0; if (sc_anfrage(9, ver, ant) == 0) vok = 1; }
    if (!vok) { st_anhaengen(" Protokoll abgelehnt"); return; }
    if (sc_anfrage(10, 0, ant)) { st_anhaengen(" Eigenschaften Fehler"); return; }
    uint8_t port_ant = ant[16];
    kprint("[HV] storvsc Eigenschaften:"); for (int i = 12; i < 32; i++) { kprint(" "); kprint_dec(ant[i]); } kprint("\n");
    if (sc_anfrage(8, 0, ant)) { st_anhaengen(" Ende Fehler"); return; }
    st_anhaengen(" verbunden");
    int r = sc_platte_suchen(port_ant);
    if (r) { st_anhaengen(" keine Platte gefunden "); st_zahl(r); return; }
    r = sc_kapazitaet();
    if (r) { st_anhaengen(" Groesse unbekannt "); st_zahl(r); return; }
    s_ok = 1;
    static struct block_dev bd; bd.name = "hv0"; bd.sectors = s_sektoren; bd.read = hv_blk_read; bd.write = hv_blk_write; bd.parent = 0; bd.offset = 0;
    block_register(&bd);
    st_anhaengen(" hv0 "); st_zahl((int)(s_sektoren / 2048)); st_anhaengen(" MiB");
}

/* ---------- Hyper-V-Netzwerk (netvsc): NVSP + RNDIS ueber VMBus ---------- */
#define NRX_BYTES (2u * 1024 * 1024)
#define NSB 4
static uint8_t nrx[NRX_BYTES] __attribute__((aligned(4096)));        /* Empfangspuffer (Host schreibt Rahmen hinein) */
static uint8_t nsend[NSB][4096] __attribute__((aligned(4096)));       /* Sendepuffer */
static uint8_t nctl[4096] __attribute__((aligned(4096)));             /* RNDIS-Steuernachrichten */
static uint8_t npk[512], nrp[2048], nant[512], n_comp[48];
static uint64_t n_tid = 0x2000, n_comp_tid, ntx_tid[NSB];
static int n_ok, n_ctl_da, n_comp_da, n_in_poll;
static uint8_t n_mac[6];
static uint32_t n_req;
static void (*n_rx)(const uint8_t*, int);

static int ring_lesen_voll(struct kanal* c, uint8_t* out, uint32_t max) {
    struct ring r = ring_empfang(c);
    uint32_t rd = r.hdr[1], w = r.hdr[0];
    if (rd == w) return -1;
    uint8_t dsc[16]; ring_kopiere_aus(&r, rd, dsc, 16);
    uint32_t pk = (uint32_t)(*(uint16_t*)(dsc + 4)) * 8, total = pk + 8;
    if (pk < 16 || total > RING_DATA) { r.hdr[1] = w; return -1; }
    if (pk > max) { r.hdr[1] = (rd + total) % RING_DATA; return -1; }
    ring_kopiere_aus(&r, rd, out, pk);
    mfence(); r.hdr[1] = (rd + total) % RING_DATA; mfence();
    if (r.hdr[3]) signal_host(c);                                      /* Host wartete auf freien Platz */
    return (int)pk;
}
static int nv_senden(uint16_t typ, uint16_t flags, const uint8_t* extra, uint32_t el, const uint8_t* pl, uint32_t pllen, uint64_t tid) {
    uint32_t len = (16 + el + pllen + 7) & ~7u;
    if (len > sizeof(npk)) return -2;
    mz(npk, len);
    uint16_t* d = (uint16_t*)npk; d[0] = typ; d[1] = (uint16_t)((16 + el) / 8); d[2] = (uint16_t)(len / 8); d[3] = flags;
    mc(npk + 8, &tid, 8);
    if (el) mc(npk + 16, extra, el);
    mc(npk + 16 + el, pl, pllen);
    return ring_senden_roh(&nv, npk, len);
}
static void nv_paket(const uint8_t* p, int len) {
    uint16_t typ = *(uint16_t*)p, off8 = *(uint16_t*)(p + 2); uint64_t tid = *(uint64_t*)(p + 8);
    if ((int)off8 * 8 > len) return;
    const uint8_t* pl = p + off8 * 8; int pllen = len - off8 * 8;
    if (typ == 11) {
        if (pllen >= 16) { mc(n_comp, pl, pllen > 48 ? 48 : (uint32_t)pllen); n_comp_tid = tid; n_comp_da = 1; }
        for (int i = 0; i < NSB; i++) if (ntx_tid[i] == tid) ntx_tid[i] = 0;
        return;
    }
    if (typ == 7 && pllen >= 8 && len >= 24) {
        uint32_t cnt = *(uint32_t*)(p + 20);
        uint32_t kanal = *(uint32_t*)pl == 107 ? *(uint32_t*)(pl + 4) : 0xFFFFu;
        for (uint32_t i = 0; i < cnt && 24 + 8 * (i + 1) <= (uint32_t)len; i++) {
            uint32_t bc = *(uint32_t*)(p + 24 + 8 * i), bo = *(uint32_t*)(p + 28 + 8 * i);
            if (bc < 8 || bo > NRX_BYTES || bc > NRX_BYTES - bo) continue;
            const uint8_t* m = nrx + bo;
            if (kanal == 1) { mc(nant, m, bc > sizeof(nant) ? (uint32_t)sizeof(nant) : bc); n_ctl_da = 1; }
            else if (kanal == 0 && *(uint32_t*)m == 1 && bc >= 44) {
                uint32_t doff = *(uint32_t*)(m + 8), dl = *(uint32_t*)(m + 12);
                if (8 + doff + dl <= bc && n_rx) n_rx(m + 8 + doff, (int)dl);
            }
        }
        uint8_t c[40]; mz(c, 40); *(uint32_t*)c = 108; *(uint32_t*)(c + 4) = 1;     /* SEND_RNDIS_PKT_COMPLETE, Erfolg */
        nv_senden(11, 0, 0, 0, c, 40, tid);
    }
}
static void nv_lesen_alle(void) {
    if (n_in_poll) return;
    n_in_poll = 1;
    for (int k = 0; k < 32; k++) { int n = ring_lesen_voll(&nv, nrp, sizeof(nrp)); if (n < 0) break; nv_paket(nrp, n); }
    n_in_poll = 0;
}
static int nv_warten(int* flag) {
    uint32_t t0 = hv_ticks();
    for (long spin = 0; spin < 300000000L && hv_ticks() - t0 < 500; spin++) {
        nv_lesen_alle();
        if (*flag) return 0;
        __asm__ volatile ("pause");
    }
    return -1;
}
static int nv_inband(const uint8_t* msg, int mit_antwort) {
    uint8_t pl[40]; mz(pl, 40); mc(pl, msg, 24);
    uint64_t tid = ++n_tid; n_comp_da = 0;
    if (nv_senden(6, mit_antwort ? 1 : 0, 0, 0, pl, 40, tid)) return -1;
    if (!mit_antwort) return 0;
    for (int i = 0; i < 2; i++) { if (nv_warten(&n_comp_da)) return -2; if (n_comp_tid == tid) return 0; n_comp_da = 0; }
    return -3;
}
/* RNDIS-Steuernachricht (liegt in nctl) senden und die Antwort in nant abwarten */
static int nv_ctl(uint32_t len) {
    uint8_t ex[24]; mz(ex, 24); *(uint32_t*)(ex + 4) = 1; *(uint32_t*)(ex + 8) = len; *(uint32_t*)(ex + 12) = 0;
    uint64_t pfn = (uint64_t)(uintptr_t)nctl >> 12; mc(ex + 16, &pfn, 8);
    uint8_t pl[40]; mz(pl, 40); *(uint32_t*)pl = 107; *(uint32_t*)(pl + 4) = 1; *(uint32_t*)(pl + 8) = 0xFFFFFFFFu;
    n_ctl_da = 0; uint64_t tid = ++n_tid;
    if (nv_senden(9, 1, ex, 24, pl, 40, tid)) return -1;
    if (nv_warten(&n_ctl_da)) return -2;
    return 0;
}
static int gpadl_erzeugen(uint32_t relid, uint32_t handle, const void* mem, uint32_t pages) {
    uint8_t m[256], ant[240]; uint32_t* w = (uint32_t*)m;
    mz(m, sizeof(m));
    uint32_t erste = pages > 26 ? 26 : pages;
    w[0] = 8; w[2] = relid; w[3] = handle;
    *(uint16_t*)(m + 16) = (uint16_t)(8 + 8 * pages); *(uint16_t*)(m + 18) = 1;
    *(uint32_t*)(m + 20) = pages * 4096; *(uint32_t*)(m + 24) = 0;
    for (uint32_t i = 0; i < erste; i++) { uint64_t pfn = ((uint64_t)(uintptr_t)mem >> 12) + i; mc(m + 28 + 8 * i, &pfn, 8); }
    if (post_msg(msg_conn, m, 28 + 8 * erste)) return -1;
    uint32_t pos = erste, nr = 1;
    while (pos < pages) {
        uint32_t n = pages - pos > 28 ? 28 : pages - pos;
        mz(m, sizeof(m)); w[0] = 9; w[2] = nr++; w[3] = handle;
        for (uint32_t i = 0; i < n; i++) { uint64_t pfn = ((uint64_t)(uintptr_t)mem >> 12) + pos + i; mc(m + 16 + 8 * i, &pfn, 8); }
        if (post_msg(msg_conn, m, 16 + 8 * n)) return -2;
        pos += n;
    }
    if (typ_warten(10, ant, 40000000) < 0) return -3;
    return *(uint32_t*)(ant + 16) ? -4 : 0;
}

int hv_net_bereit(void) { return n_ok; }
const uint8_t* hv_net_mac(void) { return n_mac; }
void hv_net_poll(void (*rx)(const uint8_t*, int)) { if (!n_ok) return; n_rx = rx; nv_lesen_alle(); }
int hv_net_senden(const uint8_t* f, int len) {
    if (!n_ok || len > 1514 || len < 14) return -1;
    int b = -1;
    for (int versuch = 0; versuch < 2 && b < 0; versuch++) {
        for (int i = 0; i < NSB; i++) if (!ntx_tid[i]) { b = i; break; }
        if (b < 0) { if (n_in_poll) return -2; nv_lesen_alle(); }
    }
    if (b < 0) return -2;
    uint8_t* m = nsend[b]; mz(m, 44);
    uint32_t tot = 44 + (uint32_t)len;
    *(uint32_t*)m = 1; *(uint32_t*)(m + 4) = tot; *(uint32_t*)(m + 8) = 36; *(uint32_t*)(m + 12) = (uint32_t)len;
    mc(m + 44, f, (uint32_t)len);
    uint8_t ex[24]; mz(ex, 24); *(uint32_t*)(ex + 4) = 1; *(uint32_t*)(ex + 8) = tot; *(uint32_t*)(ex + 12) = 0;
    uint64_t pfn = (uint64_t)(uintptr_t)m >> 12; mc(ex + 16, &pfn, 8);
    uint8_t pl[40]; mz(pl, 40); *(uint32_t*)pl = 107; *(uint32_t*)(pl + 4) = 0; *(uint32_t*)(pl + 8) = 0xFFFFFFFFu;
    uint64_t tid = ++n_tid; ntx_tid[b] = tid;
    if (nv_senden(9, 1, ex, 24, pl, 40, tid)) { ntx_tid[b] = 0; return -3; }
    return 0;
}

static void nv_start(void) {
    if (!nv.art) { st_anhaengen(" | Netz: kein Adapter"); return; }
    st_anhaengen(" | Netz:");
    int e = kanal_oeffnen(&nv, 3);
    if (e) { st_anhaengen(" Kanal Fehler "); st_zahl(e); return; }
    static const uint32_t vers[4] = { 0x00000002, 0x00030002, 0x00040000, 0x00050000 };
    int vok = 0;
    for (int i = 0; i < 4 && !vok; i++) {
        uint8_t msg[24]; mz(msg, 24); *(uint32_t*)msg = 1; *(uint32_t*)(msg + 4) = vers[i]; *(uint32_t*)(msg + 8) = vers[i];
        if (nv_inband(msg, 1) == 0 && *(uint32_t*)n_comp == 2 && *(uint32_t*)(n_comp + 12) == 1) vok = 1;
    }
    if (!vok) { st_anhaengen(" Protokoll abgelehnt"); return; }
    { uint8_t msg[24]; mz(msg, 24); *(uint32_t*)msg = 100; *(uint32_t*)(msg + 4) = 5; *(uint32_t*)(msg + 8) = 0; nv_inband(msg, 0); }   /* NDIS 5.0 */
    e = gpadl_erzeugen(nv.relid, 0xE1E30, nrx, NRX_BYTES / 4096);
    if (e) { st_anhaengen(" Empfangspuffer Fehler "); st_zahl(e); return; }
    { uint8_t msg[24]; mz(msg, 24); *(uint32_t*)msg = 101; *(uint32_t*)(msg + 4) = 0xE1E30; *(uint16_t*)(msg + 8) = 0xCAFE;
      if (nv_inband(msg, 1) || *(uint32_t*)n_comp != 102 || *(uint32_t*)(n_comp + 4) != 1) { st_anhaengen(" Empfangspuffer abgelehnt"); return; } }
    mz(nctl, 4096); *(uint32_t*)nctl = 2; *(uint32_t*)(nctl + 4) = 24; *(uint32_t*)(nctl + 8) = ++n_req; *(uint32_t*)(nctl + 12) = 1; *(uint32_t*)(nctl + 16) = 0; *(uint32_t*)(nctl + 20) = 0x4000;   /* RNDIS INIT */
    if (nv_ctl(24)) { st_anhaengen(" RNDIS Start Fehler"); return; }
    if (*(uint32_t*)nant != 0x80000002u || *(uint32_t*)(nant + 12) != 0) { st_anhaengen(" RNDIS abgelehnt"); return; }
    static const uint8_t fest[6] = { 0x02, 0x5A, 0x59, 0x4E, 0x45, 0x58 };
    for (int i = 0; i < 6; i++) n_mac[i] = fest[i];
    mz(nctl, 4096); *(uint32_t*)nctl = 4; *(uint32_t*)(nctl + 4) = 28; *(uint32_t*)(nctl + 8) = ++n_req; *(uint32_t*)(nctl + 12) = 0x01010101u; *(uint32_t*)(nctl + 16) = 0; *(uint32_t*)(nctl + 20) = 20;   /* MAC erfragen */
    if (nv_ctl(28) == 0 && *(uint32_t*)nant == 0x80000004u && *(uint32_t*)(nant + 12) == 0 && *(uint32_t*)(nant + 16) >= 6) {
        uint32_t off = 8 + *(uint32_t*)(nant + 20); if (off + 6 <= sizeof(nant)) for (int i = 0; i < 6; i++) n_mac[i] = nant[off + i];
    } else st_anhaengen(" MAC unbekannt");
    mz(nctl, 4096); *(uint32_t*)nctl = 5; *(uint32_t*)(nctl + 4) = 32; *(uint32_t*)(nctl + 8) = ++n_req; *(uint32_t*)(nctl + 12) = 0x0001010Eu; *(uint32_t*)(nctl + 16) = 4; *(uint32_t*)(nctl + 20) = 20; *(uint32_t*)(nctl + 24) = 0; *(uint32_t*)(nctl + 28) = 0x0D;   /* Paketfilter */
    if (nv_ctl(32) || *(uint32_t*)nant != 0x80000005u || *(uint32_t*)(nant + 12) != 0) { st_anhaengen(" Paketfilter Fehler"); return; }
    n_ok = 1;
    st_anhaengen(" bereit");
}

int hv_init(void) {
    uint32_t a, b, c, d;
    cpuid(1, &a, &b, &c, &d);
    if (!(c & (1u << 31))) return 0;
    cpuid(0x40000000, &a, &b, &c, &d);
    if (b != 0x7263694D || c != 0x666F736F || d != 0x76482074) return 0;       /* "Microsoft Hv" */
    uint32_t maxleaf = a; cpuid(0x40000003, &a, &b, &c, &d);
    if (maxleaf < 0x40000003 || !(a & (1u << 2)) || !(a & (1u << 5))) { st_setze("Hyper-V: Dienste fehlen"); return 1; }
    hv_da = 1; zeitquelle = (a >> 1) & 1;
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
    sc_start();
    nv_start();
    for (long i = 0; i < 40000000 && ((kb.offen && !kb.bereit) || (ms.offen && !ms.bereit)); i++) { hv_poll(); __asm__ volatile ("pause"); }
    if (kb.offen && !kb.bereit) { struct ring r = ring_empfang(&kb); struct ring q = ring_sendung(&kb); char z[40]; const char* h = "0123456789ABCDEF"; int k = 0; z[k++]=' '; z[k++]='['; z[k++]=h[(q.hdr[0]>>4)&15]; z[k++]=h[q.hdr[0]&15]; z[k++]='/'; z[k++]=h[(q.hdr[1]>>4)&15]; z[k++]=h[q.hdr[1]&15]; z[k++]='|'; z[k++]=h[(r.hdr[0]>>4)&15]; z[k++]=h[r.hdr[0]&15]; z[k++]='/'; z[k++]=h[(r.hdr[1]>>4)&15]; z[k++]=h[r.hdr[1]&15]; z[k++]=']'; z[k]=0; st_anhaengen(" Tastatur ohne Antwort"); st_anhaengen(z); }
    kprint("[HV] "); kprint(status); kprint("\n");
    return 1;
}
