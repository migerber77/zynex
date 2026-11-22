/* Lernschleife, siehe include/wissen.h */
#include "wissen.h"
#include "zlk.h"
#include "net.h"
#include "extki.h"

static int slen(const char* s) { int n = 0; while (s[n]) n++; return n; }
static void kat(char* d, const char* s, int max) { int n = slen(d), i = 0; while (s[i] && n + i < max - 1) { d[n + i] = s[i]; i++; } d[n + i] = 0; }
static char klein(char c) { return c >= 'A' && c <= 'Z' ? (char)(c + 32) : c; }

/* Frage -> kleine, satzzeichenfreie Form ("wie weit ist der mond von der erde entfernt") */
static void normal(const char* f, char* k, int max) {
    int n = 0, sp = 1;
    for (; *f && n < max - 1; f++) {
        unsigned char c = (unsigned char)*f;
        int ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c >= 128;
        if (ok) { k[n++] = klein((char)c); sp = 0; }
        else if (!sp) { k[n++] = ' '; sp = 1; }
    }
    while (n > 0 && k[n - 1] == ' ') n--;
    k[n] = 0;
}

static const char* const STOP[] = { "wie", "was", "wer", "wo", "wann", "warum", "wieso", "weshalb", "welche", "welcher", "welches", "wieviel", "wieviele", "ist", "sind", "war", "waren", "wird", "hat", "haben", "der", "die", "das", "den", "dem", "des", "ein", "eine", "einen", "einem", "von", "vom", "zu", "zum", "zur", "im", "in", "an", "am", "auf", "mit", "und", "oder", "bei", "er", "sie", "ihn", "ihm", "ihr", "dort", "ich", "du", "mir", "mich", "sag", "kannst", "bitte", "gibt", "es",
    "what", "who", "where", "when", "why", "how", "which", "is", "are", "was", "were", "the", "a", "an", "of", "to", "in", "on", "and", "or", "does", "do", "did", "from", "much", "many", "far", "me", "tell",
    "quel", "quelle", "est", "le", "la", "les", "du", "des", "de", "un", "une", "et", "che", "il", "lo", "come", "qual", "el", "los", "las", "que", "es", "como", 0 };
static int ist_stop(const char* w, int n) { for (int i = 0; STOP[i]; i++) { int m = slen(STOP[i]); if (m == n) { int ok = 1; for (int j = 0; j < n; j++) if (w[j] != STOP[i][j]) { ok = 0; break; } if (ok) return 1; } } return 0; }

/* Inhaltsworte der normalisierten Frage in w[][24] */
static int inhalt(const char* k, char w[10][24]) {
    int nw = 0, i = 0;
    while (k[i] && nw < 10) {
        while (k[i] == ' ') i++;
        int s = i; while (k[i] && k[i] != ' ') i++;
        int n = i - s; if (n <= 0) break;
        if (n < 2 || ist_stop(k + s, n)) continue;
        if (n > 23) n = 23;
        for (int j = 0; j < n; j++) w[nw][j] = k[s + j];
        w[nw][n] = 0; nw++;
    }
    return nw;
}

/* Lernschluessel: die Inhaltsworte alphabetisch sortiert. So treffen "Wie weit ist der Mond von der Erde entfernt?" und
 * "Wie weit ist die Erde vom Mond entfernt" denselben Eintrag. */
static void schluessel(const char* frage, char* key, int max) {
    char k[160]; normal(frage, k, sizeof(k)); char w[10][24]; int nw = inhalt(k, w);
    if (nw < 1) { int i = 0; for (; k[i] && i < max - 1; i++) key[i] = k[i]; key[i] = 0; return; }
    for (int a = 1; a < nw; a++) for (int b = a; b > 0; b--) { int c = 0; for (int j = 0; w[b][j] || w[b-1][j]; j++) { if (w[b][j] != w[b-1][j]) { c = w[b][j] < w[b-1][j] ? -1 : 1; break; } } if (c < 0) { char t[24]; for (int j = 0; j < 24; j++) { t[j] = w[b][j]; w[b][j] = w[b-1][j]; w[b-1][j] = t[j]; } } else break; }
    key[0] = 0; for (int i = 0; i < nw; i++) { if (i) kat(key, " ", max); kat(key, w[i], max); }
}

int wissen_ist_frage(const char* s) {
    char k[160]; normal(s, k, sizeof(k)); char w[10][24]; int nw = inhalt(k, w);
    if (nw < 1) return 0;
    for (int i = 0; s[i]; i++) if (s[i] == '?') return 1;
    static const char* const Q[] = { "wie ", "was ", "wer ", "wo ", "wann ", "warum ", "wieso ", "welche", "wieviel", "what ", "who ", "where ", "when ", "why ", "how ", "which ", "quel", "qui ", "ou ", "quand ", "comment ", "chi ", "dove ", "quando ", "come ", "quien ", "donde ", "cuando ", "como ", 0 };
    for (int i = 0; Q[i]; i++) { int m = slen(Q[i]), ok = 1; for (int j = 0; j < m; j++) if (k[j] != Q[i][j]) { ok = 0; break; } if (ok) return 1; }
    return 0;
}

int wissen_satz(const char* text, const char* frage, char* out, int max) {
    char k[160]; normal(frage, k, sizeof(k)); char w[10][24]; int nw = inhalt(k, w);
    int best = -1, bestn = -1, bs = 0, be = 0, first_e = 0;
    int i = 0, tl = slen(text);
    while (i < tl) {
        int s = i; while (s < tl && text[s] == ' ') s++;
        int e = s; while (e < tl) { if (text[e] == '.' && (e + 1 >= tl || text[e + 1] == ' ') && !(e >= 2 && text[e - 2] == ' ' && e - s > 1 && 0)) { e++; break; } e++; }
        if (e <= s) break;
        if (!first_e) first_e = e;
        int score = 0;
        for (int q = 0; q < nw; q++) {                      /* Wort (oder sein Wortanfang bis auf 2 Zeichen) kommt vor */
            int wl = slen(w[q]); int need = wl > 5 ? wl - 2 : wl;
            for (int p = s; p + need <= e; p++) { int ok = 1; for (int j = 0; j < need; j++) if (klein(text[p + j]) != w[q][j]) { ok = 0; break; } if (ok) { score++; break; } }
        }
        int hat_zahl = 0; for (int p = s; p < e; p++) if (text[p] >= '0' && text[p] <= '9') { hat_zahl = 1; break; }
        int zahlfrage = 0; { static const char* const Z[] = { "wie weit", "wie viel", "wie gross", "wie hoch", "wie lang", "wie alt", "wann", "how far", "how many", "how much", "how big", "how long", "how old", "when", 0 }; for (int z = 0; Z[z]; z++) { int m = slen(Z[z]), ok = 1; for (int j = 0; j < m; j++) if (k[j] != Z[z][j]) { ok = 0; break; } if (ok) zahlfrage = 1; } }
        int wert = score * 10 + (zahlfrage && hat_zahl ? 12 : 0);
        if (score > 0 && wert > bestn) { bestn = wert; best = score; bs = s; be = e; }
        i = e;
    }
    if (best <= 0) { out[0] = 0; return 0; }
    int n = be - bs; if (n > max - 1) n = max - 1; if (n > 320) n = 320;
    for (int j = 0; j < n; j++) out[j] = text[bs + j];
    out[n] = 0; return best;
}

static void lerne_paar(const char* key, const char* ant) {
    static char z[560]; z[0] = 0; kat(z, "F:", 560); kat(z, key, 560); kat(z, "\nA:", 560);
    for (int i = 0; ant[i]; i++) { char c[2] = { ant[i] == '\n' ? ' ' : ant[i], 0 }; kat(z, c, 560); }
    kat(z, "\n", 560);
    for (int r = 0; r < 3; r++) zlk_lerne(z, -1);
    zlk_speichern();
}
int wissen_merke(const char* frage, const char* antwort) {
    char k[160]; schluessel(frage, k, sizeof(k)); if (!k[0] || !antwort[0]) return -1;
    lerne_paar(k, antwort); return 0;
}

static void urlkodiert(const char* s, char* o, int max) {
    static const char* H = "0123456789ABCDEF"; int n = 0;
    for (; *s && n < max - 4; s++) {
        unsigned char c = (unsigned char)*s;
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z')) o[n++] = (char)c;
        else if (c == ' ') o[n++] = '+';
        else { o[n++] = '%'; o[n++] = H[c >> 4]; o[n++] = H[c & 15]; }
    }
    o[n] = 0;
}

int wissen_frage(const char* frage, const char* sprache, char* out, int max, int* herkunft) {
    out[0] = 0; if (herkunft) *herkunft = 0;
    if (!wissen_ist_frage(frage)) return -3;
    char k[160]; normal(frage, k, sizeof(k)); char ks[160]; schluessel(frage, ks, sizeof(ks));
    /* 1. Lernkern fragen */
    static char anf[200], ant[400], fz[200]; anf[0] = 0; kat(anf, "F:", 200); kat(anf, ks, 200); kat(anf, "\nA:", 200);
    fz[0] = 0; kat(fz, "F:", 200); kat(fz, ks, 200); kat(fz, "\n", 200);
    int n = 0;
    if (zlk_pruefe(fz) < 100) n = zlk_ergaenze(anf, ant, sizeof(ant), 0);           /* kennt der Lernkern diese Frage (unter 1 Bit je Zeichen)? */
    if (n > 2 && zlk_sicherheit() < 60) { for (int i = 0; i <= n && i < max; i++) out[i] = ant[i]; out[max - 1] = 0; if (herkunft) *herkunft = WISSEN_ENGINE; return slen(out); }
    /* 2. Im Internet nachschlagen */
    if (!net_up()) return -1;
    char w[10][24]; int nw = inhalt(k, w); if (nw < 1) return -3;
    static char suche[200], rohsuche[160], url[520]; rohsuche[0] = 0;
    for (int i = 0; i < nw; i++) { if (i) kat(rohsuche, " ", 160); kat(rohsuche, w[i], 160); }
    urlkodiert(rohsuche, suche, sizeof(suche));
    url[0] = 0; kat(url, "https://", 520); kat(url, sprache && sprache[0] ? sprache : "de", 520);
    kat(url, ".wikipedia.org/w/api.php?action=query&generator=search&gsrlimit=1&prop=extracts&exintro=1&explaintext=1&format=json&gsrsearch=", 520); kat(url, suche, 520);
    const char *kopf, *body; uint32_t kl, bl, st;
    if (net_seite(url, &kopf, &kl, &body, &bl, &st) != 0 || st != 200) return -1;
    static char extrakt[3000];
    int el = ext_json_text(body, bl, "extract", extrakt, sizeof(extrakt));
    if (el <= 0) return -2;
    static char satz[340];
    if (wissen_satz(extrakt, frage, satz, sizeof(satz)) <= 0) return -2;
    lerne_paar(ks, satz);
    int sl = slen(satz); if (sl > max - 1) sl = max - 1;
    for (int i = 0; i < sl; i++) out[i] = satz[i];
    out[sl] = 0; if (herkunft) *herkunft = WISSEN_WEB; return sl;
}
