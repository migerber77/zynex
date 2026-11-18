#!/bin/bash
# Baut das Installationspaket: EFI-Systempartition mit eigenem Lader, Kernel, Signatur und Konfiguration auf einer Muster-Platte (GPT),
# danach als Paket (tools/mkpack.py), das der Zynex-Installer auf die Zielplatte schreibt.
# Aufruf: tools/mk-zynexos-template.sh <kernel.bin> <BOOTX64.EFI> <kernel.sig> <zynex.cfg> <paket>     Benoetigt (root/sudo): losetup mount mkfs.fat sgdisk
set -euo pipefail
K="${1:?kernel.bin}"; L="${2:?BOOTX64.EFI}"; SIG="${3:?kernel.sig}"; CFG="${4:?zynex.cfg}"; OUT="${5:?paket}"
HIER="$(cd "$(dirname "$0")" && pwd)"
W=$(mktemp -d)
IMG="$W/disk.img"
LOOP=""
aufraeumen() {
    sudo umount "$W/mnt" 2>/dev/null || true
    [ -n "$LOOP" ] && sudo losetup -d "$LOOP" 2>/dev/null || true
    rm -rf "$W"
}
trap aufraeumen EXIT
truncate -s $((88064 * 512)) "$IMG"
sgdisk -n 1:2048:86015 -t 1:ef00 "$IMG" >/dev/null
LOOP=$(sudo losetup -fP --show "$IMG")
sudo partprobe "$LOOP" 2>/dev/null || true
for i in 1 2 3 4 5 6 7 8 9 10; do [ -b "${LOOP}p1" ] && break; sleep 1; done
sudo mkfs.fat -F 32 -s 1 -n ZYNEXESP "${LOOP}p1" >/dev/null
mkdir -p "$W/mnt"
sudo mount "${LOOP}p1" "$W/mnt"
sudo mkdir -p "$W/mnt/EFI/BOOT" "$W/mnt/boot"
sudo cp "$L" "$W/mnt/EFI/BOOT/BOOTX64.EFI"
sudo cp "$K" "$W/mnt/boot/kernel.bin"
sudo cp "$SIG" "$W/mnt/boot/kernel.sig"
sudo cp "$CFG" "$W/mnt/boot/zynex.cfg"
sudo find "$W/mnt" | sed "s|$W/mnt||" | head -20
sync
sudo umount "$W/mnt"
sudo losetup -d "$LOOP"; LOOP=""
python3 "$HIER/mkpack.py" "$IMG" "$OUT"
