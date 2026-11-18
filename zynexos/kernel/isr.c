/*
 * Zynex OS - Interrupt / Exception Handlers
 */

#include "isr.h"
#include "pic.h"
#include "idt.h"
#include "keyboard.h"
#include "timer.h"

extern void kprint(const char* str);
extern void kprint_dec(uint32_t val);
extern void kprint_hex(uint32_t val);

static isr_handler_t handlers[256];

static const char* exception_messages[] = {
    "Division By Zero", "Debug", "Non Maskable Interrupt", "Breakpoint",
    "Into Detected Overflow", "Out of Bounds", "Invalid Opcode", "No Coprocessor",
    "Double Fault", "Coprocessor Segment Overrun", "Bad TSS", "Segment Not Present",
    "Stack Fault", "General Protection Fault", "Page Fault", "Unknown Interrupt",
    "Coprocessor Fault", "Alignment Check", "Machine Check",
    "Reserved", "Reserved", "Reserved", "Reserved", "Reserved", "Reserved",
    "Reserved", "Reserved", "Reserved", "Reserved", "Reserved", "Reserved", "Reserved"
};

void isr_register_handler(uint8_t num, isr_handler_t handler) {
    handlers[num] = handler;
}

void isr_handler(struct registers* regs) {
    if (handlers[regs->int_no]) {
        handlers[regs->int_no](regs);
        return;
    }

    kprint("\n[EXCEPTION] ");
    if (regs->int_no < 32) {
        kprint(exception_messages[regs->int_no]);
    } else {
        kprint("Unknown");
    }
    kprint(" (int ");
    kprint_dec((uint32_t)regs->int_no);
    kprint(") err=0x");
    kprint_hex((uint32_t)regs->err_code);
    kprint("\n rip=0x"); kprint_hex((uint32_t)(regs->rip >> 32)); kprint_hex((uint32_t)regs->rip);
    kprint("\nSystem halted.\n");

    for (;;) {
        __asm__ volatile ("cli; hlt");
    }
}

void irq_handler(struct registers* regs) {
    pic_send_eoi((uint8_t)(regs->int_no - 32));

    if (handlers[regs->int_no]) {
        handlers[regs->int_no](regs);
    }
}

void isr_init(void) {
    pic_remap(32, 40);

    /* Timer and keyboard are registered by their own init functions */
    pic_clear_mask(0);
    pic_clear_mask(1);

    __asm__ volatile ("sti");

    kprint("[ISR] Handlers ready, interrupts enabled (STI)\n");
}
