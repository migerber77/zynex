# Zynex OS (x86_64, eigener UEFI-Lader, kein BIOS)

AS=nasm
CC=gcc
LD=ld
ASFLAGS=-f elf64
CFLAGS=-m64 -ffreestanding -fno-stack-protector -fpie -nostdlib -mno-red-zone -mcmodel=small -mno-sse -mno-mmx -Wall -Wextra -O2 -Iinclude
LDFLAGS=-m elf_x86_64 -pie --no-dynamic-linker -z norelro -T linker/linker.ld -nostdlib -z max-page-size=4096 -z noexecstack

BUILD=build
KERNEL=$(BUILD)/kernel.bin

OBJS=$(BUILD)/boot.o $(BUILD)/kernel.o $(BUILD)/bootinfo.o $(BUILD)/gdt.o $(BUILD)/pmm.o $(BUILD)/paging.o \
     $(BUILD)/idt.o $(BUILD)/pic.o $(BUILD)/isr.o $(BUILD)/isr_asm.o \
     $(BUILD)/timer.o $(BUILD)/keyboard.o $(BUILD)/task.o \
     $(BUILD)/kheap.o $(BUILD)/ramfs.o $(BUILD)/arena.o $(BUILD)/elf.o $(BUILD)/ai.o $(BUILD)/llm.o $(BUILD)/gehirn.o \
     $(BUILD)/capability.o $(BUILD)/semantic.o $(BUILD)/module.o \
     $(BUILD)/block.o $(BUILD)/syscall.o $(BUILD)/synfs.o \
     $(BUILD)/pci.o $(BUILD)/nvme.o $(BUILD)/usb.o $(BUILD)/hyperv.o $(BUILD)/wifi.o $(BUILD)/font8x16.o $(BUILD)/font8x16_l1.o $(BUILD)/ahci.o $(BUILD)/benutzer.o $(BUILD)/gpt.o $(BUILD)/installer.o $(BUILD)/gui.o $(BUILD)/desktop.o $(BUILD)/logo.o $(BUILD)/layouts.o $(BUILD)/layout_api.o $(BUILD)/qubit.o $(BUILD)/wpa.o $(BUILD)/net.o $(BUILD)/crypto.o $(BUILD)/tls.o $(BUILD)/pk.o $(BUILD)/x509.o $(BUILD)/ca_roots.o $(BUILD)/synbin.o $(BUILD)/synbin_embed.o $(BUILD)/shell.o $(BUILD)/befehle.o $(BUILD)/browser.o $(BUILD)/fw.o $(BUILD)/zyndb.o $(BUILD)/krypto.o

.PHONY: all clean run run-uefi
all: $(KERNEL)
$(BUILD):
	mkdir -p $(BUILD)
$(BUILD)/boot.o: boot/boot.asm | $(BUILD)
	$(AS) $(ASFLAGS) $< -o $@
$(BUILD)/isr_asm.o: kernel/isr.asm | $(BUILD)
	$(AS) $(ASFLAGS) $< -o $@
# Nur die KI-Datei darf SSE-Fliesskomma nutzen (Interrupts fassen keine XMM-Register an)
$(BUILD)/ai.o: kernel/ai.c | $(BUILD)
	$(CC) $(filter-out -mno-sse -mno-mmx,$(CFLAGS)) -c $< -o $@
$(BUILD)/llm.o: kernel/llm.c | $(BUILD)
	$(CC) $(filter-out -mno-sse -mno-mmx,$(CFLAGS)) -c $< -o $@
$(BUILD)/gehirn.o: kernel/gehirn.c | $(BUILD)
	$(CC) $(filter-out -mno-sse -mno-mmx,$(CFLAGS)) -c $< -o $@
# .synbin-Programme mit dem eigenen Compiler bauen und in den Kernel einbinden
SYNPROGS=$(BUILD)/hallo.synbin $(BUILD)/rechnen.synbin $(BUILD)/zeit.synbin $(BUILD)/schleife.synbin
$(BUILD)/%.synbin: synsrc/%.syn tools/sync.py | $(BUILD)
	python3 tools/sync.py $< -o $@
$(BUILD)/synbin_embed.o: kernel/synbin_embed.c $(SYNPROGS) | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/%.o: kernel/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(KERNEL): $(OBJS) linker/linker.ld
	$(LD) $(LDFLAGS) -o $@ $(OBJS)
run: $(KERNEL)
	qemu-system-x86_64 -kernel $(KERNEL) -m 256M -nographic -no-reboot
clean:
	rm -rf $(BUILD)

# Eigener UEFI-Lader (PE32+/EFI-Anwendung). Der oeffentliche Schluessel kommt aus loader/zynex_key.h (tools/mk-loader.sh erzeugt ihn).
LOADER=$(BUILD)/BOOTX64.EFI
.PHONY: loader
loader: $(LOADER)
$(LOADER): loader/loader.c loader/uefi.h loader/zynex_key.h include/lms.h | $(BUILD)
	clang --target=x86_64-unknown-windows -ffreestanding -fshort-wchar -fno-stack-protector -mno-red-zone -fno-builtin -nostdlib -O2 -Wall -c loader/loader.c -o $(BUILD)/loader.o
	lld-link /subsystem:efi_application /entry:efi_main /nodefaultlib /out:$@ $(BUILD)/loader.o
