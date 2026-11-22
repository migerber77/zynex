/* Zynies Gehirn = der Zynex Lernkern (siehe include/zlk.h und wissen.h). Es gibt kein Sprachmodell von aussen mehr:
 * Antworten kommen aus dem Lernkern, sonst wird im Internet nachgeschlagen und der Lernkern lernt dabei. */
#include "gehirn.h"
#include "wissen.h"
#include "zlk.h"
#include "zyndb.h"

extern void kprint(const char* str);

static int slen(const char* s) { int n = 0; while (s[n]) n++; return n; }
static void kopie(char* d, const char* s, int max) { int i = 0; while (s[i] && i < max - 1) { d[i] = s[i]; i++; } d[i] = 0; }

int gehirn_start(void) { return 0; }
int gehirn_bereit(void) { return 1; }
const char* gehirn_status(void) { return "Lernkern bereit"; }
int gehirn_vorhanden(void) { return 1; }
void gehirn_vergessen(void) { }
void gehirn_kontext(const char* t) { (void)t; }
int gehirn_modelle_zeigen(void) { kprint("  Zynex Lernkern (modell.zyki), eingebaut\n"); return 1; }
int gehirn_modell_waehlen(const char* dev) { (void)dev; return -1; }
int gehirn_modell_loeschen(const char* dev) { (void)dev; return -3; }

int gehirn_frage(const char* frage, char* antwort, int max, void (*cb)(const char* teil, int n)) {
    int hk = 0, n = -3;
    if (zdb_geti("wissen.an", 1)) n = wissen_frage(frage, "de", antwort, max, &hk);
    if (n <= 0) {
        const char* t = n == -1 ? "Das weiss ich noch nicht, und ich habe gerade kein Internet zum Nachschlagen. Ich lerne mit jedem Gespraech dazu."
                      : n == -2 ? "Dazu habe ich im Internet nichts Passendes gefunden."
                      : "Ich habe es mir gemerkt, kann damit aber noch nichts anfangen. Frag mich etwas mit wie, was, wer, wo oder wann.";
        if (n == -3) { zlk_lerne(frage, -1); zlk_lerne("\n", 1); zlk_speichern(); }
        kopie(antwort, t, max); n = slen(antwort);
    }
    if (cb) cb(antwort, n);
    return n;
}
