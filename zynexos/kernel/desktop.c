/*
 * Zynex OS Quantum - Startbild und Desktop (Framebuffer, Maus + Tastatur)
 * Kein Startmenue, keine Programmliste: Mitte = Zynie (der Roboter), unten die Eingabeleiste. Was eingegeben wird, erkennt Zynie als Absicht
 * (Intent) und oeffnet das Passende: "text erstellen" -> Textverarbeitung, "rechne 12*7" -> Ergebnis, "system" -> Zustand usw.
 * Sprachen: Deutsch und Englisch (die gespeicherte Sprache de/en, alle anderen zeigen Englisch).
 */
#include "fw.h"
#include "krypto.h"
#include "block.h"
#include "benutzer.h"
#include "zyndb.h"
#include "layouts.h"
#include "extki.h"
#include "chats.h"
#include "gedaechtnis.h"
#include "synfs.h"
#include "desktop.h"
#include "hyperv.h"
#include "gehirn.h"
#include "benutzer.h"
#include "synfs.h"
#include "usb.h"
#include "timer.h"
#include "pmm.h"
#include "net.h"
#include "qubit.h"
#include "multiboot.h"
#include "shell.h"
#include "installer.h"

extern void kprint(const char* str);
extern void kprint_dec(uint32_t val);
extern int  gfx_available(void);
extern void gfx_size(int* w, int* h);
extern void gfx_console(int an);
extern void gfx_begin(void);
extern void gfx_end(void);
extern uint32_t* gfx_fbptr(uint32_t* pitch_px);
static int gdepth;
static void gb_(void) { if (!gdepth++) gfx_begin(); }
static void ge_(void) { if (--gdepth == 0) gfx_end(); }
#define gfx_begin gb_
#define gfx_end ge_
extern void gfx_rect(int x, int y, int w, int h, uint32_t c);
extern void gfx_bitmap(int x, int y, int w, int h, const uint32_t* px);
extern void gfx_text(int x, int y, const char* s, uint32_t fg, int scale);
extern int  gfx_text_w(const char* s, int scale);
extern const int zynex_logo_w, zynex_logo_h, zynex_logo_gross_w, zynex_logo_gross_h;
extern const uint32_t zynex_logo[], zynex_logo_gross[];

#define C_BG     0x000A111Fu
#define C_CARD   0x00121A2Bu
#define C_BORDER 0x002A3752u
#define C_TEXT   0x00E9EDF5u
#define C_MUTED  0x008995B0u
#define C_ACC    0x004A72F5u
#define C_ACC2   0x006F90FBu
#define C_CYAN   0x0000F2FEu
#define C_VIOLET 0x009D4EDDu
#define C_OK     0x0034D399u
#define C_ERR    0x00EF4444u
#define C_FIELD  0x000E1525u
#define C_OFF    0x00334155u

static int W, H, lang;                                    /* lang: 0 Deutsch, 1 Englisch, 2 Franzoesisch, 3 Italienisch, 4 Spanisch */
static const char* const LNAME[5] = { "Deutsch", "English", "Francais", "Italiano", "Espanol" };
static const char* const LCODE[5] = { "de", "en", "fr", "it", "es" };
static const char* const LENG[5] = { "German", "English", "French", "Italian", "Spanish" };
/* Eingebaute Uebersetzungen (Schluessel: der englische Text). Alles andere holt ZynDB (vom KI-Modell erzeugt) oder bleibt englisch. */
static const struct { const char* en, *fr, *it, *es; } UEB[] = {
    { "User", "Utilisateur", "Utente", "Usuario" }, { "Password", "Mot de passe", "Password", "Contrasena" },
    { "Sign-in failed", "Echec de la connexion", "Accesso non riuscito", "Error de inicio de sesion" },
    { "Continue with Enter", "Continuer avec Entree", "Continua con Invio", "Continuar con Intro" },
    { "Wrong password", "Mot de passe incorrect", "Password errata", "Contrasena incorrecta" },
    { "Unlock disk (password)", "Deverrouiller le disque (mot de passe)", "Sblocca il disco (password)", "Desbloquear disco (contrasena)" },
    { "Your data is encrypted. Continue with Enter", "Vos donnees sont chiffrees. Continuer avec Entree", "I tuoi dati sono cifrati. Continua con Invio", "Tus datos estan cifrados. Continuar con Intro" },
    { "Hello! I am Zynie. What would you like to do?", "Bonjour ! Je suis Zynie. Que voulez-vous faire ?", "Ciao! Sono Zynie. Cosa vuoi fare?", "Hola! Soy Zynie. Que quieres hacer?" },
    { "I did not understand that yet.", "Je n'ai pas encore compris.", "Non ho ancora capito.", "Aun no lo he entendido." },
    { "One moment, thinking ...", "Un instant, je reflechis ...", "Un momento, sto pensando ...", "Un momento, estoy pensando ..." },
    { "Document", "Document", "Documento", "Documento" }, { "Calculator", "Calculatrice", "Calcolatrice", "Calculadora" },
    { "System info", "Infos systeme", "Info di sistema", "Info del sistema" }, { "Save", "Enregistrer", "Salva", "Guardar" },
    { "New", "Nouveau", "Nuovo", "Nuevo" }, { "Close", "Fermer", "Chiudi", "Cerrar" }, { "New document", "Nouveau document", "Nuovo documento", "Nuevo documento" },
    { "Tasks", "Taches", "Attivita", "Tareas" }, { "Agents", "Agents", "Agenti", "Agentes" }, { "Nothing open.", "Rien d'ouvert.", "Niente di aperto.", "Nada abierto." },
    { "Run", "Lancer", "Avvia", "Ejecutar" }, { "Approve", "Approuver", "Approva", "Aprobar" }, { "Reject", "Refuser", "Rifiuta", "Rechazar" },
    { "All done", "Tout termine", "Tutto fatto", "Todo hecho" }, { "Esc: close window", "Echap : fermer la fenetre", "Esc: chiudi la finestra", "Esc: cerrar la ventana" },
    { "Free memory: ", "Memoire libre : ", "Memoria libera: ", "Memoria libre: " }, { "Uptime: ", "Duree de fonctionnement : ", "Tempo di attivita: ", "Tiempo activo: " },
    { "Network: ", "Reseau : ", "Rete: ", "Red: " }, { "no card", "pas de carte", "nessuna scheda", "sin tarjeta" },
    { "Language model ready (runs locally)", "Modele de langage pret (local)", "Modello linguistico pronto (locale)", "Modelo de lenguaje listo (local)" },
    { "no language model loaded", "aucun modele de langage", "nessun modello linguistico", "ningun modelo de lenguaje" },
    { "Next workspace", "Bureau suivant", "Spazio successivo", "Siguiente escritorio" }, { "Previous workspace", "Bureau precedent", "Spazio precedente", "Escritorio anterior" },
    { "Close widget", "Fermer le widget", "Chiudi widget", "Cerrar widget" }, { "Settings", "Parametres", "Impostazioni", "Ajustes" },
    { "Cut", "Couper", "Taglia", "Cortar" }, { "Copy", "Copier", "Copia", "Copiar" }, { "Paste", "Coller", "Incolla", "Pegar" }, { "Select all", "Tout selectionner", "Seleziona tutto", "Seleccionar todo" }, { "Clear", "Effacer", "Cancella", "Borrar" },
    { "What should I remember?", "Que dois-je retenir ?", "Cosa devo ricordare?", "Que debo recordar?" },
    { "Noted. I will not forget that.", "C'est note. Je ne l'oublierai pas.", "Preso nota. Non lo dimentichero.", "Anotado. No lo olvidare." },
    { "I never remember passwords, keys or account numbers.", "Je ne retiens jamais les mots de passe, cles ou numeros de compte.", "Non ricordo mai password, chiavi o numeri di conto.", "Nunca recuerdo contrasenas, claves ni numeros de cuenta." },
    { "I could not save that.", "Je n'ai pas pu l'enregistrer.", "Non sono riuscito a salvarlo.", "No pude guardarlo." },
    { "I have cleared my whole memory.", "J'ai efface toute ma memoire.", "Ho cancellato tutta la mia memoria.", "He borrado toda mi memoria." },
    { "Really everything? Say: forget all yes", "Vraiment tout ? Dis : oublie tout oui", "Proprio tutto? Di': dimentica tutto si", "Realmente todo? Di: olvida todo si" },
    { " memory(ies) forgotten.", " souvenir(s) oublie(s).", " ricordo/i dimenticato/i.", " recuerdo(s) olvidado(s)." },
    { "I found nothing about that.", "Je n'ai rien trouve a ce sujet.", "Non ho trovato nulla al riguardo.", "No encontre nada sobre eso." },
    { "This is what I know:", "Voici ce que je sais :", "Ecco cosa so:", "Esto es lo que se:" },
    { "Nothing yet. Say for example: remember that my dog is called Bello.", "Rien pour l'instant. Dis par exemple : souviens-toi que mon chien s'appelle Bello.", "Ancora niente. Di ad esempio: ricorda che il mio cane si chiama Bello.", "Aun nada. Di por ejemplo: recuerda que mi perro se llama Bello." },
};
#define NUEB ((int)(sizeof(UEB) / sizeof(UEB[0])))
static char pend_en[48][100]; static int npend;
static int ueq(const char* a, const char* b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }
static void ui_key(int l, const char* en, char* key) {
    key[0] = 'u'; key[1] = 'i'; key[2] = '.'; key[3] = LCODE[l][0]; key[4] = LCODE[l][1]; key[5] = '.';
    uint32_t h = 2166136261u; for (const char* p = en; *p; p++) { h ^= (uint8_t)*p; h *= 16777619u; }
    for (int i = 0; i < 8; i++) key[6 + i] = "0123456789abcdef"[(h >> (28 - 4 * i)) & 15]; key[14] = 0;
}
static const char* uebers(const char* en) {
    for (int i = 0; i < NUEB; i++) if (ueq(UEB[i].en, en)) return lang == 2 ? UEB[i].fr : lang == 3 ? UEB[i].it : UEB[i].es;
    char key[16]; ui_key(lang, en, key);
    const char* v = zdb_get(key); if (v && *v) return v;
    if (npend < 48) { int da = 0; for (int i = 0; i < npend; i++) if (ueq(pend_en[i], en)) { da = 1; break; } if (!da) { int k = 0; while (en[k] && k < 99) { pend_en[npend][k] = en[k]; k++; } pend_en[npend][k] = 0; npend++; } }
    return en;
}
static const char* TR(const char* de, const char* en) { return lang == 0 ? de : lang == 1 ? en : uebers(en); }
static void lang_setze(int l) { if (l < 0 || l > 4) return; lang = l; zdb_put("system.sprache", LCODE[l]); }
static int ki_nutzbar(void) { return gehirn_vorhanden() || ext_quelle(); }     /* die interne KI laesst sich nicht abschalten */
/* Text in allen fuenf Sprachen */
static const char* T5(const char* de, const char* en, const char* fr, const char* it, const char* es) { return lang == 0 ? de : lang == 1 ? en : lang == 2 ? fr : lang == 3 ? it : es; }

static int slen(const char* s) { int n = 0; while (s[n]) n++; return n; }
static void scopy(char* d, const char* s, int max) { int i = 0; while (s[i] && i < max - 1) { d[i] = s[i]; i++; } d[i] = 0; }
static void cat(char* d, const char* s, int max) { int n = slen(d); scopy(d + n, s, max - n); }
static void dec(char* out, uint32_t v) { char t[12]; int n = 0; if (!v) t[n++] = '0'; while (v) { t[n++] = (char)('0' + v % 10); v /= 10; } int k = 0; while (n) out[k++] = t[--n]; out[k] = 0; }
static int isq(int v) { int r = 0; while ((r + 1) * (r + 1) <= v) r++; return r; }

/* UTF-8 -> Latin-1 fuer die Schrift */
static const char* l1(const char* s) {
    static char buf[4][240]; static int bi;
    char* o = buf[bi = (bi + 1) & 3]; int n = 0;
    for (int i = 0; s[i] && n < 238; i++) {
        unsigned char c = (unsigned char)s[i], d = (unsigned char)s[i + 1];
        if ((c == 0xC3 || c == 0xC2) && d >= 0x80 && d <= 0xBF) { o[n++] = (char)(c == 0xC3 ? d + 0x40 : d); i++; }
        else o[n++] = (char)c;
    }
    o[n] = 0; return o;
}
static void gtxt(int x, int y, const char* s, uint32_t c, int sc) { gfx_text(x, y, l1(s), c, sc); }
static int gtw(const char* s, int sc) { return gfx_text_w(l1(s), sc); }

static void rrect(int x, int y, int w, int h, int r, uint32_t c) {
    if (w <= 0 || h <= 0) return;
    if (r * 2 > h) r = h / 2;
    if (r * 2 > w) r = w / 2;
    gfx_rect(x, y + r, w, h - 2 * r, c);
    for (int j = 0; j < r; j++) {
        int dy = r - j, in = r - isq(r * r - dy * dy + 1); if (in < 0) in = 0;
        gfx_rect(x + in, y + j, w - 2 * in, 1, c); gfx_rect(x + in, y + h - 1 - j, w - 2 * in, 1, c);
    }
}
static void rrect_rand(int x, int y, int w, int h, int r, uint32_t fill, uint32_t rand) { rrect(x, y, w, h, r, rand); rrect(x + 1, y + 1, w - 2, h - 2, r - 1 > 0 ? r - 1 : 0, fill); }

static uint32_t mix(uint32_t a, uint32_t b, int t /*0..256*/) {
    uint32_t r = 0;
    for (int s = 0; s <= 16; s += 8) { int x = (int)((a >> s) & 255), y = (int)((b >> s) & 255); r |= (uint32_t)(x + (y - x) * t / 256) << s; }
    return r;
}

/* ---------- Ladeanzeige "Quanten-Orbit": ein pulsierender Kern, drei Elektronen auf gekippten Bahnen mit Schweif ---------- */
static const int sin90t[19] = { 0, 89, 178, 265, 350, 433, 512, 587, 658, 724, 784, 839, 887, 928, 962, 989, 1008, 1020, 1024 };
static int isin(int deg) {
    deg %= 360; if (deg < 0) deg += 360;
    int sg = 1; if (deg >= 180) { deg -= 180; sg = -1; } if (deg > 90) deg = 180 - deg;
    int lo = sin90t[deg / 5], hi = sin90t[deg / 5 + 1 > 18 ? 18 : deg / 5 + 1];
    return sg * (lo + (hi - lo) * (deg % 5) / 5);
}
static int icos(int deg) { return isin(deg + 90); }
static void lader(int cx, int cy, int R, uint32_t fr, uint32_t bg) {
    static const uint32_t farbe[3] = { 0x0022D3EEu, 0x00A78BFAu, 0x006F90FBu };
    gfx_begin();
    gfx_rect(cx - R - 8, cy - R - 8, 2 * R + 16, 2 * R + 16, bg);
    int puls = isin((int)fr * 9), kr = R / 5 + R / 14 * puls / 1024; if (kr < 3) kr = 3;
    uint32_t kern = mix(0x0022D3EEu, 0x00A78BFAu, (isin((int)fr * 4) + 1024) * 255 / 2048);
    for (int pass = 0; pass < 2; pass++) {
        if (pass == 1) {
            rrect(cx - kr - 5, cy - kr - 5, 2 * kr + 10, 2 * kr + 10, kr + 5, mix(bg, kern, 50));
            rrect(cx - kr - 2, cy - kr - 2, 2 * kr + 4, 2 * kr + 4, kr + 2, mix(bg, kern, 120));
            rrect(cx - kr, cy - kr, 2 * kr, 2 * kr, kr, kern);
            rrect(cx - kr / 2, cy - kr / 2 - 1, kr, kr, kr / 2, 0x00FFFFFFu);
        }
        for (int e = 0; e < 3; e++) {
            int tilt = e * 60;
            for (int k = 13; k >= 0; k--) {
                int ang = (int)fr * (e == 1 ? -11 : 9) + e * 120 - k * 11 * (e == 1 ? -1 : 1);
                int x0 = R * icos(ang) / 1024, y0 = R * 4 / 10 * isin(ang) / 1024;
                int x = (x0 * icos(tilt) - y0 * isin(tilt)) / 1024, y = (x0 * isin(tilt) + y0 * icos(tilt)) / 1024;
                if ((y >= 0) != (pass == 1)) continue;
                int sz = R / 7 + 2 - k * (R / 7) / 16; if (sz < 2) sz = 2;
                int tt = 255 - k * 255 / 15;
                rrect(cx + x - sz / 2, cy + y - sz / 2, sz, sz, sz / 2, mix(bg, farbe[e], tt));
            }
        }
    }
    gfx_end();
}

/* ---------- Startbild ---------- */
static int splash_an;
static const char* const qname[4] = { "Q0:ENT", "Q1:SEM", "Q2:CAP", "Q3:AI" };
static int splash_q[4];
static int splash_p;
static char splash_t[64];
static char splash_info[400];

static void splash_zeichne(void) {
    if (!splash_an) return;
    gfx_begin();
    gfx_rect(0, 0, W, H, C_BG);
    int lx = (W - zynex_logo_gross_w) / 2, ly = H / 2 - zynex_logo_gross_h / 2 - 70;
    gfx_bitmap(lx, ly, zynex_logo_gross_w, zynex_logo_gross_h, zynex_logo_gross);
    int y = ly + zynex_logo_gross_h + 28;
    for (int i = 0; i < 4; i++) {
        int cx = W / 2 - 150 + i * 100;
        rrect(cx - 16, y, 32, 32, 16, splash_q[i] ? C_CYAN : C_OFF);
        rrect(cx - 13, y + 3, 26, 26, 13, splash_q[i] ? 0x00063A48u : C_FIELD);
        rrect(cx - 4, y + 12, 8, 8, 4, splash_q[i] ? 0x00B0FBFFu : 0x00475569u);
        gfx_text(cx - 24, y + 38, qname[i], splash_q[i] ? C_CYAN : 0x00647488u, 1);
    }
    int bw = 360, bx = (W - bw) / 2, by = y + 66;
    rrect(bx, by, bw, 8, 4, C_FIELD);
    int fw = bw * splash_p / 100; if (fw > 0) rrect(bx, by, fw < 8 ? 8 : fw, 8, 4, mix(C_CYAN, C_VIOLET, splash_p * 256 / 100));
    lader(W - 90, 90, 36, timer_get_ticks() / 4, C_BG);
    gfx_text(bx, by + 16, splash_t, C_CYAN, 1);
    char pc[8]; dec(pc, (uint32_t)splash_p); cat(pc, "%", 8);
    gfx_text(bx + bw - gfx_text_w(pc, 1), by + 16, pc, C_MUTED, 1);
    { int mc = (W - 40) / 8; if (mc > 120) mc = 120; int n = 0; while (splash_info[n]) n++;
      for (int z = 0, pos = 0; z < 4 && pos < n; z++) { char t[130]; int k = 0; while (k < mc && pos < n) t[k++] = splash_info[pos++]; t[k] = 0; gfx_text(20, H - 90 + z * 18, t, 0x00647488u, 1); } }
    gfx_end();
}
void desktop_splash_info(const char* text) {
    int i = 0; while (text[i] && i < 399) { splash_info[i] = text[i]; i++; } splash_info[i] = 0;
    if (splash_an) splash_zeichne();
}
void desktop_splash_start(void) {
    if (!gfx_available()) return;
    gfx_size(&W, &H);
    splash_an = 1; splash_p = 0; scopy(splash_t, "UEFI", 64); for (int i = 0; i < 4; i++) splash_q[i] = 0;
    gfx_console(0);
    splash_zeichne();
}
void desktop_splash(int qubit, int prozent, const char* text) {
    if (!splash_an) return;
    if (qubit >= 0 && qubit < 4) splash_q[qubit] = 1;
    splash_p = prozent; scopy(splash_t, text, 64);
    splash_zeichne();
}
void desktop_splash_ende(void) {
    if (!splash_an) return;
    uint32_t t0 = timer_get_ticks();
    { uint32_t lf = 0; for (int i = 0; i < 2000000 && timer_get_ticks() - t0 < 90; i++) { __asm__ volatile ("pause"); if (timer_get_ticks() / 4 != lf) { lf = timer_get_ticks() / 4; lader(W - 90, 90, 36, lf, C_BG); } } }
    splash_an = 0;
}

/* ---------- Zustand ---------- */
enum { Z_LOGIN, Z_DESK, Z_EDIT, Z_WEB, Z_RECH, Z_INFO };
static int zustand;
static char fname[16], fpw[40]; static int ffocus, fmsg;
static char eingabe[96]; static int einlen;
static char antwort[6][100]; static int nantwort;
#define QN 32
static uint16_t kq[QN]; static unsigned qh, qt; static int was_down;
static struct { int x, y, w, h, id, owner; } knoepfe[120]; static int nkn;
static uint32_t robot_phase;
static uint32_t sprech_bis, letzte_eingabe;               /* Ticks (100 pro Sekunde) */

/* Textverarbeitung */
#define DOK_MAX 4000
static char dok[DOK_MAX + 1]; static int doklen, dokcur, dokscroll, dokgeaendert; static char dokname[24];
static int ed_x, ed_y, ed_w, ed_h, ed_cols, ed_rows, ed_s;

static int kn_owner = -1;
static void kn_reg(int id, int x, int y, int w, int h) { if (nkn < 120) { knoepfe[nkn].owner = kn_owner; knoepfe[nkn].x = x; knoepfe[nkn].y = y; knoepfe[nkn].w = w; knoepfe[nkn].h = h; knoepfe[nkn].id = id; nkn++; } }
static void knopf(int id, const char* label, int x, int y, int w, int primaer) {
    rrect_rand(x, y, w, 34, 10, primaer ? C_ACC : C_CARD, primaer ? C_ACC : C_BORDER);
    gtxt(x + (w - gtw(label, 1)) / 2, y + 9, label, C_TEXT, 1);
    kn_reg(id, x, y, w, 34);
}

static void antwort_setze(const char* a, const char* b, const char* c, const char* d, const char* e, const char* f) {
    const char* t[6] = { a, b, c, d, e, f }; nantwort = 0;
    int zeichen = 0;
    for (int i = 0; i < 6; i++) if (t[i]) { scopy(antwort[nantwort], t[i], 100); zeichen += slen(t[i]); nantwort++; }
    sprech_bis = timer_get_ticks() + (uint32_t)zeichen * 4 + 40; letzte_eingabe = timer_get_ticks();     /* ca. 40 ms je Zeichen */
}
static void reply1(const char* s) { antwort_setze(s, 0, 0, 0, 0, 0); }

/* ---------- Zynie, der Roboter (verschiebbar, skalierbar, spricht, schaut, Ruhemodus) ---------- */
static int zx, zy, zs = 100;                              /* Mitte, Groesse in Prozent */
static int zdrag, zdx, zdy;                               /* 0 nein, 1 verschieben, 2 Groesse aendern */
static int lbx, lby, lbw, lbh;                            /* zuletzt gezeichneter Rahmen */
#define SC(v) ((v) * zs / 100)
#define IDLE_TICKS 4500                                   /* 45 s ohne Eingabe: Ruhemodus */
static int zr_x(void) { return zx - SC(90); }
static int zr_y(void) { return zy - SC(108); }
static int zr_w(void) { return SC(180); }
static int zr_h(void) { return SC(232); }
static int ruhend(void) { return timer_get_ticks() - letzte_eingabe > IDLE_TICKS && timer_get_ticks() > sprech_bis; }

static void zynie(int cx, int cy, uint32_t ph) {
    uint32_t t = timer_get_ticks();
    int spricht = t < sprech_bis, ruhe = ruhend();
    int puls = (int)((ph / 3) % 16); if (puls > 8) puls = 16 - puls;
    int blink = !ruhe && ((ph % 24) == 0 || (ph % 24) == 1);
    uint32_t glow = ruhe ? 0x00206A80u : mix(0x0000A8C8u, C_CYAN, puls * 32);
    /* Blickrichtung: beim Sprechen zur Antwortkarte (rechts/unten), sonst langsam umherschauen, im Ruhemodus geradeaus */
    static const int bx[8] = { 0, 5, 0, -5, 4, -4, 0, 3 }, by[8] = { 0, 0, 3, 0, -2, 2, -3, 1 };
    int k = (int)((t / 150) % 8), ex = ruhe ? 0 : spricht ? 4 : bx[k], ey = ruhe ? 2 : spricht ? 2 : by[k];
    static const int lipp[6] = { 3, 12, 18, 8, 14, 5 };
    int mh = spricht ? lipp[(t / 6) % 6] : (ruhe ? 3 : 4);
    gfx_rect(zr_x(), zr_y(), zr_w(), zr_h(), C_BG);
    rrect(cx - SC(2), cy - SC(92), SC(4), SC(24), SC(2), 0x003B82F6u);                              /* Antenne */
    rrect(cx - SC(8), cy - SC(104), SC(16), SC(16), SC(8), spricht ? C_VIOLET : glow);
    rrect(cx - SC(72), cy - SC(40), SC(12), SC(34), SC(6), 0x001E3A66u); rrect(cx + SC(60), cy - SC(40), SC(12), SC(34), SC(6), 0x001E3A66u);   /* Ohren */
    rrect(cx - SC(60), cy - SC(70), SC(120), SC(100), SC(30), 0x002B3A57u);                         /* Kopf */
    rrect(cx - SC(57), cy - SC(67), SC(114), SC(94), SC(28), 0x00DCE9F8u);
    rrect(cx - SC(46), cy - SC(54), SC(92), SC(62), SC(20), 0x000A1428u);                           /* Visier */
    int eh = blink ? 3 : ruhe ? 8 : 24;
    rrect(cx - SC(30) + SC(ex), cy - SC(36) + SC((24 - eh) / 2 + ey), SC(16), SC(eh) < 2 ? 2 : SC(eh), SC(6), glow);                 /* Augen */
    rrect(cx + SC(14) + SC(ex), cy - SC(36) + SC((24 - eh) / 2 + ey), SC(16), SC(eh) < 2 ? 2 : SC(eh), SC(6), glow);
    int mw = SC(28);                                                                                /* Mund mit Lippen */
    rrect(cx - mw / 2, cy - SC(4) - SC(mh) / 2, mw, SC(mh) + SC(4), SC(5), mix(0x0000A8C8u, C_VIOLET, 140));
    if (mh > 6) rrect(cx - mw / 2 + SC(3), cy - SC(4) - SC(mh) / 2 + SC(2), mw - SC(6), SC(mh) - SC(0), SC(4), 0x00050A14u);
    rrect(cx - SC(40), cy + SC(34), SC(80), SC(62), SC(20), 0x00DCE9F8u);                           /* Koerper */
    rrect(cx - SC(37), cy + SC(37), SC(74), SC(56), SC(18), 0x00B6CCE6u);
    rrect(cx - SC(16), cy + SC(50), SC(32), SC(16), SC(8), 0x000A1428u);
    rrect(cx - SC(12), cy + SC(54), SC(24), SC(8), SC(4), glow);                                    /* Brustleuchte */
    int arm = spricht ? (int)((t / 10) % 6) - 3 : 0;                                                /* Arme gestikulieren leicht */
    rrect(cx - SC(62), cy + SC(42), SC(16), SC(44), SC(8), 0x00DCE9F8u); rrect(cx + SC(46), cy + SC(42 + arm), SC(16), SC(44), SC(8), 0x00DCE9F8u);
    if (ruhe) { gfx_text(cx + SC(66), cy - SC(80), "z", 0x00647488u, 1 + zs / 120); gfx_text(cx + SC(80), cy - SC(96), "Z", 0x00647488u, 1 + zs / 100); }
    int gx = zr_x() + zr_w() - 12, gy = zr_y() + zr_h() - 12;                                         /* Griff zum Vergroessern */
    gfx_rect(gx, gy + 10, 12, 2, 0x00475569u); gfx_rect(gx + 10, gy, 2, 12, 0x00475569u); gfx_rect(gx + 5, gy + 5, 2, 2, 0x00475569u);
}

/* ---------- Anmeldung ---------- */
static void feld(int idx, const char* label, const char* text, int maskiert, int x, int y, int w) {
    gtxt(x, y, label, C_MUTED, 1);
    rrect_rand(x, y + 22, w, 40, 10, C_FIELD, idx == ffocus ? C_ACC : C_BORDER);
    char t[44]; int n = slen(text); if (n > 36) n = 36;
    for (int i = 0; i < n; i++) t[i] = maskiert ? '*' : text[i];
    t[n] = 0;
    gfx_text(x + 14, y + 34, t, C_TEXT, 1);
    if (idx == ffocus) gfx_rect(x + 14 + n * 8, y + 32, 2, 20, C_TEXT);
}
static int entsperr;     /* 1 = die Platte ist verschluesselt und gesperrt: statt der Anmeldung wird das Plattenpasswort abgefragt */
static void zeichne_login(void) {
    gfx_begin();
    gfx_rect(0, 0, W, H, C_BG);
    int cw = 360, cx = (W - cw) / 2, y = H / 2 - 250;
    gfx_bitmap((W - zynex_logo_w) / 2, y, zynex_logo_w, zynex_logo_h, zynex_logo);
    y += zynex_logo_h + 24;
    if (entsperr) {
        feld(0, TR("Festplatte entsperren (Passwort)", "Unlock disk (password)"), fpw, 1, cx, y, cw);
        if (fmsg) gtxt(cx, y + 90, TR("Falsches Passwort", "Wrong password"), C_ERR, 1);
        gtxt(cx, y + 190, TR("Die Daten sind verschluesselt. Weiter mit Enter", "Your data is encrypted. Continue with Enter"), C_MUTED, 1);
        gfx_end(); hv_status_zeichnen(); return;
    }
    feld(0, TR("Benutzer", "User"), fname, 0, cx, y, cw);
    feld(1, TR("Passwort", "Password"), fpw, 1, cx, y + 80, cw);
    if (fmsg) gtxt(cx, y + 160, TR("Anmeldung fehlgeschlagen", "Sign-in failed"), C_ERR, 1);
    gtxt(cx, y + 190, TR("Weiter mit Enter", "Continue with Enter"), C_MUTED, 1);
    gfx_end();
    hv_status_zeichnen();
}

/* ---------- Desktop ---------- */
static const char* const chip_text[6] = { "text erstellen", "rechne 12*7", "system", "dateien", "quanten", "browser" };
static int bar_x, bar_y, bar_w;

/* ---------- Fenster und Widgets (verschiebbar, schliessbar, mehrere gleichzeitig) ---------- */
enum { FT_EDIT = 1, FT_WEB, FT_RECH, FT_INFO, FT_EINST, FT_CHATS, FT_GED };
enum { WG_UHR = 1, WG_SYS, WG_AUF, WG_AGENT };
#define WT_H 30
struct fenster { int typ, x, y, w, h; };
static struct fenster fen[6]; static int nfen, fok;          /* Reihenfolge = Tiefe (das letzte liegt oben); fok: das oberste Fenster hat den Tastaturfokus */
struct widget { int typ, x, y, w, h; };
static struct widget wid[6]; static int nwid;
static void layout_speichern(void) {
    char b[100], a[12]; b[0] = 0;
    for (int j = 0; j < nwid; j++) { dec(a, (uint32_t)wid[j].typ); cat(b, a, 100); cat(b, ",", 100); dec(a, (uint32_t)(wid[j].x + 2000)); cat(b, a, 100); cat(b, ",", 100); dec(a, (uint32_t)(wid[j].y + 2000)); cat(b, a, 100); cat(b, ";", 100); }
    zdb_put("desktop.widgets", b);
    b[0] = 0; dec(a, (uint32_t)zx); cat(b, a, 100); cat(b, " ", 100); dec(a, (uint32_t)zy); cat(b, a, 100); cat(b, " ", 100); dec(a, (uint32_t)zs); cat(b, a, 100);
    zdb_put("desktop.zynie", b);
}
static int lies(const char** p) { while (**p && (**p < '0' || **p > '9')) (*p)++; int v = 0; while (**p >= '0' && **p <= '9') { v = v * 10 + (**p - '0'); (*p)++; } return v; }
static int in_szene;
static int nsug, sugsel = -1;
static int flaeche;
static char alog[10][100]; static int nalog, alauf = -1, pend;
static void aufg_laden(void); static int aufg_zeile(int n, char* out, int max); static int aufg_zahl(void);
static const char* const ag_name[4][2] = { { "Morgen-Briefing", "Morning briefing" }, { "Sicherheits-Agent", "Security agent" }, { "Wartungs-Agent", "Maintenance agent" }, { "Aufgaben-Agent", "Task agent" } };
static void zeichne_einst(void); static void zeichne_chats(void); static void zeichne_ged(void); static void zeichne_desktop(void); static void zeichne_editor(int voll); static void zeichne_web(int voll); static void zeichne_rechner(void); static void zeichne_info(void);
static int fenster_idx(int typ) { for (int i = 0; i < nfen; i++) if (fen[i].typ == typ) return i; return -1; }
static int schneidet(int x, int y, int w, int h, int x2, int y2, int w2, int h2) { return !(x + w <= x2 || x2 + w2 <= x || y + h <= y2 || y2 + h2 <= y); }
static int deckt(int x, int y, int w, int h) {                /* liegt ein Fenster oder Widget ueber diesem Bereich? */
    if (flaeche != 0) return 0;
    for (int i = 0; i < nfen; i++) if (schneidet(x, y, w, h, fen[i].x, fen[i].y, fen[i].w, fen[i].h)) return 1;
    for (int i = 0; i < nwid; i++) if (schneidet(x, y, w, h, wid[i].x, wid[i].y, wid[i].w, wid[i].h)) return 1;
    return 0;
}
static void xor_rahmen(int x, int y, int w, int h) {         /* Umriss beim Ziehen (zweimal zeichnen = loeschen) */
    uint32_t pp; uint32_t* f = gfx_fbptr(&pp); if (!f) return;
    for (int t = 0; t < 3; t++) {
        for (int i = 0; i < w; i++) { int px = x + i; if (px < 0 || px >= W) continue; if (y + t >= 0 && y + t < H) f[(uint64_t)(y + t) * pp + px] ^= 0x00FFFFFFu; if (y + h - 1 - t >= 0 && y + h - 1 - t < H) f[(uint64_t)(y + h - 1 - t) * pp + px] ^= 0x00FFFFFFu; }
        for (int j = 3; j < h - 3; j++) { int py = y + j; if (py < 0 || py >= H) continue; if (x + t >= 0 && x + t < W) f[(uint64_t)py * pp + x + t] ^= 0x00FFFFFFu; if (x + w - 1 - t >= 0 && x + w - 1 - t < W) f[(uint64_t)py * pp + x + w - 1 - t] ^= 0x00FFFFFFu; }
    }
}
/* Ladeanzeige unten rechts, solange das System arbeitet (KI denkt, Sprachpaket wird erzeugt) */
static uint32_t busy_f; static int busy_an;
static void zeichne_desktop(void);
static void busy_tick(void) { uint32_t f = timer_get_ticks() / 5; if (!busy_an || f != busy_f) { busy_an = 1; busy_f = f; lader(W - 70, H - 70, 28, f, C_BG); } }
static void busy_ende(void) { if (!busy_an) return; busy_an = 0; gfx_begin(); gfx_rect(W - 70 - 40, H - 70 - 40, 80, 80, C_BG); gfx_end(); zeichne_desktop(); }
static const char* fenster_titel(int typ) { if (typ == FT_GED) return TR("Gedaechtnis", "Memory"); if (typ == FT_EINST) return TR("Einstellungen", "Settings"); if (typ == FT_CHATS) return "Chats"; return typ == FT_EDIT ? TR("Dokument", "Document") : typ == FT_WEB ? "Browser" : typ == FT_RECH ? TR("Rechner", "Calculator") : TR("System-Info", "System info"); }
static void fenster_zeichnen(int i) {
    struct fenster* f = &fen[i]; int top = (i == nfen - 1 && fok);
    gfx_begin();
    rrect_rand(f->x, f->y, f->w, f->h, 12, C_BG, top ? C_ACC : C_BORDER);
    rrect(f->x + 2, f->y + 2, f->w - 4, WT_H - 2, 10, top ? mix(C_CARD, C_ACC, 50) : C_CARD);
    gtxt(f->x + 16, f->y + 8, fenster_titel(f->typ), top ? C_TEXT : C_MUTED, 1);
    rrect_rand(f->x + f->w - 32, f->y + 5, 22, 20, 6, C_FIELD, C_BORDER); gtxt(f->x + f->w - 25, f->y + 7, "x", C_MUTED, 1);
    kn_owner = i;
    if (f->typ == FT_EDIT) zeichne_editor(1); else if (f->typ == FT_WEB) zeichne_web(1); else if (f->typ == FT_RECH) zeichne_rechner(); else if (f->typ == FT_EINST) zeichne_einst(); else if (f->typ == FT_CHATS) zeichne_chats(); else if (f->typ == FT_GED) zeichne_ged(); else zeichne_info();
    kn_owner = -1;
    gfx_end();
}
static void kn_loesche_owner(int o) { int k = 0; for (int i = 0; i < nkn; i++) if (knoepfe[i].owner != o) knoepfe[k++] = knoepfe[i]; nkn = k; }

/* Widgets */
static void wid_groesse(int typ, int* w, int* h) { *w = typ == WG_UHR ? 230 : 260; *h = typ == WG_UHR ? 110 : typ == WG_SYS ? 120 : 150; }
static void wid_zeichnen(int j) {
    struct widget* g = &wid[j]; char z[100], a[12];
    gfx_begin();
    rrect_rand(g->x, g->y, g->w, g->h, 14, C_CARD, C_BORDER); gfx_rect(g->x + 16, g->y + 1, g->w - 32, 2, C_ACC);
    gtxt(g->x + g->w - 22, g->y + 8, "x", C_MUTED, 1);
    if (g->typ == WG_UHR) {
        uint32_t ymd, hm; char zt[12] = "--:--";
        if (rtc_get(&ymd, &hm) == 0 || hm) { zt[0] = 0; dec(a, hm / 100); if (hm / 100 < 10) cat(zt, "0", 12); cat(zt, a, 12); cat(zt, ":", 12); dec(a, hm % 100); if (hm % 100 < 10) cat(zt, "0", 12); cat(zt, a, 12); }
        gfx_text(g->x + 18, g->y + 22, zt, C_TEXT, 4);
        scopy(z, TR("Laufzeit ", "Uptime "), 100); dec(a, timer_get_seconds() / 3600); cat(z, a, 100); cat(z, " h ", 100); dec(a, timer_get_seconds() / 60 % 60); cat(z, a, 100); cat(z, " min", 100); gtxt(g->x + 18, g->y + 80, z, C_MUTED, 1);
    } else if (g->typ == WG_SYS) {
        uint32_t fr = pmm_get_free_pages() / 256, to = pmm_get_total_pages() / 256; char ip[32] = "";
        gtxt(g->x + 18, g->y + 14, "System", C_MUTED, 1);
        scopy(z, "RAM ", 100); dec(a, fr); cat(z, a, 100); cat(z, TR(" MiB frei", " MiB free"), 100); gtxt(g->x + 18, g->y + 38, z, C_TEXT, 1);
        int bw = g->w - 36, used = to ? (int)((to - fr) * (uint32_t)bw / to) : 0; rrect(g->x + 18, g->y + 62, bw, 8, 4, C_FIELD); if (used > 0) rrect(g->x + 18, g->y + 62, used < 8 ? 8 : used, 8, 4, mix(C_ACC, C_VIOLET, 128));
        int ns = net_status(ip, 32); scopy(z, TR("Netz: ", "Net: "), 100); cat(z, ns == 2 ? ip : ns == 1 ? TR("keine Adresse", "no address") : TR("keine Karte", "no card"), 100); gtxt(g->x + 18, g->y + 86, z, C_MUTED, 1);
    } else if (g->typ == WG_AUF) {
        gtxt(g->x + 18, g->y + 14, TR("Aufgaben", "Tasks"), C_MUTED, 1);
        aufg_laden(); int na = aufg_zahl();
        if (!na) gtxt(g->x + 18, g->y + 40, TR("Nichts offen.", "Nothing open."), C_TEXT, 1);
        for (int n = 0; n < na && n < 5; n++) { char t[100]; if (aufg_zeile(n, t, 100)) { if (slen(t) > 28) t[28] = 0; rrect_rand(g->x + 18, g->y + 42 + n * 20, 10, 10, 3, C_FIELD, C_ACC); gtxt(g->x + 36, g->y + 38 + n * 20, t, C_TEXT, 1); } }
    } else {
        gtxt(g->x + 18, g->y + 14, TR("Agenten", "Agents"), C_MUTED, 1);
        if (alauf < 0) gtxt(g->x + 18, g->y + 40, TR("Noch kein Lauf. Sag: briefing", "No run yet. Say: briefing"), C_TEXT, 1);
        else { gtxt(g->x + 18, g->y + 40, TR(ag_name[alauf][0], ag_name[alauf][1]), C_TEXT, 1); if (nalog) { char t[100]; scopy(t, alog[nalog - 1], 100); if (slen(t) > 30) t[30] = 0; gtxt(g->x + 18, g->y + 64, t, C_MUTED, 1); } if (pend) gtxt(g->x + 18, g->y + 96, TR("Freigabe offen", "Approval open"), 0x00F59E0Bu, 1); }
    }
    gfx_end();
}
static void szene_oben(void) { for (int j = 0; j < nwid; j++) wid_zeichnen(j); for (int i = 0; i < nfen; i++) fenster_zeichnen(i); }
static int zustand_fuer(int typ) { return typ == FT_EDIT ? Z_EDIT : typ == FT_WEB ? Z_WEB : typ == FT_RECH ? Z_RECH : Z_INFO; }   /* Info und Einstellungen teilen sich Z_INFO */
static void fenster_hoch(int i) { struct fenster t = fen[i]; for (int j = i; j < nfen - 1; j++) fen[j] = fen[j + 1]; fen[nfen - 1] = t; }
static void fokus_nach_schliessen(void) { if (nfen > 0) { fok = 1; zustand = zustand_fuer(fen[nfen - 1].typ); } else { fok = 0; zustand = Z_DESK; } }
static void fenster_schliessen_typ(int typ) {
    int i = fenster_idx(typ); if (i < 0) return;
    for (int j = i; j < nfen - 1; j++) fen[j] = fen[j + 1];
    nfen--; fokus_nach_schliessen(); nsug = 0; sugsel = -1; zeichne_desktop();
}
static void fenster_oeffnen(int typ);

static void zeichne_robot(void) {
    if (!in_szene && deckt(zr_x() - 4, zr_y() - 4, zr_w() + 8, zr_h() + 8)) { zeichne_desktop(); return; }
    gfx_begin();
    if (lbw) gfx_rect(lbx, lby, lbw, lbh, C_BG);
    zynie(zx, zy, robot_phase);
    lbx = zr_x(); lby = zr_y(); lbw = zr_w(); lbh = zr_h();
    gfx_end();
}
static int absatz(int x, int y, int w, const char* text, uint32_t c) {
    const char* s = l1(text); static char z[200]; int maxc = w / 8, lines = 0;
    while (*s) {
        int n = 0, last = -1;
        while (s[n] && n < maxc) { if (s[n] == ' ') last = n; n++; }
        if (s[n] && last > 0) n = last;
        for (int i = 0; i < n; i++) z[i] = s[i];
        z[n] = 0; gfx_text(x, y + lines * 20, z, c, 1); lines++;
        s += n; while (*s == ' ') s++;
    }
    return lines;
}
static void zeichne_antwort(void) {
    int cw = bar_w, cx = bar_x, ay = H / 5;
    if (!in_szene && deckt(cx - 10, ay - 6, cw + 20, bar_y - ay - 20)) { zeichne_desktop(); return; }
    gfx_begin();
    gfx_rect(cx - 10, ay - 6, cw + 20, bar_y - ay - 20, C_BG);
    if (nantwort > 0) {
        int hh = 24; for (int i = 0; i < nantwort; i++) hh += 22;
        rrect_rand(cx, ay, cw, hh + 8, 16, C_CARD, C_BORDER);
        int y = ay + 14;
        for (int i = 0; i < nantwort; i++) { int n = absatz(cx + 18, y, cw - 36, antwort[i], i == 0 ? C_TEXT : C_MUTED); y += 22 * (n ? n : 1); }
    }
    gfx_end();
}
/* ---------- Autovorschlaege (ab dem ersten Buchstaben) ---------- */
typedef struct { const char* de; const char* en; int typ; } sug_e;       /* typ 0 Befehl, 1 Frage an Zynie, 2 Verlauf */
static const sug_e katalog[] = {
    { "text erstellen", "write a text", 0 }, { "browser", "browser", 0 }, { "rechner", "calculator", 0 }, { "systeminfo", "system info", 0 }, { "einstellungen", "settings", 0 }, { "sprache englisch", "language german", 0 }, { "widget uhr", "widget clock", 0 }, { "widget system", "widget system", 0 }, { "widget aufgaben", "widget tasks", 0 }, { "widget agenten", "widget agents", 0 }, { "alle fenster schliessen", "close all windows", 0 }, { "browser de.wikipedia.org", "browser en.wikipedia.org", 0 }, { "suche ", "search ", 0 }, { "rechne 12*7", "calc 12*7", 0 }, { "system", "system status", 0 }, { "dateien", "files", 0 },
    { "quanten", "quantum demo", 0 }, { "sicherheit", "security", 0 }, { "firewall", "firewall", 0 }, { "agenten", "agents", 0 }, { "briefing", "briefing", 0 },
    { "agent sicherheit", "agent security", 0 }, { "agent wartung", "agent maintenance", 0 }, { "agent aufgaben", "agent tasks", 0 },
    { "aufgabe ", "task ", 0 }, { "arbeitsflaeche system", "workspace system", 0 }, { "arbeitsflaeche agenten", "workspace agents", 0 },
    { "arbeitsflaeche aufgaben", "workspace tasks", 0 }, { "wuerfel drehen", "rotate cube", 0 }, { "hilfe", "help", 0 },
    { "abmelden", "sign out", 0 }, { "neustart", "restart", 0 }, { "wartung", "maintenance", 0 },
    { "was ist ein qubit?", "what is a qubit?", 1 }, { "was ist verschraenkung?", "what is entanglement?", 1 },
    { "was kann zynex os?", "what can zynex os do?", 1 }, { "wie funktioniert der kernel?", "how does the kernel work?", 1 },
    { "was ist synfs?", "what is synfs?", 1 }, { "wie sicher ist der start?", "how secure is the boot?", 1 },
    { "wer bist du?", "who are you?", 1 }, { "was ist ein betriebssystem?", "what is an operating system?", 1 },
    { "erklaere mir quantencomputer", "explain quantum computers to me", 1 }, { "erzaehle mir einen witz", "tell me a joke", 1 },
    { "schreibe mir ein gedicht", "write me a poem", 1 },
};
#define NKAT ((int)(sizeof(katalog) / sizeof(katalog[0])))
static char hist[8][64]; static int nhist;
static char sug[5][96]; static int sugtyp[5], sugpre[5];
static const char* sug_q;
static int lc(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }
static int praefix(const char* s, const char* q) { for (; *q; q++, s++) if (lc((unsigned char)*s) != lc((unsigned char)*q)) return 0; return 1; }
static int gleich(const char* a, const char* b) { while (*a && *b) { if (lc((unsigned char)*a) != lc((unsigned char)*b)) return 0; a++; b++; } return !*a && !*b; }
static void sug_add(const char* s, int typ, int pre) {
    if (nsug >= 5 || gleich(s, sug_q)) return;
    for (int i = 0; i < nsug; i++) if (gleich(sug[i], s)) return;
    scopy(sug[nsug], s, 96); sugtyp[nsug] = typ; sugpre[nsug] = pre; nsug++;
}
static void sug_suche(void) {
    nsug = 0; sugsel = -1;
    const char* q = eingabe; while (*q == ' ') q++;
    if (!*q) return;
    sug_q = q; int ql = slen(q);
    for (int i = nhist - 1; i >= 0; i--) if (praefix(hist[i], q)) sug_add(hist[i], 2, ql);
    for (int i = 0; i < NKAT; i++) { const char* s = lang == 0 ? katalog[i].de : katalog[i].en; if (praefix(s, q)) sug_add(s, katalog[i].typ, ql); }
    for (int i = 0; i < NKAT; i++) {
        const char* s = lang == 0 ? katalog[i].de : katalog[i].en;
        for (int j = 1; s[j]; j++) if (s[j - 1] == ' ' && praefix(s + j, q)) { sug_add(s, katalog[i].typ, 0); break; }
    }
}
static void hist_add(const char* s) {
    for (int i = 0; i < nhist; i++) if (gleich(hist[i], s)) { for (int j = i; j + 1 < nhist; j++) scopy(hist[j], hist[j + 1], 64); nhist--; break; }
    if (nhist >= 8) { for (int j = 0; j + 1 < nhist; j++) scopy(hist[j], hist[j + 1], 64); nhist--; }
    scopy(hist[nhist++], s, 64);
}

/* ---------- Aufgaben (Datei aufgaben.txt in SynFS) ---------- */
static char aufg[1500]; static int auflen;
static void aufg_laden(void) { int r = zdb_get_text("desktop.aufgaben", aufg, sizeof(aufg) - 1); if (r < 0) r = synfs_read("aufgaben.txt", aufg, sizeof(aufg) - 1); auflen = r > 0 ? r : 0; aufg[auflen] = 0; }
static int aufg_zeile(int n, char* out, int max) {                     /* n-te nichtleere Zeile; 0 = keine */
    int z = 0, i = 0;
    while (i < auflen) {
        int e = i; while (e < auflen && aufg[e] != '\n') e++;
        int leer = 1; for (int j = i; j < e; j++) if (aufg[j] != ' ') leer = 0;
        if (!leer) { if (z == n) { int k = 0; for (int j = i; j < e && k < max - 1; j++) out[k++] = aufg[j]; out[k] = 0; return 1; } z++; }
        i = e + 1;
    }
    return 0;
}
static int aufg_zahl(void) { char t[4]; int n = 0; while (aufg_zeile(n, t, 4)) n++; return n; }
static void aufg_speichern(void) { zdb_put_text("desktop.aufgaben", aufg, auflen); }
static int aufg_add(const char* t) {
    aufg_laden(); int n = slen(t);
    if (auflen + n + 2 >= (int)sizeof(aufg) - 8) return -1;
    for (int i = 0; i < n; i++) aufg[auflen++] = t[i];
    aufg[auflen++] = '\n'; aufg[auflen] = 0; aufg_speichern(); return 0;
}
static void aufg_leeren(void) { aufg[0] = '\n'; auflen = 1; aufg[1] = 0; aufg_speichern(); auflen = 0; aufg[0] = 0; }
static int doks_liste(char (*namen)[24], int max) {
    int n = 0; char b[2];
    for (int i = 1; i < 30 && n < max; i++) { char a[8]; dec(a, (uint32_t)i); scopy(namen[n], "dokument-", 24); cat(namen[n], a, 24); cat(namen[n], ".txt", 24); if (synfs_read(namen[n], b, 1) >= 0) n++; }
    return n;
}

/* ---------- Agenten (lokal im Kernel, Wirkung nach aussen nur nach Freigabe) ---------- */
#define NAG 4
static const char* const ag_desc[NAG][2] = {
    { "Prueft Zeit, Speicher, Netzwerk, Dokumente und Aufgaben und fasst alles zusammen.", "Checks time, memory, network, documents and tasks and sums it up." },
    { "Prueft Startkette, Signatur, Benutzerrolle und Netzwerk.", "Checks boot chain, signature, user role and network." },
    { "Prueft den Speicher und schlaegt vor, das Dateisystem zu sichern (Freigabe noetig).", "Checks memory and proposes to sync the file system (needs approval)." },
    { "Liest deine Aufgaben und schlaegt vor, erledigte zu entfernen (Freigabe noetig).", "Reads your tasks and proposes to clear them (needs approval)." },
};
static void al(const char* s) {
    if (nalog < 10) { scopy(alog[nalog], s, 100); nalog++; }
    kprint("[AGENT] Schritt: "); kprint(s); kprint("\n");
}
static void zeit_text(char* o, uint32_t s) {
    char a[8]; o[0] = 0; dec(a, s / 3600); cat(o, a, 24); cat(o, ":", 24);
    dec(a, s / 60 % 60); if (s / 60 % 60 < 10) cat(o, "0", 24); cat(o, a, 24); cat(o, ":", 24);
    dec(a, s % 60); if (s % 60 < 10) cat(o, "0", 24); cat(o, a, 24);
}
static void ag_start(int i) {
    nalog = 0; pend = 0; alauf = i; char z[100], a[12], b[12], ip[32];
    kprint("[AGENT] Start: "); kprint(ag_name[i][1]); kprint("\n");
    if (i == 0) {
        scopy(z, TR("Laufzeit ", "Uptime "), 100); char t[24]; zeit_text(t, timer_get_seconds()); cat(z, t, 100); al(z);
        scopy(z, TR("Speicher frei: ", "Memory free: "), 100); dec(a, pmm_get_free_pages() / 256); cat(z, a, 100); cat(z, " / ", 100); dec(a, pmm_get_total_pages() / 256); cat(z, a, 100); cat(z, " MiB", 100); al(z);
        int ns = net_status(ip, 32); scopy(z, TR("Netzwerk: ", "Network: "), 100); cat(z, ns == 2 ? ip : ns == 1 ? TR("Karte da, keine Adresse", "card found, no address") : TR("keine Karte", "no card"), 100); al(z);
        char nm[8][24]; int nd = doks_liste(nm, 8); aufg_laden(); int na = aufg_zahl();
        scopy(z, TR("Dokumente: ", "Documents: "), 100); dec(a, (uint32_t)nd); cat(z, a, 100); cat(z, TR(",  offene Aufgaben: ", ",  open tasks: "), 100); dec(b, (uint32_t)na); cat(z, b, 100); al(z);
        al(TR("Briefing fertig.", "Briefing done."));
    } else if (i == 1) {
        al((g_boot.flags & BOOT_SIGNIERT) ? TR("Startkette: durch LMS-Signatur (quantensicher) geprueft", "Boot chain: verified by LMS signature (quantum-safe)") : TR("ACHTUNG: Kernel nicht durch Signatur geprueft", "WARNING: kernel not verified by signature"));
        scopy(z, TR("Angemeldet als ", "Signed in as "), 100); cat(z, benutzer_name(), 100); cat(z, " (", 100); cat(z, benutzer_rollentext(benutzer_rolle()), 100); cat(z, ")", 100); al(z);
        int ns = net_status(ip, 32); al(ns == 2 ? TR("Netzwerk aktiv: nur lokale Dienste sichtbar", "Network active: only local services visible") : TR("Kein Netzwerk: kein Angriff von aussen moeglich", "No network: no attack from outside possible"));
        al((g_boot.flags & BOOT_SIGNIERT) ? TR("Ergebnis: alles in Ordnung.", "Result: all fine.") : TR("Ergebnis: bitte den Kernel pruefen.", "Result: please check the kernel."));
    } else if (i == 2) {
        uint32_t fr = pmm_get_free_pages() / 256, to = pmm_get_total_pages() / 256;
        scopy(z, TR("Speicher: ", "Memory: "), 100); dec(a, fr); cat(z, a, 100); cat(z, TR(" MiB frei von ", " MiB free of "), 100); dec(a, to); cat(z, a, 100); al(z);
        al(fr * 10 > to ? TR("Speicher ist ausreichend.", "Memory is sufficient.") : TR("Speicher wird knapp.", "Memory is getting low."));
        al(TR("Vorschlag: Dateisystem jetzt sichern (SynFS sync).", "Proposal: sync the file system now (SynFS sync)."));
        pend = 1; al(TR("Wartet auf deine Freigabe.", "Waiting for your approval.")); kprint("[AGENT] Freigabe angefragt: Dateisystem sichern\n");
    } else {
        aufg_laden(); int na = aufg_zahl(); scopy(z, TR("Offene Aufgaben: ", "Open tasks: "), 100); dec(a, (uint32_t)na); cat(z, a, 100); al(z);
        for (int n = 0; n < na && n < 5; n++) { char t[90]; if (aufg_zeile(n, t, 90)) { scopy(z, "- ", 100); cat(z, t, 100); al(z); } }
        if (na) { al(TR("Vorschlag: alle Aufgaben als erledigt entfernen.", "Proposal: remove all tasks as done.")); pend = 2; al(TR("Wartet auf deine Freigabe.", "Waiting for your approval.")); kprint("[AGENT] Freigabe angefragt: Aufgaben entfernen\n"); }
        else al(TR("Nichts zu tun.", "Nothing to do."));
    }
}
static void ag_entscheid(int ja) {
    if (!pend) return;
    if (ja) {
        if (pend == 1) { synfs_sync(); al(TR("Freigegeben: Dateisystem gesichert.", "Approved: file system synced.")); }
        else { aufg_leeren(); al(TR("Freigegeben: Aufgaben entfernt.", "Approved: tasks removed.")); }
        kprint("[AGENT] Freigabe erteilt\n");
    } else { al(TR("Abgelehnt: es wurde nichts veraendert.", "Rejected: nothing was changed.")); kprint("[AGENT] Freigabe abgelehnt\n"); }
    pend = 0;
}

/* ---------- Design: Karten ---------- */
enum { K_CHIP0 = 1, K_SUG0 = 8, K_DOT0 = 14, K_PREV = 18, K_NEXT = 19, K_SPEICHERN = 20, K_NEU, K_SCHLIESSEN, K_AG0 = 30, K_FREI = 34, K_ABL, K_NEUDOK, K_AUFLEER };
static const char* const fl_name[4][2] = { { "Zynie", "Zynie" }, { "System", "System" }, { "Agenten", "Agents" }, { "Aufgaben", "Tasks" } };
static void kn_loesche(int lo, int hi) { int k = 0; for (int i = 0; i < nkn; i++) if (knoepfe[i].id < lo || knoepfe[i].id > hi) knoepfe[k++] = knoepfe[i]; nkn = k; }
static void karte(int x, int y, int w, int h, const char* titel) {
    rrect_rand(x, y, w, h, 14, C_CARD, C_BORDER);
    gfx_rect(x + 16, y + 1, w - 32, 2, C_ACC);
    gtxt(x + 18, y + 14, titel, C_MUTED, 1);
}

static void zeichne_eingabe(void) {
    if (!in_szene && deckt(bar_x - 10, bar_y - 6, bar_w + 20, 215)) { zeichne_desktop(); return; }
    gfx_begin();
    kn_loesche(K_CHIP0, K_SUG0 + 5);
    gfx_rect(bar_x - 10, bar_y - 6, bar_w + 20, 215, C_BG);
    rrect_rand(bar_x, bar_y, bar_w, 52, 18, C_FIELD, C_ACC);
    rrect(bar_x + 14, bar_y + 20, 12, 12, 6, C_OK);
    if (einlen == 0) gtxt(bar_x + 40, bar_y + 18, TR("Frag mich alles - schon der erste Buchstabe zeigt Vorschlaege ...", "Ask me anything - the first letter already shows suggestions ..."), 0x00647488u, 1);
    else gfx_text(bar_x + 40, bar_y + 18, eingabe, C_TEXT, 1);
    gfx_rect(bar_x + 40 + einlen * 8, bar_y + 14, 2, 24, C_TEXT);
    if (nsug > 0) {
        int y0 = bar_y + 60;
        rrect_rand(bar_x, y0, bar_w, nsug * 30 + 12, 14, C_CARD, C_BORDER);
        for (int i = 0; i < nsug; i++) {
            int ry = y0 + 6 + i * 30;
            if (i == sugsel) rrect(bar_x + 6, ry, bar_w - 12, 28, 10, mix(C_CARD, C_ACC, 120));
            char a[96]; int pl = sugpre[i] < 95 ? sugpre[i] : 95; for (int j = 0; j < pl; j++) a[j] = sug[i][j]; a[pl] = 0;
            int px = bar_x + 20;
            if (pl) { gtxt(px, ry + 6, a, C_TEXT, 1); px += gtw(a, 1); }
            gtxt(px, ry + 6, sug[i] + pl, i == sugsel ? C_TEXT : C_MUTED, 1);
            const char* tg = sugtyp[i] == 0 ? TR("Befehl", "command") : sugtyp[i] == 1 ? TR("Frage", "question") : TR("Verlauf", "history");
            gtxt(bar_x + bar_w - 20 - gtw(tg, 1), ry + 6, tg, sugtyp[i] == 1 ? 0x00A78BFAu : sugtyp[i] == 0 ? C_ACC2 : C_MUTED, 1);
            kn_reg(K_SUG0 + i, bar_x + 6, ry, bar_w - 12, 28);
        }
    } else {
        int x = bar_x;
        for (int i = 0; i < 6; i++) {
            int w = gtw(chip_text[i], 1) + 24;
            rrect_rand(x, bar_y + 66, w, 30, 15, C_CARD, C_BORDER);
            gtxt(x + 12, bar_y + 73, chip_text[i], C_MUTED, 1);
            kn_reg(K_CHIP0 + i, x, bar_y + 66, w, 30);
            x += w + 8;
        }
        gtxt(bar_x, bar_y + 108, TR("Tab/Pfeile: Vorschlaege   |   F1-F4 oder Bild auf/ab: Arbeitsflaeche drehen", "Tab/arrows: suggestions   |   F1-F4 or PgUp/PgDn: rotate workspace"), 0x00647488u, 1);
    }
    gfx_end();
}

static void zeichne_leiste(void) {
    gfx_begin();
    kn_loesche(K_DOT0, K_NEXT);
    gfx_rect(0, 0, W, 40, C_BG);
    gfx_rect(0, 39, W, 1, C_BORDER);
    gtxt(16, 12, "Zynex OS Quantum", C_ACC2, 1);
    char r[48]; scopy(r, benutzer_name(), 20); cat(r, " (", 48); cat(r, benutzer_rollentext(benutzer_rolle()), 48); cat(r, ")", 48);
    uint32_t ymd, hm; char zt[12] = "";
    if (rtc_get(&ymd, &hm) == 0 || hm) { char a[8]; dec(a, hm / 100); if (hm / 100 < 10) { zt[0] = '0'; zt[1] = 0; } cat(zt, a, 12); cat(zt, ":", 12); dec(a, hm % 100); if (hm % 100 < 10) cat(zt, "0", 12); cat(zt, a, 12); }
    int x = W - 16;
    x -= gtw(r, 1); gtxt(x, 12, r, C_MUTED, 1);
    if (zt[0]) { x -= 16 + gtw(zt, 1); gtxt(x, 12, zt, C_TEXT, 1); }
    /* Wuerfel-Anzeige in der Mitte: < o o o o > Name */
    const char* nm = TR(fl_name[flaeche][0], fl_name[flaeche][1]);
    int tw = 4 * 26 + 80 + gtw(nm, 1), sx = (W - tw) / 2;
    gtxt(sx, 12, "<", C_MUTED, 1); kn_reg(K_PREV, sx - 8, 4, 28, 32);
    for (int i = 0; i < 4; i++) {
        int cx = sx + 30 + i * 26;
        if (i == flaeche) rrect(cx, 14, 12, 12, 6, C_ACC2); else rrect_rand(cx, 14, 12, 12, 6, C_BG, C_OFF);
        kn_reg(K_DOT0 + i, cx - 6, 4, 24, 32);
    }
    gtxt(sx + 30 + 4 * 26 + 4, 12, ">", C_MUTED, 1); kn_reg(K_NEXT, sx + 30 + 4 * 26 - 4, 4, 28, 32);
    gtxt(sx + 30 + 4 * 26 + 30, 12, nm, C_TEXT, 1);
    gfx_end();
}

/* Arbeitsflaeche 1: System */
static void sys_pos(int i, int* x, int* y, int* w, int* h) {
    int pad = 40, gw = (W - 3 * pad) / 2, gh = (H - 56 - 90) / 2; if (gh > 300) gh = 300;
    *x = pad + (i % 2) * (gw + pad); *y = 62 + (i / 2) * (gh + 30); *w = gw; *h = gh;
}
static void sys_k_speicher(void) {
    int x, y, w, h; sys_pos(0, &x, &y, &w, &h);
    uint32_t fr = pmm_get_free_pages() / 256, to = pmm_get_total_pages() / 256; char a[12], t[40];
    karte(x, y, w, h, TR("Arbeitsspeicher", "Memory"));
    dec(a, fr); scopy(t, a, 40); cat(t, " MiB", 40); gfx_text(x + 18, y + 48, t, C_TEXT, 3);
    scopy(t, TR("frei von ", "free of "), 40); dec(a, to); cat(t, a, 40); cat(t, " MiB", 40); gtxt(x + 18, y + 96, t, C_MUTED, 1);
    int bw = w - 36, used = to ? (int)((to - fr) * (uint32_t)bw / to) : 0;
    rrect(x + 18, y + 130, bw, 12, 6, C_FIELD); if (used > 0) rrect(x + 18, y + 130, used < 12 ? 12 : used, 12, 6, mix(C_ACC, C_VIOLET, 128));
}
static void sys_k_zeit(void) {
    int x, y, w, h; sys_pos(1, &x, &y, &w, &h); char t[24];
    karte(x, y, w, h, TR("Laufzeit und Uhr", "Uptime and clock"));
    zeit_text(t, timer_get_seconds()); gfx_text(x + 18, y + 48, t, C_TEXT, 3);
    gtxt(x + 18, y + 96, TR("seit dem Start (Stunden:Minuten:Sekunden)", "since boot (hours:minutes:seconds)"), C_MUTED, 1);
}
static void sys_k_netz(void) {
    int x, y, w, h; sys_pos(2, &x, &y, &w, &h); char ip[32] = "";
    karte(x, y, w, h, TR("Netzwerk", "Network"));
    int ns = net_status(ip, 32);
    rrect(x + 18, y + 52, 14, 14, 7, ns == 2 ? C_OK : ns == 1 ? 0x00F59E0Bu : C_OFF);
    gtxt(x + 42, y + 50, ns == 2 ? TR("verbunden", "connected") : ns == 1 ? TR("Karte da, keine Adresse", "card found, no address") : TR("keine Netzwerkkarte", "no network card"), C_TEXT, 1);
    if (ns == 2) gfx_text(x + 18, y + 90, ip, C_ACC2, 2);
}
static void sys_k_sich(void) {
    int x, y, w, h; sys_pos(3, &x, &y, &w, &h); char z[100];
    karte(x, y, w, h, TR("Sicherheit und KI", "Security and AI"));
    int sig = (g_boot.flags & BOOT_SIGNIERT) != 0;
    rrect(x + 18, y + 52, 14, 14, 7, sig ? C_OK : C_ERR);
    gtxt(x + 42, y + 50, sig ? TR("Startkette durch LMS-Signatur geprueft", "Boot chain verified by LMS signature") : TR("Kernel nicht durch Signatur geprueft", "Kernel not verified by signature"), C_TEXT, 1);
    scopy(z, TR("Benutzer: ", "User: "), 100); cat(z, benutzer_name(), 100); cat(z, " (", 100); cat(z, benutzer_rollentext(benutzer_rolle()), 100); cat(z, ")", 100); gtxt(x + 18, y + 84, z, C_MUTED, 1);
    rrect(x + 18, y + 118, 14, 14, 7, gehirn_vorhanden() ? C_OK : C_OFF);
    gtxt(x + 42, y + 116, gehirn_vorhanden() ? TR("Sprachmodell bereit (laeuft lokal)", "Language model ready (runs locally)") : TR("kein Sprachmodell geladen", "no language model loaded"), C_TEXT, 1);
}
static void zeichne_f_system(void) { sys_k_speicher(); sys_k_zeit(); sys_k_netz(); sys_k_sich(); }

/* Arbeitsflaeche 2: Agenten */
static void zeichne_f_agenten(void) {
    int lx = 40, lw = (W - 120) * 45 / 100; if (lw < 360) lw = 360;
    int rx = lx + lw + 40, rw = W - rx - 40, ah = (H - 62 - 50) / NAG - 10; if (ah > 110) ah = 110;
    for (int i = 0; i < NAG; i++) {
        int y = 62 + i * (ah + 10);
        rrect_rand(lx, y, lw, ah, 14, i == alauf ? mix(C_CARD, C_ACC, 40) : C_CARD, i == alauf ? C_ACC : C_BORDER);
        gtxt(lx + 18, y + 12, TR(ag_name[i][0], ag_name[i][1]), C_TEXT, 1);
        absatz(lx + 18, y + 36, lw - 160, TR(ag_desc[i][0], ag_desc[i][1]), C_MUTED);
        knopf(K_AG0 + i, TR("Starten", "Run"), lx + lw - 120, y + (ah - 34) / 2, 100, 1);
    }
    int rh = H - 62 - 50; karte(rx, 62, rw, rh, alauf >= 0 ? TR(ag_name[alauf][0], ag_name[alauf][1]) : TR("Lauf-Protokoll", "Run log"));
    if (alauf < 0) absatz(rx + 18, 62 + 46, rw - 36, TR("Waehle links einen Agenten. Alles mit Wirkung wartet auf deine Freigabe. Du kannst auch sagen: briefing, agent sicherheit ...", "Pick an agent on the left. Anything with an effect waits for your approval. You can also say: briefing, agent security ..."), C_MUTED);
    for (int i = 0; i < nalog; i++) { rrect(rx + 18, 62 + 52 + i * 24, 8, 8, 4, C_OK); gtxt(rx + 36, 62 + 48 + i * 24, alog[i], i == nalog - 1 ? C_TEXT : C_MUTED, 1); }
    if (pend) {
        int by = 62 + rh - 78;
        rrect_rand(rx + 14, by, rw - 28, 64, 12, C_FIELD, 0x00F59E0Bu);
        gtxt(rx + 28, by + 8, TR("Freigabe noetig", "Approval needed"), 0x00F59E0Bu, 1);
        knopf(K_FREI, TR("Freigeben", "Approve"), rx + 28, by + 24, 130, 1);
        knopf(K_ABL, TR("Ablehnen", "Reject"), rx + 170, by + 24, 130, 0);
    }
}

/* Arbeitsflaeche 3: Dokumente und Aufgaben */
static void zeichne_f_aufgaben(void) {
    int pad = 40, cw = (W - 3 * pad) / 2, ch = H - 62 - 50;
    char nm[8][24]; int nd = doks_liste(nm, 8);
    karte(pad, 62, cw, ch, TR("Deine Dokumente", "Your documents"));
    if (!nd) gtxt(pad + 18, 62 + 46, TR("Noch keine Dokumente.", "No documents yet."), C_MUTED, 1);
    for (int i = 0; i < nd; i++) { rrect(pad + 18, 62 + 52 + i * 26, 8, 8, 4, C_ACC2); gtxt(pad + 36, 62 + 48 + i * 26, nm[i], C_TEXT, 1); }
    knopf(K_NEUDOK, TR("Neues Dokument", "New document"), pad + 18, 62 + ch - 52, 180, 1);
    int rx = pad + cw + pad; aufg_laden(); int na = aufg_zahl();
    karte(rx, 62, cw, ch, TR("Aufgaben", "Tasks"));
    if (!na) absatz(rx + 18, 62 + 46, cw - 36, TR("Keine Aufgaben. Sag zum Beispiel: aufgabe Rechnung bezahlen", "No tasks. Try saying: task pay the invoice"), C_MUTED);
    for (int i = 0; i < na && i < 10; i++) { char t[100]; if (!aufg_zeile(i, t, 100)) break; rrect_rand(rx + 18, 62 + 50 + i * 26, 12, 12, 4, C_FIELD, C_ACC); gtxt(rx + 40, 62 + 48 + i * 26, t, C_TEXT, 1); }
    knopf(K_AUFLEER, TR("Alle erledigt", "All done"), rx + 18, 62 + ch - 52, 180, 0);
}

static void zeichne_desktop(void) {
    gfx_begin(); gfx_rect(0, 0, W, H, C_BG); gfx_end();
    nkn = 0; lbw = 0; in_szene = 1; zeichne_leiste();
    if (flaeche == 0) { zeichne_robot(); zeichne_antwort(); zeichne_eingabe(); szene_oben(); }
    else { gfx_begin(); if (flaeche == 1) zeichne_f_system(); else if (flaeche == 2) zeichne_f_agenten(); else zeichne_f_aufgaben(); gfx_end(); }
    in_szene = 0;
}

/* ---------- Wuerfel: die Arbeitsflaechen drehen sich wie die Seiten eines Wuerfels ---------- */
extern uint32_t* gfx_fbptr(uint32_t* pitch_px);
static uint32_t *snapA, *snapB; static int snap_fehler;
static const int sin90[19] = { 0, 89, 178, 265, 350, 433, 512, 587, 658, 724, 784, 839, 887, 928, 962, 989, 1008, 1020, 1024 };
static void fb_sichern(uint32_t* d) { uint32_t pp; uint32_t* f = gfx_fbptr(&pp); if (!f) return; for (int y = 0; y < H; y++) { uint32_t* s = f + (uint64_t)y * pp; uint32_t* t = d + (uint64_t)y * W; for (int x = 0; x < W; x++) t[x] = s[x]; } }
static void wuerfel_flaeche(const uint32_t* src, int deg, int dir, int neu) {
    uint32_t pp; uint32_t* f = gfx_fbptr(&pp); if (!f) return;
    int a = W / 2, D = 3 * a, F = D - a;
    int s = sin90[deg / 5], c = sin90[(90 - deg) / 5];
    int prev = 0, havePrev = 0;
    for (int i = 0; i < W; i++) {
        int x, z;
        if (!neu) { int u = i - a; x = (u * c - a * s) / 1024; z = (-u * s - a * c) / 1024; }
        else { int w = i - a; x = (a * c + w * s) / 1024; z = (-a * s + w * c) / 1024; }
        int den = z + D; if (den < 1) den = 1;
        int sx = W / 2 + (int)((int64_t)x * F / den);
        int sc16 = (int)(((int64_t)F << 16) / den);
        if (!havePrev) { prev = sx; havePrev = 1; continue; }
        int d0 = prev, d1 = sx; prev = sx;
        if (d1 <= d0) continue;
        int col = dir > 0 ? i : W - 1 - i;
        int hd = (int)(((int64_t)H * sc16) >> 16); if (hd < 2) continue; if (hd > H) hd = H;
        int y0 = (H - hd) / 2; int inv = (int)(((int64_t)H << 16) / hd); int dunkel = sc16 < 52000;
        for (int dx = d0; dx < d1; dx++) {
            int ox = dir > 0 ? dx : W - 1 - dx; if (ox < 0 || ox >= W) continue;
            uint32_t* o = f + (uint64_t)y0 * pp + ox; int sy16 = 0;
            for (int dy = 0; dy < hd; dy++, o += pp, sy16 += inv) {
                uint32_t p = src[(uint64_t)(sy16 >> 16) * W + col];
                if (dunkel) p = ((p >> 1) & 0x007F7F7Fu) + ((p >> 2) & 0x003F3F3Fu);
                *o = p;
            }
        }
    }
}
static void wuerfel_dreh(int nach) {
    int von = flaeche; int diff = (nach - von + 4) % 4; int dir = diff == 3 ? -1 : 1;
    if (!snapA && !snap_fehler) {
        uint32_t seiten = ((uint32_t)W * (uint32_t)H * 4 + 4095) / 4096;
        if (pmm_get_free_pages() > seiten * 2 + 16384) { snapA = (uint32_t*)pmm_alloc_pages(seiten); snapB = (uint32_t*)pmm_alloc_pages(seiten); }
        if (!snapA || !snapB) { snapA = snapB = 0; snap_fehler = 1; }
    }
    if (snap_fehler || !gfx_fbptr(&(uint32_t){0})) { flaeche = nach; zeichne_desktop(); kprint("[DESKTOP] Wuerfel: ohne Animation\n"); return; }
    gfx_begin(); fb_sichern(snapA); gfx_end();
    flaeche = nach; zeichne_desktop();
    gfx_begin(); fb_sichern(snapB);
    uint32_t pp; uint32_t* f = gfx_fbptr(&pp);
    for (int deg = 5; deg < 90; deg += 5) {
        uint32_t t0 = timer_get_ticks();
        for (int y = 0; y < H; y++) { uint32_t* r = f + (uint64_t)y * pp; for (int x = 0; x < W; x++) r[x] = C_BG; }
        wuerfel_flaeche(snapA, deg, dir, 0); wuerfel_flaeche(snapB, deg, dir, 1);
        while (timer_get_ticks() - t0 < 2) __asm__ volatile ("pause");
    }
    for (int y = 0; y < H; y++) { uint32_t* r = f + (uint64_t)y * pp; uint32_t* q = snapB + (uint64_t)y * W; for (int x = 0; x < W; x++) r[x] = q[x]; }
    gfx_end();
    kprint("[DESKTOP] Wuerfel: gedreht\n");
}
static void flaeche_wechsel(int nach) {
    nach = ((nach % 4) + 4) % 4; if (nach == flaeche) return;
    if (nach != 0 && zustand != Z_LOGIN) { fok = 0; zustand = Z_DESK; }
    wuerfel_dreh(nach);
    kprint("[DESKTOP] Arbeitsflaeche "); kprint_dec((uint32_t)nach + 1); kprint(": "); kprint(fl_name[nach][1]); kprint("\n");
}
static void widget_neu(int typ) {
    if (flaeche != 0) flaeche_wechsel(0);
    if (nwid >= 6) { reply1(TR("Es sind schon 6 Widgets da.", "6 widgets are already there.")); zeichne_antwort(); return; }
    struct widget* g = &wid[nwid]; g->typ = typ; wid_groesse(typ, &g->w, &g->h);
    g->x = W - g->w - 30; g->y = 56; for (int j = 0; j < nwid; j++) g->y += wid[j].h + 14;
    if (g->y + g->h > H - 10) { g->x -= 280; g->y = 56; }
    nwid++; layout_speichern(); zeichne_desktop();
    kprint("[DESKTOP] Widget hinzugefuegt: "); kprint_dec((uint32_t)typ); kprint("\n");
}
static void rech_zuruecksetzen(void);
static void fenster_oeffnen(int typ) {
    int i = fenster_idx(typ);
    if (flaeche != 0) flaeche_wechsel(0);
    if (i < 0) {
        if (nfen >= 6) { reply1(TR("Es sind schon 6 Fenster offen.", "6 windows are already open.")); return; }
        int w = typ == FT_EDIT ? (W - 80 < 760 ? W - 80 : 760) : typ == FT_WEB ? W * 3 / 4 : typ == FT_RECH ? 300 : typ == FT_EINST ? 580 : typ == FT_CHATS ? 720 : typ == FT_GED ? 700 : 480;
        int h = typ == FT_EDIT ? (H - 140 < 560 ? H - 140 : 560) : typ == FT_WEB ? H * 3 / 4 : typ == FT_RECH ? 372 : typ == FT_EINST ? 490 : typ == FT_CHATS ? 470 : typ == FT_GED ? 480 : 250;
        if (typ == FT_WEB && w < 640) w = W - 40;
        int x = 60 + nfen * 36, y = 56 + nfen * 30;
        if (x + w > W - 8) x = W - w - 8; if (x < 4) x = 4; if (y + h > H - 8) y = H - h - 8; if (y < 44) y = 44;
        fen[nfen].typ = typ; fen[nfen].x = x; fen[nfen].y = y; fen[nfen].w = w; fen[nfen].h = h; nfen++;
        if (typ == FT_RECH) rech_zuruecksetzen();
    } else if (i != nfen - 1) fenster_hoch(i);
    fok = 1; zustand = zustand_fuer(typ); nsug = 0; sugsel = -1;
    zeichne_desktop();
    kprint("[DESKTOP] Fenster geoeffnet: "); kprint(fenster_titel(typ)); kprint("\n");
}

/* ---------- Textverarbeitung ---------- */
static int ed_zeilen(int* start, int max) {                 /* Zeilenanfaenge (mit Umbruch nach ed_cols); liefert Anzahl */
    int n = 0, col = 0; if (max > 0) start[n++] = 0;
    for (int i = 0; i < doklen; i++) {
        if (dok[i] == '\n') { col = 0; if (n < max) start[n++] = i + 1; }
        else if (++col >= ed_cols) { col = 0; if (n < max) start[n++] = i + 1; }
    }
    return n;
}
static int ed_start[600];
static void zeichne_editor(int voll) {
    int wi = fenster_idx(FT_EDIT); if (wi < 0) return;
    gfx_begin();
    if (voll) {
        int ko = kn_owner; kn_owner = wi; kn_loesche_owner(wi);
        ed_s = 1; ed_x = fen[wi].x; ed_y = fen[wi].y + WT_H; ed_w = fen[wi].w; ed_h = fen[wi].h - WT_H;
        ed_cols = (ed_w - 60) / (8 * ed_s); ed_rows = (ed_h - 150) / (20 * ed_s);
        rrect_rand(ed_x, ed_y, ed_w, ed_h, 16, C_CARD, C_BORDER);
        char t[60]; scopy(t, TR("Dokument  -  ", "Document  -  "), 60); cat(t, dokname, 60);
        gtxt(ed_x + 24, ed_y + 18, t, C_TEXT, 1);
        knopf(K_SPEICHERN, TR("Speichern", "Save"), ed_x + ed_w - 3 * 130 - 16, ed_y + 10, 120, 1);
        knopf(K_NEU, TR("Neu", "New"), ed_x + ed_w - 2 * 130 - 16, ed_y + 10, 120, 0);
        knopf(K_SCHLIESSEN, TR("Schliessen", "Close"), ed_x + ed_w - 130 - 16, ed_y + 10, 120, 0);
        gfx_rect(ed_x + 20, ed_y + 56, ed_w - 40, 1, C_BORDER);
        kn_owner = ko;
    }
    int px = ed_x + 30, py = ed_y + 76, lh = 20 * ed_s;
    rrect(ed_x + 24, py - 10, ed_w - 48, ed_rows * lh + 20, 8, 0x00F4F7FBu);
    int n = ed_zeilen(ed_start, 600), cl = 0, cc = 0;
    for (int i = 0; i < n; i++) if (ed_start[i] <= dokcur) cl = i;
    cc = dokcur - ed_start[cl];
    if (cl < dokscroll) dokscroll = cl;
    if (cl >= dokscroll + ed_rows) dokscroll = cl - ed_rows + 1;
    for (int r = 0; r < ed_rows && dokscroll + r < n; r++) {
        int a = ed_start[dokscroll + r], b = dokscroll + r + 1 < n ? ed_start[dokscroll + r + 1] : doklen;
        char z[130]; int k = 0; for (int i = a; i < b && k < 126; i++) if (dok[i] != '\n') z[k++] = dok[i];
        z[k] = 0; gfx_text(px, py + r * lh, z, 0x00101828u, ed_s);
    }
    int cy = cl - dokscroll; if (cy >= 0 && cy < ed_rows) gfx_rect(px + cc * 8 * ed_s, py + cy * lh, 2, 16 * ed_s, C_ACC);
    char st[100]; char a[12]; st[0] = 0;
    dec(a, (uint32_t)doklen); cat(st, a, 100); cat(st, TR(" Zeichen   |   Zeile ", " characters   |   line "), 100); dec(a, (uint32_t)cl + 1); cat(st, a, 100);
    cat(st, dokgeaendert ? TR("   |   nicht gespeichert", "   |   unsaved") : TR("   |   gespeichert", "   |   saved"), 100);
    rrect(ed_x + 24, ed_y + ed_h - 44, ed_w - 48, 28, 8, C_CARD);
    gtxt(ed_x + 34, ed_y + ed_h - 38, st, C_MUTED, 1);
    gtxt(ed_x + 24, ed_y + ed_h + 12, TR("Esc: speichern und schliessen", "Esc: save and close"), C_MUTED, 1);
    gfx_end();
}
static void dok_speichern(void) {
    synfs_create(dokname, SYNFS_FILE);
    int r = synfs_write(dokname, dok, (uint32_t)doklen);
    synfs_sync();
    dokgeaendert = 0;
    kprint("[DESKTOP] Dokument gespeichert: "); kprint(dokname); kprint(" ("); kprint_dec((uint32_t)doklen); kprint(" Bytes)"); if (r < 0) kprint(" FEHLER"); kprint("\n");
}
static void dok_neu(void) {
    char buf[2];
    for (int n = 1; n < 100; n++) {
        char a[8]; dec(a, (uint32_t)n); scopy(dokname, "dokument-", 24); cat(dokname, a, 24); cat(dokname, ".txt", 24);
        if (synfs_read(dokname, buf, 1) < 0) break;
    }
    doklen = 0; dokcur = 0; dokscroll = 0; dokgeaendert = 0; dok[0] = 0;
}
static void editor_oeffnen(void) { if (fenster_idx(FT_EDIT) < 0) dok_neu(); fenster_oeffnen(FT_EDIT); }
static void editor_schliessen(void) {
    if (doklen > 0 && dokgeaendert) dok_speichern();
    fenster_schliessen_typ(FT_EDIT);
}
static void editor_taste(uint8_t k, int shift, int altgr) {
    if (k == 0x29) { editor_schliessen(); return; }
    int n = ed_zeilen(ed_start, 600), cl = 0;
    for (int i = 0; i < n; i++) if (ed_start[i] <= dokcur) cl = i;
    int cc = dokcur - ed_start[cl];
    if (k == 0x2A) { if (dokcur > 0) { for (int i = dokcur - 1; i < doklen; i++) dok[i] = dok[i + 1]; dokcur--; doklen--; dokgeaendert = 1; } }
    else if (k == 0x4C) { if (dokcur < doklen) { for (int i = dokcur; i < doklen; i++) dok[i] = dok[i + 1]; doklen--; dokgeaendert = 1; } }
    else if (k == 0x50) { if (dokcur > 0) dokcur--; }
    else if (k == 0x4F) { if (dokcur < doklen) dokcur++; }
    else if (k == 0x52) { if (cl > 0) { int l = ed_start[cl] - ed_start[cl - 1]; dokcur = ed_start[cl - 1] + (cc < l - 1 ? cc : l - 1 > 0 ? l - 1 : 0); } }
    else if (k == 0x51) { if (cl + 1 < n) { int e = cl + 2 < n ? ed_start[cl + 2] : doklen + 1; int l = e - ed_start[cl + 1]; dokcur = ed_start[cl + 1] + (cc < l - 1 ? cc : (l - 1 > 0 ? l - 1 : 0)); if (dokcur > doklen) dokcur = doklen; } }
    else if (k == 0x4A) dokcur = ed_start[cl];
    else if (k == 0x4D) dokcur = cl + 1 < n ? ed_start[cl + 1] - (dok[ed_start[cl + 1] - 1] == '\n' ? 1 : 0) : doklen;
    else {
        char c = (k == 0x28 || k == 0x58) ? '\n' : usb_key_char(k, shift, altgr);
        if ((c == '\n' || (c >= 32 && c < 127) || (unsigned char)c >= 0xA0) && doklen < DOK_MAX) {
            for (int i = doklen; i > dokcur; i--) dok[i] = dok[i - 1];
            dok[dokcur++] = c; doklen++; dok[doklen] = 0; dokgeaendert = 1;
        } else return;
    }
    zeichne_editor(0);
}

/* ---------- Zwischenablage, Drucken (PDF), Rechtsklick-Menue und Browser ---------- */
static char clip[4096]; static int cliplen;
static char meldung[100];                                          /* kurze Rueckmeldung (Statuszeile) */
static void clip_setze(const char* t, int n) { if (n > (int)sizeof(clip) - 1) n = (int)sizeof(clip) - 1; for (int i = 0; i < n; i++) clip[i] = t[i]; clip[n] = 0; cliplen = n; }
static int sel1;                                                   /* alles im aktiven Eingabefeld markiert */
static int cur_ctrl;

/* PDF: Courier 9 pt, A4, 60 Zeilen je Seite, WinAnsi; Ergebnis im SynFS (druck-N.pdf) - ein Druckertreiber kann die Datei spaeter senden */
static char pdfb[200000]; static int pdfn; static int pdf_off[140];
static void pw(const char* s) { while (*s && pdfn < (int)sizeof(pdfb) - 8) pdfb[pdfn++] = *s++; }
static void pwn(int v) { char a[12]; dec(a, (uint32_t)v); pw(a); }
static char pdf_name[24];
static char pdf_zl[60][100];
static void pdf_seite(int nz, int nr) {
    static char strm[6000]; int sl = 0, co = 5 + 2 * nr, po = 4 + 2 * nr;
    const char* kop = "BT /F1 9 Tf 12 TL 40 800 Td\n"; for (int i = 0; kop[i]; i++) strm[sl++] = kop[i];
    for (int z = 0; z < nz && sl < 5600; z++) {
        strm[sl++] = '(';
        for (int i = 0; pdf_zl[z][i] && sl < 5600; i++) {
            unsigned char c = (unsigned char)pdf_zl[z][i];
            if (c == '(' || c == ')' || c == '\\') { strm[sl++] = '\\'; strm[sl++] = (char)c; }
            else if (c >= 32 && c < 127) strm[sl++] = (char)c;
            else if (c >= 160) { strm[sl++] = '\\'; strm[sl++] = (char)('0' + (c >> 6)); strm[sl++] = (char)('0' + ((c >> 3) & 7)); strm[sl++] = (char)('0' + (c & 7)); }
            else strm[sl++] = ' ';
        }
        const char* e = ") Tj T*\n"; for (int i = 0; e[i]; i++) strm[sl++] = e[i];
    }
    strm[sl++] = 'E'; strm[sl++] = 'T';
    pdf_off[co] = pdfn; pwn(co); pw(" 0 obj\n<< /Length "); pwn(sl); pw(" >>\nstream\n");
    for (int i = 0; i < sl && pdfn < (int)sizeof(pdfb) - 200; i++) pdfb[pdfn++] = strm[i];
    pw("\nendstream\nendobj\n");
    pdf_off[po] = pdfn; pwn(po); pw(" 0 obj\n<< /Type /Page /Parent 2 0 R /MediaBox [0 0 595 842] /Resources << /Font << /F1 3 0 R >> >> /Contents "); pwn(co); pw(" 0 R >>\nendobj\n");
}
static int pdf_drucken(const char* titel, const char* text) {
    int nseiten = 0, nz = 0, ll = 0; char l[100]; const char* p = text; pdfn = 0; (void)titel;
    pw("%PDF-1.4\n"); pdf_off[3] = pdfn; pw("3 0 obj\n<< /Type /Font /Subtype /Type1 /BaseFont /Courier /Encoding /WinAnsiEncoding >>\nendobj\n");
    for (;;) {
        if (*p == 0) { if (ll == 0) break; }
        else if (*p == '\n') p++;
        else if (ll < 92) { l[ll++] = *p++; continue; }
        l[ll] = 0; for (int i = 0; i <= ll; i++) pdf_zl[nz][i] = l[i]; nz++; ll = 0;
        if (nz == 60) { if (nseiten < 60) { pdf_seite(nz, nseiten); nseiten++; } nz = 0; }
        if (*p == 0) break;
    }
    if (nz > 0 && nseiten < 60) { pdf_seite(nz, nseiten); nseiten++; }
    if (!nseiten) return -1;
    pdf_off[2] = pdfn; pw("2 0 obj\n<< /Type /Pages /Count "); pwn(nseiten); pw(" /Kids [");
    for (int i = 0; i < nseiten; i++) { pwn(4 + 2 * i); pw(" 0 R "); }
    pw("] >>\nendobj\n");
    pdf_off[1] = pdfn; pw("1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n");
    int xr = pdfn, nobj = 4 + 2 * nseiten;
    pw("xref\n0 "); pwn(nobj); pw("\n0000000000 65535 f \n");
    for (int i = 1; i < nobj; i++) { char a[12]; dec(a, (uint32_t)pdf_off[i]); int n = slen(a); for (int k = n; k < 10; k++) pdfb[pdfn++] = '0'; pw(a); pw(" 00000 n \n"); }
    pw("trailer\n<< /Size "); pwn(nobj); pw(" /Root 1 0 R >>\nstartxref\n"); pwn(xr); pw("\n%%EOF\n");
    for (int n = 1; n < 100; n++) { char a[8], b[2]; dec(a, (uint32_t)n); scopy(pdf_name, "druck-", 24); cat(pdf_name, a, 24); cat(pdf_name, ".pdf", 24); if (synfs_read(pdf_name, b, 1) < 0) break; }
    synfs_create(pdf_name, SYNFS_FILE);
    if (synfs_write(pdf_name, pdfb, (uint32_t)pdfn) < 0) return -2;
    synfs_sync();
    kprint("[DESKTOP] Gedruckt als PDF: "); kprint(pdf_name); kprint(" ("); kprint_dec((uint32_t)nseiten); kprint(" Seiten)\n");
    return nseiten;
}

/* ---------- Browser-Oberflaeche ---------- */
#include "browser.h"
static char adr[192]; static int adrlen;
static int wscroll, wcols, wrows, wok, wlauf;                       /* wok: Seite geladen; wlauf: Abruf laeuft */
static char whist[16][200]; static int nwh, whp = -1;
static char lesez[1500]; static int lzlen;
static int wx0, wy0, ww, wh;                                         /* Seitenbereich */
enum { K_WB = 60, K_WV, K_WR, K_WL, K_WX, K_WS, K_LZ0 = 70 };
static void lz_laden(void) { int r = zdb_get_text("desktop.lesezeichen", lesez, sizeof(lesez) - 1); if (r < 0) r = synfs_read("lesezeichen.txt", lesez, sizeof(lesez) - 1); lzlen = r > 0 ? r : 0; lesez[lzlen] = 0; }
static int lz_zeile(int n, char* out, int max) {
    int z = 0, i = 0;
    while (i < lzlen) { int e = i; while (e < lzlen && lesez[e] != '\n') e++; if (e > i) { if (z == n) { int k = 0; for (int j = i; j < e && k < max - 1; j++) out[k++] = lesez[j]; out[k] = 0; return 1; } z++; } i = e + 1; }
    return 0;
}
static void lz_add(const char* u) {
    lz_laden(); char t[200]; for (int n = 0; lz_zeile(n, t, 200); n++) if (gleich(t, u)) { scopy(meldung, TR("Schon in den Lesezeichen.", "Already bookmarked."), 100); return; }
    int n = slen(u); if (lzlen + n + 2 >= (int)sizeof(lesez) - 4) { scopy(meldung, TR("Lesezeichen voll.", "Bookmarks full."), 100); return; }
    for (int i = 0; i < n; i++) lesez[lzlen++] = u[i];
    lesez[lzlen++] = '\n'; lesez[lzlen] = 0;
    zdb_put_text("desktop.lesezeichen", lesez, lzlen);
    scopy(meldung, TR("Lesezeichen gespeichert.", "Bookmark saved."), 100); kprint("[WEB] Lesezeichen: "); kprint(u); kprint("\n");
}
static const char* const web_vorgabe[] = { "de.wikipedia.org", "en.wikipedia.org", "example.com", "duckduckgo.com", "news.ycombinator.com", 0 };
static void web_sug_suche(void) {
    nsug = 0; sugsel = -1; const char* q = adr; while (*q == ' ') q++; if (!*q) return;
    sug_q = q; int ql = slen(q); char t[200];
    for (int i = nwh - 1; i >= 0; i--) { const char* u = whist[i]; if (praefix(u, "https://")) u += 8; else if (praefix(u, "http://")) u += 7; if (praefix(u, q)) sug_add(u, 2, ql); }
    lz_laden(); for (int n = 0; lz_zeile(n, t, 200); n++) { const char* u = t; if (praefix(u, "https://")) u += 8; else if (praefix(u, "http://")) u += 7; if (praefix(u, q)) sug_add(u, 0, ql); }
    for (int i = 0; web_vorgabe[i]; i++) if (praefix(web_vorgabe[i], q)) sug_add(web_vorgabe[i], 0, ql);
}
static void zeichne_web(int voll);
static void web_gehe(const char* eingabe_url, int push) {
    char u[200]; web_url_bauen(eingabe_url, u, 200);
    wlauf = 1; scopy(meldung, TR("Lade ", "Loading "), 100); cat(meldung, u, 100); zeichne_web(0);
    kprint("[WEB] Lade: "); kprint(u); kprint("\n");
    int r = web_laden(u, wcols);
    wlauf = 0; nsug = 0; sugsel = -1;
    if (r != 0) { wok = 0; scopy(meldung, web_fehler, 100); kprint("[WEB] Fehler: "); kprint(web_fehler); kprint("\n"); scopy(adr, u, 192); adrlen = slen(adr); }
    else {
        wok = 1; wscroll = 0; scopy(adr, web_url, 192); adrlen = slen(adr); meldung[0] = 0;
        if (push) { if (whp + 1 < 16) { whp++; } else { for (int i = 0; i < 15; i++) scopy(whist[i], whist[i + 1], 200); } scopy(whist[whp], web_url, 200); nwh = whp + 1; }
        kprint("[WEB] Seite: "); kprint(web_titel); kprint(" ("); kprint_dec((uint32_t)web_nz); kprint(" Zeilen, "); kprint_dec((uint32_t)web_nlinks); kprint(" Links)\n");
    }
    sel1 = 0; zeichne_web(1);
}
static void web_zurueck(int d) { int n = whp + d; if (n < 0 || n >= nwh) return; whp = n; web_gehe(whist[whp], 0); }
static void web_geo(int* fx, int* fw, int* ty) { int wi = fenster_idx(FT_WEB); *fx = *fw = *ty = 0; if (wi < 0) return; *ty = fen[wi].y + WT_H + 8; *fx = fen[wi].x + 12 + 3 * 42; *fw = fen[wi].w - 12 - 3 * 42 - 12 - 90; }
static void zeichne_web(int voll) {
    int wi = fenster_idx(FT_WEB); if (wi < 0) return;
    int wox = fen[wi].x, woy = fen[wi].y + WT_H, wow = fen[wi].w, woh = fen[wi].h - WT_H;
    int fx, fw, ty; web_geo(&fx, &fw, &ty);
    gfx_begin();
    wx0 = wox + 10; wy0 = woy + 50; ww = wow - 20; wh = woh - 50 - 30;
    wcols = (ww - 60) / 8; if (wcols > 110) wcols = 110; wrows = (wh - 24) / 20;
    if (voll) {
        int ko = kn_owner; kn_owner = wi; kn_loesche_owner(wi);
        gfx_rect(wox + 2, woy, wow - 4, woh - 2, C_BG);
        knopf(K_WB, "<", wox + 12, ty, 36, 0); knopf(K_WV, ">", wox + 54, ty, 36, 0); knopf(K_WR, "R", wox + 96, ty, 36, 0);
        rrect_rand(fx, ty, fw, 34, 12, C_FIELD, C_ACC);
        { char a[200]; scopy(a, adr, 200); int mc = (fw - 24) / 8; if (slen(a) > mc) { for (int i = 0; i < mc - 1; i++) a[i] = adr[slen(adr) - (mc - 1) + i]; a[mc - 1] = 0; }
          if (sel1 && adrlen) rrect(fx + 8, ty + 5, gtw(a, 1) + 8, 24, 6, mix(C_FIELD, C_ACC, 140));
          if (!adrlen) gtxt(fx + 14, ty + 9, TR("Adresse oder Suchbegriff ...", "Address or search ..."), 0x00647488u, 1); else gfx_text(fx + 14, ty + 9, a, C_TEXT, 1);
          gfx_rect(fx + 14 + slen(a) * 8, ty + 6, 2, 22, C_TEXT); }
        knopf(K_WS, TR("Merken", "Save"), fx + fw + 8, ty, 82, 0);
        kn_owner = ko;
    }
    rrect_rand(wx0, wy0, ww, wh, 14, C_CARD, C_BORDER);
    if (wlauf) gtxt(wx0 + 24, wy0 + 24, TR("Seite wird geladen ...", "Loading page ..."), C_MUTED, 1);
    else if (!wok) {
        int y = wy0 + 20; char t[200];
        if (meldung[0]) { gtxt(wx0 + 24, y, meldung, 0x00F59E0Bu, 1); y += 34; }
        gtxt(wx0 + 24, y, TR("Zynex Browser - Lesezeichen und Vorschlaege", "Zynex browser - bookmarks and suggestions"), C_TEXT, 1); y += 30;
        lz_laden(); int n = 0; int ko = kn_owner; kn_owner = wi;
        for (; lz_zeile(n, t, 200) && n < 8 && y + n * 28 < wy0 + wh - 50; n++) { gtxt(wx0 + 40, y + n * 28, t, C_ACC2, 1); kn_reg(K_LZ0 + n, wx0 + 30, y + n * 28 - 4, ww - 60, 26); }
        y += n * 28 + 8;
        for (int i = 0; web_vorgabe[i] && y < wy0 + wh - 50; i++) { gtxt(wx0 + 40, y, web_vorgabe[i], C_MUTED, 1); kn_reg(K_LZ0 + 10 + i, wx0 + 30, y - 4, ww - 60, 26); y += 28; }
        kn_owner = ko;
        int ns = net_status(t, 32); gtxt(wx0 + 24, wy0 + wh - 30, ns != 2 ? TR("Kein Netzwerk: zuerst verbinden (netz dhcp).", "No network: connect first (net dhcp).") : TR("Adresse oder Suchbegriff tippen. Rechtsklick: Menue.", "Type an address or search term. Right click: menu."), C_MUTED, 1);
    } else {
        if (wscroll > web_nz - wrows) wscroll = web_nz - wrows;
        if (wscroll < 0) wscroll = 0;
        for (int r = 0; r < wrows && wscroll + r < web_nz; r++) {
            struct web_zeile* z = &web_z[wscroll + r]; int y = wy0 + 14 + r * 20, x = wx0 + 24;
            uint32_t c = z->stil == 2 ? C_ACC2 : z->stil == 3 ? C_MUTED : C_TEXT;
            if (z->stil == 3) rrect(wx0 + 14, y - 2, ww - 40, 20, 4, C_FIELD);
            gfx_text(x, y, z->t, c, 1); if (z->stil == 1) gfx_text(x + 1, y, z->t, c, 1);
            for (int k = 0; k < z->nl; k++) {
                char seg[120]; int a = z->l[k].a, b = z->l[k].b, n = 0; for (int q = a; q < b && q < 115; q++) seg[n++] = z->t[q]; seg[n] = 0;
                rrect(x + a * 8 - 1, y, (b - a) * 8 + 2, 18, 0, z->stil == 3 ? C_FIELD : C_CARD); gfx_text(x + a * 8, y, seg, C_ACC2, 1); gfx_rect(x + a * 8, y + 16, (b - a) * 8, 1, C_ACC2);
            }
        }
        int sbx = wx0 + ww - 18, sbh = wh - 24; rrect(sbx, wy0 + 12, 8, sbh, 4, C_FIELD);
        if (web_nz > wrows) { int kh = sbh * wrows / web_nz; if (kh < 24) kh = 24; int ky = wy0 + 12 + (sbh - kh) * wscroll / (web_nz - wrows); rrect(sbx, ky, 8, kh, 4, C_ACC); }
    }
    gfx_rect(wox + 2, woy + woh - 28, wow - 4, 26, C_BG);
    { char s[140]; if (meldung[0] && wok) scopy(s, meldung, 140); else if (wok) scopy(s, web_titel[0] ? web_titel : web_url, 140); else s[0] = 0;
      if (slen(s) > (wow - 200) / 8) s[(wow - 200) / 8] = 0; gtxt(wox + 14, woy + woh - 24, s, C_MUTED, 1);
      char z[40], a[12]; scopy(z, "", 40); if (wok && web_nz > wrows) { dec(a, (uint32_t)wscroll + 1); cat(z, a, 40); cat(z, "-", 40); int e = wscroll + wrows; if (e > web_nz) e = web_nz; dec(a, (uint32_t)e); cat(z, a, 40); cat(z, " / ", 40); dec(a, (uint32_t)web_nz); cat(z, a, 40); }
      gtxt(wox + wow - 14 - gtw(z, 1), woy + woh - 24, z, C_MUTED, 1); }
    if (nsug > 0) {
        int y0 = ty + 36;
        rrect_rand(fx, y0, fw, nsug * 30 + 12, 14, C_CARD, C_ACC);
        for (int i = 0; i < nsug; i++) {
            int ry = y0 + 6 + i * 30; if (i == sugsel) rrect(fx + 6, ry, fw - 12, 28, 10, mix(C_CARD, C_ACC, 120));
            char a[96]; int pl = sugpre[i] < 95 ? sugpre[i] : 95; for (int j = 0; j < pl; j++) a[j] = sug[i][j]; a[pl] = 0; int px = fx + 20;
            if (pl) { gtxt(px, ry + 6, a, C_TEXT, 1); px += gtw(a, 1); } gtxt(px, ry + 6, sug[i] + pl, i == sugsel ? C_TEXT : C_MUTED, 1);
            const char* tg = sugtyp[i] == 0 ? TR("Lesezeichen", "bookmark") : TR("Verlauf", "history"); gtxt(fx + fw - 20 - gtw(tg, 1), ry + 6, tg, C_MUTED, 1);
            int ko = kn_owner; kn_owner = wi; kn_reg(K_SUG0 + i, fx + 6, ry, fw - 12, 28); kn_owner = ko;
        }
    }
    gfx_end();
}
static void web_oeffnen(const char* url) {
    nsug = 0; sugsel = -1; sel1 = 0; wlauf = 0; meldung[0] = 0;
    kprint("[DESKTOP] Browser geoeffnet\n");
    fenster_oeffnen(FT_WEB);
    if (url && *url) web_gehe(url, 1);
}
static void web_schliessen(void) { nsug = 0; sugsel = -1; sel1 = 0; fenster_schliessen_typ(FT_WEB); }
static int web_link_bei(int x, int y) {                              /* Link unter der Maus oder -1 */
    if (!wok || x < wx0 + 24 || y < wy0 + 14) return -1;
    int r = (y - wy0 - 14) / 20; if (r < 0 || r >= wrows || wscroll + r >= web_nz) return -1;
    struct web_zeile* z = &web_z[wscroll + r]; int sp = (x - wx0 - 24) / 8;
    for (int k = 0; k < z->nl; k++) if (sp >= z->l[k].a && sp < z->l[k].b) return z->l[k].id;
    return -1;
}

/* ---------- Rechtsklick-Menue ---------- */
enum { M_NEUDOK = 30, M_BROWSER, M_RECHNER, M_INFO, M_EINST, M_CHATS, M_GED, M_WUHR, M_WSYS, M_WAUF, M_WAGENT, M_WSCHL, M_FSCHL, M_AUSSCHN = 1, M_KOPIEREN, M_EINFUEGEN, M_ALLES, M_DRUCKEN, M_ZURUECK, M_VOR, M_NEULADEN, M_LINKOEFFNEN, M_LINKKOPIEREN, M_LESEZEICHEN, M_SEITEKOPIEREN, M_SEITESPEICHERN, M_SPEICHERN, M_SCHLIESSEN, M_NAECHSTE, M_VORIGE, M_LEEREN };
static struct { int offen, x, y, w, n, hover, ziel; struct { int id; const char* de; const char* en; } p[12]; } menu;
static int menu_link, menu_wid;
static void menu_add(int id, const char* de, const char* en) { if (menu.n < 12) { menu.p[menu.n].id = id; menu.p[menu.n].de = de; menu.p[menu.n].en = en; menu.n++; } }
static void zeichne_menu(void) {
    if (!menu.offen) return;
    gfx_begin();
    int h = menu.n * 30 + 12;
    rrect_rand(menu.x, menu.y, menu.w, h, 12, C_CARD, C_ACC);
    for (int i = 0; i < menu.n; i++) {
        int ry = menu.y + 6 + i * 30; if (i == menu.hover) rrect(menu.x + 6, ry, menu.w - 12, 28, 8, mix(C_CARD, C_ACC, 140));
        gtxt(menu.x + 20, ry + 6, TR(menu.p[i].de, menu.p[i].en), C_TEXT, 1);
    }
    gfx_end();
}
static void redraw_alles(void) { zeichne_desktop(); }
static void menu_schliessen(void) { if (!menu.offen) return; menu.offen = 0; redraw_alles(); }
static void menu_oeffnen(int x, int y) {
    menu.n = 0; menu.hover = -1; menu_link = -1; menu_wid = -1;
    if (flaeche == 0 && zustand == Z_DESK) for (int j = nwid - 1; j >= 0; j--) if (x >= wid[j].x && x < wid[j].x + wid[j].w && y >= wid[j].y && y < wid[j].y + wid[j].h) { menu_wid = j; break; }
    int eingabefeld = (zustand == Z_DESK && flaeche == 0 && x >= bar_x && x < bar_x + bar_w && y >= bar_y && y < bar_y + 52);
    if (zustand == Z_WEB) {
        int fx, fw, ty; web_geo(&fx, &fw, &ty);
        if (x >= fx && x < fx + fw && y >= ty && y < ty + 34) { menu_add(M_AUSSCHN, "Ausschneiden", "Cut"); menu_add(M_KOPIEREN, "Kopieren", "Copy"); menu_add(M_EINFUEGEN, "Einfuegen", "Paste"); menu_add(M_ALLES, "Alles auswaehlen", "Select all"); }
        else {
            menu_link = web_link_bei(x, y);
            if (menu_link >= 0) { menu_add(M_LINKOEFFNEN, "Link oeffnen", "Open link"); menu_add(M_LINKKOPIEREN, "Link kopieren", "Copy link"); }
            menu_add(M_ZURUECK, "Zurueck", "Back"); menu_add(M_VOR, "Vor", "Forward"); menu_add(M_NEULADEN, "Neu laden", "Reload");
            menu_add(M_SEITEKOPIEREN, "Seitentext kopieren", "Copy page text"); menu_add(M_LESEZEICHEN, "Lesezeichen setzen", "Bookmark page"); menu_add(M_DRUCKEN, "Drucken (PDF)", "Print (PDF)");
            if (menu.n < 9) menu_add(M_SEITESPEICHERN, "Als Datei speichern", "Save as file");
        }
    } else if (zustand == Z_EDIT) {
        menu_add(M_AUSSCHN, "Ausschneiden", "Cut"); menu_add(M_KOPIEREN, "Kopieren", "Copy"); menu_add(M_EINFUEGEN, "Einfuegen", "Paste"); menu_add(M_ALLES, "Alles auswaehlen", "Select all");
        menu_add(M_DRUCKEN, "Drucken (PDF)", "Print (PDF)"); menu_add(M_SPEICHERN, "Speichern", "Save"); menu_add(M_SCHLIESSEN, "Schliessen", "Close");
    } else if (zustand == Z_RECH || zustand == Z_INFO) {
        menu_add(M_FSCHL, "Fenster schliessen", "Close window");
    } else if (menu_wid >= 0) {
        menu_add(M_WSCHL, "Widget schliessen", "Close widget");
    } else if (eingabefeld) {
        menu_add(M_AUSSCHN, "Ausschneiden", "Cut"); menu_add(M_KOPIEREN, "Kopieren", "Copy"); menu_add(M_EINFUEGEN, "Einfuegen", "Paste"); menu_add(M_ALLES, "Alles auswaehlen", "Select all"); menu_add(M_LEEREN, "Leeren", "Clear");
    } else if (flaeche == 0) {
        menu_add(M_NEUDOK, "Neues Dokument", "New document"); menu_add(M_BROWSER, "Browser", "Browser"); menu_add(M_RECHNER, "Rechner", "Calculator"); menu_add(M_INFO, "System-Info", "System info");
        menu_add(M_WUHR, "Widget: Uhr", "Widget: clock"); menu_add(M_WSYS, "Widget: System", "Widget: system"); menu_add(M_WAUF, "Widget: Aufgaben", "Widget: tasks"); menu_add(M_WAGENT, "Widget: Agenten", "Widget: agents");
        menu_add(M_NAECHSTE, "Naechste Arbeitsflaeche", "Next workspace"); menu_add(M_CHATS, "Chats", "Chats"); menu_add(M_GED, "Gedaechtnis", "Memory"); menu_add(M_EINST, "Einstellungen", "Settings");
    } else {
        menu_add(M_NAECHSTE, "Naechste Arbeitsflaeche", "Next workspace"); menu_add(M_VORIGE, "Vorherige Arbeitsflaeche", "Previous workspace");
    }
    menu.w = 250; menu.x = x + menu.w > W ? W - menu.w - 4 : x; menu.y = y + menu.n * 30 + 12 > H ? H - menu.n * 30 - 16 : y; menu.offen = 1;
    zeichne_menu();
}
static void ed_einfuegen_text(const char* t, int n) {
    for (int i = 0; i < n && doklen < DOK_MAX; i++) { unsigned char c = (unsigned char)t[i]; if (c == '\r') continue; if (!(c == '\n' || (c >= 32 && c < 127) || c >= 0xA0)) continue; for (int k = doklen; k > dokcur; k--) dok[k] = dok[k - 1]; dok[dokcur++] = (char)c; doklen++; }
    dok[doklen] = 0; dokgeaendert = 1;
}
/* Aktionen fuer das aktive Eingabeziel: Chat-Leiste, Adressleiste oder Dokument */
static void ziel_aktion(int id) {
    char* b = zustand == Z_WEB ? adr : eingabe; int* n = zustand == Z_WEB ? &adrlen : &einlen; int mx = zustand == Z_WEB ? 190 : 80;
    if (zustand == Z_EDIT) {
        if (id == M_KOPIEREN || id == M_AUSSCHN) { clip_setze(dok, doklen); if (id == M_AUSSCHN) { doklen = 0; dokcur = 0; dok[0] = 0; dokgeaendert = 1; } scopy(meldung, TR("Dokument kopiert.", "Document copied."), 100); }
        else if (id == M_EINFUEGEN) ed_einfuegen_text(clip, cliplen);
        zeichne_editor(1); return;
    }
    if (id == M_KOPIEREN || id == M_AUSSCHN) { if (*n) { clip_setze(b, *n); if (id == M_AUSSCHN) { *n = 0; b[0] = 0; } } }
    else if (id == M_EINFUEGEN) { if (sel1) { *n = 0; b[0] = 0; } for (int i = 0; i < cliplen && *n < mx; i++) { unsigned char c = (unsigned char)clip[i]; if (c >= 32 && (c < 127 || c >= 0xA0)) b[(*n)++] = (char)c; } b[*n] = 0; }
    else if (id == M_LEEREN) { *n = 0; b[0] = 0; }
    if (id == M_ALLES) sel1 = *n > 0; else sel1 = 0;
    if (zustand == Z_WEB) { web_sug_suche(); zeichne_web(1); } else { sug_suche(); zeichne_eingabe(); }
}
static void menu_aktion(int id) {
    menu.offen = 0;
    switch (id) {
    case M_AUSSCHN: case M_KOPIEREN: case M_EINFUEGEN: case M_ALLES: case M_LEEREN: ziel_aktion(id); return;
    case M_DRUCKEN: {
        static char txt[40000]; int r;
        if (zustand == Z_EDIT) r = pdf_drucken(dokname, dok); else { web_text(txt, sizeof(txt)); r = pdf_drucken(web_titel, txt); }
        if (r > 0) { scopy(meldung, TR("Gedruckt als PDF: ", "Printed as PDF: "), 100); cat(meldung, pdf_name, 100); cat(meldung, TR(" (Druckertreiber folgt)", " (printer driver follows)"), 100); } else scopy(meldung, TR("Drucken fehlgeschlagen.", "Printing failed."), 100);
        redraw_alles(); return; }
    case M_ZURUECK: web_zurueck(-1); return;
    case M_VOR: web_zurueck(1); return;
    case M_NEULADEN: web_gehe(web_url[0] ? web_url : adr, 0); return;
    case M_LINKOEFFNEN: if (menu_link >= 0) web_gehe(web_links[menu_link], 1); return;
    case M_LINKKOPIEREN: if (menu_link >= 0) { clip_setze(web_links[menu_link], slen(web_links[menu_link])); scopy(meldung, TR("Link kopiert.", "Link copied."), 100); } break;
    case M_SEITEKOPIEREN: { static char t[4096]; int n = web_text(t, sizeof(t)); clip_setze(t, n); scopy(meldung, TR("Seitentext kopiert.", "Page text copied."), 100); break; }
    case M_LESEZEICHEN: if (wok) lz_add(web_url); break;
    case M_SEITESPEICHERN: { static char t[40000]; int n = web_text(t, sizeof(t)); char nm[24], a[8], b[2]; for (int i = 1; i < 100; i++) { dec(a, (uint32_t)i); scopy(nm, "seite-", 24); cat(nm, a, 24); cat(nm, ".txt", 24); if (synfs_read(nm, b, 1) < 0) break; }
        synfs_create(nm, SYNFS_FILE); synfs_write(nm, t, (uint32_t)n); synfs_sync(); scopy(meldung, TR("Gespeichert: ", "Saved: "), 100); cat(meldung, nm, 100); break; }
    case M_SPEICHERN: dok_speichern(); break;
    case M_SCHLIESSEN: editor_schliessen(); return;
    case M_NEUDOK: editor_oeffnen(); return;
    case M_BROWSER: web_oeffnen(0); return;
    case M_RECHNER: fenster_oeffnen(FT_RECH); return;
    case M_INFO: fenster_oeffnen(FT_INFO); return;
    case M_EINST: fenster_oeffnen(FT_EINST); return;
    case M_CHATS: fenster_oeffnen(FT_CHATS); return;
    case M_GED: fenster_oeffnen(FT_GED); return;
    case M_WUHR: widget_neu(WG_UHR); return; case M_WSYS: widget_neu(WG_SYS); return; case M_WAUF: widget_neu(WG_AUF); return; case M_WAGENT: widget_neu(WG_AGENT); return;
    case M_WSCHL: if (menu_wid >= 0 && menu_wid < nwid) { for (int j = menu_wid; j < nwid - 1; j++) wid[j] = wid[j + 1]; nwid--; layout_speichern(); } break;
    case M_FSCHL: if (nfen) { fenster_schliessen_typ(fen[nfen - 1].typ); return; } break;
    case M_NAECHSTE: flaeche_wechsel(flaeche + 1); return;
    case M_VORIGE: flaeche_wechsel(flaeche + 3); return;
    }
    redraw_alles();
}
static int menu_treffer(int x, int y) { if (x < menu.x || x >= menu.x + menu.w) return -1; int i = (y - menu.y - 6) / 30; return (y >= menu.y + 6 && i >= 0 && i < menu.n) ? i : -1; }

/* Kurztasten mit Strg (a c x v p l d r) */
static int strg_taste(uint8_t k) {
    int id = 0;
    if (k == 0x06) id = M_KOPIEREN; else if (k == 0x1B) id = M_AUSSCHN; else if (k == 0x19) id = M_EINFUEGEN; else if (k == 0x04) id = M_ALLES;
    else if (k == 0x13 && (zustand == Z_WEB || zustand == Z_EDIT)) id = M_DRUCKEN;
    else if (k == 0x07 && zustand == Z_WEB) id = M_LESEZEICHEN; else if (k == 0x15 && zustand == Z_WEB) id = M_NEULADEN;
    else if (k == 0x0F && zustand == Z_WEB) { sel1 = adrlen > 0; zeichne_web(1); return 1; }
    else return 0;
    if (zustand == Z_DESK && flaeche != 0) return 1;
    menu.offen = 0;
    if (id == M_KOPIEREN || id == M_AUSSCHN || id == M_EINFUEGEN || id == M_ALLES) { if (zustand == Z_EDIT && id == M_ALLES) return 1; ziel_aktion(id); }
    else menu_aktion(id);
    return 1;
}


static void web_taste(uint8_t k, int shift, int altgr) {
    if (cur_ctrl && strg_taste(k)) return;
    if (k == 0x29) { if (nsug) { nsug = 0; sugsel = -1; zeichne_web(1); } else web_schliessen(); return; }
    if (k == 0x28 || k == 0x58) {
        if (sugsel >= 0 && sugsel < nsug) { scopy(adr, sug[sugsel], 192); adrlen = slen(adr); }
        if (adrlen) web_gehe(adr, 1); return;
    }
    if (nsug > 0 && (k == 0x51 || k == 0x52)) { sugsel += (k == 0x51) ? 1 : -1; if (sugsel >= nsug) sugsel = -1; if (sugsel < -1) sugsel = nsug - 1; zeichne_web(1); return; }
    if (nsug > 0 && k == 0x2B) { scopy(adr, sug[sugsel >= 0 ? sugsel : 0], 192); adrlen = slen(adr); web_sug_suche(); zeichne_web(1); return; }
    if (k == 0x3E) { web_gehe(web_url[0] ? web_url : adr, 0); return; }                               /* F5 */
    if (k == 0x51 || k == 0x52 || k == 0x4E || k == 0x4B || k == 0x4A || k == 0x4D) {
        int d = k == 0x51 ? 1 : k == 0x52 ? -1 : k == 0x4E ? wrows - 1 : k == 0x4B ? -(wrows - 1) : k == 0x4A ? -100000 : 100000;
        wscroll += d; if (wscroll > web_nz - wrows) wscroll = web_nz - wrows; if (wscroll < 0) wscroll = 0; zeichne_web(0); return;
    }
    if (k == 0x2A) { if (sel1) { adrlen = 0; adr[0] = 0; sel1 = 0; } else if (adrlen) adr[--adrlen] = 0; web_sug_suche(); zeichne_web(1); return; }
    char c = usb_key_char(k, shift, altgr);
    if (c >= 32 && c < 127 && adrlen < 190) { if (sel1) { adrlen = 0; sel1 = 0; } adr[adrlen++] = c; adr[adrlen] = 0; web_sug_suche(); zeichne_web(1); }
}

/* ---------- Intents ---------- */
enum { I_NICHTS, I_TEXT, I_RECHNEN, I_SYSTEM, I_DATEIEN, I_QUBIT, I_SICHER, I_ABMELDEN, I_NEUSTART, I_WARTUNG, I_HILFE, I_AGENT, I_AUFGABE, I_FLAECHE, I_WEB, I_FENSTER };
static int in_par; static const char* in_text;
static int enthaelt(const char* s, const char* w) {
    for (; *s; s++) { int i = 0; while (w[i] && s[i] == w[i]) i++; if (!w[i]) return 1; }
    return 0;
}
static int hat(const char* s, const char* const* w) { for (int i = 0; w[i]; i++) if (enthaelt(s, w[i])) return 1; return 0; }
static int hat_rechnung(const char* s) {
    int ziffer = 0, op = 0;
    for (; *s; s++) { if (*s >= '0' && *s <= '9') ziffer = 1; if (*s == '+' || *s == '*' || *s == '/' || *s == '-' || *s == 'x') op = 1; }
    return ziffer && op;
}
static int erkenne(const char* s) {
    static const char* const w_text[] = { "text", "brief", "dokument", "schreib", "word", "notiz", "write", "document", "letter", "note", "essay", 0 };
    static const char* const w_sys[] = { "system", "status", "speicher", "memory", "ram", "info", "zustand", 0 };
    static const char* const w_dat[] = { "datei", "files", "ordner", "list", "meine dok", "my doc", 0 };
    static const char* const w_q[] = { "qubit", "quant", "verschr", "entangle", 0 };
    static const char* const w_sec[] = { "sicher", "secur", "signatur", "signature", "firewall", "verschluessel", "encrypt", 0 };
    static const char* const w_out[] = { "abmeld", "logout", "log out", "sperr", "lock", "sign out", 0 };
    static const char* const w_nst[] = { "neustart", "restart", "reboot", "neu starten", 0 };
    static const char* const w_hlp[] = { "hilfe", "help", "was kannst", "what can", 0 };
    {
        static const char* const kw[] = { "browser", "internet", "web", "surfe", "suche", "search", "oeffne", "open", 0 };
        int erstes = -1; for (int i = 0; kw[i]; i++) { int n = slen(kw[i]), ok = 1; for (int j = 0; j < n; j++) if (s[j] != kw[i][j]) { ok = 0; break; } if (ok && (s[n] == 0 || s[n] == ' ')) { erstes = i; break; } }
        const char* t = s; if (erstes >= 0) { while (*t && *t != ' ') t++; while (*t == ' ') t++; }
        int punkt = 0; for (const char* q = t; *q; q++) if (*q == '.') punkt = 1;
        if ((erstes >= 0 && erstes <= 5) || (erstes >= 6 && punkt) || (erstes < 0 && (enthaelt(s, "http://") || enthaelt(s, "https://") || enthaelt(s, "www.")))) { in_text = t; return I_WEB; }
    }
    if (enthaelt(s, "schliess") && (enthaelt(s, "fenster") || enthaelt(s, "alle") || enthaelt(s, "alles"))) { in_par = 99; return I_FENSTER; }
    if (enthaelt(s, "close all") || enthaelt(s, "close windows")) { in_par = 99; return I_FENSTER; }
    if (enthaelt(s, "widget")) {
        in_par = (enthaelt(s, "uhr") || enthaelt(s, "clock")) ? 10 : enthaelt(s, "sys") ? 11 : (enthaelt(s, "aufgab") || enthaelt(s, "task")) ? 12 : enthaelt(s, "agent") ? 13 : 14;
        return I_FENSTER;
    }
    if (slen(s) < 26 && (enthaelt(s, "chats") || enthaelt(s, "verlauf") || enthaelt(s, "history"))) { in_par = 4; return I_FENSTER; }
    if (slen(s) < 28 && (enthaelt(s, "gedaechtnis") || enthaelt(s, "erinnerung") || enthaelt(s, "synapse") || enthaelt(s, "memory") || enthaelt(s, "brain") || enthaelt(s, "gehirn") || enthaelt(s, "souvenir") || enthaelt(s, "memoria"))) { in_par = 5; return I_FENSTER; }
    if (enthaelt(s, "neuer chat") || enthaelt(s, "new chat")) { in_par = 30; return I_FENSTER; }
    if (slen(s) < 20 && (enthaelt(s, "anpinnen") || enthaelt(s, "pin chat") || enthaelt(s, "pin this"))) { in_par = 31; return I_FENSTER; }
    if (enthaelt(s, "chat loeschen") || enthaelt(s, "delete chat")) { in_par = 33; return I_FENSTER; }
    { const char* pj = 0; if (s[0] == 'p' && s[1] == 'r' && s[2] == 'o' && s[3] == 'j' && s[4] == 'e' && s[5] == 'k' && s[6] == 't' && s[7] == ' ') pj = s + 8; else if (s[0] == 'p' && s[1] == 'r' && s[2] == 'o' && s[3] == 'j' && s[4] == 'e' && s[5] == 'c' && s[6] == 't' && s[7] == ' ') pj = s + 8;
      if (pj && *pj) { in_text = pj; in_par = 32; return I_FENSTER; } }
    if (enthaelt(s, "einstellung") || enthaelt(s, "settings") || enthaelt(s, "parametres") || enthaelt(s, "impostazioni") || enthaelt(s, "ajustes")) { in_par = 3; return I_FENSTER; }
    if (enthaelt(s, "sprache") || enthaelt(s, "language") || enthaelt(s, "spreche") || enthaelt(s, "langue") || enthaelt(s, "lingua") || enthaelt(s, "idioma") || enthaelt(s, "speak")) {
        if (enthaelt(s, "deutsch") || enthaelt(s, "german") || enthaelt(s, "allemand")) { in_par = 20; return I_FENSTER; }
        if (enthaelt(s, "englisch") || enthaelt(s, "english") || enthaelt(s, "anglais")) { in_par = 21; return I_FENSTER; }
        if (enthaelt(s, "franz") || enthaelt(s, "french") || enthaelt(s, "francais")) { in_par = 22; return I_FENSTER; }
        if (enthaelt(s, "italien") || enthaelt(s, "italian")) { in_par = 23; return I_FENSTER; }
        if (enthaelt(s, "spanisch") || enthaelt(s, "spanish") || enthaelt(s, "espanol") || enthaelt(s, "espagnol")) { in_par = 24; return I_FENSTER; }
        in_par = 3; return I_FENSTER;
    }
    if (enthaelt(s, "rechner") || enthaelt(s, "calculator")) { in_par = 1; return I_FENSTER; }
    if (enthaelt(s, "systeminfo") || enthaelt(s, "system-info") || enthaelt(s, "system info") || enthaelt(s, "fenster info")) { in_par = 2; return I_FENSTER; }
    if (enthaelt(s, "arbeitsfl") || enthaelt(s, "workspace") || enthaelt(s, "wuerfel") || enthaelt(s, "cube")) {
        in_par = -1;
        if (enthaelt(s, "1") || enthaelt(s, "zynie")) in_par = 0; else if (enthaelt(s, "2") || enthaelt(s, "system")) in_par = 1;
        else if (enthaelt(s, "3") || enthaelt(s, "agent")) in_par = 2; else if (enthaelt(s, "4") || enthaelt(s, "aufgab") || enthaelt(s, "task")) in_par = 3;
        return I_FLAECHE;
    }
    if (enthaelt(s, "agent") || enthaelt(s, "briefing")) {
        in_par = -1;
        if (enthaelt(s, "briefing")) in_par = 0; else if (enthaelt(s, "sicher") || enthaelt(s, "secur")) in_par = 1;
        else if (enthaelt(s, "wartung") || enthaelt(s, "mainten")) in_par = 2; else if (enthaelt(s, "aufgab") || enthaelt(s, "task")) in_par = 3;
        return I_AGENT;
    }
    if ((s[0] == 'a' && s[1] == 'u' && s[2] == 'f' && s[3] == 'g' && s[4] == 'a' && s[5] == 'b' && s[6] == 'e') || (s[0] == 't' && s[1] == 'a' && s[2] == 's' && s[3] == 'k' && (s[4] == ' ' || !s[4]))) {
        const char* t = s; while (*t && *t != ' ') t++; while (*t == ' ' || *t == ':') t++;
        in_text = t; return I_AUFGABE;
    }
    if (enthaelt(s, "wartung") || enthaelt(s, "maintenance")) return I_WARTUNG;
    if (enthaelt(s, "rechne") || enthaelt(s, "calc") || hat_rechnung(s)) return I_RECHNEN;
    if (hat(s, w_text)) return I_TEXT;
    if (hat(s, w_q)) return I_QUBIT;
    if (hat(s, w_sec)) return I_SICHER;
    if (hat(s, w_dat)) return I_DATEIEN;
    if (hat(s, w_sys)) return I_SYSTEM;
    if (hat(s, w_out)) return I_ABMELDEN;
    if (hat(s, w_nst)) return I_NEUSTART;
    if (hat(s, w_hlp)) return I_HILFE;
    return I_NICHTS;
}

/* kleiner Rechner: + - * / und Klammern, ganze Zahlen (Division mit Nachkommastellen) */
static const char* rp; static int rfehler;
static int64_t r_ausdruck(void);
static void r_leer(void) { while (*rp == ' ') rp++; }
static int64_t r_faktor(void) {
    r_leer(); int neg = 0; while (*rp == '-' || *rp == '+') { if (*rp == '-') neg = !neg; rp++; r_leer(); }
    int64_t v = 0;
    if (*rp == '(') { rp++; v = r_ausdruck(); r_leer(); if (*rp == ')') rp++; else rfehler = 1; }
    else if (*rp >= '0' && *rp <= '9') { while (*rp >= '0' && *rp <= '9') { v = v * 10 + (*rp - '0'); rp++; if (v > 1000000000000000LL) { rfehler = 1; break; } } }
    else rfehler = 1;
    return neg ? -v : v;
}
static int64_t r_term(void) {
    int64_t v = r_faktor();
    for (;;) {
        r_leer();
        if (*rp == '*' || *rp == 'x' || *rp == 'X') { rp++; v *= r_faktor(); }
        else if (*rp == '/' || *rp == ':') { rp++; int64_t d = r_faktor(); if (d == 0) { rfehler = 2; return 0; } v /= d; }
        else return v;
    }
}
static int64_t r_ausdruck(void) {
    int64_t v = r_term();
    for (;;) { r_leer(); if (*rp == '+') { rp++; v += r_term(); } else if (*rp == '-') { rp++; v -= r_term(); } else return v; }
}
static void i64dec(char* o, int64_t v) { if (v < 0) { *o++ = '-'; v = -v; } char t[24]; int n = 0; if (!v) t[n++] = '0'; while (v) { t[n++] = (char)('0' + v % 10); v /= 10; } while (n) *o++ = t[--n]; *o = 0; }

/* Rechner-Fenster */
enum { K_RB0 = 100 };
static char rexpr[40]; static int rlen; static char rres[40];
static const char* const rtasten[20] = { "C", "(", ")", "/", "7", "8", "9", "*", "4", "5", "6", "-", "1", "2", "3", "+", "0", "00", "<", "=" };
static void rech_zuruecksetzen(void) { rlen = 0; rexpr[0] = 0; rres[0] = 0; }
static void rech_druecke(const char* t) {
    if (t[0] == 'C') { rech_zuruecksetzen(); }
    else if (t[0] == '<') { if (rlen) rexpr[--rlen] = 0; }
    else if (t[0] == '=') {
        if (!rlen) return;
        rp = rexpr; rfehler = 0; int64_t v = r_ausdruck(); r_leer(); if (*rp) rfehler = 1;
        if (rfehler == 2) scopy(rres, TR("= Durch null: nicht moeglich", "= cannot divide by zero"), 40);
        else if (rfehler) scopy(rres, TR("= Fehler in der Rechnung", "= error in expression"), 40);
        else { rres[0] = '='; rres[1] = ' '; i64dec(rres + 2, v); scopy(rexpr, rres + 2, 40); rlen = slen(rexpr); }
        kprint("[DESKTOP] Rechner: "); kprint(rres); kprint("\n");
    } else { for (int i = 0; t[i] && rlen < 36; i++) rexpr[rlen++] = t[i]; rexpr[rlen] = 0; rres[0] = 0; }
}
static void zeichne_rechner(void) {
    int wi = fenster_idx(FT_RECH); if (wi < 0) return;
    struct fenster* f = &fen[wi]; int cx = f->x + 12, cy = f->y + WT_H + 12, cw = f->w - 24;
    int ko = kn_owner; kn_owner = wi; kn_loesche_owner(wi);
    gfx_begin();
    gfx_rect(f->x + 2, f->y + WT_H, f->w - 4, f->h - WT_H - 2, C_BG);
    rrect_rand(cx, cy, cw, 60, 10, C_FIELD, C_BORDER);
    { char a[40]; scopy(a, rexpr, 40); int mc = (cw - 20) / 16; if (slen(a) > mc) { int off = slen(a) - mc; for (int i = 0; i <= mc; i++) a[i] = rexpr[off + i]; }
      gfx_text(cx + cw - 12 - slen(a) * 16, cy + 8, a, C_TEXT, 2); }
    gtxt(cx + 12, cy + 40, rres, C_ACC2, 1);
    for (int i = 0; i < 20; i++) {
        int bx = cx + (i % 4) * (cw / 4), by = cy + 72 + (i / 4) * 48, bw = cw / 4 - 6;
        int op = (i % 4 == 3) || i == 0; rrect_rand(bx, by, bw, 42, 10, i == 19 ? C_ACC : op ? mix(C_CARD, C_ACC, 50) : C_CARD, i == 19 ? C_ACC : C_BORDER);
        gtxt(bx + (bw - gtw(rtasten[i], 1)) / 2, by + 13, rtasten[i], C_TEXT, 1);
        kn_reg(K_RB0 + i, bx, by, bw, 42);
    }
    gfx_end();
    kn_owner = ko;
}
static void rech_taste(uint8_t k, int shift, int altgr) {
    if (k == 0x29) { fenster_schliessen_typ(FT_RECH); return; }
    if (k == 0x28 || k == 0x58) rech_druecke("=");
    else if (k == 0x2A) rech_druecke("<");
    else { char c = usb_key_char(k, shift, altgr); if ((c >= '0' && c <= '9') || c == '+' || c == '-' || c == '*' || c == '/' || c == '(' || c == ')' || c == 'x' || c == ':') { char t[2] = { c, 0 }; rech_druecke(t); } else return; }
    zeichne_rechner();
}
/* System-Info-Fenster */
static void zeichne_info(void) {
    int wi = fenster_idx(FT_INFO); if (wi < 0) return;
    struct fenster* f = &fen[wi]; int x = f->x + 20, y = f->y + WT_H + 16; char z[100], a[12], ip[32] = "";
    gfx_begin(); gfx_rect(f->x + 2, f->y + WT_H, f->w - 4, f->h - WT_H - 2, C_BG);
    scopy(z, TR("Arbeitsspeicher frei: ", "Free memory: "), 100); dec(a, pmm_get_free_pages() / 256); cat(z, a, 100); cat(z, TR(" MiB von ", " MiB of "), 100); dec(a, pmm_get_total_pages() / 256); cat(z, a, 100); cat(z, " MiB", 100); gtxt(x, y, z, C_TEXT, 1);
    scopy(z, TR("Laufzeit: ", "Uptime: "), 100); char t[24]; zeit_text(t, timer_get_seconds()); cat(z, t, 100); gtxt(x, y + 28, z, C_TEXT, 1);
    int ns = net_status(ip, 32); scopy(z, TR("Netzwerk: ", "Network: "), 100); cat(z, ns == 2 ? ip : ns == 1 ? TR("Karte da, keine Adresse", "card found, no address") : TR("keine Karte", "no card"), 100); gtxt(x, y + 56, z, C_TEXT, 1);
    gtxt(x, y + 84, (g_boot.flags & BOOT_SIGNIERT) ? TR("Start: Kernel durch Signatur beglaubigt", "Boot: kernel verified by signature") : TR("Start: nicht durch Signatur beglaubigt", "Boot: not verified by signature"), (g_boot.flags & BOOT_SIGNIERT) ? C_OK : C_ERR, 1);
    scopy(z, TR("Benutzer: ", "User: "), 100); cat(z, benutzer_name(), 100); cat(z, " (", 100); cat(z, benutzer_rollentext(benutzer_rolle()), 100); cat(z, ")", 100); gtxt(x, y + 112, z, C_MUTED, 1);
    gtxt(x, y + 150, TR("Esc: Fenster schliessen", "Esc: close window"), C_MUTED, 1);
    gfx_end();
}

/* ---------- Einstellungen ---------- */
enum { K_EIN0 = 300 };
static void ki_busy_cb(const char* t, int n) { (void)t; (void)n; busy_tick(); }
static void ascii_fold(char* t) {                                        /* UTF-8 -> reines ASCII (die Schrift kennt keine Akzente), erste Zeile */
    int o = 0;
    for (int i = 0; t[i]; i++) {
        uint8_t c = (uint8_t)t[i];
        if (c == '\n' || c == '\r') break;
        if (c < 128) t[o++] = (char)c;
        else if (c == 0xC3 && t[i + 1]) {
            uint8_t d = (uint8_t)t[++i];
            char r = (d >= 0xA0 && d <= 0xA5) || (d >= 0x80 && d <= 0x85) ? 'a' : (d == 0xA7 || d == 0x87) ? 'c' : (d >= 0xA8 && d <= 0xAB) || (d >= 0x88 && d <= 0x8B) ? 'e' : (d >= 0xAC && d <= 0xAF) || (d >= 0x8C && d <= 0x8F) ? 'i' : (d == 0xB1 || d == 0x91) ? 'n' : (d >= 0xB2 && d <= 0xB6) || (d >= 0x92 && d <= 0x96) ? 'o' : (d >= 0xB9 && d <= 0xBC) || (d >= 0x99 && d <= 0x9C) ? 'u' : 0;
            if (r) t[o++] = r;
        } else while (t[i + 1] && (((uint8_t)t[i + 1]) & 0xC0) == 0x80) i++;
    }
    while (o && (t[o - 1] == ' ' || t[o - 1] == '"')) o--;
    t[o] = 0;
}
static void sprachpaket(void) {                                          /* alle bisher gesehenen Texte per KI uebersetzen und in ZynDB ablegen */
    if (lang < 2 || !gehirn_vorhanden()) { reply1(TR("Dafuer brauche ich eine Sprache ausser Deutsch/Englisch und das Sprachmodell.", "That needs a language other than German/English and the language model.")); return; }
    int total = npend, ok = 0; static char out[160]; char p[240], key[16];
    zdb_stapel(1);
    for (int i = 0; i < total; i++) {
        busy_tick();
        scopy(p, "Translate into ", 240); cat(p, LENG[lang], 240); cat(p, ". Answer only with the translation, nothing else: ", 240); cat(p, pend_en[i], 240);
        gehirn_vergessen();
        int n = gehirn_frage(p, out, sizeof(out), ki_busy_cb); if (n <= 0) continue;
        ascii_fold(out); if (!out[0]) continue;
        ui_key(lang, pend_en[i], key); zdb_put(key, out); ok++;
    }
    zdb_stapel(0); npend = 0; busy_ende();
    kprint("[DESKTOP] Sprachpaket: "); kprint_dec((uint32_t)ok); kprint(" von "); kprint_dec((uint32_t)total); kprint(" Texten uebersetzt\n");
}
static void einst_klick(int id) {
    int k = id - K_EIN0;
    if (k >= 0 && k < 5) lang_setze(k);
    else if (k == 5 || k == 6) { int n = usb_get_layout() + (k == 5 ? -1 : 1); if (n < 0) n = KBD_LAYOUTS - 1; if (n >= KBD_LAYOUTS) n = 0; usb_set_layout(n); zdb_put("system.tastatur", kbd_layouts[n].code); }
    else if (k >= 7 && k <= 9) { fw_modus(k == 7 ? FW_AUS : k == 8 ? FW_NORMAL : FW_STRENG); fw_speichern(); }
    else if (k >= 10 && k <= 13) {
        int q = k - 10;
        if (q && !ext_schluessel_da(q)) { char m[100]; scopy(m, TR("Zuerst den Schluessel eintragen: Wartung, kiextern einrichten ", "First enter the key: maintenance, kiextern einrichten "), 100); cat(m, ext_kennung(q), 100); reply1(m); }
        else ext_setze_quelle(q);
    }
    else if (k == 14) { sprachpaket(); return; }
    else if (k == 11) { sprachpaket(); return; }
    zeichne_desktop();
}
static void zeichne_einst(void) {
    int wi = fenster_idx(FT_EINST); if (wi < 0) return;
    struct fenster* f = &fen[wi]; int x = f->x + 20, y = f->y + WT_H + 14, ko = kn_owner; kn_owner = wi; kn_loesche_owner(wi);
    gfx_begin(); gfx_rect(f->x + 2, f->y + WT_H, f->w - 4, f->h - WT_H - 2, C_BG);
    gtxt(x, y, T5("Sprache", "Language", "Langue", "Lingua", "Idioma"), C_MUTED, 1);
    for (int i = 0; i < 5; i++) knopf(K_EIN0 + i, LNAME[i], x + i * 106, y + 22, 100, i == lang);
    y += 78;
    { int c = usb_get_layout(); char t[60]; scopy(t, T5("Tastatur: ", "Keyboard: ", "Clavier : ", "Tastiera: ", "Teclado: "), 60); cat(t, kbd_layouts[c].code, 60); cat(t, " (", 60); cat(t, kbd_layouts[c].name, 60); cat(t, ")", 60); gtxt(x, y + 9, t, C_TEXT, 1); }
    knopf(K_EIN0 + 5, "<", f->x + f->w - 100, y, 36, 0); knopf(K_EIN0 + 6, ">", f->x + f->w - 56, y, 36, 0);
    y += 52; gtxt(x, y, "Firewall", C_MUTED, 1);
    { int m = fw_modus_get(); knopf(K_EIN0 + 7, T5("Aus", "Off", "Desactive", "Spento", "Apagado"), x, y + 22, 110, m == 0); knopf(K_EIN0 + 8, "Normal", x + 118, y + 22, 110, m == 1); knopf(K_EIN0 + 9, T5("Streng", "Strict", "Strict", "Rigido", "Estricto"), x + 236, y + 22, 110, m == 2); }
    y += 78; gtxt(x, y, T5("Sprachmodell (KI)", "Language model (AI)", "Modele de langage (IA)", "Modello linguistico (IA)", "Modelo de lenguaje (IA)"), C_MUTED, 1);
    { int qa = ext_quelle(); for (int i = 0; i < 4; i++) knopf(K_EIN0 + 10 + i, ext_name(i), x + i * 106, y + 22, 100, i == qa);
      gtxt(x, y + 62, qa ? TR("Achtung: Fragen gehen an den Anbieter. Zynie (intern) bleibt Rueckfall.", "Warning: questions go to the provider. Zynie (internal) stays as fallback.") : gehirn_vorhanden() ? TR("Alles bleibt auf diesem Geraet. Die interne KI laesst sich nicht abschalten.", "Everything stays on this device. The internal AI cannot be switched off.") : TR("kein internes Sprachmodell geladen", "no internal language model loaded"), qa ? 0x00F59E0Bu : C_TEXT, 1);
      if (lang >= 2 && gehirn_vorhanden()) knopf(K_EIN0 + 14, T5("Sprachpaket", "Language pack", "Pack de langue", "Pacchetto lingua", "Paquete de idioma"), x, y + 84, 200, 0); }
    y += 128;
    gtxt(x, y, (synfs_is_mounted() && synfs_krypto_hdr()->magic == SYNFS_KRY_MAGIC) ? TR("Festplatte: verschluesselt (256-Bit)", "Disk: encrypted (256-bit)") : TR("Festplatte: nicht verschluesselt (Shell: krypto aktivieren)", "Disk: not encrypted (shell: krypto aktivieren)"), C_TEXT, 1);
    gtxt(x, y + 24, zdb_offen() ? TR("Alles wird in ZynDB gespeichert (verschluesselt)", "Everything is stored in ZynDB (encrypted)") : TR("ZynDB nicht verfuegbar", "ZynDB unavailable"), C_MUTED, 1);
    gfx_end();
    lader(f->x + f->w - 60, f->y + f->h - 56, 34, timer_get_ticks() / 5, C_BG);
    kn_owner = ko;
}

/* ---------- Gedaechtnis: das Synapsen-Netz sehen ---------- */
enum { K_GED_SCHLAF = 500, K_GED_EXT, K_GED_VERG };
static int ged_vfrage;                                  /* "Alles vergessen" wartet auf die zweite Bestaetigung */
static int isqrt_i(int x) { int r = 0; while ((r + 1) * (r + 1) <= x) r++; return r; }
static void linie(int x0, int y0, int x1, int y1, uint32_t c) {
    int dx = x1 - x0, dy = y1 - y0, ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy, n = ax > ay ? ax : ay; if (!n) return;
    for (int i = 0; i <= n; i += 2) gfx_rect(x0 + dx * i / n, y0 + dy * i / n, 2, 2, c);
}
static void ged_klick(int id) {
    if (id == K_GED_SCHLAF) { ged_schlafen(); ged_vfrage = 0; }
    else if (id == K_GED_EXT) { zdb_put("ged.extern", zdb_geti("ged.extern", 0) ? "0" : "1"); ged_vfrage = 0; }
    else if (id == K_GED_VERG) { if (ged_vfrage) { ged_loeschen(); ged_vfrage = 0; } else ged_vfrage = 1; }
    zeichne_desktop();
}
static void zeichne_ged(void) {
    int wi = fenster_idx(FT_GED); if (wi < 0) return;
    struct fenster* f = &fen[wi]; int ko = kn_owner; kn_owner = wi; kn_loesche_owner(wi);
    int x0 = f->x + 2, y0 = f->y + WT_H, bh = f->h - WT_H - 2, pw = 420, ph = bh - 100;
    gfx_begin(); gfx_rect(x0, y0, f->w - 4, bh, C_BG);
    int nn, ss, gg, tt; ged_zahlen(&nn, &ss, &gg, &tt);
    { char t[120], a[12]; dec(a, (uint32_t)nn); scopy(t, a, 120); cat(t, TR(" Neuronen   ", " neurons   "), 120); dec(a, (uint32_t)ss); cat(t, a, 120); cat(t, TR(" Synapsen   ", " synapses   "), 120); dec(a, (uint32_t)gg); cat(t, a, 120); cat(t, TR(" Erinnerungen", " memories"), 120);
      gtxt(x0 + 14, y0 + 10, t, C_TEXT, 1); }
    int px0 = x0 + 8, py0 = y0 + 36; rrect_rand(px0, py0, pw, ph, 10, C_CARD, C_BORDER);
    char nm[36][13]; int nu[36], er[36], sa[160], sb[160], sw[160], ns2 = 0, bx[36], by[36];
    int c = ged_netz(36, nm, nu, er, 160, sa, sb, sw, &ns2);
    int cx = px0 + pw / 2, cy = py0 + ph / 2, rx = pw / 2 - 40, ry = ph / 2 - 26;
    for (int i = 0; i < c; i++) {                                                                 /* haeufigste Neuronen innen, Goldener Winkel nach aussen */
        int deg = (i * 1375 / 10) % 360, s = c > 1 ? isqrt_i(i * 1000 / c) : 0;
        bx[i] = cx + rx * s / 31 * icos(deg) / 1024; by[i] = cy + ry * s / 31 * isin(deg) / 1024;
    }
    for (int j = 0; j < ns2; j++) if (sw[j] >= 20) linie(bx[sa[j]], by[sa[j]], bx[sb[j]], by[sb[j]], mix(0x00243049u, C_ACC, sw[j] * 256 / 255));
    for (int i = 0; i < c; i++) {
        int r = 3 + (nu[i] > 56 ? 7 : nu[i] / 8); int hell = er[i] >= 500;
        if (hell) rrect(bx[i] - r - 5, by[i] - r - 5, 2 * r + 10, 2 * r + 10, r + 5, mix(C_CARD, C_ACC2, 110));
        rrect(bx[i] - r, by[i] - r, 2 * r, 2 * r, r, hell ? C_ACC2 : mix(0x00334155u, C_ACC, 80 + (nu[i] > 20 ? 176 : nu[i] * 8)));
        if (i < 14) gtxt(bx[i] + r + 3, by[i] - 7, nm[i], hell ? C_TEXT : C_MUTED, 1);
    }
    if (!c) gtxt(px0 + 24, py0 + ph / 2 - 8, TR("Noch leer. Sag: merk dir, dass mein Hund Bello heisst.", "Still empty. Say: remember that my dog is called Bello."), C_MUTED, 1);
    int rx0 = px0 + pw + 14, rw = f->w - 4 - (rx0 - x0) - 10;
    gtxt(rx0, y0 + 10, TR("Erinnerungen", "Memories"), C_ACC2, 1);
    { int rows = (bh - 100) / 46; if (rows > 9) rows = 9; int cols = (rw - 8) / 8;
      for (int i = 0; i < rows; i++) {
          char t[112]; int art, st; if (!ged_erinnerung(i, t, 112, &art, &st)) break;
          int ry2 = y0 + 36 + i * 46; rrect_rand(rx0, ry2, rw, 42, 8, C_CARD, C_BORDER);
          char l1_[40], l2_[40]; int len = slen(t), cut = len > cols ? cols : len; if (cut < len) { int sp = cut; while (sp > cols / 2 && t[sp] != ' ') sp--; if (t[sp] == ' ') cut = sp; }
          int k = 0; for (; k < cut && k < 39; k++) l1_[k] = t[k]; l1_[k] = 0; int o = cut; while (t[o] == ' ') o++;
          k = 0; for (; t[o] && k < 39 && k < cols - 2; k++, o++) l2_[k] = t[o]; l2_[k] = 0; if (t[o]) { l2_[slen(l2_) > 2 ? slen(l2_) - 2 : 0] = 0; cat(l2_, "..", 40); }
          gtxt(rx0 + 8, ry2 + 3, l1_, C_TEXT, 1); gtxt(rx0 + 8, ry2 + 18, l2_, C_MUTED, 1);
          rrect(rx0 + 8, ry2 + 36, 4 + st * (rw - 24) / 255, 3, 1, art == 2 ? 0x00F59E0Bu : art == 1 ? C_ACC2 : C_ACC);
      }
      if (!gg) gtxt(rx0, y0 + 40, TR("Noch keine.", "None yet."), C_MUTED, 1); }
    int yb = y0 + bh - 52;
    gtxt(x0 + 14, yb - 24, TR("Aus deinen Gespraechen lernt Zynie Verbindungen. Wichtiges bleibt, Unwichtiges verblasst.", "Zynie learns connections from your chats. Important things stay, unimportant ones fade."), C_MUTED, 1);
    knopf(K_GED_SCHLAF, TR("Schlafen", "Sleep"), x0 + 12, yb, 130, 0);
    knopf(K_GED_EXT, zdb_geti("ged.extern", 0) ? TR("Extern teilen: an", "Share external: on") : TR("Extern teilen: aus", "Share external: off"), x0 + 150, yb, 230, zdb_geti("ged.extern", 0));
    knopf(K_GED_VERG, ged_vfrage ? TR("Wirklich alles?", "Really all?") : TR("Alles vergessen", "Forget all"), f->x + f->w - 214, yb, 200, ged_vfrage);
    gfx_end(); kn_owner = ko;
}

/* ---------- Chats (Verlauf, anpinnen, Projekte) ---------- */
enum { K_CH0 = 400, K_CH_NEU = 420, K_CH_PIN, K_CH_FILTER, K_CH_DEL };
static int ch_filter;                                   /* 0 = alle Projekte, sonst Index in der Projektliste + 1 */
static void ch_filtername(char* o) {
    static char pn[8][20]; int np = chat_projekte(pn, 8); o[0] = 0;
    if (ch_filter > np) ch_filter = 0;
    if (ch_filter > 0) scopy(o, pn[ch_filter - 1], 20);
}
static void chats_klick(int id) {
    char fn[20]; ch_filtername(fn); int ids[16], n = chat_liste(ids, 16, fn);
    if (id >= K_CH0 && id < K_CH0 + 16) { if (id - K_CH0 < n) chat_waehle(ids[id - K_CH0]); }
    else if (id == K_CH_NEU) chat_neu(TR("Neuer Chat", "New chat"));
    else if (id == K_CH_PIN) { if (chat_aktiv()) chat_pin_umschalten(chat_aktiv()); }
    else if (id == K_CH_FILTER) { char pn[8][20]; int np = chat_projekte(pn, 8); ch_filter = (ch_filter + 1) % (np + 1); }
    else if (id == K_CH_DEL) { if (chat_aktiv()) chat_loeschen(chat_aktiv()); }
    zeichne_desktop();
}
static void zeichne_chats(void) {
    int wi = fenster_idx(FT_CHATS); if (wi < 0) return;
    struct fenster* f = &fen[wi]; int ko = kn_owner; kn_owner = wi; kn_loesche_owner(wi);
    int lw = 230, x0 = f->x + 2, y0 = f->y + WT_H, rx = x0 + lw + 14, rw = f->w - lw - 24, bh = f->h - WT_H - 4;
    gfx_begin(); gfx_rect(x0, y0, f->w - 4, bh, C_BG);
    rrect_rand(x0 + 8, y0 + 8, lw, bh - 16, 10, C_CARD, C_BORDER);
    char fn[20]; ch_filtername(fn); int ids[16], n = chat_liste(ids, 16, fn), akt = chat_aktiv();
    int rows = (bh - 40) / 40; if (n > rows) n = rows;
    for (int i = 0; i < n; i++) {
        int ry = y0 + 16 + i * 40; char t[40];
        if (ids[i] == akt) rrect(x0 + 12, ry, lw - 8, 36, 8, mix(C_CARD, C_ACC, 90));
        t[0] = 0; if (chat_gepinnt(ids[i])) cat(t, "* ", 40); cat(t, chat_titel(ids[i]), 40); if (slen(t) > 25) t[25] = 0;
        gtxt(x0 + 22, ry + 3, t, C_TEXT, 1);
        if (chat_projekt(ids[i])[0]) { scopy(t, chat_projekt(ids[i]), 40); if (slen(t) > 25) t[25] = 0; gtxt(x0 + 22, ry + 19, t, C_MUTED, 1); }
        kn_reg(K_CH0 + i, x0 + 12, ry, lw - 8, 36);
    }
    if (!n) gtxt(x0 + 22, y0 + 24, TR("Noch keine Chats.", "No chats yet."), C_MUTED, 1);
    int bx = rx; int by = y0 + 8;
    knopf(K_CH_NEU, TR("Neu", "New"), bx, by, 70, 1);
    knopf(K_CH_PIN, akt && chat_gepinnt(akt) ? TR("Geloest", "Unpin") : TR("Anpinnen", "Pin"), bx + 78, by, 110, 0);
    { char t[40]; scopy(t, TR("Projekt: ", "Project: "), 40); cat(t, fn[0] ? fn : TR("alle", "all"), 40); if (slen(t) > 20) t[20] = 0; knopf(K_CH_FILTER, t, bx + 196, by, 190, 0); }
    knopf(K_CH_DEL, TR("Loeschen", "Delete"), bx + 394, by, 100, 0);
    if (akt) {
        char t[80]; scopy(t, chat_titel(akt), 80); if (chat_projekt(akt)[0]) { cat(t, "   [", 80); cat(t, chat_projekt(akt), 80); cat(t, "]", 80); }
        gtxt(rx, y0 + 56, t, C_ACC2, 1);
        static char txt[3100], wl[160][84]; static uint8_t wr[160]; int nl = 0, cols = (rw - 8) / 8; if (cols > 80) cols = 80;
        int tl = chat_text(akt, txt, sizeof(txt)); (void)tl;
        for (int i = 0; txt[i] && nl < 150; ) {
            int e = i; while (txt[e] && txt[e] != '\n') e++;
            int du = txt[i] == 'U'; char pre[12]; scopy(pre, du ? TR("Du: ", "You: ") : "Zynie: ", 12); int pl = slen(pre);
            char z[260]; scopy(z, pre, 260); for (int k = i + 2; k < e && slen(z) < 255; k++) { char c[2] = { txt[k], 0 }; cat(z, c, 260); }
            for (int p = 0; z[p] && nl < 150; ) { int len = slen(z + p); if (len > cols) { len = cols; int sp = len; while (sp > cols / 2 && z[p + sp] != ' ') sp--; if (z[p + sp] == ' ') len = sp; }
                for (int k = 0; k < len; k++) wl[nl][k] = z[p + k]; wl[nl][len] = 0; wr[nl] = (uint8_t)du; nl++; p += len; while (z[p] == ' ') p++; }
            (void)pl; i = txt[e] ? e + 1 : e;
        }
        int vis = (bh - 96) / 18; int von = nl > vis ? nl - vis : 0;
        for (int i = von; i < nl; i++) gtxt(rx, y0 + 84 + (i - von) * 18, wl[i], wr[i] ? C_ACC2 : C_TEXT, 1);
        if (!nl) gtxt(rx, y0 + 84, TR("Leer. Schreib unten in den Chat.", "Empty. Type in the chat below."), C_MUTED, 1);
    } else gtxt(rx, y0 + 60, TR("Schreib Zynie etwas, dann entsteht hier dein Verlauf.", "Write to Zynie and your history appears here."), C_MUTED, 1);
    gfx_end();
    kn_owner = ko;
}

static void intent_rechnen(const char* roh) {
    char ex[96]; int k = 0;
    for (const char* s = roh; *s && k < 94; s++) if ((*s >= '0' && *s <= '9') || *s == '+' || *s == '-' || *s == '*' || *s == '/' || *s == '(' || *s == ')' || *s == 'x' || *s == ':' || *s == ' ') ex[k++] = *s;
    ex[k] = 0;
    rp = ex; rfehler = 0; int64_t v = r_ausdruck(); r_leer();
    if (*rp) rfehler = 1;
    if (rfehler == 2) { reply1(TR("Durch null kann man nicht teilen.", "You cannot divide by zero.")); return; }
    if (rfehler) { reply1(TR("Das konnte ich nicht ausrechnen. Beispiel: rechne (12+3)*7", "I could not work that out. Example: calc (12+3)*7")); return; }
    char o[110]; scopy(o, ex, 60); cat(o, " = ", 110); char z[28]; i64dec(z, v); cat(o, z, 110);
    reply1(o);
    kprint("[DESKTOP] Rechnung: "); kprint(o); kprint("\n");
}
static void intent_system(void) {
    char a[12], z1[100], z2[100], z3[100], z4[100]; char ip[32] = "";
    scopy(z1, TR("Arbeitsspeicher frei: ", "Free memory: "), 100); dec(a, pmm_get_free_pages() / 256); cat(z1, a, 100); cat(z1, TR(" MiB von ", " MiB of "), 100); dec(a, pmm_get_total_pages() / 256); cat(z1, a, 100); cat(z1, " MiB", 100);
    scopy(z2, TR("Laufzeit: ", "Uptime: "), 100); dec(a, timer_get_seconds()); cat(z2, a, 100); cat(z2, " s", 100);
    int ns = net_status(ip, 32);
    scopy(z3, TR("Netzwerk: ", "Network: "), 100); cat(z3, ns == 2 ? ip : ns == 1 ? TR("Karte da, keine Adresse", "card found, no address") : TR("keine Karte", "no card"), 100);
    scopy(z4, TR("Start: ", "Boot: "), 100); cat(z4, (g_boot.flags & BOOT_SIGNIERT) ? TR("Kernel durch Signatur beglaubigt", "kernel verified by signature") : TR("nicht durch Signatur beglaubigt", "not verified by signature"), 100);
    antwort_setze(TR("So geht es dem System:", "System status:"), z1, z2, z3, z4, 0);
}
static void intent_dateien(void) {
    char zeile[100]; scopy(zeile, "", 100); int n = 0; char buf[2];
    for (int i = 1; i < 30; i++) {
        char a[8], nm[24]; dec(a, (uint32_t)i); scopy(nm, "dokument-", 24); cat(nm, a, 24); cat(nm, ".txt", 24);
        if (synfs_read(nm, buf, 1) >= 0) { if (n) cat(zeile, ",  ", 100); cat(zeile, nm, 100); n++; }
    }
    if (!n) reply1(TR("Du hast noch keine Dokumente. Sag zum Beispiel: text erstellen", "You have no documents yet. Try saying: text erstellen"));
    else antwort_setze(TR("Deine Dokumente:", "Your documents:"), zeile, 0, 0, 0, 0);
}
static void intent_qubit(void) {
    qubit_reset(2); qubit_gate('h', 0); qubit_cnot(0, 1);
    char z1[100], z2[100], a[12];
    int p00 = qubit_wahrscheinlichkeit(0) * 100 / Q_EINS, p11 = qubit_wahrscheinlichkeit(3) * 100 / Q_EINS;
    scopy(z1, TR("Zwei verschraenkte Qubits (Bell-Zustand): |00> ", "Two entangled qubits (Bell state): |00> "), 100); dec(a, (uint32_t)p00); cat(z1, a, 100); cat(z1, "%, |11> ", 100); dec(a, (uint32_t)p11); cat(z1, a, 100); cat(z1, "%", 100);
    int m = qubit_messen_alle();
    scopy(z2, TR("Messung: ", "Measurement: "), 100); cat(z2, m == 0 ? "|00>" : m == 3 ? "|11>" : "?", 100); cat(z2, TR("  - beide Qubits zeigen immer dasselbe.", "  - both qubits always agree."), 100);
    antwort_setze(TR("Der Quantenkern im Kernel (Simulation, 1-4 Qubits):", "The quantum core in the kernel (simulation, 1-4 qubits):"), z1, z2, 0, 0, 0);
    kprint("[DESKTOP] Quanten-Demo: "); kprint(z2); kprint("\n");
}

/* Zynie antwortet mit dem Sprachmodell: jedes neue Textstueck erscheint sofort in der Sprechblase, der Mund bewegt sich */
static char ki_text[520]; static int ki_len;
static void ki_zeilen(void) {
    nantwort = 0; int i = 0;
    while (i < ki_len && nantwort < 6) {
        int n = ki_len - i; if (n > 88) { n = 88; int b = n; while (b > 40 && ki_text[i + b] != ' ') b--; if (b > 40) n = b; }
        int k = 0; for (int j = 0; j < n && k < 99; j++) antwort[nantwort][k++] = ki_text[i + j]; antwort[nantwort][k] = 0; nantwort++;
        i += n; while (i < ki_len && ki_text[i] == ' ') i++;
    }
}
static void ki_teil(const char* t, int n) {
    busy_tick();
    for (int i = 0; i < n && ki_len < (int)sizeof(ki_text) - 4; i++) {
        unsigned char c = (unsigned char)t[i];
        if (c == '\n') { if (ki_len && ki_text[ki_len - 1] != ' ') ki_text[ki_len++] = ' '; }
        else if (c < 0x80) ki_text[ki_len++] = (char)c;
        else if (c == 0xC3 && i + 1 < n) {                                            /* Umlaute (UTF-8) als ae/oe/ue/ss */
            unsigned char d = (unsigned char)t[++i];
            const char* r = d == 0xA4 ? "ae" : d == 0xB6 ? "oe" : d == 0xBC ? "ue" : d == 0x9F ? "ss" : d == 0x84 ? "Ae" : d == 0x96 ? "Oe" : d == 0x9C ? "Ue" : "";
            while (*r) ki_text[ki_len++] = *r++;
        }
    }
    uint32_t now = timer_get_ticks(); sprech_bis = now + 150; letzte_eingabe = now;
    ki_zeilen(); zeichne_antwort();
    robot_phase = now / 6; zeichne_robot();
}
static int ki_gefragt;                                 /* diese Eingabe ging an ein Sprachmodell: das Gedaechtnis lernt daraus */
static void ki_antwort(const char* frage) {
    ki_gefragt = 1; ki_len = 0;
    reply1(TR("Moment, ich denke nach ...", "One moment, thinking ...")); zeichne_antwort(); zeichne_robot();
    static char roh[600]; char fq[330];
    if (lang != 0) { scopy(fq, "Answer in ", 330); cat(fq, LENG[lang], 330); cat(fq, ". ", 330); cat(fq, frage, 330); } else scopy(fq, frage, 330);
    busy_tick();
    static char gk[560], ef[800];
    int teilen = zdb_geti("ged.extern", 0) != 0;                              /* Erinnerungen gehen nur mit Erlaubnis an externe Anbieter */
    int gkl = ged_kontext(frage, gk, sizeof(gk), ext_quelle() ? teilen : 1);
    if (ext_quelle()) {
        ef[0] = 0; if (gkl) { scopy(ef, gk, 800); cat(ef, "Frage: ", 800); } cat(ef, fq, 800);
        int en = ext_frage(ef, roh, sizeof(roh));
        if (en > 0) { ki_teil(roh, en); ki_text[ki_len] = 0; ki_zeilen(); busy_ende(); sprech_bis = timer_get_ticks() + 100; kprint("[DESKTOP] "); kprint(ext_name(ext_quelle())); kprint(" antwortet: "); kprint(ki_text); kprint("\n"); return; }
        kprint("[DESKTOP] Externe KI fehlgeschlagen: "); kprint(roh); kprint(" - Zynie (intern) antwortet\n");
        if (!gehirn_vorhanden()) { busy_ende(); reply1(roh); return; }
        ki_len = 0;
        if (!gkl || !teilen) { gkl = ged_kontext(frage, gk, sizeof(gk), 1); }      /* Rueckfall auf die interne KI: lokal darf sie alles wissen */
    }
    gehirn_kontext(gkl ? gk : "");
    int n = gehirn_frage(fq, roh, sizeof(roh), ki_teil);
    busy_ende();
    if (n < 0) { reply1(TR("Mein Sprachmodell ist gerade nicht verfuegbar.", "My language model is not available right now.")); kprint("[DESKTOP] Zynie: Sprachmodell nicht verfuegbar: "); kprint(gehirn_status()); kprint("\n"); return; }
    ki_text[ki_len] = 0; ki_zeilen();
    sprech_bis = timer_get_ticks() + 100; kprint("[DESKTOP] Zynie antwortet: "); kprint(ki_text); kprint("\n");
}

/* Erinnern und Vergessen per Sprache: "merk dir ...", "vergiss ...", "was weisst du" */
static int ged_beginnt(const char* s, const char* const* liste, const char** rest) {
    for (int i = 0; liste[i]; i++) { int n = slen(liste[i]), ok = 1; for (int j = 0; j < n; j++) if (s[j] != liste[i][j]) { ok = 0; break; } if (ok) { *rest = s + n; return 1; } }
    return 0;
}
static int ged_sprache(const char* s, const char* roh) {
    static const char* const M[] = { "merk dir ", "merke dir ", "bitte merk dir ", "remember that ", "remember ", "souviens-toi ", "souviens toi ", "ricorda ", "ricordati ", "recuerda ", "acuerdate ", 0 };
    static const char* const V[] = { "vergiss ", "vergesse ", "forget ", "oublie ", "oubliez ", "dimentica ", "dimenticare ", "olvida ", "olvidate ", 0 };
    static const char* const D[] = { "dass ", "that ", "que ", "qu'", "che ", "de que ", 0 };
    static const char* const ALLE[] = { "alles", "all", "tout", "tutto", "todo", 0 };
    static const char* const JA[] = { "ja", "yes", "y", "oui", "si", "s", 0 };
    const char* r;
    if (ged_beginnt(s, M, &r)) {
        const char* r2; if (ged_beginnt(r, D, &r2)) r = r2;
        const char* t = roh + (r - s);
        if (!*t) { reply1(TR("Was soll ich mir merken?", "What should I remember?")); }
        else { int rr = ged_merke(t, 1); reply1(rr == 0 ? TR("Gemerkt. Das vergesse ich nicht.", "Noted. I will not forget that.") : rr == -2 ? TR("Passwoerter, Schluessel und Kontonummern merke ich mir nicht.", "I never remember passwords, keys or account numbers.") : TR("Das konnte ich nicht speichern.", "I could not save that.")); }
        return 1;
    }
    if (ged_beginnt(s, V, &r)) {
        const char* r2; if (ged_beginnt(r, D, &r2)) r = r2;
        const char* t = roh + (r - s);
        { /* "alles" / "all" / "tout" / "tutto" / "todo", optional gefolgt von "ja" / "yes" / "oui" / "si" */
            int wl = 0; while (r[wl] && r[wl] != ' ') wl++;
            int alle = 0; for (int i = 0; ALLE[i]; i++) { int n = slen(ALLE[i]); if (n == wl) { int ok = 1; for (int j = 0; j < n; j++) if (r[j] != ALLE[i][j]) { ok = 0; break; } if (ok) alle = 1; } }
            if (alle && slen(r) < 14) {
                int ja = 0; const char* q = r + wl; while (*q == ' ') q++;
                for (int i = 0; JA[i]; i++) { int n = slen(JA[i]), ok = 1; for (int j = 0; j < n; j++) if (q[j] != JA[i][j]) { ok = 0; break; } if (ok && !q[n]) ja = 1; }
                if (ja) { ged_loeschen(); reply1(TR("Ich habe mein ganzes Gedaechtnis geleert.", "I have cleared my whole memory.")); }
                else reply1(TR("Wirklich alles? Sag: vergiss alles ja", "Really everything? Say: forget all yes"));
                return 1;
            }
        }
        int n = ged_vergiss(t);
        if (n) { char m[100], a[12]; dec(a, (uint32_t)n); scopy(m, a, 100); cat(m, TR(" Erinnerung(en) vergessen.", " memory(ies) forgotten."), 100); reply1(m); }
        else reply1(TR("Dazu habe ich nichts gefunden.", "I found nothing about that."));
        return 1;
    }
    if ((enthaelt(s, "was weisst du") || enthaelt(s, "what do you know") || enthaelt(s, "woran erinnerst") || enthaelt(s, "que sais-tu") || enthaelt(s, "que sais tu") || enthaelt(s, "que sais-je") || enthaelt(s, "cosa sai") || enthaelt(s, "che cosa sai") || enthaelt(s, "que sabes") || enthaelt(s, "qu'est-ce que tu sais"))
        && (slen(s) < 24 || enthaelt(s, "ueber mich") || enthaelt(s, "about me") || enthaelt(s, "sur moi") || enthaelt(s, "di me") || enthaelt(s, "de mi") || enthaelt(s, "sobre mi"))) {
        static char z[4][100]; int c = 0, art, st; char t[112];
        for (int i = 0; c < 4 && ged_erinnerung(i, t, 112, &art, &st); i++) { if (art == 0 && c >= 2) continue; scopy(z[c], t, 100); c++; }
        if (!c) reply1(TR("Noch nichts. Sag zum Beispiel: merk dir, dass mein Hund Bello heisst.", "Nothing yet. Say for example: remember that my dog is called Bello."));
        else antwort_setze(TR("Das weiss ich:", "This is what I know:"), z[0], c > 1 ? z[1] : 0, c > 2 ? z[2] : 0, c > 3 ? z[3] : 0, 0);
        return 1;
    }
    return 0;
}

static void ausfuehren_roh(void) {
    char s[96]; int k = 0; for (int i = 0; eingabe[i] && k < 94; i++) { char c = eingabe[i]; s[k++] = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; } s[k] = 0;
    char roh[96]; scopy(roh, eingabe, 96);
    int in = erkenne(s);
    if (ki_nutzbar() && in != I_WARTUNG && in != I_RECHNEN && in != I_AGENT && in != I_AUFGABE && in != I_FLAECHE && in != I_WEB && in != I_FENSTER) { int w = 1; for (int i = 0; s[i]; i++) if (s[i] == ' ' && s[i + 1] && s[i + 1] != ' ') w++; if (w > 3) in = I_NICHTS; }   /* ganze Saetze gehen an das Sprachmodell */
    einlen = 0; eingabe[0] = 0; nsug = 0; sugsel = -1; sel1 = 0;
    if (!k) return;
    hist_add(roh);
    if (ged_sprache(s, roh)) { kprint("[DESKTOP] Gedaechtnis\n"); zeichne_antwort(); zeichne_eingabe(); return; }
    kprint("[DESKTOP] Intent '"); kprint(roh); kprint("' -> ");
    switch (in) {
    case I_TEXT: kprint("Textverarbeitung\n"); editor_oeffnen(); return;
    case I_RECHNEN: kprint("Rechnen\n"); intent_rechnen(roh); break;
    case I_SYSTEM: kprint("System\n"); intent_system(); break;
    case I_DATEIEN: kprint("Dateien\n"); intent_dateien(); break;
    case I_QUBIT: kprint("Quanten\n"); intent_qubit(); break;
    case I_SICHER: kprint("Sicherheit\n"); antwort_setze(TR("Sicherheit der Startkette:", "Boot chain security:"),
            (g_boot.flags & BOOT_SIGNIERT) ? TR("Kernel und Startzeile sind durch eine hash-basierte LMS-Signatur (quantensicher) geprueft.", "Kernel and boot line are verified by a hash-based LMS signature (quantum-safe).")
                                         : TR("ACHTUNG: Der Kernel wurde nicht durch eine Signatur geprueft.", "WARNING: the kernel was not verified by a signature."),
            fw_modus_get() == 0 ? TR("Firewall: AUS - 'firewall an' in der Shell schaltet sie ein.", "Firewall: OFF - 'firewall an' in the shell turns it on.")
                                : fw_modus_get() == 2 ? TR("Firewall: streng, ausgehend nur an erlaubte Ziele.", "Firewall: strict, outgoing only to allowed targets.")
                                                      : TR("Firewall: an, erkennt Portscans, Fluten und ARP-Faelschung und sperrt Angreifer automatisch.", "Firewall: on, detects port scans, floods and ARP spoofing and quarantines attackers."),
            (synfs_is_mounted() && synfs_krypto_hdr()->magic == SYNFS_KRY_MAGIC) ? TR("Festplatte: verschluesselt (256-Bit, quantensicher).", "Disk: encrypted (256-bit, quantum-safe).")
                                                                                 : TR("Festplatte: NICHT verschluesselt - 'krypto aktivieren' in der Shell.", "Disk: NOT encrypted - 'krypto aktivieren' in the shell."), 0, 0); break;
    case I_HILFE: kprint("Hilfe\n"); antwort_setze(TR("Sag mir einfach, was du tun moechtest. Zum Beispiel:", "Just tell me what you want to do. For example:"),
            TR("\"text erstellen\" - schreibt ein Dokument", "\"write a text\" - opens a document"), TR("\"rechne (12+3)*7\" - rechnet", "\"calc (12+3)*7\" - calculates"),
            TR("\"system\" - zeigt den Zustand, \"dateien\" - deine Dokumente", "\"system\" - shows the status, \"files\" - your documents"), TR("\"briefing\", \"agenten\", \"aufgabe ...\", \"quanten\", \"abmelden\" - oder frag mich einfach alles", "\"briefing\", \"agents\", \"task ...\", \"quantum\", \"sign out\" - or just ask me anything"), 0); break;
    case I_FENSTER: kprint("Fenster\n");
        if (in_par >= 20 && in_par <= 24) { lang_setze(in_par - 20); reply1(T5("Sprache: Deutsch", "Language: English", "Langue : francais", "Lingua: italiano", "Idioma: espanol")); zeichne_desktop(); return; }
        if (in_par == 4) { fenster_oeffnen(FT_CHATS); return; }
        if (in_par == 5) { fenster_oeffnen(FT_GED); return; }
        if (in_par == 30) { chat_neu(TR("Neuer Chat", "New chat")); reply1(TR("Neuer Chat angelegt.", "New chat created.")); if (fenster_idx(FT_CHATS) >= 0) zeichne_desktop(); break; }
        if (in_par == 31) { if (chat_aktiv()) { chat_pin_umschalten(chat_aktiv()); reply1(chat_gepinnt(chat_aktiv()) ? TR("Chat angepinnt.", "Chat pinned.") : TR("Chat nicht mehr angepinnt.", "Chat unpinned.")); } else reply1(TR("Es gibt noch keinen aktiven Chat.", "There is no active chat yet.")); break; }
        if (in_par == 32) { if (chat_aktiv()) { chat_projekt_setze(chat_aktiv(), roh + (in_text - s)); reply1(TR("Chat dem Projekt zugeordnet.", "Chat assigned to the project.")); } else reply1(TR("Es gibt noch keinen aktiven Chat.", "There is no active chat yet.")); break; }
        if (in_par == 33) { if (chat_aktiv()) { chat_loeschen(chat_aktiv()); reply1(TR("Chat geloescht.", "Chat deleted.")); } else reply1(TR("Es gibt keinen aktiven Chat.", "There is no active chat.")); break; }
        if (in_par == 3) fenster_oeffnen(FT_EINST);
        else if (in_par == 1) fenster_oeffnen(FT_RECH);
        else if (in_par == 2) fenster_oeffnen(FT_INFO);
        else if (in_par >= 10 && in_par <= 13) widget_neu(in_par - 9);
        else if (in_par == 14) { if (!nwid) { widget_neu(WG_UHR); widget_neu(WG_SYS); widget_neu(WG_AUF); } else widget_neu(WG_UHR); }
        else if (in_par == 99) { if (fenster_idx(FT_EDIT) >= 0 && doklen > 0 && dokgeaendert) dok_speichern(); nfen = 0; nwid = 0; fok = 0; zustand = Z_DESK; if (flaeche == 0) zeichne_desktop(); }
        return;
    case I_WEB: kprint("Browser\n"); web_oeffnen(roh + (in_text - s)); return;
    case I_FLAECHE: kprint("Arbeitsflaeche\n"); { int z = in_par < 0 ? (flaeche + 1) % 4 : in_par; if (z == flaeche) { reply1(TR("Du bist schon dort.", "You are already there.")); break; }
            reply1(TR("Ich drehe den Wuerfel.", "Rotating the cube.")); flaeche_wechsel(z); return; }
    case I_AGENT: kprint("Agent\n"); if (in_par < 0) { reply1(TR("Ich zeige dir die Agenten.", "Showing the agents.")); flaeche_wechsel(2); return; }
        reply1(TR("Ich starte den Agenten ...", "Starting the agent ...")); ag_start(in_par); if (flaeche != 2) flaeche_wechsel(2); else zeichne_desktop(); return;
    case I_AUFGABE: kprint("Aufgabe\n");
        if (!*in_text) { reply1(TR("Aufgaben ansehen ...", "Opening tasks ...")); flaeche_wechsel(3); return; }
        if (aufg_add(roh + (in_text - s)) < 0) { reply1(TR("Die Aufgabenliste ist voll.", "The task list is full.")); break; }
        kprint("[DESKTOP] Aufgabe gespeichert: "); kprint(roh + (in_text - s)); kprint("\n");
        antwort_setze(TR("Aufgabe gespeichert:", "Task saved:"), roh + (in_text - s), TR("Sag \"aufgaben\" oder druecke F4, um alle zu sehen.", "Press F4 to see all tasks."), 0, 0, 0); break;
    case I_ABMELDEN: kprint("Abmelden\n"); if (fenster_idx(FT_EDIT) >= 0 && doklen > 0 && dokgeaendert) dok_speichern(); nfen = 0; fok = 0; benutzer_abmelden(); fname[0] = fpw[0] = 0; ffocus = 0; fmsg = 0; zustand = Z_LOGIN; zeichne_login(); return;
    case I_NEUSTART: kprint("Neustart\n"); system_neustart(); return;
    case I_WARTUNG:
        if (benutzer_rolle() != ROLLE_ADMIN) { kprint("abgelehnt\n"); reply1(TR("Wartung ist nur fuer Administratoren.", "Maintenance is for administrators only.")); break; }
        kprint("Wartungs-Konsole\n"); usb_set_rawgrab(0); shell_set_tick(0); hv_status_aus(); gfx_console(1); shell_wartung(); return;
    default:
        if (ki_nutzbar()) { kprint("Sprachmodell\n"); ki_antwort(roh); break; }
        kprint("nicht verstanden\n"); antwort_setze(TR("Das habe ich noch nicht verstanden.", "I did not understand that yet."), TR("Sag zum Beispiel: text erstellen, rechne 12*7, system oder hilfe.", "Try: write a text, calc 12*7, system or help."), 0, 0, 0, 0); break;
    }
    zeichne_antwort(); zeichne_eingabe();
}

static void ausfuehren(void) {
    char u[96]; scopy(u, eingabe, 96);
    int vorher = chat_aktiv();
    ki_gefragt = 0;
    ausfuehren_roh();
    if (zustand == Z_LOGIN) return;
    char a[400]; a[0] = 0; for (int i = 0; i < nantwort && slen(a) < 300; i++) { if (i) cat(a, " ", 400); cat(a, antwort[i], 400); }
    if (ki_gefragt) ged_lerne(u, a);                                      /* das Synapsen-Gedaechtnis lernt aus dem Gespraech */
    if (!(u[0] == 'c' && u[1] == 'h' && u[2] == 'a' && u[3] == 't') && !ged_heikel(u)) chat_log(u, a);
    (void)vorher; if (fenster_idx(FT_CHATS) >= 0 || fenster_idx(FT_GED) >= 0) zeichne_desktop();
}
static void anmelden(void) {
    if (benutzer_login(fname, fpw) == 0) {
        kprint("[DESKTOP] Willkommen, "); kprint(benutzer_name()); kprint(" ("); kprint(benutzer_rollentext(benutzer_rolle())); kprint(")\n");
        zustand = Z_DESK; einlen = 0; eingabe[0] = 0;
        char g[100]; scopy(g, TR("Hallo ", "Hello "), 100); cat(g, benutzer_name(), 100); cat(g, TR("! Ich bin Zynie. Was moechtest du tun?", "! I am Zynie. What would you like to do?"), 100);
        reply1(g); zeichne_desktop();
    } else {
        kprint("[DESKTOP] Anmeldung fehlgeschlagen\n");
        fpw[0] = 0; fmsg = 1; ffocus = 1; zeichne_login();
    }
}

static void entsperren_versuch(void) {
    char dev[20]; int i = 0; const char* g = krypto_gesperrt_dev(); while (g[i] && i < 19) { dev[i] = g[i]; i++; } dev[i] = 0;
    if (krypto_entsperren(dev, fpw) != 0) { kprint("[DESKTOP] Entsperren fehlgeschlagen\n"); fpw[0] = 0; fmsg = 1; ffocus = 0; zeichne_login(); return; }
    if (block_select(dev) != 0 || synfs_mount() != 0) { fpw[0] = 0; fmsg = 1; zeichne_login(); return; }
    kprint("[DESKTOP] Festplatte entsperrt\n");
    benutzer_laden(); fw_laden(); zdb_init(); ged_neu_laden();
    entsperr = 0; fpw[0] = 0; fmsg = 0; ffocus = 0; fname[0] = 0;
    if (benutzer_gesperrt()) zeichne_login();
    else { zustand = Z_DESK; einlen = 0; eingabe[0] = 0; reply1(TR("Festplatte entsperrt. Was moechtest du tun?", "Disk unlocked. What would you like to do?")); zeichne_desktop(); }
}

static void taste(uint8_t k, int shift, int altgr) {
    if (menu.offen) { if (k == 0x29) menu_schliessen(); return; }
    if (zustand != Z_LOGIN && k >= 0x3A && k <= 0x3D) { flaeche_wechsel(k - 0x3A); return; }
    if (zustand == Z_RECH) { rech_taste(k, shift, altgr); return; }
    if (zustand == Z_INFO) { if (k == 0x29 && nfen) fenster_schliessen_typ(fen[nfen - 1].typ); return; }
    if (zustand == Z_EDIT) { if (cur_ctrl && strg_taste(k)) return; editor_taste(k, shift, altgr); return; }
    if (zustand == Z_WEB) { web_taste(k, shift, altgr); return; }
    if (zustand == Z_LOGIN && entsperr) {
        int n = slen(fpw);
        if (k == 0x28 || k == 0x58) entsperren_versuch();
        else if (k == 0x2A) { if (n) { fpw[n - 1] = 0; fmsg = 0; zeichne_login(); } }
        else { char c = usb_key_char(k, shift, altgr); if (c >= 32 && c < 127 && n < 39) { fpw[n] = c; fpw[n + 1] = 0; fmsg = 0; zeichne_login(); } }
        return;
    }
    if (zustand == Z_LOGIN) {
        char* t = ffocus == 0 ? fname : fpw; int max = ffocus == 0 ? 15 : 39; int n = slen(t);
        if (k == 0x2B || k == 0x51 || k == 0x52) { ffocus = !ffocus; zeichne_login(); }
        else if (k == 0x28 || k == 0x58) { if (ffocus == 0) { ffocus = 1; zeichne_login(); } else anmelden(); }
        else if (k == 0x2A) { if (n) { t[n - 1] = 0; fmsg = 0; zeichne_login(); } }
        else { char c = usb_key_char(k, shift, altgr); if (c >= 32 && c < 127 && n < max) { t[n] = c; t[n + 1] = 0; fmsg = 0; zeichne_login(); } }
        return;
    }
    if (cur_ctrl && strg_taste(k)) return;
    if (k >= 0x3A && k <= 0x3D) { flaeche_wechsel(k - 0x3A); return; }                       /* F1-F4 */
    if (k == 0x4B) { flaeche_wechsel(flaeche + 3); return; }                                 /* Bild auf */
    if (k == 0x4E) { flaeche_wechsel(flaeche + 1); return; }                                 /* Bild ab */
    if (k == 0x28 || k == 0x58) {
        if (flaeche != 0) return;
        if (sugsel >= 0 && sugsel < nsug) {
            scopy(eingabe, sug[sugsel], 96); einlen = slen(eingabe);
            if (einlen && eingabe[einlen - 1] == ' ') { sug_suche(); zeichne_eingabe(); return; }          /* braucht noch Text */
        }
        ausfuehren(); return;
    }
    if (flaeche == 0 && nsug > 0 && (k == 0x51 || k == 0x52)) {                              /* Pfeil runter / hoch */
        sugsel += (k == 0x51) ? 1 : -1; if (sugsel >= nsug) sugsel = -1; if (sugsel < -1) sugsel = nsug - 1;
        zeichne_eingabe(); return;
    }
    if (flaeche == 0 && nsug > 0 && k == 0x2B) {                                             /* Tab: Vorschlag uebernehmen */
        scopy(eingabe, sug[sugsel >= 0 ? sugsel : 0], 96); einlen = slen(eingabe); sug_suche(); zeichne_eingabe(); return;
    }
    if (k == 0x2A) { if (flaeche == 0 && einlen) { if (sel1) { einlen = 0; eingabe[0] = 0; sel1 = 0; } else eingabe[--einlen] = 0; sug_suche(); zeichne_eingabe(); } return; }
    if (k == 0x29) { if (flaeche != 0) return; einlen = 0; eingabe[0] = 0; sug_suche(); zeichne_eingabe(); return; }
    char c = usb_key_char(k, shift, altgr);
    if (((c >= 32 && c < 127) || (unsigned char)c >= 0xA0) && einlen < 80) {
        if (flaeche != 0) flaeche_wechsel(0);
        if (sel1) { einlen = 0; sel1 = 0; }
        eingabe[einlen++] = c; eingabe[einlen] = 0; sug_suche(); zeichne_eingabe();
    }
}

static void klick(int x, int y, int owner) {
    for (int i = 0; i < nkn; i++) {
        if (knoepfe[i].owner != owner) continue;
        if (x < knoepfe[i].x || x >= knoepfe[i].x + knoepfe[i].w || y < knoepfe[i].y || y >= knoepfe[i].y + knoepfe[i].h) continue;
        int id = knoepfe[i].id;
        if (zustand == Z_DESK && id >= K_CHIP0 && id < K_CHIP0 + 6) { scopy(eingabe, chip_text[id - K_CHIP0], 96); einlen = slen(eingabe); ausfuehren(); }
        else if (zustand == Z_DESK && id >= K_SUG0 && id < K_SUG0 + 5 && id - K_SUG0 < nsug) {
            scopy(eingabe, sug[id - K_SUG0], 96); einlen = slen(eingabe);
            if (einlen && eingabe[einlen - 1] == ' ') { sug_suche(); zeichne_eingabe(); } else ausfuehren();
        }
        else if (zustand == Z_DESK && id >= K_DOT0 && id < K_DOT0 + 4) flaeche_wechsel(id - K_DOT0);
        else if (zustand == Z_DESK && id == K_PREV) flaeche_wechsel(flaeche + 3);
        else if (zustand == Z_DESK && id == K_NEXT) flaeche_wechsel(flaeche + 1);
        else if (zustand == Z_DESK && id >= K_AG0 && id < K_AG0 + NAG) { ag_start(id - K_AG0); zeichne_desktop(); }
        else if (zustand == Z_DESK && (id == K_FREI || id == K_ABL)) { ag_entscheid(id == K_FREI); zeichne_desktop(); }
        else if (zustand == Z_WEB && id == K_WB) web_zurueck(-1);
        else if (zustand == Z_WEB && id == K_WV) web_zurueck(1);
        else if (zustand == Z_WEB && id == K_WR) web_gehe(web_url[0] ? web_url : adr, 0);
        else if (zustand == Z_WEB && id == K_WX) web_schliessen();
        else if (zustand == Z_WEB && id == K_WS) { if (wok) lz_add(web_url); else scopy(meldung, TR("Erst eine Seite oeffnen.", "Open a page first."), 100); zeichne_web(1); }
        else if (zustand == Z_WEB && id >= K_SUG0 && id < K_SUG0 + 5 && id - K_SUG0 < nsug) { scopy(adr, sug[id - K_SUG0], 192); adrlen = slen(adr); web_gehe(adr, 1); }
        else if (zustand == Z_WEB && id >= K_LZ0 && id < K_LZ0 + 14) { char t[200]; int n = id - K_LZ0; if (n < 10) { if (lz_zeile(n, t, 200)) web_gehe(t, 1); } else if (web_vorgabe[n - 10]) web_gehe(web_vorgabe[n - 10], 1); }
        else if (zustand == Z_DESK && id == K_NEUDOK) editor_oeffnen();
        else if (zustand == Z_INFO && id >= K_EIN0 && id < K_EIN0 + 15) einst_klick(id);
        else if (zustand == Z_INFO && id >= K_CH0 && id < K_CH_DEL + 1) chats_klick(id);
        else if (zustand == Z_INFO && id >= K_GED_SCHLAF && id <= K_GED_VERG) ged_klick(id);
        else if (zustand == Z_RECH && id >= K_RB0 && id < K_RB0 + 20) { rech_druecke(rtasten[id - K_RB0]); zeichne_rechner(); }
        else if (zustand == Z_DESK && id == K_AUFLEER) { aufg_leeren(); kprint("[DESKTOP] Aufgaben geleert\n"); zeichne_desktop(); }
        else if (zustand == Z_EDIT && id == K_SPEICHERN) { dok_speichern(); zeichne_editor(0); }
        else if (zustand == Z_EDIT && id == K_NEU) { if (doklen > 0 && dokgeaendert) dok_speichern(); dok_neu(); zeichne_editor(1); }
        else if (zustand == Z_EDIT && id == K_SCHLIESSEN) editor_schliessen();
        return;
    }
    if (zustand == Z_WEB && wok) {                                  /* Link oder Rollbalken */
        int l = web_link_bei(x, y); if (l >= 0) { web_gehe(web_links[l], 1); return; }
        if (x >= wx0 + ww - 26 && y >= wy0 && y < wy0 + wh && web_nz > wrows) { wscroll = (y - wy0 - 12) * (web_nz - wrows) / (wh - 24); if (wscroll < 0) wscroll = 0; zeichne_web(0); }
    }
}

static void rohtaste(uint8_t k, int shift, int altgr) { unsigned n = (qt + 1) % QN; if (n != qh) { kq[qt] = (uint16_t)(k | (shift << 8) | (altgr << 9) | (usb_ctrl << 10)); qt = n; } }

static int fdrag, fdx, fdy, fox, foy, fdw, fdh, fobj, fdgezeichnet;
static int fenster_bei(int x, int y) { if (flaeche != 0) return -1; for (int i = nfen - 1; i >= 0; i--) if (x >= fen[i].x && x < fen[i].x + fen[i].w && y >= fen[i].y && y < fen[i].y + fen[i].h) return i; return -1; }
static void fokus_bei(int x, int y) {
    int h = fenster_bei(x, y);
    if (h >= 0) { if (h != nfen - 1) fenster_hoch(h); fok = 1; zustand = zustand_fuer(fen[nfen - 1].typ); zeichne_desktop(); }
    else if (fok) { fok = 0; zustand = Z_DESK; zeichne_desktop(); }
}
static void maus_runter(int x, int y) {
    int h = fenster_bei(x, y);
    if (h >= 0) {
        int war_oben = (h == nfen - 1) && fok;
        if (h != nfen - 1) fenster_hoch(h);
        h = nfen - 1; fok = 1; zustand = zustand_fuer(fen[h].typ);
        if (!war_oben) zeichne_desktop();
        struct fenster* f = &fen[h];
        if (y < f->y + WT_H) {
            if (x >= f->x + f->w - 34) { if (f->typ == FT_EDIT) editor_schliessen(); else if (f->typ == FT_WEB) web_schliessen(); else fenster_schliessen_typ(f->typ); return; }
            fdrag = 1; fobj = h; fdx = x - f->x; fdy = y - f->y; fox = f->x; foy = f->y; fdw = f->w; fdh = f->h; fdgezeichnet = 0; return;
        }
        klick(x, y, h); return;
    }
    if (flaeche == 0) for (int j = nwid - 1; j >= 0; j--) {
        struct widget* g = &wid[j];
        if (x >= g->x && x < g->x + g->w && y >= g->y && y < g->y + g->h) {
            if (x >= g->x + g->w - 30 && y < g->y + 26) { for (int q = j; q < nwid - 1; q++) wid[q] = wid[q + 1]; nwid--; layout_speichern(); zeichne_desktop(); return; }
            fdrag = 2; fobj = j; fdx = x - g->x; fdy = y - g->y; fox = g->x; foy = g->y; fdw = g->w; fdh = g->h; fdgezeichnet = 0; return;
        }
    }
    if (fok) { fok = 0; zustand = Z_DESK; zeichne_desktop(); }
    if (zustand == Z_DESK && flaeche == 0 && x >= zr_x() && x < zr_x() + zr_w() && y >= zr_y() && y < zr_y() + zr_h()) {
        if (x >= zr_x() + zr_w() - 24 && y >= zr_y() + zr_h() - 24) zdrag = 2; else { zdrag = 1; zdx = x - zx; zdy = y - zy; }
    } else klick(x, y, -1);
}

static void tick(void) {
    uint32_t t = timer_get_ticks();
    if (qh != qt) letzte_eingabe = t;
    while (qh != qt) { uint16_t e = kq[qh]; qh = (qh + 1) % QN; cur_ctrl = (e >> 10) & 1; taste((uint8_t)(e & 255), (e >> 8) & 1, (e >> 9) & 1); cur_ctrl = 0; }
    const struct usb_pointer* p = usb_pointer_state();
    if (p && p->present) {
        static int lpx, lpy;
        if (p->x != lpx || p->y != lpy) { lpx = p->x; lpy = p->y; letzte_eingabe = t; }
        int down = p->buttons & 1; static int was_r;
        int rd = (p->buttons & 2) != 0;
        if (rd && !was_r && zustand != Z_LOGIN) { if (menu.offen) menu_schliessen(); else { fokus_bei(p->x, p->y); menu_oeffnen(p->x, p->y); } }
        was_r = rd;
        if (menu.offen) {
            int i = menu_treffer(p->x, p->y); if (i != menu.hover) { menu.hover = i; zeichne_menu(); }
            if (down && !was_down) { was_down = down; if (i >= 0) menu_aktion(menu.p[i].id); else menu_schliessen(); }
            was_down = down; return;
        }
        if (down && !was_down && zustand != Z_LOGIN) maus_runter(p->x, p->y);
        if (down && fdrag) {                                                      /* Fenster/Widget ziehen: Umriss folgt der Maus */
            int nx = p->x - fdx, ny = p->y - fdy;
            if (nx < 60 - fdw) nx = 60 - fdw; if (nx > W - 60) nx = W - 60; if (ny < 42) ny = 42; if (ny > H - 40) ny = H - 40;
            if (!fdgezeichnet || nx != fox || ny != foy) { gfx_begin(); if (fdgezeichnet) xor_rahmen(fox, foy, fdw, fdh); fox = nx; foy = ny; xor_rahmen(fox, foy, fdw, fdh); fdgezeichnet = 1; gfx_end(); }
        }
        if (!down && fdrag) {
            if (fdgezeichnet) { gfx_begin(); xor_rahmen(fox, foy, fdw, fdh); gfx_end(); if (fdrag == 1 && fobj < nfen) { fen[fobj].x = fox; fen[fobj].y = foy; } else if (fdrag == 2 && fobj < nwid) { wid[fobj].x = fox; wid[fobj].y = foy; } }
            int war = fdgezeichnet; if (war && fdrag == 2) layout_speichern(); fdrag = 0; fdgezeichnet = 0; if (war) zeichne_desktop();
        }
        if (down && zdrag && zustand == Z_DESK && flaeche == 0) {
            int bxl = zr_x(), byl = zr_y();
            if (zdrag == 1) { zx = p->x - zdx; zy = p->y - zdy; if (zy - SC(108) < 44) zy = 44 + SC(108); if (zx - SC(90) < 0) zx = SC(90); if (zx + SC(90) > W) zx = W - SC(90); if (zy + SC(124) > H) zy = H - SC(124); }
            else { int ns = (p->x - bxl) * 100 / 180; if (ns < 50) ns = 50; if (ns > 220) ns = 220; if (byl + ns * 232 / 100 <= H) { zs = ns; zx = bxl + SC(90); zy = byl + SC(108); } }
            zeichne_desktop();
        }
        if (!down && zdrag) { zdrag = 0; layout_speichern(); kprint("[DESKTOP] Zynie: Position "); kprint_dec((uint32_t)zx); kprint(","); kprint_dec((uint32_t)zy); kprint(" Groesse "); kprint_dec((uint32_t)zs); kprint("%\n"); }
        was_down = down;
    }
    static int war_ruhe;
    int r = ruhend(); if (r != war_ruhe) { war_ruhe = r; kprint(r ? "[DESKTOP] Zynie: Ruhemodus\n" : "[DESKTOP] Zynie: wach\n"); }
    uint32_t ph = t / (t < sprech_bis ? 6 : r ? 30 : 12);
    if (ph != robot_phase) { robot_phase = ph; if (zustand != Z_LOGIN && !zdrag && !fdrag && !menu.offen && flaeche == 0 && !deckt(zr_x() - 4, zr_y() - 4, zr_w() + 8, zr_h() + 8)) zeichne_robot(); }
    { static uint32_t lmin; static uint32_t lt; if (zustand != Z_LOGIN && flaeche == 0 && nwid && !fdrag && !menu.offen && t - lt > 300) { lt = t; uint32_t ymd, hm; rtc_get(&ymd, &hm); if (hm != lmin) { if (lmin) zeichne_desktop(); lmin = hm; } } }
    { static uint32_t lf; int wi = fenster_idx(FT_EINST); if (wi >= 0 && wi == nfen - 1 && flaeche == 0 && zustand != Z_LOGIN && !fdrag && !menu.offen && t / 5 != lf) { lf = t / 5; lader(fen[wi].x + fen[wi].w - 60, fen[wi].y + fen[wi].h - 56, 34, lf, C_BG); } }
    { static uint32_t lsek; if (zustand == Z_DESK && flaeche == 1 && t / 100 != lsek) { lsek = t / 100; gfx_begin(); sys_k_zeit(); gfx_end(); } }
}

int desktop_start(void) {
    if (!gfx_available()) return -1;
    gfx_size(&W, &H);
    lang = 0; npend = 0;
    static char cfg[64]; int r = 0;
    { const char* sp = zdb_get("system.sprache"); if (sp) { for (int i = 0; i < 5; i++) if (ueq(sp, LCODE[i])) lang = i; } else r = synfs_read("sys.config", cfg, sizeof(cfg) - 1); }
    if (r > 0) { cfg[r] = 0; for (int i = 0; cfg[i]; i++) if (cfg[i] == 'l' && cfg[i + 1] == 'a' && cfg[i + 2] == 'n' && cfg[i + 3] == 'g' && cfg[i + 4] == '=') { lang = (cfg[i + 5] == 'd' ? 0 : 1); break; } }
    bar_w = W - 80 < 760 ? W - 80 : 760; bar_x = (W - bar_w) / 2; bar_y = H - 230;
    fname[0] = fpw[0] = 0; ffocus = 0; fmsg = 0; einlen = 0; eingabe[0] = 0; nsug = 0; sugsel = -1; flaeche = 0; alauf = -1; nalog = 0; pend = 0; nfen = 0; nwid = 0; fok = 0; fdrag = 0; fdgezeichnet = 0; menu.offen = 0; nantwort = 0; qh = qt = 0; was_down = 0;
    zx = 24 + 90; zy = 56 + 108; zs = 100; lbw = 0; zdrag = 0; letzte_eingabe = timer_get_ticks();
    gfx_console(0);
    { const char* ws = zdb_get("desktop.widgets"); while (ws && *ws && nwid < 6) { int t = lies(&ws), x = lies(&ws) - 2000, y = lies(&ws) - 2000; if (t >= 1 && t <= 4) { wid[nwid].typ = t; wid_groesse(t, &wid[nwid].w, &wid[nwid].h); if (x < 60 - wid[nwid].w) x = 60 - wid[nwid].w; if (x > W - 60) x = W - 60; if (y < 42) y = 42; if (y > H - 40) y = H - 40; wid[nwid].x = x; wid[nwid].y = y; nwid++; } } }
    { const char* zz = zdb_get("desktop.zynie"); if (zz) { int x = lies(&zz), y = lies(&zz), g = lies(&zz); if (g >= 50 && g <= 220 && x > 0 && x < W && y > 44 && y < H) { zx = x; zy = y; zs = g; } } }
    usb_set_rawgrab(rohtaste);
    shell_set_tick(tick);
    kprint("[DESKTOP] Zynex OS Quantum Desktop, Bildschirm "); kprint_dec((uint32_t)W); kprint("x"); kprint_dec((uint32_t)H); kprint("\n");
    entsperr = krypto_gesperrt_dev()[0] != 0;
    if (entsperr) { zustand = Z_LOGIN; kprint("[DESKTOP] Platte verschluesselt: Entsperren-Bildschirm\n"); zeichne_login(); }
    else if (benutzer_gesperrt()) { zustand = Z_LOGIN; kprint("[DESKTOP] Anmeldebildschirm\n"); zeichne_login(); }
    else { zustand = Z_DESK; reply1(TR("Hallo! Ich bin Zynie. Was moechtest du tun?", "Hello! I am Zynie. What would you like to do?")); zeichne_desktop(); }
    return 0;
}
