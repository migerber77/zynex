#!/bin/bash
# Testet die Setup-ISO: (1) Start von der ISO, (2) Installation auf eine leere Platte, (3) Start der installierten Platte.
# Aufruf: tools/qemu-iso-test.sh <zynex.iso> <bzImage>
set -u
ISO="$(readlink -f "${1:?ISO}")"
KERN="$(readlink -f "${2:?bzImage}")"
INITRD="${ISO%.iso}.initrd.gz"
WORK=$(mktemp -d)
rc=0

warte() { # warte <logdatei> <muster> <sekunden>
  local i=0
  while [ $i -lt $(( $3 / 3 )) ]; do
    grep -q "$2" "$1" 2>/dev/null && return 0
    sleep 3; i=$((i+1))
  done
  return 1
}

echo "== 1. Start von der ISO (GRUB, BIOS) =="
L1="$WORK/iso.log"; : > "$L1"
qemu-system-x86_64 -cpu max -m 3072 -smp 2 -display none -serial "file:$L1" -no-reboot -boot d -cdrom "$ISO" &
P=$!
if warte "$L1" "zynexd: bereit" 420; then echo "OK: ISO startet und Zynex laeuft"; else echo "FEHLER: ISO-Start"; tail -25 "$L1"; rc=1; fi
grep -a "zynexd:\|Zynex:" "$L1" | tail -5
kill $P 2>/dev/null; wait $P 2>/dev/null

echo "== 2. Installation auf leere Platte =="
truncate -s 3600M "$WORK/ziel.img"
L2="$WORK/setup.log"; : > "$L2"
qemu-system-x86_64 -cpu max -m 3072 -smp 2 -display none -serial "file:$L2" -no-reboot \
  -kernel "$KERN" -initrd "$INITRD" -append "console=ttyS0,115200 zynex.install=1 zynex.autoinstall=1" \
  -cdrom "$ISO" -drive "file=$WORK/ziel.img,format=raw,if=ide,index=0" &
P=$!
i=0
while [ $i -lt 300 ]; do
  grep -q "zynexd: setup fertig\|zynexd: setup fehler" "$L2" 2>/dev/null && break
  sleep 5; i=$((i+1))
done
kill $P 2>/dev/null; wait $P 2>/dev/null
grep -a "zynexd:\|zynexsetup:\|Zynex:" "$L2" | tail -14
if ! grep -q "zynexd: setup fertig" "$L2"; then echo "FEHLER: Installation nicht fertig"; tail -30 "$L2"; exit 1; fi
echo "OK: Installation fertig"

echo "== 3. Start der installierten Platte =="
L3="$WORK/ziel.log"; : > "$L3"
qemu-system-x86_64 -cpu max -m 3072 -smp 2 -display none -serial "file:$L3" -no-reboot \
  -drive "file=$WORK/ziel.img,format=raw,if=ide,index=0" &
P=$!
if warte "$L3" "zynexd: bereit" 240; then echo "OK: Installiertes System startet"; else echo "FEHLER: Installiertes System startet nicht"; tail -30 "$L3"; rc=1; fi
kill $P 2>/dev/null; wait $P 2>/dev/null
exit $rc
