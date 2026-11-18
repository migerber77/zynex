#include "zynex_gfx.h"

#include <math.h>
#include <string.h>

void zx_fill(zx_canvas *c, uint32_t farbe)
{
    int n = c->w * c->h, i;
    for (i = 0; i < n; i++)
        c->px[i] = farbe;
}

void zx_blend(zx_canvas *c, int x, int y, uint32_t f, float a)
{
    uint32_t d, r, g, b;
    float ia;

    if (x < 0 || y < 0 || x >= c->w || y >= c->h || a <= 0.0f)
        return;
    if (a > 1.0f)
        a = 1.0f;
    ia = 1.0f - a;
    d = c->px[y * c->w + x];
    r = (uint32_t)(((d >> 16) & 255) * ia + ((f >> 16) & 255) * a + 0.5f);
    g = (uint32_t)(((d >> 8) & 255) * ia + ((f >> 8) & 255) * a + 0.5f);
    b = (uint32_t)((d & 255) * ia + (f & 255) * a + 0.5f);
    c->px[y * c->w + x] = (r << 16) | (g << 8) | b;
}

void zx_rrect(zx_canvas *c, float cx, float cy, float hw, float hh, float r, uint32_t f)
{
    int x0 = (int)floorf(cx - hw) - 1, x1 = (int)ceilf(cx + hw) + 1;
    int y0 = (int)floorf(cy - hh) - 1, y1 = (int)ceilf(cy + hh) + 1;
    int x, y;

    if (hw <= 0.0f || hh <= 0.0f)
        return;
    if (r > hw)
        r = hw;
    if (r > hh)
        r = hh;
    for (y = y0; y <= y1; y++) {
        for (x = x0; x <= x1; x++) {
            float qx = fabsf(x + 0.5f - cx) - (hw - r);
            float qy = fabsf(y + 0.5f - cy) - (hh - r);
            float mx = qx > 0 ? qx : 0, my = qy > 0 ? qy : 0;
            float d = sqrtf(mx * mx + my * my) + fminf(fmaxf(qx, qy), 0.0f) - r;
            zx_blend(c, x, y, f, 0.5f - d);
        }
    }
}

void zx_lens(zx_canvas *c, float cx, float cy, float hw, float offen, float sm, float dicke, uint32_t f)
{
    int x0 = (int)floorf(cx - hw) - 1, x1 = (int)ceilf(cx + hw) + 1;
    int x, y;
    int ymin = (int)floorf(cy - fabsf(sm) - offen - dicke) - 2;
    int ymax = (int)ceilf(cy + fabsf(sm) + offen + dicke) + 2;

    if (hw <= 0.0f)
        return;
    for (x = x0; x <= x1; x++) {
        float u = (x + 0.5f - cx) / hw;
        float k, yc, ht;
        if (u < -1.0f || u > 1.0f)
            continue;
        k = 1.0f - u * u;
        yc = cy + sm * k;
        ht = dicke * 0.5f + offen * k;
        /* Enden etwas verjuengen, damit die Linse spitz zuläuft */
        for (y = ymin; y <= ymax; y++)
            zx_blend(c, x, y, f, ht - fabsf(y + 0.5f - yc) + 0.5f);
    }
}

static int lies_zeichen(const unsigned char **p)
{
    const unsigned char *s = *p;
    int cp;

    if (s[0] < 0x80) {
        cp = s[0];
        *p = s + 1;
    } else if ((s[0] & 0xE0) == 0xC0 && (s[1] & 0xC0) == 0x80) {
        cp = ((s[0] & 0x1F) << 6) | (s[1] & 0x3F);
        *p = s + 2;
    } else {
        cp = '?';
        s++;
        while ((*s & 0xC0) == 0x80)
            s++;
        *p = s;
    }
    if (cp < 32 || cp > 255)
        cp = '?';
    return cp;
}

int zx_text(zx_canvas *c, int x, int y, const char *t, int skala, uint32_t f)
{
    const unsigned char *p = (const unsigned char *)t;

    while (*p) {
        int cp = lies_zeichen(&p);
        const unsigned char *g = zx_font8x16[cp - 32];
        int row, col, sx, sy;
        for (row = 0; row < 16; row++)
            for (col = 0; col < 8; col++)
                if (g[row] & (0x80 >> col))
                    for (sy = 0; sy < skala; sy++)
                        for (sx = 0; sx < skala; sx++)
                            zx_blend(c, x + col * skala + sx, y + row * skala + sy, f, 1.0f);
        x += 8 * skala;
    }
    return x;
}

int zx_text_breite(const char *t, int skala)
{
    const unsigned char *p = (const unsigned char *)t;
    int n = 0;
    while (*p) {
        (void)lies_zeichen(&p);
        n++;
    }
    return n * 8 * skala;
}
