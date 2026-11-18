/*
 * Zynex OS - Paging (64 Bit)
 * Die Identity-Map (0 - 4 GiB, 2-MiB-Seiten) baut boot.asm vor dem Wechsel in den Long Mode.
 */

#include "paging.h"
#include "pmm.h"

extern void kprint(const char* str);
extern void kprint_hex(uint32_t val);

uint64_t paging_get_cr3(void) {
    uint64_t cr3;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(cr3));
    return cr3;
}

void paging_init(void) {
    kprint("[PAGING] Long Mode, Identity-Map 0 - 4 GiB (2-MiB-Seiten), PML4 bei 0x");
    kprint_hex((uint32_t)paging_get_cr3());
    kprint("\n");
}

extern uint64_t pml4[], pdpt[], pd[];

#define PTE_UC (0x08 | 0x10)       /* PWT | PCD: nicht zwischenspeichern */

static uint64_t* tabelle(uint64_t* eintrag) {
    if (!(*eintrag & 1)) {
        uint64_t* neu = (uint64_t*)pmm_alloc_page();
        if (!neu) return 0;
        for (int i = 0; i < 512; i++) neu[i] = 0;
        *eintrag = (uint64_t)(uintptr_t)neu | 0x3;
    }
    return (uint64_t*)(uintptr_t)(*eintrag & ~0xFFFULL);
}

int paging_map_mmio(uint64_t phys, uint64_t size) {
    uint64_t start = phys & ~0x1FFFFFULL;
    uint64_t end = (phys + size + 0x1FFFFF) & ~0x1FFFFFULL;
    if (end >= (1ULL << 48)) return -1;
    for (uint64_t a = start; a < end; a += 0x200000) {
        uint64_t* pdpt_t = tabelle(&pml4[a >> 39]);          /* PML4[0] ist bereits die Boot-Tabelle */
        if (!pdpt_t) return -2;
        uint64_t gi = (a >> 30) & 511;
        uint64_t* dir;
        if ((a >> 39) == 0 && gi < 4) dir = &pd[gi * 512];    /* 0 - 4 GiB: Boot-Verzeichnisse */
        else dir = tabelle(&pdpt_t[gi]);
        if (!dir) return -2;
        dir[(a >> 21) & 511] = a | 0x83 | PTE_UC;
    }
    uint64_t cr3 = paging_get_cr3();
    __asm__ volatile ("mov %0, %%cr3" : : "r"(cr3) : "memory");   /* TLB leeren */
    return 0;
}
