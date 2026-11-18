#include "zynex_face.h"

#include <math.h>
#include <string.h>

#define C_PLATTE 0x131925u
#define C_RAND 0x24344cu
#define C_AUGE 0x4fd8ffu
#define C_LICHT 0xe8fbffu

static const struct {
    float eye_h, eye_w, smile, gx, gy;
} STIMMUNG[ZX_MOODS] = {
    /* Neutral      */ {1.00f, 1.00f, 0.0f, 0.0f, 0.0f},
    /* Freundlich   */ {0.85f, 1.00f, 9.0f, 0.0f, 0.0f},
    /* Zuhoeren     */ {1.10f, 1.05f, 3.0f, 0.0f, 0.0f},
    /* Denkt        */ {0.80f, 0.95f, -2.0f, -0.6f, -0.7f},
    /* Ueberrascht  */ {1.25f, 1.05f, 0.0f, 0.0f, 0.0f},
    /* Mueede       */ {0.50f, 1.00f, -3.0f, 0.0f, 0.4f},
};

static const char *NAMEN[ZX_MOODS] = {"neutral", "freundlich", "zuhoeren", "denkt", "ueberrascht", "mueede"};

const char *zx_mood_name(zx_mood m) { return (m >= 0 && m < ZX_MOODS) ? NAMEN[m] : "?"; }

static uint32_t zufall(zx_face *f)
{
    f->rng = f->rng * 1664525u + 1013904223u;
    return f->rng >> 8;
}

void zx_face_init(zx_face *f, uint32_t now, uint32_t seed)
{
    memset(f, 0, sizeof(*f));
    f->rng = seed ? seed : 12345u;
    f->now = f->last = now;
    f->next_blink = now + 1500;
    f->blink_start = now - 1000;
    f->next_gaze = now + 2000;
    f->eye_h = f->eye_w = 1.0f;
    f->mouth_w = 24.0f;
}

void zx_face_set_mood(zx_face *f, zx_mood m)
{
    if (m >= 0 && m < ZX_MOODS)
        f->mood = m;
}

void zx_face_speak(zx_face *f, const char *text)
{
    f->nvis = zx_visemes_build(text, f->vis, ZX_FACE_MAXVIS, &f->vis_total);
    f->vidx = 0;
    f->speak_start = f->now;
    f->speaking = f->nvis > 0;
}

int zx_face_is_speaking(const zx_face *f) { return f->speaking; }

float zx_face_blink_open(const zx_face *f)
{
    uint32_t dt = f->now - f->blink_start;
    if (dt < 170u) {
        float p = (float)dt / 170.0f;
        return fabsf(1.0f - 2.0f * p);
    }
    return 1.0f;
}

static float weich(float ist, float soll, float dt, float tau)
{
    return ist + (soll - ist) * (1.0f - expf(-dt / tau));
}

void zx_face_update(zx_face *f, uint32_t now)
{
    float dt = (float)(now - f->last);
    float tm = 0.0f, tw = 24.0f;
    float sgx, sgy;

    f->now = now;
    f->last = now;
    if (dt > 200.0f)
        dt = 200.0f;

    /* Blinzeln, manchmal doppelt */
    if ((int32_t)(now - f->next_blink) >= 0) {
        f->blink_start = now;
        f->next_blink = now + ((zufall(f) % 5) == 0 ? 260u : 2200u + zufall(f) % 3500u);
    }

    /* Blick wandert */
    if ((int32_t)(now - f->next_gaze) >= 0) {
        f->tgx = ((int)(zufall(f) % 200) - 100) / 100.0f * 0.7f;
        f->tgy = ((int)(zufall(f) % 200) - 100) / 100.0f * 0.4f;
        f->next_gaze = now + 1200u + zufall(f) % 3000u;
    }
    sgx = f->tgx + STIMMUNG[f->mood].gx;
    sgy = f->tgy + STIMMUNG[f->mood].gy;
    f->gx = weich(f->gx, sgx > 1 ? 1 : (sgx < -1 ? -1 : sgx), dt, 120.0f);
    f->gy = weich(f->gy, sgy > 1 ? 1 : (sgy < -1 ? -1 : sgy), dt, 120.0f);

    /* Stimmung */
    f->eye_h = weich(f->eye_h, STIMMUNG[f->mood].eye_h, dt, 150.0f);
    f->eye_w = weich(f->eye_w, STIMMUNG[f->mood].eye_w, dt, 150.0f);
    f->smile = weich(f->smile, STIMMUNG[f->mood].smile, dt, 150.0f);

    /* Mund */
    if (f->speaking) {
        uint32_t t = now - f->speak_start;
        if (t >= f->vis_total) {
            f->speaking = 0;
        } else {
            while (f->vidx + 1 < f->nvis && f->vis[f->vidx + 1].t_ms <= t)
                f->vidx++;
            if (f->nvis > 0 && f->vis[f->vidx].t_ms <= t) {
                tm = f->vis[f->vidx].open_pm / 1000.0f;
                tw = (float)f->vis[f->vidx].width;
            }
        }
    }
    f->mouth_o = weich(f->mouth_o, tm, dt, 45.0f);
    f->mouth_w = weich(f->mouth_w, tw, dt, 45.0f);
}

void zx_face_draw(const zx_face *f, zx_canvas *c, float ox, float oy, float s)
{
    float atem = sinf((float)f->now / 1800.0f) * 2.0f;
    float lid = zx_face_blink_open(f);
    float eh = 40.0f * f->eye_h * (0.08f + 0.92f * lid);
    float ew = 32.0f * f->eye_w;
    float exs[2] = {132.0f, 268.0f};
    float ey = 112.0f + atem;
    int i;

    /* Platte */
    zx_rrect(c, ox + 200 * s, oy + 150 * s, 200 * s, 150 * s, 46 * s, C_RAND);
    zx_rrect(c, ox + 200 * s, oy + 150 * s, 198 * s, 148 * s, 44 * s, C_PLATTE);

    /* Augen mit Lichtpunkt */
    for (i = 0; i < 2; i++) {
        float x = exs[i] + f->gx * 12.0f;
        float y = ey + f->gy * 9.0f;
        zx_rrect(c, ox + x * s, oy + y * s, ew * s, eh * s, (ew < eh ? ew : eh) * 0.8f * s, C_AUGE);
        if (lid > 0.5f)
            zx_rrect(c, ox + (x - 10) * s, oy + (y - 14 * f->eye_h) * s, 7 * s, 7 * s, 3.5f * s, C_LICHT);
    }

    /* Mund */
    zx_lens(c, ox + 200 * s, oy + (212.0f + atem) * s, f->mouth_w * s, f->mouth_o * 17.0f * s, f->smile * s, 3.2f * s, C_AUGE);
}
