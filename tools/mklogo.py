#!/usr/bin/env python3
"""Erzeugt zynexos/kernel/logo.c aus dem Logo-Bild (Zynex OS Quantum): ein kleines (Installer) und ein grosses (Start-/Anmeldebild).
Rand wird weich auf die Hintergrundfarbe 0x0B1220 ausgeblendet. Aufruf: tools/mklogo.py <bild.jpg>"""
import sys, math
from PIL import Image
BG = (0x0B, 0x12, 0x20)
im = Image.open(sys.argv[1]).convert("RGB").crop((430, 120, 980, 700))
def mach(breite):
    h = round(im.height * breite / im.width)
    k = im.resize((breite, h), Image.LANCZOS)
    out = []
    for y in range(h):
        for x in range(breite):
            nx = (x + .5) / breite * 2 - 1; ny = (y + .5) / h * 2 - 1
            d = max(abs(nx), abs(ny)); a = 1.0 if d < .72 else max(0.0, 1 - (d - .72) / .28)
            a = a * a * (3 - 2 * a)
            p = k.getpixel((x, y))
            c = [round(BG[i] + (p[i] - BG[i]) * a) for i in range(3)]
            out.append((c[0] << 16) | (c[1] << 8) | c[2])
    return breite, h, out
s = ["/* Zynex-OS-Quantum-Logo (aus dem Originalbild erzeugt mit tools/mklogo.py), 0x00RRGGBB, weich auf 0x0B1220 ausgeblendet */", "#include <stdint.h>"]
for name, b in (("zynex_logo", 150), ("zynex_logo_gross", 300)):
    w, h, px = mach(b)
    s.append("const int %s_w = %d;\nconst int %s_h = %d;\nconst uint32_t %s[%d] = {" % (name, w, name, h, name, w * h))
    for i in range(0, len(px), 12): s.append(",".join("0x%06X" % v for v in px[i:i + 12]) + ",")
    s.append("};")
open("zynexos/kernel/logo.c", "w").write("\n".join(s) + "\n")
print("ok")
