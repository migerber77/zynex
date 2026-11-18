#pragma once
#include <stdint.h>
#include <stddef.h>

#define PAGE_PRESENT       0x001
#define PAGE_WRITE         0x002
#define PAGE_USER          0x004

/* Der Start-Code (boot.asm) legt eine Identity-Map ueber 4 GiB mit 2-MiB-Seiten an. */
void paging_init(void);
uint64_t paging_get_cr3(void);

/* Geraete-Speicher (MMIO, z. B. PCI-BARs) ungepuffert einblenden, auch oberhalb von 4 GiB. */
int paging_map_mmio(uint64_t phys, uint64_t size);
