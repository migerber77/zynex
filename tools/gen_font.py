#!/usr/bin/env python3
# Erzeugt src/zynexd/zynex_font.c aus GNU Unifont (Lizenz: SIL OFL 1.1 / GPLv2+ mit Font-Ausnahme).
import sys
from PIL import Image, ImageDraw, ImageFont
font = ImageFont.truetype(sys.argv[1], 16)
rows = []
for cp in range(32, 256):
    im = Image.new("1", (8, 16), 0)
    ImageDraw.Draw(im).text((0, 0), chr(cp), font=font, fill=1)
    g = [sum((1 << (7 - x)) for x in range(8) if im.getpixel((x, y))) for y in range(16)]
    rows.append((cp, g))
with open(sys.argv[2], "w") as f:
    f.write("/* Erzeugt von tools/gen_font.py aus GNU Unifont (SIL OFL 1.1 / GPLv2+ mit Font-Ausnahme). */\n")
    f.write('#include "zynex_gfx.h"\n\nconst unsigned char zx_font8x16[224][16] = {\n')
    for cp, g in rows:
        f.write("    {" + ",".join("0x%02X" % v for v in g) + "}, /* %d */\n" % cp)
    f.write("};\n")
