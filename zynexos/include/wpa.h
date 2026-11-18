#pragma once
#include <stdint.h>

/* WLAN-Sicherheit (WPA2-PSK, Grundlagen fuer WPA3): reine Rechenfunktionen, ohne Hardware pruefbar. */
void sha1(const void* d, uint32_t n, uint8_t out[20]);
void hmac_sha1(const uint8_t* key, uint32_t kl, const uint8_t* d, uint32_t n, uint8_t out[20]);
void pbkdf2_sha1(const uint8_t* pw, uint32_t pl, const uint8_t* salt, uint32_t sl, uint32_t iter, uint8_t* out, uint32_t ol);

/* WPA/WPA2-Passphrase (8..63 Zeichen) + SSID -> 32-Byte-PMK */
void wpa_psk(const char* pass, const char* ssid, uint8_t pmk[32]);
/* IEEE-802.11-PRF mit HMAC-SHA1; out_bits = 384 fuer CCMP-PTK */
void wpa_prf_sha1(const uint8_t* key, uint32_t kl, const char* label, const uint8_t* data, uint32_t dl, uint8_t* out, uint32_t ol);
/* KDF mit HMAC-SHA256 (802.11 SHA-256-Schluesselableitung, WPA2-PSK-SHA256 und WPA3) */
void wpa_kdf_sha256(const uint8_t* key, uint32_t kl, const char* label, const uint8_t* data, uint32_t dl, uint8_t* out, uint32_t ol);
/* PTK = PRF(PMK, "Pairwise key expansion", min(AA,SPA)|max(AA,SPA)|min(ANonce,SNonce)|max(...)); ptk: KCK(16) KEK(16) TK(16) */
void wpa_ptk(const uint8_t pmk[32], const uint8_t aa[6], const uint8_t spa[6], const uint8_t anonce[32], const uint8_t snonce[32], int sha256_kdf, uint8_t ptk[48]);

/* AES-Key-Wrap (RFC 3394) mit 128-Bit-KEK; n = Klartextlaenge (Vielfaches von 8) */
void aes_wrap(const uint8_t kek[16], const uint8_t* pt, uint32_t n, uint8_t* ct);        /* ct: n + 8 Byte */
int  aes_unwrap(const uint8_t kek[16], const uint8_t* ct, uint32_t n, uint8_t* pt);     /* n = Chiffretextlaenge (n-8 Byte Klartext), 0 = ok */

/* AES-CMAC (RFC 4493) */
void aes_cmac(const uint8_t key[16], const uint8_t* d, uint32_t n, uint8_t out[16]);

/* EAPOL-MIC: ver 2 = HMAC-SHA1 (WPA2), ver 3 = AES-CMAC (WPA3/SHA-256); frame mit genullter MIC */
void wpa_eapol_mic(const uint8_t kck[16], int ver, const uint8_t* frame, uint32_t n, uint8_t mic[16]);

/* CCMP (AES-CCM, 8-Byte-MIC): nonce 13 Byte = Prioritaet | A2 | PN; ct = Klartext + 8 Byte MIC */
void ccm_seal(const uint8_t key[16], const uint8_t nonce[13], const uint8_t* aad, uint32_t al, const uint8_t* pt, uint32_t pl, uint8_t* ct);
int  ccm_open(const uint8_t key[16], const uint8_t nonce[13], const uint8_t* aad, uint32_t al, const uint8_t* ct, uint32_t cl, uint8_t* pt);

/* Selbsttest mit bekannten Testvektoren; Rueckgabe = Anzahl Fehler */
int wpa_selbsttest(void);
