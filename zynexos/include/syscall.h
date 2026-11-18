#pragma once
#include <stdint.h>

/* Zynex Syscall ABI (int 0x80, 64 Bit)
 * args: rax=number, rbx, rcx, rdx, rsi, rdi
 * ret:  rax
 */

#define SYS_YIELD       1
#define SYS_GETTICKS    2
#define SYS_WRITE       3   /* ebx=buf, ecx=len  (console) */
#define SYS_READ        4   /* ebx=buf, ecx=max  (not fully blocking) */
#define SYS_ALLOC       5   /* ebx=size -> ptr */
#define SYS_FREE        6   /* ebx=ptr */
#define SYS_UPTIME      7
#define SYS_PS          8
#define SYS_BLK_READ    9   /* ebx=lba, ecx=buf, edx=count */
#define SYS_BLK_WRITE   10
#define SYS_BLK_INFO    11
#define SYS_QUBIT       12  /* ebx=op, ecx/edx=Argumente (siehe qubit.c), Capability CAP_QUANTUM */
#define SYS_EXIT        99

void syscall_init(void);
int64_t syscall_dispatch(uint64_t num, uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e);
