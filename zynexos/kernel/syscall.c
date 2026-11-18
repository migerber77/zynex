/*
 * Zynex OS - Syscall interface (int 0x80)
 */

#include "syscall.h"
#include "isr.h"
#include "idt.h"
#include "timer.h"
#include "task.h"
#include "kheap.h"
#include "block.h"
#include "qubit.h"

extern void kprint(const char* str);
extern void kprint_dec(uint32_t val);
extern void kputchar(char c);

/* Assembly stub */
extern void isr_syscall(void);

int64_t syscall_dispatch(uint64_t num, uint64_t a, uint64_t b, uint64_t c,
                         uint64_t d, uint64_t e) {
    (void)d; (void)e;
    switch (num) {
        case SYS_YIELD:
            task_yield();
            return 0;
        case SYS_GETTICKS:
            return (int64_t)timer_get_ticks();
        case SYS_UPTIME:
            return (int64_t)timer_get_seconds();
        case SYS_WRITE: {
            const char* buf = (const char*)(uintptr_t)a;
            uint64_t len = b;
            for (uint64_t i = 0; i < len; i++) kputchar(buf[i]);
            return (int64_t)len;
        }
        case SYS_ALLOC:
            return (int64_t)(uintptr_t)kmalloc((size_t)a);
        case SYS_FREE:
            kfree((void*)(uintptr_t)a);
            return 0;
        case SYS_PS:
            task_list();
            return 0;
        case SYS_BLK_INFO:
            block_info();
            return 0;
        case SYS_BLK_READ:
            return block_read((uint32_t)a, (void*)(uintptr_t)b, (uint32_t)c);
        case SYS_BLK_WRITE:
            return block_write((uint32_t)a, (const void*)(uintptr_t)b, (uint32_t)c);
        case SYS_QUBIT:
            return qubit_syscall(a, b, c);
        case SYS_EXIT:
            task_exit();
            return 0;
        default:
            return -1;
    }
}

static void syscall_handler(struct registers* regs) {
    /* rax=Nummer, rbx,rcx,rdx,rsi,rdi = Argumente, Rueckgabe in rax */
    int64_t ret = syscall_dispatch(regs->rax, regs->rbx, regs->rcx,
                                   regs->rdx, regs->rsi, regs->rdi);
    regs->rax = (uint64_t)ret;
}

void syscall_init(void) {
    /* Vector 0x80 */
    idt_set_gate(0x80, (uint64_t)(uintptr_t)isr_syscall, 0x08, 0xEE); /* present, ring3 possible */
    isr_register_handler(0x80, syscall_handler);
    kprint("[SYSCALL] int 0x80 interface online\n");
}
