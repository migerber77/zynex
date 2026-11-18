#include "zynex_ui.h"

#include <string.h>

#define C_HINTERGRUND 0x0b0e14u
#define C_ZYNEX 0xcfe3f0u
#define C_DU 0x8fa7b8u
#define C_LINIE 0x1e2a3du

void zx_ui_init(zx_ui *u, uint32_t now_ms)
{
    memset(u, 0, sizeof(*u));
    zx_face_init(&u->face, now_ms, now_ms ^ 0x9e3779b9u);
}

void zx_ui_say(zx_ui *u, int von_zynex, const char *text)
{
    if (u->nlog == ZX_UI_LOG) {
        memmove(u->log[0], u->log[1], sizeof(u->log[0]) * (ZX_UI_LOG - 1));
        memmove(u->von_zynex, u->von_zynex + 1, ZX_UI_LOG - 1);
        u->nlog--;
    }
    strncpy(u->log[u->nlog], text, ZX_UI_LINE - 1);
    u->log[u->nlog][ZX_UI_LINE - 1] = '\0';
    u->von_zynex[u->nlog] = (unsigned char)von_zynex;
    u->nlog++;
    if (von_zynex) {
        zx_face_speak(&u->face, text);
        zx_face_set_mood(&u->face, ZX_FREUNDLICH);
    } else {
        zx_face_set_mood(&u->face, ZX_ZUHOEREN);
    }
}

/* Zerlegt text in Zeilen mit hoechstens cols Zeichen (UTF-8-sicher). Schreibt Anfaenge/Laengen. */
static int umbrechen(const char *t, int cols, const char **start, int *len, int max)
{
    int n = 0;
    const char *p = t;

    while (*p && n < max) {
        const char *ls = p;
        const char *letztes_leer = NULL;
        int zeichen = 0;
        while (*p && zeichen < cols) {
            if (*p == ' ')
                letztes_leer = p;
            p++;
            while ((*p & 0xC0) == 0x80)
                p++;
            zeichen++;
        }
        if (*p && letztes_leer && letztes_leer > ls)
            p = letztes_leer + 1;
        start[n] = ls;
        len[n] = (int)(p - ls);
        n++;
    }
    return n;
}

#define C_BG 0x020617u
#define C_PANEL 0x0f172au
#define C_PANEL2 0x111c33u
#define C_RAND 0x1e293bu
#define C_TEXT 0xe2e8f0u
#define C_GRAU 0x94a3b8u
#define C_AKZENT 0x4fd8ffu
#define C_BLAU 0x2563ebu
#define C_GRUEN 0x34d399u
#define C_GELB 0xfbbf24u

/* Rechteck von links/oben mit Breite/Hoehe, abgerundet. */
static void box(zx_canvas *c, int x, int y, int w, int h, int r, uint32_t farbe)
{
    zx_rrect(c, x + w / 2.0f, y + h / 2.0f, w / 2.0f, h / 2.0f, (float)r, farbe);
}

/* Rahmen: erst Randfarbe, dann etwas kleiner die Fuellung. */
static void karte(zx_canvas *c, int x, int y, int w, int h, int r, uint32_t fuell, uint32_t rand)
{
    box(c, x, y, w, h, r, rand);
    box(c, x + 1, y + 1, w - 2, h - 2, r > 1 ? r - 1 : r, fuell);
}

static void text_gekuerzt(zx_canvas *c, int x, int y, const char *t, int skala, uint32_t farbe, int maxbreite)
{
    char buf[200];
    size_t n = strlen(t);
    if (n >= sizeof(buf))
        n = sizeof(buf) - 1;
    memcpy(buf, t, n);
    buf[n] = '\0';
    while (n > 3 && zx_text_breite(buf, skala) > maxbreite) {
        n--;
        while (n > 0 && (buf[n] & 0xC0) == 0x80)
            n--;
        buf[n] = '\0';
    }
    zx_text(c, x, y, buf, skala, farbe);
}

static void seitenleiste(zx_ui *u, zx_canvas *c, int sw, int skala)
{
    static const char *namen[3] = {"Chat", "Notizen", "Ged\xc3\xa4" "chtnis"};
    int i, y, th = 16 * skala + 16;
    float s = ((float)sw - 24.0f) / 400.0f;

    box(c, 0, 0, sw, c->h, 0, C_PANEL);
    box(c, sw - 1, 0, 1, c->h, 0, C_RAND);
    zx_rrect(c, 28.0f, 28.0f, 11.0f, 11.0f, 11.0f, C_AKZENT);
    zx_text(c, 48, 20, "Zynex", skala, C_TEXT);
    zx_rrect(c, (float)sw - 20.0f, 28.0f, 4.5f, 4.5f, 4.5f, u->bereit ? C_GRUEN : C_GELB);

    zx_face_draw(&u->face, c, 12.0f, 50.0f, s);
    y = 50 + (int)(300.0f * s) + 12;
    for (i = 0; i < 3; i++) {
        if (i == u->ansicht)
            karte(c, 10, y, sw - 20, th, 10, C_PANEL2, C_RAND);
        zx_text(c, 24, y + (th - 16 * skala) / 2, namen[i], skala, i == u->ansicht ? C_TEXT : C_GRAU);
        y += th + 4;
    }
    y = c->h - 16 * skala - 12;
    zx_text(c, 14, y, "Tab: Ansicht wechseln", 1, C_GRAU);
    if (u->adresse[0]) {
        y -= 20;
        text_gekuerzt(c, 14, y, u->adresse, 1, C_AKZENT, sw - 24);
        y -= 18;
        zx_text(c, 14, y, "Webseite:", 1, C_GRAU);
    }
}

static void blasen(zx_ui *u, zx_canvas *c, int x0, int x1, int y0, int y1, int skala)
{
    int lh = 16 * skala + 6, pad = 10 * skala, avatar = 30;
    int maxw = (int)((x1 - x0) * 0.74f) - 2 * pad;
    int cols = maxw / (8 * skala), y = y1, i;
    char buf[ZX_UI_LINE + 4];

    if (cols < 10)
        cols = 10;
    for (i = u->nlog - 1; i >= 0 && y > y0; i--) {
        const char *st[24];
        int ln[24], n, k, bw = 0, bh, bx, von = u->von_zynex[i];
        n = umbrechen(u->log[i], cols, st, ln, 24);
        for (k = 0; k < n; k++) {
            int l = ln[k] < (int)sizeof(buf) - 1 ? ln[k] : (int)sizeof(buf) - 1, b;
            memcpy(buf, st[k], (size_t)l);
            buf[l] = '\0';
            b = zx_text_breite(buf, skala);
            if (b > bw)
                bw = b;
        }
        bh = n * lh + 2 * pad - 6;
        y -= bh;
        if (y < y0)
            break; /* aelteste Nachricht passt nicht mehr ganz */
        bw += 2 * pad;
        if (von) {
            bx = x0 + avatar + 8;
            zx_rrect(c, (float)x0 + 13.0f, (float)y + 15.0f, 11.0f, 11.0f, 11.0f, C_AKZENT);
            karte(c, bx, y, bw, bh, 14, C_PANEL2, C_RAND);
        } else {
            bx = x1 - bw;
            box(c, bx, y, bw, bh, 14, C_BLAU);
        }
        for (k = 0; k < n; k++) {
            int l = ln[k] < (int)sizeof(buf) - 1 ? ln[k] : (int)sizeof(buf) - 1;
            memcpy(buf, st[k], (size_t)l);
            buf[l] = '\0';
            zx_text(c, bx + pad, y + pad - 3 + k * lh, buf, skala, C_TEXT);
        }
        y -= 12;
    }
}

static void liste(zx_ui *u, zx_canvas *c, int x0, int x1, int y0, int skala, int ansicht)
{
    int y = y0, i, n = ansicht == 1 ? u->nnotiz : u->nzettel, h = 16 * skala + 18;
    if (n == 0) {
        zx_text(c, x0, y, ansicht == 1 ? "Noch keine Notizen. Lege sie in der Webseite an." : "Noch nichts gemerkt. Sag: merke dir ...", skala, C_GRAU);
        return;
    }
    for (i = 0; i < n && y + h < c->h - 20; i++) {
        karte(c, x0, y, x1 - x0, h, 12, C_PANEL, C_RAND);
        text_gekuerzt(c, x0 + 14, y + 9, ansicht == 1 ? u->notiz[i] : u->zettel[i], skala, C_TEXT, x1 - x0 - 28);
        y += h + 8;
    }
}

void zx_ui_draw(zx_ui *u, zx_canvas *c)
{
    int skala = c->w >= 1500 ? 2 : 1;
    int sw = c->w / 4, hoehe_kopf, eh, x0, x1, ey;
    static const char *titel[3] = {"Gespr\xc3\xa4" "ch", "Notizen", "Ged\xc3\xa4" "chtnis"};

    if (sw < 220)
        sw = 220;
    if (sw > 340 * skala)
        sw = 340 * skala;
    zx_fill(c, C_BG);
    seitenleiste(u, c, sw, skala);

    x0 = sw + 24;
    x1 = c->w - 24;
    hoehe_kopf = 16 * skala + 24;
    zx_text(c, x0, 14, titel[u->ansicht < 3 ? u->ansicht : 0], skala, C_TEXT);
    box(c, sw, hoehe_kopf, c->w - sw, 1, 0, C_RAND);

    eh = 16 * skala + 22;
    ey = c->h - eh - 18;
    if (u->ansicht == 0) {
        if (u->nlog == 0)
            zx_text(c, x0, hoehe_kopf + 24, "Hallo, ich bin Zynex. Schreib mir etwas.", skala, C_GRAU);
        blasen(u, c, x0, x1, hoehe_kopf + 12, ey - 14, skala);
        /* Eingabefeld */
        karte(c, x0, ey, x1 - x0, eh, 16, C_PANEL, u->eingabe[0] ? C_AKZENT : C_RAND);
        {
            int tx = x0 + 16, ty = ey + (eh - 16 * skala) / 2, x;
            if (u->eingabe[0]) {
                int maxb = x1 - x0 - 40;
                const char *t = u->eingabe;
                while (zx_text_breite(t, skala) > maxb && *t) /* Ende der Zeile zeigen */
                    t++;
                x = zx_text(c, tx, ty, t, skala, C_TEXT);
            } else {
                zx_text(c, tx, ty, "Schreib Zynex etwas ...", skala, C_GRAU);
                x = tx;
            }
            if ((u->face.now / 500u) % 2u == 0u)
                zx_rrect(c, (float)x + 2.0f * skala, (float)(ty + 8 * skala), 1.5f * skala, 7.0f * skala, 0.5f, C_AKZENT);
        }
    } else {
        liste(u, c, x0, x1, hoehe_kopf + 18, skala, u->ansicht);
    }
}
