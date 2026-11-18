#pragma once
#include <stdint.h>
#include <stddef.h>

/* Minimal ELF32 structures */
#define ELF_MAGIC 0x464C457F  /* 0x7F 'E' 'L' 'F' */

struct elf32_hdr {
    uint32_t magic;
    uint8_t  ident[12];
    uint16_t type;
    uint16_t machine;
    uint32_t version;
    uint32_t entry;
    uint32_t phoff;
    uint32_t shoff;
    uint32_t flags;
    uint16_t ehsize;
    uint16_t phentsize;
    uint16_t phnum;
    uint16_t shentsize;
    uint16_t shnum;
    uint16_t shstrndx;
} __attribute__((packed));

struct elf32_phdr {
    uint32_t type;
    uint32_t offset;
    uint32_t vaddr;
    uint32_t paddr;
    uint32_t filesz;
    uint32_t memsz;
    uint32_t flags;
    uint32_t align;
} __attribute__((packed));

#define PT_LOAD 1

/* Load an ELF image from memory. Returns entry point or 0 on failure. */
uint32_t elf_load(const void* data, uint32_t size);
int      elf_check(const void* data, uint32_t size);
