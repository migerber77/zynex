/*
 * Zynex OS - WLAN-Sicherheit: SHA-1, HMAC-SHA1, PBKDF2, WPA2-PSK (PMK/PTK), AES-Key-Wrap, AES-CMAC, CCMP.
 * Alles reine Rechenfunktionen; durch den Selbsttest (Befehl wpatest) mit Testvektoren geprueft.
 */
#include "wpa.h"
#include "crypto.h"

extern void kprint(const char* s);

static void mcpy(void* d, const void* s, uint32_t n) { uint8_t* a = d; const uint8_t* b = s; while (n--) *a++ = *b++; }
static void mset(void* d, uint8_t v, uint32_t n) { uint8_t* a = d; while (n--) *a++ = v; }
static uint32_t slen(const char* s) { uint32_t n = 0; while (s[n]) n++; return n; }
static int mcmp(const void* x, const void* y, uint32_t n) { const uint8_t* a = x; const uint8_t* b = y; while (n--) { if (*a != *b) return *a < *b ? -1 : 1; a++; b++; } return 0; }

/* ---------- SHA-1 ---------- */
struct sha1c { uint32_t h[5]; uint8_t buf[64]; uint64_t len; uint32_t n; };
static uint32_t rol(uint32_t x, int s) { return (x << s) | (x >> (32 - s)); }
static void sha1_block(struct sha1c* c, const uint8_t* p) {
    uint32_t w[80];
    for (int i = 0; i < 16; i++) w[i] = ((uint32_t)p[4*i] << 24) | ((uint32_t)p[4*i+1] << 16) | ((uint32_t)p[4*i+2] << 8) | p[4*i+3];
    for (int i = 16; i < 80; i++) w[i] = rol(w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16], 1);
    uint32_t a = c->h[0], b = c->h[1], cc = c->h[2], d = c->h[3], e = c->h[4];
    for (int i = 0; i < 80; i++) {
        uint32_t f, k;
        if (i < 20) { f = (b & cc) | (~b & d); k = 0x5A827999u; }
        else if (i < 40) { f = b ^ cc ^ d; k = 0x6ED9EBA1u; }
        else if (i < 60) { f = (b & cc) | (b & d) | (cc & d); k = 0x8F1BBCDCu; }
        else { f = b ^ cc ^ d; k = 0xCA62C1D6u; }
        uint32_t t = rol(a, 5) + f + e + k + w[i];
        e = d; d = cc; cc = rol(b, 30); b = a; a = t;
    }
    c->h[0] += a; c->h[1] += b; c->h[2] += cc; c->h[3] += d; c->h[4] += e;
}
static void sha1_init(struct sha1c* c) { c->h[0] = 0x67452301u; c->h[1] = 0xEFCDAB89u; c->h[2] = 0x98BADCFEu; c->h[3] = 0x10325476u; c->h[4] = 0xC3D2E1F0u; c->len = 0; c->n = 0; }
static void sha1_upd(struct sha1c* c, const void* d, uint32_t n) {
    const uint8_t* p = d; c->len += n;
    while (n) {
        uint32_t k = 64 - c->n; if (k > n) k = n;
        mcpy(c->buf + c->n, p, k); c->n += k; p += k; n -= k;
        if (c->n == 64) { sha1_block(c, c->buf); c->n = 0; }
    }
}
static void sha1_fin(struct sha1c* c, uint8_t out[20]) {
    uint64_t bits = c->len * 8; uint8_t pad = 0x80;
    sha1_upd(c, &pad, 1); pad = 0;
    while (c->n != 56) sha1_upd(c, &pad, 1);
    uint8_t l[8]; for (int i = 0; i < 8; i++) l[i] = (uint8_t)(bits >> (56 - 8 * i));
    sha1_upd(c, l, 8);
    for (int i = 0; i < 5; i++) { out[4*i] = (uint8_t)(c->h[i] >> 24); out[4*i+1] = (uint8_t)(c->h[i] >> 16); out[4*i+2] = (uint8_t)(c->h[i] >> 8); out[4*i+3] = (uint8_t)c->h[i]; }
}
void sha1(const void* d, uint32_t n, uint8_t out[20]) { struct sha1c c; sha1_init(&c); sha1_upd(&c, d, n); sha1_fin(&c, out); }

void hmac_sha1(const uint8_t* key, uint32_t kl, const uint8_t* d, uint32_t n, uint8_t out[20]) {
    uint8_t k[64], ip[64], op[64], in[20]; struct sha1c c;
    mset(k, 0, 64);
    if (kl > 64) sha1(key, kl, k); else mcpy(k, key, kl);
    for (int i = 0; i < 64; i++) { ip[i] = k[i] ^ 0x36; op[i] = k[i] ^ 0x5c; }
    sha1_init(&c); sha1_upd(&c, ip, 64); sha1_upd(&c, d, n); sha1_fin(&c, in);
    sha1_init(&c); sha1_upd(&c, op, 64); sha1_upd(&c, in, 20); sha1_fin(&c, out);
}

void pbkdf2_sha1(const uint8_t* pw, uint32_t pl, const uint8_t* salt, uint32_t sl, uint32_t iter, uint8_t* out, uint32_t ol) {
    uint8_t s[80], u[20], t[20];
    if (sl > 64) sl = 64;
    for (uint32_t blk = 1; ol; blk++) {
        mcpy(s, salt, sl); s[sl] = (uint8_t)(blk >> 24); s[sl+1] = (uint8_t)(blk >> 16); s[sl+2] = (uint8_t)(blk >> 8); s[sl+3] = (uint8_t)blk;
        hmac_sha1(pw, pl, s, sl + 4, u); mcpy(t, u, 20);
        for (uint32_t i = 1; i < iter; i++) { hmac_sha1(pw, pl, u, 20, u); for (int k = 0; k < 20; k++) t[k] ^= u[k]; }
        uint32_t k = ol > 20 ? 20 : ol; mcpy(out, t, k); out += k; ol -= k;
    }
}

void wpa_psk(const char* pass, const char* ssid, uint8_t pmk[32]) {
    pbkdf2_sha1((const uint8_t*)pass, slen(pass), (const uint8_t*)ssid, slen(ssid), 4096, pmk, 32);
}

void wpa_prf_sha1(const uint8_t* key, uint32_t kl, const char* label, const uint8_t* data, uint32_t dl, uint8_t* out, uint32_t ol) {
    uint8_t buf[160]; uint32_t ll = slen(label), n = 0, pos = 0;
    mcpy(buf, label, ll); n = ll; buf[n++] = 0; mcpy(buf + n, data, dl); n += dl;
    for (uint8_t i = 0; pos < ol; i++) {
        uint8_t h[20]; buf[n] = i; hmac_sha1(key, kl, buf, n + 1, h);
        uint32_t k = ol - pos > 20 ? 20 : ol - pos; mcpy(out + pos, h, k); pos += k;
    }
}

void wpa_kdf_sha256(const uint8_t* key, uint32_t kl, const char* label, const uint8_t* data, uint32_t dl, uint8_t* out, uint32_t ol) {
    uint8_t buf[200]; uint32_t ll = slen(label), pos = 0;
    uint16_t bits = (uint16_t)(ol * 8);
    for (uint16_t i = 1; pos < ol; i++) {
        uint32_t n = 0;
        buf[n++] = (uint8_t)i; buf[n++] = (uint8_t)(i >> 8);
        mcpy(buf + n, label, ll); n += ll; mcpy(buf + n, data, dl); n += dl;
        buf[n++] = (uint8_t)bits; buf[n++] = (uint8_t)(bits >> 8);
        uint8_t h[32]; hmac_sha256(key, kl, buf, n, h);
        uint32_t k = ol - pos > 32 ? 32 : ol - pos; mcpy(out + pos, h, k); pos += k;
    }
}

void wpa_ptk(const uint8_t pmk[32], const uint8_t aa[6], const uint8_t spa[6], const uint8_t anonce[32], const uint8_t snonce[32], int sha256_kdf, uint8_t ptk[48]) {
    uint8_t d[76]; const uint8_t* lo; const uint8_t* hi;
    lo = mcmp(aa, spa, 6) < 0 ? aa : spa; hi = lo == aa ? spa : aa;
    mcpy(d, lo, 6); mcpy(d + 6, hi, 6);
    lo = mcmp(anonce, snonce, 32) < 0 ? anonce : snonce; hi = lo == anonce ? snonce : anonce;
    mcpy(d + 12, lo, 32); mcpy(d + 44, hi, 32);
    if (sha256_kdf) wpa_kdf_sha256(pmk, 32, "Pairwise key expansion", d, 76, ptk, 48);
    else wpa_prf_sha1(pmk, 32, "Pairwise key expansion", d, 76, ptk, 48);
}

/* ---------- AES-Key-Wrap ---------- */
void aes_wrap(const uint8_t kek[16], const uint8_t* pt, uint32_t n, uint8_t* ct) {
    uint32_t nb = n / 8; uint8_t a[8], b[16];
    mset(a, 0xA6, 8); mcpy(ct + 8, pt, n);
    for (uint32_t j = 0; j < 6; j++)
        for (uint32_t i = 1; i <= nb; i++) {
            mcpy(b, a, 8); mcpy(b + 8, ct + 8 * i, 8); aes128_enc(kek, b, b);
            uint32_t t = nb * j + i; mcpy(a, b, 8);
            a[7] ^= (uint8_t)t; a[6] ^= (uint8_t)(t >> 8); a[5] ^= (uint8_t)(t >> 16); a[4] ^= (uint8_t)(t >> 24);
            mcpy(ct + 8 * i, b + 8, 8);
        }
    mcpy(ct, a, 8);
}
int aes_unwrap(const uint8_t kek[16], const uint8_t* ct, uint32_t n, uint8_t* pt) {
    if (n < 24 || n % 8 || n > 8 * 64) return -1;
    uint32_t nb = n / 8 - 1; uint8_t a[8], b[16], r[8 * 64];
    mcpy(a, ct, 8); mcpy(r, ct + 8, n - 8);
    for (int j = 5; j >= 0; j--)
        for (uint32_t i = nb; i >= 1; i--) {
            uint32_t t = nb * (uint32_t)j + i;
            mcpy(b, a, 8); b[7] ^= (uint8_t)t; b[6] ^= (uint8_t)(t >> 8); b[5] ^= (uint8_t)(t >> 16); b[4] ^= (uint8_t)(t >> 24);
            mcpy(b + 8, r + 8 * (i - 1), 8); aes128_dec(kek, b, b);
            mcpy(a, b, 8); mcpy(r + 8 * (i - 1), b + 8, 8);
        }
    for (int i = 0; i < 8; i++) if (a[i] != 0xA6) return -1;
    mcpy(pt, r, n - 8);
    return 0;
}

/* ---------- AES-CMAC ---------- */
static void dbl(uint8_t x[16]) {
    uint8_t c = (uint8_t)(x[0] >> 7);
    for (int i = 0; i < 15; i++) x[i] = (uint8_t)((x[i] << 1) | (x[i+1] >> 7));
    x[15] = (uint8_t)(x[15] << 1); if (c) x[15] ^= 0x87;
}
void aes_cmac(const uint8_t key[16], const uint8_t* d, uint32_t n, uint8_t out[16]) {
    uint8_t k1[16], k2[16], x[16], y[16], z[16];
    mset(z, 0, 16); aes128_enc(key, z, k1); dbl(k1); mcpy(k2, k1, 16); dbl(k2);
    uint32_t blocks = (n + 15) / 16; int full = n && n % 16 == 0;
    if (!blocks) blocks = 1;
    mset(x, 0, 16);
    for (uint32_t i = 0; i + 1 < blocks; i++) { for (int k = 0; k < 16; k++) y[k] = x[k] ^ d[16 * i + k]; aes128_enc(key, y, x); }
    uint32_t off = 16 * (blocks - 1), rest = n - off; uint8_t last[16];
    mset(last, 0, 16); mcpy(last, d + off, rest);
    if (full) for (int k = 0; k < 16; k++) last[k] ^= k1[k];
    else { last[rest] = 0x80; for (int k = 0; k < 16; k++) last[k] ^= k2[k]; }
    for (int k = 0; k < 16; k++) y[k] = x[k] ^ last[k];
    aes128_enc(key, y, out);
}

void wpa_eapol_mic(const uint8_t kck[16], int ver, const uint8_t* frame, uint32_t n, uint8_t mic[16]) {
    if (ver >= 3) aes_cmac(kck, frame, n, mic);
    else { uint8_t h[20]; hmac_sha1(kck, 16, frame, n, h); mcpy(mic, h, 16); }
}

/* ---------- CCMP (AES-CCM, M=8, L=2) ---------- */
static void ccm_mac(const uint8_t key[16], const uint8_t nonce[13], const uint8_t* aad, uint32_t al, const uint8_t* pt, uint32_t pl, uint8_t t[16]) {
    uint8_t b[16], x[16]; uint32_t i;
    b[0] = (uint8_t)((al ? 0x40 : 0) | (((8 - 2) / 2) << 3) | 1); mcpy(b + 1, nonce, 13); b[14] = (uint8_t)(pl >> 8); b[15] = (uint8_t)pl;
    aes128_enc(key, b, x);
    if (al) {
        uint8_t blk[16]; uint32_t pos = 0; mset(blk, 0, 16);
        blk[0] = (uint8_t)(al >> 8); blk[1] = (uint8_t)al; pos = 2;
        for (i = 0; i < al; i++) {
            blk[pos++] = aad[i];
            if (pos == 16) { for (int k = 0; k < 16; k++) x[k] ^= blk[k]; aes128_enc(key, x, x); mset(blk, 0, 16); pos = 0; }
        }
        if (pos) { for (int k = 0; k < 16; k++) x[k] ^= blk[k]; aes128_enc(key, x, x); }
    }
    for (i = 0; i < pl; i += 16) {
        uint32_t k = pl - i > 16 ? 16 : pl - i;
        for (uint32_t j = 0; j < k; j++) x[j] ^= pt[i + j];
        aes128_enc(key, x, x);
    }
    mcpy(t, x, 16);
}
static void ccm_ctr(const uint8_t key[16], const uint8_t nonce[13], uint32_t ctr, uint8_t s[16]) {
    uint8_t a[16]; a[0] = 1; mcpy(a + 1, nonce, 13); a[14] = (uint8_t)(ctr >> 8); a[15] = (uint8_t)ctr;
    aes128_enc(key, a, s);
}
void ccm_seal(const uint8_t key[16], const uint8_t nonce[13], const uint8_t* aad, uint32_t al, const uint8_t* pt, uint32_t pl, uint8_t* ct) {
    uint8_t t[16], s[16];
    ccm_mac(key, nonce, aad, al, pt, pl, t);
    for (uint32_t i = 0; i < pl; i += 16) {
        uint32_t k = pl - i > 16 ? 16 : pl - i; ccm_ctr(key, nonce, 1 + i / 16, s);
        for (uint32_t j = 0; j < k; j++) ct[i + j] = pt[i + j] ^ s[j];
    }
    ccm_ctr(key, nonce, 0, s);
    for (int j = 0; j < 8; j++) ct[pl + j] = t[j] ^ s[j];
}
int ccm_open(const uint8_t key[16], const uint8_t nonce[13], const uint8_t* aad, uint32_t al, const uint8_t* ct, uint32_t cl, uint8_t* pt) {
    if (cl < 8) return -1;
    uint32_t pl = cl - 8; uint8_t t[16], s[16];
    for (uint32_t i = 0; i < pl; i += 16) {
        uint32_t k = pl - i > 16 ? 16 : pl - i; ccm_ctr(key, nonce, 1 + i / 16, s);
        for (uint32_t j = 0; j < k; j++) pt[i + j] = ct[i + j] ^ s[j];
    }
    ccm_mac(key, nonce, aad, al, pt, pl, t);
    ccm_ctr(key, nonce, 0, s);
    uint8_t diff = 0; for (int j = 0; j < 8; j++) diff |= (uint8_t)((t[j] ^ s[j]) ^ ct[pl + j]);
    return diff ? -1 : 0;
}

/* ---------- Selbsttest ---------- */
static int hexv(char c) { return c >= 'a' ? c - 'a' + 10 : c >= 'A' ? c - 'A' + 10 : c - '0'; }
static int gleich(const uint8_t* v, const char* hex) {
    uint32_t n = slen(hex) / 2;
    for (uint32_t i = 0; i < n; i++) if (v[i] != (uint8_t)(hexv(hex[2*i]) * 16 + hexv(hex[2*i+1]))) return 0;
    return 1;
}
static int fehler;
static void ergebnis(const char* name, int ok) { kprint("wpa: "); kprint(name); kprint(ok ? " OK\n" : " FEHLER\n"); if (!ok) fehler++; }

int wpa_selbsttest(void) {
    uint8_t o[64], p[64], k[16], n13[13]; fehler = 0;
    sha1("abc", 3, o); ergebnis("SHA-1", gleich(o, "a9993e364706816aba3e25717850c26c9cd0d89d"));
    hmac_sha1((const uint8_t*)"Jefe", 4, (const uint8_t*)"what do ya want for nothing?", 28, o); ergebnis("HMAC-SHA1", gleich(o, "effcdf6ae5eb2fa2d27416d5f184df9c259a7c79"));
    pbkdf2_sha1((const uint8_t*)"password", 8, (const uint8_t*)"salt", 4, 1, o, 20); ergebnis("PBKDF2-SHA1", gleich(o, "0c60c80f961f0e71f3a9b524af6012062fe037a6"));
    wpa_psk("password", "IEEE", o); ergebnis("WPA2-PSK (IEEE-Vektor)", gleich(o, "f42c6fc52df0ebef9ebb4b90b38a5f902e83fe1b135a70e23aed762e9710a12e"));
    for (int i = 0; i < 16; i++) { k[i] = (uint8_t)i; p[i] = (uint8_t)(i * 0x11); }
    aes128_enc(k, p, o); ergebnis("AES-128 verschluesseln (FIPS-197)", gleich(o, "69c4e0d86a7b0430d8cdb78070b4c55a"));
    aes128_dec(k, o, o + 16); ergebnis("AES-128 entschluesseln", mcmp(o + 16, p, 16) == 0);
    aes_wrap(k, p, 16, o); ergebnis("AES-Key-Wrap (RFC 3394)", gleich(o, "1fa68b0a8112b447aef34bd8fb5a7b829d3e862371d2cfe5"));
    ergebnis("AES-Key-Unwrap", aes_unwrap(k, o, 24, o + 32) == 0 && mcmp(o + 32, p, 16) == 0);
    o[3] ^= 1; ergebnis("AES-Key-Unwrap erkennt Verfaelschung", aes_unwrap(k, o, 24, o + 32) != 0);
    static const uint8_t ck[16] = {0x2b,0x7e,0x15,0x16,0x28,0xae,0xd2,0xa6,0xab,0xf7,0x15,0x88,0x09,0xcf,0x4f,0x3c};
    static const uint8_t cm[16] = {0x6b,0xc1,0xbe,0xe2,0x2e,0x40,0x9f,0x96,0xe9,0x3d,0x7e,0x11,0x73,0x93,0x17,0x2a};
    aes_cmac(ck, cm, 0, o); ergebnis("AES-CMAC leer (RFC 4493)", gleich(o, "bb1d6929e95937287fa37d129b756746"));
    aes_cmac(ck, cm, 16, o); ergebnis("AES-CMAC 16 Byte (RFC 4493)", gleich(o, "070a16b46b4d4144f79bdd9dd04a287c"));
    for (int i = 0; i < 16; i++) k[i] = (uint8_t)(0xC0 + i);
    static const uint8_t nn[13] = {0,0,0,3,2,1,0,0xA0,0xA1,0xA2,0xA3,0xA4,0xA5};
    for (int i = 0; i < 13; i++) n13[i] = nn[i];
    uint8_t aad[8], pt[23], ct[31], back[23];
    for (int i = 0; i < 8; i++) aad[i] = (uint8_t)i;
    for (int i = 0; i < 23; i++) pt[i] = (uint8_t)(8 + i);
    ccm_seal(k, n13, aad, 8, pt, 23, ct);
    ergebnis("CCMP verschluesseln (RFC 3610)", gleich(ct, "588c979a61c663d2f066d0c2c0f989806d5f6b61dac38417e8d12cfdf926e0"));
    ergebnis("CCMP entschluesseln", ccm_open(k, n13, aad, 8, ct, 31, back) == 0 && mcmp(back, pt, 23) == 0);
    ct[5] ^= 1; ergebnis("CCMP erkennt Verfaelschung", ccm_open(k, n13, aad, 8, ct, 31, back) != 0);
    uint8_t pmk[32], a1[6] = {1,2,3,4,5,6}, a2[6] = {9,8,7,6,5,4}, n1[32], n2[32], t1[48], t2[48];
    for (int i = 0; i < 32; i++) { pmk[i] = (uint8_t)i; n1[i] = (uint8_t)(i + 1); n2[i] = (uint8_t)(200 - i); }
    wpa_ptk(pmk, a1, a2, n1, n2, 0, t1); wpa_ptk(pmk, a2, a1, n2, n1, 0, t2);
    ergebnis("PTK unabhaengig von der Reihenfolge (SHA-1-PRF)", mcmp(t1, t2, 48) == 0 && t1[0] | t1[47]);
    wpa_ptk(pmk, a1, a2, n1, n2, 1, t1); wpa_ptk(pmk, a2, a1, n2, n1, 1, t2);
    ergebnis("PTK unabhaengig von der Reihenfolge (SHA-256-KDF)", mcmp(t1, t2, 48) == 0 && t1[0] | t1[47]);
    kprint(fehler ? "wpa: Selbsttest FEHLGESCHLAGEN\n" : "wpa: Selbsttest bestanden\n");
    return fehler;
}
