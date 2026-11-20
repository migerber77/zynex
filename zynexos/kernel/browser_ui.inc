/* ---------- Browser-Oberflaeche (Chrome-artig): Tabs, Adressleiste, Lesezeichenleiste, Startseite; Seiten aus der eigenen Engine (webseite.c) ---------- */
#include "webseite.h"
#include "browser.h"
#undef web_text
#define web_text wd_text
#define web_titel wd_titel
#define web_url wd_url
#define web_links wd_links
extern void gfx_text_f(int x, int y, const char* s, uint32_t fg, int font);
extern int  gfx_text_fw(const char* s, int font);
extern void gfx_bild(int x, int y, int w, int h, const uint32_t* px, int stride, int y0, int y1);

#define B_LEISTE 0x00DEE1E6u
#define B_WEISS  0x00FFFFFFu
#define B_OMNI   0x00F1F3F4u
#define B_ICON   0x005F6368u
#define B_TXT    0x00202124u
#define B_BLAU   0x001A73E8u
#define B_RAND   0x00DADCE0u

static char adr[300]; static int adrlen, adr_fokus;
static int wok, wlauf, wscroll, wfeld = -1, wbild_status;
static int wx0, wy0, ww, wh, wbreite, wlinks;                    /* Seitenbereich, Inhaltsbreite, linker Rand */
struct wtab { char url[300]; char titel[64]; int scroll; char hist[10][300]; int hp, nh; };
static struct wtab wt[6]; static int nwt, awt;
static char lesez[1500]; static int lzlen;
enum { K_WB = 60, K_WV, K_WR, K_WL, K_WX, K_WS, K_LZ0 = 70, K_WTAB0 = 860, K_WTABX0 = 866, K_WNEU = 872, K_WH = 873, K_WKACHEL0 = 874 };
static void lz_laden(void) { int r = zdb_get_text("desktop.lesezeichen", lesez, sizeof(lesez) - 1); if (r < 0) r = synfs_read("lesezeichen.txt", lesez, sizeof(lesez) - 1); lzlen = r > 0 ? r : 0; lesez[lzlen] = 0; }
static int lz_zeile(int n, char* out, int max) {
    int z = 0, i = 0;
    while (i < lzlen) { int e = i; while (e < lzlen && lesez[e] != '\n') e++; if (e > i) { if (z == n) { int k = 0; for (int j = i; j < e && k < max - 1; j++) out[k++] = lesez[j]; out[k] = 0; return 1; } z++; } i = e + 1; }
    return 0;
}
static void lz_add(const char* u) {
    lz_laden(); char t[300]; for (int n = 0; lz_zeile(n, t, 300); n++) if (gleich(t, u)) { scopy(meldung, TR("Schon in den Lesezeichen.", "Already bookmarked."), 100); return; }
    int n = slen(u); if (lzlen + n + 2 >= (int)sizeof(lesez) - 4) { scopy(meldung, TR("Lesezeichen voll.", "Bookmarks full."), 100); return; }
    for (int i = 0; i < n; i++) lesez[lzlen++] = u[i];
    lesez[lzlen++] = '\n'; lesez[lzlen] = 0;
    zdb_put_text("desktop.lesezeichen", lesez, lzlen);
    scopy(meldung, TR("Lesezeichen gespeichert.", "Bookmark saved."), 100); kprint("[WEB] Lesezeichen: "); kprint(u); kprint("\n");
}
static const char* const web_vorgabe[] = { "de.wikipedia.org", "duckduckgo.com", "text.npr.org", "lite.cnn.com", "news.ycombinator.com", "example.com", 0 };
static const char* kurz_url(const char* u) { if (praefix(u, "https://")) u += 8; else if (praefix(u, "http://")) u += 7; if (praefix(u, "www.")) u += 4; return u; }
static void web_sug_suche(void) {
    nsug = 0; sugsel = -1; const char* q = adr; while (*q == ' ') q++; if (!*q) return;
    sug_q = q; int ql = slen(q); char t[300];
    if (nwt) for (int i = wt[awt].nh - 1; i >= 0; i--) { const char* u = kurz_url(wt[awt].hist[i]); if (praefix(u, q)) sug_add(u, 2, ql); }
    lz_laden(); for (int n = 0; lz_zeile(n, t, 300); n++) { const char* u = kurz_url(t); if (praefix(u, q)) sug_add(u, 0, ql); }
    for (int i = 0; web_vorgabe[i]; i++) if (praefix(web_vorgabe[i], q)) sug_add(web_vorgabe[i], 0, ql);
}
static void zeichne_web(int voll);

/* Geometrie */
static int wt_y, wtb_y, wlz_y;                                  /* Tableiste, Werkzeugleiste, Lesezeichenleiste */
static void web_geo(int* fx, int* fw, int* ty) {                /* Adressleiste */
    int wi = fenster_idx(FT_WEB); *fx = *fw = *ty = 0; if (wi < 0) return;
    struct fenster* f = &fen[wi]; *ty = f->y + WT_H + 38 + 7; *fx = f->x + 2 + 4 * 40 + 14; *fw = f->w - 4 - (4 * 40 + 14) - 56;
}
static void web_breite(void) {
    int wi = fenster_idx(FT_WEB); if (wi < 0) return; struct fenster* f = &fen[wi];
    wt_y = f->y + WT_H; wtb_y = wt_y + 38; wlz_y = wtb_y + 46;
    wx0 = f->x + 2; wy0 = wlz_y + 30; ww = f->w - 4; wh = f->y + f->h - 2 - wy0;
    wbreite = ww - 64; if (wbreite > 980) wbreite = 980; if (wbreite < 200) wbreite = 200; wlinks = (ww - 14 - wbreite) / 2;
}

/* Symbole */
static void ic_linie(int x0, int y0, int x1, int y1, uint32_t c) { int dx = x1 - x0, dy = y1 - y0, n = (dx < 0 ? -dx : dx) > (dy < 0 ? -dy : dy) ? (dx < 0 ? -dx : dx) : (dy < 0 ? -dy : dy); if (!n) n = 1; for (int i = 0; i <= n; i++) gfx_rect(x0 + dx * i / n, y0 + dy * i / n, 2, 2, c); }
static void ic_pfeil(int cx, int cy, int dir, uint32_t c) { ic_linie(cx - 7, cy, cx + 7, cy, c); int s = dir < 0 ? cx - 7 : cx + 7; ic_linie(s, cy, s - dir * 6, cy - 6, c); ic_linie(s, cy, s - dir * 6, cy + 6, c); }
static void ic_neuladen(int cx, int cy, uint32_t c, uint32_t bg) { rrect(cx - 8, cy - 8, 17, 17, 8, c); rrect(cx - 6, cy - 6, 13, 13, 6, bg); gfx_rect(cx + 2, cy - 9, 8, 7, bg); ic_linie(cx + 2, cy - 7, cx + 7, cy - 7, c); ic_linie(cx + 7, cy - 7, cx + 7, cy - 2, c); }
static void ic_haus(int cx, int cy, uint32_t c, uint32_t bg) { for (int i = 0; i < 8; i++) gfx_rect(cx - i - 1, cy - 7 + i, 2 * i + 3, 1, c); gfx_rect(cx - 6, cy, 13, 8, c); gfx_rect(cx - 4, cy + 1, 9, 6, bg); gfx_rect(cx - 1, cy + 3, 3, 5, c); }
static void ic_schloss(int x, int y, uint32_t c, uint32_t bg) { rrect(x + 1, y, 10, 10, 5, c); rrect(x + 3, y + 2, 6, 8, 3, bg); rrect(x, y + 6, 12, 9, 2, c); }
static void ic_stern(int cx, int cy, uint32_t c, int voll, uint32_t bg) { gfx_rect(cx - 5, cy - 7, 11, 15, c); if (!voll) gfx_rect(cx - 3, cy - 5, 7, 13, bg); for (int i = 0; i < 5; i++) gfx_rect(cx - i, cy + 8 - i, 2 * i + 1, 1, bg); }

/* Seite zeichnen */
static int wfbox(int f) { return f == 9 ? 22 : f == 10 ? 30 : f == 11 ? 40 : 16; }
static void web_rect(int x, int y, int w, int h, uint32_t c) { int y1 = y + h; if (y < wy0) y = wy0; if (y1 > wy0 + wh) y1 = wy0 + wh; if (y1 > y) gfx_rect(x, y, w, y1 - y, c); }
static void web_neutab_zeichnen(void) {
    int cx = wx0 + ww / 2, y = wy0 + wh / 6;
    static const char* const L[5] = { "Z", "y", "n", "e", "x" }; static const uint32_t F[5] = { 0x004285F4u, 0x00EA4335u, 0x00FBBC05u, 0x004285F4u, 0x0034A853u };
    int x = cx - 80; for (int i = 0; i < 5; i++) { gfx_text(x, y - 10, L[i], F[i], 4); x += 32; }
    int sw = ww - 120 < 580 ? ww - 120 : 580, sx = cx - sw / 2, sy = y + 80;
    rrect(sx - 1, sy - 1, sw + 2, 48, 24, B_RAND); rrect(sx, sy, sw, 46, 23, B_WEISS);
    rrect(sx + 18, sy + 14, 14, 14, 7, B_ICON); rrect(sx + 20, sy + 16, 10, 10, 5, B_WEISS); ic_linie(sx + 29, sy + 26, sx + 34, sy + 31, B_ICON);
    gtxt(sx + 46, sy + 14, adr_fokus && adrlen ? adr : TR("Mit DuckDuckGo suchen oder URL eingeben", "Search with DuckDuckGo or type a URL"), adr_fokus && adrlen ? B_TXT : 0x0080868Bu, 1);
    kn_reg(K_WL, sx, sy, sw, 46);
    /* Kacheln: Lesezeichen, dann Vorschlaege */
    char t[300]; const char* k[8]; static char kb[8][300]; int nk = 0; lz_laden();
    for (int n = 0; nk < 8 && lz_zeile(n, t, 300); n++) { scopy(kb[nk], t, 300); k[nk] = kb[nk]; nk++; }
    for (int i = 0; web_vorgabe[i] && nk < 8; i++) { int doppelt = 0; for (int j = 0; j < nk; j++) if (gleich(kurz_url(k[j]), web_vorgabe[i])) doppelt = 1; if (!doppelt) k[nk++] = web_vorgabe[i]; }
    int kw = 112, sp = nk > 4 ? 4 : nk, ky = sy + 90, kx0 = cx - sp * kw / 2;
    for (int i = 0; i < nk; i++) {
        int kx = kx0 + (i % 4) * kw, yy = ky + (i / 4) * 116; if (yy + 100 > wy0 + wh) break;
        const char* u = kurz_url(k[i]); char ini[2] = { (char)(u[0] >= 'a' && u[0] <= 'z' ? u[0] - 32 : u[0]), 0 };
        rrect(kx + 28, yy + 6, 56, 56, 28, B_OMNI); uint32_t fc = F[i % 5]; rrect(kx + 40, yy + 18, 32, 32, 16, fc); gtxt(kx + 56 - gtw(ini, 1) / 2, yy + 26, ini, B_WEISS, 1);
        char nm[16]; int l = 0; while (u[l] && u[l] != '/' && l < 14) { nm[l] = u[l]; l++; } nm[l] = 0; if (u[l] && u[l] != '/') { nm[12] = '.'; nm[13] = '.'; nm[14] = 0; }
        gfx_text_f(kx + 56 - gfx_text_fw(nm, 12) / 2, yy + 72, nm, B_TXT, 12);
        kn_reg(K_WKACHEL0 + i, kx + 8, yy, kw - 16, 96);
    }
    { char ip[32]; if (net_status(ip, 32) != 2) gtxt(cx - gtw(TR("Kein Netzwerk: zuerst verbinden.", "No network: connect first."), 1) / 2, wy0 + wh - 40, TR("Kein Netzwerk: zuerst verbinden.", "No network: connect first."), 0x00D93025u, 1); }
}
static void web_fehler_zeichnen(void) {
    int x = wx0 + wlinks + 20, y = wy0 + wh / 5;
    rrect(x, y, 56, 56, 12, B_OMNI); gfx_text_f(x + 20, y + 10, "!", 0x005F6368u, 11);
    gfx_text_f(x, y + 80, l1(TR("Diese Website ist nicht erreichbar", "This site can't be reached")), B_TXT, 10);
    gtxt(x, y + 126, meldung, 0x005F6368u, 1);
    gtxt(x, y + 156, adr, 0x0080868Bu, 1);
    gtxt(x, y + 196, TR("Tipps: Adresse pruefen, Netzwerk pruefen (System-Info), neu laden mit F5.", "Tips: check the address and the network, reload with F5."), 0x005F6368u, 1);
}
static void web_seite_zeichnen(void) {
    gfx_rect(wx0, wy0, ww, wh, wok ? wd_hg : B_WEISS);
    if (!wok) { if (wlauf) gtxt(wx0 + 40, wy0 + 40, TR("Seite wird geladen ...", "Loading page ..."), B_ICON, 1); else if (meldung[0] && adr[0]) web_fehler_zeichnen(); else web_neutab_zeichnen(); return; }
    int mx = wd_hoehe - wh; if (mx < 0) mx = 0; if (wscroll > mx) wscroll = mx; if (wscroll < 0) wscroll = 0;
    int ox = wx0 + wlinks, oy = wy0 - wscroll + 6, unten = wy0 + wh;
    for (int i = 0; i < wd_nel; i++) {
        struct wel* e = &wd_el[i]; int x = ox + e->x, y = oy + e->y;
        if (y + e->h < wy0 || y > unten) continue;
        if (e->typ == WE_TEXT) {
            int box = wfbox(e->font), ty = y + e->h - box - (e->h - box) / 2 - 1;
            if (ty < wy0 - 4 || ty + box > unten) continue;
            if (e->deko & 2) rrect(x - 2, ty - 1, e->w + 4, box + 2, 3, B_OMNI);
            gfx_text_f(x, ty, wd_txt + e->off, e->farbe, e->font);
            if (e->deko & 1) gfx_rect(x, ty + box - 1, e->w, 1, e->farbe);
        } else if (e->typ == WE_LINIE) web_rect(x, y, e->w, 1, B_RAND);
        else if (e->typ == WE_FLAECHE) web_rect(x, y, e->w, e->h, B_OMNI);
        else if (e->typ == WE_RAHMEN) {
            if (e->ref) web_rect(x, y, e->w, e->h, 0x00F8F9FAu);
            web_rect(x, y, e->w, 1, B_RAND); web_rect(x, y + e->h, e->w, 1, B_RAND); web_rect(x, y, 1, e->h, B_RAND); web_rect(x + e->w, y, 1, e->h + 1, B_RAND);
        } else if (e->typ == WE_PUNKT) {
            if (y < wy0 || y + e->h > unten) continue;
            if (e->ref > 0) { char a[8]; dec(a, (uint32_t)e->ref); cat(a, ".", 8); gfx_text_f(x + 14 - gfx_text_fw(a, 4), y + (e->h - 16) / 2 - 1, a, B_TXT, 4); }
            else rrect(x + 4, y + e->h / 2 - 3, 6, 6, 3, B_TXT);
        } else if (e->typ == WE_BILD) {
            struct wbild* b = &wd_bild[e->ref];
            if (b->status == 1 && b->px) { int w = e->w < b->dw ? e->w : b->dw, h = e->h - 4 < b->dh ? e->h - 4 : b->dh; gfx_bild(x, y, w, h, b->px, b->dw, wy0, unten); }
            else { web_rect(x, y, e->w, e->h - 4, 0x00E8EAEDu); if (b->alt[0] && y + 22 < unten && y > wy0) { char a[80]; scopy(a, b->alt, 80); int mc = (e->w - 16) / 7; if (mc < 2) a[0] = 0; else if (slen(a) > mc) a[mc] = 0; gfx_text_f(x + 8, y + 5, a, 0x005F6368u, 12); } }
        } else if (e->typ == WE_FELD) {
            if (y < wy0 || y + e->h > unten) continue;
            struct wfeld* f = &wd_feld[e->ref];
            if (f->typ == 2) { const char* t = f->wert[0] ? f->wert : TR("Senden", "Submit"); rrect(x, y + 4, e->w, e->h - 8, 8, B_RAND); rrect(x + 1, y + 5, e->w - 2, e->h - 10, 7, 0x00F8F9FAu); gfx_text_f(x + (e->w - gfx_text_fw(t, 8)) / 2, y + 12, t, 0x001A73E8u, 8); }
            else {
                int fok_ = e->ref == wfeld; rrect(x, y + 4, e->w, e->h - 8, 16, fok_ ? B_BLAU : B_RAND); rrect(x + (fok_ ? 2 : 1), y + (fok_ ? 6 : 5), e->w - (fok_ ? 4 : 2), e->h - (fok_ ? 12 : 10), 14, B_WEISS);
                const char* t = f->wert[0] ? f->wert : f->ph; char a[200]; scopy(a, t, 200); int mc = (e->w - 32) / 8; int l = slen(a); const char* z = l > mc ? a + l - mc : a;
                gfx_text_f(x + 14, y + 12, z, f->wert[0] ? B_TXT : 0x0080868Bu, 4);
                if (fok_) gfx_rect(x + 14 + (f->wert[0] ? gfx_text_fw(z, 4) : 0), y + 11, 2, 18, B_BLAU);
            }
        }
    }
    if (wd_hoehe > wh) { int sb = wx0 + ww - 12, kh = wh * wh / wd_hoehe; if (kh < 30) kh = 30; int ky = wy0 + (wh - kh) * wscroll / (wd_hoehe - wh); gfx_rect(sb, wy0, 12, wh, 0x00F8F9FAu); rrect(sb + 3, ky + 2, 6, kh - 4, 3, 0x00BDC1C6u); }
    if (wbild_status) { const char* t = TR("Bilder werden geladen ...", "Loading images ..."); int tw = gfx_text_fw(t, 12) + 20; rrect(wx0 + 6, unten - 30, tw, 24, 6, B_OMNI); gfx_text_f(wx0 + 16, unten - 26, t, B_ICON, 12); }
}
static void zeichne_web(int voll) {
    int wi = fenster_idx(FT_WEB); if (wi < 0) return;
    (void)voll; web_breite();
    struct fenster* f = &fen[wi]; int x0 = f->x + 2, w0 = f->w - 4;
    int ko = kn_owner; kn_owner = wi; kn_loesche_owner(wi);
    gfx_begin();
    web_seite_zeichnen();
    /* Tableiste */
    gfx_rect(x0, wt_y, w0, 38, B_LEISTE);
    { int tbw = (w0 - 60) / (nwt ? nwt : 1); if (tbw > 240) tbw = 240; int tx = x0 + 8;
      for (int i = 0; i < nwt; i++) {
          int akt = i == awt;
          if (akt) { rrect(tx, wt_y + 6, tbw, 40, 10, B_WEISS); }
          else if (i + 1 != awt && i + 1 < nwt) gfx_rect(tx + tbw - 1, wt_y + 14, 1, 18, 0x00A0A4A8u);
          rrect(tx + 12, wt_y + 15, 14, 14, 7, wt[i].url[0] ? (praefix(wt[i].url, "https") ? 0x0034A853u : B_ICON) : B_BLAU);
          char t[64]; scopy(t, wt[i].titel[0] ? wt[i].titel : wt[i].url[0] ? kurz_url(wt[i].url) : TR("Neuer Tab", "New tab"), 64); int mc = (tbw - 70) / 7; if (mc < 1) mc = 1; if (slen(t) > mc) { t[mc] = 0; if (mc > 3) { t[mc - 1] = '.'; t[mc - 2] = '.'; } }
          gfx_text_f(tx + 34, wt_y + 13, t, akt ? B_TXT : 0x003C4043u, 12);
          rrect(tx + tbw - 28, wt_y + 12, 20, 20, 10, akt ? B_OMNI : B_LEISTE); gtxt(tx + tbw - 22, wt_y + 13, "x", B_ICON, 1);
          kn_reg(K_WTABX0 + i, tx + tbw - 30, wt_y + 10, 24, 24); kn_reg(K_WTAB0 + i, tx, wt_y + 4, tbw - 32, 34);
          tx += tbw;
      }
      if (nwt < 6) { rrect(tx + 6, wt_y + 9, 26, 26, 13, B_LEISTE); gtxt(tx + 12, wt_y + 12, "+", B_ICON, 1); gfx_text(tx + 13, wt_y + 13, "+", B_ICON, 1); kn_reg(K_WNEU, tx + 4, wt_y + 6, 30, 30); }
    }
    /* Werkzeugleiste */
    gfx_rect(x0, wtb_y, w0, 46, B_WEISS);
    { int bx = x0 + 8, cy = wtb_y + 23; int kz = wt[awt].hp > 0, kv = wt[awt].hp + 1 < wt[awt].nh;
      ic_pfeil(bx + 16, cy, -1, kz ? B_ICON : 0x00C4C7C5u); kn_reg(K_WB, bx, wtb_y + 5, 36, 36); bx += 40;
      ic_pfeil(bx + 16, cy, 1, kv ? B_ICON : 0x00C4C7C5u); kn_reg(K_WV, bx, wtb_y + 5, 36, 36); bx += 40;
      ic_neuladen(bx + 16, cy, B_ICON, B_WEISS); kn_reg(K_WR, bx, wtb_y + 5, 36, 36); bx += 40;
      ic_haus(bx + 16, cy, B_ICON, B_WEISS); kn_reg(K_WH, bx, wtb_y + 5, 36, 36); }
    { int fx, fw, ty; web_geo(&fx, &fw, &ty);
      if (adr_fokus) { rrect(fx - 1, ty - 1, fw + 2, 34, 17, B_BLAU); rrect(fx + 1, ty + 1, fw - 2, 30, 15, B_WEISS); } else rrect(fx, ty, fw, 32, 16, B_OMNI);
      int tx = fx + 16; uint32_t ob = adr_fokus ? B_WEISS : B_OMNI;
      if (!adr_fokus && wok && praefix(wd_url, "https")) { ic_schloss(fx + 14, ty + 8, B_ICON, ob); tx = fx + 36; }
      else if (!adr_fokus && wok) { gfx_text_f(fx + 14, ty + 8, l1(TR("Nicht sicher", "Not secure")), 0x00D93025u, 12); tx = fx + 22 + gfx_text_fw("Nicht sicher", 12); }
      char a[300];
      if (adr_fokus) scopy(a, adr, 300); else scopy(a, wok ? kurz_url(wd_url) : "", 300);
      int mc = (fw - (tx - fx) - 24) / 8; int l = slen(a); const char* z = l > mc && adr_fokus ? a + l - mc : a; if (!adr_fokus && l > mc) a[mc] = 0;
      if (sel1 && adr_fokus && adrlen) rrect(tx - 2, ty + 6, gtw(z, 1) + 4, 20, 3, 0x00C2DBFFu);
      if (!z[0] && !wok) gtxt(tx, ty + 7, TR("Suchbegriff oder Adresse", "Search or address"), 0x0080868Bu, 1); else gtxt(tx, ty + 7, z, B_TXT, 1);
      if (adr_fokus) gfx_rect(tx + gtw(z, 1), ty + 6, 2, 20, B_TXT);
      kn_reg(K_WL, fx, ty, fw - 34, 32);
      int lz = 0; if (wok) { char t[300]; lz_laden(); for (int n = 0; lz_zeile(n, t, 300); n++) if (gleich(t, wd_url)) lz = 1; }
      ic_stern(fx + fw - 18, ty + 16, lz ? B_BLAU : B_ICON, lz, ob); kn_reg(K_WS, fx + fw - 34, ty, 34, 32);
      if (wlauf) gfx_rect(x0, wtb_y + 44, w0 / 3, 2, B_BLAU); }
    /* Lesezeichenleiste */
    gfx_rect(x0, wlz_y, w0, 30, B_WEISS); gfx_rect(x0, wlz_y + 29, w0, 1, B_RAND);
    { char t[300]; int bx = x0 + 12; lz_laden();
      int n = 0; for (; lz_zeile(n, t, 300) && n < 10; n++) { const char* u = kurz_url(t); char nm[24]; int l = 0; while (u[l] && u[l] != '/' && l < 22) { nm[l] = u[l]; l++; } nm[l] = 0; int w = gfx_text_fw(nm, 12) + 34; if (bx + w > x0 + w0 - 10) break;
          rrect(bx + 6, wlz_y + 9, 12, 12, 6, 0x00BDC1C6u); gfx_text_f(bx + 24, wlz_y + 6, nm, 0x003C4043u, 12); kn_reg(K_LZ0 + n, bx, wlz_y + 2, w, 26); bx += w + 4; }
      if (!n) gfx_text_f(bx, wlz_y + 6, l1(TR("Lesezeichen: auf den Stern klicken (Strg+D)", "Bookmarks: click the star (Ctrl+D)")), 0x0080868Bu, 12); }
    /* Vorschlaege der Adressleiste */
    if (nsug > 0 && adr_fokus) {
        int fx, fw, ty; web_geo(&fx, &fw, &ty); int y0 = ty + 34;
        rrect(fx - 1, y0 - 1, fw + 2, nsug * 34 + 14, 12, B_RAND); rrect(fx, y0, fw, nsug * 34 + 12, 12, B_WEISS);
        for (int i = 0; i < nsug; i++) {
            int ry = y0 + 6 + i * 34; if (i == sugsel) rrect(fx + 4, ry, fw - 8, 32, 8, B_OMNI);
            rrect(fx + 16, ry + 10, 12, 12, 6, sugtyp[i] == 2 ? B_ICON : B_BLAU);
            gtxt(fx + 40, ry + 7, sug[i], B_TXT, 1);
            const char* tg = sugtyp[i] == 0 ? TR("Lesezeichen", "Bookmark") : TR("Verlauf", "History"); gfx_text_f(fx + fw - 20 - gfx_text_fw(tg, 12), ry + 8, tg, 0x0080868Bu, 12);
            kn_reg(K_SUG0 + i, fx + 4, ry, fw - 8, 32);
        }
    }
    gfx_end();
    kn_owner = ko;
}

/* Laden und Verlauf */
static void web_gehe(const char* eingabe_url, int push) {
    if (!nwt) { nwt = 1; awt = 0; wt[0].url[0] = 0; wt[0].titel[0] = 0; wt[0].nh = 0; wt[0].hp = -1; }
    char u[300]; web_url_bauen(eingabe_url, u, 300);
    wlauf = 1; wok = 0; adr_fokus = 0; wfeld = -1; nsug = 0; sugsel = -1; scopy(adr, u, 300); adrlen = slen(adr); meldung[0] = 0; zeichne_web(1);
    kprint("[WEB] Lade: "); kprint(u); kprint("\n");
    int r = wd_laden(u);
    wlauf = 0;
    struct wtab* t = &wt[awt];
    if (r != 0) { wok = 0; scopy(meldung, wd_fehler, 100); kprint("[WEB] Fehler: "); kprint(wd_fehler); kprint("\n"); scopy(t->url, u, 300); scopy(t->titel, TR("Fehler", "Error"), 64); }
    else {
        web_breite(); wd_layout(wbreite);
        wok = 1; wscroll = 0; scopy(adr, wd_url, 300); adrlen = slen(adr);
        scopy(t->url, wd_url, 300); scopy(t->titel, wd_titel[0] ? wd_titel : kurz_url(wd_url), 64);
        if (push) { if (t->hp + 1 < 10) t->hp++; else { for (int i = 0; i < 9; i++) scopy(t->hist[i], t->hist[i + 1], 300); } scopy(t->hist[t->hp], wd_url, 300); t->nh = t->hp + 1; }
        int nt = 0; for (int i = 0; i < wd_nel; i++) if (wd_el[i].typ == WE_TEXT) nt++;
        kprint("[WEB] Seite: "); kprint(wd_titel); kprint(" ("); kprint_dec((uint32_t)nt); kprint(" Woerter, "); kprint_dec((uint32_t)wd_nlinks); kprint(" Links, "); kprint_dec((uint32_t)wd_nbild); kprint(" Bilder, "); kprint_dec((uint32_t)wd_nfeld); kprint(" Felder, Hoehe "); kprint_dec((uint32_t)wd_hoehe); kprint(")\n");
    }
    sel1 = 0; zeichne_web(1);
}
static void web_zurueck(int d) { if (!nwt) return; struct wtab* t = &wt[awt]; int n = t->hp + d; if (n < 0 || n >= t->nh) return; t->hp = n; web_gehe(t->hist[n], 0); }
static void tab_neu(const char* url) {
    if (nwt >= 6) { scopy(meldung, TR("Hoechstens 6 Tabs.", "At most 6 tabs."), 100); return; }
    if (nwt) wt[awt].scroll = wscroll;
    awt = nwt++; struct wtab* t = &wt[awt]; t->url[0] = 0; t->titel[0] = 0; t->nh = 0; t->hp = -1; t->scroll = 0;
    wok = 0; wfeld = -1; meldung[0] = 0; adr[0] = 0; adrlen = 0; adr_fokus = 1; nsug = 0;
    kprint("[WEB] Neuer Tab\n");
    if (url && *url) web_gehe(url, 1); else zeichne_web(1);
}
static void tab_wechseln(int i) {
    if (i < 0 || i >= nwt || i == awt) return;
    wt[awt].scroll = wscroll; awt = i; wfeld = -1; adr_fokus = 0; nsug = 0;
    if (wt[i].url[0]) { int sc = wt[i].scroll; web_gehe(wt[i].url, 0); wscroll = sc; zeichne_web(1); }
    else { wok = 0; meldung[0] = 0; adr[0] = 0; adrlen = 0; adr_fokus = 1; zeichne_web(1); }
}
static void web_schliessen(void) { nsug = 0; sugsel = -1; sel1 = 0; nwt = 0; fenster_schliessen_typ(FT_WEB); }
static void tab_schliessen(int i) {
    if (i < 0 || i >= nwt) return;
    if (nwt == 1) { web_schliessen(); return; }
    for (int k = i; k < nwt - 1; k++) wt[k] = wt[k + 1];
    nwt--; int neu = i < nwt ? i : nwt - 1; awt = -1;
    awt = neu; wfeld = -1;
    if (wt[awt].url[0]) { int sc = wt[awt].scroll; web_gehe(wt[awt].url, 0); wscroll = sc; zeichne_web(1); } else { wok = 0; adr[0] = 0; adrlen = 0; adr_fokus = 1; zeichne_web(1); }
}
static void web_oeffnen(const char* url) {
    nsug = 0; sugsel = -1; sel1 = 0; wlauf = 0; meldung[0] = 0;
    kprint("[DESKTOP] Browser geoeffnet\n");
    int neu = fenster_idx(FT_WEB) < 0;
    if (neu || !nwt) { nwt = 0; awt = 0; }
    fenster_oeffnen(FT_WEB);
    if (!nwt) tab_neu(url);
    else if (url && *url) web_gehe(url, 1);
}
static int web_link_bei(int x, int y) {                          /* Link unter der Maus oder -1 */
    if (!wok || y < wy0 || y >= wy0 + wh || x < wx0 || x >= wx0 + ww) return -1;
    int px = x - wx0 - wlinks, py = y - wy0 + wscroll - 6;
    for (int i = 0; i < wd_nel; i++) { struct wel* e = &wd_el[i]; if (e->link >= 0 && px >= e->x && px < e->x + e->w && py >= e->y && py < e->y + e->h) return e->link; }
    return -1;
}
static int web_bild_bei(int x, int y) {                          /* geladenes Bild unter der Maus oder -1 */
    if (!wok) return -1; int px = x - wx0 - wlinks, py = y - wy0 + wscroll - 6;
    for (int i = 0; i < wd_nel; i++) { struct wel* e = &wd_el[i]; if (e->typ == WE_BILD && px >= e->x && px < e->x + e->w && py >= e->y && py < e->y + e->h) return e->ref; }
    return -1;
}
static void web_bild_hintergrund(int b) {                         /* Originalbild holen, speichern, als Hintergrund setzen */
    if (b < 0 || b >= wd_nbild) return;
    const char *kopf, *body; uint32_t kl, bl, st;
    if (net_seite(wd_bild[b].url, &kopf, &kl, &body, &bl, &st) != 0 || st != 200 || bl < 8) { scopy(meldung, TR("Bild konnte nicht geladen werden.", "Could not load the image."), 100); return; }
    const uint8_t* d = (const uint8_t*)body; const char* nm = d[0] == 0x89 ? "hintergrund-web.png" : d[0] == 0xFF ? "hintergrund-web.jpg" : d[0] == 'G' ? "hintergrund-web.gif" : 0;
    if (!nm) { scopy(meldung, TR("Bildformat nicht unterstuetzt (nur PNG, JPEG, GIF).", "Image format not supported (PNG, JPEG, GIF only)."), 100); return; }
    synfs_create(nm, SYNFS_FILE); synfs_write(nm, body, bl); synfs_sync();
    scopy(hg_datei, nm, 32); zdb_put("ui.hgbild", hg_datei); hg_nr = 5; wand_art = -1;
    { char a[12]; dec(a, 5); zdb_put("ui.hintergrund", a); }
    kprint("[WEB] Bild als Hintergrund: "); kprint(nm); kprint("\n");
    scopy(meldung, TR("Als Hintergrund gesetzt.", "Set as background."), 100);
}
static int web_feld_bei(int x, int y) {
    if (!wok) return -1; int px = x - wx0 - wlinks, py = y - wy0 + wscroll - 6;
    for (int i = 0; i < wd_nel; i++) { struct wel* e = &wd_el[i]; if (e->typ == WE_FELD && px >= e->x && px < e->x + e->w && py >= e->y && py < e->y + e->h) return e->ref; }
    return -1;
}
static void web_senden(int feld) {
    char u[600]; int r = wd_form_url(feld, u, sizeof(u));
    if (r == -1) { scopy(meldung, TR("Dieses Formular braucht POST: noch nicht unterstuetzt.", "This form needs POST: not supported yet."), 100); zeichne_web(1); return; }
    if (r) return;
    kprint("[WEB] Formular: "); kprint(u); kprint("\n");
    char kurz[300]; scopy(kurz, u, 300); web_gehe(kurz, 1);
}
static void web_scroll(int d) { wscroll += d; int mx = wd_hoehe - wh; if (mx < 0) mx = 0; if (wscroll > mx) wscroll = mx; if (wscroll < 0) wscroll = 0; zeichne_web(0); }
static int web_klick(int id) {                                   /* Knoepfe des Browsers; 1 = erledigt */
    if (id == K_WB) web_zurueck(-1);
    else if (id == K_WV) web_zurueck(1);
    else if (id == K_WR) { if (wt[awt].url[0]) { int sc = wscroll; web_gehe(wt[awt].url, 0); wscroll = sc; zeichne_web(1); } }
    else if (id == K_WH) { wok = 0; meldung[0] = 0; adr[0] = 0; adrlen = 0; adr_fokus = 1; wt[awt].url[0] = 0; wt[awt].titel[0] = 0; zeichne_web(1); }
    else if (id == K_WL) { adr_fokus = 1; wfeld = -1; sel1 = adrlen > 0; web_sug_suche(); zeichne_web(1); }
    else if (id == K_WS) { if (wok) lz_add(wd_url); else scopy(meldung, TR("Erst eine Seite oeffnen.", "Open a page first."), 100); zeichne_web(1); }
    else if (id == K_WX) web_schliessen();
    else if (id == K_WNEU) tab_neu("");
    else if (id >= K_WTAB0 && id < K_WTAB0 + 6) tab_wechseln(id - K_WTAB0);
    else if (id >= K_WTABX0 && id < K_WTABX0 + 6) tab_schliessen(id - K_WTABX0);
    else if (id >= K_SUG0 && id < K_SUG0 + 5 && id - K_SUG0 < nsug) { char t[300]; scopy(t, sug[id - K_SUG0], 300); web_gehe(t, 1); }
    else if (id >= K_LZ0 && id < K_LZ0 + 10) { char t[300]; lz_laden(); if (lz_zeile(id - K_LZ0, t, 300)) web_gehe(t, 1); }
    else if (id >= K_WKACHEL0 && id < K_WKACHEL0 + 8) {
        char t[300]; const char* k[8]; static char kb[8][300]; int nk = 0; lz_laden();
        for (int n = 0; nk < 8 && lz_zeile(n, t, 300); n++) { scopy(kb[nk], t, 300); k[nk] = kb[nk]; nk++; }
        for (int i = 0; web_vorgabe[i] && nk < 8; i++) { int doppelt = 0; for (int j = 0; j < nk; j++) if (gleich(kurz_url(k[j]), web_vorgabe[i])) doppelt = 1; if (!doppelt) k[nk++] = web_vorgabe[i]; }
        int i = id - K_WKACHEL0; if (i < nk) { char z[300]; scopy(z, k[i], 300); web_gehe(z, 1); }
    }
    else return 0;
    return 1;
}
static void web_maus(int x, int y) {                              /* Klick in die Seite */
    if (wok && wd_hoehe > wh && x >= wx0 + ww - 14 && y >= wy0 && y < wy0 + wh) { wscroll = (y - wy0) * (wd_hoehe - wh) / (wh ? wh : 1); web_scroll(0); return; }
    int f = web_feld_bei(x, y);
    if (f >= 0) { if (wd_feld[f].typ == 2) web_senden(f); else { wfeld = f; adr_fokus = 0; nsug = 0; zeichne_web(0); } return; }
    int l = web_link_bei(x, y); if (l >= 0) { char u[300]; scopy(u, wd_links[l], 300); web_gehe(u, 1); return; }
    if (wfeld >= 0 || adr_fokus) { wfeld = -1; adr_fokus = 0; nsug = 0; zeichne_web(0); }
}
static void web_bilder_tick(void) {                               /* sichtbare Bilder nachladen (eins pro Aufruf) */
    if (!wok || wlauf || fenster_idx(FT_WEB) != nfen - 1 || flaeche != 0) { wbild_status = 0; return; }
    int i = wd_bild_naechstes(wscroll - 300, wscroll + wh + 900);
    if (i < 0) { if (wbild_status) { wbild_status = 0; zeichne_web(0); } return; }
    if (!wbild_status) { wbild_status = 1; zeichne_web(0); }
    int r = wd_bild_laden(i);
    if (r == 0) { kprint("[WEB] Bild geladen: "); kprint_dec((uint32_t)wd_bild[i].dw); kprint("x"); kprint_dec((uint32_t)wd_bild[i].dh); kprint("\n"); }
    wd_layout(wbreite); zeichne_web(0);
}
static int strg_taste(uint8_t k);
static void web_taste(uint8_t k, int shift, int altgr) {
    if (cur_ctrl) {
        if (k == 0x17) { tab_neu(""); return; }                                   /* Strg+T */
        if (k == 0x1A) { tab_schliessen(awt); return; }                           /* Strg+W */
        if (k == 0x0F) { adr_fokus = 1; wfeld = -1; sel1 = adrlen > 0; zeichne_web(1); return; }   /* Strg+L */
        if (k == 0x07) { if (wok) lz_add(wd_url); zeichne_web(1); return; }      /* Strg+D */
        if (k == 0x2B) { tab_wechseln((awt + 1) % (nwt ? nwt : 1)); return; }      /* Strg+Tab */
        if (wfeld >= 0 && k == 0x19) { struct wfeld* f = &wd_feld[wfeld]; int n = slen(f->wert); for (int i = 0; i < cliplen && n < 198; i++) if (clip[i] >= 32) f->wert[n++] = clip[i]; f->wert[n] = 0; zeichne_web(0); return; }
        if (strg_taste(k)) return;
    }
    if (k == 0x3E) { web_klick(K_WR); return; }                                    /* F5 */
    if (wfeld >= 0) {                                                              /* Formularfeld */
        struct wfeld* f = &wd_feld[wfeld]; int n = slen(f->wert);
        if (k == 0x29) wfeld = -1;
        else if (k == 0x28 || k == 0x58) { int ff = wfeld; wfeld = -1; web_senden(ff); return; }
        else if (k == 0x2A) { if (n) f->wert[n - 1] = 0; }
        else if (k == 0x2B) { int nx = -1; for (int i = wfeld + 1; i < wd_nfeld; i++) if (wd_feld[i].typ == 0) { nx = i; break; } wfeld = nx; }
        else { char c = usb_key_char(k, shift, altgr); if (((c >= 32 && c < 127) || (unsigned char)c >= 0xA0) && n < 198) { f->wert[n] = c; f->wert[n + 1] = 0; } else return; }
        zeichne_web(0); return;
    }
    if (adr_fokus) {
        if (k == 0x29) { if (nsug) { nsug = 0; sugsel = -1; } else if (wok) { adr_fokus = 0; scopy(adr, wd_url, 300); adrlen = slen(adr); } else { web_schliessen(); return; } zeichne_web(1); return; }
        if (k == 0x28 || k == 0x58) { if (sugsel >= 0 && sugsel < nsug) { scopy(adr, sug[sugsel], 300); adrlen = slen(adr); } if (adrlen) { char t[300]; scopy(t, adr, 300); web_gehe(t, 1); } return; }
        if (nsug > 0 && (k == 0x51 || k == 0x52)) { sugsel += (k == 0x51) ? 1 : -1; if (sugsel >= nsug) sugsel = -1; if (sugsel < -1) sugsel = nsug - 1; zeichne_web(1); return; }
        if (nsug > 0 && k == 0x2B) { scopy(adr, sug[sugsel >= 0 ? sugsel : 0], 300); adrlen = slen(adr); web_sug_suche(); zeichne_web(1); return; }
        if (k == 0x2A) { if (sel1) { adrlen = 0; adr[0] = 0; sel1 = 0; } else if (adrlen) adr[--adrlen] = 0; web_sug_suche(); zeichne_web(1); return; }
        char c = usb_key_char(k, shift, altgr);
        if (((c >= 32 && c < 127) || (unsigned char)c >= 0xA0) && adrlen < 290) { if (sel1) { adrlen = 0; sel1 = 0; } adr[adrlen++] = c; adr[adrlen] = 0; web_sug_suche(); zeichne_web(1); }
        return;
    }
    /* Seite */
    if (k == 0x29) { web_schliessen(); return; }
    if (k == 0x51) { web_scroll(60); return; } if (k == 0x52) { web_scroll(-60); return; }
    if (k == 0x4E || k == 0x2C) { web_scroll(shift ? -(wh - 60) : wh - 60); return; } if (k == 0x4B) { web_scroll(-(wh - 60)); return; }
    if (k == 0x4A) { wscroll = 0; web_scroll(0); return; } if (k == 0x4D) { wscroll = wd_hoehe; web_scroll(0); return; }
    if (k == 0x2A) { web_zurueck(-1); return; }
    char c = usb_key_char(k, shift, altgr);                                        /* Tippen auf der Seite: in die Adressleiste */
    if ((c >= 32 && c < 127) || (unsigned char)c >= 0xA0) { adr_fokus = 1; adrlen = 0; adr[0] = 0; sel1 = 0; adr[adrlen++] = c; adr[adrlen] = 0; web_sug_suche(); zeichne_web(1); }
}
