/* ZynDB - siehe zyndb.h */
#include "zyndb.h"
#include "crypto.h"
#include "krypto.h"
#include "synfs.h"
#include "timer.h"

extern void kprint(const char* str);
extern void kprint_dec(uint32_t val);

#define KEYL 48
#define VALL 160
#define MAXE 512
#define DIM  64
#define DATEI "zyndb.db"

struct eintrag { char key[KEYL]; char val[VALL]; uint32_t seq, ymd, hm; uint8_t op, pad[3]; };   /* op 1 = gesetzt, 2 = geloescht */
struct kopf { uint8_t magic[4]; uint8_t flags, pad[3]; uint32_t ctr, n, naechste; uint8_t wrap[32]; uint8_t nonce[12]; };   /* flags: 1 = Plattenschluessel */

static struct eintrag E[MAXE]; static uint32_t n, naechste = 1, ctr;
static uint8_t dk[32];
static int offen, defekt;
static uint8_t datei[sizeof(struct kopf) + sizeof(E) + 16];

static uint32_t slen(const char* s) { uint32_t i = 0; while (s[i]) i++; return i; }
static int gleich(const char* a, const char* b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }
static void kopiere(char* d, const char* s, int max) { int i = 0; while (s[i] && i < max - 1) { d[i] = s[i]; i++; } d[i] = 0; }
static void mcp(void* d, const void* s, uint32_t k) { uint8_t* a = d; const uint8_t* b = s; for (uint32_t i = 0; i < k; i++) a[i] = b[i]; }
static void wisch(void* p, uint32_t k) { volatile uint8_t* a = p; for (uint32_t i = 0; i < k; i++) a[i] = 0; }
static char klein(char c) { return c >= 'A' && c <= 'Z' ? (char)(c + 32) : c; }

/* ---------- Schutz ---------- */
static void kek_holen(int flags, uint8_t kek[32]) {
    if (flags & 1) { krypto_ableiten("ZDB-KEK", kek); return; }
    static const char k[] = "ZYNEX-ZDB-OHNE-PLATTENSCHLUESSEL"; sha256(k, 32, kek);
}
static void wrap(int flags, const uint8_t in[32], uint8_t out[32]) {
    uint8_t kek[32], pad[32]; static const uint8_t m[] = "ZDB-WRAP"; kek_holen(flags, kek); hmac_sha256(kek, 32, m, 8, pad);
    for (int i = 0; i < 32; i++) out[i] = in[i] ^ pad[i];
    wisch(kek, 32); wisch(pad, 32);
}
static int speichern(void) {
    if (!offen || defekt || !synfs_is_mounted()) return -1;
    struct kopf* h = (struct kopf*)datei;
    h->magic[0] = 'Z'; h->magic[1] = 'D'; h->magic[2] = 'B'; h->magic[3] = '1';
    h->flags = krypto_aktiv_fuer_aktuelles() ? 1 : 0; h->pad[0] = h->pad[1] = h->pad[2] = 0; h->ctr = ++ctr; h->n = n; h->naechste = naechste;
    wrap(h->flags, dk, h->wrap);
    krand(h->nonce, 12);
    uint32_t pl = n * (uint32_t)sizeof(struct eintrag);
    aead_seal(dk, h->nonce, (const uint8_t*)h, sizeof(struct kopf) - 12, (const uint8_t*)E, pl, datei + sizeof(struct kopf));
    return synfs_write(DATEI, datei, (uint32_t)sizeof(struct kopf) + pl + 16) < 0 ? -1 : 0;
}
void zdb_neu_verpacken(void) { if (offen) speichern(); }

int zdb_init(void) {
    offen = 0; defekt = 0; n = 0; naechste = 1;
    if (!synfs_is_mounted()) return -1;
    int r = synfs_read(DATEI, datei, sizeof(datei));
    if (r <= 0) { krand(dk, 32); offen = 1; return 0; }                   /* neu */
    struct kopf* h = (struct kopf*)datei;
    if ((uint32_t)r < sizeof(struct kopf) + 16 || h->magic[0] != 'Z' || h->magic[1] != 'D' || h->magic[2] != 'B' || h->magic[3] != '1' || h->n > MAXE) { defekt = 1; kprint("[ZynDB] Datei beschaedigt - wird nicht ueberschrieben\n"); return -3; }
    if ((h->flags & 1) && !krypto_aktiv_fuer_aktuelles()) { kprint("[ZynDB] gesperrt (Platte verschluesselt)\n"); return -2; }
    uint32_t pl = h->n * (uint32_t)sizeof(struct eintrag);
    if ((uint32_t)r != sizeof(struct kopf) + pl + 16) { defekt = 1; kprint("[ZynDB] Groesse stimmt nicht - wird nicht ueberschrieben\n"); return -3; }
    wrap(h->flags, h->wrap, dk);
    if (aead_open(dk, h->nonce, (const uint8_t*)h, sizeof(struct kopf) - 12, datei + sizeof(struct kopf), pl + 16, (uint8_t*)E) != 0) {
        wisch(dk, 32); defekt = 1; kprint("[ZynDB] WARNUNG: Pruefsumme falsch - Datei manipuliert oder beschaedigt, wird nicht ueberschrieben\n"); return -3;
    }
    n = h->n; naechste = h->naechste; ctr = h->ctr; offen = 1;
    if (!(h->flags & 1) && krypto_aktiv_fuer_aktuelles()) speichern();     /* Platte inzwischen verschluesselt: Schluessel neu verpacken */
    return 0;
}
int zdb_offen(void) { return offen && !defekt; }
static int bereit(void) { if (!offen && !defekt && synfs_is_mounted()) zdb_init(); return offen && !defekt; }

/* ---------- Daten ---------- */
static int finde(const char* key, int vor) { for (int i = vor - 1; i >= 0; i--) if (gleich(E[i].key, key)) return i; return -1; }
static void zeit(uint32_t* ymd, uint32_t* hm) { if (rtc_get(ymd, hm) != 0 && *ymd) return; *ymd = 0; *hm = timer_get_seconds(); }
static void verdichten(void) {            /* aelteste ueberholte Versionen entfernen, bis Platz ist */
    uint32_t w = 0, frei = MAXE - 64, zu_viel = n > frei ? n - frei : 0;
    for (uint32_t i = 0; i < n; i++) {
        int ueberholt = finde(E[i].key, (int)n) != (int)i;
        if (ueberholt && zu_viel) { zu_viel--; continue; }
        if (w != i) E[w] = E[i];
        w++;
    }
    n = w;
}
static int anhaengen(const char* key, const char* val, uint8_t op) {
    if (slen(key) == 0 || slen(key) >= KEYL) return -1;
    if (n >= MAXE) verdichten();
    if (n >= MAXE) return -2;
    struct eintrag* e = &E[n]; wisch(e, sizeof(*e));
    kopiere(e->key, key, KEYL); kopiere(e->val, val, VALL); e->seq = naechste++; e->op = op; zeit(&e->ymd, &e->hm); n++;
    return speichern();
}
int zdb_put(const char* key, const char* val) { if (!bereit()) return -1; int i = finde(key, (int)n); if (i >= 0 && E[i].op == 1 && gleich(E[i].val, val)) return 0; return anhaengen(key, val, 1); }
const char* zdb_get(const char* key) { if (!bereit()) return 0; int i = finde(key, (int)n); return i >= 0 && E[i].op == 1 ? E[i].val : 0; }
int zdb_geti(const char* key, int def) {
    const char* v = zdb_get(key); if (!v || !*v) return def;
    int neg = 0, r = 0; if (*v == '-') { neg = 1; v++; }
    for (; *v >= '0' && *v <= '9'; v++) r = r * 10 + (*v - '0');
    return neg ? -r : r;
}
int zdb_del(const char* key) { if (!bereit()) return -1; int i = finde(key, (int)n); if (i < 0 || E[i].op != 1) return -3; return anhaengen(key, "", 2); }

/* ---------- Aehnlichkeitssuche ---------- */
static uint32_t fnv(const uint8_t* s, int len) { uint32_t h = 2166136261u; for (int i = 0; i < len; i++) { h ^= s[i]; h *= 16777619u; } return h; }
static void einbetten(const char* a, const char* b, int32_t v[DIM]) {
    char t[KEYL + VALL + 8]; int l = 0; t[l++] = ' ';
    for (const char* p = a; *p && l < (int)sizeof(t) - 3; p++) t[l++] = (*p == '.' || *p == '_' || *p == '-') ? ' ' : klein(*p);
    t[l++] = ' ';
    for (const char* p = b; *p && l < (int)sizeof(t) - 2; p++) t[l++] = (*p == '.' || *p == '_' || *p == '-') ? ' ' : klein(*p);
    t[l++] = ' ';
    for (int i = 0; i < DIM; i++) v[i] = 0;
    for (int i = 0; i + 3 <= l; i++) { uint32_t h = fnv((const uint8_t*)t + i, 3); v[h % DIM] += (h >> 12) & 1 ? 1 : -1; }
    for (int i = 0; i < l; ) {                                       /* ganze Woerter zaehlen staerker */
        while (i < l && t[i] == ' ') i++;
        int s = i; while (i < l && t[i] != ' ') i++;
        if (i - s >= 2) { uint32_t h = fnv((const uint8_t*)t + s, i - s) * 2654435761u; v[(h >> 8) % DIM] += ((h >> 20) & 1 ? 3 : -3); }
    }
}
static uint32_t wurzel(uint64_t x) { uint64_t r = 0, b = 1ull << 62; while (b > x) b >>= 2; while (b) { if (x >= r + b) { x -= r + b; r = (r >> 1) + b; } else r >>= 1; b >>= 2; } return (uint32_t)r; }
static int aehnlichkeit(const int32_t* q, const int32_t* d) {      /* 0..100 (Prozent Kosinus) */
    int64_t dot = 0, nq = 0, nd = 0;
    for (int i = 0; i < DIM; i++) { dot += (int64_t)q[i] * d[i]; nq += (int64_t)q[i] * q[i]; nd += (int64_t)d[i] * d[i]; }
    if (dot <= 0 || !nq || !nd) return 0;
    return (int)wurzel((uint64_t)dot * (uint64_t)dot * 10000 / ((uint64_t)nq * (uint64_t)nd));
}
static void zeige(const struct eintrag* e) { kprint("  "); kprint(e->key); kprint(" = "); kprint(e->val); kprint("\n"); }
static int suche(const char* text) {
    int32_t q[DIM], d[DIM]; einbetten("", text, q);
    int best[5], bs[5], nb = 0;
    for (int i = 0; i < (int)n; i++) {
        if (E[i].op != 1 || finde(E[i].key, (int)n) != i) continue;
        einbetten(E[i].key, E[i].val, d); int s = aehnlichkeit(q, d);
        if (s < 30) continue;
        int p; if (nb < 5) p = nb++; else if (s > bs[4]) p = 4; else continue;
        best[p] = i; bs[p] = s;
        for (int k = p; k > 0 && bs[k] > bs[k - 1]; k--) { int t1 = bs[k]; bs[k] = bs[k - 1]; bs[k - 1] = t1; t1 = best[k]; best[k] = best[k - 1]; best[k - 1] = t1; }
    }
    for (int k = 0; k < nb; k++) { kprint("  "); kprint_dec((uint32_t)bs[k]); kprint(" %  "); kprint(E[best[k]].key); kprint(" = "); kprint(E[best[k]].val); kprint("\n"); }
    return nb;
}

/* ---------- Shell ---------- */
static const char* skip(const char* s) { while (*s == ' ') s++; return s; }
static const char* wort(const char* s, char* o, int max) { s = skip(s); int i = 0; while (*s && *s != ' ' && i < max - 1) o[i++] = *s++; o[i] = 0; return s; }
static void datum(const struct eintrag* e) { if (e->ymd) { kprint_dec(e->ymd % 100); kprint("."); kprint_dec(e->ymd / 100 % 100); kprint("."); kprint_dec(e->ymd / 10000); kprint(" "); kprint_dec(e->hm / 100); kprint(":"); if (e->hm % 100 < 10) kprint("0"); kprint_dec(e->hm % 100); } else { kprint("nach "); kprint_dec(e->hm); kprint(" s"); } }
void zdb_befehl(const char* a) {
    char w[16], k[KEYL + 8]; a = wort(a, w, sizeof(w));
    if (!w[0] || gleich(w, "status")) {
        uint32_t lebend = 0; for (uint32_t i = 0; i < n; i++) if (E[i].op == 1 && finde(E[i].key, (int)n) == (int)i) lebend++;
        kprint("ZynDB: "); if (defekt) { kprint("BESCHAEDIGT oder manipuliert - gesperrt\n"); return; }
        if (!bereit()) { kprint("nicht verfuegbar (SynFS gesperrt oder nicht eingebunden)\n"); return; }
        kprint_dec(lebend); kprint(" Fakten, "); kprint_dec(n); kprint(" Versionen gespeichert\n");
        kprint(krypto_aktiv_fuer_aktuelles() ? "  Schutz: verschluesselt und manipulationssicher (ChaCha20-Poly1305, Schluessel an die Platte gebunden)\n"
                                              : "  Schutz: nur Verschleierung - die Platte ist nicht verschluesselt (krypto aktivieren)\n");
        kprint("  Befehle: db setze <name> <wert> | hole | liste [anfang] | suche <text> | verlauf <name> | zurueck <name> | loesche <name> | pruefen\n"); return;
    }
    if (!bereit()) { kprint("ZynDB nicht verfuegbar\n"); return; }
    if (gleich(w, "setze") || gleich(w, "set")) {
        a = wort(a, k, sizeof(k)); a = skip(a);
        if (!k[0] || !*a) { kprint("db setze <name> <wert>\n"); return; }
        int r = zdb_put(k, a); kprint(r == 0 ? "Gespeichert\n" : r == -2 ? "Voll\n" : r == -1 ? "Ungueltiger Name oder Speichern fehlgeschlagen\n" : "Fehler\n"); return;
    }
    if (gleich(w, "hole") || gleich(w, "get")) { wort(a, k, sizeof(k)); const char* v = zdb_get(k); if (v) { kprint(v); kprint("\n"); } else kprint("Nicht vorhanden\n"); return; }
    if (gleich(w, "liste") || gleich(w, "list")) {
        wort(a, k, sizeof(k)); int c = 0, kl = (int)slen(k);
        for (int i = 0; i < (int)n; i++) { if (E[i].op != 1 || finde(E[i].key, (int)n) != i) continue; int ok = 1; for (int j = 0; j < kl; j++) if (E[i].key[j] != k[j]) ok = 0; if (ok) { zeige(&E[i]); c++; } }
        if (!c) kprint("Keine Fakten\n"); return;
    }
    if (gleich(w, "suche") || gleich(w, "frage") || gleich(w, "find")) { a = skip(a); if (!*a) { kprint("db suche <was du suchst>\n"); return; } if (!suche(a)) kprint("Nichts Aehnliches gefunden\n"); return; }
    if (gleich(w, "verlauf") || gleich(w, "history")) {
        wort(a, k, sizeof(k)); int c = 0;
        for (int i = (int)n - 1; i >= 0 && c < 8; i--) if (gleich(E[i].key, k)) { kprint("  v"); kprint_dec(E[i].seq); kprint("  "); datum(&E[i]); kprint("  "); kprint(E[i].op == 1 ? E[i].val : "(geloescht)"); kprint("\n"); c++; }
        if (!c) kprint("Nicht vorhanden\n"); return;
    }
    if (gleich(w, "zurueck") || gleich(w, "undo")) {
        wort(a, k, sizeof(k)); int i1 = finde(k, (int)n), i2 = i1 >= 0 ? finde(k, i1) : -1;
        if (i1 >= 0 && E[i1].op == 2) { int i0 = i2; if (i0 >= 0 && E[i0].op == 1) { char v[VALL]; kopiere(v, E[i0].val, VALL); anhaengen(k, v, 1); kprint("Wiederhergestellt\n"); return; } }
        if (i1 < 0 || i2 < 0 || E[i2].op != 1) { kprint("Keine fruehere Version\n"); return; }
        char v[VALL]; kopiere(v, E[i2].val, VALL); anhaengen(k, v, 1); kprint("Auf die vorige Version zurueckgesetzt: "); kprint(v); kprint("\n"); return;
    }
    if (gleich(w, "loesche") || gleich(w, "del")) { wort(a, k, sizeof(k)); kprint(zdb_del(k) == 0 ? "Geloescht (im Verlauf bleibt es, 'db zurueck' holt es wieder)\n" : "Nicht vorhanden\n"); return; }
    if (gleich(w, "pruefen") || gleich(w, "check")) { int r = synfs_read(DATEI, datei, sizeof(datei)); struct kopf* h = (struct kopf*)datei; uint8_t k2[32]; uint32_t pl = r > 0 ? h->n * (uint32_t)sizeof(struct eintrag) : 0;
        if (r <= 0) { kprint("Noch nichts gespeichert\n"); return; }
        wrap(h->flags, h->wrap, k2); static struct eintrag T[MAXE];
        kprint(aead_open(k2, h->nonce, datei, sizeof(struct kopf) - 12, datei + sizeof(struct kopf), pl + 16, (uint8_t*)T) == 0 ? "Pruefsumme in Ordnung - nichts veraendert\n" : "FEHLER: Datei veraendert oder beschaedigt\n"); wisch(k2, 32); return; }
    kprint("db: setze | hole | liste | suche | verlauf | zurueck | loesche | pruefen | status\n");
}
