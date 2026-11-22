/* Zynex Lernkern, siehe include/zlk.h. Ganzzahlig, keine libc. */
#include "zlk.h"
#include "synfs.h"
#include "qubit.h"
#include "nn.h"

extern void kprint(const char* str);
extern void kprint_dec(uint32_t val);

#define NIN 12                     /* Eingaenge: Ordnung 0,1,2,3,4,6, Wort, Wort+Vorwort, Ordnung 8, 16, neuronales Netz, Konstante */
#define NCTX 10                     /* Gedaechtnisse (ohne Konstante) */
#define BITS 18
#define SLOTS (1u << BITS)
#define LOGMAX 65536
#define NSET (12 * 8)

struct slot { uint16_t p; uint8_t n, chk; };

static struct slot T[NCTX][SLOTS];
static int32_t W[NSET][NIN];
static uint32_t wset;
static uint16_t RECIP[256];
static int16_t STRETCH[4096];
static int ready_tab;

static uint32_t h[NCTX];           /* Kontext-Hashes am Bytegrenze */
static uint32_t c0;                /* bisher gesehene Bits des aktuellen Bytes (mit fuehrender 1) */
static uint64_t hist, hist2;       /* letzte 8 Bytes, davor weitere 8 */
static uint32_t wh, pwh;           /* aktuelles Wort / Vorwort */
static uint32_t idx[NCTX]; static uint8_t chkv[NCTX];
static int32_t in[NIN]; static int pr;
static int ng_learn = 1;           /* 0 = nur Zustand weiterschieben (Erzeugen), kein Lernen */
static uint32_t nlern;
static int kost;                   /* Bits x 100 je Zeichen, letzte Eingabe */
static char logbuf[LOGMAX]; static int loglen;
static uint32_t rnd = 2463534242u;

static int squash(int d) {
    static const int t[33] = {1,2,3,6,10,16,27,45,73,120,194,310,488,747,1101,1546,2047,2549,2994,3348,3607,3785,3901,3975,4022,4050,4068,4079,4085,4089,4092,4093,4094};
    if (d > 2047) return 4095; if (d < -2047) return 0;
    int w = d & 127; d = (d >> 7) + 16;
    return (t[d] * (128 - w) + t[d + 1] * w + 64) >> 7;
}
static void tabellen(void) {
    int pi = 0;
    for (int x = -2047; x <= 2047; x++) { int v = squash(x); for (int i = pi; i <= v; i++) STRETCH[i] = (int16_t)x; pi = v + 1; }
    for (int i = pi; i < 4096; i++) STRETCH[i] = 2047;
    for (int n = 0; n < 256; n++) RECIP[n] = (uint16_t)(65536 * 2 / (2 * n + 3));
    ready_tab = 1;
}
static uint32_t hm(uint64_t v, uint32_t salt) { v = (v + salt) * 0x9E3779B97F4A7C15ull; v ^= v >> 29; v *= 0xBF58476D1CE4E5B9ull; return (uint32_t)(v >> 32); }

static void bytegrenze(void) {
    h[0] = 0;
    h[1] = hm(hist & 0xFF, 1);
    h[2] = hm(hist & 0xFFFF, 2);
    h[3] = hm(hist & 0xFFFFFF, 3);
    h[4] = hm(hist & 0xFFFFFFFFull, 4);
    h[5] = hm(hist & 0xFFFFFFFFFFFFull, 6);
    h[6] = hm(wh, 7);
    h[7] = hm(((uint64_t)wh << 32) | pwh, 8);
    h[8] = hm(hist, 9);
    h[9] = hm(hist ^ hm(hist2, 11) * 0x9E3779B97F4A7C15ull, 10);
    nn_vorbereiten(hist, hist2);                                   /* das Netz sagt das ganze naechste Byte voraus */
}

void zlk_init(void) {                                  /* nur beim Start und beim Laden; fuer Anwender gibt es keinen Reset */
    if (!ready_tab) tabellen();
    for (int i = 0; i < NCTX; i++) for (uint32_t j = 0; j < SLOTS; j++) { T[i][j].p = 32768; T[i][j].n = 0; T[i][j].chk = 0; }
    for (int s = 0; s < NSET; s++) for (int i = 0; i < NIN; i++) W[s][i] = (i == NCTX) ? 6000 : 20000;
    hist = hist2 = 0; wh = pwh = 0; c0 = 1; nlern = 0; kost = 800; loglen = 0; ng_learn = 1; rnd = 2463534242u;
    nn_init();
    bytegrenze();
}

static int vorhersage(void) {
    for (int i = 0; i < NCTX; i++) {
        uint32_t x = (h[i] ^ (c0 * 0x9E3779B1u)) * 0x85EBCA6Bu; x ^= x >> 15; x *= 0xC2B2AE35u;
        idx[i] = x >> (32 - BITS); chkv[i] = (uint8_t)(x & 0xFF);
        struct slot* s = &T[i][idx[i]];
        if (s->chk != chkv[i]) { if (ng_learn) { s->p = 32768; s->n = 0; s->chk = chkv[i]; } in[i] = 0; continue; }
        in[i] = STRETCH[s->p >> 4];
    }
    { int pn = nn_bitprob((int)c0); in[NCTX] = STRETCH[pn]; }
    in[NCTX + 1] = 256;
    { int match = 0;                                                   /* wie viele Zeichen-/Wort-Kontexte kennen die Lage schon? */
      for (int k = 1; k < NCTX; k++) { struct slot* t = &T[k][idx[k]]; if (t->chk == chkv[k] && t->n > 0) match++; }
      if (match > 11) match = 11; int bp = 0; while ((c0 >> (bp + 1)) != 0) bp++;
      wset = (uint32_t)(match * 8 + bp); }
    int64_t dot = 0; for (int i = 0; i < NIN; i++) dot += (int64_t)in[i] * W[wset][i];
    pr = squash((int)(dot >> 16));
    if (pr < 1) pr = 1; if (pr > 4094) pr = 4094;
    return pr;
}

static void bit_lernen(int y) {
    int err = ((y << 12) - pr) * 7;
    for (int i = 0; i < NIN; i++) W[wset][i] += (in[i] * err + 0x8000) >> 16;
    for (int i = 0; i < NCTX; i++) {
        struct slot* s = &T[i][idx[i]];
        if (s->chk != chkv[i]) { s->p = 32768; s->n = 0; s->chk = chkv[i]; }
        int tgt = y ? 65535 : 0;
        s->p = (uint16_t)(s->p + (((tgt - (int)s->p) * (int)RECIP[s->n]) >> 16));
        if (s->n < (i >= 5 ? 60 : 30)) s->n++;
    }
}

static void bit_weiter(int y) {
    c0 = (c0 << 1) | (uint32_t)y;
    if (c0 >= 256) {
        uint8_t c = (uint8_t)(c0 & 0xFF);
        hist2 = (hist2 << 8) | (hist >> 56); hist = (hist << 8) | c;
        int buchst = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c >= 128 || (c >= '0' && c <= '9');
        if (buchst) { char l = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : (char)c; wh = (wh + (uint32_t)(uint8_t)l + 1) * 0x2F0B4A27u; }
        else if (wh) { pwh = wh; wh = 0; }
        c0 = 1; bytegrenze();
    }
}

static uint32_t bitkosten(int p, int y) {      /* -log2(Wahrscheinlichkeit des gesehenen Bits) x100 */
    uint32_t q = (uint32_t)(y ? p : 4096 - p) * 16; int fl = 0; while ((q >> (fl + 1)) != 0) fl++;
    int l2 = fl * 100 + (int)((q * 100u) >> fl) - 100; return (uint32_t)(1600 - l2);
}
static int sicher_bits = 800;

void zlk_lerne(const char* text, int len) {
    if (!ready_tab) zlk_init();
    if (len < 0) { len = 0; while (text[len]) len++; }
    if (!len) return;
    uint32_t cost = 0; ng_learn = 1;
    for (int k = 0; k < len; k++) {
        uint8_t c = (uint8_t)text[k];
        nn_lerne(c, 60);                                    /* Netz: ein Lernschritt (Verteilung fuer diesen Kontext liegt schon bereit) */
        for (int b = 7; b >= 0; b--) {
            int y = (c >> b) & 1, p = vorhersage();
            cost += bitkosten(p, y);
            bit_lernen(y); bit_weiter(y);
        }
        nlern++;
        if (loglen >= LOGMAX) {                                  /* Protokoll voll: aeltere Haelfte (bis zum Zeilenende) verwerfen */
            int cut = LOGMAX / 2; while (cut < loglen && logbuf[cut] != '\n') cut++; if (cut < loglen) cut++;
            for (int i = cut; i < loglen; i++) logbuf[i - cut] = logbuf[i]; loglen -= cut;
        }
        logbuf[loglen++] = (char)c;
    }
    kost = (int)(cost / (uint32_t)len);
}

/* Zufall aus dem 4-Qubit-Register: alle vier Qubits per Hadamard in gleichmaessige Ueberlagerung, dann messen (4 Bit je Messung).
 * Die Wahrscheinlichkeiten laufen im Qubit-Festkomma Q16.16 (Q_EINS = 65536). Dies ist eine klassische Simulation (16 Amplituden). */
static uint32_t qzufall16(void) {
    uint32_t v = 0;
    for (int k = 0; k < 4; k++) {
        qubit_reset(4);
        for (int q = 0; q < 4; q++) qubit_gate('h', q);
        int m = qubit_messen_alle(); if (m < 0) m = (int)(rnd = rnd * 1664525u + 1013904223u) >> 8;
        v = (v << 4) | (uint32_t)(m & 15);
    }
    return v & 0xFFFF;
}
static int squash16(int d) {                     /* wie squash(), aber mit 16 Bit Aufloesung (Q16) statt 12 */
    static const int t[33] = {1,2,3,6,10,16,27,45,73,120,194,310,488,747,1101,1546,2047,2549,2994,3348,3607,3785,3901,3975,4022,4050,4068,4079,4085,4089,4092,4093,4094};
    if (d > 2047) return 65535; if (d < -2047) return 1;
    int w = d & 127; d = (d >> 7) + 16;
    int v = (t[d] * (128 - w) + t[d + 1] * w) >> 3; return v < 1 ? 1 : v > 65535 ? 65535 : v;
}

int zlk_ergaenze(const char* anfang, char* out, int max, int temp) {
    if (!ready_tab) zlk_init();
    /* Zustand sichern: Erzeugen darf das Gelernte nicht veraendern, nur den Kontext */
    uint64_t sh = hist, sh2 = hist2; uint32_t swh = wh, spw = pwh; uint32_t sc0 = c0; uint32_t sh_[NCTX]; for (int i = 0; i < NCTX; i++) sh_[i] = h[i];
    hist = hist2 = 0; wh = pwh = 0; c0 = 1; bytegrenze();
    ng_learn = 0;
    for (int k = 0; anfang[k]; k++) { uint8_t c = (uint8_t)anfang[k]; for (int b = 7; b >= 0; b--) { vorhersage(); bit_weiter((c >> b) & 1); } }
    int n = 0; uint32_t ksum = 0, kn = 0;
    while (n < max - 1) {
        int c = 0;
        for (int b = 0; b < 8; b++) {
            int p = vorhersage(), y, praw = p;
            if (temp <= 0) y = p >= 2048;
            else { int st = STRETCH[p] * 100 / temp; y = (int)qzufall16() < squash16(st); }
            ksum += bitkosten(praw, y); c = (c << 1) | y; bit_weiter(y);
        }
        kn++;
        if (c == 0 || c == '\n') break;
        out[n++] = (char)c;
        if (n >= 3 && temp <= 0) { int rep = 1; for (int i = 1; i <= 2; i++) if (out[n - 1] != out[n - 1 - i]) rep = 0; if (rep) break; }
    }
    out[n] = 0; sicher_bits = kn ? (int)(ksum / kn) : 800;
    ng_learn = 1; hist = sh; hist2 = sh2; wh = swh; pwh = spw; c0 = sc0; for (int i = 0; i < NCTX; i++) h[i] = sh_[i]; nn_vorbereiten(hist, hist2);
    return n;
}

int zlk_pruefe(const char* text) {                 /* mittlere Bits x100 je Zeichen, ohne zu lernen und ohne den Kontext zu stoeren */
    if (!ready_tab) zlk_init();
    uint64_t sh = hist, sh2 = hist2; uint32_t swh = wh, spw = pwh, sc0 = c0; uint32_t sh_[NCTX]; for (int i = 0; i < NCTX; i++) sh_[i] = h[i];
    hist = hist2 = 0; wh = pwh = 0; c0 = 1; bytegrenze(); ng_learn = 0;
    uint32_t cost = 0, n = 0;
    for (int k = 0; text[k]; k++) { uint8_t c = (uint8_t)text[k]; for (int b = 7; b >= 0; b--) { int y = (c >> b) & 1, p = vorhersage(); cost += bitkosten(p, y); bit_weiter(y); } n++; }
    ng_learn = 1; hist = sh; hist2 = sh2; wh = swh; pwh = spw; c0 = sc0; for (int i = 0; i < NCTX; i++) h[i] = sh_[i]; nn_vorbereiten(hist, hist2);
    return n ? (int)(cost / n) : 800;
}
uint32_t zlk_gelernt(void) { return nlern; }
int zlk_kosten(void) { return kost; }
int zlk_sicherheit(void) { return sicher_bits; }

/* ---------- .zyki: das trainierte Modell als Datei ----------
 * Aufbau: "ZYKI" | Version (1) | 3 Byte 0 | Laenge u32 | FNV-1a der Nutzlast u32 | Nutzlast = Lernprotokoll.
 * Das Modell entsteht beim Laden durch erneutes Einspielen des Protokolls; deshalb laesst sich eine .zyki-Datei von mehreren Clients
 * zusammenfuehren (der Lernkern lernt dann auch das, was andere gelernt haben). */
static uint32_t fnv32(const uint8_t* p, int n) { uint32_t x = 2166136261u; for (int i = 0; i < n; i++) { x ^= p[i]; x *= 16777619u; } return x; }
static int16_t nnbuf[NN_PARAMS];
#define ZYKI_MAX ZLK_ZYKI_MAX
int zlk_zyki_bauen(uint8_t* out, int max) {
    if (max < ZYKI_MAX) return -1;
    out[0] = 'Z'; out[1] = 'Y'; out[2] = 'K'; out[3] = 'I'; out[4] = 2; out[5] = out[6] = out[7] = 0;
    uint32_t l = (uint32_t)loglen, c = fnv32((const uint8_t*)logbuf, loglen);
    for (int i = 0; i < 4; i++) { out[8 + i] = (uint8_t)(l >> (8 * i)); out[12 + i] = (uint8_t)(c >> (8 * i)); }
    for (int i = 0; i < loglen; i++) out[16 + i] = (uint8_t)logbuf[i];
    int o = 16 + loglen;
    int n = nn_exportieren(nnbuf, NN_PARAMS); if (n != NN_PARAMS) return o;      /* ohne Netz: wie Version 1 lesbar */
    out[4] = 2;
    uint8_t* w = out + o + 4; for (int i = 0; i < n; i++) { w[2 * i] = (uint8_t)(nnbuf[i] & 255); w[2 * i + 1] = (uint8_t)((nnbuf[i] >> 8) & 255); }
    uint32_t cn = fnv32(w, 2 * n);
    for (int i = 0; i < 4; i++) { out[o + i] = (uint8_t)((uint32_t)n >> (8 * i)); out[o + 4 + 2 * n + i] = (uint8_t)(cn >> (8 * i)); }
    return o + 4 + 2 * n + 4;
}
int zlk_zyki_einlesen(const uint8_t* in, int len, int zusammenfuehren) {
    if (len < 16 || in[0] != 'Z' || in[1] != 'Y' || in[2] != 'K' || in[3] != 'I' || (in[4] != 1 && in[4] != 2)) return -1;
    uint32_t l = 0, c = 0; for (int i = 0; i < 4; i++) { l |= (uint32_t)in[8 + i] << (8 * i); c |= (uint32_t)in[12 + i] << (8 * i); }
    if (l > LOGMAX || (int)l + 16 > len || fnv32(in + 16, (int)l) != c) return -2;
    int hat_netz = 0; int o = 16 + (int)l;
    if (in[4] == 2 && !zusammenfuehren && len >= o + 8) {                                  /* Netz-Abschnitt pruefen */
        uint32_t n = 0, cn = 0; for (int i = 0; i < 4; i++) n |= (uint32_t)in[o + i] << (8 * i);
        if (n == NN_PARAMS && len >= o + 8 + 2 * (int)n) {
            for (int i = 0; i < 4; i++) cn |= (uint32_t)in[o + 4 + 2 * n + i] << (8 * i);
            if (fnv32(in + o + 4, (int)(2 * n)) == cn) hat_netz = 1;
        }
    }
    if (!zusammenfuehren) zlk_init();
    if (hat_netz) nn_training(0);                                                         /* Gewichte kommen aus der Datei: nicht noch einmal trainieren */
    zlk_lerne((const char*)in + 16, (int)l);
    if (hat_netz) {
        nn_training(1);
        for (int i = 0; i < NN_PARAMS; i++) nnbuf[i] = (int16_t)((uint16_t)in[o + 4 + 2 * i] | ((uint16_t)in[o + 4 + 2 * i + 1] << 8));
        nn_importieren(nnbuf, NN_PARAMS); nn_vorbereiten(hist, hist2);
    }
    return (int)l;
}
static uint8_t zybuf[ZYKI_MAX];
int zlk_speichern(void) {
    if (!synfs_is_mounted()) return -1;
    int n = zlk_zyki_bauen(zybuf, sizeof(zybuf)); if (n < 0) return -2;
    synfs_create("modell.zyki", 0);
    return synfs_write("modell.zyki", zybuf, (uint32_t)n) < 0 ? -3 : 0;
}
void zlk_laden(void) {
    if (!synfs_is_mounted()) return;
    int n = synfs_read("modell.zyki", zybuf, sizeof(zybuf));
    if (n > 0) zlk_zyki_einlesen(zybuf, n, 0);
}
void zlk_loeschen(void) {                          /* nur fuer Administratoren (Shell prueft das): Modell und Datei entfernen */
    zlk_init(); if (synfs_is_mounted()) synfs_delete("modell.zyki");
}
void zlk_info(void) {
    kprint("[ZLK] Lernkern: "); kprint_dec(nlern); kprint(" Zeichen gelernt, mittlere Kosten letzte Eingabe: ");
    kprint_dec((uint32_t)kost / 100); kprint(" Bit/Zeichen (8 = nichts gelernt); Netz-Verlust: "); kprint_dec((uint32_t)nn_verlust() / 100); kprint(" Bit/Zeichen; Zufall: 4-Qubit-Register (Simulation), Modell: modell.zyki\n");
}
