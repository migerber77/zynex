#!/bin/bash
# Baut eine hybride UEFI-ISO (x86-64, EFI64) mit dem eigenen Zynex-Lader. Kein BIOS, kein GRUB.
# Aufbau (wie bei Debian/Ubuntu):
#   - ISO-9660-Dateisystem mit EFI/BOOT/BOOTX64.EFI und boot/* (Rufus im ISO-Modus kopiert diese Dateien auf FAT32)
#   - dieselben Dateien in einer FAT-Partition (EFI-Systempartition), als Partition 2 in einer GPT angehaengt
#     (balenaEtcher, Rufus im DD-Modus, dd: der Stick hat dann eine echte EFI-Partition)
#   - El-Torito-Eintrag auf dieselbe FAT-Partition (DVD, Hyper-V, VirtualBox)
#   - --mbr-force-bootable: setzt im Protective-MBR das Boot-Flag. Ohne das weigert sich
#     echte Firmware (z. B. manche HP/Lenovo-Notebooks) oft, vom Stick zu booten, obwohl
#     QEMU/OVMF keinen Unterschied macht (bekanntes xorriso/GPT-Problem, siehe Ubuntu-Bug 1899308).
# Aufruf: tools/mk-zynexos-iso.sh <kernel.bin> <BOOTX64.EFI> <kernel.sig> <cfg-Textzeile> <ziel.iso> [installationspaket] [modell.gguf]
#   cfg-Textzeile = Kernel-Kommandozeile, z. B. "modus=live"; sie ist mitsigniert (tools/pqsign.py bootsign).
# Benoetigt: xorriso mtools dosfstools
set -e
K="${1:?kernel.bin}"; L="${2:?BOOTX64.EFI}"; SIG="${3:?kernel.sig}"; CMD="${4:?kommandozeile}"; OUT="${5:?ziel.iso}"; PACK="${6:-}"; MODELL="${7:-}"
W=$(mktemp -d)
T="$W/iso"; mkdir -p "$T/EFI/BOOT" "$T/boot"
printf '%s\n' "$CMD" > "$W/zynex.cfg"
cp "$L" "$T/EFI/BOOT/BOOTX64.EFI"; cp "$K" "$T/boot/kernel.bin"; cp "$SIG" "$T/boot/kernel.sig"; cp "$W/zynex.cfg" "$T/boot/zynex.cfg"
[ -n "$PACK" ] && cp "$PACK" "$T/boot/zynex-install.pack"
# Das Sprachmodell liegt nur in der EFI-Partition (sonst waere die ISO doppelt so gross): Stick mit balenaEtcher oder Rufus im DD-Modus schreiben
[ -n "$MODELL" ] && printf 'Diese ISO enthaelt ein Sprachmodell in der EFI-Partition.\r\nRufus: beim Schreiben "DD-Image" waehlen (nicht ISO-Modus), sonst fehlt das Modell.\r\nbalenaEtcher: einfach schreiben.\r\n' > "$T/LIESMICH-SPRACHMODELL.txt"
if [ -n "${ZYNEX_SB_CER:-}" ] && [ -f "$ZYNEX_SB_CER" ]; then mkdir -p "$T/EFI/ZYNEX"; cp "$ZYNEX_SB_CER" "$T/EFI/ZYNEX/zynex-sb.cer"; fi   # Zertifikat zum Einspielen (Secure Boot)
# FAT-Abbild der EFI-Systempartition mit denselben Dateien
SZ=$(du -sb "$T" | cut -f1)
[ -n "$MODELL" ] && SZ=$(( SZ + $(stat -c %s "$MODELL") ))
MB=$(( SZ / 1048576 + 8 )); [ $MB -lt 34 ] && MB=34
truncate -s ${MB}M "$W/efi.img"
mkfs.vfat -F 32 -n ZYNEXEFI "$W/efi.img" >/dev/null 2>&1 || mkfs.vfat -n ZYNEXEFI "$W/efi.img" >/dev/null
export MTOOLS_SKIP_CHECK=1
mcopy -s -i "$W/efi.img" "$T/EFI" "$T/boot" ::/
[ -n "$MODELL" ] && mcopy -i "$W/efi.img" "$MODELL" ::/boot/modell.gguf
mdir -i "$W/efi.img" -/ ::/ | tail -14
xorriso -as mkisofs -iso-level 3 -r -J -joliet-long -V ZYNEXOS \
  -partition_offset 16 --mbr-force-bootable \
  -append_partition 2 0xef "$W/efi.img" -appended_part_as_gpt \
  -e --interval:appended_partition_2:all:: -no-emul-boot \
  -o "$OUT" "$T" 2>&1 | tail -3
rm -rf "$W"
ls -la "$OUT"
