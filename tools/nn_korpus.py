#!/usr/bin/env python3
"""Sammelt ein Trainingskorpus fuer das Zynex-Netz: gemeinfreie deutsche Buecher (Project Gutenberg via Gutendex)
und Einleitungen zufaelliger Wikipedia-Artikel (de ~75 %, en ~25 %). Schreibt gemischt nach argv[1]."""
import json, random, sys, urllib.request, urllib.parse, concurrent.futures as cf
import time
def get(u, t=40):
    err = None
    for k in range(2):
        try:
            with urllib.request.urlopen(urllib.request.Request(u, headers={"User-Agent": "ZynexKorpus/1.0 (https://github.com/migerber77/zynex; Lernprojekt)"}), timeout=t) as r: return r.read()
        except urllib.error.HTTPError as e:
            err = e; time.sleep(min(30.0, float(e.headers.get("Retry-After") or 5 * (k + 1))))
        except Exception as e: err = e; time.sleep(3 * (k + 1))
    print("Fehler", u[:90], err, file=sys.stderr); return b""
docs = []
FEST = [2229, 2407, 2591, 7205, 5323, 22367, 6343, 19460, 34811, 2403, 28235, 7206, 2074, 7500, 8911, 21858, 40737, 8238, 19008, 34972]
def buecher(seiten=1):
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
    if not t:                                                        # gutenberg.org lehnt Cloud-Rechner manchmal ab (503): Spiegel probieren
        import re as _re
        m = _re.search(r"/epub/(\d+)/pg\d+\.txt", url)
        if m:
            i = m.group(1)
            for alt in (f"https://gutenberg.pglaf.org/cache/epub/{i}/pg{i}.txt", f"https://www.gutenberg.org/files/{i}/{i}-0.txt", f"https://www.gutenberg.org/ebooks/{i}.txt.utf-8"):
                t = get(alt, 120).decode("utf-8", "replace")
                if t: break
    a = t.find("*** START"); a = t.find("\n", a) + 1 if a >= 0 else 0; e = t.find("*** END"); e = e if e > a else len(t)
    t = t[a:e]; out = []
    for p in t.replace("\r", "").split("\n\n"):                       # Absaetze zu Zeilen
        p = " ".join(p.split())
        if len(p) > 60: out.append(p)
    return out
import re
def klein(t):                                                        # wie normal() im Kern: Kleinbuchstaben, Satzzeichen weg
    return " ".join(re.sub(r"[^0-9a-z\u0080-\uffff]+", " ", t.lower()).split())
def frage_antwort(titel, ex):
    """Frage-Antwort-Zeilen im Format des Lernkerns (K:<frage> / A:<antwort>), damit das Netz dieses Muster kennt."""
    m = re.match(r"(.{30,260}?[.!?])(\s|$)", ex)
    if not titel or not m or len(titel) > 50: return []
    satz = m.group(1)
    return ["K:" + w + " " + klein(titel) + "\nA:" + satz for w in random.sample(["was ist", "wer ist", "was bedeutet", "erkläre", "was ist ein"], 2)]
def wiki(lang):
    u = f"https://{lang}.wikipedia.org/w/api.php?action=query&generator=random&grnnamespace=0&grnlimit=20&prop=extracts&exintro=1&explaintext=1&exlimit=20&format=json"
    try: d = json.loads(get(u) or b"{}")
    except Exception: return []
    out = []
    for p in d.get("query", {}).get("pages", {}).values():
        ex = " ".join(p.get("extract", "").split())
        if len(ex) <= 80: continue
        out.append(ex)
        if lang == "de": out.extend(frage_antwort(p.get("title", ""), ex))
    return out
n_wiki = int(sys.argv[2]) if len(sys.argv) > 2 else 400
with cf.ThreadPoolExecutor(6) as ex:
    for ps in ex.map(buch, list(buecher())[:14]): docs += ps
print("Buchabsaetze:", len(docs), flush=True)
t_ende = time.time() + 40 * 60                                          # Wikipedia hoechstens 40 Minuten lang
for i, l in enumerate(["de"] * (n_wiki * 3 // 4) + ["en"] * (n_wiki // 4)):   # Wikipedia bittet um sequentielle Abrufe
    if time.time() > t_ende: print("Wikipedia-Zeitlimit erreicht nach", i, "Abrufen"); break
    docs += wiki(l); time.sleep(1.0)
    if i % 50 == 0:
        print("Wikipedia-Abruf", i, "Absaetze", len(docs), flush=True)
        random.shuffle(docs); open(sys.argv[1], "w", encoding="utf-8").write("\n".join(docs) + "\n")     # Zwischenstand
print("Absaetze gesamt:", len(docs))
random.seed(1); random.shuffle(docs)
txt = "\n".join(docs) + "\n"
open(sys.argv[1], "w", encoding="utf-8").write(txt); print("Bytes:", len(txt.encode()))
