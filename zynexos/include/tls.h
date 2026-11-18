#pragma once
#include <stdint.h>
/* TLS 1.3 (ChaCha20-Poly1305, X25519, RSA-PSS). Prueft CertificateVerify und optional den Zertifikats-Fingerabdruck. */
int tls_get(uint32_t ip, uint16_t port, const char* host, const uint8_t* req, uint32_t reqlen,
            const char* fp_hex, const uint8_t** out, uint32_t* outlen, int* complete);
