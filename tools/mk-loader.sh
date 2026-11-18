#!/bin/bash
# Baut den Zynex-UEFI-Lader mit eingebettetem oeffentlichem Schluessel.
# Aufruf: tools/mk-loader.sh <schluessel.pub|-> [ausgabe.efi]    ("-" = ohne Schluessel, Kernel wird nicht geprueft)
set -euo pipefail
PUB="${1:?pub-datei oder -}"; OUT="${2:-zynexos/build/BOOTX64.EFI}"
HIER="$(cd "$(dirname "$0")/.." && pwd)"
python3 - "$PUB" "$HIER/zynexos/loader/zynex_key.h" <<'PY'
import sys
pub, ziel = sys.argv[1], sys.argv[2]
d = b"\0" * 56 if pub == "-" else open(pub, "rb").read()
assert len(d) == 56, "oeffentlicher LMS-Schluessel muss 56 Byte lang sein"
open(ziel, "w").write("static const unsigned char zynex_pubkey[56] = {%s};\nstatic const int zynex_key_gesetzt = %d;\n" % (",".join(str(b) for b in d), 0 if pub == "-" else 1))
PY
rm -f "$HIER/zynexos/build/BOOTX64.EFI"
make -C "$HIER/zynexos" loader
[ "$OUT" = "zynexos/build/BOOTX64.EFI" ] || cp "$HIER/zynexos/build/BOOTX64.EFI" "$OUT"
ls -la "$OUT"
