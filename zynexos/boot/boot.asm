; Zynex OS - 64-Bit-Einstieg vom eigenen UEFI-Lader (kein BIOS, kein GRUB, kein Multiboot)
; Der Lader springt im Long Mode hierher: edi = Magic, esi = Boot-Informationen (Multiboot2-Format, unter 4 GiB).
; Hier: eigene Seitentabellen (64 GiB Identity-Map mit 2-MiB-Seiten), eigene GDT, SSE, dann kernel_main(magic, info).

section .zxefi align=8
align 8
    db "ZXEFIENT"                         ; Markierung, die der Lader im Kernel-Abbild sucht
    dq _efi_start

section .text
bits 64
global _efi_start
extern kernel_main

_efi_start:
    cli
    cld
    mov r12d, edi
    mov r13d, esi

    ; Der Kernel liegt irgendwo unter 4 GiB (positionsunabhaengig): alle Adressen relativ zu RIP
    lea r8, [rel pml4]
    lea r9, [rel pdpt]
    lea r10, [rel pd]
    ; PML4[0] -> PDPT, PDPT[0..63] -> PD0..PD63 (je 1 GiB)
    mov rax, r9
    or rax, 0x3
    mov [r8], rax
    xor ecx, ecx
.pdpt:
    mov rax, rcx
    shl rax, 12
    add rax, r10
    or rax, 0x3
    mov [r9 + rcx*8], rax
    inc ecx
    cmp ecx, 64
    jne .pdpt

    ; 32768 Eintraege zu je 2 MiB (Present | Write | PS)
    xor ecx, ecx
.pd:
    mov rax, rcx
    shl rax, 21
    or rax, 0x83
    mov [r10 + rcx*8], rax
    inc ecx
    cmp ecx, 32768
    jne .pd

    mov cr3, r8

    mov rax, cr4
    or rax, 0x20 | 0x200 | 0x400          ; PAE, OSFXSR, OSXMMEXCPT
    mov cr4, rax

    mov rax, cr0
    and eax, 0xFFFFFFFB                   ; EM aus
    or eax, 0x2                           ; MP
    mov cr0, rax

    lea rax, [rel gdt64]
    lea rbx, [rel gdt64_ptr]
    mov [rbx + 2], rax
    lgdt [rbx]
    push 0x08
    lea rax, [rel .weiter]
    push rax
    retfq
.weiter:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    lea rsp, [rel stack_top]
    xor ebp, ebp
    mov edi, r12d
    mov esi, r13d
    call kernel_main
.hang:
    cli
    hlt
    jmp .hang

section .data
align 8
gdt64:
    dq 0
    dq 0x00209A0000000000                 ; 0x08 Code 64 Bit
    dq 0x0000920000000000                 ; 0x10 Daten
gdt64_end:
gdt64_ptr:
    dw gdt64_end - gdt64 - 1
    dq 0                                  ; Adresse wird beim Start eingetragen

section .bss align=4096 nobits
align 4096
global pml4, pdpt, pd
pml4:  resb 4096
pdpt:  resb 4096
pd:    resb 4096 * 64
align 16
stack_bottom:
    resb 65536
stack_top:
