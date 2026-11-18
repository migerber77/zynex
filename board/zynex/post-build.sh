#!/bin/sh
# Wird nach dem Bau des Dateisystems ausgefuehrt.
# TARGET_DIR und BINARIES_DIR setzt Buildroot.
set -e

BOARD_DIR=$(dirname "$0")

cp -f "$BOARD_DIR/grub-bios.cfg" "$TARGET_DIR/boot/grub/grub.cfg"

# Erste Stufe von GRUB, wird von genimage gebraucht
cp -f "$TARGET_DIR/lib/grub/i386-pc/boot.img" "$BINARIES_DIR"
mkdir -p "$TARGET_DIR/var/lib/zynex"
