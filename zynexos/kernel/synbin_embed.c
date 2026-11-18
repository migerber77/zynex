/* Eingebaute .synbin-Programme (Binaerdateien werden beim Bauen eingebunden) */
#include <stdint.h>

#define EMBED(sym, file) \
    __asm__(".section .rodata\n.balign 16\n.global " #sym "_start\n" #sym "_start:\n" \
            ".incbin \"" file "\"\n.global " #sym "_end\n" #sym "_end:\n.previous\n")

EMBED(prog_hallo,   "build/hallo.synbin");
EMBED(prog_rechnen, "build/rechnen.synbin");
EMBED(prog_zeit,    "build/zeit.synbin");
EMBED(prog_schleife,"build/schleife.synbin");

extern const uint8_t prog_hallo_start[], prog_hallo_end[];
extern const uint8_t prog_rechnen_start[], prog_rechnen_end[];
extern const uint8_t prog_zeit_start[], prog_zeit_end[];
extern const uint8_t prog_schleife_start[], prog_schleife_end[];

struct synbin_embedded { const char* name; const uint8_t* start; const uint8_t* end; };

const struct synbin_embedded synbin_embedded_list[] = {
    { "hallo.synbin",   prog_hallo_start,   prog_hallo_end },
    { "rechnen.synbin", prog_rechnen_start, prog_rechnen_end },
    { "zeit.synbin",    prog_zeit_start,    prog_zeit_end },
    { "schleife.synbin", prog_schleife_start, prog_schleife_end },
};
const int synbin_embedded_count = 4;
