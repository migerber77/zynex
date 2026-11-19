/*
 * Zynex OS - Intel-HD-Audio-Treiber (Ausgabe und Aufnahme)
 *
 * Ablauf: PCI-Controller (Klasse 04/03) finden und einblenden, zuruecksetzen, Befehlsring (CORB) und Antwortring (RIRB) einrichten,
 * den ersten Codec mit Audio-Funktionsgruppe untersuchen (Widgets, Verbindungslisten), einen Ausgabepfad (Pin -> ... -> DAC) und einen
 * Eingabepfad (ADC -> ... -> Mikrofon-/Line-Pin) suchen und einschalten (Strom, Verstaerker, Auswahl, Pin-Steuerung, Strom-Nummer, Format).
 * Daten laufen ueber je einen Stream-Deskriptor mit Ringpuffer aus 8 Stuecken zu 8 KiB (BDL). Alles per Polling, Format 48 kHz / 16 Bit / 2 Kanaele.
 * Nicht in Hyper-V (dort gibt es keine Audio-Hardware fuer die VM); getestet in QEMU (intel-hda + hda-duplex), gedacht fuer echte PCs und Laptops.
 */
#include "hda.h"
#include "pci.h"
#include "pmm.h"
#include "paging.h"
#include "timer.h"
#include <stdint.h>

extern void kprint(const char* str);
extern void kprint_dec(uint32_t val);
extern void kprint_hex(uint32_t val);

/* ---- Register ---- */
#define GCAP 0x00
#define GCTL 0x08
#define STATESTS 0x0E
#define INTCTL 0x20
#define CORBLBASE 0x40
#define CORBUBASE 0x44
#define CORBWP 0x48
#define CORBRP 0x4A
#define CORBCTL 0x4C
#define CORBSIZE 0x4E
#define RIRBLBASE 0x50
#define RIRBUBASE 0x54
#define RIRBWP 0x58
#define RINTCNT 0x5A
#define RIRBCTL 0x5C
#define RIRBSTS 0x5D
#define RIRBSIZE 0x5E

static volatile uint8_t* R;
static inline uint8_t  r8(uint32_t o)  { return *(volatile uint8_t*)(R + o); }
static inline uint16_t r16(uint32_t o) { return *(volatile uint16_t*)(R + o); }
static inline uint32_t r32(uint32_t o) { return *(volatile uint32_t*)(R + o); }
static inline void w8(uint32_t o, uint8_t v)   { *(volatile uint8_t*)(R + o) = v; }
static inline void w16(uint32_t o, uint16_t v) { *(volatile uint16_t*)(R + o) = v; }
static inline void w32(uint32_t o, uint32_t v) { *(volatile uint32_t*)(R + o) = v; }
static inline void fence(void) { __asm__ volatile ("mfence" : : : "memory"); }
static inline void pause_(void) { __asm__ volatile ("pause"); }
static inline void outb80(void) { __asm__ volatile ("outb %%al, $0x80" : : "a"((uint8_t)0)); }
static void warte_us(uint32_t us) { for (uint32_t i = 0; i < us; i++) outb80(); }          /* ca. 1 us je Aufruf auf echter Hardware */
static void nullen(void* p, uint32_t n) { uint8_t* b = p; for (uint32_t i = 0; i < n; i++) b[i] = 0; }

/* ---- Zustand ---- */
static int bereit, versucht;
static char info[96] = "nicht gestartet";
static int cad = -1, afg = -1;
static uint32_t* corb; static volatile uint64_t* rirb; static int corb_n, rirb_n; static uint32_t rirb_rp;
static uint32_t n_iss, n_oss;
static int s_aus = -1, s_ein = -1;                         /* Stream-Deskriptoren (Nummer) */
static int aus_pin, ein_pin, aus_dac, ein_adc;
static uint8_t aus_pf[8], aus_sl[8], aus_n, ein_pf[8], ein_sl[8], ein_n;

struct widget { uint8_t da, typ, nconn, conn[16]; uint32_t caps, cfg; };
static struct widget W[128];
static int wstart, wende;

#define CHUNK 8192
#define NCH 8
static uint8_t* ring;                                      /* 64 KiB, vom Stream gelesen/geschrieben */
static uint8_t* bdl;                                       /* 128 Byte ausgerichtet */

/* ---- Verben ---- */
static int use_imm, diag_gezeigt;
static int verb_imm(uint32_t cmd, uint32_t* antwort) {                                  /* Sofort-Befehlsschnittstelle (ICO/IRI/ICS) */
    for (long t = 0; t < 100000 && (r16(0x68) & 1); t++) warte_us(1);
    w16(0x68, 2);                                                                       /* Gueltig-Bit loeschen */
    w32(0x60, cmd); fence(); w16(0x68, 1);
    for (long t = 0; t < 100000; t++) { if ((r16(0x68) & 3) == 2) { if (antwort) *antwort = r32(0x64); w16(0x68, 2); return 0; } warte_us(1); }
    return -1;
}
static int verb(int nid, uint32_t v20, uint32_t* antwort) {
    uint32_t cmd = ((uint32_t)cad << 28) | ((uint32_t)nid << 20) | (v20 & 0xFFFFFu);
    if (use_imm) return verb_imm(cmd, antwort);
    uint32_t wp = ((uint32_t)(r16(CORBWP) & 0xFF) + 1) % (uint32_t)corb_n;
    corb[wp] = cmd; fence();
    w16(CORBWP, (uint16_t)wp);
    for (long t = 0; t < 3000000; t++) {
        uint32_t rwp = r16(RIRBWP) & 0xFF;
        if (rwp != rirb_rp) {
            rirb_rp = (rirb_rp + 1) % (uint32_t)rirb_n;
            uint32_t resp = (uint32_t)rirb[rirb_rp]; if (antwort) *antwort = resp;
            w8(RIRBSTS, 0x05);
            return 0;
        }
        if ((t & 1023) == 1023) warte_us(1);
        pause_();
    }
    if (!diag_gezeigt) {
        diag_gezeigt = 1;
        kprint("[HDA] CORB-Zeitueberschreitung: CORBWP="); kprint_hex(r16(CORBWP)); kprint(" CORBRP="); kprint_hex(r16(CORBRP));
        kprint(" CORBCTL="); kprint_hex(r8(CORBCTL)); kprint(" RIRBWP="); kprint_hex(r16(RIRBWP)); kprint(" RIRBCTL="); kprint_hex(r8(RIRBCTL));
        kprint(" GCTL="); kprint_hex(r32(GCTL)); kprint(" STATESTS="); kprint_hex(r16(STATESTS)); kprint("\n");
    }
    if (verb_imm(cmd, antwort) == 0) { use_imm = 1; kprint("[HDA] nutze Sofort-Befehle\n"); return 0; }
    return -1;
}
static uint32_t param(int nid, int p) { uint32_t r = 0; if (verb(nid, 0xF0000u | (uint32_t)p, &r)) return 0; return r; }

static void kopiere_info(const char* s) { int i = 0; while (s[i] && i < 95) { info[i] = s[i]; i++; } info[i] = 0; }

/* ---- Widgets lesen ---- */
static void verbindungen(int nid) {
    struct widget* w = &W[nid]; w->nconn = 0;
    uint32_t cl = param(nid, 0x0E); int len = (int)(cl & 0x7F), lang = (cl >> 7) & 1;
    int vorher = -1, idx = 0;
    while (idx < len && w->nconn < 16) {
        uint32_t r = 0; if (verb(nid, 0xF0200u | (uint32_t)idx, &r)) break;
        int pro = lang ? 2 : 4;
        for (int k = 0; k < pro && idx < len; k++, idx++) {
            uint32_t e = lang ? ((r >> (16 * k)) & 0xFFFF) : ((r >> (8 * k)) & 0xFF);
            int bereich = lang ? (e & 0x8000) : (e & 0x80); int n = (int)(lang ? (e & 0x7FFF) : (e & 0x7F));
            if (bereich && vorher >= 0) { for (int m = vorher + 1; m <= n && w->nconn < 16; m++) w->conn[w->nconn++] = (uint8_t)m; }
            else if (w->nconn < 16) w->conn[w->nconn++] = (uint8_t)n;
            vorher = n;
        }
    }
}
static void widgets_lesen(void) {
    uint32_t s = param(afg, 0x04); wstart = (int)((s >> 16) & 0xFF); int n = (int)(s & 0xFF); wende = wstart + n;
    if (wende > 128) wende = 128;
    for (int i = 0; i < 128; i++) W[i].da = 0;
    for (int nid = wstart; nid < wende; nid++) {
        struct widget* w = &W[nid];
        w->caps = param(nid, 0x09); w->typ = (uint8_t)((w->caps >> 20) & 0xF); w->da = 1; w->cfg = 0;
        if (w->typ == 4) { uint32_t r = 0; verb(nid, 0xF1C00u, &r); w->cfg = r; }
        if (w->caps & 0x100) verbindungen(nid); else w->nconn = 0;          /* Bit 8: hat Verbindungsliste */
    }
}

/* ---- Pfadsuche ---- */
static int dfs_aus(int nid, int tiefe, uint8_t* pf, uint8_t* sl) {                     /* von einem Pin zum DAC */
    if (tiefe >= 7) return -1;
    struct widget* w = &W[nid];
    if (w->typ == 0) { pf[tiefe] = (uint8_t)nid; return tiefe + 1; }
    for (int i = 0; i < w->nconn; i++) {
        int c = w->conn[i]; if (c < wstart || c >= wende || !W[c].da) continue;
        int n = dfs_aus(c, tiefe + 1, pf, sl); if (n > 0) { pf[tiefe] = (uint8_t)nid; sl[tiefe] = (uint8_t)i; return n; }
    }
    return -1;
}
static int dfs_ein(int nid, int tiefe, int gerat, uint8_t* pf, uint8_t* sl) {         /* vom ADC zu einem Eingangs-Pin der Art 'gerat' */
    if (tiefe >= 7) return -1;
    struct widget* w = &W[nid];
    if (w->typ == 4 && tiefe > 0) { if ((int)((w->cfg >> 20) & 0xF) == gerat && ((w->cfg >> 30) & 3) != 1) { pf[tiefe] = (uint8_t)nid; return tiefe + 1; } return -1; }
    for (int i = 0; i < w->nconn; i++) {
        int c = w->conn[i]; if (c < wstart || c >= wende || !W[c].da) continue;
        int n = dfs_ein(c, tiefe + 1, gerat, pf, sl); if (n > 0) { pf[tiefe] = (uint8_t)nid; sl[tiefe] = (uint8_t)i; return n; }
    }
    return -1;
}

/* ---- Knoten einschalten ---- */
static uint32_t ampcaps(int nid, int p) { return (W[nid].caps & 8) ? param(nid, p) : param(afg, p); }
static void aus_an(int nid) { if (!(W[nid].caps & 4)) return; uint32_t c = ampcaps(nid, 0x12); verb(nid, 0x30000u | 0xB000u | (c & 0x7F), 0); }      /* Ausgangsverstaerker: beide Kanaele, nicht stumm, 0 dB */
static void ein_an(int nid, int idx) { if (!(W[nid].caps & 2)) return; uint32_t c = ampcaps(nid, 0x0D); verb(nid, 0x30000u | 0x7000u | ((uint32_t)idx << 8) | (c & 0x7F), 0); }

static void pfad_an(const uint8_t* pf, const uint8_t* sl, int n, int ausgabe) {
    for (int i = 0; i < n; i++) {
        int nid = pf[i]; struct widget* w = &W[nid];
        verb(nid, 0x70500u, 0);                                                         /* Strom D0 */
        if (w->nconn > 1 && i + 1 < n) verb(nid, 0x70100u | sl[i], 0);                   /* Verbindung waehlen */
        aus_an(nid);
        if (i + 1 < n) ein_an(nid, sl[i]); else if (w->typ != 0 && w->typ != 4) ein_an(nid, 0);
        if (w->typ == 3 || w->typ == 2) { for (int k = 0; k < w->nconn; k++) ein_an(nid, k); }   /* Mischer: alle Eingaenge hoerbar */
    }
    int pin = ausgabe ? pf[0] : pf[n - 1];
    uint32_t pc = param(pin, 0x0C);
    if (ausgabe) { int gerat = (int)((W[pin].cfg >> 20) & 0xF); verb(pin, 0x70700u | (gerat == 2 ? 0xC0u : 0x40u), 0); if (pc & (1u << 16)) verb(pin, 0x70C00u | 2, 0); }
    else { int gerat = (int)((W[pin].cfg >> 20) & 0xF); verb(pin, 0x70700u | (gerat == 0xA ? 0x24u : 0x20u), 0); }
}

/* ---- Streams ---- */
#define SD(i) (0x80u + (uint32_t)(i) * 0x20u)
static int strom_reset(int i) {
    uint32_t o = SD(i);
    w8(o, 0);
    w8(o, 1);
    for (long t = 0; t < 100000 && !(r8(o) & 1); t++) warte_us(1);
    w8(o, 0);
    for (long t = 0; t < 100000 && (r8(o) & 1); t++) warte_us(1);
    return (r8(o) & 1) ? -1 : 0;
}
static int strom_vorbereiten(int i, int tag, uint16_t fmt) {
    if (strom_reset(i)) return -1;
    uint32_t o = SD(i);
    for (int c = 0; c < NCH; c++) {                                                        /* BDL: 8 Eintraege zu je 8 KiB */
        uint64_t a = (uint64_t)(uintptr_t)(ring + c * CHUNK); uint32_t* e = (uint32_t*)(bdl + c * 16);
        e[0] = (uint32_t)a; e[1] = (uint32_t)(a >> 32); e[2] = CHUNK; e[3] = 0;
    }
    fence();
    w8(o + 3, 0x1C);                                                                         /* Status loeschen */
    w8(o + 2, (uint8_t)(tag << 4));                                                          /* Strom-Nummer */
    w32(o + 8, CHUNK * NCH);
    w16(o + 0xC, NCH - 1);
    w16(o + 0x12, fmt);
    w32(o + 0x18, (uint32_t)(uintptr_t)bdl); w32(o + 0x1C, (uint32_t)((uint64_t)(uintptr_t)bdl >> 32));
    return 0;
}
static void strom_start(int i) { w8(SD(i), 0x02); }
static void strom_stop(int i) { w8(SD(i), 0); for (long t = 0; t < 100000 && (r8(SD(i)) & 2); t++) warte_us(1); }
static uint32_t lpib(int i) { return r32(SD(i) + 4); }

/* ---- Start ---- */
static int pfade_suchen(void) {
    int ok = 0;
    /* Ausgabe: Lautsprecher (1), Kopfhoerer (2), Line-Out (0) */
    static const int aus_art[3] = { 1, 2, 0 };
    aus_n = 0; aus_pin = -1; aus_dac = -1;
    for (int a = 0; a < 3 && !aus_n; a++)
        for (int nid = wstart; nid < wende && !aus_n; nid++) {
            struct widget* w = &W[nid]; if (!w->da || w->typ != 4) continue;
            if ((int)((w->cfg >> 20) & 0xF) != aus_art[a] || ((w->cfg >> 30) & 3) == 1) continue;
            if (!(param(nid, 0x0C) & (1u << 4))) continue;                                  /* Pin kann Ausgabe */
            int n = dfs_aus(nid, 0, aus_pf, aus_sl); if (n > 0) { aus_n = (uint8_t)n; aus_pin = nid; aus_dac = aus_pf[n - 1]; }
        }
    if (aus_n) ok |= 1;
    /* Eingabe: Mikrofon (0xA), dann Line-In (8) */
    static const int ein_art[2] = { 0xA, 8 };
    ein_n = 0; ein_pin = -1; ein_adc = -1;
    for (int a = 0; a < 2 && !ein_n; a++)
        for (int nid = wstart; nid < wende && !ein_n; nid++) {
            struct widget* w = &W[nid]; if (!w->da || w->typ != 1) continue;
            int n = dfs_ein(nid, 0, ein_art[a], ein_pf, ein_sl); if (n > 0) { ein_n = (uint8_t)n; ein_adc = nid; ein_pin = ein_pf[n - 1]; }
        }
    if (ein_n) ok |= 2;
    return ok;
}

int hda_init(void) {
    if (bereit) return 0;
    if (versucht) return -1;
    versucht = 1;
    struct pci_dev* d = 0;
    for (int i = 0, n = pci_count(); i < n; i++) { struct pci_dev* p = pci_get(i); if (p && p->cls == 4 && p->sub == 3) { d = p; break; } }
    if (!d) { kopiere_info("keine Audio-Hardware (HD Audio) gefunden"); kprint("[HDA] keine Audio-Hardware gefunden\n"); return -1; }
    pci_enable(d);
    if (d->vendor == 0x8086) { uint32_t t = pci_read32(d, 0x44); pci_write32(d, 0x44, t & ~7u); }   /* Intel: Verkehrsklasse 0 */
    uint64_t bar = pci_bar(d, 0);
    if (!bar || paging_map_mmio(bar, 0x4000) != 0) { kopiere_info("MMIO nicht einblendbar"); kprint("[HDA] MMIO-Mapping fehlgeschlagen\n"); return -2; }
    R = (volatile uint8_t*)(uintptr_t)bar;
    kprint("[HDA] Controller "); kprint_hex(d->vendor); kprint(":"); kprint_hex(d->device); kprint("\n");

    w32(GCTL, 0);                                                                           /* Zuruecksetzen */
    for (long t = 0; t < 100000 && (r32(GCTL) & 1); t++) warte_us(1);
    warte_us(200);
    w32(GCTL, 1);
    for (long t = 0; t < 100000 && !(r32(GCTL) & 1); t++) warte_us(1);
    if (!(r32(GCTL) & 1)) { kopiere_info("Controller startet nicht"); kprint("[HDA] Controller startet nicht\n"); return -3; }
    for (long t = 0; t < 100000 && !(r16(STATESTS) & 0x7FFF); t++) warte_us(1);             /* Codecs melden sich */
    uint16_t st = r16(STATESTS) & 0x7FFF;
    uint16_t gcap = r16(GCAP); n_iss = (gcap >> 8) & 0xF; n_oss = (gcap >> 12) & 0xF;
    w32(INTCTL, 0);
    if (!st) { kopiere_info("kein Codec"); kprint("[HDA] kein Codec gemeldet\n"); return -4; }

    uint8_t* seite = (uint8_t*)pmm_alloc_pages(2); ring = (uint8_t*)pmm_alloc_pages(NCH * CHUNK / 4096); bdl = (uint8_t*)pmm_alloc_page();
    if (!seite || !ring || !bdl) { kopiere_info("kein Speicher"); return -5; }
    nullen(seite, 8192); nullen(ring, NCH * CHUNK); nullen(bdl, 4096);
    /* CORB (bis 1 KiB) bei seite+0, RIRB (bis 2 KiB) bei seite+2048 */
    corb = (uint32_t*)seite; rirb = (volatile uint64_t*)(seite + 2048);
    w8(CORBCTL, 0); w8(RIRBCTL, 0);
    uint8_t cs = r8(CORBSIZE); if (cs & 0x40) { w8(CORBSIZE, (uint8_t)((cs & ~3) | 2)); corb_n = 256; } else if (cs & 0x20) { w8(CORBSIZE, (uint8_t)((cs & ~3) | 1)); corb_n = 16; } else { w8(CORBSIZE, (uint8_t)(cs & ~3)); corb_n = 2; }
    uint8_t rs = r8(RIRBSIZE); if (rs & 0x40) { w8(RIRBSIZE, (uint8_t)((rs & ~3) | 2)); rirb_n = 256; } else if (rs & 0x20) { w8(RIRBSIZE, (uint8_t)((rs & ~3) | 1)); rirb_n = 16; } else { w8(RIRBSIZE, (uint8_t)(rs & ~3)); rirb_n = 2; }
    w32(CORBLBASE, (uint32_t)(uintptr_t)corb); w32(CORBUBASE, (uint32_t)((uint64_t)(uintptr_t)corb >> 32));
    w32(RIRBLBASE, (uint32_t)(uintptr_t)rirb); w32(RIRBUBASE, (uint32_t)((uint64_t)(uintptr_t)rirb >> 32));
    w16(CORBRP, 0x8000); for (long t = 0; t < 10000 && !(r16(CORBRP) & 0x8000); t++) warte_us(1);
    w16(CORBRP, 0);      for (long t = 0; t < 10000 &&  (r16(CORBRP) & 0x8000); t++) warte_us(1);
    w16(CORBWP, 0);
    w16(RIRBWP, 0x8000); rirb_rp = 0;
    w16(RINTCNT, 1);
    w8(CORBCTL, 0x02); w8(RIRBCTL, 0x02);
    fence();

    for (int c = 0; c < 15 && afg < 0; c++) {                                              /* ersten Codec mit Audio-Funktionsgruppe nehmen */
        if (!(st & (1u << c))) continue;
        cad = c;
        uint32_t root = param(0, 0x04); int fs = (int)((root >> 16) & 0xFF), fn = (int)(root & 0xFF);
        for (int f = fs; f < fs + fn; f++) if ((param(f, 0x05) & 0xFF) == 1) { afg = f; break; }
    }
    if (afg < 0) { kopiere_info("Codec ohne Audio-Funktion"); kprint("[HDA] Codec ohne Audio-Funktion\n"); return -6; }
    uint32_t vid = param(0, 0x00);
    kprint("[HDA] Codec "); kprint_dec((uint32_t)cad); kprint(", Hersteller/Typ "); kprint_hex(vid); kprint(", Funktionsgruppe "); kprint_dec((uint32_t)afg); kprint("\n");
    verb(afg, 0x70500u, 0);                                                                  /* AFG: Strom D0 */
    warte_us(2000);
    widgets_lesen();
    int pf = pfade_suchen();
    s_ein = n_iss ? 0 : -1; s_aus = n_oss ? (int)n_iss : -1;
    if (!(pf & 1) || s_aus < 0) { aus_n = 0; }
    if (!(pf & 2) || s_ein < 0) { ein_n = 0; }
    if (aus_n) {
        pfad_an(aus_pf, aus_sl, aus_n, 1);
        verb(aus_dac, 0x70600u | (1u << 4), 0); verb(aus_dac, 0x20000u | 0x0011u, 0);          /* Strom 1, 48 kHz 16 Bit stereo */
        kprint("[HDA] Ausgabe: Pin "); kprint_dec((uint32_t)aus_pin); kprint(" -> Wandler "); kprint_dec((uint32_t)aus_dac); kprint("\n");
    }
    if (ein_n) {
        pfad_an(ein_pf, ein_sl, ein_n, 0);
        verb(ein_adc, 0x70600u | (2u << 4), 0); verb(ein_adc, 0x20000u | 0x0011u, 0);          /* Strom 2 */
        kprint("[HDA] Eingabe: Wandler "); kprint_dec((uint32_t)ein_adc); kprint(" <- Pin "); kprint_dec((uint32_t)ein_pin); kprint("\n");
    }
    if (!aus_n && !ein_n) { kopiere_info("Codec ohne nutzbaren Ein-/Ausgang"); kprint("[HDA] kein nutzbarer Ein-/Ausgang\n"); return -7; }
    bereit = 1;
    { char t[96]; int k = 0; const char* a = "HD Audio bereit:"; while (*a) t[k++] = *a++; if (aus_n) { a = " Ausgabe"; while (*a) t[k++] = *a++; } if (ein_n) { a = " Eingabe"; while (*a) t[k++] = *a++; } t[k] = 0; kopiere_info(t); }
    kprint("[HDA] bereit\n");
    return 0;
}
int hda_bereit(void) { return bereit; }
const char* hda_info(void) { return info; }
int hda_hat_ausgabe(void) { return bereit && aus_n; }
int hda_hat_eingabe(void) { return bereit && ein_n; }

/* ---- Ausgabe ---- */
int hda_spielen_strom(uint32_t (*fill)(int16_t* stereo, uint32_t frames, void* ctx), void* ctx) {
    if (hda_init() || !aus_n) return -1;
    const uint32_t frames_pro = CHUNK / 4;
    uint32_t daten_chunks = 0;                                                               /* Anzahl Stuecke, die echte Daten enthalten */
    int ende = 0;
    for (int c = 0; c < NCH; c++) {                                                          /* Ring vorfuellen */
        int16_t* p = (int16_t*)(ring + c * CHUNK); uint32_t got = ende ? 0 : fill(p, frames_pro, ctx);
        if (got > frames_pro) got = frames_pro;
        if (got == 0) ende = 1; else daten_chunks = (uint32_t)c + 1;
        for (uint32_t k = got * 2; k < frames_pro * 2; k++) p[k] = 0;
    }
    if (strom_vorbereiten(s_aus, 1, 0x0011)) return -2;
    fence();
    uint32_t gefuellt = NCH, gespielt = 0, letzte = 0, zeit0 = timer_get_ticks();
    strom_start(s_aus);
    int abbruch = 0;
    while (1) {
        uint32_t cur = lpib(s_aus) / CHUNK; if (cur >= NCH) cur = 0;
        if (cur != letzte) { gespielt += (cur + NCH - letzte) % NCH; letzte = cur; zeit0 = timer_get_ticks(); }
        while (gefuellt < gespielt + NCH) {                                                  /* Stuecke, die schon abgespielt sind, neu fuellen */
            int16_t* p = (int16_t*)(ring + (gefuellt % NCH) * CHUNK); uint32_t got = ende ? 0 : fill(p, frames_pro, ctx);
            if (got > frames_pro) got = frames_pro;
            if (got == 0) ende = 1; else daten_chunks = gefuellt + 1;
            for (uint32_t k = got * 2; k < frames_pro * 2; k++) p[k] = 0;
            gefuellt++; fence();
        }
        if (ende && gespielt >= daten_chunks) break;                                         /* alles abgespielt */
        if (timer_get_ticks() - zeit0 > 200) { abbruch = 1; break; }                         /* Position bewegt sich 2 s nicht: Abbruch */
        pause_();
    }
    strom_stop(s_aus);
    return abbruch ? -3 : 0;
}

/* ---- Aufnahme ---- */
int hda_aufnehmen(int16_t* mono, uint32_t max_frames) {
    if (hda_init() || !ein_n) return -1;
    if (strom_vorbereiten(s_ein, 2, 0x0011)) return -2;
    nullen(ring, NCH * CHUNK); fence();
    uint32_t fertig = 0, letzte = 0, got = 0, zeit0 = timer_get_ticks();
    strom_start(s_ein);
    while (got < max_frames) {
        uint32_t cur = lpib(s_ein) / CHUNK; if (cur >= NCH) cur = 0;
        if (cur != letzte) { zeit0 = timer_get_ticks(); }
        uint32_t neu = (cur + NCH - letzte) % NCH; letzte = cur;
        for (uint32_t n = 0; n < neu && got < max_frames; n++) {
            const int16_t* p = (const int16_t*)(ring + (fertig % NCH) * CHUNK);
            for (uint32_t k = 0; k < CHUNK / 4 && got < max_frames; k++) mono[got++] = (int16_t)(((int32_t)p[2 * k] + (int32_t)p[2 * k + 1]) / 2);
            fertig++;
        }
        if (timer_get_ticks() - zeit0 > 200) { strom_stop(s_ein); return got ? (int)got : -3; }
        pause_();
    }
    strom_stop(s_ein);
    return (int)got;
}

/* ---- Shell: audio ---- */
static int zahl(const char** s, int std) {
    while (**s == ' ') (*s)++;
    if (**s < '0' || **s > '9') return std;
    int v = 0; while (**s >= '0' && **s <= '9') { v = v * 10 + (**s - '0'); (*s)++; }
    return v;
}
/* Sinuston ohne Gleitkomma: Rekursion y[n] = 2cos(w) y[n-1] - y[n-2]; cos/sin per Taylor-Reihe in Q24 (w = 2 pi hz / 48000, Q24: hz * 2196) */
struct ton { int32_t y1, y2, c; int16_t amp; uint32_t rest; };
static void ton_start(struct ton* t, int hz, int ms) {
    int64_t w = (int64_t)hz * 2196, w2 = (w * w) >> 24, w3 = (w2 * w) >> 24, w4 = (w2 * w2) >> 24, w5 = (w4 * w) >> 24, w6 = (w4 * w2) >> 24, w7 = (w6 * w) >> 24;
    int64_t c = (1LL << 24) - w2 / 2 + w4 / 24 - w6 / 720, sn = w - w3 / 6 + w5 / 120 - w7 / 5040;
    t->c = (int32_t)(2 * c); t->y1 = (int32_t)(sn >> 4); t->y2 = 0; t->amp = 6000; t->rest = (uint32_t)ms * 48;     /* y in Q20 (1.0 = 1048576) */
}
static uint32_t ton_fill(int16_t* out, uint32_t frames, void* ctx) {
    struct ton* t = ctx; uint32_t n = frames < t->rest ? frames : t->rest;
    for (uint32_t i = 0; i < n; i++) {
        int32_t y = (int32_t)(((int64_t)t->c * t->y1) >> 24) - t->y2; t->y2 = t->y1; t->y1 = y;
        int32_t v = (int32_t)(((int64_t)y * t->amp) >> 20); if (v > 32767) v = 32767; if (v < -32768) v = -32768;
        out[2 * i] = (int16_t)v; out[2 * i + 1] = (int16_t)v;
    }
    t->rest -= n; return n;
}
static int isqrt(uint64_t v) { uint64_t r = 0, b = 1ull << 62; while (b > v) b >>= 2; while (b) { if (v >= r + b) { v -= r + b; r = (r >> 1) + b; } else r >>= 1; b >>= 2; } return (int)r; }
static int16_t aufn[48000 * 6];

void audio_befehl(const char* a) {
    while (*a == ' ') a++;
    if (!*a || (a[0] == 's' && a[1] == 't')) {
        hda_init();
        kprint("Audio: "); kprint(info); kprint("\n");
        if (hda_bereit()) { kprint("  Ausgabe: "); kprint(aus_n ? "ja" : "nein"); kprint(", Eingabe: "); kprint(ein_n ? "ja" : "nein"); kprint(", Streams ein/aus: "); kprint_dec(n_iss); kprint("/"); kprint_dec(n_oss); kprint("\n"); }
        kprint("  Befehle: audio status | ton [Hz] [ms] | aufnahme [ms]\n");
        return;
    }
    if (a[0] == 't') {                                                                       /* ton */
        while (*a && *a != ' ') a++;
        int hz = zahl(&a, 440), ms = zahl(&a, 700);
        if (hz < 50) hz = 50; if (hz > 8000) hz = 8000; if (ms < 50) ms = 50; if (ms > 5000) ms = 5000;
        if (hda_init() || !hda_hat_ausgabe()) { kprint("Keine Audio-Ausgabe verfuegbar ("); kprint(info); kprint(")\n"); return; }
        struct ton t; ton_start(&t, hz, ms);
        int r = hda_spielen_strom(ton_fill, &t);
        if (r == 0) { kprint("Ton gespielt: "); kprint_dec((uint32_t)hz); kprint(" Hz, "); kprint_dec((uint32_t)ms); kprint(" ms\n"); } else { kprint("Wiedergabe fehlgeschlagen (Code "); kprint_dec((uint32_t)(-r)); kprint(")\n"); }
        return;
    }
    if (a[0] == 'a') {                                                                       /* aufnahme */
        while (*a && *a != ' ') a++;
        int ms = zahl(&a, 1000); if (ms < 100) ms = 100; if (ms > 5000) ms = 5000;
        if (hda_init() || !hda_hat_eingabe()) { kprint("Kein Mikrofon verfuegbar ("); kprint(info); kprint(")\n"); return; }
        int n = hda_aufnehmen(aufn, (uint32_t)ms * 48);
        if (n <= 0) { kprint("Aufnahme fehlgeschlagen (Code "); kprint_dec((uint32_t)(-n)); kprint(")\n"); return; }
        int spitze = 0; uint64_t q = 0; int nd = 0;
        for (int i = 0; i < n; i++) { int v = aufn[i]; int av = v < 0 ? -v : v; if (av > spitze) spitze = av; q += (uint64_t)((int64_t)v * v); if (i && (aufn[i - 1] < 0) != (v < 0)) nd++; }
        int rms = isqrt(q / (uint64_t)n);
        kprint("Aufnahme: "); kprint_dec((uint32_t)n); kprint(" Werte ("); kprint_dec((uint32_t)(n / 48)); kprint(" ms), Spitze "); kprint_dec((uint32_t)spitze); kprint(", Effektivwert "); kprint_dec((uint32_t)rms);
        kprint(", Nulldurchgaenge "); kprint_dec((uint32_t)nd); kprint(", etwa "); kprint_dec((uint32_t)((uint64_t)nd * 48000 / 2 / (uint64_t)n)); kprint(" Hz\n");
        return;
    }
    kprint("audio: status | ton [Hz] [ms] | aufnahme [ms]\n");
}
