#!/usr/bin/env python3
"""Erzeugt kernel/font8x16_l1.c (Latin-1, 0xA0-0xFF) aus DejaVu Sans Mono (freie Lizenz)."""
from PIL import Image, ImageDraw, ImageFont
f = "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf"
fo = ImageFont.truetype(f, 13)
out = ["/* 8x16-Schrift Latin-1 (0xA0-0xFF), aus DejaVu Sans Mono erzeugt (tools/mkfont_latin1.py) */",
       '#include "font8x16.h"', "const uint8_t font8x16_l1[96][16] = {"]
for c in range(0xA0, 0x100):
    im = Image.new("L", (8, 16), 0)
    ImageDraw.Draw(im).text((0, -1), chr(c), font=fo, fill=255)
    g = [sum((1 << (7 - x)) for x in range(8) if im.getpixel((x, y)) > 80) for y in range(16)]
    out.append("    {" + ",".join("0x%02X" % b for b in g) + "}, /* U+%04X */" % c)
out.append("};")
open("kernel/font8x16_l1.c", "w").write("\n".join(out) + "\n")
