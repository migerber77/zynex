#!/bin/sh
# UEFI-Variante: GPT-Image mit EFI-Partition. UUID des Dateisystems in grub.cfg einsetzen.
set -e
BOARD_DIR=$(dirname "$0")
UUID=$(dumpe2fs "$BINARIES_DIR/rootfs.ext2" 2>/dev/null | sed -n 's/^Filesystem UUID: *\(.*\)/\1/p')
cp -f "$BOARD_DIR/grub-efi.cfg" "$BINARIES_DIR/efi-part/EFI/BOOT/grub.cfg"
sed -i "s/UUID_TMP/$UUID/g" "$BINARIES_DIR/efi-part/EFI/BOOT/grub.cfg"
sed "s/UUID_TMP/$UUID/g" "$BOARD_DIR/genimage-efi.cfg" > "$BINARIES_DIR/genimage-efi.cfg"
support/scripts/genimage.sh -c "$BINARIES_DIR/genimage-efi.cfg"
