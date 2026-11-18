/* Zynex OS - X.509: Parser, Signaturpruefung, Hostnamenvergleich, Kettenpruefung */
#include "x509.h"
#include "crypto.h"
#pragma GCC diagnostic ignored "-Wmisleading-indentation"

extern const uint32_t ca_root_count;
extern const uint32_t ca_root_tab[][2];
extern const uint8_t ca_root_blob[];

static void mc(void* d, const void* s, uint32_t n) { uint8_t* a = d; const uint8_t* b = s; for (uint32_t i = 0; i < n; i++) a[i] = b[i]; }
static int  eq(const uint8_t* a, uint32_t al, const uint8_t* b, uint32_t bl) { if (al != bl) return 0; for (uint32_t i = 0; i < al; i++) if (a[i] != b[i]) return 0; return 1; }

/* TLV lesen: start = Anfang inkl. Header, v/l = Inhalt, next = Ende */
struct tlv { const uint8_t *start, *v, *next; uint32_t l; uint8_t tag; };
static int rd(const uint8_t* p, const uint8_t* e, struct tlv* t) {
    if (p + 2 > e) return -1;
    t->start = p; t->tag = p[0]; uint32_t n = p[1]; p += 2;
    if (n & 0x80) { int k = n & 0x7F; if (k < 1 || k > 3 || p + k > e) return -1; n = 0; while (k--) n = (n << 8) | *p++; }
    if (p + n > e || p + n < p) return -1;
    t->v = p; t->l = n; t->next = p + n;
    return 0;
}

static uint32_t parse_time(const struct tlv* t) {
    const uint8_t* s = t->v; uint32_t l = t->l, y, off;
    if (t->tag == 0x17 && l >= 12) { y = (uint32_t)((s[0] - '0') * 10 + (s[1] - '0')); y += y < 50 ? 2000 : 1900; off = 2; }
    else if (t->tag == 0x18 && l >= 14) { y = (uint32_t)((s[0] - '0') * 1000 + (s[1] - '0') * 100 + (s[2] - '0') * 10 + (s[3] - '0')); off = 4; }
    else return 0;
    uint32_t m = (uint32_t)((s[off] - '0') * 10 + (s[off + 1] - '0')), d = (uint32_t)((s[off + 2] - '0') * 10 + (s[off + 3] - '0'));
    return y * 10000 + m * 100 + d;
}

int x509_parse(const uint8_t* der, uint32_t len, struct xcert* c) {
    struct tlv a, b, t; const uint8_t* end = der + len;
    for (uint32_t i = 0; i < sizeof(*c); i++) ((uint8_t*)c)[i] = 0;
    c->der = der; c->derlen = len;
    if (rd(der, end, &a) || a.tag != 0x30) return -1;
    const uint8_t* ce = a.v + a.l;
    if (rd(a.v, ce, &b) || b.tag != 0x30) return -1;                         /* tbsCertificate */
    c->tbs = b.start; c->tbslen = (uint32_t)(b.next - b.start);
    const uint8_t* p = b.v; const uint8_t* te = b.next;
    if (rd(p, te, &t)) return -1;
    if (t.tag == 0xA0) p = t.next;
    if (rd(p, te, &t) || t.tag != 0x02) return -1; p = t.next;               /* Seriennummer */
    if (rd(p, te, &t) || t.tag != 0x30) return -1; p = t.next;               /* Signaturalgorithmus (innen) */
    if (rd(p, te, &t) || t.tag != 0x30) return -1; c->issuer = t.start; c->issuerlen = (uint32_t)(t.next - t.start); p = t.next;
    if (rd(p, te, &t) || t.tag != 0x30) return -1;                           /* Gueltigkeit */
    { struct tlv v1, v2; if (rd(t.v, t.next, &v1) || rd(v1.next, t.next, &v2)) return -1; c->notbefore = parse_time(&v1); c->notafter = parse_time(&v2); }
    p = t.next;
    if (rd(p, te, &t) || t.tag != 0x30) return -1; c->subject = t.start; c->subjectlen = (uint32_t)(t.next - t.start); p = t.next;
    if (rd(p, te, &t) || t.tag != 0x30) return -1;                           /* SubjectPublicKeyInfo */
    {
        struct tlv alg, oid, bits;
        const uint8_t* se = t.next;
        if (rd(t.v, se, &alg) || alg.tag != 0x30 || rd(alg.v, alg.next, &oid) || oid.tag != 0x06) return -1;
        if (rd(alg.next, se, &bits) || bits.tag != 0x03 || bits.l < 2) return -1;
        static const uint8_t rsa[] = {0x2a,0x86,0x48,0x86,0xf7,0x0d,0x01,0x01,0x01};
        static const uint8_t ecp[] = {0x2a,0x86,0x48,0xce,0x3d,0x02,0x01};
        static const uint8_t p256[] = {0x2a,0x86,0x48,0xce,0x3d,0x03,0x01,0x07};
        static const uint8_t p384[] = {0x2b,0x81,0x04,0x00,0x22};
        if (eq(oid.v, oid.l, rsa, 9)) {
            struct tlv seq, ni, ei;
            if (rd(bits.v + 1, bits.next, &seq) || seq.tag != 0x30 || rd(seq.v, seq.next, &ni) || ni.tag != 0x02 || rd(ni.next, seq.next, &ei) || ei.tag != 0x02) return -1;
            c->keytype = 1; c->n = ni.v; c->nl = ni.l; c->e = ei.v; c->el = ei.l;
        } else if (eq(oid.v, oid.l, ecp, 7)) {
            struct tlv cur;
            if (rd(oid.next, alg.next, &cur) || cur.tag != 0x06) return -1;
            c->q = bits.v + 1; c->ql = bits.l - 1;
            c->keytype = eq(cur.v, cur.l, p256, 8) ? 2 : eq(cur.v, cur.l, p384, 5) ? 3 : 0;
        }
        p = se;
    }
    while (p < te) {                                                          /* optionale Felder, Erweiterungen */
        if (rd(p, te, &t)) return -1; p = t.next;
        if (t.tag != 0xA3) continue;
        struct tlv exts, ex;
        if (rd(t.v, t.next, &exts) || exts.tag != 0x30) return -1;
        for (const uint8_t* q = exts.v; q < exts.next; ) {
            if (rd(q, exts.next, &ex) || ex.tag != 0x30) return -1; q = ex.next;
            struct tlv oid, nx, val;
            if (rd(ex.v, ex.next, &oid) || oid.tag != 0x06) return -1;
            if (rd(oid.next, ex.next, &nx)) return -1;
            if (nx.tag == 0x01) { if (rd(nx.next, ex.next, &val)) return -1; } else val = nx;
            if (val.tag != 0x04) continue;
            static const uint8_t san[] = {0x55,0x1d,0x11}, bc[] = {0x55,0x1d,0x13};
            if (eq(oid.v, oid.l, san, 3)) { c->san = val.v; c->sanlen = val.l; }
            else if (eq(oid.v, oid.l, bc, 3)) {
                c->has_bc = 1; struct tlv s, f;
                if (!rd(val.v, val.next, &s) && s.tag == 0x30 && !rd(s.v, s.next, &f) && f.tag == 0x01 && f.l == 1) c->ca = f.v[0] != 0;
            }
        }
    }
    if (rd(b.next, ce, &t) || t.tag != 0x30) return -1;                       /* signatureAlgorithm */
    { struct tlv o; if (rd(t.v, t.next, &o) || o.tag != 0x06) return -1; c->sigoid = o.v; c->sigoidlen = o.l; }
    if (rd(t.next, ce, &t) || t.tag != 0x03 || t.l < 2) return -1;            /* signatureValue */
    c->sig = t.v + 1; c->siglen = t.l - 1;
    return 0;
}

static int hash_of(const uint8_t* d, uint32_t n, uint32_t hl, uint8_t* out) {
    if (hl == 32) sha256(d, n, out); else if (hl == 48) sha384(d, n, out); else if (hl == 64) sha512(d, n, out); else return -1;
    return 0;
}

/* 0 = Signatur von c wurde mit dem Schluessel von iss erstellt */
static int cert_sig_ok(const struct xcert* c, const struct xcert* iss) {
    static const uint8_t r256[] = {0x2a,0x86,0x48,0x86,0xf7,0x0d,0x01,0x01,0x0b}, r384[] = {0x2a,0x86,0x48,0x86,0xf7,0x0d,0x01,0x01,0x0c}, r512[] = {0x2a,0x86,0x48,0x86,0xf7,0x0d,0x01,0x01,0x0d};
    static const uint8_t e256[] = {0x2a,0x86,0x48,0xce,0x3d,0x04,0x03,0x02}, e384[] = {0x2a,0x86,0x48,0xce,0x3d,0x04,0x03,0x03}, e512[] = {0x2a,0x86,0x48,0xce,0x3d,0x04,0x03,0x04};
    uint32_t hl = 0; int rsa = 0;
    if (eq(c->sigoid, c->sigoidlen, r256, 9)) { hl = 32; rsa = 1; } else if (eq(c->sigoid, c->sigoidlen, r384, 9)) { hl = 48; rsa = 1; } else if (eq(c->sigoid, c->sigoidlen, r512, 9)) { hl = 64; rsa = 1; }
    else if (eq(c->sigoid, c->sigoidlen, e256, 8)) hl = 32; else if (eq(c->sigoid, c->sigoidlen, e384, 8)) hl = 48; else if (eq(c->sigoid, c->sigoidlen, e512, 8)) hl = 64;
    if (!hl) return -1;
    uint8_t h[64]; hash_of(c->tbs, c->tbslen, hl, h);
    if (rsa) return iss->keytype == 1 ? rsa_pkcs1_verify(iss->n, iss->nl, iss->e, iss->el, c->sig, c->siglen, h, hl) : -2;
    if (iss->keytype == 2) return ecdsa_verify(256, iss->q, iss->ql, h, hl, c->sig, c->siglen);
    if (iss->keytype == 3) return ecdsa_verify(384, iss->q, iss->ql, h, hl, c->sig, c->siglen);
    return -2;
}

int x509_verify_tls(const struct xcert* c, uint16_t alg, const uint8_t* msg, uint32_t ml, const uint8_t* sig, uint32_t sl) {
    uint8_t h[64];
    if (alg == 0x0804) { if (c->keytype != 1) return -2; sha256(msg, ml, h); return rsa_pss_verify(c->n, c->nl, c->e, c->el, sig, sl, h); }
    if (alg == 0x0403) { if (c->keytype != 2) return -2; sha256(msg, ml, h); return ecdsa_verify(256, c->q, c->ql, h, 32, sig, sl); }
    if (alg == 0x0503) { if (c->keytype != 3) return -2; sha384(msg, ml, h); return ecdsa_verify(384, c->q, c->ql, h, 48, sig, sl); }
    if (alg == 0x0401) { if (c->keytype != 1) return -2; sha256(msg, ml, h); return rsa_pkcs1_verify(c->n, c->nl, c->e, c->el, sig, sl, h, 32); }
    if (alg == 0x0501) { if (c->keytype != 1) return -2; sha384(msg, ml, h); return rsa_pkcs1_verify(c->n, c->nl, c->e, c->el, sig, sl, h, 48); }
    return -1;
}

/* ---- Hostname ---- */
static int lc(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }
static int dns_match(const uint8_t* pat, uint32_t pl, const char* host, uint32_t hl) {
    if (pl > 2 && pat[0] == '*' && pat[1] == '.') {
        uint32_t i = 0; while (i < hl && host[i] != '.') i++;
        if (i == 0 || i >= hl) return 0;
        host += i; hl -= i; pat += 1; pl -= 1;                              /* ".example.com" vergleichen */
    }
    if (pl != hl) return 0;
    for (uint32_t i = 0; i < pl; i++) if (lc(pat[i]) != lc((uint8_t)host[i])) return 0;
    return 1;
}
static int host_ip(const char* h, uint8_t ip[4]) {
    uint32_t v = 0; int parts = 0, digs = 0;
    for (;; h++) {
        if (*h >= '0' && *h <= '9') { v = v * 10 + (uint32_t)(*h - '0'); digs++; if (v > 255) return 0; }
        else if (*h == '.' || *h == 0) { if (!digs || parts > 3) return 0; ip[parts++] = (uint8_t)v; v = 0; digs = 0; if (!*h) break; }
        else return 0;
    }
    return parts == 4;
}
static int host_ok(const struct xcert* c, const char* host) {
    uint32_t hl = 0; while (host[hl]) hl++;
    uint8_t ip[4]; int isip = host_ip(host, ip);
    struct tlv s, g;
    if (!c->san || rd(c->san, c->san + c->sanlen, &s) || s.tag != 0x30) return 0;
    for (const uint8_t* p = s.v; p < s.next; ) {
        if (rd(p, s.next, &g)) return 0; p = g.next;
        if (!isip && g.tag == 0x82 && dns_match(g.v, g.l, host, hl)) return 1;
        if (isip && g.tag == 0x87 && g.l == 4 && eq(g.v, 4, ip, 4)) return 1;
    }
    return 0;
}

/* ---- Vertrauensspeicher ---- */
#define EXTRA 4
static uint8_t extra_der[EXTRA][3072]; static uint32_t extra_len[EXTRA]; static int extra_n = 0;
int x509_add_root(const uint8_t* der, uint32_t len) {
    struct xcert c;
    if (len > sizeof(extra_der[0]) || extra_n >= EXTRA || x509_parse(der, len, &c)) return -1;
    mc(extra_der[extra_n], der, len); extra_len[extra_n++] = len; return 0;
}
uint32_t x509_root_count(void) { return ca_root_count + (uint32_t)extra_n; }

/* sucht Root mit Subjekt == name; liefert den n-ten Treffer (n ab 0) */
static int find_root(const uint8_t* name, uint32_t nl, int nth, struct xcert* out) {
    for (uint32_t i = 0; i < ca_root_count + (uint32_t)extra_n; i++) {
        const uint8_t* d; uint32_t l;
        if (i < ca_root_count) { d = ca_root_blob + ca_root_tab[i][0]; l = ca_root_tab[i][1]; }
        else { d = extra_der[i - ca_root_count]; l = extra_len[i - ca_root_count]; }
        if (x509_parse(d, l, out)) continue;
        if (eq(out->subject, out->subjectlen, name, nl) && nth-- == 0) return 0;
    }
    return -1;
}

#define MAXCH 8
int x509_chain_verify(const uint8_t* list, uint32_t ll, const char* host, uint32_t today, const char** err) {
    static struct xcert ch[MAXCH]; int n = 0;
    for (uint32_t p = 0; p + 3 <= ll; ) {
        uint32_t cl = ((uint32_t)list[p] << 16) | ((uint32_t)list[p + 1] << 8) | list[p + 2]; p += 3;
        if (p + cl + 2 > ll) { *err = "Zertifikatsliste defekt"; return -1; }
        if (n < MAXCH) { if (x509_parse(list + p, cl, &ch[n])) { *err = "Zertifikat nicht lesbar"; return -1; } n++; }
        p += cl; uint32_t el = ((uint32_t)list[p] << 8) | list[p + 1]; p += 2 + el;
    }
    if (n == 0) { *err = "kein Zertifikat"; return -1; }
    if (!host_ok(&ch[0], host)) { *err = "Zertifikat gehoert nicht zu diesem Servernamen"; return -2; }
    if (today) for (int i = 0; i < n; i++) if (today < ch[i].notbefore || today > ch[i].notafter) { *err = "Zertifikat abgelaufen oder noch nicht gueltig"; return -3; }
    static struct xcert root;
    for (int i = 0; i < n; i++) {
        for (int nth = 0; ; nth++) {                                          /* Aussteller im Vertrauensspeicher? */
            if (find_root(ch[i].issuer, ch[i].issuerlen, nth, &root)) break;
            if (today && (today < root.notbefore || today > root.notafter)) continue;
            if (cert_sig_ok(&ch[i], &root) == 0) return 0;
        }
        if (i + 1 < n && eq(ch[i + 1].subject, ch[i + 1].subjectlen, ch[i].issuer, ch[i].issuerlen)) {
            if (!ch[i + 1].has_bc || !ch[i + 1].ca) { *err = "Zwischenzertifikat ist keine CA"; return -4; }
            if (cert_sig_ok(&ch[i], &ch[i + 1]) != 0) { *err = "Signatur im Zertifikatspfad ungueltig"; return -5; }
            continue;
        }
        break;
    }
    *err = "Aussteller ist keiner vertrauten Root-CA bekannt";
    return -6;
}
