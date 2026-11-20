#!/bin/bash
# Baut alles: Kernel, Lader (mit frischem Einmal-Schluessel, LMS H=5), Signaturen, Installationspaket, Test-ISO und Release-ISO,
# dazu eine manipulierte Test-ISO. Ergebnis im aktuellen Ordner: zynexos-test.iso zynexos-os.iso zynexos-manip.iso
set -euo pipefail
cd "$(dirname "$0")/.."
make -C zynexos
K=zynexos/build/kernel.bin
D=$(mktemp -d)
python3 tools/pqsign.py keygen "$D/schluessel.json" "$D/schluessel.pub" 5
tools/mk-loader.sh "$D/schluessel.pub" "$D/BOOTX64.EFI"
# UEFI Secure Boot: Lader mit eigenem Schluessel signieren (dauerhaft: Secrets ZYNEX_SB_KEY / ZYNEX_SB_CERT, sonst Wegwerf-Schluessel pro Bau)
if command -v sbsign >/dev/null; then
  if [ -n "${ZYNEX_SB_KEY:-}" ] && [ -n "${ZYNEX_SB_CERT:-}" ]; then
    printf '%s\n' "$ZYNEX_SB_KEY" > "$D/sb.key"; printf '%s\n' "$ZYNEX_SB_CERT" > "$D/sb.crt"; chmod 600 "$D/sb.key"
    openssl x509 -in "$D/sb.crt" -outform DER -out "$D/sb.cer"; echo "Secure Boot: dauerhafter Schluessel aus den Secrets"
  else
    tools/sb-schluessel.sh "$D/sb"; echo "Secure Boot: WEGWERF-Schluessel (nur fuer diesen Bau; Secrets ZYNEX_SB_KEY/ZYNEX_SB_CERT setzen fuer dauerhaften Schluessel)"
  fi
  sbsign --key "$D/sb.key" --cert "$D/sb.crt" --output "$D/BOOTX64.signed.efi" "$D/BOOTX64.EFI"
  sbverify --cert "$D/sb.crt" "$D/BOOTX64.signed.efi"
  mv "$D/BOOTX64.signed.efi" "$D/BOOTX64.EFI"
  cp "$D/sb.crt" zynex-sb.crt; cp "$D/sb.cer" zynex-sb.cer
  export ZYNEX_SB_CER="$PWD/zynex-sb.cer"
else
  echo "Secure Boot: sbsign nicht installiert, Lader bleibt unsigniert"
fi
signiere() { printf '%s\n' "$1" > "$D/cfg-$2"; python3 tools/pqsign.py bootsign "$D/schluessel.json" "$D/sig-$2" "$K" "$D/cfg-$2"; }
signiere "modus=system" system
signiere "modus=live" live
signiere "modus=live installer=gui" gui
tools/mk-zynexos-template.sh "$K" "$D/BOOTX64.EFI" "$D/sig-system" "$D/cfg-system" zynex-install.pack
tools/mk-zynexos-iso.sh "$K" "$D/BOOTX64.EFI" "$D/sig-live" "modus=live" zynexos-test.iso zynex-install.pack
tools/mk-zynexos-iso.sh "$K" "$D/BOOTX64.EFI" "$D/sig-gui" "modus=live installer=gui" zynexos-os.iso zynex-install.pack
# optional: Test-ISO mit Sprachmodell (ZYNEX_MODELL=<datei.gguf>)
[ -n "${ZYNEX_MODELL:-}" ] && tools/mk-zynexos-iso.sh "$K" "$D/BOOTX64.EFI" "$D/sig-live" "modus=live" zynexos-ki.iso zynex-install.pack "$ZYNEX_MODELL"
# optional: Installations-ISO mit Sprachmodell (Desktop-Installer kopiert das Modell auf die Platte)
[ -n "${ZYNEX_MODELL:-}" ] && tools/mk-zynexos-iso.sh "$K" "$D/BOOTX64.EFI" "$D/sig-gui" "modus=live installer=gui" zynexos-os-ki.iso zynex-install.pack "${ZYNEX_MODELL_ISO:-$ZYNEX_MODELL}"
# Manipulation: ein Byte im Kernel aendern, alte Signatur behalten
python3 - "$K" "$D/kernel-manip.bin" <<'PY'
import sys
d = bytearray(open(sys.argv[1], "rb").read()); d[0x2000] ^= 0xFF; open(sys.argv[2], "wb").write(d)
PY
tools/mk-zynexos-iso.sh "$D/kernel-manip.bin" "$D/BOOTX64.EFI" "$D/sig-live" "modus=live" zynexos-manip.iso
rm -rf "$D"
