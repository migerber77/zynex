/* Zynex OS - Raeume und Schluessel (siehe include/raeume.h) */
#include "raeume.h"
#include "synfs.h"
#include "benutzer.h"

#define GR_MAX 96
#define ZU_MAX 192
#define RMAGIC 0x52414531u            /* 'RAE1' */

struct raum { uint8_t id, parent, used, sicht; char name[RA_NAME]; char besitzer[16]; };
struct grant { uint8_t raum, stufe, used, pad; char wer[16]; };
struct zuo { char name[32]; uint8_t raum, used, pad[2]; };
struct bestand { uint32_t magic; struct raum r[RA_MAX]; struct grant g[GR_MAX]; struct zuo z[ZU_MAX]; };
static struct bestand B; static int geladen;

static int slen(const char* s) { int n = 0; while (s[n]) n++; return n; }
static int seq(const char* a, const char* b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }
static void scp(char* d, const char* s, int m) { int i = 0; while (s && s[i] && i < m - 1) { d[i] = s[i]; i++; } d[i] = 0; }
static void speichern(void) { if (synfs_is_mounted()) synfs_write("sys.raeume", &B, sizeof(B)); }

void raum_laden(void) {
    for (uint32_t i = 0; i < sizeof(B); i++) ((uint8_t*)&B)[i] = 0;
    geladen = 1;
    if (!synfs_is_mounted()) return;
    static struct bestand t;
    int n = synfs_read("sys.raeume", &t, sizeof(t));
    if (n == (int)sizeof(t) && t.magic == RMAGIC) { for (uint32_t i = 0; i < sizeof(B); i++) ((uint8_t*)&B)[i] = ((uint8_t*)&t)[i]; } else B.magic = RMAGIC;
}
static void sicher(void) { if (!geladen) raum_laden(); }
static struct raum* rp(int id) { sicher(); if (id <= 0 || id >= RA_MAX) return 0; return B.r[id].used ? &B.r[id] : 0; }

const char* raum_name(int id) { if (id == 0) return "Zynex"; struct raum* r = rp(id); return r ? r->name : 0; }
const char* raum_besitzer(int id) { struct raum* r = rp(id); return r ? r->besitzer : ""; }
int raum_parent(int id) { if (id == 0) return -1; struct raum* r = rp(id); return r ? r->parent : -1; }
int raum_pfad(int id, char* out, int max) {
    int kette[10], n = 0;
    while (id > 0 && n < 10) { kette[n++] = id; id = raum_parent(id); if (id < 0) break; }
    int o = 0; scp(out, "Zynex", max); o = slen(out);
    for (int i = n - 1; i >= 0; i--) { const char* nm = raum_name(kette[i]); if (!nm || o + 2 >= max) break; out[o++] = '/'; for (int k = 0; nm[k] && o + 1 < max; k++) out[o++] = nm[k]; out[o] = 0; }
    return o;
}

static struct grant* gp(int raum, const char* wer) { for (int i = 0; i < GR_MAX; i++) if (B.g[i].used && B.g[i].raum == raum && seq(B.g[i].wer, wer)) return &B.g[i]; return 0; }

int raum_stufe(int id, const char* wer, int rolle) {
    sicher();
    if (rolle == ROLLE_ADMIN) return ST_VERW;                    /* Administratoren und Einrichtungsmodus */
    int s = -1;
    for (int r = id, tiefe = 0; r >= 0 && tiefe < 12; tiefe++) {
        if (r > 0) { struct raum* p = rp(r); if (!p) break; if (wer[0] && seq(p->besitzer, wer)) { s = ST_VERW; break; } }
        struct grant* g = wer[0] ? gp(r, wer) : 0; if (!g) g = gp(r, "*");
        if (g) { s = g->stufe; break; }
        r = r == 0 ? -1 : raum_parent(r);
    }
    if (s < 0) s = rolle == ROLLE_GAST ? ST_SEHEN : ST_MITARB;        /* Wurzel ohne Eintrag */
    if (rolle == ROLLE_GAST && s > ST_SEHEN) s = ST_SEHEN;            /* Rolle als Deckel */
    return s;
}
int raum_unter(int parent, int n, const char* wer, int rolle) {
    sicher();
    for (int i = 1; i < RA_MAX; i++) if (B.r[i].used && B.r[i].parent == parent && raum_stufe(i, wer, rolle) >= ST_SEHEN) { if (n-- == 0) return i; }
    return -1;
}
int raum_neu(int parent, const char* name, const char* besitzer, int rolle) {
    sicher();
    int l = slen(name); if (l < 1) return -1;
    for (int i = 0; i < l; i++) if (name[i] == '/' || name[i] == ':' || (unsigned char)name[i] < 32) return -1;
    if (parent != 0 && !rp(parent)) return -1;
    if (raum_stufe(parent, besitzer, rolle) < ST_MITARB) return -4;
    for (int i = 1; i < RA_MAX; i++) if (B.r[i].used && B.r[i].parent == parent && seq(B.r[i].name, name)) return -3;
    for (int i = 1; i < RA_MAX; i++) if (!B.r[i].used) {
        B.r[i].used = 1; B.r[i].id = (uint8_t)i; B.r[i].parent = (uint8_t)parent; B.r[i].sicht = 0; scp(B.r[i].name, name, RA_NAME); scp(B.r[i].besitzer, besitzer, 16);
        if (besitzer[0]) raum_sicht_setzen(i, 0);                                   /* privat */
        speichern(); return i;
    }
    return -2;
}
int raum_loeschen(int id, const char* wer, int rolle) {
    struct raum* r = rp(id); if (!r) return -1;
    if (raum_stufe(id, wer, rolle) < ST_VERW) return -2;
    for (int i = 1; i < RA_MAX; i++) if (B.r[i].used && B.r[i].parent == id) return -1;
    if (datei_zahl(id, (const char* const[]){ "", 0 }) > 0) return -1;
    for (int i = 0; i < GR_MAX; i++) if (B.g[i].used && B.g[i].raum == id) B.g[i].used = 0;
    r->used = 0; speichern(); return 0;
}
int raum_sicht(int id) { sicher(); struct grant* g = gp(id, "*"); return !g ? 2 : g->stufe == 0 ? 0 : g->stufe == 1 ? 1 : 2; }
int raum_schluessel(int id, const char* wer) { sicher(); struct grant* g = gp(id, wer); return g ? g->stufe : -1; }
void raum_schluessel_setzen(int id, const char* wer, int stufe) {
    sicher(); struct grant* g = gp(id, wer);
    if (stufe < 0) { if (g) g->used = 0; speichern(); return; }
    if (!g) { for (int i = 0; i < GR_MAX; i++) if (!B.g[i].used) { g = &B.g[i]; break; } if (!g) return; g->used = 1; g->raum = (uint8_t)id; scp(g->wer, wer, 16); }
    g->stufe = (uint8_t)stufe; speichern();
}
void raum_sicht_setzen(int id, int s) { raum_schluessel_setzen(id, "*", s == 0 ? ST_GESPERRT : s == 1 ? ST_SEHEN : ST_MITARB); }

int datei_raum(const char* name) { sicher(); for (int i = 0; i < ZU_MAX; i++) if (B.z[i].used && seq(B.z[i].name, name)) return rp(B.z[i].raum) || B.z[i].raum == 0 ? B.z[i].raum : 0; return 0; }
void datei_zuordnen(const char* name, int raum) {
    sicher(); int frei = -1;
    for (int i = 0; i < ZU_MAX; i++) { if (B.z[i].used && seq(B.z[i].name, name)) { if (raum == 0) B.z[i].used = 0; else B.z[i].raum = (uint8_t)raum; speichern(); return; } if (!B.z[i].used && frei < 0) frei = i; }
    if (raum == 0 || frei < 0) return;
    B.z[frei].used = 1; scp(B.z[frei].name, name, 32); B.z[frei].raum = (uint8_t)raum; speichern();
}
void datei_vergessen(const char* name) { datei_zuordnen(name, 0); }
int datei_nth(int raum, const char* const* endungen, int n, char* out, int max) {
    char nm[32];
    for (int k = 0; synfs_nte_datei(k, endungen, nm, 32); k++) {
        if (nm[0] == 's' && nm[1] == 'y' && nm[2] == 's' && nm[3] == '.') continue;            /* Systemdateien bleiben unsichtbar */
        if (nm[0] == 'b' && nm[1] == 'f' && nm[2] == '.') continue;
        if (datei_raum(nm) != raum) continue;
        if (n-- == 0) { scp(out, nm, max); return 1; }
    }
    return 0;
}
int datei_zahl(int raum, const char* const* endungen) { char t[32]; int n = 0; while (datei_nth(raum, endungen, n, t, 32)) n++; return n; }
