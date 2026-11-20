/* ======================================================================= Zynex Zeichnen =======================================================================
 * Leinwand im Speicher (0x00RRGGBB), Werkzeuge: Pinsel, Radierer, Linie, Rechteck, Ellipse, Fuellen, Text, Pipette; Rueckgaengig (5 Schritte),
 * Effekte, Speichern/Oeffnen als PNG. KI: "Zynie zeichnet" (Beschreibung -> Zeichenbefehle), Bildideen. Agent: Bild-Agent (Illustration zum Dokument). */
#include "font_aa.h"
#include "bilder.h"
enum { K_M0 = 800 };
static uint32_t *mz, *mz_ziel, *mz_undo[5], *mz_ag; static int mz_w, mz_h, mz_nu, mz_uk;
static int m_werkz, m_gr = 6, m_gef, m_geaendert, m_zieh, m_x0, m_y0, m_lx, m_ly, m_tipp, m_tx, m_ty, m_tlen; static uint32_t m_farbe = 0x00202124u;
static char mname[28], m_text[100];
static int mc_x, mc_y, mc_vw, mc_vh;                                  /* Leinwand auf dem Bildschirm */
static int *m_stapel; static uint32_t m_stapel_max;
static uint8_t *m_ladepuffer, *m_arb; static uint32_t *m_bild;
static const uint32_t MPAL[24] = { 0x00000000u, 0x00FFFFFFu, 0x007F7F7Fu, 0x00C3C3C3u, 0x00880015u, 0x00ED1C24u, 0x00FF7F27u, 0x00FFF200u, 0x0022B14Cu, 0x0000A2E8u, 0x003F48CCu, 0x00A349A4u,
                                   0x00B97A57u, 0x00FFAEC9u, 0x00FFC90Eu, 0x00EFE4B0u, 0x00B5E61Du, 0x0099D9EAu, 0x007092BEu, 0x00C8BFE7u, 0x00202124u, 0x001A73E8u, 0x0034A853u, 0x00EA4335u };
static const char* const MWERKZ[8][2] = { { "Pinsel (B)", "Brush (B)" }, { "Radierer (E)", "Eraser (E)" }, { "Linie (L)", "Line (L)" }, { "Rechteck (R)", "Rectangle (R)" }, { "Ellipse (O)", "Ellipse (O)" }, { "Fuellen (F)", "Fill (F)" }, { "Text (T)", "Text (T)" }, { "Pipette (I)", "Picker (I)" } };

static int mal_speicher(void) {
    if (mz) return 0;
    mz_w = W - 120 < 1280 ? W - 120 : 1280; mz_h = H - 300 < 760 ? H - 300 : 760; if (mz_w < 320) mz_w = 320; if (mz_h < 200) mz_h = 200;
    uint32_t seiten = ((uint32_t)mz_w * (uint32_t)mz_h * 4 + 4095) / 4096;
    if (pmm_get_free_pages() < seiten * 9 + 20000) { mz_w = 640; mz_h = 400; seiten = ((uint32_t)mz_w * (uint32_t)mz_h * 4 + 4095) / 4096; }
    mz = (uint32_t*)pmm_alloc_pages(seiten); if (!mz) return -1;
    for (int i = 0; i < 5; i++) mz_undo[i] = (uint32_t*)pmm_alloc_pages(seiten);
    mz_ag = (uint32_t*)pmm_alloc_pages(seiten);
    m_stapel_max = 512u * 1024u; m_stapel = (int*)pmm_alloc_pages(m_stapel_max * 8 / 4096);
    for (int i = 0; i < mz_w * mz_h; i++) mz[i] = 0x00FFFFFFu;
    mz_ziel = mz; mz_nu = 0; mz_uk = 0;
    return 0;
}
/* ---- Zeichnen in den Puffer mz_ziel ---- */
static void m_px(int x, int y, uint32_t c) { if (x >= 0 && y >= 0 && x < mz_w && y < mz_h) mz_ziel[y * mz_w + x] = c; }
static void m_span(int x0, int x1, int y, uint32_t c) { if (y < 0 || y >= mz_h) return; if (x0 < 0) x0 = 0; if (x1 >= mz_w) x1 = mz_w - 1; for (int x = x0; x <= x1; x++) mz_ziel[y * mz_w + x] = c; }
static void m_scheibe(int cx, int cy, int r, uint32_t c) { if (r < 1) { m_px(cx, cy, c); return; } for (int dy = -r; dy <= r; dy++) { int dx = isq(r * r - dy * dy); m_span(cx - dx, cx + dx, cy + dy, c); } }
static void m_strich(int x0, int y0, int x1, int y1, int r, uint32_t c) {
    int dx = x1 - x0, dy = y1 - y0, n = (dx < 0 ? -dx : dx) > (dy < 0 ? -dy : dy) ? (dx < 0 ? -dx : dx) : (dy < 0 ? -dy : dy); if (!n) n = 1;
    int st = r > 2 ? r / 2 : 1;
    for (int i = 0; i <= n; i += st) m_scheibe(x0 + dx * i / n, y0 + dy * i / n, r, c);
    m_scheibe(x1, y1, r, c);
}
static void m_rechteck(int x0, int y0, int x1, int y1, uint32_t c, int gef, int r) {
    if (x1 < x0) { int t = x0; x0 = x1; x1 = t; } if (y1 < y0) { int t = y0; y0 = y1; y1 = t; }
    if (gef) { for (int y = y0; y <= y1; y++) m_span(x0, x1, y, c); return; }
    m_strich(x0, y0, x1, y0, r, c); m_strich(x1, y0, x1, y1, r, c); m_strich(x1, y1, x0, y1, r, c); m_strich(x0, y1, x0, y0, r, c);
}
static void m_ellipse(int cx, int cy, int rx, int ry, uint32_t c, int gef, int r) {
    if (rx < 1) rx = 1; if (ry < 1) ry = 1;
    if (gef) { for (int dy = -ry; dy <= ry; dy++) { int dx = (int)((int64_t)rx * isqf((uint32_t)((int64_t)(ry * ry - dy * dy) * 1024 * 1024 / ((int64_t)ry * ry))) / 1024); m_span(cx - dx, cx + dx, cy + dy, c); } return; }
    int lx = cx + rx, ly = cy;
    for (int d = 2; d <= 360; d += 2) { int x = cx + rx * icos(d) / 1024, y = cy + ry * isin(d) / 1024; m_strich(lx, ly, x, y, r, c); lx = x; ly = y; }
}
static void m_dreieck(int x0, int y0, int x1, int y1, int x2, int y2, uint32_t c) {
    int ymin = y0 < y1 ? (y0 < y2 ? y0 : y2) : (y1 < y2 ? y1 : y2), ymax = y0 > y1 ? (y0 > y2 ? y0 : y2) : (y1 > y2 ? y1 : y2);
    int px[3] = { x0, x1, x2 }, py[3] = { y0, y1, y2 };
    for (int y = ymin; y <= ymax; y++) {
        int xs[3], n = 0;
        for (int i = 0; i < 3; i++) { int j = (i + 1) % 3; int ya = py[i], yb = py[j]; if (ya == yb) continue; if ((y >= ya && y < yb) || (y >= yb && y < ya)) xs[n++] = px[i] + (y - ya) * (px[j] - px[i]) / (yb - ya); }
        if (n >= 2) { int a = xs[0], b = xs[1]; if (a > b) { int t = a; a = b; b = t; } m_span(a, b, y, c); }
    }
}
static void m_text_zeichnen(int x, int y, const char* s, uint32_t c, int font) {   /* geglaettete Schrift in die Leinwand */
    const struct aa_glyph* f = aa_glyphen + font * AA_ZEICHEN;
    for (; *s; s++) {
        const struct aa_glyph* g = f + aa_index((unsigned char)*s); const uint8_t* a = aa_daten + g->off;
        for (int j = 0; j < g->h; j++) for (int i = 0; i < g->w; i++) {
            int al = a[j * g->w + i], xx = x + g->x0 + i, yy = y + g->y0 + j; if (!al || xx < 0 || yy < 0 || xx >= mz_w || yy >= mz_h) continue;
            uint32_t d = mz_ziel[yy * mz_w + xx], r = 0; for (int sh = 0; sh <= 16; sh += 8) { int dv = (int)((d >> sh) & 255), cv = (int)((c >> sh) & 255); r |= (uint32_t)(dv + (cv - dv) * al / 255) << sh; }
            mz_ziel[yy * mz_w + xx] = r;
        }
        x += g->adv;
    }
}
static int m_tfont(void) { return m_gr <= 3 ? 4 : m_gr <= 6 ? 9 : m_gr <= 12 ? 10 : 11; }
static void m_fuellen(int x, int y, uint32_t c) {
    if (x < 0 || y < 0 || x >= mz_w || y >= mz_h || !m_stapel) return;
    uint32_t alt = mz_ziel[y * mz_w + x]; if (alt == c) return;
    uint32_t sp = 0; m_stapel[sp++] = x; m_stapel[sp++] = y;
    while (sp) {
        int yy = m_stapel[--sp], xx = m_stapel[--sp];
        while (xx > 0 && mz_ziel[yy * mz_w + xx - 1] == alt) xx--;
        int ob = 0, un = 0;
        for (; xx < mz_w && mz_ziel[yy * mz_w + xx] == alt; xx++) {
            mz_ziel[yy * mz_w + xx] = c;
            if (yy > 0) { int t = mz_ziel[(yy - 1) * mz_w + xx] == alt; if (t && !ob && sp + 2 < m_stapel_max * 2) { m_stapel[sp++] = xx; m_stapel[sp++] = yy - 1; } ob = t; }
            if (yy < mz_h - 1) { int t = mz_ziel[(yy + 1) * mz_w + xx] == alt; if (t && !un && sp + 2 < m_stapel_max * 2) { m_stapel[sp++] = xx; m_stapel[sp++] = yy + 1; } un = t; }
        }
    }
}
/* ---- Rueckgaengig ---- */
static void m_merken(void) { if (!mz_undo[mz_uk]) return; uint32_t* u = mz_undo[mz_uk]; for (int i = 0; i < mz_w * mz_h; i++) u[i] = mz[i]; mz_uk = (mz_uk + 1) % 5; if (mz_nu < 5) mz_nu++; }
static uint32_t* m_letzter(void) { return mz_nu ? mz_undo[(mz_uk + 4) % 5] : 0; }
static void m_rueckgaengig(void) { if (!mz_nu) { scopy(ob_meld, TR("Nichts rueckgaengig zu machen", "Nothing to undo"), 100); return; } mz_uk = (mz_uk + 4) % 5; mz_nu--; uint32_t* u = mz_undo[mz_uk]; for (int i = 0; i < mz_w * mz_h; i++) mz[i] = u[i]; m_geaendert = 1; kprint("[MAL] Rueckgaengig\n"); }
static void m_bereich_zurueck(int x0, int y0, int x1, int y1) {          /* Vorschau: Bereich aus dem letzten Stand wiederherstellen */
    uint32_t* u = m_letzter(); if (!u) return;
    if (x0 > x1) { int t = x0; x0 = x1; x1 = t; } if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }
    if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0; if (x1 >= mz_w) x1 = mz_w - 1; if (y1 >= mz_h) y1 = mz_h - 1;
    for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++) mz[y * mz_w + x] = u[y * mz_w + x];
}
/* ---- Anzeige ---- */
static void m_zeige(int x0, int y0, int x1, int y1) {
    if (x0 > x1) { int t = x0; x0 = x1; x1 = t; } if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }
    if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0; if (x1 >= mz_w) x1 = mz_w - 1; if (y1 >= mz_h) y1 = mz_h - 1; if (x1 >= mc_vw) x1 = mc_vw - 1;
    if (x1 < x0 || y1 < y0) return;
    gfx_begin(); gfx_bild(mc_x + x0, mc_y + y0, x1 - x0 + 1, y1 - y0 + 1, mz + y0 * mz_w + x0, mz_w, mc_y, mc_y + mc_vh); gfx_end();
}
static void m_alles_zeigen(void) { m_zeige(0, 0, mz_w - 1, mz_h - 1); }
/* ---- PNG speichern (unkomprimiertes Deflate) ---- */
static uint32_t crc_tab[256]; static int crc_ok;
static uint32_t crc32_(const uint8_t* d, uint32_t n, uint32_t c) {
    if (!crc_ok) { for (uint32_t i = 0; i < 256; i++) { uint32_t v = i; for (int k = 0; k < 8; k++) v = v & 1 ? 0xEDB88320u ^ (v >> 1) : v >> 1; crc_tab[i] = v; } crc_ok = 1; }
    c = ~c; for (uint32_t i = 0; i < n; i++) c = crc_tab[(c ^ d[i]) & 255] ^ (c >> 8); return ~c;
}
static void be32w(uint8_t* p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }
static int m_png(const uint32_t* px, int w, int h, uint8_t* o, uint32_t max) {
    uint32_t roh = (uint32_t)h * (1 + (uint32_t)w * 3), bloecke = (roh + 65534) / 65535, idat = 2 + roh + bloecke * 5 + 4;
    if (8 + 25 + 12 + idat + 12 > max) return -1;
    static const uint8_t sig[8] = { 0x89, 'P', 'N', 'G', 13, 10, 26, 10 }; uint32_t n = 0;
    for (int i = 0; i < 8; i++) o[n++] = sig[i];
    be32w(o + n, 13); n += 4; uint32_t c0 = n; o[n++] = 'I'; o[n++] = 'H'; o[n++] = 'D'; o[n++] = 'R'; be32w(o + n, (uint32_t)w); n += 4; be32w(o + n, (uint32_t)h); n += 4; o[n++] = 8; o[n++] = 2; o[n++] = 0; o[n++] = 0; o[n++] = 0;
    be32w(o + n, crc32_(o + c0, 17, 0)); n += 4;
    be32w(o + n, idat); n += 4; uint32_t ci = n; o[n++] = 'I'; o[n++] = 'D'; o[n++] = 'A'; o[n++] = 'T'; o[n++] = 0x78; o[n++] = 0x01;
    uint32_t a1 = 1, a2 = 0, rest = roh, pos = 0;                         /* Rohdaten zeilenweise erzeugen und in Bloecke packen */
    uint32_t blk_offen = 0, blk_kopf = 0;
    for (int y = 0; y < h; y++) for (int x = -1; x < w; x++) for (int k = 0; k < (x < 0 ? 1 : 3); k++) {
        uint8_t b = x < 0 ? 0 : (uint8_t)(px[y * w + x] >> (16 - 8 * k));
        if (!blk_offen) { uint32_t l = rest > 65535 ? 65535 : rest; rest -= l; blk_kopf = n; o[n++] = rest ? 0 : 1; o[n++] = (uint8_t)l; o[n++] = (uint8_t)(l >> 8); o[n++] = (uint8_t)~l; o[n++] = (uint8_t)(~l >> 8); blk_offen = l; }
        o[n++] = b; blk_offen--; pos++; a1 = (a1 + b) % 65521; a2 = (a2 + a1) % 65521;
    }
    (void)blk_kopf; (void)pos;
    be32w(o + n, a2 << 16 | a1); n += 4;
    be32w(o + n, crc32_(o + ci, n - ci, 0)); n += 4;
    be32w(o + n, 0); n += 4; uint32_t ce = n; o[n++] = 'I'; o[n++] = 'E'; o[n++] = 'N'; o[n++] = 'D'; be32w(o + n, crc32_(o + ce, 4, 0)); n += 4;
    return (int)n;
}
static int m_puffer(void) {
    if (m_ladepuffer) return 0;
    m_ladepuffer = (uint8_t*)pmm_alloc_pages(2048); m_arb = (uint8_t*)pmm_alloc_pages(3072); m_bild = (uint32_t*)pmm_alloc_pages(2048);   /* 8 MB, 12 MB, 2 M Pixel */
    return m_ladepuffer && m_arb && m_bild ? 0 : -1;
}
static void m_speichern_als(const char* name, const uint32_t* px) {
    if (m_puffer()) { scopy(ob_meld, TR("Kein Speicher zum Speichern", "No memory to save"), 100); return; }
    int n = m_png(px, mz_w, mz_h, m_ladepuffer, 2048u * 4096u);
    if (n < 0) { scopy(ob_meld, TR("Bild zu gross", "Image too large"), 100); return; }
    ob_speichere(name, (const char*)m_ladepuffer, n);
}
static void mal_speichern(void) { m_speichern_als(mname, mz); m_geaendert = 0; }
static void mal_laden(const char* name) {
    if (mal_speicher() || m_puffer()) return;
    int r = synfs_read(name, m_ladepuffer, 2048u * 4096u); if (r <= 0) return;
    int w, h; if (bild_dekodieren(m_ladepuffer, (uint32_t)r, m_bild, 2048u * 1024u, m_arb, 3072u * 4096u, &w, &h) != 0) { scopy(ob_meld, TR("Bild nicht lesbar", "Image not readable"), 100); return; }
    m_merken();
    for (int y = 0; y < mz_h; y++) for (int x = 0; x < mz_w; x++) mz[y * mz_w + x] = (x < w && y < h) ? m_bild[y * w + x] : 0x00FFFFFFu;
    scopy(mname, name, 28); m_geaendert = 0; kprint("[OFFICE] Bild geoeffnet: "); kprint(name); kprint("\n");
}
static void mal_neu(void) { if (mal_speicher()) return; for (int i = 0; i < mz_w * mz_h; i++) mz[i] = 0x00FFFFFFu; ob_name_neu("bild-", ".png", mname); m_geaendert = 0; mz_nu = 0; }
static void mal_init(void) { if (!mname[0]) mal_neu(); }
/* ---- Zeichenbefehle (von der KI) ---- */
static int m_zahl(const char** p, int* v) {
    while (**p == ' ' || **p == ',' || **p == '\t' || **p == '(' || **p == ')' || **p == '=' || **p == ':') (*p)++;
    int neg = 0; if (**p == '-') { neg = 1; (*p)++; }
    if (**p < '0' || **p > '9') return 0;
    int x = 0; while (**p >= '0' && **p <= '9') { x = x * 10 + (**p - '0'); (*p)++; } if (**p == '.') { (*p)++; while (**p >= '0' && **p <= '9') (*p)++; }
    *v = neg ? -x : x; return 1;
}
static int m_farbe_lesen(const char** p, uint32_t* c) {
    while (**p == ' ' || **p == ',' || **p == '\t') (*p)++;
    if (**p == '#') { const char* q = *p + 1; int n = 0; uint32_t v = 0; while (n < 6) { int d = (*q >= '0' && *q <= '9') ? *q - '0' : ((*q | 32) >= 'a' && (*q | 32) <= 'f') ? (*q | 32) - 'a' + 10 : -1; if (d < 0) break; v = v << 4 | (uint32_t)d; n++; q++; }
        if (n == 3) v = ((v >> 8) & 15) * 0x110000u + ((v >> 4) & 15) * 0x1100u + (v & 15) * 0x11u; if (n == 3 || n == 6) { *c = v; *p = q; return 1; } return 0; }
    static const struct { const char* n; uint32_t c; } T[] = { { "schwarz", 0 }, { "black", 0 }, { "weiss", 0xFFFFFF }, { "white", 0xFFFFFF }, { "rot", 0xE53935 }, { "red", 0xE53935 }, { "gruen", 0x43A047 }, { "green", 0x43A047 }, { "blau", 0x1E88E5 }, { "blue", 0x1E88E5 }, { "gelb", 0xFDD835 }, { "yellow", 0xFDD835 }, { "orange", 0xFB8C00 }, { "braun", 0x795548 }, { "brown", 0x795548 }, { "grau", 0x9E9E9E }, { "gray", 0x9E9E9E }, { "grey", 0x9E9E9E }, { "lila", 0x8E24AA }, { "purple", 0x8E24AA }, { "rosa", 0xF48FB1 }, { "pink", 0xF48FB1 }, { "hellblau", 0x81D4FA }, { "lightblue", 0x81D4FA }, { 0, 0 } };
    for (int i = 0; T[i].n; i++) if (praefix(*p, T[i].n)) { *c = T[i].c; *p += slen(T[i].n); return 1; }
    return 0;
}
static int m_befehle(const char* t) {                                    /* eine Zeile je Befehl; Rueckgabe: Anzahl gezeichneter Formen */
    int n = 0;
    while (*t) {
        const char* e = t; while (*e && *e != '\n') e++;
        char z[200]; int l = (int)(e - t); if (l > 199) l = 199; for (int i = 0; i < l; i++) z[i] = t[i]; z[l] = 0;
        const char* p = z; while (*p == ' ' || *p == '-' || *p == '*' || (*p >= '0' && *p <= '9' && (p[1] == '.' || p[1] == ')'))) { if (*p >= '0' && *p <= '9') p++; p++; }
        int a[8]; uint32_t c = 0;
        #define WORT(w) (praefix(p, w) ? (p += slen(w), 1) : 0)
        if (WORT("hintergrund") || WORT("background")) { if (m_farbe_lesen(&p, &c)) { for (int i = 0; i < mz_w * mz_h; i++) mz_ziel[i] = c; n++; } }
        else if (WORT("rechteck") || WORT("rect")) { if (m_zahl(&p, &a[0]) && m_zahl(&p, &a[1]) && m_zahl(&p, &a[2]) && m_zahl(&p, &a[3]) && m_farbe_lesen(&p, &c)) { m_rechteck(a[0], a[1], a[0] + a[2], a[1] + a[3], c, 1, 1); n++; } }
        else if (WORT("kreis") || WORT("circle")) { if (m_zahl(&p, &a[0]) && m_zahl(&p, &a[1]) && m_zahl(&p, &a[2]) && m_farbe_lesen(&p, &c)) { m_ellipse(a[0], a[1], a[2], a[2], c, 1, 1); n++; } }
        else if (WORT("ellipse")) { if (m_zahl(&p, &a[0]) && m_zahl(&p, &a[1]) && m_zahl(&p, &a[2]) && m_zahl(&p, &a[3]) && m_farbe_lesen(&p, &c)) { m_ellipse(a[0], a[1], a[2], a[3], c, 1, 1); n++; } }
        else if (WORT("linie") || WORT("line")) { if (m_zahl(&p, &a[0]) && m_zahl(&p, &a[1]) && m_zahl(&p, &a[2]) && m_zahl(&p, &a[3]) && m_zahl(&p, &a[4]) && m_farbe_lesen(&p, &c)) { m_strich(a[0], a[1], a[2], a[3], a[4] / 2 > 0 ? a[4] / 2 : 1, c); n++; } }
        else if (WORT("dreieck") || WORT("triangle")) { if (m_zahl(&p, &a[0]) && m_zahl(&p, &a[1]) && m_zahl(&p, &a[2]) && m_zahl(&p, &a[3]) && m_zahl(&p, &a[4]) && m_zahl(&p, &a[5]) && m_farbe_lesen(&p, &c)) { m_dreieck(a[0], a[1], a[2], a[3], a[4], a[5], c); n++; } }
        else if (WORT("text")) { if (m_zahl(&p, &a[0]) && m_zahl(&p, &a[1]) && m_zahl(&p, &a[2]) && m_farbe_lesen(&p, &c)) { while (*p == ' ' || *p == '"') p++; char s[100]; int k = 0; while (*p && *p != '"' && k < 99) s[k++] = *p++; s[k] = 0; m_text_zeichnen(a[0], a[1], s, c, a[2] <= 16 ? 4 : a[2] <= 22 ? 9 : a[2] <= 30 ? 10 : 11); n++; } }
        #undef WORT
        t = *e ? e + 1 : e;
    }
    return n;
}
static void m_ki_zeichnen(const char* motiv, int ergaenzen, uint32_t* ziel, char* out, int max) {
    char auftrag[900], g[40], a[12];
    scopy(auftrag, TR("Zeichne auf eine Leinwand von ", "Draw on a canvas of "), 900); dec(a, (uint32_t)mz_w); cat(auftrag, a, 900); cat(auftrag, "x", 900); dec(a, (uint32_t)mz_h); cat(auftrag, a, 900);
    cat(auftrag, TR(" Pixeln (0,0 = oben links). Antworte NUR mit Befehlen, einer pro Zeile, ohne Erklaerung:\n", " pixels (0,0 = top left). Reply ONLY with commands, one per line, no explanation:\n"), 900);
    cat(auftrag, "HINTERGRUND #rrggbb\nRECHTECK x y breite hoehe #rrggbb\nKREIS x y radius #rrggbb\nELLIPSE x y rx ry #rrggbb\nLINIE x1 y1 x2 y2 dicke #rrggbb\nDREIECK x1 y1 x2 y2 x3 y3 #rrggbb\nTEXT x y groesse #rrggbb Text\n", 900);
    cat(auftrag, TR("Spaetere Befehle liegen oben. Hoechstens 60 Befehle. ", "Later commands are on top. At most 60 commands. "), 900);
    if (ergaenzen) cat(auftrag, TR("Es gibt schon ein Bild; ergaenze es nur, KEIN HINTERGRUND. ", "There is already a picture; only add to it, NO BACKGROUND. "), 900);
    cat(auftrag, TR("Motiv: ", "Subject: "), 900); cat(auftrag, motiv, 900);
    (void)g;
    int n = ob_ki(auftrag, 0, out, max);
    if (n <= 0) return;
    uint32_t* alt = mz_ziel; mz_ziel = ziel; int k = m_befehle(out); mz_ziel = alt;
    kprint("[MAL] KI-Zeichnung: "); kprint_dec((uint32_t)k); kprint(" Formen\n");
    if (!k) { scopy(ob_meld, TR("Die KI-Antwort enthielt keine Zeichenbefehle.", "The AI reply contained no drawing commands."), 100); }
}
/* ---- Fenster ---- */
static void zeichne_mal(void) {
    int wi = fenster_idx(FT_MAL); if (wi < 0) return;
    if (mal_speicher()) return;
    int ko = kn_owner; kn_owner = wi; kn_loesche_owner(wi);
    gfx_begin();
    char st[120], a[12]; scopy(st, mname, 120); cat(st, "   |   ", 120); dec(a, (uint32_t)mz_w); cat(st, a, 120); cat(st, " x ", 120); dec(a, (uint32_t)mz_h); cat(st, a, 120);
    cat(st, "   |   ", 120); cat(st, TR(MWERKZ[m_werkz][0], MWERKZ[m_werkz][1]), 120); cat(st, TR(", Groesse ", ", size "), 120); dec(a, (uint32_t)m_gr); cat(st, a, 120);
    cat(st, m_geaendert ? TR("   |   nicht gespeichert", "   |   unsaved") : "", 120);
    ob_rahmen(wi, FT_MAL, st);
    if (ob_tab[FT_MAL] == 0) {
        rb_knopf(K_M0 + 0, TR("Neu", "New"), 0, OC_MAL); rb_knopf(K_M0 + 1, TR("Oeffnen", "Open"), 0, OC_MAL); rb_knopf(K_M0 + 2, TR("Speichern", "Save"), 0, OC_MAL); rb_gruppe(TR("Datei", "File"));
        static const char* const KN[8][2] = { { "Pinsel", "Brush" }, { "Radierer", "Eraser" }, { "Linie", "Line" }, { "Rechteck", "Rect" }, { "Ellipse", "Ellipse" }, { "Fuellen", "Fill" }, { "Text", "Text" }, { "Pipette", "Picker" } };
        for (int i = 0; i < 8; i++) rb_knopf(K_M0 + 3 + i, KN[i][lang == 0 ? 0 : 1], m_werkz == i, OC_MAL); rb_gruppe(TR("Werkzeuge", "Tools"));
        static const int GR[4] = { 2, 6, 12, 24 };
        for (int i = 0; i < 4; i++) { static const char* const GN[4] = { "S", "M", "L", "XL" }; rb_knopf(K_M0 + 11 + i, GN[i], m_gr == GR[i], OC_MAL); } rb_gruppe(TR("Groesse", "Size"));
        rb_knopf(K_M0 + 15, TR("Rueckgaengig", "Undo"), 0, OC_MAL); rb_knopf(K_M0 + 16, m_gef ? TR("Gefuellt", "Filled") : TR("Umriss", "Outline"), m_gef, OC_MAL); rb_gruppe(TR("Bearbeiten", "Edit"));
    } else {
        rb_knopf(K_M0 + 20, TR("Zynie zeichnet ...", "Zynie draws ..."), 0, OC_MAL); rb_knopf(K_M0 + 21, TR("Ergaenzen ...", "Add to it ..."), 0, OC_MAL); rb_knopf(K_M0 + 22, TR("Bildideen", "Ideas"), 0, OC_MAL); rb_gruppe(TR("Zynie malt mit", "Zynie paints"));
        rb_knopf(K_M0 + 23, TR("Graustufen", "Grayscale"), 0, OC_MAL); rb_knopf(K_M0 + 24, TR("Invertieren", "Invert"), 0, OC_MAL); rb_knopf(K_M0 + 25, TR("Weichzeichnen", "Blur"), 0, OC_MAL); rb_gruppe(TR("Effekte", "Effects"));
        rb_knopf(K_M0 + 26, TR("Bild-Agent", "Image agent"), 0, OC_MAL); rb_gruppe(TR("Agenten", "Agents"));
    }
    int ax = oa_x, ay = oa_y, aw = oa_w, ah = oa_h, sw = 96;
    gfx_rect(ax, ay, sw, ah, O_RIB); gfx_rect(ax + sw, ay, 1, ah, O_RAND);
    rrect(ax + 14, ay + 12, 68, 44, 8, O_RAND); rrect(ax + 16, ay + 14, 64, 40, 7, m_farbe);
    for (int i = 0; i < 24; i++) { int x = ax + 12 + (i % 3) * 25, y = ay + 70 + (i / 3) * 25; if (y + 22 > ay + ah) break; if (MPAL[i] == m_farbe) rrect(x - 2, y - 2, 24, 24, 6, O_BLAU); rrect(x, y, 20, 20, 5, MPAL[i] == 0x00FFFFFFu ? O_RAND : MPAL[i]); if (MPAL[i] == 0x00FFFFFFu) rrect(x + 1, y + 1, 18, 18, 4, MPAL[i]); kn_reg(K_M0 + 30 + i, x - 2, y - 2, 24, 24); }
    gfx_rect(ax + sw + 1, ay, aw - sw - 1, ah, 0x00E6E6E6u);
    int fx = ax + sw + 1 + 16, fy = ay + 14, fw = aw - sw - 1 - 32, fh = ah - 28;
    mc_vw = mz_w < fw ? mz_w : fw; mc_vh = mz_h < fh ? mz_h : fh; mc_x = fx + (fw - mc_vw) / 2; mc_y = fy;
    gfx_rect(mc_x + 4, mc_y + 4, mc_vw, mc_vh, 0x00C0C0C0u);
    gfx_end();
    m_alles_zeigen();
    kn_owner = ko;
}
static void m_vorschau(int x, int y) {                                  /* Linie/Rechteck/Ellipse waehrend des Ziehens */
    int bx0 = m_x0 < m_lx ? m_x0 : m_lx, bx1 = m_x0 > m_lx ? m_x0 : m_lx, by0 = m_y0 < m_ly ? m_y0 : m_ly, by1 = m_y0 > m_ly ? m_y0 : m_ly; int r = m_gr + 2;
    if (m_werkz == 4) { int rx = m_lx - m_x0, ry = m_ly - m_y0; if (rx < 0) rx = -rx; if (ry < 0) ry = -ry; bx0 = m_x0 - rx; bx1 = m_x0 + rx; by0 = m_y0 - ry; by1 = m_y0 + ry; }
    m_bereich_zurueck(bx0 - r, by0 - r, bx1 + r, by1 + r);
    int r2 = m_gr / 2 > 0 ? m_gr / 2 : 1;
    if (m_werkz == 2) m_strich(m_x0, m_y0, x, y, r2, m_farbe);
    else if (m_werkz == 3) m_rechteck(m_x0, m_y0, x, y, m_farbe, m_gef, r2);
    else { int rx = x - m_x0, ry = y - m_y0; if (rx < 0) rx = -rx; if (ry < 0) ry = -ry; m_ellipse(m_x0, m_y0, rx, ry, m_farbe, m_gef, r2); }
    int nx0 = m_x0 < x ? m_x0 : x, nx1 = m_x0 > x ? m_x0 : x, ny0 = m_y0 < y ? m_y0 : y, ny1 = m_y0 > y ? m_y0 : y;
    if (m_werkz == 4) { int rx = x - m_x0, ry = y - m_y0; if (rx < 0) rx = -rx; if (ry < 0) ry = -ry; nx0 = m_x0 - rx; nx1 = m_x0 + rx; ny0 = m_y0 - ry; ny1 = m_y0 + ry; }
    m_zeige((bx0 < nx0 ? bx0 : nx0) - r, (by0 < ny0 ? by0 : ny0) - r, (bx1 > nx1 ? bx1 : nx1) + r, (by1 > ny1 ? by1 : ny1) + r);
    m_lx = x; m_ly = y;
}
static void m_text_zeigen(void) { uint32_t* u = m_letzter(); if (u) for (int i = 0; i < mz_w * mz_h; i++) mz[i] = u[i]; m_text_zeichnen(m_tx, m_ty, m_text, m_farbe, m_tfont()); int w = gfx_text_fw(m_text, m_tfont()); m_zeige(m_tx - 4, m_ty - 4, m_tx + w + 60, m_ty + 60); if (m_tipp) { gfx_begin(); gfx_rect(mc_x + m_tx + w + 1, mc_y + m_ty, 2, m_tfont() == 4 ? 16 : m_tfont() == 9 ? 22 : m_tfont() == 10 ? 30 : 40, O_BLAU); gfx_end(); } }
static int mal_runter(int x, int y) {
    if (x < mc_x || y < mc_y || x >= mc_x + mc_vw || y >= mc_y + mc_vh) return 0;
    int mx = x - mc_x, my = y - mc_y; kp_fokus = 0;
    if (m_tipp) { m_tipp = 0; m_text_zeigen(); }
    if (m_werkz == 7) { m_farbe = mz[my * mz_w + mx]; zeichne_mal(); return 1; }
    m_merken(); m_geaendert = 1;
    if (m_werkz == 5) { m_fuellen(mx, my, m_farbe); m_alles_zeigen(); kprint("[MAL] Gefuellt\n"); return 1; }
    if (m_werkz == 6) { m_tipp = 1; m_tx = mx; m_ty = my; m_tlen = 0; m_text[0] = 0; m_text_zeigen(); return 1; }
    m_x0 = m_lx = mx; m_y0 = m_ly = my; m_zieh = 1; ob_maus = 1;
    if (m_werkz <= 1) { uint32_t c = m_werkz == 1 ? 0x00FFFFFFu : m_farbe; int r = m_gr / 2 > 0 ? m_gr / 2 : 1; m_scheibe(mx, my, r, c); m_zeige(mx - r, my - r, mx + r, my + r); }
    return 1;
}
static void mal_zug(int x, int y) {
    if (!m_zieh) return;
    int mx = x - mc_x, my = y - mc_y; if (mx < 0) mx = 0; if (my < 0) my = 0; if (mx >= mz_w) mx = mz_w - 1; if (my >= mz_h) my = mz_h - 1;
    if (mx == m_lx && my == m_ly) return;
    if (m_werkz <= 1) { uint32_t c = m_werkz == 1 ? 0x00FFFFFFu : m_farbe; int r = m_gr / 2 > 0 ? m_gr / 2 : 1; m_strich(m_lx, m_ly, mx, my, r, c); m_zeige((m_lx < mx ? m_lx : mx) - r, (m_ly < my ? m_ly : my) - r, (m_lx > mx ? m_lx : mx) + r, (m_ly > my ? m_ly : my) + r); m_lx = mx; m_ly = my; }
    else m_vorschau(mx, my);
}
static void mal_los(int x, int y) { (void)x; (void)y; if (m_zieh) { kprint("[MAL] Strich\n"); } m_zieh = 0; ob_maus = 0; zeichne_mal(); }
static void m_effekt(int e) {
    m_merken(); m_geaendert = 1;
    if (e == 0) for (int i = 0; i < mz_w * mz_h; i++) { uint32_t p = mz[i]; uint32_t g = (((p >> 16) & 255) * 77 + ((p >> 8) & 255) * 150 + (p & 255) * 29) >> 8; mz[i] = g * 0x010101u; }
    else if (e == 1) for (int i = 0; i < mz_w * mz_h; i++) mz[i] = ~mz[i] & 0x00FFFFFFu;
    else { uint32_t* u = m_letzter(); for (int y = 1; y < mz_h - 1; y++) for (int x = 1; x < mz_w - 1; x++) { uint32_t r = 0, g = 0, b = 0; for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) { uint32_t p = u[(y + dy) * mz_w + x + dx]; r += p >> 16 & 255; g += p >> 8 & 255; b += p & 255; } mz[y * mz_w + x] = (r / 9) << 16 | (g / 9) << 8 | (b / 9); } }
    kprint("[MAL] Effekt "); kprint_dec((uint32_t)e); kprint("\n");
}
static void mal_klick(int id) {
    int k = id - K_M0;
    if (k == 0) { if (m_geaendert) mal_speichern(); mal_neu(); }
    else if (k == 1) { if (m_geaendert) mal_speichern(); char nm[28]; if (ob_naechste("bild-", ".png", mname, nm)) mal_laden(nm); else scopy(ob_meld, TR("Noch keine Bilder gespeichert", "No images saved yet"), 100); }
    else if (k == 2) mal_speichern();
    else if (k >= 3 && k < 11) { m_werkz = k - 3; m_tipp = 0; }
    else if (k >= 11 && k < 15) { static const int GR[4] = { 2, 6, 12, 24 }; m_gr = GR[k - 11]; }
    else if (k == 15) m_rueckgaengig();
    else if (k == 16) m_gef = !m_gef;
    else if (k == 20 || k == 21) { kp_auf[FT_MAL] = 1; kp_fokus = FT_MAL; kp_modus = k == 20 ? KE_MAL : KE_MAL_DAZU; }
    else if (k == 22) { static char out[1500]; int n = ob_ki(TR("Gib 5 kurze, konkrete Ideen fuer einfache Zeichnungen (je eine Zeile).", "Give 5 short, concrete ideas for simple drawings (one per line)."), 0, out, sizeof(out)); (void)n; kp_zeige(FT_MAL, out, KA_KEINE); }
    else if (k >= 23 && k <= 25) m_effekt(k - 23);
    else if (k == 26) { if (m_geaendert) mal_speichern(); ag_start(8); flaeche_wechsel(2); return; }
    else if (k >= 30 && k < 54) { m_farbe = MPAL[k - 30]; if (m_werkz == 1 || m_werkz == 7) m_werkz = 0; }
    zeichne_mal();
}
static void mal_taste(uint8_t k, int shift, int altgr) {
    if (cur_ctrl && k == 0x16) { mal_speichern(); zeichne_mal(); return; }
    if (cur_ctrl && k == 0x1D) { m_tipp = 0; m_rueckgaengig(); zeichne_mal(); return; }
    char c = usb_key_char(k, shift, altgr);
    if (m_tipp) {
        if (k == 0x29) { m_tipp = 0; m_rueckgaengig(); zeichne_mal(); return; }
        if (k == 0x28 || k == 0x58) { m_tipp = 0; m_text_zeigen(); kprint("[MAL] Text: "); kprint(m_text); kprint("\n"); zeichne_mal(); return; }
        if (k == 0x2A) { if (m_tlen) m_text[--m_tlen] = 0; }
        else if (((c >= 32 && c < 127) || (unsigned char)c >= 0xA0) && m_tlen < 98) { m_text[m_tlen++] = c; m_text[m_tlen] = 0; }
        else return;
        m_text_zeigen(); return;
    }
    if (k == 0x29) { office_schliessen(FT_MAL); return; }
    int w = c == 'b' ? 0 : c == 'e' ? 1 : c == 'l' ? 2 : c == 'r' ? 3 : c == 'o' ? 4 : c == 'f' ? 5 : c == 't' ? 6 : c == 'i' ? 7 : -1;
    if (w >= 0) { m_werkz = w; zeichne_mal(); return; }
    if (c == '+') { m_gr = m_gr >= 24 ? 24 : m_gr * 2; zeichne_mal(); return; }
    if (c == '-') { m_gr = m_gr <= 2 ? 2 : m_gr / 2; zeichne_mal(); return; }
}
/* Bild-Agent: Illustration zum Dokument (Agent 8) */
static void ag_bild(void) {
    if (mal_speicher()) { al(TR("Kein Speicher fuer das Bild.", "No memory for the image.")); return; }
    static char q[2600]; char titel[100];
    if (!ob_quelltext(q, sizeof(q)) || !q[0]) scopy(q, TR("Zynex OS", "Zynex OS"), sizeof(q));
    { const char* p = q; while (*p == '#' || *p == ' ') p++; int k = 0; while (p[k] && p[k] != '\n' && k < 60) { titel[k] = p[k]; k++; } titel[k] = 0; }
    char z[100]; scopy(z, TR("Thema: ", "Topic: "), 100); cat(z, titel, 100); al(z);
    uint32_t* alt = mz_ziel; mz_ziel = mz_ag;
    for (int y = 0; y < mz_h; y++) { uint32_t c = mix(0x001E3A8Au, 0x007C3AEDu, y * 256 / mz_h); for (int x = 0; x < mz_w; x++) mz_ag[y * mz_w + x] = c; }
    int ki = ext_quelle() || gehirn_vorhanden(), n = 0;
    if (ki) { static char out[2400]; al(TR("Zynie zeichnet eine Illustration ...", "Zynie draws an illustration ...")); char motiv[400]; scopy(motiv, TR("eine schlichte, farbige Illustration zum Thema: ", "a simple, colourful illustration about: "), 400); cat(motiv, titel, 400); m_ki_zeichnen(motiv, 0, mz_ag, out, sizeof(out)); mz_ziel = mz_ag; for (const char* p = out; *p; p++) if (*p == '\n') n++; }
    if (!n) {                                                            /* ohne KI: Titelbild mit Kreisen */
        mz_ziel = mz_ag;
        for (int i = 0; i < 7; i++) m_ellipse(mz_w * (i * 37 % 100) / 100, mz_h * (i * 53 % 100) / 100, 40 + i * 18, 40 + i * 18, mix(0x007C3AEDu, 0x0022D3EEu, i * 36), 1, 1);
        m_rechteck(40, mz_h / 2 - 50, mz_w - 40, mz_h / 2 + 50, 0x00FFFFFFu, 1, 1);
        m_text_zeichnen(70, mz_h / 2 - 22, titel, 0x001E3A8Au, 10);
        al(TR("Ohne KI: Titelbild mit dem Thema.", "Without AI: cover image with the topic."));
    }
    mz_ziel = alt;
    al(TR("Vorschlag: als Bild speichern und in Zeichnen oeffnen.", "Proposal: save as image and open in Draw.")); pend = 7;
    al(TR("Wartet auf deine Freigabe.", "Waiting for your approval.")); kprint("[AGENT] Freigabe angefragt: Bild speichern\n");
}
static void ag_bild_frei(void) {
    m_merken(); for (int i = 0; i < mz_w * mz_h; i++) mz[i] = mz_ag[i];
    ob_name_neu("bild-", ".png", mname); mal_speichern();
    char z[100]; scopy(z, TR("Freigegeben: gespeichert als ", "Approved: saved as "), 100); cat(z, mname, 100); al(z);
}
