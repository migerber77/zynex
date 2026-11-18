#!/usr/bin/env python3
"""Erzeugt zynexos/kernel/layouts.c: Tastaturbelegungen als Tabellen (HID-Tastennummer -> Zeichen, Latin-1).
Ebenen: 0 normal, 1 Umschalt, 2 AltGr, 3 AltGr+Umschalt. Index = HID-Nummer - 4 (4..0x38), Index 53 = ISO-Zusatztaste 0x64."""
import sys
US_N = "\0\0\0\0abcdefghijklmnopqrstuvwxyz1234567890\n\033\b\t -=[]\\\0;'`,./"
US_S = "\0\0\0\0ABCDEFGHIJKLMNOPQRSTUVWXYZ!@#$%^&*()\n\033\b\t _+{}|\0:\"~<>?"
K = {c: 4 + i for i, c in enumerate("abcdefghijklmnopqrstuvwxyz")}
for i, c in enumerate("1234567890"): K[c] = 30 + i
K.update({"-": 0x2D, "=": 0x2E, "[": 0x2F, "]": 0x30, "\\": 0x31, "#": 0x32, ";": 0x33, "'": 0x34, "`": 0x35, ",": 0x36, ".": 0x37, "/": 0x38, "iso": 0x64})

def idx(h): return 53 if h == 0x64 else h - 4

def basis():
    t = [[0] * 54 for _ in range(4)]
    for h in range(4, 0x39):
        t[0][idx(h)] = ord(US_N[h]) if h < len(US_N) else 0
        t[1][idx(h)] = ord(US_S[h]) if h < len(US_S) else 0
    return t

def put(t, key, n, s=None, a=None, as_=None):
    h = K[key] if isinstance(key, str) else key
    for pl, v in enumerate((n, s, a, as_)):
        t[pl][idx(h)] = 0 if v is None else ord(v) if isinstance(v, str) else v

def tausch(t, a, b):
    for pl in range(4):
        x, y = idx(K[a]), idx(K[b]); t[pl][x], t[pl][y] = t[pl][y], t[pl][x]

def zeile(t, norm, shift, alt=None):
    for i, c in enumerate("1234567890"):
        put(t, c, norm[i], shift[i], (alt or {}).get(c))

def iso(t, n, s=None, a=None, as_=None):          # Taste links neben Enter: ANSI-Nummer 0x31 und ISO-Nummer 0x32
    put(t, "\\", n, s, a, as_); put(t, "#", n, s, a, as_)

def ch(fr_ch=False):
    t = basis(); tausch(t, "y", "z")
    zeile(t, "1234567890", '+"*ç%&/()=', {"1": "|", "2": "@", "3": "#", "7": "|"})
    put(t, "-", "'", "?", "´"); put(t, "=", "^", "`", "~")
    if fr_ch:
        put(t, "[", "è", "ü", "["); put(t, ";", "é", "ö"); put(t, "'", "à", "ä", "{")
    else:
        put(t, "[", "ü", "è", "["); put(t, ";", "ö", "é"); put(t, "'", "ä", "à", "{")
    put(t, "]", "¨", "!", "]"); iso(t, "$", "£", "}")
    put(t, "`", "§", "°"); put(t, ",", ",", ";"); put(t, ".", ".", ":"); put(t, "/", "-", "_")
    put(t, "iso", "<", ">", "\\")
    return t

def de():
    t = basis(); tausch(t, "y", "z")
    zeile(t, "1234567890", '!"§$%&/()=', {"2": "²", "3": "³", "7": "{", "8": "[", "9": "]", "0": "}"})
    put(t, "q", "q", "Q", "@"); put(t, "m", "m", "M", "µ")
    put(t, "-", "ß", "?", "\\"); put(t, "=", "´", "`"); put(t, "[", "ü", "Ü"); put(t, "]", "+", "*", "~")
    iso(t, "#", "'"); put(t, ";", "ö", "Ö"); put(t, "'", "ä", "Ä"); put(t, "`", "^", "°")
    put(t, ",", ",", ";"); put(t, ".", ".", ":"); put(t, "/", "-", "_"); put(t, "iso", "<", ">", "|")
    return t

def fr():
    t = basis(); tausch(t, "a", "q"); tausch(t, "z", "w")
    put(t, "m", ",", "?"); put(t, ";", "m", "M")
    zeile(t, "&é\"'(-è_çà", "1234567890", {"2": "~", "3": "#", "4": "{", "5": "[", "6": "|", "7": "`", "8": "\\", "9": "^", "0": "@"})
    put(t, "-", ")", "°", "]"); put(t, "=", "=", "+", "}"); put(t, "[", "^", "¨"); put(t, "]", "$", "£", "¤")
    iso(t, "*", "µ"); put(t, "'", "ù", "%"); put(t, "`", "²"); put(t, ",", ";", "."); put(t, ".", ":", "/"); put(t, "/", "!", "§")
    put(t, "iso", "<", ">")
    return t

def it():
    t = basis()
    zeile(t, "1234567890", '!"£$%&/()=')
    put(t, "-", "'", "?"); put(t, "=", "ì", "^"); put(t, "[", "è", "é", "[", "{"); put(t, "]", "+", "*", "]", "}")
    iso(t, "ù", "§"); put(t, ";", "ò", "ç", "@"); put(t, "'", "à", "°", "#"); put(t, "`", "\\", "|")
    put(t, ",", ",", ";"); put(t, ".", ".", ":"); put(t, "/", "-", "_"); put(t, "iso", "<", ">")
    return t

def es():
    t = basis()
    zeile(t, "1234567890", '!"·$%&/()=', {"1": "|", "2": "@", "3": "#", "4": "~", "6": "¬"})
    put(t, "-", "'", "?"); put(t, "=", "¡", "¿"); put(t, "[", "`", "^", "[", "{"); put(t, "]", "+", "*", "]", "}")
    iso(t, "ç", "Ç", "}"); put(t, ";", "ñ", "Ñ"); put(t, "'", "´", "¨", "{"); put(t, "`", "º", "ª", "\\")
    put(t, ",", ",", ";"); put(t, ".", ".", ":"); put(t, "/", "-", "_"); put(t, "iso", "<", ">")
    return t

def uk():
    t = basis()
    put(t, "2", "2", '"'); put(t, "3", "3", "£"); put(t, "'", "'", "@"); iso(t, "#", "~")
    put(t, "`", "`", "¬"); put(t, "iso", "\\", "|")
    return t

def us(): return basis()

L = [("ch", "Deutsch (Schweiz)", ch()), ("chf", "Fran\u00e7ais (Suisse)", ch(True)), ("de", "Deutsch (Deutschland)", de()),
     ("us", "English (US)", us()), ("uk", "English (UK)", uk()), ("fr", "Fran\u00e7ais (France)", fr()),
     ("it", "Italiano", it()), ("es", "Espa\u00f1ol", es())]

def latin1_c(s):                                   # Namen als Latin-1-Bytes ausgeben
    return "".join(c if ord(c) < 128 else "\\x%02X\" \"" % ord(c) for c in s)

out = ["/* AUTOMATISCH ERZEUGT von tools/gen_layouts.py - nicht von Hand aendern */", '#include "layouts.h"', "",
       "const struct kbd_layout kbd_layouts[KBD_LAYOUTS] = {"]
for code, name, t in L:
    out.append('  { "%s", "%s", {' % (code, latin1_c(name)))
    for pl in range(4):
        out.append("    { " + ",".join("0x%02X" % c for c in t[pl]) + " },")
    out.append("  } },")
out.append("};")
open(sys.argv[1] if len(sys.argv) > 1 else "zynexos/kernel/layouts.c", "w").write("\n".join(out) + "\n")
