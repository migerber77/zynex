#!/usr/bin/env python3
"""Erzeugt zynexos/kernel/font_aa.c: geglaettete Schriften (8-Bit-Deckung je Pixel) fuer den Framebuffer.
   Schrift 0-3: DejaVu Sans Mono, feste Breite 8*s, Zeilenhoehe 16*s (ersetzt die Pixelschrift in gfx_text)
   Schrift 4-7: Inter Medium, proportional (Oberflaeche, gfx_text_p)
   Zeichen 32-126 und 160-255 (Latin-1).
   Schrift 8-12: Webseiten: Inter SemiBold 15/20/26/34 (fett, Ueberschriften), Inter Regular 13 (klein)
   Aufruf: python3 tools/mkfont.py <DejaVuSansMono.ttf> <Inter-Medium.otf> <ziel.c> <Inter-SemiBold.otf> <Inter-Regular.otf>"""
import sys
from PIL import Image, ImageDraw, ImageFont

mono_ttf, prop_ttf, ziel, bold_ttf, reg_ttf = sys.argv[1:6]
ZEICHEN = list(range(32, 127)) + list(range(160, 256))
GAMMA = 0.8                      # Deckung leicht anheben (helle Schrift auf dunklem Grund wirkt sonst duenn)

def schrift(pfad, groesse, skala, mono, box=None):
    f = ImageFont.truetype(pfad, groesse)
    box = box or 16 * skala
    grund = round(box * 0.77)      # Grundlinie innerhalb der Zeile
    glyphen = []
    for c in ZEICHEN:
        ch = chr(c)
        pad = groesse
        im = Image.new("L", (groesse * 3, groesse * 3), 0)
        d = ImageDraw.Draw(im)
        d.text((pad, pad * 2), ch, font=f, fill=255, anchor="ls")
        bb = im.getbbox()
        adv = 8 * skala if mono else max(1, round(f.getlength(ch)))
        if not bb:
            glyphen.append((0, 0, 0, 0, adv, b""))
            continue
        crop = im.crop(bb)
        w, h = crop.size
        px = bytes(min(255, round(255 * (v / 255) ** GAMMA)) for v in crop.getdata())
        x0 = bb[0] - pad
        y0 = bb[1] - pad * 2 + grund
        if mono:                   # in die feste Zelle zentrieren
            x0 = round((8 * skala - f.getlength(ch)) / 2) + x0
        glyphen.append((w, h, x0, y0, adv, px))
    return glyphen

fonts = []
for s in (1, 2, 3, 4):
    fonts.append(schrift(mono_ttf, round(8 * s / 0.6021), s, True))
for s, g in ((1, 15), (2, 27), (3, 40), (4, 54)):
    fonts.append(schrift(prop_ttf, g, s, False))
for g, b in ((15, 16), (20, 22), (26, 30), (34, 40)):
    fonts.append(schrift(bold_ttf, g, 1, False, b))
fonts.append(schrift(reg_ttf, 13, 1, False, 16))

daten = bytearray()
meta = []
for fnt in fonts:
    for (w, h, x0, y0, adv, px) in fnt:
        meta.append((len(daten), w, h, x0, y0, adv))
        daten += px

with open(ziel, "w") as o:
    o.write("/* Geglaettete Schriften, erzeugt mit tools/mkfont.py - nicht von Hand aendern.\n")
    o.write(" * DejaVu Sans Mono: Bitstream Vera / DejaVu-Lizenz (frei).  Inter: SIL Open Font License 1.1, (c) The Inter Project Authors.\n")
    o.write(" * Schrift 0-3 = Mono (Skala 1-4), 4-7 = Inter (Skala 1-4), 8-11 Inter SemiBold 15/20/26/34, 12 Inter 13; 191 Zeichen je Schrift (32-126, 160-255). */\n")
    o.write("#include <stdint.h>\n#include \"font_aa.h\"\n")
    o.write("const struct aa_glyph aa_glyphen[%d] = {\n" % len(meta))
    for i, (off, w, h, x0, y0, adv) in enumerate(meta):
        o.write("{%d,%d,%d,%d,%d,%d}," % (off, w, h, x0, y0, adv) + ("\n" if i % 6 == 5 else ""))
    o.write("};\nconst uint8_t aa_daten[%d] = {\n" % len(daten))
    for i in range(0, len(daten), 32):
        o.write(",".join(str(b) for b in daten[i:i + 32]) + ",\n")
    o.write("};\n")
print("Glyphen:", len(meta), "Bytes:", len(daten))
