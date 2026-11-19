/*
 * Zynex OS - Shell (incl. SynFS)
 */

#include "shell.h"
#include "gehirn.h"
#include "pmm.h"
#include "timer.h"
#include "task.h"
#include "kheap.h"
#include "ramfs.h"
#include "arena.h"
#include "ai.h"
#include "capability.h"
#include "semantic.h"
#include "module.h"
#include "block.h"
#include "syscall.h"
#include "synfs.h"
#include "synbin.h"
#include "pci.h"
#include "usb.h"
#include "hyperv.h"
#include "layouts.h"
#include "qubit.h"
#include "lms.h"
#include "lms_testvec.h"
#include "net.h"
#include "wifi.h"
#include "wlan.h"
#include "x509.h"
#include "benutzer.h"
#include "installer.h"
#include "gui.h"
#include "desktop.h"
#include "wpa.h"
#include "multiboot.h"
#include "befehle.h"
#include "fw.h"
#include "zyndb.h"
#include "extki.h"
#include "chats.h"
#include "gedaechtnis.h"
#include "krypto.h"
#include "hda.h"

extern void kprint(const char* str);
extern void kprint_dec(uint32_t val);
extern void kputchar(char c);

#define SHELL_BUF_SIZE 128
static char input_buf[SHELL_BUF_SIZE];
static int  input_pos = 0;

static int require_cap(enum cap_type type, const char* what);
static int streq(const char* a, const char* b);
static void shell_prompt(void) {
    if (benutzer_name()[0]) { kprint(benutzer_name()); kprint("@"); }
    kprint("zynex> ");
}

/* ---- Rueckfragen (Anmeldung, Passwoerter): die naechste Eingabezeile geht an eine Funktion ---- */
static struct { int aktiv; int maskiert; void (*cb)(const char*); } frage;
static void shell_frage(const char* p, int maskiert, void (*cb)(const char*)) {
    kprint(p); frage.aktiv = 1; frage.maskiert = maskiert; frage.cb = cb; input_pos = 0; input_buf[0] = 0;
}
static char tmp_name[16]; static int tmp_rolle; static char tmp_pw[40];
static void wische(char* p, int n) { for (int i = 0; i < n; i++) p[i] = 0; }
static void kopiere(char* d, const char* s, int max) { int i = 0; while (s[i] && i < max - 1) { d[i] = s[i]; i++; } d[i] = 0; }

static struct install_cfg icfg;
static void inst_fortschritt(int p, const char* t) { kprint("  ["); kprint_dec((uint32_t)p); kprint(" %] "); kprint(t); kprint("\n"); }
static void inst_fertig(const char* l) {
    if (!streq(l, icfg.pw)) { kprint("Die Passwoerter sind verschieden - abgebrochen\n"); wische(icfg.pw, sizeof(icfg.pw)); return; }
    kprint("Installation startet - bitte nicht ausschalten\n");
    int r = installer_start(&icfg, inst_fortschritt);
    wische(icfg.pw, sizeof(icfg.pw));
    if (r == 0) kprint("Fertig! Entferne die Installations-ISO/den USB-Stick und gib 'reboot' ein.\n");
    else kprint("Installation fehlgeschlagen\n");
}
static void inst_pw1(const char* l) { kopiere(icfg.pw, l, sizeof(icfg.pw)); shell_frage("Passwort wiederholen: ", 1, inst_fertig); }
static void login_start(void);
static void cb_login_pw(const char* l) {
    if (benutzer_login(tmp_name, l) == 0) {
        kprint("Willkommen, "); kprint(benutzer_name()); kprint(" ("); kprint(benutzer_rollentext(benutzer_rolle())); kprint(")\n");
    } else {
        kprint("Anmeldung fehlgeschlagen\n");
        __asm__ volatile ("sti"); timer_sleep(1500);
        login_start();
    }
}
static void cb_login_name(const char* l) { kopiere(tmp_name, l, 16); shell_frage("Passwort: ", 1, cb_login_pw); }
static void login_start(void) { shell_frage("Benutzer: ", 0, cb_login_name); }


/* ---- Verschluesselung: Passwort-Rueckfragen ---- */
static char kr_alt[40];
static void kr_meldung(int r) {
    if (r == 0) kprint("Fertig.\n");
    else if (r == -1) kprint("SynFS ist nicht eingebunden\n");
    else if (r == -2) kprint("Schon verschluesselt (oder Passwort falsch)\n");
    else if (r == -3) kprint("Passwort zu kurz (mindestens 8 Zeichen)\n");
    else kprint("Fehlgeschlagen - Platte pruefen\n");
}
static void kr_aktiv_fertig(const char* l) {
    if (!streq(l, tmp_pw)) { kprint("Die Passwoerter sind verschieden - abgebrochen\n"); wische(tmp_pw, sizeof(tmp_pw)); return; }
    kprint("Verschluessele - NICHT ausschalten, das kann je nach Plattengroesse dauern ...\n");
    int r = krypto_einrichten(tmp_pw);
    wische(tmp_pw, sizeof(tmp_pw));
    if (r == 0) kprint("Fertig. Alle Daten sind jetzt verschluesselt. Beim naechsten Start: krypto entsperren\n"); else kr_meldung(r);
}
static void kr_aktiv_pw1(const char* l) {
    if (l[0] && l[1] && l[2] && l[3] && l[4] && l[5] && l[6] && l[7]) { kopiere(tmp_pw, l, sizeof(tmp_pw)); shell_frage("Passwort wiederholen: ", 1, kr_aktiv_fertig); }
    else kprint("Passwort zu kurz (mindestens 8 Zeichen)\n");
}
static void kr_entsperr_fertig(const char* l) {
    char dev[20]; kopiere(dev, krypto_gesperrt_dev(), sizeof(dev));
    int r = krypto_entsperren(dev, l);
    if (r == -1) { kprint("Falsches Passwort\n"); return; }
    if (r != 0) { kprint("Dieses Geraet ist nicht verschluesselt\n"); return; }
    if (block_select(dev) == 0 && synfs_mount() == 0) {
        kprint("Entsperrt und eingebunden\n"); benutzer_laden(); zdb_init(); ged_neu_laden(); fw_laden();
        if (benutzer_gesperrt()) login_start();
    } else kprint("Entsperrt, aber das Einbinden schlug fehl\n");
}
static void kr_pw_neu2(const char* l) {
    if (!streq(l, tmp_pw)) { kprint("Die Passwoerter sind verschieden - abgebrochen\n"); }
    else { int r = krypto_passwort(kr_alt, tmp_pw); if (r == -2) kprint("Altes Passwort falsch\n"); else if (r == 0) kprint("Passwort geaendert\n"); else kr_meldung(r); }
    wische(tmp_pw, sizeof(tmp_pw)); wische(kr_alt, sizeof(kr_alt));
}
static void kr_pw_neu1(const char* l) { kopiere(tmp_pw, l, sizeof(tmp_pw)); shell_frage("Neues Passwort wiederholen: ", 1, kr_pw_neu2); }
static void kr_pw_alt(const char* l) { kopiere(kr_alt, l, sizeof(kr_alt)); shell_frage("Neues Passwort: ", 1, kr_pw_neu1); }
static void krypto_befehl(const char* a) {
    while (*a == ' ') a++;
    if (!*a || streq(a, "status")) krypto_status();
    else if (streq(a, "aktivieren") || streq(a, "an")) {
        if (!require_cap(CAP_ADMIN, "krypto")) return;
        kprint("Verschluesselt alle Daten dieses SynFS (256-Bit, quantensicher gegen Grover). Ohne Passwort gibt es KEINEN Zugriff mehr.\n");
        shell_frage("Neues Passwort (mind. 8 Zeichen): ", 1, kr_aktiv_pw1);
    }
    else if (streq(a, "entsperren")) {
        if (!krypto_gesperrt_dev()[0]) { kprint("Nichts gesperrt\n"); return; }
        shell_frage("Passwort: ", 1, kr_entsperr_fertig);
    }
    else if (streq(a, "fortsetzen")) { if (!require_cap(CAP_ADMIN, "krypto")) return; kprint("Weiter mit der Verschluesselung ...\n"); kr_meldung(krypto_fortsetzen()); }
    else if (streq(a, "passwort")) { if (!require_cap(CAP_ADMIN, "krypto")) return; shell_frage("Altes Passwort: ", 1, kr_pw_alt); }
    else kprint("krypto: status | aktivieren | entsperren | passwort | fortsetzen\n");
}


/* ---- Externe Modelle, Modelle, Chats ---- */
static int ke_prov;
static void ke_key(const char* l) { if (!l[0]) { kprint("Abgebrochen\n"); return; } ext_schluessel_setze(ke_prov, l); ext_setze_quelle(ke_prov); kprint(ext_name(ke_prov)); kprint(" ist eingerichtet und aktiv. ACHTUNG: Fragen im Chat gehen jetzt an diesen Anbieter. Zurueck: kiextern intern\n"); }
static int prov_aus(const char* n) { for (int i = 1; i < 4; i++) if (streq(n, ext_kennung(i)) || streq(n, ext_name(i))) return i; if (streq(n, "chatgpt")) return 1; return 0; }
static void kiextern_befehl(const char* a) {
    while (*a == ' ') a++;
    char w[16]; int n = 0; while (*a && *a != ' ' && n < 15) w[n++] = *a++; w[n] = 0; while (*a == ' ') a++;
    if (!w[0] || streq(w, "status")) {
        kprint("Aktive Quelle: "); kprint(ext_name(ext_quelle())); kprint("  (Zynie intern bleibt immer als Rueckfall bereit)\n");
        for (int i = 1; i < 4; i++) { kprint("  "); kprint(ext_name(i)); kprint(": Schluessel "); kprint(ext_schluessel_da(i) ? "gesetzt" : "fehlt"); kprint(", Modell "); kprint(ext_modell(i)); kprint("\n"); }
        kprint("  kiextern einrichten <openai|claude|gemini> | modell <anbieter> <name> | intern | test\n"); return;
    }
    if (streq(w, "intern")) { ext_setze_quelle(0); kprint("Zynie (intern) antwortet wieder\n"); return; }
    if (streq(w, "einrichten")) { int p = prov_aus(a); if (!p) { kprint("kiextern einrichten <openai|claude|gemini>\n"); return; } ke_prov = p; kprint("Der Schluessel wird verschluesselt in ZynDB abgelegt.\n"); shell_frage("API-Schluessel: ", 1, ke_key); return; }
    if (streq(w, "modell")) { char pn[16]; int k = 0; while (*a && *a != ' ' && k < 15) pn[k++] = *a++; pn[k] = 0; while (*a == ' ') a++; int p = prov_aus(pn); if (!p || !*a) { kprint("kiextern modell <openai|claude|gemini> <modellname>\n"); return; } ext_modell_setze(p, a); kprint("Modell gesetzt\n"); return; }
    if (streq(w, "test")) {
        if (!ext_quelle()) { kprint("Zurzeit antwortet Zynie (intern). Erst: kiextern einrichten ...\n"); return; }
        static char o[500]; kprint("Frage an "); kprint(ext_name(ext_quelle())); kprint(" ...\n");
        int r = ext_frage("Antworte mit einem kurzen Satz: Wer bist du?", o, sizeof(o)); kprint(r > 0 ? "Antwort: " : "Fehler: "); kprint(o); kprint("\n"); return;
    }
    kprint("kiextern: status | einrichten | modell | intern | test\n");
}
static void kimodell_befehl(const char* a) {
    while (*a == ' ') a++;
    char w[12]; int n = 0; while (*a && *a != ' ' && n < 11) w[n++] = *a++; w[n] = 0; while (*a == ' ') a++;
    char dev[24]; int k = 0; while (*a && *a != ' ' && k < 23) dev[k++] = *a++; dev[k] = 0; while (*a == ' ') a++;
    if (!w[0] || streq(w, "liste")) { kprint("Sprachmodelle auf diesem Rechner:\n"); gehirn_modelle_zeigen(); kprint("  waehlen: ki modell waehle <platte> (gilt ab dem naechsten Start) | loeschen: ki modell loesche <platte> ja\n  Ein neues Modell kommt mit tools/mkpack.py auf eine eigene Modell-Partition; das letzte Modell laesst sich nicht loeschen.\n"); return; }
    if (streq(w, "waehle")) { if (!require_cap(CAP_ADMIN, "ki modell")) return; int r = gehirn_modell_waehlen(dev); kprint(r == 0 ? "Gewaehlt - wird beim naechsten Start geladen\n" : r == -1 ? "Dort liegt kein Modell (siehe: ki modell liste)\n" : "Speichern fehlgeschlagen\n"); return; }
    if (streq(w, "loesche")) {
        if (!require_cap(CAP_ADMIN, "ki modell")) return;
        if (!streq(a, "ja")) { kprint("Zum Bestaetigen: ki modell loesche "); kprint(dev); kprint(" ja\n"); return; }
        int r = gehirn_modell_loeschen(dev); kprint(r == 0 ? "Modell geloescht\n" : r == -3 ? "Das ist das letzte Modell - Zynie braucht mindestens eins\n" : r == -1 ? "Dort liegt kein Modell\n" : "Loeschen fehlgeschlagen\n"); return;
    }
    kprint("ki modell: liste | waehle <platte> | loesche <platte> ja\n");
}
static void chat_befehl(const char* a) {
    while (*a == ' ') a++;
    char w[12]; int n = 0; while (*a && *a != ' ' && n < 11) w[n++] = *a++; w[n] = 0; while (*a == ' ') a++;
    if (!w[0] || streq(w, "liste")) {
        int ids[16], c = chat_liste(ids, 16, ""); if (!c) { kprint("Noch keine Chats\n"); return; }
        for (int i = 0; i < c; i++) { kprint(ids[i] == chat_aktiv() ? " > " : "   "); kprint(chat_gepinnt(ids[i]) ? "* " : "  "); kprint(chat_titel(ids[i])); if (chat_projekt(ids[i])[0]) { kprint("  ["); kprint(chat_projekt(ids[i])); kprint("]"); } kprint("\n"); } return;
    }
    if (streq(w, "neu")) { kprint(chat_neu(a) > 0 ? "Neuer Chat angelegt\n" : "Fehlgeschlagen\n"); return; }
    int id = chat_aktiv(); if (!id) { kprint("Kein aktiver Chat\n"); return; }
    if (streq(w, "pin")) { chat_pin_umschalten(id); kprint(chat_gepinnt(id) ? "Angepinnt\n" : "Nicht mehr angepinnt\n"); return; }
    if (streq(w, "projekt")) { chat_projekt_setze(id, a); kprint("Projekt gesetzt\n"); return; }
    if (streq(w, "loeschen")) { chat_loeschen(id); kprint("Chat geloescht\n"); return; }
    kprint("chat: liste | neu | pin | projekt <name> | loeschen\n");
}

static void neu_fertig(const char* l) {
    if (!streq(l, tmp_pw)) kprint("Die Passwoerter sind verschieden - abgebrochen\n");
    else {
        int r = benutzer_neu(tmp_name, tmp_pw, tmp_rolle);
        if (r == 0) { kprint("Benutzer angelegt: "); kprint(tmp_name); kprint(" ("); kprint(benutzer_rollentext(benutzer_anzahl() == 1 ? ROLLE_ADMIN : tmp_rolle)); kprint(")\n"); }
        else if (r == -1) kprint("Ungueltiger Name (1-15 Zeichen: a-z 0-9 _ - .)\n");
        else if (r == -2) kprint("Benutzer existiert schon\n");
        else if (r == -3) kprint("Maximal 8 Benutzer\n");
        else if (r == -5) kprint("Passwort zu kurz (mindestens 4 Zeichen)\n");
        else kprint("Speichern fehlgeschlagen (ist SynFS eingebunden?)\n");
    }
    wische(tmp_pw, sizeof(tmp_pw));
}
static void neu_pw1(const char* l) { kopiere(tmp_pw, l, sizeof(tmp_pw)); shell_frage("Passwort wiederholen: ", 1, neu_fertig); }
static void pw_fertig(const char* l) {
    if (!streq(l, tmp_pw)) kprint("Die Passwoerter sind verschieden - abgebrochen\n");
    else { int r = benutzer_passwort(tmp_name, tmp_pw); kprint(r == 0 ? "Passwort geaendert\n" : r == -5 ? "Passwort zu kurz (mindestens 4 Zeichen)\n" : "Fehlgeschlagen\n"); }
    wische(tmp_pw, sizeof(tmp_pw));
}
static void pw_pw1(const char* l) { kopiere(tmp_pw, l, sizeof(tmp_pw)); shell_frage("Neues Passwort wiederholen: ", 1, pw_fertig); }

static int streq(const char* a, const char* b) {
    while (*a && *b) { if (*a != *b) return 0; a++; b++; }
    return *a == *b;
}
static int starts_with(const char* str, const char* prefix) {
    while (*prefix) { if (*str != *prefix) return 0; str++; prefix++; }
    return 1;
}

static int require_cap(enum cap_type type, const char* what) {
    uint32_t tid = task_get_current_id();
    if (!benutzer_darf((int)type)) { kprint("[Benutzer] Keine Berechtigung fuer: "); kprint(what); kprint("\n"); return 0; }
    if (cap_check(type, tid)) return 1;
    kprint("[CAP] Denied: "); kprint(what); kprint("\n");
    return 0;
}

static int64_t do_syscall(uint64_t num, uint64_t a, uint64_t b, uint64_t c) {
    int64_t ret;
    __asm__ volatile ("int $0x80" : "=a"(ret) : "a"(num), "b"(a), "c"(b), "d"(c) : "memory");
    return ret;
}

/* Diagnose fuer den Quanten-Kernel (Nutzer sehen spaeter keine Shell; Agenten nutzen den Systemaufruf SYS_QUBIT) */
static int ziffer(const char* s) { return (s[0] >= '0' && s[0] <= '9') ? s[0] - '0' : -1; }
static void cmd_qubit(const char* p) {
    while (*p == ' ') p++;
    char t0[12] = {0}, t1[4] = {0}, t2[4] = {0}; int k = 0;
    while (*p && *p != ' ' && k < 11) t0[k++] = *p++;
    while (*p == ' ') p++;
    k = 0;
    while (*p && *p != ' ' && k < 3) t1[k++] = *p++;
    while (*p == ' ') p++;
    k = 0;
    while (*p && *p != ' ' && k < 3) t2[k++] = *p++;
    int x = ziffer(t1), y = ziffer(t2); int64_t r = 0;
    if (streq(t0, "selbsttest")) { qubit_selbsttest(); return; }
    if (streq(t0, "zustand")) { qubit_ausgabe(); return; }
    if (streq(t0, "reset") && x >= 0) r = do_syscall(SYS_QUBIT, 1, (uint64_t)x, 0);
    else if (t0[0] && !t0[1] && x >= 0 && (t0[0]=='h'||t0[0]=='x'||t0[0]=='y'||t0[0]=='z'||t0[0]=='s'||t0[0]=='t')) r = do_syscall(SYS_QUBIT, 2, (uint64_t)t0[0], (uint64_t)x);
    else if (streq(t0, "cnot") && x >= 0 && y >= 0) r = do_syscall(SYS_QUBIT, 3, (uint64_t)x, (uint64_t)y);
    else if (streq(t0, "cz") && x >= 0 && y >= 0) r = do_syscall(SYS_QUBIT, 7, (uint64_t)x, (uint64_t)y);
    else if (streq(t0, "swap") && x >= 0 && y >= 0) r = do_syscall(SYS_QUBIT, 8, (uint64_t)x, (uint64_t)y);
    else if (streq(t0, "messen") && x >= 0) { r = do_syscall(SYS_QUBIT, 4, (uint64_t)x, 0); if (r >= 0) { kprint("Qubit "); kprint_dec((uint32_t)x); kprint(" gemessen: "); kprint_dec((uint32_t)r); kprint("\n"); return; } }
    else if (streq(t0, "alle")) { r = do_syscall(SYS_QUBIT, 6, 0, 0); if (r >= 0) { kprint("Gemessener Zustand: "); kprint_dec((uint32_t)r); kprint("\n"); return; } }
    else { kprint("Usage: qubit reset <1-4> | h|x|y|z|s|t <q> | cnot|cz|swap <a> <b> | messen <q> | alle | zustand | selbsttest\n"); return; }
    if (r == -100) kprint("[CAP] Denied: qubit\n"); else if (r < 0) kprint("Qubit: ungueltige Angabe\n"); else kprint("ok\n");
}

static void cmd_help(void) {
    kprint("Commands:\n");
    kprint("  help about mem heap ai ki aidemo ticks uptime ps caps\n");
    kprint("  Installation (von der ISO): installer (grafisch)   oder: install <platte> <benutzername> [kuerzel] [ja]   reboot  halt\n");
    kprint("  Benutzer: whoami  anmelden  abmelden  benutzer liste|neu <name> <admin|benutzer|gast>|loeschen <name>|passwort [name]\n");
    kprint("  block syscall ls create write cat rm\n");
    kprint("  sem*  |  SynFS:\n");
    kprint("  syninfo synls synsync\n");
    kprint("  syncreate <n>  synai <n>  synwrite <n> <text>\n");
    kprint("  syncat <n>  syndel <n>  syntag <n> <tag>  synfind <tag>  synuntag <n> <tag>\n  Objekte: obj <tag> [tag2 ..] (alle Tags muessen passen)  zeit (zuletzt geaenderte Dateien)\n  Snapshots: snap neu <n>  snap liste  snap zurueck <n>  snap loeschen <n>\n");
    kprint("  .synbin: synrun <n>  synhdr <n>  syngrant <0-3>\n");
    kprint("  Netz: net  dhcp  ping <ip|name>  dns <name>  http <adresse> [fingerabdruck]  wget <adresse> <name>\n  https:// = TLS 1.3 mit Root-CA-Pruefung (cas, casadd <datei.der>); Fingerabdruck = Pinning\n  Geraete: pci  block  usb  mouse  tastatur ch|us  synmount <dev>  synformat <dev> ja\n");
}

static void cmd_about(void) {
    kprint("Zynex OS + SynFS (Synaptic File System)\n");
    kprint("  Journaled | Extent-based | AI tags | Fast path\n");
}

static void ki_teil(const char* t, int n) { char b[64]; int k = n > 63 ? 63 : n; for (int i = 0; i < k; i++) b[i] = t[i]; b[k] = 0; kprint(b); }
static void ki_befehl(const char* frage) {
    if (!*frage) { kprint("Aufruf: ki <Frage>   (Status: "); kprint(gehirn_status()); kprint(")\n"); return; }
    static char antwort[1200];
    kprint("[KI] Zynie denkt nach ...\n");
    static char gk[560]; ged_kontext(frage, gk, sizeof(gk), 1); gehirn_kontext(gk);        /* Erinnerungen aus dem Synapsen-Gedaechtnis */
    int n = gehirn_frage(frage, antwort, sizeof(antwort), 0);
    if (n < 0) { kprint("[KI] Nicht moeglich: "); kprint(gehirn_status()); kprint("\n"); return; }
    kprint("[KI] Antwort: "); kprint(antwort); kprint("\n");
    ged_lerne(frage, antwort);
    (void)ki_teil;
}

static void unbekannt(const char* cmd) {
    int w = 1; for (const char* c = cmd; *c; c++) if (*c == ' ' && c[1] && c[1] != ' ') w++;
    if (gehirn_vorhanden() && w >= 3) { ki_befehl(cmd); return; }               /* ganze Saetze gehen an Zynie */
    char v[24];
    if (befehl_vorschlag(cmd, v, 24)) { kprint("Unbekannt. Meintest du: "); kprint(v); kprint("  ?   (hilfe zeigt alle Befehle)\n"); return; }
    kprint("Unbekannt. 'hilfe' zeigt alle Befehle.\n");
}
static void run_legacy(char* cmd) {

    /* Netz- und Geraetebefehle brauchen CAP_IO, systeminterne Dateien (sys.*) nur Admins */
    if ((streq(cmd, "net") || streq(cmd, "dhcp") || starts_with(cmd, "ping ") || starts_with(cmd, "dns ") || starts_with(cmd, "http ") ||
         starts_with(cmd, "wget ")) && !require_cap(CAP_IO, "Netzwerk")) return;
    if ((starts_with(cmd, "syncat sys.") || starts_with(cmd, "synwrite sys.") || starts_with(cmd, "syndel sys.") || starts_with(cmd, "syncreate sys.") ||
         starts_with(cmd, "syntag sys.") || starts_with(cmd, "synuntag sys.")) && !require_cap(CAP_ADMIN, "Systemdatei sys.*")) return;

    if (streq(cmd, "help")) cmd_help();
    else if (streq(cmd, "whoami")) {
        if (benutzer_anzahl() == 0) kprint("Kein Konto angelegt (Einrichtungsmodus, alle Rechte)\n");
        else { kprint(benutzer_name()); kprint(" ("); kprint(benutzer_rollentext(benutzer_rolle())); kprint(")\n"); }
    }
    else if (streq(cmd, "abmelden") || streq(cmd, "anmelden")) {
        if (benutzer_anzahl() == 0) kprint("Es gibt noch kein Konto (benutzer neu <name> admin)\n");
        else { benutzer_abmelden(); login_start(); }
    }
    else if (starts_with(cmd, "benutzer")) {
        const char* p = cmd + 8; while (*p == ' ') p++;
        char sub[12]; int si = 0; while (*p && *p != ' ' && si < 11) sub[si++] = *p++;
        sub[si] = 0; while (*p == ' ') p++;
        if (streq(sub, "liste")) benutzer_liste();
        else if (streq(sub, "neu")) {
            char n[16]; int ni = 0; while (*p && *p != ' ' && ni < 15) n[ni++] = *p++;
            n[ni] = 0; while (*p == ' ') p++;
            int rolle = *p ? benutzer_rolle_aus_text(p) : ROLLE_BENUTZER;
            if (!n[0] || rolle < 0) kprint("Usage: benutzer neu <name> <admin|benutzer|gast>\n");
            else if (benutzer_anzahl() > 0 && !require_cap(CAP_ADMIN, "benutzer neu")) { }
            else { kopiere(tmp_name, n, 16); tmp_rolle = rolle; shell_frage("Passwort: ", 1, neu_pw1); }
        }
        else if (streq(sub, "loeschen")) {
            if (!require_cap(CAP_ADMIN, "benutzer loeschen")) return;
            int r = benutzer_loeschen(p);
            kprint(r == 0 ? "Benutzer geloescht\n" : r == -6 ? "Der letzte Admin kann nicht geloescht werden\n" : "Benutzer nicht gefunden\n");
            if (r == 0 && benutzer_gesperrt()) login_start();
        }
        else if (streq(sub, "passwort")) {
            const char* ziel = *p ? p : benutzer_name();
            if (!ziel[0]) kprint("Kein Konto angemeldet\n");
            else if (*p && !streq(p, benutzer_name()) && !require_cap(CAP_ADMIN, "fremdes Passwort aendern")) { }
            else { kopiere(tmp_name, ziel, 16); shell_frage("Neues Passwort: ", 1, pw_pw1); }
        }
        else kprint("Usage: benutzer liste|neu <name> <rolle>|loeschen <name>|passwort [name]\n");
    }
    else if (streq(cmd, "about")) cmd_about();
    else if (streq(cmd, "mem")) {
        if (require_cap(CAP_MEM_READ,"mem")) {
            kprint("Free: "); kprint_dec(pmm_get_free_pages()*4); kprint(" KiB\n");
        }
    }
    else if (streq(cmd, "heap")) { if (require_cap(CAP_MEM_READ,"heap")) kheap_stats(); }
    else if (starts_with(cmd, "ki ") || streq(cmd, "ki")) { if (require_cap(CAP_AI,"ki")) ki_befehl(cmd + (cmd[2] ? 3 : 2)); }
    else if (streq(cmd, "ai")) { if (require_cap(CAP_AI,"ai")) ai_stats(); }
    else if (streq(cmd, "aidemo")) { if (require_cap(CAP_AI,"aidemo")) ai_demo(); }
    else if (streq(cmd, "ticks")) { kprint("Ticks: "); kprint_dec(timer_get_ticks()); kprint("\n"); }
    else if (streq(cmd, "uptime")) { kprint("Uptime: "); kprint_dec(timer_get_seconds()); kprint(" s\n"); }
    else if (streq(cmd, "ps")) { if (require_cap(CAP_TASK,"ps")) task_list(); }
    else if (streq(cmd, "caps")) cap_list();
    else if (streq(cmd, "mods")) { if (require_cap(CAP_MODULE,"mods")) mod_list(); }
    else if (streq(cmd, "block")) block_info();
    else if (streq(cmd, "syscall")) {
        int64_t t = do_syscall(SYS_GETTICKS,0,0,0);
        kprint("SYS_GETTICKS -> "); kprint_dec((uint32_t)t); kprint("\n");
        do_syscall(SYS_BLK_INFO,0,0,0);
    }
    else if (streq(cmd, "ls")) { if (require_cap(CAP_FS,"ls")) ramfs_list(); }
    else if (streq(cmd, "sem")) { if (require_cap(CAP_AI,"sem")) sem_list(); }
    /* SynFS */
    else if (streq(cmd, "syninfo")) synfs_info();
    else if (streq(cmd, "synls")) { if (require_cap(CAP_FS,"synls")) synfs_list(); }
    else if (streq(cmd, "synsync")) { if (require_cap(CAP_FS,"synsync")) synfs_sync(); }
    else if (starts_with(cmd, "syncreate ")) {
        if (!require_cap(CAP_FS,"syncreate")) return;
        const char* n = cmd+10; while (*n==' ') n++;
        if (synfs_create(n, SYNFS_FILE)==0) kprint("OK\n"); else kprint("Fail\n");
    }
    else if (starts_with(cmd, "synai ")) {
        if (!require_cap(CAP_FS,"synai")) return;
        const char* n = cmd+6; while (*n==' ') n++;
        if (synfs_create(n, SYNFS_AI)==0) kprint("AI inode OK\n"); else kprint("Fail\n");
    }
    else if (starts_with(cmd, "synwrite ")) {
        if (!require_cap(CAP_FS,"synwrite")) return;
        const char* p = cmd+9; while (*p==' ') p++;
        char name[32]; int ni=0;
        while (*p && *p!=' ' && ni<31) name[ni++]=*p++;
        name[ni]=0; while (*p==' ') p++;
        if (name[0] && *p) {
            uint32_t len=0; const char* s=p; while (*s++) len++;
            int r = synfs_write(name, p, len);
            if (r>=0) { kprint("Wrote "); kprint_dec((uint32_t)r); kprint(" B\n"); }
            else kprint("Write fail\n");
        } else kprint("Usage: synwrite <name> <text>\n");
    }
    else if (starts_with(cmd, "synstress ")) {            /* Belastungstest: synstress <anzahl> <kib> schreiben|pruefen|loeschen */
        if (!require_cap(CAP_FS,"synstress")) return;
        static uint8_t sbuf[1 << 20];
        const char* p = cmd + 10; uint32_t zahl[2] = {0, 0};
        for (int k = 0; k < 2; k++) { while (*p == ' ') p++; while (*p >= '0' && *p <= '9') zahl[k] = zahl[k] * 10 + (uint32_t)(*p++ - '0'); }
        while (*p == ' ') p++;
        int modus = starts_with(p, "pruefen") ? 1 : starts_with(p, "loeschen") ? 2 : 0;
        uint32_t n = zahl[0], len = zahl[1] * 1024, ok = 0, fehler = 0;
        if (!n || n > 1000 || !len || len > sizeof(sbuf)) { kprint("Usage: synstress <anzahl <=1000> <kib <=1024> schreiben|pruefen|loeschen\n"); return; }
        for (uint32_t i = 0; i < n; i++) {
            char nm[16] = "st"; int l = 2; char t[6]; int tl = 0; uint32_t v = i; do { t[tl++] = (char)('0' + v % 10); v /= 10; } while (v);
            while (tl) { nm[l++] = t[--tl]; }
            nm[l] = 0;
            if (modus == 2) { if (synfs_delete(nm) == 0) ok++; else fehler++; continue; }
            if (modus == 0) {
                for (uint32_t b = 0; b < len; b++) sbuf[b] = (uint8_t)(b * 7 + i);
                if (synfs_write(nm, sbuf, len) != (int)len) { fehler++; continue; }
            }
            for (uint32_t b = 0; b < len; b++) sbuf[b] = 0;
            if (synfs_read(nm, sbuf, len) != (int)len) { fehler++; continue; }
            int gut = 1; for (uint32_t b = 0; b < len; b++) if (sbuf[b] != (uint8_t)(b * 7 + i)) { gut = 0; break; }
            if (gut) ok++; else fehler++;
        }
        kprint("synstress: "); kprint_dec(ok); kprint(" OK, "); kprint_dec(fehler); kprint(" Fehler\n");
    }
    else if (starts_with(cmd, "syncat ")) {
        if (!require_cap(CAP_FS,"syncat")) return;
        const char* n = cmd+7; while (*n==' ') n++;
        char buf[512];
        int r = synfs_read(n, buf, sizeof(buf)-1);
        if (r>=0) { buf[r]=0; kprint(buf); kprint("\n"); }
        else kprint("Not found\n");
    }
    else if (starts_with(cmd, "syndel ")) {
        if (!require_cap(CAP_FS,"syndel")) return;
        const char* n = cmd+7; while (*n==' ') n++;
        if (synfs_delete(n)==0) kprint("Deleted\n"); else kprint("Fail\n");
    }
    else if (starts_with(cmd, "syntag ")) {
        if (!require_cap(CAP_FS,"syntag")) return;
        const char* p = cmd+7; while (*p==' ') p++;
        char name[32]; int ni=0;
        while (*p && *p!=' ' && ni<31) name[ni++]=*p++;
        name[ni]=0; while (*p==' ') p++;
        if (name[0] && *p) {
            { int r = synfs_tag(name, p); if (r==0) kprint("Tagged\n"); else if (r==-3) kprint("Maximal 8 Tags pro Datei\n"); else kprint("Fail\n"); }
        } else kprint("Usage: syntag <name> <tag>\n");
    }
    else if (starts_with(cmd, "synuntag ")) {
        if (!require_cap(CAP_FS,"synuntag")) return;
        const char* p = cmd+9; while (*p==' ') p++;
        char name[32]; int ni=0;
        while (*p && *p!=' ' && ni<31) name[ni++]=*p++;
        name[ni]=0; while (*p==' ') p++;
        if (name[0] && *p) { if (synfs_untag(name, p)==0) kprint("Tag entfernt\n"); else kprint("Fail\n"); }
        else kprint("Usage: synuntag <name> <tag>\n");
    }
    else if (streq(cmd, "zeit")) { if (require_cap(CAP_FS,"zeit")) synfs_timeline(10); }
    else if (starts_with(cmd, "synfind ")) {
        if (!require_cap(CAP_FS,"synfind")) return;
        const char* tag = cmd+8; while (*tag==' ') tag++;
        char name[32];
        if (synfs_find_tag(tag, name, sizeof(name))>=0) {
            kprint("Found: "); kprint(name); kprint("\n");
        } else kprint("No match\n");
    }
    else if (starts_with(cmd, "snap")) {
        if (!require_cap(CAP_FS,"snap")) return;
        const char* p = cmd+4; while (*p==' ') p++;
        char sub[12]; int si=0; while (*p && *p!=' ' && si<11) sub[si++]=*p++;
        sub[si]=0; while (*p==' ') p++;
        if (streq(sub,"liste")) synfs_snap_list();
        else if (streq(sub,"neu") && *p) { int r = synfs_snap_create(p); if (r==0) kprint("Snapshot erstellt\n"); else if (r==-2) kprint("Name existiert schon\n"); else if (r==-3) kprint("Alle 4 Snapshot-Plaetze belegt (snap loeschen <name>)\n"); else kprint("Snapshot fehlgeschlagen\n"); }
        else if (streq(sub,"zurueck") && *p) { if (synfs_snap_restore(p)==0) kprint("Zustand zurueckgesetzt\n"); else kprint("Snapshot nicht gefunden\n"); }
        else if (streq(sub,"loeschen") && *p) { if (synfs_snap_delete(p)==0) kprint("Snapshot geloescht\n"); else kprint("Snapshot nicht gefunden\n"); }
        else kprint("Usage: snap neu|liste|zurueck|loeschen <name>\n");
    }
    else if (starts_with(cmd, "obj ")) {
        if (!require_cap(CAP_FS,"obj")) return;
        static char tb[4][SYNFS_TAG_LEN]; const char* tp[4]; int tn = 0;
        const char* p = cmd+4;
        while (*p && tn < 4) {
            while (*p==' ') p++;
            if (!*p) break;
            int k = 0; while (*p && *p!=' ' && k < SYNFS_TAG_LEN-1) tb[tn][k++] = *p++;
            tb[tn][k] = 0; tp[tn] = tb[tn]; tn++;
        }
        kprint("Objekte mit Tag");
        for (int i = 0; i < tn; i++) { kprint(i ? " + '" : " '"); kprint(tp[i]); kprint("'"); }
        kprint(":\n");
        if (tn == 0 || synfs_list_tags(tp, tn) <= 0) kprint("  (keine)\n");
    }
    else if (streq(cmd, "pci")) pci_list();
    else if (streq(cmd, "usb")) usb_list();
    else if (streq(cmd, "net")) net_info();
    else if (streq(cmd, "wifi")) wifi_info();
    else if (streq(cmd, "dhcp")) { if (net_dhcp() == 0) net_info(); else kprint("DHCP ohne Antwort\n"); }
    else if (starts_with(cmd, "ping ")) { const char* z = cmd + 5; while (*z == ' ') z++; net_ping(z); }
    else if (starts_with(cmd, "http ")) {
        const char* z = cmd + 5; while (*z == ' ') z++;
        char url[96]; int n = 0; while (*z && *z != ' ' && n < 95) url[n++] = *z++;
        url[n] = 0; while (*z == ' ') z++;
        net_http_fp(url, *z ? z : 0);
    }
    else if (starts_with(cmd, "wget ")) {
        if (!require_cap(CAP_FS,"wget")) return;
        const char* z = cmd + 5; while (*z == ' ') z++;
        char url[96]; int n = 0; while (*z && *z != ' ' && n < 95) url[n++] = *z++;
        url[n] = 0; while (*z == ' ') z++;
        if (url[0] && *z) {
            char name[32]; int k = 0; while (*z && *z != ' ' && k < 31) name[k++] = *z++;
            name[k] = 0; while (*z == ' ') z++;
            net_wget(url, name, *z ? z : 0);
        } else kprint("Usage: wget <adresse> <name> [fingerabdruck]\n");
    }
    else if (starts_with(cmd, "usbnet ")) {
        if (!require_cap(CAP_IO,"usbnet")) return;
        const char* a = cmd + 7; while (*a == ' ') a++;
        if (streq(a, "ecm")) { usb_net_prefer(0); kprint("USB-Netzwerk: ECM bevorzugt, Geraet wird neu aufgenommen\n"); }
        else if (streq(a, "rndis")) { usb_net_prefer(1); kprint("USB-Netzwerk: RNDIS bevorzugt, Geraet wird neu aufgenommen\n"); }
        else kprint("Usage: usbnet ecm|rndis\n");
    }
    else if (streq(cmd, "wpatest")) { wpa_selbsttest(); }
    else if (streq(cmd, "wlantest")) { wlan_selbsttest(); }
    else if (streq(cmd, "wlan") || starts_with(cmd, "wlan ")) wlan_befehl(cmd + 4);
    else if (starts_with(cmd, "qubit")) { cmd_qubit(cmd + 5); }
    else if (streq(cmd, "pqtest")) {
        /* Selbsttest der quantensicheren Signaturpruefung (LMS/SHA-256) mit einem von tools/pqsign.py erzeugten Vektor */
        static uint8_t m2[TV_MSG_LEN], s2[sizeof(tv_sig)];
        for (int i = 0; i < TV_MSG_LEN; i++) m2[i] = tv_msg[i];
        for (uint32_t i = 0; i < sizeof(tv_sig); i++) s2[i] = tv_sig[i];
        int gut = lms_verify(tv_pub, tv_sig, sizeof(tv_sig), m2, TV_MSG_LEN, 0, 0) == 0;
        m2[3] ^= 1; int msg_kaputt = lms_verify(tv_pub, tv_sig, sizeof(tv_sig), m2, TV_MSG_LEN, 0, 0) != 0; m2[3] ^= 1;
        s2[200] ^= 1; int sig_kaputt = lms_verify(tv_pub, s2, sizeof(tv_sig), m2, TV_MSG_LEN, 0, 0) != 0; s2[200] ^= 1;
        int kurz = lms_verify(tv_pub, tv_sig, sizeof(tv_sig) - 1, m2, TV_MSG_LEN, 0, 0) != 0;
        kprint("pq: gueltige Signatur "); kprint(gut ? "OK" : "FEHLT"); kprint(", veraenderte Nachricht abgelehnt "); kprint(msg_kaputt ? "OK" : "FEHLT");
        kprint(", veraenderte Signatur abgelehnt "); kprint(sig_kaputt ? "OK" : "FEHLT"); kprint(", falsche Laenge abgelehnt "); kprint(kurz ? "OK" : "FEHLT");
        kprint(gut && msg_kaputt && sig_kaputt && kurz ? "\npq: Selbsttest bestanden\n" : "\npq: FEHLER\n");
    }
    else if (streq(cmd, "firewall") || starts_with(cmd, "firewall ")) { if (cmd[8] && !streq(cmd + 9, "status") && !streq(cmd + 9, "log") && !streq(cmd + 9, "regeln") && !require_cap(CAP_ADMIN, "firewall")) return; fw_befehl(cmd + 8); }
    else if (streq(cmd, "kiextern") || starts_with(cmd, "kiextern ")) { if (cmd[8] && !streq(cmd + 9, "status") && !require_cap(CAP_ADMIN, "kiextern")) return; kiextern_befehl(cmd + 8); }
    else if (streq(cmd, "kimodell") || starts_with(cmd, "kimodell ")) kimodell_befehl(cmd + 8);
    else if (streq(cmd, "chat") || starts_with(cmd, "chat ")) chat_befehl(cmd + 4);
    else if (streq(cmd, "db") || starts_with(cmd, "db ")) zdb_befehl(cmd + 2);
    else if (streq(cmd, "gedaechtnis") || starts_with(cmd, "gedaechtnis ")) ged_befehl(cmd + 11);
    else if (streq(cmd, "krypto") || starts_with(cmd, "krypto ")) krypto_befehl(cmd + 6);
    else if (streq(cmd, "audio") || starts_with(cmd, "audio ")) audio_befehl(cmd + 5);
    else if (streq(cmd, "sicherheit")) {
        kprint("Startkette: "); kprint((g_boot.flags & BOOT_SIGNIERT) ? "Kernel und Kommandozeile durch LMS-Signatur (SHA-256, quantensicher) geprueft\n" : "NICHT durch eine Signatur geprueft\n");
    }
    else if (streq(cmd, "cas")) { kprint("Vertrauenswuerdige Root-CAs: "); kprint_dec(x509_root_count()); kprint("\n"); }
    else if (starts_with(cmd, "casadd ")) {
        if (!require_cap(CAP_ADMIN,"casadd")) return;
        if (!require_cap(CAP_ADMIN,"casadd")) return;
        const char* n = cmd + 7; while (*n == ' ') n++;
        static uint8_t der[3072]; int r = synfs_read(n, der, sizeof(der));
        if (r > 0 && x509_add_root(der, (uint32_t)r) == 0) kprint("Root-CA hinzugefuegt (nur bis zum Neustart)\n"); else kprint("Fehlgeschlagen (DER-Zertifikat im SynFS, max. 3 KB)\n");
    }
    else if (starts_with(cmd, "dns ")) {
        const char* z = cmd + 4; while (*z == ' ') z++;
        uint32_t ip;
        if (net_dns(z, &ip) == 0) { kprint(z); kprint(" = "); kprint_dec(ip >> 24); kprint("."); kprint_dec((ip >> 16) & 255); kprint("."); kprint_dec((ip >> 8) & 255); kprint("."); kprint_dec(ip & 255); kprint("\n"); }
        else kprint("Name nicht aufloesbar\n");
    }
    else if (starts_with(cmd, "tastatur")) {
        const char* a1 = cmd + 8; while (*a1 == ' ') a1++;
        int w = *a1 ? kbd_find(a1) : -1;
        if (w >= 0) { usb_set_layout(w); zdb_put("system.tastatur", kbd_layouts[w].code); kprint("Tastatur: "); kprint(kbd_layouts[w].code); kprint(" ("); kprint(kbd_layouts[w].name); kprint(")\n"); }
        else {
            int c = usb_get_layout();
            kprint("Tastatur: "); kprint(kbd_layouts[c].code); kprint(" ("); kprint(kbd_layouts[c].name); kprint("). Aendern: tastatur <kuerzel>\nVerfuegbar:");
            for (int i = 0; i < KBD_LAYOUTS; i++) { kprint(" "); kprint(kbd_layouts[i].code); }
            kprint("\n");
        }
    }
    else if (streq(cmd, "mouse")) {
        const struct usb_pointer* m = usb_pointer_state();
        if (!m->present) kprint("Kein Zeiger-/Touch-Ereignis bisher\n");
        else { kprint("Zeiger x="); kprint_dec((uint32_t)m->x); kprint(" y="); kprint_dec((uint32_t)m->y);
               kprint(" Tasten="); kprint_dec(m->buttons); kprint(m->down ? " gedrueckt" : " frei");
               kprint(" Ereignisse="); kprint_dec(m->events); kprint("\n"); }
    }
    else if (starts_with(cmd, "synmount ")) {
        if (!require_cap(CAP_ADMIN,"synmount")) return;
        const char* n = cmd+9; while (*n==' ') n++;
        synfs_sync();
        if (block_select(n)==0) {
            if (synfs_mount()==0) {
                kprint("SynFS liegt jetzt auf diesem Geraet\n");
                benutzer_laden();
                if (benutzer_gesperrt()) login_start();
            }
        }
        else kprint("Unbekanntes Geraet (siehe 'block')\n");
    }
    else if (starts_with(cmd, "synformat ")) {
        if (!require_cap(CAP_ADMIN,"synformat")) return;
        const char* p = cmd+10; while (*p==' ') p++;
        char dev[16]; int di=0;
        while (*p && *p!=' ' && di<15) dev[di++]=*p++;
        dev[di]=0; while (*p==' ') p++;
        if (!streq(p, "ja")) {
            kprint("ACHTUNG: schreibt SynFS an den Anfang des Datentraegers und zerstoert dort alles.\n");
            kprint("Zum Bestaetigen: synformat <dev> ja\n");
        } else if (streq(dev, "ramdisk")) kprint("Die Ramdisk wird beim Start formatiert.\n");
        else {
            synfs_sync();
            if (block_select(dev)==0 && synfs_format("zynex-synfs")==0 && synfs_mount()==0) {
                kprint("Formatiert und eingebunden\n");
                synbin_install_embedded();
            } else kprint("Fehlgeschlagen\n");
        }
    }
    else if (starts_with(cmd, "synrun ")) {
        if (!require_cap(CAP_TASK,"synrun")) return;
        const char* n = cmd+7; while (*n==' ') n++;
        synbin_run(n);
    }
    else if (starts_with(cmd, "synhdr ")) {
        if (!require_cap(CAP_FS,"synhdr")) return;
        const char* n = cmd+7; while (*n==' ') n++;
        synbin_info(n);
    }
    else if (starts_with(cmd, "syngrant ")) {
        if (!require_cap(CAP_ADMIN,"syngrant")) return;
        const char* n = cmd+9; while (*n==' ') n++;
        if (*n>='0' && *n<='3') { synbin_set_allowed((uint8_t)(*n-'0')); kprint("Erlaubt: "); kputchar(*n); kprint("\n"); }
        else kprint("Usage: syngrant <0-3>  (1=Konsole, 2=Timer)\n");
    }
    else if (streq(cmd, "installer")) {
        if (!require_cap(CAP_ADMIN,"installer")) return;
        if (!installer_bereit()) kprint("Nur von der Installations-ISO aus moeglich (kein Installationspaket geladen)\n");
        else if (gui_start() != 0) kprint("Kein Grafikmodus - nutze: install <platte> <benutzername>\n");
    }
    else if (starts_with(cmd, "install")) {
        if (!require_cap(CAP_ADMIN,"install")) return;
        const char* p = cmd + 7; while (*p == ' ') p++;
        char teil[4][20]; int tn = 0;
        while (*p && tn < 4) { int k = 0; while (*p && *p != ' ' && k < 19) teil[tn][k++] = *p++; teil[tn][k] = 0; tn++; while (*p == ' ') p++; }
        if (!installer_bereit()) kprint("Nur von der Installations-ISO aus moeglich (kein Installationspaket geladen)\n");
        else if (tn < 2) kprint("Usage: install <platte> <benutzername> [kuerzel] [ja]   (Platten: siehe 'block')\n");
        else {
            kopiere(icfg.disk, teil[0], 16); kopiere(icfg.user, teil[1], 16);
            kopiere(icfg.layout, tn >= 3 && kbd_find(teil[2]) >= 0 ? teil[2] : "ch", 4);
            icfg.daten_mib = 0;
            const char* fehler;
            if (installer_pruefe_platte(icfg.disk, &fehler)) { kprint(fehler); kprint("\n"); }
            else if (!(tn >= 3 && streq(teil[tn - 1], "ja"))) {
                kprint("ACHTUNG: Alle Daten auf '"); kprint(icfg.disk); kprint("' werden geloescht.\n");
                kprint("Zum Bestaetigen: install "); kprint(icfg.disk); kprint(" "); kprint(icfg.user); kprint(" "); kprint(icfg.layout); kprint(" ja\n");
            } else shell_frage("Passwort fuer den Administrator: ", 1, inst_pw1);
        }
    }
    else if (streq(cmd, "reboot")) {
        if (!require_cap(CAP_ADMIN,"reboot")) return;
        synfs_sync();
        kprint("Neustart ...\n");
        system_neustart();
    }
    else if (streq(cmd, "halt")) {
        if (!require_cap(CAP_ADMIN,"halt")) return;
        synfs_sync();
        kprint("Halted.\n"); for (;;) __asm__ volatile("cli;hlt");
    }
    else if (starts_with(cmd, "echo ")) { kprint(cmd+5); kprint("\n"); }
    else if (starts_with(cmd, "create ")) {
        if (!require_cap(CAP_FS,"create")) return;
        const char* n=cmd+7; while(*n==' ') n++;
        if (ramfs_create(n)==0) kprint("OK\n"); else kprint("Fail\n");
    }
    else if (starts_with(cmd, "write ")) {
        if (!require_cap(CAP_FS,"write")) return;
        const char* p=cmd+6; while(*p==' ') p++;
        char name[32]; int ni=0;
        while(*p && *p!=' ' && ni<31) name[ni++]=*p++;
        name[ni]=0; while(*p==' ') p++;
        if (name[0] && *p) {
            uint32_t len=0; const char* s=p; while(*s++) len++;
            if (ramfs_write(name,p,len)>=0) kprint("OK\n"); else kprint("Fail\n");
        }
    }
    else if (starts_with(cmd, "cat ")) {
        if (!require_cap(CAP_FS,"cat")) return;
        const char* n=cmd+4; while(*n==' ') n++;
        char buf[512]; int r=ramfs_read(n,buf,sizeof(buf)-1);
        if (r>=0) { buf[r]=0; kprint(buf); kprint("\n"); } else kprint("Not found\n");
    }
    else if (starts_with(cmd, "rm ")) {
        if (!require_cap(CAP_FS,"rm")) return;
        const char* n=cmd+3; while(*n==' ') n++;
        if (ramfs_delete(n)==0) kprint("Deleted\n"); else kprint("Fail\n");
    }
    else unbekannt(cmd);
}

static void process_command(void) {
    input_buf[input_pos] = 0;
    char* cmd = input_buf;
    while (*cmd == ' ') cmd++;
    if (!*cmd) return;
    if (streq(cmd, "hilfe alt") || streq(cmd, "help alt") || streq(cmd, "help")) { cmd_help(); return; }
    if (starts_with(cmd, "hilfe") || starts_with(cmd, "help ") || streq(cmd, "?")) { befehl_hilfe(cmd[0] == '?' ? "" : cmd + (cmd[1] == 'i' ? 5 : 4)); return; }
    static char neu[160];
    if (befehl_uebersetze(cmd, neu, sizeof(neu))) {
        char* p = neu;
        while (*p) { char* e = p; while (*e && *e != ';') e++; char sav = *e; *e = 0; run_legacy(p); if (!sav) break; p = e + 1; }
        return;
    }
    if (befehl_bereich(cmd)) { kprint("Diese Aktion gibt es nicht. Moeglich ist:\n"); befehl_hilfe(cmd); return; }
    run_legacy(cmd);
}

static void (*grab_fn)(char) = 0;
static void (*tick_fn)(void) = 0;
void shell_set_grab(void (*f)(char)) { grab_fn = f; }
void shell_set_tick(void (*f)(void)) { tick_fn = f; }

void shell_handle_key(char c) {
    if (grab_fn) { grab_fn(c); return; }
    if (frage.aktiv) {                                       /* Rueckfrage: Zeile geht an die Funktion, Passwoerter werden als * gezeigt */
        if (c=='\n'||c=='\r') {
            kputchar('\n');
            char zeile[SHELL_BUF_SIZE]; kopiere(zeile, input_buf, SHELL_BUF_SIZE);
            wische(input_buf, SHELL_BUF_SIZE); input_pos = 0;
            void (*cb)(const char*) = frage.cb; frage.aktiv = 0;
            cb(zeile); wische(zeile, SHELL_BUF_SIZE);
            if (!frage.aktiv) shell_prompt();
        } else if (c=='\b') {
            if (input_pos>0) { input_pos--; input_buf[input_pos]=0; kputchar('\b'); kputchar(' '); kputchar('\b'); }
        } else if (((c>=32 && c<127) || (unsigned char)c>=0xA0) && input_pos < SHELL_BUF_SIZE-1) {
            input_buf[input_pos++]=c; input_buf[input_pos]=0; kputchar(frage.maskiert ? '*' : c);
        }
        return;
    }
    if (c=='\t') {
        char liste[200]; int alt = input_pos; int n = befehl_tab(input_buf, &input_pos, SHELL_BUF_SIZE, liste, sizeof(liste));
        for (int i = alt; i < input_pos; i++) kputchar(input_buf[i]);
        if (n > 1) { kputchar('\n'); kprint("  "); kprint(liste); kputchar('\n'); shell_prompt(); kprint(input_buf); }
        return;
    }
    if (c=='\n'||c=='\r') {
        kputchar('\n'); process_command();
        input_pos=0; input_buf[0]=0; if (!frage.aktiv) shell_prompt(); return;
    }
    if (c=='\b') {
        if (input_pos>0) {
            input_pos--; input_buf[input_pos]=0;
            kputchar('\b'); kputchar(' '); kputchar('\b');
        }
        return;
    }
    if (((c>=32 && c<127) || (unsigned char)c>=0xA0) && input_pos < SHELL_BUF_SIZE-1) {
        input_buf[input_pos++]=c; input_buf[input_pos]=0; kputchar(c);
    }
}

void shell_wartung(void) { kprint("\n[WARTUNG] Text-Konsole\n"); shell_prompt(); }

void shell_init(void) {
    input_pos=0; input_buf[0]=0;
    for (const char* c = g_boot.cmdline; *c; c++)                       /* installer=gui im Kernel-Aufruf: Installations-Assistent starten */
        if (c[0]=='i' && c[1]=='n' && c[2]=='s' && c[3]=='t' && c[4]=='a' && c[5]=='l' && c[6]=='l' && c[7]=='e' && c[8]=='r' && c[9]=='=' && c[10]=='g') {
            if (installer_bereit() && gui_start() == 0) return;
            break;
        }
    for (const char* c = g_boot.cmdline; *c; c++)                       /* installiertes System mit Bildschirm: Desktop statt Text-Shell */
        if (c[0]=='m' && c[1]=='o' && c[2]=='d' && c[3]=='u' && c[4]=='s' && c[5]=='=' && c[6]=='s') { if (desktop_start() == 0) return; break; }
    kprint("\n"); cmd_about();
    kprint("\nTippe 'hilfe' oder die ersten Buchstaben und Tab.\n\n");
    if (benutzer_gesperrt()) login_start(); else shell_prompt();
}

void shell_run(void) {
    for (;;) { if (hv_aktiv()) __asm__ volatile("pause"); else __asm__ volatile("hlt"); usb_poll(); net_poll(); if (tick_fn) tick_fn(); usb_hotplug(); }
}
