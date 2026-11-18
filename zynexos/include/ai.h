#pragma once
#include <stdint.h>
#include <stddef.h>

/* Simple matrix (row-major) */
struct matrix {
    uint32_t rows;
    uint32_t cols;
    float*   data;
};

struct matrix* matrix_create(uint32_t rows, uint32_t cols);
void           matrix_free(struct matrix* m);
void           matrix_zero(struct matrix* m);
void           matrix_fill(struct matrix* m, float v);
int            matrix_mul(const struct matrix* a, const struct matrix* b, struct matrix* out);
void           matrix_print(const struct matrix* m, const char* name);

/* Tiny dense layer: out = activation(weights * in + bias) */
struct dense_layer {
    struct matrix* weights;  /* out_features x in_features */
    struct matrix* bias;     /* out_features x 1 */
    int            relu;
};

struct dense_layer* dense_create(uint32_t in_features, uint32_t out_features, int relu);
void                dense_free(struct dense_layer* l);
int                 dense_forward(struct dense_layer* l, const struct matrix* in, struct matrix* out);

void ai_demo(void);
