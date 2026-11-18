/* Zynex OS Linker Script (x86_64): positionsunabhaengig (PIE), Link-Adresse 1 MiB, der Lader legt den Kernel irgendwo unter 4 GiB ab und wendet die Relokationen an */

ENTRY(_efi_start)

SECTIONS
{
    . = 1M;
    _kernel_start = .;

    .text BLOCK(4K) : ALIGN(4K)
    {
        KEEP(*(.zxefi))
        *(.text .text.*)
    }

    .rodata BLOCK(4K) : ALIGN(4K)
    {
        *(.rodata .rodata.*)
    }

    .dynsym : ALIGN(8) { *(.dynsym) }
    .dynstr : { *(.dynstr) }
    .gnu.hash : ALIGN(8) { *(.gnu.hash) }
    .hash : ALIGN(8) { *(.hash) }
    .rela.dyn : ALIGN(8) { *(.rela.dyn) *(.rela.*) }

    .data BLOCK(4K) : ALIGN(4K)
    {
        *(.data .data.*)
        *(.got) *(.got.plt) *(.igot.plt)
    }

    .dynamic : ALIGN(8) { *(.dynamic) }

    .bss BLOCK(4K) : ALIGN(4K)
    {
        *(COMMON)
        *(.bss .bss.*)
    }

    _kernel_end = .;
}
