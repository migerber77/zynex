#!/bin/bash
# mkdisk.sh ISO GROESSE AUSGABEBASIS  -> AUSGABEBASIS.img und .vhdx (GRUB, BIOS, ext2)
set -e
ISO=$1; SIZE=$2; OUT=$3
truncate -s $SIZE $OUT.img
sudo parted -s $OUT.img mklabel msdos mkpart primary ext2 1MiB 100% set 1 boot on
L=$(sudo losetup -fP --show $OUT.img)
sudo mkfs.ext2 -q ${L}p1
sudo mkdir -p /mnt/z; sudo mount ${L}p1 /mnt/z
sudo xorriso -osirrox on -indev $ISO -extract / /mnt/z 2>&1 | tail -1
sudo grub-install --target=i386-pc --boot-directory=/mnt/z/boot --modules="part_msdos ext2 multiboot2" $L
sudo umount /mnt/z; sudo losetup -d $L
qemu-img convert -O vhdx -o subformat=dynamic $OUT.img $OUT.vhdx
ls -la $OUT.img $OUT.vhdx
