/* Entwicklungsrechner-Werkzeug fuer den Zynex-LLM-Motor.
 *   llm_host <modell.gguf> logits <tok> <tok> ...   gibt die Logits nach der Eingabe aus (zum Vergleich mit der Referenz)
 *   llm_host <modell.gguf> encode <text>            Token-Nummern des Textes
 *   llm_host <modell.gguf> chat <frage> [max]       ChatML-Antwort (greedy), mit Tempo-Angabe */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../zynexos/include/llm.h"

static void* al(size_t n) { void* p = calloc(1, n); return p; }
int main(int argc, char** argv) {
    if (argc < 3) { fprintf(stderr, "Aufruf: siehe Kopf der Datei\n"); return 2; }
    FILE* f = fopen(argv[1], "rb"); if (!f) { perror("open"); return 2; }
    fseek(f, 0, SEEK_END); long sz = ftell(f); rewind(f);
    uint8_t* d = malloc(sz); if (fread(d, 1, sz, f) != (size_t)sz) return 2; fclose(f);
    static struct llm m; int r = llm_load(&m, d, (uint64_t)sz, al, 512);
    if (r) { printf("Laden fehlgeschlagen: %s (%d)\n", llm_fehler(r), r); return 1; }
    printf("Modell: %d Schichten, dim %d, %d Koepfe (%d KV), Vokabular %d, Kontext %d\n", m.n_layer, m.dim, m.n_head, m.n_kv, m.vocab, m.ctx);
    if (!strcmp(argv[2], "encode")) {
        int ids[512]; int n = llm_encode(&m, argv[3], (int)strlen(argv[3]), ids, 512);
        for (int i = 0; i < n; i++) printf("%d ", ids[i]); printf("\n"); return 0;
    }
    if (!strcmp(argv[2], "logits")) {
        float* lg = 0; for (int i = 3; i < argc; i++) lg = llm_forward(&m, atoi(argv[i]), i - 3);
        for (int i = 0; i < m.vocab; i++) printf("%.6f\n", lg[i]); return 0;
    }
    if (!strcmp(argv[2], "chat")) {
        int mx = argc > 4 ? atoi(argv[4]) : 60; int ids[400], n = 0; char buf[1024];
        ids[n++] = m.im_start; { const char* sys = "system\nDu bist Zynie, der freundliche Assistent von Zynex OS. Antworte kurz, sachlich und in der Sprache der Frage. Wenn du etwas nicht sicher weisst, sag es ehrlich, statt zu raten."; n += llm_encode(&m, sys, (int)strlen(sys), ids + n, 100); } ids[n++] = m.im_end; n += llm_encode(&m, "\n", 1, ids + n, 10);
        ids[n++] = m.im_start; n += llm_encode(&m, "user\n", 5, ids + n, 10); n += llm_encode(&m, argv[3], (int)strlen(argv[3]), ids + n, 200); ids[n++] = m.im_end; n += llm_encode(&m, "\n", 1, ids + n, 10);
        ids[n++] = m.im_start; n += llm_encode(&m, "assistant\n", 10, ids + n, 10);
        clock_t t0 = clock(); float* lg = 0; int pos = 0;
        for (int i = 0; i < n; i++) lg = llm_forward(&m, ids[i], pos++);
        double tp = (double)(clock() - t0) / CLOCKS_PER_SEC; t0 = clock(); int gen = 0;
        printf("Antwort: ");
        for (; gen < mx && pos < m.ctx; gen++) {
            int t = llm_sample(&m, lg, 0, 0); if (t == m.im_end || t == m.eos) break;
            int k = llm_decode(&m, t, buf, 1000); fwrite(buf, 1, k, stdout); fflush(stdout);
            lg = llm_forward(&m, t, pos++);
        }
        double tg = (double)(clock() - t0) / CLOCKS_PER_SEC;
        printf("\n[Eingabe %d Token in %.2f s, Ausgabe %d Token in %.2f s = %.2f Token/s]\n", n, tp, gen, tg, gen / (tg > 0 ? tg : 1));
        return 0;
    }
    return 2;
}
