#!/usr/bin/env python3
"""Bettet src/zynexd/web/index.html als C-Datei ein (zynex_web_html.c). Aufruf: python3 tools/einbetten.py"""
import pathlib
r = pathlib.Path(__file__).resolve().parent.parent / "src" / "zynexd"
d = (r / "web" / "index.html").read_bytes()
zeilen = []
for i in range(0, len(d), 24):
    zeilen.append(",".join(str(b) for b in d[i:i + 24]))
(r / "zynex_web_html.c").write_text(
    "/* Erzeugt von tools/einbetten.py - nicht von Hand aendern. */\n"
    "const unsigned zx_web_html_len = %d;\nconst char zx_web_html[] = {\n%s,0};\n" % (len(d), ",\n".join(zeilen)))
