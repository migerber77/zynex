/* Zynex OS - Bild-Dekoder fuer den Browser: PNG (eigenes Inflate), JPEG (baseline), GIF (erstes Bild). Ausgabe 0x00RRGGBB.
 * Ohne Gleitkomma (Kernel ohne SSE). Transparenz wird auf Weiss gemischt. */
#include "bilder.h"

/* ======================================================================= Inflate (RFC 1951) ======================================================================= */
struct inf { const uint8_t* in; uint32_t inlen, inpos; uint32_t bitbuf, bitcnt; uint8_t* out; uint32_t outmax, outpos; int err; };
struct huff { uint16_t cnt[16]; uint16_t sym[320]; };
static int bits(struct inf* s, int n) {
    uint32_t v = s->bitbuf;
    while (s->bitcnt < (uint32_t)n) { if (s->inpos >= s->inlen) { s->err = 1; return 0; } v |= (uint32_t)s->in[s->inpos++] << s->bitcnt; s->bitcnt += 8; }
    s->bitbuf = v >> n; s->bitcnt -= (uint32_t)n; return (int)(v & ((1u << n) - 1));
}
static int dekod(struct inf* s, const struct huff* h) {
    int code = 0, first = 0, index = 0;
    for (int len = 1; len < 16; len++) {
        code |= bits(s, 1); if (s->err) return -1;
        int c = h->cnt[len];
        if (code - c < first) return h->sym[index + (code - first)];
        index += c; first += c; first <<= 1; code <<= 1;
    }
    s->err = 2; return -1;
}
static int baue(struct huff* h, const uint8_t* len, int n) {
    uint16_t offs[16];
    for (int i = 0; i < 16; i++) h->cnt[i] = 0;
    for (int i = 0; i < n; i++) h->cnt[len[i]]++;
    if (h->cnt[0] == n) return 0;
    int left = 1; for (int i = 1; i < 16; i++) { left <<= 1; left -= h->cnt[i]; if (left < 0) return -1; }
    offs[1] = 0; for (int i = 1; i < 15; i++) offs[i + 1] = (uint16_t)(offs[i] + h->cnt[i]);
    for (int i = 0; i < n; i++) if (len[i]) h->sym[offs[len[i]]++] = (uint16_t)i;
    return left;
}
static const uint16_t LBASE[29] = { 3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258 };
static const uint16_t LEXT[29] = { 0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0 };
static const uint16_t DBASE[30] = { 1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577 };
static const uint16_t DEXT[30] = { 0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13 };
static int codes(struct inf* s, const struct huff* lc, const struct huff* dc) {
    for (;;) {
        int sym = dekod(s, lc); if (sym < 0) return -1;
        if (sym < 256) { if (s->outpos >= s->outmax) return -2; s->out[s->outpos++] = (uint8_t)sym; }
        else if (sym == 256) return 0;
        else {
            sym -= 257; if (sym >= 29) return -3;
            int len = LBASE[sym] + bits(s, LEXT[sym]);
            int ds = dekod(s, dc); if (ds < 0 || ds >= 30) return -3;
            uint32_t dist = DBASE[ds] + (uint32_t)bits(s, DEXT[ds]);
            if (s->err || dist > s->outpos) return -3;
            if (s->outpos + (uint32_t)len > s->outmax) return -2;
            for (int i = 0; i < len; i++) { s->out[s->outpos] = s->out[s->outpos - dist]; s->outpos++; }
        }
    }
}
static int inflate_(struct inf* s) {
    static struct huff lc, dc; int last;
    do {
        last = bits(s, 1); int typ = bits(s, 2); if (s->err) return -1;
        if (typ == 0) {
            s->bitbuf = 0; s->bitcnt = 0;
            if (s->inpos + 4 > s->inlen) return -1;
            uint32_t len = s->in[s->inpos] | (uint32_t)s->in[s->inpos + 1] << 8; s->inpos += 4;
            if (s->inpos + len > s->inlen || s->outpos + len > s->outmax) return -2;
            for (uint32_t i = 0; i < len; i++) s->out[s->outpos++] = s->in[s->inpos++];
        } else if (typ == 1) {
            static uint8_t l[320]; int i = 0;
            for (; i < 144; i++) l[i] = 8; for (; i < 256; i++) l[i] = 9; for (; i < 280; i++) l[i] = 7; for (; i < 288; i++) l[i] = 8;
            baue(&lc, l, 288); for (i = 0; i < 30; i++) l[i] = 5; baue(&dc, l, 30);
            int r = codes(s, &lc, &dc); if (r) return r;
        } else if (typ == 2) {
            static const uint8_t ord[19] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };
            uint8_t l[320]; int nlen = bits(s, 5) + 257, ndist = bits(s, 5) + 1, ncode = bits(s, 4) + 4;
            if (s->err || nlen > 286 || ndist > 30) return -3;
            for (int i = 0; i < 19; i++) l[ord[i]] = i < ncode ? (uint8_t)bits(s, 3) : 0;
            if (baue(&lc, l, 19) != 0) return -3;
            int i = 0;
            while (i < nlen + ndist) {
                int sym = dekod(s, &lc); if (sym < 0) return -3;
                if (sym < 16) l[i++] = (uint8_t)sym;
                else {
                    int len = 0, rep;
                    if (sym == 16) { if (!i) return -3; len = l[i - 1]; rep = 3 + bits(s, 2); }
                    else if (sym == 17) rep = 3 + bits(s, 3); else rep = 11 + bits(s, 7);
                    if (i + rep > nlen + ndist) return -3;
                    while (rep--) l[i++] = (uint8_t)len;
                }
            }
            if (baue(&lc, l, nlen) < 0 || baue(&dc, l + nlen, ndist) < 0) return -3;
            int r = codes(s, &lc, &dc); if (r) return r;
        } else return -3;
    } while (!last);
    return 0;
}

/* ======================================================================= PNG ======================================================================= */
static uint32_t be32(const uint8_t* p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static uint32_t aufweiss(int r, int g, int b, int a) { r = (r * a + 255 * (255 - a)) / 255; g = (g * a + 255 * (255 - a)) / 255; b = (b * a + 255 * (255 - a)) / 255; return (uint32_t)r << 16 | (uint32_t)g << 8 | (uint32_t)b; }
static int paeth(int a, int b, int c) { int p = a + b - c, pa = p > a ? p - a : a - p, pb = p > b ? p - b : b - p, pc = p > c ? p - c : c - p; return (pa <= pb && pa <= pc) ? a : pb <= pc ? b : c; }
static int png(const uint8_t* d, uint32_t n, uint32_t* out, uint32_t maxpx, uint8_t* arb, uint32_t arbmax, int* W, int* H) {
    uint32_t p = 8, w = 0, h = 0, idat = 0; int tiefe = 0, art = 0, il = 0; uint8_t pal[256][4]; int npal = 0;
    for (int i = 0; i < 256; i++) { pal[i][0] = pal[i][1] = pal[i][2] = 0; pal[i][3] = 255; }
    int trns_g = -1, trns_r = -1, trns_gg = -1, trns_b = -1;
    while (p + 12 <= n) {
        uint32_t len = be32(d + p); const uint8_t* t = d + p + 4; const uint8_t* c = d + p + 8;
        if (p + 12 + len > n) break;
        if (t[0] == 'I' && t[1] == 'H' && t[2] == 'D' && t[3] == 'R') { w = be32(c); h = be32(c + 4); tiefe = c[8]; art = c[9]; il = c[12]; }
        else if (t[0] == 'P' && t[1] == 'L' && t[2] == 'T' && t[3] == 'E') { npal = (int)(len / 3); if (npal > 256) npal = 256; for (int i = 0; i < npal; i++) { pal[i][0] = c[3 * i]; pal[i][1] = c[3 * i + 1]; pal[i][2] = c[3 * i + 2]; } }
        else if (t[0] == 't' && t[1] == 'R' && t[2] == 'N' && t[3] == 'S') { if (art == 3) { for (uint32_t i = 0; i < len && i < 256; i++) pal[i][3] = c[i]; } else if (art == 0 && len >= 2) trns_g = c[1]; else if (art == 2 && len >= 6) { trns_r = c[1]; trns_gg = c[3]; trns_b = c[5]; } }
        else if (t[0] == 'I' && t[1] == 'D' && t[2] == 'A' && t[3] == 'T') { if (idat + len > arbmax / 3) return BILD_ZU_GROSS; for (uint32_t i = 0; i < len; i++) arb[idat + i] = c[i]; idat += len; }
        else if (t[0] == 'I' && t[1] == 'E' && t[2] == 'N' && t[3] == 'D') break;
        p += 12 + len;
    }
    if (!w || !h || !idat) return BILD_DEFEKT;
    if (il) return BILD_NICHT_UNTERSTUETZT;
    if ((uint64_t)w * h > maxpx || w > 8000 || h > 8000) return BILD_ZU_GROSS;
    int kan = art == 0 ? 1 : art == 2 ? 3 : art == 3 ? 1 : art == 4 ? 2 : art == 6 ? 4 : 0; if (!kan) return BILD_DEFEKT;
    if (tiefe != 8 && tiefe != 16 && !((art == 0 || art == 3) && (tiefe == 1 || tiefe == 2 || tiefe == 4))) return BILD_NICHT_UNTERSTUETZT;
    uint32_t bpp_bits = (uint32_t)kan * (uint32_t)tiefe, zeile = (w * bpp_bits + 7) / 8, bpp = (bpp_bits + 7) / 8, roh = (zeile + 1) * h;
    if (idat + roh > arbmax) return BILD_ZU_GROSS;
    struct inf s = { arb + 2, idat - 2, 0, 0, 0, arb + idat, roh, 0, 0 };
    if (inflate_(&s) != 0 && s.outpos < roh) return BILD_DEFEKT;
    uint8_t* r = arb + idat;
    for (uint32_t y = 0; y < h; y++) {
        uint8_t* z = r + y * (zeile + 1); uint8_t f = z[0]; z++; uint8_t* o = y ? r + (y - 1) * (zeile + 1) + 1 : 0;
        for (uint32_t i = 0; i < zeile; i++) {
            int a = i >= bpp ? z[i - bpp] : 0, b = o ? o[i] : 0, c = (o && i >= bpp) ? o[i - bpp] : 0;
            if (f == 1) z[i] = (uint8_t)(z[i] + a); else if (f == 2) z[i] = (uint8_t)(z[i] + b); else if (f == 3) z[i] = (uint8_t)(z[i] + ((a + b) >> 1)); else if (f == 4) z[i] = (uint8_t)(z[i] + paeth(a, b, c)); else if (f > 4) return BILD_DEFEKT;
        }
    }
    int st = tiefe == 16 ? 2 : 1;
    for (uint32_t y = 0; y < h; y++) {
        const uint8_t* z = r + y * (zeile + 1) + 1; uint32_t* q = out + (uint64_t)y * w;
        for (uint32_t x = 0; x < w; x++) {
            if (tiefe < 8) {
                uint32_t bit = x * (uint32_t)tiefe; int v = (z[bit >> 3] >> (8 - tiefe - (int)(bit & 7))) & ((1 << tiefe) - 1);
                if (art == 3) q[x] = aufweiss(pal[v][0], pal[v][1], pal[v][2], pal[v][3]); else { int g = v * 255 / ((1 << tiefe) - 1); q[x] = aufweiss(g, g, g, v == trns_g ? 0 : 255); }
                continue;
            }
            const uint8_t* px = z + x * (uint32_t)(kan * st);
            if (art == 0) { int g = px[0]; q[x] = aufweiss(g, g, g, (st == 1 && g == trns_g) ? 0 : 255); }
            else if (art == 2) { int rr = px[0], gg = px[st], bb = px[2 * st]; q[x] = (st == 1 && rr == trns_r && gg == trns_gg && bb == trns_b) ? 0x00FFFFFFu : (uint32_t)rr << 16 | (uint32_t)gg << 8 | (uint32_t)bb; }
            else if (art == 3) { int v = px[0]; q[x] = aufweiss(pal[v][0], pal[v][1], pal[v][2], pal[v][3]); }
            else if (art == 4) q[x] = aufweiss(px[0], px[0], px[0], px[st]);
            else q[x] = aufweiss(px[0], px[st], px[2 * st], px[3 * st]);
        }
    }
    *W = (int)w; *H = (int)h; return 0;
}

/* ======================================================================= JPEG (baseline) ======================================================================= */
struct jh { uint8_t laenge[17]; uint8_t wert[256]; int min[18], max[18], valptr[17]; };
static struct jh jdc[4], jac[4];
static uint16_t jq[4][64];
static const uint8_t ZZ[64] = { 0,1,8,16,9,2,3,10,17,24,32,25,18,11,4,5,12,19,26,33,40,48,41,34,27,20,13,6,7,14,21,28,35,42,49,56,57,50,43,36,29,22,15,23,30,37,44,51,58,59,52,45,38,31,39,46,53,60,61,54,47,55,62,63 };
static const int16_t KOS[8][8] = {   /* C(u) * cos((2x+1)u pi/16) * 4096; C(0) = 1/sqrt(2) */
    { 2896, 2896, 2896, 2896, 2896, 2896, 2896, 2896 }, { 4017, 3406, 2276, 799, -799, -2276, -3406, -4017 }, { 3784, 1567, -1567, -3784, -3784, -1567, 1567, 3784 },
    { 3406, -799, -4017, -2276, 2276, 4017, 799, -3406 }, { 2896, -2896, -2896, 2896, 2896, -2896, -2896, 2896 }, { 2276, -4017, 799, 3406, -3406, -799, 4017, -2276 },
    { 1567, -3784, 3784, -1567, -1567, 3784, -3784, 1567 }, { 799, -2276, 3406, -4017, 4017, -3406, 2276, -799 } };
struct jb { const uint8_t* d; uint32_t n, p; uint32_t buf; int cnt; int err; };
static int jbit(struct jb* b) {
    if (!b->cnt) {
        if (b->p >= b->n) { b->err = 1; return 0; }
        uint8_t c = b->d[b->p++];
        if (c == 0xFF) { uint8_t c2 = b->p < b->n ? b->d[b->p] : 0; if (c2 == 0) b->p++; else { b->err = 2; return 0; } }
        b->buf = c; b->cnt = 8;
    }
    b->cnt--; return (int)((b->buf >> b->cnt) & 1);
}
static int jbits(struct jb* b, int n) { int v = 0; for (int i = 0; i < n; i++) v = (v << 1) | jbit(b); return v; }
static int jdek(struct jb* b, struct jh* h) { int code = 0; for (int l = 1; l <= 16; l++) { code = (code << 1) | jbit(b); if (b->err) return -1; if (h->max[l] >= 0 && code <= h->max[l] && code >= h->min[l]) return h->wert[h->valptr[l] + code - h->min[l]]; } return -1; }
static int jext(int v, int t) { return t && v < (1 << (t - 1)) ? v - (1 << t) + 1 : v; }
static void jhuff_bauen(struct jh* h) {
    int code = 0, k = 0;
    for (int l = 1; l <= 16; l++) { h->valptr[l] = k; h->min[l] = code; code += h->laenge[l]; k += h->laenge[l]; h->max[l] = h->laenge[l] ? code - 1 : -1; code <<= 1; }
}
static void idct(int16_t* blk, uint8_t* o) {          /* blk in natuerlicher Reihenfolge, Ausgabe 8x8 0..255 */
    int32_t t[64];
    for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++) { int64_t s = 0; for (int u = 0; u < 8; u++) s += (int64_t)KOS[u][x] * blk[y * 8 + u]; t[y * 8 + x] = (int32_t)(s >> 11); }
    for (int x = 0; x < 8; x++) for (int y = 0; y < 8; y++) { int64_t s = 0; for (int v = 0; v < 8; v++) s += (int64_t)KOS[v][y] * t[v * 8 + x]; int r = (int)((s + (1 << 14)) >> 15) + 128; o[y * 8 + x] = (uint8_t)(r < 0 ? 0 : r > 255 ? 255 : r); }
}
static int jpeg(const uint8_t* d, uint32_t n, uint32_t* out, uint32_t maxpx, uint8_t* arb, uint32_t arbmax, int* W, int* H) {
    struct { int id, h, v, tq, td, ta; int16_t pred; } k[4]; int nk = 0, w = 0, h = 0, hmax = 1, vmax = 1, dri = 0;
    uint32_t p = 2;
    while (p + 4 <= n) {
        if (d[p] != 0xFF) { p++; continue; }
        uint8_t m = d[p + 1]; p += 2;
        if (m == 0xD8 || m == 0x01 || (m >= 0xD0 && m <= 0xD7) || m == 0xFF) { if (m == 0xFF) p--; continue; }
        if (p + 2 > n) break;
        uint32_t len = (uint32_t)d[p] << 8 | d[p + 1]; const uint8_t* s = d + p + 2; uint32_t sl = len - 2;
        if (p + len > n) return BILD_DEFEKT;
        if (m == 0xDB) { uint32_t i = 0; while (i < sl) { int pq = s[i] >> 4, tq = s[i] & 3; i++; for (int j = 0; j < 64; j++) { jq[tq][ZZ[j]] = pq ? (uint16_t)(s[i + 2 * j] << 8 | s[i + 2 * j + 1]) : s[i + j]; } i += pq ? 128 : 64; } }
        else if (m == 0xC4) { uint32_t i = 0; while (i + 17 <= sl) { int tc = s[i] >> 4, th = s[i] & 3; struct jh* hh = tc ? &jac[th] : &jdc[th]; int sum = 0; for (int l = 1; l <= 16; l++) { hh->laenge[l] = s[i + l]; sum += s[i + l]; } if (sum > 256 || i + 17 + (uint32_t)sum > sl) return BILD_DEFEKT; for (int j = 0; j < sum; j++) hh->wert[j] = s[i + 17 + j]; jhuff_bauen(hh); i += 17 + (uint32_t)sum; } }
        else if (m == 0xC0 || m == 0xC1) { h = s[1] << 8 | s[2]; w = s[3] << 8 | s[4]; nk = s[5]; if (nk != 1 && nk != 3) return BILD_NICHT_UNTERSTUETZT; for (int i = 0; i < nk; i++) { k[i].id = s[6 + 3 * i]; k[i].h = s[7 + 3 * i] >> 4; k[i].v = s[7 + 3 * i] & 15; k[i].tq = s[8 + 3 * i] & 3; if (k[i].h > hmax) hmax = k[i].h; if (k[i].v > vmax) vmax = k[i].v; if (!k[i].h || !k[i].v || k[i].h > 2 || k[i].v > 2) return BILD_NICHT_UNTERSTUETZT; } }
        else if (m == 0xC2 || m == 0xC3 || (m >= 0xC5 && m <= 0xCF && m != 0xC8 && m != 0xCC)) return BILD_NICHT_UNTERSTUETZT;
        else if (m == 0xDD) dri = s[0] << 8 | s[1];
        else if (m == 0xDA) {
            if (!w || !h) return BILD_DEFEKT;
            if ((uint64_t)w * h > maxpx) return BILD_ZU_GROSS;
            int ns = s[0]; for (int i = 0; i < ns; i++) { int id = s[1 + 2 * i]; for (int j = 0; j < nk; j++) if (k[j].id == id) { k[j].td = s[2 + 2 * i] >> 4; k[j].ta = s[2 + 2 * i] & 15; } }
            if (ns != nk) return BILD_NICHT_UNTERSTUETZT;
            int mcuw = 8 * hmax, mcuh = 8 * vmax, mx = (w + mcuw - 1) / mcuw, my = (h + mcuh - 1) / mcuh;
            /* Ebenen je Komponente (volle MCU-Groesse) im Hilfsspeicher */
            uint32_t pw[4], ph[4], off[4], ges = 0;
            for (int i = 0; i < nk; i++) { pw[i] = (uint32_t)(mx * k[i].h * 8); ph[i] = (uint32_t)(my * k[i].v * 8); off[i] = ges; ges += pw[i] * ph[i]; }
            if (ges > arbmax) return BILD_ZU_GROSS;
            struct jb b = { d, n, p + len, 0, 0, 0 }; for (int i = 0; i < nk; i++) k[i].pred = 0;
            int16_t blk[64]; uint8_t pix[64]; int rest = dri;
            for (int my_ = 0; my_ < my; my_++) for (int mx_ = 0; mx_ < mx; mx_++) {
                if (dri && !rest) {                                             /* Neustartmarke */
                    b.cnt = 0; while (b.p + 1 < n && !(d[b.p] == 0xFF && d[b.p + 1] >= 0xD0 && d[b.p + 1] <= 0xD7)) b.p++; b.p += 2; b.err = 0;
                    for (int i = 0; i < nk; i++) k[i].pred = 0; rest = dri;
                }
                for (int c = 0; c < nk; c++) for (int by = 0; by < k[c].v; by++) for (int bx = 0; bx < k[c].h; bx++) {
                    for (int i = 0; i < 64; i++) blk[i] = 0;
                    int t = jdek(&b, &jdc[k[c].td]); if (t < 0) goto ende;
                    int diff = t ? jext(jbits(&b, t), t) : 0; k[c].pred = (int16_t)(k[c].pred + diff); blk[0] = (int16_t)(k[c].pred * jq[k[c].tq][0]);
                    for (int i = 1; i < 64; ) {
                        int rs = jdek(&b, &jac[k[c].ta]); if (rs < 0) goto ende;
                        int r = rs >> 4, sz = rs & 15;
                        if (!sz) { if (r == 15) { i += 16; continue; } break; }
                        i += r; if (i > 63) break;
                        blk[ZZ[i]] = (int16_t)(jext(jbits(&b, sz), sz) * jq[k[c].tq][ZZ[i]]); i++;
                    }
                    idct(blk, pix);
                    uint8_t* e = arb + off[c]; uint32_t x0 = (uint32_t)((mx_ * k[c].h + bx) * 8), y0 = (uint32_t)((my_ * k[c].v + by) * 8);
                    for (int yy = 0; yy < 8; yy++) for (int xx = 0; xx < 8; xx++) e[(y0 + yy) * pw[c] + x0 + xx] = pix[yy * 8 + xx];
                }
                if (dri) rest--;
            }
            ende:
            for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
                int Y = arb[off[0] + (uint32_t)(y * k[0].v / vmax) * pw[0] + (uint32_t)(x * k[0].h / hmax)];
                if (nk == 1) { out[(uint64_t)y * w + x] = (uint32_t)Y * 0x010101u; continue; }
                int cb = arb[off[1] + (uint32_t)(y * k[1].v / vmax) * pw[1] + (uint32_t)(x * k[1].h / hmax)] - 128, cr = arb[off[2] + (uint32_t)(y * k[2].v / vmax) * pw[2] + (uint32_t)(x * k[2].h / hmax)] - 128;
                int r = Y + ((91881 * cr) >> 16), g = Y - ((22554 * cb + 46802 * cr) >> 16), bl = Y + ((116130 * cb) >> 16);
                r = r < 0 ? 0 : r > 255 ? 255 : r; g = g < 0 ? 0 : g > 255 ? 255 : g; bl = bl < 0 ? 0 : bl > 255 ? 255 : bl;
                out[(uint64_t)y * w + x] = (uint32_t)r << 16 | (uint32_t)g << 8 | (uint32_t)bl;
            }
            *W = w; *H = h; return 0;
        }
        p += len;
    }
    return BILD_DEFEKT;
}

/* ======================================================================= GIF ======================================================================= */
static int gif(const uint8_t* d, uint32_t n, uint32_t* out, uint32_t maxpx, uint8_t* arb, uint32_t arbmax, int* W, int* H) {
    if (n < 13) return BILD_DEFEKT;
    int sw = d[6] | d[7] << 8, sh = d[8] | d[9] << 8, fl = d[10]; uint32_t p = 13;
    uint8_t gpal[768], lpal[768]; int gn = 0, transp = -1;
    if (fl & 0x80) { gn = 2 << (fl & 7); if (p + (uint32_t)gn * 3 > n) return BILD_DEFEKT; for (int i = 0; i < gn * 3; i++) gpal[i] = d[p + (uint32_t)i]; p += (uint32_t)gn * 3; }
    if (!sw || !sh || (uint64_t)sw * sh > maxpx) return BILD_ZU_GROSS;
    for (uint32_t i = 0; i < (uint32_t)(sw * sh); i++) out[i] = 0x00FFFFFFu;
    while (p < n) {
        if (d[p] == 0x21) {                                                   /* Erweiterung */
            if (p + 2 < n && d[p + 1] == 0xF9 && d[p + 2] >= 4 && p + 7 < n && (d[p + 3] & 1)) transp = d[p + 6];
            p += 2; while (p < n && d[p]) p += d[p] + 1u; p++;
        } else if (d[p] == 0x2C) {
            if (p + 10 > n) return BILD_DEFEKT;
            int ix = d[p + 1] | d[p + 2] << 8, iy = d[p + 3] | d[p + 4] << 8, iw = d[p + 5] | d[p + 6] << 8, ih = d[p + 7] | d[p + 8] << 8, lf = d[p + 9]; p += 10;
            uint8_t* pal = gpal; int pn = gn;
            if (lf & 0x80) { pn = 2 << (lf & 7); if (p + (uint32_t)pn * 3 > n) return BILD_DEFEKT; for (int i = 0; i < pn * 3; i++) lpal[i] = d[p + (uint32_t)i]; p += (uint32_t)pn * 3; pal = lpal; }
            if (p >= n) return BILD_DEFEKT;
            int mincode = d[p++]; if (mincode < 2 || mincode > 8) return BILD_DEFEKT;
            /* Datenbloecke zusammenhaengend kopieren */
            uint32_t dl = 0; while (p < n && d[p]) { uint32_t bl = d[p]; if (p + 1 + bl > n || dl + bl > arbmax / 2) return BILD_DEFEKT; for (uint32_t i = 0; i < bl; i++) arb[dl + i] = d[p + 1 + i]; dl += bl; p += bl + 1; }
            /* LZW */
            static uint16_t prefix[4096]; static uint8_t suffix[4096], stapel[4097];
            uint8_t* idx = arb + arbmax / 2; uint32_t imax = arbmax / 2, ni = 0;
            int clear = 1 << mincode, eoi = clear + 1, nxt = clear + 2, size = mincode + 1, alt = -1; uint8_t erst = 0;
            uint32_t bitpos = 0;
            for (int i = 0; i < clear; i++) { prefix[i] = 0xFFFF; suffix[i] = (uint8_t)i; }
            while (ni < (uint32_t)(iw * ih)) {
                if ((bitpos + (uint32_t)size + 7) / 8 > dl) break;
                int code = 0; for (int b = 0; b < size; b++) { uint32_t bp = bitpos + (uint32_t)b; code |= ((arb[bp >> 3] >> (bp & 7)) & 1) << b; } bitpos += (uint32_t)size;
                if (code == clear) { size = mincode + 1; nxt = clear + 2; alt = -1; continue; }
                if (code == eoi) break;
                int c = code, sp = 0;
                if (alt < 0) { if (c >= clear) break; idx[ni++] = (uint8_t)c; alt = c; erst = (uint8_t)c; continue; }
                if (c >= nxt) { stapel[sp++] = erst; c = alt; }
                while (c >= clear && sp < 4096) { stapel[sp++] = suffix[c]; c = prefix[c]; }
                stapel[sp++] = (uint8_t)c; erst = (uint8_t)c;
                while (sp && ni < imax) idx[ni++] = stapel[--sp];
                if (nxt < 4096) { prefix[nxt] = (uint16_t)alt; suffix[nxt] = erst; nxt++; if (nxt == (1 << size) && size < 12) size++; }
                alt = code;
            }
            int inter = (lf & 0x40) != 0; static const int st[4] = { 0, 4, 2, 1 }, sc[4] = { 8, 8, 4, 2 };
            int zeile = 0, pass = 0;
            for (int y = 0; y < ih; y++) {
                int ty = y;
                if (inter) { ty = st[pass] + zeile * sc[pass]; while (ty >= ih && pass < 3) { pass++; zeile = 0; ty = st[pass]; } zeile++; }
                for (int x = 0; x < iw; x++) {
                    uint32_t q = (uint32_t)(y * iw + x); if (q >= ni) break;
                    int v = idx[q]; if (v == transp || v >= pn) continue;
                    int ox = ix + x, oy = iy + ty; if (ox >= sw || oy >= sh) continue;
                    out[(uint32_t)(oy * sw + ox)] = (uint32_t)pal[3 * v] << 16 | (uint32_t)pal[3 * v + 1] << 8 | pal[3 * v + 2];
                }
            }
            *W = sw; *H = sh; return 0;
        } else break;
    }
    return BILD_DEFEKT;
}

int bild_dekodieren(const uint8_t* d, uint32_t n, uint32_t* out, uint32_t maxpx, uint8_t* arb, uint32_t arbmax, int* w, int* h) {
    if (n > 8 && d[0] == 0x89 && d[1] == 'P' && d[2] == 'N' && d[3] == 'G') return png(d, n, out, maxpx, arb, arbmax, w, h);
    if (n > 4 && d[0] == 0xFF && d[1] == 0xD8) return jpeg(d, n, out, maxpx, arb, arbmax, w, h);
    if (n > 6 && d[0] == 'G' && d[1] == 'I' && d[2] == 'F') return gif(d, n, out, maxpx, arb, arbmax, w, h);
    return BILD_UNBEKANNT;
}
