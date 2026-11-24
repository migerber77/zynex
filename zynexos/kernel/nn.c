/* Zynex Neuronales Netz, siehe include/nn.h. Wird mit SSE uebersetzt (Makefile); Interrupts fassen keine XMM-Register an. */
#include "nn.h"

/* Startmodell: vortrainierte Gewichte (kernel/nn_start.bin, int16, siehe tools/nn_pretrain.c und den Workflow zynexos-pretrain) */
__asm__(".section .rodata\n.balign 4\n.globl nn_start_bin\nnn_start_bin:\n.incbin \"kernel/nn_start.bin\"\n.globl nn_start_ende\nnn_start_ende:\n.text\n");
extern const char nn_start_bin[], nn_start_ende[];

#ifndef NN_OHNE_START
#define NN_OHNE_START 0
#endif
#define NW 16
#define NE 12
#define NI (NW * NE)
#define NH NN_NH
#define NV 256

static float emb[NV][NE], W1[NH][NI], b1[NH], W2[NV][NH], b2[NV];
static float x[NI], h[NH], pr[NV];
static int bytes_[NW];
static int bereit, trainiert = 1;
static float verlust_summe; static int verlust_n;

static float fexp(float v) {                     /* e^v, Fehler < 1e-4 */
    if (v < -80.f) return 0.f;
    if (v > 80.f) v = 80.f;
    float t = v * 1.44269504f; int k = (int)t; if ((float)k > t) k--;
    float f = t - (float)k;
    float p = 1.f + f * (0.69314718f + f * (0.24022651f + f * (0.05550411f + f * 0.00961813f)));
    union { uint32_t u; float f; } c; c.u = (uint32_t)(k + 127) << 23;
    return p * c.f;
}
static float ftanh(float v) { if (v > 9.f) return 1.f; if (v < -9.f) return -1.f; float e = fexp(2.f * v); return (e - 1.f) / (e + 1.f); }
static float flog2(float v) {                     /* log2, fuer die Verlustanzeige */
    union { float f; uint32_t u; } c; c.f = v; int e = (int)((c.u >> 23) & 255) - 127; c.u = (c.u & 0x7FFFFFu) | 0x3F800000u;
    float m = c.f; return (float)e + (m - 1.f) * (1.4425f - 0.4425f * (m - 1.f) * 0.9f);
}

static uint32_t rs = 88172645u;
static float zuf(void) { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return (float)(rs & 0xFFFFFF) / 8388608.f - 1.f; }   /* -1..1 */

void nn_init(void) {
    rs = 88172645u;
    for (int i = 0; i < NV; i++) for (int e = 0; e < NE; e++) emb[i][e] = 0.3f * zuf();
    for (int j = 0; j < NH; j++) { b1[j] = 0.f; for (int i = 0; i < NI; i++) W1[j][i] = 0.18f * zuf(); }
    for (int v = 0; v < NV; v++) { b2[v] = 0.f; for (int j = 0; j < NH; j++) W2[v][j] = 0.12f * zuf(); }
    bereit = 1; verlust_summe = 0; verlust_n = 0;
    if (!NN_OHNE_START && nn_start_ende - nn_start_bin == 2 * NN_PARAMS) {                    /* vortrainierte Gewichte uebernehmen */
        static int16_t w[NN_PARAMS]; const unsigned char* b = (const unsigned char*)nn_start_bin;
        for (int i = 0; i < NN_PARAMS; i++) w[i] = (int16_t)((uint16_t)b[2 * i] | ((uint16_t)b[2 * i + 1] << 8));
        nn_importieren(w, NN_PARAMS);
    }
}
void nn_training(int an) { trainiert = an; }

void nn_vorbereiten(uint64_t hist, uint64_t hist2) {
    if (!bereit) nn_init();
    for (int k = 0; k < NW; k++) { int b = k < 8 ? (int)((hist >> (8 * k)) & 255) : (int)((hist2 >> (8 * (k - 8))) & 255); bytes_[k] = b; for (int e = 0; e < NE; e++) x[k * NE + e] = emb[b][e]; }
    for (int j = 0; j < NH; j++) { float s = b1[j]; const float* w = W1[j]; for (int i = 0; i < NI; i++) s += w[i] * x[i]; h[j] = ftanh(s); }
    float mx = -1e30f; static float lg[NV];
    for (int v = 0; v < NV; v++) { float s = b2[v]; const float* w = W2[v]; for (int j = 0; j < NH; j++) s += w[j] * h[j]; lg[v] = s; if (s > mx) mx = s; }
    float z = 0.f; for (int v = 0; v < NV; v++) { pr[v] = fexp(lg[v] - mx); z += pr[v]; }
    float iz = 1.f / z; for (int v = 0; v < NV; v++) pr[v] *= iz;
}

int nn_bitprob(int c0) {
    if (!bereit || c0 < 1 || c0 > 255) return 2048;
    int k = 0; while ((c0 >> (k + 1)) != 0) k++;            /* k = Anzahl schon bekannter Bits */
    int v = c0 - (1 << k), width = 1 << (7 - k), lo = v << (8 - k);
    float a = 0.f, b = 0.f;
    for (int i = 0; i < width; i++) { a += pr[lo + i]; b += pr[lo + width + i]; }
    float t = a + b; if (t <= 1e-12f) return 2048;
    int p = (int)(b / t * 4096.f); return p < 1 ? 1 : p > 4095 ? 4095 : p;
}

void nn_lerne(int c, int lr_t) {
    if (!bereit || !trainiert || c < 0 || c > 255) return;
    float lr = (float)lr_t * 0.001f;
    { float p = pr[c]; if (p < 1e-6f) p = 1e-6f; verlust_summe += -flog2(p); verlust_n++; }
    static float dl[NV], dh[NH], dz[NH], dx[NI];
    for (int v = 0; v < NV; v++) dl[v] = pr[v]; dl[c] -= 1.f;
    for (int j = 0; j < NH; j++) dh[j] = 0.f;
    for (int v = 0; v < NV; v++) { float g = dl[v]; const float* w = W2[v]; for (int j = 0; j < NH; j++) dh[j] += w[j] * g; }      /* mit den alten Gewichten */
    for (int j = 0; j < NH; j++) dz[j] = dh[j] * (1.f - h[j] * h[j]);
    for (int i = 0; i < NI; i++) dx[i] = 0.f;
    for (int j = 0; j < NH; j++) { float g = dz[j]; const float* w = W1[j]; for (int i = 0; i < NI; i++) dx[i] += w[i] * g; }
    for (int v = 0; v < NV; v++) { float g = lr * dl[v]; float* w = W2[v]; for (int j = 0; j < NH; j++) w[j] -= g * h[j]; b2[v] -= g; }
    for (int j = 0; j < NH; j++) { float g = lr * dz[j]; float* w = W1[j]; for (int i = 0; i < NI; i++) w[i] -= g * x[i]; b1[j] -= g; }
    for (int k = 0; k < NW; k++) for (int e = 0; e < NE; e++) emb[bytes_[k]][e] -= lr * dx[k * NE + e];
}
int nn_verlust(void) { return verlust_n ? (int)(verlust_summe / (float)verlust_n * 100.f) : 800; }

static int16_t q(float w) { float s = w * 2048.f; if (s > 32767.f) s = 32767.f; if (s < -32767.f) s = -32767.f; return (int16_t)(s >= 0 ? s + 0.5f : s - 0.5f); }
int nn_exportieren(int16_t* out, int max) {
    if (max < NN_PARAMS) return -1; if (!bereit) nn_init();
    int n = 0;
    for (int i = 0; i < NV; i++) for (int e = 0; e < NE; e++) out[n++] = q(emb[i][e]);
    for (int j = 0; j < NH; j++) for (int i = 0; i < NI; i++) out[n++] = q(W1[j][i]);
    for (int j = 0; j < NH; j++) out[n++] = q(b1[j]);
    for (int v = 0; v < NV; v++) for (int j = 0; j < NH; j++) out[n++] = q(W2[v][j]);
    for (int v = 0; v < NV; v++) out[n++] = q(b2[v]);
    return n;
}
int nn_importieren(const int16_t* in, int n) {
    if (n != NN_PARAMS) return -1;
    int k = 0; const float f = 1.f / 2048.f;
    for (int i = 0; i < NV; i++) for (int e = 0; e < NE; e++) emb[i][e] = (float)in[k++] * f;
    for (int j = 0; j < NH; j++) for (int i = 0; i < NI; i++) W1[j][i] = (float)in[k++] * f;
    for (int j = 0; j < NH; j++) b1[j] = (float)in[k++] * f;
    for (int v = 0; v < NV; v++) for (int j = 0; j < NH; j++) W2[v][j] = (float)in[k++] * f;
    for (int v = 0; v < NV; v++) b2[v] = (float)in[k++] * f;
    bereit = 1; return 0;
}
