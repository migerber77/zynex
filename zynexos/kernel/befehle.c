/*
 * Zynex OS - moderne Befehlssprache. Statt verstreuter Kuerzel (synls, syncat, ps, pci ...) gibt es wenige Bereiche mit klaren Aktionen:
 *   system | datei | netz | geraete | nutzer | ki | quanten | strom | installieren | hilfe
 * Jeder Bereich zeigt ohne Aktion das Wichtigste, Tab vervollstaendigt, bei Tippfehlern kommt "Meintest du ...?".
 */
#include "befehle.h"
#include <stdint.h>

extern void kprint(const char* str);

typedef struct { const char* bereich; const char* aktion; const char* tmpl; const char* info; } zeile_t;
/* bereich/aktion: Alternativen mit '|' (erste = Anzeigename); aktion "" = ohne Aktion, "*" = beliebiger Text. '%' im Muster = Rest der Eingabe. */
static const zeile_t tab[] = {
    { "system|sys",        "",                     "about;mem;uptime", "Ueberblick" },
    { "system|sys",        "speicher|memory",      "mem;heap",         "Arbeitsspeicher" },
    { "system|sys",        "laufzeit|uptime",      "uptime",           "wie lange das System laeuft" },
    { "system|sys",        "prozesse|tasks",       "ps",               "laufende Aufgaben" },
    { "system|sys",        "module|modules",       "mods",             "geladene Module" },
    { "system|sys",        "rechte|caps",          "caps",             "Berechtigungen" },
    { "system|sys",        "sicherheit|security",  "sicherheit",       "Startkette und Signatur" },
    { "system|sys",        "ki|ai",                "ai",               "KI-Speicher und Zaehler" },
    { "datei|files|dateien", "",                   "synls",            "alle Dateien" },
    { "datei|files|dateien", "liste|list",         "synls",            "alle Dateien" },
    { "datei|files|dateien", "lesen|read|zeige",   "syncat %",         "Datei anzeigen" },
    { "datei|files|dateien", "schreiben|write",    "synwrite %",       "Datei schreiben: datei schreiben <name> <text>" },
    { "datei|files|dateien", "neu|new",            "syncreate %",      "Datei anlegen" },
    { "datei|files|dateien", "loeschen|delete",    "syndel %",         "Datei loeschen" },
    { "datei|files|dateien", "suche|find",         "synfind %",        "Dateien nach Stichwort (Tag) suchen" },
    { "datei|files|dateien", "markieren|tag",      "syntag %",         "Stichwort an Datei: datei markieren <name> <tag>" },
    { "datei|files|dateien", "zeit|history",       "zeit",             "zuletzt geaenderte Dateien" },
    { "datei|files|dateien", "sichern|sync",       "synsync",          "alles auf die Platte schreiben" },
    { "datei|files|dateien", "info",               "syninfo",          "Zustand des Dateisystems" },
    { "netz|net|network",  "",                     "net",              "Netzwerkzustand" },
    { "netz|net|network",  "status",               "net",              "Netzwerkzustand" },
    { "netz|net|network",  "dhcp|verbinden",       "dhcp",             "Adresse automatisch holen" },
    { "netz|net|network",  "ping",                 "ping %",           "Erreichbarkeit pruefen" },
    { "netz|net|network",  "dns|aufloesen",        "dns %",            "Namen in Adresse umwandeln" },
    { "netz|net|network",  "web|http",             "http %",           "Webseite abrufen" },
    { "netz|net|network",  "laden|get",            "wget %",           "Datei aus dem Netz laden: netz laden <adresse> <name>" },
    { "netz|net|network",  "wlan|wifi",            "wifi",             "WLAN-Treiber und Chip" },
    { "geraete|devices|hw", "",                    "pci;usb;block",    "alle Geraete" },
    { "geraete|devices|hw", "pci",                 "pci",              "PCI-Geraete" },
    { "geraete|devices|hw", "usb",                 "usb",              "USB-Geraete" },
    { "geraete|devices|hw", "platten|disks",       "block",            "Speichergeraete" },
    { "geraete|devices|hw", "maus|mouse",          "mouse",            "Mauszustand" },
    { "geraete|devices|hw", "tastatur|keyboard",   "tastatur %",       "Tastaturlayout" },
    { "nutzer|user|users", "",                     "whoami",           "wer bin ich" },
    { "nutzer|user|users", "ich|me",               "whoami",           "wer bin ich" },
    { "nutzer|user|users", "liste|list",           "benutzer liste",   "alle Konten" },
    { "nutzer|user|users", "neu|new",              "benutzer neu %",   "Konto anlegen: nutzer neu <name> <admin|benutzer|gast>" },
    { "nutzer|user|users", "loeschen|delete",      "benutzer loeschen %", "Konto loeschen" },
    { "nutzer|user|users", "passwort|password",    "benutzer passwort %", "Passwort aendern" },
    { "nutzer|user|users", "abmelden|logout",      "abmelden",         "abmelden" },
    { "firewall|fw",       "",                     "firewall status",  "Zustand der Firewall" },
    { "firewall|fw",       "status",               "firewall status",  "Zustand, Quarantaene, verworfene Pakete" },
    { "firewall|fw",       "log",                  "firewall log",     "letzte Ereignisse (Portscans, Faelschungen ...)" },
    { "firewall|fw",       "regeln|rules",         "firewall regeln",  "eigene Regeln anzeigen" },
    { "firewall|fw",       "an|on",                "firewall an",      "einschalten (normal)" },
    { "firewall|fw",       "aus|off",              "firewall aus",     "ausschalten" },
    { "firewall|fw",       "streng|strict",        "firewall streng",  "ausgehend nur an erlaubte Ziele" },
    { "firewall|fw",       "erlaube|allow",        "firewall erlaube %",   "Regel: firewall erlaube <ip> [ein|aus] [tcp|udp] [port]" },
    { "firewall|fw",       "blockiere|block",      "firewall blockiere %", "Regel: firewall blockiere <ip> [ein|aus] [tcp|udp] [port]" },
    { "firewall|fw",       "freigeben|release",    "firewall freigeben %", "Quarantaene aufheben" },
    { "firewall|fw",       "loeschen|clear",       "firewall loeschen",    "alle Regeln loeschen" },
    { "krypto|verschluesselung|encryption", "",    "krypto status",    "Zustand der Festplattenverschluesselung" },
    { "krypto|verschluesselung|encryption", "status", "krypto status", "Zustand der Festplattenverschluesselung" },
    { "krypto|verschluesselung|encryption", "aktivieren|enable", "krypto aktivieren", "alle Daten verschluesseln (fragt nach Passwort)" },
    { "krypto|verschluesselung|encryption", "entsperren|unlock", "krypto entsperren", "verschluesselte Platte mit Passwort oeffnen" },
    { "krypto|verschluesselung|encryption", "passwort|password", "krypto passwort", "Passwort aendern" },
    { "krypto|verschluesselung|encryption", "fortsetzen|resume", "krypto fortsetzen", "unterbrochene Verschluesselung weiterfuehren" },
    { "chat|chats",        "",                     "chat liste",       "alle Chats" },
    { "chat|chats",        "liste|list",           "chat liste",       "alle Chats, gepinnte zuerst" },
    { "chat|chats",        "neu|new",              "chat neu %",       "neuer Chat" },
    { "chat|chats",        "pin|anpinnen",         "chat pin",         "aktiven Chat anpinnen/loesen" },
    { "chat|chats",        "projekt|project",      "chat projekt %",   "aktiven Chat einem Projekt zuordnen" },
    { "chat|chats",        "loeschen|delete",      "chat loeschen",    "aktiven Chat loeschen" },
    { "db|daten", "",                  "db status",        "Zustand der Gedaechtnis-Datenbank ZynDB" },
    { "db|daten", "setze|set",         "db setze %",       "Fakt speichern: db setze <name> <wert>" },
    { "db|daten", "hole|get",          "db hole %",        "Fakt lesen" },
    { "db|daten", "liste|list",        "db liste %",       "Fakten auflisten (optional Namensanfang)" },
    { "db|daten", "suche|frage|find",  "db suche %",       "nach Bedeutung suchen, tippfehlertolerant" },
    { "db|daten", "verlauf|history",   "db verlauf %",     "alle Versionen eines Fakts" },
    { "db|daten", "zurueck|undo",      "db zurueck %",     "auf die vorige Version zurueck" },
    { "db|daten", "loesche|del",       "db loesche %",     "Fakt loeschen (im Verlauf wiederholbar)" },
    { "db|daten", "pruefen|check",     "db pruefen",       "Manipulation pruefen" },
    { "gedaechtnis|synapsen|erinnerung|memory|brain", "",   "gedaechtnis status",   "Zynies Synapsen-Gedaechtnis: Zustand" },
    { "gedaechtnis|synapsen|erinnerung|memory|brain", "liste|list",       "gedaechtnis liste",    "alle Erinnerungen mit Staerke" },
    { "gedaechtnis|synapsen|erinnerung|memory|brain", "merke|merk|remember", "gedaechtnis merke %", "etwas Wichtiges merken (verblasst nie)" },
    { "gedaechtnis|synapsen|erinnerung|memory|brain", "erinnere|recall",  "gedaechtnis erinnere %", "Abruf testen: welche Neuronen feuern, was kommt zurueck" },
    { "gedaechtnis|synapsen|erinnerung|memory|brain", "vergiss|forget",   "gedaechtnis vergiss %", "Erinnerungen zu einem Thema vergessen" },
    { "gedaechtnis|synapsen|erinnerung|memory|brain", "netz|net",         "gedaechtnis netz",     "haeufigste Neuronen, staerkste Synapsen" },
    { "gedaechtnis|synapsen|erinnerung|memory|brain", "schlafen|sleep",   "gedaechtnis schlafen", "Konsolidierung: Wichtiges wiederholen, Schwaches vergessen" },
    { "gedaechtnis|synapsen|erinnerung|memory|brain", "loeschen|clear",   "gedaechtnis loeschen %", "alles vergessen (gedaechtnis loeschen ja)" },
    { "ki|frag",        "extern|external",      "kiextern %",       "externes Modell: ki extern einrichten claude | modell | intern | test" },
    { "ki|frag",        "modelle|models",       "kimodell liste",   "installierte Modelle" },
    { "ki|frag",        "modell|model",         "kimodell %",       "ki modell liste | waehle <platte> | loesche <platte> ja" },
    { "ki|frag",        "*",                    "ki %",             "frag Zynie irgendetwas: ki was ist ein Qubit?" },
    { "quanten|quantum",   "*",                    "qubit %",          "Quantenkern: quanten h 0 | messen | zustand" },
    { "strom|power",       "neustart|restart",     "reboot",           "neu starten" },
    { "strom|power",       "aus|off",              "halt",             "ausschalten" },
    { "installieren|install", "",                  "installer",        "grafischer Installations-Assistent" },
    { "installieren|install", "*",                 "install %",        "installieren <platte> <name>" },
};
#define NTAB ((int)(sizeof(tab) / sizeof(tab[0])))

static int lc(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }
static int slen(const char* s) { int n = 0; while (s[n]) n++; return n; }
static void kopie(char* d, const char* s, int max) { int i = 0; while (s[i] && i < max - 1) { d[i] = s[i]; i++; } d[i] = 0; }
static int wort_gleich(const char* a, int al, const char* b) { int i = 0; for (; i < al && b[i]; i++) if (lc((unsigned char)a[i]) != lc((unsigned char)b[i])) return 0; return i == al && !b[i]; }
/* tok (Laenge n) gleich einer Alternative in "a|b|c"? */
static int alt_gleich(const char* alts, const char* tok, int n) {
    while (*alts) { int l = 0; while (alts[l] && alts[l] != '|') l++; char b[24]; int k = l < 23 ? l : 23; for (int i = 0; i < k; i++) b[i] = alts[i]; b[k] = 0; if (wort_gleich(tok, n, b)) return 1; alts += l; if (*alts == '|') alts++; }
    return 0;
}
static void erstes(const char* alts, char* o, int max) { int l = 0; while (alts[l] && alts[l] != '|' && l < max - 1) { o[l] = alts[l]; l++; } o[l] = 0; }

int befehl_uebersetze(const char* ein, char* aus, int max) {
    while (*ein == ' ') ein++;
    const char* t1 = ein; int n1 = 0; while (t1[n1] && t1[n1] != ' ') n1++;
    if (!n1) return 0;
    const char* r1 = t1 + n1; while (*r1 == ' ') r1++;
    const char* t2 = r1; int n2 = 0; while (t2[n2] && t2[n2] != ' ') n2++;
    const char* r2 = t2 + n2; while (*r2 == ' ') r2++;
    int bereich = 0;
    for (int i = 0; i < NTAB; i++) if (alt_gleich(tab[i].bereich, t1, n1)) { bereich = 1; break; }
    if (!bereich) return 0;
    for (int i = 0; i < NTAB; i++) {
        if (!alt_gleich(tab[i].bereich, t1, n1)) continue;
        const char* a = tab[i].aktion; int treffer = 0; const char* rest = r2;
        if (a[0] == '*' && !a[1]) { treffer = 1; rest = r1; }
        else if (!a[0]) treffer = (n2 == 0);
        else if (n2 && alt_gleich(a, t2, n2)) treffer = 1;
        if (!treffer) continue;
        int k = 0; const char* m = tab[i].tmpl;
        for (; *m && k < max - 1; m++) {
            if (*m == '%') { while (*rest && k < max - 1) aus[k++] = *rest++; while (k > 0 && aus[k - 1] == ' ') k--; }
            else aus[k++] = *m;
        }
        while (k > 0 && aus[k - 1] == ' ') k--;
        aus[k] = 0; return 1;
    }
    return 0;                                                       /* Bereich bekannt, Aktion nicht: Hilfe des Bereichs zeigt der Aufrufer */
}

static int bekannter_bereich(const char* t, int n) { for (int i = 0; i < NTAB; i++) if (alt_gleich(tab[i].bereich, t, n)) return 1; return 0; }

void befehl_hilfe(const char* thema) {
    while (*thema == ' ') thema++;
    int n = 0; while (thema[n] && thema[n] != ' ') n++;
    if (n && bekannter_bereich(thema, n)) {
        char b[24]; for (int i = 0; i < NTAB; i++) if (alt_gleich(tab[i].bereich, thema, n)) { erstes(tab[i].bereich, b, 24); break; }
        kprint("\n  "); kprint(b); kprint("\n");
        for (int i = 0; i < NTAB; i++) {
            if (!alt_gleich(tab[i].bereich, thema, n)) continue;
            char ak[24]; erstes(tab[i].aktion, ak, 24); const char* a = tab[i].aktion;
            kprint("    "); kprint(b); if (ak[0] && !(a[0] == '*')) { kprint(" "); kprint(ak); } else if (a[0] == '*') kprint(" <...>");
            int l = slen(b) + 4 + (ak[0] && a[0] != '*' ? slen(ak) + 1 : (a[0] == '*' ? 6 : 0));
            for (; l < 30; l++) kprint(" ");
            kprint(tab[i].info); kprint("\n");
        }
        kprint("\n"); return;
    }
    kprint("\nZynex OS - Befehle  (Deutsch oder Englisch, Tab vervollstaendigt)\n\n");
    static const char* const ber[][2] = {
        { "system",       "Zustand, Speicher, Laufzeit, Prozesse, Rechte, Sicherheit" }, { "datei",        "anzeigen, schreiben, suchen, markieren, sichern" },
        { "netz",         "Status, verbinden, ping, Namen, Web, WLAN" },                { "geraete",      "PCI, USB, Platten, Maus, Tastatur" },
        { "nutzer",       "wer bin ich, Konten, Passwort, abmelden" },                  { "ki <frage>",   "frag Zynie irgendetwas" },
        { "quanten",      "Quantenkern im Kernel ausprobieren" },                       { "strom",        "neustart oder aus" },
        { "installieren", "Zynex auf eine Platte bringen" },                            { "firewall",     "Schutz vor Angriffen, Regeln, Quarantaene" },
        { "krypto",       "Platte quantensicher verschluesseln, entsperren" },
        { "db",           "Datenbank ZynDB: Konfiguration, Suche, Zeitreise" },
        { "gedaechtnis",  "Synapsen-Gedaechtnis: merken, erinnern, vergessen" },
    };
    for (int i = 0; i < 13; i++) { kprint("  "); kprint(ber[i][0]); int l = 2 + slen(ber[i][0]); for (; l < 20; l++) kprint(" "); kprint(ber[i][1]); kprint("\n"); }
    kprint("\n  Mehr zu einem Bereich: hilfe datei   |   ohne Aktion zeigt jeder Bereich das Wichtigste\n");
    kprint("  Ganze Saetze gehen an Zynie, zum Beispiel: wie viel Speicher ist frei?\n");
    kprint("  Die alten Kurzbefehle (synls, whoami, ps ...) funktionieren weiter: hilfe alt\n\n");
}

static int dist(const char* a, int al, const char* b, int bl) {
    int d[24][24]; if (al > 22) al = 22; if (bl > 22) bl = 22;
    for (int i = 0; i <= al; i++) d[i][0] = i; for (int j = 0; j <= bl; j++) d[0][j] = j;
    for (int i = 1; i <= al; i++) for (int j = 1; j <= bl; j++) {
        int c = lc((unsigned char)a[i - 1]) == lc((unsigned char)b[j - 1]) ? 0 : 1, m = d[i - 1][j] + 1;
        if (d[i][j - 1] + 1 < m) m = d[i][j - 1] + 1; if (d[i - 1][j - 1] + c < m) m = d[i - 1][j - 1] + c; d[i][j] = m;
    }
    return d[al][bl];
}
const char* befehl_vorschlag(const char* ein, char* out, int max) {
    while (*ein == ' ') ein++;
    int n = 0; while (ein[n] && ein[n] != ' ') n++;
    if (n < 2) return 0;
    int best = 3, bi = -1; char name[24];
    for (int i = 0; i < NTAB; i++) {
        const char* alts = tab[i].bereich;
        while (*alts) {
            int l = 0; while (alts[l] && alts[l] != '|') l++; char b[24]; int k = l < 23 ? l : 23; for (int q = 0; q < k; q++) b[q] = alts[q]; b[k] = 0;
            int dd = dist(ein, n, b, k); if (dd < best) { best = dd; bi = i; kopie(name, b, 24); }
            alts += l; if (*alts == '|') alts++;
        }
    }
    if (bi < 0) return 0;
    kopie(out, name, max); return out;
}

int befehl_tab(char* buf, int* pos, int max, char* liste, int listmax) {
    int sp = -1; for (int i = 0; i < *pos; i++) if (buf[i] == ' ') { sp = i; break; }
    char kand[40][24]; int nk = 0; int start;
    if (sp < 0) {                                                   /* Bereich vervollstaendigen */
        start = 0;
        for (int i = 0; i < NTAB; i++) {
            char b[24]; erstes(tab[i].bereich, b, 24); int dup = 0; for (int j = 0; j < nk; j++) if (wort_gleich(kand[j], slen(kand[j]), b)) dup = 1;
            if (dup || nk >= 40) continue;
            int ok = 1; for (int q = 0; q < *pos; q++) if (lc((unsigned char)b[q]) != lc((unsigned char)buf[q]) || !b[q]) { ok = 0; break; }
            if (ok) kopie(kand[nk++], b, 24);
        }
        static const char* const extra[] = { "hilfe", "abmelden", 0 };
        for (int e = 0; extra[e]; e++) { int ok = 1; for (int q = 0; q < *pos; q++) if (lc((unsigned char)extra[e][q]) != lc((unsigned char)buf[q]) || !extra[e][q]) { ok = 0; break; } if (ok && nk < 40) kopie(kand[nk++], extra[e], 24); }
    } else {                                                        /* Aktion vervollstaendigen */
        int sp2 = -1; for (int i = sp + 1; i < *pos; i++) if (buf[i] == ' ') { sp2 = i; break; }
        if (sp2 >= 0) return 0;
        start = sp + 1; int pl = *pos - start;
        for (int i = 0; i < NTAB; i++) {
            if (!alt_gleich(tab[i].bereich, buf, sp) || !tab[i].aktion[0] || tab[i].aktion[0] == '*') continue;
            char b[24]; erstes(tab[i].aktion, b, 24); int ok = 1; for (int q = 0; q < pl; q++) if (lc((unsigned char)b[q]) != lc((unsigned char)buf[start + q]) || !b[q]) { ok = 0; break; }
            if (ok && nk < 40) kopie(kand[nk++], b, 24);
        }
    }
    if (!nk) return 0;
    int pl = *pos - start, gl = slen(kand[0]);                      /* gemeinsamer Anfang */
    for (int j = 1; j < nk; j++) { int q = 0; while (q < gl && lc((unsigned char)kand[j][q]) == lc((unsigned char)kand[0][q])) q++; gl = q; }
    if (gl > pl) { for (int q = pl; q < gl && *pos < max - 2; q++) buf[(*pos)++] = kand[0][q]; if (nk == 1 && *pos < max - 1) buf[(*pos)++] = ' '; buf[*pos] = 0; }
    else if (nk == 1 && *pos < max - 1 && buf[*pos - 1] != ' ') { buf[(*pos)++] = ' '; buf[*pos] = 0; }
    int l = 0; liste[0] = 0;
    if (nk > 1) for (int j = 0; j < nk; j++) { const char* s = kand[j]; while (*s && l < listmax - 3) liste[l++] = *s++; liste[l++] = ' '; liste[l++] = ' '; }
    liste[l] = 0; return nk;
}

int befehl_bereich(const char* ein) { while (*ein == ' ') ein++; int n = 0; while (ein[n] && ein[n] != ' ') n++; return n && bekannter_bereich(ein, n); }
