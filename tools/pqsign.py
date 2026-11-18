#!/usr/bin/env python3
"""Hash-basierte Signaturen (LMS nach RFC 8554, LMOTS_SHA256_N32_W8) fuer die Zynex-Startkette.
  pqsign.py keygen <privat.json> <oeffentlich.bin> [hoehe 5|10|15]   (5 = 32 Signaturen, 10 = 1024, 15 = 32768)
  pqsign.py sign <privat.json> <signatur.bin> <teil> [<teil> ...]   (signiert die Aneinanderreihung der Dateien)
  pqsign.py verify <oeffentlich.bin> <signatur.bin> <teil> [...]
LMS ist zustandsbehaftet: jeder Schluessel darf pro Blatt nur EINMAL signieren (der Zaehler 'naechstes' in der privaten Datei).
Die private Datei geheim halten; wer sie kopiert und zweimal benutzt, bricht die Sicherheit."""
import hashlib, json, os, struct, sys

LMS_TYP = {5: 5, 10: 6, 15: 7}
OTS_TYP, P, N = 4, 34, 32

def H(*t): return hashlib.sha256(b"".join(t)).digest()
def u32(x): return struct.pack(">I", x)
def u16(x): return struct.pack(">H", x)

def x_i(seed, I, q, i): return H(I, u32(q), u16(i), b"\xff", seed)

def kette(I, q, i, start, a, b):
    t = start
    for j in range(a, b): t = H(I, u32(q), u16(i), bytes([j]), t)
    return t

def ots_pub(seed, I, q):
    ys = [kette(I, q, i, x_i(seed, I, q, i), 0, 255) for i in range(P)]
    return H(I, u32(q), u16(0x8080), *ys)

def baum(seed, I, h):
    T = {}
    for q in range(1 << h): T[(1 << h) + q] = H(I, u32((1 << h) + q), u16(0x8282), ots_pub(seed, I, q))
    for r in range((1 << h) - 1, 0, -1): T[r] = H(I, u32(r), u16(0x8383), T[2 * r], T[2 * r + 1])
    return T

def lies(dateien): return b"".join(open(f, "rb").read() for f in dateien)

def keygen(priv, pub, h):
    seed, I = os.urandom(32), os.urandom(16)
    T = baum(seed, I, h)
    json.dump({"hoehe": h, "seed": seed.hex(), "I": I.hex(), "naechstes": 0, "baum": {str(k): v.hex() for k, v in T.items()}}, open(priv, "w"))
    open(pub, "wb").write(u32(LMS_TYP[h]) + u32(OTS_TYP) + I + T[1])
    print("Schluessel erzeugt: Hoehe %d (%d Signaturen), oeffentlicher Schluessel %s" % (h, 1 << h, pub))

def sign(priv, sig, msg):
    k = json.load(open(priv)); h, q = k["hoehe"], k["naechstes"]
    if q >= (1 << h): sys.exit("FEHLER: Schluessel verbraucht (alle Blaetter benutzt)")
    seed, I = bytes.fromhex(k["seed"]), bytes.fromhex(k["I"]); T = {int(a): bytes.fromhex(b) for a, b in k["baum"].items()}
    k["naechstes"] = q + 1; json.dump(k, open(priv, "w"))            # Zaehler zuerst sichern, dann signieren
    C = os.urandom(32)
    Q = H(I, u32(q), u16(0x8181), C, msg)
    Qc = Q + u16(sum(255 - b for b in Q))
    ys = [kette(I, q, i, x_i(seed, I, q, i), 0, Qc[i]) for i in range(P)]
    node = (1 << h) + q
    pfad = b"".join(T[(node >> i) ^ 1] for i in range(h))
    open(sig, "wb").write(u32(q) + u32(OTS_TYP) + C + b"".join(ys) + u32(LMS_TYP[h]) + pfad)
    print("Signiert (Blatt %d von %d), Signatur %d Byte" % (q, 1 << h, os.path.getsize(sig)))

def verify(pub, sig, msg):
    p, s = open(pub, "rb").read(), open(sig, "rb").read()
    lt = struct.unpack(">I", p[:4])[0]; h = {v: k for k, v in LMS_TYP.items()}[lt]; I, T1 = p[8:24], p[24:56]
    q = struct.unpack(">I", s[:4])[0]; C = s[8:40]; ys = [s[40 + 32 * i:72 + 32 * i] for i in range(P)]
    pfad = s[40 + 32 * P + 4:]
    Q = H(I, u32(q), u16(0x8181), C, msg); Qc = Q + u16(sum(255 - b for b in Q))
    zs = [kette(I, q, i, ys[i], Qc[i], 255) for i in range(P)]
    K = H(I, u32(q), u16(0x8080), *zs)
    node = (1 << h) + q; t = H(I, u32(node), u16(0x8282), K)
    for i in range(h):
        sib = pfad[32 * i:32 * i + 32]
        t = H(I, u32(node // 2), u16(0x8383), *( (sib, t) if node & 1 else (t, sib) ))
        node //= 2
    return t == T1

def bootnachricht(kern, cfg):
    k = open(kern, "rb").read(); c = open(cfg, "rb").read()
    z = c.split(b"\n")[0].split(b"\r")[0][:127]
    return b"ZXBOOT1\0" + struct.pack("<I", len(k)) + k + z

if __name__ == "__main__":
    a = sys.argv[1:]
    if a[:1] == ["keygen"]: keygen(a[1], a[2], int(a[3]) if len(a) > 3 else 10)
    elif a[:1] == ["sign"]: sign(a[1], a[2], lies(a[3:]))
    elif a[:1] == ["bootsign"]:      # bootsign <priv> <sig> <kernel> <cfg>: Nachricht = "ZXBOOT1\\0" | u32le Laenge | Kernel | Kommandozeile (erste Zeile der cfg)
        sign(a[1], a[2], bootnachricht(a[3], a[4]))
    elif a[:1] == ["bootverify"]:
        ok = verify(a[1], a[2], bootnachricht(a[3], a[4])); print("Signatur gueltig" if ok else "Signatur UNGUELTIG"); sys.exit(0 if ok else 1)
    elif a[:1] == ["verify"]:
        ok = verify(a[1], a[2], lies(a[3:])); print("Signatur gueltig" if ok else "Signatur UNGUELTIG"); sys.exit(0 if ok else 1)
    else: sys.exit(__doc__)
