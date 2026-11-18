/* Festplattenverschluesselung (siehe krypto.h). */
#include "krypto.h"
#include "crypto.h"
#include "synfs.h"
#include "block.h"
#include "zyndb.h"
#include <stddef.h>

extern void kprint(const char* str);
extern void kprint_dec(uint32_t val);

#define ITER 20000u
#define STAPEL 64u

static struct { int geladen; char dev[20]; uint8_t rk[4][32]; uint32_t fort; } K;
static char gesperrt[20];

static void mkopie(void* d, const void* s, uint32_t n) { uint8_t* a = d; const uint8_t* b = s; for (uint32_t i = 0; i < n; i++) a[i] = b[i]; }
static int gleich(const char* a, const char* b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }
static int ctgleich(const uint8_t* a, const uint8_t* b, uint32_t n) { uint8_t d = 0; for (uint32_t i = 0; i < n; i++) d |= (uint8_t)(a[i] ^ b[i]); return d == 0; }
static void wische(void* p, uint32_t n) { volatile uint8_t* a = p; for (uint32_t i = 0; i < n; i++) a[i] = 0; }
static uint32_t slen(const char* s) { uint32_t n = 0; while (s[n]) n++; return n; }

/* Rundenfunktion: Halbblock (256 Byte) -> Schluesselstrom, der in das andere Halbblock gexort wird */
static void runde(const uint8_t rk[32], uint32_t lba, uint8_t r, const uint8_t* quelle, uint8_t* ziel) {
    uint8_t m[5 + 256]; m[0] = (uint8_t)lba; m[1] = (uint8_t)(lba >> 8); m[2] = (uint8_t)(lba >> 16); m[3] = (uint8_t)(lba >> 24); m[4] = r;
    mkopie(m + 5, quelle, 256);
    uint8_t k[32]; hmac_sha256(rk, 32, m, sizeof(m), k);
    static const uint8_t nonce[12] = { 'Z', 'Y', 'N', 'E', 'X', '-', 'S', 'E', 'K', 'T', 'O', 'R' };
    chacha20_stream(k, nonce, ziel, 256);
    wische(k, 32);
}
void krypto_sektor(int enc, const uint8_t rk[4][32], uint32_t lba, uint8_t* s) {
    uint8_t* L = s; uint8_t* R = s + 256;
    if (enc) { runde(rk[0], lba, 0, L, R); runde(rk[1], lba, 1, R, L); runde(rk[2], lba, 2, L, R); runde(rk[3], lba, 3, R, L); }
    else     { runde(rk[3], lba, 3, R, L); runde(rk[2], lba, 2, L, R); runde(rk[1], lba, 1, R, L); runde(rk[0], lba, 0, L, R); }
}

int krypto_aktiv_fuer(const char* dev) { return K.geladen && dev && gleich(dev, K.dev); }
int krypto_schreibbar(uint32_t lba) { return K.geladen && lba != 0 && lba < K.fort; }
void krypto_lesen(uint32_t lba, uint8_t* buf, uint32_t count) {
    for (uint32_t i = 0; i < count; i++) if (krypto_schreibbar(lba + i)) krypto_sektor(0, K.rk, lba + i, buf + i * 512);
}
void krypto_schreiben(uint32_t lba, uint8_t* s) { if (krypto_schreibbar(lba)) krypto_sektor(1, K.rk, lba, s); }

static void runden_schluessel(const uint8_t dk[32], uint8_t rk[4][32]) {
    for (int i = 0; i < 4; i++) { uint8_t m[12] = { 'Z','Y','N','E','X','-','R','U','N','D','E', (uint8_t)i }; hmac_sha256(dk, 32, m, 12, rk[i]); }
}
static void pruefwert(const uint8_t dk[32], uint8_t out[32]) { static const uint8_t m[] = "ZYNEX-PRUEF"; hmac_sha256(dk, 32, m, 11, out); }
static void verpacken(const char* pw, const uint8_t salt[16], uint32_t iter, const uint8_t in[32], uint8_t out[32]) {
    uint8_t kek[32], pad[32]; static const uint8_t m[] = "ZYNEX-WRAP";
    pbkdf2_sha256((const uint8_t*)pw, slen(pw), salt, 16, iter, kek);
    hmac_sha256(kek, 32, m, 10, pad);
    for (int i = 0; i < 32; i++) out[i] = in[i] ^ pad[i];
    wische(kek, 32); wische(pad, 32);
}

void krypto_vergessen(const char* dev) { if (K.geladen && dev && gleich(dev, K.dev)) { wische(&K, sizeof(K)); } }
void krypto_sperren(void) { wische(&K, sizeof(K)); }
void krypto_merke_gesperrt(const char* dev) { uint32_t i = 0; while (dev && dev[i] && i < 19) { gesperrt[i] = dev[i]; i++; } gesperrt[i] = 0; }
const char* krypto_gesperrt_dev(void) { return gesperrt; }

static void setze_geraet(const char* dev) { uint32_t i = 0; while (dev[i] && i < 19) { K.dev[i] = dev[i]; i++; } K.dev[i] = 0; }

int krypto_entsperren(const char* dev, const char* pw) {
    static uint8_t sek[512];
    if (block_dev_read(dev, 0, sek, 1) < 0) return -2;
    const struct synfs_super* sp = (const struct synfs_super*)sek;
    if (sp->magic != SYNFS_MAGIC || sp->kr.magic != SYNFS_KRY_MAGIC) return -2;
    uint8_t dk[32], pr[32];
    verpacken(pw, sp->kr.salt, sp->kr.iter, sp->kr.wrap, dk);          /* XOR ist selbstinvers */
    pruefwert(dk, pr);
    if (!ctgleich(pr, sp->kr.pruef, 32)) { wische(dk, 32); return -1; }
    wische(&K, sizeof(K));
    setze_geraet(dev); runden_schluessel(dk, K.rk); K.fort = sp->kr.fortschritt; K.geladen = 1;
    wische(dk, 32);
    gesperrt[0] = 0;
    return 0;
}

static int verschluessele_ab(void) {
    static uint8_t puf[STAPEL * 512];
    struct synfs_krypto* h = synfs_krypto_hdr();
    uint32_t ende = synfs_ende(), schritt = 0;
    while (K.fort < ende) {
        uint32_t n = ende - K.fort; if (n > STAPEL) n = STAPEL;
        if (block_read(K.fort, puf, n) < 0) return -1;                  /* noch unverschluesselt: Hook laesst den Bereich durch */
        for (uint32_t i = 0; i < n; i++) krypto_sektor(1, K.rk, K.fort + i, puf + i * 512);
        if (block_write(K.fort, puf, n) < 0) return -2;                 /* Hook verschluesselt erst unterhalb von fort: roh schreiben */
        K.fort += n; h->fortschritt = K.fort;
        synfs_krypto_sichern();
        if (++schritt % 512 == 0) { kprint("  "); kprint_dec((uint32_t)((uint64_t)K.fort * 100 / ende)); kprint(" %\n"); }
    }
    return 0;
}

int krypto_einrichten(const char* pw) {
    if (!synfs_is_mounted()) return -1;
    if (K.geladen) return -2;
    struct synfs_krypto* h = synfs_krypto_hdr();
    if (h->magic == SYNFS_KRY_MAGIC) return -2;
    if (slen(pw) < 8) return -3;
    synfs_sync();
    uint8_t dk[32]; krand(dk, 32);
    h->magic = SYNFS_KRY_MAGIC; h->version = 1; h->iter = ITER; h->fortschritt = 1;
    krand(h->salt, 16);
    verpacken(pw, h->salt, ITER, dk, h->wrap);
    pruefwert(dk, h->pruef);
    wische(&K, sizeof(K));
    setze_geraet(block_get()->name); runden_schluessel(dk, K.rk); K.fort = 1; K.geladen = 1;
    wische(dk, 32);
    synfs_krypto_sichern();
    int r = verschluessele_ab();
    if (r == 0) zdb_neu_verpacken();
    return r;
}

int krypto_fortsetzen(void) {
    if (!K.geladen || !synfs_is_mounted()) return -1;
    return verschluessele_ab();
}

int krypto_passwort(const char* alt, const char* neu) {
    if (!K.geladen || !synfs_is_mounted()) return -1;
    if (slen(neu) < 8) return -3;
    struct synfs_krypto* h = synfs_krypto_hdr();
    uint8_t dk[32], pr[32];
    verpacken(alt, h->salt, h->iter, h->wrap, dk); pruefwert(dk, pr);
    if (!ctgleich(pr, h->pruef, 32)) { wische(dk, 32); return -2; }
    krand(h->salt, 16);
    verpacken(neu, h->salt, h->iter, dk, h->wrap);
    wische(dk, 32);
    return synfs_krypto_sichern();
}

void krypto_status(void) {
    kprint("Verschluesselung: ");
    if (!synfs_is_mounted()) {
        if (gesperrt[0]) { kprint("GESPERRT ("); kprint(gesperrt); kprint(") - entsperren mit: krypto entsperren\n"); }
        else kprint("kein SynFS eingebunden\n");
        return;
    }
    struct synfs_krypto* h = synfs_krypto_hdr();
    if (h->magic != SYNFS_KRY_MAGIC) { kprint("aus - einschalten mit: krypto aktivieren\n"); return; }
    kprint("an, entsperrt (256-Bit, Feistel mit ChaCha20/HMAC-SHA-256, Passwort ueber PBKDF2 ");
    kprint_dec(h->iter); kprint(" Runden)\n");
    if (h->fortschritt < synfs_ende()) { kprint("  UNVOLLSTAENDIG: "); kprint_dec((uint32_t)((uint64_t)h->fortschritt * 100 / synfs_ende())); kprint(" % verschluesselt - weiter mit: krypto fortsetzen\n"); }
    else kprint("  alle Sektoren verschluesselt (nur der Superblock mit Name und Groesse bleibt lesbar)\n");
}

int krypto_aktiv_fuer_aktuelles(void) { return K.geladen && krypto_aktiv_fuer(block_get()->name); }
int krypto_ableiten(const char* label, uint8_t out[32]) { if (!K.geladen) return -1; hmac_sha256(K.rk[0], 32, (const uint8_t*)label, slen(label), out); return 0; }
