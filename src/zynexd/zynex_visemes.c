#include "zynex_visemes.h"

#include <string.h>

struct form {
    uint16_t open_pm;
    uint16_t width;
    uint16_t dauer_ms;
};

/* Zeichen -> Mundform. Gleiche Werte wie im Browser-Prototyp. */
static int form_fuer(uint32_t cp, struct form *f)
{
    switch (cp) {
    case 'a': *f = (struct form){1000, 24, 95}; return 1;
    case 0xE4: *f = (struct form){600, 28, 95}; return 1;  /* ae */
    case 'e': *f = (struct form){550, 28, 95}; return 1;
    case 'i': *f = (struct form){300, 32, 95}; return 1;
    case 'o': *f = (struct form){850, 15, 95}; return 1;
    case 0xF6: *f = (struct form){600, 15, 95}; return 1;  /* oe */
    case 'u': *f = (struct form){450, 12, 95}; return 1;
    case 0xFC: *f = (struct form){400, 13, 95}; return 1;  /* ue */
    case 'y': *f = (struct form){300, 28, 95}; return 1;
    case 'm': case 'b': case 'p':
        *f = (struct form){0, 24, 70};
        return 1;
    case 'f': case 'v': case 'w':
        *f = (struct form){140, 26, 70};
        return 1;
    case 's': case 'z': case 'c': case 'j': case 't': case 'd': case 'n':
    case 'l': case 'r': case 'k': case 'g': case 'q': case 'x': case 'h':
        *f = (struct form){250, 25, 70};
        return 1;
    case 0xDF: /* sz */
        *f = (struct form){250, 25, 70};
        return 1;
    case ' ': *f = (struct form){60, 24, 45}; return 1;
    case '.': case '!': case '?':
        *f = (struct form){0, 24, 380};
        return 1;
    case ',': case ';': case ':': case '-':
        *f = (struct form){0, 24, 220};
        return 1;
    default:
        return 0;
    }
}

/* Liest ein Zeichen (ASCII oder zweibytiges UTF-8). Gibt die Laenge in Bytes
 * zurueck, 0 am Textende. Zeichen ausserhalb davon liefern cp = 0. */
static size_t lies(const unsigned char *p, uint32_t *cp)
{
    if (p[0] == 0)
        return 0;
    if (p[0] < 0x80) {
        uint32_t c = p[0];
        if (c >= 'A' && c <= 'Z')
            c += 32;
        *cp = c;
        return 1;
    }
    if ((p[0] & 0xE0) == 0xC0 && (p[1] & 0xC0) == 0x80) {
        uint32_t c = ((uint32_t)(p[0] & 0x1F) << 6) | (uint32_t)(p[1] & 0x3F);
        if (c == 0xC4 || c == 0xD6 || c == 0xDC)
            c += 32; /* Grossbuchstabe -> Kleinbuchstabe */
        *cp = c;
        return 2;
    }
    *cp = 0;
    if ((p[0] & 0xF0) == 0xE0)
        return (p[1] && p[2]) ? 3 : 1;
    if ((p[0] & 0xF8) == 0xF0)
        return (p[1] && p[2] && p[3]) ? 4 : 1;
    return 1;
}

size_t zx_visemes_build(const char *text, zx_viseme *out, size_t cap, uint32_t *total_ms)
{
    const unsigned char *p = (const unsigned char *)text;
    size_t n = 0;
    uint32_t t = 0;

    if (total_ms)
        *total_ms = 0;
    if (text == NULL)
        return 0;

    while (*p) {
        uint32_t cp = 0;
        size_t len = lies(p, &cp);
        struct form f;

        if (len == 0)
            break;
        if (cp != 0 && form_fuer(cp, &f)) {
            if (out != NULL && n < cap) {
                out[n].t_ms = t;
                out[n].byte_index = (uint32_t)(p - (const unsigned char *)text);
                out[n].open_pm = f.open_pm;
                out[n].width = f.width;
                n++;
            }
            t += f.dauer_ms;
        }
        p += len;
    }
    if (total_ms)
        *total_ms = t;
    return n;
}
