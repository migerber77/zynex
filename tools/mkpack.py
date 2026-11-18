#!/usr/bin/env python3
"""Macht aus einem Platten-Abbild ein Installationspaket (nur nicht-leere 4-KiB-Bloecke).
Format: "ZPK1", u32 Anzahl; je Eintrag: u32 LBA, u32 Sektoren, Daten.  Sektor 0 (Startsektor) ist immer dabei;
Sektoren 1..2047 (GPT) werden vom Installer selbst geschrieben und fehlen deshalb."""
import struct, sys

img = open(sys.argv[1], "rb").read()
BLK = 4096
recs = [(0, img[:512])]
i = 2048 * 512
run = None
while i < len(img):
    blk = img[i:i + BLK]
    if any(blk):
        if run and run[0] + len(run[1]) == i:
            run = (run[0], run[1] + blk)
        else:
            if run: recs.append((run[0] // 512, run[1]))
            run = (i, blk)
    i += BLK
if run:
    recs.append((run[0] // 512, run[1]))
out = b"ZPK1" + struct.pack("<I", len(recs))
for lba, d in recs:
    assert len(d) % 512 == 0
    out += struct.pack("<II", lba, len(d) // 512) + d
open(sys.argv[2], "wb").write(out)
print("Installationspaket: %d Bereiche, %d KiB" % (len(recs), len(out) // 1024))
