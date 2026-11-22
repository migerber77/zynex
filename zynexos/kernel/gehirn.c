/* Zynies Gehirn = der Zynex Lernkern (siehe include/zlk.h und wissen.h). Es gibt kein Sprachmodell von aussen mehr:
 * Antworten kommen aus dem Lernkern, sonst wird im Internet nachgeschlagen und der Lernkern lernt dabei. */
#include "gehirn.h"
#include "wissen.h"
#include "zlk.h"
#include "zyndb.h"

extern void kprint(const char* str);

static int slen(const char* s) { int n = 0; while (s[n]) n++; return n; }
static void kopie(char* d, const char* s, int max) { int i = 0; while (s[i] && i < max - 1) { d[i] = s[i]; i++; } d[i] = 0; }

static char vorfrage[200];     /* letzte beantwortete Frage (Gespraechsverlauf) */
int gehirn_start(void) { return 0; }
int gehirn_bereit(void) { return 1; }
const char* gehirn_status(void) { return "Lernkern bereit"; }
int gehirn_vorhanden(void) { return 1; }
void gehirn_vergessen(void) { vorfrage[0] = 0; }
void gehirn_kontext(const char* t) { (void)t; }
int gehirn_modelle_zeigen(void) { kprint("  Zynex Lernkern (modell.zyki), eingebaut\n"); return 1; }
int gehirn_modell_waehlen(const char* dev) { (void)dev; return -1; }
int gehirn_modell_loeschen(const char* dev) { (void)dev; return -3; }

/* Gespraechsverlauf: die letzte beantwortete Frage. Kurze Nachfragen ("und wie gross ist er?") bekommen ihr Thema von dort. */
static int gleich_wort(const char* w, int wl, const char* l) { int i = 0; for (; i < wl; i++) { char c = w[i]; if (c >= 'A' && c <= 'Z') c += 32; if (l[i] != c) return 0; } return l[i] == 0; }
static int ist_nachfrage(const char* f) {
    static const char* pron[] = { "er", "sie", "es", "ihn", "ihm", "ihr", "ihnen", "dort", "dann", "davon", "dazu", "daran", "damit", "darin", "dafuer", "dieser", "diese", "dieses", "dessen", "deren", "he", "she", "it", "they", "there", 0 };
    int zahl = 0, i = 0;
    for (;;) {
        while (f[i] == ' ' || f[i] == '?' || f[i] == ',' || f[i] == '.') i++;
        if (!f[i]) break;
        int a = i; while (f[i] && f[i] != ' ' && f[i] != '?' && f[i] != ',' && f[i] != '.') i++;
        int wl = i - a; zahl++;
        if (zahl == 1 && (gleich_wort(f + a, wl, "und") || gleich_wort(f + a, wl, "and") || gleich_wort(f + a, wl, "aber"))) return 1;
        for (int k = 0; pron[k]; k++) if (gleich_wort(f + a, wl, pron[k])) return 1;
    }
    return zahl <= 3;
}

/* Thema einer Frage = das erste gross geschriebene Wort nach dem ersten (deutsche Hauptwoerter): "Wie weit ist der Mond von der Erde entfernt?" -> "Mond" */
static int thema_von(const char* f, char* out, int max) {
    int n = 0, i = 0, wort = 0;
    while (f[i]) {
        while (f[i] == ' ') i++;
        int a = i; while (f[i] && f[i] != ' ' && f[i] != '?' && f[i] != ',' && f[i] != '.') i++;
        int wl = i - a; if (wl > 0) { wort++; if (wort > 1 && n == 0 && f[a] >= 'A' && f[a] <= 'Z' && n + wl + 2 < max) { if (n) out[n++] = ' '; for (int j = 0; j < wl; j++) out[n++] = f[a + j]; } }
        while (f[i] == '?' || f[i] == ',' || f[i] == '.') i++;
    }
    out[n] = 0; return n;
}

int gehirn_frage(const char* frage_roh, char* antwort, int max, void (*cb)(const char* teil, int n)) {
    int hk = 0, n = -3;
    char voll[420], thema[100]; const char* frage = frage_roh;
    int hat_thema = thema_von(frage_roh, thema, sizeof thema);
    if (!hat_thema && vorfrage[0] && ist_nachfrage(frage_roh) && slen(frage_roh) + slen(vorfrage) + 2 < (int)sizeof voll) {
        int k = 0;
        for (int i = 0; frage_roh[i]; i++) voll[k++] = frage_roh[i];
        voll[k++] = ' ';
        for (int i = 0; vorfrage[i]; i++) voll[k++] = vorfrage[i];
        voll[k] = 0; frage = voll;
    }
    if (zdb_geti("wissen.an", 1)) n = wissen_frage(frage, "de", antwort, max, &hk);
    if (n > 0 && hat_thema) kopie(vorfrage, thema, (int)sizeof vorfrage);
    if (n <= 0) {
        const char* t = n == -1 ? "Das weiss ich noch nicht, und ich habe gerade kein Internet zum Nachschlagen. Ich lerne mit jedem Gespraech dazu."
                      : n == -2 ? "Dazu habe ich im Internet nichts Passendes gefunden."
                      : "Ich habe es mir gemerkt, kann damit aber noch nichts anfangen. Frag mich etwas mit wie, was, wer, wo oder wann.";
        if (n == -3) { zlk_lerne(frage_roh, -1); zlk_lerne("\n", 1); zlk_speichern(); }
        kopie(antwort, t, max); n = slen(antwort);
    }
    if (cb) cb(antwort, n);
    return n;
}
