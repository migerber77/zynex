/* Das Gesicht von Zynex: Blinzeln, Blick, Atmen, Stimmung, Mund aus der Viseme-Zeitleiste. */
#ifndef ZYNEX_FACE_H
#define ZYNEX_FACE_H

#include <stddef.h>
#include <stdint.h>

#include "zynex_gfx.h"
#include "zynex_visemes.h"

#define ZX_FACE_W 400
#define ZX_FACE_H 300
#define ZX_FACE_MAXVIS 512

typedef enum {
    ZX_NEUTRAL = 0,
    ZX_FREUNDLICH,
    ZX_ZUHOEREN,
    ZX_DENKT,
    ZX_UEBERRASCHT,
    ZX_MUEDE,
    ZX_MOODS
} zx_mood;

typedef struct {
    zx_mood mood;
    uint32_t now, last;
    uint32_t blink_start, next_blink;
    uint32_t next_gaze;
    uint32_t rng;
    float gx, gy, tgx, tgy;      /* Blick aktuell / Ziel, -1..1 */
    float eye_h, eye_w, smile;   /* weich nachgefuehrt */
    float mouth_o, mouth_w;      /* 0..1 / halbe Breite */
    zx_viseme vis[ZX_FACE_MAXVIS];
    size_t nvis, vidx;
    uint32_t vis_total, speak_start;
    int speaking;
} zx_face;

void zx_face_init(zx_face *f, uint32_t now_ms, uint32_t seed);
void zx_face_set_mood(zx_face *f, zx_mood m);
void zx_face_speak(zx_face *f, const char *text);
int zx_face_is_speaking(const zx_face *f);
void zx_face_update(zx_face *f, uint32_t now_ms);
/* 1.0 = Auge offen, 0.0 = Lid geschlossen */
float zx_face_blink_open(const zx_face *f);
/* Zeichnet das Gesicht; (ox,oy) obere linke Ecke, skala = Bildpunkte pro Gesichtseinheit */
void zx_face_draw(const zx_face *f, zx_canvas *c, float ox, float oy, float skala);
const char *zx_mood_name(zx_mood m);

#endif
