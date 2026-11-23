/* Eingebettete Bilder: Zynos (280x358, RGB565, schwarz = durchsichtig) und der Zynex-Hintergrund (1024x559, RGB565). */
__asm__(".section .rodata\n.balign 4\n.globl zynie_bild\nzynie_bild:\n.incbin \"kernel/zynie.bin\"\n.globl hintergrund_bild\nhintergrund_bild:\n.incbin \"kernel/hintergrund.bin\"\n.text\n");
