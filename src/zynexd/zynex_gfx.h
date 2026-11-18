/* Einfache Software-Grafik: Canvas, geglaettete Formen, Text. Kein GPU, keine Bibliothek. */
#ifndef ZYNEX_GFX_H
#define ZYNEX_GFX_H

#include <stdint.h>

typedef struct {
    uint32_t *px; /* 0x00RRGGBB, w*h Punkte */
    int w, h;
} zx_canvas;

extern const unsigned char zx_font8x16[224][16]; /* Zeichen 32..255 */

void zx_fill(zx_canvas *c, uint32_t farbe);
void zx_blend(zx_canvas *c, int x, int y, uint32_t farbe, float alpha);
/* Abgerundetes Rechteck um (cx,cy) mit Halbbreite hw, Halbhoehe hh, Radius r. Geglaettete Kanten. */
void zx_rrect(zx_canvas *c, float cx, float cy, float hw, float hh, float r, uint32_t farbe);
/* Mund: Linse mit Mittellinie y = cy + sm*(1-u^2), u=(x-cx)/hw, Dicke 'dicke' + 2*offen*(1-u^2). */
void zx_lens(zx_canvas *c, float cx, float cy, float hw, float offen, float sm, float dicke, uint32_t farbe);
/* UTF-8-Text (Latin-1-Bereich). Gibt die x-Position nach dem Text zurueck. */
int zx_text(zx_canvas *c, int x, int y, const char *utf8, int skala, uint32_t farbe);
int zx_text_breite(const char *utf8, int skala);

#endif
