/* Chat-Verlauf, siehe chats.h */
#include "chats.h"
#include "zyndb.h"

#define MAXC 28
#define BLOB 3000

static int slen(const char* s) { int n = 0; while (s[n]) n++; return n; }
static int gl(const char* a, const char* b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }
static void kop(char* d, const char* s, int max) { int i = 0; while (s[i] && i < max - 1) { d[i] = s[i]; i++; } d[i] = 0; }
static void zahl(char* o, int v) { char t[12]; int n = 0; if (v <= 0) t[n++] = '0'; while (v > 0) { t[n++] = (char)('0' + v % 10); v /= 10; } int k = 0; while (n) o[k++] = t[--n]; o[k] = 0; }
static void key(char* k, int id, const char* f) { int l = 0; const char* p = "chat."; while (*p) k[l++] = *p++; char z[12]; zahl(z, id); for (int i = 0; z[i]; i++) k[l++] = z[i]; k[l++] = '.'; while (*f) k[l++] = *f++; k[l] = 0; }

static int ids_lesen(int* ids, int max) {
    const char* s = zdb_get("chat.liste"); int n = 0; if (!s) return 0;
    while (*s && n < max) { while (*s && (*s < '0' || *s > '9')) s++; if (!*s) break; int v = 0; while (*s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0'); ids[n++] = v; }
    return n;
}
static void ids_schreiben(const int* ids, int n) {
    char b[160]; int l = 0; b[0] = 0;
    for (int i = 0; i < n && l < 150; i++) { char z[12]; zahl(z, ids[i]); if (i) b[l++] = ','; for (int k = 0; z[k]; k++) b[l++] = z[k]; }
    b[l] = 0; zdb_put("chat.liste", b);
}
static int vorhanden(int id) { int ids[MAXC + 2], n = ids_lesen(ids, MAXC + 2); for (int i = 0; i < n; i++) if (ids[i] == id) return 1; return 0; }

int chat_aktiv(void) { int a = zdb_geti("chat.aktiv", 0); return a > 0 && vorhanden(a) ? a : 0; }
void chat_waehle(int id) { char z[12]; zahl(z, id); zdb_put("chat.aktiv", z); }
const char* chat_titel(int id) { char k[32]; key(k, id, "t"); const char* v = zdb_get(k); return v ? v : ""; }
const char* chat_projekt(int id) { char k[32]; key(k, id, "p"); const char* v = zdb_get(k); return v ? v : ""; }
int chat_gepinnt(int id) { char k[32]; key(k, id, "pin"); const char* v = zdb_get(k); return v && v[0] == '1'; }
void chat_pin_umschalten(int id) { char k[32]; key(k, id, "pin"); zdb_put(k, chat_gepinnt(id) ? "0" : "1"); }
void chat_projekt_setze(int id, const char* name) { char k[32], v[20]; key(k, id, "p"); kop(v, name, 20); zdb_put(k, v); }

int chat_neu(const char* titel) {
    int ids[MAXC + 2], n = ids_lesen(ids, MAXC + 2);
    if (n >= MAXC) { int w = -1; for (int i = 0; i < n; i++) if (!chat_gepinnt(ids[i])) { w = i; break; } if (w < 0) return -1; chat_loeschen(ids[w]); n = ids_lesen(ids, MAXC + 2); }
    int id = zdb_geti("chat.zaehler", 0) + 1; char z[12], t[40], k[32];
    zdb_stapel(1);
    zahl(z, id); zdb_put("chat.zaehler", z);
    ids[n++] = id; ids_schreiben(ids, n);
    kop(t, titel && titel[0] ? titel : "Neuer Chat", 30); key(k, id, "t"); zdb_put(k, t);
    chat_waehle(id);
    zdb_stapel(0);
    return id;
}
int chat_loeschen(int id) {
    int ids[MAXC + 2], n = ids_lesen(ids, MAXC + 2), w = 0, da = 0;
    for (int i = 0; i < n; i++) { if (ids[i] == id) { da = 1; continue; } ids[w++] = ids[i]; }
    if (!da) return -1;
    char k[32]; zdb_stapel(1);
    ids_schreiben(ids, w);
    key(k, id, "t"); zdb_del(k); key(k, id, "p"); zdb_del(k); key(k, id, "pin"); zdb_del(k); key(k, id, "m"); zdb_put_text(k, "", 0);
    if (zdb_geti("chat.aktiv", 0) == id) { char z[12]; zahl(z, w ? ids[w - 1] : 0); zdb_put("chat.aktiv", z); }
    zdb_stapel(0);
    return 0;
}
int chat_liste(int* out, int max, const char* projekt) {
    int ids[MAXC + 2], n = ids_lesen(ids, MAXC + 2), o = 0;
    for (int i = 0; i < n; i++) for (int j = i + 1; j < n; j++) if (ids[j] > ids[i]) { int t = ids[i]; ids[i] = ids[j]; ids[j] = t; }     /* neueste zuerst */
    for (int pass = 0; pass < 2; pass++) for (int i = 0; i < n && o < max; i++) {
        if ((chat_gepinnt(ids[i]) ? 0 : 1) != pass) continue;
        if (projekt && projekt[0] && !gl(chat_projekt(ids[i]), projekt)) continue;
        out[o++] = ids[i];
    }
    return o;
}
int chat_projekte(char names[][20], int max) {
    int ids[MAXC + 2], n = ids_lesen(ids, MAXC + 2), o = 0;
    for (int i = 0; i < n; i++) { const char* p = chat_projekt(ids[i]); if (!p[0]) continue; int da = 0; for (int j = 0; j < o; j++) if (gl(names[j], p)) da = 1; if (!da && o < max) { kop(names[o], p, 20); o++; } }
    return o;
}
int chat_text(int id, char* out, int max) { char k[32]; key(k, id, "m"); int r = zdb_get_text(k, out, max); if (r < 0) { out[0] = 0; return 0; } return r; }
void chat_anhaengen(int id, char rolle, const char* text) {
    static char b[BLOB + 260]; char k[32]; key(k, id, "m");
    int l = chat_text(id, b, BLOB);
    int tl = slen(text); if (tl > 240) tl = 240;
    b[l++] = rolle; b[l++] = ':'; for (int i = 0; i < tl; i++) b[l++] = text[i] == '\n' ? ' ' : text[i]; b[l++] = '\n'; b[l] = 0;
    while (l > BLOB) { int e = 0; while (b[e] && b[e] != '\n') e++; if (!b[e]) break; e++; for (int i = e; i <= l; i++) b[i - e] = b[i]; l -= e; }
    zdb_put_text(k, b, l);
}
void chat_log(const char* frage, const char* antwort) {
    if (!frage || !frage[0]) return;
    int id = chat_aktiv();
    if (!id) { char t[32]; kop(t, frage, 29); id = chat_neu(t); if (id < 0) return; }
    else if (gl(chat_titel(id), "Neuer Chat")) { char t[32], k[32]; kop(t, frage, 29); key(k, id, "t"); zdb_put(k, t); }
    zdb_stapel(1);
    chat_anhaengen(id, 'U', frage);
    if (antwort && antwort[0]) chat_anhaengen(id, 'A', antwort);
    zdb_stapel(0);
}
