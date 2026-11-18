#!/usr/bin/env python3
"""Startet die Zynex-OS-ISO in QEMU (nur UEFI, eigener Zynex-Lader), tippt Shell-Befehle ueber den QEMU-Monitor
und prueft serielle Ausgabe und Bild.
Aufruf: python3 tools/zynexos_test.py <iso> bios|uefi [szenario ...]
Szenarien: basis, platte, sata, usb, layout, netz   (ohne Angabe: alle)"""
import os, socket, subprocess, sys, time

ISO, MODUS = sys.argv[1], sys.argv[2]
import re
SZEN = sys.argv[3:] or ["basis", "platte", "sata", "usb", "layout", "netz"]
QMP = "/tmp/zynexos.qmp"
LOG, SOCK, PPM = "/tmp/zynexos.log", "/tmp/zynexos.mon", "/tmp/zynexos.ppm"
TASTEN = {" ": "spc", "-": "minus", ".": "dot", "/": "slash", ":": "shift-semicolon", "*": "shift-8", "+": "shift-equal", "(": "shift-9", ")": "shift-0"}


class VM:
    def __init__(self, extra=(), cd=True):
        for f in (LOG, SOCK, QMP):
            if os.path.exists(f):
                os.remove(f)
        a = ["qemu-system-x86_64", "-machine", "q35", "-cpu", "max", "-m", os.environ.get("ZYNEX_MEM", "512M"), "-vnc", "127.0.0.1:77",
             "-serial", "file:" + LOG,
             "-monitor", "unix:%s,server,nowait" % SOCK, "-qmp", "unix:%s,server,nowait" % QMP, "-no-reboot"]
        a += ["-cdrom", ISO, "-boot", "d"] if cd else ["-boot", "c"]
        if MODUS == "uefi":
            ovmf = next(p for p in ("/usr/share/ovmf/OVMF.fd", "/usr/share/OVMF/OVMF_CODE_4M.fd",
                                    "/usr/share/OVMF/OVMF_CODE.fd") if os.path.exists(p))
            a += ["-bios", ovmf]
        self.q = subprocess.Popen(a + list(extra), stderr=open("/tmp/qemu.err", "w"))
        for _ in range(80):
            if os.path.exists(SOCK):
                break
            time.sleep(0.25)
        if self.q.poll() is not None:
            print("QEMU beendet:", open("/tmp/qemu.err").read())
        self.m = socket.socket(socket.AF_UNIX)
        self.m.connect(SOCK)
        time.sleep(0.5)

    def abs_zeiger(self, x, y):
        """Absolute Zeigerposition (0..32767) per QMP an das USB-Tablet senden."""
        q = socket.socket(socket.AF_UNIX)
        q.connect(QMP)
        q.recv(4096)
        for cmd in ('{"execute":"qmp_capabilities"}',
                    '{"execute":"input-send-event","arguments":{"events":['
                    '{"type":"abs","data":{"axis":"x","value":%d}},'
                    '{"type":"abs","data":{"axis":"y","value":%d}}]}}' % (x, y)):
            q.sendall((cmd + "\n").encode())
            time.sleep(0.3)
            q.recv(4096)
        q.close()

    def log(self):
        try:
            return open(LOG, errors="replace").read()
        except FileNotFoundError:
            return ""

    def warte(self, muster, sek):
        t = time.time()
        while time.time() - t < sek:
            if muster in self.log():
                return True
            time.sleep(0.5)
        return False

    def mon(self, cmd, pause=0.12):
        self.m.sendall((cmd + "\n").encode())
        time.sleep(pause)

    def tippe(self, zeile, warte=1.2):
        for c in zeile:
            self.mon("sendkey " + TASTEN.get(c, c))
        self.mon("sendkey ret")
        time.sleep(warte)

    def hell(self):
        self.mon("screendump " + PPM, 1.0)
        try:
            d = open(PPM, "rb").read()
            teile = d.split(b"\n", 3)
            px = teile[3]
            return sum(1 for i in range(0, len(px) - 2, 3) if px[i + 1] > 150)
        except Exception:
            return 0

    def ende(self):
        self.q.kill()
        self.q.wait()


ergebnis = []


def bildschirm(out):
    m = re.search(r"\[LOADER\] Bildschirm (\d+)x(\d+)", out)
    return (int(m.group(1)), int(m.group(2))) if m and int(m.group(1)) else (1024, 768)


def pruefe(name, ok):
    print(("OK:    " if ok else "FEHLT: ") + name)
    ergebnis.append(ok)


def starten(extra=(), cd=True, us=True):
    vm = VM(extra, cd)
    if not vm.warte("All systems online", 150):
        print("FEHLER: Kernel kommt nicht bis 'All systems online'")
        print(vm.log())
        vm.ende()
        sys.exit(1)
    print("OK: Kernel gestartet (%s)" % MODUS)
    if us:
        vm.tippe("tastatur us", 1.0)                 # QEMU sendet US-Tasten; Standard der PS/2-/USB-Tastatur ist Schweiz
    return vm


def basis():
    print("\n===== Szenario basis =====")
    vm = starten()
    for b in ["help", "mem", "syninfo", "syncreate foo", "synwrite foo hallo welt", "syncat foo",
              "whoami", "syntag foo ai", "synfind ai", "synls", "synsync",
              "snap neu vor", "synwrite foo geaendert", "syncat foo", "snap zurueck vor", "syncat foo", "snap liste", "obj ai",
              "snap loeschen vor", "snap liste",
              "syncreate bar", "syntag foo a1", "syntag foo a2", "syntag foo a3", "syntag foo a4", "syntag foo a5", "syntag bar ai",
              "obj ai a5", "obj ai", "synuntag foo a5", "obj a5", "syntag bar zz", "zeit", "syntag foo a6", "syntag foo a7", "syntag foo a8",
              "syntag foo a9", "syninfo", "aidemo", "ps", "uptime",
              "pci", "block",
              "synhdr hallo.synbin", "synrun hallo.synbin", "synrun rechnen.synbin",
              "synrun zeit.synbin", "syngrant 3", "synrun zeit.synbin", "synrun schleife.synbin",
              "synstress 300 2 schreiben", "synstress 1 1024 schreiben", "syninfo", "snap neu gross", "synstress 300 2 loeschen", "synstress 300 2 pruefen",
              "snap zurueck gross", "synstress 300 2 pruefen", "synstress 1 1024 pruefen", "snap loeschen gross", "synstress 300 2 loeschen", "synstress 1 1024 loeschen", "syninfo", "wpatest",
              "qubit selbsttest", "qubit reset 2", "qubit h 0", "qubit cnot 0 1", "qubit zustand", "qubit messen 0", "qubit messen 1", "qubit reset 5"]:
        vm.tippe(b, 4.0 if b.startswith("synstress") or b.startswith("snap") else 1.2)
    time.sleep(1)
    hell = vm.hell()
    out = vm.log()
    print("=========== SERIELLE AUSGABE ===========")
    print(out)
    print("=========== PRUEFUNG ===========")
    pruefe("Text auf dem Bildschirm sichtbar (%d helle Pixel)" % hell, hell > 500)
    for name, muster, anzahl in [
        ("Shell antwortet auf help", "syncreate", 1),
        ("SynFS: Datei gelesen", "hallo welt", 1),
        ("SynFS: Tag-Suche findet foo", "foo", 2),
        (".synbin: Programm laeuft (6*7)", "6 mal 7 = 42", 1),
        (".synbin: Punkt vor Strich", "10*10-1 = 99", 1),
        (".synbin: Berechtigung verweigert", "Berechtigung fehlt", 1),
        (".synbin: Timer nach syngrant", "Ticks seit dem Start: ", 1),
        (".synbin: while-Schleife", "Summe 1..10 = 55", 1),
        (".synbin: if/else (FizzBuzz)", "Fizz 4 Buzz Fizz 7 8 Fizz Buzz 11 Fizz 13 14 FizzBuzz", 1),
        ("Ohne Konten: Einrichtungsmodus", "Kein Konto angelegt", 1),
        ("Snapshot: erstellt", "Snapshot erstellt", 1),
        ("Snapshot: geaenderte Datei sichtbar", "geaendert", 2),
        ("Snapshot: Zustand zurueckgesetzt", "Zustand zurueckgesetzt", 1),
        ("Snapshot: alter Inhalt wieder da", "hallo welt", 3),
        ("Snapshot: Liste zeigt Namen", "  vor  (Nr.", 1),
        ("Objekt-Abfrage nach Tag", "Objekte mit Tag 'ai':", 1),
        ("Snapshot: geloescht", "Snapshot geloescht", 1),
        ("PCI-Aufzaehlung", "Geraete gefunden", 1)]:
        pruefe(name, out.count(muster) >= anzahl)

    def abschnitt(befehl):
        return out.split("zynex> " + befehl, 1)[-1].split("zynex>", 1)[0]
    import re as _re
    st = [(int(x), int(y)) for x, y in _re.findall(r"synstress: (\d+) OK, (\d+) Fehler", out)]
    pruefe("SynFS gross: 300 Dateien geschrieben und zurueckgelesen", st[:1] == [(300, 0)])
    pruefe("SynFS gross: 1-MiB-Datei geschrieben und zurueckgelesen", st[1:2] == [(1, 0)])
    pruefe("SynFS gross: Snapshot mit 300 Dateien, danach alle geloescht", st[2:4] == [(300, 0), (0, 300)])
    pruefe("SynFS gross: Rueckkehr zum Snapshot stellt alle 300 Dateien und die 1-MiB-Datei her", st[4:6] == [(300, 0), (1, 0)])
    pruefe("SynFS gross: Aufraeumen", st[6:8] == [(300, 0), (0, 1)])
    pruefe("SynFS gross: mehr als 64 Inodes (Info zeigt Gesamtzahl)", _re.search(r"Free inodes: \d+ of (\d+)", out) is not None and int(_re.search(r"Free inodes: \d+ of (\d+)", out).group(1)) > 64)
    pruefe("WLAN-Krypto: Selbsttest mit Testvektoren bestanden (WPA2-PSK, Key-Wrap, CMAC, CCMP)", "wpa: Selbsttest bestanden" in out and "FEHLER" not in out.split("zynex> wpatest", 1)[-1].split("zynex>", 1)[0])
    qt = out.split("zynex> qubit selbsttest", 1)[-1]
    pruefe("Quanten-Kernel: Selbsttest 15/15 (Gatter, Bell, GHZ, Messung, Zufall)", "[QUBIT] Selbsttest: 15/15 bestanden" in qt and "FEHLT" not in qt.split("zynex>", 1)[0])
    pruefe("Quanten-Kernel per Systemaufruf: Bell-Zustand 50 % |00> und 50 % |11>", "|00>  50.0 %" in qt and "|11>  50.0 %" in qt and "|01>  0.0 %" in qt)
    mq = re.findall(r"Qubit (\d) gemessen: (\d)", qt)
    pruefe("Quanten-Kernel: Messung kollabiert, verschraenkte Qubits stimmen ueberein", len(mq) == 2 and mq[0][1] == mq[1][1])
    pruefe("Quanten-Kernel: ungueltige Angabe wird abgelehnt", "Qubit: ungueltige Angabe" in qt)
    pruefe("Tags: mehr als 4 Tags pro Datei", "Tagged" in abschnitt("syntag foo a5"))
    pruefe("Tags: Maximum 8 wird gemeldet", "Maximal 8 Tags" in abschnitt("syntag foo a9"))
    ab = abschnitt("obj ai a5")
    pruefe("Objekt-Abfrage mit zwei Tags (UND): nur foo", "  foo" in ab and "bar" not in ab)
    ms = re.findall(r"zynex> obj ai\s*\n(.*?)zynex>", out, re.S)
    ab = ms[-1] if ms else ""
    pruefe("Objekt-Abfrage mit einem Tag: foo und bar", "foo" in ab and "bar" in ab)
    pruefe("Tag entfernen", "(keine)" in abschnitt("obj a5"))
    z = abschnitt("zeit")
    pruefe("Zeitleiste: zuletzt geaenderte Datei zuerst", "Zeitleiste" in z and 0 <= z.find("bar") < z.find("foo"))
    print("INFO Zeitleiste: %s" % ("Datum/Uhrzeit aus der Hardware-Uhr" if re.search(r"#\d+  \d{4}-\d\d-\d\d \d\d:\d\d", z) else "Uhr unplausibel (nur Nummern)"))
    vm.ende()


def platte(dev="nvme0", titel="NVMe", extra=None, img="/tmp/zynex-nvme.img", bereit="nvme0 bereit"):
    print("\n===== Szenario platte (%s) =====" % titel)
    subprocess.run(["qemu-img", "create", "-f", "raw", img, "64M"], check=True, stdout=subprocess.DEVNULL)
    if extra is None:
        extra = ["-drive", "file=%s,if=none,id=nv0,format=raw" % img, "-device", "nvme,serial=zynex0,drive=nv0"]

    vm = starten(extra)
    for b in ["block", "synformat %s" % dev, "synformat %s ja" % dev, "block", "syncreate notiz",
              "synwrite notiz daten-auf-der-platte", "synsync", "synls", "snap neu s1",
              "synwrite notiz neuer-inhalt", "syntag notiz wichtig", "synsync",
              "benutzer neu michel admin", "geheim12", "geheim12", "benutzer neu x1 gast", "aaaa1", "bbbb1", "benutzer neu gast1 gast", "pw1234", "pw1234",
              "benutzer neu zweiter benutzer", "pw5678", "pw5678", "whoami", "benutzer liste", "synsync"]:
        vm.tippe(b, 6.0 if b in ("geheim12", "aaaa1", "bbbb1", "pw1234", "pw5678") else 2.0)
    out1 = vm.log()
    print(out1[out1.find("zynex> block"):])
    vm.ende()

    vm = starten(extra)                    # zweiter Start: Daten muessen noch da sein
    for b in ["synmount %s" % dev, "michel", "falsch1", "michel", "geheim12",
              "synls", "syncat notiz", "synrun hallo.synbin", "snap liste", "obj wichtig", "snap zurueck s1", "syncat notiz",
              "snap loeschen s1", "snap liste", "whoami", "benutzer liste",
              "anmelden", "gast1", "pw1234", "whoami", "synformat %s ja" % dev, "synwrite notiz x", "http 10.0.2.2:8080/hallo.txt",
              "anmelden", "zweiter", "pw5678", "whoami", "syncat sys.benutzer", "synformat %s ja" % dev, "synwrite notiz erlaubt", "syncat notiz",
              "benutzer neu dritter gast", "benutzer liste"]:
        vm.tippe(b, 7.0 if b in ("falsch1", "geheim12", "pw1234", "pw5678") or b.startswith("synmount") else 2.0)
    out2 = vm.log()
    print(out2[out2.find("zynex> synmount"):])
    vm.ende()
    print("=========== PRUEFUNG ===========")
    pruefe("%s-Platte erkannt" % titel, bereit in out1)
    pruefe("Formatieren braucht Bestaetigung", "Zum Bestaetigen" in out1)
    pruefe("SynFS auf %s formatiert" % dev, "Formatiert und eingebunden" in out1)
    vor = out2.split("zynex> snap zurueck", 1)[0]
    pruefe("Nach Neustart: Datei von der Platte gelesen", "neuer-inhalt" in vor)
    pruefe("Benutzer: Konto angelegt, Passwort-Abfrage", "Benutzer angelegt: michel (admin)" in out1 and "Passwort wiederholen:" in out1)
    pruefe("Benutzer: ungleiche Passwoerter werden abgelehnt", "Die Passwoerter sind verschieden" in out1 and "x1" not in out1.split("zynex> benutzer liste", 1)[-1])
    pruefe("Benutzer: Passwort wird nie angezeigt", "geheim12" not in out1 + out2 and "pw1234" not in out1 + out2 and "pw5678" not in out1 + out2)
    pruefe("Benutzer: Konten bleiben nach Neustart (Login-Abfrage)", "Benutzer: " in out2 and "Passwort: " in out2)
    pruefe("Benutzer: falsches Passwort abgelehnt", "Anmeldung fehlgeschlagen" in out2)
    pruefe("Benutzer: Anmeldung als Admin", "Willkommen, michel (admin)" in out2)
    pruefe("Benutzer: Liste zeigt alle Konten", all(t in out2 for t in ("  michel  (admin)", "  gast1  (gast)", "  zweiter  (benutzer)")))
    pruefe("Benutzer: Gast darf nicht formatieren/schreiben/ins Netz", all(t in out2 for t in ("Keine Berechtigung fuer: synformat", "Keine Berechtigung fuer: synwrite", "Keine Berechtigung fuer: Netzwerk")))
    pruefe("Benutzer: normaler Benutzer ohne Systemdateien/Formatieren, aber mit Schreibrecht", "Keine Berechtigung fuer: Systemdatei sys.*" in out2 and out2.count("Keine Berechtigung fuer: synformat") >= 2 and "Wrote 7 B" in out2)
    pruefe("Benutzer: nur Admin legt Konten an", "Keine Berechtigung fuer: benutzer neu" in out2)
    pruefe("Nach Neustart: Snapshot noch vorhanden", "  s1  (Nr." in vor)
    pruefe("Nach Neustart: Tag-Abfrage findet Datei", "Objekte mit Tag 'wichtig':\n  notiz" in vor)
    pruefe("Snapshot-Rueckkehr: alter Inhalt von der Platte", "daten-auf-der-platte" in out2.split("zynex> snap zurueck", 1)[-1])
    pruefe("Snapshot geloescht, Liste leer", "(keine)" in out2.split("zynex> snap loeschen", 1)[-1])
    pruefe("Nach Neustart: .synbin von der Platte gestartet", "6 mal 7 = 42" in out2)





def sata():
    img = "/tmp/zynex-sata.img"
    extra = ["-drive", "file=%s,if=none,id=satadisk,format=raw" % img, "-device", "ide-hd,drive=satadisk,bus=ide.0"]
    platte("sata0", "SATA", extra, img, "sata0 bereit")


def installieren():
    print("\n===== Szenario installer (Installation auf SATA-Platte, danach Start nur von der Platte) =====")
    img = "/tmp/zynex-ziel.img"
    subprocess.run(["qemu-img", "create", "-f", "raw", img, "256M"], check=True, stdout=subprocess.DEVNULL)
    extra = ["-drive", "file=%s,if=none,id=satadisk,format=raw" % img, "-device", "ide-hd,drive=satadisk,bus=ide.0"]
    vm = starten(extra)
    for b in ["help", "block", "install sata0 michel us"]:
        vm.tippe(b, 2.0)
    vm.tippe("install sata0 michel us ja", 2.0)
    vm.tippe("geheim12", 4.0)
    vm.tippe("geheim12", 2.0)
    fertig = vm.warte("Fertig! Entferne", 300)
    time.sleep(1)
    for b in ["block", "synls"]:
        vm.tippe(b, 2.0)
    vm.tippe("reboot", 3.0)
    beendet = False
    for _ in range(40):
        if vm.q.poll() is not None:
            beendet = True
            break
        time.sleep(0.5)
    out1 = vm.log()
    print(out1[out1.find("zynex> install"):])
    vm.ende()

    # Neustart: nur die Platte, keine ISO
    vm = starten(extra, cd=False, us=False)
    vm.warte("[DESKTOP] Anmeldebildschirm", 20)
    for b in ["michel", "falsch1", "michel", "geheim12"]:
        vm.tippe(b, 7.0 if b in ("falsch1", "geheim12") else 2.0)
    for b in ["text erstellen", "hallo welt"]:
        vm.tippe(b, 2.0)
    vm.mon("sendkey esc"); time.sleep(2.0)
    for b in ["rechne 12*7", "system", "dateien", "quanten", "sicherheit", "hilfe", "briefing", "agent wartung", "aufgabe milch kaufen", "arbeitsflaeche system", "firewall"]:
        vm.tippe(b, 2.0)
    vm.tippe("browser", 3.0)
    vm.mon("sendkey esc"); time.sleep(2.0)
    vm.tippe("widget uhr", 2.0)
    vm.tippe("rechner", 3.0)
    vm.mon("sendkey esc"); time.sleep(2.0)
    vm.tippe("einstellungen", 3.0)
    vm.mon("sendkey esc"); time.sleep(2.0)
    vm.tippe("sprache franzoesisch", 2.0)
    vm.tippe("sprache deutsch", 2.0)
    vm.tippe("chats", 3.0)
    vm.mon("sendkey esc"); time.sleep(2.0)
    vm.tippe("kauderwelsch", 2.0)
    hell_d = vm.hell()
    vm.tippe("wartung", 2.0)
    for b in ["whoami", "block", "synls", "syninfo", "synrun hallo.synbin", "benutzer liste", "install sata0 x ja"]:
        vm.tippe(b, 2.0)
    out2 = vm.log()
    hell = vm.hell()
    print(out2)
    vm.ende()
    print("=========== PRUEFUNG ===========")
    pruefe("Installer: Paket geladen, Hilfe zeigt install", "install <platte>" in out1)
    pruefe("Installer: Sicherheitsabfrage vor dem Loeschen", "Zum Bestaetigen: install sata0 michel us ja" in out1)
    pruefe("Installer: Passwort-Abfrage zweimal", out1.count("Passwort") >= 2 and "geheim12" not in out1)
    pruefe("Installer: Installation abgeschlossen", fertig)
    pruefe("Installer: Partitionen sata0p1-2 angelegt", all(("sata0p%d" % n) in out1.split("zynex> block", 2)[-1] for n in (1, 2)))
    pruefe("Installer: reboot startet die Maschine neu (QEMU beendet)", beendet)
    pruefe("System von der Platte: Kernel startet (Zynex-Lader von der Platte)", "All systems online" in out2)
    pruefe("System von der Platte: Datenpartition eingebunden", "[SYSTEM] Installiertes System von sata0p2 gestartet" in out2)
    pruefe("Desktop: Anmeldebildschirm statt Shell", "[DESKTOP] Anmeldebildschirm" in out2)
    pruefe("Desktop: falsches Passwort abgelehnt", "[DESKTOP] Anmeldung fehlgeschlagen" in out2)
    pruefe("Desktop: Anmeldung als Admin", "[DESKTOP] Willkommen, michel (admin)" in out2)
    pruefe("Desktop: 'text erstellen' oeffnet die Textverarbeitung", "-> Textverarbeitung" in out2)
    pruefe("Desktop: Dokument wird gespeichert", "[DESKTOP] Dokument gespeichert: dokument-1.txt (11 Bytes)" in out2)
    pruefe("Desktop: Rechnen", "12*7 = 84" in out2)
    pruefe("Desktop: System, Dateien, Quanten, Sicherheit, Hilfe", all(x in out2 for x in ("-> System", "-> Dateien", "-> Quanten", "[DESKTOP] Quanten-Demo: Messung", "-> Sicherheit", "-> Hilfe")))
    pruefe("Desktop: Agent Briefing laeuft", "[AGENT] Start: Morning briefing" in out2 and "[AGENT] Schritt: " in out2)
    pruefe("Desktop: Agent fragt Freigabe an", "[AGENT] Freigabe angefragt: Dateisystem sichern" in out2)
    pruefe("Desktop: Aufgabe gespeichert", "[DESKTOP] Aufgabe gespeichert: milch kaufen" in out2)
    pruefe("Desktop: Arbeitsflaeche dreht sich", "[DESKTOP] Arbeitsflaeche 2: System" in out2)
    pruefe("Desktop: Browser oeffnet sich", "[DESKTOP] Browser geoeffnet" in out2)
    pruefe("Desktop: Widget wird hinzugefuegt", "[DESKTOP] Widget hinzugefuegt: 1" in out2)
    pruefe("Desktop: Einstellungsfenster oeffnet sich", "[DESKTOP] Fenster geoeffnet: Einstellungen" in out2)
    pruefe("Desktop: Chats-Fenster oeffnet sich", "[DESKTOP] Fenster geoeffnet: Chats" in out2)
    pruefe("Desktop: Sprachwechsel per Chat", out2.count("-> Fenster") >= 3)
    pruefe("Desktop: Rechner-Fenster oeffnet sich", "[DESKTOP] Fenster geoeffnet: Rechner" in out2)
    pruefe("Desktop: Unbekanntes wird nicht verstanden", "kauderwelsch' -> nicht verstanden" in out2)
    pruefe("Desktop: Bildschirm ist belegt", hell_d > 300)
    pruefe("Desktop: Wartungs-Konsole fuer Admin", "[WARTUNG] Text-Konsole" in out2)
    pruefe("System von der Platte: Partitionen sichtbar", all(("sata0p%d" % n) in out2 for n in (1, 2)))
    pruefe("System von der Platte: Programme laufen (.synbin)", "6 mal 7 = 42" in out2)
    pruefe("System von der Platte: nur Live-ISO darf installieren", "Nur von der Installations-ISO aus moeglich" in out2)
    pruefe("System von der Platte: Bildschirm zeigt Text", hell > 300)


def kiinstall():
    """Installation MIT Sprachmodell: Modell geht vom Stick auf die Modell-Partition; danach Start nur von der Platte, 'ki' laedt es von dort."""
    print("\n===== Szenario kiinstall (Modell wandert vom Stick auf die Platte) =====")
    img = "/tmp/zynex-ki.img"
    subprocess.run(["qemu-img", "create", "-f", "raw", img, "1500M"], check=True, stdout=subprocess.DEVNULL)
    extra = ["-drive", "file=%s,if=none,id=satadisk,format=raw" % img, "-device", "ide-hd,drive=satadisk,bus=ide.0"]
    vm = starten(extra)
    vm.tippe("install sata0 michel us ja", 2.0)
    vm.tippe("geheim12", 4.0)
    vm.tippe("geheim12", 2.0)
    fertig = vm.warte("Fertig! Entferne", 1500)
    time.sleep(1)
    vm.tippe("block", 2.0)
    vm.tippe("reboot", 3.0)
    for _ in range(40):
        if vm.q.poll() is not None:
            break
        time.sleep(0.5)
    out1 = vm.log()
    vm.ende()
    vm = starten(extra, cd=False, us=False)
    vm.warte("[DESKTOP] Anmeldebildschirm", 20)
    for b in ["michel", "geheim12"]:
        vm.tippe(b, 7.0 if b == "geheim12" else 2.0)
    vm.tippe("system", 2.0)                                   # kurzer Befehl: wie bisher, ohne Modell
    vm.tippe("wieviel ist vierzig plus zwei", 1.0)            # ganzer Satz: Zynie fragt das Sprachmodell
    ok = vm.warte("[DESKTOP] Zynie antwortet:", 1500)
    time.sleep(2)
    hell_ki = vm.hell()
    out2 = vm.log()
    print(out1[-1500:]); print(out2[-2500:])
    vm.ende()
    pruefe("Modell-Installation: abgeschlossen", fertig)
    pruefe("Modell-Installation: dritte Partition sata0p3", "sata0p3" in out1.split("zynex> block")[-1])
    pruefe("Von der Platte: Modell wird aus der Partition geladen", "[KI] Lade Sprachmodell von sata0p3" in out2)
    pruefe("Desktop: kurzer Befehl 'system' geht weiter an die Systemanzeige", "-> System" in out2)
    pruefe("Desktop: ganzer Satz geht an das Sprachmodell", "-> Sprachmodell" in out2)
    pruefe("Desktop: Bildschirm zeigt Zynies Antwort", hell_ki > 300)
    pruefe("Von der Platte: Modell bereit", "[KI] Sprachmodell bereit" in out2)
    pruefe("Von der Platte: Lader hat kein Modell vom Stick gelesen", "[LOADER] Lese Sprachmodell" not in out2)
    pruefe("Desktop: Zynie antwortet", ok)
    m = re.search(r"\[DESKTOP\] Zynie antwortet: (.*)", out2); antwort = m.group(1).strip() if m else ""
    host, modell = os.environ.get("ZYNEX_LLM_HOST"), os.environ.get("ZYNEX_MODELL")
    if host and modell:
        r = subprocess.run([host, modell, "chat", "wieviel ist vierzig plus zwei", "200"], capture_output=True, text=True)
        h = re.search(r"Antwort: (.*)", r.stdout); erwartet = h.group(1).strip() if h else "?"
        print("Platte:", antwort, "| Referenz:", erwartet)
        pruefe("Desktop: Antwort identisch mit der Referenz (Wortlaut)", antwort.replace(" ", "") == erwartet.replace("\n", " ").replace(" ", ""))


def layout():
    print("\n===== Szenario layout (Schweizer Tastatur) =====")
    extra = ["-device", "qemu-xhci,id=xhci", "-device", "usb-kbd,bus=xhci.0"]
    vm = starten(extra, us=False)
    vm.tippe("echo y z")                       # QEMU-US-Tasten y,z -> de_CH: z,y
    for k in ["e", "c", "h", "o", "spc"]:
        vm.mon("sendkey " + k)
    vm.mon("sendkey bracket_left")             # ue
    vm.mon("sendkey semicolon")                # oe
    vm.mon("sendkey apostrophe")               # ae
    vm.mon("sendkey ret", 0.12)
    time.sleep(1.5)
    vm.tippe("tastatur")
    time.sleep(1)
    vm.tippe("tastatur fr", 1.0)                # AZERTY: Tasten q, w, ; (US-Positionen) -> a, z, m
    for k in ["e", "c", "h", "o", "spc", "q", "w", "semicolon"]:
        vm.mon("sendkey " + k)
    vm.mon("sendkey ret", 0.12)
    time.sleep(1.5)
    out = vm.log()
    hell = vm.hell()
    print(out[out.find("[PCI]") - 5:])
    vm.ende()
    print("=========== PRUEFUNG ===========")
    pruefe("Z und Y getauscht", "echo z y" in out)
    pruefe("Umlaute ue oe ae", "\u00fc\u00f6\u00e4" in out)
    pruefe("Tastatur-Befehl meldet Schweiz", "Schweiz" in out)
    pruefe("Weitere Belegung (fr, AZERTY) umschaltbar", "echo azm" in out)
    pruefe("Bildschirm zeigt Text", hell > 300)


def ps2():
    print("\n===== Szenario ps2 (PS/2-Tastatur mit Schweizer Layout, PS/2-Maus) =====")
    vm = starten((), us=False)                  # ohne USB: Tastatur und Maus haengen am i8042 wie im Notebook
    vm.tippe("echo y z")                        # QEMU-US-Tasten y,z -> de_CH: z,y
    for k in ["e", "c", "h", "o", "spc"]:
        vm.mon("sendkey " + k)
    vm.mon("sendkey bracket_left")              # ue
    vm.mon("sendkey semicolon")                 # oe
    vm.mon("sendkey apostrophe")                # ae
    vm.mon("sendkey ret", 0.12)
    time.sleep(1.5)
    for k in ["e", "c", "h", "o", "spc"]:
        vm.mon("sendkey " + k)
    vm.mon("sendkey alt_r-2")                   # AltGr+2 = @
    vm.mon("sendkey shift-7")                   # Umschalt+7 = /
    vm.mon("sendkey ret", 0.12)
    time.sleep(1.5)
    vm.tippe("mouse")
    vm.mon("mouse_move 40 30", 0.5)
    vm.mon("mouse_button 1", 0.5)
    vm.tippe("mouse")
    vm.mon("mouse_button 0", 0.5)
    vm.tippe("mouse")
    out = vm.log()
    print(out[out.find("[PS2]") - 5:])
    vm.ende()
    print("=========== PRUEFUNG ===========")
    import re
    pruefe("PS/2: Controller eingerichtet, Maus bereit", "[PS2] Maus/TrackPoint bereit" in out)
    pruefe("PS/2: Z und Y getauscht (Schweizer Layout)", "echo z y" in out)
    pruefe("PS/2: Umlaute ue oe ae", "\u00fc\u00f6\u00e4" in out)
    pruefe("PS/2: AltGr-Kombination (@) und Umschalt (/)", "echo @/" in out)
    m = re.findall(r"Zeiger x=(\d+) y=(\d+) Tasten=(\d+)", out)
    bw, bh = bildschirm(out)
    pruefe("PS/2-Maus: Zeiger bewegt (Mitte + 40/30)", bool(m) and abs(int(m[0][0]) - (bw // 2 + 40)) < 6 and abs(int(m[0][1]) - (bh // 2 + 30)) < 6)
    pruefe("PS/2-Maus: Taste gedrueckt und losgelassen erkannt", any(t[2] == "1" for t in m) and m[-1][2] == "0")


def netz():
    www = "/tmp/www"
    os.makedirs(www, exist_ok=True)
    open(www + "/hallo.txt", "w").write("Hallo-vom-Server\n")
    gross = bytes(97 + i % 26 for i in range(20000))
    open(www + "/gross.txt", "wb").write(gross)
    summe = sum(gross)
    srv = subprocess.Popen([sys.executable, "-m", "http.server", "8080", "--bind", "0.0.0.0", "--directory", www],
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    subprocess.run(["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-keyout", "/tmp/k.pem", "-out", "/tmp/c.pem",
                    "-subj", "/CN=zynex-test", "-days", "2"], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    fpl = subprocess.run(["openssl", "x509", "-in", "/tmp/c.pem", "-noout", "-fingerprint", "-sha256"], capture_output=True, text=True).stdout
    fp = fpl.split("=", 1)[1].strip().replace(":", "").lower()
    tls = subprocess.Popen(["openssl", "s_server", "-accept", "8443", "-cert", "/tmp/c.pem", "-key", "/tmp/k.pem", "-tls1_3",
                            "-ciphersuites", "TLS_CHACHA20_POLY1305_SHA256", "-WWW"], cwd=www,
                           stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    pki = "/tmp/pki"
    os.makedirs(pki, exist_ok=True)
    def ssl(*a):
        subprocess.run(["openssl", *a], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, cwd=pki)
    open(pki + "/ca.ext", "w").write("basicConstraints=critical,CA:TRUE\nkeyUsage=critical,keyCertSign,cRLSign\n")
    open(pki + "/leaf.ext", "w").write("basicConstraints=CA:FALSE\nsubjectAltName=IP:10.0.2.2,DNS:zynex.test\n")
    # Kette A: Root RSA-2048 -> Zwischen-CA EC P-256 -> Server EC P-256
    ssl("req", "-x509", "-newkey", "rsa:2048", "-nodes", "-keyout", "rootA.key", "-out", "rootA.pem", "-subj", "/CN=Zynex Test Root A", "-days", "3", "-addext", "basicConstraints=critical,CA:TRUE", "-addext", "keyUsage=critical,keyCertSign")
    ssl("ecparam", "-name", "prime256v1", "-genkey", "-noout", "-out", "interA.key")
    ssl("req", "-new", "-key", "interA.key", "-out", "interA.csr", "-subj", "/CN=Zynex Test Inter A")
    ssl("x509", "-req", "-in", "interA.csr", "-CA", "rootA.pem", "-CAkey", "rootA.key", "-CAcreateserial", "-out", "interA.pem", "-days", "3", "-extfile", "ca.ext")
    ssl("ecparam", "-name", "prime256v1", "-genkey", "-noout", "-out", "leafA.key")
    ssl("req", "-new", "-key", "leafA.key", "-out", "leafA.csr", "-subj", "/CN=zynex.test")
    ssl("x509", "-req", "-in", "leafA.csr", "-CA", "interA.pem", "-CAkey", "interA.key", "-CAcreateserial", "-out", "leafA.pem", "-days", "3", "-extfile", "leaf.ext")
    # Kette B: Root EC P-384 (SHA-384) -> Server RSA-2048
    ssl("ecparam", "-name", "secp384r1", "-genkey", "-noout", "-out", "rootB.key")
    ssl("req", "-x509", "-new", "-key", "rootB.key", "-out", "rootB.pem", "-subj", "/CN=Zynex Test Root B", "-days", "3", "-sha384", "-addext", "basicConstraints=critical,CA:TRUE", "-addext", "keyUsage=critical,keyCertSign")
    ssl("req", "-new", "-newkey", "rsa:2048", "-nodes", "-keyout", "leafB.key", "-out", "leafB.csr", "-subj", "/CN=zynex.test")
    ssl("x509", "-req", "-in", "leafB.csr", "-CA", "rootB.pem", "-CAkey", "rootB.key", "-CAcreateserial", "-out", "leafB.pem", "-days", "3", "-sha384", "-extfile", "leaf.ext")
    ssl("x509", "-in", "rootA.pem", "-outform", "der", "-out", www + "/roota.der")
    ssl("x509", "-in", "rootB.pem", "-outform", "der", "-out", www + "/rootb.der")
    def srv_tls(port, cert, key, chain=None):
        a = ["openssl", "s_server", "-accept", str(port), "-cert", cert, "-key", key, "-tls1_3",
             "-ciphersuites", "TLS_CHACHA20_POLY1305_SHA256", "-WWW"]
        if chain:
            a += ["-cert_chain", chain]
        return subprocess.Popen(a, cwd=www, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    tlsG = subprocess.Popen(["openssl", "s_server", "-accept", "8446", "-cert", "/tmp/c.pem", "-key", "/tmp/k.pem", "-tls1_3",
                             "-ciphersuites", "TLS_AES_128_GCM_SHA256", "-WWW"], cwd=www,
                            stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    def srv12(port, cert, key, cipher, chain=None):
        a = ["openssl", "s_server", "-accept", str(port), "-cert", cert, "-key", key, "-tls1_2", "-cipher", cipher, "-WWW"]
        if chain:
            a += ["-cert_chain", chain]
        return subprocess.Popen(a, cwd=www, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    tls12 = [srv12(8447, "/tmp/c.pem", "/tmp/k.pem", "ECDHE-RSA-AES128-GCM-SHA256"),
             srv12(8448, "/tmp/c.pem", "/tmp/k.pem", "ECDHE-RSA-CHACHA20-POLY1305"),
             subprocess.Popen(["openssl", "s_server", "-accept", "8450", "-cert", "/tmp/c.pem", "-key", "/tmp/k.pem", "-tls1_2", "-groups", "P-256",
                               "-cipher", "ECDHE-RSA-AES128-GCM-SHA256", "-WWW"], cwd=www, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL),
             subprocess.Popen(["openssl", "s_server", "-accept", "8451", "-cert", "/tmp/c.pem", "-key", "/tmp/k.pem", "-tls1_3", "-groups", "P-256",
                               "-ciphersuites", "TLS_AES_128_GCM_SHA256", "-WWW"], cwd=www, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL),
             srv12(8449, pki + "/leafA.pem", pki + "/leafA.key", "ECDHE-ECDSA-AES128-GCM-SHA256", pki + "/interA.pem")]
    tlsA = srv_tls(8444, pki + "/leafA.pem", pki + "/leafA.key", pki + "/interA.pem")
    tlsB = srv_tls(8445, pki + "/leafB.pem", pki + "/leafB.key")
    time.sleep(1)
    for titel, extra in (("e1000e", []),
                         ("e1000", ["-nic", "none", "-netdev", "user,id=n0", "-device", "e1000,netdev=n0"]),
                         ("virtio", ["-nic", "none", "-netdev", "user,id=n0", "-device", "virtio-net-pci,netdev=n0,disable-legacy=on"])):
        print("\n===== Szenario netz (%s) =====" % titel)
        vm = starten(extra)
        cmds = ["net", "ping 10.0.2.2", "dns example.com"]
        if titel in ("e1000e", "virtio"):
            cmds += ["http 10.0.2.2:8080/hallo.txt", "http 10.0.2.2:8080/gross.txt", "wget 10.0.2.2:8080/hallo.txt seite", "syncat seite", "wget 10.0.2.2:8080/fehlt.txt nix"]
            if titel == "e1000e":
                cmds += ["http https://10.0.2.2:8443/hallo.txt", "http https://10.0.2.2:8443/gross.txt " + fp,
                         "http https://10.0.2.2:8443/hallo.txt " + "0" * 64, "wget https://10.0.2.2:8443/hallo.txt sicher " + fp, "syncat sicher",
                         "http https://10.0.2.2:8444/hallo.txt", "wget 10.0.2.2:8080/roota.der wa", "casadd wa", "cas",
                         "http https://10.0.2.2:8444/hallo.txt", "wget 10.0.2.2:8080/rootb.der wb", "casadd wb",
                         "http https://10.0.2.2:8445/hallo.txt", "http https://10.0.2.2:8443/hallo.txt",
                         "http https://10.0.2.2:8446/gross.txt " + fp,
                         "http https://10.0.2.2:8447/gross.txt " + fp, "http https://10.0.2.2:8448/gross.txt " + fp,
                         "http https://10.0.2.2:8449/hallo.txt",
                         "http https://10.0.2.2:8450/gross.txt " + fp, "http https://10.0.2.2:8451/gross.txt " + fp,
                         "http https://example.com/", "http https://www.wikipedia.org/", "http https://www.cloudflare.com/"]
        for b in cmds:
            vm.tippe(b, 6.0)
        out = vm.log()
        print(out[out.find("[NET]") - 5:])
        vm.ende()
        print("=========== PRUEFUNG ===========")
        pruefe("%s: Karte erkannt" % titel, "[NET] Karte bereit" in out)
        pruefe("%s: DHCP-Adresse erhalten" % titel, "[NET] DHCP: IP 10.0.2.15" in out)
        pruefe("%s: Befehl net zeigt Router" % titel, "Router 10.0.2.2" in out.split("zynex> net", 1)[-1])
        pruefe("%s: Ping beantwortet (ARP+ICMP)" % titel, "Antwort von 10.0.2.2" in out)
        if titel in ("e1000e", "virtio"):
            pruefe("%s: HTTP Status 200 (TCP)" % titel, "HTTP/1.0 200 OK" in out)
            pruefe("HTTP: kleine Datei richtig", "Hallo-vom-Server" in out)
            pruefe("wget: im SynFS gespeichert", "Gespeichert: seite (17 Bytes)" in out)
            pruefe("wget: gespeicherte Datei lesbar", "Hallo-vom-Server" in out.split("zynex> syncat seite", 1)[-1])
            pruefe("wget: 404 wird nicht gespeichert", "Status 404" in out)
            pruefe("HTTP: 20000 Bytes vollstaendig", "Inhalt: 20000 Bytes, Pruefsumme %d" % summe in out)
        if titel == "e1000e":
            pruefe("TLS: Verbindung + Zertifikat-Fingerabdruck angezeigt", fp in out)
            pruefe("TLS: selbstsigniertes Zertifikat ohne Fingerabdruck wird abgelehnt", out.count("Zertifikat NICHT vertrauenswuerdig") >= 3 and "Aussteller ist keiner vertrauten Root-CA bekannt" in out)
            pruefe("TLS: Kette Root-RSA > CA-EC-P256 > Server-EC-P256 (ECDSA) vertrauenswuerdig", out.count("Zertifikat vertrauenswuerdig (Kette bis zur Root-CA") >= 1)
            pruefe("TLS: Kette Root-EC-P384/SHA-384 > Server-RSA vertrauenswuerdig", out.count("Zertifikat vertrauenswuerdig (Kette bis zur Root-CA") >= 2)
            pruefe("TLS: AES-128-GCM-Server, 20000 Bytes", out.count("Pruefsumme %d" % summe) >= 3)
            pruefe("TLS 1.2: ECDHE-RSA-AES128-GCM, 20000 Bytes", out.count("Pruefsumme %d" % summe) >= 4)
            pruefe("TLS 1.2: ECDHE-RSA-ChaCha20-Poly1305, 20000 Bytes", out.count("Pruefsumme %d" % summe) >= 5)
            pruefe("TLS 1.2 mit P-256-Schluesselaustausch, 20000 Bytes", out.count("Pruefsumme %d" % summe) >= 6)
            pruefe("TLS 1.3 mit P-256-Schluesselaustausch, 20000 Bytes", out.count("Pruefsumme %d" % summe) >= 7)
            pruefe("TLS 1.2: ECDHE-ECDSA-GCM mit Zertifikatskette", out.count("Zertifikat vertrauenswuerdig (Kette bis zur Root-CA") >= 3)
            pruefe("casadd/cas: eigene Root-CA hinzugefuegt", "Root-CA hinzugefuegt" in out)
            pruefe("TLS: 20000 Bytes mit Fingerabdruck", "Fingerabdruck stimmt, Server ist authentisch" in out and out.count("Pruefsumme %d" % summe) >= 2)
            pruefe("TLS: falscher Fingerabdruck wird abgelehnt", "Fingerabdruck stimmt NICHT" in out)
            pruefe("TLS: wget ueber https gespeichert", "Gespeichert: sicher (17 Bytes)" in out)
        if titel == "e1000e":
            for site in ("example.com", "www.wikipedia.org", "www.cloudflare.com"):
                teil = out.split("zynex> http https://%s/" % site, 1)[-1].split("zynex>", 1)[0]
                print("INFO Internet %s: %s" % (site, "HTTP-Antwort erhalten" if "HTTP/1." in teil else "keine Antwort (Runner ohne Internet?) -> " + " | ".join(teil.strip().splitlines()[:4])))
        print("INFO %s: DNS %s" % (titel, "ok" if " = " in out.split("zynex> dns", 1)[-1] else "keine Antwort (Runner ohne Namensdienst moeglich)"))
    srv.kill()
    tls.kill()
    tlsA.kill()
    tlsG.kill()
    for t in tls12:
        t.kill()
    tlsB.kill()


def usb():
    print("\n===== Szenario usb (xHCI, Tastatur, Tablet) =====")
    extra = ["-device", "qemu-xhci,id=xhci", "-device", "usb-kbd,bus=xhci.0", "-device", "usb-tablet,bus=xhci.0"]
    vm = starten(extra)
    vm.tippe("tastatur us")                      # QEMU sendet US-Tasten
    vm.tippe("usb")
    vm.abs_zeiger(8192, 8192)                    # Tablet: absolute Position
    vm.abs_zeiger(16384, 16384)                  # Mitte des Bildschirms
    vm.mon("mouse_button 1", 0.3)
    vm.tippe("mouse")
    vm.mon("mouse_button 0", 0.3)
    vm.tippe("mouse")
    vm.tippe("echo tastatur-per-usb")
    time.sleep(1)
    out = vm.log()
    hell = vm.hell()
    print(out[out.find("[PCI]") - 5:])
    vm.ende()
    print("=========== PRUEFUNG ===========")
    pruefe("xHCI-Controller gestartet", "xHCI bereit" in out)
    pruefe("USB-Tastatur erkannt", "[USB] Tastatur an Port" in out)
    pruefe("USB-Tablet/Touch erkannt", "[USB] Zeiger/Touch an Port" in out)
    pruefe("PS/2 abgeschaltet", "PS/2-Tastatur abgeschaltet" in out)
    pruefe("Eingabe ueber USB-Tastatur (Befehl usb)", "Tastatur Speed" in out or "  Tastatur" in out.split("zynex> usb", 1)[-1])
    pruefe("Eingabe ueber USB-Tastatur (echo)", "tastatur-per-usb" in out.split("zynex> echo", 1)[-1])
    pruefe("Zeiger bewegt (Tablet)", "Zeiger x=" in out)
    import re
    m = re.findall(r"Zeiger x=(\d+) y=(\d+) Tasten=(\d+)", out)
    bw, bh = bildschirm(out)
    pruefe("Zeigerposition plausibel (Mitte)", bool(m) and abs(int(m[0][0]) - bw // 2) < bw // 5 and abs(int(m[0][1]) - bh // 2) < bh // 5)
    pruefe("Taste gedrueckt erkannt", any(t[2] == "1" for t in m))
    pruefe("Taste losgelassen erkannt", bool(m) and m[-1][2] == "0")


def grafik():
    global ISO
    print("\n===== Szenario grafik (grafischer Installer: Tastatur + Maus, danach Start von der Platte) =====")
    ISO = os.environ.get("ZYNEX_GUI_ISO", ISO)
    img = "/tmp/zynex-gui.img"
    subprocess.run(["qemu-img", "create", "-f", "raw", img, "256M"], check=True, stdout=subprocess.DEVNULL)
    sata = ["-drive", "file=%s,if=none,id=satadisk,format=raw" % img, "-device", "ide-hd,drive=satadisk,bus=ide.0"]
    extra = sata + ["-device", "qemu-xhci,id=xhci", "-device", "usb-kbd,bus=xhci.0", "-device", "usb-tablet,bus=xhci.0"]
    vm = VM(extra)                                # Standardstart der Release-ISO = grafischer Installer
    seite1 = vm.warte("[GUI] Seite 1: Sprache", 150)
    bs = re.search(r"\[GUI\] Bildschirm (\d+)x(\d+)", vm.log())
    W, H = (int(bs.group(1)), int(bs.group(2))) if bs else (1024, 768)

    def seiten():
        return len(re.findall(r"\[GUI\] Seite", vm.log()))

    def klick_auf(prefix):
        m = re.findall(r"\[GUI\] %s x=(\d+) y=(\d+)" % prefix, vm.log())
        if not m:
            return False
        x, y = int(m[-1][0]), int(m[-1][1])
        vm.abs_zeiger(x * 32767 // W, y * 32767 // H)
        try:
            vm.mon("mouse_button 1", 0.4)
            vm.mon("mouse_button 0", 1.5)
        except (BrokenPipeError, ConnectionResetError):
            pass                                   # Neustart-Knopf: QEMU beendet sich sofort
        return True

    def knopf_klick(name):
        return klick_auf("Knopf " + name)

    def taste(*t):
        for k in t:
            vm.mon("sendkey " + k, 0.3)
        time.sleep(1.0)

    def schreibe(text):
        for c in text:
            vm.mon("sendkey " + TASTEN.get(c, c), 0.2)
        time.sleep(0.8)

    ok_sp = klick_auf("Sprache en")               # Maus: Sprache English
    ok_w1 = knopf_klick("Weiter")                 # Maus: Weiter -> Tastatur
    s_tas = vm.warte("[GUI] Seite 2: Tastatur", 15)
    ok_er = knopf_klick("Erkennen")               # Maus: Tastenerkennung starten
    s_erk = vm.warte("[GUI] Seite 9: Erkennung", 15)
    taste("y")                                    # Frage 1: Taste mit Z -> liegt auf der Y-Position = QWERTZ
    taste("minus")                                # Frage 2: Taste mit ß -> Position von '-' = Deutschland
    erkannt = vm.warte("[GUI] Erkannt de", 15)
    ok_ue = knopf_klick("Uebernehmen")
    ok_w = knopf_klick("Weiter")                  # Maus: Weiter -> Platte
    s2 = vm.warte("[GUI] Seite 3: Zielplatte", 15)
    m = re.findall(r"\[GUI\] Platte (\d+) sata0", vm.log())
    taste(m[-1] if m else "1")                    # Platte waehlen (Zifferntaste)
    knopf_klick("Weiter")                         # Maus: Weiter -> Aufteilung
    s_auf = vm.warte("[GUI] Seite 10: Aufteilung", 15)
    knopf_klick("Weiter")                         # Maus: Weiter -> Konto (ganze Platte)
    s3 = vm.warte("[GUI] Seite 4: Benutzerkonto", 15)
    schreibe("michel"); taste("tab")
    schreibe("geheim12"); taste("tab")
    schreibe("geheim13"); taste("ret")            # Wiederholung falsch -> Fehlermeldung
    s_fehler = "[GUI] Meldung Die Passwoerter sind verschieden" in vm.log()
    taste("backspace"); schreibe("2"); taste("ret")
    s4 = vm.warte("[GUI] Seite 5: Netzwerk", 15)
    taste("ret")
    s5 = vm.warte("[GUI] Seite 6: Zusammenfassung", 15)
    ok_i = knopf_klick("Installieren")            # Maus: Installieren
    fertig = vm.warte("[GUI] Installation fertig", 300)
    s7 = vm.warte("[GUI] Seite 8: Fertig", 15)
    ok_n = knopf_klick("Neustart")                # Maus: Neustart
    beendet = False
    for _ in range(40):
        if vm.q.poll() is not None:
            beendet = True
            break
        time.sleep(0.5)
    out1 = vm.log()
    print(out1[out1.find("[GUI] Bildschirm"):][-2500:])
    vm.ende()

    vm = starten(sata, cd=False, us=False)        # Neustart nur von der Platte
    vm.warte("[DESKTOP] Anmeldebildschirm", 20)
    for b in ["michel", "geheim12", "wartung"]:
        vm.tippe(b, 7.0 if b == "geheim12" else 2.0)
    for b in ["tastatur us", "whoami", "block", "synrun hallo.synbin", "syncat sys.config"]:
        vm.tippe(b, 2.0)
    out2 = vm.log()
    print(out2[-1500:])
    vm.ende()
    print("=========== PRUEFUNG ===========")
    pruefe("GUI: Sprachseite erscheint (installer=gui im Standardeintrag)", seite1)
    pruefe("GUI: Sprache per Mausklick, Weiter -> Tastaturseite", ok_sp and ok_w1 and s_tas)
    pruefe("GUI: Tastenerkennung (Z auf Y-Position, ß auf Minus-Position) erkennt Deutschland", ok_er and s_erk and erkannt)
    pruefe("GUI: Erkannte Belegung uebernehmen, Weiter per Maus -> Plattenseite", ok_ue and ok_w and s2)
    pruefe("GUI: Platte per Zifferntaste, Weiter per Maus -> Aufteilung -> Kontoseite", s_auf and s3)
    pruefe("GUI: falsche Passwort-Wiederholung wird abgelehnt", s_fehler)
    pruefe("GUI: Konto angenommen -> Netzwerk -> Zusammenfassung", s4 and s5)
    pruefe("GUI: Installieren per Mausklick, Installation fertig", ok_i and fertig)
    pruefe("GUI: Fertig-Seite, Neustart per Mausklick beendet die Maschine", s7 and ok_n and beendet)
    pruefe("GUI: installiertes System startet von der Platte", "[SYSTEM] Installiertes System von sata0p2 gestartet" in out2)
    pruefe("GUI: Anmeldung mit dem im Assistenten angelegten Konto", "[DESKTOP] Willkommen, michel (admin)" in out2)
    pruefe("GUI: installiertes System fuehrt Programme aus", "6 mal 7 = 42" in out2)
    pruefe("GUI: gewaehlte Tastatur und Sprache im installierten System gespeichert", "layout=de" in out2 and "lang=en" in out2)


def usbnet():
    print("\n===== Szenario usbnet (USB-Netzwerk: CDC-ECM, RNDIS, Hot-Plug) =====")
    www = "/tmp/www2"
    os.makedirs(www, exist_ok=True)
    open(www + "/hallo.txt", "w").write("Hallo-vom-Server\n")
    srv = subprocess.Popen([sys.executable, "-m", "http.server", "8081", "--bind", "0.0.0.0", "--directory", www],
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(1)
    extra = ["-nic", "none", "-netdev", "user,id=un0", "-device", "qemu-xhci,id=xhci", "-device", "usb-kbd,bus=xhci.0",
             "-device", "usb-net,netdev=un0,bus=xhci.0,id=unet"]
    vm = starten(extra)
    ok_boot = "[USB] Netzwerk (CDC-ECM)" in vm.log()
    vm.tippe("tastatur us")                      # QEMU sendet US-Tasten
    for b in ["usb", "net", "ping 10.0.2.2", "http 10.0.2.2:8081/hallo.txt"]:
        vm.tippe(b, 6.0)
    ecm_log = vm.log()
    vm.tippe("usbnet rndis", 3.0)
    rndis = vm.warte("[USB] Netzwerk (RNDIS)", 40)
    import re as _r
    def dhcp_n():
        return len(_r.findall(r"\[NET\] DHCP: IP 10\.0\.2\.\d+", vm.log()))
    t0 = time.time()
    while dhcp_n() < 2 and time.time() - t0 < 40:
        time.sleep(0.5)
    dhcp2 = dhcp_n() >= 2
    time.sleep(2)
    for b in ["ping 10.0.2.2", "http 10.0.2.2:8081/hallo.txt"]:
        vm.tippe(b, 6.0)
    rndis_log = vm.log().split("[USB] Netzwerk (RNDIS)", 1)[-1]
    vm.mon("device_del unet", 1.0)
    weg = vm.warte("[NET] USB-Netzwerk getrennt", 40)
    vm.tippe("net", 3.0)
    vm.mon("device_add usb-net,netdev=un0,bus=xhci.0,id=unet2", 1.0)
    da = vm.warte("[NET] USB-Netzwerk bereit", 40)
    t0 = time.time()
    while dhcp_n() < 3 and time.time() - t0 < 40:
        time.sleep(0.5)
    dhcp3 = dhcp_n() >= 3
    time.sleep(2)
    for b in ["ping 10.0.2.2", "http 10.0.2.2:8081/hallo.txt"]:
        vm.tippe(b, 6.0)
    out = vm.log()
    hot_log = out.split("[NET] USB-Netzwerk bereit", 1)[-1]
    print(out[out.find("[USB]") - 5:])
    vm.ende()
    srv.kill()
    print("=========== PRUEFUNG ===========")
    pruefe("USB-Netzwerk: CDC-ECM-Geraet beim Start erkannt", ok_boot)
    pruefe("USB-Netzwerk (ECM): DHCP-Adresse erhalten", "[NET] DHCP: IP 10.0.2.15" in ecm_log)
    pruefe("USB-Netzwerk (ECM): Ping beantwortet", "Antwort von 10.0.2.2" in ecm_log)
    pruefe("USB-Netzwerk (ECM): HTTP-Download", "Hallo-vom-Server" in ecm_log.split("zynex> http", 1)[-1])
    pruefe("USB-Netzwerk (RNDIS): Geraet neu aufgenommen", rndis)
    pruefe("USB-Netzwerk (RNDIS): DHCP-Adresse erhalten", dhcp2)
    pruefe("USB-Netzwerk (RNDIS): Ping beantwortet", "Antwort von 10.0.2.2" in rndis_log)
    pruefe("USB-Netzwerk (RNDIS): HTTP-Download", "Hallo-vom-Server" in rndis_log.split("zynex> http", 1)[-1])
    pruefe("Hot-Plug: Entfernen wird erkannt", weg and "entfernt" in out)
    pruefe("Hot-Plug: Einstecken wird erkannt, DHCP laeuft", da and dhcp3)
    pruefe("Hot-Plug: Ping nach dem Einstecken", "Antwort von 10.0.2.2" in hot_log)
    pruefe("Hot-Plug: HTTP-Download nach dem Einstecken", "Hallo-vom-Server" in hot_log.split("zynex> http", 1)[-1])


def boot():
    print("\n===== Szenario boot (eigener Lader, Signatur) =====")
    vm = starten()
    vm.tippe("sicherheit", 1.5); vm.tippe("pqtest", 3.0); vm.tippe("mem", 1.5); vm.tippe("firewall status", 1.5); vm.tippe("krypto status", 1.5); vm.tippe("db setze netz.dns 1.1.1.1", 1.5); vm.tippe("db suche dns", 1.5); vm.tippe("db status", 1.5)
    out = vm.log()
    print(out)
    vm.ende()
    pruefe("Lader: meldet sich", "[LOADER] Zynex OS Lader (UEFI)" in out)
    pruefe("Lader: LMS-Signatur geprueft und gueltig", "[LOADER] Signatur gueltig" in out)
    pruefe("Lader: Kernel geladen, Einstieg gefunden", "[LOADER] Kernel geladen, Einstieg 0x" in out)
    pruefe("Kernel: Start ueber den Zynex-Lader", "Start ueber den Zynex-UEFI-Lader" in out)
    pruefe("Kernel: [SECURE] Signatur beglaubigt", "[SECURE] Kernel durch hash-basierte Signatur" in out)
    pruefe("Kernel: Sicherheitsbefehl", "Kernel und Kommandozeile durch LMS-Signatur" in out)
    pruefe("Kernel: pqtest bestanden", "pq: Selbsttest bestanden" in out)
    pruefe("Kernel: Firewall aktiv", "Firewall: normal" in out)
    pruefe("Kernel: Verschluesselung meldet Zustand", "Verschluesselung: aus" in out)
    pruefe("Kernel: ZynDB speichert und findet", "netz.dns = 1.1.1.1" in out and "ZynDB: 1 Fakten" in out)


def ki():
    """Sprachmodell im Kernel: Der Lader reicht das Modell vom Stick, der Befehl 'ki' beantwortet eine Frage."""
    print("\n===== Szenario ki (Sprachmodell im Kernel) =====")
    vm = starten()
    vm.tippe("ki wieviel ist vierzig plus zwei", 1.0)
    fertig = vm.warte("[KI] Antwort:", 1500)
    time.sleep(1)
    out = vm.log()
    print(out[-3000:])
    vm.ende()
    pruefe("KI: Lader hat das Sprachmodell gelesen", "[LOADER] Lese Sprachmodell" in out)
    pruefe("KI: Modell geladen", "[KI] Sprachmodell bereit" in out)
    pruefe("KI: Antwort kommt", fertig)
    m = re.search(r"\[KI\] Antwort: (.*)", out)
    antwort = m.group(1).strip() if m else ""
    pruefe("KI: Antwort ist nicht leer", len(antwort) > 3)
    # Vergleich mit dem Entwicklungsrechner-Programm (gleiche Rechnung, gleiches Modell): muss Wort fuer Wort gleich sein
    host, modell = os.environ.get("ZYNEX_LLM_HOST"), os.environ.get("ZYNEX_MODELL")
    if host and modell:
        r = subprocess.run([host, modell, "chat", "wieviel ist vierzig plus zwei", "200"], capture_output=True, text=True)
        h = re.search(r"Antwort: (.*)", r.stdout)
        erwartet = h.group(1).strip() if h else "?"
        print("Kernel:", antwort, "| Referenz:", erwartet)
        pruefe("KI: Kernel-Antwort identisch mit der Referenz", antwort == erwartet)


def manip():
    """Manipulierter Kernel (ein Byte geaendert, Signatur unveraendert): der Lader muss den Start verweigern."""
    print("\n===== Szenario manip (Manipulation wird erkannt) =====")
    vm = VM()
    vm.warte("Start abgebrochen", 90)
    time.sleep(1)
    out = vm.log()
    print(out)
    vm.ende()
    pruefe("Manipulation: Lader meldet ungueltige Signatur", "Signatur ungueltig" in out)
    pruefe("Manipulation: Start abgebrochen", "[LOADER] Start abgebrochen" in out)
    pruefe("Manipulation: Kernel startet NICHT", "All systems online" not in out and "[SECURE]" not in out)


for s in SZEN:
    {"kiinstall": kiinstall, "ki": ki, "boot": boot, "manip": manip, "grafik": grafik, "basis": basis, "platte": platte, "sata": sata, "usb": usb, "layout": layout, "netz": netz, "installer": installieren, "usbnet": usbnet, "ps2": ps2}[s]()

sys.exit(0 if all(ergebnis) else 1)
