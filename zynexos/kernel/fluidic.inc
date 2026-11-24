/* ======================================================================= Fluidic-Design: Symbole, Hub, Rad =======================================================================
 * Nachbau des Icon-Packs (Zynex OS Quantum Fluidic Icon Pack): Glas-Squircle, Umriss mit Farbverlauf Cyan -> Blau -> Violett, Leuchtpunkte.
 * Koordinaten der Symbole wie im SVG (Raster 64 x 64). */
#define FLG 0xFFFFFFFEu                                           /* "Farbverlauf" statt fester Farbe */
static int fl_x0, fl_y0, fl_s;
#define FU(u) (fl_x0 * 16 + (u) * fl_s * 16 / 64)
#define FV(v) (fl_y0 * 16 + (v) * fl_s * 16 / 64)
static void fl_aa(int x16, int y16, uint32_t c, int a) {
    int xi = x16 >> 4, yi = y16 >> 4, fx = x16 & 15, fy = y16 & 15;
    gfx_blend(xi, yi, c, a * (16 - fx) * (16 - fy) / 256); gfx_blend(xi + 1, yi, c, a * fx * (16 - fy) / 256);
    gfx_blend(xi, yi + 1, c, a * (16 - fx) * fy / 256); gfx_blend(xi + 1, yi + 1, c, a * fx * fy / 256);
}
static uint32_t fl_gc(int u, int v) { int t = (u + v) * 256 / 128; if (t < 0) t = 0; if (t > 256) t = 256; return t < 128 ? mix(Q_CY, Q_BLAU, t * 2) : mix(Q_BLAU, Q_VIO, (t - 128) * 2); }
static void fl_seg16(int x0, int y0, int x1, int y1, uint32_t col, int w16, int a, int gu, int gv) {
    int dx = x1 - x0, dy = y1 - y0, ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy, n = (ax > ay ? ax : ay) / 8 + 1;
    for (int i = 0; i <= n; i++) {
        int X = x0 + dx * i / n - 8, Y = y0 + dy * i / n - 8; uint32_t c = col == FLG ? fl_gc(gu + (int)((int64_t)(x1 - x0) * i / n / 16 * 64 / (fl_s ? fl_s : 1)), gv + (int)((int64_t)(y1 - y0) * i / n / 16 * 64 / (fl_s ? fl_s : 1))) : col;
        if (w16 <= 20) { fl_aa(X + 8, Y + 8, c, a); continue; }
        int h = w16 / 2;
        for (int oy = -h; oy <= h; oy += 8) for (int ox = -h; ox <= h; ox += 8) if (ox * ox + oy * oy <= h * h + 16) fl_aa(X + 8 + ox, Y + 8 + oy, c, a * 2 / 3 + 20);
    }
}
static void fl_poly(const int* p, int n, int closed, uint32_t col, int wd, int a) {          /* p = u,v,u,v,... ; wd in 1/10 Einheiten */
    int w16 = wd * fl_s * 16 / 640; if (w16 < 16) w16 = 16;
    for (int i = 0; i + 1 < n || (closed && i < n); i++) { int j = (i + 1) % n; fl_seg16(FU(p[2 * i]), FV(p[2 * i + 1]), FU(p[2 * j]), FV(p[2 * j + 1]), col, w16, a, p[2 * i], p[2 * i + 1]); }
}
static void fl_ln(int u0, int v0, int u1, int v1, uint32_t col, int wd) { int p[4] = { u0, v0, u1, v1 }; fl_poly(p, 2, 0, col, wd, 255); }
static void fl_circ(int cu, int cv, int r, uint32_t col, int wd, int a, int dash) {
    int w16 = wd * fl_s * 16 / 640; if (w16 < 16) w16 = 16;
    for (int d = 0; d < 360; d += 6) {
        if (dash && (d / 6) % 3 == 2) continue;
        int u0 = cu + r * icos(d) / 1024, v0 = cv + r * isin(d) / 1024, u1 = cu + r * icos(d + 6) / 1024, v1 = cv + r * isin(d + 6) / 1024;
        fl_seg16(FU(cu) + r * fl_s * 16 / 64 * icos(d) / 1024, FV(cv) + r * fl_s * 16 / 64 * isin(d) / 1024, FU(cu) + r * fl_s * 16 / 64 * icos(d + 6) / 1024, FV(cv) + r * fl_s * 16 / 64 * isin(d + 6) / 1024, col, w16, a, u0, v0); (void)u1; (void)v1;
    }
}
static void fl_dot(int u, int v, int r10, uint32_t c, int halo) {                              /* Leuchtpunkt, r in 1/10 Einheiten */
    int cx = (FU(u) + 8) >> 4, cy = (FV(v) + 8) >> 4, rp = r10 * fl_s / 640; if (rp < 1) rp = 1;
    if (halo) for (int k = 2; k >= 1; k--) { int rr = rp + 2 * k; for (int y = -rr; y <= rr; y++) for (int x = -rr; x <= rr; x++) if (x * x + y * y <= rr * rr) gfx_blend(cx + x, cy + y, c, 22); }
    rrect(cx - rp, cy - rp, 2 * rp + 1, 2 * rp + 1, rp, c);
}
static void fl_fill(const int* p, int n, uint32_t c, int a) {                                   /* gefuellte konvexe Flaeche */
    int y0 = 9999, y1 = -9999; int X[16], Y[16]; for (int i = 0; i < n && i < 16; i++) { X[i] = (FU(p[2 * i]) + 8) >> 4; Y[i] = (FV(p[2 * i + 1]) + 8) >> 4; if (Y[i] < y0) y0 = Y[i]; if (Y[i] > y1) y1 = Y[i]; }
    for (int y = y0; y <= y1; y++) {
        int lo = 99999, hi = -99999;
        for (int i = 0; i < n; i++) { int j = (i + 1) % n; int ya = Y[i], yb = Y[j]; if ((y < ya && y < yb) || (y > ya && y > yb) || ya == yb) continue; int x = X[i] + (X[j] - X[i]) * (y - ya) / (yb - ya); if (x < lo) lo = x; if (x > hi) hi = x; }
        for (int x = lo; x <= hi; x++) gfx_blend(x, y, c, a);
    }
}
static int fl_rrpath(int u, int v, int w, int h, int r, int* o) {                             /* abgerundetes Rechteck als Linienzug */
    int n = 0; const int cxs[4] = { u + w - r, u + w - r, u + r, u + r }, cys[4] = { v + r, v + h - r, v + h - r, v + r };
    for (int c = 0; c < 4; c++) for (int k = 0; k <= 3; k++) { int d = (270 + c * 90 + k * 30) % 360; o[2 * n] = cxs[c] + r * icos(d) / 1024; o[2 * n + 1] = cys[c] + r * isin(d) / 1024; n++; }
    return n;
}
static void fl_rr(int u, int v, int w, int h, int r, uint32_t col, int wd) {                  /* Glas-Rechteck mit Farbverlauf-Rand */
    int px = (FU(u) + 8) >> 4, py = (FV(v) + 8) >> 4, pw = w * fl_s / 64, ph = h * fl_s / 64;
    rrect(px, py, pw, ph, r * fl_s / 64, mix(0x000B1830u, Q_SKY, 36));
    int p[48]; int n = fl_rrpath(u, v, w, h, r, p);
    fl_poly(p, n, 1, col, wd * 2, 55); fl_poly(p, n, 1, col, wd, 255);
}
/* ---- die 12 Symbole (Raster 64 x 64, Zahlen wie in den SVGs des Icon-Packs) ---- */
static void fl_glyph(int id) {
    switch (id) {
    case 0: { fl_rr(14, 10, 36, 44, 8, 0x0067E8F9u, 24); int w[10] = { 21, 21, 26, 34, 30, 24, 35, 34, 39, 21 }; fl_poly(w, 5, 0, 0x00FFFFFFu, 25, 255);
              fl_ln(21, 41, 43, 41, 0x0067E8F9u, 22); fl_ln(21, 47, 35, 47, Q_VIO, 22); fl_dot(43, 21, 25, Q_CY, 1); break; }
    case 1: { fl_rr(12, 12, 40, 40, 9, 0x005EEAD4u, 24); fl_ln(12, 25, 52, 25, 0x00226A99u, 14); fl_ln(12, 38, 52, 38, 0x00226A99u, 14); fl_ln(26, 12, 26, 52, 0x00226A99u, 14);
              int c[8] = { 18, 45, 28, 33, 36, 37, 46, 20 }; fl_poly(c, 4, 0, Q_CY, 28, 255); fl_dot(46, 20, 30, 0x00FFFFFFu, 1); break; }
    case 2: { fl_rr(11, 14, 42, 30, 7, Q_VIO, 24); int t[6] = { 27, 23, 39, 29, 27, 35 }; fl_fill(t, 3, Q_CY, 190); fl_poly(t, 3, 1, 0x00FFFFFFu, 14, 255);
              fl_ln(32, 44, 32, 51, FLG, 24); fl_ln(23, 51, 41, 51, FLG, 24); fl_dot(46, 20, 22, Q_ROSA, 1); break; }
    case 3: { fl_rr(11, 13, 42, 32, 7, 0x0067E8F9u, 24); int s[14] = { 18, 34, 22, 26, 28, 36, 33, 27, 37, 22, 41, 25, 45, 21 }; fl_poly(s, 7, 0, Q_CY, 25, 255);
              int pn[8] = { 39, 36, 47, 28, 50, 31, 42, 39 }; fl_fill(pn, 4, Q_VIO, 220); fl_poly(pn, 4, 1, 0x00FFFFFFu, 12, 255); fl_ln(22, 51, 42, 51, Q_SKY, 24); break; }
    case 4: { int c[16] = { 15, 46, 18, 38, 26, 35, 34, 31, 36, 25, 33, 19, 28, 16, 22, 21 }; fl_poly(c, 8, 0, FLG, 28, 255);
              int pn[10] = { 33, 31, 47, 17, 50, 20, 36, 34, 30, 36 }; fl_fill(pn, 5, mix(0x000B1830u, Q_SKY, 90), 235); fl_poly(pn, 5, 1, 0x00FFFFFFu, 20, 255); fl_dot(15, 46, 32, Q_CY, 1); break; }
    case 5: { fl_circ(32, 32, 20, FLG, 24, 255, 0);
              { int e[26]; for (int k = 0; k < 13; k++) { int d = k * 30, ex = 9 * icos(d) / 1024, ey = 20 * isin(d) / 1024; e[2 * k] = 32 + (ex * 927 + ey * 375) / 1000; e[2 * k + 1] = 32 + (-ex * 375 + ey * 927) / 1000; } fl_poly(e, 13, 0, 0x0067E8F9u, 16, 255); }
              int w[10] = { 13, 36, 22, 31, 32, 33, 42, 31, 51, 28 }; fl_poly(w, 5, 0, Q_ROSA, 22, 255); fl_dot(43, 21, 30, Q_CY, 1); break; }
    case 6: { fl_rr(15, 10, 34, 44, 8, 0x0067E8F9u, 24); fl_rr(20, 16, 24, 9, 3, 0x0067E8F9u, 14);
              static const int dx[7] = { 23, 32, 41, 23, 32, 23, 32 }, dy[7] = { 32, 32, 32, 40, 40, 47, 47 }; for (int k = 0; k < 7; k++) fl_dot(dx[k], dy[k], 22, k == 2 ? Q_CY : 0x00FFFFFFu, 0);
              fl_rr(39, 38, 5, 11, 2, Q_VIO, 14); break; }
    case 7: { int b[40]; for (int k = 0; k < 20; k++) { int d = 130 + k * 17; b[2 * k] = 32 + 18 * icos(d) / 1024; b[2 * k + 1] = 28 + 16 * isin(d) / 1024; }
              fl_poly(b, 20, 0, FLG, 24, 255); { int t[8] = { 21, 41, 13, 47, 16, 37, 17, 34 }; fl_poly(t, 4, 0, FLG, 24, 255); }
              fl_dot(25, 28, 26, Q_CY, 0); fl_dot(32, 28, 30, 0x00FFFFFFu, 0); fl_dot(39, 28, 26, 0x00C084FCu, 0); break; }
    case 8: { int h[40] = { 32, 13, 24, 15, 18, 21, 15, 28, 16, 35, 20, 40, 24, 43, 24, 50, 40, 50, 40, 43, 45, 40, 49, 35, 49, 28, 46, 20, 40, 15 };
              fl_poly(h, 15, 1, Q_VIO, 24, 255); int n[16] = { 23, 28, 29, 23, 36, 29, 41, 24 }; fl_poly(n, 4, 0, Q_CY, 20, 255); fl_ln(29, 23, 32, 35, Q_CY, 20);
              fl_dot(23, 28, 25, 0x00FFFFFFu, 0); fl_dot(29, 23, 25, Q_CY, 1); fl_dot(36, 29, 25, Q_ROSA, 1); fl_dot(41, 24, 25, 0x00FFFFFFu, 0); break; }
    case 9: { fl_circ(32, 32, 16, FLG, 25, 255, 1); fl_circ(32, 32, 8, 0x00FFFFFFu, 22, 255, 0);
              fl_dot(32, 11, 26, Q_CY, 1); fl_dot(32, 53, 26, Q_VIO, 1); fl_dot(11, 32, 26, Q_SKY, 1); fl_dot(53, 32, 26, Q_ROSA, 1); break; }
    case 10: { int f[24] = { 10, 24, 10, 20, 13, 15, 17, 14, 25, 14, 30, 19, 48, 19, 52, 21, 54, 25, 54, 47, 51, 51, 48, 52 };
               int g[34] = { 16, 52, 12, 51, 10, 48, 10, 24 }; fl_fill((int[]){ 10, 20, 54, 20, 54, 50, 10, 50 }, 4, mix(0x000B1830u, Q_SKY, 60), 120);
               fl_poly(f, 12, 0, FLG, 24, 255); fl_poly(g, 4, 0, FLG, 24, 255); fl_ln(10, 27, 54, 27, 0x0067E8F9u, 18);
               int w[12] = { 21, 38, 26, 35, 30, 41, 35, 38, 39, 36, 44, 37 }; fl_poly(w, 6, 0, Q_ROSA, 20, 255);
               fl_dot(21, 38, 23, Q_CY, 1); fl_dot(35, 38, 23, 0x00FFFFFFu, 0); fl_dot(44, 37, 21, 0x00C084FCu, 0); break; }
    default: { fl_circ(32, 23, 8, Q_CY, 23, 255, 0); int b[14] = { 16, 49, 18, 41, 24, 36, 32, 34, 40, 36, 46, 41, 48, 49 }; fl_poly(b, 7, 0, FLG, 24, 255);
               int z[8] = { 27, 42, 37, 42, 28, 49, 37, 49 }; fl_poly(z, 4, 0, Q_CY, 18, 255); break; }
    }
}
/* ---- die 12 Rad-Symbole (Quanten-Symbole des Multi-Ring-Tresors): ids 12..23 in fl_icon ---- */
static void fl_ellrot(int cu, int cv, int rx, int ry, int rot, uint32_t col, int wd) {
    int e[26]; int cr = icos(rot), sr = isin(rot);
    for (int k = 0; k < 13; k++) { int d = k * 30, ex = rx * icos(d) / 1024, ey = ry * isin(d) / 1024; e[2 * k] = cu + (ex * cr - ey * sr) / 1024; e[2 * k + 1] = cv + (ex * sr + ey * cr) / 1024; }
    fl_poly(e, 13, 0, col, wd, 255);
}
static void fl_sym(int id) {
    switch (id) {
    case 0: fl_circ(32, 32, 16, FLG, 24, 255, 0); fl_dot(32, 32, 60, Q_CY, 1); fl_dot(32, 12, 25, 0x00FFFFFFu, 0); break;                            /* Kern */
    case 1: { int w[18] = { 12, 32, 16, 28, 21, 32, 26, 36, 32, 32, 38, 28, 43, 32, 48, 36, 52, 32 }; fl_poly(w, 9, 0, Q_CY, 70, 70); fl_poly(w, 9, 0, Q_CY, 30, 255); break; }   /* Welle */
    case 2: { int b[12] = { 35, 12, 19, 34, 31, 34, 28, 52, 45, 29, 33, 29 }; fl_poly(b, 6, 1, 0x00C084FCu, 70, 70); fl_poly(b, 6, 1, 0x00C084FCu, 26, 255); break; }                    /* Blitz */
    case 3: { int h[20] = { 32, 12, 48, 19, 48, 31, 45, 40, 39, 47, 32, 52, 25, 47, 19, 40, 16, 31, 16, 19 }; fl_poly(h, 10, 1, FLG, 26, 255); break; }                                  /* Schild */
    case 4: { int l[48]; for (int k = 0; k < 24; k++) { int d = k * 15, sn = isin(d), cs = icos(d), den = 1024 + sn * sn / 1024; l[2 * k] = 32 + 21 * cs / den; l[2 * k + 1] = 32 + 21 * sn * cs / (1024 * den); } fl_poly(l, 24, 1, FLG, 28, 255); break; } /* Unendlich */
    case 5: { int t[6] = { 32, 13, 50, 47, 14, 47 }; fl_poly(t, 3, 1, 0x0067E8F9u, 26, 255); fl_ln(32, 13, 32, 47, 0x00E879F9u, 16); break; }                                           /* Prisma */
    case 6: fl_ellrot(32, 32, 20, 8, 35, Q_CY, 22); fl_ellrot(32, 32, 20, 8, -35, 0x00A855F7u, 22); fl_dot(32, 32, 35, 0x00FFFFFFu, 1); break;                                           /* Orbit */
    case 7: { int st[16] = { 32, 12, 36, 28, 52, 32, 36, 36, 32, 52, 28, 36, 12, 32, 28, 28 }; fl_poly(st, 8, 1, FLG, 24, 255); break; }                                                  /* Pulsar */
    case 8: { int e[16] = { 12, 32, 19, 25, 32, 22, 45, 25, 52, 32, 45, 39, 32, 42, 19, 39 }; fl_poly(e, 8, 1, Q_SKY, 25, 255); fl_dot(32, 32, 55, 0x00C084FCu, 1); break; }                /* Fokus */
    case 9: { int hx[12] = { 32, 13, 48, 22, 48, 42, 32, 51, 16, 42, 16, 22 }; fl_poly(hx, 6, 1, Q_CY, 24, 255); int v[6] = { 16, 22, 32, 31, 48, 22 }; fl_poly(v, 3, 0, 0x00A855F7u, 20, 255); fl_ln(32, 31, 32, 51, 0x00A855F7u, 20); break; } /* Tessera */
    case 10: { int t[6] = { 20, 22, 45, 24, 32, 45 }; fl_poly(t, 3, 1, FLG, 22, 255); fl_dot(20, 22, 40, Q_CY, 1); fl_dot(45, 24, 40, 0x00C084FCu, 1); fl_dot(32, 45, 45, 0x00FFFFFFu, 1); break; }  /* Synapse */
    default: { int z[8] = { 19, 19, 45, 19, 20, 45, 45, 45 }; fl_poly(z, 4, 0, Q_CY, 70, 70); fl_poly(z, 4, 0, FLG, 34, 255); break; }                                                    /* Zynex-Z */
    }
}
static const char* const FL_SYMNAME[12] = { "Kern", "Welle", "Blitz", "Schild", "Unendlich", "Prisma", "Orbit", "Pulsar", "Fokus", "Tessera", "Synapse", "Zynex-Z" };
static void fl_icon(int id, int x, int y, int s, int aktiv) {                                  /* Glas-Squircle mit Symbol */
    int r = s * 26 / 100;
    if (aktiv) { for (int k = 3; k >= 1; k--) rrect(x - 3 * k, y - 3 * k, s + 6 * k, s + 6 * k, r + 3 * k, mix(0x00081428u, k == 2 ? Q_VIO : Q_CY, 28 / k + 8)); }
    rrect(x, y, s, s, r, aktiv ? mix(0x000A1830u, Q_CY, 200) : mix(0x000A1830u, Q_SKY, 70));
    rr_grad(x + 1, y + 1, s - 2, s - 2, r - 1, mix(0x0016233Fu, Q_CY, 26), mix(0x00070C18u, Q_VIO, 22));
    gfx_rect(x + r, y + 1, s - 2 * r, 1, mix(0x00162440u, 0x00FFFFFFu, 70));
    fl_s = s * 76 / 100; fl_x0 = x + (s - fl_s) / 2; fl_y0 = y + (s - fl_s) / 2;
    if (id >= 12) fl_sym(id - 12); else fl_glyph(id);
}
/* ---- zentraler Rad-Anker (Quantum Hub) ---- */
static void fl_hub(int cx, int cy, int r, int t) {
    for (int k = 5; k >= 1; k--) rrect(cx - r - 3 * k, cy - r - 3 * k, 2 * (r + 3 * k), 2 * (r + 3 * k), r + 3 * k, mix(hg_zeile(cy), (k & 1) ? Q_CY : Q_VIO, 26 / k + 5));
    rrect(cx - r, cy - r, 2 * r, 2 * r, r, mix(0x0006121Eu, Q_CY, 200));
    for (int k = 0; k < r - 2; k += 2) { int rr = r - 2 - k; rrect(cx - rr - k / 5, cy - rr - k / 5, 2 * rr, 2 * rr, rr, mix(0x001A2C4Eu, 0x00050810u, k * 256 / r)); }
    for (int d = 0; d < 360; d += 3) if (((d + t) / 12) % 2 == 0) { int x16 = (cx * 16) + (r * 76 / 100) * 16 * icos(d) / 1024, y16 = (cy * 16) + (r * 76 / 100) * 16 * isin(d) / 1024; fl_aa(x16 - 8, y16 - 8, mix(Q_CY, Q_VIO, (d + 360 - 45) % 360 * 256 / 360), 200); }
    { int rd = r * 94 / 100; const uint32_t dc[4] = { Q_CY, 0x00C084FCu, Q_SKY, Q_VIO };
      for (int k = 0; k < 4; k++) { int px = cx + rd * icos(k * 90 - 90) / 1024, py = cy + rd * isin(k * 90 - 90) / 1024; for (int q = 2; q >= 1; q--) rrect(px - 1 - q, py - 1 - q, 2 + 2 * q, 2 + 2 * q, 1 + q, mix(0x00050810u, dc[k], 30 / q + 10)); rrect(px - 1, py - 1, 3, 3, 1, dc[k]); } }
    fl_s = r * 2 * 80 / 100; fl_x0 = cx - fl_s / 2; fl_y0 = cy - fl_s / 2;
    { int z[8] = { 21, 21, 43, 21, 22, 43, 43, 43 }; fl_poly(z, 4, 0, Q_CY, 70, 70); fl_poly(z, 4, 0, FLG, 36, 255); }
    { int w[14] = { 17, 33, 24, 33, 28, 31, 32, 32, 36, 33, 40, 31, 47, 31 }; fl_poly(w, 7, 0, 0x00FFFFFFu, 17, 230); }
    fl_dot(32, 32, 32, 0x00FFFFFFu, 1);
}
/* ---- das Rad (Halbring unten): Glas, Prisma-Rand, Symbole, Hub, Name des gewaehlten Programms ---- */
static void fl_rad(int cx, int cy, int R, int prozent, int n, int sel, unsigned offen, const char* name, const char* intent, int hubt) {
    int ra = (R + 50) * prozent / 100, ri = (R - 50) * prozent / 100;
    for (int y = cy - ra; y <= cy + 10; y++) {                                              /* Glas-Halbring */
        int dy = cy - y; if (dy < 0) dy = 0; int xa = isq(ra * ra - dy * dy), xi = dy < ri ? isq(ri * ri - dy * dy) : 0;
        for (int x = cx - xa; x < cx - xi; x++) { int t = (x - (cx - xa)) * 128 / (xa ? xa : 1); gfx_blend(x, y, mix(0x000B2038u, 0x001E0F44u, t * 256 / 256), 205 - dy * 40 / (ra ? ra : 1)); }
        for (int x = cx + xi; x < cx + xa; x++) { int t = 128 + (x - cx) * 128 / (xa ? xa : 1); gfx_blend(x, y, mix(0x000B2038u, 0x001E0F44u, t), 205 - dy * 40 / (ra ? ra : 1)); }
    }
    for (int d2 = 0; d2 <= 360; d2++) {                                                      /* Prisma-Rand (Kegelverlauf Cyan - Weiss - Violett) */
        int d = d2 / 2; int t = d2 * 256 / 360; uint32_t c = t < 128 ? mix(Q_CY, 0x00E8F4FFu, t) : mix(0x00E8F4FFu, Q_VIO, t - 128); if (d2 % 90 > 70) c = mix(c, 0x00FFFFFFu, 120);
        int xo = (cx * 16) + ra * 16 * icos(d2 / 2 * 1) / 1024, yo = (cy * 16) - ra * 16 * isin(d2 / 2) / 1024;
        fl_aa(xo - 8, yo - 8, c, 190); fl_aa(xo - 8 + (icos(d) >= 0 ? -16 : 16), yo - 8 + 16, c, 90);
        int xi2 = (cx * 16) + ri * 16 * icos(d) / 1024, yi2 = (cy * 16) - ri * 16 * isin(d) / 1024; fl_aa(xi2 - 8, yi2 - 8, mix(c, 0x00000000u, 90), 150);
        if (d2 % 3 == 0) { int rm = (ra + ri) / 2; fl_aa((cx * 16) + rm * 16 * icos(d) / 1024 - 8, (cy * 16) - rm * 16 * isin(d) / 1024 - 8, Q_SKY, 70); }
    }
    for (int i = 0; i < n; i++) {
        int deg = 180 - i * 180 / (n - 1), r = R * prozent / 100, x = cx + r * icos(deg) / 1024, y = cy - r * isin(deg) / 1024;
        int dist = sel < 0 ? 9 : (i > sel ? i - sel : sel - i), s = i == sel ? 68 : dist == 1 ? 56 : 48;
        fl_icon(i, x - s / 2, y - s / 2, s, i == sel);
        if (offen & (1u << i)) { rrect(x - 3, y + s / 2 + 5, 7, 7, 3, Q_CY); }
    }
    if (prozent >= 100) {
        char t[80]; int l = 0; for (const char* q = name; *q && l < 30; q++) t[l++] = *q; for (const char* q = " - "; *q; q++) t[l++] = *q; for (const char* q = intent; *q && l < 78; q++) t[l++] = *q; t[l] = 0;
        if (sel < 0) { l = 0; for (const char* q = "Zynex"; *q; q++) t[l++] = *q; t[l] = 0; }
        int w = gtw(t, 1) + 32, px = cx - w / 2, py = cy - 44;
        rrect(px - 1, py - 1, w + 2, 30, 15, mix(0x00081428u, Q_CY, 90)); rrect(px, py, w, 28, 14, 0x00060D1Cu); gtxt(px + 16, py + 6, t, 0x00A5F3FCu, 1);
        rrect(px + 8, py + 11, 6, 6, 3, Q_CY);
    }
    fl_hub(cx, cy + 18, 30, hubt);
}
