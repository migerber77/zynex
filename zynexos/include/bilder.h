#pragma once
#include <stdint.h>
/* Bild-Dekoder (PNG, JPEG baseline, GIF) -> 0x00RRGGBB. arb = Hilfsspeicher (PNG: komprimiert + Rohdaten, JPEG: Farbebenen). */
#define BILD_UNBEKANNT -1
#define BILD_ZU_GROSS -2
#define BILD_DEFEKT -3
#define BILD_NICHT_UNTERSTUETZT -4
int bild_dekodieren(const uint8_t* d, uint32_t n, uint32_t* out, uint32_t maxpx, uint8_t* arb, uint32_t arbmax, int* w, int* h);
