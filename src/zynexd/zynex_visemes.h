/* Mundformen aus Text: erzeugt eine Zeitleiste, die das Gesicht abspielt. */
#ifndef ZYNEX_VISEMES_H
#define ZYNEX_VISEMES_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t t_ms;        /* Startzeit ab Beginn der Aeusserung */
    uint32_t byte_index;  /* Position im Text, fuer die Synchronisation mit der Stimme */
    uint16_t open_pm;     /* Muendung des Mundes, 0..1000 */
    uint16_t width;       /* halbe Mundbreite in Bildpunkten (Gesicht 400x400) */
} zx_viseme;

/* Baut die Zeitleiste fuer einen UTF-8-Text. Schreibt hoechstens cap Eintraege.
 * total_ms (darf NULL sein) bekommt die Gesamtdauer des ganzen Textes.
 * Rueckgabe: Anzahl geschriebener Eintraege. */
size_t zx_visemes_build(const char *text, zx_viseme *out, size_t cap, uint32_t *total_ms);

#endif
