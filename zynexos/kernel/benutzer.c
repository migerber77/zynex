/*
 * Zynex OS - Benutzerverwaltung
 * Konten (bis 8) in der SynFS-Datei "sys.benutzer". Passwort: PBKDF2-HMAC-SHA256, 20000 Runden, 16 Byte Salz.
 * Rollen: Gast (nur lesen/KI), Benutzer (Dateien, Netz, Programme), Admin (alles).
 */
#include "benutzer.h"
#include "capability.h"
#include "crypto.h"
#include "synfs.h"

extern void kprint(const char* str);
extern void kprint_dec(uint32_t val);

#define MAXB 8
#define RUNDEN 20000
#define DATEI "sys.benutzer"
#define BMAGIC 0x4E454258u /* 'XBEN' */

struct konto { char name[16]; uint8_t rolle, used, pad[2]; uint8_t salt[16]; uint8_t hash[32]; };
struct bdatei { uint32_t magic; struct konto k[MAXB]; };

static struct konto kto[MAXB];
static int aktuell = -1;

static int seq(const char* a, const char* b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }
static uint32_t slen(const char* s) { uint32_t n = 0; while (s[n]) n++; return n; }
static void zero(void* p, uint32_t n) { uint8_t* b = p; for (uint32_t i = 0; i < n; i++) b[i] = 0; }

static int speichern(void) {
    static struct bdatei d;
    d.magic = BMAGIC;
    for (int i = 0; i < MAXB; i++) d.k[i] = kto[i];
    if (synfs_write(DATEI, &d, sizeof(d)) < 0) return -1;
    return 0;
}
int benutzer_laden(void) {
    static struct bdatei d;
    aktuell = -1;
    zero(kto, sizeof(kto));
    int r = synfs_read(DATEI, &d, sizeof(d));
    if (r == (int)sizeof(d) && d.magic == BMAGIC) for (int i = 0; i < MAXB; i++) kto[i] = d.k[i];
    return benutzer_anzahl();
}
int benutzer_anzahl(void) { int n = 0; for (int i = 0; i < MAXB; i++) if (kto[i].used) n++; return n; }
int benutzer_gesperrt(void) { return benutzer_anzahl() > 0 && aktuell < 0; }

static int suche(const char* name) {
    for (int i = 0; i < MAXB; i++) if (kto[i].used && seq(kto[i].name, name)) return i;
    return -1;
}
static int name_ok(const char* n) {
    uint32_t l = slen(n);
    if (l < 1 || l > 15) return 0;
    for (uint32_t i = 0; i < l; i++) {
        char c = n[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.')) return 0;
    }
    return 1;
}
static void hashen(const char* pw, const uint8_t salt[16], uint8_t out[32]) { pbkdf2_sha256((const uint8_t*)pw, slen(pw), salt, 16, RUNDEN, out); }
static void setze_pw(struct konto* k, const char* pw) { krand(k->salt, 16); hashen(pw, k->salt, k->hash); }

int benutzer_neu(const char* name, const char* pw, int rolle) {
    if (!name_ok(name)) return -1;
    if (slen(pw) < 4) return -5;
    if (rolle < 0 || rolle > 2) return -1;
    if (suche(name) >= 0) return -2;
    int erster = benutzer_anzahl() == 0;
    if (erster) rolle = ROLLE_ADMIN;                       /* das erste Konto ist immer Admin */
    int f = -1; for (int i = 0; i < MAXB; i++) if (!kto[i].used) { f = i; break; }
    if (f < 0) return -3;
    zero(&kto[f], sizeof(kto[f]));
    for (uint32_t i = 0; i <= slen(name); i++) kto[f].name[i] = name[i];
    kto[f].rolle = (uint8_t)rolle; kto[f].used = 1;
    setze_pw(&kto[f], pw);
    if (speichern() < 0) { kto[f].used = 0; return -4; }
    if (erster) aktuell = f;                               /* wer die Einrichtung macht, ist gleich angemeldet */
    return 0;
}
static int admins(void) { int n = 0; for (int i = 0; i < MAXB; i++) if (kto[i].used && kto[i].rolle == ROLLE_ADMIN) n++; return n; }
int benutzer_loeschen(const char* name) {
    int i = suche(name); if (i < 0) return -2;
    if (kto[i].rolle == ROLLE_ADMIN && admins() <= 1) return -6;           /* letzten Admin nicht loeschen */
    kto[i].used = 0;
    if (i == aktuell) aktuell = -1;
    return speichern() < 0 ? -4 : 0;
}
int benutzer_passwort(const char* name, const char* pw) {
    int i = suche(name); if (i < 0) return -2;
    if (slen(pw) < 4) return -5;
    setze_pw(&kto[i], pw);
    return speichern() < 0 ? -4 : 0;
}
int benutzer_login(const char* name, const char* pw) {
    int i = suche(name);
    uint8_t h[32], dummy[16] = {0}; uint8_t d = 0;
    if (i < 0) { hashen(pw, dummy, h); return -1; }                         /* gleiche Rechenzeit, kein Hinweis auf Kontonamen */
    hashen(pw, kto[i].salt, h);
    for (int k = 0; k < 32; k++) d |= h[k] ^ kto[i].hash[k];
    if (d) return -1;
    aktuell = i;
    return 0;
}
void benutzer_abmelden(void) { aktuell = -1; }
const char* benutzer_name(void) { return aktuell >= 0 ? kto[aktuell].name : ""; }
int benutzer_rolle(void) {
    if (benutzer_anzahl() == 0) return ROLLE_ADMIN;
    return aktuell >= 0 ? kto[aktuell].rolle : -1;
}
const char* benutzer_rollentext(int r) { return r == ROLLE_ADMIN ? "admin" : r == ROLLE_BENUTZER ? "benutzer" : "gast"; }
int benutzer_rolle_aus_text(const char* t) {
    if (seq(t, "admin")) return ROLLE_ADMIN;
    if (seq(t, "benutzer")) return ROLLE_BENUTZER;
    if (seq(t, "gast")) return ROLLE_GAST;
    return -1;
}
int benutzer_darf(int t) {
    int r = benutzer_rolle();
    if (r < 0) return 0;
    if (r == ROLLE_ADMIN) return 1;
    if (r == ROLLE_BENUTZER) return !(t == CAP_ADMIN || t == CAP_MODULE || t == CAP_MEM_WRITE);
    return t == CAP_MEM_READ || t == CAP_AI;
}
void benutzer_liste(void) {
    kprint("Benutzer:\n");
    for (int i = 0; i < MAXB; i++) if (kto[i].used) {
        kprint("  "); kprint(kto[i].name); kprint("  ("); kprint(benutzer_rollentext(kto[i].rolle)); kprint(")");
        if (i == aktuell) kprint("  <- angemeldet");
        kprint("\n");
    }
    if (!benutzer_anzahl()) kprint("  (keine - der erste Benutzer wird Admin: benutzer neu <name> admin)\n");
}
