/* Zynex Lernkern, siehe include/zlk.h. Ganzzahlig, keine libc. */
#include "zlk.h"
#include "synfs.h"

extern void kprint(const char* str);
extern void kprint_dec(uint32_t val);

#define NIN 9                      /* Eingaenge: Ordnung 0,1,2,3,4,6, Wort, Wort+Vorwort, Konstante */
#define NCTX 8                     /* Gedaechtnisse (ohne Konstante) */
#define BITS 16
#define SLOTS (1u << BITS)
#define LOGMAX 8192

struct slot { uint16_t p; uint8_t n, chk; };

static struct slot T[NCTX][SLOTS];
static int32_t W[256][NIN];
static uint16_t RECIP[256];
static int16_t STRETCH[4096];
static int ready_tab;

static uint32_t h[NCTX];           /* Kontext-Hashes am Bytegrenze */
static uint32_t c0;                /* bisher gesehene Bits des aktuellen Bytes (mit fuehrender 1) */
static uint64_t hist;              /* letzte 8 Bytes */
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
}

void zlk_init(void) {
    if (!ready_tab) tabellen();
    for (int i = 0; i < NCTX; i++) for (uint32_t j = 0; j < SLOTS; j++) { T[i][j].p = 32768; T[i][j].n = 0; T[i][j].chk = 0; }
    for (int s = 0; s < 256; s++) for (int i = 0; i < NIN; i++) W[s][i] = 20000;
    hist = 0; wh = pwh = 0; c0 = 1; nlern = 0; kost = 800; loglen = 0; ng_learn = 1; rnd = 2463534242u;
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
    in[NCTX] = 256;
    int64_t dot = 0; for (int i = 0; i < NIN; i++) dot += (int64_t)in[i] * W[c0][i];
    pr = squash((int)(dot >> 16));
    if (pr < 1) pr = 1; if (pr > 4094) pr = 4094;
    return pr;
}

static void bit_lernen(int y) {
    int err = ((y << 12) - pr) * 7;
    for (int i = 0; i < NIN; i++) W[c0][i] += (in[i] * err + 0x8000) >> 16;
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
        hist = (hist << 8) | c;
        int buchst = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c >= 128 || (c >= '0' && c <= '9');
        if (buchst) { char l = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : (char)c; wh = (wh + (uint32_t)(uint8_t)l + 1) * 0x2F0B4A27u; }
        else if (wh) { pwh = wh; wh = 0; }
        c0 = 1; bytegrenze();
    }
}

void zlk_lerne(const char* text, int len) {
    if (!ready_tab) zlk_init();
    if (len < 0) { len = 0; while (text[len]) len++; }
    if (!len) return;
    uint32_t cost = 0; ng_learn = 1;
    for (int k = 0; k < len; k++) {
        uint8_t c = (uint8_t)text[k];
        for (int b = 7; b >= 0; b--) {
            int y = (c >> b) & 1, p = vorhersage();
            uint32_t q = (uint32_t)(y ? p : 4096 - p) * 16; int fl = 0; while ((q >> (fl + 1)) != 0) fl++;   /* -log2(q/65536) x100, linear in der Mantisse */
            int l2 = fl * 100 + (int)((q * 100u) >> fl) - 100; cost += (uint32_t)(1600 - l2);
            bit_lernen(y); bit_weiter(y);
        }
        nlern++;
        if (loglen < LOGMAX) logbuf[loglen++] = (char)c;
    }
    kost = (int)(cost / (uint32_t)len);
}

static uint32_t zufall(void) { rnd ^= rnd << 13; rnd ^= rnd >> 17; rnd ^= rnd << 5; return rnd; }

int zlk_ergaenze(const char* anfang, char* out, int max, int temp) {
    if (!ready_tab) zlk_init();
    /* Zustand sichern: Erzeugen darf das Gelernte nicht veraendern, nur den Kontext */
    uint64_t sh = hist; uint32_t swh = wh, spw = pwh; uint32_t sc0 = c0; uint32_t sh_[NCTX]; for (int i = 0; i < NCTX; i++) sh_[i] = h[i];
    hist = 0; wh = pwh = 0; c0 = 1; bytegrenze();
    ng_learn = 0;
    for (int k = 0; anfang[k]; k++) { uint8_t c = (uint8_t)anfang[k]; for (int b = 7; b >= 0; b--) { vorhersage(); bit_weiter((c >> b) & 1); } }
    int n = 0;
    while (n < max - 1) {
        int c = 0;
        for (int b = 0; b < 8; b++) {
            int p = vorhersage(), y;
            if (temp <= 0) y = p >= 2048;
            else { int st = STRETCH[p] * 100 / temp; p = squash(st); y = (int)(zufall() % 4096) < p; }
            c = (c << 1) | y; bit_weiter(y);
        }
        if (c == 0 || c == '\n') break;
        out[n++] = (char)c;
        if (n >= 3 && temp <= 0) { int rep = 1; for (int i = 1; i <= 2; i++) if (out[n - 1] != out[n - 1 - i]) rep = 0; if (rep) break; }
    }
    out[n] = 0;
    ng_learn = 1; hist = sh; wh = swh; pwh = spw; c0 = sc0; for (int i = 0; i < NCTX; i++) h[i] = sh_[i];
    return n;
}

uint32_t zlk_gelernt(void) { return nlern; }
int zlk_kosten(void) { return kost; }

void zlk_speichern(void) {
    if (!synfs_is_mounted() || !loglen) return;
    synfs_create("zlk.log", 0);
    synfs_write("zlk.log", logbuf, (uint32_t)loglen);
}
void zlk_laden(void) {
    if (!synfs_is_mounted()) return;
    static char buf[LOGMAX];
    int n = synfs_read("zlk.log", buf, LOGMAX);
    if (n > 0) { zlk_init(); zlk_lerne(buf, n); }
}
void zlk_info(void) {
    kprint("[ZLK] Lernkern: "); kprint_dec(nlern); kprint(" Zeichen gelernt, mittlere Kosten letzte Eingabe: ");
    kprint_dec((uint32_t)kost / 100); kprint(" Bit/Zeichen (8 = nichts gelernt)\n");
}
