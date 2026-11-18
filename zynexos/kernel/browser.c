/*
 * Zynex OS - Browser (Aufbereitung): HTML -> Textzeilen mit Links. Keine Bilder, keine Stilvorlagen, keine Skripte; dafuer schnell, sicher und lesbar.
 */
#include "browser.h"
#include "net.h"
extern void kprint(const char* str);

struct web_zeile web_z[WEB_ZEILEN];
int web_nz; char web_titel[80], web_url[200], web_fehler[100];
char web_links[WEB_LINKS][160]; int web_nlinks;

static int lc(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }
static int slen(const char* s) { int n = 0; while (s[n]) n++; return n; }
static void kopie(char* d, const char* s, int max) { int i = 0; while (s[i] && i < max - 1) { d[i] = s[i]; i++; } d[i] = 0; }
static int praef(const char* s, const char* p) { for (; *p; s++, p++) if (lc((unsigned char)*s) != *p) return 0; return 1; }

/* ---------- Adressen ---------- */
static void basis_teile(const char* b, char* host, int hm, char* pfad, int pm, int* https) {   /* host enthaelt scheme://host[:port] */
    int i = 0; *https = praef(b, "https"); while (b[i] && !(b[i] == ':' && b[i + 1] == '/' && b[i + 2] == '/')) i++;
    if (!b[i]) { kopie(host, b, hm); kopie(pfad, "/", pm); return; }
    i += 3; while (b[i] && b[i] != '/' && b[i] != '?' && b[i] != '#') i++;
    int hl = i < hm - 1 ? i : hm - 1; for (int k = 0; k < hl; k++) host[k] = b[k]; host[hl] = 0;
    if (b[i] == '/') kopie(pfad, b + i, pm); else if (b[i] == '?') { pfad[0] = '/'; kopie(pfad + 1, b + i, pm - 1); } else kopie(pfad, "/", pm);
}
static int hexw(int c) { return c >= '0' && c <= '9' ? c - '0' : (c | 32) >= 'a' && (c | 32) <= 'f' ? (c | 32) - 'a' + 10 : -1; }
static void aufloesen(const char* basis, const char* href, char* out, int max) {
    out[0] = 0;
    while (*href == ' ') href++;
    if (!*href || href[0] == '#' || praef(href, "mailto:") || praef(href, "javascript:") || praef(href, "tel:")) return;
    char h[160]; kopie(h, href, 160);
    for (int i = 0; h[i]; i++) if (h[i] == '#') { h[i] = 0; break; }
    for (int i = 0; h[i]; i++) if (h[i] == '&' && h[i + 1] == 'a' && h[i + 2] == 'm' && h[i + 3] == 'p' && h[i + 4] == ';') { int j = i + 1; while (h[j + 4]) { h[j] = h[j + 4]; j++; } h[j] = 0; }
    /* Weiterleitungen von Suchmaschinen (uddg=...) auf das echte Ziel */
    for (int i = 0; h[i]; i++) if (h[i] == 'u' && praef(h + i, "uddg=")) {
        int k = 0; i += 5; while (h[i] && h[i] != '&' && k < max - 1) { if (h[i] == '%' && hexw(h[i + 1]) >= 0 && hexw(h[i + 2]) >= 0) { out[k++] = (char)(hexw(h[i + 1]) * 16 + hexw(h[i + 2])); i += 3; } else out[k++] = h[i++]; }
        out[k] = 0; return;
    }
    if (praef(h, "http://") || praef(h, "https://")) { kopie(out, h, max); return; }
    char host[100], pfad[160]; int https; basis_teile(basis, host, 100, pfad, 160, &https);
    if (h[0] == '/' && h[1] == '/') { kopie(out, https ? "https:" : "http:", max); int n = slen(out); kopie(out + n, h, max - n); return; }
    kopie(out, host, max); int n = slen(out);
    if (h[0] == '/') { kopie(out + n, h, max - n); return; }
    int q = slen(pfad); for (int i = 0; pfad[i]; i++) if (pfad[i] == '?') { q = i; break; }
    while (q > 0 && pfad[q - 1] != '/') q--;
    for (int i = 0; i < q && n < max - 1; i++) out[n++] = pfad[i];
    kopie(out + n, h, max - n);
}
void web_url_bauen(const char* e, char* out, int max) {
    while (*e == ' ') e++;
    int punkt = 0, blank = 0, n = slen(e); for (int i = 0; i < n; i++) { if (e[i] == '.') punkt = 1; if (e[i] == ' ') blank = 1; }
    if (praef(e, "http://") || praef(e, "https://")) { kopie(out, e, max); return; }
    if (punkt && !blank && n > 3) { kopie(out, "https://", max); kopie(out + 8, e, max - 8); return; }
    kopie(out, "https://lite.duckduckgo.com/lite/?q=", max); int k = slen(out);                 /* alles andere ist eine Suche */
    static const char hx[] = "0123456789ABCDEF";
    for (int i = 0; i < n && k < max - 4; i++) { unsigned char c = (unsigned char)e[i]; if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) out[k++] = (char)c; else if (c == ' ') out[k++] = '+'; else { out[k++] = '%'; out[k++] = hx[c >> 4]; out[k++] = hx[c & 15]; } }
    out[k] = 0;
}

/* ---------- Aufbereitung ---------- */
static int cols, zi, col, lastblank, pend_sp, cur_link, stil, in_pre, in_title, skip, einr;
static char wd[100]; static int wl, wlink;
static const char* basis_url;
static void zeile_leeren(struct web_zeile* z) { z->t[0] = 0; z->stil = 0; z->nl = 0; }
static void zeile_ende(void) {
    if (zi >= WEB_ZEILEN - 1) { col = 0; return; }
    struct web_zeile* z = &web_z[zi]; z->t[col] = 0; lastblank = (col == 0); zi++; zeile_leeren(&web_z[zi]); col = 0;
}
static void umbruch(int blank) {
    if (col > 0) zeile_ende();
    if (blank && !lastblank && zi > 0) { web_z[zi].t[0] = 0; zeile_ende(); }
}
static void put(char c, int link) {
    if (zi >= WEB_ZEILEN - 1 || col >= cols) return;
    struct web_zeile* z = &web_z[zi]; if (col == 0) z->stil = (uint8_t)stil;
    z->t[col] = c;
    if (link >= 0) {
        if (z->nl && z->l[z->nl - 1].id == link && z->l[z->nl - 1].b == col) z->l[z->nl - 1].b = (uint8_t)(col + 1);
        else if (z->nl < 6) { z->l[z->nl].a = (uint8_t)col; z->l[z->nl].b = (uint8_t)(col + 1); z->l[z->nl].id = (uint16_t)link; z->nl++; }
    }
    col++;
}
static void wort_ende(void) {
    if (!wl) return;
    int off = einr; if (col == 0 && off) { for (int i = 0; i < off; i++) put(' ', -1); }
    int need = wl + (col > off ? 1 : 0);
    if (col + need > cols && col > off) { umbruch(0); for (int i = 0; i < einr; i++) put(' ', -1); }
    if (col > einr && col > 0) put(' ', (wlink >= 0 && wlink == cur_link && pend_sp) ? wlink : -1);
    for (int i = 0; i < wl; i++) { if (col >= cols) { umbruch(0); for (int k = 0; k < einr; k++) put(' ', -1); } put(wd[i], wlink); }
    wl = 0; pend_sp = 0; lastblank = 0;
}
static void zeichen(int c) {                                             /* Latin-1 */
    if (in_title) { int n = slen(web_titel); if (n < 78 && !(c <= ' ' && (n == 0 || web_titel[n - 1] == ' '))) { web_titel[n] = (char)(c <= ' ' ? ' ' : c); web_titel[n + 1] = 0; } return; }
    if (skip) return;
    if (in_pre) { if (c == '\n') { wort_ende(); umbruch(0); } else if (c == '\t') { for (int i = 0; i < 4; i++) put(' ', -1); } else if (c >= 32 && c != '\r') put((char)c, cur_link); return; }
    if (c <= ' ') { wort_ende(); pend_sp = 1; return; }
    if (wl == 0) wlink = cur_link;
    if (wl < 98) wd[wl++] = (char)c;
}
static int entitaet(const char* h, uint32_t len, uint32_t* i) {          /* nach '&': liefert Latin-1-Zeichen oder -1 */
    uint32_t j = *i + 1; char n[10]; int k = 0;
    while (j < len && k < 9 && h[j] != ';' && h[j] != ' ' && h[j] != '&' && h[j] != '<') n[k++] = h[j++];
    n[k] = 0; if (j >= len || h[j] != ';') return -1;
    int v = -1;
    if (n[0] == '#') { int x = 0, hex = (n[1] | 32) == 'x'; for (int q = hex ? 2 : 1; n[q]; q++) { int d = hexw(n[q]); if (d < 0 || (!hex && d > 9)) { x = -1; break; } x = x * (hex ? 16 : 10) + d; } v = x; if (v == 0x2013 || v == 0x2014 || v == 0x2212) v = '-'; else if (v == 0x2018 || v == 0x2019) v = '\''; else if (v == 0x201C || v == 0x201D) v = '"'; else if (v == 0x2026) v = '.'; else if (v > 255) v = '?'; }
    else {
        static const struct { const char* n; int c; } t[] = { {"amp",'&'},{"lt",'<'},{"gt",'>'},{"quot",'"'},{"apos",'\''},{"nbsp",' '},{"auml",0xE4},{"ouml",0xF6},{"uuml",0xFC},{"Auml",0xC4},{"Ouml",0xD6},{"Uuml",0xDC},{"szlig",0xDF},{"copy",0xA9},{"reg",0xAE},{"euro",'E'},{"ndash",'-'},{"mdash",'-'},{"hellip",'.'},{"laquo",0xAB},{"raquo",0xBB},{"eacute",0xE9},{"egrave",0xE8},{"agrave",0xE0},{"middot",0xB7},{"bull",'*'},{"lsquo",'\''},{"rsquo",'\''},{"ldquo",'"'},{"rdquo",'"'},{"deg",0xB0},{0,0} };
        for (int q = 0; t[q].n; q++) { int ok = 1; for (int m = 0; ; m++) { if (t[q].n[m] != n[m]) { ok = 0; break; } if (!n[m]) break; } if (ok) { v = t[q].c; break; } }
    }
    if (v < 0) return -1; *i = j; return v;
}
static int attr(const char* tag, const char* name, char* out, int max) {      /* tag zeigt hinter dem Namen, bis '>' */
    int nl = slen(name);
    for (int i = 0; tag[i] && tag[i] != '>'; i++) {
        if ((i == 0 || tag[i - 1] == ' ' || tag[i - 1] == '\n' || tag[i - 1] == '\t') && praef(tag + i, name) && (tag[i + nl] == '=' || tag[i + nl] == ' ')) {
            int p = i + nl; while (tag[p] == ' ') p++; if (tag[p] != '=') continue; p++; while (tag[p] == ' ') p++;
            char q = tag[p]; int k = 0; if (q == '"' || q == '\'') { p++; while (tag[p] && tag[p] != q && k < max - 1) out[k++] = tag[p++]; } else while (tag[p] && tag[p] != ' ' && tag[p] != '>' && k < max - 1) out[k++] = tag[p++];
            out[k] = 0; return 1;
        }
    }
    return 0;
}
static int strcmp_(const char* a, const char* b);
static void tag_bearbeiten(const char* t, int zu) {                       /* t = Name ab erstem Zeichen (nach '<' oder '</') */
    char n[12]; int k = 0; while (t[k] && t[k] != ' ' && t[k] != '>' && t[k] != '/' && t[k] != '\n' && k < 11) { n[k] = (char)lc((unsigned char)t[k]); k++; } n[k] = 0;
    const char* rest = t + k;
    #define IS(x) (!strcmp_(n, x))
    if (!zu && (!strcmp_(n, "script") || !strcmp_(n, "style") || !strcmp_(n, "noscript") || !strcmp_(n, "svg") || !strcmp_(n, "template"))) { skip = 1; return; }
    if (zu && (!strcmp_(n, "script") || !strcmp_(n, "style") || !strcmp_(n, "noscript") || !strcmp_(n, "svg") || !strcmp_(n, "template"))) { skip = 0; return; }
    if (skip) return;
    if (IS("title")) { in_title = !zu; if (!zu) web_titel[0] = 0; return; }
    if (IS("a")) {
        if (zu) { wort_ende(); cur_link = -1; return; }
        char h[160];
        if (attr(rest, "href", h, 160) && web_nlinks < WEB_LINKS) { char u[160]; aufloesen(basis_url, h, u, 160); if (u[0]) { kopie(web_links[web_nlinks], u, 160); wort_ende(); cur_link = web_nlinks++; } }
        return;
    }
    if (IS("br")) { wort_ende(); umbruch(0); return; }
    if (IS("hr")) { wort_ende(); umbruch(1); for (int i = 0; i < cols; i++) put('-', -1); umbruch(1); return; }
    if (IS("img")) { char a[60]; if (attr(rest, "alt", a, 60) && a[0]) { zeichen('['); for (int i = 0; a[i]; i++) zeichen((unsigned char)a[i]); zeichen(']'); } return; }
    if (n[0] == 'h' && n[1] >= '1' && n[1] <= '6' && !n[2]) { wort_ende(); umbruch(1); stil = zu ? 0 : (n[1] <= '1' ? 1 : 2); return; }
    if (IS("li")) { wort_ende(); umbruch(0); if (!zu) { einr = 2; pend_sp = 0; wlink = -1; zeichen('-'); wort_ende(); } return; }
    if (IS("ul") || IS("ol")) { wort_ende(); umbruch(1); einr = zu ? 0 : 2; return; }
    if (IS("pre") || IS("code") || IS("blockquote")) { wort_ende(); if (!IS("code")) umbruch(1); in_pre = (IS("pre") && !zu); stil = zu ? 0 : 3; return; }
    if (IS("td") || IS("th")) { wort_ende(); if (!zu) { zeichen(' '); zeichen('|'); zeichen(' '); } return; }
    if (IS("p") || IS("table") || IS("form") || IS("section") || IS("article") || IS("main") || IS("dl") || IS("figure")) { wort_ende(); umbruch(1); return; }
    if (IS("div") || IS("tr") || IS("header") || IS("footer") || IS("nav") || IS("aside") || IS("dt") || IS("dd") || IS("option")) { wort_ende(); umbruch(0); return; }
    #undef IS
}
static int strcmp_(const char* a, const char* b) { while (*a && *a == *b) { a++; b++; } return *a - *b; }

int web_html(const char* h, uint32_t len, const char* basis, int spalten) {
    cols = spalten > 110 ? 110 : spalten < 20 ? 20 : spalten; zi = 0; col = 0; lastblank = 1; pend_sp = 0; cur_link = -1; stil = 0; in_pre = 0; in_title = 0; skip = 0; einr = 0; wl = 0; wlink = -1;
    web_nlinks = 0; web_titel[0] = 0; basis_url = basis; zeile_leeren(&web_z[0]);
    for (uint32_t i = 0; i < len && zi < WEB_ZEILEN - 2; i++) {
        char c = h[i];
        if (c == '<') {
            if (i + 3 < len && h[i + 1] == '!' && h[i + 2] == '-' && h[i + 3] == '-') { i += 3; while (i + 2 < len && !(h[i] == '-' && h[i + 1] == '-' && h[i + 2] == '>')) i++; i += 2; continue; }
            int zu = (i + 1 < len && h[i + 1] == '/'); uint32_t s = i + 1 + zu; char buf[300]; int k = 0; uint32_t j = s; char q = 0;
            while (j < len && k < 299 && (q || h[j] != '>')) { if (h[j] == '"' || h[j] == '\'') q = (q == h[j]) ? 0 : (q ? q : h[j]); buf[k++] = h[j++]; }
            buf[k] = 0; i = j;
            if (buf[0] == '!' || buf[0] == '?') continue;
            tag_bearbeiten(buf, zu);
            if (skip) {                                                  /* Skript/Stil bis zum Ende ueberspringen */
                const char* e = (buf[0] == 's' && buf[1] == 't') ? "</style" : (buf[0] == 'n') ? "</noscript" : (buf[0] == 's' && buf[1] == 'v') ? "</svg" : (buf[0] == 't') ? "</template" : "</script";
                while (i + 1 < len) { i++; if (h[i] == '<' && praef(h + i, e) ) { while (i < len && h[i] != '>') i++; skip = 0; break; } }
            }
        } else if (c == '&') { uint32_t j = i; int v = entitaet(h, len, &j); if (v >= 0) { zeichen(v); i = j; } else zeichen('&'); }
        else if ((unsigned char)c >= 0xC0 && i + 1 < len) {              /* UTF-8 -> Latin-1 */
            unsigned char b = (unsigned char)h[i + 1]; int v = '?';
            if ((unsigned char)c == 0xC3) v = b + 0x40; else if ((unsigned char)c == 0xC2) v = b; else if ((unsigned char)c == 0xE2) { v = (b == 0x80 && i + 2 < len) ? '-' : '?'; i++; }
            else if ((unsigned char)c >= 0xE0) i++;
            i++; if ((unsigned char)c >= 0xF0 && i + 1 < len) i += 2; zeichen(v);
        } else if ((unsigned char)c >= 0x80) { /* Folgebyte ignorieren */ }
        else zeichen((unsigned char)c);
    }
    wort_ende(); if (col > 0) zeile_ende();
    web_nz = zi; if (!web_nz) { web_nz = 1; kopie(web_z[0].t, "(leere Seite)", 116); }
    return 0;
}

int web_laden(const char* url, int spalten) {
    char u[200]; kopie(u, url, 200); web_fehler[0] = 0;
    for (int versuch = 0; versuch < 5; versuch++) {
        const char *kopf, *body; uint32_t kl, bl, st;
        int r = net_seite(u, &kopf, &kl, &body, &bl, &st);
        if (r != 0) { kopie(web_fehler, r == -2 ? "Adresse nicht aufloesbar (Netzwerk/DNS?)" : "Verbindung fehlgeschlagen (siehe Netzwerk)", 100); return -1; }
        if (st >= 300 && st < 400) {
            char loc[160]; loc[0] = 0;
            for (uint32_t i = 0; i + 9 < kl; i++) if ((i == 0 || kopf[i - 1] == '\n') && praef(kopf + i, "location:")) { uint32_t p = i + 9; while (p < kl && kopf[p] == ' ') p++; int k = 0; while (p < kl && kopf[p] != '\r' && kopf[p] != '\n' && k < 159) loc[k++] = kopf[p++]; loc[k] = 0; break; }
            if (!loc[0]) { kopie(web_fehler, "Weiterleitung ohne Ziel", 100); return -2; }
            char nu[200]; aufloesen(u, loc, nu, 200); if (!nu[0]) { kopie(web_fehler, "Weiterleitung nicht lesbar", 100); return -2; }
            kopie(u, nu, 200); continue;
        }
        if (st != 200) { kopie(web_fehler, "Der Server meldet einen Fehler (Status ", 100); char a[8]; int n = 0; uint32_t v = st; char t[8]; int tl = 0; do { t[tl++] = (char)('0' + v % 10); v /= 10; } while (v); while (tl) a[n++] = t[--tl]; a[n] = 0; int l = slen(web_fehler); kopie(web_fehler + l, a, 100 - l); l = slen(web_fehler); if (l < 98) { web_fehler[l] = ')'; web_fehler[l + 1] = 0; } return -3; }
        kopie(web_url, u, 200);
        return web_html(body, bl, u, spalten);
    }
    kopie(web_fehler, "Zu viele Weiterleitungen", 100); return -4;
}
int web_text(char* out, int max) {
    int k = 0;
    for (int i = 0; i < web_nz && k < max - 2; i++) { for (int j = 0; web_z[i].t[j] && k < max - 2; j++) out[k++] = web_z[i].t[j]; out[k++] = '\n'; }
    out[k] = 0; return k;
}
