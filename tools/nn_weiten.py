#!/usr/bin/env python3
"""Verbreitert ein vortrainiertes Netz (NH alt -> NH neu = doppelt), ohne dass es sein Wissen verliert (Net2Net):
jedes versteckte Neuron wird kopiert, die Ausgabegewichte beider Kopien halbiert, die Kopie bekommt winziges Rauschen,
damit beide danach verschiedene Wege gehen. Aufruf: nn_weiten.py alt.bin neu.bin NH_alt"""
import struct, sys, random
alt, neu, nh = sys.argv[1], sys.argv[2], int(sys.argv[3])
NE, NW, NV = 12, 16, 256; NI = NE * NW
d = open(alt, "rb").read(); w = list(struct.unpack("<%dh" % (len(d) // 2), d))
assert len(w) == NV * NE + nh * NI + nh + NV * nh + NV, "Groesse passt nicht zu NH_alt"
o = 0; emb = w[o:o + NV * NE]; o += NV * NE
W1 = [w[o + j * NI:o + (j + 1) * NI] for j in range(nh)]; o += nh * NI
b1 = w[o:o + nh]; o += nh
W2 = [w[o + v * nh:o + (v + 1) * nh] for v in range(NV)]; o += NV * nh
b2 = w[o:o + NV]
random.seed(7); r = lambda: random.randint(-3, 3)                   # +-3/2048 = +-0.0015
n2 = 2 * nh
W1n = W1 + [[max(-32767, min(32767, x + r())) for x in row] for row in W1]
b1n = b1 + b1
W2n = [[int(round(x / 2.0)) for x in row] * 2 for row in W2]
out = emb + [x for row in W1n for x in row] + b1n + [x for row in W2n for x in row] + b2
open(neu, "wb").write(struct.pack("<%dh" % len(out), *out)); print("NH", nh, "->", n2, ",", len(out), "Werte")
