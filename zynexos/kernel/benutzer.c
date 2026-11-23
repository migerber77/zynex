/*
 * Zynex OS - Benutzerverwaltung
 * Konten (bis 8) in der SynFS-Datei "sys.benutzer". Passwort: PBKDF2-HMAC-SHA256, 20000 Runden, 16 Byte Salz.
 * Rollen: Gast (nur lesen/KI), Benutzer (Dateien, Netz, Programme), Admin (alles).
 */
#include "benutzer.h"
#include "capability.h"
#include "crypto.h"
#include "synfs.h"
#include "timer.h"

extern void kprint(const char* str);
extern void kprint_dec(uint32_t val);

#define MAXB 8
#define RUNDEN 20000
#define DATEI "sys.benutzer"
#define BMAGIC 0x4E454258u /* 'XBEN' */

struct konto { char name[16]; uint8_t rolle, used, pad[2]; uint8_t salt[16]; uint8_t hash[32]; };
struct bdatei { uint32_t magic; struct konto k[MAXB]; };

static struct konto kto[MAXB]; static int rad_geladen;
static int aktuell = -1;

static int seq(const char* a, const char* b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }
static uint32_t slen(const char* s) { uint32_t n = 0; while (s[n]) n++; return n; }
static void zero(void* p, uint32_t n) { uint8_t* b = p; for (uint32_t i = 0; i < n; i++) b[i] = 0; }

int benutzer_rad_entfernen(const char* name);
static void rad_umbenennen(const char* alt, const char* neu);
static int speichern(void) {
    static struct bdatei d;
    d.magic = BMAGIC;
    for (int i = 0; i < MAXB; i++) d.k[i] = kto[i];
    if (synfs_write(DATEI, &d, sizeof(d)) < 0) return -1;
    return 0;
}
int benutzer_laden(void) {
    static struct bdatei d;
    aktuell = -1; rad_geladen = 0;
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
    kto[i].used = 0; benutzer_rad_entfernen(name);
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

int benutzer_nth(int n, char* name, int* rolle) {
    for (int i = 0; i < MAXB; i++) if (kto[i].used && n-- == 0) { for (int k = 0; k < 16; k++) name[k] = kto[i].name[k]; if (rolle) *rolle = kto[i].rolle; return 1; }
    return 0;
}
int benutzer_umbenennen(const char* alt, const char* neu) {
    int i = suche(alt); if (i < 0) return -2;
    if (!name_ok(neu)) return -1;
    if (seq(alt, neu)) return 0;
    if (suche(neu) >= 0) return -2;
    char bak[16]; for (int k = 0; k < 16; k++) bak[k] = kto[i].name[k];
    for (uint32_t k = 0; k <= slen(neu); k++) kto[i].name[k] = neu[k];
    if (speichern() < 0) { for (int k = 0; k < 16; k++) kto[i].name[k] = bak[k]; return -4; }
    rad_umbenennen(alt, neu);
    return 0;
}
int benutzer_rolle_setzen(const char* name, int rolle) {
    int i = suche(name); if (i < 0) return -2;
    if (rolle < 0 || rolle > 2) return -1;
    if (kto[i].rolle == ROLLE_ADMIN && rolle != ROLLE_ADMIN && admins() <= 1) return -6;
    kto[i].rolle = (uint8_t)rolle;
    return speichern() < 0 ? -4 : 0;
}

/* ---- Rad-Schluessel: zweite, passwortlose Anmeldung (Symbolfolge, siehe desktop.c). Eigene Datei "sys.radkey", Konten-Datei bleibt unveraendert ---- */
#define RDATEI "sys.radkey"
#define RMAGIC 0x59454B52u /* 'RKEY' */
struct rkonto { char name[16]; uint8_t used, pad[3]; uint8_t salt[16]; uint8_t hash[32]; };
struct rdatei { uint32_t magic; struct rkonto k[MAXB]; };
static struct rdatei rd_;
static uint32_t rad_fehler, rad_bis;
static void rad_laden(void) {
    if (rad_geladen) return; rad_geladen = 1; zero(&rd_, sizeof(rd_));
    static struct rdatei t; int r = synfs_read(RDATEI, &t, sizeof(t));
    if (r == (int)sizeof(t) && t.magic == RMAGIC) for (uint32_t i = 0; i < sizeof(t); i++) ((uint8_t*)&rd_)[i] = ((uint8_t*)&t)[i];
}
static int rad_speichern(void) { rd_.magic = RMAGIC; return synfs_write(RDATEI, &rd_, sizeof(rd_)) < 0 ? -1 : 0; }
static int rad_suche(const char* n) { rad_laden(); for (int i = 0; i < MAXB; i++) if (rd_.k[i].used && seq(rd_.k[i].name, n)) return i; return -1; }
int benutzer_rad_hat(const char* name) { return rad_suche(name) >= 0; }
int benutzer_rad_setzen(const char* name, const char* code) {
    if (suche(name) < 0) return -2; if (slen(code) < 6) return -5;
    int i = rad_suche(name); if (i < 0) { for (int k = 0; k < MAXB; k++) if (!rd_.k[k].used) { i = k; break; } if (i < 0) return -3; }
    zero(&rd_.k[i], sizeof(rd_.k[i])); for (uint32_t k = 0; k <= slen(name) && k < 16; k++) rd_.k[i].name[k] = name[k];
    rd_.k[i].used = 1; krand(rd_.k[i].salt, 16); hashen(code, rd_.k[i].salt, rd_.k[i].hash);
    return rad_speichern();
}
int benutzer_rad_entfernen(const char* name) { int i = rad_suche(name); if (i < 0) return 0; rd_.k[i].used = 0; return rad_speichern(); }
static void rad_umbenennen(const char* alt, const char* neu) { int i = rad_suche(alt); if (i < 0) return; zero(rd_.k[i].name, 16); for (uint32_t k = 0; k <= slen(neu) && k < 16; k++) rd_.k[i].name[k] = neu[k]; rad_speichern(); }
int benutzer_rad_sperre(void) { uint32_t t = timer_get_seconds(); return rad_bis > t ? (int)(rad_bis - t) : 0; }
int benutzer_rad_login(const char* name, const char* code) {
    if (benutzer_rad_sperre() > 0) return -7;
    int i = rad_suche(name), k = suche(name);
    uint8_t h[32], dummy[16] = {0}, d = 0;
    if (i < 0 || k < 0) { hashen(code, dummy, h); d = 1; } else { hashen(code, rd_.k[i].salt, h); for (int q = 0; q < 32; q++) d |= h[q] ^ rd_.k[i].hash[q]; }
    if (d) {                                                         /* Fehlversuche: ab dem 3. wachsende Wartezeit (30 s, 60 s, 120 s ... bis 15 min) */
        rad_fehler++;
        if (rad_fehler >= 3) { uint32_t w = 30u << (rad_fehler - 3 > 5 ? 5 : rad_fehler - 3); if (w > 900) w = 900; rad_bis = timer_get_seconds() + w; }
        return -1;
    }
    rad_fehler = 0; aktuell = k; return 0;
}
