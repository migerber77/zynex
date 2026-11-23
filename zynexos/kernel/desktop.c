/*
 * Zynex OS Quantum - Startbild und Desktop (Framebuffer, Maus + Tastatur)
 * Kein Startmenue, keine Programmliste: Mitte = Zynos (der Roboter), unten die Eingabeleiste. Was eingegeben wird, erkennt Zynos als Absicht
 * (Intent) und oeffnet das Passende: "text erstellen" -> Textverarbeitung, "rechne 12*7" -> Ergebnis, "system" -> Zustand usw.
 * Sprachen: Deutsch und Englisch (die gespeicherte Sprache de/en, alle anderen zeigen Englisch).
 */
#include "fw.h"
#include "krypto.h"
#include "block.h"
#include "benutzer.h"
#include "raeume.h"
#include "zyndb.h"
#include "layouts.h"
#include "extki.h"
#include "wissen.h"
#include "zlk.h"
#include "fakten.h"
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

/* Farbschemas: die Farben sind Variablen (Einstellungen > Aussehen). Reihenfolge: bg, card, border, text, muted, acc, acc2, field, off, Hintergrund oben/unten, Leuchtflecken 1/2 */
static const struct thema { const char* de; const char* en; uint32_t c[13]; } THEMEN[6] = {
    { "Nacht", "Night",     { 0x000A1222u, 0x00101C34u, 0x001E4467u, 0x00E2E8F0u, 0x0094A3B8u, 0x0038BDF8u, 0x00C084FCu, 0x00091428u, 0x00334155u, 0x00040711u, 0x00091124u, 0x000E3A55u, 0x004A2380u } },
    { "Ozean", "Ocean",     { 0x00071A22u, 0x000E2631u, 0x001F4658u, 0x00E6F4F8u, 0x0088AEBDu, 0x000FA9B8u, 0x005CD8E2u, 0x00061820u, 0x00284A58u, 0x00041218u, 0x000B3A4Au, 0x00139AAAu, 0x002070B8u } },
    { "Violett", "Violet",  { 0x000E0A1Fu, 0x001A1330u, 0x003A2D66u, 0x00EFE9FAu, 0x009E94C0u, 0x008B5CF6u, 0x00A78BFAu, 0x00120C26u, 0x003F3560u, 0x000A0618u, 0x002A1457u, 0x007C3AEDu, 0x00D02A80u } },
    { "Wald", "Forest",     { 0x000A1410u, 0x00112019u, 0x00264636u, 0x00E8F3ECu, 0x0086A998u, 0x002FB67Cu, 0x005FD39Eu, 0x000A1812u, 0x002F4A3Cu, 0x00070F0Bu, 0x00123D2Au, 0x00208F5Fu, 0x0090A82Cu } },
    { "Abend", "Sunset",    { 0x00180D12u, 0x0024141Bu, 0x004D2A36u, 0x00F8EDE8u, 0x00B99A98u, 0x00F0603Au, 0x00FF8F66u, 0x001B0E14u, 0x00573641u, 0x00120810u, 0x004A1B2Au, 0x00E5533Du, 0x00F59E0Bu } },
    { "Hell", "Light",      { 0x00F5F7FCu, 0x00FFFFFFu, 0x00D3DAE8u, 0x001B2438u, 0x0066728Cu, 0x003B63F0u, 0x002F52D6u, 0x00EAEFFAu, 0x00B4BFD6u, 0x00DCE6FAu, 0x00F4EEFFu, 0x00B8CEFFu, 0x00E0C8FFu } },
};
static uint32_t th_bg, th_card, th_border, th_text, th_muted, th_acc, th_acc2, th_field, th_off, th_hg1, th_hg2, th_gl1, th_gl2;
static int th_nr, hg_nr;                                  /* gewaehltes Farbschema und Hintergrund (0 Verlauf, 1 Aurora, 2 Raster, 3 Einfarbig, 4 Futuristisch, 5 eigenes Bild) */
static char hg_datei[32];
static void thema_anwenden(int n);
static void th_laden(void);
static void thema_anwenden(int n) {
    const uint32_t* c = THEMEN[n < 0 || n > 5 ? 0 : n].c;
    th_bg = c[0]; th_card = c[1]; th_border = c[2]; th_text = c[3]; th_muted = c[4]; th_acc = c[5]; th_acc2 = c[6]; th_field = c[7]; th_off = c[8]; th_hg1 = c[9]; th_hg2 = c[10]; th_gl1 = c[11]; th_gl2 = c[12];
}
static void th_laden(void) { th_nr = zdb_geti("ui.schema", 0); if (th_nr < 0 || th_nr > 5) th_nr = 0; hg_nr = zdb_geti("ui.hintergrund", 7); if (hg_nr < 0 || hg_nr > 7) hg_nr = 7; { const char* d = zdb_get("ui.hgbild"); int i = 0; while (d && d[i] && i < 31) { hg_datei[i] = d[i]; i++; } hg_datei[i] = 0; } if (hg_nr == 5 && !hg_datei[0]) hg_nr = 4; thema_anwenden(th_nr); }
#define C_BG     th_bg
#define C_CARD   th_card
#define C_BORDER th_border
#define C_TEXT   th_text
#define C_MUTED  th_muted
#define C_ACC    th_acc
#define C_ACC2   th_acc2
#define C_SPL    0x000B1220u                              /* Start- und Anmeldebild: das Logo ist auf diese Farbe geglaettet */
#define C_WALL   0xFFFFFFFFu                              /* "Farbe" fuer lader(): auf dem Hintergrundbild zeichnen */
#define C_CYAN   0x0000F2FEu
#define C_VIOLET 0x009D4EDDu
#define C_OK     0x0034D399u
#define C_ERR    0x00EF4444u
#define C_FIELD  th_field
#define C_OFF    th_off

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
    { "Hello! I am Zynos. What would you like to do?", "Bonjour ! Je suis Zynos. Que voulez-vous faire ?", "Ciao! Sono Zynos. Cosa vuoi fare?", "Hola! Soy Zynos. Que quieres hacer?" },
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
extern void gfx_text_p(int x, int y, const char* s, uint32_t fg, int scale);
extern int  gfx_text_pw(const char* s, int scale);
extern void gfx_blend(int x, int y, uint32_t c, int a);
static void gtxt(int x, int y, const char* s, uint32_t c, int sc) { gfx_text_p(x, y, l1(s), c, sc); }       /* geglaettete Inter-Schrift */
static int gtw(const char* s, int sc) { return gfx_text_pw(l1(s), sc); }

static void rrect(int x, int y, int w, int h, int r, uint32_t c) {         /* abgerundetes Rechteck mit geglaetteten Ecken (4x4 Abtastung) */
    if (w <= 0 || h <= 0) return;
    if (r * 2 > h) r = h / 2;
    if (r * 2 > w) r = w / 2;
    if (r < 2) { gfx_rect(x, y, w, h, c); return; }
    gfx_rect(x, y + r, w, h - 2 * r, c);
    gfx_rect(x + r, y, w - 2 * r, r, c); gfx_rect(x + r, y + h - r, w - 2 * r, r, c);
    int R = 8 * r, R2 = R * R;
    for (int j = 0; j < r; j++) {
        int voll = r;                                                      /* ab dieser Spalte ist die Zeile ganz gedeckt */
        for (int i = r - 1; i >= 0; i--) {
            int n = 0;
            for (int t = 0; t < 4; t++) { int Y = 8 * j + 2 * t + 1 - R; for (int u = 0; u < 4; u++) { int X = 8 * i + 2 * u + 1 - R; if (X * X + Y * Y <= R2) n++; } }
            if (n == 16) { voll = i; continue; }
            if (!n) break;
            int al = n * 255 / 16;
            gfx_blend(x + i, y + j, c, al); gfx_blend(x + w - 1 - i, y + j, c, al); gfx_blend(x + i, y + h - 1 - j, c, al); gfx_blend(x + w - 1 - i, y + h - 1 - j, c, al);
        }
        if (voll < r) { gfx_rect(x + voll, y + j, r - voll, 1, c); gfx_rect(x + w - r, y + j, r - voll, 1, c); gfx_rect(x + voll, y + h - 1 - j, r - voll, 1, c); gfx_rect(x + w - r, y + h - 1 - j, r - voll, 1, c); }
    }
}
/* Glas: der Hintergrund scheint getoent durch (abgerundet) */
static void glas(int x0, int y0, int w, int h, int rr, uint32_t tint, int a) {
    if (w <= 0 || h <= 0) return; if (rr * 2 > h) rr = h / 2;
    for (int y = 0; y < h; y++) {
        int ix = 0, dy = y < rr ? rr - y : y >= h - rr ? y - (h - rr - 1) : -1;
        if (dy >= 0) { ix = rr - isq(rr * rr - dy * dy); if (ix < 0) ix = 0; }
        for (int x = ix; x < w - ix; x++) gfx_blend(x0 + x, y0 + y, tint, a);
    }
}
static uint32_t mix(uint32_t a, uint32_t b, int t);
static void rrect_rand(int x, int y, int w, int h, int r, uint32_t fill, uint32_t rand) { if (th_nr != 5 && rand == C_BORDER) rand = mix(C_BORDER, 0x0038BDF8u, 110); rrect(x, y, w, h, r, rand); rrect(x + 1, y + 1, w - 2, h - 2, r - 1 > 0 ? r - 1 : 0, fill); }
/* ---------- Liquid-Glass-Helfer (Fluidic-Design: dunkles Glas, Cyan -> Blau -> Violett) ---------- */
static uint32_t mix(uint32_t a, uint32_t b, int t);
#define Q_GLAS (th_nr != 5)                                   /* Hell-Schema behaelt das schlichte Aussehen */
#define Q_CY    0x0000F2FEu
#define Q_SKY   0x0038BDF8u
#define Q_BLAU  0x004FACFEu
#define Q_VIO   0x00A855F7u
#define Q_ROSA  0x00E879F9u
static void h_grad(int x, int y, int w, int h, uint32_t c1, uint32_t c2) {          /* waagerechter Farbverlauf (Spaltenstreifen) */
    if (w <= 0 || h <= 0) return;
    for (int i = 0; i < w; i += 2) gfx_rect(x + i, y, w - i < 2 ? w - i : 2, h, mix(c1, c2, i * 256 / (w > 1 ? w - 1 : 1)));
}
static void rr_grad(int x, int y, int w, int h, int r, uint32_t c1, uint32_t c2) {  /* abgerundetes Rechteck mit senkrechtem Verlauf (c1 oben, c2 unten) */
    if (w <= 0 || h <= 0) return; if (r * 2 > h) r = h / 2; if (r * 2 > w) r = w / 2;
    for (int j = 0; j < h; j++) {
        int in = 0, dy = j < r ? r - j : j >= h - r ? j - (h - r - 1) : 0;
        if (dy > 0) { int q = r * r - (dy - 1) * (dy - 1), t = 0; while ((t + 1) * (t + 1) <= q) t++; in = r - t; }
        gfx_rect(x + in, y + j, w - 2 * in, 1, mix(c1, c2, j * 256 / h));
    }
}
static void h_pille(int x, int y, int w, int h, uint32_t c1, uint32_t c2) {          /* Pille/Knopf mit waagerechtem Verlauf */
    int r = h / 2 < 12 ? h / 2 : 12; rrect(x, y, 2 * r + 2, h, r, c1); rrect(x + w - 2 * r - 2, y, 2 * r + 2, h, r, c2); h_grad(x + r, y, w - 2 * r, h, mix(c1, c2, 256 * r / (w ? w : 1)), mix(c1, c2, 256 - 256 * r / (w ? w : 1)));
}

static uint32_t mix(uint32_t a, uint32_t b, int t /*0..256*/) {
    uint32_t r = 0;
    for (int s = 0; s <= 16; s += 8) { int x = (int)((a >> s) & 255), y = (int)((b >> s) & 255); r |= (uint32_t)(x + (y - x) * t / 256) << s; }
    return r;
}

/* ---------- Hintergrundbild (Verlauf, Aurora, Raster, einfarbig): hg() malt einen Ausschnitt neu, so bleiben Flecken beim Neuzeichnen aus ---------- */
static int isqf(uint32_t v) { uint32_t r = 0, b = 1u << 30; while (b > v) b >>= 2; while (b) { if (v >= r + b) { v -= r + b; r = (r >> 1) + b; } else r >>= 1; b >>= 2; } return (int)r; }
static uint32_t* wand; static int wand_art = -1;
static uint32_t hg_zeile(int y) { if (hg_nr >= 4 && wand && wand_art == hg_nr && y >= 0 && y < H) return wand[y * W + W / 2]; if (hg_nr == 3) return th_hg1; if (y < 0) y = 0; if (y >= H) y = H - 1; return mix(th_hg1, th_hg2, y * 256 / H); }
static void hg_glow(int x, int y, int w, uint32_t base) {            /* zwei weiche Leuchtflecken aus verschachtelten Ellipsen */
    for (int g = 0; g < 2; g++) {
        int cx = g ? W * 80 / 100 : W * 22 / 100, cy = g ? H * 78 / 100 : H * 28 / 100, rx = g ? W * 36 / 100 : W * 34 / 100, ry = g ? H * 46 / 100 : H * 48 / 100;
        int dy = y - cy; if (dy < 0) dy = -dy; if (dy >= ry) continue;
        int fy = (int)((int64_t)dy * dy * 1024 / ((int64_t)ry * ry));
        uint32_t gc = g ? th_gl2 : th_gl1;
        for (int L = 1; L <= 9; L++) {
            int m = 1024 - fy - L * 1024 / 10; if (m <= 0) break;
            int half = (int)((int64_t)rx * isqf((uint32_t)m * 1024) / 1024);
            int a = cx - half, b = cx + half; if (a < x) a = x; if (b > x + w) b = x + w; if (b <= a) continue;
            gfx_rect(a, y, b - a, 1, mix(base, gc, L * 150 / 9));
        }
    }
}
/* ---------- Hintergrundbild im Speicher (Futuristisch oder eigenes Bild): einmal berechnet, danach nur kopiert ---------- */
extern void gfx_bild(int x, int y, int w, int h, const uint32_t* px, int stride, int y0, int y1);
static char wand_name[32];
static int wand_bild(const char* name, uint32_t* ziel);                  /* zeichnen.inc: Bild laden und auf den Bildschirm skalieren */
static uint32_t h32(int x, int y, int s) { uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + (uint32_t)s * 2246822519u; h = (h ^ (h >> 13)) * 1274126177u; return h ^ (h >> 16); }
static int wglatt(int t) { return (int)((int64_t)t * t * (3072 - 2 * t) / (1024 * 1024)); }
static int vrausch(int x, int y, int z, int s) {                          /* Wertrauschen 0..1023, Zellgroesse z */
    int gx = x / z, gy = y / z, fx = wglatt((x % z) * 1024 / z), fy = wglatt((y % z) * 1024 / z);
    int a = (int)(h32(gx, gy, s) & 1023), b = (int)(h32(gx + 1, gy, s) & 1023), c = (int)(h32(gx, gy + 1, s) & 1023), d = (int)(h32(gx + 1, gy + 1, s) & 1023);
    int o = a + (b - a) * fx / 1024, u = c + (d - c) * fx / 1024; return o + (u - o) * fy / 1024;
}
static uint16_t neb1[961 * 541], neb2[961 * 541];
static uint32_t wmix(uint32_t a, uint32_t b, int t) { if (t < 0) t = 0; if (t > 256) t = 256; return mix(a, b, t); }
extern const uint16_t zynie_bild[], hintergrund_bild[];
#define ZB_W 280
#define ZB_H 358
#define ZB_SPLIT 300
#define HB_W 1024
#define HB_H 559
static uint32_t c565(uint16_t v) { uint32_t r = (v >> 11) & 31, g = (v >> 5) & 63, b = v & 31; return ((r << 3 | r >> 2) << 16) | ((g << 2 | g >> 4) << 8) | (b << 3 | b >> 2); }
static int isin(int deg);
static int iatan2(int y, int x) {                                          /* Winkel in Grad 0..359 */
    int ax = x < 0 ? -x : x, ay = y < 0 ? -y : y; if (!ax && !ay) return 0;
    int a; if (ay <= ax) { int t = ay * 1024 / ax; a = t * (45 * 1024 + 15 * (1024 - t)) / (1024 * 1024); } else { int t = ax * 1024 / ay; a = 90 - t * (45 * 1024 + 15 * (1024 - t)) / (1024 * 1024); }
    if (x < 0) a = 180 - a; if (y < 0) a = 360 - a; return ((a % 360) + 360) % 360;
}
static void wand_futur(uint32_t* b) {
    int hz = H * 66 / 100, sk = W / 4 + 1, sh = H / 4 + 1; if (sk > 961) sk = 961; if (sh > 541) sh = 541;
    int z0 = W / 6 > 40 ? W / 6 : 40;
    for (int y = 0; y < sh; y++) for (int x = 0; x < sk; x++) {                /* Nebel in Viertel-Aufloesung */
        int f = (vrausch(x * 4, y * 4, z0, 1) * 8 + vrausch(x * 4, y * 4, z0 / 2, 2) * 4 + vrausch(x * 4, y * 4, z0 / 4, 3) * 2 + vrausch(x * 4, y * 4, z0 / 8, 4)) / 15;
        neb1[y * sk + x] = (uint16_t)f; neb2[y * sk + x] = (uint16_t)vrausch(x * 4 + 777, y * 4, z0, 5);
    }
    for (int y = 0; y < H; y++) {
        uint32_t basis = y < hz ? wmix(0x0005060Fu, 0x0026104Au, y * 256 / hz) : wmix(0x00140828u, 0x00050310u, (y - hz) * 256 / (H - hz + 1));
        int qy = y / 4, fy = (y % 4) * 256 / 4; if (qy >= sh - 1) qy = sh - 2;
        for (int x = 0; x < W; x++) {
            uint32_t c = basis;
            if (y < hz) {
                int qx = x / 4, fx = (x % 4) * 256 / 4; if (qx >= sk - 1) qx = sk - 2;
                int n = ((neb1[qy * sk + qx] * (256 - fx) + neb1[qy * sk + qx + 1] * fx) * (256 - fy) + (neb1[(qy + 1) * sk + qx] * (256 - fx) + neb1[(qy + 1) * sk + qx + 1] * fx) * fy) >> 16;
                int m = ((neb2[qy * sk + qx] * (256 - fx) + neb2[qy * sk + qx + 1] * fx) * (256 - fy) + (neb2[(qy + 1) * sk + qx] * (256 - fx) + neb2[(qy + 1) * sk + qx + 1] * fx) * fy) >> 16;
                int a = (n - 430) * 3; if (a < 0) a = 0; if (a > 1023) a = 1023; a = a * (hz - y) / hz;
                c = wmix(c, wmix(0x00C026D3u, 0x0006B6D4u, m / 4), a * 170 / 1024);
            }
            if (y < hz) {                                                                          /* Milchstrasse: diagonales Band, vom Nebelrauschen zerfasert */
                int ly = hz * 88 / 100 - (hz * 74 / 100) * x / W; int d = y - ly; if (d < 0) d = -d;
                int bw = H * 13 / 100 + 1; int band = 256 - d * 256 / bw;
                if (band > 0) { int qx = x / 4, qy2 = y / 4; if (qx >= sk - 1) qx = sk - 2; if (qy2 >= sh - 1) qy2 = sh - 2; int n2 = neb1[qy2 * sk + qx] + neb2[qy2 * sk + qx] / 2; int a2 = band * band / 256 * (n2 + 200) / 1100; c = wmix(c, wmix(0x00FFE9C8u, 0x00A8B8FFu, band), a2 * 120 / 256); }
            }
            int dh = y - hz; if (dh < 0) dh = -dh; int gl = 256 - dh * 256 / (H / 10 + 1);           /* Leuchten am Horizont */
            if (gl > 0) c = wmix(c, 0x00FF2E97u, gl * gl / 400);
            b[y * W + x] = c;
        }
    }
    /* Sterne */
    for (int i = 0; i < W * H / 420; i++) { int x = (int)(h32(i, 1, 9) % (uint32_t)W), y = (int)(h32(i, 2, 9) % (uint32_t)hz); int hl = 90 + (int)(h32(i, 3, 9) % 166); uint32_t tn = h32(i, 4, 9) % 5; uint32_t sc = tn == 0 ? ((uint32_t)hl << 16 | (uint32_t)(hl * 85 / 100) << 8 | (uint32_t)(hl * 60 / 100)) : tn == 1 ? ((uint32_t)(hl * 70 / 100) << 16 | (uint32_t)(hl * 85 / 100) << 8 | (uint32_t)hl) : (uint32_t)hl * 0x010101u;
        b[y * W + x] = wmix(b[y * W + x], sc | 0x101020u, 230);
        if (hl > 235 && x > 1 && y > 1 && x < W - 2 && y < hz - 2) { for (int k = 1; k <= 2; k++) { int t = 140 / k; b[y * W + x + k] = wmix(b[y * W + x + k], sc, t); b[y * W + x - k] = wmix(b[y * W + x - k], sc, t); b[(y + k) * W + x] = wmix(b[(y + k) * W + x], sc, t); b[(y - k) * W + x] = wmix(b[(y - k) * W + x], sc, t); } } }
    /* Planet mit Ring */
    int pcx = W * 76 / 100, pcy = H * 30 / 100, pr = H * 15 / 100, rx = pr * 19 / 10, ry = pr * 38 / 100;
    for (int y = pcy - pr - pr / 4; y <= pcy + pr + pr / 4; y++) for (int x = pcx - rx - 4; x <= pcx + rx + 4; x++) {
        if (x < 0 || y < 0 || x >= W || y >= H) continue;
        int dx = x - pcx, dy = y - pcy, d2 = dx * dx + dy * dy; uint32_t* q = &b[y * W + x];
        int innen = d2 <= pr * pr;
        int e = (int)((int64_t)dx * dx * 1024 / ((int64_t)rx * rx) + (int64_t)dy * dy * 1024 / ((int64_t)ry * ry));
        int ring = e >= 760 && e <= 1024 && !(innen && dy < 0);
        if (innen) { int l = (-dx - dy) * 256 / (2 * pr) + 128; uint32_t pc = wmix(0x00180A3Cu, 0x007C5CFFu, l); int rand = isq(d2) * 256 / pr; if (rand > 215) pc = wmix(pc, 0x0022D3EEu, (rand - 215) * 5); *q = pc; }
        else { int d = isq(d2); if (d < pr + pr / 5) *q = wmix(*q, 0x0022D3EEu, (pr + pr / 5 - d) * 180 / (pr / 5 + 1)); }
        if (ring) *q = wmix(*q, 0x00D8C8FFu, 120 + (e - 760) / 4);
    }
    /* Mond mit Kratern unten links am Horizont, Sternschnuppen */
    { int mx = W * 15 / 100, my = H * 24 / 100, mr = H * 5 / 100 + 6;
      for (int y = my - mr - 8; y <= my + mr + 8; y++) for (int x = mx - mr - 8; x <= mx + mr + 8; x++) {
          if (x < 0 || y < 0 || x >= W || y >= H) continue;
          int dx = x - mx, dy = y - my, d2 = dx * dx + dy * dy; uint32_t* q = &b[y * W + x];
          if (d2 <= mr * mr) {
              int l = (-dx - dy) * 256 / (2 * mr) + 150; if (l < 40) l = 40; if (l > 256) l = 256;
              uint32_t mc = wmix(0x00181828u, 0x00E4E6F2u, l);
              for (int k = 0; k < 7; k++) { int kx = mx + (int)(h32(k, 1, 31) % (uint32_t)(mr)) - mr / 2, ky = my + (int)(h32(k, 2, 31) % (uint32_t)(mr)) - mr / 2, kr = mr / 8 + (int)(h32(k, 3, 31) % (uint32_t)(mr / 6 + 1)) + 1;
                  int ex = x - kx, ey = y - ky; if (ex * ex + ey * ey < kr * kr) mc = wmix(mc, 0x00303048u, 110); }
              *q = mc;
          } else { int d = isq(d2); if (d < mr + mr / 3) *q = wmix(*q, 0x00A8B8FFu, (mr + mr / 3 - d) * 70 / (mr / 3 + 1)); }
      }
      for (int i = 0; i < 4; i++) {
          int x0 = W * (35 + i * 15) / 100 + (int)(h32(i, 5, 33) % (uint32_t)(W / 12)), y0 = H * (6 + (int)(h32(i, 6, 33) % 22)) / 100, len = W / 9 + (int)(h32(i, 7, 33) % (uint32_t)(W / 20));
          for (int k = 0; k < len; k++) { int x = x0 + k, y = y0 - k * 3 / 7; if (x < 0 || x >= W || y < 0 || y >= hz) continue; int t = 255 - k * 255 / len; t = t * t / 255;
              b[y * W + x] = wmix(b[y * W + x], 0x00DDEEFFu, t); if (k < len / 6 && y + 1 < hz) b[(y + 1) * W + x] = wmix(b[(y + 1) * W + x], 0x0022D3EEu, t / 2); }
      }
    }
    /* Spiralgalaxie, zweiter Planet (Gasriese mit Baendern), Sternhaufen */
    { int gx = W * 52 / 100, gy = H * 13 / 100, gr = H * 11 / 100 + 10;
      for (int y = gy - gr; y <= gy + gr; y++) for (int x = gx - gr * 2; x <= gx + gr * 2; x++) {
          if (x < 0 || y < 0 || x >= W || y >= hz) continue;
          int dx = x - gx, dy = (y - gy) * 22 / 10; int r = isq(dx * dx + dy * dy); if (r >= gr * 2) continue;
          int fall = 256 - r * 256 / (gr * 2); fall = fall * fall / 256;
          int th = iatan2(dy, dx); int arm = (isin(th * 2 - r * 360 / (gr / 2 + 1)) + 1024) / 8;            /* 0..256 */
          int in = fall * (90 + arm * 166 / 256) / 256;
          uint32_t gc = wmix(0x008AA8FFu, 0x00FFE3B8u, fall * 2 - 220);
          b[y * W + x] = wmix(b[y * W + x], gc, in * 190 / 256);
      } }
    { int px2 = W * 31 / 100, py2 = H * 47 / 100, pr2 = H * 6 / 100 + 5;
      for (int y = py2 - pr2 - 6; y <= py2 + pr2 + 6; y++) for (int x = px2 - pr2 - 6; x <= px2 + pr2 + 6; x++) {
          if (x < 0 || y < 0 || x >= W || y >= hz) continue;
          int dx = x - px2, dy = y - py2, d2 = dx * dx + dy * dy; uint32_t* q = &b[y * W + x];
          if (d2 <= pr2 * pr2) {
              int l = (-dx - dy * 2 / 3) * 256 / (2 * pr2) + 140; if (l < 30) l = 30; if (l > 256) l = 256;
              int band = (isin(dy * 1024 / (pr2 + 1) * 9 / 10 + vrausch(x, y / 3, 24, 51) / 8) + 1024) / 8;
              uint32_t pc = wmix(wmix(0x00B8501Cu, 0x00F4B070u, band), 0x00180806u, 256 - l);
              int rand = isq(d2) * 256 / pr2; if (rand > 225) pc = wmix(pc, 0x00FFB070u, (rand - 225) * 6);
              *q = pc;
          } else { int d = isq(d2); if (d < pr2 + 6) *q = wmix(*q, 0x00FF9A50u, (pr2 + 6 - d) * 70 / 7); }
      } }
    for (int k = 0; k < 9; k++) {
        int cx0 = (int)(h32(k, 1, 61) % (uint32_t)W), cy0 = (int)(h32(k, 2, 61) % (uint32_t)(hz * 8 / 10)), cr = 14 + (int)(h32(k, 3, 61) % 30);
        for (int i = 0; i < 45; i++) { int ox = (int)(h32(k * 64 + i, 4, 61) % (uint32_t)(cr * 2)) - cr, oy = (int)(h32(k * 64 + i, 5, 61) % (uint32_t)(cr * 2)) - cr; if (ox * ox + oy * oy > cr * cr) continue;
            int x = cx0 + ox, y = cy0 + oy; if (x < 0 || y < 0 || x >= W || y >= hz) continue; b[y * W + x] = wmix(b[y * W + x], 0x00FFF4E0u, 120 + (int)(h32(k * 64 + i, 6, 61) % 120)); }
    }
    /* Neon-Raster unter dem Horizont */
    for (int k = 1; k <= 16; k++) { int yk = hz + (H - hz) * k * k / 256; if (yk >= H) break; int t = 60 + k * 12; for (int x = 0; x < W; x++) { b[yk * W + x] = wmix(b[yk * W + x], 0x0022D3EEu, t); if (yk + 1 < H) b[(yk + 1) * W + x] = wmix(b[(yk + 1) * W + x], 0x0022D3EEu, t / 3); } }
    for (int i = -30; i <= 30; i++) { int xb = W / 2 + i * W / 9; for (int y = hz; y < H; y++) { int x = W / 2 + (xb - W / 2) * (y - hz) / (H - hz); if (x < 0 || x >= W) continue; int t = (y - hz) * 230 / (H - hz) + 20; b[y * W + x] = wmix(b[y * W + x], 0x00A855F7u, t); } }
}
/* Wasser-Hintergrund: tiefes Blau, Kaustiken (Lichtnetz wie unter einer Wasseroberflaeche), Lichtschein von oben */
static void wand_wasser(uint32_t* b) {
    int sk = W / 4 + 1, sh = H / 4 + 1; if (sk > 961) sk = 961; if (sh > 541) sh = 541;
    int z0 = W / 7 > 40 ? W / 7 : 40;
    for (int y = 0; y < sh; y++) for (int x = 0; x < sk; x++) {
        int X = x * 4, Y = y * 4;
        int wx = vrausch(X + 311, Y, z0, 21) - 512, wy = vrausch(X, Y + 977, z0, 22) - 512;                       /* Verwirbelung */
        int cs = 0;
        for (int o = 0; o < 2; o++) {                                                                              /* zwei Lagen Wellenlinien */
            int n = vrausch(X + wx * (o + 1) / 3 + o * 541, Y + wy * (o + 1) / 3, z0 / (o + 1) + 8, 23 + o);
            int g = 1023 - (n > 512 ? n - 512 : 512 - n) * 2;                                                      /* 1023 auf der Linie */
            g = g * g >> 10; g = g * g >> 10;                                                                       /* scharf */
            cs += g * 2 / 5;
        }
        neb1[y * sk + x] = (uint16_t)(cs > 1023 ? 1023 : cs);
        neb2[y * sk + x] = (uint16_t)vrausch(X + 123, Y + 321, z0 * 2, 27);
    }
    for (int y = 0; y < H; y++) {
        int qy = y / 4, fy = (y % 4) * 256 / 4; if (qy >= sh - 1) qy = sh - 2;
        int lt = 256 - y * 256 / H;                                                                              /* oben hell */
        uint32_t basis = wmix(0x00041226u, 0x000C5A86u, lt * lt / 256);
        for (int x = 0; x < W; x++) {
            int qx = x / 4, fx = (x % 4) * 256 / 4; if (qx >= sk - 1) qx = sk - 2;
            int ca = ((neb1[qy * sk + qx] * (256 - fx) + neb1[qy * sk + qx + 1] * fx) * (256 - fy) + (neb1[(qy + 1) * sk + qx] * (256 - fx) + neb1[(qy + 1) * sk + qx + 1] * fx) * fy) >> 16;
            int m = ((neb2[qy * sk + qx] * (256 - fx) + neb2[qy * sk + qx + 1] * fx) * (256 - fy) + (neb2[(qy + 1) * sk + qx] * (256 - fx) + neb2[(qy + 1) * sk + qx + 1] * fx) * fy) >> 16;
            uint32_t c = wmix(basis, 0x00087A8Cu, m / 6);                                                         /* tuerkise Schlieren */
            int dx = x - W / 2; int strahl = 256 - (dx < 0 ? -dx : dx) * 256 / (W / 2 + 1);                        /* Lichtkegel von oben */
            c = wmix(c, 0x007FDFF5u, ca * (lt * 2 / 3 + 40) / 1024 * (strahl + 128) / 384);
            { int rr = vrausch(x + y / 3, 0, W / 14 + 8, 31) + vrausch(x + y / 4 + 400, 0, W / 30 + 6, 32) / 2 - 560; if (rr > 0) c = wmix(c, 0x00B8F0FFu, rr * (lt * lt / 256) / 1100); }   /* Lichtstrahlen von oben */
            b[y * W + x] = c;
        }
    }
    /* Meeresboden mit Tang, aufsteigende Blasen, Schwebeteilchen */
    for (int x = 0; x < W; x++) { int hy = H - H / 11 - vrausch(x, 0, W / 5 + 8, 41) * (H / 9) / 1024; for (int y = hy; y < H; y++) { int dd = y - hy; b[y * W + x] = wmix(b[y * W + x], 0x00010A14u, dd < 6 ? 150 + dd * 18 : 256); } }
    for (int k = 0; k < W / 70; k++) {
        int x0 = (int)(h32(k, 1, 71) % (uint32_t)W), y0 = H - H / 14, hh = H / 7 + (int)(h32(k, 2, 71) % (uint32_t)(H / 5)), ph0 = (int)(h32(k, 3, 71) % 360);
        for (int yy = 0; yy < hh; yy++) { int x = x0 + isin(yy * 2 + ph0) * (6 + yy / 14) / 1024, y = y0 - yy; if (y < 0) break; int t = 230 - yy * 150 / hh;
            for (int dx = -1; dx <= 1; dx++) if (x + dx >= 0 && x + dx < W) b[y * W + x + dx] = wmix(b[y * W + x + dx], 0x00083C40u, t); }
    }
    for (int k = 0; k < W * H / 40000; k++) {
        int bx = (int)(h32(k, 1, 81) % (uint32_t)W), by = (int)(h32(k, 2, 81) % (uint32_t)(H * 85 / 100)), br = 3 + (int)(h32(k, 3, 81) % 11);
        for (int y = by - br - 1; y <= by + br + 1; y++) for (int x = bx - br - 1; x <= bx + br + 1; x++) {
            if (x < 0 || y < 0 || x >= W || y >= H) continue;
            int dx = x - bx, dy = y - by, d = isq(dx * dx + dy * dy); uint32_t* q = &b[y * W + x];
            if (d == br || d == br - 1) *q = wmix(*q, 0x00C8F6FFu, d == br ? 170 : 90);
            else if (d < br) *q = wmix(*q, 0x0080D8F0u, 28);
            if (dx == -br / 3 && dy == -br / 3 && br > 4) *q = wmix(*q, 0x00FFFFFFu, 220);
        }
    }
    for (int k = 0; k < W * H / 1500; k++) { int x = (int)(h32(k, 1, 91) % (uint32_t)W), y = (int)(h32(k, 2, 91) % (uint32_t)H); b[y * W + x] = wmix(b[y * W + x], 0x00D8F8FFu, 70 + (int)(h32(k, 3, 91) % 120)); }
}
/* Zynex-Hintergrund: das eingebettete Bild (1024x559) bildfuellend auf den Bildschirm skaliert (bilinear) */
static void wand_zynex(uint32_t* b) {
    int64_t s16 = (int64_t)W * 65536 / HB_W; int64_t sh16 = (int64_t)H * 65536 / HB_H; if (sh16 > s16) s16 = sh16;      /* Faktor in 16.16 */
    int64_t ox = ((int64_t)HB_W * s16 - (int64_t)W * 65536) / 2, oy = ((int64_t)HB_H * s16 - (int64_t)H * 65536) / 2;
    for (int y = 0; y < H; y++) {
        int64_t v = ((int64_t)y * 65536 + oy) * 65536 / s16; int y0 = (int)(v >> 16), fy = (int)((v >> 8) & 255); if (y0 < 0) y0 = 0; if (y0 >= HB_H - 1) { y0 = HB_H - 2; fy = 255; }
        const uint16_t* r0 = hintergrund_bild + y0 * HB_W; const uint16_t* r1 = r0 + HB_W;
        for (int x = 0; x < W; x++) {
            int64_t u = ((int64_t)x * 65536 + ox) * 65536 / s16; int x0 = (int)(u >> 16), fx = (int)((u >> 8) & 255); if (x0 < 0) x0 = 0; if (x0 >= HB_W - 1) { x0 = HB_W - 2; fx = 255; }
            uint32_t a = mix(c565(r0[x0]), c565(r0[x0 + 1]), fx), c = mix(c565(r1[x0]), c565(r1[x0 + 1]), fx);
            b[y * W + x] = mix(a, c, fy);
        }
    }
}
static int wand_bereit(void) {                                           /* 1 = Hintergrundbild im Speicher passend zur Wahl */
    if (hg_nr < 4) return 0;
    if (!wand) { uint32_t s = ((uint32_t)W * (uint32_t)H * 4 + 4095) / 4096; if (pmm_get_free_pages() < s + 8192) return 0; wand = (uint32_t*)pmm_alloc_pages(s); if (!wand) return 0; }
    if (wand_art == hg_nr && (hg_nr == 4 || hg_nr == 6 || hg_nr == 7 || ueq(wand_name, hg_datei))) return 1;
    if (hg_nr == 5) { if (wand_bild(hg_datei, wand) != 0) { kprint("[DESKTOP] Hintergrundbild nicht lesbar: "); kprint(hg_datei); kprint("\n"); hg_nr = 4; } else { scopy(wand_name, hg_datei, 32); wand_art = 5; kprint("[DESKTOP] Hintergrundbild: "); kprint(hg_datei); kprint("\n"); return 1; } }
    if (hg_nr == 7) { wand_zynex(wand); wand_art = 7; kprint("[DESKTOP] Zynex-Hintergrund berechnet\n"); return 1; }
    if (hg_nr == 6) { wand_wasser(wand); wand_art = 6; kprint("[DESKTOP] Wasser-Hintergrund berechnet\n"); return 1; }
    wand_futur(wand); wand_art = 4; kprint("[DESKTOP] Futuristischer Hintergrund berechnet\n");
    return 1;
}
static void hg(int x, int y, int w, int h) {
    if (x < 0) { w += x; x = 0; } if (y < 0) { h += y; y = 0; } if (x + w > W) w = W - x; if (y + h > H) h = H - y;
    if (w <= 0 || h <= 0) return;
    gfx_begin();
    if (hg_nr == 3) { gfx_rect(x, y, w, h, th_hg1); gfx_end(); return; }
    if (hg_nr >= 4 && wand_bereit()) { gfx_bild(x, y, w, h, wand + y * W + x, W, 0, H); gfx_end(); return; }
    for (int r = y; r < y + h; ) {
        int t = r * 256 / H, e = r + 1; while (e < y + h && e * 256 / H == t) e++;
        uint32_t c = mix(th_hg1, th_hg2, t);
        gfx_rect(x, r, w, e - r, c);
        if (hg_nr == 2) {
            uint32_t lc = mix(c, th_border, 110);
            for (int yy = r; yy < e; yy++) if (yy % 56 == 0) gfx_rect(x, yy, w, 1, lc);
            for (int xx = (x + 55) / 56 * 56; xx < x + w; xx += 56) gfx_rect(xx, r, 1, e - r, lc);
        } else if (hg_nr == 1) for (int yy = r; yy < e; yy++) hg_glow(x, yy, w, c);
        r = e;
    }
    gfx_end();
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
/* Welle: ein Ring laeuft vom Klick nach aussen und verzerrt das Bild darunter (Bildausschnitt wird gesichert und danach wiederhergestellt) */
extern uint32_t* gfx_fbptr(uint32_t* pitch_px);
static uint32_t* wsnap;
static int isqw(int v) { int r = 0; for (int b = 1 << 15; b; b >>= 1) { int t = r | b; if (t * t <= v) r = t; } return r; }
extern void virtiogpu_tick(void);
static void wasser_welle(int cx, int cy) {                                  /* Ring laeuft ueber den ganzen Bildschirm, zeitgesteuert (~1,3 s) */
    uint32_t pp; uint32_t* f = gfx_fbptr(&pp); if (!f) return;
    if (!wsnap) { uint32_t pg = ((uint32_t)W * (uint32_t)H * 4 + 4095) / 4096; if (pmm_get_free_pages() < pg + 4096) return; wsnap = (uint32_t*)pmm_alloc_pages(pg); if (!wsnap) return; }
    gfx_begin();
    for (int y = 0; y < H; y++) { uint32_t* row = f + (uint64_t)y * pp; uint32_t* sn = wsnap + (uint64_t)y * W; for (int x = 0; x < W; x++) sn[x] = row[x]; }
    int ex = cx > W - cx ? cx : W - cx, ey = cy > H - cy ? cy : H - cy, Rm = isqw(ex * ex + ey * ey);
    const int W0 = 40, T = 130;
    uint32_t t0 = timer_get_ticks(); int rprev = -1000;
    for (;;) {
        int el = (int)(timer_get_ticks() - t0); if (el > T) break;
        int r = (int)((int64_t)(Rm + W0) * el / T), A = 2 + 12 * (T - el) / T;                      /* Frontradius, Wellenhoehe in Pixeln */
        int dmax = r + W0, dminp = rprev - W0; if (dmax > Rm + W0) dmax = Rm + W0;
        int ya = cy - dmax; if (ya < 0) ya = 0; int yb = cy + dmax; if (yb >= H) yb = H - 1;
        for (int y = ya; y <= yb; y++) {
            int dy = y - cy, xo2 = dmax * dmax - dy * dy; if (xo2 < 0) continue;
            int xo = isqw(xo2), xi = (dminp > 0 && dminp * dminp > dy * dy) ? isqw(dminp * dminp - dy * dy) : -1;
            uint32_t* row = f + (uint64_t)y * pp; uint32_t* sn = wsnap + (uint64_t)y * W;
            for (int x = cx - xo < 0 ? 0 : cx - xo; x <= cx + xo && x < W; x++) {
                int dx = x - cx; if (xi >= 0 && dx > -xi && dx < xi) { x = cx + xi - 1; continue; }
                int d = isqw(dx * dx + dy * dy), u = d - r, au = u < 0 ? -u : u;
                if (au >= W0 || d == 0) { row[x] = sn[x]; continue; }
                int off = isin(u * 9) * A / 1024 * (W0 - au) / W0;
                int sx = x + dx * off / d, sy = y + dy * off / d;
                if (sx < 0) sx = 0; if (sx >= W) sx = W - 1; if (sy < 0) sy = 0; if (sy >= H) sy = H - 1;
                uint32_t c = wsnap[(uint64_t)sy * W + sx];
                row[x] = off > 0 ? mix(c, 0x00FFFFFFu, off * 9) : off < 0 ? mix(c, 0x00000000u, -off * 6) : c;
            }
        }
        rprev = r; virtiogpu_tick();
    }
    for (int y = 0; y < H; y++) { uint32_t* row = f + (uint64_t)y * pp; uint32_t* sn = wsnap + (uint64_t)y * W; for (int x = 0; x < W; x++) row[x] = sn[x]; }
    gfx_end();
}
static void lader(int cx, int cy, int R, uint32_t fr, uint32_t bg) {
    static const uint32_t farbe[3] = { 0x0022D3EEu, 0x00A78BFAu, 0x006F90FBu };
    gfx_begin();
    if (bg == C_WALL) { hg(cx - R - 8, cy - R - 8, 2 * R + 16, 2 * R + 16); bg = hg_zeile(cy); }
    else gfx_rect(cx - R - 8, cy - R - 8, 2 * R + 16, 2 * R + 16, bg);
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
    thema_anwenden(0);
    gfx_begin();
    gfx_rect(0, 0, W, H, C_SPL);
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
    lader(W - 90, 90, 36, timer_get_ticks() / 4, C_SPL);
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
    { uint32_t lf = 0; for (int i = 0; i < 2000000 && timer_get_ticks() - t0 < 90; i++) { __asm__ volatile ("pause"); if (timer_get_ticks() / 4 != lf) { lf = timer_get_ticks() / 4; lader(W - 90, 90, 36, lf, C_SPL); } } }
    splash_an = 0;
}

/* ---------- Zustand ---------- */
enum { Z_LOGIN, Z_DESK, Z_EDIT, Z_WEB, Z_RECH, Z_INFO, Z_OFF };
static int zustand;
static char fname[16], fpw[40]; static int ffocus, fmsg;
static char eingabe[96]; static int einlen;
static char antwort[6][100]; static int nantwort;
#define QN 32
static uint16_t kq[QN]; static unsigned qh, qt; static int was_down;
static struct { int x, y, w, h, id, owner; } knoepfe[320]; static int nkn;
static uint32_t robot_phase;
static uint32_t sprech_bis, letzte_eingabe;               /* Ticks (100 pro Sekunde) */

/* Textverarbeitung */
#define DOK_MAX 16000
#define ED_ZMAX 2000
static char dok[DOK_MAX + 1]; static int doklen, dokcur, dokscroll, dokgeaendert; static char dokname[24];
/* Rich Text: je Zeichen ein Stilbyte. Bit0 fett, Bit1 kursiv, Bit2 unterstrichen, Bit3 Schreibmaschinenschrift (Mono), Bit4-6 Groessenrang 0..4 (13, 15, 27, 40, 54 px) */
#define ST_STD 0x10
#define ST_FETT 1
#define ST_KURSIV 2
#define ST_UNTER 4
#define ST_MONO 8
extern int gfx_font_box(int font);
extern int gfx_zeichen_adv(unsigned char ch, int font, int stil);
extern int gfx_zeichen_stil(int x, int y, unsigned char ch, uint32_t fg, int font, int stil);
static uint8_t dokst[DOK_MAX + 1]; static int doksel = -1; static uint8_t dok_cst = ST_STD;
static int ed_pxw = 600; static int ed_zh[ED_ZMAX], ed_za[ED_ZMAX];
static const int st_px[5] = { 13, 15, 27, 40, 54 };
static int st_font(uint8_t st) { int r = (st >> 4) & 7; if (r > 4) r = 4; static const uint8_t sans[5] = { 12, 4, 5, 6, 7 }, mono[5] = { 0, 0, 1, 2, 3 }; return (st & ST_MONO) ? mono[r] : sans[r]; }
static int st_adv(unsigned char c, uint8_t st) { return gfx_zeichen_adv(c, st_font(st), st & 3); }
static int st_asc(uint8_t st) { return (gfx_font_box(st_font(st)) * 77 + 50) / 100; }
static int st_desc(uint8_t st) { return gfx_font_box(st_font(st)) - st_asc(st); }
static int ed_selbereich(int* a, int* b) { if (doksel < 0 || doksel == dokcur) return 0; *a = doksel < dokcur ? doksel : dokcur; *b = doksel < dokcur ? dokcur : doksel; return 1; }
static void dok_loesche(int a, int b) {
    if (a < 0) a = 0; if (b > doklen) b = doklen; if (b <= a) return;
    int n = b - a; for (int i = a; i <= doklen - n; i++) { dok[i] = dok[i + n]; dokst[i] = dokst[i + n]; }
    doklen -= n; if (dokcur > b) dokcur -= n; else if (dokcur > a) dokcur = a; dok[doklen] = 0; dokgeaendert = 1;
}
static int dok_einf(int pos, unsigned char c, uint8_t st) {
    if (doklen >= DOK_MAX || pos < 0 || pos > doklen) return 0;
    for (int k = doklen; k > pos; k--) { dok[k] = dok[k - 1]; dokst[k] = dokst[k - 1]; }
    dok[pos] = (char)c; dokst[pos] = st; doklen++; dok[doklen] = 0; dokgeaendert = 1; return 1;
}
static void dok_sel_loeschen(void) { int a, b; if (ed_selbereich(&a, &b)) { dok_loesche(a, b); dokcur = a; } doksel = -1; }
static void dok_leeren(void) { doklen = 0; dokcur = 0; dok[0] = 0; doksel = -1; dok_cst = ST_STD; dokgeaendert = 1; }
static void ed_stil_sync(void) { dok_cst = dokcur > 0 ? dokst[dokcur - 1] : ST_STD; }
static uint8_t ed_stil_jetzt(void) { int a, b; if (ed_selbereich(&a, &b)) return dokst[a]; return dok_cst; }
static void ed_stil_toggle(int bit) {
    int a, b;
    if (ed_selbereich(&a, &b)) { int alle = 1; for (int i = a; i < b; i++) if (!(dokst[i] & bit)) alle = 0; for (int i = a; i < b; i++) { if (alle) dokst[i] &= (uint8_t)~bit; else dokst[i] |= (uint8_t)bit; } dokgeaendert = 1; }
    else dok_cst ^= (uint8_t)bit;
}
static void ed_stil_groesse(int d) {
    int a, b; uint8_t s0 = ed_stil_jetzt(); int r = ((s0 >> 4) & 7) + d; if (r < 0) r = 0; if (r > 4) r = 4;
    if (ed_selbereich(&a, &b)) { for (int i = a; i < b; i++) dokst[i] = (uint8_t)((dokst[i] & 0x8F) | (r << 4)); dokgeaendert = 1; }
    else dok_cst = (uint8_t)((dok_cst & 0x8F) | (r << 4));
}
static void ed_stil_art(void) { ed_stil_toggle(ST_MONO); }
static void ed_stil_setze(uint8_t st) {
    int a, b; if (ed_selbereich(&a, &b)) { for (int i = a; i < b; i++) dokst[i] = st; dokgeaendert = 1; } else dok_cst = st;
}
static int ed_x, ed_y, ed_w, ed_h, ed_cols, ed_rows, ed_s;

static int kn_owner = -1;
static void kn_reg(int id, int x, int y, int w, int h) { if (nkn < 320) { knoepfe[nkn].owner = kn_owner; knoepfe[nkn].x = x; knoepfe[nkn].y = y; knoepfe[nkn].w = w; knoepfe[nkn].h = h; knoepfe[nkn].id = id; nkn++; } }
static void knopf(int id, const char* label, int x, int y, int w, int primaer) {
    if (Q_GLAS) {
        if (primaer) { rrect(x - 1, y - 1, w + 2, 36, 13, mix(C_BG, Q_CY, 60)); h_pille(x, y, w, 34, 0x0006B6D4u, 0x002563EBu); gtxt(x + (w - gtw(label, 1)) / 2, y + 9, label, 0x00031221u, 1); }
        else { rrect(x, y, w, 34, 12, mix(C_BG, 0x00FFFFFFu, 34)); rrect(x + 1, y + 1, w - 2, 32, 11, mix(C_BG, C_CARD, 200)); gtxt(x + (w - gtw(label, 1)) / 2, y + 9, label, C_TEXT, 1); }
    } else {
        rrect_rand(x, y, w, 34, 10, primaer ? C_ACC : C_CARD, primaer ? C_ACC : C_BORDER);
        gtxt(x + (w - gtw(label, 1)) / 2, y + 9, label, primaer ? 0x00FFFFFFu : C_TEXT, 1);
    }
    kn_reg(id, x, y, w, 34);
}

static void antwort_setze(const char* a, const char* b, const char* c, const char* d, const char* e, const char* f) {
    const char* t[6] = { a, b, c, d, e, f }; nantwort = 0;
    int zeichen = 0;
    for (int i = 0; i < 6; i++) if (t[i]) { scopy(antwort[nantwort], t[i], 100); zeichen += slen(t[i]); nantwort++; }
    sprech_bis = timer_get_ticks() + (uint32_t)zeichen * 4 + 40; letzte_eingabe = timer_get_ticks();     /* ca. 40 ms je Zeichen */
}
static void reply1(const char* s) { antwort_setze(s, 0, 0, 0, 0, 0); }
static int ra_darf(const char* name, int stufe) { return raum_stufe(datei_raum(name), benutzer_name(), benutzer_rolle()) >= stufe; }   /* Raum-Schluessel der Datei */

/* ---------- Zynos, der Roboter (verschiebbar, skalierbar, spricht, schaut, Ruhemodus) ---------- */
static int zx, zy, zs = 160;                              /* Mitte, Groesse in Prozent */
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
    /* Zynos: das gezeichnete Leuchtwesen (eingebettetes Bild). Schwarz ist durchsichtig (Bildschirm-Mischung), die Figur schwebt ueber ihrer Plattform */
    (void)cx; (void)cy;
    uint32_t t = timer_get_ticks();
    int spricht = t < sprech_bis, ruhe = ruhend();
    int puls = (int)((ph / 3) % 16); if (puls > 8) puls = 16 - puls;
    int st = ruhe ? 110 : spricht ? 256 : 200 + puls * 7;                                   /* Helligkeit 0..256 */
    int bob = ruhe ? 0 : (isin((int)((t / 5) % 360)) + 1024) * SC(12) / 2048;               /* 0 .. SC(12) Pixel nach unten */
    hg(zr_x(), zr_y(), zr_w(), zr_h());
    uint32_t pp; uint32_t* f = gfx_fbptr(&pp);
    int x0 = zr_x(), y0 = zr_y(), w = zr_w(), h = zr_h(), dh = h - SC(12), split = ZB_SPLIT * dh / ZB_H;
    if (f && dh > 8 && w > 8) {
        static const int lipp[6] = { 3, 12, 18, 8, 14, 5 };
        const int MX = 140, MY = 112, MW = 11; int oh = 1 + lipp[(t / 6) % 6] * 7 / 18;              /* Mund im Bild: Mitte, halbe Breite, halbe Hoehe (bewegt sich beim Sprechen) */
        for (int y = 0; y < dh; y++) {
            int yy = y0 + y + (y < split ? bob : 0); if (yy < 0 || yy >= H) continue;
            int sy = y * ZB_H / dh; const uint16_t* sr = zynie_bild + sy * ZB_W; uint32_t* row = f + (uint64_t)yy * pp;
            for (int x = 0; x < w; x++) {
                int sx = x * ZB_W / w; uint16_t v = sr[sx]; int rim = 0;
                int xx = x0 + x; if (xx < 0 || xx >= W) continue;
                if (spricht && sx >= MX - MW - 3 && sx <= MX + MW + 3 && sy >= MY - oh - 3 && sy <= MY + oh + 3) {
                    int e = (sx - MX) * (sx - MX) * 64 / (MW * MW) + (sy - MY) * (sy - MY) * 64 / (oh * oh);
                    if (e < 64) { row[xx] = mix(row[xx], 0x00030B14u, 232); continue; }
                    if (e < 105) { rim = 1; if (!v) v = 1; }
                }
                if (!v) continue;
                uint32_t c = c565(v), bg = row[xx], o = 0;
                if (rim) c = mix(c, 0x0070F4FFu, 170);
                for (int sh = 0; sh <= 16; sh += 8) { int cc = (int)((c >> sh) & 255) * st / 256, bb = (int)((bg >> sh) & 255); o |= (uint32_t)(255 - (255 - bb) * (255 - cc) / 255) << sh; }
                row[xx] = o;
            }
        }
    }
    if (ruhe) { gfx_text(x0 + w * 70 / 100, y0 + h * 12 / 100, "z", 0x00647488u, 1 + zs / 120); gfx_text(x0 + w * 80 / 100, y0 + h * 4 / 100, "Z", 0x00647488u, 1 + zs / 100); }
    if (spricht) { gfx_rect(x0 + w / 2 - 20, y0 + h - 6, 40, 2, C_VIOLET); }
    int gx = x0 + w - 12, gy = y0 + h - 12;                                         /* Griff zum Vergroessern */
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
static char lg_info[80]; static void lg_rad_start(void); static int rw_aktiv; static int entsperr;     /* 1 = die Platte ist verschluesselt und gesperrt: statt der Anmeldung wird das Plattenpasswort abgefragt */
static void zeichne_login(void) {
    thema_anwenden(0);
    gfx_begin();
    gfx_rect(0, 0, W, H, C_SPL);
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
    rrect(cx, y + 222, cw, 40, 14, mix(C_SPL, Q_CY, 120)); rrect(cx + 1, y + 223, cw - 2, 38, 13, mix(C_SPL, C_CARD, 200));
    { const char* t = TR("Mit Rad-Schluessel anmelden (F2)", "Sign in with wheel key (F2)"); gtxt(cx + (cw - gtw(t, 1)) / 2, y + 233, t, Q_CY, 1); }
    if (lg_info[0]) gtxt(cx, y + 274, lg_info, C_ERR, 1);
    gfx_end();
    hv_status_zeichnen();
}

/* ---------- Desktop ---------- */
static const char* const chip_text[6] = { "text erstellen", "rechne 12*7", "system", "dateien", "quanten", "browser" };
static int bar_x, bar_y, bar_w;

/* ---------- Fenster und Widgets (verschiebbar, schliessbar, mehrere gleichzeitig) ---------- */
enum { FT_EDIT = 1, FT_WEB, FT_RECH, FT_INFO, FT_EINST, FT_CHATS, FT_GED, FT_TAB, FT_PRAES, FT_BOARD, FT_MAL, FT_DATEI, FT_BENUTZER };
static void zeichne_tabelle(void); static void zeichne_praes(void); static void zeichne_board(void); static void zeichne_mal(void);
static int ob_vollbild;                                     /* Praesentation laeuft im Vollbild */
static void praes_vollbild_zeichnen(void);
static uint32_t ob_farbe(int typ);                          /* Programmfarbe (Titelleiste) */
static void ag_office(int i); static void ag_office_frei(int p); static void office_init(int typ); static void office_schliessen(int typ);
enum { WG_UHR = 1, WG_SYS, WG_AUF, WG_AGENT, WG_NOTIZ };
#define WT_H 30
struct fenster { int typ, x, y, w, h; };
static struct fenster fen[6]; static int nfen, fok;          /* Reihenfolge = Tiefe (das letzte liegt oben); fok: das oberste Fenster hat den Tastaturfokus */
#define NWID_MAX 10
#define NZ_N 8                                           /* Notizzettel */
#define NZ_T 300
struct widget { int typ, x, y, w, h, id; };
static struct widget wid[NWID_MAX]; static int nwid;
static char nz[NZ_N][NZ_T + 1]; static int nz_len[NZ_N], nz_used[NZ_N]; static int notiz_fok = -1; static char nz_meld[48];
static int ob_ki(const char* auftrag, const char* daten, char* out, int max);
static int notiz_taste(uint8_t k, int shift, int altgr);
static void ac_aktualisieren(void); static void ac_wahl(int i);
static void layout_speichern(void) {
    char b[220], a[12]; b[0] = 0;
    for (int j = 0; j < nwid; j++) { dec(a, (uint32_t)(wid[j].typ == WG_NOTIZ ? 10 + wid[j].id : wid[j].typ)); cat(b, a, 220); cat(b, ",", 220); dec(a, (uint32_t)(wid[j].x + 2000)); cat(b, a, 220); cat(b, ",", 220); dec(a, (uint32_t)(wid[j].y + 2000)); cat(b, a, 220); cat(b, ";", 220); }
    zdb_put("desktop.widgets", b);
    b[0] = 0; dec(a, (uint32_t)zx); cat(b, a, 100); cat(b, " ", 100); dec(a, (uint32_t)zy); cat(b, a, 100); cat(b, " ", 100); dec(a, (uint32_t)zs); cat(b, a, 100);
    zdb_put("desktop.zynos", b);
}
static int lies(const char** p) { while (**p && (**p < '0' || **p > '9')) (*p)++; int v = 0; while (**p >= '0' && **p <= '9') { v = v * 10 + (**p - '0'); (*p)++; } return v; }
static int in_szene;
static int nsug, sugsel = -1;
static int flaeche;
static char alog[10][100]; static int nalog, alauf = -1, pend;
static void aufg_laden(void); static int aufg_zeile(int n, char* out, int max); static int aufg_zahl(void);
static const char* const ag_name[9][2] = { { "Morgen-Briefing", "Morning briefing" }, { "Sicherheits-Agent", "Security agent" }, { "Wartungs-Agent", "Maintenance agent" }, { "Aufgaben-Agent", "Task agent" }, { "Dokument-Agent", "Document agent" }, { "Tabellen-Agent", "Sheet agent" }, { "Folien-Agent", "Slides agent" }, { "Ablauf-Agent (SmartBoard)", "Flow agent (SmartBoard)" }, { "Bild-Agent", "Image agent" } };
static void zeichne_benutzer(void); static void zeichne_datei(void); static void zeichne_einst(void); static void zeichne_chats(void); static void zeichne_ged(void); static void zeichne_desktop(void); static void zeichne_editor(int voll); static void zeichne_web(int voll); static void zeichne_rechner(void); static void zeichne_info(void);
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
static int desk_gen;                                              /* zaehlt ganze Neuzeichnungen (macht den Schnappschuss des Befehlsrads ungueltig) */
static int rad_modus(void);                                      /* 1 = Befehlsrad statt Vorschlagsliste (Maus vorhanden) */
static void rad_weg(void);
static void busy_tick(void) { uint32_t f = timer_get_ticks() / 5; if (!busy_an || f != busy_f) { busy_an = 1; busy_f = f; lader(W - 70, H - 70, 28, f, C_WALL); } }
static void busy_ende(void) { if (!busy_an) return; busy_an = 0; gfx_begin(); hg(W - 70 - 40, H - 70 - 40, 80, 80); gfx_end(); zeichne_desktop(); }
static const char* fenster_titel(int typ) { if (typ == FT_MAL) return TR("Zynex Zeichnen", "Zynex Draw"); if (typ == FT_TAB) return TR("Zynex Tabelle", "Zynex Sheets"); if (typ == FT_PRAES) return TR("Zynex Praesentation", "Zynex Slides"); if (typ == FT_BOARD) return "SmartBoard"; if (typ == FT_GED) return TR("Gedaechtnis", "Memory"); if (typ == FT_DATEI) return TR("Datei-Explorer", "File Explorer"); if (typ == FT_BENUTZER) return TR("Benutzerverwaltung", "User management"); if (typ == FT_EINST) return TR("Einstellungen", "Settings"); if (typ == FT_CHATS) return "Chats"; return typ == FT_EDIT ? TR("Zynex Text", "Zynex Text") : typ == FT_WEB ? "Browser" : typ == FT_RECH ? TR("Rechner", "Calculator") : TR("System-Info", "System info"); }
static void fenster_zeichnen(int i) {
    struct fenster* f = &fen[i]; int top = (i == nfen - 1 && fok);
    gfx_begin();
    if (Q_GLAS) {
        uint32_t w0 = hg_zeile(f->y + f->h / 2);
        rrect(f->x - 6, f->y - 2, f->w + 12, f->h + 14, 26, mix(w0, 0x00000000u, 40));                                 /* weicher Schatten */
        rrect(f->x - 3, f->y + 2, f->w + 6, f->h + 8, 22, mix(w0, 0x00000000u, 90));
        if (top) { rrect(f->x - 5, f->y - 4, f->w + 10, f->h + 8, 24, mix(w0, Q_VIO, 22)); rrect(f->x - 2, f->y - 2, f->w + 4, f->h + 4, 21, mix(w0, Q_CY, 34)); }   /* Leuchtrand */
        rrect(f->x, f->y, f->w, f->h, 18, top ? mix(0x00122A44u, Q_SKY, 150) : mix(C_BG, Q_SKY, 70));                 /* Rand */
        rr_grad(f->x + 1, f->y + 1, f->w - 2, f->h - 2, 17, mix(C_CARD, C_BG, 100), mix(C_BG, 0x00000000u, 90));      /* Glaskoerper */
        uint32_t t1 = top ? mix(C_BG, Q_CY, 52) : mix(C_BG, Q_CY, 22), t2 = top ? mix(C_BG, Q_VIO, 78) : mix(C_BG, Q_VIO, 30);
        rrect(f->x + 1, f->y + 1, 40, WT_H, 16, t1); rrect(f->x + f->w - 41, f->y + 1, 40, WT_H, 16, t2);
        h_grad(f->x + 20, f->y + 1, f->w - 40, WT_H - 1, mix(t1, t2, 256 * 20 / (f->w > 0 ? f->w : 1)), mix(t1, t2, 256 - 256 * 20 / (f->w > 0 ? f->w : 1)));
        gfx_rect(f->x + 2, f->y + WT_H - 1, f->w - 4, 1, top ? mix(C_BG, Q_SKY, 130) : mix(C_BG, Q_SKY, 50));
        { uint32_t oc = ob_farbe(f->typ); uint32_t ac = oc ? oc : (top ? Q_CY : C_OFF);                                 /* Mini-Symbol */
          rrect(f->x + 11, f->y + 6, 20, 20, 7, top ? mix(C_BG, Q_SKY, 150) : mix(C_BG, Q_SKY, 60)); rrect(f->x + 12, f->y + 7, 18, 18, 6, 0x00070D1Au);
          rrect(f->x + 17, f->y + 12, 8, 8, 4, top ? ac : mix(ac, 0x00606878u, 150)); }
        gtxt(f->x + 38, f->y + 8, fenster_titel(f->typ), top ? 0x00FFFFFFu : C_MUTED, 1);
        rrect(f->x + f->w - 32, f->y + 5, 22, 20, 10, top ? mix(C_BG, 0x00F87171u, 150) : mix(C_BG, C_OFF, 150)); rrect(f->x + f->w - 31, f->y + 6, 20, 18, 9, top ? mix(C_BG, 0x00EF4444u, 70) : mix(C_BG, C_OFF, 50));
        gtxt(f->x + f->w - 25, f->y + 7, "x", top ? 0x00FCA5A5u : C_MUTED, 1);
    } else {
    { uint32_t w0 = hg_zeile(f->y + f->h / 2), sc = th_nr == 5 ? 0x00203050u : 0x00000000u; int st = th_nr == 5 ? 40 : 100;      /* weicher Schatten */
      rrect(f->x - 3, f->y + 3, f->w + 6, f->h + 8, 18, mix(w0, sc, st / 2)); rrect(f->x - 1, f->y + 5, f->w + 2, f->h + 3, 15, mix(w0, sc, st)); }
    rrect_rand(f->x, f->y, f->w, f->h, 14, C_BG, top ? C_ACC : C_BORDER);
    if (ob_farbe(f->typ)) { uint32_t oc = ob_farbe(f->typ); rrect(f->x + 2, f->y + 2, f->w - 4, WT_H - 2, 12, top ? oc : mix(oc, 0x00808080u, 120)); gfx_rect(f->x + 2, f->y + WT_H - 8, f->w - 4, 8, top ? oc : mix(oc, 0x00808080u, 120)); }
    else {
    rrect(f->x + 2, f->y + 2, f->w - 4, WT_H - 2, 12, top ? mix(C_CARD, C_ACC, 70) : C_CARD);
    gfx_rect(f->x + 3, f->y + WT_H - 3, f->w - 6, 2, top ? C_ACC : C_BORDER); }
    rrect(f->x + 14, f->y + 10, 10, 10, 5, top ? C_ACC2 : C_OFF);
    gtxt(f->x + 32, f->y + 8, fenster_titel(f->typ), ob_farbe(f->typ) ? 0x00FFFFFFu : top ? C_TEXT : C_MUTED, 1);
    rrect(f->x + f->w - 32, f->y + 5, 22, 20, 10, top ? 0x00EF4444u : C_OFF); gtxt(f->x + f->w - 25, f->y + 7, "x", 0x00FFFFFFu, 1);
    }
    kn_owner = i;
    if (f->typ == FT_EDIT) zeichne_editor(1); else if (f->typ == FT_WEB) zeichne_web(1); else if (f->typ == FT_RECH) zeichne_rechner(); else if (f->typ == FT_EINST) zeichne_einst(); else if (f->typ == FT_CHATS) zeichne_chats(); else if (f->typ == FT_GED) zeichne_ged(); else if (f->typ == FT_DATEI) zeichne_datei(); else if (f->typ == FT_BENUTZER) zeichne_benutzer(); else if (f->typ == FT_TAB) zeichne_tabelle(); else if (f->typ == FT_PRAES) zeichne_praes(); else if (f->typ == FT_BOARD) zeichne_board(); else if (f->typ == FT_MAL) zeichne_mal(); else zeichne_info();
    kn_owner = -1;
    gfx_end();
}
static void kn_loesche_owner(int o) { int k = 0; for (int i = 0; i < nkn; i++) if (knoepfe[i].owner != o) knoepfe[k++] = knoepfe[i]; nkn = k; }

/* Widgets */
static void wid_groesse(int typ, int* w, int* h) { *w = typ == WG_NOTIZ ? 280 : typ == WG_UHR ? 230 : 260; *h = typ == WG_NOTIZ ? 230 : typ == WG_UHR ? 110 : typ == WG_SYS ? 120 : 150; }
static void notiz_speichern(void) {
    static char b[NZ_N * (NZ_T + 4) + 8]; int o = 0;
    for (int i = 0; i < NZ_N; i++) if (nz_used[i]) { b[o++] = (char)('0' + i); b[o++] = 0x1F; for (int k = 0; k < nz_len[i]; k++) b[o++] = nz[i][k]; b[o++] = 0x1E; }
    b[o] = 0; zdb_put_text("desktop.notizen", b, o);
}
static void notiz_laden(void) {
    static char b[NZ_N * (NZ_T + 4) + 8]; int r = zdb_get_text("desktop.notizen", b, sizeof(b) - 1); if (r <= 0) return; b[r] = 0;
    int i = 0; while (i < r) {
        int id = b[i] - '0'; if (id < 0 || id >= NZ_N || b[i + 1] != 0x1F) break;
        i += 2; int n = 0; while (i < r && b[i] != 0x1E && n < NZ_T) nz[id][n++] = b[i++];
        nz[id][n] = 0; nz_len[id] = n; while (i < r && b[i] != 0x1E) i++; i++;
    }
}
static void notiz_zeichnen(int j) {
    struct widget* g = &wid[j]; int id = g->id; uint32_t gelb = 0x00FFF2A8u, kopf = 0x00F6DE6Fu, tx = 0x003B3300u;
    gfx_begin();
    rrect_rand(g->x, g->y, g->w, g->h, 10, gelb, 0x00D9BE3Cu);
    rrect(g->x + 1, g->y + 1, g->w - 2, 26, 9, kopf); gfx_rect(g->x + 1, g->y + 16, g->w - 2, 11, kopf);
    gtxt(g->x + 12, g->y + 5, nz_meld[0] && notiz_fok == j ? nz_meld : TR("Notiz", "Note"), tx, 1);
    gtxt(g->x + g->w - 22, g->y + 5, "x", tx, 1);
    int px = g->x + 12, cols = g->w - 24, ly = g->y + 36, lh = 18, bottom = g->y + g->h - 40; char z[100]; int zl = 0, fx = px, fy = ly;
    for (int i = 0; i <= nz_len[id]; i++) {
        int ende = i == nz_len[id], c = ende ? 0 : (unsigned char)nz[id][i];
        if (!ende && c != '\n') {
            if (zl < 96) { z[zl++] = (char)c; z[zl] = 0; }
            if (zl > 1 && gtw(z, 1) > cols) {
                int b = zl - 1; while (b > 0 && z[b] != ' ') b--;
                if (b > 0) { z[b] = 0; if (ly + lh <= bottom) gtxt(px, ly, z, tx, 1); ly += lh; int r = zl - b - 1; for (int k = 0; k < r; k++) z[k] = z[b + 1 + k]; z[r] = 0; zl = r; }
                else { char l = z[zl - 1]; z[zl - 1] = 0; if (ly + lh <= bottom) gtxt(px, ly, z, tx, 1); ly += lh; z[0] = l; z[1] = 0; zl = 1; }
            }
            continue;
        }
        if (ly + lh <= bottom) gtxt(px, ly, z, tx, 1);
        if (ende) { fx = px + gtw(z, 1); fy = ly; } else ly += lh;
        zl = 0; z[0] = 0;
    }
    if (!nz_len[id] && notiz_fok != j) gtxt(px, g->y + 36, TR("Klicken und schreiben ...", "Click and type ..."), 0x00907A1Au, 1);
    if (notiz_fok == j && fy + lh <= bottom + 4) gfx_rect(fx + 1, fy, 2, 16, tx);
    static const char* const bn[3][2] = { { "Besser", "Improve" }, { "Aufgaben", "Tasks" }, { "Weiter", "Continue" } };
    int zb = (g->w - 24) / 3;
    for (int k = 0; k < 3; k++) { int bx = g->x + 12 + k * zb; rrect_rand(bx, g->y + g->h - 34, zb - 6, 26, 8, 0x00FFF9D6u, 0x00D9BE3Cu); const char* t = TR(bn[k][0], bn[k][1]); gtxt(bx + (zb - 6 - gtw(t, 1)) / 2, g->y + g->h - 30, t, tx, 1); }
    gfx_end();
}
static void wid_zeichnen(int j) {
    struct widget* g = &wid[j]; char z[100], a[12];
    if (g->typ == WG_NOTIZ) { notiz_zeichnen(j); return; }
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
static int zustand_fuer(int typ) { return (typ == FT_TAB || typ == FT_PRAES || typ == FT_BOARD || typ == FT_MAL) ? Z_OFF : typ == FT_EDIT ? Z_EDIT : typ == FT_WEB ? Z_WEB : typ == FT_RECH ? Z_RECH : Z_INFO; }   /* Info und Einstellungen teilen sich Z_INFO */
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
    if (lbw) hg(lbx, lby, lbw, lbh);
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
static void antwort_ort(int* cx, int* ay, int* cw) {          /* Platz der Antwortkarte: dort, wo Zynos nicht steht */
    *cx = bar_x; *cw = bar_w; *ay = H / 5 < 70 ? 70 : H / 5;
    int zx0 = zr_x() - 14, zx1 = zr_x() + zr_w() + 14, zy0 = zr_y() - 14, zy1 = zr_y() + zr_h() + 14, hoch = bar_y - *ay - 20;
    if (!(zx0 < *cx + *cw && zx1 > *cx && zy0 < *ay + hoch && zy1 > *ay)) return;
    if (W - 30 - zx1 >= 380) { *cx = zx1 > bar_x ? zx1 : bar_x; *cw = W - 30 - *cx < bar_w ? W - 30 - *cx : bar_w; return; }      /* rechts neben Zynos */
    if (zx0 - 30 >= 380) { *cw = zx0 - 30 < bar_w ? zx0 - 30 : bar_w; *cx = zx0 - *cw; return; }                                     /* links daneben */
    if (bar_y - 30 - zy1 >= 110) { *ay = zy1; return; }                                                                                  /* darunter */
}
static void zeichne_antwort(void) {
    int cw, cx, ay; antwort_ort(&cx, &ay, &cw);
    if (!in_szene && deckt(cx - 10, ay - 6, cw + 20, bar_y - ay - 20)) { zeichne_desktop(); return; }
    gfx_begin();
    hg(cx - 10, ay - 6, cw + 20, bar_y - ay - 20);
    if (nantwort > 0) {
        int hh = 24, mc = (cw - 46) / 8; if (mc < 8) mc = 8; for (int i = 0; i < nantwort; i++) { int l = slen(antwort[i]), n = (l + mc - 1) / mc + (l > mc ? 1 : 0); hh += 22 * (n ? n : 1); }
        glas(cx, ay, cw, hh + 8, 20, C_CARD, 200); gfx_rect(cx + 20, ay + 1, cw - 40, 1, mix(C_CARD, 0x00FFFFFFu, 60));
        rrect(cx + 18, ay + 14, 6, 18, 3, C_ACC);
        int y = ay + 14; cx += 10; cw -= 10;
        for (int i = 0; i < nantwort; i++) { int n = absatz(cx + 18, y, cw - 36, antwort[i], i == 0 ? C_TEXT : C_MUTED); y += 22 * (n ? n : 1); }
    }
    gfx_end();
}
/* ---------- Autovorschlaege (ab dem ersten Buchstaben) ---------- */
typedef struct { const char* de; const char* en; int typ; } sug_e;       /* typ 0 Befehl, 1 Frage an Zynos, 2 Verlauf */
static const sug_e katalog[] = {
    { "text erstellen", "write a text", 0 }, { "tabelle erstellen", "new spreadsheet", 0 }, { "praesentation erstellen", "new presentation", 0 }, { "smartboard", "smartboard", 0 }, { "zeichnen", "draw", 0 }, { "agent bild", "agent image", 0 },
    { "word", "word", 0 }, { "excel", "excel", 0 }, { "powerpoint", "powerpoint", 0 }, { "tabelle", "spreadsheet", 0 }, { "agent dokument", "agent document", 0 }, { "agent tabelle", "agent sheet", 0 }, { "agent folien", "agent slides", 0 }, { "agent ablauf", "agent flow", 0 },
    { "browser", "browser", 0 }, { "rechner", "calculator", 0 }, { "systeminfo", "system info", 0 }, { "einstellungen", "settings", 0 }, { "sprache englisch", "language german", 0 }, { "widget uhr", "widget clock", 0 }, { "widget system", "widget system", 0 }, { "widget aufgaben", "widget tasks", 0 }, { "widget agenten", "widget agents", 0 }, { "alle fenster schliessen", "close all windows", 0 }, { "browser de.wikipedia.org", "browser en.wikipedia.org", 0 }, { "suche ", "search ", 0 }, { "rechne 12*7", "calc 12*7", 0 }, { "system", "system status", 0 }, { "dateien", "files", 0 },
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
    { "merk dir ", "remember ", 0 }, { "vergiss ", "forget ", 0 }, { "was weisst du", "what do you know", 0 }, { "gedaechtnis", "memory", 0 }, { "chats", "chats", 0 },
    { "rechne ", "calc ", 0 }, { "sprache deutsch", "language german", 0 }, { "sprache francais", "language french", 0 }, { "sprache italiano", "language italian", 0 }, { "sprache espanol", "language spanish", 0 },
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
    { static char zc[64]; int n = zlk_ergaenze(q, zc, sizeof(zc), 0); if (n >= 2 && zlk_sicherheit() < 150) { static char zs[96]; scopy(zs, q, 96); cat(zs, zc, 96); sug_add(zs, 4, ql); } }   /* Lernkern */
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
#define NAG 9
static const char* const ag_desc[NAG][2] = {
    { "Prueft Zeit, Speicher, Netzwerk, Dokumente und Aufgaben und fasst alles zusammen.", "Checks time, memory, network, documents and tasks and sums it up." },
    { "Prueft Startkette, Signatur, Benutzerrolle und Netzwerk.", "Checks boot chain, signature, user role and network." },
    { "Prueft den Speicher und schlaegt vor, das Dateisystem zu sichern (Freigabe noetig).", "Checks memory and proposes to sync the file system (needs approval)." },
    { "Liest deine Aufgaben und schlaegt vor, erledigte zu entfernen (Freigabe noetig).", "Reads your tasks and proposes to clear them (needs approval)." },
    { "Liest das Dokument, fasst es zusammen und legt die Zusammenfassung als neues Dokument an (Freigabe).", "Reads the document, summarises it and saves the summary as a new document (approval)." },
    { "Findet Zahlenspalten in der Tabelle und fuegt Summenzeilen ein (Freigabe).", "Finds number columns in the sheet and adds sum rows (approval)." },
    { "Macht aus dem Dokument eine Praesentation (Freigabe).", "Turns the document into a presentation (approval)." },
    { "Liest den Ablauf auf dem SmartBoard und legt die Schritte als Aufgaben an (Freigabe).", "Reads the flow on the SmartBoard and creates the steps as tasks (approval)." },
    { "Zeichnet eine Illustration zum Dokument und speichert sie als Bild (Freigabe).", "Draws an illustration for the document and saves it as an image (approval)." },
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
    } else if (i >= 4) { ag_office(i);
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
        else if (pend >= 3) ag_office_frei(pend);
        else { aufg_leeren(); al(TR("Freigegeben: Aufgaben entfernt.", "Approved: tasks removed.")); }
        kprint("[AGENT] Freigabe erteilt\n");
    } else { al(TR("Abgelehnt: es wurde nichts veraendert.", "Rejected: nothing was changed.")); kprint("[AGENT] Freigabe abgelehnt\n"); }
    pend = 0;
}

/* ---------- Design: Karten ---------- */
enum { K_CHIP0 = 1, K_SUG0 = 8, K_DOT0 = 14, K_PREV = 18, K_NEXT = 19, K_SPEICHERN = 20, K_NEU, K_SCHLIESSEN, K_AG0 = 240, K_FREI = 34, K_ABL, K_NEUDOK, K_AUFLEER };
static const char* const fl_name[4][2] = { { "Zynos", "Zynos" }, { "System", "System" }, { "Agenten", "Agents" }, { "Aufgaben", "Tasks" } };
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
    hg(bar_x - 10, bar_y - 6, bar_w + 20, 215);
    rrect(bar_x - 4, bar_y - 4, bar_w + 8, 60, 30, mix(hg_zeile(bar_y), C_ACC, 70));
    rrect(bar_x - 2, bar_y - 2, bar_w + 4, 56, 28, mix(hg_zeile(bar_y), C_ACC, 150));
    rrect(bar_x, bar_y, bar_w, 52, 26, C_FIELD);
    rrect(bar_x + 14, bar_y + 14, 24, 24, 12, mix(C_FIELD, C_ACC, 90)); rrect(bar_x + 21, bar_y + 21, 10, 10, 5, C_ACC2);
    if (einlen == 0) gtxt(bar_x + 50, bar_y + 18, TR("Frag mich alles - schon der erste Buchstabe zeigt Vorschlaege ...", "Ask me anything - the first letter already shows suggestions ..."), C_MUTED, 1);
    else gtxt(bar_x + 50, bar_y + 18, eingabe, C_TEXT, 1);
    gfx_rect(bar_x + 50 + (einlen ? gtw(eingabe, 1) : 0), bar_y + 15, 2, 22, C_ACC2);
    if (nsug > 0 && !rad_modus()) {
        int y0 = bar_y + 60;
        glas(bar_x, y0, bar_w, nsug * 30 + 12, 16, C_CARD, 225);
        for (int i = 0; i < nsug; i++) {
            int ry = y0 + 6 + i * 30;
            if (i == sugsel) rrect(bar_x + 6, ry, bar_w - 12, 28, 10, mix(C_CARD, C_ACC, 120));
            char a[96]; int pl = sugpre[i] < 95 ? sugpre[i] : 95; for (int j = 0; j < pl; j++) a[j] = sug[i][j]; a[pl] = 0;
            int px = bar_x + 20;
            if (pl) { gtxt(px, ry + 6, a, C_TEXT, 1); px += gtw(a, 1); }
            gtxt(px, ry + 6, sug[i] + pl, i == sugsel ? C_TEXT : C_MUTED, 1);
            const char* tg = sugtyp[i] == 0 ? TR("Befehl", "command") : sugtyp[i] == 1 ? TR("Frage", "question") : sugtyp[i] == 4 ? TR("Lernkern", "learning core") : TR("Verlauf", "history");
            gtxt(bar_x + bar_w - 20 - gtw(tg, 1), ry + 6, tg, sugtyp[i] == 1 ? 0x00A78BFAu : sugtyp[i] == 0 ? C_ACC2 : C_MUTED, 1);
            kn_reg(K_SUG0 + i, bar_x + 6, ry, bar_w - 12, 28);
        }
    } else {
        int x = bar_x;
        for (int i = 0; i < 6; i++) {
            int w = gtw(chip_text[i], 1) + 24;
            glas(x, bar_y + 66, w, 30, 15, C_CARD, 170);
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
    hg(0, 0, W, 40); glas(0, 0, W, 40, 0, C_CARD, 170);
    gfx_rect(0, 39, W, 1, mix(C_BORDER, C_ACC, 60));
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
        if (i == flaeche) rrect(cx, 14, 12, 12, 6, C_ACC2); else rrect(cx + 2, 16, 8, 8, 4, C_OFF);
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
    gtxt(x + 42, y + 116, gehirn_vorhanden() ? TR("Lernkern bereit (laeuft lokal)", "Learning core ready (runs locally)") : TR("kein Sprachmodell geladen", "no language model loaded"), C_TEXT, 1);
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
    kprint("[DESKTOP] Aufgaben-Flaeche gezeichnet\n");
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

/* ---------- Dock: Programme unten in der Mitte (Glas, Symbole, Punkt = offen) ---------- */
enum { K_DOCK0 = 260, NDOCK = 12, K_ORB = 290 };
static const int dock_typ[NDOCK] = { FT_EDIT, FT_TAB, FT_PRAES, FT_BOARD, FT_MAL, FT_WEB, FT_RECH, FT_CHATS, FT_GED, FT_EINST, FT_DATEI, FT_BENUTZER };
static const uint32_t dock_f[NDOCK][2] = { { 0x004F86E8u, 0x002B579Au }, { 0x0033A86Bu, 0x00217346u }, { 0x00F0703Cu, 0x00C43E1Cu }, { 0x009B4BE0u, 0x007719AAu }, { 0x00FACC15u, 0x00EA580Cu }, { 0x0038BDF8u, 0x000284C7u },
                                          { 0x0094A3B8u, 0x00475569u }, { 0x002DD4BFu, 0x000F9488u }, { 0x00F472B6u, 0x00BE185Du }, { 0x0094A3B8u, 0x0064748Bu }, { 0x00FBBF24u, 0x00D97706u }, { 0x00A78BFAu, 0x006D28D9u } };
static const char* const dock_n[NDOCK][2] = { { "Text", "Text" }, { "Tabelle", "Sheets" }, { "Praesentation", "Slides" }, { "SmartBoard", "SmartBoard" }, { "Zeichnen", "Draw" }, { "Browser", "Browser" },
                                              { "Rechner", "Calculator" }, { "Chats", "Chats" }, { "Gedaechtnis", "Memory" }, { "Einstellungen", "Settings" }, { "Dateien", "Files" }, { "Benutzer", "Users" } };
#include "fluidic.inc"
static void dock_symbol(int i, int x, int y, int s) {
    if (Q_GLAS && s >= 24) { fl_icon(i, x, y, s, 0); return; }
    if (i == 4) {                                                       /* Zeichnen: Palette mit Farbtupfern */
        rrect(x, y, s, s, s / 4, dock_f[4][1]); rrect(x, y, s, s * 3 / 5, s / 4, dock_f[4][0]); gfx_rect(x, y + s * 2 / 5, s, s / 5, mix(dock_f[4][0], dock_f[4][1], 128));
        int u = s / 12; rrect(x + 2 * u, y + 2 * u, 8 * u, 8 * u, 4 * u, 0x00FFFFFFu); rrect(x + 6 * u, y + 6 * u, 3 * u, 3 * u, 3 * u / 2, dock_f[4][1]);
        rrect(x + 3 * u, y + 4 * u, 2 * u, 2 * u, u, 0x00EF4444u); rrect(x + 5 * u, y + 3 * u, 2 * u, 2 * u, u, 0x003B82F6u); rrect(x + 3 * u, y + 7 * u, 2 * u, 2 * u, u, 0x0022C55Eu); rrect(x + 7 * u, y + 4 * u, 2 * u, 2 * u, u, 0x00EAB308u);
        return;
    }
    if (i == 11) {                                                      /* Benutzer: Person */
        if (Q_GLAS && s > 24) { rrect(x, y, s, s, s / 4, mix(dock_f[11][0], dock_f[11][1], 128)); rr_grad(x + 2, y + 2, s - 4, s - 4, s / 4 - 2, mix(0x000E2038u, dock_f[11][0], 90), mix(0x00050B1Cu, dock_f[11][1], 110)); }
        else { rrect(x, y, s, s, s / 4, dock_f[11][1]); rrect(x, y, s, s * 3 / 5, s / 4, dock_f[11][0]); }
        int u = s / 12; rrect(x + 4 * u, y + 2 * u + u / 2, 4 * u, 4 * u, 2 * u, 0x00FFFFFFu); rrect(x + 2 * u + u / 2, y + 7 * u, 7 * u, 4 * u, 2 * u, 0x00FFFFFFu);
        return;
    }
    if (i == 10) {                                                      /* Dateien: Ordner */
        if (Q_GLAS && s > 24) { rrect(x, y, s, s, s / 4, mix(dock_f[10][0], dock_f[10][1], 128)); rr_grad(x + 2, y + 2, s - 4, s - 4, s / 4 - 2, mix(0x000E2038u, dock_f[10][0], 90), mix(0x00050B1Cu, dock_f[10][1], 110)); }
        else { rrect(x, y, s, s, s / 4, dock_f[10][1]); rrect(x, y, s, s * 3 / 5, s / 4, dock_f[10][0]); }
        int u = s / 12; rrect(x + 2 * u, y + 3 * u, 4 * u, 2 * u, u / 2, 0x00FFFFFFu); rrect(x + 2 * u, y + 4 * u, 8 * u, 5 * u, u, 0x00FFFFFFu); gfx_rect(x + 3 * u, y + 6 * u, 6 * u, 1 + u / 3, dock_f[10][1]);
        return;
    }
    if (i > 4) i--;
    { int fi = i >= 4 ? i + 1 : i;
      if (Q_GLAS && s > 24) { rrect(x, y, s, s, s / 4, mix(dock_f[fi][0], dock_f[fi][1], 128)); rr_grad(x + 2, y + 2, s - 4, s - 4, s / 4 - 2, mix(0x000E2038u, dock_f[fi][0], 90), mix(0x00050B1Cu, dock_f[fi][1], 110)); gfx_rect(x + s / 5, y + 3, s * 3 / 5, 1, mix(0x00FFFFFFu, dock_f[fi][0], 90)); }
      else { rrect(x, y, s, s, s / 4, dock_f[fi][1]);
    rrect(x, y, s, s * 3 / 5, s / 4, dock_f[fi][0]); gfx_rect(x, y + s * 2 / 5, s, s / 5, mix(dock_f[fi][0], dock_f[fi][1], 128)); } }
    uint32_t w = 0x00FFFFFFu; int c = x + s / 2, m = y + s / 2, u = s / 12;
    if (i == 0) { for (int k = 0; k < 4; k++) rrect(x + 3 * u, y + 3 * u + k * 2 * u + u / 2, k == 3 ? 4 * u : 6 * u, u, u / 2, w); }
    else if (i == 1) { rrect(x + 3 * u, y + 3 * u, 6 * u, 6 * u, u, w); for (int k = 1; k < 3; k++) { gfx_rect(x + 3 * u + k * 2 * u, y + 3 * u, 1 + u / 3, 6 * u, dock_f[i >= 4 ? i + 1 : i][1]); gfx_rect(x + 3 * u, y + 3 * u + k * 2 * u, 6 * u, 1 + u / 3, dock_f[i >= 4 ? i + 1 : i][1]); } }
    else if (i == 2) { rrect(x + 2 * u, y + 3 * u, 8 * u, 5 * u, u, w); gfx_rect(c - u / 2, y + 8 * u, u, 2 * u, w); rrect(x + 3 * u + u / 2, y + 4 * u + u / 2, 3 * u, u, u / 2, dock_f[i >= 4 ? i + 1 : i][1]); }
    else if (i == 3) { rrect(x + 2 * u, y + 3 * u, 3 * u, 3 * u, u / 2, w); rrect(x + 7 * u, y + 6 * u, 3 * u, 3 * u, u / 2, w); for (int k = 0; k <= 3 * u; k++) gfx_rect(x + 4 * u + k, y + 5 * u + k * 2 / 3, 2, 2, w); }
    else if (i == 4) { rrect(c - 3 * u, m - 3 * u, 6 * u, 6 * u, 3 * u, w); rrect(c - 2 * u, m - 2 * u, 4 * u, 4 * u, 2 * u, dock_f[i >= 4 ? i + 1 : i][1]); gfx_rect(c - 3 * u, m - u / 3, 6 * u, 1 + u / 2, w); gfx_rect(c - u / 3, m - 3 * u, 1 + u / 2, 6 * u, w); }
    else if (i == 5) { for (int k = 0; k < 4; k++) rrect(x + 3 * u + (k % 2) * 3 * u + u / 2, y + 3 * u + (k / 2) * 3 * u + u / 2, 2 * u, 2 * u, u / 2, k == 3 ? 0x00FBBF24u : w); }
    else if (i == 6) { rrect(x + 2 * u, y + 3 * u, 8 * u, 5 * u, 2 * u, w); for (int k = 0; k < u * 2; k++) gfx_rect(x + 4 * u - k / 2, y + 8 * u + k / 2, u, 1, w); }
    else if (i == 7) { int px[4] = { 3, 8, 5, 9 }, py[4] = { 4, 3, 8, 8 }; for (int a = 0; a < 3; a++) for (int k = 0; k <= 8; k++) gfx_rect(x + (px[a] * u * (8 - k) + px[a + 1] * u * k) / 8, y + (py[a] * u * (8 - k) + py[a + 1] * u * k) / 8, 2, 2, w);
                       for (int a = 0; a < 4; a++) rrect(x + px[a] * u - u, y + py[a] * u - u, 2 * u, 2 * u, u, w); }
    else { rrect(c - 3 * u, m - 3 * u, 6 * u, 6 * u, 3 * u, w); for (int k = 0; k < 8; k++) rrect(c + 3 * u * icos(k * 45) / 1024 - u, m + 3 * u * isin(k * 45) / 1024 - u, 2 * u, 2 * u, u / 2, w); rrect(c - 3 * u / 2, m - 3 * u / 2, 3 * u, 3 * u, 3 * u / 2, dock_f[i >= 4 ? i + 1 : i][1]); }
}
/* ---------- Orbit: eine leuchtende Kugel; Klick oder Strg+Leertaste faechert die Programme als Halbkreis-Rad auf ---------- */
static int orb_auf, orb_sel = -1;
static void dock_oeffnen(int i);
static int orb_cx(void) { return W / 2; }
static int orb_cy(void) { return H - 50; }
#define ORB_R 200
#define ORB_W 640
#define ORB_H 330
static uint32_t orb_puffer[ORB_W * ORB_H]; static int orb_gesichert, orb_px, orb_py, orb_pw, orb_ph;
static void orb_kugel(int cx, int cy, int r, int auf) {                     /* Kugel mit Verlauf und Leuchtring */
    uint32_t a = C_ACC, b = 0x009D4EDDu;
    for (int k = 4; k >= 1; k--) rrect(cx - r - 3 * k, cy - r - 3 * k, 2 * (r + 3 * k), 2 * (r + 3 * k), r + 3 * k, mix(hg_zeile(cy), a, 34 / k + 6));
    for (int k = 0; k < r; k += 2) { int rr = r - k; rrect(cx - rr, cy - rr + k / 3, 2 * rr, 2 * rr - k / 2, rr, mix(b, a, k * 256 / r)); }
    rrect(cx - r / 2, cy - r + 4, r, r / 2, r / 4, mix(a, 0x00FFFFFFu, 110));                  /* Glanzlicht */
    if (auf) { for (int k = -6; k <= 6; k++) { gfx_rect(cx + k - 1, cy + k - 1, 3, 3, 0x00FFFFFFu); gfx_rect(cx + k - 1, cy - k - 1, 3, 3, 0x00FFFFFFu); } }
    else for (int k = 0; k < 3; k++) for (int j = 0; j < 3; j++) rrect(cx - 9 + k * 7, cy - 9 + j * 7, 5, 5, 2, 0x00FFFFFFu);
}
static void orb_ort(int i, int prozent, int* x, int* y) {                   /* Position von Programm i auf dem Halbkreis */
    int deg = 180 - i * 180 / (NDOCK - 1), r = ORB_R * prozent / 100;
    *x = orb_cx() + r * icos(deg) / 1024; *y = orb_cy() - 18 - r * isin(deg) / 1024;
}
static void zeichne_dock(void) {                                            /* zugeklappt: nur die Kugel, darunter Punkte fuer offene Programme */
    int cx = orb_cx(), cy = orb_cy();
    if (Q_GLAS) fl_hub(cx, cy, 28, 0); else orb_kugel(cx, cy, 24, 0);
    kn_reg(K_ORB, cx - 30, cy - 30, 60, 60);
    int n = 0; for (int i = 0; i < NDOCK; i++) if (fenster_idx(dock_typ[i]) >= 0) n++;
    int x = cx - (n * 14) / 2 + 3; for (int i = 0; i < NDOCK; i++) if (fenster_idx(dock_typ[i]) >= 0) { rrect(x, cy + 34, 8, 8, 4, dock_f[i][0]); x += 14; }
}
static void orb_sichern(void) {
    orb_pw = ORB_W; orb_ph = ORB_H; orb_px = orb_cx() - ORB_W / 2; orb_py = H - ORB_H; if (orb_px < 0) orb_px = 0; if (orb_px + orb_pw > W) orb_pw = W - orb_px;
    uint32_t pp; uint32_t* f = gfx_fbptr(&pp); if (!f) { orb_gesichert = 0; return; }
    for (int y = 0; y < orb_ph; y++) for (int x = 0; x < orb_pw; x++) orb_puffer[y * ORB_W + x] = f[(uint64_t)(orb_py + y) * pp + orb_px + x];
    orb_gesichert = 1;
}
static void orb_zurueck(void) {
    uint32_t pp; uint32_t* f = gfx_fbptr(&pp); if (!f || !orb_gesichert) return;
    for (int y = 0; y < orb_ph; y++) for (int x = 0; x < orb_pw; x++) f[(uint64_t)(orb_py + y) * pp + orb_px + x] = orb_puffer[y * ORB_W + x];
}
static const char* const dock_intent[NDOCK] = { "office/writer", "office/grid", "office/deck", "office/smartboard", "creative/draw", "net/browser", "tools/calculator", "ai/chat", "ai/memory", "system/settings", "system/files", "system/users" };
static void zeichne_orbit(int prozent) {                                    /* aufgeklappt: Glas-Halbring, Programme, Name in der Mitte */
    int cx = orb_cx(), cy = orb_cy() - 18;
    gfx_begin(); orb_zurueck();
    if (Q_GLAS) {
        unsigned of = 0; for (int i = 0; i < NDOCK; i++) if (fenster_idx(dock_typ[i]) >= 0) of |= 1u << i;
        fl_rad(cx, cy, ORB_R, prozent, NDOCK, orb_sel, of, orb_sel >= 0 ? TR(dock_n[orb_sel][0], dock_n[orb_sel][1]) : "Zynex", orb_sel >= 0 ? dock_intent[orb_sel] : "", 0);
        if (prozent >= 100) { const char* h = orb_sel >= 0 ? TR("Klicken oder Enter", "Click or Enter") : TR("Programm waehlen", "Choose an app"); gtxt(cx - gtw(h, 1) / 2, cy - 76, h, C_MUTED, 1); }
        gfx_end(); return;
    }
    int ra = (ORB_R + 50) * prozent / 100, ri = (ORB_R - 50) * prozent / 100;
    for (int y = cy - ra; y <= cy + 10; y++) {                              /* Glas-Halbring */
        int dy = cy - y; if (dy < 0) dy = 0; int xa = isq(ra * ra - dy * dy), xi = dy < ri ? isq(ri * ri - dy * dy) : 0;
        for (int x = cx - xa; x < cx - xi; x++) gfx_blend(x, y, C_CARD, 185);
        for (int x = cx + xi; x < cx + xa; x++) gfx_blend(x, y, C_CARD, 185);
    }
    for (int d = 0; d <= 180; d += 1) { int x = cx + ra * icos(d) / 1024, y = cy - ra * isin(d) / 1024; gfx_blend(x, y, mix(C_CARD, 0x00FFFFFFu, 90), 200); }
    for (int i = 0; i < NDOCK; i++) {
        int x, y; orb_ort(i, prozent, &x, &y); int s = i == orb_sel ? 60 : 48;
        if (i == orb_sel) rrect(x - s / 2 - 6, y - s / 2 - 6, s + 12, s + 12, s / 2 + 6, mix(C_CARD, dock_f[i][0], 150));
        dock_symbol(i, x - s / 2, y - s / 2, s);
        if (fenster_idx(dock_typ[i]) >= 0) rrect(x - 3, y + s / 2 + 5, 6, 6, 3, C_ACC2);
    }
    if (prozent >= 100) {
        const char* t = orb_sel >= 0 ? TR(dock_n[orb_sel][0], dock_n[orb_sel][1]) : "Zynex";
        gtxt(cx - gtw(t, 2) / 2, cy - 92, t, C_TEXT, 2);
        const char* h = orb_sel >= 0 ? TR("Klicken oder Enter", "Click or Enter") : TR("Programm waehlen", "Choose an app");
        gtxt(cx - gtw(h, 1) / 2, cy - 52, h, C_MUTED, 1);
    }
    orb_kugel(orb_cx(), orb_cy(), 24, 1);
    gfx_end();
}
static void orb_oeffnen(void) {
    if (orb_auf || flaeche != 0 || zustand == Z_LOGIN) return;
    orb_auf = 1; orb_sel = -1;
    gfx_begin(); orb_sichern(); gfx_end();
    for (int p = 20; p <= 100; p += 20) { uint32_t t0 = timer_get_ticks(); zeichne_orbit(p); for (uint32_t n = 0; n < 300000 && timer_get_ticks() - t0 < 2; n++) __asm__ volatile ("pause"); }
    kprint("[DESKTOP] Orbit geoeffnet\n");
}
static void orb_schliessen(void) { if (!orb_auf) return; orb_auf = 0; orb_sel = -1; gfx_begin(); orb_zurueck(); gfx_end(); zeichne_desktop(); }
static int orb_treffer(int x, int y) {
    for (int i = 0; i < NDOCK; i++) { int ox, oy; orb_ort(i, 100, &ox, &oy); if ((x - ox) * (x - ox) + (y - oy) * (y - oy) <= 34 * 34) return i; }
    return -1;
}

static void zeichne_desktop(void) {
    if (ob_vollbild) { praes_vollbild_zeichnen(); return; }
    desk_gen++;
    thema_anwenden(th_nr);
    gfx_begin(); hg(0, 0, W, H); gfx_end();
    nkn = 0; lbw = 0; in_szene = 1; zeichne_leiste();
    if (flaeche == 0) { zeichne_robot(); zeichne_antwort(); zeichne_eingabe(); zeichne_dock(); szene_oben(); }
    if (orb_auf && flaeche == 0) { in_szene = 0; gfx_begin(); orb_sichern(); gfx_end(); zeichne_orbit(100); }
    else if (flaeche != 0) { gfx_begin(); if (flaeche == 1) zeichne_f_system(); else if (flaeche == 2) zeichne_f_agenten(); else zeichne_f_aufgaben(); gfx_end(); }
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
        for (int y = 0; y < H; y++) { uint32_t* r = f + (uint64_t)y * pp; for (int x = 0; x < W; x++) r[x] = th_hg1; }
        wuerfel_flaeche(snapA, deg, dir, 0); wuerfel_flaeche(snapB, deg, dir, 1);
        for (uint32_t n = 0; n < 400000 && timer_get_ticks() - t0 < 2; n++) __asm__ volatile ("pause");   /* begrenzt: tickt der Timer nicht (z. B. Hyper-V ohne PIT), darf die Drehung nicht haengen */
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
static void widget_weg(int j) {
    if (j < 0 || j >= nwid) return;
    if (wid[j].typ == WG_NOTIZ) { int id = wid[j].id; nz_used[id] = 0; nz_len[id] = 0; nz[id][0] = 0; notiz_speichern(); }
    if (notiz_fok == j) notiz_fok = -1; else if (notiz_fok > j) notiz_fok--;
    for (int q = j; q < nwid - 1; q++) wid[q] = wid[q + 1];
    nwid--; layout_speichern();
}
static void widget_neu(int typ) {
    if (flaeche != 0) flaeche_wechsel(0);
    if (nwid >= NWID_MAX) { reply1(TR("Es sind schon 10 Widgets da.", "10 widgets are already there.")); zeichne_antwort(); return; }
    struct widget* g = &wid[nwid]; g->typ = typ; g->id = 0; wid_groesse(typ, &g->w, &g->h);
    if (typ == WG_NOTIZ) {
        int f = -1; for (int i = 0; i < NZ_N; i++) if (!nz_used[i]) { f = i; break; }
        if (f < 0) { reply1(TR("Es gibt schon 8 Notizzettel.", "There are already 8 sticky notes.")); zeichne_antwort(); return; }
        g->id = f; nz_used[f] = 1; nz_len[f] = 0; nz[f][0] = 0;
    }
    g->x = W - g->w - 30; g->y = 56; for (int j = 0; j < nwid; j++) g->y += wid[j].h + 14;
    if (g->y + g->h > H - 10) { g->x -= 280; g->y = 56; }
    nwid++; layout_speichern(); if (typ == WG_NOTIZ) { notiz_fok = nwid - 1; nz_meld[0] = 0; notiz_speichern(); } zeichne_desktop();
    kprint("[DESKTOP] Widget hinzugefuegt: "); kprint_dec((uint32_t)typ); kprint("\n");
}
static void rech_zuruecksetzen(void);
static void fenster_oeffnen(int typ) {
    int i = fenster_idx(typ);
    if (flaeche != 0) flaeche_wechsel(0);
    if (i < 0) {
        if (nfen >= 6) { reply1(TR("Es sind schon 6 Fenster offen.", "6 windows are already open.")); return; }
        int w = typ == FT_EDIT ? (W - 80 < 760 ? W - 80 : 760) : typ == FT_WEB ? W * 3 / 4 : typ == FT_RECH ? 300 : typ == FT_EINST ? 660 : typ == FT_CHATS ? 720 : typ == FT_GED ? 840 : 480;
        int h = typ == FT_EDIT ? (H - 140 < 560 ? H - 140 : 560) : typ == FT_WEB ? H * 3 / 4 : typ == FT_RECH ? 372 : typ == FT_EINST ? 600 : typ == FT_CHATS ? 470 : typ == FT_GED ? 580 : 250;
        if (typ == FT_DATEI) { w = 1000; h = 580; }
        if (typ == FT_BENUTZER) { w = 760; h = 600; }
        if (typ == FT_WEB && w < 640) w = W - 40;
        if (typ == FT_EDIT || typ == FT_TAB || typ == FT_PRAES || typ == FT_BOARD || typ == FT_WEB || typ == FT_MAL) { w = W - 60 < 1500 ? W - 60 : 1500; h = H - 70 < 900 ? H - 70 : 900; }
        int x = 60 + nfen * 36, y = 56 + nfen * 30;
        if (x + w > W - 8) x = W - w - 8; if (x < 4) x = 4; if (y + h > H - 8) y = H - h - 8; if (y < 44) y = 44;
        {   /* nicht unter Zynos oeffnen: wenn das Fenster den Roboter verdeckt, rechts neben ihn schieben (sonst unter ihn) */
            int rx0 = zr_x() - 8, ry0 = zr_y() - 8, rx1 = zr_x() + zr_w() + 8, ry1 = zr_y() + zr_h() + 8;
            if (x < rx1 && x + w > rx0 && y < ry1 && y + h > ry0) {
                if (rx1 + w <= W - 8) x = rx1;
                else if (ry1 + h <= H - 8) y = ry1;
            }
        }
        fen[nfen].typ = typ; fen[nfen].x = x; fen[nfen].y = y; fen[nfen].w = w; fen[nfen].h = h; nfen++;
        if (typ == FT_RECH) rech_zuruecksetzen();
        office_init(typ);
    } else if (i != nfen - 1) fenster_hoch(i);
    fok = 1; zustand = zustand_fuer(typ); nsug = 0; sugsel = -1;
    zeichne_desktop();
    kprint("[DESKTOP] Fenster geoeffnet: "); kprint(fenster_titel(typ)); kprint("\n");
}

/* ---------- Textverarbeitung ---------- */
static int ed_zeilen(int* start, int max) {                 /* Zeilenanfaenge (Wortumbruch nach Pixelbreite ed_pxw); liefert Anzahl; fuellt ed_za/ed_zh */
    int n = 0, i = 0; if (max <= 0) return 0;
    for (;;) {
        start[n] = i;
        int x = 0, lastsp = -1, j = i, ende, next = -1;
        while (j < doklen) {
            unsigned char c = (unsigned char)dok[j];
            if (c == '\n') { next = j + 1; break; }
            int w = st_adv(c, dokst[j]);
            if (x + w > ed_pxw && j > i) { if (lastsp >= i) next = lastsp + 1; else next = j; break; }
            x += w; if (c == ' ') lastsp = j;
            j++;
        }
        ende = j; if (next >= 0 && dok[ende] != '\n') ende = next;
        int asc = 0, desc = 0;
        for (int k = i; k < ende; k++) { if (dok[k] == '\n') continue; int a = st_asc(dokst[k]), d = st_desc(dokst[k]); if (a > asc) asc = a; if (d > desc) desc = d; }
        if (!asc) { uint8_t s0 = (i == dokcur) ? dok_cst : (uint8_t)ST_STD; asc = st_asc(s0); desc = st_desc(s0); }
        ed_za[n] = asc; ed_zh[n] = asc + desc + 4;
        n++;
        if (next < 0 || n >= max) break;
        i = next;
    }
    return n;
}
static int ed_start[ED_ZMAX];
static void ob_word(int voll);
static void zeichne_editor(int voll) { ob_word(voll); }
static void dok_fmtname(char* out) { scopy(out, dokname, 24); int n = slen(out); if (n > 4 && out[n - 4] == '.') { out[n - 3] = 'f'; out[n - 2] = 'm'; out[n - 1] = 't'; } }
static void dok_speichern(void) {
    if (!ra_darf(dokname, ST_MITARB)) { reply1(TR("Kein Schluessel zum Speichern in diesem Raum.", "No key to save in this room.")); return; }
    synfs_create(dokname, SYNFS_FILE);
    int r = synfs_write(dokname, dok, (uint32_t)doklen);
    { static uint8_t rl[2 * DOK_MAX + 4]; int o = 0, i = 0;                              /* Formatdatei: Lauflaengen (Anzahl, Stil) */
      while (i < doklen) { int k = i; while (k < doklen && k - i < 255 && dokst[k] == dokst[i]) k++; rl[o++] = (uint8_t)(k - i); rl[o++] = dokst[i]; i = k; }
      char fn[24]; dok_fmtname(fn); synfs_create(fn, SYNFS_FILE); synfs_write(fn, rl, (uint32_t)o); }
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
    dok_leeren(); dokscroll = 0; dokgeaendert = 0;
}
static void editor_oeffnen(void) { if (fenster_idx(FT_EDIT) < 0) dok_neu(); fenster_oeffnen(FT_EDIT); }
static void editor_schliessen(void) {
    if (doklen > 0 && dokgeaendert) dok_speichern();
    fenster_schliessen_typ(FT_EDIT);
}
static int ed_zeile_von(int n, int pos) { int cl = 0; for (int i = 0; i < n; i++) if (ed_start[i] <= pos) cl = i; return cl; }
static int ed_x_von(int zl, int pos) { int x = 0; for (int k = ed_start[zl]; k < pos && k < doklen; k++) if (dok[k] != '\n') x += st_adv((unsigned char)dok[k], dokst[k]); return x; }
static int ed_pos_x(int n, int zl, int xo) {                 /* Zeichenposition in Zeile zl bei Pixelabstand xo vom Zeilenanfang */
    int a = ed_start[zl], b = zl + 1 < n ? ed_start[zl + 1] : doklen; if (b > a && b <= doklen && dok[b - 1] == '\n') b--;
    int x = 0; for (int k = a; k < b; k++) { int w = st_adv((unsigned char)dok[k], dokst[k]); if (xo < x + w / 2) return k; x += w; }
    return b;
}
static void ed_alles_waehlen(void) { doksel = 0; dokcur = doklen; ed_stil_sync(); }
static int ed_strg(uint8_t k) {                                /* Strg+B / I / U / A im Text */
    if (k == 0x05) ed_stil_toggle(ST_FETT); else if (k == 0x0C) ed_stil_toggle(ST_KURSIV); else if (k == 0x18) ed_stil_toggle(ST_UNTER);
    else if (k == 0x04) ed_alles_waehlen(); else return 0;
    zeichne_editor(1); return 1;
}
static void editor_taste(uint8_t k, int shift, int altgr) {
    if (k == 0x29) { if (doksel >= 0) { doksel = -1; zeichne_editor(0); return; } editor_schliessen(); return; }
    int n = ed_zeilen(ed_start, ED_ZMAX), cl = ed_zeile_von(n, dokcur);
    int bewegt = (k == 0x50 || k == 0x4F || k == 0x52 || k == 0x51 || k == 0x4A || k == 0x4D);
    if (bewegt && shift && doksel < 0) doksel = dokcur;
    if (bewegt && !shift) doksel = -1;
    if (k == 0x2A) { if (doksel >= 0 && doksel != dokcur) dok_sel_loeschen(); else if (dokcur > 0) dok_loesche(dokcur - 1, dokcur); doksel = -1; ed_stil_sync(); }
    else if (k == 0x4C) { if (doksel >= 0 && doksel != dokcur) dok_sel_loeschen(); else if (dokcur < doklen) dok_loesche(dokcur, dokcur + 1); doksel = -1; ed_stil_sync(); }
    else if (k == 0x50) { if (dokcur > 0) dokcur--; ed_stil_sync(); }
    else if (k == 0x4F) { if (dokcur < doklen) dokcur++; ed_stil_sync(); }
    else if (k == 0x52) { if (cl > 0) dokcur = ed_pos_x(n, cl - 1, ed_x_von(cl, dokcur)); ed_stil_sync(); }
    else if (k == 0x51) { if (cl + 1 < n) dokcur = ed_pos_x(n, cl + 1, ed_x_von(cl, dokcur)); ed_stil_sync(); }
    else if (k == 0x4A) { dokcur = ed_start[cl]; ed_stil_sync(); }
    else if (k == 0x4D) { dokcur = cl + 1 < n ? ed_start[cl + 1] - (dok[ed_start[cl + 1] - 1] == '\n' ? 1 : 0) : doklen; ed_stil_sync(); }
    else {
        char c = (k == 0x28 || k == 0x58) ? '\n' : usb_key_char(k, shift, altgr);
        if ((c == '\n' || (c >= 32 && c < 127) || (unsigned char)c >= 0xA0) && doklen < DOK_MAX) {
            dok_sel_loeschen();
            dok_einf(dokcur, (unsigned char)c, dok_cst); dokcur++;
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

#include "zynjava.h"
#include "browser_ui.inc"

/* ---------- Rechtsklick-Menue ---------- */
enum { M_NEUDOK = 30, M_BROWSER, M_RECHNER, M_INFO, M_EINST, M_CHATS, M_GED, M_DATEI, M_BENUTZER, M_WUHR, M_WSYS, M_WAUF, M_WAGENT, M_WNOTIZ, M_WSCHL, M_FSCHL, M_AUSSCHN = 1, M_KOPIEREN, M_EINFUEGEN, M_ALLES, M_DRUCKEN, M_ZURUECK, M_VOR, M_NEULADEN, M_LINKOEFFNEN, M_LINKKOPIEREN, M_LESEZEICHEN, M_SEITEKOPIEREN, M_SEITESPEICHERN, M_SPEICHERN, M_SCHLIESSEN, M_NAECHSTE, M_VORIGE, M_LEEREN, M_BILDHG = 60 };
static int menu_bild = -1;
static struct { int offen, x, y, w, n, hover, ziel; struct { int id; const char* de; const char* en; } p[16]; } menu;
static int menu_link, menu_wid;
static void menu_add(int id, const char* de, const char* en) { if (menu.n < 16) { menu.p[menu.n].id = id; menu.p[menu.n].de = de; menu.p[menu.n].en = en; menu.n++; } }
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
            menu_link = web_link_bei(x, y); menu_bild = web_bild_bei(x, y);
            if (menu_bild >= 0 && wd_bild[menu_bild].status == 1) menu_add(M_BILDHG, "Bild als Hintergrund", "Set image as background");
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
        menu_add(M_WUHR, "Widget: Uhr", "Widget: clock"); menu_add(M_WSYS, "Widget: System", "Widget: system"); menu_add(M_WAUF, "Widget: Aufgaben", "Widget: tasks"); menu_add(M_WAGENT, "Widget: Agenten", "Widget: agents"); menu_add(M_WNOTIZ, "Notizzettel", "Sticky note");
        menu_add(M_NAECHSTE, "Naechste Arbeitsflaeche", "Next workspace"); menu_add(M_CHATS, "Chats", "Chats"); menu_add(M_GED, "Gedaechtnis", "Memory"); menu_add(M_DATEI, "Dateien", "Files"); menu_add(M_BENUTZER, "Benutzer", "Users"); menu_add(M_EINST, "Einstellungen", "Settings");
    } else {
        menu_add(M_NAECHSTE, "Naechste Arbeitsflaeche", "Next workspace"); menu_add(M_VORIGE, "Vorherige Arbeitsflaeche", "Previous workspace");
    }
    menu.w = 250; menu.x = x + menu.w > W ? W - menu.w - 4 : x; menu.y = y + menu.n * 30 + 12 > H ? H - menu.n * 30 - 16 : y; menu.offen = 1;
    zeichne_menu();
}
static void ed_einfuegen_text(const char* t, int n) {
    if (doksel >= 0) dok_sel_loeschen();
    int zstart = 1, kopf = 0;                                   /* "# Titel"-Zeilen (KI-Text, alte Dokumente) werden zur Ueberschrift */
    for (int i = 0; i < n && doklen < DOK_MAX; i++) {
        unsigned char c = (unsigned char)t[i]; if (c == '\r') continue;
        if (!(c == '\n' || (c >= 32 && c < 127) || c >= 0xA0)) continue;
        if (zstart && c == '#') { kopf = 1; while (i + 1 < n && (t[i + 1] == '#' || t[i + 1] == ' ')) i++; zstart = 0; continue; }
        zstart = 0;
        uint8_t st = kopf ? (uint8_t)(ST_FETT | (3 << 4)) : dok_cst;
        dok_einf(dokcur, c, st); dokcur++;
        if (c == '\n') { zstart = 1; kopf = 0; }
    }
    dok[doklen] = 0; dokgeaendert = 1;
}
/* Aktionen fuer das aktive Eingabeziel: Chat-Leiste, Adressleiste oder Dokument */
static void ziel_aktion(int id) {
    char* b = zustand == Z_WEB ? adr : eingabe; int* n = zustand == Z_WEB ? &adrlen : &einlen; int mx = zustand == Z_WEB ? 190 : 80;
    if (zustand == Z_EDIT) {
        if (id == M_KOPIEREN || id == M_AUSSCHN) { { int sa, sb; if (ed_selbereich(&sa, &sb)) clip_setze(dok + sa, sb - sa); else clip_setze(dok, doklen); } if (id == M_AUSSCHN) { int sa, sb; if (ed_selbereich(&sa, &sb)) { dok_sel_loeschen(); } else dok_leeren(); } scopy(meldung, TR("Dokument kopiert.", "Document copied."), 100); }
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
    case M_BILDHG: web_bild_hintergrund(menu_bild); redraw_alles(); return;
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
    case M_DATEI: fenster_oeffnen(FT_DATEI); return;
    case M_BENUTZER: fenster_oeffnen(FT_BENUTZER); return;
    case M_WUHR: widget_neu(WG_UHR); return; case M_WSYS: widget_neu(WG_SYS); return; case M_WAUF: widget_neu(WG_AUF); return; case M_WAGENT: widget_neu(WG_AGENT); return; case M_WNOTIZ: widget_neu(WG_NOTIZ); return;
    case M_WSCHL: widget_weg(menu_wid); break;
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
    const char* s0 = s;
    for (; *s; s++) { if (*s >= '0' && *s <= '9') ziffer = 1; if (*s == '+' || *s == '*' || *s == '/' || *s == '-' || *s == 'x') op = 1; }
    if (ziffer && !op) { static const char* const w[] = { " durch ", " mal ", " plus ", " minus ", " geteilt ", " times ", " divided by ", " fois ", " moins ", " diviso ", " meno ", " por ", " menos ", 0 }; for (int i = 0; w[i]; i++) if (enthaelt(s0, w[i])) { op = 1; break; } }
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
    if (!enthaelt(s, "agent")) {
        if (enthaelt(s, "tabelle") || enthaelt(s, "excel") || enthaelt(s, "spreadsheet") || enthaelt(s, "kalkulation")) { in_par = 40; return I_FENSTER; }
        if (enthaelt(s, "praesentation") || enthaelt(s, "powerpoint") || enthaelt(s, "folien") || enthaelt(s, "presentation") || enthaelt(s, "slides")) { in_par = 41; return I_FENSTER; }
        if (enthaelt(s, "smartboard") || enthaelt(s, "whiteboard") || enthaelt(s, "zeichenflaeche") || enthaelt(s, "flussdiagramm") || enthaelt(s, "diagramm zeichnen")) { in_par = 42; return I_FENSTER; }
        if ((s[0] == 'w' && s[1] == 'o' && s[2] == 'r' && s[3] == 'd' && !s[4]) || enthaelt(s, "textverarbeitung")) return I_TEXT;
        if (enthaelt(s, "zeichnen") || enthaelt(s, "zeichenprogramm") || enthaelt(s, "malen") || enthaelt(s, "paint") || (s[0] == 'd' && s[1] == 'r' && s[2] == 'a' && s[3] == 'w' && !s[4])) { in_par = 43; return I_FENSTER; }
    }
    if (enthaelt(s, "rechner") || enthaelt(s, "calculator")) { in_par = 1; return I_FENSTER; }
    if (enthaelt(s, "systeminfo") || enthaelt(s, "system-info") || enthaelt(s, "system info") || enthaelt(s, "fenster info")) { in_par = 2; return I_FENSTER; }
    if (enthaelt(s, "arbeitsfl") || enthaelt(s, "workspace") || enthaelt(s, "wuerfel") || enthaelt(s, "cube")) {
        in_par = -1;
        if (enthaelt(s, "1") || enthaelt(s, "zynos") || enthaelt(s, "zynie")) in_par = 0; else if (enthaelt(s, "2") || enthaelt(s, "system")) in_par = 1;
        else if (enthaelt(s, "3") || enthaelt(s, "agent")) in_par = 2; else if (enthaelt(s, "4") || enthaelt(s, "aufgab") || enthaelt(s, "task")) in_par = 3;
        return I_FLAECHE;
    }
    if (enthaelt(s, "agent") || enthaelt(s, "briefing")) {
        in_par = -1;
        if (enthaelt(s, "briefing")) in_par = 0; else if (enthaelt(s, "sicher") || enthaelt(s, "secur")) in_par = 1;
        else if (enthaelt(s, "dokument") || enthaelt(s, "document") || enthaelt(s, "text")) in_par = 4; else if (enthaelt(s, "tabell") || enthaelt(s, "sheet") || enthaelt(s, "excel")) in_par = 5;
        else if (enthaelt(s, "folie") || enthaelt(s, "slide") || enthaelt(s, "praesent") || enthaelt(s, "present")) in_par = 6; else if (enthaelt(s, "ablauf") || enthaelt(s, "board") || enthaelt(s, "flow")) in_par = 7; else if (enthaelt(s, "bild") || enthaelt(s, "image") || enthaelt(s, "illustr") || enthaelt(s, "zeichn")) in_par = 8;
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

/* kleiner Rechner: + - * / und Klammern, Dezimalzahlen (Festkomma mit 6 Nachkommastellen; Komma oder Punkt) */
#define RS 1000000LL
static const char* rp; static int rfehler;
static int64_t r_ausdruck(void);
static void r_leer(void) { while (*rp == ' ') rp++; }
static int64_t r_pruefe(int64_t v) { if (v > 4000000000000000000LL / 1000 || v < -4000000000000000000LL / 1000) { rfehler = 1; return 0; } return v; }
static int64_t r_faktor(void) {
    r_leer(); int neg = 0; while (*rp == '-' || *rp == '+') { if (*rp == '-') neg = !neg; rp++; r_leer(); }
    int64_t v = 0;
    if (*rp == '(') { rp++; v = r_ausdruck(); r_leer(); if (*rp == ')') rp++; else rfehler = 1; }
    else if ((*rp >= '0' && *rp <= '9') || ((*rp == '.' || *rp == ',') && rp[1] >= '0' && rp[1] <= '9')) {
        int64_t ip = 0; while (*rp >= '0' && *rp <= '9') { ip = ip * 10 + (*rp - '0'); rp++; if (ip > 1000000000000LL) { rfehler = 1; break; } }
        int64_t fp = 0, sk = RS / 10;
        if ((*rp == '.' || *rp == ',') && rp[1] >= '0' && rp[1] <= '9') { rp++; while (*rp >= '0' && *rp <= '9') { if (sk) { fp += (*rp - '0') * sk; sk /= 10; } rp++; } }
        v = ip * RS + fp;
    }
    else rfehler = 1;
    return neg ? -v : v;
}
static int64_t r_term(void) {
    int64_t v = r_faktor();
    for (;;) {
        r_leer();
        if (*rp == '*' || *rp == 'x' || *rp == 'X') { rp++; int64_t w = r_faktor(); int64_t p; if (__builtin_mul_overflow(v, w, &p)) { rfehler = 1; return 0; } v = p / RS; }
        else if (*rp == '/' || *rp == ':') {
            rp++; int64_t d = r_faktor(); if (d == 0) { rfehler = 2; return 0; }
            int64_t z; if (__builtin_mul_overflow(v, RS, &z)) { rfehler = 1; return 0; }
            int64_t h = d < 0 ? -d : d; v = (z + ((z < 0) == (d < 0) ? h : -h) / 2) / d;                 /* gerundet */
        }
        else return r_pruefe(v);
    }
}
static int64_t r_ausdruck(void) {
    int64_t v = r_term();
    for (;;) { r_leer(); if (*rp == '+') { rp++; v += r_term(); } else if (*rp == '-') { rp++; v -= r_term(); } else return r_pruefe(v); }
}
/* Ausgabe: bis zu 4 Nachkommastellen, ohne ueberfluessige Nullen; Dezimalzeichen je nach Sprache */
static void i64dec(char* o, int64_t v) {
    if (v < 0) { *o++ = '-'; v = -v; }
    v = (v + 50) / 100;                                                          /* auf 4 Stellen runden (Einheit 1e-4) */
    int64_t ip = v / 10000, fp = v % 10000;
    char t[24]; int n = 0; if (!ip) t[n++] = '0'; while (ip) { t[n++] = (char)('0' + ip % 10); ip /= 10; } while (n) *o++ = t[--n];
    if (fp) { *o++ = lang == 0 ? ',' : '.'; char f[4] = { (char)('0' + fp / 1000), (char)('0' + fp / 100 % 10), (char)('0' + fp / 10 % 10), (char)('0' + fp % 10) }; int e = 4; while (e > 0 && f[e - 1] == '0') e--; for (int i = 0; i < e; i++) *o++ = f[i]; }
    *o = 0;
}

/* Rechner-Fenster */
enum { K_RB0 = 100 };
static char rexpr[40]; static int rlen; static char rres[40];
static const char* const rtasten[20] = { "C", "(", ")", "/", "7", "8", "9", "*", "4", "5", "6", "-", "1", "2", "3", "+", "0", ",", "<", "=" };
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
    else { char c = usb_key_char(k, shift, altgr); if ((c >= '0' && c <= '9') || c == '+' || c == '-' || c == '*' || c == '/' || c == '(' || c == ')' || c == 'x' || c == ':' || c == '.' || c == ',') { char t[2] = { c, 0 }; rech_druecke(t); } else return; }
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
    if (1) { reply1(TR("Dafuer brauche ich eine Sprache ausser Deutsch/Englisch und das Sprachmodell.", "That needs a language other than German/English and the language model.")); return; }
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
static int einst_tab;                                    /* 0 Allgemein, 1 KI, 2 Aussehen */
static int ek_feld;                                      /* Eingabe laeuft: 1 = API-Schluessel, 2 = Modellname, 0 = keine */
static char ek_buf[200]; static int ek_len;
static char ek_meldung[100]; static int ek_ok;
enum { K_ETAB = 15, K_EKEY = 18, K_EKEYDEL, K_EMOD_V, K_EMOD_N, K_EMOD_EIGEN, K_ETEST, K_ETHEMA = 30, K_EHG = 40 };
static const char* const ANBIETER_HOST[4] = { "", "api.openai.com", "api.anthropic.com", "generativelanguage.googleapis.com" };
static const char* const MODELLE[4][5] = {
    { 0, 0, 0, 0, 0 }, { "gpt-4o-mini", "gpt-4o", "gpt-4.1-mini", "gpt-4.1", 0 }, { "claude-haiku-5-5", "claude-sonnet-5-5", "claude-opus-5-5", 0, 0 }, { "gemini-2.0-flash", "gemini-2.5-flash", "gemini-2.5-pro", 0, 0 },
};
static void th_speichern(void) {
    char a[12]; dec(a, (uint32_t)th_nr); zdb_put("ui.schema", a); dec(a, (uint32_t)hg_nr); zdb_put("ui.hintergrund", a);
    kprint("[DESKTOP] Aussehen: Farbschema "); kprint(THEMEN[th_nr].en); kprint(", Hintergrund "); kprint_dec((uint32_t)hg_nr); kprint("\n");
}
static void modell_wechsel(int vor) {                    /* naechstes/vorheriges Modell des gewaehlten Anbieters */
    int q = ext_quelle(); if (!q) return;
    int n = 0, cur = -1; const char* m = ext_modell(q);
    while (n < 5 && MODELLE[q][n]) { if (ueq(MODELLE[q][n], m)) cur = n; n++; }
    if (!n) return;
    cur = cur < 0 ? (vor ? 0 : n - 1) : (cur + (vor ? 1 : n - 1)) % n;
    ext_modell_setze(q, MODELLE[q][cur]);
    kprint("[DESKTOP] KI-Modell: "); kprint(ext_modell(q)); kprint("\n");
}
static void ek_start(int feld) { ek_feld = feld; ek_len = 0; ek_buf[0] = 0; ek_meldung[0] = 0; }
static void ext_pruefen(void) {                          /* Verbindung zum gewaehlten Anbieter testen */
    int q = ext_quelle(); ek_ok = 0;
    if (!q) return;
    if (!ext_schluessel_da(q)) { scopy(ek_meldung, TR("Zuerst den Schluessel eingeben.", "Enter the key first."), 100); return; }
    static char o[300];
    busy_tick();
    int n = ext_frage("Antworte nur mit dem Wort OK.", o, sizeof(o));
    ascii_fold(o);
    if (n > 0) { ek_ok = 1; scopy(ek_meldung, TR("Verbindung ok. Antwort: ", "Connection ok. Reply: "), 100); o[40] = 0; cat(ek_meldung, o, 100); }
    else { scopy(ek_meldung, o[0] ? o : TR("Keine Verbindung.", "No connection."), 100); }
    kprint("[DESKTOP] KI-Test: "); kprint(ek_meldung); kprint("\n");
    busy_ende();
}
static int aufl_wahl = -2;                                                 /* -1 = automatisch, sonst Index in g_boot.modi */
static int aufl_speichern(void) {                                          /* Wunsch in Sektor 20 der EFI-Partition (FAT32-Reservebereich) */
    struct block_dev* d = block_get(); if (!d || !d->name) return -1;
    char esp[24]; scopy(esp, d->name, 24); int n = slen(esp);
    if (n < 3 || esp[n - 2] != 'p' || esp[n - 1] != '2') return -1;
    esp[n - 1] = '1';
    static uint8_t b[512];
    if (block_dev_read(esp, 0, b, 1) < 0 || (b[14] | b[15] << 8) < 24 || b[510] != 0x55 || b[511] != 0xAA) return -2;
    for (int i = 0; i < 512; i++) b[i] = 0;
    if (aufl_wahl >= 0) { const char* m = "ZXAUFL1"; for (int i = 0; i < 7; i++) b[i] = (uint8_t)m[i]; uint32_t w = g_boot.modi[aufl_wahl][0], h = g_boot.modi[aufl_wahl][1]; for (int i = 0; i < 4; i++) { b[8 + i] = (uint8_t)(w >> (8 * i)); b[12 + i] = (uint8_t)(h >> (8 * i)); } }
    if (block_dev_write(esp, 20, b, 1) < 0) return -3;
    return 0;
}
static void aufl_text(char* t) { if (aufl_wahl < 0) { scopy(t, TR("Automatisch", "Automatic"), 40); return; } char a[12]; dec(a, g_boot.modi[aufl_wahl][0]); scopy(t, a, 40); cat(t, " x ", 40); dec(a, g_boot.modi[aufl_wahl][1]); cat(t, a, 40); }
static void aufl_init(void) { if (aufl_wahl != -2) return; aufl_wahl = -1; for (uint32_t i = 0; i < g_boot.nmodi; i++) if ((int)g_boot.modi[i][0] == W && (int)g_boot.modi[i][1] == H) aufl_wahl = (int)i; }
static void aufl_uebernehmen(void) {
    int r = aufl_speichern(); char t[40]; aufl_text(t);
    if (r == 0) { scopy(ek_meldung, TR("Gespeichert: ", "Saved: "), 100); cat(ek_meldung, t, 100); cat(ek_meldung, TR(" - gilt ab dem naechsten Neustart.", " - takes effect after the next restart."), 100); ek_ok = 1; }
    else { scopy(ek_meldung, r == -1 ? TR("Nur auf dem installierten System moeglich.", "Only possible on the installed system.") : TR("EFI-Partition nicht beschreibbar.", "EFI partition not writable."), 100); ek_ok = 0; }
    kprint("[DESKTOP] Aufloesung gespeichert: "); kprint(t); kprint(r ? " (Fehler)\n" : "\n");
}
static void einst_klick(int id) {
    int k = id - K_EIN0;
    if (k >= K_ETAB && k < K_ETAB + 3) { einst_tab = k - K_ETAB; ek_feld = 0; ek_meldung[0] = 0; kprint("[DESKTOP] Einstellungen: Reiter "); kprint_dec((uint32_t)einst_tab); kprint("\n"); }
    else if (k >= 0 && k < 5) lang_setze(k);
    else if (k == 5 || k == 6) { int n = usb_get_layout() + (k == 5 ? -1 : 1); if (n < 0) n = KBD_LAYOUTS - 1; if (n >= KBD_LAYOUTS) n = 0; usb_set_layout(n); zdb_put("system.tastatur", kbd_layouts[n].code); }
    else if (k >= 7 && k <= 9) { fw_modus(k == 7 ? FW_AUS : k == 8 ? FW_NORMAL : FW_STRENG); fw_speichern(); }
    else if (k >= 10 && k <= 13) {
        int q = k - 10; ek_feld = 0; ek_meldung[0] = 0; ext_setze_quelle(q);
        if (q && !ext_schluessel_da(q)) ek_start(1);                                       /* gleich nach dem Schluessel fragen */
    }
    else if (k == 14) { sprachpaket(); return; }
    else if (k == K_EKEY - 0 && ext_quelle()) ek_start(1);
    else if (k == K_EKEYDEL && ext_quelle()) { ext_schluessel_setze(ext_quelle(), ""); ek_feld = 0; scopy(ek_meldung, TR("Schluessel geloescht.", "Key deleted."), 100); ek_ok = 0; }
    else if ((k == K_EMOD_V || k == K_EMOD_N) && ext_quelle()) { modell_wechsel(k == K_EMOD_N); ek_feld = 0; ek_meldung[0] = 0; }
    else if (k == K_EMOD_EIGEN && ext_quelle()) ek_start(2);
    else if (k == 51 || k == 52) { aufl_init(); int n = (int)g_boot.nmodi; aufl_wahl += k == 52 ? 1 : -1; if (aufl_wahl >= n) aufl_wahl = -1; if (aufl_wahl < -1) aufl_wahl = n - 1; ek_meldung[0] = 0; }
    else if (k == 53) { aufl_init(); aufl_uebernehmen(); }
    else if (k == 50) { if (benutzer_rolle() != ROLLE_ADMIN) scopy(ek_meldung, TR("Nur ein Administrator kann verschluesseln.", "Only an administrator can encrypt."), 100); else { ek_start(3); scopy(ek_meldung, TR("Neues Plattenpasswort (mind. 8 Zeichen), dann Enter. Ohne Passwort KEIN Zugriff mehr!", "New disk password (min. 8 chars), then Enter. Without it there is NO access!"), 100); } }
    else if (k == K_ETEST) { ek_feld = 0; scopy(ek_meldung, TR("Pruefe ...", "Checking ..."), 100); zeichne_einst(); ext_pruefen(); }
    else if (k >= K_ETHEMA && k < K_ETHEMA + 6) { th_nr = k - K_ETHEMA; th_speichern(); }
    else if (k >= K_EHG && k < K_EHG + 5) { hg_nr = k - K_EHG; th_speichern(); }
    else if (k == K_EHG + 6 || k == K_EHG + 7) { hg_nr = (int)(k - K_EHG); th_speichern(); }
    else if (k == K_EHG + 5) {                                              /* eigenes Bild: naechste Bilddatei */
        static const char* const END[] = { ".png", ".jpg", ".jpeg", ".gif", 0 }; char nm[32]; int i = 0, gef = -1;
        while (synfs_nte_datei(i, END, nm, 32)) { if (ueq(nm, hg_datei)) gef = i; i++; }
        if (!i) { scopy(ek_meldung, TR("Keine Bilder gefunden. Speichere eines in Zeichnen oder im Browser (Rechtsklick auf ein Bild).", "No images found. Save one in Draw or in the browser (right-click an image)."), 100); }
        else { synfs_nte_datei(hg_nr == 5 ? (gef + 1) % i : (gef >= 0 ? gef : 0), END, nm, 32); scopy(hg_datei, nm, 32); zdb_put("ui.hgbild", hg_datei); hg_nr = 5; th_speichern(); }
    }
    zeichne_desktop();
}
static char ek_pw1[64];
static void ek_wische(char* b, int n) { for (int i = 0; i < n; i++) ((volatile char*)b)[i] = 0; }
static void krypto_aus_einst(void) {                                       /* Plattenverschluesselung aus den Einstellungen */
    if (!synfs_is_mounted()) { scopy(ek_meldung, TR("Nur auf dem installierten System moeglich (Live-System: keine Platte eingebunden).", "Only on the installed system (live system: no disk mounted)."), 100); return; }
    scopy(ek_meldung, TR("Verschluessele ... nicht ausschalten!", "Encrypting ... do not switch off!"), 100); zeichne_einst(); busy_tick();
    kprint("[DESKTOP] Plattenverschluesselung startet\n");
    int r = krypto_einrichten(ek_pw1);
    busy_ende(); ek_wische(ek_pw1, sizeof(ek_pw1));
    ek_ok = r == 0;
    if (r == 0) { scopy(ek_meldung, TR("Fertig: die Platte ist verschluesselt. Beim Start wird das Passwort abgefragt.", "Done: the disk is encrypted. The password is asked at boot."), 100); }
    else if (r == -1) scopy(ek_meldung, TR("SynFS ist nicht eingebunden.", "SynFS is not mounted."), 100);
    else if (r == -2) scopy(ek_meldung, TR("Die Platte ist schon verschluesselt.", "The disk is already encrypted."), 100);
    else if (r == -3) scopy(ek_meldung, TR("Passwort zu kurz (mindestens 8 Zeichen).", "Password too short (at least 8 characters)."), 100);
    else { char a[12]; scopy(ek_meldung, TR("Fehlgeschlagen (Code ", "Failed (code "), 100); dec(a, (uint32_t)(-r)); cat(ek_meldung, "-", 100); cat(ek_meldung, a, 100); cat(ek_meldung, TR("). Shell: krypto fortsetzen", "). Shell: krypto fortsetzen"), 100); }
    kprint("[DESKTOP] Plattenverschluesselung: "); kprint(ek_meldung); kprint("\n");
}
static void einst_taste(uint8_t k, int shift, int altgr) {                  /* Eingabe in das Schluessel-, Modell- oder Passwortfeld */
    int q = ext_quelle();
    if (k == 0x29) { ek_feld = 0; ek_len = 0; ek_buf[0] = 0; ek_wische(ek_pw1, sizeof(ek_pw1)); }
    else if ((k == 0x28 || k == 0x58) && ek_feld >= 3) {
        if (ek_feld == 3) { if (ek_len < 8) scopy(ek_meldung, TR("Mindestens 8 Zeichen.", "At least 8 characters."), 100); else { scopy(ek_pw1, ek_buf, 64); ek_feld = 4; scopy(ek_meldung, TR("Passwort wiederholen, dann Enter.", "Repeat the password, then Enter."), 100); } }
        else { int gl = ueq(ek_buf, ek_pw1); ek_feld = 0; if (!gl) { scopy(ek_meldung, TR("Die Passwoerter sind verschieden.", "The passwords differ."), 100); ek_wische(ek_pw1, sizeof(ek_pw1)); } else { ek_wische(ek_buf, sizeof(ek_buf)); ek_len = 0; krypto_aus_einst(); } }
        ek_wische(ek_buf, sizeof(ek_buf)); ek_len = 0;
    }
    else if (k == 0x28 || k == 0x58) {
        if (ek_len && q) {
            if (ek_feld == 1) { ext_schluessel_setze(q, ek_buf); ek_ok = 0; kprint("[DESKTOP] KI-Schluessel gespeichert ("); kprint_dec((uint32_t)ek_len); kprint(" Zeichen)\n"); scopy(ek_meldung, TR("Schluessel gespeichert. Mit \"Verbindung testen\" pruefen.", "Key saved. Check it with \"Test connection\"."), 100); }
            else { ext_modell_setze(q, ek_buf); scopy(ek_meldung, TR("Modell gespeichert.", "Model saved."), 100); }
        }
        ek_feld = 0; ek_len = 0; ek_buf[0] = 0;
    }
    else if (k == 0x2A) { if (ek_len) ek_buf[--ek_len] = 0; }
    else if (cur_ctrl && k == 0x19) { for (int i = 0; i < cliplen && ek_len < 190; i++) if (clip[i] >= 33 && clip[i] < 127) ek_buf[ek_len++] = clip[i]; ek_buf[ek_len] = 0; }
    else { char c = usb_key_char(k, shift, altgr); if (c >= 33 && c < 127 && ek_len < 190) { ek_buf[ek_len++] = c; ek_buf[ek_len] = 0; } else return; }
    zeichne_einst();
}
static void einst_kurztaste(uint8_t k, int shift, int altgr) {            /* Tastatur: Tab = Reiter wechseln; Aussehen: 1-6 Farbschema, H Hintergrund; KI: 0-3 Anbieter, M Modell */
    if (k == 0x2B) { einst_tab = (einst_tab + 1) % 3; ek_feld = 0; ek_meldung[0] = 0; kprint("[DESKTOP] Einstellungen: Reiter "); kprint_dec((uint32_t)einst_tab); kprint("\n"); zeichne_desktop(); return; }
    char c = usb_key_char(k, shift, altgr);
    if (einst_tab == 0 && (c == 'v' || c == 'V')) { einst_klick(K_EIN0 + 50); return; }       /* v = Platte verschluesseln */
    if (einst_tab == 0 && (c == 'a' || c == 'A')) { aufl_init(); aufl_wahl = (int)g_boot.nmodi - 1; aufl_uebernehmen(); zeichne_desktop(); return; }   /* a = groesste Aufloesung waehlen */
    if (einst_tab == 2) {
        if (c >= '1' && c <= '6') { th_nr = c - '1'; th_speichern(); zeichne_desktop(); }
        else if (c == 'h' || c == 'H') { hg_nr = (hg_nr + 1) % 5; th_speichern(); zeichne_desktop(); }
        else if (c == 'w' || c == 'W') { hg_nr = 6; th_speichern(); zeichne_desktop(); }
        else if (c == 'z' || c == 'Z') { hg_nr = 7; th_speichern(); zeichne_desktop(); }
    } else if (einst_tab == 1) {
        if (c >= '0' && c <= '3') einst_klick(K_EIN0 + 10 + (c - '0'));
        else if ((c == 'm' || c == 'M') && ext_quelle()) { modell_wechsel(1); zeichne_einst(); }
    }
}
static void einst_feld(int x, int y, int w, int aktiv, const char* text, int maske) {
    rrect_rand(x, y, w, 30, 8, C_FIELD, aktiv ? C_ACC : C_BORDER);
    char t[64]; int n = slen(text), mc = (w - 24) / 8; if (mc > 62) mc = 62;
    int von = n > mc ? n - mc : 0, o = 0;
    for (int i = von; i < n && o < 62; i++) t[o++] = (maske == 2 || (maske && i < n - 4)) ? '*' : text[i];
    t[o] = 0; gfx_text(x + 10, y + 7, t, C_TEXT, 1);
    if (aktiv) gfx_rect(x + 10 + o * 8, y + 5, 2, 20, C_TEXT);
}
static void zeichne_einst(void) {
    int wi = fenster_idx(FT_EINST); if (wi < 0) return;
    struct fenster* f = &fen[wi]; int x = f->x + 24, bw = f->w - 48, ko = kn_owner; kn_owner = wi; kn_loesche_owner(wi);
    int y = f->y + WT_H + 12;
    gfx_begin(); gfx_rect(f->x + 2, f->y + WT_H, f->w - 4, f->h - WT_H - 2, C_BG);
    { int tw = (bw - 12) / 3; const char* tn[3] = { T5("Allgemein", "General", "General", "Generale", "General"), T5("KI-Modelle", "AI models", "Modeles IA", "Modelli IA", "Modelos IA"), T5("Aussehen", "Appearance", "Apparence", "Aspetto", "Aspecto") };
      for (int i = 0; i < 3; i++) knopf(K_EIN0 + K_ETAB + i, tn[i], x + i * (tw + 6), y, tw, i == einst_tab); }
    if (Q_GLAS) h_grad(x, y + 44, bw, 2, mix(C_BG, Q_CY, 130), mix(C_BG, Q_VIO, 130));
    y += 50;
    if (einst_tab == 0) {
        gtxt(x, y, T5("Sprache", "Language", "Langue", "Lingua", "Idioma"), C_MUTED, 1);
        { int w5 = (bw - 4 * 8) / 5; for (int i = 0; i < 5; i++) knopf(K_EIN0 + i, LNAME[i], x + i * (w5 + 8), y + 22, w5, i == lang); }
        y += 78;
        { int c = usb_get_layout(); char t[60]; scopy(t, T5("Tastatur: ", "Keyboard: ", "Clavier : ", "Tastiera: ", "Teclado: "), 60); cat(t, kbd_layouts[c].code, 60); cat(t, " (", 60); cat(t, kbd_layouts[c].name, 60); cat(t, ")", 60); gtxt(x, y + 9, t, C_TEXT, 1); }
        knopf(K_EIN0 + 5, "<", f->x + f->w - 100, y, 36, 0); knopf(K_EIN0 + 6, ">", f->x + f->w - 56, y, 36, 0);
        y += 52; aufl_init();
        { char t[60], a[40]; aufl_text(a); scopy(t, T5("Bildschirm: ", "Screen: ", "Ecran : ", "Schermo: ", "Pantalla: "), 60); cat(t, a, 60); gtxt(x, y + 9, t, C_TEXT, 1);
          knopf(K_EIN0 + 51, "<", f->x + f->w - 236, y, 36, 0); knopf(K_EIN0 + 52, ">", f->x + f->w - 194, y, 36, 0); knopf(K_EIN0 + 53, TR("Uebernehmen", "Apply"), f->x + f->w - 152, y, 128, 0);
          if (g_boot.nmodi <= 1) gfx_text_f(x, y + 34, l1(TR("Die Firmware bietet nur diese Aufloesung an (Hyper-V: Set-VMVideo am Host).", "The firmware offers only this resolution (Hyper-V: Set-VMVideo on the host).")), C_MUTED, 12); }
        y += 56; gtxt(x, y, "Firewall", C_MUTED, 1);
        { int m = fw_modus_get(); knopf(K_EIN0 + 7, T5("Aus", "Off", "Desactive", "Spento", "Apagado"), x, y + 22, 120, m == 0); knopf(K_EIN0 + 8, "Normal", x + 128, y + 22, 120, m == 1); knopf(K_EIN0 + 9, T5("Streng", "Strict", "Strict", "Rigido", "Estricto"), x + 256, y + 22, 120, m == 2); }
        y += 82;
        { int ver = synfs_is_mounted() && synfs_krypto_hdr()->magic == SYNFS_KRY_MAGIC;
          gtxt(x, y, T5("Festplatte", "Disk", "Disque", "Disco", "Disco"), C_MUTED, 1);
          rrect(x, y + 26, 14, 14, 7, ver ? C_OK : 0x00F59E0Bu);
          gtxt(x + 22, y + 24, ver ? TR("verschluesselt (256 Bit)", "encrypted (256-bit)") : synfs_is_mounted() ? TR("nicht verschluesselt", "not encrypted") : TR("Live-System (keine Platte eingebunden)", "live system (no disk mounted)"), C_TEXT, 1);
          if (!ver && ek_feld < 3) knopf(K_EIN0 + 50, TR("Jetzt verschluesseln", "Encrypt now"), x + bw - 200, y + 16, 200, 1);
          if (ek_feld >= 3) einst_feld(x, y + 52, bw, 1, ek_buf, 2);
          if (ek_meldung[0] && ek_feld != 1 && ek_feld != 2) gtxt(x, y + (ek_feld >= 3 ? 90 : 56), ek_meldung, ek_ok ? C_OK : 0x00F59E0Bu, 1);
          y += ek_feld >= 3 ? 70 : 40; }
        gtxt(x, y + 26 + 30, zdb_offen() ? TR("Alles wird in ZynDB gespeichert (verschluesselt)", "Everything is stored in ZynDB (encrypted)") : TR("ZynDB nicht verfuegbar", "ZynDB unavailable"), C_MUTED, 1);
    } else if (einst_tab == 1) {
        int qa = ext_quelle();
        gtxt(x, y, T5("Sprachmodell (KI)", "Language model (AI)", "Modele de langage (IA)", "Modello linguistico (IA)", "Modelo de lenguaje (IA)"), C_MUTED, 1);
        { int w4 = (bw - 3 * 8) / 4; for (int i = 0; i < 4; i++) knopf(K_EIN0 + 10 + i, ext_name(i), x + i * (w4 + 8), y + 22, w4, i == qa); }
        y += 70;
        if (!qa) {
            gtxt(x, y, gehirn_vorhanden() ? TR("Zynos (intern) laeuft lokal. Nichts verlaesst dieses Geraet.", "Zynos (internal) runs locally. Nothing leaves this device.") : TR("Kein internes Sprachmodell geladen.", "No internal language model loaded."), C_TEXT, 1);
            gtxt(x, y + 28, TR("Das kleine lokale Modell macht Fehler. Fuer bessere Antworten", "The small local model makes mistakes. For better answers"), C_MUTED, 1);
            gtxt(x, y + 46, TR("ChatGPT, Claude oder Gemini oben waehlen und den Schluessel eintragen.", "choose ChatGPT, Claude or Gemini above and enter your key."), C_MUTED, 1);
            if (0) knopf(K_EIN0 + 14, T5("Sprachpaket", "Language pack", "Pack de langue", "Pacchetto lingua", "Paquete de idioma"), x, y + 84, 220, 0);
        } else {
            int ch = 236; rrect_rand(x, y, bw, ch, 12, C_CARD, C_BORDER);
            { char t[100]; scopy(t, ext_name(qa), 100); cat(t, "  -  ", 100); cat(t, ANBIETER_HOST[qa], 100); gtxt(x + 14, y + 12, t, C_ACC2, 1); }
            gtxt(x + 14, y + 38, TR("API-Schluessel", "API key"), C_MUTED, 1);
            if (ek_feld == 1) einst_feld(x + 14, y + 58, bw - 28, 1, ek_buf, 1);
            else einst_feld(x + 14, y + 58, bw - 28, 0, ext_schluessel_da(qa) ? TR("gespeichert (verschluesselt)", "saved (encrypted)") : TR("noch kein Schluessel", "no key yet"), 0);
            knopf(K_EIN0 + K_EKEY, ext_schluessel_da(qa) ? TR("Aendern", "Change") : TR("Eingeben", "Enter"), x + 14, y + 98, 150, ek_feld == 1);
            if (ext_schluessel_da(qa)) knopf(K_EIN0 + K_EKEYDEL, TR("Loeschen", "Delete"), x + 172, y + 98, 130, 0);
            if (ek_feld == 1) gtxt(x + 312, y + 107, TR("Enter = speichern, Esc = abbrechen, Strg+V = einfuegen", "Enter = save, Esc = cancel, Ctrl+V = paste"), C_MUTED, 1);
            gtxt(x + 14, y + 144, TR("Modell", "Model"), C_MUTED, 1);
            knopf(K_EIN0 + K_EMOD_V, "<", x + 14, y + 164, 36, 0);
            if (ek_feld == 2) einst_feld(x + 56, y + 166, bw - 56 - 56 - 168, 1, ek_buf, 0); else einst_feld(x + 56, y + 166, bw - 56 - 56 - 168, 0, ext_modell(qa), 0);
            knopf(K_EIN0 + K_EMOD_N, ">", x + bw - 14 - 36 - 160, y + 164, 36, 0);
            knopf(K_EIN0 + K_EMOD_EIGEN, TR("Eigener Name", "Custom name"), x + bw - 14 - 150, y + 164, 150, ek_feld == 2);
            knopf(K_EIN0 + K_ETEST, TR("Verbindung testen", "Test connection"), x + 14, y + 192, 210, 0);
            gtxt(x + 236, y + 201, ek_meldung, ek_ok ? C_OK : (ek_meldung[0] ? 0x00F59E0Bu : C_MUTED), 1);
            gtxt(x, y + ch + 10, TR("Fragen und Antworten gehen verschluesselt (TLS) direkt an den Anbieter.", "Questions and answers go to the provider over TLS."), C_MUTED, 1);
            gtxt(x, y + ch + 30, TR("Faellt der Anbieter aus, antwortet Zynos (intern).", "If the provider fails, Zynos (internal) answers."), C_MUTED, 1);
        }
    } else {
        gtxt(x, y, T5("Farbschema", "Colour scheme", "Couleurs", "Colori", "Colores"), C_MUTED, 1);
        int sw = (bw - 20) / 3;
        for (int i = 0; i < 6; i++) {
            const uint32_t* c = THEMEN[i].c; int sx = x + (i % 3) * (sw + 10), sy = y + 24 + (i / 3) * 78;
            if (i == th_nr) rrect(sx - 3, sy - 3, sw + 6, 72, 15, C_ACC2);
            rrect_rand(sx, sy, sw, 66, 12, c[1], c[2]);
            rrect(sx + 8, sy + 8, sw - 16, 26, 7, c[9]); rrect(sx + 8 + (sw - 16) / 2, sy + 8, (sw - 16) / 2, 26, 7, c[10]);
            rrect(sx + 14, sy + 14, 14, 14, 7, c[5]); rrect(sx + 32, sy + 18, 40, 6, 3, c[3]);
            gtxt(sx + 10, sy + 42, lang == 0 ? THEMEN[i].de : THEMEN[i].en, c[3], 1);
            kn_reg(K_EIN0 + K_ETHEMA + i, sx, sy, sw, 66);
        }
        y += 24 + 2 * 78 + 6;
        gtxt(x, y, T5("Hintergrund", "Background", "Arriere-plan", "Sfondo", "Fondo"), C_MUTED, 1);
        { static const char* const hn[8][2] = { { "Verlauf", "Gradient" }, { "Aurora", "Aurora" }, { "Raster", "Grid" }, { "Einfarbig", "Solid" }, { "Futuristisch", "Futuristic" }, { "Eigenes Bild", "Own image" }, { "Wasser", "Water" }, { "Zynex", "Zynex" } };
          int w3 = (bw - 2 * 8) / 3; for (int i = 0; i < 8; i++) knopf(K_EIN0 + K_EHG + i, hn[i][lang == 0 ? 0 : 1], x + (i % 3) * (w3 + 8), y + 22 + (i / 3) * 42, w3, i == hg_nr);
          y += 126; if (hg_nr == 5) { char t[60]; scopy(t, TR("Bild: ", "Image: "), 60); cat(t, hg_datei, 60); cat(t, TR("  (nochmals klicken = naechstes)", "  (click again = next)"), 60); gfx_text_f(x, y + 70, t, C_MUTED, 12); }
          if (ek_meldung[0] && einst_tab == 2) gfx_text_f(x, y + 88, l1(ek_meldung), 0x00F59E0Bu, 12); }
        gtxt(x, y + 70, TR("Die Aenderung ist sofort hinter dem Fenster zu sehen.", "The change is visible right away behind the window."), C_MUTED, 1);
    }
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
enum { K_GED_N0 = 510 };
static int ged_fok;                                      /* gewaehltes Neuron (Rang) */
static void ged_pille(int x, int y, int w, int h, uint32_t rand_c, uint32_t f1, uint32_t f2) { rrect(x, y, w, h, h / 2, rand_c); rr_grad(x + 1, y + 1, w - 2, h - 2, h / 2 - 1, f1, f2); }
static void ged_chip(int x, int y, const char* t, uint32_t c) { int w = gtw(t, 1) + 20; ged_pille(x, y, w, 24, mix(0x000A1830u, c, 90), 0x00101E38u, 0x00070D1Cu); gtxt(x + 10, y + 4, t, c, 1); }
static void ged_knopf(int id, const char* label, int x, int y, int w, int stil) {
    if (!Q_GLAS) { knopf(id, label, x, y, w, stil == 1 || stil == 2); return; }
    if (stil == 1) { rrect(x - 1, y - 1, w + 2, 38, 14, mix(C_BG, Q_VIO, 140)); h_pille(x, y, w, 36, 0x006D28D9u, 0x002563EBu); gtxt(x + (w - gtw(label, 1)) / 2, y + 10, label, 0x00FFFFFFu, 1); }
    else if (stil == 2) { rrect(x, y, w, 36, 13, mix(C_BG, Q_CY, 120)); rrect(x + 1, y + 1, w - 2, 34, 12, mix(C_BG, Q_CY, 40)); gtxt(x + (w - gtw(label, 1)) / 2, y + 10, label, Q_CY, 1); }
    else if (stil == 3) { rrect(x, y, w, 36, 13, 0x00B91C1Cu); rrect(x + 1, y + 1, w - 2, 34, 12, 0x002A0F14u); gtxt(x + (w - gtw(label, 1)) / 2, y + 10, label, 0x00FCA5A5u, 1); }
    else { rrect(x, y, w, 36, 13, mix(C_BG, 0x00FFFFFFu, 36)); rrect(x + 1, y + 1, w - 2, 34, 12, mix(C_BG, C_CARD, 200)); gtxt(x + (w - gtw(label, 1)) / 2, y + 10, label, C_TEXT, 1); }
    kn_reg(id, x, y, w, 36);
}
static void ged_klick(int id) {
    if (id >= K_GED_N0 && id < K_GED_N0 + 16) { ged_fok = id - K_GED_N0; ged_vfrage = 0; }
    else if (id == K_GED_SCHLAF) { ged_schlafen(); ged_vfrage = 0; }
    else if (id == K_GED_EXT) { zdb_put("ged.extern", zdb_geti("ged.extern", 0) ? "0" : "1"); ged_vfrage = 0; }
    else if (id == K_GED_VERG) { if (ged_vfrage) { ged_loeschen(); ged_vfrage = 0; ged_fok = 0; } else ged_vfrage = 1; }
    zeichne_desktop();
}
static void zeichne_ged(void) {
    int wi = fenster_idx(FT_GED); if (wi < 0) return;
    struct fenster* f = &fen[wi]; int ko = kn_owner; kn_owner = wi; kn_loesche_owner(wi);
    int x0 = f->x + 2, y0 = f->y + WT_H, cw = f->w - 4, bh = f->h - WT_H - 2, fh = 92;
    gfx_begin(); gfx_rect(x0, y0, cw, bh, Q_GLAS ? 0x00070D1Cu : C_BG);
    int nn, ss, gg, tt; ged_zahlen(&nn, &ss, &gg, &tt);
    { char a[12], t[40]; int x = x0 + 14;
      ged_chip(x, y0 + 10, "Semantic HVM", Q_CY); x += gtw("Semantic HVM", 1) + 34;
      dec(a, (uint32_t)nn); scopy(t, a, 40); cat(t, TR(" Neuronen", " neurons"), 40); ged_chip(x, y0 + 10, t, 0x0067E8F9u); x += gtw(t, 1) + 28;
      dec(a, (uint32_t)ss); scopy(t, a, 40); cat(t, TR(" Synapsen", " synapses"), 40); ged_chip(x, y0 + 10, t, 0x00D8B4FEu); x += gtw(t, 1) + 28;
      dec(a, (uint32_t)gg); scopy(t, a, 40); cat(t, TR(" Erinnerungen", " memories"), 40); ged_chip(x, y0 + 10, t, 0x006EE7B7u); }
    int px0 = x0 + 12, py0 = y0 + 46, pw = cw * 62 / 100 - 12, ph = bh - 46 - fh - 12;
    if (Q_GLAS) { rrect(px0, py0, pw, ph, 18, mix(0x00070D1Cu, Q_SKY, 55)); rr_grad(px0 + 1, py0 + 1, pw - 2, ph - 2, 17, 0x000C1830u, 0x0004080Fu); }
    else rrect_rand(px0, py0, pw, ph, 10, C_CARD, C_BORDER);
    if (Q_GLAS) {
        for (int k = 0; k < 2; k++) { int gx = k ? px0 + pw * 33 / 100 : px0 + pw / 2, gy = k ? py0 + ph * 36 / 100 : py0 + ph / 2, gr = k ? 70 : 90; uint32_t gc = k ? Q_VIO : Q_CY;
            for (int y = -gr; y <= gr; y += 2) for (int x = -gr; x <= gr; x += 2) { int d2 = x * x + y * y; if (d2 < gr * gr) { int a = 22 * (gr * gr - d2) / (gr * gr); gfx_rect(gx + x, gy + y, 2, 2, mix(0x00070E1Cu, gc, a)); } } }
        for (int y = py0 + 12; y < py0 + ph - 8; y += 24) for (int x = px0 + 12; x < px0 + pw - 8; x += 24) gfx_blend(x, y, Q_SKY, 70);
    }
    char nm[36][13]; int nu[36], er[36], sa[160], sb[160], sw[160], ns2 = 0, bx[36], by[36];
    int c = ged_netz(36, nm, nu, er, 160, sa, sb, sw, &ns2);
    if (ged_fok >= c) ged_fok = 0;
    int cx = px0 + pw / 2, cy = py0 + ph / 2, rx = pw / 2 - 60, ry = ph / 2 - 30;
    for (int i = 0; i < c; i++) {
        int deg = (i * 1375 / 10) % 360, s = c > 1 ? isqrt_i(i * 1000 / c) : 0;
        bx[i] = cx + rx * s / 31 * icos(deg) / 1024; by[i] = cy + ry * s / 31 * isin(deg) / 1024;
    }
    for (int j = 0; j < ns2; j++) if (sw[j] >= 20) {
        int hl = sa[j] == ged_fok || sb[j] == ged_fok;
        if (Q_GLAS) {
            uint32_t lc = mix(Q_CY, Q_VIO, (bx[sa[j]] - px0) * 256 / pw);
            if (hl) { fl_seg16(bx[sa[j]] * 16, by[sa[j]] * 16, bx[sb[j]] * 16, by[sb[j]] * 16, Q_CY, 64, 40, 0, 0); fl_seg16(bx[sa[j]] * 16, by[sa[j]] * 16, bx[sb[j]] * 16, by[sb[j]] * 16, Q_CY, 32, 235, 0, 0);
                int t = 35 + (j * 29) % 40; gfx_rect((bx[sa[j]] + (bx[sb[j]] - bx[sa[j]]) * t / 100) - 2, (by[sa[j]] + (by[sb[j]] - by[sa[j]]) * t / 100) - 2, 4, 4, 0x00FFFFFFu); }
            else fl_seg16(bx[sa[j]] * 16, by[sa[j]] * 16, bx[sb[j]] * 16, by[sb[j]] * 16, lc, 16, 40 + sw[j] * 90 / 255, 0, 0);
        } else linie(bx[sa[j]], by[sa[j]], bx[sb[j]], by[sb[j]], mix(0x00243049u, C_ACC, sw[j] * 256 / 255));
    }
    for (int i = c - 1; i >= 0; i--) {
        int hell = er[i] >= 500, sel = i == ged_fok;
        if (Q_GLAS && i < 14) {
            int pw2 = gtw(nm[i], 1) + 30, qx = bx[i] - pw2 / 2, qy = by[i] - 12;
            if (qx < px0 + 6) qx = px0 + 6; if (qx + pw2 > px0 + pw - 6) qx = px0 + pw - 6 - pw2; if (qy < py0 + 6) qy = py0 + 6; if (qy + 26 > py0 + ph - 6) qy = py0 + ph - 32;
            if (sel) { rrect(qx - 5, qy - 5, pw2 + 10, 34, 17, mix(0x00070E1Cu, Q_CY, 40)); rrect(qx - 2, qy - 2, pw2 + 4, 28, 14, mix(0x00070E1Cu, Q_CY, 90)); }
            ged_pille(qx, qy, pw2, 24, sel ? Q_CY : mix(0x000A1830u, 0x00FFFFFFu, 46), 0x0010203Cu, 0x00080F20u);
            rrect(qx + 8, qy + 7, 10, 10, 5, mix(0x000A1830u, hell ? 0x00C084FCu : Q_CY, 70)); rrect(qx + 10, qy + 9, 6, 6, 3, hell ? 0x00C084FCu : 0x0067E8F9u);
            gtxt(qx + 22, qy + 4, nm[i], sel || hell ? 0x00FFFFFFu : 0x00CBD5E1u, 1);
            kn_reg(K_GED_N0 + i, qx, qy, pw2, 24);
        } else {
            int r = 3 + (nu[i] > 56 ? 7 : nu[i] / 8);
            rrect(bx[i] - r, by[i] - r, 2 * r, 2 * r, r, hell ? C_ACC2 : mix(0x00334155u, C_ACC, 80 + (nu[i] > 20 ? 176 : nu[i] * 8)));
            if (i < 14) gtxt(bx[i] + r + 3, by[i] - 7, nm[i], hell ? C_TEXT : C_MUTED, 1);
        }
    }
    if (!c) gtxt(px0 + 24, py0 + ph / 2 - 8, TR("Noch leer. Sag: merk dir, dass mein Hund Bello heisst.", "Still empty. Say: remember that my dog is called Bello."), C_MUTED, 1);
    gtxt(px0 + 12, py0 + ph - 22, TR("Neuron anklicken zum Fokussieren", "Click a neuron to focus"), 0x00708098u, 1);
    int rx0 = px0 + pw + 14, rw = x0 + cw - 12 - rx0;
    if (Q_GLAS) { rrect(rx0, py0, rw, ph, 18, mix(0x00070D1Cu, 0x00FFFFFFu, 28)); rr_grad(rx0 + 1, py0 + 1, rw - 2, ph - 2, 17, 0x00091428u, 0x00060B18u); } else rrect_rand(rx0, py0, rw, ph, 10, C_CARD, C_BORDER);
    gtxt(rx0 + 14, py0 + 12, TR("Erinnerungen", "Memories"), Q_GLAS ? 0x0067E8F9u : C_ACC2, 1); { const char* lz = "Langzeit-HVM"; gtxt(rx0 + rw - 14 - gtw(lz, 1), py0 + 12, lz, C_MUTED, 1); }
    gfx_rect(rx0 + 12, py0 + 36, rw - 24, 1, mix(0x00070D1Cu, 0x00FFFFFFu, 40));
    int fcy = py0 + ph - 78;                                                                         /* Fokus-Karte unten */
    { int rows = (fcy - (py0 + 46) - 6) / 56; if (rows > 8) rows = 8; int cols = (rw - 32) / 8; int shown = 0;
      for (int i = 0; i < rows; i++) {
          char t[112]; int art, st; if (!ged_erinnerung(i, t, 112, &art, &st)) break; shown++;
          int ry2 = py0 + 46 + i * 56; rrect(rx0 + 10, ry2, rw - 20, 52, 12, Q_GLAS ? mix(0x00070D1Cu, 0x00C084FCu, 80) : C_BORDER); rr_grad(rx0 + 11, ry2 + 1, rw - 22, 50, 11, 0x0010183Au, 0x00090F22u);
          char l1_[40], l2_[40]; int len = slen(t), cut = len > cols ? cols : len; if (cut < len) { int sp = cut; while (sp > cols / 2 && t[sp] != ' ') sp--; if (t[sp] == ' ') cut = sp; }
          int k = 0; for (; k < cut && k < 39; k++) l1_[k] = t[k]; l1_[k] = 0; int o = cut; while (t[o] == ' ') o++;
          k = 0; for (; t[o] && k < 39 && k < cols - 2; k++, o++) l2_[k] = t[o]; l2_[k] = 0; if (t[o]) { l2_[slen(l2_) > 2 ? slen(l2_) - 2 : 0] = 0; cat(l2_, "..", 40); }
          gtxt(rx0 + 20, ry2 + 4, "HVM SNAPSHOT", 0x00D8B4FEu, 1); { char pc[8]; dec(pc, (uint32_t)(st * 100 / 255)); cat(pc, "%", 8); gtxt(rx0 + rw - 20 - gtw(pc, 1), ry2 + 4, pc, 0x00D8B4FEu, 1); }
          gtxt(rx0 + 20, ry2 + 20, l1_, 0x00E2E8F0u, 1); gtxt(rx0 + 20, ry2 + 35, l2_, C_MUTED, 1);
          if (Q_GLAS) h_grad(rx0 + 20, ry2 + 49, 4 + st * (rw - 44) / 255, 2, Q_CY, art == 2 ? 0x00F59E0Bu : Q_VIO);
      }
      if (!shown) { int y = py0 + 48; rrect(rx0 + 10, y, rw - 20, 84, 12, mix(0x00070D1Cu, 0x00FFFFFFu, 36)); rrect(rx0 + 11, y + 1, rw - 22, 82, 11, 0x00080F1Eu);
          gtxt(rx0 + 20, y + 10, TR("Noch keine konsolidierten", "No consolidated memories yet."), C_MUTED, 1); gtxt(rx0 + 20, y + 28, TR("Erinnerungen. Klicke unten auf", "Click \"Sleep\" below to move"), C_MUTED, 1); gtxt(rx0 + 20, y + 46, TR("\"Schlafen\", um Synapsen ins", "active synapses into"), 0x00D8B4FEu, 1); gtxt(rx0 + 20, y + 64, TR("Langzeitgedaechtnis zu ueberfuehren.", "long-term memory."), C_MUTED, 1); } }
    { rrect(rx0 + 10, fcy, rw - 20, 68, 14, mix(0x00070D1Cu, Q_CY, 70)); rr_grad(rx0 + 11, fcy + 1, rw - 22, 66, 13, mix(0x00081428u, Q_CY, 30), mix(0x00081428u, Q_VIO, 40));
      gtxt(rx0 + 22, fcy + 8, TR("AKTIVER FOKUS", "ACTIVE FOCUS"), Q_CY, 1);
      if (c) { int deg = 0; for (int j = 0; j < ns2; j++) if (sa[j] == ged_fok || sb[j] == ged_fok) deg++; char t[60], a[12]; int w100 = er[ged_fok] / 10; if (w100 > 99) w100 = 99;
        gtxt(rx0 + 22, fcy + 26, nm[ged_fok], 0x00FFFFFFu, 1);
        scopy(t, TR("Gewichtung: 0.", "Weight: 0."), 60); if (w100 < 10) cat(t, "0", 60); dec(a, (uint32_t)w100); cat(t, a, 60); cat(t, " - ", 60); dec(a, (uint32_t)deg); cat(t, a, 60); cat(t, TR(" Synapsen", " synapses"), 60);
        gtxt(rx0 + 22, fcy + 45, t, C_MUTED, 1); }
      else gtxt(rx0 + 22, fcy + 30, "-", C_MUTED, 1); }
    int yb = y0 + bh - fh;                                                                           /* Fusszeile */
    if (Q_GLAS) { gfx_rect(x0, yb, cw, fh, 0x00050B18u); gfx_rect(x0, yb, cw, 1, mix(0x00050B18u, Q_SKY, 70)); }
    gtxt(x0 + 16, yb + 12, TR("Aus deinen Gespraechen lernt Zynos Verbindungen. Wichtiges bleibt, Unwichtiges verblasst.", "Zynos learns connections from your chats. Important things stay, unimportant ones fade."), C_MUTED, 1);
    { int bw1 = 226, bw2 = 224, bw3 = 164, bx0 = x0 + cw - 16 - bw1 - bw2 - bw3 - 16;
      ged_knopf(K_GED_SCHLAF, TR("Schlafen (Konsolidieren)", "Sleep (consolidate)"), bx0, yb + 40, bw1, 1);
      ged_knopf(K_GED_EXT, zdb_geti("ged.extern", 0) ? TR("Extern teilen: AN", "Share external: ON") : TR("Extern teilen: aus", "Share external: off"), bx0 + bw1 + 8, yb + 40, bw2, zdb_geti("ged.extern", 0) ? 2 : 0);
      ged_knopf(K_GED_VERG, ged_vfrage ? TR("Wirklich alles?", "Really all?") : TR("Alles vergessen", "Forget all"), bx0 + bw1 + bw2 + 16, yb + 40, bw3, 3); }
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
            int du = txt[i] == 'U'; char pre[12]; scopy(pre, du ? TR("Du: ", "You: ") : "Zynos: ", 12); int pl = slen(pre);
            char z[260]; scopy(z, pre, 260); for (int k = i + 2; k < e && slen(z) < 255; k++) { char c[2] = { txt[k], 0 }; cat(z, c, 260); }
            for (int p = 0; z[p] && nl < 150; ) { int len = slen(z + p); if (len > cols) { len = cols; int sp = len; while (sp > cols / 2 && z[p + sp] != ' ') sp--; if (z[p + sp] == ' ') len = sp; }
                for (int k = 0; k < len; k++) wl[nl][k] = z[p + k]; wl[nl][len] = 0; wr[nl] = (uint8_t)du; nl++; p += len; while (z[p] == ' ') p++; }
            (void)pl; i = txt[e] ? e + 1 : e;
        }
        int vis = (bh - 96) / 18; int von = nl > vis ? nl - vis : 0;
        for (int i = von; i < nl; i++) gtxt(rx, y0 + 84 + (i - von) * 18, wl[i], wr[i] ? C_ACC2 : C_TEXT, 1);
        if (!nl) gtxt(rx, y0 + 84, TR("Leer. Schreib unten in den Chat.", "Empty. Type in the chat below."), C_MUTED, 1);
    } else gtxt(rx, y0 + 60, TR("Schreib Zynos etwas, dann entsteht hier dein Verlauf.", "Write to Zynos and your history appears here."), C_MUTED, 1);
    gfx_end();
    kn_owner = ko;
}

static void intent_rechnen(const char* roh) {
    static const struct { const char* w; char c; } OPW[] = {                          /* Rechenwoerter in den fuenf Sprachen */
        { "geteilt durch", '/' }, { "dividiert durch", '/' }, { "divided by", '/' }, { "divise par", '/' }, { "diviso", '/' }, { "dividido por", '/' }, { "durch", '/' }, { "geteilt", '/' },
        { "multipliziert mit", '*' }, { "multiplied by", '*' }, { "mal", '*' }, { "times", '*' }, { "fois", '*' }, { "per", '*' }, { "por", '*' },
        { "plus", '+' }, { "und", '+' }, { "and", '+' }, { "mas", '+' }, { "piu", '+' }, { "minus", '-' }, { "weniger", '-' }, { "moins", '-' }, { "meno", '-' }, { "menos", '-' }, { 0, 0 } };
    char ex[96]; int k = 0; int n = slen(roh);
    for (int i = 0; i < n && k < 92; ) {
        char c = roh[i];
        if ((c >= '0' && c <= '9') || c == '+' || c == '-' || c == '*' || c == '/' || c == '(' || c == ')' || c == 'x' || c == 'X' || c == ':' || c == ' ' || c == '.' || c == ',') { ex[k++] = c; i++; continue; }
        int hit = 0;
        if (i == 0 || roh[i - 1] == ' ') for (int j = 0; OPW[j].w; j++) {
            int l = slen(OPW[j].w), ok = 1; for (int q = 0; q < l; q++) { char a = roh[i + q]; if (a >= 'A' && a <= 'Z') a = (char)(a + 32); if (a != OPW[j].w[q]) { ok = 0; break; } }
            if (ok && (roh[i + l] == ' ' || roh[i + l] == 0)) { ex[k++] = OPW[j].c; i += l; hit = 1; break; }
        }
        if (!hit) i++;                                                                  /* andere Woerter ("was ist") ueberspringen */
    }
    while (k > 0 && (ex[k - 1] == ' ' || ex[k - 1] == '.' || ex[k - 1] == ',' || ex[k - 1] == '?')) k--;     /* Satzzeichen am Ende */
    ex[k] = 0;
    char* es = ex; while (*es == ' ') es++;
    rp = es; rfehler = 0; int64_t v = r_ausdruck(); r_leer();
    if (*rp) rfehler = 1;
    if (rfehler == 2) { reply1(TR("Durch null kann man nicht teilen.", "You cannot divide by zero.")); return; }
    if (rfehler) { reply1(TR("Das konnte ich nicht ausrechnen. Beispiel: rechne (12+3)*7", "I could not work that out. Example: calc (12+3)*7")); return; }
    char o[110]; scopy(o, es, 60); cat(o, " = ", 110); char z[40]; i64dec(z, v); cat(o, z, 110);
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

/* Zynos antwortet mit dem Sprachmodell: jedes neue Textstueck erscheint sofort in der Sprechblase, der Mund bewegt sich */
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
    if (lang != 0) { scopy(fq, "Answer in ", 330); cat(fq, LENG[lang], 330); cat(fq, ". ", 330); cat(fq, frage, 330); } else { scopy(fq, "Antworte kurz auf Deutsch. ", 330); cat(fq, frage, 330); }
    busy_tick();
    static char gk[560], ef[800];
    int teilen = zdb_geti("ged.extern", 0) != 0;                              /* Erinnerungen gehen nur mit Erlaubnis an externe Anbieter */
    int gkl = ged_kontext(frage, gk, sizeof(gk), ext_quelle() ? teilen : 1);
    if (ext_quelle()) {
        ef[0] = 0; if (gkl) { scopy(ef, gk, 800); cat(ef, "Frage: ", 800); } cat(ef, fq, 800);
        int en = ext_frage(ef, roh, sizeof(roh));
        if (en > 0) { ki_teil(roh, en); ki_text[ki_len] = 0; ki_zeilen(); busy_ende(); sprech_bis = timer_get_ticks() + 100; kprint("[DESKTOP] "); kprint(ext_name(ext_quelle())); kprint(" antwortet: "); kprint(ki_text); kprint("\n"); return; }
        kprint("[DESKTOP] Externe KI fehlgeschlagen: "); kprint(roh); kprint(" - Zynos (intern) antwortet\n");
        if (!gehirn_vorhanden()) { busy_ende(); reply1(roh); return; }
        ki_len = 0;
        if (!gkl || !teilen) { gkl = ged_kontext(frage, gk, sizeof(gk), 1); }      /* Rueckfall auf die interne KI: lokal darf sie alles wissen */
    }
    gehirn_kontext(gkl ? gk : "");
    int n = gehirn_frage(frage, roh, sizeof(roh), ki_teil);
    busy_ende();
    if (n < 0) { reply1(TR("Mein Sprachmodell ist gerade nicht verfuegbar.", "My language model is not available right now.")); kprint("[DESKTOP] Zynos: Sprachmodell nicht verfuegbar: "); kprint(gehirn_status()); kprint("\n"); return; }
    ki_text[ki_len] = 0; ki_zeilen();
    sprech_bis = timer_get_ticks() + 100; kprint("[DESKTOP] Zynos antwortet: "); kprint(ki_text); kprint("\n");
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
    hist_add(roh); zlk_lerne(roh, -1); zlk_lerne("\n", 1); zlk_speichern();
    if (ged_sprache(s, roh)) { kprint("[DESKTOP] Gedaechtnis\n"); zeichne_antwort(); zeichne_eingabe(); return; }
    { static char fa[160]; if (in != I_FENSTER && fakt_antwort(roh, lang, fa, sizeof(fa))) { kprint("[DESKTOP] Fakt: "); kprint(fa); kprint("\n"); reply1(fa); zeichne_antwort(); zeichne_eingabe(); return; } }
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
        if (in_par == 40) { fenster_oeffnen(FT_TAB); return; }
        if (in_par == 41) { fenster_oeffnen(FT_PRAES); return; }
        if (in_par == 42) { fenster_oeffnen(FT_BOARD); return; }
        if (in_par == 43) { fenster_oeffnen(FT_MAL); return; }
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
        kprint("nicht verstanden\n"); antwort_setze(TR("Das habe ich noch nicht verstanden.", "I did not understand that yet."), TR("Sag zum Beispiel: text erstellen, rechne 12*7, system oder hilfe.", "Try: write a text, calc 12*7, system or help."), TR("Freie Fragen brauchen ein Sprachmodell. Es ist hier keins geladen (ISO mit KI verwenden).", "Free questions need a language model. None is loaded here (use the ISO with AI)."), 0, 0, 0); break;
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
        char g[100]; scopy(g, TR("Hallo ", "Hello "), 100); cat(g, benutzer_name(), 100); cat(g, TR("! Ich bin Zynos. Was moechtest du tun?", "! I am Zynos. What would you like to do?"), 100);
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
    benutzer_laden(); raum_laden(); fw_laden(); zdb_init(); th_laden(); ged_neu_laden();
    entsperr = 0; fpw[0] = 0; fmsg = 0; ffocus = 0; fname[0] = 0;
    if (benutzer_gesperrt()) zeichne_login();
    else { zustand = Z_DESK; einlen = 0; eingabe[0] = 0; reply1(TR("Festplatte entsperrt. Was moechtest du tun?", "Disk unlocked. What would you like to do?")); zeichne_desktop(); }
}

#include "autotext.inc"
#include "office.inc"

/* ---------- Rad-Schluessel: passwortlose Anmeldung ----------
 * Ein Rad aus 12 Symbolen. Das Geheimnis ist eine Folge von 6 Symbolen, jedes in einer von 3 Farben (Klick auf die Mitte
 * wechselt die Farbe). Nach jedem Griff mischt das Rad die Symbole neu und dreht sich: Mitschauen, Fingerspuren und
 * gemerkte Klickpositionen verraten nichts. Nach 3 Fehlversuchen waechst die Wartezeit. Das Passwort bleibt als Alternative. */
#define RW_N 6
static char be_msg[100];
static int rw_aktiv, rw_zweck, rw_n, rw_tint, rw_perm[12], rw_rot, rw_runde; static char rw_code[16], rw_erst[16], rw_ziel[16], rw_msg[100];
static void anmeld_fertig(void) {
    kprint("[DESKTOP] Willkommen, "); kprint(benutzer_name()); kprint(" ("); kprint(benutzer_rollentext(benutzer_rolle())); kprint(")\n");
    zustand = Z_DESK; einlen = 0; eingabe[0] = 0;
    char g[100]; scopy(g, TR("Hallo ", "Hello "), 100); cat(g, benutzer_name(), 100); cat(g, TR("! Ich bin Zynos. Was moechtest du tun?", "! I am Zynos. What would you like to do?"), 100);
    reply1(g); zeichne_desktop();
}
static void rw_mischen(void) {
    uint8_t r[13]; krand(r, 13); for (int i = 0; i < 12; i++) rw_perm[i] = i;
    for (int i = 11; i > 0; i--) { int j = r[i] % (i + 1), t = rw_perm[i]; rw_perm[i] = rw_perm[j]; rw_perm[j] = t; }
    rw_rot = r[12] % 30;
}
static void rw_zeichne(void) {
    static const uint32_t tc[3] = { Q_CY, 0x00C084FCu, Q_ROSA };
    thema_anwenden(0); gfx_begin(); gfx_rect(0, 0, W, H, C_SPL);
    int cx = W / 2, cy = H / 2 + 6, R = H / 2 - 120; if (R > 190) R = 190; if (R < 110) R = 110; int s = R * 33 / 100;
    char t[100]; scopy(t, rw_zweck == 1 ? TR("Rad-Schluessel: ", "Wheel key: ") : TR("Rad-Schluessel einrichten: ", "Set up wheel key: "), 100); cat(t, rw_ziel, 100);
    gtxt(cx - gtw(t, 1) / 2, cy - R - s / 2 - 46, t, 0x00FFFFFFu, 1);
    { const char* h = rw_zweck == 2 && rw_runde == 1 ? TR("Zur Bestaetigung dieselbe Folge noch einmal.", "Enter the same sequence once more to confirm.") : TR("Waehle 6 Symbole. Klick auf die Mitte wechselt die Farbe.", "Pick 6 symbols. Click the centre to change colour."); gtxt(cx - gtw(h, 1) / 2, cy - R - s / 2 - 24, h, C_MUTED, 1); }
    for (int k = 0; k < 12; k++) {
        int d = -90 + k * 30 + rw_rot, x = cx + R * icos(d) / 1024, y = cy + R * isin(d) / 1024;
        rrect(x - s / 2 - 3, y - s / 2 - 3, s + 6, s + 6, s * 26 / 100 + 3, mix(C_SPL, tc[rw_tint], 150));
        fl_icon(rw_perm[k], x - s / 2, y - s / 2, s, 0);
    }
    rrect(cx - 74, cy - 74, 148, 148, 74, mix(C_SPL, tc[rw_tint], 90)); fl_hub(cx, cy, 58, 0);
    for (int i = 0; i < RW_N; i++) { int dx = cx - (RW_N * 18) / 2 + i * 18 + 9, dy = cy + 96; rrect(dx - 5, dy - 5, 10, 10, 5, i < rw_n ? tc[rw_tint] : mix(C_SPL, C_MUTED, 90)); }
    int sek = rw_zweck == 1 ? benutzer_rad_sperre() : 0;
    if (sek > 0) { char m[80], a[12]; dec(a, (uint32_t)sek); scopy(m, TR("Gesperrt, noch ", "Locked, "), 80); cat(m, a, 80); cat(m, TR(" s", " s left"), 80); gtxt(cx - gtw(m, 1) / 2, cy + R + s / 2 + 18, m, C_ERR, 1); }
    else if (rw_msg[0]) gtxt(cx - gtw(rw_msg, 1) / 2, cy + R + s / 2 + 18, rw_msg, rw_msg[0] == 'F' ? C_ERR : Q_CY, 1);
    knopf(0, TR("Zurueck", "Back"), cx - 170, H - 66, 150, 0); knopf(0, TR("Neu beginnen", "Start over"), cx + 20, H - 66, 150, 0);
    gfx_end(); hv_status_zeichnen();
}
static void rw_start(int zweck, const char* name) {
    rw_aktiv = 1; rw_zweck = zweck; rw_n = 0; rw_tint = 0; rw_runde = 0; rw_msg[0] = 0; scopy(rw_ziel, name, 16); rw_code[0] = 0; rw_mischen(); rw_zeichne();
}
static void rw_ende(void) { rw_aktiv = 0; rw_code[0] = rw_erst[0] = 0; if (zustand == Z_LOGIN) zeichne_login(); else zeichne_desktop(); }
static void rw_pick(int icon) {
    if (rw_zweck == 1 && benutzer_rad_sperre() > 0) { rw_zeichne(); return; }
    rw_code[rw_n * 2] = (char)('a' + icon); rw_code[rw_n * 2 + 1] = (char)('0' + rw_tint); rw_n++; rw_code[rw_n * 2] = 0; rw_tint = 0; rw_msg[0] = 0;
    if (rw_n < RW_N) { rw_mischen(); rw_zeichne(); return; }
    if (rw_zweck == 1) {
        if (benutzer_rad_login(rw_ziel, rw_code) == 0) { rw_aktiv = 0; rw_code[0] = 0; anmeld_fertig(); return; }
        kprint("[DESKTOP] Rad-Anmeldung fehlgeschlagen\n"); scopy(rw_msg, TR("Falscher Rad-Schluessel.", "Wrong wheel key."), 100);
    } else if (rw_runde == 0) { scopy(rw_erst, rw_code, 16); rw_runde = 1; }
    else {
        int gleich = ueq(rw_erst, rw_code);
        if (gleich && benutzer_rad_setzen(rw_ziel, rw_code) == 0) { kprint("[DESKTOP] Rad-Schluessel gesetzt: "); kprint(rw_ziel); kprint("\n"); scopy(be_msg, TR("Rad-Schluessel gespeichert.", "Wheel key saved."), 100); rw_ende(); return; }
        scopy(rw_msg, gleich ? TR("Fehler beim Speichern.", "Could not save.") : TR("Die Folgen waren verschieden. Nochmal von vorn.", "The sequences differed. Start again."), 100); rw_runde = 0;
    }
    rw_n = 0; rw_code[0] = 0; rw_tint = 0; rw_mischen(); rw_zeichne();
}
static void rw_klick(int mx, int my) {
    int cx = W / 2, cy = H / 2 + 6, R = H / 2 - 120; if (R > 190) R = 190; if (R < 110) R = 110; int s = R * 33 / 100;
    if (my >= H - 66 && my < H - 26) {
        if (mx >= cx - 170 && mx < cx - 20) { rw_ende(); return; }
        if (mx >= cx + 20 && mx < cx + 170) { rw_n = 0; rw_code[0] = 0; rw_tint = 0; rw_runde = 0; rw_msg[0] = 0; rw_mischen(); rw_zeichne(); return; }
    }
    if ((mx - cx) * (mx - cx) + (my - cy) * (my - cy) < 70 * 70) { rw_tint = (rw_tint + 1) % 3; rw_zeichne(); return; }
    for (int k = 0; k < 12; k++) { int d = -90 + k * 30 + rw_rot, x = cx + R * icos(d) / 1024, y = cy + R * isin(d) / 1024; if (mx >= x - s / 2 && mx < x + s / 2 && my >= y - s / 2 && my < y + s / 2) { rw_pick(rw_perm[k]); return; } }
}
static void lg_rad_start(void) {
    if (!fname[0]) { scopy(lg_info, TR("Erst den Benutzernamen eintippen.", "Type the user name first."), 80); zeichne_login(); return; }
    if (!benutzer_rad_hat(fname)) { scopy(lg_info, TR("Dieses Konto hat keinen Rad-Schluessel.", "This account has no wheel key."), 80); zeichne_login(); return; }
    lg_info[0] = 0; rw_start(1, fname);
}
static void lg_klick(int mx, int my) { int cw = 360, cx = (W - cw) / 2, y = H / 2 - 250 + zynex_logo_h + 24 + 222; if (mx >= cx && mx < cx + cw && my >= y && my < y + 40) lg_rad_start(); }

/* ---------- Datei-Explorer (SynFS + Raeume/Schluessel) ---------- */
enum { K_DA0 = 1000, K_DA_OEFF = 1020, K_DA_DEL, K_DA_UP, K_DA_DN, K_DA_NEU, K_DA_MOVE, K_DA_KEY, K_DA_HOCH, K_DA_F0 = 1030, K_DA_R0 = 1040, K_DA_SICHT = 1055, K_DA_RDEL, K_DA_ZURUECK, K_DA_S0 = 1060, K_DA_END = 1070 };
static int da_filter, da_sel, da_scroll, da_vfrage, da_raum, da_modus, da_mv; static char da_buf[24], da_msg[100], da_mvname[32];
static const char* const DA_ALLE[] = { "", 0 }, * const DA_TXT[] = { ".txt", 0 }, * const DA_BILD[] = { ".png", ".jpg", ".jpeg", ".gif", 0 };
static const char* const* da_endungen(void) { return da_filter == 1 ? DA_TXT : da_filter == 2 ? DA_BILD : DA_ALLE; }
static int da_st(int r) { return raum_stufe(r, benutzer_name(), benutzer_rolle()); }
static int da_nsub(void) { int n = 0; while (raum_unter(da_raum, n, benutzer_name(), benutzer_rolle()) >= 0) n++; return n; }
static int da_zahl(void) { return da_nsub() + datei_zahl(da_raum, da_endungen()); }
static const char* da_stufetxt(int s) { return s >= 3 ? TR("Verwalten", "Manage") : s == 2 ? TR("Mitarbeiten", "Contribute") : s == 1 ? TR("Sehen", "View") : TR("Gesperrt", "Locked"); }
static uint32_t da_stufefarbe(int s) { return s >= 3 ? Q_VIO : s == 2 ? Q_CY : s == 1 ? Q_SKY : 0x00F43F5Eu; }
static int da_ist_bild(const char* n) { int l = slen(n); if (l < 4) return 0; const char* e = n + l - 4; return (e[0] == '.' && ((e[1] == 'p' && e[2] == 'n' && e[3] == 'g') || (e[1] == 'j' && e[2] == 'p' && e[3] == 'g') || (e[1] == 'g' && e[2] == 'i' && e[3] == 'f'))) || (l > 5 && n[l - 5] == '.' && n[l - 4] == 'j' && n[l - 3] == 'p' && n[l - 2] == 'e' && n[l - 1] == 'g'); }
static int da_ist_txt(const char* n) { int l = slen(n); return l > 4 && n[l - 4] == '.' && n[l - 3] == 't' && n[l - 2] == 'x' && n[l - 1] == 't'; }
static int da_baum(int* id, int* tief, int max) {               /* Raumbaum (Tiefensuche) fuer die Seitenleiste */
    int n = 0, st[6], si[6], sp = 0; id[n] = 0; tief[n++] = 0; st[0] = 0; si[0] = 0; sp = 1;
    while (sp > 0 && n < max) {
        int p = st[sp - 1], k = raum_unter(p, si[sp - 1]++, benutzer_name(), benutzer_rolle());
        if (k < 0) { sp--; continue; }
        id[n] = k; tief[n++] = sp; if (sp < 5) { st[sp] = k; si[sp] = 0; sp++; }
    }
    return n;
}
static void da_msg_set(const char* de, const char* en) { scopy(da_msg, TR(de, en), 100); }
static void da_klick(int id) {
    char nm[32]; int ns = da_nsub(), sel_raum = da_sel < ns ? raum_unter(da_raum, da_sel, benutzer_name(), benutzer_rolle()) : -1, st = da_st(da_raum);
    int has_file = da_sel >= ns && datei_nth(da_raum, da_endungen(), da_sel - ns, nm, 32);
    if (id != K_DA_DEL && id != K_DA_RDEL) da_vfrage = 0;
    if (id >= K_DA0 && id < K_DA0 + 16) { da_sel = da_scroll + (id - K_DA0); da_msg[0] = 0; }
    else if (id >= K_DA_F0 && id < K_DA_F0 + 3) { da_filter = id - K_DA_F0; da_sel = 0; da_scroll = 0; }
    else if (id >= K_DA_R0 && id < K_DA_R0 + 14) { int ids[14], td[14]; int n = da_baum(ids, td, 14); if (id - K_DA_R0 < n) { da_raum = ids[id - K_DA_R0]; da_sel = 0; da_scroll = 0; da_modus = 0; da_msg[0] = 0; } }
    else if (id == K_DA_UP) { if (da_scroll > 0) da_scroll--; }
    else if (id == K_DA_DN) { if (da_scroll + 1 < da_zahl()) da_scroll++; }
    else if (id == K_DA_HOCH) { int p = raum_parent(da_raum); if (p >= 0) { da_raum = p; da_sel = 0; da_scroll = 0; da_modus = 0; } }
    else if (id == K_DA_OEFF) {
        if (sel_raum >= 0) { da_raum = sel_raum; da_sel = 0; da_scroll = 0; da_msg[0] = 0; }
        else if (has_file) {
            kprint("[DESKTOP] Explorer oeffnet: "); kprint(nm); kprint("\n");
            if (da_ist_txt(nm)) { editor_oeffnen(); dok_laden(nm); }
            else if (da_ist_bild(nm)) { scopy(hg_datei, nm, 32); zdb_put("ui.hgbild", hg_datei); hg_nr = 5; th_speichern(); }
            else reply1(TR("Diese Dateiart kann ich noch nicht oeffnen.", "I cannot open this file type yet."));
        }
    }
    else if (id == K_DA_DEL) {
        if (sel_raum >= 0) {
            if (!da_vfrage) da_vfrage = 1;
            else { int e = raum_loeschen(sel_raum, benutzer_name(), benutzer_rolle()); da_vfrage = 0; if (e == 0) { da_msg_set("Raum geloescht.", "Room deleted."); if (da_sel > 0) da_sel--; } else if (e == -2) da_msg_set("Dafuer fehlt dir der Schluessel (Verwalten).", "You need the Manage key for that."); else da_msg_set("Raum ist nicht leer.", "Room is not empty."); }
        } else if (has_file) {
            if (st < ST_MITARB) { da_msg_set("Dafuer fehlt dir die Stufe Mitarbeiten.", "You need the Contribute level."); da_vfrage = 0; }
            else if (da_vfrage) { synfs_delete(nm); datei_vergessen(nm); kprint("[DESKTOP] Explorer geloescht: "); kprint(nm); kprint("\n"); da_vfrage = 0; if (da_sel > 0) da_sel--; da_msg[0] = 0; } else da_vfrage = 1;
        }
    }
    else if (id == K_DA_NEU) { if (st < ST_MITARB) da_msg_set("In diesem Raum darfst du nichts anlegen.", "You may not create anything here."); else { da_modus = 1; da_buf[0] = 0; da_msg[0] = 0; } }
    else if (id == K_DA_MOVE) {
        if (da_mv) { if (st < ST_MITARB) da_msg_set("Hier darfst du nichts ablegen.", "You may not put anything here."); else { datei_zuordnen(da_mvname, da_raum); da_mv = 0; da_msg_set("Verschoben.", "Moved."); } }
        else if (has_file) { if (st < ST_MITARB) da_msg_set("Dafuer fehlt dir die Stufe Mitarbeiten.", "You need the Contribute level."); else { da_mv = 1; scopy(da_mvname, nm, 32); da_msg_set("Gehe in den Zielraum und waehle \"Hierher\".", "Go to the target room and choose \"Here\"."); } }
    }
    else if (id == K_DA_KEY) { if (st < ST_VERW) da_msg_set("Schluessel verwalten darf nur, wer diesen Raum verwaltet.", "Only managers of this room can edit keys."); else { da_modus = da_modus == 2 ? 0 : 2; da_msg[0] = 0; } }
    else if (id == K_DA_ZURUECK) da_modus = 0;
    else if (id == K_DA_SICHT && st >= ST_VERW) raum_sicht_setzen(da_raum, (raum_sicht(da_raum) + 1) % 3);
    else if (id == K_DA_RDEL && da_raum > 0 && st >= ST_VERW) {
        if (!da_vfrage) da_vfrage = 1;
        else { int p = raum_parent(da_raum), e = raum_loeschen(da_raum, benutzer_name(), benutzer_rolle()); da_vfrage = 0; if (e == 0) { da_raum = p < 0 ? 0 : p; da_modus = 0; da_sel = 0; da_scroll = 0; da_msg_set("Raum geloescht.", "Room deleted."); } else da_msg_set("Raum ist nicht leer.", "Room is not empty."); }
    }
    else if (id >= K_DA_S0 && id < K_DA_S0 + 8 && st >= ST_VERW) {
        char un[16]; int ur; if (benutzer_nth(id - K_DA_S0, un, &ur)) {
            int k = raum_schluessel(da_raum, un); int nx = k < 0 ? ST_SEHEN : k == ST_SEHEN ? ST_MITARB : k == ST_MITARB ? ST_VERW : k == ST_VERW ? ST_GESPERRT : -1;
            raum_schluessel_setzen(da_raum, un, nx);
        }
    }
    zeichne_desktop();
}
static void da_taste(uint8_t k, int shift, int altgr) {
    int n = slen(da_buf);
    if (k == 0x29) { da_modus = 0; da_buf[0] = 0; }
    else if (k == 0x28 || k == 0x58) {
        if (n) { int e = raum_neu(da_raum, da_buf, benutzer_name(), benutzer_rolle());
            if (e > 0) { kprint("[DESKTOP] Raum angelegt: "); kprint(da_buf); kprint("\n"); da_msg_set("Raum angelegt (privat). Mit \"Schluessel\" teilen.", "Room created (private). Share it with \"Keys\"."); }
            else if (e == -3) da_msg_set("Diesen Raum gibt es hier schon.", "This room already exists."); else if (e == -2) da_msg_set("Keine freien Raeume mehr.", "No free rooms left."); else if (e == -4) da_msg_set("Dafuer fehlt dir die Stufe Mitarbeiten.", "You need the Contribute level."); else da_msg_set("Ungueltiger Name.", "Invalid name."); }
        da_buf[0] = 0; da_modus = 0;
    }
    else if (k == 0x2A) { if (n) da_buf[n - 1] = 0; }
    else { char c = usb_key_char(k, shift, altgr); if (c >= 32 && c < 127 && c != '/' && c != ':' && n < 18) { da_buf[n] = c; da_buf[n + 1] = 0; } else return; }
    zeichne_desktop();
}

/* ---------- Benutzerverwaltung ---------- */
enum { K_BE0 = 950, K_BE_NEU = 960, K_BE_EDIT, K_BE_PW, K_BE_FOTO, K_BE_DEL, K_BE_ROLLE, K_BE_RAD, K_BE_RADX, K_BE_FO0 = 970, K_BE_END = 990 };
static int be_sel, be_modus, be_schritt, be_rolle_neu = 1, be_vfrage; static char be_buf[40], be_name1[20];
static char av_name[8][16]; static uint32_t av_px[8][64 * 64]; static int av_ok[8];
static const char* be_rolletxt(int r) { return r == 2 ? TR("Admin", "Admin") : r == 1 ? TR("Benutzer", "User") : TR("Gast", "Guest"); }
static int be_admin(void) { return benutzer_rolle() == ROLLE_ADMIN; }
static void be_fotokey(const char* n, char* k) { scopy(k, "bf.", 24); cat(k, n, 24); }
static void be_av_weg(const char* n) { for (int i = 0; i < 8; i++) if (ueq(av_name[i], n)) { av_name[i][0] = 0; av_ok[i] = 0; } }
static int be_av(const char* n) {                              /* Cache-Platz mit geladenem Foto, sonst -1 */
    char k[24]; be_fotokey(n, k); const char* f = zdb_get(k); if (!f || !f[0]) return -1;
    for (int i = 0; i < 8; i++) if (av_ok[i] && ueq(av_name[i], n)) return i;
    int fr = -1; for (int i = 0; i < 8; i++) if (!av_ok[i]) { fr = i; break; } if (fr < 0) fr = 0;
    if (av_laden(f, av_px[fr], 64) != 0) { scopy(av_name[fr], n, 16); av_ok[fr] = 0; return -1; }
    scopy(av_name[fr], n, 16); av_ok[fr] = 1; return fr;
}
static void be_avatar(int cx, int cy, int D, const char* n, uint32_t c1, uint32_t c2) {
    int sl = be_av(n); int r = D / 2;
    for (int y = 0; y < D; y++) {
        int dy = y - r, w2 = isq(r * r - dy * dy); if (w2 <= 0) continue;
        if (sl >= 0) { for (int x = r - w2; x < r + w2; x++) gfx_rect(cx - r + x, cy - r + y, 1, 1, av_px[sl][(y * 64 / D) * 64 + x * 64 / D]); }
        else gfx_rect(cx - w2, cy - r + y, 2 * w2, 1, mix(c1, c2, y * 256 / D));
    }
    if (sl < 0) { char t[2] = { n[0] >= 'a' && n[0] <= 'z' ? (char)(n[0] - 32) : n[0], 0 }; int sc = D >= 80 ? 3 : 2; gtxt(cx - gtw(t, sc) / 2, cy - 8 * sc, t, 0x00FFFFFFu, sc); }
}
static int be_name_at(int i, char* n, int* r) { return benutzer_nth(i, n, r); }
static void be_fehler(int e) {
    scopy(be_msg, e == -1 ? TR("Name: nur a-z, 0-9, _ - . (max. 15)", "Name: only a-z, 0-9, _ - . (max 15)") : e == -2 ? TR("Name vergeben oder unbekannt.", "Name taken or unknown.") : e == -3 ? TR("Maximal 8 Konten.", "Maximum 8 accounts.") : e == -5 ? TR("Passwort: mindestens 4 Zeichen.", "Password: at least 4 characters.") : e == -6 ? TR("Der letzte Admin bleibt.", "The last admin stays.") : TR("Fehler beim Speichern.", "Save failed."), 100);
}
static void be_taste(uint8_t k, int shift, int altgr) {
    int n = slen(be_buf); char sn[16]; int sr; int have = be_name_at(be_sel, sn, &sr);
    if (k == 0x29) { be_modus = 0; be_buf[0] = 0; be_msg[0] = 0; }
    else if (k == 0x28 || k == 0x58) {
        int e = 0;
        if (be_modus == 1 && be_schritt == 0) { if (!n) return; scopy(be_name1, be_buf, 20); be_buf[0] = 0; be_schritt = 1; scopy(be_msg, TR("Passwort eingeben, dann Enter.", "Enter a password, then Enter."), 100); zeichne_desktop(); return; }
        if (be_modus == 1) { e = benutzer_neu(be_name1, be_buf, be_rolle_neu); if (!e) { kprint("[DESKTOP] Benutzer angelegt: "); kprint(be_name1); kprint("\n"); scopy(be_msg, TR("Benutzer angelegt.", "User created."), 100); } else be_fehler(e); }
        else if (be_modus == 2 && have) { e = benutzer_umbenennen(sn, be_buf); if (!e) { char a[24], b[24]; be_fotokey(sn, a); be_fotokey(be_buf, b); const char* f = zdb_get(a); if (f && f[0] && !ueq(sn, be_buf)) { zdb_put(b, f); zdb_put(a, ""); } be_av_weg(sn); kprint("[DESKTOP] Benutzer umbenannt: "); kprint(be_buf); kprint("\n"); scopy(be_msg, TR("Gespeichert.", "Saved."), 100); } else be_fehler(e); }
        else if (be_modus == 3 && have) { e = benutzer_passwort(sn, be_buf); if (!e) { kprint("[DESKTOP] Passwort geaendert: "); kprint(sn); kprint("\n"); scopy(be_msg, TR("Passwort geaendert.", "Password changed."), 100); } else be_fehler(e); }
        { int i = 0; while (be_buf[i]) be_buf[i++] = 0; } be_modus = 0; be_schritt = 0;
    }
    else if (k == 0x2A) { if (n) be_buf[n - 1] = 0; }
    else { char c = usb_key_char(k, shift, altgr); if (be_modus != 3 && !(be_modus == 1 && be_schritt == 1) && c >= 'A' && c <= 'Z') c = (char)(c + 32); if (c >= 33 && c < 127 && n < 38) { be_buf[n] = c; be_buf[n + 1] = 0; } else return; }
    zeichne_desktop();
}
static void be_klick(int id) {
    char sn[16]; int sr = 0; int anz = 0; { char t[16]; while (be_name_at(anz, t, 0)) anz++; }
    int have = be_name_at(be_sel, sn, &sr); int admin = be_admin(); int selbst = have && ueq(sn, benutzer_name());
    be_msg[0] = 0;
    if (id >= K_BE0 && id < K_BE0 + 8) { if (id - K_BE0 < anz) be_sel = id - K_BE0; be_modus = 0; be_vfrage = 0; }
    else if (id == K_BE_NEU) { if (!admin) scopy(be_msg, TR("Nur Admins legen Konten an.", "Only admins create accounts."), 100); else { be_modus = 1; be_schritt = 0; be_buf[0] = 0; scopy(be_msg, TR("Name eingeben, dann Enter.", "Enter a name, then Enter."), 100); } be_vfrage = 0; }
    else if (id == K_BE_EDIT && have) { if (!admin) scopy(be_msg, TR("Nur Admins bearbeiten Konten.", "Only admins edit accounts."), 100); else { be_modus = 2; scopy(be_buf, sn, 40); } be_vfrage = 0; }
    else if (id == K_BE_ROLLE) { if (be_modus == 1 || !have) be_rolle_neu = (be_rolle_neu + 1) % 3; else if (!admin) scopy(be_msg, TR("Nur Admins aendern Rollen.", "Only admins change roles."), 100); else { int e = benutzer_rolle_setzen(sn, (sr + 1) % 3); if (e) be_fehler(e); else kprint("[DESKTOP] Benutzerrolle geaendert\n"); } be_vfrage = 0; }
    else if (id == K_BE_PW && have) { if (!admin && !selbst) scopy(be_msg, TR("Nur das eigene Passwort.", "Only your own password."), 100); else { be_modus = 3; be_buf[0] = 0; scopy(be_msg, TR("Neues Passwort, dann Enter.", "New password, then Enter."), 100); } be_vfrage = 0; }
    else if (id == K_BE_RAD && have) { if (!admin && !selbst) scopy(be_msg, TR("Nur der eigene Rad-Schluessel.", "Only your own wheel key."), 100); else { be_modus = 0; rw_start(2, sn); return; } }
    else if (id == K_BE_RADX && have) { if (!admin && !selbst) scopy(be_msg, TR("Nur der eigene Rad-Schluessel.", "Only your own wheel key."), 100); else { benutzer_rad_entfernen(sn); scopy(be_msg, TR("Rad-Schluessel entfernt.", "Wheel key removed."), 100); } }
    else if (id == K_BE_FOTO && have) { if (!admin && !selbst) scopy(be_msg, TR("Nur das eigene Foto.", "Only your own photo."), 100); else be_modus = be_modus == 4 ? 0 : 4; be_vfrage = 0; }
    else if (id >= K_BE_FO0 && id < K_BE_FO0 + 12 && have && be_modus == 4) {
        char nm[32]; if (id == K_BE_FO0 + 11) { char k[24]; be_fotokey(sn, k); zdb_put(k, ""); be_av_weg(sn); be_modus = 0; scopy(be_msg, TR("Foto entfernt.", "Photo removed."), 100); }
        else if (synfs_nte_datei(id - K_BE_FO0, DA_BILD, nm, 32)) { char k[24]; be_fotokey(sn, k); zdb_put(k, nm); be_av_weg(sn); kprint("[DESKTOP] Benutzerfoto gesetzt: "); kprint(sn); kprint("\n"); be_modus = 0; if (be_av(sn) < 0) scopy(be_msg, TR("Bild nicht lesbar.", "Image not readable."), 100); }
    }
    else if (id == K_BE_DEL && have) {
        if (!admin) scopy(be_msg, TR("Nur Admins loeschen Konten.", "Only admins delete accounts."), 100);
        else if (!be_vfrage) be_vfrage = 1;
        else { int e = benutzer_loeschen(sn); be_vfrage = 0; if (e) be_fehler(e); else { char k[24]; be_fotokey(sn, k); zdb_put(k, ""); be_av_weg(sn); kprint("[DESKTOP] Benutzer geloescht: "); kprint(sn); kprint("\n"); if (be_sel > 0) be_sel--; if (!benutzer_name()[0]) { zeichne_desktop(); return; } } }
    }
    zeichne_desktop();
}
static void zeichne_benutzer(void) {
    int wi = fenster_idx(FT_BENUTZER); if (wi < 0) return;
    struct fenster* f = &fen[wi]; int ko = kn_owner; kn_owner = wi; kn_loesche_owner(wi);
    int x0 = f->x + 2, y0 = f->y + WT_H, bh = f->h - WT_H - 4, lw = 250;
    gfx_begin(); gfx_rect(x0, y0, f->w - 4, bh, C_BG);
    int anz = 0; { char t[16]; while (be_name_at(anz, t, 0)) anz++; }
    if (be_sel >= anz) be_sel = anz ? anz - 1 : 0;
    char sn[16] = ""; int sr = 0; int have = be_name_at(be_sel, sn, &sr);
    for (int i = 0; i < anz && i < 8; i++) {
        char nm[16]; int r = 0; be_name_at(i, nm, &r); int ry = y0 + 12 + i * 52, on = i == be_sel;
        if (on) { rrect(x0 + 8, ry, lw, 46, 14, mix(C_BG, Q_CY, 120)); if (Q_GLAS) h_pille(x0 + 9, ry + 1, lw - 2, 44, 0x00083A55u, 0x002A1058u); else rrect(x0 + 9, ry + 1, lw - 2, 44, 13, mix(C_CARD, C_ACC, 90)); }
        else rrect_rand(x0 + 8, ry, lw, 46, 14, C_CARD, C_BORDER);
        be_avatar(x0 + 34, ry + 23, 34, nm, Q_SKY, Q_VIO);
        gtxt(x0 + 60, ry + 6, nm, on ? 0x00FFFFFFu : C_TEXT, 1); gtxt(x0 + 60, ry + 24, be_rolletxt(r), on ? 0x00B8E4FFu : C_MUTED, 1);
        if (ueq(nm, benutzer_name())) { int tw = gtw(TR("du", "you"), 1) + 14; rrect(x0 + lw - tw - 2, ry + 12, tw, 20, 10, mix(C_BG, Q_CY, 100)); gtxt(x0 + lw - tw + 5, ry + 14, TR("du", "you"), 0x00FFFFFFu, 1); }
        kn_reg(K_BE0 + i, x0 + 8, ry, lw, 46);
    }
    if (!anz) gtxt(x0 + 20, y0 + 24, TR("Noch keine Konten.", "No accounts yet."), C_MUTED, 1);
    int rx = x0 + lw + 24, rw = f->w - 4 - lw - 34;
    if (Q_GLAS) { rrect(rx, y0 + 12, rw, 150, 18, mix(C_BG, Q_SKY, 60)); rr_grad(rx + 1, y0 + 13, rw - 2, 148, 17, 0x00081A33u, 0x00050B1Cu); } else rrect_rand(rx, y0 + 12, rw, 150, 18, C_CARD, C_BORDER);
    if (have) {
        be_avatar(rx + 70, y0 + 87, 96, sn, Q_SKY, Q_VIO);
        gtxt(rx + 140, y0 + 50, sn, C_TEXT, 2);
        { const char* rt = be_rolletxt(sr); int tw = gtw(rt, 1) + 20; h_pille(rx + 140, y0 + 90, tw, 24, mix(0x00062033u, Q_CY, 90), mix(0x001A0B3Au, Q_VIO, 120)); gtxt(rx + 150, y0 + 94, rt, 0x00FFFFFFu, 1); }
        { char k[24]; be_fotokey(sn, k); const char* fo = zdb_get(k); gtxt(rx + 140, y0 + 124, fo && fo[0] ? fo : TR("kein Foto", "no photo"), C_MUTED, 1); }
    }
    int by = y0 + 176, bw = (rw - 16) / 3;
    knopf(K_BE_NEU, TR("Neuer Benutzer", "New user"), rx, by, bw, 1);
    knopf(K_BE_EDIT, TR("Bearbeiten", "Edit"), rx + bw + 8, by, bw, 0);
    knopf(K_BE_PW, TR("Passwort aendern", "Change password"), rx + 2 * (bw + 8), by, bw, 0);
    knopf(K_BE_FOTO, TR("Foto waehlen", "Choose photo"), rx, by + 42, bw, be_modus == 4);
    { char t[40]; scopy(t, TR("Rolle: ", "Role: "), 40); cat(t, be_rolletxt(be_modus == 1 ? be_rolle_neu : sr), 40); knopf(K_BE_ROLLE, t, rx + bw + 8, by + 42, bw, 0); }
    knopf(K_BE_DEL, be_vfrage ? TR("Wirklich loeschen?", "Really delete?") : TR("Loeschen", "Delete"), rx + 2 * (bw + 8), by + 42, bw, be_vfrage);
    { int hr = have && benutzer_rad_hat(sn); knopf(K_BE_RAD, hr ? TR("Rad-Schluessel neu", "New wheel key") : TR("Rad-Schluessel einrichten", "Set up wheel key"), rx, by + 84, bw + bw / 2, 0); if (hr) knopf(K_BE_RADX, TR("Rad-Schluessel entfernen", "Remove wheel key"), rx + bw + bw / 2 + 8, by + 84, bw + bw / 2, 0); }
    int ey = by + 136;
    if (be_modus >= 1 && be_modus <= 3) {
        const char* lb = be_modus == 1 ? (be_schritt == 0 ? TR("Neuer Name", "New name") : TR("Passwort", "Password")) : be_modus == 2 ? TR("Name aendern", "Rename") : TR("Neues Passwort", "New password");
        gtxt(rx, ey, lb, C_MUTED, 1);
        rrect_rand(rx, ey + 22, rw, 40, 10, C_FIELD, C_ACC);
        char t[44]; int n = slen(be_buf); if (n > 36) n = 36; int mask = be_modus == 3 || (be_modus == 1 && be_schritt == 1);
        for (int i = 0; i < n; i++) t[i] = mask ? '*' : be_buf[i]; t[n] = 0;
        gfx_text(rx + 14, ey + 34, t, C_TEXT, 1); gfx_rect(rx + 14 + n * 8, ey + 32, 2, 20, C_TEXT);
        gtxt(rx, ey + 70, TR("Enter = uebernehmen, Esc = abbrechen", "Enter = apply, Esc = cancel"), C_MUTED, 1);
    } else if (be_modus == 4) {
        gtxt(rx, ey, TR("Bild als Profilfoto waehlen:", "Choose an image as profile photo:"), C_MUTED, 1);
        char nm[32]; int rows = 0;
        for (int i = 0; i < 10 && synfs_nte_datei(i, DA_BILD, nm, 32); i++) {
            int ry = ey + 22 + i * 26; rrect_rand(rx, ry, rw, 24, 8, C_CARD, C_BORDER); gtxt(rx + 12, ry + 4, nm, C_TEXT, 1); kn_reg(K_BE_FO0 + i, rx, ry, rw, 24); rows++;
        }
        if (!rows) gtxt(rx, ey + 24, TR("Keine Bilder in SynFS (png/jpg/gif).", "No images in SynFS (png/jpg/gif)."), C_MUTED, 1);
        int ry = ey + 22 + 10 * 26 > y0 + bh - 60 ? y0 + bh - 60 : ey + 22 + 10 * 26; (void)ry;
        knopf(K_BE_FO0 + 11, TR("Foto entfernen", "Remove photo"), rx + rw - 170, y0 + bh - 46, 170, 0);
    }
    if (be_msg[0]) gtxt(rx, y0 + bh - 30, be_msg, mix(C_MUTED, Q_CY, 140), 1);
    gfx_end(); kn_owner = ko;
}
static void da_ordner(int x, int y, uint32_t c) { rrect(x, y + 1, 9, 5, 2, c); rrect(x, y + 4, 20, 14, 4, c); gfx_rect(x + 3, y + 8, 14, 1, mix(c, 0x00FFFFFFu, 150)); }
static void zeichne_datei(void) {
    int wi = fenster_idx(FT_DATEI); if (wi < 0) return;
    struct fenster* f = &fen[wi]; int ko = kn_owner; kn_owner = wi; kn_loesche_owner(wi);
    int x0 = f->x + 2, y0 = f->y + WT_H, bh = f->h - WT_H - 4, sw = 190;
    gfx_begin(); gfx_rect(x0, y0, f->w - 4, bh, C_BG);
    int st = da_st(da_raum), ns = da_nsub(), n = ns + datei_zahl(da_raum, da_endungen());
    if (da_sel >= n) da_sel = n ? n - 1 : 0; if (da_scroll > da_sel) da_scroll = da_sel;
    int rows = (bh - 136) / 34; if (rows > 16) rows = 16; if (rows < 1) rows = 1; if (da_sel >= da_scroll + rows) da_scroll = da_sel - rows + 1;
    gtxt(x0 + 14, y0 + 10, TR("RAEUME", "ROOMS"), C_MUTED, 1);
    { int ids[14], td[14]; int bn = da_baum(ids, td, 14);
      for (int i = 0; i < bn; i++) {
          int ry = y0 + 30 + i * 28, ix = x0 + 8 + td[i] * 10, on = ids[i] == da_raum; if (ry + 26 > y0 + bh - 70) break;
          if (on) { rrect(ix, ry, sw - 8 - td[i] * 10, 26, 10, mix(C_BG, Q_SKY, 110)); if (Q_GLAS) h_pille(ix + 1, ry + 1, sw - 10 - td[i] * 10, 24, 0x00083A55u, 0x002A1058u); else rrect(ix + 1, ry + 1, sw - 10 - td[i] * 10, 24, 9, mix(C_CARD, C_ACC, 90)); }
          da_ordner(ix + 8, ry + 4, on ? 0x00FFFFFFu : ids[i] == 0 ? Q_SKY : Q_VIO);
          gtxt(ix + 34, ry + 5, raum_name(ids[i]), on ? 0x00FFFFFFu : C_TEXT, 1); kn_reg(K_DA_R0 + i, ix, ry, sw - 8 - td[i] * 10, 26);
      } }
    { static const char* const fd[3] = { "Alle", "Text", "Bilder" }, * const fe[3] = { "All", "Text", "Images" };
      for (int i = 0; i < 3; i++) { int fx = x0 + 10 + i * 58, on = da_filter == i; rrect(fx, y0 + bh - 62, 54, 22, 11, on ? mix(C_BG, Q_CY, 130) : mix(C_BG, C_BORDER, 60)); gtxt(fx + (54 - gtw(lang == 0 ? fd[i] : fe[i], 1)) / 2, y0 + bh - 58, lang == 0 ? fd[i] : fe[i], on ? 0x00FFFFFFu : C_MUTED, 1); kn_reg(K_DA_F0 + i, fx, y0 + bh - 62, 54, 22); } }
    { const char* t = da_stufetxt(st); int tw = gtw(t, 1) + 22; rrect(x0 + 10, y0 + bh - 32, tw, 22, 11, mix(C_BG, da_stufefarbe(st), 110)); rrect(x0 + 18, y0 + bh - 25, 8, 8, 4, da_stufefarbe(st)); gtxt(x0 + 32, y0 + bh - 28, t, 0x00FFFFFFu, 1); }
    int rx = x0 + sw + 14, rw = f->w - 4 - sw - 22;
    rrect_rand(rx, y0 + 12, rw, 30, 15, mix(C_BG, C_CARD, 128), C_BORDER);
    { char t[100]; raum_pfad(da_raum, t, 100); gtxt(rx + 16, y0 + 19, t, Q_GLAS ? Q_CY : C_ACC2, 1); if (da_raum > 0) { int sv = raum_sicht(da_raum); const char* sx = sv == 0 ? TR("PRIVAT", "PRIVATE") : sv == 1 ? TR("GETEILT: SEHEN", "SHARED: VIEW") : TR("GETEILT", "SHARED"); int w2 = gtw(sx, 1) + 16; rrect(rx + rw - w2 - 8, y0 + 17, w2, 20, 10, mix(C_BG, sv == 0 ? 0x00F43F5Eu : Q_CY, 70)); gtxt(rx + rw - w2, y0 + 19, sx, 0x00FFFFFFu, 1); } }
    int ly = y0 + 54;
    if (da_modus == 2) {                                                                                 /* Schluessel-Ansicht */
        gtxt(rx + 4, ly, TR("Schluessel fuer diesen Raum", "Keys for this room"), 0x00FFFFFFu, 1);
        gtxt(rx + 4, ly + 20, TR("Wer darf hier was? Gilt auch fuer alle Unterraeume.", "Who may do what here? Applies to all sub-rooms too."), C_MUTED, 1);
        { int sv = da_raum >= 0 ? raum_sicht(da_raum) : 0; char t[60]; scopy(t, TR("Alle anderen: ", "Everyone else: "), 60); cat(t, sv == 0 ? TR("Gesperrt (privat)", "Locked (private)") : sv == 1 ? TR("Sehen", "View") : TR("Mitarbeiten", "Contribute"), 60); knopf(K_DA_SICHT, t, rx + 4, ly + 46, 300, sv != 0); }
        gtxt(rx + 4, ly + 92, TR("PERSONEN (Klick wechselt: kein Schluessel > Sehen > Mitarbeiten > Verwalten > Gesperrt)", "PEOPLE (click cycles: no key > View > Contribute > Manage > Locked)"), C_MUTED, 1);
        for (int i = 0; i < 8; i++) {
            char un[16]; int ur; if (!benutzer_nth(i, un, &ur)) break; int ry = ly + 114 + i * 32; if (ry + 30 > y0 + bh - 60) break;
            int k = raum_schluessel(da_raum, un), own = !ueq(raum_besitzer(da_raum), un) ? 0 : 1;
            rrect(rx, ry, rw, 28, 11, mix(C_BG, C_BORDER, 40)); rrect(rx + 1, ry + 1, rw - 2, 26, 10, mix(C_BG, C_CARD, 150));
            gtxt(rx + 14, ry + 6, un, C_TEXT, 1); gtxt(rx + 150, ry + 6, benutzer_rollentext(ur), C_MUTED, 1);
            const char* t = own ? TR("Besitzer", "Owner") : k < 0 ? TR("kein Schluessel", "no key") : da_stufetxt(k); uint32_t c = own ? Q_VIO : k < 0 ? C_MUTED : da_stufefarbe(k); int tw = gtw(t, 1) + 18;
            rrect(rx + rw - tw - 10, ry + 4, tw, 20, 10, mix(C_BG, c, 80)); gtxt(rx + rw - tw - 1, ry + 6, t, 0x00FFFFFFu, 1); if (!own) kn_reg(K_DA_S0 + i, rx, ry, rw, 28);
        }
        int yb2 = y0 + bh - 46; knopf(K_DA_ZURUECK, TR("Fertig", "Done"), rx, yb2, 120, 1);
        if (da_raum > 0) knopf(K_DA_RDEL, da_vfrage ? TR("Wirklich?", "Really?") : TR("Raum loeschen", "Delete room"), rx + 128, yb2, 170, da_vfrage);
        if (da_msg[0]) gtxt(rx + 310, yb2 + 10, da_msg, mix(C_MUTED, Q_CY, 140), 1);
        gfx_end(); kn_owner = ko; return;
    }
    if (da_modus == 1) {
        rrect(rx, ly, rw, 34, 13, mix(C_BG, Q_CY, 140)); rrect(rx + 1, ly + 1, rw - 2, 32, 12, mix(C_BG, C_CARD, 220));
        char t[40]; scopy(t, da_buf, 40); cat(t, "_", 40); gtxt(rx + 14, ly + 9, TR("Neuer Raum: ", "New room: "), C_MUTED, 1); gtxt(rx + 14 + gtw(TR("Neuer Raum: ", "New room: "), 1), ly + 9, t, 0x00FFFFFFu, 1);
        gtxt(rx + 4, ly + 46, TR("Name tippen, Enter = anlegen, Esc = abbrechen. Neue Raeume sind privat.", "Type a name, Enter = create, Esc = cancel. New rooms are private."), C_MUTED, 1); ly += 70;
    }
    for (int i = 0; i < rows; i++) {
        char nm[32]; int idx = da_scroll + i; int rid = idx < ns ? raum_unter(da_raum, idx, benutzer_name(), benutzer_rolle()) : -1;
        if (idx >= n) break; if (rid < 0 && !datei_nth(da_raum, da_endungen(), idx - ns, nm, 32)) break;
        int ry = ly + i * 34, on = idx == da_sel;
        if (on) { rrect(rx, ry, rw, 30, 11, mix(C_BG, Q_CY, 120)); if (Q_GLAS) h_pille(rx + 1, ry + 1, rw - 2, 28, 0x00083A55u, 0x002A1058u); else rrect(rx + 1, ry + 1, rw - 2, 28, 10, mix(C_CARD, C_ACC, 90)); }
        else { rrect(rx, ry, rw, 30, 11, mix(C_BG, C_BORDER, 40)); rrect(rx + 1, ry + 1, rw - 2, 28, 10, mix(C_BG, C_CARD, 150)); }
        if (rid >= 0) {
            da_ordner(rx + 8, ry + 5, Q_VIO); gtxt(rx + 36, ry + 7, raum_name(rid), on ? 0x00FFFFFFu : C_TEXT, 1);
            int s2 = da_st(rid), sv = raum_sicht(rid); char tg[24]; scopy(tg, sv == 0 ? TR("PRIVAT", "PRIVATE") : TR("GETEILT", "SHARED"), 24); int tw = gtw(tg, 1) + 16, tw2 = gtw(da_stufetxt(s2), 1) + 16;
            rrect(rx + rw - tw - 10, ry + 5, tw, 20, 10, mix(C_BG, sv == 0 ? 0x00F43F5Eu : Q_CY, 70)); gtxt(rx + rw - tw - 2, ry + 7, tg, 0x00FFFFFFu, 1);
            rrect(rx + rw - tw - tw2 - 18, ry + 5, tw2, 20, 10, mix(C_BG, da_stufefarbe(s2), 70)); gtxt(rx + rw - tw - tw2 - 10, ry + 7, da_stufetxt(s2), 0x00FFFFFFu, 1);
        } else {
            uint32_t ic = da_ist_bild(nm) ? Q_ROSA : da_ist_txt(nm) ? Q_SKY : C_MUTED;
            rrect(rx + 10, ry + 6, 16, 18, 4, ic); rrect(rx + 12, ry + 8, 12, 14, 3, mix(C_BG, ic, 60)); gfx_rect(rx + 15, ry + 12, 6, 1, 0x00FFFFFFu); gfx_rect(rx + 15, ry + 16, 6, 1, 0x00FFFFFFu);
            gtxt(rx + 36, ry + 7, nm, on ? 0x00FFFFFFu : C_TEXT, 1);
            const char* tg = da_ist_bild(nm) ? TR("BILD", "IMAGE") : da_ist_txt(nm) ? TR("TEXT", "TEXT") : "DATA"; int tw = gtw(tg, 1) + 16; rrect(rx + rw - tw - 10, ry + 5, tw, 20, 10, mix(C_BG, ic, 70)); gtxt(rx + rw - tw - 2, ry + 7, tg, 0x00FFFFFFu, 1);
        }
        kn_reg(K_DA0 + i, rx, ry, rw, 30);
    }
    if (!n) gtxt(rx + 10, ly + 8, TR("Dieser Raum ist leer.", "This room is empty."), C_MUTED, 1);
    int yb = y0 + bh - 46;
    knopf(K_DA_HOCH, "<", rx, yb, 38, 0);
    knopf(K_DA_OEFF, TR("Oeffnen", "Open"), rx + 46, yb, 100, 1);
    knopf(K_DA_DEL, da_vfrage ? TR("Wirklich?", "Really?") : TR("Loeschen", "Delete"), rx + 154, yb, 110, da_vfrage);
    knopf(K_DA_NEU, TR("Neuer Raum", "New room"), rx + 272, yb, 128, 0);
    knopf(K_DA_MOVE, da_mv ? TR("Hierher", "Here") : TR("Verschieben", "Move"), rx + 408, yb, 124, da_mv);
    knopf(K_DA_KEY, TR("Schluessel", "Keys"), rx + 540, yb, 120, 0);
    if (rx + 690 < rx + rw - 90) { knopf(K_DA_UP, "^", rx + rw - 84, yb, 38, 0); knopf(K_DA_DN, "v", rx + rw - 40, yb, 38, 0); }
    if (da_msg[0]) gtxt(rx + 4, yb - 20, da_msg, mix(C_MUTED, Q_CY, 140), 1);
    else if (da_mv) { char t[60]; scopy(t, TR("Verschiebe: ", "Moving: "), 60); cat(t, da_mvname, 60); gtxt(rx + 4, yb - 20, t, mix(C_MUTED, Q_VIO, 160), 1); }
    gfx_end(); kn_owner = ko;
}

static void taste_roh(uint8_t k, int shift, int altgr) {
    if (rw_aktiv) { if (k == 0x29) rw_ende(); return; }
    if (menu.offen) { if (k == 0x29) menu_schliessen(); return; }
    if (zustand != Z_LOGIN && k >= 0x3A && k <= 0x3D) { flaeche_wechsel(k - 0x3A); return; }
    if (zustand == Z_RECH) { rech_taste(k, shift, altgr); return; }
    if (zustand == Z_INFO && da_modus == 1 && fenster_idx(FT_DATEI) >= 0) { da_taste(k, shift, altgr); return; }
    if (zustand == Z_INFO && be_modus >= 1 && be_modus <= 3 && fenster_idx(FT_BENUTZER) >= 0) { be_taste(k, shift, altgr); return; }
    if (zustand == Z_INFO && ek_feld && fenster_idx(FT_EINST) >= 0) { einst_taste(k, shift, altgr); return; }
    if (zustand == Z_INFO && nfen && fen[nfen - 1].typ == FT_EINST && k != 0x29) { einst_kurztaste(k, shift, altgr); return; }
    if (zustand == Z_INFO) { if (k == 0x29 && nfen) fenster_schliessen_typ(fen[nfen - 1].typ); return; }
    if (zustand == Z_OFF) { office_taste(k, shift, altgr); return; }
    if (zustand == Z_EDIT && kp_fokus == FT_EDIT) { kp_taste(k, shift, altgr); return; }
    if (zustand == Z_EDIT) { if (cur_ctrl && k == 0x16) { dok_speichern(); zeichne_editor(1); return; } if (cur_ctrl && ed_strg(k)) return; if (cur_ctrl && strg_taste(k)) return; editor_taste(k, shift, altgr); return; }
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
        if (k == 0x3B) { lg_rad_start(); return; }
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

/* ---------- Befehlsrad: erster Buchstabe oeffnet am Mauszeiger ein Rad mit Befehlen, frueheren Eingaben und Merk-Saetzen ----------
 * Mausrad dreht, Mitteltaste/Tab uebernimmt, Linksklick fuehrt aus, Pfeile dienen als Ersatz, Esc schliesst. Das Rad liegt als Glas ueber dem Desktop;
 * darunter wird ein Schnappschuss des Bildschirms gehalten und beim Schliessen zurueckgeschrieben. */
static void ausfuehren(void);
#define RMAX 16
#define RR 170                                    /* Radius */
#define RLW 232                                   /* Breite der Beschriftungen */
#define RBW (40 + 24 + RR + 16 + RLW + 16)
#define RBH 420
#define RSL 22                                    /* Winkel zwischen zwei Eintraegen */
static struct { int offen, n, sel, bewegt, gen, cx, cy, dir, bx, by; } rad;
static int rad_ext;                              /* 1 = Rad zeigt Vorschlaege der Autovervollstaendigung (Programme), 0 = Befehlsrad im Chat */
static void ac_wahl(int i);
static char rc[RMAX][96]; static int rtyp[RMAX];
static int rrx[RMAX], rry[RMAX], rrw[RMAX];       /* Trefferflaechen der sichtbaren Eintraege (-1 = nicht sichtbar) */
static uint32_t radbuf[RBW * RBH];
static int rad_modus(void) { const struct usb_pointer* p = usb_pointer_state(); return p && p->present && gfx_fbptr(&(uint32_t){0}) != 0; }
static int sin_d(int deg) {
    deg %= 360; if (deg < 0) deg += 360;
    int neg = 0; if (deg >= 180) { neg = 1; deg -= 180; }
    if (deg > 90) deg = 180 - deg;
    int i = deg / 5, f = deg % 5; int v = sin90[i] + (i < 18 ? (sin90[i + 1] - sin90[i]) * f / 5 : 0);
    return neg ? -v : v;
}
static int cos_d(int deg) { return sin_d(deg + 90); }
static int wortstart(const char* t, const char* q) { for (int j = 1; t[j]; j++) if (t[j - 1] == ' ' && praefix(t + j, q)) return 1; return 0; }
static void rad_add(const char* t, int typ) {
    if (rad.n >= RMAX || gleich(t, sug_q)) return;
    for (int i = 0; i < rad.n; i++) if (gleich(rc[i], t)) return;
    scopy(rc[rad.n], t, 96); rtyp[rad.n] = typ; rad.n++;
}
static void rad_baue(void) {
    rad.n = 0; rad.sel = 0; rad.bewegt = 0;
    const char* q = eingabe; while (*q == ' ') q++;
    if (!*q) return;
    sug_q = q;
    { static char zc[64]; int n = zlk_ergaenze(q, zc, sizeof(zc), 0); if (n >= 2 && zlk_sicherheit() < 150) { char zs[96]; scopy(zs, q, 96); cat(zs, zc, 96); rad_add(zs, 4); } }   /* Lernkern */
    for (int i = nhist - 1; i >= 0; i--) if (praefix(hist[i], q)) rad_add(hist[i], 2);
    for (int i = 0; i < NKAT; i++) { const char* t = lang == 0 ? katalog[i].de : katalog[i].en; if (praefix(t, q)) rad_add(t, katalog[i].typ == 1 ? 1 : 0); }
    for (int i = 0; i < NKAT; i++) { const char* t = lang == 0 ? katalog[i].de : katalog[i].en; if (wortstart(t, q)) rad_add(t, katalog[i].typ == 1 ? 1 : 0); }
    char t[112]; int art, st;
    for (int i = 0; rad.n < RMAX && ged_erinnerung(i, t, 112, &art, &st); i++) if (praefix(t, q) || wortstart(t, q)) rad_add(t, 3);
}
static void rad_glas(int x0, int y0, int w, int h, int rr) {                    /* abgedunkelte, abgerundete Flaeche (Glas) */
    uint32_t pp; uint32_t* f = gfx_fbptr(&pp); if (!f) return;
    for (int y = 0; y < h; y++) {
        int yy = y0 + y; if (yy < 0 || yy >= H) continue;
        int dy = -1; if (y < rr) dy = rr - y; else if (y >= h - rr) dy = y - (h - rr - 1);
        int ix = 0; if (dy >= 0) { for (ix = 0; ix < rr; ix++) if ((rr - ix) * (rr - ix) + dy * dy <= rr * rr) break; }
        for (int x = ix; x < w - ix; x++) { int xx = x0 + x; if (xx < 0 || xx >= W) continue; uint32_t* q = f + (uint64_t)yy * pp + xx; uint32_t v = *q; *q = ((v >> 2) & 0x003F3F3Fu) + ((v >> 3) & 0x001F1F1Fu) + 0x00061226u; }
    }
}
static void rad_sichern(int bx, int by) {
    uint32_t pp; uint32_t* f = gfx_fbptr(&pp); if (!f) return;
    for (int y = 0; y < RBH; y++) { int yy = by + y; for (int x = 0; x < RBW; x++) { int xx = bx + x; radbuf[y * RBW + x] = (yy >= 0 && yy < H && xx >= 0 && xx < W) ? f[(uint64_t)yy * pp + xx] : 0; } }
}
static void rad_zurueck(void) {
    uint32_t pp; uint32_t* f = gfx_fbptr(&pp); if (!f) return;
    for (int y = 0; y < RBH; y++) { int yy = rad.by + y; if (yy < 0 || yy >= H) continue; for (int x = 0; x < RBW; x++) { int xx = rad.bx + x; if (xx >= 0 && xx < W) f[(uint64_t)yy * pp + xx] = radbuf[y * RBW + x]; } }
}
static uint32_t rad_farbe(int typ) { return typ == 4 ? 0x0034D399u : typ == 1 ? 0x00A78BFAu : typ == 2 ? 0x0094A3B8u : typ == 3 ? C_OK : C_ACC; }
static void rad_punkt(int x, int y, int s, uint32_t c) { gfx_rect(x - s / 2, y - s / 2, s, s, c); }
static void aa_pt(int x16, int y16, uint32_t c, int a) {                         /* weicher Punkt auf 1/16-Pixel-Raster */
    int xi = x16 >> 4, yi = y16 >> 4, fx = x16 & 15, fy = y16 & 15;
    gfx_blend(xi, yi, c, a * (16 - fx) * (16 - fy) / 256); gfx_blend(xi + 1, yi, c, a * fx * (16 - fy) / 256);
    gfx_blend(xi, yi + 1, c, a * (16 - fx) * fy / 256); gfx_blend(xi + 1, yi + 1, c, a * fx * fy / 256);
}
static void aa_linie(int x0, int y0, int x1, int y1, uint32_t c, int a, int dick) {   /* geglaettete Linie; dick = 1 oder 2 */
    int dx = x1 - x0, dy = y1 - y0, ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy, n = ax > ay ? ax : ay; if (n == 0) n = 1;
    for (int i = 0; i <= n; i++) {
        int X = x0 * 16 + dx * 16 * i / n - 8, Y = y0 * 16 + dy * 16 * i / n - 8;
        aa_pt(X, Y, c, a); if (dick > 1) { aa_pt(X + (ay > ax ? 8 : 0), Y + (ay > ax ? 0 : 8), c, a); }
    }
}
static void rad_kreis(int cx, int cy, int r, uint32_t c) { rrect(cx - r, cy - r, 2 * r, 2 * r, r, c); }
static void rad_icon(int typ, int bx, int by, uint32_t fc) {                      /* runde Plakette mit Symbol (Befehl, Frage, Verlauf, Erinnerung, Vorschlag) */
    uint32_t w = 0x00FFFFFFu, dk = mix(fc, 0x00000000u, 120);
    rad_kreis(bx, by, 11, mix(fc, 0x00000000u, 110)); rad_kreis(bx, by, 10, fc); rad_kreis(bx, by - 3, 7, mix(fc, w, 55));
    rad_kreis(bx, by + 1, 9, fc);
    if (typ == 0) { aa_linie(bx - 4, by - 4, bx, by, w, 255, 2); aa_linie(bx, by, bx - 4, by + 4, w, 255, 2); rrect(bx + 1, by + 3, 6, 2, 1, w); }
    else if (typ == 1) { rrect(bx - 6, by - 6, 12, 9, 4, w); gfx_rect(bx - 4, by + 3, 3, 2, w); gfx_rect(bx - 4, by + 5, 1, 1, w); for (int k = 0; k < 3; k++) gfx_rect(bx - 4 + k * 3, by - 3, 2, 2, dk); }
    else if (typ == 2) { rad_kreis(bx, by, 6, w); rad_kreis(bx, by, 5, dk); gfx_rect(bx, by - 4, 1, 5, w); aa_linie(bx, by, bx + 3, by + 2, w, 255, 1); }
    else if (typ == 3) { rrect(bx - 1, by - 6, 2, 12, 1, w); rrect(bx - 6, by - 1, 12, 2, 1, w); rad_kreis(bx, by, 2, w); gfx_rect(bx - 3, by - 3, 1, 1, w); gfx_rect(bx + 3, by - 3, 1, 1, w); gfx_rect(bx - 3, by + 3, 1, 1, w); gfx_rect(bx + 3, by + 3, 1, 1, w); }
    else { aa_linie(bx + 2, by - 6, bx - 3, by + 1, w, 255, 2); aa_linie(bx - 3, by + 1, bx + 2, by + 1, w, 255, 2); aa_linie(bx + 2, by + 1, bx - 2, by + 7, w, 255, 2); }
}
static void rad_zeichne(void) {                                                   /* zeichnet das Rad auf den (sauberen) Bildschirm */
    int cx = rad.cx, cy = rad.cy, dir = rad.dir;
    for (int i = 0; i < RMAX; i++) rrx[i] = -1;
    gfx_begin();
    int gx = dir > 0 ? cx + 24 : cx - 24 - (RR + 16 + RLW + 8), gw = RR + 16 + RLW + 8, gy = cy - 196, gh = 392;
    rad_glas(gx, gy, gw, gh, 22);
    if (Q_GLAS) { h_grad(gx + 22, gy, gw - 44, 2, mix(C_BG, Q_CY, 110), mix(C_BG, Q_VIO, 110)); h_grad(gx + 22, gy + gh - 2, gw - 44, 2, mix(C_BG, Q_VIO, 110), mix(C_BG, Q_CY, 110));
      for (int k = 0; k < 2; k++) { rrect(gx + (k ? gw - 2 : 0), gy + 22, 2, gh - 44, 1, mix(C_BG, k ? Q_VIO : Q_CY, 90)); } }
    for (int a = -84 * 4; a <= 84 * 4; a++) {                                                  /* Ring: durchgehende, weiche Linie mit Farbverlauf */
        int aa = a / 4; int cs = cos_d(aa), sn = sin_d(aa);
        uint32_t rc2 = Q_GLAS ? mix(mix(Q_CY, Q_VIO, (aa + 84) * 256 / 168), C_BG, 60 + (aa < 0 ? -aa : aa) * 2) : mix(C_BORDER, C_ACC, 40 + (84 - (aa < 0 ? -aa : aa)) * 120 / 84);
        int X = (cx * 16) + dir * (RR * 16 * cs / 1024) - 8, Y = (cy * 16) + RR * 16 * sn / 1024 - 8;
        aa_pt(X, Y, rc2, 200); aa_pt(X + dir * 8, Y + 8, rc2, 120);
    }
    rad_kreis(cx, cy, 20, mix(C_BG, C_ACC, 40)); rad_kreis(cx, cy, 16, mix(C_BG, C_ACC, 90));                 /* Nabe mit Schein */
    rad_kreis(cx, cy, 11, C_ACC); rad_kreis(cx, cy - 1, 8, mix(C_ACC, 0x00FFFFFFu, 110)); rad_kreis(cx, cy - 1, 3, 0x00FFFFFFu);
    for (int o = -3; o <= 3; o++) {
        int i = rad.sel + o; if (i < 0 || i >= rad.n) continue;
        int ang = o * RSL; int px = cx + dir * (RR * cos_d(ang) / 1024), py = cy + RR * sin_d(ang) / 1024;
        int selk = (o == 0); uint32_t fc = rad_farbe(rtyp[i]);
        char t[40]; scopy(t, rc[i], 40); int tl = slen(t);
        while (tl > 3 && gtw(t, 1) + 44 > RLW) { t[--tl] = 0; t[tl - 1] = '.'; if (tl > 1) t[tl - 2] = '.'; }
        int w = gtw(t, 1) + 44; if (w > RLW) w = RLW; if (w < 80) w = 80;
        int x = dir > 0 ? px + 16 : px - 16 - w, y = py - 15;
        if (selk) {                                                                          /* Lichtstrahl von der Nabe zum gewaehlten Eintrag, Schein um die Pille */
            aa_linie(cx + dir * (24 * cos_d(ang) / 1024), cy + 24 * sin_d(ang) / 1024, px, py, C_ACC, 200, 2);
            rrect(x - 4, y - 4, w + 8, 38, 19, mix(C_BG, fc, 40)); rrect(x - 2, y - 2, w + 4, 34, 17, mix(C_BG, fc, 90)); if (Q_GLAS) h_pille(x, y, w, 30, mix(0x00062033u, Q_CY, 70), mix(0x001A0B3Au, Q_VIO, 90)); else rrect(x, y, w, 30, 15, mix(C_CARD, fc, 120));
            rad_kreis(px, py, 9, mix(C_BG, fc, 90)); rad_kreis(px, py, 6, fc); rad_kreis(px, py, 3, 0x00FFFFFFu);
        } else { rrect_rand(x, y, w, 30, 15, mix(C_CARD, C_BG, 90), C_BORDER); rad_kreis(px, py, 4, fc); }
        rad_icon(rtyp[i] >= 0 && rtyp[i] <= 4 ? rtyp[i] : 0, x + 16, y + 15, fc);
        gtxt(x + 32, y + 7, t, selk ? C_TEXT : (o == 1 || o == -1) ? 0x00B8C4D6u : C_MUTED, 1);
        rrx[i] = x; rry[i] = y; rrw[i] = w;
    }
    { const char* h = TR("Mausrad drehen - Mitteltaste/Tab uebernehmen", "Wheel: turn - middle click/Tab inserts"); int hw = gtw(h, 1); gtxt(gx + (gw - hw) / 2, gy + gh - 26, h, C_MUTED, 1); }
    { char z[16], a[8]; dec(a, (uint32_t)rad.sel + 1); scopy(z, a, 16); cat(z, "/", 16); dec(a, (uint32_t)rad.n); cat(z, a, 16); gtxt(gx + gw - 16 - gtw(z, 1), gy + 10, z, C_MUTED, 1); }
    gfx_end();
}
static void rad_weg(void) {
    if (!rad.offen) return;
    rad.offen = 0;
    if (rad.gen == desk_gen) { gfx_begin(); rad_zurueck(); gfx_end(); }
}
static void rad_zeige(int cx, int cy) {
    rad.dir = (cx + RBW - 40 <= W) ? 1 : -1;
    if (rad.dir > 0) { if (cx < 40) cx = 40; if (cx > W - RBW + 40) cx = W - RBW + 40; } else { if (cx > W - 40) cx = W - 40; if (cx < RBW - 40) cx = RBW - 40; }
    if (cy < RBH / 2) cy = RBH / 2; if (cy > H - RBH / 2) cy = H - RBH / 2;
    rad.cx = cx; rad.cy = cy; rad.bx = rad.dir > 0 ? cx - 40 : cx + 40 - RBW; rad.by = cy - RBH / 2;
    int neu = !rad.offen;
    gfx_begin(); rad_sichern(rad.bx, rad.by); gfx_end();
    rad.gen = desk_gen; rad.offen = 1;
    rad_zeichne();
    if (neu) { kprint(rad_ext ? "[DESKTOP] Rad (Vorschlaege): geoeffnet, " : "[DESKTOP] Rad: geoeffnet, "); kprint_dec((uint32_t)rad.n); kprint(" Eintraege\n"); }
}
static void rad_neu(void) {
    if (zustand != Z_DESK || flaeche != 0 || menu.offen || sel1 || einlen == 0 || !rad_modus()) return;
    rad_ext = 0;
    rad_baue(); if (rad.n == 0) return;
    const struct usb_pointer* p = usb_pointer_state();
    rad_zeige(p->x, p->y);
}
static void rad_drehen(int d) {
    if (!rad.offen) return;
    int n = rad.sel + d; if (n < 0) n = 0; if (n >= rad.n) n = rad.n - 1;
    if (n == rad.sel) return;
    rad.sel = n; rad.bewegt = 1;
    gfx_begin(); rad_zurueck(); gfx_end(); rad_zeichne();
}
static void rad_einfuegen(int lauf) {
    if (!rad.offen || rad.sel < 0 || rad.sel >= rad.n) return;
    if (rad_ext) { ac_wahl(rad.sel); return; }
    char t[96]; scopy(t, rc[rad.sel], 96);
    rad_weg();
    scopy(eingabe, t, 96); einlen = slen(eingabe); sel1 = 0; sug_suche();
    kprint("[DESKTOP] Rad: uebernommen\n");
    if (lauf && !(einlen && eingabe[einlen - 1] == ' ')) { ausfuehren(); return; }
    zeichne_eingabe(); rad_neu();
}
static int rad_treffer(int x, int y) {
    for (int i = 0; i < rad.n; i++) if (rrx[i] >= 0 && x >= rrx[i] && x < rrx[i] + rrw[i] && y >= rry[i] && y < rry[i] + 28) return i;
    return -1;
}
static int rad_taste(uint8_t k) {
    if (!rad.offen) return 0;
    if (k == 0x51 || k == 0x52) { rad_drehen(k == 0x51 ? 1 : -1); return 1; }                  /* Pfeil runter/hoch dreht */
    if (k == 0x2B) { rad_einfuegen(0); return 1; }                                             /* Tab uebernimmt */
    if (k == 0x29) { rad_weg(); return 1; }                                                    /* Esc schliesst */
    if ((k == 0x28 || k == 0x58) && rad.bewegt) { rad_einfuegen(1); return 1; }                /* Enter nach dem Drehen: uebernehmen und ausfuehren */
    return 0;
}
/* ---------- Autovervollstaendigung in den Programmen (Text, Zynos-Feld, Tabellenzelle) ---------- */
static int ac_kontext(char* ctx, int max) {
    int top = nfen ? fen[nfen - 1].typ : 0; ac_ziel = AZ_NICHTS;
    if (menu.offen) return 0;
    if ((zustand == Z_EDIT || zustand == Z_OFF) && kp_fokus && kp_fokus == top && kp_einlen > 1) { scopy(ctx, kp_ein, max); ac_ziel = AZ_KP; return AZ_KP; }
    if (zustand == Z_EDIT && top == FT_EDIT && !kp_fokus) {
        int a, b; if (ed_selbereich(&a, &b)) return 0;
        int s0 = dokcur; while (s0 > 0 && dok[s0 - 1] != '\n' && dokcur - s0 < 90) s0--;
        if (dokcur - s0 < 2 || dokcur - s0 >= max) return 0;
        for (int i = s0; i < dokcur; i++) ctx[i - s0] = dok[i]; ctx[dokcur - s0] = 0;
        ac_ziel = AZ_TEXT; ac_pos = dokcur; ac_len = doklen; return AZ_TEXT;
    }
    if (zustand == Z_OFF && top == FT_TAB && t_edit && t_ebuf[0] != '=' && t_elen > 1) { scopy(ctx, t_ebuf, max); ac_ziel = AZ_TAB; return AZ_TAB; }
    if (zustand == Z_OFF && top == FT_PRAES && !kp_fokus && pf_feld) {
        const char* f = pf_feld == 1 ? pf_t[pf_cur] : pf_b[pf_cur]; int n = slen(f), s0 = n; while (s0 > 0 && f[s0 - 1] != '\n' && n - s0 < 90) s0--;
        if (n - s0 < 2 || n - s0 >= max) return 0; for (int i = s0; i < n; i++) ctx[i - s0] = f[i]; ctx[n - s0] = 0; ac_ziel = AZ_PF; return AZ_PF;
    }
    if (zustand == Z_OFF && top == FT_BOARD && !kp_fokus && b_sel >= 0 && b_edit && !b_ersetze && slen(bf[b_sel].t) > 1) { scopy(ctx, bf[b_sel].t, max); ac_ziel = AZ_BD; return AZ_BD; }
    if (zustand == Z_DESK && flaeche == 0 && !menu.offen && notiz_fok >= 0 && notiz_fok < nwid && wid[notiz_fok].typ == WG_NOTIZ) {
        int id = wid[notiz_fok].id, n = nz_len[id], s0 = n; while (s0 > 0 && nz[id][s0 - 1] != '\n' && n - s0 < 90) s0--;
        if (n - s0 < 2 || n - s0 >= max) return 0; for (int i = s0; i < n; i++) ctx[i - s0] = nz[id][i]; ctx[n - s0] = 0; ac_ziel = AZ_NZ; return AZ_NZ;
    }
    return 0;
}
static int ac_lernpos = -1;
static void ac_satz_lernen(void) {                              /* gerade beendeten Satz merken */
    if (dokcur < 8 || dokcur == ac_lernpos) return;
    char c = dok[dokcur - 1]; if (c != '.' && c != '!' && c != '?') return;
    ac_lernpos = dokcur;
    int s0 = dokcur - 1; while (s0 > 0 && dok[s0 - 1] != '\n' && !((dok[s0 - 1] == ' ') && s0 >= 2 && (dok[s0 - 2] == '.' || dok[s0 - 2] == '!' || dok[s0 - 2] == '?')) && dokcur - s0 < 90) s0--;
    char t[96]; int n = 0; for (int i = s0; i < dokcur && n < 94; i++) t[n++] = dok[i]; t[n] = 0;
    if (t[0] >= 'A' && t[0] <= 'Z') t[0] = (char)(t[0] + 32);                      /* Satzanfang klein speichern: passt dann ueberall */
    ac_lerne(t);
}
static void ac_aktualisieren(void) {
    char ctx[100]; ac_n = 0;
    int z = ac_kontext(ctx, 100); if (!z) return;
    if (z == AZ_TEXT) ac_satz_lernen();
    ac_baue(ctx, z == AZ_TEXT ? dok : 0, z == AZ_TEXT ? doklen : 0);
    if (!ac_n) return;
    if (z == AZ_TEXT) ob_word(0);                                                  /* grauer Vorschlag hinter der Eingabemarke */
    if (rad_modus()) {
        rad_ext = 1; rad.n = ac_n; rad.sel = 0; rad.bewegt = 0;
        for (int i = 0; i < ac_n; i++) { scopy(rc[i], ac_c[i], 96); rtyp[i] = z == AZ_TEXT ? 0 : 1; }
        const struct usb_pointer* p = usb_pointer_state();
        if (z == AZ_TEXT && ac_hat) rad_zeige(ac_cx + 24, ac_cy + ac_ch / 2); else rad_zeige(p->x, p->y);
    } else if (z != AZ_TEXT && z != AZ_NZ) { scopy(ob_meld, "Tab: ", 100); cat(ob_meld, ac_c[0], 100); ob_neu_zeichnen(nfen ? fen[nfen - 1].typ : FT_EDIT); }
}
static void ac_wahl(int i) {
    if (i < 0 || i >= ac_n) return;
    char t[96]; scopy(t, ac_c[i], 96); int er = ac_er[i]; int z = ac_ziel;
    rad_weg(); ob_meld[0] = 0;
    if (z == AZ_TEXT) {
        if (er > dokcur) er = dokcur; dok_loesche(dokcur - er, dokcur);
        for (int k = 0; t[k]; k++) { dok_einf(dokcur, (unsigned char)t[k], dok_cst); dokcur++; }
        zeichne_editor(0);
    } else if (z == AZ_KP) {
        if (er > kp_einlen) er = kp_einlen; kp_einlen -= er; kp_ein[kp_einlen] = 0;
        for (int k = 0; t[k] && kp_einlen < 196; k++) kp_ein[kp_einlen++] = t[k]; kp_ein[kp_einlen] = 0;
        ob_neu_zeichnen(kp_fokus);
    } else if (z == AZ_TAB) {
        if (er > t_elen) er = t_elen; t_elen -= er; t_ebuf[t_elen] = 0;
        for (int k = 0; t[k] && t_elen < 46; k++) t_ebuf[t_elen++] = t[k]; t_ebuf[t_elen] = 0;
        zeichne_tabelle();
    } else if (z == AZ_PF) {
        char* f = pf_feld == 1 ? pf_t[pf_cur] : pf_b[pf_cur]; int mx = pf_feld == 1 ? 78 : 590, n = slen(f);
        if (er > n) er = n; n -= er; for (int k = 0; t[k] && n < mx; k++) f[n++] = t[k]; f[n] = 0; pf_geaendert = 1; zeichne_praes();
    } else if (z == AZ_BD) {
        char* f = bf[b_sel].t; int n = slen(f); if (er > n) er = n; n -= er; for (int k = 0; t[k] && n < 118; k++) f[n++] = t[k]; f[n] = 0; b_geaendert = 1; board_flaeche();
    } else if (z == AZ_NZ) {
        int id = wid[notiz_fok].id, n = nz_len[id]; if (er > n) er = n; n -= er;
        for (int k = 0; t[k] && n < NZ_T; k++) nz[id][n++] = t[k]; nz[id][n] = 0; nz_len[id] = n; notiz_speichern(); zeichne_desktop();
    } else return;
    kprint("[DESKTOP] Vorschlag uebernommen: "); kprint(t); kprint("\n");
    ac_aktualisieren();
}
static void taste(uint8_t k, int shift, int altgr) {
    if (ob_vollbild) { praes_vollbild_taste(k); return; }
    if (orb_auf) {                                                                   /* Orbit: Pfeile waehlen, Enter oeffnet, Esc schliesst */
        if (k == 0x50 || k == 0x52) { orb_sel = orb_sel <= 0 ? NDOCK - 1 : orb_sel - 1; zeichne_orbit(100); }
        else if (k == 0x4F || k == 0x51 || k == 0x2B) { orb_sel = orb_sel < 0 || orb_sel >= NDOCK - 1 ? 0 : orb_sel + 1; zeichne_orbit(100); }
        else if ((k == 0x28 || k == 0x58) && orb_sel >= 0) dock_oeffnen(orb_sel);
        else orb_schliessen();
        return;
    }
    if (cur_ctrl && k == 0x2C && zustand != Z_LOGIN) { if (flaeche != 0) flaeche_wechsel(0); if (nfen && fok) { fok = 0; zustand = Z_DESK; zeichne_desktop(); } orb_oeffnen(); return; }   /* Strg+Leertaste */
    int desk = zustand == Z_DESK && flaeche == 0 && !menu.offen;
    if (notiz_taste(k, shift, altgr)) return;
    if (desk && rad_taste(k)) return;
    if (rad.offen && rad_ext && rad_taste(k)) return;
    if (!rad.offen && ac_n > 0 && k == 0x2B && ac_ziel == AZ_TEXT && ac_pos == dokcur && ac_len == doklen && zustand == Z_EDIT) { ac_wahl(0); return; }
    if (!rad.offen && ac_n > 0 && k == 0x2B && (ac_ziel == AZ_KP || ac_ziel == AZ_TAB || ac_ziel == AZ_PF || ac_ziel == AZ_BD) && (zustand == Z_EDIT || zustand == Z_OFF)) { ac_wahl(0); return; }
    rad_weg();
    ac_n = 0;
    taste_roh(k, shift, altgr);
    if (zustand == Z_DESK && flaeche == 0 && !menu.offen) rad_neu(); else ac_aktualisieren();
}

static void dock_oeffnen(int i) {
    orb_auf = 0; orb_sel = -1;
    kprint("[DESKTOP] Orbit: "); kprint(dock_n[i][1]); kprint("\n");
    if (dock_typ[i] == FT_EDIT) editor_oeffnen(); else if (dock_typ[i] == FT_WEB) web_oeffnen(""); else fenster_oeffnen(dock_typ[i]);
}
static void klick(int x, int y, int owner) {
    for (int i = 0; i < nkn; i++) {
        if (knoepfe[i].owner != owner) continue;
        if (x < knoepfe[i].x || x >= knoepfe[i].x + knoepfe[i].w || y < knoepfe[i].y || y >= knoepfe[i].y + knoepfe[i].h) continue;
        int id = knoepfe[i].id;
        if (id >= K_OF0 && id < K_OF_END && owner >= 0) { office_klick(id, owner); return; }
        if (zustand == Z_DESK && id >= K_DOCK0 && id < K_DOCK0 + NDOCK) { dock_oeffnen(id - K_DOCK0); return; }
        if (zustand == Z_DESK && id == K_ORB) { orb_oeffnen(); return; }
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
        else if (zustand == Z_WEB && web_klick(id)) { }
        else if (zustand == Z_DESK && id == K_NEUDOK) editor_oeffnen();
        else if (zustand == Z_INFO && id >= K_EIN0 && id < K_EIN0 + 60) einst_klick(id);
        else if (zustand == Z_INFO && id >= K_CH0 && id < K_CH_DEL + 1) chats_klick(id);
        else if (zustand == Z_INFO && id >= K_GED_SCHLAF && id < K_GED_N0 + 16) ged_klick(id);
        else if (zustand == Z_INFO && id >= K_DA0 && id < K_DA_END) da_klick(id);
        else if (zustand == Z_INFO && id >= K_BE0 && id < K_BE_END) be_klick(id);
        else if (zustand == Z_RECH && id >= K_RB0 && id < K_RB0 + 20) { rech_druecke(rtasten[id - K_RB0]); zeichne_rechner(); }
        else if (zustand == Z_DESK && id == K_AUFLEER) { aufg_leeren(); kprint("[DESKTOP] Aufgaben geleert\n"); zeichne_desktop(); }
        else if (zustand == Z_EDIT && id == K_SPEICHERN) { dok_speichern(); zeichne_editor(0); }
        else if (zustand == Z_EDIT && id == K_NEU) { if (doklen > 0 && dokgeaendert) dok_speichern(); dok_neu(); zeichne_editor(1); }
        else if (zustand == Z_EDIT && id == K_SCHLIESSEN) editor_schliessen();
        return;
    }
    if (zustand == Z_WEB) web_maus(x, y);                                /* Link, Formularfeld oder Rollbalken */
}

static void rohtaste(uint8_t k, int shift, int altgr) { unsigned n = (qt + 1) % QN; if (n != qh) { kq[qt] = (uint16_t)(k | (shift << 8) | (altgr << 9) | (usb_ctrl << 10)); qt = n; } }

static int fdrag, fdx, fdy, fox, foy, fdw, fdh, fobj, fdgezeichnet;
static int fenster_bei(int x, int y) { if (flaeche != 0) return -1; for (int i = nfen - 1; i >= 0; i--) if (x >= fen[i].x && x < fen[i].x + fen[i].w && y >= fen[i].y && y < fen[i].y + fen[i].h) return i; return -1; }
static void fokus_bei(int x, int y) {
    int h = fenster_bei(x, y);
    if (h >= 0) { if (h != nfen - 1) fenster_hoch(h); fok = 1; zustand = zustand_fuer(fen[nfen - 1].typ); zeichne_desktop(); }
    else if (fok) { fok = 0; zustand = Z_DESK; zeichne_desktop(); }
}
/* ---------- Notizzettel: Tippen, KI-Knoepfe ---------- */
static void notiz_ki(int j, int art) {
    int id = wid[j].id; static char out[900]; static char auf[300];
    if (!nz_len[id]) { scopy(nz_meld, TR("Erst etwas schreiben", "Write something first"), 48); return; }
    if (art == 0) scopy(auf, TR("Verbessere den folgenden Notizzettel-Text: korrigiere Rechtschreibung und formuliere klarer. Gib nur den verbesserten Text aus, maximal 280 Zeichen.", "Improve the following note: fix spelling and make it clearer. Output only the improved text, at most 280 characters."), 300);
    else if (art == 1) scopy(auf, TR("Extrahiere aus dem folgenden Notizzettel-Text alle Aufgaben. Gib pro Zeile genau eine Aufgabe aus, ohne Nummerierung und ohne weiteren Text.", "Extract all tasks from the following note. Output exactly one task per line, no numbering and no other text."), 300);
    else scopy(auf, TR("Setze den folgenden Notizzettel-Text sinnvoll fort. Gib nur die Fortsetzung aus, maximal 200 Zeichen.", "Continue the following note sensibly. Output only the continuation, at most 200 characters."), 300);
    nz[id][nz_len[id]] = 0;
    int n = ob_ki(auf, nz[id], out, (int)sizeof(out));
    if (n <= 0) { scopy(nz_meld, TR("Keine KI verfuegbar", "No AI available"), 48); return; }
    if (art == 1) {
        int cnt = 0, i = 0; while (out[i]) { int e = i; while (out[e] && out[e] != '\n') e++; char t[120]; int tl = 0; for (int k = i; k < e && tl < 119; k++) t[tl++] = out[k]; t[tl] = 0;
            while (tl && (t[tl - 1] == ' ' || t[tl - 1] == '\r')) t[--tl] = 0; int a = 0; while (t[a] == '-' || t[a] == '*' || t[a] == ' ') a++;
            if (t[a] && aufg_add(t + a) >= 0) cnt++; i = out[e] ? e + 1 : e; }
        scopy(nz_meld, cnt ? TR("Aufgaben angelegt", "Tasks added") : TR("Keine Aufgaben", "No tasks"), 48);
    } else if (art == 0) {
        int l = 0; while (out[l] && l < NZ_T) { nz[id][l] = out[l]; l++; } nz[id][l] = 0; nz_len[id] = l; scopy(nz_meld, TR("Verbessert", "Improved"), 48);
    } else {
        int l = nz_len[id]; if (l < NZ_T) nz[id][l++] = ' '; for (int k = 0; out[k] && l < NZ_T; k++) nz[id][l++] = out[k]; nz[id][l] = 0; nz_len[id] = l; scopy(nz_meld, TR("Fortgesetzt", "Continued"), 48);
    }
    notiz_speichern();
    kprint("[DESKTOP] Notiz-KI ausgefuehrt\n");
}
static int notiz_taste(uint8_t k, int shift, int altgr) {
    if (notiz_fok < 0 || notiz_fok >= nwid || wid[notiz_fok].typ != WG_NOTIZ) return 0;
    if (zustand != Z_DESK || flaeche != 0 || menu.offen) return 0;
    int id = wid[notiz_fok].id; nz_meld[0] = 0;
    if (rad.offen && rad_ext && rad_taste(k)) return 1;
    if (k == 0x2B && ac_n > 0 && ac_ziel == AZ_NZ) { ac_wahl(0); return 1; }
    rad_weg(); ac_n = 0;
    if (k == 0x29) { notiz_fok = -1; zeichne_desktop(); return 1; }
    if (k == 0x2A) { if (nz_len[id] > 0) nz[id][--nz_len[id]] = 0; }
    else {
        char c = (k == 0x28 || k == 0x58) ? '\n' : usb_key_char(k, shift, altgr);
        if (!((c == '\n' || (c >= 32 && c < 127) || (unsigned char)c >= 0xA0) && nz_len[id] < NZ_T)) return 0;
        nz[id][nz_len[id]++] = c; nz[id][nz_len[id]] = 0;
    }
    notiz_speichern(); gfx_begin(); wid_zeichnen(notiz_fok); gfx_end(); ac_aktualisieren(); return 1;
}
/* 1 = verbraucht (Klick auf Rumpf/Knoepfe), 0 = Kopfzeile */
static int notiz_klick(int j, int x, int y) {
    struct widget* g = &wid[j]; if (y < g->y + 26) return 0;
    notiz_fok = j; nz_meld[0] = 0;
    if (y >= g->y + g->h - 34 && y < g->y + g->h - 8) { int zb = (g->w - 24) / 3, k = (x - g->x - 12) / zb; if (x >= g->x + 12 && k >= 0 && k < 3) notiz_ki(j, k); }
    zeichne_desktop(); return 1;
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
            if (x >= f->x + f->w - 34) { if (f->typ == FT_EDIT) editor_schliessen(); else if (f->typ == FT_WEB) web_schliessen(); else if (ob_farbe(f->typ)) office_schliessen(f->typ); else fenster_schliessen_typ(f->typ); return; }
            fdrag = 1; fobj = h; fdx = x - f->x; fdy = y - f->y; fox = f->x; foy = f->y; fdw = f->w; fdh = f->h; fdgezeichnet = 0; return;
        }
        if (office_maus(f->typ, x, y)) return;
        klick(x, y, h); return;
    }
    if (flaeche == 0) for (int j = nwid - 1; j >= 0; j--) {
        struct widget* g = &wid[j];
        if (x >= g->x && x < g->x + g->w && y >= g->y && y < g->y + g->h) {
            if (x >= g->x + g->w - 30 && y < g->y + 26) { widget_weg(j); zeichne_desktop(); return; }
            if (g->typ == WG_NOTIZ && notiz_klick(j, x, y)) return;
            fdrag = 2; fobj = j; fdx = x - g->x; fdy = y - g->y; fox = g->x; foy = g->y; fdw = g->w; fdh = g->h; fdgezeichnet = 0; return;
        }
    }
    if (notiz_fok >= 0) notiz_fok = -1;
    if (fok) { fok = 0; zustand = Z_DESK; zeichne_desktop(); }
    if (zustand == Z_DESK && flaeche == 0 && x >= zr_x() && x < zr_x() + zr_w() && y >= zr_y() && y < zr_y() + zr_h()) {
        if (x >= zr_x() + zr_w() - 24 && y >= zr_y() + zr_h() - 24) zdrag = 2; else { zdrag = 1; zdx = x - zx; zdy = y - zy; }
    } else { if ((hg_nr == 6 || hg_nr == 7) && zustand == Z_DESK && flaeche == 0 && y > 48) wasser_welle(x, y); klick(x, y, -1); }
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
        { int wd = usb_wheel_take(); if (wd && rad.offen && !menu.offen) rad_drehen(wd > 0 ? -1 : 1); else if (wd && zustand == Z_WEB && !menu.offen && fenster_idx(FT_WEB) == nfen - 1) web_scroll(wd > 0 ? -120 : 120); }               /* Mausrad dreht das Befehlsrad */
        { static int was_mid; int mid = (p->buttons & 4) != 0; if (mid && !was_mid && rad.offen) rad_einfuegen(0); was_mid = mid; }
        if (rd && !was_r && zustand != Z_LOGIN && !rw_aktiv) rad_weg();
        if (rd && !was_r && zustand != Z_LOGIN && !rw_aktiv) { if (menu.offen) menu_schliessen(); else { fokus_bei(p->x, p->y); menu_oeffnen(p->x, p->y); } }
        was_r = rd;
        if (rw_aktiv) { if (down && !was_down) rw_klick(p->x, p->y); was_down = down; return; }
        if (zustand == Z_LOGIN && !entsperr && down && !was_down) lg_klick(p->x, p->y);
        if (menu.offen) {
            int i = menu_treffer(p->x, p->y); if (i != menu.hover) { menu.hover = i; zeichne_menu(); }
            if (down && !was_down) { was_down = down; if (i >= 0) menu_aktion(menu.p[i].id); else menu_schliessen(); }
            was_down = down; return;
        }
        if (orb_auf) {
            int i = orb_treffer(p->x, p->y); if (i != orb_sel) { orb_sel = i; zeichne_orbit(100); }
            if (down && !was_down) { was_down = down; if (i >= 0) dock_oeffnen(i); else orb_schliessen(); return; }
            was_down = down; return;
        }
        if (down && !was_down && rad.offen) { int i = rad_treffer(p->x, p->y); if (i >= 0) { rad.sel = i; rad_einfuegen(1); was_down = down; return; } rad_weg(); }
        if (ob_vollbild) { if (down && !was_down) praes_vollbild_taste(0x4F); was_down = down; return; }
        if (down && !was_down && zustand != Z_LOGIN) maus_runter(p->x, p->y);
        if (down && ob_maus) office_zug(p->x, p->y);
        if (!down && ob_maus) office_los(p->x, p->y);
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
        if (!down && zdrag) { zdrag = 0; layout_speichern(); kprint("[DESKTOP] Zynos: Position "); kprint_dec((uint32_t)zx); kprint(","); kprint_dec((uint32_t)zy); kprint(" Groesse "); kprint_dec((uint32_t)zs); kprint("%\n"); }
        was_down = down;
    }
    if (rad.offen) return;                                                       /* solange das Rad offen ist, nichts darunter neu zeichnen */
    static int war_ruhe;
    int r = ruhend(); if (r != war_ruhe) { war_ruhe = r; kprint(r ? "[DESKTOP] Zynos: Ruhemodus\n" : "[DESKTOP] Zynos: wach\n"); }
    uint32_t ph = t / (t < sprech_bis ? 12 : r ? 60 : 24);
    if (ph != robot_phase && qh == qt) { robot_phase = ph; if (zustand != Z_LOGIN && !zdrag && !fdrag && !menu.offen && flaeche == 0 && !deckt(zr_x() - 4, zr_y() - 4, zr_w() + 8, zr_h() + 8)) zeichne_robot(); }
    { static uint32_t lmin; static uint32_t lt; if (zustand != Z_LOGIN && flaeche == 0 && nwid && !fdrag && !menu.offen && t - lt > 300) { lt = t; uint32_t ymd, hm; rtc_get(&ymd, &hm); if (hm != lmin) { if (lmin) zeichne_desktop(); lmin = hm; } } }
    { static uint32_t lf; int wi = fenster_idx(FT_EINST); if (wi >= 0 && wi == nfen - 1 && flaeche == 0 && zustand != Z_LOGIN && !fdrag && !menu.offen && t / 5 != lf) { lf = t / 5; lader(fen[wi].x + fen[wi].w - 60, fen[wi].y + fen[wi].h - 56, 34, lf, C_BG); } }
    { static uint32_t lw; if (qh == qt && !fdrag && !menu.offen && t - lw > 15) { lw = t; web_bilder_tick(); } }       /* Bilder im Browser nachladen */
    { static uint32_t la; if (!fdrag && !menu.offen && t - la >= 3) { la = t; web_app_tick(); } }     /* Programme (ZynJava) im Browser */
    { static uint32_t lsek; if (zustand == Z_DESK && flaeche == 1 && t / 100 != lsek) { lsek = t / 100; gfx_begin(); sys_k_zeit(); gfx_end(); } }
}

int desktop_start(void) {
    if (!gfx_available()) return -1;
    gfx_size(&W, &H);
    lang = 0; npend = 0;
    static char cfg[64]; int r = 0;
    th_laden();
    { const char* sp = zdb_get("system.sprache"); if (sp) { for (int i = 0; i < 5; i++) if (ueq(sp, LCODE[i])) lang = i; } else r = synfs_read("sys.config", cfg, sizeof(cfg) - 1); }
    if (r > 0) { cfg[r] = 0; for (int i = 0; cfg[i]; i++) if (cfg[i] == 'l' && cfg[i + 1] == 'a' && cfg[i + 2] == 'n' && cfg[i + 3] == 'g' && cfg[i + 4] == '=') { lang = (cfg[i + 5] == 'd' ? 0 : 1); break; } }
    bar_w = W - 80 < 760 ? W - 80 : 760; bar_x = (W - bar_w) / 2; bar_y = H - 310;
    fname[0] = fpw[0] = 0; ffocus = 0; fmsg = 0; einlen = 0; eingabe[0] = 0; nsug = 0; sugsel = -1; flaeche = 0; alauf = -1; nalog = 0; pend = 0; nfen = 0; nwid = 0; fok = 0; fdrag = 0; fdgezeichnet = 0; menu.offen = 0; nantwort = 0; qh = qt = 0; was_down = 0;
    zs = 160; zx = 24 + SC(90); zy = 56 + SC(108); lbw = 0; zdrag = 0; letzte_eingabe = timer_get_ticks();
    gfx_console(0);
    notiz_laden();
    { const char* ws = zdb_get("desktop.widgets"); while (ws && *ws && nwid < NWID_MAX) { int t = lies(&ws), x = lies(&ws) - 2000, y = lies(&ws) - 2000; if ((t >= 1 && t <= 4) || (t >= 10 && t < 10 + NZ_N)) { wid[nwid].typ = t >= 10 ? WG_NOTIZ : t; wid[nwid].id = t >= 10 ? t - 10 : 0; if (t >= 10) nz_used[t - 10] = 1; wid_groesse(wid[nwid].typ, &wid[nwid].w, &wid[nwid].h); if (x < 60 - wid[nwid].w) x = 60 - wid[nwid].w; if (x > W - 60) x = W - 60; if (y < 42) y = 42; if (y > H - 40) y = H - 40; wid[nwid].x = x; wid[nwid].y = y; nwid++; } } }
    { const char* zz = zdb_get("desktop.zynos"); if (zz) { int x = lies(&zz), y = lies(&zz), g = lies(&zz); if (g >= 50 && g <= 220 && x > 0 && x < W && y > 44 && y < H) { zx = x; zy = y; zs = g; } } }
    usb_set_rawgrab(rohtaste);
    shell_set_tick(tick);
    kprint("[DESKTOP] Zynex OS Quantum Desktop, Bildschirm "); kprint_dec((uint32_t)W); kprint("x"); kprint_dec((uint32_t)H); kprint("\n");
    entsperr = krypto_gesperrt_dev()[0] != 0;
    if (entsperr) { zustand = Z_LOGIN; kprint("[DESKTOP] Platte verschluesselt: Entsperren-Bildschirm\n"); zeichne_login(); }
    else if (benutzer_gesperrt()) { zustand = Z_LOGIN; kprint("[DESKTOP] Anmeldebildschirm\n"); zeichne_login(); }
    else { zustand = Z_DESK; reply1(TR("Hallo! Ich bin Zynos. Was moechtest du tun?", "Hello! I am Zynos. What would you like to do?")); zeichne_desktop(); }
    return 0;
}
