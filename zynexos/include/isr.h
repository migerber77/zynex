#pragma once
#include <stdint.h>

/* Register, die der ISR-Stub auf den Stack legt (64 Bit) */
struct registers {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t int_no, err_code;
    uint64_t rip, cs, rflags, rsp, ss;
};

typedef void (*isr_handler_t)(struct registers*);

void isr_init(void);
void isr_register_handler(uint8_t num, isr_handler_t handler);
void irq_handler(struct registers* regs);
void isr_handler(struct registers* regs);
