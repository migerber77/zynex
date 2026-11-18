/* Zynex OS - Kryptobibliothek (SHA-256, HMAC, HKDF, ChaCha20-Poly1305, X25519, RSA-PSS-Pruefung) */
#include "crypto.h"

static void mc(void* d, const void* s, uint32_t n) { uint8_t* a = d; const uint8_t* b = s; for (uint32_t i = 0; i < n; i++) a[i] = b[i]; }
static void mz(void* d, uint32_t n) { uint8_t* a = d; for (uint32_t i = 0; i < n; i++) a[i] = 0; }
static uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
static uint32_t rotl(uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }
static uint32_t ld32le(const uint8_t* p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static void st32le(uint8_t* p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }

/* ---------- SHA-256 ---------- */
static const uint32_t K[64] = {
 0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
 0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
 0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
 0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2 };

static void sha_block(struct sha256* c, const uint8_t* p) {
    uint32_t w[64], a, b, cc, d, e, f, g, h;
    for (int i = 0; i < 16; i++) w[i] = (uint32_t)p[4*i] << 24 | (uint32_t)p[4*i+1] << 16 | (uint32_t)p[4*i+2] << 8 | p[4*i+3];
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = rotr(w[i-15], 7) ^ rotr(w[i-15], 18) ^ (w[i-15] >> 3);
        uint32_t s1 = rotr(w[i-2], 17) ^ rotr(w[i-2], 19) ^ (w[i-2] >> 10);
        w[i] = w[i-16] + s0 + w[i-7] + s1;
    }
    a = c->h[0]; b = c->h[1]; cc = c->h[2]; d = c->h[3]; e = c->h[4]; f = c->h[5]; g = c->h[6]; h = c->h[7];
    for (int i = 0; i < 64; i++) {
        uint32_t t1 = h + (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) + ((e & f) ^ (~e & g)) + K[i] + w[i];
        uint32_t t2 = (rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) + ((a & b) ^ (a & cc) ^ (b & cc));
        h = g; g = f; f = e; e = d + t1; d = cc; cc = b; b = a; a = t1 + t2;
    }
    c->h[0] += a; c->h[1] += b; c->h[2] += cc; c->h[3] += d; c->h[4] += e; c->h[5] += f; c->h[6] += g; c->h[7] += h;
}
void sha256_init(struct sha256* c) {
    static const uint32_t i0[8] = {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    for (int i = 0; i < 8; i++) c->h[i] = i0[i];
    c->len = 0; c->n = 0;
}
void sha256_update(struct sha256* c, const void* d, uint32_t n) {
    const uint8_t* p = d; c->len += n;
    while (n) {
        uint32_t k = 64 - c->n; if (k > n) k = n;
        mc(c->buf + c->n, p, k); c->n += k; p += k; n -= k;
        if (c->n == 64) { sha_block(c, c->buf); c->n = 0; }
    }
}
void sha256_final(struct sha256* c, uint8_t out[32]) {
    uint64_t bits = c->len * 8;
    uint8_t pad = 0x80; sha256_update(c, &pad, 1);
    pad = 0; while (c->n != 56) sha256_update(c, &pad, 1);
    uint8_t l[8]; for (int i = 0; i < 8; i++) l[i] = (uint8_t)(bits >> (56 - 8 * i));
    sha256_update(c, l, 8);
    for (int i = 0; i < 8; i++) { out[4*i] = (uint8_t)(c->h[i] >> 24); out[4*i+1] = (uint8_t)(c->h[i] >> 16); out[4*i+2] = (uint8_t)(c->h[i] >> 8); out[4*i+3] = (uint8_t)c->h[i]; }
}
void sha256(const void* d, uint32_t n, uint8_t out[32]) { struct sha256 c; sha256_init(&c); sha256_update(&c, d, n); sha256_final(&c, out); }

/* ---------- HMAC / HKDF ---------- */
void hmac_sha256(const uint8_t* key, uint32_t kl, const uint8_t* d, uint32_t n, uint8_t out[32]) {
    uint8_t k[64], ip[64], op[64], in[32]; struct sha256 c;
    mz(k, 64);
    if (kl > 64) sha256(key, kl, k); else mc(k, key, kl);
    for (int i = 0; i < 64; i++) { ip[i] = k[i] ^ 0x36; op[i] = k[i] ^ 0x5c; }
    sha256_init(&c); sha256_update(&c, ip, 64); sha256_update(&c, d, n); sha256_final(&c, in);
    sha256_init(&c); sha256_update(&c, op, 64); sha256_update(&c, in, 32); sha256_final(&c, out);
}
void hkdf_extract(const uint8_t* salt, uint32_t sl, const uint8_t* ikm, uint32_t il, uint8_t out[32]) {
    static const uint8_t z[32] = {0};
    if (!salt) { salt = z; sl = 32; }
    hmac_sha256(salt, sl, ikm, il, out);
}
void hkdf_expand_label(const uint8_t secret[32], const char* label, const uint8_t* ctx, uint32_t cl, uint8_t* out, uint32_t ol) {
    uint8_t info[128]; uint32_t n = 0, ll = 0;
    while (label[ll]) ll++;
    info[n++] = (uint8_t)(ol >> 8); info[n++] = (uint8_t)ol;
    info[n++] = (uint8_t)(6 + ll);
    mc(info + n, "tls13 ", 6); n += 6; mc(info + n, label, ll); n += ll;
    info[n++] = (uint8_t)cl; mc(info + n, ctx, cl); n += cl;
    info[n++] = 1;
    uint8_t t[32]; hmac_sha256(secret, 32, info, n, t);
    mc(out, t, ol);                                              /* ol <= 32 */
}

/* ---------- ChaCha20-Poly1305 (RFC 8439) ---------- */
#define QR(a,b,c,d) a += b; d ^= a; d = rotl(d,16); c += d; b ^= c; b = rotl(b,12); a += b; d ^= a; d = rotl(d,8); c += d; b ^= c; b = rotl(b,7);
static void chacha_block(const uint8_t key[32], uint32_t ctr, const uint8_t nonce[12], uint8_t out[64]) {
    uint32_t s[16], x[16];
    s[0] = 0x61707865; s[1] = 0x3320646e; s[2] = 0x79622d32; s[3] = 0x6b206574;
    for (int i = 0; i < 8; i++) s[4 + i] = ld32le(key + 4 * i);
    s[12] = ctr; for (int i = 0; i < 3; i++) s[13 + i] = ld32le(nonce + 4 * i);
    for (int i = 0; i < 16; i++) x[i] = s[i];
    for (int r = 0; r < 10; r++) {
        QR(x[0],x[4],x[8],x[12]) QR(x[1],x[5],x[9],x[13]) QR(x[2],x[6],x[10],x[14]) QR(x[3],x[7],x[11],x[15])
        QR(x[0],x[5],x[10],x[15]) QR(x[1],x[6],x[11],x[12]) QR(x[2],x[7],x[8],x[13]) QR(x[3],x[4],x[9],x[14])
    }
    for (int i = 0; i < 16; i++) st32le(out + 4 * i, x[i] + s[i]);
}
static void chacha_xor(const uint8_t key[32], uint32_t ctr, const uint8_t nonce[12], const uint8_t* in, uint32_t n, uint8_t* out) {
    uint8_t b[64];
    for (uint32_t off = 0; off < n; off += 64, ctr++) {
        chacha_block(key, ctr, nonce, b);
        uint32_t k = n - off < 64 ? n - off : 64;
        for (uint32_t i = 0; i < k; i++) out[off + i] = in[off + i] ^ b[i];
    }
}

struct poly { uint32_t r[5], h[5], pad[4]; };
static void poly_blocks(struct poly* p, const uint8_t* m, uint32_t bytes, uint32_t hibit) {
    uint32_t r0 = p->r[0], r1 = p->r[1], r2 = p->r[2], r3 = p->r[3], r4 = p->r[4];
    uint32_t s1 = r1 * 5, s2 = r2 * 5, s3 = r3 * 5, s4 = r4 * 5;
    uint32_t h0 = p->h[0], h1 = p->h[1], h2 = p->h[2], h3 = p->h[3], h4 = p->h[4];
    while (bytes >= 16) {
        h0 += ld32le(m) & 0x3ffffff; h1 += (ld32le(m + 3) >> 2) & 0x3ffffff; h2 += (ld32le(m + 6) >> 4) & 0x3ffffff;
        h3 += (ld32le(m + 9) >> 6) & 0x3ffffff; h4 += (ld32le(m + 12) >> 8) | hibit;
        uint64_t d0 = (uint64_t)h0*r0 + (uint64_t)h1*s4 + (uint64_t)h2*s3 + (uint64_t)h3*s2 + (uint64_t)h4*s1;
        uint64_t d1 = (uint64_t)h0*r1 + (uint64_t)h1*r0 + (uint64_t)h2*s4 + (uint64_t)h3*s3 + (uint64_t)h4*s2;
        uint64_t d2 = (uint64_t)h0*r2 + (uint64_t)h1*r1 + (uint64_t)h2*r0 + (uint64_t)h3*s4 + (uint64_t)h4*s3;
        uint64_t d3 = (uint64_t)h0*r3 + (uint64_t)h1*r2 + (uint64_t)h2*r1 + (uint64_t)h3*r0 + (uint64_t)h4*s4;
        uint64_t d4 = (uint64_t)h0*r4 + (uint64_t)h1*r3 + (uint64_t)h2*r2 + (uint64_t)h3*r1 + (uint64_t)h4*r0;
        uint32_t c = (uint32_t)(d0 >> 26); h0 = (uint32_t)d0 & 0x3ffffff;
        d1 += c; c = (uint32_t)(d1 >> 26); h1 = (uint32_t)d1 & 0x3ffffff;
        d2 += c; c = (uint32_t)(d2 >> 26); h2 = (uint32_t)d2 & 0x3ffffff;
        d3 += c; c = (uint32_t)(d3 >> 26); h3 = (uint32_t)d3 & 0x3ffffff;
        d4 += c; c = (uint32_t)(d4 >> 26); h4 = (uint32_t)d4 & 0x3ffffff;
        h0 += c * 5; c = h0 >> 26; h0 &= 0x3ffffff; h1 += c;
        m += 16; bytes -= 16;
    }
    p->h[0] = h0; p->h[1] = h1; p->h[2] = h2; p->h[3] = h3; p->h[4] = h4;
}
static void poly_init(struct poly* p, const uint8_t k[32]) {
    p->r[0] = ld32le(k) & 0x3ffffff; p->r[1] = (ld32le(k + 3) >> 2) & 0x3ffff03; p->r[2] = (ld32le(k + 6) >> 4) & 0x3ffc0ff;
    p->r[3] = (ld32le(k + 9) >> 6) & 0x3f03fff; p->r[4] = (ld32le(k + 12) >> 8) & 0x00fffff;
    for (int i = 0; i < 5; i++) p->h[i] = 0;
    for (int i = 0; i < 4; i++) p->pad[i] = ld32le(k + 16 + 4 * i);
}
static void poly_update(struct poly* p, const uint8_t* m, uint32_t n) {       /* n: volle Blocks, Rest mit Nullen aufgefuellt */
    uint32_t full = n & ~15u;
    if (full) poly_blocks(p, m, full, 1u << 24);
    if (n - full) { uint8_t b[16]; mz(b, 16); mc(b, m + full, n - full); poly_blocks(p, b, 16, 1u << 24); }
}
static void poly_finish(struct poly* p, uint8_t mac[16]) {
    uint32_t h0 = p->h[0], h1 = p->h[1], h2 = p->h[2], h3 = p->h[3], h4 = p->h[4], c;
    c = h1 >> 26; h1 &= 0x3ffffff; h2 += c; c = h2 >> 26; h2 &= 0x3ffffff; h3 += c; c = h3 >> 26; h3 &= 0x3ffffff;
    h4 += c; c = h4 >> 26; h4 &= 0x3ffffff; h0 += c * 5; c = h0 >> 26; h0 &= 0x3ffffff; h1 += c;
    uint32_t g0 = h0 + 5; c = g0 >> 26; g0 &= 0x3ffffff;
    uint32_t g1 = h1 + c; c = g1 >> 26; g1 &= 0x3ffffff;
    uint32_t g2 = h2 + c; c = g2 >> 26; g2 &= 0x3ffffff;
    uint32_t g3 = h3 + c; c = g3 >> 26; g3 &= 0x3ffffff;
    uint32_t g4 = h4 + c - (1u << 26);
    uint32_t mask = (g4 >> 31) - 1;
    g0 &= mask; g1 &= mask; g2 &= mask; g3 &= mask; g4 &= mask; mask = ~mask;
    h0 = (h0 & mask) | g0; h1 = (h1 & mask) | g1; h2 = (h2 & mask) | g2; h3 = (h3 & mask) | g3; h4 = (h4 & mask) | g4;
    h0 = (h0 | (h1 << 26)); h1 = ((h1 >> 6) | (h2 << 20)); h2 = ((h2 >> 12) | (h3 << 14)); h3 = ((h3 >> 18) | (h4 << 8));
    uint64_t f = (uint64_t)h0 + p->pad[0]; h0 = (uint32_t)f;
    f = (uint64_t)h1 + p->pad[1] + (f >> 32); h1 = (uint32_t)f;
    f = (uint64_t)h2 + p->pad[2] + (f >> 32); h2 = (uint32_t)f;
    f = (uint64_t)h3 + p->pad[3] + (f >> 32); h3 = (uint32_t)f;
    st32le(mac, h0); st32le(mac + 4, h1); st32le(mac + 8, h2); st32le(mac + 12, h3);
}
static void aead_tag(const uint8_t key[32], const uint8_t nonce[12], const uint8_t* aad, uint32_t al, const uint8_t* ct, uint32_t cl, uint8_t tag[16]) {
    uint8_t blk[64], lens[16]; struct poly p;
    chacha_block(key, 0, nonce, blk);
    poly_init(&p, blk);
    poly_update(&p, aad, al); poly_update(&p, ct, cl);
    for (int i = 0; i < 8; i++) { lens[i] = (uint8_t)((uint64_t)al >> (8 * i)); lens[8 + i] = (uint8_t)((uint64_t)cl >> (8 * i)); }
    poly_blocks(&p, lens, 16, 1u << 24);
    poly_finish(&p, tag);
}
void aead_seal(const uint8_t key[32], const uint8_t nonce[12], const uint8_t* aad, uint32_t al, const uint8_t* pt, uint32_t pl, uint8_t* ct) {
    chacha_xor(key, 1, nonce, pt, pl, ct);
    aead_tag(key, nonce, aad, al, ct, pl, ct + pl);
}
int aead_open(const uint8_t key[32], const uint8_t nonce[12], const uint8_t* aad, uint32_t al, const uint8_t* ct, uint32_t cl, uint8_t* pt) {
    if (cl < 16) return -1;
    uint8_t tag[16]; uint32_t pl = cl - 16;
    aead_tag(key, nonce, aad, al, ct, pl, tag);
    uint8_t d = 0; for (int i = 0; i < 16; i++) d |= tag[i] ^ ct[pl + i];
    if (d) return -1;
    chacha_xor(key, 1, nonce, ct, pl, pt);
    return 0;
}

/* ---------- X25519 (nach TweetNaCl) ---------- */
typedef int64_t gf[16];
static const gf gf121665 = {0xDB41, 1};
static void car25519(gf o) {
    for (int i = 0; i < 16; i++) {
        o[i] += (1LL << 16); int64_t c = o[i] >> 16;
        o[(i + 1) * (i < 15)] += c - 1 + 37 * (c - 1) * (i == 15);
        o[i] -= c << 16;
    }
}
static void sel25519(gf p, gf q, int b) {
    int64_t c = ~((int64_t)b - 1);
    for (int i = 0; i < 16; i++) { int64_t t = c & (p[i] ^ q[i]); p[i] ^= t; q[i] ^= t; }
}
static void pack25519(uint8_t* o, const gf n) {
    gf m, t; for (int i = 0; i < 16; i++) t[i] = n[i];
    car25519(t); car25519(t); car25519(t);
    for (int j = 0; j < 2; j++) {
        m[0] = t[0] - 0xffed;
        for (int i = 1; i < 15; i++) { m[i] = t[i] - 0xffff - ((m[i - 1] >> 16) & 1); m[i - 1] &= 0xffff; }
        m[15] = t[15] - 0x7fff - ((m[14] >> 16) & 1);
        int b = (int)((m[15] >> 16) & 1); m[14] &= 0xffff;
        sel25519(t, m, 1 - b);
    }
    for (int i = 0; i < 16; i++) { o[2 * i] = (uint8_t)(t[i] & 0xff); o[2 * i + 1] = (uint8_t)(t[i] >> 8); }
}
static void unpack25519(gf o, const uint8_t* n) { for (int i = 0; i < 16; i++) o[i] = n[2 * i] + ((int64_t)n[2 * i + 1] << 8); o[15] &= 0x7fff; }
static void fA(gf o, const gf a, const gf b) { for (int i = 0; i < 16; i++) o[i] = a[i] + b[i]; }
static void fZ(gf o, const gf a, const gf b) { for (int i = 0; i < 16; i++) o[i] = a[i] - b[i]; }
static void fM(gf o, const gf a, const gf b) {
    int64_t t[31]; for (int i = 0; i < 31; i++) t[i] = 0;
    for (int i = 0; i < 16; i++) for (int j = 0; j < 16; j++) t[i + j] += a[i] * b[j];
    for (int i = 0; i < 15; i++) t[i] += 38 * t[i + 16];
    for (int i = 0; i < 16; i++) o[i] = t[i];
    car25519(o); car25519(o);
}
static void fS(gf o, const gf a) { fM(o, a, a); }
static void inv25519(gf o, const gf i) {
    gf c; for (int a = 0; a < 16; a++) c[a] = i[a];
    for (int a = 253; a >= 0; a--) { fS(c, c); if (a != 2 && a != 4) fM(c, c, i); }
    for (int a = 0; a < 16; a++) o[a] = c[a];
}
static void scalarmult(uint8_t* q, const uint8_t* n, const uint8_t* p) {
    uint8_t z[32]; int64_t x[80]; gf a, b, c, d, e, f;
    for (int i = 0; i < 31; i++) z[i] = n[i];
    z[31] = (n[31] & 127) | 64; z[0] &= 248;
    unpack25519(x, p);
    for (int i = 0; i < 16; i++) { b[i] = x[i]; d[i] = a[i] = c[i] = 0; }
    a[0] = d[0] = 1;
    for (int i = 254; i >= 0; --i) {
        int r = (z[i >> 3] >> (i & 7)) & 1;
        sel25519(a, b, r); sel25519(c, d, r);
        fA(e, a, c); fZ(a, a, c); fA(c, b, d); fZ(b, b, d); fS(d, e); fS(f, a); fM(a, c, a); fM(c, b, e);
        fA(e, a, c); fZ(a, a, c); fS(b, a); fZ(c, d, f); fM(a, c, gf121665); fA(a, a, d); fM(c, c, a); fM(a, d, f); fM(d, b, x); fS(b, e);
        sel25519(a, b, r); sel25519(c, d, r);
    }
    for (int i = 0; i < 16; i++) { x[i + 16] = a[i]; x[i + 32] = c[i]; x[i + 48] = b[i]; x[i + 64] = d[i]; }
    inv25519(x + 32, x + 32); fM(x + 16, x + 16, x + 32);
    pack25519(q, x + 16);
}
void x25519_pub(uint8_t out[32], const uint8_t priv[32]) { uint8_t base[32]; mz(base, 32); base[0] = 9; scalarmult(out, priv, base); }
int x25519_shared(uint8_t out[32], const uint8_t priv[32], const uint8_t peer[32]) {
    scalarmult(out, priv, peer);
    uint8_t d = 0; for (int i = 0; i < 32; i++) d |= out[i];
    return d ? 0 : -1;
}

/* ---------- RSA-PSS-Pruefung (Montgomery, 32-Bit-Worte) ---------- */
#define RW 130
static uint32_t big_n[RW], big_rr[RW], big_a[RW], big_x[RW], big_t[RW + 2];
static int big_w;
static uint32_t big_n0;

static void mont_mul(uint32_t* r, const uint32_t* a, const uint32_t* b) {
    int w = big_w; uint32_t* t = big_t;
    for (int i = 0; i < w + 2; i++) t[i] = 0;
    for (int i = 0; i < w; i++) {
        uint64_t C = 0, s;
        for (int j = 0; j < w; j++) { s = (uint64_t)t[j] + (uint64_t)a[j] * b[i] + C; t[j] = (uint32_t)s; C = s >> 32; }
        s = (uint64_t)t[w] + C; t[w] = (uint32_t)s; t[w + 1] = (uint32_t)(s >> 32);
        uint32_t m = t[0] * big_n0;
        s = (uint64_t)t[0] + (uint64_t)m * big_n[0]; C = s >> 32;
        for (int j = 1; j < w; j++) { s = (uint64_t)t[j] + (uint64_t)m * big_n[j] + C; t[j - 1] = (uint32_t)s; C = s >> 32; }
        s = (uint64_t)t[w] + C; t[w - 1] = (uint32_t)s; C = s >> 32;
        t[w] = t[w + 1] + (uint32_t)C;
    }
    int ge = t[w] != 0;
    if (!ge) { ge = 1; for (int j = w - 1; j >= 0; j--) { if (t[j] != big_n[j]) { ge = t[j] > big_n[j]; break; } } }
    if (ge) { int64_t br = 0; for (int j = 0; j < w; j++) { int64_t d = (int64_t)t[j] - big_n[j] - br; t[j] = (uint32_t)d; br = d < 0; } }
    for (int j = 0; j < w; j++) r[j] = t[j];
}
static int big_ge(const uint32_t* a, const uint32_t* b) { for (int j = big_w - 1; j >= 0; j--) if (a[j] != b[j]) return a[j] > b[j]; return 1; }

static void mgf1(const uint8_t* seed, uint32_t sl, uint8_t* out, uint32_t ol) {
    uint8_t buf[64], h[32];
    for (uint32_t c = 0, off = 0; off < ol; c++, off += 32) {
        mc(buf, seed, sl); buf[sl] = (uint8_t)(c >> 24); buf[sl + 1] = (uint8_t)(c >> 16); buf[sl + 2] = (uint8_t)(c >> 8); buf[sl + 3] = (uint8_t)c;
        sha256(buf, sl + 4, h);
        for (uint32_t i = 0; i < 32 && off + i < ol; i++) out[off + i] = h[i];
    }
}

int rsa_pss_verify(const uint8_t* n, uint32_t nl, const uint8_t* e, uint32_t el, const uint8_t* sig, uint32_t sl, const uint8_t mhash[32]) {
    while (nl && !n[0]) { n++; nl--; }
    if (nl < 128 || nl > 512 || sl != nl || el == 0 || el > 4) return -1;
    if (!(n[nl - 1] & 1)) return -1;
    uint32_t ev = 0; for (uint32_t i = 0; i < el; i++) ev = (ev << 8) | e[i];
    if (ev < 3 || !(ev & 1)) return -1;
    int w = (int)((nl + 3) / 4); big_w = w;
    for (int i = 0; i < RW; i++) big_n[i] = big_a[i] = big_x[i] = big_rr[i] = 0;
    for (uint32_t i = 0; i < nl; i++) { uint32_t pos = nl - 1 - i; big_n[pos / 4] |= (uint32_t)n[i] << (8 * (pos % 4)); }
    for (uint32_t i = 0; i < sl; i++) { uint32_t pos = sl - 1 - i; big_a[pos / 4] |= (uint32_t)sig[i] << (8 * (pos % 4)); }
    if (big_ge(big_a, big_n)) return -1;
    /* n0 = -n^-1 mod 2^32 */
    uint32_t inv = big_n[0]; for (int i = 0; i < 5; i++) inv *= 2 - big_n[0] * inv;
    big_n0 = (uint32_t)(0u - inv);
    /* rr = R^2 mod n = 2^(64w) mod n durch Verdoppeln */
    big_rr[0] = 1;
    for (int i = 0; i < 64 * w; i++) {
        uint32_t carry = 0;
        for (int j = 0; j < w; j++) { uint32_t nc = big_rr[j] >> 31; big_rr[j] = (big_rr[j] << 1) | carry; carry = nc; }
        if (carry || big_ge(big_rr, big_n)) { int64_t br = 0; for (int j = 0; j < w; j++) { int64_t d = (int64_t)big_rr[j] - big_n[j] - br; big_rr[j] = (uint32_t)d; br = d < 0; } }
    }
    uint32_t am[RW], one[RW], res[RW];
    mont_mul(am, big_a, big_rr);                                   /* a in Montgomery-Form */
    for (int j = 0; j < w; j++) res[j] = am[j];
    int top = 31; while (!((ev >> top) & 1)) top--;
    for (int b = top - 1; b >= 0; b--) { mont_mul(res, res, res); if ((ev >> b) & 1) mont_mul(res, res, am); }
    for (int j = 0; j < w; j++) one[j] = 0;
    one[0] = 1;
    mont_mul(res, res, one);                                       /* zurueck aus der Montgomery-Form */
    static uint8_t em[512];
    for (uint32_t i = 0; i < nl; i++) { uint32_t pos = nl - 1 - i; em[i] = (uint8_t)(res[pos / 4] >> (8 * (pos % 4))); }
    /* EMSA-PSS-VERIFY (SHA-256, Salzlaenge 32) */
    uint32_t modbits = nl * 8; { uint8_t t = n[0]; int z = 0; while (!(t & 0x80)) { t <<= 1; z++; } modbits -= (uint32_t)z; }
    uint32_t embits = modbits - 1, emlen = (embits + 7) / 8;
    const uint8_t* emp = em + (nl - emlen);
    if (emlen < 32 + 32 + 2 || emp[emlen - 1] != 0xbc) return -2;
    uint32_t dbl = emlen - 32 - 1;
    const uint8_t* H = emp + dbl;
    static uint8_t db[512]; mgf1(H, 32, db, dbl);
    for (uint32_t i = 0; i < dbl; i++) db[i] ^= emp[i];
    db[0] &= (uint8_t)(0xFF >> (8 * emlen - embits));
    uint32_t i = 0; while (i < dbl - 32 - 1 && db[i] == 0) i++;
    if (i != dbl - 32 - 1 || db[i] != 1) return -3;
    uint8_t m2[72], h2[32]; mz(m2, 8); mc(m2 + 8, mhash, 32); mc(m2 + 40, db + dbl - 32, 32);
    sha256(m2, 72, h2);
    uint8_t d = 0; for (int k = 0; k < 32; k++) d |= h2[k] ^ H[k];
    return d ? -4 : 0;
}

/* ---------- SHA-512 / SHA-384 ---------- */
static const uint64_t K5[80] = {
0x428a2f98d728ae22ULL,0x7137449123ef65cdULL,0xb5c0fbcfec4d3b2fULL,0xe9b5dba58189dbbcULL,0x3956c25bf348b538ULL,0x59f111f1b605d019ULL,0x923f82a4af194f9bULL,0xab1c5ed5da6d8118ULL,
0xd807aa98a3030242ULL,0x12835b0145706fbeULL,0x243185be4ee4b28cULL,0x550c7dc3d5ffb4e2ULL,0x72be5d74f27b896fULL,0x80deb1fe3b1696b1ULL,0x9bdc06a725c71235ULL,0xc19bf174cf692694ULL,
0xe49b69c19ef14ad2ULL,0xefbe4786384f25e3ULL,0x0fc19dc68b8cd5b5ULL,0x240ca1cc77ac9c65ULL,0x2de92c6f592b0275ULL,0x4a7484aa6ea6e483ULL,0x5cb0a9dcbd41fbd4ULL,0x76f988da831153b5ULL,
0x983e5152ee66dfabULL,0xa831c66d2db43210ULL,0xb00327c898fb213fULL,0xbf597fc7beef0ee4ULL,0xc6e00bf33da88fc2ULL,0xd5a79147930aa725ULL,0x06ca6351e003826fULL,0x142929670a0e6e70ULL,
0x27b70a8546d22ffcULL,0x2e1b21385c26c926ULL,0x4d2c6dfc5ac42aedULL,0x53380d139d95b3dfULL,0x650a73548baf63deULL,0x766a0abb3c77b2a8ULL,0x81c2c92e47edaee6ULL,0x92722c851482353bULL,
0xa2bfe8a14cf10364ULL,0xa81a664bbc423001ULL,0xc24b8b70d0f89791ULL,0xc76c51a30654be30ULL,0xd192e819d6ef5218ULL,0xd69906245565a910ULL,0xf40e35855771202aULL,0x106aa07032bbd1b8ULL,
0x19a4c116b8d2d0c8ULL,0x1e376c085141ab53ULL,0x2748774cdf8eeb99ULL,0x34b0bcb5e19b48a8ULL,0x391c0cb3c5c95a63ULL,0x4ed8aa4ae3418acbULL,0x5b9cca4f7763e373ULL,0x682e6ff3d6b2b8a3ULL,
0x748f82ee5defb2fcULL,0x78a5636f43172f60ULL,0x84c87814a1f0ab72ULL,0x8cc702081a6439ecULL,0x90befffa23631e28ULL,0xa4506cebde82bde9ULL,0xbef9a3f7b2c67915ULL,0xc67178f2e372532bULL,
0xca273eceea26619cULL,0xd186b8c721c0c207ULL,0xeada7dd6cde0eb1eULL,0xf57d4f7fee6ed178ULL,0x06f067aa72176fbaULL,0x0a637dc5a2c898a6ULL,0x113f9804bef90daeULL,0x1b710b35131c471bULL,
0x28db77f523047d84ULL,0x32caab7b40c72493ULL,0x3c9ebe0a15c9bebcULL,0x431d67c49c100d4cULL,0x4cc5d4becb3e42b6ULL,0x597f299cfc657e2aULL,0x5fcb6fab3ad6faecULL,0x6c44198c4a475817ULL };
static uint64_t rotr64(uint64_t x, int n) { return (x >> n) | (x << (64 - n)); }
static void sha512_block(struct sha512* c, const uint8_t* p) {
    uint64_t w[80], a, b, cc, d, e, f, g, h;
    for (int i = 0; i < 16; i++) { w[i] = 0; for (int k = 0; k < 8; k++) w[i] = (w[i] << 8) | p[8 * i + k]; }
    for (int i = 16; i < 80; i++) {
        uint64_t s0 = rotr64(w[i-15], 1) ^ rotr64(w[i-15], 8) ^ (w[i-15] >> 7);
        uint64_t s1 = rotr64(w[i-2], 19) ^ rotr64(w[i-2], 61) ^ (w[i-2] >> 6);
        w[i] = w[i-16] + s0 + w[i-7] + s1;
    }
    a = c->h[0]; b = c->h[1]; cc = c->h[2]; d = c->h[3]; e = c->h[4]; f = c->h[5]; g = c->h[6]; h = c->h[7];
    for (int i = 0; i < 80; i++) {
        uint64_t t1 = h + (rotr64(e, 14) ^ rotr64(e, 18) ^ rotr64(e, 41)) + ((e & f) ^ (~e & g)) + K5[i] + w[i];
        uint64_t t2 = (rotr64(a, 28) ^ rotr64(a, 34) ^ rotr64(a, 39)) + ((a & b) ^ (a & cc) ^ (b & cc));
        h = g; g = f; f = e; e = d + t1; d = cc; cc = b; b = a; a = t1 + t2;
    }
    c->h[0] += a; c->h[1] += b; c->h[2] += cc; c->h[3] += d; c->h[4] += e; c->h[5] += f; c->h[6] += g; c->h[7] += h;
}
static void sha512_run(const void* d, uint32_t n, const uint64_t* iv, uint8_t* out, int outlen) {
    struct sha512 c; for (int i = 0; i < 8; i++) c.h[i] = iv[i];
    const uint8_t* p = d; uint32_t rest = n;
    while (rest >= 128) { sha512_block(&c, p); p += 128; rest -= 128; }
    uint8_t buf[256]; mz(buf, 256); mc(buf, p, rest); buf[rest] = 0x80;
    uint32_t tot = rest < 112 ? 128 : 256;
    uint64_t bits = (uint64_t)n * 8;
    for (int i = 0; i < 8; i++) buf[tot - 1 - i] = (uint8_t)(bits >> (8 * i));
    sha512_block(&c, buf); if (tot == 256) sha512_block(&c, buf + 128);
    for (int i = 0; i < outlen; i++) out[i] = (uint8_t)(c.h[i / 8] >> (56 - 8 * (i % 8)));
}
void sha512(const void* d, uint32_t n, uint8_t out[64]) {
    static const uint64_t iv[8] = {0x6a09e667f3bcc908ULL,0xbb67ae8584caa73bULL,0x3c6ef372fe94f82bULL,0xa54ff53a5f1d36f1ULL,0x510e527fade682d1ULL,0x9b05688c2b3e6c1fULL,0x1f83d9abfb41bd6bULL,0x5be0cd19137e2179ULL};
    sha512_run(d, n, iv, out, 64);
}
void sha384(const void* d, uint32_t n, uint8_t out[48]) {
    static const uint64_t iv[8] = {0xcbbb9d5dc1059ed8ULL,0x629a292a367cd507ULL,0x9159015a3070dd17ULL,0x152fecd8f70e5939ULL,0x67332667ffc00b31ULL,0x8eb44a8768581511ULL,0xdb0c2e0d64f98fa7ULL,0x47b5481dbefa4fa4ULL};
    sha512_run(d, n, iv, out, 48);
}

/* ---------- AES-128-GCM ---------- */
static uint8_t sbox[256]; static int sbox_ok = 0;
static uint8_t xt(uint8_t x) { return (uint8_t)((x << 1) ^ ((x >> 7) * 0x1b)); }
static void sbox_init(void) {
    uint8_t p = 1, q = 1;
    do {
        p = (uint8_t)(p ^ (p << 1) ^ ((p & 0x80) ? 0x1b : 0));            /* p *= 3 */
        q ^= (uint8_t)(q << 1); q ^= (uint8_t)(q << 2); q ^= (uint8_t)(q << 4); if (q & 0x80) q ^= 0x09;   /* q /= 3 */
        uint8_t x = (uint8_t)(q ^ ((q << 1) | (q >> 7)) ^ ((q << 2) | (q >> 6)) ^ ((q << 3) | (q >> 5)) ^ ((q << 4) | (q >> 4)));
        sbox[p] = (uint8_t)(x ^ 0x63);
    } while (p != 1);
    sbox[0] = 0x63; sbox_ok = 1;
}
static void aes_keys(const uint8_t key[16], uint8_t rk[176]) {
    mc(rk, key, 16); uint8_t rc = 1;
    for (int i = 16; i < 176; i += 4) {
        uint8_t t[4]; mc(t, rk + i - 4, 4);
        if (i % 16 == 0) { uint8_t a = t[0]; t[0] = (uint8_t)(sbox[t[1]] ^ rc); t[1] = sbox[t[2]]; t[2] = sbox[t[3]]; t[3] = sbox[a]; rc = xt(rc); }
        for (int k = 0; k < 4; k++) rk[i + k] = rk[i - 16 + k] ^ t[k];
    }
}
static void aes_enc(const uint8_t rk[176], const uint8_t in[16], uint8_t out[16]) {
    uint8_t s[16], t[16];
    for (int i = 0; i < 16; i++) s[i] = in[i] ^ rk[i];
    for (int r = 1; r <= 10; r++) {
        for (int i = 0; i < 16; i++) t[i] = sbox[s[i]];
        uint8_t u[16];                                                       /* ShiftRows */
        for (int c = 0; c < 4; c++) for (int rr = 0; rr < 4; rr++) u[4 * c + rr] = t[4 * ((c + rr) & 3) + rr];
        if (r < 10) {
            for (int c = 0; c < 4; c++) {
                uint8_t a0 = u[4*c], a1 = u[4*c+1], a2 = u[4*c+2], a3 = u[4*c+3], all = a0 ^ a1 ^ a2 ^ a3;
                s[4*c]   = a0 ^ all ^ xt(a0 ^ a1);
                s[4*c+1] = a1 ^ all ^ xt(a1 ^ a2);
                s[4*c+2] = a2 ^ all ^ xt(a2 ^ a3);
                s[4*c+3] = a3 ^ all ^ xt(a3 ^ a0);
            }
        } else mc(s, u, 16);
        for (int i = 0; i < 16; i++) s[i] ^= rk[16 * r + i];
    }
    mc(out, s, 16);
}
struct ghash { uint64_t hh, hl, yh, yl; };
static uint64_t be64(const uint8_t* p) { uint64_t v = 0; for (int i = 0; i < 8; i++) v = (v << 8) | p[i]; return v; }
static void gh_block(struct ghash* g, const uint8_t b[16]) {
    uint64_t xh = g->yh ^ be64(b), xl = g->yl ^ be64(b + 8), zh = 0, zl = 0, vh = g->hh, vl = g->hl;
    for (int i = 0; i < 128; i++) {
        uint64_t bit = i < 64 ? (xh >> (63 - i)) & 1 : (xl >> (127 - i)) & 1;
        if (bit) { zh ^= vh; zl ^= vl; }
        uint64_t lsb = vl & 1;
        vl = (vl >> 1) | (vh << 63); vh >>= 1;
        if (lsb) vh ^= 0xe100000000000000ULL;
    }
    g->yh = zh; g->yl = zl;
}
static void gh_data(struct ghash* g, const uint8_t* d, uint32_t n) {
    uint8_t b[16];
    for (; n >= 16; d += 16, n -= 16) gh_block(g, d);
    if (n) { mz(b, 16); mc(b, d, n); gh_block(g, b); }
}
static void gcm_core(const uint8_t key[16], const uint8_t nonce[12], const uint8_t* aad, uint32_t al, const uint8_t* in, uint32_t n, uint8_t* out, int enc, uint8_t tag[16]) {
    if (!sbox_ok) sbox_init();
    uint8_t rk[176], h[16], z[16] = {0}, j0[16], ctr[16], ks[16];
    aes_keys(key, rk); aes_enc(rk, z, h);
    struct ghash g; g.hh = be64(h); g.hl = be64(h + 8); g.yh = g.yl = 0;
    mc(j0, nonce, 12); j0[12] = j0[13] = j0[14] = 0; j0[15] = 1;
    gh_data(&g, aad, al);
    if (!enc) gh_data(&g, in, n);
    mc(ctr, j0, 16);
    for (uint32_t off = 0; off < n; off += 16) {
        for (int i = 15; i >= 12; i--) if (++ctr[i]) break;
        aes_enc(rk, ctr, ks);
        uint32_t k = n - off < 16 ? n - off : 16;
        for (uint32_t i = 0; i < k; i++) out[off + i] = in[off + i] ^ ks[i];
    }
    if (enc) gh_data(&g, out, n);
    uint8_t lb[16]; uint64_t ab = (uint64_t)al * 8, cb = (uint64_t)n * 8;
    for (int i = 0; i < 8; i++) { lb[i] = (uint8_t)(ab >> (56 - 8 * i)); lb[8 + i] = (uint8_t)(cb >> (56 - 8 * i)); }
    gh_block(&g, lb);
    aes_enc(rk, j0, ks);
    for (int i = 0; i < 8; i++) { tag[i] = (uint8_t)((g.yh >> (56 - 8 * i)) ^ ks[i]); tag[8 + i] = (uint8_t)((g.yl >> (56 - 8 * i)) ^ ks[8 + i]); }
}
void gcm_seal(const uint8_t key[16], const uint8_t nonce[12], const uint8_t* aad, uint32_t al, const uint8_t* pt, uint32_t pl, uint8_t* ct) {
    gcm_core(key, nonce, aad, al, pt, pl, ct, 1, ct + pl);
}
int gcm_open(const uint8_t key[16], const uint8_t nonce[12], const uint8_t* aad, uint32_t al, const uint8_t* ct, uint32_t cl, uint8_t* pt) {
    if (cl < 16) return -1;
    uint32_t n = cl - 16; uint8_t tag[16];
    gcm_core(key, nonce, aad, al, ct, n, pt, 0, tag);
    uint8_t d = 0; for (int i = 0; i < 16; i++) d |= tag[i] ^ ct[n + i];
    return d ? -1 : 0;
}

/* ---------- PBKDF2 / Zufall ---------- */
void pbkdf2_sha256(const uint8_t* pw, uint32_t pl, const uint8_t* salt, uint32_t sl, uint32_t iter, uint8_t out[32]) {
    uint8_t buf[64 + 4], u[32], t[32];
    if (sl > 64) sl = 64;
    mc(buf, salt, sl); buf[sl] = 0; buf[sl + 1] = 0; buf[sl + 2] = 0; buf[sl + 3] = 1;
    hmac_sha256(pw, pl, buf, sl + 4, u);
    mc(t, u, 32);
    for (uint32_t i = 1; i < iter; i++) {
        uint8_t v[32];
        hmac_sha256(pw, pl, u, 32, v);
        mc(u, v, 32);
        for (int k = 0; k < 32; k++) t[k] ^= u[k];
    }
    mc(out, t, 32);
}
void krand(uint8_t* p, uint32_t n) {
    uint32_t a = 1, b, c = 0, d;
    __asm__ volatile ("cpuid" : "+a"(a), "=b"(b), "+c"(c), "=d"(d));
    int hw = (c >> 30) & 1;
    for (uint32_t i = 0; i < n; i += 8) {
        uint64_t v = 0; int ok = 0;
        if (hw) for (int t = 0; t < 20 && !ok; t++) { unsigned char f; __asm__ volatile ("rdrand %0; setc %1" : "=r"(v), "=qm"(f) : : "cc"); ok = f; }
        if (!ok) { uint32_t lo, hi; __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi)); v = ((uint64_t)hi << 32) | lo; v *= 0x9E3779B97F4A7C15ULL; }
        for (uint32_t k = 0; k < 8 && i + k < n; k++) p[i + k] = (uint8_t)(v >> (8 * k));
    }
}

/* ---------- AES-128 Einzelblock (Verschluesselung und Entschluesselung), fuer WLAN (Key-Wrap, CCMP, CMAC) ---------- */
void aes128_enc(const uint8_t key[16], const uint8_t in[16], uint8_t out[16]) {
    uint8_t rk[176];
    if (!sbox_ok) sbox_init();
    aes_keys(key, rk); aes_enc(rk, in, out);
}
static uint8_t gmul(uint8_t a, uint8_t b) {
    uint8_t p = 0;
    for (int i = 0; i < 8; i++) { if (b & 1) p ^= a; a = xt(a); b >>= 1; }
    return p;
}
void aes128_dec(const uint8_t key[16], const uint8_t in[16], uint8_t out[16]) {
    uint8_t rk[176], inv[256], s[16], t[16];
    if (!sbox_ok) sbox_init();
    aes_keys(key, rk);
    for (int i = 0; i < 256; i++) inv[sbox[i]] = (uint8_t)i;
    for (int i = 0; i < 16; i++) s[i] = in[i] ^ rk[160 + i];
    for (int r = 9; r >= 0; r--) {
        for (int c = 0; c < 4; c++) for (int rr = 0; rr < 4; rr++) t[4 * ((c + rr) & 3) + rr] = s[4 * c + rr];     /* InvShiftRows */
        for (int i = 0; i < 16; i++) s[i] = (uint8_t)(inv[t[i]] ^ rk[16 * r + i]);
        if (r > 0) {
            for (int c = 0; c < 4; c++) {
                uint8_t a0 = s[4*c], a1 = s[4*c+1], a2 = s[4*c+2], a3 = s[4*c+3];
                t[4*c]   = (uint8_t)(gmul(a0, 14) ^ gmul(a1, 11) ^ gmul(a2, 13) ^ gmul(a3, 9));
                t[4*c+1] = (uint8_t)(gmul(a0, 9) ^ gmul(a1, 14) ^ gmul(a2, 11) ^ gmul(a3, 13));
                t[4*c+2] = (uint8_t)(gmul(a0, 13) ^ gmul(a1, 9) ^ gmul(a2, 14) ^ gmul(a3, 11));
                t[4*c+3] = (uint8_t)(gmul(a0, 11) ^ gmul(a1, 13) ^ gmul(a2, 9) ^ gmul(a3, 14));
            }
            mc(s, t, 16);
        }
    }
    mc(out, s, 16);
}
