#!/bin/sh
# Startet das Image in QEMU und wartet auf "zynexd: bereit" auf der seriellen Konsole.
# Danach (nur Hinweis, kein Fehler): wartet auf "zynexd: modell geladen".
IMG="${1:?Pfad zu disk.img}"
LOG="${2:-serial.log}"
MODE="${3:-bios}"
EXTRA=""
DRIVE="if=ide"
NIC="e1000"
if [ "$MODE" = "efi" ]; then EXTRA="-bios /usr/share/ovmf/OVMF.fd"; DRIVE="if=virtio"; NIC="virtio-net-pci"; fi
: > "$LOG"
# -cpu max: das Modell braucht AVX2, die Standard-CPU von QEMU hat das nicht
qemu-system-x86_64 $EXTRA -cpu max -m 4096 -smp 2 -display none -serial "file:$LOG" \
  -drive "file=$IMG,format=raw,$DRIVE" -no-reboot \
  -netdev user,id=n0,hostfwd=tcp::18080-:8080 -device "$NIC,netdev=n0" &
PID=$!
i=0
while [ $i -lt 90 ]; do
  grep -q "zynexd: bereit" "$LOG" 2>/dev/null && break
  sleep 2; i=$((i+1))
done
if ! grep -q "zynexd: bereit" "$LOG"; then
  echo "FEHLER: kein Start innerhalb von 180 s"; tail -50 "$LOG"; kill $PID 2>/dev/null; exit 1
fi
echo "OK: Zynex ist gestartet"
# Weboberflaeche ueber die Netzwerkkarte erreichbar?
i=0; WEB=0
while [ $i -lt 40 ]; do
  curl -s -m 3 http://127.0.0.1:18080/api/status 2>/dev/null | grep -q bereit && { WEB=1; break; }
  sleep 2; i=$((i+1))
done
if [ $WEB = 1 ]; then echo "OK: Weboberflaeche antwortet ($(curl -s -m 3 http://127.0.0.1:18080/api/status))"; curl -s -m 3 http://127.0.0.1:18080/ | head -c 80; echo
else echo "WARNUNG: Weboberflaeche nicht erreichbar (Netzwerk/DHCP?)"; fi
i=0
while [ $i -lt 120 ]; do
  grep -q "zynexd: modell geladen" "$LOG" 2>/dev/null && { echo "OK: Modell geladen"; kill $PID 2>/dev/null; exit 0; }
  sleep 2; i=$((i+1))
done
echo "WARNUNG: Modell nicht innerhalb von 240 s geladen (langsame Emulation oder Fehler)"; tail -30 "$LOG"
kill $PID 2>/dev/null
exit 0
