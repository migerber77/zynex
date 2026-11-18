/*
 * Zynex OS - TLS 1.3 / 1.2 Client
 * TLS 1.3: TLS_AES_128_GCM_SHA256, TLS_CHACHA20_POLY1305_SHA256; TLS 1.2: ECDHE (X25519) mit AES-128-GCM oder ChaCha20-Poly1305 (RSA/ECDSA); Server-Zertifikate mit RSA- oder ECDSA-Schluessel (P-256/P-384).
 * Geprueft werden: Finished-MACs, CertificateVerify, Zertifikatskette bis zu einer eingebauten Root-CA,
 * Servername (SAN) und Gueltigkeitsdatum; alternativ der SHA-256-Fingerabdruck des Zertifikats (Pinning).
 */
#include "tls.h"
#include "crypto.h"
#include "timer.h"
#include "x509.h"

extern void kprint(const char* str);
extern void kprint_dec(uint32_t val);
extern void kputchar(char c);

/* aus net.c */
extern int  tcp_open(uint32_t ip, uint16_t port);
extern int  tcp_put(const uint8_t* d, int n);
extern uint32_t tcp_have(void);
extern uint8_t* tcp_data(void);
extern void tcp_drop(uint32_t n);
extern int  tcp_eof(void);
extern void tcp_shut(void);
extern void net_poll(void);

static void mc(void* d, const void* s, uint32_t n) { uint8_t* a = d; const uint8_t* b = s; for (uint32_t i = 0; i < n; i++) a[i] = b[i]; }
static void put16(uint8_t* p, uint16_t v) { p[0] = v >> 8; p[1] = (uint8_t)v; }
static uint16_t get16(const uint8_t* p) { return (uint16_t)((p[0] << 8) | p[1]); }
static uint32_t get24(const uint8_t* p) { return ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2]; }

struct prot { uint8_t key[32], iv[12]; uint64_t seq; int on; };
static struct prot rk, wk;
static uint16_t suite;
static int v12;
static int is_gcm(void) { return suite == 0x1301 || suite == 0xc02b || suite == 0xc02f; }
static struct sha256 tr;
static uint8_t rec[16700], sendbuf[16700], hsbuf[24000], certbuf[16384], outbuf[65536];
static uint32_t hsl, certlen, certlistlen;
static struct xcert leaf;

static int rdrand64(uint64_t* v) {
    for (int i = 0; i < 20; i++) { unsigned char ok; __asm__ volatile ("rdrand %0; setc %1" : "=r"(*v), "=qm"(ok) : : "cc"); if (ok) return 0; }
    return -1;
}
static int has_rdrand(void) {
    uint32_t a = 1, b, c = 0, d;
    __asm__ volatile ("cpuid" : "+a"(a), "=b"(b), "+c"(c), "=d"(d));
    return (c >> 30) & 1;
}
static int zufall(uint8_t* p, int n) {
    for (int i = 0; i < n; i += 8) { uint64_t v; if (rdrand64(&v)) return -1; for (int k = 0; k < 8 && i + k < n; k++) p[i + k] = (uint8_t)(v >> (8 * k)); }
    return 0;
}

static void set_keys(struct prot* p, const uint8_t secret[32]) {
    hkdf_expand_label(secret, "key", 0, 0, p->key, is_gcm() ? 16 : 32);
    hkdf_expand_label(secret, "iv", 0, 0, p->iv, 12);
    p->seq = 0; p->on = 1;
}
static void nonce_of(const struct prot* p, uint8_t n[12]) {
    for (int i = 0; i < 12; i++) n[i] = p->iv[i];
    for (int i = 0; i < 8; i++) n[11 - i] ^= (uint8_t)(p->seq >> (8 * i));
}
static void t_seal(const struct prot* p, const uint8_t n[12], const uint8_t* aad, uint32_t al, const uint8_t* pt, uint32_t pl, uint8_t* ct) {
    if (is_gcm()) gcm_seal(p->key, n, aad, al, pt, pl, ct); else aead_seal(p->key, n, aad, al, pt, pl, ct);
}
static int t_open(const struct prot* p, const uint8_t n[12], const uint8_t* aad, uint32_t al, const uint8_t* ct, uint32_t cl, uint8_t* pt) {
    return is_gcm() ? gcm_open(p->key, n, aad, al, ct, cl, pt) : aead_open(p->key, n, aad, al, ct, cl, pt);
}
static void thash(uint8_t out[32]) { struct sha256 c = tr; sha256_final(&c, out); }

static int warte(uint32_t n) {
    uint32_t t0 = timer_get_ticks();
    while (tcp_have() < n) {
        if (tcp_eof() && tcp_have() < n) return -1;
        if (timer_get_ticks() - t0 > 1000) return -2;
        net_poll();
    }
    return 0;
}

static int send_rec(uint8_t type, const uint8_t* d, uint32_t n) {
    uint32_t l;
    if (!wk.on) {
        sendbuf[0] = type; sendbuf[1] = 3; sendbuf[2] = v12 ? 3 : 1; put16(sendbuf + 3, (uint16_t)n);
        mc(sendbuf + 5, d, n); l = 5 + n;
    } else if (v12) {
        uint8_t aad[13], nonce[12];
        for (int i = 0; i < 8; i++) aad[i] = (uint8_t)(wk.seq >> (56 - 8 * i));
        aad[8] = type; aad[9] = 3; aad[10] = 3; put16(aad + 11, (uint16_t)n);
        sendbuf[0] = type; sendbuf[1] = 3; sendbuf[2] = 3;
        if (is_gcm()) {
            mc(nonce, wk.iv, 4); mc(nonce + 4, aad, 8);
            mc(sendbuf + 5, aad, 8);
            gcm_seal(wk.key, nonce, aad, 13, d, n, sendbuf + 13);
            l = 5 + 8 + n + 16;
        } else {
            nonce_of(&wk, nonce);
            aead_seal(wk.key, nonce, aad, 13, d, n, sendbuf + 5);
            l = 5 + n + 16;
        }
        put16(sendbuf + 3, (uint16_t)(l - 5)); wk.seq++;
    } else {
        static uint8_t inner[16500];
        mc(inner, d, n); inner[n] = type;
        uint32_t cl = n + 1 + 16;
        sendbuf[0] = 23; sendbuf[1] = 3; sendbuf[2] = 3; put16(sendbuf + 3, (uint16_t)cl);
        uint8_t nonce[12]; nonce_of(&wk, nonce);
        t_seal(&wk, nonce, sendbuf, 5, inner, n + 1, sendbuf + 5);
        wk.seq++; l = 5 + cl;
    }
    return tcp_put(sendbuf, (int)l);
}

/* liest einen Record; liefert Typ (TLS 1.3 verschluesselt: innerer Typ) und Inhalt in rec[] */
static int read_rec(uint8_t* type, uint32_t* len) {
    if (warte(5) != 0) return -1;
    uint8_t* h = tcp_data(); uint8_t t = h[0]; uint32_t l = get16(h + 3);
    if (l > 16640) return -2;
    if (warte(5 + l) != 0) return -1;
    h = tcp_data();
    if (v12 && rk.on && t != 20) {
        uint8_t aad[13], nonce[12]; uint32_t pl; int r;
        for (int i = 0; i < 8; i++) aad[i] = (uint8_t)(rk.seq >> (56 - 8 * i));
        aad[8] = t; aad[9] = 3; aad[10] = 3;
        if (is_gcm()) {
            if (l < 24) { tcp_drop(5 + l); return -3; }
            pl = l - 24; put16(aad + 11, (uint16_t)pl);
            mc(nonce, rk.iv, 4); mc(nonce + 4, h + 5, 8);
            r = gcm_open(rk.key, nonce, aad, 13, h + 13, l - 8, rec);
        } else {
            if (l < 16) { tcp_drop(5 + l); return -3; }
            pl = l - 16; put16(aad + 11, (uint16_t)pl);
            nonce_of(&rk, nonce);
            r = aead_open(rk.key, nonce, aad, 13, h + 5, l, rec);
        }
        if (r != 0) { tcp_drop(5 + l); return -3; }
        rk.seq++; *type = t; *len = pl;
    } else if (t == 23 && rk.on) {
        uint8_t nonce[12]; nonce_of(&rk, nonce);
        if (t_open(&rk, nonce, h, 5, h + 5, l, rec) != 0) { tcp_drop(5 + l); return -3; }
        rk.seq++;
        uint32_t pl = l - 16;
        while (pl && rec[pl - 1] == 0) pl--;
        if (!pl) { tcp_drop(5 + l); return -4; }
        *type = rec[pl - 1]; *len = pl - 1;
    } else {
        mc(rec, h + 5, l); *type = t; *len = l;
    }
    tcp_drop(5 + l);
    return 0;
}

static void alert_text(const uint8_t* d, uint32_t n) {
    kprint("TLS-Alarm vom Server (Code "); kprint_dec(n >= 2 ? d[1] : 0); kprint(")\n");
}

/* ---- Hardware-Uhr (CMOS) ---- */
static inline void outb_(uint16_t p, uint8_t v) { __asm__ volatile ("outb %0, %1" : : "a"(v), "Nd"(p)); }
static inline uint8_t inb_(uint16_t p) { uint8_t v; __asm__ volatile ("inb %1, %0" : "=a"(v) : "Nd"(p)); return v; }
static uint8_t cmos(uint8_t r) { outb_(0x70, r); return inb_(0x71); }
static uint32_t heute(void) {                       /* JJJJMMTT oder 0, wenn die Uhr unplausibel ist */
    for (int i = 0; i < 100000 && (cmos(0x0A) & 0x80); i++) {}
    uint8_t d = cmos(7), m = cmos(8), y = cmos(9), b = cmos(0x0B);
    if (!(b & 4)) { d = (uint8_t)((d & 15) + (d >> 4) * 10); m = (uint8_t)((m & 15) + (m >> 4) * 10); y = (uint8_t)((y & 15) + (y >> 4) * 10); }
    uint32_t jahr = 2000u + y;
    if (jahr < 2025 || m < 1 || m > 12 || d < 1 || d > 31) return 0;
    return jahr * 10000 + (uint32_t)m * 100 + d;
}

static int fp_gleich(const uint8_t h[32], const char* fp) {
    uint8_t w[32]; int n = 0;
    for (; *fp; fp++) {
        int v;
        if (*fp >= '0' && *fp <= '9') v = *fp - '0'; else if (*fp >= 'a' && *fp <= 'f') v = *fp - 'a' + 10;
        else if (*fp >= 'A' && *fp <= 'F') v = *fp - 'A' + 10; else if (*fp == ':' || *fp == ' ') continue; else return 0;
        if (n >= 64) return 0;
        if (n & 1) w[n / 2] |= (uint8_t)v; else w[n / 2] = (uint8_t)(v << 4);
        n++;
    }
    if (n != 64) return 0;
    for (int i = 0; i < 32; i++) if (w[i] != h[i]) return 0;
    return 1;
}
static void print_hex(const uint8_t* p, int n) {
    static const char* h = "0123456789abcdef";
    for (int i = 0; i < n; i++) { kputchar(h[p[i] >> 4]); kputchar(h[p[i] & 15]); }
}

/* Fingerabdruck- oder Kettenpruefung; 0 = ok */
static int zert_pruefen(const char* host, const char* fp_hex, const uint8_t fpr[32]) {
    kprint("TLS: Zertifikat-Fingerabdruck (SHA-256): "); print_hex(fpr, 32); kputchar('\n');
    if (fp_hex && *fp_hex) {
        if (!fp_gleich(fpr, fp_hex)) { kprint("TLS: Fingerabdruck stimmt NICHT - Verbindung abgebrochen\n"); tcp_shut(); return -9; }
        kprint("TLS: Fingerabdruck stimmt, Server ist authentisch\n");
    } else {
        const char* err = 0; uint32_t d = heute();
        if (x509_chain_verify(certbuf, certlistlen, host, d, &err) != 0) {
            kprint("TLS: Zertifikat NICHT vertrauenswuerdig: "); kprint(err); kprint("\nTLS: Verbindung abgebrochen (oder Fingerabdruck angeben)\n");
            tcp_shut(); return -9;
        }
        kprint("TLS: Zertifikat vertrauenswuerdig (Kette bis zur Root-CA geprueft, Servername stimmt)\n");
        if (!d) kprint("TLS: Hinweis - Hardware-Uhr unplausibel, Gueltigkeitsdatum nicht geprueft\n");
    }
    return 0;
}

/* ---- TLS 1.2 ---- */
static void prf(const uint8_t* sec, uint32_t sl, const char* label, const uint8_t* seed, uint32_t seedl, uint8_t* out, uint32_t n) {
    uint8_t ls[96], a[32], t[32], in[32 + 96]; uint32_t ll = 0, lsl;
    while (label[ll]) { ls[ll] = (uint8_t)label[ll]; ll++; }
    mc(ls + ll, seed, seedl); lsl = ll + seedl;
    hmac_sha256(sec, sl, ls, lsl, a);
    for (uint32_t off = 0; off < n; off += 32) {
        mc(in, a, 32); mc(in + 32, ls, lsl);
        hmac_sha256(sec, sl, in, 32 + lsl, t);
        uint32_t k = n - off < 32 ? n - off : 32;
        mc(out + off, t, k);
        hmac_sha256(sec, sl, a, 32, t); mc(a, t, 32);
    }
}

#define F12(msg) do { kprint(msg); tcp_shut(); return -7; } while (0)

static int tls12_hs(const char* host, const char* fp_hex, const uint8_t crnd[32], const uint8_t srnd[32],
                    const uint8_t priv[32], const uint8_t pub[32], const uint8_t priv2[32], const uint8_t pub2[65]) {
    uint8_t type; uint32_t rl;
    int got_cert = 0, got_ske = 0, done = 0;
    uint8_t spub[65], fpr[32]; uint32_t spl = 0, grp12 = 0;
    certlistlen = 0;
    for (;;) {
        while (hsl >= 4 && !done) {
            uint8_t mt = hsbuf[0]; uint32_t ml = get24(hsbuf + 1);
            if (hsl < 4 + ml) break;
            if (mt == 11) {
                if (ml < 6) F12("TLS: Zertifikat defekt\n");
                uint32_t listlen = get24(hsbuf + 4); if (listlen + 3 > ml) F12("TLS: Zertifikat defekt\n");
                uint32_t q = 7, o = 0, end = 7 + listlen; int first = 1;
                while (q + 3 <= end) {
                    uint32_t cl = get24(hsbuf + q);
                    if (q + 3 + cl > end || o + cl + 5 > sizeof(certbuf)) F12("TLS: Zertifikat zu gross oder defekt\n");
                    uint32_t at = o;
                    mc(certbuf + o, hsbuf + q, cl + 3); o += cl + 3; certbuf[o++] = 0; certbuf[o++] = 0;
                    if (first) {                                   /* leaf zeigt in certbuf, nicht in hsbuf (wird verschoben) */
                        sha256(certbuf + at + 3, cl, fpr);
                        if (x509_parse(certbuf + at + 3, cl, &leaf)) F12("TLS: Zertifikat nicht lesbar\n");
                        first = 0;
                    }
                    q += 3 + cl;
                }
                if (first) F12("TLS: kein Zertifikat\n");
                certlistlen = o; got_cert = 1;
            } else if (mt == 12) {
                if (!got_cert || ml < 8) F12("TLS: Schluesselaustausch ohne Zertifikat\n");
                uint32_t pl = hsbuf[7];
                if (hsbuf[4] != 3 || (get16(hsbuf + 5) == 0x1d ? pl != 32 : (get16(hsbuf + 5) != 0x17 || pl != 65))) F12("TLS: Server nutzt keine unterstuetzte Kurve (X25519 oder P-256)\n");
                grp12 = get16(hsbuf + 5); spl = pl;
                uint32_t po = 4 + 4 + pl;                          /* Offset der Signaturangabe */
                if (po + 4 > 4 + ml) F12("TLS: Schluesselaustausch defekt\n");
                uint16_t alg = get16(hsbuf + po), sl = get16(hsbuf + po + 2);
                if (po + 4 + sl > 4 + ml) F12("TLS: Schluesselaustausch defekt\n");
                if (alg != 0x0804 && alg != 0x0403 && alg != 0x0503 && alg != 0x0401 && alg != 0x0501) F12("TLS: Signaturverfahren nicht unterstuetzt\n");
                uint8_t msg[64 + 69]; mc(msg, crnd, 32); mc(msg + 32, srnd, 32); mc(msg + 64, hsbuf + 4, 4 + pl);
                if (x509_verify_tls(&leaf, alg, msg, 64 + 4 + pl, hsbuf + po + 4, sl) != 0) { kprint("TLS: Signatur des Servers ist UNGUELTIG - Verbindung abgebrochen\n"); tcp_shut(); return -8; }
                mc(spub, hsbuf + 8, pl); got_ske = 1;
            } else if (mt == 13) {
                F12("TLS: Server verlangt ein Client-Zertifikat (nicht unterstuetzt)\n");
            } else if (mt == 14) {
                done = 1;
            }
            sha256_update(&tr, hsbuf, 4 + ml);
            for (uint32_t i = 0; i + 4 + ml < hsl; i++) hsbuf[i] = hsbuf[i + 4 + ml];
            hsl -= 4 + ml;
        }
        if (done) break;
        if (read_rec(&type, &rl) != 0) { kprint("TLS: Handshake abgebrochen\n"); tcp_shut(); return -6; }
        if (type == 21) { alert_text(rec, rl); tcp_shut(); return -6; }
        if (type != 22) F12("TLS: unerwarteter Record im Handshake\n");
        if (hsl + rl > sizeof(hsbuf)) F12("TLS: Handshake zu gross\n");
        mc(hsbuf + hsl, rec, rl); hsl += rl;
    }
    if (!got_cert || !got_ske) F12("TLS: Server hat sich nicht ausgewiesen\n");
    int zr = zert_pruefen(host, fp_hex, fpr); if (zr) return zr;

    uint8_t shared[32], seed[64], master[48], kb[96];
    if ((grp12 == 0x1d ? x25519_shared(shared, priv, spub) : p256_shared(priv2, spub, shared)) != 0) F12("TLS: ungueltiger Schluessel\n");
    mc(seed, crnd, 32); mc(seed + 32, srnd, 32);
    prf(shared, 32, "master secret", seed, 64, master, 48);
    mc(seed, srnd, 32); mc(seed + 32, crnd, 32);
    uint32_t kl = is_gcm() ? 16 : 32, il = is_gcm() ? 4 : 12;
    prf(master, 48, "key expansion", seed, 64, kb, 2 * kl + 2 * il);
    for (int i = 0; i < 32; i++) { wk.key[i] = 0; rk.key[i] = 0; }
    for (int i = 0; i < 12; i++) { wk.iv[i] = 0; rk.iv[i] = 0; }
    mc(wk.key, kb, kl); mc(rk.key, kb + kl, kl); mc(wk.iv, kb + 2 * kl, il); mc(rk.iv, kb + 2 * kl + il, il);
    wk.seq = rk.seq = 0; wk.on = 0; rk.on = 0;

    uint8_t cke[70]; uint32_t cl = 5 + spl;
    cke[0] = 16; cke[1] = 0; cke[2] = 0; cke[3] = (uint8_t)(1 + spl); cke[4] = (uint8_t)spl;
    if (grp12 == 0x1d) mc(cke + 5, pub, 32); else mc(cke + 5, pub2, 65);
    sha256_update(&tr, cke, cl);
    uint8_t one = 1;
    if (send_rec(22, cke, cl) || send_rec(20, &one, 1)) F12("TLS: Senden fehlgeschlagen\n");
    wk.on = 1;
    uint8_t th[32], fin[16];
    thash(th);
    fin[0] = 20; fin[1] = 0; fin[2] = 0; fin[3] = 12;
    prf(master, 48, "client finished", th, 32, fin + 4, 12);
    sha256_update(&tr, fin, 16);
    if (send_rec(22, fin, 16)) F12("TLS: Senden fehlgeschlagen\n");
    for (;;) {
        if (read_rec(&type, &rl) != 0) { kprint("TLS: Finished des Servers fehlt oder ist defekt\n"); tcp_shut(); return -6; }
        if (type == 20) { rk.on = 1; rk.seq = 0; continue; }
        if (type == 21) { alert_text(rec, rl); tcp_shut(); return -6; }
        if (type != 22 || !rk.on || rl != 16 || rec[0] != 20) F12("TLS: unerwarteter Record im Handshake\n");
        break;
    }
    uint8_t exp[12]; thash(th);
    prf(master, 48, "server finished", th, 32, exp, 12);
    uint8_t d = 0; for (int i = 0; i < 12; i++) d |= exp[i] ^ rec[4 + i];
    if (d) { kprint("TLS: Finished-Pruefung fehlgeschlagen\n"); tcp_shut(); return -8; }
    return 0;
}

int tls_get(uint32_t ip, uint16_t port, const char* host, const uint8_t* req, uint32_t reqlen,
            const char* fp_hex, const uint8_t** out, uint32_t* outlen, int* complete) {
    __asm__ volatile ("sti");
    *out = outbuf; *outlen = 0; *complete = 0;
    if (!has_rdrand()) { kprint("TLS: CPU hat keinen Zufallszahlengenerator (RDRAND), abgebrochen\n"); return -1; }
    rk.on = 0; wk.on = 0; hsl = 0; certlen = 0; v12 = 0;
    uint8_t priv[32], pub[32], rnd[32], sid[32], priv2[32], pub2[65];
    if (zufall(priv, 32) || zufall(rnd, 32) || zufall(sid, 32)) { kprint("TLS: Zufallszahlen nicht verfuegbar\n"); return -1; }
    x25519_pub(pub, priv);
    for (int versuch = 0; ; versuch++) {
        if (zufall(priv2, 32)) { kprint("TLS: Zufallszahlen nicht verfuegbar\n"); return -1; }
        if (p256_pub(priv2, pub2) == 0) break;
        if (versuch > 8) { kprint("TLS: Schluesselerzeugung fehlgeschlagen\n"); return -1; }
    }
    if (tcp_open(ip, port) != 0) { kprint("Verbindung fehlgeschlagen\n"); return -2; }

    /* ClientHello */
    uint8_t ch[640]; uint32_t n = 0;
    uint32_t hl = 0; while (host[hl]) hl++;
    ch[n++] = 1; n += 3;
    ch[n++] = 3; ch[n++] = 3; mc(ch + n, rnd, 32); n += 32;
    ch[n++] = 32; mc(ch + n, sid, 32); n += 32;
    { static const uint8_t cs[] = { 0,12, 0x13,0x01, 0x13,0x03, 0xc0,0x2b, 0xc0,0x2f, 0xcc,0xa9, 0xcc,0xa8 }; mc(ch + n, cs, sizeof(cs)); n += sizeof(cs); }
    ch[n++] = 1; ch[n++] = 0;
    uint32_t extlen_pos = n; n += 2;
    uint32_t ext0 = n;
    put16(ch + n, 0); put16(ch + n + 2, (uint16_t)(hl + 5)); put16(ch + n + 4, (uint16_t)(hl + 3)); ch[n + 6] = 0; put16(ch + n + 7, (uint16_t)hl); mc(ch + n + 9, host, hl); n += 9 + hl;
    static const uint8_t ex[] = { 0,10,0,8,0,6,0,0x1d,0,0x17,0,0x18,  0,11,0,2,1,0,  0,13,0,12,0,10,4,3,5,3,8,4,4,1,5,1,  0,0x2b,0,5,4,3,4,3,3,  0,0x33,0,107,0,105,0,0x1d,0,0x20 };
    mc(ch + n, ex, sizeof(ex)); n += sizeof(ex); mc(ch + n, pub, 32); n += 32;
    ch[n++] = 0; ch[n++] = 0x17; ch[n++] = 0; ch[n++] = 65; mc(ch + n, pub2, 65); n += 65;
    put16(ch + extlen_pos, (uint16_t)(n - ext0));
    ch[1] = 0; put16(ch + 2, (uint16_t)(n - 4));
    sha256_init(&tr); sha256_update(&tr, ch, n);
    if (send_rec(22, ch, n) != 0) { kprint("TLS: Senden fehlgeschlagen\n"); tcp_shut(); return -3; }

    /* ServerHello */
    uint8_t type; uint32_t rl;
    uint8_t spub[65], srnd[32]; int have_key = 0, grp = 0;
    for (;;) {
        if (read_rec(&type, &rl) != 0) { kprint("TLS: keine Antwort des Servers\n"); tcp_shut(); return -4; }
        if (type == 21) { alert_text(rec, rl); tcp_shut(); return -5; }
        if (type == 20) continue;
        if (type != 22 || rl < 42 || rec[0] != 2) { kprint("TLS: unerwartete Antwort\n"); tcp_shut(); return -5; }
        break;
    }
    {
        uint32_t ml = get24(rec + 1); if (ml + 4 > rl) { kprint("TLS: ServerHello defekt\n"); tcp_shut(); return -5; }
        static const uint8_t hrr[32] = {0xCF,0x21,0xAD,0x74,0xE5,0x9A,0x61,0x11,0xBE,0x1D,0x8C,0x02,0x1E,0x65,0xB8,0x91,0xC2,0xA2,0x11,0x16,0x7A,0xBB,0x8C,0x5E,0x07,0x9E,0x09,0xE2,0xC8,0xA8,0x33,0x9C};
        int gleich = 1; for (int i = 0; i < 32; i++) if (rec[6 + i] != hrr[i]) gleich = 0;
        if (gleich) { kprint("TLS: HelloRetryRequest wird nicht unterstuetzt\n"); tcp_shut(); return -5; }
        uint32_t p = 4 + 2 + 32; uint32_t sl = rec[p++]; p += sl;
        if (p + 5 > ml + 4) { kprint("TLS: ServerHello defekt\n"); tcp_shut(); return -5; }
        uint16_t cs = get16(rec + p); p += 2; p++;
        if (cs != 0x1303 && cs != 0x1301 && cs != 0xc02b && cs != 0xc02f && cs != 0xcca9 && cs != 0xcca8) { kprint("TLS: Server waehlt keine unterstuetzte Verschluesselung\n"); tcp_shut(); return -5; }
        suite = cs;
        uint32_t el = get16(rec + p); p += 2; uint32_t ee = p + el;
        int ver13 = 0;
        while (p + 4 <= ee && ee <= ml + 4) {
            uint16_t et = get16(rec + p), elen = get16(rec + p + 2); p += 4;
            if (et == 0x2b && elen == 2 && get16(rec + p) == 0x0304) ver13 = 1;
            if (et == 0x33 && elen == 36 && get16(rec + p) == 0x1d && get16(rec + p + 2) == 32) { mc(spub, rec + p + 4, 32); have_key = 1; grp = 0x1d; }
            if (et == 0x33 && elen == 69 && get16(rec + p) == 0x17 && get16(rec + p + 2) == 65) { mc(spub, rec + p + 4, 65); have_key = 1; grp = 0x17; }
            p += elen;
        }
        if (cs >= 0xc000) {
            if (ver13) { kprint("TLS: ungueltige Verschluesselung fuer TLS 1.3\n"); tcp_shut(); return -5; }
            v12 = 1;
            if (ml + 4 < rl) { mc(hsbuf, rec + ml + 4, rl - ml - 4); hsl = rl - ml - 4; }
        } else if (!ver13 || !have_key) { kprint("TLS: Server spricht kein TLS 1.3 mit X25519/P-256\n"); tcp_shut(); return -5; }
        mc(srnd, rec + 6, 32);
        sha256_update(&tr, rec, ml + 4);
    }
    if (v12) { int r12 = tls12_hs(host, fp_hex, rnd, srnd, priv, pub, priv2, pub2); if (r12) return r12; goto anfrage; }
    uint8_t shared[32];
    if ((grp == 0x1d ? x25519_shared(shared, priv, spub) : p256_shared(priv2, spub, shared)) != 0) { kprint("TLS: ungueltiger Schluessel\n"); tcp_shut(); return -5; }

    /* Schluesselableitung */
    uint8_t zeros[32] = {0}, early[32], empty[32], derived[32], hs[32], th[32], c_hs[32], s_hs[32];
    sha256("", 0, empty);
    hkdf_extract(0, 0, zeros, 32, early);
    hkdf_expand_label(early, "derived", empty, 32, derived, 32);
    hkdf_extract(derived, 32, shared, 32, hs);
    thash(th);
    hkdf_expand_label(hs, "c hs traffic", th, 32, c_hs, 32);
    hkdf_expand_label(hs, "s hs traffic", th, 32, s_hs, 32);
    set_keys(&rk, s_hs);

    /* verschluesselter Teil des Handshakes */
    int got_cert = 0, got_cv = 0, done = 0;
    uint8_t fpr[32];
    while (!done) {
        if (read_rec(&type, &rl) != 0) { kprint("TLS: Handshake abgebrochen (Entschluesselung fehlgeschlagen?)\n"); tcp_shut(); return -6; }
        if (type == 20) continue;
        if (type == 21) { alert_text(rec, rl); tcp_shut(); return -6; }
        if (type != 22) { kprint("TLS: unerwarteter Record im Handshake\n"); tcp_shut(); return -6; }
        if (hsl + rl > sizeof(hsbuf)) { kprint("TLS: Handshake zu gross\n"); tcp_shut(); return -6; }
        mc(hsbuf + hsl, rec, rl); hsl += rl;
        while (hsl >= 4 && !done) {
            uint8_t mt = hsbuf[0]; uint32_t ml = get24(hsbuf + 1);
            if (hsl < 4 + ml) break;
            if (mt == 11) {
                if (ml < 8 || hsbuf[4] != 0) { kprint("TLS: Zertifikat defekt\n"); tcp_shut(); return -7; }
                uint32_t listlen = get24(hsbuf + 5), cl = get24(hsbuf + 8);
                if (listlen + 4 > ml || cl + 3 > listlen || listlen > sizeof(certbuf)) { kprint("TLS: Zertifikat zu gross oder defekt\n"); tcp_shut(); return -7; }
                mc(certbuf, hsbuf + 8, listlen); certlistlen = listlen; certlen = cl;
                sha256(certbuf + 3, certlen, fpr);
                if (x509_parse(certbuf + 3, certlen, &leaf)) { kprint("TLS: Zertifikat nicht lesbar\n"); tcp_shut(); return -7; }
                got_cert = 1;
                sha256_update(&tr, hsbuf, 4 + ml);
            } else if (mt == 15) {
                if (!got_cert || ml < 4) { kprint("TLS: CertificateVerify ohne Zertifikat\n"); tcp_shut(); return -7; }
                uint16_t alg = get16(hsbuf + 4), sl = get16(hsbuf + 6);
                if ((alg != 0x0804 && alg != 0x0403 && alg != 0x0503) || (uint32_t)sl + 4 > ml) { kprint("TLS: Signaturverfahren nicht unterstuetzt\n"); tcp_shut(); return -7; }
                uint8_t cvh[32], msg[64 + 34 + 1 + 32];
                thash(cvh);
                for (int i = 0; i < 64; i++) msg[i] = 0x20;
                mc(msg + 64, "TLS 1.3, server CertificateVerify", 33); msg[97] = 0; mc(msg + 98, cvh, 32);
                if (x509_verify_tls(&leaf, alg, msg, 130, hsbuf + 8, sl) != 0) { kprint("TLS: Signatur des Servers ist UNGUELTIG - Verbindung abgebrochen\n"); tcp_shut(); return -8; }
                got_cv = 1;
                sha256_update(&tr, hsbuf, 4 + ml);
            } else if (mt == 20) {
                if (!got_cv || ml != 32) { kprint("TLS: Server hat sich nicht ausgewiesen\n"); tcp_shut(); return -7; }
                uint8_t fk[32], exp[32], h2[32];
                hkdf_expand_label(s_hs, "finished", 0, 0, fk, 32);
                thash(h2); hmac_sha256(fk, 32, h2, 32, exp);
                uint8_t d = 0; for (int i = 0; i < 32; i++) d |= exp[i] ^ hsbuf[4 + i];
                if (d) { kprint("TLS: Finished-Pruefung fehlgeschlagen\n"); tcp_shut(); return -8; }
                sha256_update(&tr, hsbuf, 4 + ml);
                done = 1;
            } else {
                sha256_update(&tr, hsbuf, 4 + ml);              /* EncryptedExtensions u.a. */
            }
            for (uint32_t i = 0; i + 4 + ml < hsl; i++) hsbuf[i] = hsbuf[i + 4 + ml];
            hsl -= 4 + ml;
        }
    }
    { int zr = zert_pruefen(host, fp_hex, fpr); if (zr) return zr; }

    /* Anwendungsschluessel + Client-Finished */
    uint8_t hsf[32], derived2[32], master[32], c_ap[32], s_ap[32], fk[32], vd[32], fin[36];
    thash(hsf);
    hkdf_expand_label(hs, "derived", empty, 32, derived2, 32);
    hkdf_extract(derived2, 32, zeros, 32, master);
    hkdf_expand_label(master, "c ap traffic", hsf, 32, c_ap, 32);
    hkdf_expand_label(master, "s ap traffic", hsf, 32, s_ap, 32);
    set_keys(&wk, c_hs);
    hkdf_expand_label(c_hs, "finished", 0, 0, fk, 32);
    hmac_sha256(fk, 32, hsf, 32, vd);
    fin[0] = 20; fin[1] = 0; fin[2] = 0; fin[3] = 32; mc(fin + 4, vd, 32);
    if (send_rec(22, fin, 36) != 0) { kprint("TLS: Senden fehlgeschlagen\n"); tcp_shut(); return -10; }
    set_keys(&wk, c_ap);
    set_keys(&rk, s_ap);

  anfrage:
    /* Anfrage senden */
    for (uint32_t off = 0; off < reqlen; ) {
        uint32_t k = reqlen - off > 8000 ? 8000 : reqlen - off;
        if (send_rec(23, req + off, k) != 0) { kprint("TLS: Senden fehlgeschlagen\n"); tcp_shut(); return -11; }
        off += k;
    }
    /* Antwort lesen */
    uint32_t t0 = timer_get_ticks(), letzte = t0;
    for (;;) {
        if (timer_get_ticks() - t0 > 1500 || timer_get_ticks() - letzte > 400) break;
        if (tcp_have() < 5) { if (tcp_eof()) break; net_poll(); continue; }
        int r = read_rec(&type, &rl);
        if (r != 0) break;
        letzte = timer_get_ticks();
        if (type == 23) {
            uint32_t k = rl; if (*outlen + k > sizeof(outbuf)) k = sizeof(outbuf) - *outlen;
            mc(outbuf + *outlen, rec, k); *outlen += k;
        } else if (type == 21) { if (rl >= 2 && rec[1] == 0) *complete = 1; else alert_text(rec, rl); break; }
    }
    tcp_shut();
    return 0;
}
