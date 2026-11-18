/* Linux-Framebuffer (/dev/fb0): Bildpunkte in einen Hintergrundpuffer zeichnen, dann kopieren. */
#ifndef ZYNEX_FB_H
#define ZYNEX_FB_H

#include <stddef.h>
#include <stdint.h>

#include "zynex_gfx.h"

typedef struct {
    int fd;
    uint8_t *map;
    size_t len;
    int w, h, stride, bpp;
    int r_off, g_off, b_off, r_len, g_len, b_len;
    zx_canvas cv; /* Hintergrundpuffer */
} zx_fb;

int zx_fb_open(zx_fb *fb, const char *geraet); /* 0 = ok */
void zx_fb_flip(zx_fb *fb);
void zx_fb_close(zx_fb *fb);

#endif
