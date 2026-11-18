#!/bin/bash
# Baut die Zynex-ISO (bootet per BIOS und UEFI, von DVD oder als USB-Stick-Abbild).
# Aufruf: tools/mk-iso.sh <out/images> <ziel.iso>
# Benoetigt: busybox-static grub-pc-bin grub-efi-amd64-bin mtools xorriso cpio
set -e
IMGDIR="${1:?Verzeichnis mit bzImage und rootfs.ext2}"
OUT="${2:?Ziel-ISO}"
W=$(mktemp -d)
mkdir -p "$W/iso/boot/grub" "$W/iso/zynex" "$W/ird"/{bin,sbin,proc,sys,dev,cd,ro,rw,newroot,mnt}

# Mini-Startsystem (initramfs): sucht die ISO, legt das System schreibbar darueber, startet es
cp /bin/busybox "$W/ird/bin/busybox"
"$W/ird/bin/busybox" --install -s "$W/ird/bin"
# --install -s legt Links mit absolutem Host-Pfad an: in der Initramfs auf relative umbiegen
for l in "$W"/ird/bin/*; do
    [ -L "$l" ] && ln -sf busybox "$l"
done
ls -la "$W/ird/bin/busybox" "$W/ird/bin/sh"
file "$W/ird/bin/busybox" | cut -c1-120
test "$(readlink "$W/ird/bin/mount")" = busybox || { echo "FEHLER: mount-Link falsch"; exit 1; }
"$W/ird/bin/sh" -c "echo initramfs-shell-ok" || { echo "FEHLER: busybox in der Initramfs funktioniert nicht"; exit 1; }
cat > "$W/ird/init" <<'INIT'
#!/bin/busybox sh
export PATH=/bin:/sbin
mount -t proc proc /proc
mount -t sysfs sys /sys
mount -t devtmpfs dev /dev
echo "Zynex: suche Setup-Medium ..." > /dev/kmsg
fertig=0
i=0
while [ $fertig = 0 ] && [ $i -lt 90 ]; do
    for d in /dev/sr* /dev/sd* /dev/vd* /dev/hd* /dev/nvme*n*; do
        [ -b "$d" ] || continue
        if mount -t iso9660 -o ro "$d" /cd 2>/dev/null; then
            if [ -f /cd/zynex/rootfs.ext2 ]; then fertig=1; break; fi
            umount /cd
        fi
    done
    [ $fertig = 1 ] || { sleep 1; i=$((i+1)); }
done
if [ $fertig = 0 ]; then
    echo "Zynex: Setup-Medium nicht gefunden" > /dev/kmsg
    exec /bin/busybox sh
fi
mount -o loop,ro /cd/zynex/rootfs.ext2 /ro || exec /bin/busybox sh
mount -t tmpfs tmpfs /rw
mkdir -p /rw/up /rw/work
mount -t overlay overlay -o lowerdir=/ro,upperdir=/rw/up,workdir=/rw/work /newroot || exec /bin/busybox sh
mkdir -p /newroot/cd /newroot/ro
mount --move /cd /newroot/cd
mount --move /ro /newroot/ro
umount /proc /sys 2>/dev/null
mount --move /dev /newroot/dev 2>/dev/null
exec switch_root /newroot /sbin/init
INIT
chmod +x "$W/ird/init"
(cd "$W/ird" && find . | cpio -o -H newc --quiet | gzip -9) > "$W/iso/boot/initrd.gz"

cp "$IMGDIR/bzImage" "$W/iso/boot/bzImage"
cp "$IMGDIR/rootfs.ext2" "$W/iso/zynex/rootfs.ext2"
cat > "$W/iso/boot/grub/grub.cfg" <<'CFG'
set default="0"
set timeout="5"

menuentry "Zynex installieren (Setup)" {
	linux /boot/bzImage console=ttyS0,115200 console=tty1 zynex.install=1
	initrd /boot/initrd.gz
}

menuentry "Zynex ausprobieren (Live, ohne Installation)" {
	linux /boot/bzImage console=ttyS0,115200 console=tty1
	initrd /boot/initrd.gz
}
CFG
grub-mkrescue -o "$OUT" "$W/iso" -- -volid ZYNEX 2>&1 | tail -3
cp "$W/iso/boot/initrd.gz" "${OUT%.iso}.initrd.gz"
ls -la "$OUT"
rm -rf "$W"
