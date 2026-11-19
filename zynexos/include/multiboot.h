#pragma once
#include <stdint.h>

/* Multiboot2 */
#define MULTIBOOT2_MAGIC 0x36D76289

#define MB2_TAG_END         0
#define MB2_TAG_CMDLINE     1
#define MB2_TAG_MODULE      3
#define MB2_TAG_MMAP        6
#define MB2_TAG_FRAMEBUFFER 8
#define MB2_TAG_ZYNEX       0x7A58      /* eigene Marke des Zynex-Laders: u32 Flags */
#define BOOT_SIGNIERT       1           /* Kernel und Kommandozeile waren durch eine gueltige LMS-Signatur gedeckt */
#define BOOT_SECUREBOOT     4           /* UEFI Secure Boot war beim Start aktiv */
#define BOOT_RGB            2           /* Bildschirm in RGB-Reihenfolge (Rot/Blau tauschen) */

struct mb2_tag {
    uint32_t type;
    uint32_t size;
} __attribute__((packed));

struct mb2_mmap_entry {
    uint64_t addr;
    uint64_t len;
    uint32_t type;
    uint32_t reserved;
} __attribute__((packed));

struct mb2_tag_framebuffer {
    uint32_t type;
    uint32_t size;
    uint64_t addr;
    uint32_t pitch;
    uint32_t width;
    uint32_t height;
    uint8_t  bpp;
    uint8_t  fb_type;     /* 0 indexed, 1 RGB, 2 EGA-Text */
    uint16_t reserved;
} __attribute__((packed));

#define MULTIBOOT_MEMORY_AVAILABLE 1

/* Vom Kernel kopierte Boot-Informationen (die Multiboot-Struktur selbst
 * liegt in Speicher, den der Kernel spaeter weiterverwendet). */
#define BI_MAX_REGIONS 256
struct boot_region { uint64_t addr, len; uint32_t type; };

struct boot_info {
    int      nregions;
    struct boot_region regions[BI_MAX_REGIONS];
    int      has_fb;
    uint64_t fb_addr;
    uint32_t fb_pitch, fb_width, fb_height;
    uint8_t  fb_bpp, fb_type;
    char     cmdline[128];
    uint32_t flags;                 /* BOOT_* */
    uint64_t model_addr, model_size; /* Sprachmodell (GGUF) im RAM, 0 = keins */
    uint64_t mod_addr, mod_size;    /* erstes Multiboot-Modul (Installationspaket), 0 = keins */
};

extern struct boot_info g_boot;
void bootinfo_parse(uint32_t mbi_addr);
