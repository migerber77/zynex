/*
 * Zynex OS - Basic AI Primitives (Matrix + Dense Layer)
 */

#include "ai.h"
#include "arena.h"
#include "kheap.h"

extern void kprint(const char* str);
extern void kprint_dec(uint32_t val);
extern void kputchar(char c);

/* Very small float print */
static void print_float(float v) {
    if (v < 0) {
        kputchar('-');
        v = -v;
    }
    int ipart = (int)v;
    int fpart = (int)((v - ipart) * 100);
    if (fpart < 0) fpart = -fpart;
    kprint_dec((uint32_t)ipart);
    kputchar('.');
    if (fpart < 10) kputchar('0');
    kprint_dec((uint32_t)fpart);
}

struct matrix* matrix_create(uint32_t rows, uint32_t cols) {
    struct matrix* m = (struct matrix*)kmalloc(sizeof(struct matrix));
    if (!m) return 0;
    m->rows = rows;
    m->cols = cols;
    m->data = (float*)ai_alloc(rows * cols * sizeof(float));
    if (!m->data) {
        kfree(m);
        return 0;
    }
    matrix_zero(m);
    return m;
}

void matrix_free(struct matrix* m) {
    if (!m) return;
    /* data lives in AI arena – not individually freed */
    kfree(m);
}

void matrix_zero(struct matrix* m) {
    if (!m || !m->data) return;
    uint32_t n = m->rows * m->cols;
    for (uint32_t i = 0; i < n; i++) m->data[i] = 0.0f;
}

void matrix_fill(struct matrix* m, float v) {
    if (!m || !m->data) return;
    uint32_t n = m->rows * m->cols;
    for (uint32_t i = 0; i < n; i++) m->data[i] = v;
}

int matrix_mul(const struct matrix* a, const struct matrix* b, struct matrix* out) {
    if (!a || !b || !out) return -1;
    if (a->cols != b->rows) return -2;
    if (out->rows != a->rows || out->cols != b->cols) return -3;

    for (uint32_t i = 0; i < a->rows; i++) {
        for (uint32_t j = 0; j < b->cols; j++) {
            float sum = 0.0f;
            for (uint32_t k = 0; k < a->cols; k++) {
                sum += a->data[i * a->cols + k] * b->data[k * b->cols + j];
            }
            out->data[i * out->cols + j] = sum;
        }
    }
    return 0;
}

void matrix_print(const struct matrix* m, const char* name) {
    if (!m) return;
    kprint(name ? name : "matrix");
    kprint(" (");
    kprint_dec(m->rows);
    kprint("x");
    kprint_dec(m->cols);
    kprint("):\n");
    for (uint32_t i = 0; i < m->rows; i++) {
        kprint("  [");
        for (uint32_t j = 0; j < m->cols; j++) {
            print_float(m->data[i * m->cols + j]);
            if (j + 1 < m->cols) kprint(", ");
        }
        kprint("]\n");
    }
}

struct dense_layer* dense_create(uint32_t in_features, uint32_t out_features, int relu) {
    struct dense_layer* l = (struct dense_layer*)kmalloc(sizeof(struct dense_layer));
    if (!l) return 0;
    l->weights = matrix_create(out_features, in_features);
    l->bias    = matrix_create(out_features, 1);
    l->relu    = relu;
    if (!l->weights || !l->bias) {
        dense_free(l);
        return 0;
    }
    /* Small deterministic init */
    for (uint32_t i = 0; i < out_features * in_features; i++) {
        l->weights->data[i] = 0.1f * ((int)(i % 7) - 3);
    }
    for (uint32_t i = 0; i < out_features; i++) {
        l->bias->data[i] = 0.01f * (float)i;
    }
    return l;
}

void dense_free(struct dense_layer* l) {
    if (!l) return;
    matrix_free(l->weights);
    matrix_free(l->bias);
    kfree(l);
}

int dense_forward(struct dense_layer* l, const struct matrix* in, struct matrix* out) {
    if (!l || !in || !out) return -1;
    /* out = weights * in */
    if (matrix_mul(l->weights, in, out) != 0) return -2;

    /* add bias + optional ReLU */
    for (uint32_t i = 0; i < out->rows; i++) {
        out->data[i] += l->bias->data[i];
        if (l->relu && out->data[i] < 0.0f) {
            out->data[i] = 0.0f;
        }
    }
    return 0;
}

void ai_demo(void) {
    kprint("\n[AI Demo] Small dense layer forward pass\n");

    /* Input: 4x1 */
    struct matrix* input = matrix_create(4, 1);
    if (!input) {
        kprint("  Failed to create input\n");
        return;
    }
    input->data[0] = 1.0f;
    input->data[1] = 0.5f;
    input->data[2] = -0.3f;
    input->data[3] = 0.8f;
    matrix_print(input, "input");

    struct dense_layer* layer = dense_create(4, 3, 1);
    if (!layer) {
        kprint("  Failed to create layer\n");
        matrix_free(input);
        return;
    }

    struct matrix* output = matrix_create(3, 1);
    if (dense_forward(layer, input, output) == 0) {
        matrix_print(output, "output (ReLU)");
    } else {
        kprint("  Forward failed\n");
    }

    matrix_free(output);
    dense_free(layer);
    matrix_free(input);

    kprint("[AI Demo] Done\n\n");
}
