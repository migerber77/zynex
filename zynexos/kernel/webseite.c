/* Zynex-Browser-Engine, siehe include/webseite.h */
#include "webseite.h"
#include "bilder.h"
#include "net.h"
#include "pmm.h"

extern void kprint(const char* str);
extern void kprint_dec(uint32_t v);
extern int  gfx_text_fw(const char* s, int font);

/* Schriften (font_aa.c): 0 Mono, 4 Inter 15, 8 fett 15, 9 fett 20, 10 fett 26, 11 fett 34, 12 klein 13 */
enum { F_MONO = 0, F_NORM = 4, F_FETT = 8, F_H3 = 9, F_H2 = 10, F_H1 = 11, F_KLEIN = 12 };
static int fbox(int f) { return f == F_H3 ? 22 : f == F_H2 ? 30 : f == F_H1 ? 40 : 16; }
static int fzeile(int f) { return fbox(f) + (f == F_H1 || f == F_H2 ? 10 : f == F_H3 ? 8 : 7); }
#define C_TXT  0x00202124u
#define C_LINK 0x001A0DABu

struct wel wd_el[WD_EL]; int wd_nel;
#define TXT_MAX (900u * 1024u)
char wd_txt[TXT_MAX]; static uint32_t txtn;
char wd_links[WD_LINKS][300]; int wd_nlinks;
struct wbild wd_bild[WD_BILDER]; int wd_nbild;
struct wfeld wd_feld[WD_FELDER]; int wd_nfeld;
struct wform wd_form[WD_FORMS]; int wd_nform;
int wd_hoehe; uint32_t wd_hg = 0x00FFFFFFu;
char wd_titel[100], wd_url[300], wd_fehler[120];

static int lc(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }
static int slen(const char* s) { int n = 0; while (s[n]) n++; return n; }
static void kopie(char* d, const char* s, int max) { int i = 0; while (s[i] && i < max - 1) { d[i] = s[i]; i++; } d[i] = 0; }
static int praef(const char* s, const char* p) { for (; *p; s++, p++) if (lc((unsigned char)*s) != *p) return 0; return 1; }
static int gleich(const char* a, const char* b) { while (*a && lc((unsigned char)*a) == lc((unsigned char)*b)) { a++; b++; } return !*a && !*b; }
static int enthaelt(const char* s, const char* w) { for (; *s; s++) if (praef(s, w)) return 1; return 0; }
static int hexw(int c) { return c >= '0' && c <= '9' ? c - '0' : (c | 32) >= 'a' && (c | 32) <= 'f' ? (c | 32) - 'a' + 10 : -1; }

/* ---------- Adressen ---------- */
static void basis_teile(const char* b, char* host, int hm, char* pfad, int pm, int* https) {
    int i = 0; *https = praef(b, "https"); while (b[i] && !(b[i] == ':' && b[i + 1] == '/' && b[i + 2] == '/')) i++;
    if (!b[i]) { kopie(host, b, hm); kopie(pfad, "/", pm); return; }
    i += 3; while (b[i] && b[i] != '/' && b[i] != '?' && b[i] != '#') i++;
    int hl = i < hm - 1 ? i : hm - 1; for (int k = 0; k < hl; k++) host[k] = b[k]; host[hl] = 0;
    if (b[i] == '/') kopie(pfad, b + i, pm); else if (b[i] == '?') { pfad[0] = '/'; kopie(pfad + 1, b + i, pm - 1); } else kopie(pfad, "/", pm);
}
void wd_aufloesen(const char* basis, const char* href, char* out, int max) {
    out[0] = 0;
    while (*href == ' ' || *href == '\n' || *href == '\t') href++;
    if (!*href || href[0] == '#' || praef(href, "mailto:") || praef(href, "javascript:") || praef(href, "tel:") || praef(href, "data:")) return;
    char h[300]; kopie(h, href, 300);
    for (int i = 0; h[i]; i++) if (h[i] == '#') { h[i] = 0; break; }
    for (int i = 0; h[i]; i++) if (h[i] == '&' && h[i + 1] == 'a' && h[i + 2] == 'm' && h[i + 3] == 'p' && h[i + 4] == ';') { int j = i + 1; while (h[j + 4]) { h[j] = h[j + 4]; j++; } h[j] = 0; }
    for (int i = 0; h[i]; i++) if (h[i] == 'u' && praef(h + i, "uddg=")) {                       /* DuckDuckGo-Weiterleitung */
        int k = 0; i += 5; while (h[i] && h[i] != '&' && k < max - 1) { if (h[i] == '%' && hexw(h[i + 1]) >= 0 && hexw(h[i + 2]) >= 0) { out[k++] = (char)(hexw(h[i + 1]) * 16 + hexw(h[i + 2])); i += 3; } else out[k++] = h[i++]; }
        out[k] = 0; return;
    }
    if (praef(h, "http://") || praef(h, "https://")) { kopie(out, h, max); return; }
    char host[120], pfad[300]; int https; basis_teile(basis, host, 120, pfad, 300, &https);
    if (h[0] == '/' && h[1] == '/') { kopie(out, https ? "https:" : "http:", max); int n = slen(out); kopie(out + n, h, max - n); return; }
    kopie(out, host, max); int n = slen(out);
    if (h[0] == '/') { kopie(out + n, h, max - n); return; }
    if (h[0] == '?') { int q = 0; while (pfad[q] && pfad[q] != '?') q++; for (int i = 0; i < q && n < max - 1; i++) out[n++] = pfad[i]; kopie(out + n, h, max - n); return; }
    int q = slen(pfad); for (int i = 0; pfad[i]; i++) if (pfad[i] == '?') { q = i; break; }
    while (q > 0 && pfad[q - 1] != '/') q--;
    for (int i = 0; i < q && n < max - 1; i++) out[n++] = pfad[i];
    out[n] = 0;
    const char* r = h; while (r[0] == '.' && r[1] == '/') r += 2;
    while (r[0] == '.' && r[1] == '.' && r[2] == '/') { r += 3; int hl = slen(host); if (n > hl + 1) { n--; while (n > hl && out[n - 1] != '/') n--; out[n] = 0; } }
    kopie(out + n, r, max - n);
}

static int pool_neu;                                          /* neue Seite: Bildspeicher von vorn */
/* ---------- Bausteine ---------- */
enum { T_WORT = 1, T_BR, T_ABS, T_HR, T_BILD, T_FELD, T_LI, T_EINR, T_TAB_AN, T_TAB_AUS, T_TR_AN, T_TR_AUS, T_TD_AN, T_TD_AUS, T_PRE_AN, T_PRE_AUS, T_APP };
struct wapp wd_app[WD_APPS]; int wd_napp;
struct wtok { uint8_t typ, font, deko, sp; int16_t link, a; uint32_t farbe, off; };
#define TOK_MAX 110000
static struct wtok tok[TOK_MAX]; static int ntok;
static void t_neu(int typ, int a) { if (ntok >= TOK_MAX) return; struct wtok* t = &tok[ntok++]; t->typ = (uint8_t)typ; t->a = (int16_t)a; t->link = -1; t->font = 0; t->deko = 0; t->sp = 0; t->farbe = 0; t->off = 0; }

/* Zustand beim Zerlegen */
static int fett, kopf, mono, klein, link_, vor, sp_offen, liste_tiefe, in_title, latin1, unterstr;
static int tab_tiefe, tab_zeile[8], tab_zelle[8];
static int lnum[8], ltyp[8];                                  /* Listennummern je Tiefe (ltyp 1 = nummeriert) */
static uint32_t farbe_st[24]; static uint32_t farbe_tag[24]; static int nfarbe;
static int form_akt = -1, sel_feld = -1, ta_feld = -1;
static const char* basis_url;
static char wort[200]; static int wl;

static int akt_font(void) {
    if (kopf == 1) return F_H1; if (kopf == 2) return F_H2; if (kopf == 3 || kopf == 4) return F_H3; if (kopf) return F_FETT;
    if (mono) return F_MONO; if (fett) return F_FETT; if (klein) return F_KLEIN; return F_NORM;
}
static void wort_ende(void) {
    if (!wl) return;
    if (ntok >= TOK_MAX || txtn + (uint32_t)wl + 2 >= TXT_MAX) { wl = 0; return; }
    struct wtok* t = &tok[ntok++]; t->typ = T_WORT; t->font = (uint8_t)akt_font(); t->link = (int16_t)link_; t->sp = (uint8_t)sp_offen; t->a = 0;
    t->farbe = link_ >= 0 ? C_LINK : nfarbe ? farbe_st[nfarbe - 1] : C_TXT; t->deko = (uint8_t)((link_ >= 0 || unterstr) ? 1 : 0) | (uint8_t)(mono && !vor ? 2 : 0);
    t->off = txtn; for (int i = 0; i < wl; i++) wd_txt[txtn++] = wort[i]; wd_txt[txtn++] = 0;
    wl = 0; sp_offen = 0;
}
static void zeichen(int c) {
    if (in_title) { int n = slen(wd_titel); if (n < 98 && !(c <= ' ' && (n == 0 || wd_titel[n - 1] == ' '))) { wd_titel[n] = (char)(c <= ' ' ? ' ' : c); wd_titel[n + 1] = 0; } return; }
    if (ta_feld >= 0) { struct wfeld* f = &wd_feld[ta_feld]; int n = slen(f->wert); if (n < 198 && c >= ' ') { f->wert[n] = (char)c; f->wert[n + 1] = 0; } return; }
    if (sel_feld >= 0) return;
    if (vor) { if (c == '\n') { wort_ende(); t_neu(T_BR, 0); } else if (c == ' ' || c == '\t') { if (wl < 196) { wort[wl++] = ' '; if (c == '\t') { wort[wl++] = ' '; wort[wl++] = ' '; wort[wl++] = ' '; } } } else if (c >= 32 && c != '\r') { if (wl < 196) wort[wl++] = (char)c; else { wort_ende(); } } return; }
    if (c <= ' ') { wort_ende(); sp_offen = 1; return; }
    if (c == 0xAD) return;                                                     /* weiches Trennzeichen */
    if (wl < 190) wort[wl++] = (char)c; else { wort_ende(); wort[wl++] = (char)c; }
}
static int entitaet(const char* h, uint32_t len, uint32_t* i) {
    uint32_t j = *i + 1; char n[12]; int k = 0;
    while (j < len && k < 11 && h[j] != ';' && h[j] != ' ' && h[j] != '&' && h[j] != '<') n[k++] = h[j++];
    n[k] = 0; if (j >= len || h[j] != ';') return -1;
    int v = -1;
    if (n[0] == '#') { int x = 0, hex = (n[1] | 32) == 'x'; for (int q = hex ? 2 : 1; n[q]; q++) { int d = hexw(n[q]); if (d < 0 || (!hex && d > 9)) { x = -1; break; } x = x * (hex ? 16 : 10) + d; } v = x;
        if (v == 0x2013 || v == 0x2014 || v == 0x2212 || v == 0x2010 || v == 0x2011) v = '-'; else if (v == 0x2018 || v == 0x2019 || v == 0x201A) v = '\''; else if (v == 0x201C || v == 0x201D || v == 0x201E) v = '"'; else if (v == 0x2026) v = '.'; else if (v == 0x2022) v = 0xB7; else if (v == 0x20AC) v = 'E'; else if (v == 0x200B || v == 0x200C || v == 0x200D || v == 0xFEFF) v = 0xAD; else if (v == 0x2009 || v == 0x202F || v == 0x2002 || v == 0x2003) v = ' '; else if (v > 255) v = '?'; }
    else {
        static const struct { const char* n; int c; } t[] = { {"amp",'&'},{"lt",'<'},{"gt",'>'},{"quot",'"'},{"apos",'\''},{"nbsp",' '},{"auml",0xE4},{"ouml",0xF6},{"uuml",0xFC},{"Auml",0xC4},{"Ouml",0xD6},{"Uuml",0xDC},{"szlig",0xDF},
            {"copy",0xA9},{"reg",0xAE},{"euro",'E'},{"ndash",'-'},{"mdash",'-'},{"hellip",'.'},{"laquo",0xAB},{"raquo",0xBB},{"eacute",0xE9},{"egrave",0xE8},{"agrave",0xE0},{"aacute",0xE1},{"oacute",0xF3},{"iacute",0xED},{"uacute",0xFA},{"ccedil",0xE7},{"ntilde",0xF1},
            {"middot",0xB7},{"bull",0xB7},{"lsquo",'\''},{"rsquo",'\''},{"sbquo",'\''},{"ldquo",'"'},{"rdquo",'"'},{"bdquo",'"'},{"deg",0xB0},{"shy",0xAD},{"thinsp",' '},{"ensp",' '},{"emsp",' '},{"zwnj",0xAD},{"zwj",0xAD},{"times",0xD7},{"divide",0xF7},{"para",0xB6},{"sect",0xA7},{"pound",0xA3},{"yen",0xA5},{"cent",0xA2},{"plusmn",0xB1},{"frac12",0xBD},{"frac14",0xBC},{"sup2",0xB2},{"sup3",0xB3},{"micro",0xB5},{"larr",'<'},{"rarr",'>'},{0,0} };
        for (int q = 0; t[q].n; q++) { int ok = 1; for (int m = 0; ; m++) { if (t[q].n[m] != n[m]) { ok = 0; break; } if (!n[m]) break; } if (ok) { v = t[q].c; break; } }
    }
    if (v < 0) return -1; *i = j; return v;
}
static int attr(const char* tag, const char* name, char* out, int max) {
    int nl = slen(name);
    for (int i = 0; tag[i] && tag[i] != '>'; i++) {
        if ((i == 0 || tag[i - 1] == ' ' || tag[i - 1] == '\n' || tag[i - 1] == '\t' || tag[i - 1] == '"' || tag[i - 1] == '\'') && praef(tag + i, name) && (tag[i + nl] == '=' || tag[i + nl] == ' ' || tag[i + nl] == '>' || tag[i + nl] == '/' || !tag[i + nl])) {
            int p = i + nl; while (tag[p] == ' ') p++;
            if (tag[p] != '=') { out[0] = 0; return 1; }
            p++; while (tag[p] == ' ') p++;
            char q = tag[p]; int k = 0; if (q == '"' || q == '\'') { p++; while (tag[p] && tag[p] != q && k < max - 1) out[k++] = tag[p++]; } else while (tag[p] && tag[p] != ' ' && tag[p] != '>' && k < max - 1) out[k++] = tag[p++];
            out[k] = 0;
            /* Entitaeten im Attribut (&amp;) */
            for (int a = 0; out[a]; a++) if (out[a] == '&' && out[a + 1] == 'a' && out[a + 2] == 'm' && out[a + 3] == 'p' && out[a + 4] == ';') { int b = a + 1; while (out[b + 4]) { out[b] = out[b + 4]; b++; } out[b] = 0; }
            return 1;
        }
    }
    return 0;
}
static int farbe_lesen(const char* s, uint32_t* c) {                    /* #rgb, #rrggbb, Namen */
    while (*s == ' ') s++;
    if (*s == '#') {
        int n = 0; while (hexw(s[1 + n]) >= 0) n++;
        if (n == 3) { int r = hexw(s[1]), g = hexw(s[2]), b = hexw(s[3]); *c = (uint32_t)(r * 17) << 16 | (uint32_t)(g * 17) << 8 | (uint32_t)(b * 17); return 1; }
        if (n >= 6) { uint32_t v = 0; for (int i = 0; i < 6; i++) v = v << 4 | (uint32_t)hexw(s[1 + i]); *c = v; return 1; }
        return 0;
    }
    static const struct { const char* n; uint32_t c; } T[] = { {"black",0},{"white",0xFFFFFF},{"red",0xCC0000},{"green",0x008000},{"blue",0x0000CC},{"gray",0x808080},{"grey",0x808080},{"navy",0x000080},{"maroon",0x800000},{"purple",0x800080},{"orange",0xE67E00},{"darkred",0x8B0000},{"darkblue",0x00008B},{"darkgreen",0x006400},{"teal",0x008080},{"silver",0xC0C0C0},{0,0} };
    for (int i = 0; T[i].n; i++) if (praef(s, T[i].n)) { *c = T[i].c; return 1; }
    return 0;
}
static int stil_wert(const char* stil, const char* name, char* out, int max) {   /* "color: #333; ..." */
    int nl = slen(name);
    for (int i = 0; stil[i]; i++) {
        if ((i == 0 || stil[i - 1] == ';' || stil[i - 1] == ' ' || stil[i - 1] == '"') && praef(stil + i, name)) {
            int p = i + nl; while (stil[p] == ' ') p++; if (stil[p] != ':') continue; p++; while (stil[p] == ' ') p++;
            int k = 0; while (stil[p] && stil[p] != ';' && k < max - 1) out[k++] = stil[p++]; out[k] = 0; return 1;
        }
    }
    return 0;
}
static int zahl(const char* s) { int v = 0; while (*s == ' ') s++; while (*s >= '0' && *s <= '9') { v = v * 10 + (*s - '0'); s++; } if (*s == '%') return 0; return v; }

/* Ueberspringen ganzer Elemente (Skripte, versteckte Bereiche) */
static char skip_name[16]; static int skip_tiefe;
static uint32_t name_hash(const char* n) { uint32_t h = 5381; while (*n) h = h * 33 + (uint8_t)*n++; return h; }
static int ist_leer(const char* n) {
    static const char* const L[] = { "img", "br", "hr", "input", "meta", "link", "source", "wbr", "area", "col", "embed", "param", "track", "base", 0 };
    for (int i = 0; L[i]; i++) if (gleich(n, L[i])) return 1; return 0;
}
static void absatz(int px) { wort_ende(); t_neu(T_ABS, px); sp_offen = 0; }
static void zeile(void) { wort_ende(); t_neu(T_BR, 1); sp_offen = 0; }       /* a=1: weicher Umbruch (nur wenn die Zeile nicht leer ist) */

static void tag(const char* t, int zu) {
    char n[16]; int k = 0; while (t[k] && t[k] != ' ' && t[k] != '>' && t[k] != '/' && t[k] != '\n' && t[k] != '\t' && t[k] != '\r' && k < 15) { n[k] = (char)lc((unsigned char)t[k]); k++; } n[k] = 0;
    const char* rest = t + k;
    if (skip_tiefe) {
        if (gleich(n, skip_name) && !ist_leer(n)) { if (zu) skip_tiefe--; else skip_tiefe++; }
        return;
    }
    if (wl) wort_ende();                                                    /* Wort vor dem Tag mit dem bisherigen Stil abschliessen */
    #define IS(x) gleich(n, x)
    if (!zu) {
        char a[200];
        int versteckt = attr(rest, "hidden", a, 4) || (attr(rest, "aria-hidden", a, 8) && gleich(a, "true"));
        if (!versteckt && attr(rest, "style", a, 200)) { char v[40]; if (stil_wert(a, "display", v, 40) && praef(v, "none")) versteckt = 1; if (stil_wert(a, "visibility", v, 40) && praef(v, "hidden")) versteckt = 1; }
        if ((versteckt && !ist_leer(n) && !IS("body") && !IS("html")) || IS("noscript") || IS("svg") || IS("template") || IS("iframe") || IS("object") || IS("canvas") || IS("math") || IS("video") || IS("audio") || IS("dialog")) {
            wort_ende(); kopie(skip_name, n, 16); skip_tiefe = 1; return;
        }
    }
    /* Farbe aus style="color" oder <font color>: gilt bis zum gleichnamigen Ende-Tag */
    if (!zu && !ist_leer(n)) {
        char a[200], v[40]; uint32_t c;
        if ((attr(rest, "style", a, 200) && stil_wert(a, "color", v, 40) && farbe_lesen(v, &c)) || (IS("font") && attr(rest, "color", v, 40) && farbe_lesen(v, &c))) {
            if (nfarbe < 24) { int hell = ((c >> 16) & 255) + ((c >> 8) & 255) + (c & 255) > 600; farbe_st[nfarbe] = hell ? C_TXT : c; farbe_tag[nfarbe] = name_hash(n); nfarbe++; }
        }
        if (IS("body")) { if ((attr(rest, "style", a, 200) && (stil_wert(a, "background-color", v, 40) || stil_wert(a, "background", v, 40)) && farbe_lesen(v, &c)) || (attr(rest, "bgcolor", v, 40) && farbe_lesen(v, &c))) { int hell = ((c >> 16) & 255) + ((c >> 8) & 255) + (c & 255); if (hell > 450) wd_hg = c; } }
    } else if (zu && nfarbe && farbe_tag[nfarbe - 1] == name_hash(n)) nfarbe--;

    if (IS("title")) { in_title = !zu; if (!zu) wd_titel[0] = 0; return; }
    if (IS("a")) {
        if (zu) { wort_ende(); link_ = -1; return; }
        char h[300];
        if (attr(rest, "href", h, 300) && wd_nlinks < WD_LINKS) { char u[300]; wd_aufloesen(basis_url, h, u, 300); if (u[0]) { kopie(wd_links[wd_nlinks], u, 300); wort_ende(); link_ = wd_nlinks++; } }
        return;
    }
    if (IS("b") || IS("strong")) { wort_ende(); fett += zu ? -1 : 1; if (fett < 0) fett = 0; return; }
    if (IS("small") || IS("sub") || IS("sup") || IS("figcaption") || IS("cite")) { wort_ende(); klein += zu ? -1 : 1; if (klein < 0) klein = 0; if (IS("figcaption")) zeile(); return; }
    if (IS("u") || IS("ins")) { wort_ende(); unterstr = !zu; return; }
    if (IS("code") || IS("tt") || IS("kbd") || IS("samp")) { wort_ende(); mono += zu ? -1 : 1; if (mono < 0) mono = 0; return; }
    if (IS("br")) { wort_ende(); t_neu(T_BR, 0); sp_offen = 0; return; }
    if (IS("hr")) { wort_ende(); t_neu(T_HR, 0); return; }
    if (n[0] == 'h' && n[1] >= '1' && n[1] <= '6' && !n[2]) { absatz(n[1] <= '2' ? 18 : 12); kopf = zu ? 0 : n[1] - '0'; return; }
    if (IS("p")) { absatz(12); return; }
    if (IS("ul") || IS("ol") || IS("menu")) {
        if (!zu) { absatz(liste_tiefe ? 2 : 10); if (liste_tiefe < 7) { liste_tiefe++; lnum[liste_tiefe] = 0; ltyp[liste_tiefe] = IS("ol"); } t_neu(T_EINR, 28); }
        else { wort_ende(); if (liste_tiefe) { liste_tiefe--; t_neu(T_EINR, -28); } absatz(liste_tiefe ? 2 : 10); }
        return;
    }
    if (IS("li")) { if (!zu) { wort_ende(); int nr = 0; if (liste_tiefe && ltyp[liste_tiefe]) nr = ++lnum[liste_tiefe]; t_neu(T_LI, nr); sp_offen = 0; } else zeile(); return; }
    if (IS("dd")) { if (!zu) { zeile(); t_neu(T_EINR, 28); } else { zeile(); t_neu(T_EINR, -28); } return; }
    if (IS("blockquote")) { absatz(10); t_neu(T_EINR, zu ? -32 : 32); return; }
    if (IS("pre")) { absatz(8); if (!zu) { t_neu(T_PRE_AN, 0); vor = 1; mono++; } else { wort_ende(); t_neu(T_PRE_AUS, 0); vor = 0; if (mono) mono--; } return; }
    if (IS("table")) {
        if (!zu) { absatz(8); if (tab_tiefe < 7) { tab_tiefe++; tab_zeile[tab_tiefe] = 0; tab_zelle[tab_tiefe] = 0; t_neu(T_TAB_AN, 0); } }
        else if (tab_tiefe) { wort_ende(); if (tab_zelle[tab_tiefe]) t_neu(T_TD_AUS, 0); if (tab_zeile[tab_tiefe]) t_neu(T_TR_AUS, 0); t_neu(T_TAB_AUS, 0); tab_tiefe--; absatz(8); }
        return;
    }
    if (tab_tiefe && IS("tr")) {
        wort_ende();
        if (tab_zelle[tab_tiefe]) { t_neu(T_TD_AUS, 0); tab_zelle[tab_tiefe] = 0; }
        if (tab_zeile[tab_tiefe]) { t_neu(T_TR_AUS, 0); tab_zeile[tab_tiefe] = 0; }
        if (!zu) { t_neu(T_TR_AN, 0); tab_zeile[tab_tiefe] = 1; }
        return;
    }
    if (tab_tiefe && (IS("td") || IS("th"))) {
        wort_ende();
        if (tab_zelle[tab_tiefe]) { t_neu(T_TD_AUS, 0); tab_zelle[tab_tiefe] = 0; if (IS("th")) { if (fett) fett--; } }
        if (!zu) { if (!tab_zeile[tab_tiefe]) { t_neu(T_TR_AN, 0); tab_zeile[tab_tiefe] = 1; } t_neu(T_TD_AN, IS("th")); tab_zelle[tab_tiefe] = 1; sp_offen = 0; if (IS("th")) fett++; }
        return;
    }
    if (IS("img")) {
        char src[300], a[100];
        if (!(attr(rest, "src", src, 300) && src[0] && !praef(src, "data:"))) { if (!attr(rest, "data-src", src, 300)) src[0] = 0; }
        if (!src[0] || wd_nbild >= WD_BILDER) { if (attr(rest, "alt", a, 100) && a[0]) { int s = sp_offen; wort_ende(); sp_offen = s; for (int i = 0; a[i]; i++) zeichen((unsigned char)a[i]); } return; }
        struct wbild* b = &wd_bild[wd_nbild]; wd_aufloesen(basis_url, src, b->url, 300);
        b->aw = (int16_t)(attr(rest, "width", a, 20) ? zahl(a) : 0); b->ah = (int16_t)(attr(rest, "height", a, 20) ? zahl(a) : 0);
        if (!attr(rest, "alt", b->alt, 80)) b->alt[0] = 0;
        b->status = 0; b->px = 0; b->dw = b->dh = 0;
        if (!b->url[0] || (b->aw && b->aw < 6) || (b->ah && b->ah < 6)) return;
        { int l = slen(b->url); const char* e = l > 5 ? b->url + l - 5 : b->url; if (enthaelt(e, ".svg") || enthaelt(e, "webp") || enthaelt(e, "avif") || enthaelt(e, ".ico")) b->status = 3; }
        wort_ende(); t_neu(T_BILD, wd_nbild); tok[ntok - 1].link = (int16_t)link_; tok[ntok - 1].sp = (uint8_t)sp_offen; sp_offen = 0; wd_nbild++;
        return;
    }
    if (IS("form")) {
        if (!zu && wd_nform < WD_FORMS) { char a[300], m[10]; struct wform* f = &wd_form[wd_nform]; if (attr(rest, "action", a, 300)) wd_aufloesen(basis_url, a, f->action, 300); else kopie(f->action, basis_url, 300); if (!f->action[0]) kopie(f->action, basis_url, 300); f->post = attr(rest, "method", m, 10) && gleich(m, "post"); form_akt = wd_nform++; absatz(6); }
        else if (zu) { form_akt = -1; absatz(6); }
        return;
    }
    if ((IS("input") || IS("textarea") || IS("button") || IS("select")) && !zu && wd_nfeld < WD_FELDER) {
        char typ[16], a[200]; if (!attr(rest, "type", typ, 16)) kopie(typ, IS("button") ? "submit" : "text", 16);
        if (IS("input") && (gleich(typ, "checkbox") || gleich(typ, "radio") || gleich(typ, "file") || gleich(typ, "image") || gleich(typ, "reset"))) return;
        if (IS("button") && !gleich(typ, "submit")) return;
        struct wfeld* f = &wd_feld[wd_nfeld]; f->form = (int16_t)form_akt;
        f->typ = (gleich(typ, "hidden")) ? 1 : (gleich(typ, "submit") || gleich(typ, "button")) ? 2 : 0; if (IS("select")) f->typ = 1;
        if (!attr(rest, "name", f->name, 48)) f->name[0] = 0;
        if (!attr(rest, "value", f->wert, 200)) f->wert[0] = 0;
        if (!attr(rest, "placeholder", f->ph, 64)) { if (!attr(rest, "aria-label", f->ph, 64)) f->ph[0] = 0; }
        f->breite = (int16_t)(attr(rest, "size", a, 10) ? zahl(a) * 9 + 20 : 260); if (f->breite < 120) f->breite = 120; if (f->breite > 420) f->breite = 420;
        int idx = wd_nfeld++;
        if (IS("select")) { sel_feld = idx; return; }
        if (IS("textarea")) { ta_feld = idx; f->breite = 420; }
        if (IS("button")) { /* Beschriftung aus dem Inhalt */ ta_feld = idx; }
        if (f->typ == 2 && !f->wert[0] && !IS("button")) kopie(f->wert, "Senden", 200);
        if (f->typ != 1) { wort_ende(); t_neu(T_FELD, idx); tok[ntok - 1].sp = (uint8_t)sp_offen; sp_offen = 1; }
        return;
    }
    if (IS("option") && sel_feld >= 0 && !zu) { char v[200]; if (!wd_feld[sel_feld].wert[0] && attr(rest, "value", v, 200)) kopie(wd_feld[sel_feld].wert, v, 200); return; }
    if (IS("select") && zu) { sel_feld = -1; return; }
    if ((IS("textarea") || IS("button")) && zu) { ta_feld = -1; return; }
    if (IS("div") || IS("section") || IS("article") || IS("header") || IS("footer") || IS("nav") || IS("aside") || IS("main") || IS("figure") || IS("address") || IS("dl") || IS("dt") || IS("details") || IS("summary") || IS("fieldset") || IS("legend") || IS("caption") || IS("center")) { zeile(); return; }
    #undef IS
}

int wd_html(const char* h, uint32_t len, const char* basis) {
    ntok = 0; txtn = 0; wd_nlinks = 0; wd_nbild = 0; wd_napp = 0; wd_nfeld = 0; wd_nform = 0; wd_titel[0] = 0; wd_hg = 0x00FFFFFFu;
    fett = kopf = mono = klein = vor = sp_offen = liste_tiefe = in_title = unterstr = 0; link_ = -1; tab_tiefe = 0; nfarbe = 0; form_akt = sel_feld = ta_feld = -1; skip_tiefe = 0; wl = 0;
    basis_url = basis; pool_neu = 1;
    latin1 = 0; { uint32_t m = len < 3000 ? len : 3000; for (uint32_t i = 0; i + 12 < m; i++) if (praef(h + i, "charset=")) { const char* c = h + i + 8; while (*c == '"' || *c == '\'' || *c == ' ') c++; if (praef(c, "iso-8859") || praef(c, "windows-125") || praef(c, "latin")) latin1 = 1; break; } }
    for (uint32_t i = 0; i < len; i++) {
        char c = h[i];
        if (c == '<') {
            if (i + 3 < len && h[i + 1] == '!' && h[i + 2] == '-' && h[i + 3] == '-') { i += 3; while (i + 2 < len && !(h[i] == '-' && h[i + 1] == '-' && h[i + 2] == '>')) i++; i += 2; continue; }
            int zu = (i + 1 < len && h[i + 1] == '/'); uint32_t s = i + 1 + (uint32_t)zu;
            if (s >= len || !((h[s] >= 'a' && h[s] <= 'z') || (h[s] >= 'A' && h[s] <= 'Z') || h[s] == '!' || h[s] == '?')) { if (!skip_tiefe) zeichen('<'); continue; }
            char buf[600]; int k = 0; uint32_t j = s; char q = 0;
            while (j < len && (q || h[j] != '>')) { if (h[j] == '"' || h[j] == '\'') q = (q == h[j]) ? 0 : (q ? q : h[j]); if (k < 599) buf[k++] = h[j]; j++; }
            buf[k] = 0; i = j;
            if (buf[0] == '!' || buf[0] == '?') continue;
            /* Programme: <applet>, <zynjava>, <script type="text/java"> -> Quelltext merken */
            if (!zu && !skip_tiefe && (praef(buf, "applet") || praef(buf, "zynjava") || praef(buf, "script"))) {
                int isapp = praef(buf, "applet") || praef(buf, "zynjava");
                const char* rest_ = buf; while (*rest_ && *rest_ != ' ' && *rest_ != '\t' && *rest_ != '\n') rest_++;
                char ty[60]; ty[0] = 0; if (!isapp) { attr(rest_, "type", ty, 60); if (!(enthaelt(ty, "java") && !enthaelt(ty, "javascript") && !enthaelt(ty, "ecmascript"))) isapp = 0; else isapp = 1; }
                if (isapp && wd_napp < WD_APPS) {
                    struct wapp* ap = &wd_app[wd_napp]; char a[120]; ap->quelle[0] = 0; ap->url[0] = 0; ap->status = 0;
                    ap->w = (int16_t)(attr(rest_, "width", a, 20) ? zahl(a) : 480); ap->h = (int16_t)(attr(rest_, "height", a, 20) ? zahl(a) : 320);
                    if (ap->w < 80) ap->w = 480; if (ap->h < 60) ap->h = 320;
                    if (attr(rest_, "src", a, 120) && a[0]) wd_aufloesen(basis_url, a, ap->url, 300);
                    const char* e = praef(buf, "script") ? "</script" : praef(buf, "applet") ? "</applet" : "</zynjava";
                    int q = 0; uint32_t j2 = i + 1;
                    while (j2 < len && !(h[j2] == '<' && praef(h + j2, e))) {
                        if (q < (int)sizeof(ap->quelle) - 2) {
                            char c2 = h[j2];
                            if (c2 == '&') { if (praef(h + j2, "&lt;")) { c2 = '<'; j2 += 3; } else if (praef(h + j2, "&gt;")) { c2 = '>'; j2 += 3; } else if (praef(h + j2, "&amp;")) { c2 = '&'; j2 += 4; } else if (praef(h + j2, "&quot;")) { c2 = '"'; j2 += 5; } }
                            ap->quelle[q++] = c2;
                        }
                        j2++;
                    }
                    ap->quelle[q] = 0; while (j2 < len && h[j2] != '>') j2++; i = j2;
                    if (q > 3 || ap->url[0]) { wort_ende(); t_neu(T_ABS, 8); t_neu(T_APP, wd_napp); wd_napp++; t_neu(T_ABS, 8); }
                    continue;
                }
            }
            /* Rohtext-Elemente: bis zum Ende-Tag ueberspringen */
            if (!zu && (praef(buf, "script") || praef(buf, "style")) && (buf[6] == ' ' || buf[6] == '>' || !buf[6] || buf[5] == ' ' || !buf[5])) {
                const char* e = praef(buf, "style") ? "</style" : "</script";
                while (i + 1 < len) { i++; if (h[i] == '<' && praef(h + i, e)) { while (i < len && h[i] != '>') i++; break; } }
                continue;
            }
            tag(buf, zu);
        } else if (skip_tiefe) continue;
        else if (c == '&') { uint32_t j = i; int v = entitaet(h, len, &j); if (v >= 0) { zeichen(v); i = j; } else zeichen('&'); }
        else if ((unsigned char)c >= 0x80 && !latin1) {
            unsigned char a = (unsigned char)c;
            if (a >= 0xC0 && a < 0xE0 && i + 1 < len) { int v = ((a & 0x1F) << 6) | (h[i + 1] & 0x3F); i++; zeichen(v < 256 ? v : '?'); }
            else if (a >= 0xE0 && a < 0xF0 && i + 2 < len) {
                int v = ((a & 0x0F) << 12) | ((h[i + 1] & 0x3F) << 6) | (h[i + 2] & 0x3F); i += 2;
                if (v == 0x2013 || v == 0x2014 || v == 0x2212) v = '-'; else if (v == 0x2018 || v == 0x2019 || v == 0x201A) v = '\''; else if (v == 0x201C || v == 0x201D || v == 0x201E) v = '"'; else if (v == 0x2026) { zeichen('.'); zeichen('.'); v = '.'; }
                else if (v == 0x2022 || v == 0x00B7) v = 0xB7; else if (v == 0x20AC) { zeichen('E'); zeichen('U'); v = 'R'; } else if (v == 0x200B || v == 0x200C || v == 0x200D || v == 0xFEFF || v == 0x00AD) v = 0xAD; else if (v == 0x2009 || v == 0x202F || v == 0x2002 || v == 0x2003 || v == 0x00A0) v = ' '; else if (v == 0x2192) v = '>'; else if (v == 0x2190) v = '<'; else if (v > 255) v = -1;
                if (v >= 0) zeichen(v);
            } else if (a >= 0xF0) { i += 3; }
        }
        else zeichen((unsigned char)c);
    }
    wort_ende();
    while (tab_tiefe) { if (tab_zelle[tab_tiefe]) t_neu(T_TD_AUS, 0); if (tab_zeile[tab_tiefe]) t_neu(T_TR_AUS, 0); t_neu(T_TAB_AUS, 0); tab_tiefe--; }
    /* Titel: Leerzeichen am Ende weg */
    { int n = slen(wd_titel); while (n && wd_titel[n - 1] == ' ') wd_titel[--n] = 0; }
    return 0;
}

/* ---------- Layout ---------- */
static int L_x0, L_x1, L_cx, L_y, L_lh, L_start, L_ein, L_luecke, L_leer;
static int el_neu(int typ, int x, int y, int w, int h) {
    if (wd_nel >= WD_EL) return -1;
    struct wel* e = &wd_el[wd_nel]; e->typ = (uint8_t)typ; e->x = x; e->y = y; e->w = (int16_t)w; e->h = (int16_t)h; e->link = -1; e->ref = -1; e->font = 0; e->deko = 0; e->farbe = 0; e->off = 0;
    return wd_nel++;
}
static void zeile_fertig(void) {
    if (wd_nel > L_start) {
        int lh = L_lh; for (int i = L_start; i < wd_nel; i++) if (wd_el[i].typ != WE_RAHMEN && wd_el[i].typ != WE_FLAECHE) wd_el[i].y = L_y + (lh - wd_el[i].h);
        L_y += lh;
    }
    L_lh = 0; L_start = wd_nel; L_cx = L_x0 + L_ein; L_leer = 1;
}
static void luecke_anwenden(void) { if (L_luecke) { L_y += L_luecke; L_luecke = 0; } }
static void platz(int w, int h, int sp) {                      /* Platz in der Zeile: sp = Abstand davor */
    if (!L_leer && L_cx + sp + w > L_x1) zeile_fertig();
    if (L_leer) luecke_anwenden(); else L_cx += sp;
    if (h > L_lh) L_lh = h;
    L_leer = 0;
}
struct tabst { int x0, x1, ein, zeile_y, zeile_max, spalte, nsp, spw, rahmen[24], nr; };
static struct tabst TS[8]; static int nts;
static int spalten_zaehlen(int ab) {                            /* Zellen bis zum Zeilenende auf dieser Tabellenebene */
    int tiefe = 0, n = 0;
    for (int i = ab + 1; i < ntok; i++) {
        int t = tok[i].typ;
        if (t == T_TAB_AN) tiefe++; else if (t == T_TAB_AUS) { if (!tiefe) break; tiefe--; }
        else if (!tiefe && t == T_TR_AUS) break;
        else if (!tiefe && t == T_TD_AN) n++;
    }
    return n ? n : 1;
}
void wd_layout(int breite) {
    wd_nel = 0; L_x0 = 0; L_x1 = breite; L_y = 8; L_lh = 0; L_start = 0; L_ein = 0; L_cx = 0; L_luecke = 0; L_leer = 1; nts = 0;
    int pre_el = -1, pre_y = 0;
    for (int i = 0; i < ntok; i++) {
        struct wtok* t = &tok[i];
        switch (t->typ) {
        case T_WORT: {
            const char* s = wd_txt + t->off; int w = gfx_text_fw(s, t->font), h = fzeile(t->font), sp = t->sp ? gfx_text_fw(" ", t->font) : 0;
            platz(w, h, sp);
            int e = el_neu(WE_TEXT, L_cx, 0, w, h); if (e < 0) break;
            wd_el[e].font = t->font; wd_el[e].farbe = t->farbe; wd_el[e].link = t->link; wd_el[e].deko = t->deko; wd_el[e].off = t->off;
            L_cx += w; break; }
        case T_BR: if (t->a && L_leer) break; if (L_leer) { luecke_anwenden(); L_y += fzeile(F_NORM); } else zeile_fertig(); break;
        case T_ABS: if (!L_leer) zeile_fertig(); if (t->a > L_luecke && L_y > 8) L_luecke = t->a; break;
        case T_HR: if (!L_leer) zeile_fertig(); luecke_anwenden(); { int e = el_neu(WE_LINIE, L_x0 + L_ein, L_y + 8, L_x1 - L_x0 - L_ein, 1); (void)e; L_start = wd_nel; } L_y += 18; break;
        case T_EINR: if (!L_leer) zeile_fertig(); L_ein += t->a; if (L_ein < 0) L_ein = 0; if (L_ein > (L_x1 - L_x0) / 2) L_ein = (L_x1 - L_x0) / 2; L_cx = L_x0 + L_ein; break;
        case T_LI: {
            if (!L_leer) zeile_fertig(); luecke_anwenden();
            int e = el_neu(WE_PUNKT, L_x0 + L_ein - 18, 0, 14, fzeile(F_NORM)); if (e >= 0) { wd_el[e].ref = t->a; wd_el[e].farbe = C_TXT; }
            L_lh = fzeile(F_NORM); L_leer = 0; L_cx = L_x0 + L_ein; break; }
        case T_BILD: {
            struct wbild* b = &wd_bild[t->a]; int w = b->dw, h = b->dh;
            if (!w || !h) { if (b->status == 1) break; w = b->aw; h = b->ah; if (b->status >= 2 || !w || !h) { if (b->status >= 2 && b->alt[0]) { w = gfx_text_fw(b->alt, F_KLEIN) + 16; h = 26; } else break; } }
            int maxw = L_x1 - L_x0 - L_ein; if (w > maxw && maxw > 20) { h = h * maxw / w; w = maxw; }
            platz(w, h + 4, t->sp ? 4 : 0);
            int e = el_neu(WE_BILD, L_cx, 0, w, h + 4); if (e >= 0) { wd_el[e].ref = t->a; wd_el[e].link = t->link; }
            L_cx += w; break; }
        case T_APP: {
            struct wapp* ap = &wd_app[t->a]; int w = ap->w, h = ap->h; int maxw = L_x1 - L_x0 - L_ein; if (maxw > 40 && w > maxw) { h = h * maxw / w; w = maxw; }
            if (!L_leer) zeile_fertig(); luecke_anwenden(); platz(w, h + 4, 0);
            int e = el_neu(WE_APP, L_cx, 0, w, h + 4); if (e >= 0) wd_el[e].ref = t->a;
            L_cx += w; break; }
        case T_FELD: {
            struct wfeld* f = &wd_feld[t->a]; int w = f->typ == 2 ? gfx_text_fw(f->wert[0] ? f->wert : "Senden", F_FETT) + 32 : f->breite; if (w > L_x1 - L_x0) w = L_x1 - L_x0;
            platz(w, 40, t->sp ? 6 : 0);
            int e = el_neu(WE_FELD, L_cx, 0, w, 40); if (e >= 0) wd_el[e].ref = t->a;
            L_cx += w; break; }
        case T_PRE_AN: if (!L_leer) zeile_fertig(); luecke_anwenden(); pre_el = el_neu(WE_FLAECHE, L_x0 + L_ein, L_y, L_x1 - L_x0 - L_ein, 0); pre_y = L_y; L_y += 8; L_ein += 12; L_cx = L_x0 + L_ein; break;
        case T_PRE_AUS: if (!L_leer) zeile_fertig(); L_y += 8; L_ein -= 12; if (L_ein < 0) L_ein = 0; if (pre_el >= 0) { wd_el[pre_el].h = (int16_t)(L_y - pre_y > 32000 ? 32000 : L_y - pre_y); pre_el = -1; } L_cx = L_x0 + L_ein; break;
        case T_TAB_AN: if (!L_leer) zeile_fertig(); luecke_anwenden(); if (nts < 8) { struct tabst* s = &TS[nts++]; s->x0 = L_x0; s->x1 = L_x1; s->ein = L_ein; s->zeile_y = L_y; s->zeile_max = L_y; s->nr = 0; s->nsp = 1; s->spw = 0; s->spalte = 0; } break;
        case T_TR_AN: if (nts) { struct tabst* s = &TS[nts - 1]; if (!L_leer) zeile_fertig(); s->nsp = spalten_zaehlen(i); s->spw = (s->x1 - s->x0 - s->ein) / s->nsp; s->spalte = 0; s->zeile_y = L_y; s->zeile_max = L_y; s->nr = 0; } break;
        case T_TD_AN: if (nts) {
                struct tabst* s = &TS[nts - 1];
                int cx0 = s->x0 + s->ein + s->spalte * s->spw;
                if (s->nr < 24) { int e = el_neu(WE_RAHMEN, cx0, s->zeile_y, s->spw, 0); if (e >= 0) { s->rahmen[s->nr++] = e; wd_el[e].ref = t->a; } }
                L_x0 = cx0 + 6; L_x1 = cx0 + s->spw - 6; L_ein = 0; L_y = s->zeile_y + 5; L_lh = 0; L_start = wd_nel; L_cx = L_x0; L_leer = 1; L_luecke = 0; s->spalte++;
            } break;
        case T_TD_AUS: if (nts) { struct tabst* s = &TS[nts - 1]; if (!L_leer) zeile_fertig(); if (L_y + 5 > s->zeile_max) s->zeile_max = L_y + 5; } break;
        case T_TR_AUS: if (nts) { struct tabst* s = &TS[nts - 1]; if (!L_leer) zeile_fertig(); if (L_y > s->zeile_max) s->zeile_max = L_y; for (int k = 0; k < s->nr; k++) wd_el[s->rahmen[k]].h = (int16_t)(s->zeile_max - s->zeile_y); s->nr = 0;
                L_y = s->zeile_max; L_x0 = s->x0; L_x1 = s->x1; L_ein = s->ein; L_start = wd_nel; L_cx = L_x0 + L_ein; L_leer = 1; L_lh = 0; } break;
        case T_TAB_AUS: if (nts) { struct tabst* s = &TS[--nts]; if (!L_leer) zeile_fertig(); L_x0 = s->x0; L_x1 = s->x1; L_ein = s->ein; L_start = wd_nel; L_cx = L_x0 + L_ein; L_leer = 1; L_lh = 0; if (L_y < s->zeile_max) L_y = s->zeile_max; } break;
        }
    }
    if (!L_leer) zeile_fertig();
    wd_hoehe = L_y + 24;
}

/* ---------- Laden ---------- */
static int status_text(uint32_t st) { char a[12]; int n = 0; uint32_t v = st; char t[12]; int tl = 0; do { t[tl++] = (char)('0' + v % 10); v /= 10; } while (v); while (tl) a[n++] = t[--tl]; a[n] = 0; int l = slen(wd_fehler); kopie(wd_fehler + l, a, 120 - l); return 0; }
static int holen(const char* url, char* endurl, const char** body, uint32_t* bl, char* typ, int typmax) {
    char u[300]; kopie(u, url, 300);
    for (int versuch = 0; versuch < 6; versuch++) {
        const char* kopf_; uint32_t kl, st;
        int r = net_seite(u, &kopf_, &kl, body, bl, &st);
        if (r != 0) { kopie(wd_fehler, r == -2 ? "Adresse nicht aufloesbar (Netzwerk/DNS?)" : r == -1 ? "Kein Netzwerk" : "Verbindung fehlgeschlagen", 120); return -1; }
        if (typ) { typ[0] = 0; for (uint32_t i = 0; i + 13 < kl; i++) if ((i == 0 || kopf_[i - 1] == '\n') && praef(kopf_ + i, "content-type:")) { uint32_t p = i + 13; while (p < kl && kopf_[p] == ' ') p++; int k = 0; while (p < kl && kopf_[p] != '\r' && kopf_[p] != '\n' && k < typmax - 1) typ[k++] = kopf_[p++]; typ[k] = 0; break; } }
        if (st >= 300 && st < 400) {
            char loc[300]; loc[0] = 0;
            for (uint32_t i = 0; i + 9 < kl; i++) if ((i == 0 || kopf_[i - 1] == '\n') && praef(kopf_ + i, "location:")) { uint32_t p = i + 9; while (p < kl && kopf_[p] == ' ') p++; int k = 0; while (p < kl && kopf_[p] != '\r' && kopf_[p] != '\n' && k < 299) loc[k++] = kopf_[p++]; loc[k] = 0; break; }
            if (!loc[0]) { kopie(wd_fehler, "Weiterleitung ohne Ziel", 120); return -2; }
            char nu[300]; wd_aufloesen(u, loc, nu, 300); if (!nu[0]) { kopie(wd_fehler, "Weiterleitung nicht lesbar", 120); return -2; }
            kopie(u, nu, 300); continue;
        }
        if (st != 200) { kopie(wd_fehler, "Der Server meldet einen Fehler: Status ", 120); status_text(st); return -3; }
        if (endurl) kopie(endurl, u, 300);
        return 0;
    }
    kopie(wd_fehler, "Zu viele Weiterleitungen", 120); return -4;
}
int wd_laden(const char* url) {
    const char* body; uint32_t bl; char u[300], typ[80];
    wd_fehler[0] = 0;
    int r = holen(url, u, &body, &bl, typ, 80);
    if (r) return r;
    kopie(wd_url, u, 300);
    if (typ[0] && !enthaelt(typ, "html") && !enthaelt(typ, "text") && !enthaelt(typ, "xml")) {
        if (enthaelt(typ, "image/")) {                         /* ein Bild direkt aufrufen */
            static char h[700]; kopie(h, "<html><title>Bild</title><img src=\"", 700); int n = slen(h); kopie(h + n, u, 700 - n); n = slen(h); kopie(h + n, "\"></html>", 700 - n);
            return wd_html(h, (uint32_t)slen(h), u);
        }
        kopie(wd_fehler, "Dieser Inhalt kann nicht angezeigt werden: ", 120); int l = slen(wd_fehler); kopie(wd_fehler + l, typ, 120 - l); return -5;
    }
    if (typ[0] && enthaelt(typ, "text/plain")) {             /* reiner Text */
        wd_html("", 0, u); t_neu(T_PRE_AN, 0); vor = 1; mono = 1;
        for (uint32_t i = 0; i < bl; i++) zeichen((unsigned char)body[i]);
        wort_ende(); t_neu(T_PRE_AUS, 0); vor = 0; mono = 0; return 0;
    }
    return wd_html(body, bl, u);
}

int wd_app_quelle(int i) {
    if (i < 0 || i >= wd_napp) return -1; struct wapp* ap = &wd_app[i];
    if (ap->quelle[0] || !ap->url[0]) return ap->quelle[0] ? 0 : -1;
    const char* body; uint32_t bl; if (holen(ap->url, 0, &body, &bl, 0, 0) != 0) return -1;
    if (bl >= sizeof(ap->quelle)) bl = sizeof(ap->quelle) - 1; for (uint32_t k = 0; k < bl; k++) ap->quelle[k] = body[k]; ap->quelle[bl] = 0; return 0;
}

/* ---------- Bilder ---------- */
static uint32_t *dek_out, *pool; static uint8_t* dek_arb; static uint32_t dek_max, arb_max, pool_max, pool_pos; static int speicher_versucht;
static int speicher(void) {
    if (dek_out) return 0;
    if (speicher_versucht) return -1;
    speicher_versucht = 1;
    uint32_t frei = (uint32_t)pmm_get_free_pages();
    uint32_t mb = frei > 60000 ? 16 : frei > 30000 ? 6 : 0;                  /* > 240 MB frei: grosse Puffer */
    if (!mb) return -1;
    dek_max = mb * 262144u; arb_max = mb * 1536u * 1024u; pool_max = mb * 393216u;
    dek_out = (uint32_t*)pmm_alloc_pages(dek_max / 1024); dek_arb = (uint8_t*)pmm_alloc_pages(arb_max / 4096); pool = (uint32_t*)pmm_alloc_pages(pool_max / 1024);
    if (!dek_out || !dek_arb || !pool) { dek_out = 0; return -1; }
    return 0;
}
int wd_bild_naechstes(int y0, int y1) {
    for (int i = 0; i < wd_nel; i++) { struct wel* e = &wd_el[i]; if (e->typ != WE_BILD || e->y > y1 || e->y + e->h < y0) continue; if (wd_bild[e->ref].status == 0) return e->ref; }
    /* Bilder ohne bekannte Groesse sind nicht im Layout: das erste davon */
    for (int i = 0; i < wd_nbild; i++) if (wd_bild[i].status == 0 && (!wd_bild[i].aw || !wd_bild[i].ah)) { int drin = 0; for (int k = 0; k < wd_nel; k++) if (wd_el[k].typ == WE_BILD && wd_el[k].ref == i) { drin = 1; break; } if (!drin) return i; }
    return -1;
}
int wd_bild_laden(int i) {
    if (i < 0 || i >= wd_nbild) return -1;
    struct wbild* b = &wd_bild[i];
    if (speicher()) { b->status = 2; return -1; }
    if (pool_neu) { pool_neu = 0; pool_pos = 0; }
    { int n = 0; for (int k = 0; k < wd_nbild; k++) if (wd_bild[k].status == 1 || wd_bild[k].status == 2) n++; if (n >= 40) { b->status = 3; return -4; } }
    const char* body; uint32_t bl; char typ[60]; char gespeichert[120]; kopie(gespeichert, wd_fehler, 120);
    int r = holen(b->url, 0, &body, &bl, typ, 60);
    if (r) r = holen(b->url, 0, &body, &bl, typ, 60);                 /* ein zweiter Versuch: das Netz verliert gelegentlich Pakete */
    kopie(wd_fehler, gespeichert, 120);
    if (r) { b->status = 2; return -2; }
    int w, h;
    r = bild_dekodieren((const uint8_t*)body, bl, dek_out, dek_max, dek_arb, arb_max, &w, &h);
    if (r) { b->status = 2; kprint("[WEB] Bild nicht lesbar ("); kprint_dec((uint32_t)-r); kprint("): "); kprint(b->url); kprint("\n"); return r; }
    int dw = b->aw ? b->aw : w, dh = b->ah ? b->ah : (b->aw ? h * b->aw / w : h);
    if (dw > 1100) { dh = dh * 1100 / dw; dw = 1100; }
    if (dw < 1) dw = 1; if (dh < 1) dh = 1;
    if (pool_pos + (uint32_t)(dw * dh) > pool_max) { b->status = 2; return -3; }
    uint32_t* z = pool + pool_pos; pool_pos += (uint32_t)(dw * dh);
    for (int y = 0; y < dh; y++) {                                   /* verkleinern: Mittel ueber den Quellbereich (hoechstens 4x4 Proben) */
        int sy0 = y * h / dh, sy1 = (y + 1) * h / dh; if (sy1 <= sy0) sy1 = sy0 + 1;
        for (int x = 0; x < dw; x++) {
            int sx0 = x * w / dw, sx1 = (x + 1) * w / dw; if (sx1 <= sx0) sx1 = sx0 + 1;
            int stx = (sx1 - sx0 + 3) / 4, sty = (sy1 - sy0 + 3) / 4; uint32_t r_ = 0, g = 0, bb = 0, n = 0;
            for (int yy = sy0; yy < sy1; yy += sty) for (int xx = sx0; xx < sx1; xx += stx) { uint32_t p = dek_out[(uint32_t)yy * (uint32_t)w + (uint32_t)xx]; r_ += p >> 16 & 255; g += p >> 8 & 255; bb += p & 255; n++; }
            z[y * dw + x] = (r_ / n) << 16 | (g / n) << 8 | (bb / n);
        }
    }
    b->px = z; b->dw = (int16_t)dw; b->dh = (int16_t)dh; b->status = 1;
    return 0;
}

/* ---------- Formulare, Text ---------- */
int wd_form_url(int feld, char* out, int max) {
    if (feld < 0 || feld >= wd_nfeld) return -2;
    int f = wd_feld[feld].form; if (f < 0) return -2;
    if (wd_form[f].post) return -1;
    kopie(out, wd_form[f].action, max); int n = slen(out);
    for (int i = 0; i < n; i++) if (out[i] == '?') { n = i; break; }
    int erst = 1; static const char hx[] = "0123456789ABCDEF";
    for (int i = 0; i < wd_nfeld && n < max - 8; i++) {
        struct wfeld* x = &wd_feld[i]; if (x->form != f || !x->name[0]) continue;
        if (x->typ == 2 && i != feld) continue;
        out[n++] = erst ? '?' : '&'; erst = 0;
        for (const char* s = x->name; *s && n < max - 4; s++) out[n++] = *s;
        out[n++] = '=';
        for (const unsigned char* s = (const unsigned char*)x->wert; *s && n < max - 7; s++) {
            unsigned c = *s;
            if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '-' || c == '_' || c == '.') out[n++] = (char)c;
            else if (c == ' ') out[n++] = '+';
            else if (c >= 0x80) { out[n++] = '%'; out[n++] = hx[(0xC0 | c >> 6) >> 4]; out[n++] = hx[(0xC0 | c >> 6) & 15]; out[n++] = '%'; out[n++] = hx[(0x80 | (c & 63)) >> 4]; out[n++] = hx[(0x80 | (c & 63)) & 15]; }
            else { out[n++] = '%'; out[n++] = hx[c >> 4]; out[n++] = hx[c & 15]; }
        }
    }
    out[n] = 0; return 0;
}
int wd_text(char* out, int max) {
    int k = 0, ly = -1;
    for (int i = 0; i < wd_nel && k < max - 2; i++) {
        struct wel* e = &wd_el[i]; if (e->typ != WE_TEXT) continue;
        if (ly >= 0 && e->y != ly) out[k++] = '\n'; else if (ly >= 0 && k) out[k++] = ' ';
        for (const char* s = wd_txt + e->off; *s && k < max - 2; s++) out[k++] = *s;
        ly = e->y;
    }
    out[k] = 0; return k;
}
