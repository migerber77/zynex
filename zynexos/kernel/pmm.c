/*
 * Zynex OS - Physical Memory Manager
 * Simple bitmap-based frame allocator.
 */

#include "pmm.h"
#include "multiboot.h"

/* Bitmap: 1 bit per page. 1 = used, 0 = free */
static uint32_t bitmap[PMM_MAX_PAGES / 32];
static uint32_t total_pages = 0;
static uint32_t used_pages  = 0;
static uint32_t highest_page = 0;

/* External early print functions from kernel.c */
extern void kprint(const char* str);
extern void kprint_hex(uint32_t val);
extern void kprint_dec(uint32_t val);

static inline void bitmap_set(uint32_t bit) {
    bitmap[bit / 32] |= (1u << (bit % 32));
}

static inline void bitmap_clear(uint32_t bit) {
    bitmap[bit / 32] &= ~(1u << (bit % 32));
}

static inline int bitmap_test(uint32_t bit) {
    return (bitmap[bit / 32] >> (bit % 32)) & 1;
}

/* Mark a range of physical memory as used or free */
static void pmm_mark_region(uint64_t base, uint64_t length, int used) {
    uint64_t start = PAGE_ALIGN_DOWN(base);
    uint64_t end   = PAGE_ALIGN_UP(base + length);

    for (uint64_t addr = start; addr < end; addr += PAGE_SIZE) {
        uint32_t pfn = (uint32_t)ADDR_TO_PFN(addr);
        if (ADDR_TO_PFN(addr) >= PMM_MAX_PAGES) break;

        if (used) {
            if (!bitmap_test(pfn)) {
                bitmap_set(pfn);
                used_pages++;
            }
        } else {
            if (bitmap_test(pfn)) {
                bitmap_clear(pfn);
                if (used_pages > 0) used_pages--;
            }
        }

        if (pfn > highest_page) highest_page = pfn;
    }
}

extern char _kernel_start[];
extern char _kernel_end[];

void pmm_init(void) {
    /* Alles zunaechst als belegt markieren (sicherer Standard) */
    for (uint32_t i = 0; i < PMM_MAX_PAGES / 32; i++) {
        bitmap[i] = 0xFFFFFFFF;
    }
    used_pages = PMM_MAX_PAGES;
    total_pages = 0;
    highest_page = 0;

    if (g_boot.nregions == 0) {
        kprint("[PMM] Keine Speicherkarte vom Bootloader!\n");
        return;
    }

    kprint("[PMM] Speicherkarte (Multiboot2)...\n");

    for (int i = 0; i < g_boot.nregions; i++) {
        const struct boot_region* r = &g_boot.regions[i];
        if (r->type != MULTIBOOT_MEMORY_AVAILABLE) continue;
        pmm_mark_region(r->addr, r->len, 0);

        kprint("  Frei: 0x");
        kprint_hex((uint32_t)(r->addr >> 32)); kprint_hex((uint32_t)r->addr);
        kprint(" (");
        kprint_dec((uint32_t)(r->len / 1024));
        kprint(" KiB)\n");
    }

    /* Die ersten 1 MiB (BIOS, VGA, ...) und das Kernel-Abbild belegen */
    uint64_t kstart = (uint64_t)(uintptr_t)_kernel_start, kend = (uint64_t)(uintptr_t)_kernel_end;
    pmm_mark_region(0, 0x200000, 1);                              /* untere 2 MiB nie vergeben */
    pmm_mark_region(kstart, kend - kstart, 1);                    /* Kernel-Abbild (liegt irgendwo unter 4 GiB) */
    if (g_boot.mod_size) pmm_mark_region(g_boot.mod_addr, g_boot.mod_size, 1);      /* Installationspaket nicht ueberschreiben */

    if (g_boot.model_size) pmm_mark_region(g_boot.model_addr, g_boot.model_size, 1);   /* Sprachmodell nicht ueberschreiben */

    used_pages = 0;
    for (uint32_t i = 0; i <= highest_page; i++) {
        if (bitmap_test(i)) used_pages++;
    }
    total_pages = highest_page + 1;

    kprint("[PMM] Hoechste Seite: ");
    kprint_dec(highest_page);
    kprint("\n[PMM] Verfolgte Seiten: ");
    kprint_dec(total_pages);
    kprint(" (");
    kprint_dec(total_pages * 4);
    kprint(" KiB)\n[PMM] Freie Seiten: ");
    kprint_dec(pmm_get_free_pages());
    kprint("\n");
}

void* pmm_alloc_page(void) {
    for (uint32_t i = 0; i <= highest_page; i++) {
        if (!bitmap_test(i)) {
            bitmap_set(i);
            used_pages++;
            return (void*)(uintptr_t)PFN_TO_ADDR(i);
        }
    }
    return NULL; /* Out of memory */
}

void* pmm_alloc_pages(size_t count) {
    if (count == 0) return NULL;
    if (count == 1) return pmm_alloc_page();

    if (count > highest_page + 1) return NULL;
    for (uint32_t i = 0; i <= highest_page + 1 - (uint32_t)count; i++) {
        int free = 1;
        for (size_t j = 0; j < count; j++) {
            if (bitmap_test(i + j)) {
                free = 0;
                break;
            }
        }
        if (free) {
            for (size_t j = 0; j < count; j++) {
                bitmap_set(i + j);
                used_pages++;
            }
            return (void*)(uintptr_t)PFN_TO_ADDR(i);
        }
    }
    return NULL;
}

void pmm_free_page(void* addr) {
    if (!addr) return;
    uint64_t pfn64 = ADDR_TO_PFN((uintptr_t)addr);
    if (pfn64 >= PMM_MAX_PAGES) return;
    uint32_t pfn = (uint32_t)pfn64;

    if (bitmap_test(pfn)) {
        bitmap_clear(pfn);
        if (used_pages > 0) used_pages--;
    }
}

void pmm_free_pages(void* addr, size_t count) {
    uint8_t* p = (uint8_t*)addr;
    for (size_t i = 0; i < count; i++) {
        pmm_free_page(p);
        p += PAGE_SIZE;
    }
}

uint32_t pmm_get_total_pages(void) { return total_pages; }
uint32_t pmm_get_used_pages(void)  { return used_pages; }
uint32_t pmm_get_free_pages(void)  {
    return (total_pages > used_pages) ? (total_pages - used_pages) : 0;
}
