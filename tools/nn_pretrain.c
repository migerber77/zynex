/* Vortraining des Zynex-Netzes auf dem Rechner (nutzt dasselbe nn.c wie der Kern).
 * Aufruf: nn_pretrain korpus.txt ausgabe.bin [sekunden] [lr_start] [lr_ende]
 * Die Ausgabe ist die rohe Gewichtsdatei (int16 little endian), die der Kern als Startmodell einbettet. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>
#include "../zynexos/include/nn.h"
static double jetzt(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }
static int sichere(const char* pfad) {                                           /* aktuelle Gewichte schreiben */
    static int16_t w[NN_PARAMS]; if (nn_exportieren(w, NN_PARAMS) != NN_PARAMS) return 1;
    FILE* o = fopen(pfad, "wb"); if (!o) return 1; for (int i = 0; i < NN_PARAMS; i++) { fputc(w[i] & 255, o); fputc((w[i] >> 8) & 255, o); } fclose(o);
    return 0;
}
int main(int argc, char** argv) {
    if (argc < 3) { fprintf(stderr, "nn_pretrain korpus ausgabe [sekunden] [lr_start] [lr_ende]\n"); return 1; }
    FILE* f = fopen(argv[1], "rb"); if (!f) { perror("korpus"); return 1; }
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char* t = malloc(n + 1); if (fread(t, 1, n, f) != (size_t)n) return 1; fclose(f);
    double limit = argc > 3 ? atof(argv[3]) : 600; int lr0 = argc > 4 ? atoi(argv[4]) : 60, lr1 = argc > 5 ? atoi(argv[5]) : 8;
    long hold = n / 50; if (hold > 200000) hold = 200000; long ntr = n - hold;       /* letzte 2 % zum Pruefen, nie trainiert */
    nn_init(); double t0 = jetzt(); long done = 0; int ep = 0; double beste = 1e9;
    printf("Korpus %ld Bytes, Training %ld, Pruefung %ld, Zeitlimit %.0f s\n", n, ntr, hold, limit); fflush(stdout);
    for (;;) {
        uint64_t h = 0, h2 = 0; double s = 0; long c = 0; int stop = 0;
        for (long i = 0; i < ntr; i++) {
            double el = jetzt() - t0; if ((i & 1023) == 0) { if (el > limit) { stop = 1; break; } }
            double fr = el / limit; int lr = lr0 + (int)((lr1 - lr0) * fr); if (lr < 1) lr = 1;
            nn_vorbereiten(h, h2); nn_lerne(t[i], lr); h2 = (h2 << 8) | (h >> 56); h = (h << 8) | t[i]; done++;
            if ((done % 50000) == 0) { printf("  %ld Bytes, %.0f s, Verlust %.2f Bit/Zeichen, lr %d\n", done, el, nn_verlust() / 100.0, lr); fflush(stdout); }
        }
        ep++;
        nn_training(0); uint64_t g = 0, g2 = 0; double b = 0; for (long i = ntr; i < n; i++) { nn_vorbereiten(g, g2); int k = t[i]; int c0 = 1; double bits = 0; for (int q = 7; q >= 0; q--) { int y = (k >> q) & 1, p = nn_bitprob(c0); double pp = (y ? p : 4096 - p) / 4096.0; bits -= __builtin_log2(pp); c0 = (c0 << 1) | y; } b += bits; g2 = (g2 << 8) | (g >> 56); g = (g << 8) | t[i]; c++; } nn_training(1);
        printf("Durchgang %d: Pruefverlust (unbekannter Text) %.3f Bit/Zeichen\n", ep, b / c); fflush(stdout);
        if (b / c < beste) { beste = b / c; if (sichere(argv[2])) return 1; printf("Bestes Modell bisher gesichert (Durchgang %d, %.3f)\n", ep, beste); fflush(stdout); }
        if (stop) break;
    }
    if (beste > 1e8 && sichere(argv[2])) return 1;                                  /* nie ein Durchgang geschafft: Stand sichern */
    printf("Gewichte nach %s geschrieben (%d Werte)\n", argv[2], NN_PARAMS); return 0;
}
