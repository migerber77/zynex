/*
 * Zynex OS - Minimal ELF32 Loader
 */

#include "elf.h"
#include "kheap.h"
#include "pmm.h"

extern void kprint(const char* str);
extern void kprint_hex(uint32_t val);
extern void kprint_dec(uint32_t val);

int elf_check(const void* data, uint32_t size) {
    if (size < sizeof(struct elf32_hdr)) return 0;
    const struct elf32_hdr* hdr = (const struct elf32_hdr*)data;
    if (hdr->magic != ELF_MAGIC) return 0;
    if (hdr->ident[0] != 1) return 0; /* 32-bit */
    if (hdr->type != 2) return 0;     /* ET_EXEC */
    if (hdr->machine != 3) return 0;  /* x86 */
    return 1;
}

uint32_t elf_load(const void* data, uint32_t size) {
    if (!elf_check(data, size)) {
        kprint("[ELF] Invalid ELF image\n");
        return 0;
    }

    const struct elf32_hdr* hdr = (const struct elf32_hdr*)data;
    const uint8_t* base = (const uint8_t*)data;

    kprint("[ELF] Loading ELF, entry=0x");
    kprint_hex(hdr->entry);
    kprint(" phnum=");
    kprint_dec(hdr->phnum);
    kprint("\n");

    const struct elf32_phdr* ph = (const struct elf32_phdr*)(base + hdr->phoff);

    for (uint16_t i = 0; i < hdr->phnum; i++) {
        if (ph[i].type != PT_LOAD) continue;

        kprint("  PT_LOAD vaddr=0x");
        kprint_hex(ph[i].vaddr);
        kprint(" filesz=");
        kprint_dec(ph[i].filesz);
        kprint(" memsz=");
        kprint_dec(ph[i].memsz);
        kprint("\n");

        /* For now we only support loading into already-mapped identity region */
        if (ph[i].vaddr + ph[i].memsz > 0x1000000) {
            kprint("[ELF] Segment outside identity map\n");
            return 0;
        }

        uint8_t* dest = (uint8_t*)(uintptr_t)ph[i].vaddr;
        const uint8_t* src = base + ph[i].offset;

        for (uint32_t j = 0; j < ph[i].filesz; j++) {
            dest[j] = src[j];
        }
        for (uint32_t j = ph[i].filesz; j < ph[i].memsz; j++) {
            dest[j] = 0;
        }
    }

    kprint("[ELF] Load complete\n");
    return hdr->entry;
}
