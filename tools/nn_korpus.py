#!/usr/bin/env python3
"""Sammelt ein Trainingskorpus fuer das Zynex-Netz: gemeinfreie deutsche Buecher (Project Gutenberg via Gutendex)
und Einleitungen zufaelliger Wikipedia-Artikel (de ~75 %, en ~25 %). Schreibt gemischt nach argv[1]."""
import json, random, sys, urllib.request, urllib.parse, concurrent.futures as cf
def get(u, t=60):
    for _ in range(3):
        try:
            with urllib.request.urlopen(urllib.request.Request(u, headers={"User-Agent": "ZynexKorpus/1.0 (migerber77/zynex)"}), timeout=t) as r: return r.read()
        except Exception as e: err = e
    print("Fehler", u[:90], err, file=sys.stderr); return b""
docs = []
def buecher(seiten=3):
    u = "https://gutendex.com/books/?languages=de&sort=popular"
    for _ in range(seiten):
        d = json.loads(get(u) or b"{}")
        for b in d.get("results", []):
            f = b.get("formats", {}); url = next((v for k, v in f.items() if k.startswith("text/plain")), None)
            if url: yield url
        u = d.get("next");
        if not u: break
def buch(url):
    t = get(url, 120).decode("utf-8", "replace")
    a = t.find("*** START"); a = t.find("\n", a) + 1 if a >= 0 else 0; e = t.find("*** END"); e = e if e > a else len(t)
    t = t[a:e]; out = []
    for p in t.replace("\r", "").split("\n\n"):                       # Absaetze zu Zeilen
        p = " ".join(p.split())
        if len(p) > 60: out.append(p)
    return out
def wiki(lang):
    u = f"https://{lang}.wikipedia.org/w/api.php?action=query&generator=random&grnnamespace=0&grnlimit=20&prop=extracts&exintro=1&explaintext=1&exlimit=20&format=json"
    try: d = json.loads(get(u) or b"{}")
    except Exception: return []
    return [" ".join(p["extract"].split()) for p in d.get("query", {}).get("pages", {}).values() if len(p.get("extract", "")) > 80]
n_wiki = int(sys.argv[2]) if len(sys.argv) > 2 else 400
with cf.ThreadPoolExecutor(8) as ex:
    for ps in ex.map(buch, list(buecher())): docs += ps
    nb = len(docs); print("Buchabsaetze:", nb)
    for ps in ex.map(wiki, ["de"] * (n_wiki * 3 // 4) + ["en"] * (n_wiki // 4)): docs += ps
print("Absaetze gesamt:", len(docs))
random.seed(1); random.shuffle(docs)
txt = "\n".join(docs) + "\n"
open(sys.argv[1], "w", encoding="utf-8").write(txt); print("Bytes:", len(txt.encode()))
