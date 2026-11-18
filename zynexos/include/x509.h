#pragma once
#include <stdint.h>
/* X.509-Zertifikate: Parsen, Signaturpruefung, Hostnamen, Zertifikatsketten gegen Root-CAs. */
struct xcert {
    const uint8_t *der, *tbs, *sigoid, *sig, *issuer, *subject, *san;
    uint32_t derlen, tbslen, sigoidlen, siglen, issuerlen, subjectlen, sanlen;
    int keytype;                       /* 1 = RSA, 2 = EC P-256, 3 = EC P-384, 0 = anderes */
    const uint8_t *n, *e, *q;
    uint32_t nl, el, ql;
    uint32_t notbefore, notafter;      /* JJJJMMTT */
    int has_bc, ca;
};
int  x509_parse(const uint8_t* der, uint32_t len, struct xcert* c);
/* Signatur einer TLS-1.3-CertificateVerify (alg: 0x0403, 0x0503, 0x0804); 0 = gueltig */
int  x509_verify_tls(const struct xcert* c, uint16_t alg, const uint8_t* msg, uint32_t mlen, const uint8_t* sig, uint32_t sl);
/* list = TLS-certificate_list (je: len3, DER, ext-len2, ext). Rueckgabe 0 = vertrauenswuerdig; sonst Text in *err */
int  x509_chain_verify(const uint8_t* list, uint32_t listlen, const char* host, uint32_t today, const char** err);
int  x509_add_root(const uint8_t* der, uint32_t len);
uint32_t x509_root_count(void);
