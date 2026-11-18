#!/bin/bash
# Installationstest (nur BIOS): bootet das Image mit "autoinstall" und einer leeren zweiten Platte,
# wartet auf "zynexd: setup fertig" und startet dann nur die zweite Platte.
set -u
IMG="${1:?Pfad zu disk.img}"
cd "$(dirname "$IMG")"
WORK=$(mktemp -d)
cp "$IMG" "$WORK/live.img"
# GRUB-Menue der Kopie: Installationseintrag als Standard, mit Automatik
OFF=$(sfdisk -J "$WORK/live.img" | python3 -c 'import sys,json;print(json.load(sys.stdin)["partitiontable"]["partitions"][0]["start"]*512)')
sudo mkdir -p /mnt/zl
sudo mount -o loop,offset=$OFF "$WORK/live.img" /mnt/zl
sudo sed -i 's/zynex.install=1/zynex.install=1 zynex.autoinstall=1/; s/set default="0"/set default="1"/; s/set timeout="5"/set timeout="1"/' /mnt/zl/boot/grub/grub.cfg
sudo umount /mnt/zl
truncate -s 3500M "$WORK/ziel.img"
LOG="$WORK/install.log"; : > "$LOG"
qemu-system-x86_64 -cpu max -m 3072 -smp 2 -display none -serial "file:$LOG" -no-reboot \
  -drive "file=$WORK/live.img,format=raw,if=ide,index=0" -drive "file=$WORK/ziel.img,format=raw,if=ide,index=1" &
PID=$!
i=0
while [ $i -lt 270 ]; do
  grep -q "zynexd: setup fertig\|zynexd: setup fehler" "$LOG" 2>/dev/null && break
  sleep 5; i=$((i+1))
done
kill $PID 2>/dev/null; wait $PID 2>/dev/null
grep "zynexd:\|zynexsetup:" "$LOG" | tail -14
if ! grep -q "zynexd: setup fertig" "$LOG"; then echo "FEHLER: Installation nicht fertig"; tail -30 "$LOG"; exit 1; fi
echo "OK: Installation fertig, starte nur die Zielplatte"
LOG2="$WORK/ziel.log"; : > "$LOG2"
qemu-system-x86_64 -cpu max -m 3072 -smp 2 -display none -serial "file:$LOG2" -no-reboot \
  -drive "file=$WORK/ziel.img,format=raw,if=ide,index=0" &
PID=$!
i=0
while [ $i -lt 60 ]; do
  grep -q "zynexd: bereit" "$LOG2" 2>/dev/null && break
  sleep 3; i=$((i+1))
done
kill $PID 2>/dev/null; wait $PID 2>/dev/null
if grep -q "zynexd: bereit" "$LOG2"; then echo "OK: Installiertes System startet"; exit 0; fi
echo "FEHLER: Installiertes System startet nicht"; tail -30 "$LOG2"; exit 1
