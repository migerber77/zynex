#pragma once
#include <stdint.h>
/* Kleine Kryptobibliothek fuer TLS 1.3: SHA-256, HMAC, HKDF, ChaCha20-Poly1305, X25519, RSA-PSS-Pruefung. */

struct sha256 { uint32_t h[8]; uint8_t buf[64]; uint64_t len; uint32_t n; };
void sha256_init(struct sha256* c);
void sha256_update(struct sha256* c, const void* d, uint32_t n);
void sha256_final(struct sha256* c, uint8_t out[32]);          /* zerstoert den Zustand (vorher kopieren) */
void sha256(const void* d, uint32_t n, uint8_t out[32]);
void hmac_sha256(const uint8_t* key, uint32_t kl, const uint8_t* d, uint32_t n, uint8_t out[32]);
void hkdf_extract(const uint8_t* salt, uint32_t sl, const uint8_t* ikm, uint32_t il, uint8_t out[32]);
void hkdf_expand_label(const uint8_t secret[32], const char* label, const uint8_t* ctx, uint32_t cl, uint8_t* out, uint32_t ol);

/* AEAD: ct = Klartext (len) + 16 Byte Tag. aead_open liefert 0 bei Erfolg. */
void aead_seal(const uint8_t key[32], const uint8_t nonce[12], const uint8_t* aad, uint32_t al, const uint8_t* pt, uint32_t pl, uint8_t* ct);
int  aead_open(const uint8_t key[32], const uint8_t nonce[12], const uint8_t* aad, uint32_t al, const uint8_t* ct, uint32_t cl, uint8_t* pt);

void x25519_pub(uint8_t out[32], const uint8_t priv[32]);
int  x25519_shared(uint8_t out[32], const uint8_t priv[32], const uint8_t peer[32]);   /* -1 bei Nullergebnis */

/* RSA-PSS (SHA-256, Salz 32): n und e als grosse Zahlen (Big-Endian), sig/ n gleich lang. 0 = Signatur gueltig. */
int rsa_pss_verify(const uint8_t* n, uint32_t nl, const uint8_t* e, uint32_t el,
                   const uint8_t* sig, uint32_t sl, const uint8_t mhash[32]);

/* SHA-384 / SHA-512 */
struct sha512 { uint64_t h[8]; uint8_t buf[128]; uint64_t len; uint32_t n; };
void sha384(const void* d, uint32_t n, uint8_t out[48]);
void sha512(const void* d, uint32_t n, uint8_t out[64]);

/* Oeffentliche-Schluessel-Pruefungen (alle Zahlen Big-Endian). hash_len 32/48/64 waehlt SHA-256/384/512. 0 = gueltig. */
int rsa_pkcs1_verify(const uint8_t* n, uint32_t nl, const uint8_t* e, uint32_t el,
                     const uint8_t* sig, uint32_t sl, const uint8_t* hash, uint32_t hash_len);
/* Q = unkomprimierter Punkt 04||X||Y; curve 256 oder 384; sig = DER (SEQUENCE{r,s}) */
int ecdsa_verify(int curve, const uint8_t* q, uint32_t ql, const uint8_t* hash, uint32_t hash_len,
                 const uint8_t* sig_der, uint32_t sigl);

/* AES-128-GCM: ct = Chiffretext (len) + 16 Byte Tag. gcm_open liefert 0 bei Erfolg. */
void gcm_seal(const uint8_t key[16], const uint8_t nonce[12], const uint8_t* aad, uint32_t al, const uint8_t* pt, uint32_t pl, uint8_t* ct);
int  gcm_open(const uint8_t key[16], const uint8_t nonce[12], const uint8_t* aad, uint32_t al, const uint8_t* ct, uint32_t cl, uint8_t* pt);

/* ECDH P-256: priv = 32 Zufallsbytes (0 < d < n), Punkte unkomprimiert (65 Bytes); Rueckgabe 0 = ok */
int p256_pub(const uint8_t priv[32], uint8_t out[65]);
int p256_shared(const uint8_t priv[32], const uint8_t peer[65], uint8_t out[32]);

/* PBKDF2-HMAC-SHA256 (Passwort-Hash) und Zufall (RDRAND, sonst Zeitzaehler als Notbehelf) */
void pbkdf2_sha256(const uint8_t* pw, uint32_t pl, const uint8_t* salt, uint32_t sl, uint32_t iter, uint8_t out[32]);
void krand(uint8_t* p, uint32_t n);

/* AES-128 Einzelblock */
void aes128_enc(const uint8_t key[16], const uint8_t in[16], uint8_t out[16]);
void aes128_dec(const uint8_t key[16], const uint8_t in[16], uint8_t out[16]);
/* ChaCha20: buf wird mit dem Schluesselstrom (Zaehler ab 1) verXORt */
void chacha20_stream(const uint8_t key[32], const uint8_t nonce[12], uint8_t* buf, uint32_t n);
