/* Zynex LLM-Motor, siehe include/llm.h */
#include "llm.h"

/* ---------- kleine Hilfen (keine libc) ---------- */
static int slen_(const char* s) { int n = 0; while (s[n]) n++; return n; }
static int mem_eq(const char* a, const char* b, uint32_t n) { for (uint32_t i = 0; i < n; i++) if (a[i] != b[i]) return 0; return 1; }
static void mem_zero(void* p, size_t n) { uint8_t* b = p; for (size_t i = 0; i < n; i++) b[i] = 0; }
static uint32_t fnv(const char* s, uint32_t n) { uint32_t h = 2166136261u; for (uint32_t i = 0; i < n; i++) { h ^= (uint8_t)s[i]; h *= 16777619u; } return h; }
static uint64_t rd64(const uint8_t* p) { uint64_t v = 0; for (int i = 7; i >= 0; i--) v = v << 8 | p[i]; return v; }
static uint32_t rd32(const uint8_t* p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static float rdf32(const uint8_t* p) { union { uint32_t u; float f; } c; c.u = rd32(p); return c.f; }
static int ends_with(const char* s, uint32_t n, const char* suf) { uint32_t m = (uint32_t)slen_(suf); return n >= m && mem_eq(s + n - m, suf, m); }

/* ---------- Mathematik (Doppelte Genauigkeit fuer Winkel, Float fuer den Rest) ---------- */
static double d_exp(double x) {
    if (x > 700) x = 700;
    if (x < -700) x = -700;
    const double ln2 = 0.6931471805599453;
    long k = (long)(x / ln2 + (x >= 0 ? 0.5 : -0.5));
    double r = x - (double)k * ln2, t = 1, s = 1;
    for (int i = 1; i < 14; i++) { t *= r / i; s += t; }
    union { uint64_t u; double d; } c; c.u = (uint64_t)(k + 1023) << 52;
    return s * c.d;
}
static double d_ln(double x) {
    union { uint64_t u; double d; } c; c.d = x;
    int e = (int)((c.u >> 52) & 2047) - 1023; c.u = (c.u & 0x000FFFFFFFFFFFFFull) | 0x3FF0000000000000ull;
    double m = c.d;                                   /* 1 <= m < 2 */
    if (m > 1.4142135623730951) { m /= 2; e++; }
    double s = (m - 1) / (m + 1), s2 = s * s, t = s, sum = 0;
    for (int i = 1; i < 40; i += 2) { sum += t / i; t *= s2; }
    return 2 * sum + e * 0.6931471805599453;
}
static void d_sincos(double x, double* sn, double* cs) {
    const double pi2 = 6.283185307179586, pi = 3.141592653589793;
    double k = (double)(long)(x / pi2); x -= k * pi2;
    if (x > pi) x -= pi2;
    if (x < -pi) x += pi2;
    double x2 = x * x, ts = x, tc = 1, s = x, c = 1;
    for (int i = 1; i < 16; i++) { tc *= -x2 / ((2 * i - 1) * (2 * i)); c += tc; ts *= -x2 / ((2 * i) * (2 * i + 1)); s += ts; }
    *sn = s; *cs = c;
}
static float f_exp(float x) { return (float)d_exp((double)x); }
#ifdef __SSE2__
#include <emmintrin.h>
static float f_sqrt(float x) { return _mm_cvtss_f32(_mm_sqrt_ss(_mm_set_ss(x))); }
#else
static float f_sqrt(float x) { return __builtin_sqrtf(x); }
#endif
static float f16f(uint16_t h) {
    uint32_t s = (uint32_t)(h & 0x8000) << 16, e = (h >> 10) & 31, m = h & 1023, u;
    if (e == 0) { if (!m) u = s; else { int sh = 0; while (!(m & 1024)) { m <<= 1; sh++; } m &= 1023; u = s | (uint32_t)(113 - sh) << 23 | m << 13; } }
    else if (e == 31) u = s | 0x7F800000u | m << 13;
    else u = s | (e + 112) << 23 | m << 13;
    union { uint32_t u; float f; } c; c.u = u; return c.f;
}

const char* llm_fehler(int c) {
    switch (c) {
    case 0: return "ok"; case -1: return "keine GGUF-Datei"; case -2: return "GGUF-Version nicht unterstuetzt (erwartet 3)";
    case -3: return "unbekannter Metadatentyp"; case -4: return "Metadaten fehlen"; case -5: return "Tensor fehlt";
    case -6: return "Gewichtsformat nicht unterstuetzt (erlaubt: F32, F16, Q8_0)"; case -7: return "kein Speicher"; case -8: return "Modell zu gross fuer die Einstellungen";
    case -9: return "Tokenizer fehlt"; default: return "Fehler";
    }
}

/* ---------- GGUF lesen ---------- */
struct rd { const uint8_t* p; const uint8_t* end; int err; };
static uint32_t r32(struct rd* r) { if (r->p + 4 > r->end) { r->err = 1; return 0; } uint32_t v = rd32(r->p); r->p += 4; return v; }
static uint64_t r64(struct rd* r) { if (r->p + 8 > r->end) { r->err = 1; return 0; } uint64_t v = rd64(r->p); r->p += 8; return v; }
static const char* rstr(struct rd* r, uint32_t* n) { uint64_t l = r64(r); if (r->err || r->p + l > r->end) { r->err = 1; *n = 0; return 0; } const char* s = (const char*)r->p; r->p += l; *n = (uint32_t)l; return s; }
static const uint32_t gsz[13] = { 1, 1, 2, 2, 4, 4, 4, 1, 0, 0, 8, 8, 8 };

struct kvinfo { int64_t ival; double fval; const uint8_t* arr; uint64_t cnt; uint32_t etype; int have; const char* sval; uint32_t slen; };

/* liest einen Wert ab r->p; legt Zahlen in kv ab; Arrays: Zeiger auf den ersten Eintrag, Anzahl, Elementtyp (Zeiger bleibt hinter dem Array) */
static int lies_wert(struct rd* r, uint32_t type, struct kvinfo* kv) {
    kv->have = 1; kv->arr = 0; kv->cnt = 0;
    if (type == 8) { kv->sval = rstr(r, &kv->slen); return r->err ? -3 : 0; }
    if (type == 9) {
        uint32_t et = r32(r); uint64_t n = r64(r); kv->etype = et; kv->cnt = n; kv->arr = r->p;
        if (et == 8) { for (uint64_t i = 0; i < n && !r->err; i++) { uint32_t l; rstr(r, &l); } }
        else if (et < 13 && gsz[et]) { if (r->p + n * gsz[et] > r->end) r->err = 1; else r->p += n * gsz[et]; }
        else return -3;
        return r->err ? -3 : 0;
    }
    if (type > 12) return -3;
    if (r->p + gsz[type] > r->end) { r->err = 1; return -3; }
    const uint8_t* p = r->p; r->p += gsz[type];
    switch (type) {
    case 0: kv->ival = p[0]; break; case 1: kv->ival = (int8_t)p[0]; break; case 2: kv->ival = p[0] | p[1] << 8; break; case 3: kv->ival = (int16_t)(p[0] | p[1] << 8); break;
    case 4: kv->ival = rd32(p); break; case 5: kv->ival = (int32_t)rd32(p); break; case 6: kv->fval = (double)rdf32(p); kv->ival = (int64_t)kv->fval; break;
    case 7: kv->ival = p[0]; break; case 10: case 11: kv->ival = (int64_t)rd64(p); break;
    case 12: { union { uint64_t u; double d; } c; c.u = rd64(p); kv->fval = c.d; kv->ival = (int64_t)c.d; break; }
    }
    if (type != 6 && type != 12) kv->fval = (double)kv->ival;
    return 0;
}

static const struct llm_tensor* finde(const struct llm_tensor* t, int n, const char* name) {
    uint32_t l = (uint32_t)slen_(name);
    for (int i = 0; i < n; i++) if (t[i].nlen == l && mem_eq(t[i].name, name, l)) return &t[i];
    return 0;
}
static void put_int(char* o, int v) { char t[12]; int n = 0; if (!v) t[n++] = '0'; while (v) { t[n++] = (char)('0' + v % 10); v /= 10; } int k = 0; while (n) o[k++] = t[--n]; o[k] = 0; }
static const struct llm_tensor* lt(const struct llm_tensor* t, int n, int layer, const char* suffix) {
    char nm[96]; int k = 0; const char* a = "blk."; while (*a) nm[k++] = *a++; char d[12]; put_int(d, layer); for (int i = 0; d[i]; i++) nm[k++] = d[i]; nm[k++] = '.'; while (*suffix) nm[k++] = *suffix++; nm[k] = 0;
    return finde(t, n, nm);
}

/* ---------- Byte-Level-BPE (GPT-2/Qwen) ---------- */
static int bpe_cp_zu_byte[512]; static int bpe_byte_zu_cp[256]; static int bpe_init;
static void bpe_tabellen(void) {
    if (bpe_init) return;
    bpe_init = 1;
    int n = 0;
    for (int b = 0; b < 256; b++) {
        int druck = (b >= 33 && b <= 126) || (b >= 161 && b <= 172) || (b >= 174 && b <= 255);
        int cp = druck ? b : 256 + n++;
        bpe_byte_zu_cp[b] = cp; bpe_cp_zu_byte[cp] = b;
    }
}
static int utf8_put(char* o, int cp) { if (cp < 0x80) { o[0] = (char)cp; return 1; } o[0] = (char)(0xC0 | cp >> 6); o[1] = (char)(0x80 | (cp & 63)); return 2; }

static int vocab_suche(const struct llm* m, const char* s, uint32_t n) {
    uint32_t h = fnv(s, n) & (m->hash_n - 1);
    for (uint32_t i = 0; i < m->hash_n; i++, h = (h + 1) & (m->hash_n - 1)) {
        int id = m->hash_v[h]; if (id < 0) return -1;
        if (m->tlen[id] == n && mem_eq(m->tok[id], s, n)) return id;
    }
    return -1;
}
static int merge_rang(const struct llm* m, const char* a, uint32_t na, const char* b, uint32_t nb) {
    char t[160]; if (na + nb + 1 > sizeof(t)) return -1;
    for (uint32_t i = 0; i < na; i++) t[i] = a[i]; t[na] = ' '; for (uint32_t i = 0; i < nb; i++) t[na + 1 + i] = b[i];
    uint32_t n = na + nb + 1, h = fnv(t, n) & (m->mhash_n - 1);
    for (uint32_t i = 0; i < m->mhash_n; i++, h = (h + 1) & (m->mhash_n - 1)) {
        int r = m->mhash_v[h]; if (r < 0) return -1;
        if (m->mslen[r] == n && mem_eq(m->mstr[r], t, n)) return r;
    }
    return -1;
}
static int ist_buchstabe(unsigned char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c >= 0x80; }
static int ist_ziffer(unsigned char c) { return c >= '0' && c <= '9'; }
static int ist_leer(unsigned char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == 11 || c == 12; }
static int ist_nl(unsigned char c) { return c == '\n' || c == '\r'; }
static int ist_sonst(unsigned char c) { return !ist_leer(c) && !ist_buchstabe(c) && !ist_ziffer(c); }

/* naechstes Vor-Token (angenaeherte Qwen2-Regex) ab i; liefert das Ende */
static int vorwort(const char* s, int n, int i) {
    unsigned char c = (unsigned char)s[i];
    if (c == '\'' && i + 1 < n) {                                            /* 's 't 'm 'd 're 've 'll */
        unsigned char a = (unsigned char)s[i + 1] | 32;
        if (a == 's' || a == 't' || a == 'm' || a == 'd') return i + 2;
        if (i + 2 < n) { unsigned char b = (unsigned char)s[i + 2] | 32; if ((a == 'r' && b == 'e') || (a == 'v' && b == 'e') || (a == 'l' && b == 'l')) return i + 3; }
    }
    int j = i;
    if (ist_buchstabe(c)) { while (j < n && ist_buchstabe((unsigned char)s[j])) j++; return j; }
    if (!ist_nl(c) && !ist_ziffer(c) && !ist_buchstabe(c) && i + 1 < n && ist_buchstabe((unsigned char)s[i + 1])) { j = i + 1; while (j < n && ist_buchstabe((unsigned char)s[j])) j++; return j; }
    if (ist_ziffer(c)) return i + 1;
    if (ist_sonst(c) || (c == ' ' && i + 1 < n && ist_sonst((unsigned char)s[i + 1]))) {
        j = i; if (c == ' ') j++;
        while (j < n && ist_sonst((unsigned char)s[j])) j++;
        while (j < n && ist_nl((unsigned char)s[j])) j++;
        return j;
    }
    j = i; while (j < n && ist_leer((unsigned char)s[j])) j++;                /* Leerraum */
    int lastnl = -1; for (int k = i; k < j; k++) if (ist_nl((unsigned char)s[k])) lastnl = k;
    if (lastnl >= 0) return lastnl + 1;
    if (j < n && j - i > 1) return j - 1;
    return j;
}

int llm_encode(const struct llm* m, const char* text, int len, int* ids, int max) {
    bpe_tabellen();
    int nid = 0, i = 0;
    while (i < len) {
        int e = vorwort(text, len, i);
        char buf[512]; int st[260]; int ns = 0, bl = 0;
        for (int k = i; k < e && ns < 250; k++) { st[ns++] = bl; bl += utf8_put(buf + bl, bpe_byte_zu_cp[(unsigned char)text[k]]); }
        st[ns] = bl;
        for (;;) {
            int best = -1, bi = -1;
            for (int k = 0; k + 1 < ns; k++) {
                int r = merge_rang(m, buf + st[k], (uint32_t)(st[k + 1] - st[k]), buf + st[k + 1], (uint32_t)(st[k + 2] - st[k + 1]));
                if (r >= 0 && (best < 0 || r < best)) { best = r; bi = k; }
            }
            if (bi < 0) break;
            for (int k = bi + 1; k < ns; k++) st[k] = st[k + 1];
            ns--;
        }
        for (int k = 0; k < ns; k++) {
            int id = vocab_suche(m, buf + st[k], (uint32_t)(st[k + 1] - st[k]));
            if (id < 0) id = 0;
            if (nid < max) ids[nid++] = id;
        }
        i = e;
    }
    return nid;
}

int llm_decode(const struct llm* m, int id, char* out, int max) {
    bpe_tabellen();
    if (id < 0 || id >= m->ntok) return 0;
    const char* s = m->tok[id]; uint32_t n = m->tlen[id];
    if (n >= 4 && s[0] == '<' && s[1] == '|' && s[n - 1] == '>' && s[n - 2] == '|') return 0;       /* Sondermarke */
    int o = 0;
    for (uint32_t k = 0; k < n && o < max;) {
        unsigned char c = (unsigned char)s[k]; int cp;
        if (c < 0x80) { cp = c; k++; } else if ((c & 0xE0) == 0xC0 && k + 1 < n) { cp = (c & 31) << 6 | (s[k + 1] & 63); k += 2; } else { cp = c; k++; }
        out[o++] = (char)(cp < 512 ? bpe_cp_zu_byte[cp] : '?');
    }
    return o;
}

/* ---------- Laden ---------- */
int llm_load(struct llm* m, const uint8_t* data, uint64_t size, llm_alloc_fn al, int max_ctx) {
    mem_zero(m, sizeof(*m));
    if (size < 24 || data[0] != 'G' || data[1] != 'G' || data[2] != 'U' || data[3] != 'F') return -1;
    if (rd32(data + 4) != 3) return -2;
    uint64_t nt = rd64(data + 8), nkv = rd64(data + 16);
    struct rd r = { data + 24, data + size, 0 };
    int64_t n_layer = 0, dim = 0, ffn = 0, nh = 0, nkvh = 0, ctx = 0; double eps = 1e-5, base = 10000; int arch_llama = 0; uint64_t align = 32;
    const uint8_t *tokarr = 0, *mrgarr = 0; uint64_t ntok = 0, nmrg = 0; int64_t eos = -1, bos = -1;
    for (uint64_t i = 0; i < nkv; i++) {
        uint32_t kl; const char* k = rstr(&r, &kl); uint32_t type = r32(&r); struct kvinfo kv; mem_zero(&kv, sizeof(kv));
        if (r.err) return -3;
        if (lies_wert(&r, type, &kv) < 0) return -3;
        if (ends_with(k, kl, ".block_count")) n_layer = kv.ival;
        else if (ends_with(k, kl, ".embedding_length")) dim = kv.ival;
        else if (ends_with(k, kl, ".feed_forward_length")) ffn = kv.ival;
        else if (ends_with(k, kl, ".attention.head_count")) nh = kv.ival;
        else if (ends_with(k, kl, ".attention.head_count_kv")) nkvh = kv.ival;
        else if (ends_with(k, kl, ".attention.layer_norm_rms_epsilon")) eps = kv.fval;
        else if (ends_with(k, kl, ".rope.freq_base")) base = kv.fval;
        else if (ends_with(k, kl, ".context_length")) ctx = kv.ival;
        else if (kl == 20 && mem_eq(k, "general.architecture", 20)) arch_llama = (kv.slen == 5 && mem_eq(kv.sval, "llama", 5));
        else if (kl == 17 && mem_eq(k, "general.alignment", 17)) align = (uint64_t)kv.ival;
        else if (kl == 21 && mem_eq(k, "tokenizer.ggml.tokens", 21)) { tokarr = kv.arr; ntok = kv.cnt; }
        else if (kl == 21 && mem_eq(k, "tokenizer.ggml.merges", 21)) { mrgarr = kv.arr; nmrg = kv.cnt; }
        else if (kl == 25 && mem_eq(k, "tokenizer.ggml.eos_token_id", 25)) eos = kv.ival;
        else if (kl == 25 && mem_eq(k, "tokenizer.ggml.bos_token_id", 25)) bos = kv.ival;
    }
    if (!n_layer || !dim || !ffn || !nh) return -4;
    if (!nkvh) nkvh = nh;
    if (!tokarr || !mrgarr) return -9;
    if (n_layer > LLM_MAX_LAYERS || dim % 32 || ffn % 32) return -8;
    /* Tensor-Verzeichnis */
    struct llm_tensor* tens = al(sizeof(struct llm_tensor) * nt); if (!tens) return -7;
    uint64_t offs[1];
    (void)offs;
    uint64_t* toff = al(sizeof(uint64_t) * nt); if (!toff) return -7;
    for (uint64_t i = 0; i < nt; i++) {
        uint32_t nl; const char* nm = rstr(&r, &nl); uint32_t nd = r32(&r);
        if (r.err || nd > 4) return -3;
        tens[i].name = nm; tens[i].nlen = nl; for (int d = 0; d < 4; d++) tens[i].ne[d] = 1;
        for (uint32_t d = 0; d < nd; d++) tens[i].ne[d] = r64(&r);
        tens[i].type = r32(&r); toff[i] = r64(&r);
        if (r.err) return -3;
    }
    uint64_t pos = (uint64_t)(r.p - data); uint64_t base_off = (pos + align - 1) / align * align;
    for (uint64_t i = 0; i < nt; i++) { tens[i].data = data + base_off + toff[i]; if (tens[i].type != 0 && tens[i].type != 1 && tens[i].type != 8 && (tens[i].nlen > 0)) { /* Typ wird erst bei Verwendung geprueft */ } }
    int n = (int)nt;
    m->emb = finde(tens, n, "token_embd.weight"); m->onorm = finde(tens, n, "output_norm.weight"); m->out = finde(tens, n, "output.weight");
    if (!m->emb || !m->onorm) return -5;
    if (!m->out) m->out = m->emb;
    m->n_layer = (int)n_layer; m->dim = (int)dim; m->ffn = (int)ffn; m->n_head = (int)nh; m->n_kv = (int)nkvh; m->head_dim = (int)(dim / nh); m->kv_dim = m->n_kv * m->head_dim;
    m->vocab = (int)ntok; m->eps = (float)eps; m->rope_base = (float)base; m->neox = !arch_llama;
    m->ctx = max_ctx; if (ctx && ctx < max_ctx) m->ctx = (int)ctx;
    for (int l = 0; l < m->n_layer; l++) {
        struct llm_layer* L = &m->L[l];
        L->an = lt(tens, n, l, "attn_norm.weight"); L->q = lt(tens, n, l, "attn_q.weight"); L->k = lt(tens, n, l, "attn_k.weight"); L->v = lt(tens, n, l, "attn_v.weight");
        L->o = lt(tens, n, l, "attn_output.weight"); L->fn = lt(tens, n, l, "ffn_norm.weight"); L->g = lt(tens, n, l, "ffn_gate.weight"); L->u = lt(tens, n, l, "ffn_up.weight"); L->d = lt(tens, n, l, "ffn_down.weight");
        L->qb = lt(tens, n, l, "attn_q.bias"); L->kb = lt(tens, n, l, "attn_k.bias"); L->vb = lt(tens, n, l, "attn_v.bias");
        if (!L->an || !L->q || !L->k || !L->v || !L->o || !L->fn || !L->g || !L->u || !L->d) return -5;
        const struct llm_tensor* w[7] = { L->q, L->k, L->v, L->o, L->g, L->u, L->d };
        for (int i = 0; i < 7; i++) if (w[i]->type != 0 && w[i]->type != 1 && w[i]->type != 8) return -6;
    }
    if (m->emb->type != 0 && m->emb->type != 1 && m->emb->type != 8) return -6;
    if (m->out->type != 0 && m->out->type != 1 && m->out->type != 8) return -6;
    /* Tokenizer */
    m->tok = al(sizeof(char*) * ntok); m->tlen = al(sizeof(uint32_t) * ntok); if (!m->tok || !m->tlen) return -7;
    { struct rd q = { tokarr, data + size, 0 }; for (uint64_t i = 0; i < ntok; i++) { m->tok[i] = rstr(&q, &m->tlen[i]); if (q.err) return -9; } }
    m->ntok = (int)ntok;
    m->hash_n = 1; while (m->hash_n < ntok * 2) m->hash_n <<= 1;
    m->hash_v = al(sizeof(int) * m->hash_n); if (!m->hash_v) return -7;
    for (uint32_t i = 0; i < m->hash_n; i++) m->hash_v[i] = -1;
    for (uint64_t i = 0; i < ntok; i++) { uint32_t h = fnv(m->tok[i], m->tlen[i]) & (m->hash_n - 1); while (m->hash_v[h] >= 0) h = (h + 1) & (m->hash_n - 1); m->hash_v[h] = (int)i; }
    m->mstr = al(sizeof(char*) * nmrg); m->mslen = al(sizeof(uint32_t) * nmrg); if (!m->mstr || !m->mslen) return -7;
    { struct rd q = { mrgarr, data + size, 0 }; for (uint64_t i = 0; i < nmrg; i++) { m->mstr[i] = rstr(&q, &m->mslen[i]); if (q.err) return -9; } }
    m->nmerge = (int)nmrg;
    m->mhash_n = 1; while (m->mhash_n < nmrg * 2) m->mhash_n <<= 1;
    m->mhash_v = al(sizeof(int) * m->mhash_n); if (!m->mhash_v) return -7;
    for (uint32_t i = 0; i < m->mhash_n; i++) m->mhash_v[i] = -1;
    for (uint64_t i = 0; i < nmrg; i++) { uint32_t h = fnv(m->mstr[i], m->mslen[i]) & (m->mhash_n - 1); while (m->mhash_v[h] >= 0) h = (h + 1) & (m->mhash_n - 1); m->mhash_v[h] = (int)i; }
    m->eos = (int)eos; m->bos = (int)bos;
    m->im_start = vocab_suche(m, "<|im_start|>", 12); m->im_end = vocab_suche(m, "<|im_end|>", 10);
    /* Laufzeitpuffer */
    int hd = m->head_dim, d = m->dim, mx = m->dim > m->ffn ? m->dim : m->ffn;
    m->x = al(4 * (size_t)d); m->xb = al(4 * (size_t)mx); m->xb2 = al(4 * (size_t)d); m->hb = al(4 * (size_t)m->ffn); m->hb2 = al(4 * (size_t)m->ffn);
    m->q = al(4 * (size_t)d); m->att = al(4 * (size_t)m->n_head * m->ctx); m->logits = al(4 * (size_t)m->vocab);
    m->kc = al(4 * (size_t)m->n_layer * m->ctx * m->kv_dim); m->vc = al(4 * (size_t)m->n_layer * m->ctx * m->kv_dim);
    m->rc = al(4 * (size_t)m->ctx * (hd / 2)); m->rs = al(4 * (size_t)m->ctx * (hd / 2));
    m->xq = al((size_t)mx); m->xs = al(4 * (size_t)(mx / 32 + 1));
    if (!m->x || !m->xb || !m->xb2 || !m->hb || !m->hb2 || !m->q || !m->att || !m->logits || !m->kc || !m->vc || !m->rc || !m->rs || !m->xq || !m->xs) return -7;
    double lnb = d_ln((double)m->rope_base);
    for (int i = 0; i < hd / 2; i++) {
        double f = d_exp(-(double)(2 * i) / hd * lnb);
        for (int p = 0; p < m->ctx; p++) { double sn, cs; d_sincos((double)p * f, &sn, &cs); m->rc[p * (hd / 2) + i] = (float)cs; m->rs[p * (hd / 2) + i] = (float)sn; }
    }
    m->rng = 0x2545F491u; m->ok = 1;
    return 0;
}

/* ---------- Rechnen ---------- */
#ifdef __SSE2__
#include <emmintrin.h>
#endif
/* schnelle Umwandlung fuer Q8_0-Skalen (normale Zahlen und 0) */
static inline float f16s(uint16_t h) {
    union { uint32_t u; float f; } c; uint32_t a = h & 0x7FFF;
    c.u = a ? ((uint32_t)(h & 0x8000) << 16) | ((a << 13) + 0x38000000u) : 0; return c.f;
}
static void quant_x(const float* x, int n, int8_t* xq, float* xs) {
    for (int b = 0; b < n / 32; b++) {
        float amax = 0; for (int j = 0; j < 32; j++) { float v = x[b * 32 + j]; if (v < 0) v = -v; if (v > amax) amax = v; }
        float d = amax / 127.0f, id = d != 0 ? 1.0f / d : 0; xs[b] = d;
        for (int j = 0; j < 32; j++) { float v = x[b * 32 + j] * id; xq[b * 32 + j] = (int8_t)(int)(v >= 0 ? v + 0.5f : v - 0.5f); }
    }
}
/* y[o] = W[o][0..in) * x ; W Zeile o liegt hintereinander (ne[0] = in) */
static void matvec(float* y, const struct llm_tensor* t, int in, int out, const float* x, const int8_t* xq, const float* xs) {
    if (t->type == 8) {
        int nb = in / 32; const uint8_t* w = t->data;
        for (int o = 0; o < out; o++) {
            const uint8_t* row = w + (size_t)o * nb * 34;
#ifdef __SSE2__
            __m128 accv = _mm_setzero_ps(); const __m128i zero = _mm_setzero_si128();
            for (int b = 0; b < nb; b++) {
                const uint8_t* blk = row + (size_t)b * 34; const int8_t* a = xq + b * 32; __m128i s = zero;
                for (int h = 0; h < 2; h++) {
                    __m128i qv = _mm_loadu_si128((const __m128i*)(blk + 2 + 16 * h)), av = _mm_loadu_si128((const __m128i*)(a + 16 * h));
                    __m128i ql = _mm_unpacklo_epi8(qv, _mm_cmpgt_epi8(zero, qv)), qh = _mm_unpackhi_epi8(qv, _mm_cmpgt_epi8(zero, qv));
                    __m128i al = _mm_unpacklo_epi8(av, _mm_cmpgt_epi8(zero, av)), ah = _mm_unpackhi_epi8(av, _mm_cmpgt_epi8(zero, av));
                    s = _mm_add_epi32(s, _mm_add_epi32(_mm_madd_epi16(ql, al), _mm_madd_epi16(qh, ah)));
                }
                accv = _mm_add_ps(accv, _mm_mul_ps(_mm_cvtepi32_ps(s), _mm_set1_ps(f16s((uint16_t)(blk[0] | blk[1] << 8)) * xs[b])));
            }
            float tmp[4]; _mm_storeu_ps(tmp, accv); y[o] = (tmp[0] + tmp[1]) + (tmp[2] + tmp[3]);
#else
            float acc = 0;
            for (int b = 0; b < nb; b++) {
                const uint8_t* blk = row + (size_t)b * 34; int32_t s = 0; const int8_t* q = (const int8_t*)(blk + 2); const int8_t* a = xq + b * 32;
                for (int j = 0; j < 32; j++) s += (int32_t)q[j] * a[j];
                acc += (float)s * f16s((uint16_t)(blk[0] | blk[1] << 8)) * xs[b];
            }
            y[o] = acc;
#endif
        }
    } else if (t->type == 1) {
        for (int o = 0; o < out; o++) { const uint8_t* w = t->data + (size_t)o * in * 2; float acc = 0; for (int i = 0; i < in; i++) acc += f16f((uint16_t)(w[2 * i] | w[2 * i + 1] << 8)) * x[i]; y[o] = acc; }
    } else {
        for (int o = 0; o < out; o++) { const uint8_t* w = t->data + (size_t)o * in * 4; float acc = 0; for (int i = 0; i < in; i++) acc += rdf32(w + 4 * i) * x[i]; y[o] = acc; }
    }
}
static void zeile(float* o, const struct llm_tensor* t, int row, int n) {           /* eine Zeile der Einbettung */
    if (t->type == 8) { int nb = n / 32; const uint8_t* w = t->data + (size_t)row * nb * 34; for (int b = 0; b < nb; b++) { float d = f16f((uint16_t)(w[b * 34] | w[b * 34 + 1] << 8)); for (int j = 0; j < 32; j++) o[b * 32 + j] = d * (float)(int8_t)w[b * 34 + 2 + j]; } }
    else if (t->type == 1) { const uint8_t* w = t->data + (size_t)row * n * 2; for (int i = 0; i < n; i++) o[i] = f16f((uint16_t)(w[2 * i] | w[2 * i + 1] << 8)); }
    else { const uint8_t* w = t->data + (size_t)row * n * 4; for (int i = 0; i < n; i++) o[i] = rdf32(w + 4 * i); }
}
static void rmsnorm(float* o, const float* x, const struct llm_tensor* g, int n, float eps) {
    float ss = 0; for (int i = 0; i < n; i++) ss += x[i] * x[i];
    float s = 1.0f / f_sqrt(ss / n + eps);
    for (int i = 0; i < n; i++) o[i] = x[i] * s * rdf32(g->data + 4 * i);
}
static void bias(float* y, const struct llm_tensor* b, int n) { if (b) for (int i = 0; i < n; i++) y[i] += rdf32(b->data + 4 * i); }
static void rope(const struct llm* m, float* v, int nheads, int pos) {
    int hd = m->head_dim, half = hd / 2; const float* c = m->rc + pos * half; const float* s = m->rs + pos * half;
    for (int h = 0; h < nheads; h++) {
        float* p = v + h * hd;
        for (int i = 0; i < half; i++) {
            int a = m->neox ? i : 2 * i, b = m->neox ? i + half : 2 * i + 1;
            float x0 = p[a], x1 = p[b]; p[a] = x0 * c[i] - x1 * s[i]; p[b] = x0 * s[i] + x1 * c[i];
        }
    }
}

float* llm_forward(struct llm* m, int token, int pos) {
    int d = m->dim, hd = m->head_dim, kvd = m->kv_dim, nh = m->n_head, grp = m->n_head / m->n_kv;
    if (pos >= m->ctx || token < 0 || token >= m->vocab) return 0;
    zeile(m->x, m->emb, token, d);
    for (int l = 0; l < m->n_layer; l++) {
        const struct llm_layer* L = &m->L[l];
        rmsnorm(m->xb, m->x, L->an, d, m->eps); quant_x(m->xb, d, m->xq, m->xs);
        matvec(m->q, L->q, d, d, m->xb, m->xq, m->xs); bias(m->q, L->qb, d);
        float* kp = m->kc + ((size_t)l * m->ctx + pos) * kvd; float* vp = m->vc + ((size_t)l * m->ctx + pos) * kvd;
        matvec(kp, L->k, d, kvd, m->xb, m->xq, m->xs); bias(kp, L->kb, kvd);
        matvec(vp, L->v, d, kvd, m->xb, m->xq, m->xs); bias(vp, L->vb, kvd);
        rope(m, m->q, nh, pos); rope(m, kp, m->n_kv, pos);
        float sc = 1.0f / f_sqrt((float)hd);
        for (int h = 0; h < nh; h++) {
            const float* q = m->q + h * hd; float* a = m->att + (size_t)h * m->ctx; int kvh = h / grp;
            float mx = -1e30f;
            for (int t = 0; t <= pos; t++) { const float* k = m->kc + ((size_t)l * m->ctx + t) * kvd + kvh * hd; float s = 0; for (int i = 0; i < hd; i++) s += q[i] * k[i]; s *= sc; a[t] = s; if (s > mx) mx = s; }
            float sum = 0; for (int t = 0; t <= pos; t++) { a[t] = f_exp(a[t] - mx); sum += a[t]; }
            float inv = 1.0f / sum; float* o = m->xb + h * hd; for (int i = 0; i < hd; i++) o[i] = 0;
            for (int t = 0; t <= pos; t++) { const float* v = m->vc + ((size_t)l * m->ctx + t) * kvd + kvh * hd; float w = a[t] * inv; for (int i = 0; i < hd; i++) o[i] += w * v[i]; }
        }
        quant_x(m->xb, d, m->xq, m->xs); matvec(m->xb2, L->o, d, d, m->xb, m->xq, m->xs);
        for (int i = 0; i < d; i++) m->x[i] += m->xb2[i];
        rmsnorm(m->xb, m->x, L->fn, d, m->eps); quant_x(m->xb, d, m->xq, m->xs);
        matvec(m->hb, L->g, d, m->ffn, m->xb, m->xq, m->xs); matvec(m->hb2, L->u, d, m->ffn, m->xb, m->xq, m->xs);
        for (int i = 0; i < m->ffn; i++) { float g = m->hb[i]; m->hb[i] = g / (1.0f + f_exp(-g)) * m->hb2[i]; }
        quant_x(m->hb, m->ffn, m->xq, m->xs); matvec(m->xb2, L->d, m->ffn, d, m->hb, m->xq, m->xs);
        for (int i = 0; i < d; i++) m->x[i] += m->xb2[i];
    }
    rmsnorm(m->xb, m->x, m->onorm, d, m->eps); quant_x(m->xb, d, m->xq, m->xs);
    matvec(m->logits, m->out, d, m->vocab, m->xb, m->xq, m->xs);
    m->pos = pos + 1;
    return m->logits;
}

int llm_sample(struct llm* m, const float* lg, float temp, int top_k) {
    if (temp <= 0 || top_k <= 1) { int b = 0; for (int i = 1; i < m->vocab; i++) if (lg[i] > lg[b]) b = i; return b; }
    if (top_k > 64) top_k = 64;
    int idx[64]; float val[64]; int n = 0;
    for (int i = 0; i < m->vocab; i++) {
        float v = lg[i];
        if (n < top_k) { int j = n++; while (j > 0 && val[j - 1] < v) { val[j] = val[j - 1]; idx[j] = idx[j - 1]; j--; } val[j] = v; idx[j] = i; }
        else if (v > val[n - 1]) { int j = n - 1; while (j > 0 && val[j - 1] < v) { val[j] = val[j - 1]; idx[j] = idx[j - 1]; j--; } val[j] = v; idx[j] = i; }
    }
    float sum = 0, p[64]; for (int i = 0; i < n; i++) { p[i] = f_exp((val[i] - val[0]) / temp); sum += p[i]; }
    m->rng ^= m->rng << 13; m->rng ^= m->rng >> 17; m->rng ^= m->rng << 5;
    float r = (float)(m->rng & 0xFFFFFF) / 16777216.0f * sum;
    for (int i = 0; i < n; i++) { r -= p[i]; if (r <= 0) return idx[i]; }
    return idx[n - 1];
}
