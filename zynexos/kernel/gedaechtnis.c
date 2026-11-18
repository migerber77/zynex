/* Synapsen-Gedaechtnis, siehe include/gedaechtnis.h */
#include "gedaechtnis.h"
#include "zyndb.h"

extern void kprint(const char* str);
extern void kprint_dec(uint32_t val);

#define MAXN 160            /* Neuronen */
#define MAXS 600            /* Synapsen */
#define MAXG 40             /* Erinnerungen */
#define WL   13             /* Wortstamm: 12 Zeichen + 0 */
#define GL   112            /* Text einer Erinnerung */
#define NK   6              /* Neuronen je Erinnerung */
#define ZYKLUS 10           /* alle 10 Gespraeche: verblassen + Wiederholung */
#define WG   3              /* Arbeitsgedaechtnis */

struct neuron { char w[WL]; uint16_t nutz, zuletzt; };
struct synapse { uint16_t a, b; uint8_t w; };                                  /* a < b, Staerke 1..255 */
struct erinn { char t[GL]; uint8_t art, staerke, nn; uint16_t n[NK]; uint16_t zeit, zuletzt; };   /* art: 0 Gespraech, 1 persoenlich, 2 wichtig (merk dir) */

static struct neuron N[MAXN]; static int nN;
static struct synapse S[MAXS]; static int nS;
static struct erinn G[MAXG];                                                   /* frei = t[0] == 0 */
static int16_t akt[MAXN];                                                      /* Erregung 0..1000 (fluechtig) */
static uint32_t takt; static int geladen;
static char wq[WG][64], wa[WG][72]; static int nwg;                           /* Arbeitsgedaechtnis (nur im RAM) */

static int slen(const char* s) { int n = 0; while (s[n]) n++; return n; }
static int gl(const char* a, const char* b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }
static void kop(char* d, const char* s, int max) { int i = 0; while (s[i] && i < max - 1) { d[i] = s[i]; i++; } d[i] = 0; }
static int enthaelt(const char* h, const char* n) { int nl = slen(n); if (!nl) return 1; for (int i = 0; h[i]; i++) { int k = 0; while (k < nl && h[i + k] == n[k]) k++; if (k == nl) return 1; } return 0; }
static int minv(int a, int b) { return a < b ? a : b; }

/* ---------- Text zerlegen ---------- */
static void falte(const char* t, char* o, int max) {                          /* klein, ASCII; ae/oe/ue/ss fuer Umlaute, Akzente auf den Grundbuchstaben */
    int l = 0;
    for (int i = 0; t[i] && l < max - 3; i++) {
        unsigned char c = (unsigned char)t[i];
        if (c < 0x80) { o[l++] = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : (char)c; }
        else if (c == 0xC3 && t[i + 1]) {
            unsigned char d = (unsigned char)t[++i]; const char* r = "";
            if (d == 0xA4 || d == 0x84) r = "ae"; else if (d == 0xB6 || d == 0x96) r = "oe"; else if (d == 0xBC || d == 0x9C) r = "ue"; else if (d == 0x9F) r = "ss";
            else if ((d >= 0xA0 && d <= 0xA5) || (d >= 0x80 && d <= 0x85)) r = "a"; else if (d == 0xA7 || d == 0x87) r = "c";
            else if ((d >= 0xA8 && d <= 0xAB) || (d >= 0x88 && d <= 0x8B)) r = "e"; else if ((d >= 0xAC && d <= 0xAF) || (d >= 0x8C && d <= 0x8F)) r = "i";
            else if (d == 0xB1 || d == 0x91) r = "n"; else if ((d >= 0xB2 && d <= 0xB5) || (d >= 0x92 && d <= 0x95)) r = "o"; else if ((d >= 0xB9 && d <= 0xBB) || (d >= 0x99 && d <= 0x9B)) r = "u";
            while (*r) o[l++] = *r++;
        } else o[l++] = ' ';                                                   /* andere Zeichen trennen Woerter */
    }
    o[l] = 0;
}
static const char* const STOP[] = {
    "der","die","das","den","dem","des","ein","eine","einen","einem","einer","und","oder","aber","nicht","ist","sind","war","bin","bist","sein","habe","hat","haben","auch","noch","nur","mit","von",
    "fuer","auf","aus","bei","nach","ueber","vor","wie","was","wer","wo","wann","warum","ich","du","er","sie","es","wir","ihr","mich","mir","dich","dir","mein","dein","ihre","sich","als","wenn","dann",
    "im","am","zum","zur","schon","sehr","kann","koennen","will","wollen","soll","muss","mal","bitte","doch","ja","nein","kannst","bitte","mein","meine","meinen","meinem","meiner","dass","denn","weil",
    "the","an","and","or","but","not","are","was","been","have","has","had","with","from","for","on","in","at","to","of","that","this","these","it","its","you","your","me","my","we","our","they","them",
    "what","who","where","when","why","how","do","does","did","can","could","will","would","should","please","yes","no","also","just","very","there","here","about",
    "le","la","les","un","une","des","et","ou","mais","pas","est","sont","de","du","que","qui","dans","pour","sur","avec","je","tu","il","elle","nous","vous","ils","mon","ma","mes","ton","ta","tes","son","sa","ses","ce","cet","cette",
    "lo","gli","le","una","non","sono","di","del","che","chi","con","per","su","io","lui","lei","noi","voi","mio","mia","il","un",
    "el","los","las","uno","unos","pero","es","son","con","por","para","yo","ella","mi","mis","su","sus","quien","como","muy",
    0 };
static int stoppwort(const char* w) { for (int i = 0; STOP[i]; i++) if (gl(STOP[i], w)) return 1; return 0; }
static const char* const ENDG[] = { "ungen","ung","keit","heit","lich","ern","est","ing","en","er","es","em","ed","ly","e","n","s",0 };
static void stamm(const char* w, char* o) {
    char b[40]; kop(b, w, 40); int l = slen(b);
    for (int i = 0; ENDG[i]; i++) { int el = slen(ENDG[i]); if (l - el >= 4 && gl(b + l - el, ENDG[i])) { b[l - el] = 0; l -= el; break; } }
    if (l > WL - 1) { l = WL - 1; b[l] = 0; }
    kop(o, b, WL);
}
static int zerlege(const char* text, char st[][WL], int max) {
    char f[400]; falte(text, f, sizeof(f)); int n = 0;
    for (int i = 0; f[i] && n < max; ) {
        while (f[i] && !((f[i] >= 'a' && f[i] <= 'z') || (f[i] >= '0' && f[i] <= '9'))) i++;
        char w[40]; int l = 0; int ziffer = 0;
        while (f[i] && ((f[i] >= 'a' && f[i] <= 'z') || (f[i] >= '0' && f[i] <= '9'))) { if (f[i] <= '9') ziffer = 1; if (l < 39) w[l++] = f[i]; i++; }
        w[l] = 0;
        if (l < (ziffer ? 2 : 3) || stoppwort(w)) continue;
        char s[WL]; stamm(w, s); int da = 0; for (int k = 0; k < n; k++) if (gl(st[k], s)) da = 1;
        if (!da) kop(st[n++], s, WL);
    }
    return n;
}
int ged_heikel(const char* text) {                                          /* Geheimnisse merkt sich das Gedaechtnis nie */
    char f[400]; falte(text, f, sizeof(f));
    static const char* const H[] = { "passw", "kennwort", "passphrase", "schluessel", "secret", "geheimzahl", "token", "apikey", "api key", "iban", "kreditkarte", "credit card", "private key", 0 };
    for (int i = 0; H[i]; i++) if (enthaelt(f, H[i])) return 1;
    for (int i = 0; f[i]; i++) if (f[i] == ' ' && f[i + 1] == 'p' && f[i + 2] == 'i' && f[i + 3] == 'n' && (f[i + 4] == ' ' || !f[i + 4])) return 1;
    return 0;
}

/* ---------- Neuronen und Synapsen ---------- */
static int n_finde(const char* w) { for (int i = 0; i < nN; i++) if (gl(N[i].w, w)) return i; return -1; }
static void n_entferne(int idx) {
    for (int j = 0; j < nS; ) { if (S[j].a == idx || S[j].b == idx) { S[j] = S[--nS]; continue; } j++; }
    for (int g = 0; g < MAXG; g++) { if (!G[g].t[0]) continue; int w = 0; for (int k = 0; k < G[g].nn; k++) if (G[g].n[k] != idx) G[g].n[w++] = G[g].n[k]; G[g].nn = (uint8_t)w; }
    int last = nN - 1;
    if (idx != last) {
        for (int j = 0; j < nS; j++) { if (S[j].a == last) S[j].a = (uint16_t)idx; if (S[j].b == last) S[j].b = (uint16_t)idx; if (S[j].a > S[j].b) { uint16_t t = S[j].a; S[j].a = S[j].b; S[j].b = t; } }
        for (int g = 0; g < MAXG; g++) if (G[g].t[0]) for (int k = 0; k < G[g].nn; k++) if (G[g].n[k] == last) G[g].n[k] = (uint16_t)idx;
        N[idx] = N[last]; akt[idx] = akt[last];
    }
    nN--;
}
static int n_schwaechster(void) {
    int best = 0, bs = 1 << 30;
    for (int i = 0; i < nN; i++) {
        int sc = N[i].nutz, refs = 0; for (int g = 0; g < MAXG; g++) if (G[g].t[0]) for (int k = 0; k < G[g].nn; k++) if (G[g].n[k] == i) refs++;
        sc += refs * 12; for (int j = 0; j < nS; j++) if (S[j].a == i || S[j].b == i) sc += S[j].w / 24;
        if (sc < bs) { bs = sc; best = i; }
    }
    return best;
}
static int n_hole(const char* w) {
    int i = n_finde(w); if (i >= 0) return i;
    if (nN >= MAXN) n_entferne(n_schwaechster());
    i = nN++; kop(N[i].w, w, WL); N[i].nutz = 0; N[i].zuletzt = (uint16_t)takt; akt[i] = 0; return i;
}
static int s_finde(int a, int b) { for (int i = 0; i < nS; i++) if (S[i].a == a && S[i].b == b) return i; return -1; }
static void s_staerke(int a, int b, int rate) {                                /* Hebb: Zuwachs = Prozentsatz des Abstands zur Saettigung */
    if (a == b) return; if (a > b) { int t = a; a = b; b = t; }
    int i = s_finde(a, b);
    if (i < 0) {
        if (nS >= MAXS) { int w = 0; for (int j = 1; j < nS; j++) if (S[j].w < S[w].w) w = j; if (S[w].w > 40) return; i = w; }
        else i = nS++;
        S[i].a = (uint16_t)a; S[i].b = (uint16_t)b; S[i].w = 0;
    }
    int dw = (255 - S[i].w) * rate / 100; if (dw < 1) dw = 1; S[i].w = (uint8_t)minv(255, S[i].w + dw);
}

/* ---------- Speichern und Laden (verschluesselt in ZynDB) ---------- */
static const char B64[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz+/";
static int b64v(char c) { for (int i = 0; i < 64; i++) if (B64[i] == c) return i; return -1; }
static char* put64(char* p, int v, int w) { for (int i = w - 1; i >= 0; i--) p[i] = B64[(v >> (6 * (w - 1 - i))) & 63]; return p + w; }
static int get64(const char** p, int w) { int v = 0; for (int i = 0; i < w; i++) { int d = b64v((*p)[i]); if (d < 0) return -1; v = v << 6 | d; } *p += w; return v; }
static char buf[6400];
static void sichern(void) {
    if (!geladen || !zdb_offen()) return;
    zdb_stapel(1);
    char* p = buf; for (int i = 0; i < nN; i++) { p += 0; for (const char* w = N[i].w; *w; w++) *p++ = *w; *p++ = '.'; p = put64(p, N[i].nutz > 4095 ? 4095 : N[i].nutz, 2); p = put64(p, N[i].zuletzt, 3); *p++ = ';'; }
    *p = 0; zdb_put_text("ged.n", buf, (int)(p - buf));
    p = buf; for (int i = 0; i < nS; i++) { p = put64(p, S[i].a, 2); p = put64(p, S[i].b, 2); p = put64(p, S[i].w, 2); }
    *p = 0; zdb_put_text("ged.s", buf, (int)(p - buf));
    p = buf; for (int g = 0; g < MAXG; g++) {
        if (!G[g].t[0]) continue;
        p = put64(p, G[g].art, 1); p = put64(p, G[g].staerke, 2); p = put64(p, G[g].nn, 1); p = put64(p, G[g].zeit, 3); p = put64(p, G[g].zuletzt, 3);
        for (int k = 0; k < G[g].nn; k++) p = put64(p, G[g].n[k], 2);
        for (const char* t = G[g].t; *t; t++) *p++ = *t == '\n' ? ' ' : *t;
        *p++ = '\n';
    }
    *p = 0; zdb_put_text("ged.e", buf, (int)(p - buf));
    char z[12]; int l = 0; { uint32_t v = takt; char t[12]; int c = 0; do { t[c++] = (char)('0' + v % 10); v /= 10; } while (v); while (c) z[l++] = t[--c]; z[l] = 0; }
    zdb_put("ged.takt", z);
    zdb_stapel(0);
}
void ged_init(void) {
    if (geladen) return;
    zdb_get("ged.takt");                                                       /* stoesst das verzoegerte Oeffnen von ZynDB an */
    if (!zdb_offen()) return;
    nN = nS = 0; takt = (uint32_t)zdb_geti("ged.takt", 0); for (int g = 0; g < MAXG; g++) G[g].t[0] = 0;
    int l = zdb_get_text("ged.n", buf, sizeof(buf));
    if (l > 0) { const char* p = buf; while (*p && nN < MAXN) { int k = 0; char w[WL]; while (*p && *p != '.' && k < WL - 1) w[k++] = *p++; w[k] = 0; if (*p != '.') break; p++; int nu = get64(&p, 2), ze = get64(&p, 3); if (nu < 0 || ze < 0 || *p != ';' || !k) break; p++; kop(N[nN].w, w, WL); N[nN].nutz = (uint16_t)nu; N[nN].zuletzt = (uint16_t)ze; nN++; } }
    l = zdb_get_text("ged.s", buf, sizeof(buf));
    if (l > 0) { const char* p = buf; while (*p && nS < MAXS) { int a = get64(&p, 2), b = get64(&p, 2), w = get64(&p, 2); if (a < 0 || b < 0 || w < 0 || a >= nN || b >= nN || a >= b) break; S[nS].a = (uint16_t)a; S[nS].b = (uint16_t)b; S[nS].w = (uint8_t)w; nS++; } }
    l = zdb_get_text("ged.e", buf, sizeof(buf));
    if (l > 0) {
        const char* p = buf; int gi = 0;
        while (*p && gi < MAXG) {
            int art = get64(&p, 1), st = get64(&p, 2), nn = get64(&p, 1), ze = get64(&p, 3), zl = get64(&p, 3);
            if (art < 0 || st < 0 || nn < 0 || nn > NK || ze < 0 || zl < 0) break;
            struct erinn* e = &G[gi]; e->art = (uint8_t)art; e->staerke = (uint8_t)minv(255, st); e->nn = (uint8_t)nn; e->zeit = (uint16_t)ze; e->zuletzt = (uint16_t)zl;
            int ok = 1; for (int k = 0; k < nn; k++) { int n = get64(&p, 2); if (n < 0 || n >= nN) { ok = 0; break; } e->n[k] = (uint16_t)n; }
            if (!ok) break;
            int t = 0; while (*p && *p != '\n' && t < GL - 1) e->t[t++] = *p++; e->t[t] = 0; while (*p && *p != '\n') p++; if (*p == '\n') p++;
            if (e->t[0]) gi++;
        }
    }
    geladen = 1;
}

/* ---------- Erinnerungen ---------- */
static int g_frei(void) {
    for (int g = 0; g < MAXG; g++) if (!G[g].t[0]) return g;
    int w = -1, bs = 1 << 30;                                                  /* voll: die schwaechste unwichtige Erinnerung weicht */
    for (int g = 0; g < MAXG; g++) { if (G[g].art == 2) continue; int sc = G[g].staerke + G[g].art * 60; if (sc < bs) { bs = sc; w = g; } }
    return w;
}
static int g_gleich(const uint16_t* ns, int nn, int prozent) {
    for (int g = 0; g < MAXG; g++) {
        if (!G[g].t[0] || !G[g].nn || !nn) continue;
        int ueb = 0; for (int i = 0; i < nn; i++) for (int k = 0; k < G[g].nn; k++) if (G[g].n[k] == ns[i]) ueb++;
        if (ueb * 100 >= prozent * (nn > G[g].nn ? nn : G[g].nn)) return g;
    }
    return -1;
}
static void verdrahte(const uint16_t* ns, int nn, int rate) { for (int i = 0; i < nn; i++) for (int j = i + 1; j < nn; j++) s_staerke(ns[i], ns[j], rate); }
static void platz_schaffen(void) { while (nN > MAXN - 20) n_entferne(n_schwaechster()); }   /* damit beim Anlegen kein Neuron umnummeriert wird */
static int g_ablegen(const char* text, int art, int staerke) {
    char st[10][WL]; int c = zerlege(text, st, 10); if (c < 1) return -1;
    platz_schaffen();
    uint16_t ns[NK]; int nn = 0;
    for (int i = 0; i < c && nn < NK; i++) { int id = n_hole(st[i]); N[id].nutz = (uint16_t)minv(65000, N[id].nutz + 2); N[id].zuletzt = (uint16_t)takt; ns[nn++] = (uint16_t)id; }
    int g = g_gleich(ns, nn, art >= 1 ? 60 : 80), vorhanden = g >= 0;           /* fast gleiche Aussage: aktualisieren statt doppelt ablegen */
    if (!vorhanden) { g = g_frei(); if (g < 0) return -3; }
    struct erinn* e = &G[g];
    if (!vorhanden) { e->art = (uint8_t)art; e->staerke = (uint8_t)staerke; e->zeit = (uint16_t)(takt ? takt : 1); }
    else { if (art > e->art) e->art = (uint8_t)art; e->staerke = (uint8_t)minv(255, (e->staerke > staerke ? e->staerke : staerke) + 25); }
    kop(e->t, text, GL); for (int i = 0; e->t[i]; i++) if (e->t[i] == '\n') e->t[i] = ' ';
    e->nn = (uint8_t)nn; for (int i = 0; i < nn; i++) e->n[i] = ns[i]; e->zuletzt = (uint16_t)takt;
    verdrahte(ns, nn, art == 2 ? 30 : 14);
    return g;
}
int ged_merke(const char* text, int wichtig) {
    ged_init(); if (!geladen) return -3;
    while (*text == ' ') text++;
    if (!text[0]) return -1;
    if (ged_heikel(text)) return -2;
    takt++;
    int g = g_ablegen(text, wichtig ? 2 : 1, wichtig ? 255 : 170);
    if (g < 0) return g == -1 ? -1 : -3;
    sichern(); return 0;
}
int ged_vergiss(const char* text) {
    ged_init(); if (!geladen) return 0;
    char q[8][WL]; int qc = zerlege(text, q, 8); if (!qc) return 0;
    int weg = 0;
    for (int g = 0; g < MAXG; g++) {
        if (!G[g].t[0]) continue;
        int treffer = 0, bekannt = 0;
        for (int i = 0; i < qc; i++) { int id = n_finde(q[i]); if (id < 0) continue; bekannt++; for (int k = 0; k < G[g].nn; k++) if (G[g].n[k] == id) { treffer++; break; } }
        if (!bekannt || treffer < bekannt) continue;
        for (int i = 0; i < G[g].nn; i++) for (int j = i + 1; j < G[g].nn; j++) { int a = G[g].n[i], b = G[g].n[j]; if (a > b) { int t = a; a = b; b = t; } int s = s_finde(a, b); if (s >= 0) { S[s].w /= 3; if (!S[s].w) S[s] = S[--nS]; } }
        G[g].t[0] = 0; G[g].nn = 0; weg++;
    }
    if (weg) { nwg = 0; sichern(); }                                           /* auch das Arbeitsgedaechtnis vergisst */
    return weg;
}
void ged_loeschen(void) {
    ged_init(); nN = nS = 0; for (int g = 0; g < MAXG; g++) G[g].t[0] = 0; nwg = 0; takt = 0; sichern();
}

/* ---------- Abruf durch Aktivierungsausbreitung ---------- */
static int abruf(const char* text, int* sel, int* score, int max, int* seeds_out) {
    char q[12][WL]; int qc = zerlege(text, q, 12), seeds = 0;
    for (int i = 0; i < nN; i++) akt[i] = 0;
    for (int i = 0; i < qc; i++) { int id = n_finde(q[i]); if (id >= 0) { akt[id] = 1000; seeds++; } }
    if (seeds_out) *seeds_out = seeds;
    if (!seeds) return 0;
    for (int runde = 0; runde < 2; runde++) {
        static int neu[MAXN]; for (int i = 0; i < nN; i++) neu[i] = akt[i];
        for (int j = 0; j < nS; j++) {
            int a = S[j].a, b = S[j].b, w = S[j].w;
            neu[b] += akt[a] * w / 640; neu[a] += akt[b] * w / 640;            /* bei voller Staerke laufen gut 40 % der Erregung weiter */
        }
        for (int i = 0; i < nN; i++) akt[i] = (int16_t)(akt[i] >= 1000 ? 1000 : (neu[i] > 999 ? 999 : neu[i]));
    }
    int k = 0;
    for (int g = 0; g < MAXG; g++) {
        if (!G[g].t[0]) continue;
        int summe = 0, direkt = 0; for (int i = 0; i < G[g].nn; i++) { int a = akt[G[g].n[i]]; summe += a; if (a >= 1000) direkt++; }
        if (!direkt && summe < 450) continue;
        int sc = summe + G[g].staerke * 2 + direkt * 300;
        int p; if (k < max) p = k++; else if (sc > score[max - 1]) p = max - 1; else continue;
        sel[p] = g; score[p] = sc;
        for (int i = p; i > 0 && score[i] > score[i - 1]; i--) { int t = score[i]; score[i] = score[i - 1]; score[i - 1] = t; t = sel[i]; sel[i] = sel[i - 1]; sel[i - 1] = t; }
    }
    return k;
}
static void verstaerke(const int* sel, int k) {                                 /* Langzeitverstaerkung: was abgerufen wird, haelt laenger */
    for (int i = 0; i < k; i++) {
        struct erinn* e = &G[sel[i]]; e->staerke = (uint8_t)minv(255, e->staerke + 8); e->zuletzt = (uint16_t)takt;
        for (int j = 0; j < e->nn; j++) { N[e->n[j]].nutz = (uint16_t)minv(65000, N[e->n[j]].nutz + 1); for (int s = 0; s < nN; s++) if (akt[s] >= 1000 && s != e->n[j]) s_staerke(s, e->n[j], 5); }
    }
}
int ged_kontext(const char* frage, char* out, int max, int erinnerungen) {
    ged_init(); out[0] = 0; if (!geladen || max < 40) return 0;
    int sel[3], sc[3], seeds = 0, k = erinnerungen ? abruf(frage, sel, sc, 3, &seeds) : 0, l = 0;
    if (k) {
        const char* kopf = "Das weisst du aus frueheren Gespraechen: "; for (int i = 0; kopf[i] && l < max - 2; i++) out[l++] = kopf[i];
        for (int i = 0; i < k; i++) {
            const char* t = G[sel[i]].t; int tl = slen(t); if (tl > 100) tl = 100;
            if (l + tl + 4 >= max - 40) break;
            for (int j = 0; j < tl; j++) out[l++] = t[j];
            out[l++] = '.'; out[l++] = ' ';
        }
        verstaerke(sel, k);
    }
    int worte = 1; for (int i = 0; frage[i]; i++) if (frage[i] == ' ' && frage[i + 1] && frage[i + 1] != ' ') worte++;
    if (nwg && worte <= 8 && l < max - 60) {                                    /* kurze Frage: wahrscheinlich eine Folgefrage */
        int i = (nwg - 1) % WG; const char* a = "Davor: "; for (int j = 0; a[j] && l < max - 2; j++) out[l++] = a[j];
        for (int j = 0; wq[i][j] && l < max - 2; j++) out[l++] = wq[i][j];
        const char* b = " -> "; for (int j = 0; b[j] && l < max - 2; j++) out[l++] = b[j];
        for (int j = 0; wa[i][j] && l < max - 2; j++) out[l++] = wa[i][j];
        if (l < max - 2) out[l++] = '\n';
    }
    out[l] = 0; if (k) sichern();
    return l;
}

/* ---------- Lernen, Vergessen, Schlafen ---------- */
static int ist_frage(const char* text, char st0[]) {
    char f[400]; falte(text, f, sizeof(f)); for (int i = 0; f[i]; i++) if (f[i] == '?') return 1;
    int i = 0; while (f[i] == ' ') i++; int l = 0; char w[16]; while (f[i] && f[i] != ' ' && l < 15) w[l++] = f[i++]; w[l] = 0;
    static const char* const Q[] = { "wie","was","wer","wo","wann","warum","wieso","weshalb","welche","welcher","welches","wieviel","kannst","kann","koennen","darf","gibt","ist","sind","hast","hat","zeig","oeffne","starte","schreib","erstelle","mach",
        "how","what","who","where","when","why","which","can","could","is","are","do","does","did","will","would","should","show","open","qui","que","quoi","ou","quand","comment","pourquoi","come","cosa","chi","dove","quando","perche","como","quien","donde","cuando","por",0 };
    st0[0] = 0; for (int k = 0; Q[k]; k++) if (gl(Q[k], w)) return 1;
    for (int k = 0; k < l; k++) st0[k] = w[k]; st0[l] = 0; return 0;
}
static int persoenlich(const char* erstes) {
    static const char* const P[] = { "ich","mein","meine","meinen","meinem","meiner","i","my","je","mon","ma","mes","io","mio","mia","yo","mi","wir","unser","unsere",0 };
    for (int i = 0; P[i]; i++) if (gl(P[i], erstes)) return 1; return 0;
}
static void wg_push(const char* q, const char* a) {
    int i = nwg % WG; nwg++; kop(wq[i], q, 64); kop(wa[i], a, 72);
    for (int k = 0; wq[i][k]; k++) if (wq[i][k] == '\n') wq[i][k] = ' ';
    for (int k = 0; wa[i][k]; k++) if (wa[i][k] == '\n') wa[i][k] = ' ';
}
static int zyklus(void) {                                                       /* verblassen, Wiederholung wichtiger Erinnerungen, aufraeumen */
    int verloren = 0;
    for (int g = 0; g < MAXG; g++) {                                            /* Wiederholung: Wichtiges wird im "Schlaf" erneut verdrahtet */
        if (!G[g].t[0] || (G[g].art == 0 && G[g].staerke < 110)) continue;
        verdrahte(G[g].n, G[g].nn, 3);
    }
    for (int j = 0; j < nS; ) {
        int w = S[j].w; w -= w >= 32 ? w / 32 : 1;
        if (w <= 0) { S[j] = S[--nS]; verloren++; continue; }
        S[j].w = (uint8_t)w; j++;
    }
    for (int g = 0; g < MAXG; g++) {
        if (!G[g].t[0] || G[g].art == 2) continue;
        int d = G[g].art == 0 ? 3 : 1; if (G[g].staerke <= d) { G[g].t[0] = 0; G[g].nn = 0; verloren++; } else G[g].staerke = (uint8_t)(G[g].staerke - d);
    }
    for (int i = 0; i < nN; ) {                                                 /* Neuronen ohne Verbindung und ohne Erinnerung verschwinden */
        int genutzt = 0; for (int j = 0; j < nS && !genutzt; j++) if (S[j].a == i || S[j].b == i) genutzt = 1;
        for (int g = 0; g < MAXG && !genutzt; g++) if (G[g].t[0]) for (int k = 0; k < G[g].nn; k++) if (G[g].n[k] == i) genutzt = 1;
        if (!genutzt) { n_entferne(i); continue; }
        N[i].nutz = (uint16_t)(N[i].nutz - N[i].nutz / 16); i++;
    }
    return verloren;
}
int ged_schlafen(void) { ged_init(); if (!geladen) return 0; int v = 0; for (int i = 0; i < 3; i++) v += zyklus(); sichern(); return v; }
void ged_lerne(const char* nutzer, const char* antwort) {
    ged_init(); if (!geladen || !nutzer || !nutzer[0]) return;
    if (ged_heikel(nutzer)) return;
    takt++; platz_schaffen();
    char st[12][WL]; int c = zerlege(nutzer, st, 12);
    uint16_t ids[12]; for (int i = 0; i < c; i++) { ids[i] = (uint16_t)n_hole(st[i]); N[ids[i]].nutz = (uint16_t)minv(65000, N[ids[i]].nutz + 1); N[ids[i]].zuletzt = (uint16_t)takt; }
    for (int i = 0; i < c; i++) for (int j = i + 1; j < c && j <= i + 3; j++) s_staerke(ids[i], ids[j], 8);          /* Nachbarn im Satz */
    if (antwort && antwort[0] && c) {                                                                                /* Frage <-> Antwort schwach verbinden */
        char as[6][WL]; int ac = zerlege(antwort, as, 6);
        for (int j = 0; j < ac; j++) { int id = n_hole(as[j]); for (int i = 0; i < c && i < 4; i++) s_staerke(ids[i], id, 3); }
    }
    char erstes[16]; int frage = ist_frage(nutzer, erstes);
    int worte = 1; for (int i = 0; nutzer[i]; i++) if (nutzer[i] == ' ' && nutzer[i + 1] && nutzer[i + 1] != ' ') worte++;
    if (!frage && worte >= 3 && c >= 2) g_ablegen(nutzer, persoenlich(erstes) ? 1 : 0, persoenlich(erstes) ? 150 : 80);
    wg_push(nutzer, antwort ? antwort : "");
    if (takt % ZYKLUS == 0) zyklus();
    sichern();
}

/* ---------- Anzeige ---------- */
void ged_zahlen(int* n, int* s, int* g, int* t) { ged_init(); int c = 0; for (int i = 0; i < MAXG; i++) if (G[i].t[0]) c++; if (n) *n = nN; if (s) *s = nS; if (g) *g = c; if (t) *t = (int)takt; }
void ged_erregung_loesche(void) { for (int i = 0; i < MAXN; i++) akt[i] = 0; }
int ged_netz(int maxn, char names[][13], int* nutz, int* erregt, int maxs, int* sa, int* sb, int* sw, int* ns) {
    ged_init(); static int ord[MAXN], rang[MAXN];
    int c = nN < maxn ? nN : maxn; for (int i = 0; i < nN; i++) { ord[i] = i; rang[i] = -1; }
    for (int i = 0; i < c; i++) { int b = i; for (int j = i + 1; j < nN; j++) if (N[ord[j]].nutz > N[ord[b]].nutz) b = j; int t = ord[i]; ord[i] = ord[b]; ord[b] = t; rang[ord[i]] = i; kop(names[i], N[ord[i]].w, 13); nutz[i] = N[ord[i]].nutz; erregt[i] = akt[ord[i]]; }
    int o = 0; for (int j = 0; j < nS && o < maxs; j++) if (rang[S[j].a] >= 0 && rang[S[j].b] >= 0) { sa[o] = rang[S[j].a]; sb[o] = rang[S[j].b]; sw[o] = S[j].w; o++; }
    *ns = o; return c;
}
int ged_erinnerung(int i, char* text, int max, int* art, int* staerke) {
    ged_init(); int idx[MAXG], c = 0; for (int g = 0; g < MAXG; g++) if (G[g].t[0]) idx[c++] = g;
    for (int a = 0; a < c; a++) for (int b = a + 1; b < c; b++) { int sa = G[idx[a]].staerke + G[idx[a]].art * 100, sb = G[idx[b]].staerke + G[idx[b]].art * 100; if (sb > sa) { int t = idx[a]; idx[a] = idx[b]; idx[b] = t; } }
    if (i < 0 || i >= c) return 0;
    kop(text, G[idx[i]].t, max); if (art) *art = G[idx[i]].art; if (staerke) *staerke = G[idx[i]].staerke; return 1;
}

/* ---------- Shell ---------- */
static const char* skip(const char* s) { while (*s == ' ') s++; return s; }
static const char* ARTT[] = { "Gespraech", "persoenlich", "WICHTIG" };
void ged_befehl(const char* a) {
    ged_init();
    if (!geladen) { kprint("Gedaechtnis nicht verfuegbar (ZynDB gesperrt oder SynFS nicht eingebunden)\n"); return; }
    a = skip(a); char w[12]; int n = 0; while (*a && *a != ' ' && n < 11) w[n++] = *a++; w[n] = 0; a = skip(a);
    if (!w[0] || gl(w, "status")) {
        int nn, ss, gg, tt; ged_zahlen(&nn, &ss, &gg, &tt);
        kprint("Synapsen-Gedaechtnis: "); kprint_dec((uint32_t)nn); kprint(" Neuronen, "); kprint_dec((uint32_t)ss); kprint(" Synapsen, "); kprint_dec((uint32_t)gg); kprint(" Erinnerungen, Takt "); kprint_dec((uint32_t)tt); kprint("\n");
        kprint("  verschluesselt in ZynDB (ged.*). Es lernt aus deinen Gespraechen, verstaerkt Oft-Gebrauchtes und laesst Unwichtiges verblassen.\n");
        kprint("  Befehle: gedaechtnis liste | merke <text> | erinnere <text> | vergiss <text> | netz | schlafen | loeschen ja\n"); return;
    }
    if (gl(w, "liste") || gl(w, "list")) {
        char t[GL]; int art, st, c = 0;
        for (int i = 0; ged_erinnerung(i, t, GL, &art, &st); i++) { kprint("  ["); kprint(ARTT[art]); kprint(", Staerke "); kprint_dec((uint32_t)st); kprint("] "); kprint(t); kprint("\n"); c++; }
        if (!c) kprint("Noch keine Erinnerungen. Sag zum Beispiel: merk dir, dass mein Hund Bello heisst\n"); return;
    }
    if (gl(w, "merke") || gl(w, "merk") || gl(w, "remember")) {
        int r = ged_merke(a, 1); kprint(r == 0 ? "Gemerkt (wichtig, verblasst nicht)\n" : r == -2 ? "Passwoerter, Schluessel und Kontonummern merke ich mir nicht\n" : r == -1 ? "gedaechtnis merke <text>\n" : "Gedaechtnis voll oder nicht verfuegbar\n"); return;
    }
    if (gl(w, "vergiss") || gl(w, "forget")) { int r = ged_vergiss(a); if (r) { kprint_dec((uint32_t)r); kprint(" Erinnerung(en) vergessen\n"); } else kprint("Dazu habe ich nichts gefunden\n"); return; }
    if (gl(w, "erinnere") || gl(w, "frage") || gl(w, "recall")) {
        if (!*a) { kprint("gedaechtnis erinnere <woran>\n"); return; }
        int sel[3], sc[3], seeds, k = abruf(a, sel, sc, 3, &seeds);
        kprint("  "); kprint_dec((uint32_t)seeds); kprint(" Neuronen feuern\n");
        for (int i = 0; i < k; i++) { kprint("  "); kprint_dec((uint32_t)(sc[i] * 100 / 3000 > 100 ? 100 : sc[i] * 100 / 3000)); kprint(" %  "); kprint(G[sel[i]].t); kprint("\n"); }
        if (!k) kprint("  Nichts Passendes im Gedaechtnis\n"); else verstaerke(sel, k);
        return;
    }
    if (gl(w, "netz") || gl(w, "net")) {
        char nm[12][13]; int nu[12], er[12], sa[200], sb[200], sw[200], ns; int c = ged_netz(12, nm, nu, er, 200, sa, sb, sw, &ns);
        for (int i = 0; i < ns; i++) for (int j = i + 1; j < ns; j++) if (sw[j] > sw[i]) { int t = sw[i]; sw[i] = sw[j]; sw[j] = t; t = sa[i]; sa[i] = sa[j]; sa[j] = t; t = sb[i]; sb[i] = sb[j]; sb[j] = t; }
        kprint("Haeufigste Neuronen:"); for (int i = 0; i < c; i++) { kprint(" "); kprint(nm[i]); kprint("("); kprint_dec((uint32_t)nu[i]); kprint(")"); } kprint("\n");
        kprint("Staerkste Synapsen:\n"); for (int i = 0; i < ns && i < 8; i++) { kprint("  "); kprint(nm[sa[i]]); kprint(" <-> "); kprint(nm[sb[i]]); kprint("  "); kprint_dec((uint32_t)sw[i]); kprint("/255\n"); }
        if (!ns) kprint("  noch keine\n"); return;
    }
    if (gl(w, "schlafen") || gl(w, "sleep")) { int v = ged_schlafen(); kprint("Konsolidiert: Wichtiges wiederholt, "); kprint_dec((uint32_t)v); kprint(" schwache Verbindungen/Erinnerungen vergessen\n"); return; }
    if (gl(w, "loeschen") || gl(w, "clear")) { if (!gl(a, "ja")) { kprint("Das loescht das ganze Gedaechtnis. Zum Bestaetigen: gedaechtnis loeschen ja\n"); return; } ged_loeschen(); kprint("Gedaechtnis geleert\n"); return; }
    kprint("gedaechtnis: status | liste | merke <text> | erinnere <text> | vergiss <text> | netz | schlafen | loeschen ja\n");
}
void ged_neu_laden(void) { geladen = 0; ged_init(); }
