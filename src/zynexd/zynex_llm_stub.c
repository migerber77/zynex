/* Platzhalter, wenn ohne llama.cpp gebaut wird (z.B. fuer die schnellen Tests). */
#include <stdio.h>

#include "zynex_llm.h"

zx_llm *zx_llm_open(const char *pfad, int n_ctx, int n_threads, char *err, size_t err_cap)
{
    (void)pfad;
    (void)n_ctx;
    (void)n_threads;
    if (err && err_cap)
        snprintf(err, err_cap, "ohne llama.cpp gebaut");
    return NULL;
}

void zx_llm_close(zx_llm *m) { (void)m; }

int zx_llm_antwort(zx_llm *m, const char *s, const char *v, const char *n, int mx, zx_llm_stueck cb, void *cn, double *t)
{
    (void)m; (void)s; (void)v; (void)n; (void)mx; (void)cb; (void)cn; (void)t;
    return -1;
}

int zx_llm_verfuegbar(void) { return 0; }
