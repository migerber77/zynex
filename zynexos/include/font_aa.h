#pragma once
#include <stdint.h>
/* Geglaettete Schriften (tools/mkfont.py): Schrift 0-3 Mono, 4-7 Inter; je 191 Zeichen (32-126, 160-255) */
struct aa_glyph { uint32_t off; uint8_t w, h; int8_t x0, y0; uint8_t adv; };
#define AA_ZEICHEN 191
extern const struct aa_glyph aa_glyphen[];
extern const uint8_t aa_daten[];
static inline int aa_index(unsigned char c) { return c >= 160 ? 95 + (c - 160) : (c >= 32 && c <= 126) ? c - 32 : 0; }
