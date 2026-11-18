#include "zynex_mem.h"

#include <stdio.h>
#include <string.h>

static void kopiere(char *ziel, size_t cap, const char *quelle)
{
    size_t n = strlen(quelle);
    if (n >= cap)
        n = cap - 1;
    memcpy(ziel, quelle, n);
    ziel[n] = '\0';
}

static void speichern(zx_mem *m)
{
    FILE *f;
    int i;
    if (!m->pfad[0])
        return;
    f = fopen(m->pfad, "w");
    if (!f) {
        m->speicherbar = 0;
        return;
    }
    for (i = 0; i < m->n; i++)
        fprintf(f, "%s\n", m->zettel[i]);
    m->speicherbar = fclose(f) == 0;
}

void zx_mem_init(zx_mem *m, const char *pfad)
{
    FILE *f;
    char zeile[ZX_MEM_LEN + 8];

    memset(m, 0, sizeof(*m));
    if (!pfad || !pfad[0])
        return;
    kopiere(m->pfad, sizeof(m->pfad), pfad);
    f = fopen(m->pfad, "r");
    if (f) {
        while (m->n < ZX_MEM_MAX && fgets(zeile, sizeof(zeile), f)) {
            size_t l = strlen(zeile);
            while (l > 0 && (zeile[l - 1] == '\n' || zeile[l - 1] == '\r'))
                zeile[--l] = '\0';
            if (l > 0)
                kopiere(m->zettel[m->n++], ZX_MEM_LEN, zeile);
        }
        fclose(f);
        m->speicherbar = 1;
    } else {
        f = fopen(m->pfad, "a");
        m->speicherbar = f != NULL;
        if (f)
            fclose(f);
    }
}

static char klein(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
}

/* Pruefen, ob text mit praefix beginnt (ohne Gross/Klein). Rueckgabe: Zeiger hinter dem Praefix oder NULL. */
static const char *beginnt(const char *text, const char *praefix)
{
    while (*praefix) {
        if (klein(*text) != *praefix)
            return NULL;
        text++;
        praefix++;
    }
    return text;
}

static const char *ueberspringe(const char *p)
{
    while (*p == ' ' || *p == ':' || *p == ',' || *p == '\t')
        p++;
    return p;
}

static int enthaelt(const char *text, const char *stichwort)
{
    size_t n = strlen(stichwort);
    for (; *text; text++) {
        size_t i = 0;
        while (i < n && text[i] && klein(text[i]) == stichwort[i])
            i++;
        if (i == n)
            return 1;
    }
    return 0;
}

int zx_mem_befehl(zx_mem *m, const char *text, char *out, size_t cap)
{
    const char *p;
    int i;

    if ((p = beginnt(text, "merke dir")) != NULL || (p = beginnt(text, "merk dir")) != NULL ||
        (p = beginnt(text, "merke")) != NULL) {
        char zettel[ZX_MEM_LEN];
        size_t l;
        p = ueberspringe(p);
        if (!*p) {
            snprintf(out, cap, "Was soll ich mir merken?");
            return 1;
        }
        kopiere(zettel, sizeof(zettel), p);
        l = strlen(zettel);
        while (l > 0 && (zettel[l - 1] == ' ' || zettel[l - 1] == '.'))
            zettel[--l] = '\0';
        if (m->n >= ZX_MEM_MAX) {
            snprintf(out, cap, "Mein Merkzettel ist voll. Sag 'vergiss alles' oder 'vergiss' mit einem Stichwort.");
            return 1;
        }
        kopiere(m->zettel[m->n++], ZX_MEM_LEN, zettel);
        speichern(m);
        snprintf(out, cap, m->pfad[0] && !m->speicherbar
                               ? "Gemerkt, aber nur bis zum Ausschalten."
                               : "Gemerkt: %s.", zettel);
        return 1;
    }
    if (beginnt(text, "vergiss alles") || beginnt(text, "vergiss alle")) {
        m->n = 0;
        m->nv = 0;
        speichern(m);
        snprintf(out, cap, "Okay, ich habe alles vergessen.");
        return 1;
    }
    if ((p = beginnt(text, "vergiss")) != NULL) {
        int geloescht = 0;
        p = ueberspringe(p);
        if (!*p) {
            snprintf(out, cap, "Was soll ich vergessen?");
            return 1;
        }
        for (i = m->n - 1; i >= 0; i--) {
            if (enthaelt(m->zettel[i], p) || strstr(m->zettel[i], p)) {
                memmove(m->zettel[i], m->zettel[i + 1], (size_t)(m->n - i - 1) * ZX_MEM_LEN);
                m->n--;
                geloescht++;
            }
        }
        speichern(m);
        if (geloescht)
            snprintf(out, cap, "Okay, %d Merkzettel vergessen.", geloescht);
        else
            snprintf(out, cap, "Dazu habe ich nichts gemerkt.");
        return 1;
    }
    if (enthaelt(text, "was weisst du") || enthaelt(text, "was hast du dir gemerkt") ||
        enthaelt(text, "zeig dein gedaechtnis") || enthaelt(text, "zeig mein gedaechtnis") ||
        enthaelt(text, "was weiss du")) {
        size_t o = 0;
        if (m->n == 0) {
            snprintf(out, cap, "Ich habe mir noch nichts gemerkt. Sag 'merke dir ...'.");
            return 1;
        }
        o += (size_t)snprintf(out, cap, "Ich habe gemerkt: ");
        for (i = 0; i < m->n && o + 2 < cap; i++)
            o += (size_t)snprintf(out + o, cap - o, "%s%s", i ? "; " : "", m->zettel[i]);
        return 1;
    }
    return 0;
}

void zx_mem_system(const zx_mem *m, const char *basis, char *out, size_t cap)
{
    size_t o;
    int i;
    o = (size_t)snprintf(out, cap, "%s", basis);
    if (m->n > 0 && o < cap) {
        o += (size_t)snprintf(out + o, cap - o, " Das weisst du ueber den Nutzer:");
        for (i = 0; i < m->n && o < cap; i++)
            o += (size_t)snprintf(out + o, cap - o, " %s.", m->zettel[i]);
    }
}

void zx_mem_verlauf(const zx_mem *m, char *out, size_t cap)
{
    size_t o = 0;
    int i;
    if (cap)
        out[0] = '\0';
    for (i = 0; i < m->nv && o + 1 < cap; i++)
        o += (size_t)snprintf(out + o, cap - o,
                              "<|im_start|>user\n%s<|im_end|>\n<|im_start|>assistant\n%s<|im_end|>\n",
                              m->frage[i], m->antw[i]);
}

void zx_mem_verlauf_add(zx_mem *m, const char *frage, const char *antwort)
{
    if (m->nv == ZX_VERLAUF_N) {
        memmove(m->frage[0], m->frage[1], (size_t)(ZX_VERLAUF_N - 1) * ZX_VERLAUF_LEN);
        memmove(m->antw[0], m->antw[1], (size_t)(ZX_VERLAUF_N - 1) * ZX_VERLAUF_LEN);
        m->nv--;
    }
    kopiere(m->frage[m->nv], ZX_VERLAUF_LEN, frage);
    kopiere(m->antw[m->nv], ZX_VERLAUF_LEN, antwort);
    m->nv++;
}

int zx_mem_neu(zx_mem *m, const char *text)
{
    if (!text || !text[0] || m->n >= ZX_MEM_MAX)
        return 0;
    kopiere(m->zettel[m->n++], ZX_MEM_LEN, text);
    speichern(m);
    return 1;
}

int zx_mem_loesche(zx_mem *m, int index)
{
    if (index < 0 || index >= m->n)
        return 0;
    memmove(m->zettel[index], m->zettel[index + 1], (size_t)(m->n - index - 1) * ZX_MEM_LEN);
    m->n--;
    speichern(m);
    return 1;
}

void zx_mem_alles_loeschen(zx_mem *m)
{
    m->n = 0;
    m->nv = 0;
    speichern(m);
}
