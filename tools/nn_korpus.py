#!/usr/bin/env python3
"""Sammelt ein Trainingskorpus fuer das Zynex-Netz: gemeinfreie deutsche Buecher (Project Gutenberg via Gutendex)
und Einleitungen zufaelliger Wikipedia-Artikel (de ~75 %, en ~25 %). Schreibt gemischt nach argv[1]."""
import json, random, sys, urllib.request, urllib.parse, concurrent.futures as cf
import time
def get(u, t=90):
    err = None
    for k in range(5):
        try:
            with urllib.request.urlopen(urllib.request.Request(u, headers={"User-Agent": "ZynexKorpus/1.0 (https://github.com/migerber77/zynex; Lernprojekt)"}), timeout=t) as r: return r.read()
        except urllib.error.HTTPError as e:
            err = e; time.sleep(float(e.headers.get("Retry-After") or 5 * (k + 1)))
        except Exception as e: err = e; time.sleep(3 * (k + 1))
    print("Fehler", u[:90], err, file=sys.stderr); return b""
docs = []
FEST = [2229, 2407, 2591, 7205, 5323, 22367, 6343, 19460, 34811, 2403, 28235, 7206, 2074, 7500, 8911, 21858, 40737, 8238, 19008, 34972]
def buecher(seiten=4):
    gesehen = set()
    u = "https://gutendex.com/books/?languages=de&sort=popular"
    for _ in range(seiten):
        d = json.loads(get(u) or b"{}")
        for b in d.get("results", []):
            f = b.get("formats", {}); url = next((v for k, v in f.items() if k.startswith("text/plain")), None)
            if url and url not in gesehen: gesehen.add(url); yield url
        u = d.get("next")
        if not u: break
    for i in FEST:                                                   # Rueckfall, falls Gutendex nicht antwortet
        url = f"https://www.gutenberg.org/cache/epub/{i}/pg{i}.txt"
        if url not in gesehen: gesehen.add(url); yield url
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
with cf.ThreadPoolExecutor(6) as ex:
    for ps in ex.map(buch, list(buecher())): docs += ps
print("Buchabsaetze:", len(docs))
for l in ["de"] * (n_wiki * 3 // 4) + ["en"] * (n_wiki // 4):          # Wikipedia bittet um sequentielle Abrufe
    docs += wiki(l); time.sleep(1.0)
print("Absaetze gesamt:", len(docs))
random.seed(1); random.shuffle(docs)
txt = "\n".join(docs) + "\n"
open(sys.argv[1], "w", encoding="utf-8").write(txt); print("Bytes:", len(txt.encode()))
