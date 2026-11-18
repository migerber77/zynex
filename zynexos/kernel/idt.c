/*
 * Zynex OS - Interrupt Descriptor Table
 */

#include "idt.h"
#include "isr.h"
#include "pic.h"

extern void kprint(const char* str);
extern void kprint_dec(uint32_t val);

static struct idt_entry idt[256];
static struct idt_ptr   idtp;

void idt_set_gate(uint8_t num, uint64_t base, uint16_t selector, uint8_t flags) {
    idt[num].base_low  = base & 0xFFFF;
    idt[num].base_mid  = (base >> 16) & 0xFFFF;
    idt[num].base_high = (uint32_t)(base >> 32);
    idt[num].selector  = selector;
    idt[num].ist       = 0;
    idt[num].flags     = flags;
    idt[num].zero      = 0;
}

void idt_init(void) {
    idtp.limit = sizeof(idt) - 1;
    idtp.base  = (uint64_t)(uintptr_t)&idt;

    /* Clear IDT */
    for (int i = 0; i < 256; i++) {
        idt_set_gate(i, 0, 0, 0);
    }

    /* CPU Exceptions (0-31) */
    idt_set_gate(0,  (uint64_t)(uintptr_t)isr0,  0x08, 0x8E);
    idt_set_gate(1,  (uint64_t)(uintptr_t)isr1,  0x08, 0x8E);
    idt_set_gate(2,  (uint64_t)(uintptr_t)isr2,  0x08, 0x8E);
    idt_set_gate(3,  (uint64_t)(uintptr_t)isr3,  0x08, 0x8E);
    idt_set_gate(4,  (uint64_t)(uintptr_t)isr4,  0x08, 0x8E);
    idt_set_gate(5,  (uint64_t)(uintptr_t)isr5,  0x08, 0x8E);
    idt_set_gate(6,  (uint64_t)(uintptr_t)isr6,  0x08, 0x8E);
    idt_set_gate(7,  (uint64_t)(uintptr_t)isr7,  0x08, 0x8E);
    idt_set_gate(8,  (uint64_t)(uintptr_t)isr8,  0x08, 0x8E);
    idt_set_gate(9,  (uint64_t)(uintptr_t)isr9,  0x08, 0x8E);
    idt_set_gate(10, (uint64_t)(uintptr_t)isr10, 0x08, 0x8E);
    idt_set_gate(11, (uint64_t)(uintptr_t)isr11, 0x08, 0x8E);
    idt_set_gate(12, (uint64_t)(uintptr_t)isr12, 0x08, 0x8E);
    idt_set_gate(13, (uint64_t)(uintptr_t)isr13, 0x08, 0x8E);
    idt_set_gate(14, (uint64_t)(uintptr_t)isr14, 0x08, 0x8E);
    idt_set_gate(15, (uint64_t)(uintptr_t)isr15, 0x08, 0x8E);
    idt_set_gate(16, (uint64_t)(uintptr_t)isr16, 0x08, 0x8E);
    idt_set_gate(17, (uint64_t)(uintptr_t)isr17, 0x08, 0x8E);
    idt_set_gate(18, (uint64_t)(uintptr_t)isr18, 0x08, 0x8E);
    idt_set_gate(19, (uint64_t)(uintptr_t)isr19, 0x08, 0x8E);
    idt_set_gate(20, (uint64_t)(uintptr_t)isr20, 0x08, 0x8E);
    idt_set_gate(21, (uint64_t)(uintptr_t)isr21, 0x08, 0x8E);
    idt_set_gate(22, (uint64_t)(uintptr_t)isr22, 0x08, 0x8E);
    idt_set_gate(23, (uint64_t)(uintptr_t)isr23, 0x08, 0x8E);
    idt_set_gate(24, (uint64_t)(uintptr_t)isr24, 0x08, 0x8E);
    idt_set_gate(25, (uint64_t)(uintptr_t)isr25, 0x08, 0x8E);
    idt_set_gate(26, (uint64_t)(uintptr_t)isr26, 0x08, 0x8E);
    idt_set_gate(27, (uint64_t)(uintptr_t)isr27, 0x08, 0x8E);
    idt_set_gate(28, (uint64_t)(uintptr_t)isr28, 0x08, 0x8E);
    idt_set_gate(29, (uint64_t)(uintptr_t)isr29, 0x08, 0x8E);
    idt_set_gate(30, (uint64_t)(uintptr_t)isr30, 0x08, 0x8E);
    idt_set_gate(31, (uint64_t)(uintptr_t)isr31, 0x08, 0x8E);

    /* Hardware IRQs (32-47) */
    idt_set_gate(32, (uint64_t)(uintptr_t)irq0,  0x08, 0x8E);
    idt_set_gate(33, (uint64_t)(uintptr_t)irq1,  0x08, 0x8E);
    idt_set_gate(34, (uint64_t)(uintptr_t)irq2,  0x08, 0x8E);
    idt_set_gate(35, (uint64_t)(uintptr_t)irq3,  0x08, 0x8E);
    idt_set_gate(36, (uint64_t)(uintptr_t)irq4,  0x08, 0x8E);
    idt_set_gate(37, (uint64_t)(uintptr_t)irq5,  0x08, 0x8E);
    idt_set_gate(38, (uint64_t)(uintptr_t)irq6,  0x08, 0x8E);
    idt_set_gate(39, (uint64_t)(uintptr_t)irq7,  0x08, 0x8E);
    idt_set_gate(40, (uint64_t)(uintptr_t)irq8,  0x08, 0x8E);
    idt_set_gate(41, (uint64_t)(uintptr_t)irq9,  0x08, 0x8E);
    idt_set_gate(42, (uint64_t)(uintptr_t)irq10, 0x08, 0x8E);
    idt_set_gate(43, (uint64_t)(uintptr_t)irq11, 0x08, 0x8E);
    idt_set_gate(44, (uint64_t)(uintptr_t)irq12, 0x08, 0x8E);
    idt_set_gate(45, (uint64_t)(uintptr_t)irq13, 0x08, 0x8E);
    idt_set_gate(46, (uint64_t)(uintptr_t)irq14, 0x08, 0x8E);
    idt_set_gate(47, (uint64_t)(uintptr_t)irq15, 0x08, 0x8E);

    /* Load IDT */
    __asm__ volatile ("lidt %0" : : "m"(idtp));

    kprint("[IDT] Interrupt Descriptor Table loaded (256 entries)\n");
}
