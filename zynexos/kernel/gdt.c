/*
 * Zynex OS - Global Descriptor Table (64 Bit)
 * Kernel- und User-Segmente (Ring 0 / Ring 3), flaches Modell.
 */

#include "gdt.h"

extern void kprint(const char* str);

static uint64_t gdt[5] __attribute__((aligned(16)));
static struct gdt_ptr gp;

void gdt_init(void) {
    gdt[0] = 0;
    gdt[1] = 0x00209A0000000000ULL;  /* 0x08 Kernel-Code, 64 Bit */
    gdt[2] = 0x0000920000000000ULL;  /* 0x10 Kernel-Daten */
    gdt[3] = 0x0000F20000000000ULL;  /* 0x18 User-Daten */
    gdt[4] = 0x0020FA0000000000ULL;  /* 0x20 User-Code, 64 Bit */

    gp.limit = sizeof(gdt) - 1;
    gp.base  = (uint64_t)(uintptr_t)gdt;

    __asm__ volatile ("lgdt %0" : : "m"(gp));
    __asm__ volatile (
        "mov $0x10, %%ax\n"
        "mov %%ax, %%ds\n"
        "mov %%ax, %%es\n"
        "mov %%ax, %%fs\n"
        "mov %%ax, %%gs\n"
        "mov %%ax, %%ss\n"
        "pushq $0x08\n"
        "lea 1f(%%rip), %%rax\n"
        "pushq %%rax\n"
        "lretq\n"
        "1:\n"
        : : : "rax", "memory");

    kprint("[GDT] Loaded (Kernel + User, 64-bit)\n");
}
