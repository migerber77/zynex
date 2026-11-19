#!/bin/bash
# Baut eine UEFI-bootfaehige ISO/USB-Abbild (hybrides GPT) mit dem eigenen Zynex-Lader. Kein BIOS, kein GRUB.
# Aufruf: tools/mk-zynexos-iso.sh <kernel.bin> <BOOTX64.EFI> <kernel.sig> <cfg-Textzeile> <ziel.iso> [installationspaket]
#   cfg-Textzeile = Kernel-Kommandozeile, z. B. "modus=live" oder "modus=live installer=gui". Sie wird mitsigniert,
#   deshalb muss <kernel.sig> mit exakt dieser Zeile erzeugt worden sein (tools/pqsign.py bootsign).
# Benoetigt: xorriso mtools dosfstools
set -e
K="${1:?kernel.bin}"; L="${2:?BOOTX64.EFI}"; SIG="${3:?kernel.sig}"; CMD="${4:?kommandozeile}"; OUT="${5:?ziel.iso}"; PACK="${6:-}"; MODELL="${7:-}"   # optional: Sprachmodell (GGUF), landet als \boot\modell.gguf auf dem Stick
W=$(mktemp -d)
mkdir -p "$W/iso/boot"
SZ=$(( $(stat -c %s "$K") + $(stat -c %s "$L") + $(stat -c %s "$SIG") ))
[ -n "$PACK" ] && SZ=$(( SZ + $(stat -c %s "$PACK") ))
[ -n "$MODELL" ] && SZ=$(( SZ + $(stat -c %s "$MODELL") ))
MB=$(( SZ / 1048576 + 8 )); [ $MB -lt 24 ] && MB=24
truncate -s ${MB}M "$W/iso/boot/efi.img"
mkfs.vfat -F 16 -n ZYNEXEFI "$W/iso/boot/efi.img" >/dev/null 2>&1 || mkfs.vfat -F 32 -n ZYNEXEFI "$W/iso/boot/efi.img" >/dev/null
printf '%s\n' "$CMD" > "$W/zynex.cfg"
export MTOOLS_SKIP_CHECK=1
mmd -i "$W/iso/boot/efi.img" ::/EFI ::/EFI/BOOT ::/boot
mcopy -i "$W/iso/boot/efi.img" "$L" ::/EFI/BOOT/BOOTX64.EFI
if [ -n "${ZYNEX_SB_CER:-}" ] && [ -f "$ZYNEX_SB_CER" ]; then mmd -i "$W/iso/boot/efi.img" ::/EFI/ZYNEX; mcopy -i "$W/iso/boot/efi.img" "$ZYNEX_SB_CER" ::/EFI/ZYNEX/zynex-sb.cer; fi   # Zertifikat zum Einspielen in die Firmware (Secure Boot)
mcopy -i "$W/iso/boot/efi.img" "$K" ::/boot/kernel.bin
mcopy -i "$W/iso/boot/efi.img" "$SIG" ::/boot/kernel.sig
mcopy -i "$W/iso/boot/efi.img" "$W/zynex.cfg" ::/boot/zynex.cfg
[ -n "$PACK" ] && mcopy -i "$W/iso/boot/efi.img" "$PACK" ::/boot/zynex-install.pack
[ -n "$MODELL" ] && mcopy -i "$W/iso/boot/efi.img" "$MODELL" ::/boot/modell.gguf
mdir -i "$W/iso/boot/efi.img" -/ ::/ | tail -12
xorriso -as mkisofs -iso-level 3 -V ZYNEXOS -o "$OUT" -e boot/efi.img -no-emul-boot -isohybrid-gpt-basdat "$W/iso" 2>&1 | tail -3
rm -rf "$W"
ls -la "$OUT"
