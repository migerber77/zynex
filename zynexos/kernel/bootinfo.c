/*
 * Zynex OS - Multiboot2-Informationen einlesen und kopieren
 */
#include "multiboot.h"

struct boot_info g_boot;

void bootinfo_parse(uint32_t mbi_addr) {
    const uint8_t* base = (const uint8_t*)(uintptr_t)mbi_addr;
    uint32_t total = *(const uint32_t*)base;
    const uint8_t* p = base + 8;
    const uint8_t* end = base + total;

    while (p + sizeof(struct mb2_tag) <= end) {
        const struct mb2_tag* t = (const struct mb2_tag*)p;
        if (t->type == MB2_TAG_END) break;

        if (t->type == MB2_TAG_MMAP) {
            uint32_t esize = *(const uint32_t*)(p + 8);
            const uint8_t* e = p + 16;
            const uint8_t* tend = p + t->size;
            while (esize >= sizeof(struct mb2_mmap_entry) && e + esize <= tend) {
                const struct mb2_mmap_entry* m = (const struct mb2_mmap_entry*)e;
                if (g_boot.nregions < BI_MAX_REGIONS) {
                    g_boot.regions[g_boot.nregions].addr = m->addr;
                    g_boot.regions[g_boot.nregions].len  = m->len;
                    g_boot.regions[g_boot.nregions].type = m->type;
                    g_boot.nregions++;
                }
                e += esize;
            }
        } else if (t->type == MB2_TAG_FRAMEBUFFER) {
            const struct mb2_tag_framebuffer* f = (const struct mb2_tag_framebuffer*)p;
            g_boot.has_fb    = 1;
            g_boot.fb_addr   = f->addr;
            g_boot.fb_pitch  = f->pitch;
            g_boot.fb_width  = f->width;
            g_boot.fb_height = f->height;
            g_boot.fb_bpp    = f->bpp;
            g_boot.fb_type   = f->fb_type;
        } else if (t->type == MB2_TAG_ZYNEX) {
            g_boot.flags = *(const uint32_t*)(p + 8);
        } else if (t->type == 0x7A59) {
            g_boot.model_addr = *(const uint64_t*)(p + 8); g_boot.model_size = *(const uint64_t*)(p + 16);
        } else if (t->type == MB2_TAG_MODULE && g_boot.mod_size == 0) {
            uint32_t ms = *(const uint32_t*)(p + 8), me = *(const uint32_t*)(p + 12);
            if (me > ms) { g_boot.mod_addr = ms; g_boot.mod_size = me - ms; }
        } else if (t->type == MB2_TAG_CMDLINE) {
            const char* s = (const char*)(p + 8);
            int i = 0;
            while (s[i] && i < (int)sizeof(g_boot.cmdline) - 1) { g_boot.cmdline[i] = s[i]; i++; }
            g_boot.cmdline[i] = 0;
        }
        p += (t->size + 7) & ~7u;
    }
}
