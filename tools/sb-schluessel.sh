#!/bin/bash
# Erzeugt einen eigenen Secure-Boot-Schluessel (RSA 3072, 10 Jahre): <praefix>.key (GEHEIM), <praefix>.crt (PEM), <praefix>.cer (DER, zum Einspielen in die Firmware).
# Aufruf: tools/sb-schluessel.sh <praefix>
# Fuer dauerhafte Nutzung den privaten Schluessel als GitHub-Secret ZYNEX_SB_KEY und das Zertifikat als ZYNEX_SB_CERT hinterlegen (Inhalt der .key- bzw. .crt-Datei).
set -euo pipefail
P="${1:?praefix}"
openssl req -new -x509 -newkey rsa:3072 -sha256 -nodes -days 3650 -subj "/CN=Zynex OS Secure Boot/O=Zynex OS/" -keyout "$P.key" -out "$P.crt" 2>/dev/null
openssl x509 -in "$P.crt" -outform DER -out "$P.cer"
chmod 600 "$P.key"
echo "Schluessel: $P.key  (geheim halten)   Zertifikat: $P.crt / $P.cer"
