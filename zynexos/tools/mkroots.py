#!/usr/bin/env python3
"""Erzeugt kernel/ca_roots.c aus einem PEM-CA-Buendel (Mozilla, z. B. certifi). Aufruf: mkroots.py cacert.pem"""
import re, sys, base64, hashlib
pem = open(sys.argv[1]).read()
ders = [base64.b64decode(b) for b in re.findall(r"-----BEGIN CERTIFICATE-----(.*?)-----END CERTIFICATE-----", pem, re.S)]
blob = bytearray(); tab = []
for d in ders:
    tab.append((len(blob), len(d))); blob += d
o = ["/* Vertrauenswuerdige Root-CAs (Mozilla-Bundle), erzeugt mit tools/mkroots.py - nicht von Hand aendern */",
     "#include <stdint.h>", "const uint32_t ca_root_count = %d;" % len(ders),
     "const uint32_t ca_root_tab[][2] = {" + ",".join("{%d,%d}" % t for t in tab) + "};",
     "const uint8_t ca_root_blob[%d] = {" % len(blob)]
for i in range(0, len(blob), 24):
    o.append(",".join(str(b) for b in blob[i:i+24]) + ",")
o.append("};")
open("kernel/ca_roots.c", "w").write("\n".join(o) + "\n")
print(len(ders), "Roots,", len(blob), "Bytes")
