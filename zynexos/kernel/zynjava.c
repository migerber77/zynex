/* ZynJava: Uebersetzer (Java-Teilmenge -> Bytecode) und Stapelmaschine, siehe include/zynjava.h */
#include "zynjava.h"
#include "font8x16.h"

extern void kprint(const char* str);
extern void kprint_dec(uint32_t v);
extern uint32_t timer_get_ticks(void);

/* ---------- Typen und Befehle ---------- */
enum { T_INT = 0, T_STR, T_ARR, T_ARR2, T_VOID };
#define T_OBJ0 16            /* Objekt der Klasse cid = 16+cid; Feld solcher Objekte = 128+cid */
#define T_OARR0 128
#define TAG_ARR2 1
#define TAG_OBJ 0x10
#define TAG_OARR 0x40
#define MAXF 32
#define NCL_MAX 16
static int is_ref(int ty) { return ty == T_ARR || ty == T_ARR2 || ty >= T_OBJ0; }
enum {
    O_PUSH, O_LDL, O_STL, O_LDG, O_STG, O_AGET, O_ASETV, O_ALEN, O_NEWA, O_NEWA2, O_DUP, O_DUP2, O_POP,
    O_ADD, O_SUB, O_MUL, O_DIV, O_MOD, O_AND, O_OR, O_XOR, O_SHL, O_SHR, O_USHR,
    O_LT, O_LE, O_GT, O_GE, O_EQ, O_NE, O_NEG, O_NOT, O_BNOT,
    O_SCAT, O_ITOS, O_SEQ, O_JMP, O_JZ, O_JNZ, O_CALLM, O_CALL, O_RET, O_RETV, O_BI, O_ALIT, O_HALT, O_NEWO, O_GETF, O_SETF, O_INSTOF
};
enum {
    B_COLOR1, B_COLOR3, B_FILLRECT, B_DRAWRECT, B_LINE, B_FILLOVAL, B_DRAWOVAL, B_STRING, B_FONT, B_WIDTH, B_HEIGHT, B_PIXEL, B_NOP0, B_NOP1,
    B_ABS, B_MIN, B_MAX, B_SQRT, B_SIN, B_COS, B_RND, B_POW,
    B_SLEN, B_CHARAT, B_SUBSTR, B_INDEXOF, B_ISEMPTY, B_UPPER, B_LOWER, B_PARSE, B_PRINT, B_MILLIS, B_GETX, B_GETY, B_GETKEY, B_RGB, B_CLEARALL, B_TOCHR
};

#define CODE_MAX 4096                 /* Befehle je Programm */
#define STK_MAX 4096
#define HEAP_MAX 65536
#define SPOOL_MAX 48000
#define NSTR_MAX 4096
#define NFN_MAX 160
#define NG_MAX 128
#define STEPS 600000

struct fn { char name[44]; int pc, nargs, nloc, ret, def; int cls; int stat, synth; unsigned mh; };
struct cls {
    char name[20]; int parent, isif, flat, nown, ntotal, ppos, ifpos[3], ifc[3];
    char fname[MAXF][20]; uint8_t ftype[MAXF]; uint8_t ftot[MAXF];
};
struct zjv {
    int ok, ladet; char fehler[120];
    int w, h; uint32_t px[ZJ_MAXW * ZJ_MAXH];
    int code[CODE_MAX * 2]; int ncode, init_pc;
    struct fn fns[NFN_MAX]; int nfn;
    char gname[NG_MAX][44]; uint8_t gtype[NG_MAX]; int g[NG_MAX]; int ng;
    int stk[STK_MAX]; int sp, fp;
    int heap[HEAP_MAX]; int hp;
    char spool[SPOOL_MAX]; int soff[NSTR_MAX]; int nstr, nconst, spn, spconst;
    struct cls cl[NCL_MAX]; int ncl;
    uint32_t farbe; int fscale; uint32_t rnd; int ex, ey, ekey;
    int f_start, f_paint, f_tick, f_down, f_up, f_key, f_move;
};
static struct zjv vm_[ZJ_MAX];
const char* zj_fehler(int s) { return (s >= 0 && s < ZJ_MAX) ? vm_[s].fehler : "?"; }
const uint32_t* zj_pixel(int s) { return vm_[s].px; }

/* ---------- Hilfen ---------- */
static int slen(const char* s) { int n = 0; while (s[n]) n++; return n; }
static void kop(char* d, const char* s, int m) { int i = 0; while (s[i] && i < m - 1) { d[i] = s[i]; i++; } d[i] = 0; }
static int gl(const char* a, const char* b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }
static void dezi(char* o, int v) { char t[12]; int n = 0, neg = v < 0; unsigned u = neg ? (unsigned)(-(long long)v) : (unsigned)v; do { t[n++] = (char)('0' + u % 10); u /= 10; } while (u); int k = 0; if (neg) o[k++] = '-'; while (n) o[k++] = t[--n]; o[k] = 0; }

/* ---------- Zeichnen in die Flaeche ---------- */
static void px_(struct zjv* v, int x, int y, uint32_t c) { if (x >= 0 && y >= 0 && x < v->w && y < v->h) v->px[y * v->w + x] = c; }
static void fillr(struct zjv* v, int x, int y, int w, int h, uint32_t c) {
    if (w < 0) { x += w; w = -w; } if (h < 0) { y += h; h = -h; }
    int x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y, x1 = x + w > v->w ? v->w : x + w, y1 = y + h > v->h ? v->h : y + h;
    for (int j = y0; j < y1; j++) for (int i = x0; i < x1; i++) v->px[j * v->w + i] = c;
}
static void line_(struct zjv* v, int x0, int y0, int x1, int y1, uint32_t c) {
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, dy = y1 > y0 ? y0 - y1 : y1 - y0, sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, e = dx + dy;
    for (int n = 0; n < 4000; n++) { px_(v, x0, y0, c); if (x0 == x1 && y0 == y1) break; int e2 = 2 * e; if (e2 >= dy) { e += dy; x0 += sx; } if (e2 <= dx) { e += dx; y0 += sy; } }
}
static void oval_(struct zjv* v, int x, int y, int w, int h, uint32_t c, int fuell) {
    if (w <= 0 || h <= 0) return;
    int rx = w / 2, ry = h / 2, cx = x + rx, cy = y + ry; if (rx < 1 || ry < 1) { fillr(v, x, y, w, h, c); return; }
    long long rx2 = (long long)rx * rx, ry2 = (long long)ry * ry;
    for (int j = -ry; j <= ry; j++) {
        long long t = rx2 * (ry2 - (long long)j * j) / ry2; int dx = 0; while ((long long)(dx + 1) * (dx + 1) <= t) dx++;
        if (fuell) fillr(v, cx - dx, cy + j, 2 * dx + 1, 1, c);
        else { int dn = dx; long long t2 = rx2 * (ry2 - (long long)(j + 1 > ry ? ry : j + 1) * (j + 1 > ry ? ry : j + 1)) / ry2; int d2 = 0; while ((long long)(d2 + 1) * (d2 + 1) <= t2) d2++; if (j == ry || j == -ry) d2 = 0;
               for (int k = d2; k <= dn; k++) { px_(v, cx - k, cy + j, c); px_(v, cx + k, cy + j, c); } if (j == -ry || j == ry) fillr(v, cx - dn, cy + j, 2 * dn + 1, 1, c); }
    }
}
static void text_(struct zjv* v, const char* s, int x, int y, uint32_t c) {
    int sc = v->fscale < 1 ? 1 : v->fscale;
    for (; *s; s++) {
        unsigned char ch = (unsigned char)*s; const uint8_t* gph = 0;
        if (ch >= 32 && ch < 127) gph = font8x16[ch - 32]; else if (ch >= 0xA0) gph = font8x16_l1[ch - 0xA0];
        if (gph) for (int j = 0; j < 16; j++) { uint8_t b = gph[j]; if (!b) continue; for (int i = 0; i < 8; i++) if (b & (0x80 >> i)) fillr(v, x + i * sc, y + j * sc, sc, sc, c); }
        x += 8 * sc;
    }
}

/* ---------- Zeichenketten und Felder zur Laufzeit ---------- */
static int s_neu(struct zjv* v, const char* t, int n) {
    if (v->nstr >= NSTR_MAX || v->spn + n + 1 > SPOOL_MAX) return 0;
    int h = v->nstr++; v->soff[h] = v->spn; for (int i = 0; i < n; i++) v->spool[v->spn++] = t[i]; v->spool[v->spn++] = 0; return h;
}
static const char* s_text(struct zjv* v, int h) { return (h > 0 && h < v->nstr) ? v->spool + v->soff[h] : ""; }
static void gc(struct zjv* v);

/* ====================== Uebersetzer ====================== */
enum { TK_END, TK_ID, TK_NUM, TK_STR, TK_OP };
struct tok { uint8_t t; int16_t line; int v; };
#define TOK_MAX 7000
static struct tok tk[TOK_MAX]; static int ntk, tp;
static char idpool[12000]; static int nid;
static char cerr[120]; static int cerr_zeile;
static struct zjv* V;

#define OP2(a, b) ((a) | ((b) << 8))
#define OP3(a, b, c) ((a) | ((b) << 8) | ((c) << 16))
static void fehler(const char* m) { if (!cerr[0]) { kop(cerr, m, 100); cerr_zeile = tk[tp < ntk ? tp : ntk - 1].line; } }
static int str_const(const char* t, int n) { int h = s_neu(V, t, n); if (!h) fehler("Zu viele oder zu lange Texte"); return h; }

static int lex(const char* s) {
    int line = 1; ntk = 0; idpool[0] = 0; idpool[1] = 't'; idpool[2] = 'h'; idpool[3] = 'i'; idpool[4] = 's'; idpool[5] = 0; nid = 6;
    while (*s) {
        char c = *s;
        if (c == '\n') { line++; s++; continue; }
        if (c == ' ' || c == '\t' || c == '\r') { s++; continue; }
        if (c == '/' && s[1] == '/') { while (*s && *s != '\n') s++; continue; }
        if (c == '/' && s[1] == '*') { s += 2; while (*s && !(s[0] == '*' && s[1] == '/')) { if (*s == '\n') line++; s++; } if (*s) s += 2; continue; }
        if (ntk >= TOK_MAX - 2) { kop(cerr, "Programm zu lang", 100); return -1; }
        struct tok* t = &tk[ntk]; t->line = (int16_t)line;
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '$') {
            t->t = TK_ID; t->v = nid; while ((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') || *s == '_' || *s == '$' || (*s >= '0' && *s <= '9')) { if (nid < (int)sizeof(idpool) - 2) idpool[nid++] = *s; s++; }
            idpool[nid++] = 0; ntk++; continue;
        }
        if (c >= '0' && c <= '9') {
            long long v = 0;
            if (c == '0' && (s[1] == 'x' || s[1] == 'X')) { s += 2; while ((*s >= '0' && *s <= '9') || ((*s | 32) >= 'a' && (*s | 32) <= 'f')) { int d = *s <= '9' ? *s - '0' : (*s | 32) - 'a' + 10; v = v * 16 + d; s++; } }
            else { while (*s >= '0' && *s <= '9') { v = v * 10 + (*s - '0'); s++; } if (*s == '.' ) { kop(cerr, "Kommazahlen werden nicht unterstuetzt (nur ganze Zahlen)", 100); return -1; } }
            if (*s == 'L' || *s == 'l') s++;
            t->t = TK_NUM; t->v = (int)v; ntk++; continue;
        }
        if (c == '\'') {
            s++; int v = (unsigned char)*s;
            if (*s == '\\') { s++; v = *s == 'n' ? 10 : *s == 't' ? 9 : *s == 'r' ? 13 : *s == '0' ? 0 : (unsigned char)*s; }
            s++; if (*s == '\'') s++;
            t->t = TK_NUM; t->v = v; ntk++; continue;
        }
        if (c == '"') {
            s++; static char b[400]; int n = 0;
            while (*s && *s != '"' && *s != '\n') { char ch = *s; if (ch == '\\') { s++; ch = *s == 'n' ? '\n' : *s == 't' ? '\t' : *s; } if (n < 399) b[n++] = ch; s++; }
            if (*s == '"') s++;
            t->t = TK_STR; t->v = str_const(b, n); ntk++; continue;
        }
        int o = (unsigned char)c; s++;
        if ((c == '=' || c == '!' || c == '<' || c == '>' || c == '+' || c == '-' || c == '*' || c == '/' || c == '%' || c == '&' || c == '|' || c == '^') && *s) {
            if (*s == '=' && c != '&' && c != '|') { o = OP2(c, '='); s++; }
            else if ((c == '+' && *s == '+') || (c == '-' && *s == '-') || (c == '&' && *s == '&') || (c == '|' && *s == '|')) { o = OP2(c, *s); s++; }
            else if (c == '<' && *s == '<') { s++; if (*s == '=') { s++; o = OP3('<', '<', '='); } else o = OP2('<', '<'); }
            else if (c == '>' && *s == '>') { s++; if (*s == '>') { s++; o = OP3('>', '>', '>'); } else if (*s == '=') { s++; o = OP3('>', '>', '='); } else o = OP2('>', '>'); }
            else if ((c == '&' || c == '|' || c == '^') && *s == '=') { o = OP2(c, '='); s++; }
        }
        t->t = TK_OP; t->v = o; ntk++;
    }
    tk[ntk].t = TK_END; tk[ntk].line = (int16_t)line; return 0;
}

static const char* idn(int i) { return idpool + tk[i].v; }
static int is_op(int o) { return tk[tp].t == TK_OP && tk[tp].v == o; }
static int is_id(const char* n) { return tk[tp].t == TK_ID && gl(idn(tp), n); }
static int eat(int o) { if (is_op(o)) { tp++; return 1; } return 0; }
static void need(int o, const char* m) { if (!eat(o)) fehler(m); }

/* Befehlsausgabe */
static int* cb; static int ncur; static int cmax;
static int emit(int op, int arg) { if (ncur >= cmax) { fehler("Programm zu lang"); return 0; } cb[ncur * 2] = op; cb[ncur * 2 + 1] = arg; return ncur++; }
static int op_at(int i) { return cb[i * 2]; }
static int arg_at(int i) { return cb[i * 2 + 1]; }
static void patch(int at) { cb[at * 2 + 1] = ncur - (at + 1); }
static void patch_to(int at, int to) { cb[at * 2 + 1] = to - (at + 1); }

/* Namen */
struct lvar { int name, type, slot; };
static struct lvar lv_[64]; static int nlv, nloc_max, cur_ret;
static int find_local(const char* n) { for (int i = nlv - 1; i >= 0; i--) if (gl(idpool + lv_[i].name, n)) return i; return -1; }
static int find_global(const char* n) { for (int i = 0; i < V->ng; i++) if (gl(V->gname[i], n)) return i; return -1; }
static int find_fn(const char* n) { for (int i = 0; i < V->nfn; i++) if (gl(V->fns[i].name, n)) return i; return -1; }
static int add_global(const char* n, int type) {
    int i = find_global(n); if (i >= 0) return i;
    if (V->ng >= NG_MAX) { fehler("Zu viele globale Variablen"); return 0; }
    kop(V->gname[V->ng], n, 44); V->gtype[V->ng] = (uint8_t)type; return V->ng++;
}
static int add_local(int nameidx, int type) {
    if (nlv >= 64) { fehler("Zu viele lokale Variablen"); return 0; }
    lv_[nlv].name = tk[nameidx].v; lv_[nlv].type = type; lv_[nlv].slot = nlv; if (nlv + 1 > nloc_max) nloc_max = nlv + 1; return nlv++;
}
static int hidden_local(void) { if (nlv >= 64) { fehler("Zu viele lokale Variablen"); return 0; } lv_[nlv].name = 0; lv_[nlv].type = T_INT; lv_[nlv].slot = nlv; if (nlv + 1 > nloc_max) nloc_max = nlv + 1; return nlv++; }

/* ---------- Klassen ---------- */
static int cur_cls = -1, cur_static; static int fin_tp[NCL_MAX][MAXF];
static unsigned hash_(const char* s) { unsigned h = 2166136261u; while (*s) { h ^= (unsigned char)*s++; h *= 16777619u; } return h; }
static int tag_of(int ty) { return ty >= T_OARR0 ? TAG_OARR + (ty - T_OARR0) : 0; }
static int class_find(const char* n) { for (int i = 0; i < V->ncl; i++) if (gl(V->cl[i].name, n)) return i; return -1; }
static int cls_off(int c) { int o = 0; for (int d = 0; d < 20; d++) { int p = V->cl[c].parent; if (p < 0) break; o += V->cl[p].nown; c = p; } return o; }
static int find_field(int c, const char* n, int* idx, int* ty) {
    for (int d = 0; c >= 0 && d < 20; d++, c = V->cl[c].parent)
        for (int i = 0; i < V->cl[c].nown; i++) if (gl(V->cl[c].fname[i], n)) { *idx = cls_off(c) + i; *ty = V->cl[c].ftype[i]; return 0; }
    return -1;
}
static void mangle(char* o, int c, const char* n) { kop(o, V->cl[c].name, 44); int l = slen(o); o[l] = '.'; kop(o + l + 1, n, 44 - l - 1); }
static int find_static(int c, const char* n) {
    char m[44]; for (int d = 0; c >= 0 && d < 20; d++, c = V->cl[c].parent) { mangle(m, c, n); int g = find_global(m); if (g >= 0) return g; }
    return -1;
}
static int find_method(int c, const char* n, int na) {
    unsigned h = hash_(n);
    for (int d = 0; c >= 0 && d < 20; d++, c = V->cl[c].parent)
        for (int i = 0; i < V->nfn; i++) { struct fn* f = &V->fns[i]; if (f->cls == c && f->mh == h && f->nargs - (f->stat ? 0 : 1) == na) return i; }
    return -1;
}
static int find_ctor(int c, int na) { unsigned h = hash_("<init>"); for (int i = 0; i < V->nfn; i++) { struct fn* f = &V->fns[i]; if (f->cls == c && f->mh == h && f->nargs == na + 1) return i; } return -1; }
static int reg_method(int c, const char* n, int nargs, int ret, int stat) {
    unsigned h = hash_(n);
    for (int i = 0; i < V->nfn; i++) { struct fn* f = &V->fns[i]; if (f->cls == c && f->mh == h && f->nargs == nargs) return i; }
    if (V->nfn >= NFN_MAX - 1) { fehler("Zu viele Funktionen"); return 0; }
    struct fn* f = &V->fns[V->nfn]; mangle(f->name, c, n); f->cls = c; f->mh = h; f->nargs = nargs; f->ret = ret; f->stat = stat; f->synth = 0; f->def = 1; f->pc = -1; f->nloc = 0; return V->nfn++;
}
static int is_handler(const char* n) {
    static const char* const H[] = { "paint", "init", "setup", "tick", "update", "step", "start", "mouseDown", "mousePressed", "mouseClicked", "mouseUp", "mouseReleased", "keyTyped", "keyPressed", "keyDown", "mouseMove", "mouseMoved", "mouseDragged", "main" };
    for (unsigned i = 0; i < sizeof(H) / sizeof(H[0]); i++) if (gl(n, H[i])) return 1; return 0;
}
static int is_applet(const char* n) { return gl(n, "Applet") || gl(n, "JApplet") || gl(n, "JPanel") || gl(n, "Canvas") || gl(n, "Panel") || gl(n, "Frame") || gl(n, "JFrame") || gl(n, "Component") || gl(n, "JComponent"); }
/* Durchgang 0: Klassen sammeln. Eine "Startklasse" (Applet, main, paint ...) bleibt wie bisher flach (Felder = globale Variablen). */
static void class_scan(void) {
    V->ncl = 0; int score[NCL_MAX], strong[NCL_MAX];
    for (int i = 0; i < ntk; i++) {
        if (tk[i].t != TK_ID) continue; const char* w = idn(i);
        int isif = gl(w, "interface"); if (!isif && !gl(w, "class")) continue;
        if (i > 0 && tk[i - 1].t == TK_OP && tk[i - 1].v == '.') continue;
        if (tk[i + 1].t != TK_ID) continue;
        if (V->ncl >= NCL_MAX) { tp = i; fehler("Zu viele Klassen (hoechstens 16)"); return; }
        int cid = V->ncl++; struct cls* C = &V->cl[cid];
        kop(C->name, idn(i + 1), 20); C->parent = -1; C->isif = isif; C->flat = 0; C->nown = 0; C->ntotal = 0; C->ppos = -1;
        for (int k = 0; k < 3; k++) { C->ifpos[k] = -1; C->ifc[k] = -1; }
        score[cid] = strong[cid] = 0; int ni = 0, j = i + 2;
        while (tk[j].t != TK_END && !(tk[j].t == TK_OP && tk[j].v == '{')) {
            if (tk[j].t == TK_ID && gl(idn(j), "extends") && !isif && tk[j + 1].t == TK_ID) { C->ppos = j + 1; if (is_applet(idn(j + 1))) strong[cid] = 1; j += 2; continue; }
            if (tk[j].t == TK_ID && (gl(idn(j), "implements") || (isif && gl(idn(j), "extends")))) {
                j++; while (tk[j].t == TK_ID || (tk[j].t == TK_OP && tk[j].v == ',')) { if (tk[j].t == TK_ID && ni < 3) C->ifpos[ni++] = j; j++; } continue; }
            j++;
        }
        if (tk[j].t == TK_OP && tk[j].v == '{') {
            int d = 0; for (int k = j; tk[k].t != TK_END; k++) {
                if (tk[k].t == TK_OP) { if (tk[k].v == '{') d++; else if (tk[k].v == '}') { d--; if (d == 0) break; } }
                else if (d == 1 && tk[k].t == TK_ID && tk[k + 1].t == TK_OP && tk[k + 1].v == '(' && (tk[k - 1].t == TK_ID || (tk[k - 1].t == TK_OP && tk[k - 1].v == ']')) && !gl(idn(k - 1), "new") && !gl(idn(k - 1), "return")) {
                    if (is_handler(idn(k))) score[cid]++; if (gl(idn(k), "main") || gl(idn(k), "paint")) strong[cid] = 1; }
            }
        }
    }
    int nreal = 0, anyflat = 0, last = -1, best = -1, bs = 0;
    for (int c = 0; c < V->ncl; c++) if (!V->cl[c].isif) { nreal++; last = c; if (strong[c]) { V->cl[c].flat = 1; anyflat = 1; } if (score[c] > bs) { bs = score[c]; best = c; } }
    if (!anyflat && best >= 0) V->cl[best].flat = 1;
    for (int c = 0; c < V->ncl; c++) {
        struct cls* C = &V->cl[c];
        if (C->ppos >= 0) { int p = class_find(idn(C->ppos)); C->parent = (p == c || (p >= 0 && V->cl[p].flat)) ? -1 : p; }
        for (int k = 0; k < 3; k++) if (C->ifpos[k] >= 0) C->ifc[k] = class_find(idn(C->ifpos[k]));
    }
}
static void layout(void) {
    for (int c = 0; c < V->ncl; c++) {
        int ch[20], n = 0; for (int p = c; p >= 0 && n < 20; p = V->cl[p].parent) ch[n++] = p;
        int k = 0; for (int m = n - 1; m >= 0; m--) { struct cls* Q = &V->cl[ch[m]]; for (int i = 0; i < Q->nown; i++) { if (k < MAXF) V->cl[c].ftot[k++] = Q->ftype[i]; else { fehler("Zu viele Felder in einer Klasse (hoechstens 32)"); return; } } }
        V->cl[c].ntotal = k;
    }
}
static int add_this(int cid) {
    if (nlv >= 64) { fehler("Zu viele lokale Variablen"); return 0; }
    lv_[nlv].name = 1; lv_[nlv].type = T_OBJ0 + cid; lv_[nlv].slot = nlv; if (nlv + 1 > nloc_max) nloc_max = nlv + 1; return nlv++;
}

/* Typen im Quelltext */
static int type_word(const char* n) {
    if (gl(n, "int") || gl(n, "long") || gl(n, "short") || gl(n, "byte") || gl(n, "char") || gl(n, "boolean") || gl(n, "Integer")) return T_INT;
    if (gl(n, "String")) return T_STR; if (gl(n, "void")) return T_VOID; return -1;
}
static int is_modifier(const char* n) { return gl(n, "public") || gl(n, "private") || gl(n, "protected") || gl(n, "static") || gl(n, "final") || gl(n, "abstract") || gl(n, "synchronized") || gl(n, "native") || gl(n, "volatile") || gl(n, "transient"); }
static int mods_st;
static void skip_mods(void) { mods_st = 0; for (;;) { if (is_op('@')) { tp++; if (tk[tp].t == TK_ID) tp++; if (is_op('(')) { int d = 0; do { if (is_op('(')) d++; if (is_op(')')) d--; tp++; } while (d && tk[tp].t != TK_END); } continue; } if (tk[tp].t == TK_ID && is_modifier(idn(tp))) { if (gl(idn(tp), "static")) mods_st = 1; tp++; continue; } break; } }
static int check_float(const char* n) { if (gl(n, "float") || gl(n, "double")) { fehler("float/double gibt es nicht: bitte ganze Zahlen (int) verwenden"); return 1; } return 0; }
/* Typ lesen (mit [] / [][]); -1 = kein Typ. unbekannte Namen zaehlen als int, wenn danach ein Name folgt */
static int read_type(void) {
    if (tk[tp].t != TK_ID) return -1; const char* n = idn(tp); int base = type_word(n);
    int nx_id = tk[tp + 1].t == TK_ID, nx_arr = tk[tp + 1].t == TK_OP && tk[tp + 1].v == '[' && tk[tp + 2].t == TK_OP && tk[tp + 2].v == ']';
    if (gl(n, "float") || gl(n, "double")) { if (!nx_id && !nx_arr) return -1; check_float(n); tp++; return T_INT; }
    if (!nx_id && !nx_arr) return -1;
    if (base < 0) { int cc = class_find(n); base = cc >= 0 ? T_OBJ0 + cc : T_INT; }
    tp++; int dim = 0; while (is_op('[') && tk[tp + 1].t == TK_OP && tk[tp + 1].v == ']') { tp += 2; dim++; }
    if (dim == 1) return base >= T_OBJ0 ? T_OARR0 + (base - T_OBJ0) : T_ARR; if (dim >= 2) return T_ARR2; return base;
}

/* ---------- Eingebaute Funktionen ---------- */
struct bi { const char* n; int na, id, ret, strfirst; };
static const struct bi BI[] = {
    { "setColor", 1, B_COLOR1, T_VOID, 0 }, { "setColor", 3, B_COLOR3, T_VOID, 0 }, { "fillRect", 4, B_FILLRECT, T_VOID, 0 }, { "drawRect", 4, B_DRAWRECT, T_VOID, 0 },
    { "drawLine", 4, B_LINE, T_VOID, 0 }, { "fillOval", 4, B_FILLOVAL, T_VOID, 0 }, { "drawOval", 4, B_DRAWOVAL, T_VOID, 0 }, { "drawString", 3, B_STRING, T_VOID, 1 },
    { "setFontSize", 1, B_FONT, T_VOID, 0 }, { "getWidth", 0, B_WIDTH, T_INT, 0 }, { "getHeight", 0, B_HEIGHT, T_INT, 0 }, { "width", 0, B_WIDTH, T_INT, 0 }, { "height", 0, B_HEIGHT, T_INT, 0 },
    { "setPixel", 2, B_PIXEL, T_VOID, 0 }, { "clear", 0, B_CLEARALL, T_VOID, 0 }, { "repaint", 0, B_NOP0, T_VOID, 0 }, { "sleep", 1, B_NOP1, T_VOID, 0 }, { "exit", 0, B_NOP0, T_VOID, 0 },
    { "abs", 1, B_ABS, T_INT, 0 }, { "min", 2, B_MIN, T_INT, 0 }, { "max", 2, B_MAX, T_INT, 0 }, { "sqrt", 1, B_SQRT, T_INT, 0 }, { "sin", 1, B_SIN, T_INT, 0 }, { "cos", 1, B_COS, T_INT, 0 },
    { "random", 1, B_RND, T_INT, 0 }, { "rnd", 1, B_RND, T_INT, 0 }, { "pow", 2, B_POW, T_INT, 0 },
    { "length", 1, B_SLEN, T_INT, 0 }, { "charAt", 2, B_CHARAT, T_INT, 0 }, { "substring", 3, B_SUBSTR, T_STR, 0 }, { "indexOf", 2, B_INDEXOF, T_INT, 0 }, { "isEmpty", 1, B_ISEMPTY, T_INT, 0 },
    { "toUpperCase", 1, B_UPPER, T_STR, 0 }, { "toLowerCase", 1, B_LOWER, T_STR, 0 }, { "parseInt", 1, B_PARSE, T_INT, 0 },
    { "println", 1, B_PRINT, T_VOID, 1 }, { "print", 1, B_PRINT, T_VOID, 1 }, { "millis", 0, B_MILLIS, T_INT, 0 }, { "currentTimeMillis", 0, B_MILLIS, T_INT, 0 },
    { "getX", 0, B_GETX, T_INT, 0 }, { "getY", 0, B_GETY, T_INT, 0 }, { "getKey", 0, B_GETKEY, T_INT, 0 }, { "getKeyChar", 0, B_GETKEY, T_INT, 0 }, { "getKeyCode", 0, B_GETKEY, T_INT, 0 },
    { "rgb", 3, B_RGB, T_INT, 0 }, { "chr", 1, B_TOCHR, T_STR, 0 },
};
#define NBI ((int)(sizeof(BI) / sizeof(BI[0])))

static int expr(void);
static int assign(void);

static struct { const char* n; int rgb; } const FARBEN[] = {
    { "RED", 0xFF0000 }, { "GREEN", 0x00C000 }, { "BLUE", 0x0000FF }, { "BLACK", 0x000000 }, { "WHITE", 0xFFFFFF }, { "YELLOW", 0xFFFF00 }, { "ORANGE", 0xFFA500 },
    { "GRAY", 0x808080 }, { "LIGHT_GRAY", 0xC0C0C0 }, { "DARK_GRAY", 0x404040 }, { "CYAN", 0x00FFFF }, { "MAGENTA", 0xFF00FF }, { "PINK", 0xFFAFAF },
    { "red", 0xFF0000 }, { "green", 0x00C000 }, { "blue", 0x0000FF }, { "black", 0 }, { "white", 0xFFFFFF }, { "yellow", 0xFFFF00 }, { "orange", 0xFFA500 }, { "gray", 0x808080 }, { "cyan", 0x00FFFF }, { "magenta", 0xFF00FF }, { "pink", 0xFFAFAF },
};

/* Aufruf einer eingebauten Funktion; recv = Empfaenger liegt schon auf dem Stapel */
static int call_builtin(const char* name, int recv) {
    int given = 0, t0 = T_INT; int first = recv;
    int cand = -1; for (int i = 0; i < NBI; i++) if (gl(BI[i].n, name)) { cand = i; break; }
    if (recv) t0 = T_STR;
    need('(', "'(' erwartet");
    int na = recv;
    int sargs[4]; (void)sargs;
    if (!is_op(')')) {
        do {
            int t = expr(); na++; given++;
            if (na == 1) { t0 = t; first = 1; }
            if (na == 1 && cand >= 0 && BI[cand].strfirst && t == T_INT) emit(O_ITOS, 0);
            if (na == 1 && cand >= 0 && !BI[cand].strfirst && gl(name, "length") && t == T_ARR) { /* arr.length() nicht Java: ignorieren */ }
        } while (eat(','));
    }
    need(')', "')' erwartet");
    (void)first; (void)given;
    int found = -1; for (int i = 0; i < NBI; i++) if (gl(BI[i].n, name) && BI[i].na == na) { found = i; break; }
    if (found < 0 && gl(name, "substring") && na == 2) { emit(O_PUSH, 100000); found = -2; for (int i = 0; i < NBI; i++) if (gl(BI[i].n, "substring") && BI[i].na == 3) found = i; }
    if (found < 0 && (gl(name, "equals") || gl(name, "equalsIgnoreCase")) && na == 2) { emit(O_SEQ, 0); return T_INT; }
    if (found < 0 && (gl(name, "toString") || gl(name, "valueOf") || gl(name, "str")) && na == 1) { if (t0 == T_INT) emit(O_ITOS, 0); return T_STR; }
    if (found < 0 && gl(name, "concat") && na == 2) { emit(O_SCAT, 0); return T_STR; }
    if (found < 0) { char m[100]; kop(m, "Unbekannte Funktion oder falsche Argumentzahl: ", 100); int l = slen(m); kop(m + l, name, 100 - l); fehler(m); return T_INT; }
    emit(O_BI, BI[found].id); return BI[found].ret;
}

static int postfix(void);
static int count_args(void) {
    int p = tp; if (!(tk[p].t == TK_OP && tk[p].v == '(')) return 0; p++; if (tk[p].t == TK_OP && tk[p].v == ')') return 0;
    int d = 0, n = 1;
    for (; tk[p].t != TK_END; p++) if (tk[p].t == TK_OP) { int o = tk[p].v; if (o == '(' || o == '[' || o == '{') d++; else if (o == ')' || o == ']' || o == '}') { if (!d) break; d--; } else if (o == ',' && !d) n++; }
    return n;
}
static void call_args(void) { need('(', "'(' erwartet"); if (!is_op(')')) { do { expr(); } while (eat(',')); } need(')', "')' erwartet"); }
static void array_lit(int tag) {
    need('{', "'{' erwartet"); int cnt = 0, sv = tp, d = 0;
    while (tk[tp].t != TK_END) { if (is_op('{')) d++; if (is_op('}')) { if (!d) break; d--; } if (is_op(',') && !d) cnt++; tp++; }
    if (tp > sv && !(tk[tp - 1].t == TK_OP && tk[tp - 1].v == ',')) cnt++;
    tp = sv; emit(O_PUSH, cnt); emit(O_NEWA, tag); int n = 0;
    while (!is_op('}') && tk[tp].t != TK_END) { emit(O_DUP, 0); emit(O_PUSH, n++); expr(); emit(O_ASETV, 0); emit(O_POP, 0); if (!eat(',')) break; }
    need('}', "'}' erwartet");
}
static int user_call(int fi, int nameidx) {
    (void)nameidx; struct fn* f = &V->fns[fi]; int na = 0;
    need('(', "'(' erwartet");
    if (!is_op(')')) { do { expr(); na++; } while (eat(',')); }
    need(')', "')' erwartet");
    if (na != f->nargs) { fehler("Falsche Anzahl Argumente beim Funktionsaufruf"); return f->ret == T_VOID ? T_VOID : f->ret; }
    emit(O_CALL, fi); return f->ret;
}

/* Wertsatz fuer Zuweisungen / ++ / --: Lesezugriff als letzten Befehl erkennen */
static int lv_kind(int start) {          /* 1 lokal, 2 global, 3 Feldelement, 0 nichts */
    int last = ncur - 1; if (last < start) return 0;
    int op = op_at(last); if (op == O_LDL) return 1; if (op == O_LDG) return 2; if (op == O_AGET) return 3; if (op == O_GETF) return 4; return 0;
}
static int lv_type_g(int arg) { return V->gtype[arg]; }

static int primary(int* lvt) {
    struct tok* t = &tk[tp];
    *lvt = -1;
    if (t->t == TK_NUM) { tp++; emit(O_PUSH, t->v); return T_INT; }
    if (t->t == TK_STR) { tp++; emit(O_PUSH, t->v); return T_STR; }
    if (is_op('(')) {
        /* Typumwandlung (int) (char) ... oder Klammerausdruck */
        if (tk[tp + 1].t == TK_ID && tk[tp + 2].t == TK_OP && tk[tp + 2].v == ')' && type_word(idn(tp + 1)) == T_INT && !(tk[tp + 3].t == TK_OP && (tk[tp + 3].v == '+' || tk[tp + 3].v == '-') && 0)) {
            tp += 3; return primary(lvt) == T_STR ? T_STR : T_INT; }
        if (tk[tp + 1].t == TK_ID && tk[tp + 2].t == TK_OP && tk[tp + 2].v == ')' && class_find(idn(tp + 1)) >= 0 &&
            (tk[tp + 3].t == TK_ID || tk[tp + 3].t == TK_NUM || tk[tp + 3].t == TK_STR || (tk[tp + 3].t == TK_OP && tk[tp + 3].v == '('))) {
            int cc = class_find(idn(tp + 1)); tp += 3; postfix(); return T_OBJ0 + cc; }
        tp++; int ty = expr(); need(')', "')' erwartet"); return ty;
    }
    if (t->t != TK_ID) { fehler("Ausdruck erwartet"); tp++; return T_INT; }
    const char* n = idn(tp);
    if (gl(n, "true")) { tp++; emit(O_PUSH, 1); return T_INT; }
    if (gl(n, "false")) { tp++; emit(O_PUSH, 0); return T_INT; }
    if (gl(n, "null")) { tp++; emit(O_PUSH, 0); return T_INT; }
    if (gl(n, "this")) {
        tp++; if (cur_cls < 0 || cur_static) { fehler("this gibt es nur in Methoden einer Klasse"); return T_INT; }
        if (is_op('(')) { fehler("this(...) wird nicht unterstuetzt"); return T_INT; }
        emit(O_LDL, 0); return T_OBJ0 + cur_cls;
    }
    if (gl(n, "super")) {
        tp++; int par = cur_cls >= 0 ? V->cl[cur_cls].parent : -1;
        if (cur_cls < 0 || cur_static || !is_op('.') || tk[tp + 1].t != TK_ID || tk[tp + 2].t != TK_OP || tk[tp + 2].v != '(') { fehler("super.methode(...) erwartet"); return T_INT; }
        const char* mn = idn(tp + 1); tp += 2; int na = count_args(); int fi = par >= 0 ? find_method(par, mn, na) : -1;
        if (fi < 0) { fehler("Methode der Oberklasse nicht gefunden"); return T_INT; }
        emit(O_LDL, 0); call_args(); emit(O_CALL, fi); return V->fns[fi].ret;
    }
    if (gl(n, "new")) {
        tp++; if (tk[tp].t != TK_ID) { fehler("Typ nach new erwartet"); return T_INT; }
        const char* tn = idn(tp);
        int ncid = class_find(tn);
        if (ncid >= 0) {
            tp++;
            if (is_op('[')) { tp++; expr(); need(']', "']' erwartet"); emit(O_NEWA, TAG_OARR + ncid); return T_OARR0 + ncid; }
            if (!is_op('(')) { fehler("( erwartet"); return T_INT; }
            int na = count_args(), ci = find_ctor(ncid, na);
            if (ci < 0) { fehler("Passender Konstruktor fehlt (Anzahl Argumente)"); return T_INT; }
            emit(O_NEWO, ncid); emit(O_DUP, 0); call_args(); emit(O_CALL, ci); emit(O_POP, 0); return T_OBJ0 + ncid;
        }
        if (gl(tn, "Color") && tk[tp + 1].t == TK_OP && tk[tp + 1].v == '(') { tp++; return call_builtin("rgb", 0); }
        if (check_float(tn)) return T_INT;
        if (type_word(tn) < 0 || type_word(tn) == T_VOID) { fehler("Dieses Objekt (new) gibt es nicht; nur int[] und String"); return T_INT; }
        tp++; int dims = 0;
        if (is_op('[')) {
            tp++; expr(); need(']', "']' erwartet"); dims = 1;
            if (is_op('[') && tk[tp + 1].t != TK_OP) { tp++; expr(); need(']', "']' erwartet"); dims = 2; }
            else while (is_op('[') && tk[tp + 1].t == TK_OP && tk[tp + 1].v == ']') tp += 2;
            if (dims == 2) { emit(O_NEWA2, 0); return T_ARR2; }
            emit(O_NEWA, 0); return T_ARR;
        }
        if (is_op('(')) { tp++; need(')', "')' erwartet"); emit(O_PUSH, 0); return T_STR; }     /* new String() */
        fehler("[ erwartet"); return T_INT;
    }
    if (gl(n, "Color") && tk[tp + 1].t == TK_OP && tk[tp + 1].v == '.' && tk[tp + 2].t == TK_ID && !(tk[tp + 3].t == TK_OP && tk[tp + 3].v == '(')) {
        const char* cn = idn(tp + 2); for (unsigned i = 0; i < sizeof(FARBEN) / sizeof(FARBEN[0]); i++) if (gl(FARBEN[i].n, cn)) { tp += 3; emit(O_PUSH, FARBEN[i].rgb); return T_INT; }
        fehler("Unbekannte Farbe"); tp += 3; return T_INT;
    }
    int li = find_local(n), gi = -1;
    if (li < 0 && tk[tp + 1].t == TK_OP && tk[tp + 1].v == '.' && tk[tp + 2].t == TK_ID) {      /* Klasse.statisch */
        int cc = class_find(n);
        if (cc >= 0 && !V->cl[cc].flat) {
            tp += 2; const char* mn = idn(tp);
            if (tk[tp + 1].t == TK_OP && tk[tp + 1].v == '(') {
                tp++; int na = count_args(), fi = find_method(cc, mn, na);
                if (fi < 0 || !V->fns[fi].stat) { fehler("Statische Methode nicht gefunden"); return T_INT; }
                call_args(); emit(O_CALL, fi); return V->fns[fi].ret;
            }
            int g = find_static(cc, mn); if (g < 0) { fehler("Statisches Feld nicht gefunden"); return T_INT; }
            tp++; emit(O_LDG, g); return lv_type_g(g);
        }
    }
    int fld = -1, fty = T_INT;
    if (li < 0 && cur_cls >= 0) {
        if (!cur_static && find_field(cur_cls, n, &fld, &fty) < 0) fld = -1;
        if (fld < 0 && tk[tp + 1].t == TK_OP && tk[tp + 1].v == '(') {                          /* Methode der eigenen Klasse */
            tp++; int na = count_args(), fi = find_method(cur_cls, n, na);
            if (fi >= 0) {
                int st = V->fns[fi].stat; if (!st && cur_static) { fehler("Nicht-statische Methode aus statischem Kontext"); return T_INT; }
                if (!st) emit(O_LDL, 0);
                call_args(); emit(st ? O_CALL : O_CALLM, fi); return V->fns[fi].ret;
            }
            tp--;
        }
        if (fld < 0) gi = find_static(cur_cls, n);
    }
    if (li < 0 && fld < 0 && gi < 0) gi = find_global(n);
    if (li >= 0 || gi >= 0 || fld >= 0) {
        tp++; int ty;
        if (li >= 0) { emit(O_LDL, lv_[li].slot); ty = lv_[li].type; }
        else if (fld >= 0) { emit(O_LDL, 0); emit(O_GETF, fld); ty = fty; }
        else { emit(O_LDG, gi); ty = lv_type_g(gi); }
        /* Variable gefolgt von .name( ... ) bei int = Objekt-Aufruf wie g.fillRect(...) */
        if (ty == T_INT && is_op('.') && tk[tp + 1].t == TK_ID) {
            emit(O_POP, 0);
            while (is_op('.') && tk[tp + 1].t == TK_ID && tk[tp + 2].t == TK_OP && tk[tp + 2].v == '.') tp += 2;
            tp++; const char* mn = idn(tp); tp++; return call_builtin(mn, 0);
        }
        return ty;
    }
    int fi = find_fn(n);
    if (fi >= 0 && tk[tp + 1].t == TK_OP && tk[tp + 1].v == '(') { tp++; return user_call(fi, tp - 1); }
    /* Qualifizierte Namen: System.out.println, Math.abs, g.fillRect, Integer.parseInt ... */
    if (tk[tp + 1].t == TK_OP && tk[tp + 1].v == '.') {
        while (tk[tp].t == TK_ID && tk[tp + 1].t == TK_OP && tk[tp + 1].v == '.' && tk[tp + 2].t == TK_ID) { tp += 2; if (tk[tp + 1].t == TK_OP && tk[tp + 1].v == '(') break; }
        if (tk[tp].t == TK_ID && tk[tp + 1].t == TK_OP && tk[tp + 1].v == '(') { const char* mn = idn(tp); int f2 = find_fn(mn); tp++; if (f2 >= 0) return user_call(f2, tp - 1); return call_builtin(mn, 0); }
        if (gl(idn(tp), "PI")) { tp++; emit(O_PUSH, 3); return T_INT; }
        fehler("Unbekannter Name nach Punkt"); tp++; return T_INT;
    }
    if (tk[tp + 1].t == TK_OP && tk[tp + 1].v == '(') { tp++; return call_builtin(n, 0); }
    { char m[100]; kop(m, "Unbekannter Name: ", 100); int l = slen(m); kop(m + l, n, 100 - l); fehler(m); } tp++; return T_INT;
}

static int postfix(void) {
    int start = ncur, lvt; int ty = primary(&lvt);
    for (;;) {
        if (is_op('[')) {
            if (ty != T_ARR && ty != T_ARR2 && ty < T_OARR0) { fehler("[ ] nur bei Feldern (int[])"); return T_INT; }
            tp++; expr(); need(']', "']' erwartet"); emit(O_AGET, 0); ty = ty == T_ARR2 ? T_ARR : ty >= T_OARR0 ? T_OBJ0 + (ty - T_OARR0) : T_INT;
        } else if (is_op('.') && tk[tp + 1].t == TK_ID) {
            const char* mn = idn(tp + 1);
            if (ty >= T_OARR0 && gl(mn, "length")) { tp += 2; emit(O_ALEN, 0); ty = T_INT; }
            else if (ty >= T_OBJ0 && ty < T_OARR0) {
                int cid = ty - T_OBJ0;
                if (tk[tp + 2].t == TK_OP && tk[tp + 2].v == '(') {
                    tp += 2; int na = count_args(), fi = find_method(cid, mn, na);
                    if (fi < 0 || V->fns[fi].stat) { fehler("Diese Methode gibt es in der Klasse nicht"); return T_INT; }
                    call_args(); emit(O_CALLM, fi); ty = V->fns[fi].ret;
                } else {
                    int fidx, fty2; if (find_field(cid, mn, &fidx, &fty2) < 0) { fehler("Dieses Feld gibt es in der Klasse nicht"); return T_INT; }
                    tp += 2; emit(O_GETF, fidx); ty = fty2;
                }
            }
            else if (ty == T_ARR && gl(mn, "length")) { tp += 2; emit(O_ALEN, 0); ty = T_INT; }
            else if (ty == T_ARR2 && gl(mn, "length")) { tp += 2; emit(O_ALEN, 0); ty = T_INT; }
            else if (ty == T_STR) { tp += 2; ty = call_builtin(mn, 1); }
            else { fehler("Diese Methode gibt es nicht"); return T_INT; }
        } else break;
    }
    (void)start; return ty;
}

static void incdec(int kind, int arg, int add, int nachher) {          /* kind aus lv_kind; Lesebefehl ist schon entfernt (bei Feld: arr idx liegt auf dem Stapel) */
    int op = add ? O_ADD : O_SUB;
    if (kind == 4) { emit(O_DUP, 0); emit(O_GETF, arg); emit(O_PUSH, 1); emit(op, 0); emit(O_SETF, arg); if (nachher) { emit(O_PUSH, 1); emit(add ? O_SUB : O_ADD, 0); } return; }
    if (kind == 3) { emit(O_DUP2, 0); emit(O_AGET, 0); emit(O_PUSH, 1); emit(op, 0); emit(O_ASETV, 0); return; }
    int ld = kind == 1 ? O_LDL : O_LDG, st = kind == 1 ? O_STL : O_STG;
    if (nachher) { emit(ld, arg); emit(ld, arg); emit(O_PUSH, 1); emit(op, 0); emit(st, arg); }
    else { emit(ld, arg); emit(O_PUSH, 1); emit(op, 0); emit(O_DUP, 0); emit(st, arg); }
}

static int unary(void) {
    if (is_op('-')) { tp++; if (tk[tp].t == TK_NUM) { emit(O_PUSH, -tk[tp].v); tp++; return T_INT; } unary(); emit(O_NEG, 0); return T_INT; }
    if (is_op('+')) { tp++; return unary(); }
    if (is_op('!')) { tp++; unary(); emit(O_NOT, 0); return T_INT; }
    if (is_op('~')) { tp++; unary(); emit(O_BNOT, 0); return T_INT; }
    if (is_op(OP2('+', '+')) || is_op(OP2('-', '-'))) {
        int add = is_op(OP2('+', '+')); tp++; int start = ncur; int ty = unary(); int k = lv_kind(start);
        if (!k) { fehler("++ / -- braucht eine Variable"); return ty; }
        int arg = arg_at(ncur - 1); ncur--; incdec(k, arg, add, 0); return T_INT;
    }
    int start = ncur; int ty = postfix();
    if (is_op(OP2('+', '+')) || is_op(OP2('-', '-'))) {
        int add = is_op(OP2('+', '+')); int k = lv_kind(start);
        if (!k) { fehler("++ / -- braucht eine Variable"); tp++; return ty; }
        tp++; int arg = arg_at(ncur - 1); ncur--; incdec(k, arg, add, 1); return T_INT;
    }
    return ty;
}
static int mul_(void) {
    int t = unary();
    while (is_op('*') || is_op('/') || is_op('%')) { int o = tk[tp].v; tp++; unary(); emit(o == '*' ? O_MUL : o == '/' ? O_DIV : O_MOD, 0); t = T_INT; }
    return t;
}
static int add_(void) {
    int t = mul_();
    while (is_op('+') || is_op('-')) {
        int o = tk[tp].v; tp++;
        if (o == '+' && (t == T_STR)) { int t2 = mul_(); emit(O_SCAT, t2 == T_STR ? 0 : 2); t = T_STR; continue; }
        int pc = ncur; (void)pc; int t2 = mul_();
        if (o == '+' && t2 == T_STR) { emit(O_SCAT, 1); t = T_STR; continue; }
        emit(o == '+' ? O_ADD : O_SUB, 0); t = T_INT;
    }
    return t;
}
static int shift_(void) {
    int t = add_();
    while (is_op(OP2('<', '<')) || is_op(OP2('>', '>')) || is_op(OP3('>', '>', '>'))) { int o = tk[tp].v; tp++; add_(); emit(o == OP2('<', '<') ? O_SHL : o == OP2('>', '>') ? O_SHR : O_USHR, 0); t = T_INT; }
    return t;
}
static int rel_(void) {
    int t = shift_();
    for (;;) {
        if (is_op('<') || is_op('>') || is_op(OP2('<', '=')) || is_op(OP2('>', '='))) { int o = tk[tp].v; tp++; shift_(); emit(o == '<' ? O_LT : o == '>' ? O_GT : o == OP2('<', '=') ? O_LE : O_GE, 0); t = T_INT; }
        else if (is_id("instanceof")) { tp++; int cc = tk[tp].t == TK_ID ? class_find(idn(tp)) : -1; if (cc < 0) { fehler("Klasse nach instanceof unbekannt"); return T_INT; } tp++; emit(O_INSTOF, cc); t = T_INT; }
        else break;
    }
    return t;
}
static int eq_(void) {
    int t = rel_();
    while (is_op(OP2('=', '=')) || is_op(OP2('!', '='))) {
        int o = tk[tp].v; tp++; int t2 = rel_();
        if (t == T_STR && t2 == T_STR) { emit(O_SEQ, 0); if (o == OP2('!', '=')) emit(O_NOT, 0); }
        else emit(o == OP2('=', '=') ? O_EQ : O_NE, 0);
        t = T_INT;
    }
    return t;
}
static int band_(void) { int t = eq_(); while (is_op('&')) { tp++; eq_(); emit(O_AND, 0); t = T_INT; } return t; }
static int bxor_(void) { int t = band_(); while (is_op('^')) { tp++; band_(); emit(O_XOR, 0); t = T_INT; } return t; }
static int bor_(void) { int t = bxor_(); while (is_op('|')) { tp++; bxor_(); emit(O_OR, 0); t = T_INT; } return t; }
static int land_(void) {
    int t = bor_(); int fix[8], nf = 0;
    while (is_op(OP2('&', '&'))) { tp++; emit(O_DUP, 0); if (nf < 8) fix[nf++] = emit(O_JZ, 0); emit(O_POP, 0); bor_(); t = T_INT; }
    for (int i = 0; i < nf; i++) patch(fix[i]);
    return t;
}
static int lor_(void) {
    int t = land_(); int fix[8], nf = 0;
    while (is_op(OP2('|', '|'))) { tp++; emit(O_DUP, 0); if (nf < 8) fix[nf++] = emit(O_JNZ, 0); emit(O_POP, 0); land_(); t = T_INT; }
    for (int i = 0; i < nf; i++) patch(fix[i]);
    return t;
}
static int tern_(void) {
    int t = lor_();
    if (is_op('?')) {
        tp++; int jz = emit(O_JZ, 0); int t1 = assign(); need(':', "':' erwartet"); int jm = emit(O_JMP, 0); patch(jz); int t2 = tern_(); patch(jm); (void)t2; return t1;
    }
    return t;
}
static int assign(void) {
    int start = ncur; int t = tern_();
    int o = tk[tp].t == TK_OP ? tk[tp].v : 0;
    int comp = 0, bop = 0;
    if (o == '=') comp = 0;
    else if (o == OP2('+', '=')) { comp = 1; bop = O_ADD; } else if (o == OP2('-', '=')) { comp = 1; bop = O_SUB; } else if (o == OP2('*', '=')) { comp = 1; bop = O_MUL; }
    else if (o == OP2('/', '=')) { comp = 1; bop = O_DIV; } else if (o == OP2('%', '=')) { comp = 1; bop = O_MOD; } else if (o == OP2('&', '=')) { comp = 1; bop = O_AND; }
    else if (o == OP2('|', '=')) { comp = 1; bop = O_OR; } else if (o == OP2('^', '=')) { comp = 1; bop = O_XOR; } else if (o == OP3('<', '<', '=')) { comp = 1; bop = O_SHL; } else if (o == OP3('>', '>', '=')) { comp = 1; bop = O_SHR; }
    else return t;
    int k = lv_kind(start); if (!k) { fehler("Links von = muss eine Variable stehen"); return t; }
    tp++; int arg = arg_at(ncur - 1); ncur--;
    if (k == 4) {
        if (comp) { emit(O_DUP, 0); emit(O_GETF, arg); }
        int rt = assign();
        if (comp) { if (bop == O_ADD && t == T_STR) emit(O_SCAT, rt == T_STR ? 0 : 2); else emit(bop, 0); }
        else if (t == T_STR && rt != T_STR) emit(O_ITOS, 0);
        emit(O_SETF, arg); return t;
    }
    int ld = k == 1 ? O_LDL : O_LDG, st = k == 1 ? O_STL : O_STG;
    if (k == 3) {
        if (comp) { emit(O_DUP2, 0); emit(O_AGET, 0); }
        int rt = assign(); if (comp) emit(bop, 0); (void)rt; emit(O_ASETV, 0); return T_INT;
    }
    if (comp) emit(ld, arg);
    int rt = assign();
    if (comp) { if (bop == O_ADD && t == T_STR) emit(O_SCAT, rt == T_STR ? 0 : 2); else emit(bop, 0); }
    else if (t == T_STR && rt != T_STR) emit(O_ITOS, 0);
    emit(O_DUP, 0); emit(st, arg); return t;
}
static int expr(void) { return assign(); }

/* ---------- Anweisungen ---------- */
struct loop { int brk[16], nbrk, cont[16], ncont, sw; };
static struct loop lp[10]; static int nlp;
static void stmt(void);
static void block_(void) {
    int sv = nlv; need('{', "'{' erwartet");
    while (!is_op('}') && tk[tp].t != TK_END && !cerr[0]) stmt();
    need('}', "'}' erwartet"); nlv = sv;
}
static int cond_(void) { need('(', "'(' erwartet"); expr(); need(')', "')' erwartet"); return emit(O_JZ, 0); }
static void loop_end(int cont_target) {
    struct loop* l = &lp[nlp - 1];
    for (int i = 0; i < l->nbrk; i++) patch(l->brk[i]);
    for (int i = 0; i < l->ncont; i++) patch_to(l->cont[i], cont_target);
    nlp--;
}
static void decl_rest(int type) {
    do {
        if (tk[tp].t != TK_ID) { fehler("Variablenname erwartet"); return; }
        int ni = tp; tp++; int ty = type;
        if (is_op('[') && tk[tp + 1].t == TK_OP && tk[tp + 1].v == ']') { tp += 2; ty = T_ARR; }
        int li = add_local(ni, ty);
        if (eat('=')) {
            if (is_op('{')) {                        /* Feldliste {1,2,3} */
                tp++; int n = 0; int base = ncur; (void)base; emit(O_PUSH, 0); int newa = emit(O_NEWA, tag_of(ty)); (void)newa;
                /* Laenge steht noch nicht fest: Elemente zaehlen, dann Anzahl einsetzen */
                int cnt = 0; int sv = tp; int d = 0; while (tk[tp].t != TK_END) { if (is_op('{')) d++; if (is_op('}')) { if (!d) break; d--; } if (is_op(',') && !d) cnt++; tp++; } if (tp > sv && !(tk[tp - 1].t == TK_OP && tk[tp - 1].v == ',')) cnt++;
                tp = sv; cb[(ncur - 2) * 2 + 1] = cnt;
                emit(O_STL, lv_[li].slot);
                while (!is_op('}') && tk[tp].t != TK_END) { emit(O_LDL, lv_[li].slot); emit(O_PUSH, n++); expr(); emit(O_ASETV, 0); emit(O_POP, 0); if (!eat(',')) break; }
                need('}', "'}' erwartet");
            } else {
                int rt = assign(); if (ty == T_STR && rt != T_STR) emit(O_ITOS, 0);
                emit(O_STL, lv_[li].slot);
            }
        } else if (ty == T_STR) { emit(O_PUSH, 0); emit(O_STL, lv_[li].slot); }
        else { emit(O_PUSH, 0); emit(O_STL, lv_[li].slot); }
    } while (eat(','));
}
static void stmt(void) {
    if (cerr[0]) return;
    if (eat(';')) return;
    if (is_op('{')) { block_(); return; }
    if (tk[tp].t == TK_ID) {
        const char* n = idn(tp);
        if (gl(n, "if")) {
            tp++; int jz = cond_(); stmt();
            if (is_id("else")) { tp++; int jm = emit(O_JMP, 0); patch(jz); stmt(); patch(jm); } else patch(jz);
            return;
        }
        if (gl(n, "while")) {
            tp++; int top = ncur; int jz = cond_(); nlp++; lp[nlp - 1].nbrk = lp[nlp - 1].ncont = 0; lp[nlp - 1].sw = 0; stmt();
            int j = emit(O_JMP, 0); patch_to(j, top); patch(jz); loop_end(top); return;
        }
        if (gl(n, "do")) {
            tp++; int top = ncur; nlp++; lp[nlp - 1].nbrk = lp[nlp - 1].ncont = 0; lp[nlp - 1].sw = 0; stmt();
            int ct = ncur; if (!is_id("while")) { fehler("while erwartet"); return; } tp++; need('(', "'(' erwartet"); expr(); need(')', "')' erwartet"); eat(';');
            int jnz = emit(O_JNZ, 0); patch_to(jnz, top); loop_end(ct); return;
        }
        if (gl(n, "for")) {
            tp++; need('(', "'(' erwartet"); int sv = nlv;
            /* for (int x : feld) */
            if (tk[tp].t == TK_ID && (type_word(idn(tp)) == T_INT || class_find(idn(tp)) >= 0) && tk[tp + 1].t == TK_ID && tk[tp + 2].t == TK_OP && tk[tp + 2].v == ':') {
                int vty = type_word(idn(tp)) == T_INT ? T_INT : T_OBJ0 + class_find(idn(tp));
                tp++; int ni = tp; tp += 2; int arrs = hidden_local(), idx = hidden_local(), var = add_local(ni, vty);
                expr(); emit(O_STL, lv_[arrs].slot); need(')', "')' erwartet"); emit(O_PUSH, 0); emit(O_STL, lv_[idx].slot);
                int top = ncur; emit(O_LDL, lv_[idx].slot); emit(O_LDL, lv_[arrs].slot); emit(O_ALEN, 0); emit(O_LT, 0); int jz = emit(O_JZ, 0);
                emit(O_LDL, lv_[arrs].slot); emit(O_LDL, lv_[idx].slot); emit(O_AGET, 0); emit(O_STL, lv_[var].slot);
                nlp++; lp[nlp - 1].nbrk = lp[nlp - 1].ncont = 0; lp[nlp - 1].sw = 0; stmt();
                int ct = ncur; emit(O_LDL, lv_[idx].slot); emit(O_PUSH, 1); emit(O_ADD, 0); emit(O_STL, lv_[idx].slot);
                int j = emit(O_JMP, 0); patch_to(j, top); patch(jz); loop_end(ct); nlv = sv; return;
            }
            if (!is_op(';')) { int ty = read_type(); if (ty >= 0) decl_rest(ty); else { expr(); emit(O_POP, 0); } }
            need(';', "';' erwartet");
            int top = ncur, jz = -1; if (!is_op(';')) { expr(); jz = emit(O_JZ, 0); } need(';', "';' erwartet");
            /* Fortsetzung-Teil zwischenspeichern: Tokenbereich merken, nach dem Rumpf uebersetzen */
            int upd = tp; int d = 0; while (tk[tp].t != TK_END) { if (is_op('(')) d++; if (is_op(')')) { if (!d) break; d--; } tp++; }
            int after = tp; need(')', "')' erwartet");
            nlp++; lp[nlp - 1].nbrk = lp[nlp - 1].ncont = 0; lp[nlp - 1].sw = 0; stmt();
            int ct = ncur; int save = tp; tp = upd;
            while (tp < after && !cerr[0]) { expr(); emit(O_POP, 0); if (!eat(',')) break; }
            tp = save;
            int j = emit(O_JMP, 0); patch_to(j, top); if (jz >= 0) patch(jz); loop_end(ct); nlv = sv; return;
        }
        if (gl(n, "return")) {
            tp++; if (is_op(';')) { tp++; emit(O_RET, 0); return; }
            expr(); need(';', "';' erwartet"); emit(O_RETV, 0); return;
        }
        if (gl(n, "break")) { tp++; eat(';'); if (!nlp) { fehler("break ausserhalb einer Schleife"); return; } struct loop* l = &lp[nlp - 1]; if (l->nbrk < 16) l->brk[l->nbrk++] = emit(O_JMP, 0); return; }
        if (gl(n, "continue")) {
            tp++; eat(';'); int k = nlp - 1; while (k >= 0 && lp[k].sw) k--; if (k < 0) { fehler("continue ausserhalb einer Schleife"); return; }
            if (lp[k].ncont < 16) lp[k].cont[lp[k].ncont++] = emit(O_JMP, 0); return;
        }
        if (gl(n, "switch")) {
            tp++; need('(', "'(' erwartet"); int sv = nlv; int tmp = hidden_local(); expr(); emit(O_STL, lv_[tmp].slot); need(')', "')' erwartet"); need('{', "'{' erwartet");
            int jd = emit(O_JMP, 0); nlp++; lp[nlp - 1].nbrk = lp[nlp - 1].ncont = 0; lp[nlp - 1].sw = 1;
            int cv[40], cp[40], nc = 0, defp = -1;
            while (!is_op('}') && tk[tp].t != TK_END && !cerr[0]) {
                if (is_id("case")) { tp++; int neg = eat('-'); int v = 0; if (tk[tp].t == TK_NUM) { v = tk[tp].v; tp++; } else { fehler("case braucht eine Zahl oder ein Zeichen"); return; } if (neg) v = -v; need(':', "':' erwartet"); if (nc < 40) { cv[nc] = v; cp[nc++] = ncur; } }
                else if (is_id("default")) { tp++; need(':', "':' erwartet"); defp = ncur; }
                else stmt();
            }
            need('}', "'}' erwartet");
            int jend = emit(O_JMP, 0); patch(jd);
            for (int i = 0; i < nc; i++) { emit(O_LDL, lv_[tmp].slot); emit(O_PUSH, cv[i]); emit(O_EQ, 0); int j = emit(O_JNZ, 0); patch_to(j, cp[i]); }
            if (defp >= 0) { int j = emit(O_JMP, 0); patch_to(j, defp); }
            patch(jend); loop_end(ncur); nlv = sv; return;
        }
    }
    { int save = tp; skip_mods(); int ty = read_type(); if (ty >= 0 && tk[tp].t == TK_ID) { decl_rest(ty); need(';', "';' erwartet"); return; } tp = save; }
    expr(); need(';', "';' erwartet"); emit(O_POP, 0);
}

/* ---------- Oberste Ebene: Klassen, Funktionen, globale Variablen ---------- */
static void skip_body(void) { int d = 0; do { if (is_op('{')) d++; else if (is_op('}')) d--; tp++; } while (d > 0 && tk[tp].t != TK_END); }
static void skip_parens(void) { int d = 0; do { if (is_op('(')) d++; else if (is_op(')')) d--; tp++; } while (d > 0 && tk[tp].t != TK_END); }
static void skip_init(void) {
    int d = 0;
    while (tk[tp].t != TK_END) {
        if (is_op('(') || is_op('{') || is_op('[')) d++;
        else if (is_op(')') || is_op('}') || is_op(']')) { if (!d) break; d--; }
        else if (!d && (is_op(',') || is_op(';'))) break;
        tp++;
    }
}
static int ibuf[CODE_MAX * 2]; static int nibuf;

static void global_init(int gi, int ty) {
    int* sb = cb; int sn = ncur, sm = cmax; cb = ibuf; ncur = nibuf; cmax = CODE_MAX; nlv = 0;
    if (is_op('{')) {
        tp++; int cnt = 0, sv = tp, d = 0; while (tk[tp].t != TK_END) { if (is_op('{')) d++; if (is_op('}')) { if (!d) break; d--; } if (is_op(',') && !d) cnt++; tp++; }
        if (tp > sv && !(tk[tp - 1].t == TK_OP && tk[tp - 1].v == ',')) cnt++;
        tp = sv; emit(O_PUSH, cnt); emit(O_NEWA, tag_of(ty)); emit(O_STG, gi); int n = 0;
        while (!is_op('}') && tk[tp].t != TK_END) { emit(O_LDG, gi); emit(O_PUSH, n++); expr(); emit(O_ASETV, 0); emit(O_POP, 0); if (!eat(',')) break; }
        need('}', "'}' erwartet");
    } else { int rt = assign(); if (ty == T_STR && rt != T_STR) emit(O_ITOS, 0); emit(O_STG, gi); }
    nibuf = ncur; cb = sb; ncur = sn; cmax = sm;
}

static int read_params(int* pt, int* pn) {          /* tp hinter '(' ; verbraucht ')' ; -1 bei Fehler */
    int na = 0;
    if (!is_op(')')) {
        do { skip_mods(); int pty = read_type(); if (pty < 0 || tk[tp].t != TK_ID) { fehler("Parameter: Typ und Name erwartet"); return -1; }
             if (na < 16) { pt[na] = pty; pn[na] = tp; na++; } tp++;
             while (is_op('[') && tk[tp + 1].t == TK_OP && tk[tp + 1].v == ']') { tp += 2; if (na > 0 && na <= 16) pt[na - 1] = T_ARR; } } while (eat(','));
    }
    need(')', "')' erwartet"); return na;
}
static void field_inits(int cid) {
    struct cls* C = &V->cl[cid]; int off = cls_off(cid);
    for (int i = 0; i < C->nown; i++) if (fin_tp[cid][i] >= 0) {
        int save = tp; tp = fin_tp[cid][i]; int ty = C->ftype[i];
        emit(O_LDL, 0);
        if (is_op('{')) array_lit(tag_of(ty)); else { int rt = assign(); if (ty == T_STR && rt != T_STR) emit(O_ITOS, 0); }
        emit(O_SETF, off + i); emit(O_POP, 0); tp = save;
    }
}
static void parent_ctor0(int cid) {
    int par = V->cl[cid].parent; if (par < 0) return;
    int ci = find_ctor(par, 0); if (ci >= 0) { emit(O_LDL, 0); emit(O_CALL, ci); emit(O_POP, 0); }
}
static void class_body(int pass, int cid) {
    struct cls* C = &V->cl[cid]; int sv_cls = cur_cls, sv_st = cur_static; int has_ctor = 0;
    if (!is_op('{')) { fehler("'{' erwartet"); return; }
    tp++;
    while (!is_op('}') && tk[tp].t != TK_END && !cerr[0]) {
        skip_mods(); int st = mods_st; int fst = mods_st || C->isif;
        cur_cls = cid; cur_static = st;
        if (eat(';')) continue;
        if (is_op('{')) { skip_body(); continue; }
        if (tk[tp].t != TK_ID) { fehler("Unerwartetes Zeichen in der Klasse"); return; }
        const char* n = idn(tp);
        if (gl(n, "class") || gl(n, "interface") || gl(n, "enum")) {
            int cc = tk[tp + 1].t == TK_ID ? class_find(idn(tp + 1)) : -1;
            while (!is_op('{') && tk[tp].t != TK_END) tp++;
            if (cc >= 0 && !V->cl[cc].flat && !gl(n, "enum")) class_body(pass, cc); else skip_body();
            continue;
        }
        if (gl(n, C->name) && tk[tp + 1].t == TK_OP && tk[tp + 1].v == '(') {          /* Konstruktor */
            tp += 2; int pt[16], pn[16]; int na = read_params(pt, pn); if (na < 0) return;
            while (!is_op('{') && tk[tp].t != TK_END) tp++;
            has_ctor = 1; int fi = reg_method(cid, "<init>", na + 1, T_VOID, 0);
            if (pass == 1) { skip_body(); continue; }
            nlv = 0; nloc_max = 0; nlp = 0; add_this(cid); for (int i = 0; i < na; i++) add_local(pn[i], pt[i]);
            if (nlv > nloc_max) nloc_max = nlv;
            V->fns[fi].pc = ncur; cur_cls = cid; cur_static = 0; cur_ret = T_VOID;
            need('{', "'{' erwartet");
            if (is_id("this") && tk[tp + 1].t == TK_OP && tk[tp + 1].v == '(') { fehler("this(...) wird nicht unterstuetzt"); return; }
            if (is_id("super") && tk[tp + 1].t == TK_OP && tk[tp + 1].v == '(') {
                tp++; int sa = count_args(); int par = C->parent; int ci = par >= 0 ? find_ctor(par, sa) : -1;
                if (par >= 0 && ci < 0) { fehler("Konstruktor der Oberklasse passt nicht"); return; }
                emit(O_LDL, 0); call_args(); if (ci >= 0) { emit(O_CALL, ci); emit(O_POP, 0); } else for (int i = 0; i <= sa; i++) emit(O_POP, 0);
                eat(';');
            } else parent_ctor0(cid);
            field_inits(cid);
            while (!is_op('}') && tk[tp].t != TK_END && !cerr[0]) stmt();
            need('}', "'}' erwartet"); emit(O_RET, 0); nlv = 0;
            V->fns[fi].nloc = nloc_max < na + 1 ? na + 1 : nloc_max;
            continue;
        }
        int ty = read_type(); if (ty < 0 || tk[tp].t != TK_ID) { fehler("Typ und Name erwartet"); return; }
        int ni = tp; tp++;
        if (is_op('(')) {                                                       /* Methode */
            tp++; int pt[16], pn[16]; int na = read_params(pt, pn); if (na < 0) return;
            while (!is_op('{') && !is_op(';') && tk[tp].t != TK_END) tp++;
            int nargs = na + (st ? 0 : 1); int fi = reg_method(cid, idn(ni), nargs, ty, st);
            if (pass == 1 || !is_op('{')) { if (is_op('{')) skip_body(); else tp++; continue; }
            nlv = 0; nloc_max = 0; nlp = 0; if (!st) add_this(cid); for (int i = 0; i < na; i++) add_local(pn[i], pt[i]);
            if (nlv > nloc_max) nloc_max = nlv;
            V->fns[fi].pc = ncur; cur_cls = cid; cur_static = st; cur_ret = ty;
            block_();
            if (ty == T_VOID) emit(O_RET, 0); else { emit(O_PUSH, 0); emit(O_RETV, 0); }
            V->fns[fi].nloc = nloc_max < nargs ? nargs : nloc_max;
            continue;
        }
        tp = ni;                                                                /* Felder */
        do {
            if (tk[tp].t != TK_ID) { fehler("Variablenname erwartet"); return; }
            int vi = tp; tp++; int vt = ty;
            if (is_op('[') && tk[tp + 1].t == TK_OP && tk[tp + 1].v == ']') { tp += 2; vt = ty >= T_OBJ0 && ty < T_OARR0 ? T_OARR0 + (ty - T_OBJ0) : T_ARR; }
            if (fst) {
                char m[44]; mangle(m, cid, idn(vi));
                if (pass == 1) { add_global(m, vt); if (eat('=')) skip_init(); }
                else { int gi = find_global(m); if (eat('=')) global_init(gi, V->gtype[gi]); }
            } else if (pass == 1) {
                if (C->nown >= MAXF) { fehler("Zu viele Felder in einer Klasse (hoechstens 32)"); return; }
                int idx = C->nown++; kop(C->fname[idx], idn(vi), 20); C->ftype[idx] = (uint8_t)vt; fin_tp[cid][idx] = -1;
                if (eat('=')) { fin_tp[cid][idx] = tp; skip_init(); }
            } else if (eat('=')) skip_init();
        } while (eat(','));
        need(';', "';' erwartet");
    }
    need('}', "'}' erwartet");
    cur_cls = cid; cur_static = 0;
    if (!has_ctor && !C->isif) {                                                /* Standard-Konstruktor */
        int fi = reg_method(cid, "<init>", 1, T_VOID, 0);
        if (pass == 1) V->fns[fi].synth = 1;
        else if (!cerr[0]) { nlv = 0; nloc_max = 0; add_this(cid); V->fns[fi].pc = ncur; parent_ctor0(cid); field_inits(cid); emit(O_RET, 0); nlv = 0; V->fns[fi].nloc = 1; }
    }
    cur_cls = sv_cls; cur_static = sv_st;
}

static void toplevel(int pass) {
    tp = 0; cur_cls = -1; cur_static = 0;
    while (tk[tp].t != TK_END && !cerr[0]) {
        skip_mods();
        if (tk[tp].t == TK_OP) { tp++; continue; }                        /* Ende einer Klasse oder Semikolon */
        if (tk[tp].t != TK_ID) { fehler("Unerwartetes Zeichen"); return; }
        const char* n = idn(tp);
        if (gl(n, "import") || gl(n, "package")) { while (!is_op(';') && tk[tp].t != TK_END) tp++; tp++; continue; }
        if (gl(n, "class") || gl(n, "interface")) {
            int cc = tk[tp + 1].t == TK_ID ? class_find(idn(tp + 1)) : -1;
            if (cc >= 0 && !V->cl[cc].flat) { while (!is_op('{') && tk[tp].t != TK_END) tp++; class_body(pass, cc); cur_cls = -1; cur_static = 0; continue; }
        }
        if (gl(n, "class") || gl(n, "interface") || gl(n, "enum")) { while (!is_op('{') && tk[tp].t != TK_END) tp++; tp++; continue; }
        if (tk[tp + 1].t == TK_OP && tk[tp + 1].v == '(') {               /* Konstruktor o.ae.: ueberspringen */
            tp++; skip_parens(); while (!is_op('{') && tk[tp].t != TK_END) tp++; skip_body(); continue;
        }
        int ty = read_type(); if (ty < 0 || tk[tp].t != TK_ID) { fehler("Typ und Name erwartet"); return; }
        int ni = tp; tp++;
        if (is_op('(')) {                                                  /* Funktion */
            nlv = 0; nloc_max = 0; int na = 0; int pt[16], pn[16]; tp++;
            if (!is_op(')')) {
                do { skip_mods(); int pty = read_type(); if (pty < 0 || tk[tp].t != TK_ID) { fehler("Parameter: Typ und Name erwartet"); return; } if (na < 16) { pt[na] = pty; pn[na] = tp; na++; } tp++;
                     while (is_op('[') && tk[tp + 1].t == TK_OP && tk[tp + 1].v == ']') { tp += 2; if (na > 0 && na <= 16) pt[na - 1] = T_ARR; } } while (eat(','));
            }
            need(')', "')' erwartet");
            while (!is_op('{') && !is_op(';') && tk[tp].t != TK_END) tp++;     /* throws ... */
            if (pass == 1) {
                const char* fname = idn(ni); int fi = find_fn(fname);
                if (fi < 0) { if (V->nfn >= NFN_MAX) { fehler("Zu viele Funktionen"); return; } fi = V->nfn++; kop(V->fns[fi].name, fname, 44); V->fns[fi].cls = -1; V->fns[fi].stat = 1; V->fns[fi].synth = 0; V->fns[fi].mh = hash_(fname); }
                V->fns[fi].nargs = na; V->fns[fi].ret = ty; V->fns[fi].def = 1; V->fns[fi].pc = -1;
                if (is_op('{')) skip_body(); else tp++;
            } else {
                int fi = find_fn(idn(ni)); if (fi < 0) { fehler("interner Fehler: Funktion unbekannt"); return; }
                if (!is_op('{')) { tp++; continue; }
                nlv = 0; nloc_max = 0; for (int i = 0; i < na; i++) add_local(pn[i], pt[i]);
                if (nlv > nloc_max) nloc_max = nlv;
                V->fns[fi].pc = ncur; cur_ret = ty; nlp = 0;
                block_();
                if (ty == T_VOID) emit(O_RET, 0); else { emit(O_PUSH, 0); emit(O_RETV, 0); }
                V->fns[fi].nloc = nloc_max < na ? na : nloc_max;
            }
            continue;
        }
        /* globale Variablen */
        tp = ni;
        do {
            if (tk[tp].t != TK_ID) { fehler("Variablenname erwartet"); return; }
            int vi = tp; tp++; int vt = ty;
            if (is_op('[') && tk[tp + 1].t == TK_OP && tk[tp + 1].v == ']') { tp += 2; vt = T_ARR; }
            if (pass == 1) { add_global(idn(vi), vt); if (eat('=')) skip_init(); }
            else { int gi = find_global(idn(vi)); if (eat('=')) global_init(gi, V->gtype[gi]); }
        } while (eat(','));
        need(';', "';' erwartet");
    }
}

/* ====================== Maschine ====================== */
static void rt_fehler(struct zjv* v, const char* m) { if (v->ok) { v->ok = 0; kop(v->fehler, "Laufzeitfehler: ", 120); int l = slen(v->fehler); kop(v->fehler + l, m, 120 - l); kprint("[ZYNJAVA] "); kprint(v->fehler); kprint("\n"); } }
static int h_neu(struct zjv* v, int n, int tag) {
    if (n < 0 || n > 20000) { rt_fehler(v, "Feldgroesse ungueltig"); return 0; }
    if (v->hp + n + 1 >= HEAP_MAX) { rt_fehler(v, "Speicher voll (zu viele Felder oder Objekte)"); return 0; }
    int h = v->hp; v->heap[h] = n | (tag << 16); for (int i = 1; i <= n; i++) v->heap[h + i] = 0; v->hp += n + 1; return h;
}
static int is_obj(struct zjv* v, int h) { if (h <= 0 || h >= v->hp) return 0; int t = v->heap[h] >> 16; return t >= TAG_OBJ && t < TAG_OARR; }
static int cls_is(struct zjv* v, int c, int target, int depth) {
    if (c < 0 || depth > 20) return 0; if (c == target) return 1;
    for (int i = 0; i < 3; i++) if (v->cl[c].ifc[i] >= 0 && cls_is(v, v->cl[c].ifc[i], target, depth + 1)) return 1;
    return cls_is(v, v->cl[c].parent, target, depth + 1);
}
/* ---------- Speicherbereinigung (am Anfang jedes Ereignisses; Stapel ist dann leer) ---------- */
static uint8_t gc_m[HEAP_MAX];                         /* 2 = Blockanfang, 1 = erreichbar */
static int gc_fwd[HEAP_MAX], gc_stk[8192], gc_smap[NSTR_MAX], gc_soff[NSTR_MAX];
static uint8_t gc_sm[NSTR_MAX];
static char gc_tmp[SPOOL_MAX];
static int gc_slot(struct zjv* v, int h, int i) {         /* 0 Zahl, 1 Text, 2 Verweis */
    int tag = v->heap[h] >> 16;
    if (tag == 0) return 0;
    if (tag == TAG_ARR2 || tag >= TAG_OARR) return 2;
    int c = tag - TAG_OBJ; if (c < 0 || c >= v->ncl) return 0;
    int t = v->cl[c].ftot[i]; return t == T_STR ? 1 : is_ref(t) ? 2 : 0;
}
static void gc(struct zjv* v) {
    if (v->spn < SPOOL_MAX * 6 / 10 && v->nstr < NSTR_MAX * 6 / 10 && v->hp < HEAP_MAX / 2) return;
    int hp = v->hp, ns = v->nstr, sk = 0;
    for (int h = 1; h < hp; ) { int n = v->heap[h] & 0xFFFF; gc_m[h] = 2; for (int k = 1; k <= n && h + k < hp; k++) gc_m[h + k] = 0; h += n + 1; }
    for (int i = 0; i < ns; i++) gc_sm[i] = 0;
    for (int i = 0; i < v->ng; i++) {
        int t = v->gtype[i], x = v->g[i];
        if (t == T_STR) { if (x > 0 && x < ns) gc_sm[x] = 1; }
        else if (is_ref(t) && x > 0 && x < hp && (gc_m[x] & 2) && !(gc_m[x] & 1)) { gc_m[x] |= 1; if (sk >= 8192) return; gc_stk[sk++] = x; }
    }
    while (sk > 0) {
        int h = gc_stk[--sk], n = v->heap[h] & 0xFFFF;
        for (int i = 0; i < n; i++) {
            int k = gc_slot(v, h, i), x = v->heap[h + 1 + i];
            if (k == 1) { if (x > 0 && x < ns) gc_sm[x] = 1; }
            else if (k == 2 && x > 0 && x < hp && (gc_m[x] & 2) && !(gc_m[x] & 1)) { gc_m[x] |= 1; if (sk >= 8192) return; gc_stk[sk++] = x; }
        }
    }
    /* neue Adressen der Bloecke */
    int np = 1; for (int h = 1; h < hp; ) { int n = v->heap[h] & 0xFFFF; if (gc_m[h] & 1) { gc_fwd[h] = np; np += n + 1; } h += n + 1; }
    /* Texte neu ordnen (feste Texte bleiben stehen) */
    int nh = v->nconst, tn = 0;
    for (int i = 0; i < v->nconst && i < ns; i++) gc_smap[i] = i;
    for (int i = v->nconst; i < ns; i++) {
        gc_smap[i] = 0; if (!gc_sm[i]) continue;
        const char* t = v->spool + v->soff[i]; int l = slen(t); if (tn + l + 1 > SPOOL_MAX) continue;
        gc_soff[nh] = tn; for (int k = 0; k < l; k++) gc_tmp[tn++] = t[k]; gc_tmp[tn++] = 0; gc_smap[i] = nh++;
    }
    for (int j = v->nconst; j < nh; j++) { v->soff[j] = v->spconst + gc_soff[j]; }
    for (int k = 0; k < tn; k++) v->spool[v->spconst + k] = gc_tmp[k];
    /* Verweise anpassen */
    for (int i = 0; i < v->ng; i++) {
        int t = v->gtype[i], x = v->g[i];
        if (t == T_STR) v->g[i] = (x > 0 && x < ns) ? gc_smap[x] : 0;
        else if (is_ref(t) && x > 0 && x < hp && (gc_m[x] & 1)) v->g[i] = gc_fwd[x];
    }
    for (int h = 1; h < hp; ) {
        int n = v->heap[h] & 0xFFFF;
        if (gc_m[h] & 1) for (int i = 0; i < n; i++) {
            int k = gc_slot(v, h, i), x = v->heap[h + 1 + i];
            if (k == 1) v->heap[h + 1 + i] = (x > 0 && x < ns) ? gc_smap[x] : 0;
            else if (k == 2 && x > 0 && x < hp && (gc_m[x] & 1)) v->heap[h + 1 + i] = gc_fwd[x];
        }
        h += n + 1;
    }
    /* Bloecke nach unten schieben */
    for (int h = 1; h < hp; ) { int n = v->heap[h] & 0xFFFF; if (gc_m[h] & 1) { int d = gc_fwd[h]; if (d != h) for (int k = 0; k <= n; k++) v->heap[d + k] = v->heap[h + k]; } h += n + 1; }
    v->hp = np; v->nstr = nh; v->spn = v->spconst + tn;
}

static const int SINT[91] = {0,17,35,52,70,87,105,122,139,156,174,191,208,225,242,259,276,292,309,326,342,358,375,391,407,423,438,454,469,485,500,515,530,545,559,574,588,602,616,629,643,656,669,682,695,707,719,731,743,755,766,777,788,799,809,819,829,839,848,857,866,875,883,891,899,906,914,921,927,934,940,946,951,956,961,966,970,974,978,982,985,988,990,993,995,996,998,999,999,1000,1000};
static int sin_deg(int d) { d %= 360; if (d < 0) d += 360; int neg = 0; if (d >= 180) { d -= 180; neg = 1; } if (d > 90) d = 180 - d; return neg ? -SINT[d] : SINT[d]; }
static uint32_t rgbc(int c) { return (uint32_t)c & 0x00FFFFFFu; }

static void builtin(struct zjv* v, int id) {
    int* S = v->stk; int r = 0;
    switch (id) {
    case B_COLOR1: v->farbe = rgbc(S[--v->sp]); break;
    case B_COLOR3: { int b = S[--v->sp], g = S[--v->sp], rr = S[--v->sp]; v->farbe = ((uint32_t)(rr & 255) << 16) | ((uint32_t)(g & 255) << 8) | (uint32_t)(b & 255); break; }
    case B_RGB: { int b = S[--v->sp], g = S[--v->sp], rr = S[--v->sp]; r = ((rr & 255) << 16) | ((g & 255) << 8) | (b & 255); break; }
    case B_FILLRECT: { int h = S[--v->sp], w = S[--v->sp], y = S[--v->sp], x = S[--v->sp]; fillr(v, x, y, w, h, v->farbe); break; }
    case B_DRAWRECT: { int h = S[--v->sp], w = S[--v->sp], y = S[--v->sp], x = S[--v->sp]; fillr(v, x, y, w + 1, 1, v->farbe); fillr(v, x, y + h, w + 1, 1, v->farbe); fillr(v, x, y, 1, h + 1, v->farbe); fillr(v, x + w, y, 1, h + 1, v->farbe); break; }
    case B_LINE: { int y1 = S[--v->sp], x1 = S[--v->sp], y0 = S[--v->sp], x0 = S[--v->sp]; line_(v, x0, y0, x1, y1, v->farbe); break; }
    case B_FILLOVAL: { int h = S[--v->sp], w = S[--v->sp], y = S[--v->sp], x = S[--v->sp]; oval_(v, x, y, w, h, v->farbe, 1); break; }
    case B_DRAWOVAL: { int h = S[--v->sp], w = S[--v->sp], y = S[--v->sp], x = S[--v->sp]; oval_(v, x, y, w, h, v->farbe, 0); break; }
    case B_STRING: { int y = S[--v->sp], x = S[--v->sp], s = S[--v->sp]; text_(v, s_text(v, s), x, y, v->farbe); break; }
    case B_FONT: { int n = S[--v->sp]; v->fscale = n >= 44 ? 4 : n >= 30 ? 3 : n >= 20 ? 2 : 1; break; }
    case B_WIDTH: r = v->w; break;
    case B_HEIGHT: r = v->h; break;
    case B_PIXEL: { int y = S[--v->sp], x = S[--v->sp]; px_(v, x, y, v->farbe); break; }
    case B_CLEARALL: fillr(v, 0, 0, v->w, v->h, v->farbe); break;
    case B_NOP0: break;
    case B_NOP1: v->sp--; break;
    case B_ABS: r = S[--v->sp]; if (r < 0) r = -r; break;
    case B_MIN: { int b = S[--v->sp], a = S[--v->sp]; r = a < b ? a : b; break; }
    case B_MAX: { int b = S[--v->sp], a = S[--v->sp]; r = a > b ? a : b; break; }
    case B_SQRT: { int a = S[--v->sp]; if (a < 0) a = 0; int x = 0; while ((long long)(x + 1) * (x + 1) <= a) x++; r = x; break; }
    case B_SIN: r = sin_deg(S[--v->sp]); break;
    case B_COS: r = sin_deg(S[--v->sp] + 90); break;
    case B_RND: { int n = S[--v->sp]; v->rnd ^= v->rnd << 13; v->rnd ^= v->rnd >> 17; v->rnd ^= v->rnd << 5; r = n > 0 ? (int)(v->rnd % (uint32_t)n) : 0; break; }
    case B_POW: { int e = S[--v->sp], b = S[--v->sp]; r = 1; for (int i = 0; i < e && i < 64; i++) r *= b; break; }
    case B_SLEN: r = slen(s_text(v, S[--v->sp])); break;
    case B_CHARAT: { int i = S[--v->sp]; const char* t = s_text(v, S[--v->sp]); if (i < 0 || i >= slen(t)) rt_fehler(v, "charAt ausserhalb"); else r = (unsigned char)t[i]; break; }
    case B_SUBSTR: { int e = S[--v->sp], a = S[--v->sp]; const char* t = s_text(v, S[--v->sp]); int l = slen(t); if (e > l) e = l; if (a < 0) a = 0; if (a > e) a = e; r = s_neu(v, t + a, e - a); if (!r && e > a) rt_fehler(v, "Textspeicher voll"); break; }
    case B_INDEXOF: { int h2 = S[--v->sp]; const char* t = s_text(v, S[--v->sp]); const char* n = s_text(v, h2); int l = slen(t), m = slen(n); r = -1;
                      if (h2 > 0 && h2 < v->nstr) { for (int i = 0; i + m <= l; i++) { int ok = 1; for (int k = 0; k < m; k++) if (t[i + k] != n[k]) { ok = 0; break; } if (ok) { r = i; break; } } }
                      else for (int i = 0; i < l; i++) if ((unsigned char)t[i] == (unsigned)h2) { r = i; break; }
                      break; }
    case B_ISEMPTY: r = slen(s_text(v, S[--v->sp])) == 0; break;
    case B_UPPER: case B_LOWER: { static char b[400]; const char* t = s_text(v, S[--v->sp]); int l = slen(t); if (l > 399) l = 399; for (int i = 0; i < l; i++) { char c = t[i]; if (id == B_UPPER && c >= 'a' && c <= 'z') c = (char)(c - 32); if (id == B_LOWER && c >= 'A' && c <= 'Z') c = (char)(c + 32); b[i] = c; } r = s_neu(v, b, l); break; }
    case B_PARSE: { const char* t = s_text(v, S[--v->sp]); int neg = 0, i = 0; long long a = 0; if (t[0] == '-') { neg = 1; i = 1; } while (t[i] >= '0' && t[i] <= '9') a = a * 10 + (t[i++] - '0'); r = (int)(neg ? -a : a); break; }
    case B_PRINT: { const char* t = s_text(v, S[--v->sp]); kprint("[ZYNJAVA] "); kprint(t); kprint("\n"); break; }
    case B_MILLIS: r = (int)(timer_get_ticks() * 10u); break;
    case B_GETX: r = v->ex; break;
    case B_GETY: r = v->ey; break;
    case B_GETKEY: r = v->ekey; break;
    case B_TOCHR: { char c[2]; c[0] = (char)S[--v->sp]; c[1] = 0; r = s_neu(v, c, 1); break; }
    default: break;
    }
    S[v->sp++] = r;
}

static int call_fn(struct zjv* v, int fi, int a, int b) {
    if (fi < 0 || !v->ok) return 0;
    struct fn* f = &v->fns[fi]; int* S = v->stk; int cs_pc[64], cs_fp[64], depth = 0;
    v->sp = 0; v->fp = 0;
    for (int i = 0; i < f->nargs; i++) S[v->sp++] = i == 0 ? a : i == 1 ? b : 0;
    for (int i = f->nargs; i < f->nloc; i++) S[v->sp++] = 0;
    int pc = f->pc; int steps = 0; int* C = v->code;
    for (;;) {
        if (++steps > STEPS) { rt_fehler(v, "Programm laeuft zu lange (Endlosschleife?)"); return 0; }
        if (pc < 0 || pc >= v->ncode) { rt_fehler(v, "Sprung ins Leere"); return 0; }
        int op = C[pc * 2], ar = C[pc * 2 + 1]; pc++;
        if (v->sp > STK_MAX - 8 || v->sp < 0) { rt_fehler(v, "Stapel ueberlaeuft (Rekursion?)"); return 0; }
        switch (op) {
        case O_PUSH: S[v->sp++] = ar; break;
        case O_LDL: S[v->sp++] = S[v->fp + ar]; break;
        case O_STL: S[v->fp + ar] = S[--v->sp]; break;
        case O_LDG: S[v->sp++] = v->g[ar]; break;
        case O_STG: v->g[ar] = S[--v->sp]; break;
        case O_AGET: { int i = S[--v->sp], h = S[--v->sp]; if (h <= 0 || h >= v->hp) { rt_fehler(v, "Feld nicht angelegt"); return 0; } if (i < 0 || i >= (v->heap[h] & 0xFFFF)) { rt_fehler(v, "Feldindex ausserhalb"); return 0; } S[v->sp++] = v->heap[h + 1 + i]; break; }
        case O_ASETV: { int val = S[--v->sp], i = S[--v->sp], h = S[--v->sp]; if (h <= 0 || h >= v->hp) { rt_fehler(v, "Feld nicht angelegt"); return 0; } if (i < 0 || i >= (v->heap[h] & 0xFFFF)) { rt_fehler(v, "Feldindex ausserhalb"); return 0; } v->heap[h + 1 + i] = val; S[v->sp++] = val; break; }
        case O_ALEN: { int h = S[--v->sp]; if (h <= 0 || h >= v->hp) { rt_fehler(v, "Feld nicht angelegt"); return 0; } S[v->sp++] = v->heap[h] & 0xFFFF; break; }
        case O_NEWA: { int n = S[--v->sp]; int h = h_neu(v, n, ar); if (!v->ok) return 0; S[v->sp++] = h; break; }
        case O_NEWA2: { int m = S[--v->sp], n = S[--v->sp]; int h = h_neu(v, n, TAG_ARR2); if (!v->ok) return 0; for (int i = 0; i < n; i++) { int in = h_neu(v, m, 0); if (!v->ok) return 0; v->heap[h + 1 + i] = in; } S[v->sp++] = h; break; }
        case O_DUP: S[v->sp] = S[v->sp - 1]; v->sp++; break;
        case O_DUP2: S[v->sp] = S[v->sp - 2]; S[v->sp + 1] = S[v->sp - 1]; v->sp += 2; break;
        case O_POP: v->sp--; break;
        case O_ADD: { int b2 = S[--v->sp]; S[v->sp - 1] = (int)((unsigned)S[v->sp - 1] + (unsigned)b2); break; }
        case O_SUB: { int b2 = S[--v->sp]; S[v->sp - 1] = (int)((unsigned)S[v->sp - 1] - (unsigned)b2); break; }
        case O_MUL: { int b2 = S[--v->sp]; S[v->sp - 1] = (int)((unsigned)S[v->sp - 1] * (unsigned)b2); break; }
        case O_DIV: { int b2 = S[--v->sp]; if (!b2) { rt_fehler(v, "Division durch null"); return 0; } S[v->sp - 1] = (int)((long long)S[v->sp - 1] / b2); break; }
        case O_MOD: { int b2 = S[--v->sp]; if (!b2) { rt_fehler(v, "Division durch null"); return 0; } S[v->sp - 1] = (int)((long long)S[v->sp - 1] % b2); break; }
        case O_AND: { int b2 = S[--v->sp]; S[v->sp - 1] &= b2; break; }
        case O_OR: { int b2 = S[--v->sp]; S[v->sp - 1] |= b2; break; }
        case O_XOR: { int b2 = S[--v->sp]; S[v->sp - 1] ^= b2; break; }
        case O_SHL: { int b2 = S[--v->sp]; S[v->sp - 1] = (int)((unsigned)S[v->sp - 1] << (b2 & 31)); break; }
        case O_SHR: { int b2 = S[--v->sp]; S[v->sp - 1] >>= (b2 & 31); break; }
        case O_USHR: { int b2 = S[--v->sp]; S[v->sp - 1] = (int)((unsigned)S[v->sp - 1] >> (b2 & 31)); break; }
        case O_LT: { int b2 = S[--v->sp]; S[v->sp - 1] = S[v->sp - 1] < b2; break; }
        case O_LE: { int b2 = S[--v->sp]; S[v->sp - 1] = S[v->sp - 1] <= b2; break; }
        case O_GT: { int b2 = S[--v->sp]; S[v->sp - 1] = S[v->sp - 1] > b2; break; }
        case O_GE: { int b2 = S[--v->sp]; S[v->sp - 1] = S[v->sp - 1] >= b2; break; }
        case O_EQ: { int b2 = S[--v->sp]; S[v->sp - 1] = S[v->sp - 1] == b2; break; }
        case O_NE: { int b2 = S[--v->sp]; S[v->sp - 1] = S[v->sp - 1] != b2; break; }
        case O_NEG: S[v->sp - 1] = (int)(0u - (unsigned)S[v->sp - 1]); break;
        case O_NOT: S[v->sp - 1] = !S[v->sp - 1]; break;
        case O_BNOT: S[v->sp - 1] = ~S[v->sp - 1]; break;
        case O_ITOS: { char t[16]; dezi(t, S[v->sp - 1]); int h = s_neu(v, t, slen(t)); if (!h) { rt_fehler(v, "Textspeicher voll"); return 0; } S[v->sp - 1] = h; break; }
        case O_SCAT: {
            int b2 = S[--v->sp], a2 = S[v->sp - 1]; char t1[16], t2[16]; const char* x = s_text(v, a2); const char* y = s_text(v, b2);
            if (ar == 1) { dezi(t1, a2); x = t1; } if (ar == 2) { dezi(t2, b2); y = t2; }
            static char buf[900]; int l = 0; for (int i = 0; x[i] && l < 880; i++) buf[l++] = x[i]; for (int i = 0; y[i] && l < 899; i++) buf[l++] = y[i];
            int h = s_neu(v, buf, l); if (!h && l) { rt_fehler(v, "Textspeicher voll"); return 0; } S[v->sp - 1] = h; break; }
        case O_SEQ: { int b2 = S[--v->sp], a2 = S[v->sp - 1]; S[v->sp - 1] = gl(s_text(v, a2), s_text(v, b2)); break; }
        case O_JMP: pc += ar; break;
        case O_JZ: if (!S[--v->sp]) pc += ar; break;
        case O_JNZ: if (S[--v->sp]) pc += ar; break;
        case O_NEWO: { int h = h_neu(v, v->cl[ar].ntotal, TAG_OBJ + ar); if (!v->ok) return 0; S[v->sp++] = h; break; }
        case O_GETF: { int h = S[v->sp - 1]; if (!is_obj(v, h)) { rt_fehler(v, "Objekt ist null"); return 0; } if (ar >= (v->heap[h] & 0xFFFF)) { rt_fehler(v, "Feld ungueltig"); return 0; } S[v->sp - 1] = v->heap[h + 1 + ar]; break; }
        case O_SETF: { int val = S[--v->sp], h = S[v->sp - 1]; if (!is_obj(v, h)) { rt_fehler(v, "Objekt ist null"); return 0; } if (ar >= (v->heap[h] & 0xFFFF)) { rt_fehler(v, "Feld ungueltig"); return 0; } v->heap[h + 1 + ar] = val; S[v->sp - 1] = val; break; }
        case O_INSTOF: { int h = S[v->sp - 1]; S[v->sp - 1] = is_obj(v, h) && cls_is(v, (v->heap[h] >> 16) - TAG_OBJ, ar, 0); break; }
        case O_CALLM: {
            struct fn* sf = &v->fns[ar]; int h = S[v->sp - sf->nargs];
            if (!is_obj(v, h)) { rt_fehler(v, "Methode auf null aufgerufen"); return 0; }
            int c = (v->heap[h] >> 16) - TAG_OBJ, tgt = -1;
            if (v->cl[c].parent < 0 && sf->cls == c && sf->pc >= 0) tgt = ar;
            for (int d = 0; c >= 0 && d < 20 && tgt < 0; d++, c = v->cl[c].parent)
                for (int j = 0; j < v->nfn; j++) if (v->fns[j].cls == c && v->fns[j].mh == sf->mh && v->fns[j].nargs == sf->nargs && v->fns[j].pc >= 0) { tgt = j; break; }
            if (tgt < 0) { rt_fehler(v, "Methode nicht gefunden (abstrakt?)"); return 0; }
            ar = tgt; }
            /* weiter wie ein normaler Aufruf */
        /* fall through */
        case O_CALL: {
            struct fn* g2 = &v->fns[ar]; if (depth >= 60) { rt_fehler(v, "Rekursion zu tief"); return 0; }
            if (g2->pc < 0) { rt_fehler(v, "Funktion nicht definiert"); return 0; }
            cs_pc[depth] = pc; cs_fp[depth] = v->fp; depth++;
            int nf = v->sp - g2->nargs; for (int i = g2->nargs; i < g2->nloc; i++) S[nf + i] = 0; v->fp = nf; v->sp = nf + (g2->nloc > g2->nargs ? g2->nloc : g2->nargs); pc = g2->pc; break; }
        case O_RET: case O_RETV: {
            int val = op == O_RET ? 0 : S[v->sp - 1];
            v->sp = v->fp;
            if (depth == 0) return 1;
            depth--; pc = cs_pc[depth]; v->fp = cs_fp[depth]; S[v->sp++] = val; break; }
        case O_BI: builtin(v, ar); if (!v->ok) return 0; break;
        case O_HALT: return 1;
        default: rt_fehler(v, "ungueltiger Befehl"); return 0;
        }
    }
}

/* ---------- Schnittstelle ---------- */
static int fn_idx(struct zjv* v, const char* n) { for (int i = 0; i < v->nfn; i++) if (gl(v->fns[i].name, n) && v->fns[i].pc >= 0) return i; return -1; }
static int fn_alias(struct zjv* v, const char* a, const char* b, const char* c) { int i = fn_idx(v, a); if (i < 0 && b) i = fn_idx(v, b); if (i < 0 && c) i = fn_idx(v, c); return i; }

int zj_laden(int slot, const char* src, int w, int h) {
    if (slot < 0 || slot >= ZJ_MAX) return -1;
    struct zjv* v = &vm_[slot]; V = v;
    if (w < 40) w = 40; if (w > ZJ_MAXW) w = ZJ_MAXW; if (h < 40) h = 40; if (h > ZJ_MAXH) h = ZJ_MAXH;
    v->ok = 0; v->fehler[0] = 0; v->w = w; v->h = h; v->ncode = 0; v->nfn = 0; v->ng = 0; v->hp = 1; v->nstr = 1; v->spn = 0; v->soff[0] = 0;
    v->farbe = 0; v->fscale = 1; v->rnd = 2463534242u ^ (timer_get_ticks() * 2654435761u); if (!v->rnd) v->rnd = 1; v->ex = v->ey = v->ekey = 0;
    v->f_start = v->f_paint = v->f_tick = v->f_down = v->f_up = v->f_key = v->f_move = -1;
    for (int i = 0; i < NG_MAX; i++) { v->g[i] = 0; v->gtype[i] = T_INT; }
    for (int i = 0; i < w * h; i++) v->px[i] = 0x00FFFFFFu;
    cerr[0] = 0; cerr_zeile = 0; nibuf = 0; nlv = 0; nlp = 0;
    if (lex(src) < 0) { kop(v->fehler, cerr, 120); return -1; }
    cb = v->code; cmax = CODE_MAX; ncur = 0; cur_cls = -1; cur_static = 0;
    class_scan();
    if (!cerr[0]) toplevel(1);
    if (!cerr[0]) layout();
    if (!cerr[0]) { ncur = 0; toplevel(2); }
    if (!cerr[0]) {
        v->ncode = ncur; v->init_pc = ncur;
        if (ncur + nibuf + 2 >= CODE_MAX) fehler("Programm zu lang");
        else { for (int i = 0; i < nibuf * 2; i++) v->code[ncur * 2 + i] = ibuf[i]; ncur += nibuf; v->code[ncur * 2] = O_HALT; v->code[ncur * 2 + 1] = 0; ncur++; v->ncode = ncur; }
    }
    if (cerr[0]) {
        char z[12]; dezi(z, cerr_zeile); kop(v->fehler, "Zeile ", 120); int l = slen(v->fehler); kop(v->fehler + l, z, 120 - l); l = slen(v->fehler); kop(v->fehler + l, ": ", 120 - l); l = slen(v->fehler); kop(v->fehler + l, cerr, 120 - l);
        kprint("[ZYNJAVA] Fehler: "); kprint(v->fehler); kprint("\n"); return -1;
    }
    v->nconst = v->nstr; v->spconst = v->spn;
    v->f_start = fn_alias(v, "init", "setup", "main");
    v->f_paint = fn_idx(v, "paint"); v->f_tick = fn_alias(v, "tick", "update", "step");
    v->f_down = fn_alias(v, "mouseDown", "mousePressed", "mouseClicked"); v->f_up = fn_alias(v, "mouseUp", "mouseReleased", 0);
    v->f_key = fn_alias(v, "keyTyped", "keyPressed", "keyDown"); v->f_move = fn_alias(v, "mouseMove", "mouseMoved", "mouseDragged");
    v->ok = 1; v->ladet = 1;
    v->sp = 0; v->fp = 0; { struct fn tmp; (void)tmp; }
    /* globale Anfangswerte, dann init(), start() */
    { int steps_ok = 1; struct fn* f0 = &v->fns[0]; (void)f0; (void)steps_ok;
      int save = v->nfn; if (v->nfn < NFN_MAX) { struct fn* st = &v->fns[v->nfn]; kop(st->name, "$init", 44); st->cls = -1; st->stat = 1; st->mh = 0; st->pc = v->init_pc; st->nargs = 0; st->nloc = 0; st->ret = T_VOID; st->def = 1; call_fn(v, v->nfn, 0, 0); } (void)save; }
    if (v->ok && v->f_start >= 0) call_fn(v, v->f_start, 0, 0);
    { int st2 = fn_idx(v, "start"); if (v->ok && st2 >= 0 && st2 != v->f_start) call_fn(v, st2, 0, 0); }
    if (v->ok && v->f_paint >= 0) call_fn(v, v->f_paint, 0, 0);
    kprint("[ZYNJAVA] Programm geladen: "); kprint_dec((uint32_t)v->ncode); kprint(" Befehle\n");
    return v->ok ? 0 : -1;
}

int zj_hat(int slot, int art) {
    if (slot < 0 || slot >= ZJ_MAX || !vm_[slot].ladet) return 0; struct zjv* v = &vm_[slot];
    return art == ZJ_PAINT ? v->f_paint >= 0 : art == ZJ_TICK ? v->f_tick >= 0 : art == ZJ_MDOWN ? v->f_down >= 0 : art == ZJ_MUP ? v->f_up >= 0 : art == ZJ_KEY ? v->f_key >= 0 : art == ZJ_MMOVE ? v->f_move >= 0 : 0;
}
int zj_ereignis(int slot, int art, int a, int b) {
    if (slot < 0 || slot >= ZJ_MAX) return 0; struct zjv* v = &vm_[slot]; if (!v->ladet || !v->ok) return 0;
    V = v; gc(v);
    int fi = art == ZJ_TICK ? v->f_tick : art == ZJ_MDOWN ? v->f_down : art == ZJ_MUP ? v->f_up : art == ZJ_KEY ? v->f_key : art == ZJ_MMOVE ? v->f_move : -1;
    if (art == ZJ_MDOWN || art == ZJ_MUP || art == ZJ_MMOVE) { v->ex = a; v->ey = b; } if (art == ZJ_KEY) v->ekey = a;
    if (fi >= 0) call_fn(v, fi, a, b);
    if (v->ok && v->f_paint >= 0 && art != ZJ_PAINT) call_fn(v, v->f_paint, 0, 0);
    else if (v->ok && art == ZJ_PAINT && v->f_paint >= 0) call_fn(v, v->f_paint, 0, 0);
    return fi >= 0 || v->f_paint >= 0;
}
int zj_breite(int slot) { return vm_[slot].w; }
int zj_hoehe(int slot) { return vm_[slot].h; }
void zj_beenden(int slot) { if (slot >= 0 && slot < ZJ_MAX) { vm_[slot].ladet = 0; vm_[slot].ok = 0; } }
