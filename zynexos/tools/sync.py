#!/usr/bin/env python3
"""sync - Compiler fuer die Zynex-Sprache "Syn" -> .synbin (x86-64, nativ).

Aufruf: python3 sync.py quelle.syn -o ziel.synbin [--dump]

Sprache (eine Anweisung pro Zeile, '#' beginnt einen Kommentar):
  print "text\\n"        Text ausgeben            (braucht Konsole, Bit 0)
  printnum <ausdruck>    Zahl ausgeben            (Bit 0)
  let x = <ausdruck>     Variable anlegen
  x = <ausdruck>         Variable aendern
  return <ausdruck>      Programm beenden, Wert zurueckgeben
  while a < b ... end    Schleife        (Vergleiche: == != < > <= >=)
  if a == b ... else ... end   Bedingung (else ist optional)
Ausdruecke: Ganzzahlen, Variablen, + - * / % ( ) und das Wort  ticks  (Timer lesen, Bit 1).

.synbin-Aufbau (alles little-endian):
  8 Byte  Magic "SYNAPBIN"
  1 Byte  Ziel (1 = CPU x86-64, 2 = NPU, vom Kernel noch nicht unterstuetzt)
  32 Byte Intent-Hash (8 x float32)
  1 Byte  Faehigkeitsmaske (Bit 0 Konsole, Bit 1 Timer)
  8 Byte  Groesse der Nutzlast
  ...     Nutzlast = Maschinencode, Einsprung am Anfang: uint64 f(const struct syn_api*)
"""
import hashlib, re, struct, sys

MAGIC = b"SYNAPBIN"
CAP_CONSOLE, CAP_TIMER = 0x01, 0x02
API_PRINT, API_PRINTNUM, API_TICKS = 0, 8, 16


class Fehler(Exception):
    pass


def tokens(zeile, nr):
    out, i = [], 0
    while i < len(zeile):
        c = zeile[i]
        if c.isspace():
            i += 1
        elif c == '"':
            j, s = i + 1, ""
            while j < len(zeile) and zeile[j] != '"':
                if zeile[j] == "\\" and j + 1 < len(zeile):
                    s += {"n": "\n", "t": "\t", "\\": "\\", '"': '"'}.get(zeile[j + 1], zeile[j + 1])
                    j += 2
                else:
                    s += zeile[j]
                    j += 1
            if j >= len(zeile):
                raise Fehler("Zeile %d: Text nicht geschlossen" % nr)
            out.append(("str", s))
            i = j + 1
        elif c.isdigit():
            m = re.match(r"\d+", zeile[i:])
            out.append(("num", int(m.group())))
            i += len(m.group())
        elif c.isalpha() or c == "_":
            m = re.match(r"[A-Za-z_][A-Za-z0-9_]*", zeile[i:])
            out.append(("id", m.group()))
            i += len(m.group())
        elif zeile[i:i + 2] in ("==", "!=", "<=", ">="):
            out.append(("op", zeile[i:i + 2]))
            i += 2
        elif c in "+-*/%()=<>":
            out.append(("op", c))
            i += 1
        else:
            raise Fehler("Zeile %d: unerwartetes Zeichen '%s'" % (nr, c))
    return out


class Gen:
    def __init__(self):
        self.code = bytearray()
        self.vars = {}
        self.strings = []      # (Text, Position der rel32-Luecke)
        self.caps = 0

    def e(self, *b):
        for x in b:
            self.code += bytes(x) if isinstance(x, (bytes, bytearray, list)) else bytes([x])

    def var_off(self, name, nr, anlegen=False):
        if name not in self.vars:
            if not anlegen:
                raise Fehler("Zeile %d: Variable '%s' ist unbekannt" % (nr, name))
            self.vars[name] = len(self.vars)
        return 16 + 8 * self.vars[name]          # [rbp - off]

    # ---- Ausdruecke (Ergebnis in rax) ----
    def expr(self, tk, p, nr):
        p = self.term(tk, p, nr)
        while p < len(tk) and tk[p] in (("op", "+"), ("op", "-")):
            op = tk[p][1]
            self.e(0x50)                                  # push rax
            p = self.term(tk, p + 1, nr)
            self.e(0x48, 0x89, 0xC1)                      # mov rcx, rax
            self.e(0x58)                                  # pop rax
            if op == "+":
                self.e(0x48, 0x01, 0xC8)                  # add rax, rcx
            else:
                self.e(0x48, 0x29, 0xC8)                  # sub rax, rcx
        return p

    def term(self, tk, p, nr):
        p = self.atom(tk, p, nr)
        while p < len(tk) and tk[p][0] == "op" and tk[p][1] in "*/%" and len(tk[p][1]) == 1:
            op = tk[p][1]
            self.e(0x50)
            p = self.atom(tk, p + 1, nr)
            self.e(0x48, 0x89, 0xC1)                      # mov rcx, rax
            self.e(0x58)                                  # pop rax
            if op == "*":
                self.e(0x48, 0x0F, 0xAF, 0xC1)            # imul rax, rcx
            else:
                self.e(0x48, 0x85, 0xC9)                  # test rcx, rcx
                self.e(0x75, 0x04)                        # jnz +4 (Division durch 0 -> 0)
                self.e(0x31, 0xC0, 0xEB, 0x08 if op == "%" else 0x05)   # xor eax,eax ; jmp ans Ende
                self.e(0x48, 0x99)                        # cqo
                self.e(0x48, 0xF7, 0xF9)                  # idiv rcx
                if op == "%":
                    self.e(0x48, 0x89, 0xD0)              # mov rax, rdx
        return p

    def atom(self, tk, p, nr):
        if p >= len(tk):
            raise Fehler("Zeile %d: Ausdruck unvollstaendig" % nr)
        t, v = tk[p]
        if t == "num":
            self.e(0x48, 0xB8, struct.pack("<Q", v))      # mov rax, imm64
            return p + 1
        if t == "id" and v == "ticks":
            self.caps |= CAP_TIMER
            self.e(0xFF, 0x53, API_TICKS)                 # call [rbx+16]
            return p + 1
        if t == "id":
            self.e(0x48, 0x8B, 0x85, struct.pack("<i", -self.var_off(v, nr)))   # mov rax,[rbp-d]
            return p + 1
        if (t, v) == ("op", "("):
            p = self.expr(tk, p + 1, nr)
            if p >= len(tk) or tk[p] != ("op", ")"):
                raise Fehler("Zeile %d: ')' fehlt" % nr)
            return p + 1
        if (t, v) == ("op", "-"):
            p = self.atom(tk, p + 1, nr)
            self.e(0x48, 0xF7, 0xD8)                      # neg rax
            return p
        raise Fehler("Zeile %d: unerwartet '%s'" % (nr, v))

    JCC_NICHT = {"==": 0x85, "!=": 0x84, "<": 0x8D, ">": 0x8E, "<=": 0x8F, ">=": 0x8C}

    def bedingung(self, tk, p, nr):
        """Wertet 'a OP b' aus und springt zum (noch offenen) Ziel, wenn sie NICHT gilt.
        Gibt (naechste Position, Position der rel32-Luecke) zurueck."""
        p = self.expr(tk, p, nr)
        if p >= len(tk) or tk[p][0] != "op" or tk[p][1] not in self.JCC_NICHT:
            raise Fehler("Zeile %d: Vergleich fehlt (== != < > <= >=)" % nr)
        op = tk[p][1]
        self.e(0x50)
        p = self.expr(tk, p + 1, nr)
        self.e(0x48, 0x89, 0xC1, 0x58)                    # mov rcx,rax ; pop rax
        self.e(0x48, 0x39, 0xC8)                          # cmp rax, rcx
        self.e(0x0F, self.JCC_NICHT[op])
        luecke = len(self.code)
        self.e(struct.pack("<i", 0))
        return p, luecke

    def patch(self, luecke, ziel=None):
        ziel = len(self.code) if ziel is None else ziel
        self.code[luecke:luecke + 4] = struct.pack("<i", ziel - (luecke + 4))

    def epilog(self, frame):
        self.e(0x48, 0x81, 0xC4, struct.pack("<I", frame))  # add rsp, frame
        self.e(0x5B, 0x5D, 0xC3)                             # pop rbx; pop rbp; ret


def kompiliere(quelle):
    g = Gen()
    stmts = []
    for nr, zeile in enumerate(quelle.splitlines(), 1):
        zeile = zeile.split("#", 1)[0] if '"' not in zeile else zeile
        tk = tokens(zeile, nr)
        if tk:
            stmts.append((nr, tk))
    # Variablen vorab zaehlen, damit der Stack-Rahmen feststeht
    namen = set()
    for nr, tk in stmts:
        if tk[0] == ("id", "let") and len(tk) > 1:
            namen.add(tk[1][1])
    n = len(namen)
    frame = 8 + 16 * ((n + 1) // 2)       # haelt den Stack bei Aufrufen 16-Byte-ausgerichtet

    g.e(0x55, 0x48, 0x89, 0xE5)           # push rbp; mov rbp, rsp
    g.e(0x53, 0x48, 0x89, 0xFB)           # push rbx; mov rbx, rdi  (rbx = API)
    g.e(0x48, 0x81, 0xEC, struct.pack("<I", frame))
    bloecke = []        # offene while/if-Bloecke
    for nr, tk in stmts:
        k = tk[0]
        if k == ("id", "while"):
            start = len(g.code)
            p, luecke = g.bedingung(tk, 1, nr)
            if p != len(tk):
                raise Fehler("Zeile %d: Rest der Zeile nicht lesbar" % nr)
            bloecke.append(["while", start, luecke])
        elif k == ("id", "if"):
            p, luecke = g.bedingung(tk, 1, nr)
            if p != len(tk):
                raise Fehler("Zeile %d: Rest der Zeile nicht lesbar" % nr)
            bloecke.append(["if", luecke, None])
        elif k == ("id", "else"):
            if not bloecke or bloecke[-1][0] != "if" or bloecke[-1][2] is not None:
                raise Fehler("Zeile %d: else ohne passendes if" % nr)
            g.e(0xE9)
            sprung = len(g.code)
            g.e(struct.pack("<i", 0))
            g.patch(bloecke[-1][1])
            bloecke[-1][2] = sprung
        elif k == ("id", "end"):
            if not bloecke:
                raise Fehler("Zeile %d: end ohne while/if" % nr)
            b = bloecke.pop()
            if b[0] == "while":
                g.e(0xE9)
                g.e(struct.pack("<i", b[1] - (len(g.code) + 4)))
                g.patch(b[2])
            elif b[2] is None:
                g.patch(b[1])
            else:
                g.patch(b[2])
        elif k == ("id", "print"):
            if len(tk) != 2 or tk[1][0] != "str":
                raise Fehler('Zeile %d: print erwartet einen Text in ""' % nr)
            g.caps |= CAP_CONSOLE
            g.e(0x48, 0x8D, 0x3D)                         # lea rdi,[rip+rel32]
            g.strings.append((tk[1][1], len(g.code)))
            g.e(struct.pack("<i", 0))
            g.e(0xFF, 0x13)                               # call [rbx]
        elif k == ("id", "printnum"):
            g.caps |= CAP_CONSOLE
            if g.expr(tk, 1, nr) != len(tk):
                raise Fehler("Zeile %d: Rest der Zeile nicht lesbar" % nr)
            g.e(0x48, 0x89, 0xC7)                         # mov rdi, rax
            g.e(0xFF, 0x53, API_PRINTNUM)                 # call [rbx+8]
        elif k == ("id", "let") or (k[0] == "id" and len(tk) > 1 and tk[1] == ("op", "=")):
            if k == ("id", "let"):
                if len(tk) < 4 or tk[1][0] != "id" or tk[2] != ("op", "="):
                    raise Fehler("Zeile %d: let x = ausdruck" % nr)
                name, start = tk[1][1], 3
                if name in ("print", "printnum", "let", "return", "ticks", "while", "if", "else", "end"):
                    raise Fehler("Zeile %d: '%s' ist ein reserviertes Wort" % (nr, name))
                off = g.var_off(name, nr, anlegen=True)
            else:
                name, start = k[1], 2
                off = g.var_off(name, nr)
            if g.expr(tk, start, nr) != len(tk):
                raise Fehler("Zeile %d: Rest der Zeile nicht lesbar" % nr)
            g.e(0x48, 0x89, 0x85, struct.pack("<i", -off))  # mov [rbp-d], rax
        elif k == ("id", "return"):
            if len(tk) == 1:
                g.e(0x31, 0xC0)                           # xor eax, eax
            elif g.expr(tk, 1, nr) != len(tk):
                raise Fehler("Zeile %d: Rest der Zeile nicht lesbar" % nr)
            g.epilog(frame)
        else:
            raise Fehler("Zeile %d: Anweisung nicht verstanden" % nr)
    if bloecke:
        raise Fehler("Ein while/if wurde nicht mit end geschlossen")
    g.e(0x31, 0xC0)
    g.epilog(frame)
    # Texte hinter den Code legen und Verweise (RIP-relativ) eintragen
    for text, luecke in g.strings:
        pos = len(g.code)
        g.code += text.encode("utf-8") + b"\0"
        rel = pos - (luecke + 4)
        g.code[luecke:luecke + 4] = struct.pack("<i", rel)
    return bytes(g.code), g.caps


def intent_vektor(quelle):
    """Deterministischer 8-dim Vektor aus dem Quelltext. Das ist KEIN gelerntes Embedding,
    sondern ein stabiler Platzhalter, den spaeter ein echtes Modell ersetzen kann."""
    h = hashlib.sha256(quelle.encode()).digest()
    return [h[i] / 255.0 for i in range(8)]


def paket(code, caps, vec, ziel=1):
    kopf = MAGIC + bytes([ziel]) + struct.pack("<8f", *vec) + bytes([caps]) + struct.pack("<Q", len(code))
    assert len(kopf) == 50
    return kopf + code


def main():
    a = sys.argv[1:]
    if not a or "-o" not in a:
        print(__doc__)
        return 2
    src, out = a[0], a[a.index("-o") + 1]
    q = open(src, encoding="utf-8").read()
    try:
        code, caps = kompiliere(q)
    except Fehler as e:
        print("sync: Fehler:", e)
        return 1
    data = paket(code, caps, intent_vektor(q))
    open(out, "wb").write(data)
    print("sync: %s -> %s (%d Byte Code, Faehigkeiten 0x%02X)" % (src, out, len(code), caps))
    if "--dump" in a:
        print(code.hex())
    return 0


if __name__ == "__main__":
    sys.exit(main())
