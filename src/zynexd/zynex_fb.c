#include "zynex_fb.h"

#include <fcntl.h>
#include <linux/fb.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

int zx_fb_open(zx_fb *fb, const char *geraet)
{
    struct fb_var_screeninfo v;
    struct fb_fix_screeninfo x;

    memset(fb, 0, sizeof(*fb));
    fb->fd = open(geraet, O_RDWR);
    if (fb->fd < 0)
        return -1;
    if (ioctl(fb->fd, FBIOGET_VSCREENINFO, &v) < 0 || ioctl(fb->fd, FBIOGET_FSCREENINFO, &x) < 0)
        goto fehler;
    if (v.bits_per_pixel != 32 && v.bits_per_pixel != 16)
        goto fehler;
    fb->w = (int)v.xres;
    fb->h = (int)v.yres;
    fb->bpp = (int)v.bits_per_pixel;
    fb->stride = (int)x.line_length;
    fb->r_off = (int)v.red.offset;
    fb->g_off = (int)v.green.offset;
    fb->b_off = (int)v.blue.offset;
    fb->r_len = (int)v.red.length;
    fb->g_len = (int)v.green.length;
    fb->b_len = (int)v.blue.length;
    fb->len = (size_t)fb->stride * (size_t)v.yres;
    fb->map = mmap(NULL, fb->len, PROT_READ | PROT_WRITE, MAP_SHARED, fb->fd, 0);
    if (fb->map == MAP_FAILED)
        goto fehler;
    fb->cv.w = fb->w;
    fb->cv.h = fb->h;
    fb->cv.px = calloc((size_t)fb->w * (size_t)fb->h, sizeof(uint32_t));
    if (!fb->cv.px) {
        munmap(fb->map, fb->len);
        goto fehler;
    }
    return 0;
fehler:
    close(fb->fd);
    fb->fd = -1;
    return -1;
}

void zx_fb_flip(zx_fb *fb)
{
    int y, x;

    for (y = 0; y < fb->h; y++) {
        const uint32_t *src = fb->cv.px + (size_t)y * (size_t)fb->w;
        uint8_t *dst = fb->map + (size_t)y * (size_t)fb->stride;
        if (fb->bpp == 32 && fb->r_off == 16 && fb->g_off == 8 && fb->b_off == 0) {
            memcpy(dst, src, (size_t)fb->w * 4);
        } else if (fb->bpp == 32) {
            uint32_t *d = (uint32_t *)dst;
            for (x = 0; x < fb->w; x++) {
                uint32_t p = src[x];
                d[x] = (((p >> 16) & 255) << fb->r_off) | (((p >> 8) & 255) << fb->g_off) | ((p & 255) << fb->b_off);
            }
        } else {
            uint16_t *d = (uint16_t *)dst;
            for (x = 0; x < fb->w; x++) {
                uint32_t p = src[x];
                uint32_t r = ((p >> 16) & 255) >> (8 - fb->r_len);
                uint32_t g = ((p >> 8) & 255) >> (8 - fb->g_len);
                uint32_t b = (p & 255) >> (8 - fb->b_len);
                d[x] = (uint16_t)((r << fb->r_off) | (g << fb->g_off) | (b << fb->b_off));
            }
        }
    }
}

void zx_fb_close(zx_fb *fb)
{
    if (fb->fd >= 0) {
        munmap(fb->map, fb->len);
        free(fb->cv.px);
        close(fb->fd);
        fb->fd = -1;
    }
}
