/* Zynex OS - Public-Key-Pruefungen: RSA PKCS#1 v1.5 und ECDSA (P-256, P-384) mit Montgomery-Arithmetik */
#include "crypto.h"
#include "curves.h"

static void mc(void* d, const void* s, uint32_t n) { uint8_t* a = d; const uint8_t* b = s; for (uint32_t i = 0; i < n; i++) a[i] = b[i]; }

#define MAXW 130
struct mctx { uint32_t n[MAXW]; uint32_t rr[MAXW]; uint32_t n0; int w; };

static int w_ge(const uint32_t* a, const uint32_t* b, int w) { for (int j = w - 1; j >= 0; j--) if (a[j] != b[j]) return a[j] > b[j]; return 1; }
static int w_iszero(const uint32_t* a, int w) { for (int j = 0; j < w; j++) if (a[j]) return 0; return 1; }
static uint32_t w_sub(uint32_t* r, const uint32_t* a, const uint32_t* b, int w) {
    int64_t br = 0; for (int j = 0; j < w; j++) { int64_t d = (int64_t)a[j] - b[j] - br; r[j] = (uint32_t)d; br = d < 0; }
    return (uint32_t)br;
}
static uint32_t w_add(uint32_t* r, const uint32_t* a, const uint32_t* b, int w) {
    uint64_t c = 0; for (int j = 0; j < w; j++) { c += (uint64_t)a[j] + b[j]; r[j] = (uint32_t)c; c >>= 32; }
    return (uint32_t)c;
}
static void be2w(uint32_t* o, int w, const uint8_t* b, uint32_t n) {
    for (int i = 0; i < w; i++) o[i] = 0;
    for (uint32_t i = 0; i < n; i++) { uint32_t pos = n - 1 - i; if (pos / 4 < (uint32_t)w) o[pos / 4] |= (uint32_t)b[i] << (8 * (pos % 4)); }
}

static void mctx_init(struct mctx* c, const uint32_t* n, int w) {
    c->w = w; for (int i = 0; i < MAXW; i++) { c->n[i] = 0; c->rr[i] = 0; }
    for (int i = 0; i < w; i++) c->n[i] = n[i];
    uint32_t inv = n[0]; for (int i = 0; i < 5; i++) inv *= 2 - n[0] * inv;
    c->n0 = 0u - inv;
    c->rr[0] = 1;
    for (int i = 0; i < 64 * w; i++) {                                   /* rr = 2^(64w) mod n */
        uint32_t carry = 0;
        for (int j = 0; j < w; j++) { uint32_t nc = c->rr[j] >> 31; c->rr[j] = (c->rr[j] << 1) | carry; carry = nc; }
        if (carry || w_ge(c->rr, c->n, w)) w_sub(c->rr, c->rr, c->n, w);
    }
}
static void mmul(const struct mctx* c, uint32_t* r, const uint32_t* a, const uint32_t* b) {
    int w = c->w; uint32_t t[MAXW + 2];
    for (int i = 0; i < w + 2; i++) t[i] = 0;
    for (int i = 0; i < w; i++) {
        uint64_t C = 0, s;
        for (int j = 0; j < w; j++) { s = (uint64_t)t[j] + (uint64_t)a[j] * b[i] + C; t[j] = (uint32_t)s; C = s >> 32; }
        s = (uint64_t)t[w] + C; t[w] = (uint32_t)s; t[w + 1] = (uint32_t)(s >> 32);
        uint32_t m = t[0] * c->n0;
        s = (uint64_t)t[0] + (uint64_t)m * c->n[0]; C = s >> 32;
        for (int j = 1; j < w; j++) { s = (uint64_t)t[j] + (uint64_t)m * c->n[j] + C; t[j - 1] = (uint32_t)s; C = s >> 32; }
        s = (uint64_t)t[w] + C; t[w - 1] = (uint32_t)s; C = s >> 32;
        t[w] = t[w + 1] + (uint32_t)C;
    }
    if (t[w] || w_ge(t, c->n, w)) w_sub(t, t, c->n, w);
    for (int j = 0; j < w; j++) r[j] = t[j];
}
static void to_m(const struct mctx* c, uint32_t* r, const uint32_t* a) { mmul(c, r, a, c->rr); }
static void from_m(const struct mctx* c, uint32_t* r, const uint32_t* a) {
    uint32_t one[MAXW]; for (int j = 0; j < c->w; j++) one[j] = 0; one[0] = 1; mmul(c, r, a, one);
}
/* r = a^e (a und r in Montgomery-Form, e als Worte) */
static void mexp(const struct mctx* c, uint32_t* r, const uint32_t* a, const uint32_t* e, int ew) {
    uint32_t one[MAXW], res[MAXW]; int w = c->w;
    for (int j = 0; j < w; j++) one[j] = 0;
    one[0] = 1;
    to_m(c, res, one);
    for (int i = ew * 32 - 1; i >= 0; i--) { mmul(c, res, res, res); if ((e[i / 32] >> (i % 32)) & 1) mmul(c, res, res, a); }
    for (int j = 0; j < w; j++) r[j] = res[j];
}

/* ---------- RSA PKCS#1 v1.5 ---------- */
int rsa_pkcs1_verify(const uint8_t* n, uint32_t nl, const uint8_t* e, uint32_t el, const uint8_t* sig, uint32_t sl,
                     const uint8_t* hash, uint32_t hl) {
    while (nl && !n[0]) { n++; nl--; }
    if (nl < 128 || nl > 512 || sl != nl || el == 0 || el > 4 || !(n[nl - 1] & 1)) return -1;
    uint32_t ev = 0; for (uint32_t i = 0; i < el; i++) ev = (ev << 8) | e[i];
    if (ev < 3 || !(ev & 1)) return -1;
    int w = (int)((nl + 3) / 4);
    static struct mctx c; uint32_t nw[MAXW], a[MAXW], am[MAXW], res[MAXW];
    be2w(nw, w, n, nl); be2w(a, w, sig, sl);
    if (w_ge(a, nw, w)) return -1;
    mctx_init(&c, nw, w);
    to_m(&c, am, a); for (int j = 0; j < w; j++) res[j] = am[j];
    int top = 31; while (!((ev >> top) & 1)) top--;
    for (int b = top - 1; b >= 0; b--) { mmul(&c, res, res, res); if ((ev >> b) & 1) mmul(&c, res, res, am); }
    from_m(&c, res, res);
    static uint8_t em[512];
    for (uint32_t i = 0; i < nl; i++) { uint32_t pos = nl - 1 - i; em[i] = (uint8_t)(res[pos / 4] >> (8 * (pos % 4))); }
    static const uint8_t p256[] = {0x30,0x31,0x30,0x0d,0x06,0x09,0x60,0x86,0x48,0x01,0x65,0x03,0x04,0x02,0x01,0x05,0x00,0x04,0x20};
    static const uint8_t p384[] = {0x30,0x41,0x30,0x0d,0x06,0x09,0x60,0x86,0x48,0x01,0x65,0x03,0x04,0x02,0x02,0x05,0x00,0x04,0x30};
    static const uint8_t p512[] = {0x30,0x51,0x30,0x0d,0x06,0x09,0x60,0x86,0x48,0x01,0x65,0x03,0x04,0x02,0x03,0x05,0x00,0x04,0x40};
    const uint8_t* pre = hl == 32 ? p256 : hl == 48 ? p384 : hl == 64 ? p512 : 0;
    if (!pre) return -2;
    uint32_t tl = 19 + hl;
    if (nl < tl + 11 || em[0] != 0 || em[1] != 1) return -3;
    uint32_t ps = nl - tl - 3;
    for (uint32_t i = 0; i < ps; i++) if (em[2 + i] != 0xFF) return -3;
    if (em[2 + ps] != 0) return -3;
    uint8_t d = 0;
    for (uint32_t i = 0; i < 19; i++) d |= em[3 + ps + i] ^ pre[i];
    for (uint32_t i = 0; i < hl; i++) d |= em[3 + ps + 19 + i] ^ hash[i];
    return d ? -4 : 0;
}

/* ---------- ECDSA (Jacobi-Koordinaten, a = -3, Werte in Montgomery-Form) ---------- */
struct curve { int w; const uint32_t *p, *b, *n, *gx, *gy; };
struct pt { uint32_t x[12], y[12], z[12]; };   /* z == 0: unendlich */

static void fadd(const struct mctx* c, uint32_t* r, const uint32_t* a, const uint32_t* b) {
    int w = c->w; uint32_t carry = w_add(r, a, b, w);
    if (carry || w_ge(r, c->n, w)) w_sub(r, r, c->n, w);
}
static void fsub(const struct mctx* c, uint32_t* r, const uint32_t* a, const uint32_t* b) {
    int w = c->w; if (w_sub(r, a, b, w)) w_add(r, r, c->n, w);
}
#define FM(r,a,b) mmul(F, r, a, b)
static void pt_double(const struct mctx* F, struct pt* r, const struct pt* P) {
    int w = F->w;
    if (w_iszero(P->z, w)) { *r = *P; return; }
    uint32_t delta[12], gamma[12], beta[12], alpha[12], t1[12], t2[12], x3[12], y3[12], z3[12];
    FM(delta, P->z, P->z); FM(gamma, P->y, P->y); FM(beta, P->x, gamma);
    fsub(F, t1, P->x, delta); fadd(F, t2, P->x, delta); FM(alpha, t1, t2);
    fadd(F, t1, alpha, alpha); fadd(F, alpha, t1, alpha);                       /* 3*(x-d)(x+d) */
    FM(t1, alpha, alpha); fadd(F, t2, beta, beta); fadd(F, t2, t2, t2); fadd(F, t2, t2, t2);   /* 8*beta */
    fsub(F, x3, t1, t2);
    fadd(F, t1, P->y, P->z); FM(t1, t1, t1); fsub(F, t1, t1, gamma); fsub(F, z3, t1, delta);
    fadd(F, t1, beta, beta); fadd(F, t1, t1, t1); fsub(F, t1, t1, x3);          /* 4*beta - x3 */
    FM(t1, alpha, t1);
    FM(t2, gamma, gamma); fadd(F, t2, t2, t2); fadd(F, t2, t2, t2); fadd(F, t2, t2, t2);   /* 8*gamma^2 */
    fsub(F, y3, t1, t2);
    for (int j = 0; j < w; j++) { r->x[j] = x3[j]; r->y[j] = y3[j]; r->z[j] = z3[j]; }
}
static void pt_add(const struct mctx* F, struct pt* r, const struct pt* P, const struct pt* Q) {
    int w = F->w;
    if (w_iszero(P->z, w)) { *r = *Q; return; }
    if (w_iszero(Q->z, w)) { *r = *P; return; }
    uint32_t z1z1[12], z2z2[12], u1[12], u2[12], s1[12], s2[12], h[12], i[12], j[12], rr[12], v[12], t[12], x3[12], y3[12], z3[12];
    FM(z1z1, P->z, P->z); FM(z2z2, Q->z, Q->z);
    FM(u1, P->x, z2z2); FM(u2, Q->x, z1z1);
    FM(t, Q->z, z2z2); FM(s1, P->y, t); FM(t, P->z, z1z1); FM(s2, Q->y, t);
    fsub(F, h, u2, u1); fsub(F, rr, s2, s1);
    if (w_iszero(h, w)) {
        if (w_iszero(rr, w)) { pt_double(F, r, P); return; }
        for (int k = 0; k < w; k++) r->z[k] = 0;                                 /* P = -Q */
        return;
    }
    fadd(F, rr, rr, rr);
    fadd(F, t, h, h); FM(i, t, t); FM(j, h, i);
    FM(v, u1, i);
    FM(x3, rr, rr); fsub(F, x3, x3, j); fsub(F, x3, x3, v); fsub(F, x3, x3, v);
    fsub(F, t, v, x3); FM(y3, rr, t); FM(t, s1, j); fadd(F, t, t, t); fsub(F, y3, y3, t);
    fadd(F, t, P->z, Q->z); FM(t, t, t); fsub(F, t, t, z1z1); fsub(F, t, t, z2z2); FM(z3, t, h);
    for (int k = 0; k < w; k++) { r->x[k] = x3[k]; r->y[k] = y3[k]; r->z[k] = z3[k]; }
}

static int ec_verify(const struct curve* cv, const uint8_t* q, uint32_t ql, const uint8_t* hash, uint32_t hl,
                     const uint8_t* rb, uint32_t rl, const uint8_t* sb, uint32_t sl) {
    int w = cv->w; uint32_t bytes = (uint32_t)w * 4;
    if (ql != 1 + 2 * bytes || q[0] != 4) return -1;
    static struct mctx F, N;
    mctx_init(&F, cv->p, w); mctx_init(&N, cv->n, w);
    uint32_t r[12], s[12], e[12], qx[12], qy[12], t1[12], t2[12], one[12];
    be2w(r, w, rb, rl); be2w(s, w, sb, sl);
    if (rl > bytes + 1 || sl > bytes + 1) return -2;
    if (w_iszero(r, w) || w_iszero(s, w) || w_ge(r, cv->n, w) || w_ge(s, cv->n, w)) return -2;
    be2w(qx, w, q + 1, bytes); be2w(qy, w, q + 1 + bytes, bytes);
    if (w_ge(qx, cv->p, w) || w_ge(qy, cv->p, w)) return -1;
    /* Punkt auf der Kurve? y^2 = x^3 - 3x + b */
    uint32_t xm[12], ym[12], bm[12];
    to_m(&F, xm, qx); to_m(&F, ym, qy); to_m(&F, bm, cv->b);
    mmul(&F, t1, ym, ym);
    mmul(&F, t2, xm, xm); mmul(&F, t2, t2, xm);
    { uint32_t x3[12]; fadd(&F, x3, xm, xm); fadd(&F, x3, x3, xm); fsub(&F, t2, t2, x3); }
    fadd(&F, t2, t2, bm);
    for (int j = 0; j < w; j++) if (t1[j] != t2[j]) return -1;
    /* Hash -> e */
    uint8_t eb[48]; uint32_t k = hl < bytes ? hl : bytes;
    for (uint32_t i = 0; i < bytes; i++) eb[i] = 0;
    mc(eb + (bytes - k), hash, k);
    be2w(e, w, eb, bytes);
    if (w_ge(e, cv->n, w)) w_sub(e, e, cv->n, w);
    /* w = s^-1; u1 = e*w; u2 = r*w (mod n) */
    uint32_t nm2[12], sm[12], wm[12], em[12], rm[12], u1[12], u2[12];
    for (int j = 0; j < w; j++) one[j] = 0;
    one[0] = 2;
    w_sub(nm2, cv->n, one, w);
    to_m(&N, sm, s); mexp(&N, wm, sm, nm2, w);
    to_m(&N, em, e); to_m(&N, rm, r);
    mmul(&N, u1, em, wm); mmul(&N, u2, rm, wm);
    from_m(&N, u1, u1); from_m(&N, u2, u2);
    /* P = u1*G + u2*Q (Shamir) */
    struct pt G, Q, GQ, R, tmp;
    uint32_t onem[12]; for (int j = 0; j < w; j++) { one[j] = 0; }
    one[0] = 1; to_m(&F, onem, one);
    uint32_t gxm[12], gym[12]; to_m(&F, gxm, cv->gx); to_m(&F, gym, cv->gy);
    for (int j = 0; j < w; j++) { G.x[j] = gxm[j]; G.y[j] = gym[j]; G.z[j] = onem[j]; Q.x[j] = xm[j]; Q.y[j] = ym[j]; Q.z[j] = onem[j]; R.z[j] = 0; R.x[j] = 0; R.y[j] = 0; }
    pt_add(&F, &GQ, &G, &Q);
    for (int i = w * 32 - 1; i >= 0; i--) {
        pt_double(&F, &tmp, &R); R = tmp;
        int b1 = (u1[i / 32] >> (i % 32)) & 1, b2 = (u2[i / 32] >> (i % 32)) & 1;
        if (b1 && b2) { pt_add(&F, &tmp, &R, &GQ); R = tmp; }
        else if (b1) { pt_add(&F, &tmp, &R, &G); R = tmp; }
        else if (b2) { pt_add(&F, &tmp, &R, &Q); R = tmp; }
    }
    if (w_iszero(R.z, w)) return -3;
    /* x = X / Z^2 */
    uint32_t pm2[12], zi[12], zi2[12], xa[12];
    for (int j = 0; j < w; j++) one[j] = 0;
    one[0] = 2;
    w_sub(pm2, cv->p, one, w);
    mexp(&F, zi, R.z, pm2, w); mmul(&F, zi2, zi, zi); mmul(&F, xa, R.x, zi2); from_m(&F, xa, xa);
    if (w_ge(xa, cv->n, w)) w_sub(xa, xa, cv->n, w);
    for (int j = 0; j < w; j++) if (xa[j] != r[j]) return -4;
    return 0;
}

static int der_len(const uint8_t** p, const uint8_t* e, uint32_t* l) {
    if (*p >= e) return -1;
    uint32_t n = *(*p)++;
    if (n & 0x80) { int k = n & 0x7F; if (k < 1 || k > 2 || *p + k > e) return -1; n = 0; while (k--) n = (n << 8) | *(*p)++; }
    *l = n; return 0;
}
int ecdsa_verify(int curve, const uint8_t* q, uint32_t ql, const uint8_t* hash, uint32_t hl, const uint8_t* sig, uint32_t sl) {
    static const struct curve c256 = { 8, P256_P, P256_B, P256_N, P256_GX, P256_GY };
    static const struct curve c384 = { 12, P384_P, P384_B, P384_N, P384_GX, P384_GY };
    const struct curve* cv = curve == 256 ? &c256 : curve == 384 ? &c384 : 0;
    if (!cv) return -1;
    const uint8_t* p = sig; const uint8_t* e = sig + sl; uint32_t l;
    if (p >= e || *p++ != 0x30 || der_len(&p, e, &l) || p + l != e) return -5;
    const uint8_t *r, *s; uint32_t rl, sl2;
    if (p >= e || *p++ != 0x02 || der_len(&p, e, &rl) || p + rl > e) return -5;
    r = p; p += rl;
    if (p >= e || *p++ != 0x02 || der_len(&p, e, &sl2) || p + sl2 != e) return -5;
    s = p;
    while (rl && !r[0]) { r++; rl--; }
    while (sl2 && !s[0]) { s++; sl2--; }
    return ec_verify(cv, q, ql, hash, hl, r, rl, s, sl2);
}

/* ---------- ECDH auf P-256 (ephemerer Schluesselaustausch) ---------- */
static void w2be(uint8_t* o, const uint32_t* a, int w) {
    for (int i = 0; i < w * 4; i++) o[w * 4 - 1 - i] = (uint8_t)(a[i / 4] >> (8 * (i % 4)));
}
/* Punkt (bx,by) normal, Skalar k (w Worte) -> affines Ergebnis normal; -1 bei unendlich */
static int ec_mul(const struct curve* cv, const uint32_t* k, const uint32_t* bx, const uint32_t* by, uint32_t* ox, uint32_t* oy) {
    int w = cv->w; static struct mctx F; mctx_init(&F, cv->p, w);
    struct pt B, R, tmp; uint32_t one[12], onem[12];
    for (int j = 0; j < w; j++) { one[j] = 0; }
    one[0] = 1; to_m(&F, onem, one);
    to_m(&F, B.x, bx); to_m(&F, B.y, by);
    for (int j = 0; j < w; j++) { B.z[j] = onem[j]; R.x[j] = R.y[j] = R.z[j] = 0; }
    for (int i = w * 32 - 1; i >= 0; i--) {
        pt_double(&F, &tmp, &R); R = tmp;
        if ((k[i / 32] >> (i % 32)) & 1) { pt_add(&F, &tmp, &R, &B); R = tmp; }
    }
    if (w_iszero(R.z, w)) return -1;
    uint32_t pm2[12], zi[12], zi2[12], zi3[12], xa[12], ya[12];
    for (int j = 0; j < w; j++) { one[j] = 0; }
    one[0] = 2;
    w_sub(pm2, cv->p, one, w);
    mexp(&F, zi, R.z, pm2, w); mmul(&F, zi2, zi, zi); mmul(&F, zi3, zi2, zi);
    mmul(&F, xa, R.x, zi2); mmul(&F, ya, R.y, zi3);
    from_m(&F, ox, xa); from_m(&F, oy, ya);
    return 0;
}
static const struct curve C256 = { 8, P256_P, P256_B, P256_N, P256_GX, P256_GY };

int p256_pub(const uint8_t priv[32], uint8_t out[65]) {
    uint32_t d[8], x[8], y[8];
    be2w(d, 8, priv, 32);
    if (w_iszero(d, 8) || w_ge(d, P256_N, 8)) return -1;
    if (ec_mul(&C256, d, P256_GX, P256_GY, x, y)) return -1;
    out[0] = 4; w2be(out + 1, x, 8); w2be(out + 33, y, 8);
    return 0;
}
int p256_shared(const uint8_t priv[32], const uint8_t peer[65], uint8_t out[32]) {
    uint32_t d[8], qx[8], qy[8], x[8], y[8];
    be2w(d, 8, priv, 32);
    if (w_iszero(d, 8) || w_ge(d, P256_N, 8) || peer[0] != 4) return -1;
    be2w(qx, 8, peer + 1, 32); be2w(qy, 8, peer + 33, 32);
    if (w_ge(qx, P256_P, 8) || w_ge(qy, P256_P, 8)) return -1;
    static struct mctx F; mctx_init(&F, P256_P, 8);
    uint32_t xm[8], ym[8], bm[8], t1[8], t2[8], x3[8];                    /* y^2 = x^3 - 3x + b */
    to_m(&F, xm, qx); to_m(&F, ym, qy); to_m(&F, bm, P256_B);
    mmul(&F, t1, ym, ym); mmul(&F, t2, xm, xm); mmul(&F, t2, t2, xm);
    fadd(&F, x3, xm, xm); fadd(&F, x3, x3, xm); fsub(&F, t2, t2, x3); fadd(&F, t2, t2, bm);
    for (int j = 0; j < 8; j++) if (t1[j] != t2[j]) return -1;
    if (ec_mul(&C256, d, qx, qy, x, y)) return -1;
    w2be(out, x, 8);
    return 0;
}
