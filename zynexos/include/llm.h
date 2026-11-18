#pragma once
#include <stdint.h>
#include <stddef.h>
/*
 * Zynex LLM-Motor: Inferenz fuer Transformer-Sprachmodelle im GGUF-Format (Architektur "qwen2" und "llama", Gewichte Q8_0/F16/F32).
 * Ohne libc und ohne libm (eigene exp/sin/cos), laeuft im Kernel wie auf dem Entwicklungsrechner (Tests).
 * Greedy oder Temperatur-Sampling, KV-Cache, GQA, RoPE, RMSNorm, SwiGLU, Byte-Level-BPE-Tokenizer aus dem GGUF.
 */
#define LLM_MAX_LAYERS 64

struct llm_tensor { const char* name; uint32_t nlen; uint32_t type; uint64_t ne[4]; const uint8_t* data; };
struct llm_layer {
    const struct llm_tensor *an, *q, *k, *v, *o, *fn, *g, *u, *d, *qb, *kb, *vb;
};
struct llm {
    /* Modell */
    int ok, neox, n_layer, dim, ffn, n_head, n_kv, head_dim, vocab, ctx, kv_dim;
    float eps, rope_base;
    int eos, bos, im_start, im_end;
    const struct llm_tensor *emb, *onorm, *out;
    struct llm_layer L[LLM_MAX_LAYERS];
    /* Tokenizer */
    const char** tok; uint32_t* tlen; int ntok;
    int* hash_v; uint32_t hash_n;                 /* Wort -> Token-Nummer */
    const char** mstr; uint32_t* mslen; int nmerge;                    /* Verschmelzungsregeln "a b" in Rangfolge */
    int* mhash_v; uint32_t mhash_n;                                   /* "a b" -> Rang */
    /* Laufzeit */
    float *x, *xb, *xb2, *hb, *hb2, *q, *att, *logits, *kc, *vc, *rc, *rs;
    int8_t* xq; float* xs;
    int pos;
    uint32_t rng;
};
typedef void* (*llm_alloc_fn)(size_t);

int  llm_load(struct llm* m, const uint8_t* data, uint64_t size, llm_alloc_fn alloc, int max_ctx);   /* 0 = ok, <0 Fehler (siehe llm_fehler) */
const char* llm_fehler(int code);
int  llm_encode(const struct llm* m, const char* text, int len, int* ids, int max);                  /* Text -> Token (ohne Sondermarken) */
int  llm_decode(const struct llm* m, int id, char* out, int max);                                    /* Token -> Bytes (ohne Endnull); 0 bei Sondermarken */
float* llm_forward(struct llm* m, int token, int pos);                                               /* Logits fuer die naechste Stelle */
int  llm_sample(struct llm* m, const float* logits, float temperatur, int top_k);                    /* 0 = Greedy */
