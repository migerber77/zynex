#pragma once
#include <stdint.h>
#include <stddef.h>
#include "multiboot.h"

/* Page size we use (4 KiB) */
#define PAGE_SIZE 4096
#define PAGE_SHIFT 12

/* Convert between addresses and page frame numbers */
#define PAGE_ALIGN_DOWN(addr) ((addr) & ~(uint64_t)(PAGE_SIZE - 1))
#define PAGE_ALIGN_UP(addr)   (((addr) + PAGE_SIZE - 1) & ~(uint64_t)(PAGE_SIZE - 1))
#define ADDR_TO_PFN(addr)     ((uint64_t)(addr) >> PAGE_SHIFT)
#define PFN_TO_ADDR(pfn)      ((uint64_t)(pfn) << PAGE_SHIFT)

/* Maximum physical memory we track (supports up to 4 GiB for now) */
#define PMM_MAX_PAGES (1024 * 1024)   /* 4 GiB / 4 KiB */

void pmm_init(void);
void* pmm_alloc_page(void);
void* pmm_alloc_pages(size_t count);
void  pmm_free_page(void* addr);
void  pmm_free_pages(void* addr, size_t count);

/* Statistics */
uint32_t pmm_get_total_pages(void);
uint32_t pmm_get_used_pages(void);
uint32_t pmm_get_free_pages(void);