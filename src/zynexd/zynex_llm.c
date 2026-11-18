#include "zynex_llm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "llama.h"

struct zx_llm {
    struct llama_model *modell;
    struct llama_context *kontext;
    const struct llama_vocab *vokabeln;
    int n_batch;
};

static void stiller_log(enum ggml_log_level stufe, const char *text, void *nutzer)
{
    (void)stufe;
    (void)text;
    (void)nutzer;
}

static double jetzt_s(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

zx_llm *zx_llm_open(const char *pfad, int n_ctx, int n_threads, char *err, size_t err_cap)
{
    struct llama_model_params mp;
    struct llama_context_params cp;
    zx_llm *m = calloc(1, sizeof(*m));

    if (!m)
        return NULL;
    if (!getenv("ZYNEX_LLM_LOG"))
        llama_log_set(stiller_log, NULL);
    llama_backend_init();

    mp = llama_model_default_params();
    mp.n_gpu_layers = 0;
#ifdef ZYNEX_NO_MMAP
    mp.load_mode = LLAMA_LOAD_MODE_NONE; /* Genode: Modell lesen statt mmap */
#endif
    m->modell = llama_model_load_from_file(pfad, mp);
    if (!m->modell) {
        if (err && err_cap)
            snprintf(err, err_cap, "Modell nicht lesbar: %s", pfad);
        free(m);
        return NULL;
    }

    cp = llama_context_default_params();
    cp.n_ctx = (uint32_t)n_ctx;
    cp.n_batch = 512;
    cp.n_ubatch = 512;
    cp.n_threads = n_threads;
    cp.n_threads_batch = n_threads;
    cp.no_perf = true;
    m->kontext = llama_init_from_model(m->modell, cp);
    if (!m->kontext) {
        if (err && err_cap)
            snprintf(err, err_cap, "Kontext nicht erzeugbar (zu wenig Speicher?)");
        llama_model_free(m->modell);
        free(m);
        return NULL;
    }
    m->vokabeln = llama_model_get_vocab(m->modell);
    m->n_batch = (int)cp.n_batch;
    return m;
}

void zx_llm_close(zx_llm *m)
{
    if (!m)
        return;
    llama_free(m->kontext);
    llama_model_free(m->modell);
    free(m);
}

int zx_llm_verfuegbar(void) { return 1; }

int zx_llm_antwort(zx_llm *m, const char *system, const char *verlauf, const char *nutzer, int max_tokens,
                   zx_llm_stueck cb, void *cb_nutzer, double *tok_pro_s)
{
    /* ChatML. "/no_think" und leerer Denkblock halten SmolLM3 im schnellen Antwortmodus. */
    if (!verlauf)
        verlauf = "";
    size_t cap = strlen(system) + strlen(verlauf) + strlen(nutzer) + 256;
    char *prompt = malloc(cap);
    llama_token *tok;
    int n_prompt, i, erzeugt = 0;
    struct llama_sampler *sm;
    double t0, t1 = 0.0;
    int fehler = 0;

    if (!prompt)
        return -1;
    snprintf(prompt, cap,
             "<|im_start|>system\n%s\n/no_think<|im_end|>\n%s"
             "<|im_start|>user\n%s<|im_end|>\n"
             "<|im_start|>assistant\n<think>\n\n</think>\n",
             system, verlauf, nutzer);

    n_prompt = -llama_tokenize(m->vokabeln, prompt, (int32_t)strlen(prompt), NULL, 0, true, true);
    if (n_prompt <= 0 || n_prompt >= (int)llama_n_ctx(m->kontext) - 8 - max_tokens) {
        free(prompt);
        if (verlauf[0]) /* zu lang: ohne Verlauf nochmal */
            return zx_llm_antwort(m, system, "", nutzer, max_tokens, cb, cb_nutzer, tok_pro_s);
        return -2;
    }
    tok = malloc(sizeof(llama_token) * (size_t)n_prompt);
    if (!tok || llama_tokenize(m->vokabeln, prompt, (int32_t)strlen(prompt), tok, n_prompt, true, true) < 0) {
        free(tok);
        free(prompt);
        return -3;
    }
    free(prompt);

    /* Der Verlauf kommt als Text im Prompt; der Modell-Cache wird je Antwort neu gefuellt. */
    llama_memory_clear(llama_get_memory(m->kontext), true);

    sm = llama_sampler_chain_init(llama_sampler_chain_default_params());
    llama_sampler_chain_add(sm, llama_sampler_init_top_k(40));
    llama_sampler_chain_add(sm, llama_sampler_init_top_p(0.9f, 1));
    llama_sampler_chain_add(sm, llama_sampler_init_temp(0.5f));
    llama_sampler_chain_add(sm, llama_sampler_init_dist((uint32_t)time(NULL)));

    for (i = 0; i < n_prompt && !fehler; i += m->n_batch) {
        int n = n_prompt - i < m->n_batch ? n_prompt - i : m->n_batch;
        if (llama_decode(m->kontext, llama_batch_get_one(tok + i, n)) != 0)
            fehler = 1;
    }
    free(tok);
    if (fehler) {
        llama_sampler_free(sm);
        return -4;
    }

    t0 = jetzt_s();
    while (erzeugt < max_tokens) {
        llama_token t = llama_sampler_sample(sm, m->kontext, -1);
        char stueck[256];
        int n;

        if (llama_vocab_is_eog(m->vokabeln, t))
            break;
        n = llama_token_to_piece(m->vokabeln, t, stueck, (int32_t)sizeof(stueck) - 1, 0, false);
        if (n < 0)
            n = 0;
        stueck[n] = '\0';
        erzeugt++;
        if (erzeugt == 1)
            t1 = jetzt_s(); /* ab dem ersten Token messen, ohne Prompt-Verarbeitung */
        if (cb && cb(stueck, cb_nutzer))
            break;
        if (llama_decode(m->kontext, llama_batch_get_one(&t, 1)) != 0) {
            erzeugt = -5;
            break;
        }
    }
    if (tok_pro_s) {
        double dt = jetzt_s() - t1;
        *tok_pro_s = (erzeugt > 1 && dt > 0.0) ? (double)(erzeugt - 1) / dt : 0.0;
    }
    (void)t0;
    llama_sampler_free(sm);
    return erzeugt;
}
