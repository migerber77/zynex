#pragma once
/*
 * Zynex OS - Hash-basierte Signaturpruefung (LMS nach RFC 8554, LMOTS_SHA256_N32_W8), nur mit SHA-256.
 * Gilt als quantensicher: Die Sicherheit beruht nur auf der Hash-Funktion, nicht auf RSA/ECC.
 * Eigenstaendig (ohne libc), wird vom UEFI-Lader und vom Kernel benutzt.
 * Oeffentlicher Schluessel: 56 Byte = u32 lms_typ | u32 lmots_typ | 16 Byte I | 32 Byte T[1]
 * Signatur: u32 q | u32 lmots_typ | 32 Byte C | 34 x 32 Byte y | u32 lms_typ | h x 32 Byte Pfad
 */
#include <stdint.h>

#define LMS_PUB_LEN 56
#define LMS_OTS_N   32
#define LMS_OTS_P   34

typedef struct { uint32_t h[8]; uint64_t len; uint8_t buf[64]; uint32_t n; } lms_sha;

static const uint32_t lms_K[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2 };

#define LMS_ROR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

static void lms_sha_block(lms_sha* c, const uint8_t* p) {
    uint32_t w[64], a, b, d, e, f, g, h, cc;
    for (int i = 0; i < 16; i++) w[i] = (uint32_t)p[4*i] << 24 | (uint32_t)p[4*i+1] << 16 | (uint32_t)p[4*i+2] << 8 | p[4*i+3];
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = LMS_ROR(w[i-15], 7) ^ LMS_ROR(w[i-15], 18) ^ (w[i-15] >> 3), s1 = LMS_ROR(w[i-2], 17) ^ LMS_ROR(w[i-2], 19) ^ (w[i-2] >> 10);
        w[i] = w[i-16] + s0 + w[i-7] + s1;
    }
    a = c->h[0]; b = c->h[1]; cc = c->h[2]; d = c->h[3]; e = c->h[4]; f = c->h[5]; g = c->h[6]; h = c->h[7];
    for (int i = 0; i < 64; i++) {
        uint32_t t1 = h + (LMS_ROR(e, 6) ^ LMS_ROR(e, 11) ^ LMS_ROR(e, 25)) + ((e & f) ^ (~e & g)) + lms_K[i] + w[i];
        uint32_t t2 = (LMS_ROR(a, 2) ^ LMS_ROR(a, 13) ^ LMS_ROR(a, 22)) + ((a & b) ^ (a & cc) ^ (b & cc));
        h = g; g = f; f = e; e = d + t1; d = cc; cc = b; b = a; a = t1 + t2;
    }
    c->h[0] += a; c->h[1] += b; c->h[2] += cc; c->h[3] += d; c->h[4] += e; c->h[5] += f; c->h[6] += g; c->h[7] += h;
}
static void lms_sha_init(lms_sha* c) {
    static const uint32_t i0[8] = { 0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19 };
    for (int i = 0; i < 8; i++) c->h[i] = i0[i];
    c->len = 0; c->n = 0;
}
static void lms_sha_upd(lms_sha* c, const uint8_t* d, uint32_t n) {
    c->len += n;
    while (n) {
        if (c->n == 0 && n >= 64) { lms_sha_block(c, d); d += 64; n -= 64; continue; }
        uint32_t k = 64 - c->n; if (k > n) k = n;
        for (uint32_t i = 0; i < k; i++) c->buf[c->n + i] = d[i];
        c->n += k; d += k; n -= k;
        if (c->n == 64) { lms_sha_block(c, c->buf); c->n = 0; }
    }
}
static void lms_sha_fin(lms_sha* c, uint8_t out[32]) {
    uint64_t bits = c->len * 8; uint8_t one = 0x80, z = 0;
    lms_sha_upd(c, &one, 1);
    while (c->n != 56) lms_sha_upd(c, &z, 1);
    uint8_t l[8]; for (int i = 0; i < 8; i++) l[i] = (uint8_t)(bits >> (56 - 8 * i));
    lms_sha_upd(c, l, 8);
    for (int i = 0; i < 8; i++) { out[4*i] = (uint8_t)(c->h[i] >> 24); out[4*i+1] = (uint8_t)(c->h[i] >> 16); out[4*i+2] = (uint8_t)(c->h[i] >> 8); out[4*i+3] = (uint8_t)c->h[i]; }
}
static void lms_be32(uint8_t* o, uint32_t v) { o[0] = (uint8_t)(v >> 24); o[1] = (uint8_t)(v >> 16); o[2] = (uint8_t)(v >> 8); o[3] = (uint8_t)v; }
static uint32_t lms_rd32(const uint8_t* p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }

/* Prueft die Signatur ueber msg1||msg2 (zwei Teile, damit grosse Kernel-Abbilder nicht kopiert werden muessen).
 * Rueckgabe 0 = gueltig, sonst Fehlercode (<0). */
static int lms_verify(const uint8_t* pub, const uint8_t* sig, uint32_t siglen, const uint8_t* m1, uint32_t n1, const uint8_t* m2, uint32_t n2) {
    uint32_t lms_typ = lms_rd32(pub), ots_typ = lms_rd32(pub + 4);
    int h = lms_typ == 5 ? 5 : lms_typ == 6 ? 10 : lms_typ == 7 ? 15 : lms_typ == 8 ? 20 : lms_typ == 9 ? 25 : 0;
    if (!h || ots_typ != 4) return -1;
    const uint8_t* I = pub + 8; const uint8_t* T1 = pub + 24;
    uint32_t need = 4 + 4 + LMS_OTS_N + LMS_OTS_P * LMS_OTS_N + 4 + (uint32_t)h * 32;
    if (siglen != need) return -2;
    uint32_t q = lms_rd32(sig);
    if (q >= (1u << h)) return -3;
    if (lms_rd32(sig + 4) != 4) return -4;
    const uint8_t* C = sig + 8; const uint8_t* y = sig + 8 + 32;
    if (lms_rd32(sig + 8 + 32 + LMS_OTS_P * 32) != lms_typ) return -5;
    const uint8_t* path = sig + 8 + 32 + LMS_OTS_P * 32 + 4;
    uint8_t qb[4], pre[7], Q[34], tmp[32], K[32]; lms_sha s;
    lms_be32(qb, q);
    /* Q = H(I || q || 0x8181 || C || message), danach Pruefsumme anhaengen */
    lms_sha_init(&s); lms_sha_upd(&s, I, 16); lms_sha_upd(&s, qb, 4); pre[0] = 0x81; pre[1] = 0x81; lms_sha_upd(&s, pre, 2); lms_sha_upd(&s, C, 32);
    if (n1) lms_sha_upd(&s, m1, n1);
    if (n2) lms_sha_upd(&s, m2, n2);
    lms_sha_fin(&s, Q);
    uint32_t ck = 0; for (int i = 0; i < 32; i++) ck += 255u - Q[i];
    Q[32] = (uint8_t)(ck >> 8); Q[33] = (uint8_t)ck;
    /* Kc = H(I || q || 0x8080 || z[0] ... z[33]) */
    lms_sha kc; lms_sha_init(&kc); lms_sha_upd(&kc, I, 16); lms_sha_upd(&kc, qb, 4); pre[0] = 0x80; pre[1] = 0x80; lms_sha_upd(&kc, pre, 2);
    for (int i = 0; i < LMS_OTS_P; i++) {
        for (int k = 0; k < 32; k++) tmp[k] = y[i * 32 + k];
        for (uint32_t j = Q[i]; j < 255; j++) {                                     /* Kette von a_i bis 2^8-2 */
            uint8_t ib[2] = { (uint8_t)(i >> 8), (uint8_t)i }, jb = (uint8_t)j;
            lms_sha_init(&s); lms_sha_upd(&s, I, 16); lms_sha_upd(&s, qb, 4); lms_sha_upd(&s, ib, 2); lms_sha_upd(&s, &jb, 1); lms_sha_upd(&s, tmp, 32); lms_sha_fin(&s, tmp);
        }
        lms_sha_upd(&kc, tmp, 32);
    }
    lms_sha_fin(&kc, K);
    /* Blatt und Pfad bis zur Wurzel */
    uint32_t node = (1u << h) + q; uint8_t nb[4], d2[2];
    lms_be32(nb, node); d2[0] = 0x82; d2[1] = 0x82;
    lms_sha_init(&s); lms_sha_upd(&s, I, 16); lms_sha_upd(&s, nb, 4); lms_sha_upd(&s, d2, 2); lms_sha_upd(&s, K, 32); lms_sha_fin(&s, tmp);
    for (int i = 0; i < h; i++) {
        lms_be32(nb, node / 2); d2[0] = 0x83; d2[1] = 0x83;
        lms_sha_init(&s); lms_sha_upd(&s, I, 16); lms_sha_upd(&s, nb, 4); lms_sha_upd(&s, d2, 2);
        if (node & 1) { lms_sha_upd(&s, path + i * 32, 32); lms_sha_upd(&s, tmp, 32); } else { lms_sha_upd(&s, tmp, 32); lms_sha_upd(&s, path + i * 32, 32); }
        lms_sha_fin(&s, tmp);
        node /= 2;
    }
    uint8_t diff = 0; for (int i = 0; i < 32; i++) diff |= (uint8_t)(tmp[i] ^ T1[i]);
    return diff ? -6 : 0;
}
