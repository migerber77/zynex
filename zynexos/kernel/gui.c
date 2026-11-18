/*
 * Zynex OS - Grafischer Installations-Assistent im Gespraechsstil (Framebuffer, Maus + Tastatur)
 * Seiten: Sprache - Tastatur (Liste oder Erkennung per Tastendruck) - Platte - Konto - Netzwerk - Zusammenfassung - Installation - Fertig
 * Alle Texte stehen in fuenf Sprachen (UTF-8 im Quelltext, bei der Ausgabe nach Latin-1 gewandelt).
 */
#include "gui.h"
#include "hyperv.h"
#include "block.h"
#include "installer.h"
#include "shell.h"
#include "usb.h"
#include "layouts.h"
#include "net.h"
#include "timer.h"
#include "benutzer.h"
#include "multiboot.h"
#include "gpt.h"

extern void kprint(const char* str);
extern void kprint_dec(uint32_t val);
extern int  gfx_available(void);
extern void gfx_size(int* w, int* h);
extern void gfx_console(int an);
extern void gfx_begin(void);
extern void gfx_end(void);
extern void gfx_rect(int x, int y, int w, int h, uint32_t c);
extern void gfx_bitmap(int x, int y, int w, int h, const uint32_t* px);
extern const int zynex_logo_w, zynex_logo_h;
extern const uint32_t zynex_logo[];
extern void gfx_text(int x, int y, const char* s, uint32_t fg, int scale);
extern int  gfx_text_w(const char* s, int scale);

#define C_BG     0x000B1220u
#define C_CARD   0x00162038u
#define C_BORDER 0x002B3A57u
#define C_TEXT   0x00E6EDF7u
#define C_MUTED  0x008DA0BDu
#define C_ACC    0x003B82F6u
#define C_SEL    0x001E3A66u
#define C_OK     0x0034D399u
#define C_WARN   0x00F59E0Bu
#define C_ERR    0x00EF4444u
#define C_FIELD  0x000F172Au
#define C_OFF    0x00334155u

enum { S_SPRACHE, S_TASTATUR, S_PLATTE, S_KONTO, S_NETZ, S_ZUSAMMEN, S_INSTALL, S_FERTIG, S_ERKENN, S_AUFTEIL };
static const char* const seitenname[] = { "Sprache", "Tastatur", "Zielplatte", "Benutzerkonto", "Netzwerk", "Zusammenfassung", "Installation", "Fertig", "Erkennung", "Aufteilung" };
enum { B_ZURUECK = 1, B_WEITER, B_INSTALL, B_NETZTEST, B_NEUSTART, B_ERKENNEN, B_UEBERNEHMEN, B_ABBRUCH, B_WENIGER, B_MEHR };
static const char* const knopfname[] = { "", "Zurueck", "Weiter", "Installieren", "Netzwerk", "Neustart", "Erkennen", "Uebernehmen", "Abbrechen", "Weniger", "Mehr" };

/* ---------- Texte in fuenf Sprachen ---------- */
#define NL 5
static const char* const lang_code[NL] = { "de", "en", "fr", "it", "es" };
static const char* const lang_name[NL] = { "Deutsch", "English", "Fran\xC3\xA7" "ais", "Italiano", "Espa\xC3\xB1" "ol" };
enum { T_SPR_H, T_SPR_S, T_TAS_H, T_TAS_S, T_ERK_H, T_ERK_Q, T_ERK_NONE, T_ERK_RES, T_ERK_FAIL,
       T_PLA_H, T_PLA_S, T_PLA_NONE, T_PLA_WARN, T_PLA_NOPACK, T_PLA_NOSEL,
       T_KON_H, T_KON_S, T_KON_F1, T_KON_F2, T_KON_F3, T_KON_E1, T_KON_E2, T_KON_E3,
       T_NET_H, T_NET_S, T_NET_0, T_NET_1, T_NET_2, T_NET_WLAN, T_NET_PRUEF, T_NET_DHCP,
       T_ZUS_H, T_ZUS_S, T_ZUS_SPR, T_ZUS_TAS, T_ZUS_PLA, T_ZUS_ADM, T_ZUS_W1, T_ZUS_W2,
       T_INS_H, T_INS_S, T_FER_H, T_FER_S1, T_FER_S2, T_FEH_H, T_FEH_S,
       T_B_WEITER, T_B_ZURUECK, T_B_INSTALL, T_B_NEUSTART, T_B_ERKENNEN, T_B_UEBERNEHMEN, T_B_ABBRUCH,
       T_AUF_H, T_AUF_S, T_AUF_ALL, T_AUF_EIGEN, T_AUF_EFI, T_AUF_SYS, T_AUF_MODELL, T_AUF_FREI, T_AUF_GR, T_AUF_NOTE, T_N };

static const char* const TX[NL][T_N] = {
{   /* Deutsch */
    "Welche Sprache sprichst du?", "Wähle mit der Maus oder einer Zifferntaste (1-5).",
    "Welche Tastatur benutzt du?", "Wähle eine Belegung oder lass sie per Tastendruck erkennen (Taste E).",
    "Tastatur erkennen", "Drücke die Taste mit dem Zeichen:", "Gibt es dieses Zeichen nicht, drücke die Leertaste.", "Erkannt:", "Nicht erkannt. Bitte wähle die Belegung aus der Liste.",
    "Wohin soll Zynex OS?", "Wähle die Platte (Zifferntaste oder Maus). Ihr Inhalt wird gelöscht.", "Keine Platte gefunden (NVMe oder SATA).", "ACHTUNG: Alle Daten auf der gewählten Platte werden gelöscht.", "Kein Installationspaket vorhanden.", "Keine Platte gewählt.",
    "Wie heisst du?", "Lege deinen Administrator-Zugang fest (Tab wechselt das Feld, Enter bestätigt).", "Benutzername (a-z, 0-9, _ - .)", "Passwort (mindestens 6 Zeichen)", "Passwort wiederholen",
    "Benutzername: 1-15 Zeichen aus a-z 0-9 _ - .", "Passwort: mindestens 6 Zeichen.", "Die Passwörter sind verschieden.",
    "Netzwerk", "Für die Installation nicht nötig.", "Keine unterstützte Netzwerkkarte gefunden.", "Netzwerkkarte gefunden, noch keine Adresse.", "Verbunden, IP-Adresse:", "WLAN wird noch nicht unterstützt. Nutze ein Kabel oder USB-Tethering.", "Netzwerk prüfen", "Suche Adresse per DHCP ...",
    "Bereit zum Installieren", "Bitte prüfe deine Angaben.", "Sprache", "Tastatur", "Zielplatte", "Administrator", "Die Platte wird neu aufgeteilt (EFI-Start und Zynex-Daten).", "Alle bisherigen Daten auf der Platte gehen verloren.",
    "Zynex OS wird installiert", "Bitte nicht ausschalten.", "Zynex OS ist installiert.", "Entferne die ISO oder den USB-Stick und starte den Computer neu.", "Melde dich danach mit deinem Konto an.", "Die Installation ist fehlgeschlagen.", "Der Grund steht in der seriellen Ausgabe. Prüfe die Platte und versuche es erneut.",
    "Weiter", "Zurück", "Installieren", "Neustart", "Erkennen [E]", "Übernehmen", "Abbrechen", "Wie soll die Platte eingeteilt werden?", "Wähle, wie viel Platz Zynex OS bekommt (Taste 1 oder 2, Pfeiltasten oder Maus).", "Ganze Platte verwenden (empfohlen)", "Eigene Größe für Zynex festlegen", "EFI-Start", "Zynex-Daten", "KI-Modell", "Frei", "Größe der Zynex-Daten:", "Der freie Platz wird nicht angelegt und nicht formatiert." },
{   /* English */
    "Which language do you speak?", "Choose with the mouse or a number key (1-5).",
    "Which keyboard do you use?", "Pick a layout or let Zynex detect it by pressing keys (key E).",
    "Detect keyboard", "Press the key that shows this character:", "If there is no such key, press the space bar.", "Detected:", "Not detected. Please pick the layout from the list.",
    "Where should Zynex OS go?", "Choose the disk (number key or mouse). Its contents will be erased.", "No disk found (NVMe or SATA).", "WARNING: All data on the chosen disk will be erased.", "No installation package available.", "No disk selected.",
    "What is your name?", "Set up your administrator account (Tab switches field, Enter confirms).", "User name (a-z, 0-9, _ - .)", "Password (at least 6 characters)", "Repeat password",
    "User name: 1-15 characters from a-z 0-9 _ - .", "Password: at least 6 characters.", "The passwords do not match.",
    "Network", "Not needed for the installation.", "No supported network card found.", "Network card found, no address yet.", "Connected, IP address:", "Wi-Fi is not supported yet. Use a cable or USB tethering.", "Check network", "Looking for an address via DHCP ...",
    "Ready to install", "Please check your choices.", "Language", "Keyboard", "Target disk", "Administrator", "The disk will be repartitioned (EFI boot and Zynex data).", "All existing data on the disk will be lost.",
    "Installing Zynex OS", "Please do not switch off.", "Zynex OS is installed.", "Remove the ISO or USB stick and restart the computer.", "Then sign in with your account.", "The installation failed.", "The reason is in the serial output. Check the disk and try again.",
    "Next", "Back", "Install", "Restart", "Detect [E]", "Use this", "Cancel", "How should the disk be divided?", "Choose how much space Zynex OS gets (key 1 or 2, arrow keys or mouse).", "Use the whole disk (recommended)", "Set a custom size for Zynex", "EFI boot", "Zynex data", "AI model", "Free", "Size of the Zynex data:", "The free space is not created and not formatted." },
{   /* Francais */
    "Quelle langue parlez-vous ?", "Choisissez avec la souris ou une touche chiffre (1-5).",
    "Quel clavier utilisez-vous ?", "Choisissez une disposition ou faites-la détecter en appuyant sur des touches (touche E).",
    "Détecter le clavier", "Appuyez sur la touche qui porte ce caractère :", "S'il n'existe pas, appuyez sur la barre d'espace.", "Détecté :", "Non détecté. Choisissez la disposition dans la liste.",
    "Où installer Zynex OS ?", "Choisissez le disque (touche chiffre ou souris). Son contenu sera effacé.", "Aucun disque trouvé (NVMe ou SATA).", "ATTENTION : toutes les données du disque choisi seront effacées.", "Aucun paquet d'installation disponible.", "Aucun disque choisi.",
    "Comment vous appelez-vous ?", "Créez votre accès administrateur (Tab change de champ, Entrée valide).", "Nom d'utilisateur (a-z, 0-9, _ - .)", "Mot de passe (6 caractères minimum)", "Répéter le mot de passe",
    "Nom : 1 à 15 caractères parmi a-z 0-9 _ - .", "Mot de passe : 6 caractères minimum.", "Les mots de passe sont différents.",
    "Réseau", "Inutile pour l'installation.", "Aucune carte réseau prise en charge.", "Carte réseau trouvée, pas encore d'adresse.", "Connecté, adresse IP :", "Le Wi-Fi n'est pas encore pris en charge. Utilisez un câble ou le partage USB.", "Tester le réseau", "Recherche d'une adresse par DHCP ...",
    "Prêt à installer", "Vérifiez vos choix.", "Langue", "Clavier", "Disque cible", "Administrateur", "Le disque sera repartitionné (démarrage EFI et données Zynex).", "Toutes les données existantes seront perdues.",
    "Installation de Zynex OS", "Ne pas éteindre, s'il vous plaît.", "Zynex OS est installé.", "Retirez l'ISO ou la clé USB et redémarrez l'ordinateur.", "Connectez-vous ensuite avec votre compte.", "L'installation a échoué.", "La cause figure dans la sortie série. Vérifiez le disque et réessayez.",
    "Suivant", "Retour", "Installer", "Redémarrer", "Détecter [E]", "Utiliser", "Annuler", "Comment partager le disque ?", "Choisissez la place de Zynex OS (touche 1 ou 2, flèches ou souris).", "Utiliser tout le disque (recommandé)", "Définir une taille pour Zynex", "Démarrage EFI", "Données Zynex", "Modèle IA", "Libre", "Taille des données Zynex :", "L'espace libre n'est ni créé ni formaté." },
{   /* Italiano */
    "Che lingua parli?", "Scegli con il mouse o con un tasto numerico (1-5).",
    "Che tastiera usi?", "Scegli una disposizione o falla riconoscere premendo dei tasti (tasto E).",
    "Riconosci tastiera", "Premi il tasto con questo carattere:", "Se non esiste, premi la barra spaziatrice.", "Riconosciuta:", "Non riconosciuta. Scegli la disposizione dall'elenco.",
    "Dove installare Zynex OS?", "Scegli il disco (tasto numerico o mouse). Il contenuto verrà cancellato.", "Nessun disco trovato (NVMe o SATA).", "ATTENZIONE: tutti i dati del disco scelto verranno cancellati.", "Nessun pacchetto di installazione disponibile.", "Nessun disco scelto.",
    "Come ti chiami?", "Crea il tuo accesso da amministratore (Tab cambia campo, Invio conferma).", "Nome utente (a-z, 0-9, _ - .)", "Password (almeno 6 caratteri)", "Ripeti la password",
    "Nome: da 1 a 15 caratteri tra a-z 0-9 _ - .", "Password: almeno 6 caratteri.", "Le password sono diverse.",
    "Rete", "Non necessaria per l'installazione.", "Nessuna scheda di rete supportata.", "Scheda di rete trovata, ancora nessun indirizzo.", "Connesso, indirizzo IP:", "Il Wi-Fi non è ancora supportato. Usa un cavo o il tethering USB.", "Verifica rete", "Ricerca indirizzo via DHCP ...",
    "Pronto per l'installazione", "Controlla le tue scelte.", "Lingua", "Tastiera", "Disco di destinazione", "Amministratore", "Il disco verrà ripartizionato (avvio EFI e dati Zynex).", "Tutti i dati presenti andranno persi.",
    "Installazione di Zynex OS", "Non spegnere, per favore.", "Zynex OS è installato.", "Rimuovi l'ISO o la chiavetta USB e riavvia il computer.", "Poi accedi con il tuo account.", "Installazione non riuscita.", "Il motivo è nell'uscita seriale. Controlla il disco e riprova.",
    "Avanti", "Indietro", "Installa", "Riavvia", "Riconosci [E]", "Usa", "Annulla", "Come dividere il disco?", "Scegli quanto spazio ha Zynex OS (tasto 1 o 2, frecce o mouse).", "Usa tutto il disco (consigliato)", "Imposta una dimensione per Zynex", "Avvio EFI", "Dati Zynex", "Modello IA", "Libero", "Dimensione dei dati Zynex:", "Lo spazio libero non viene creato né formattato." },
{   /* Espanol */
    "¿Qué idioma hablas?", "Elige con el ratón o con una tecla numérica (1-5).",
    "¿Qué teclado usas?", "Elige una distribución o deja que se detecte pulsando teclas (tecla E).",
    "Detectar teclado", "Pulsa la tecla que muestra este carácter:", "Si no existe, pulsa la barra espaciadora.", "Detectado:", "No detectado. Elige la distribución de la lista.",
    "¿Dónde instalar Zynex OS?", "Elige el disco (tecla numérica o ratón). Se borrará su contenido.", "No se encontró ningún disco (NVMe o SATA).", "ATENCIÓN: se borrarán todos los datos del disco elegido.", "No hay paquete de instalación.", "Ningún disco elegido.",
    "¿Cómo te llamas?", "Crea tu acceso de administrador (Tab cambia de campo, Intro confirma).", "Nombre de usuario (a-z, 0-9, _ - .)", "Contraseña (mínimo 6 caracteres)", "Repetir contraseña",
    "Nombre: de 1 a 15 caracteres entre a-z 0-9 _ - .", "Contraseña: mínimo 6 caracteres.", "Las contraseñas no coinciden.",
    "Red", "No hace falta para la instalación.", "No hay tarjeta de red compatible.", "Tarjeta de red encontrada, aún sin dirección.", "Conectado, dirección IP:", "El Wi-Fi aún no es compatible. Usa un cable o compartir por USB.", "Comprobar red", "Buscando dirección por DHCP ...",
    "Listo para instalar", "Revisa tus opciones.", "Idioma", "Teclado", "Disco de destino", "Administrador", "El disco se reparticionará (arranque EFI y datos de Zynex).", "Se perderán todos los datos existentes.",
    "Instalando Zynex OS", "No apagues el equipo, por favor.", "Zynex OS está instalado.", "Quita la ISO o el USB y reinicia el equipo.", "Después inicia sesión con tu cuenta.", "La instalación ha fallado.", "El motivo está en la salida serie. Revisa el disco e inténtalo de nuevo.",
    "Siguiente", "Atrás", "Instalar", "Reiniciar", "Detectar [E]", "Usar", "Cancelar", "¿Cómo dividir el disco?", "Elige cuánto espacio tiene Zynex OS (tecla 1 o 2, flechas o ratón).", "Usar todo el disco (recomendado)", "Definir un tamaño para Zynex", "Arranque EFI", "Datos de Zynex", "Modelo de IA", "Libre", "Tamaño de los datos de Zynex:", "El espacio libre no se crea ni se formatea." },
};

/* ---------- Zustand ---------- */
struct knopf { int x, y, w, h, id; };
static struct knopf kn[8]; static int nkn;
struct feldz { int x, y, w, h; };
static struct feldz fz[3]; static int nfz;
struct zeile { int x, y, w, h, nr; };
static struct zeile zl[16]; static int nzl;

static int aktiv, seite, W, H, cx, cw;
static int lang = 0, lay = 0;
static char disks[8][16]; static uint32_t disk_mib[8]; static int ndisk, sel = 0;
static char fname[16], fpw[40], fpw2[40]; static int ffocus;
static int meldung = -1;                                  /* -1 keine, sonst Text-Nummer */
static int netz_zustand = -1; static char netz_ip[20];
static int fehler_inst = 0, was_down = 0;
static int det_schritt, det_ergebnis;                    /* Tastenerkennung: Frage Nr., Ergebnis (-1 = keines, -2 = nicht erkannt) */

#define QN 32
static volatile uint16_t kq[QN]; static volatile unsigned qh, qt;

static int slen(const char* s) { int n = 0; while (s[n]) n++; return n; }
static void scopy(char* d, const char* s, int max) { int i = 0; while (s[i] && i < max - 1) { d[i] = s[i]; i++; } d[i] = 0; }
static void dec(char* out, uint32_t v) { char t[12]; int n = 0; if (!v) t[n++] = '0'; while (v) { t[n++] = (char)('0' + v % 10); v /= 10; } int k = 0; while (n) out[k++] = t[--n]; out[k] = 0; }
static void cat(char* d, const char* s, int max) { int n = slen(d); scopy(d + n, s, max - n); }
static const char* T(int id) { return TX[lang][id]; }

/* UTF-8 -> Latin-1 (Schrift ist Latin-1); Latin-1-Bytes ohne UTF-8-Folgebyte bleiben unveraendert */
static const char* l1(const char* s) {
    static char buf[4][200]; static int bi;
    char* o = buf[bi = (bi + 1) & 3]; int n = 0;
    for (int i = 0; s[i] && n < 198; i++) {
        unsigned char c = (unsigned char)s[i], d = (unsigned char)s[i + 1];
        if ((c == 0xC3 || c == 0xC2) && d >= 0x80 && d <= 0xBF) { o[n++] = (char)(c == 0xC3 ? d + 0x40 : d); i++; }
        else o[n++] = (char)c;
    }
    o[n] = 0; return o;
}
static void gtxt(int x, int y, const char* s, uint32_t c, int sc) { gfx_text(x, y, l1(s), c, sc); }
static int gtw(const char* s, int sc) { return gfx_text_w(l1(s), sc); }

/* ---------- Zeichnen ---------- */
static int isq(int v) { int r = 0; while ((r + 1) * (r + 1) <= v) r++; return r; }
static void rrect(int x, int y, int w, int h, int r, uint32_t c) {
    if (r * 2 > h) r = h / 2;
    if (r * 2 > w) r = w / 2;
    gfx_rect(x, y + r, w, h - 2 * r, c);
    for (int j = 0; j < r; j++) {
        int dy = r - j, in = r - isq(r * r - dy * dy + 1); if (in < 0) in = 0;
        gfx_rect(x + in, y + j, w - 2 * in, 1, c); gfx_rect(x + in, y + h - 1 - j, w - 2 * in, 1, c);
    }
}
static void rrect_rand(int x, int y, int w, int h, int r, uint32_t fill, uint32_t rand) { rrect(x, y, w, h, r, rand); rrect(x + 1, y + 1, w - 2, h - 2, r - 1 > 0 ? r - 1 : 0, fill); }

static void knopf(int id, const char* label, int x, int y, int w, int an, int primaer) {
    uint32_t f = !an ? C_OFF : primaer ? C_ACC : C_BORDER;
    rrect(x, y, w, 40, 20, f);
    gtxt(x + (w - gtw(label, 1)) / 2, y + 12, label, an ? C_TEXT : C_MUTED, 1);
    if (nkn < 8) { kn[nkn].x = x; kn[nkn].y = y; kn[nkn].w = w; kn[nkn].h = 40; kn[nkn].id = id; nkn++; }
    kprint("[GUI] Knopf "); kprint(knopfname[id]); kprint(" x="); kprint_dec((uint32_t)(x + w / 2)); kprint(" y="); kprint_dec((uint32_t)(y + 20)); kprint("\n");
}
static void zeile_reg(int nr, int x, int y, int w, int h) { if (nzl < 16) { zl[nzl].x = x; zl[nzl].y = y; zl[nzl].w = w; zl[nzl].h = h; zl[nzl].nr = nr; nzl++; } }
/* Auswahl-Chip: Nummer-Plakette + Text */
static void chip(int nr, const char* text, int gewaehlt, int x, int y, int w, int h) {
    rrect_rand(x, y, w, h, 12, gewaehlt ? C_SEL : C_CARD, gewaehlt ? C_ACC : C_BORDER);
    rrect(x + 10, y + (h - 22) / 2, 22, 22, 11, gewaehlt ? C_ACC : C_BORDER);
    char d[4]; dec(d, (uint32_t)(nr + 1)); gtxt(x + 10 + (22 - gtw(d, 1)) / 2, y + (h - 16) / 2, d, C_TEXT, 1);
    gtxt(x + 44, y + (h - 16) / 2, text, C_TEXT, 1);
    zeile_reg(nr, x, y, w, h);
}
static void feld(int idx, const char* label, const char* text, int maskiert, int y) {
    gtxt(cx, y, label, C_MUTED, 1);
    rrect_rand(cx, y + 22, cw, 40, 10, C_FIELD, idx == ffocus ? C_ACC : C_BORDER);
    char t[44]; int n = slen(text); if (n > 40) n = 40;
    for (int i = 0; i < n; i++) t[i] = maskiert ? '*' : text[i];
    t[n] = 0;
    gfx_text(cx + 14, y + 22 + 12, t, C_TEXT, 1);
    if (idx == ffocus) gfx_rect(cx + 14 + n * 8, y + 22 + 10, 2, 20, C_TEXT);
    if (nfz < 3) { fz[nfz].x = cx; fz[nfz].y = y + 22; fz[nfz].w = cw; fz[nfz].h = 40; nfz++; }
}
/* Zeilenumbruch fuer laengere Texte */
static int absatz(int x, int y, int w, const char* text, uint32_t c) {
    const char* s = l1(text); static char z[200]; int maxc = w / 8, lines = 0;
    while (*s) {
        int len = 0, last = -1;
        while (s[len] && len < maxc) { if (s[len] == ' ') last = len; len++; }
        if (s[len] && last > 0) len = last;
        for (int i = 0; i < len; i++) z[i] = s[i];
        z[len] = 0;
        gfx_text(x, y + lines * 20, z, c, 1); lines++;
        s += len; while (*s == ' ') s++;
    }
    return lines;
}

#define DOTS 6
static int schritt_von(int s) { return s == S_SPRACHE ? 0 : (s == S_TASTATUR || s == S_ERKENN) ? 1 : (s == S_PLATTE || s == S_AUFTEIL) ? 2 : s == S_KONTO ? 3 : s == S_NETZ ? 4 : 5; }

/* Kopfzeile + Sprechblase des Assistenten; liefert die y-Position darunter */
static int kopf(const char* head, const char* sub, int y0) {
    gfx_rect(0, 0, W, 3, C_ACC);
    if (seite != S_SPRACHE) {
        rrect(cx, 22, 28, 28, 14, C_ACC); gfx_text(cx + 10, 28, "Z", C_TEXT, 1);
        gtxt(cx + 38, 26, "Zynex OS", C_TEXT, 1);
        int st = schritt_von(seite);
        for (int i = 0; i < DOTS; i++) rrect(cx + cw - (DOTS - i) * 18, 30, 10, 10, 5, i == st ? C_ACC : i < st ? C_SEL : C_BORDER);
    }
    int sub_l = 1; { int n = slen(l1(sub)); sub_l = n > (cw - 100) / 8 ? 2 : 1; }
    int h = 36 + 36 + sub_l * 20 + 14;
    rrect_rand(cx, y0, cw, h, 18, C_CARD, C_BORDER);
    rrect(cx + 16, y0 + 18, 40, 40, 20, C_ACC); gfx_text(cx + 16 + 16, y0 + 18 + 12, "Z", C_TEXT, 1);
    gtxt(cx + 72, y0 + 16, head, C_TEXT, 2);
    absatz(cx + 72, y0 + 56, cw - 100, sub, C_MUTED);
    return y0 + h + 16;
}

static void platten_laden(void) {
    ndisk = 0;
    for (int i = 0; i < block_count() && ndisk < 6; i++) {
        const char* n = block_name(i);
        if (!n || block_is_part(n) || (n[0] == 'r' && n[1] == 'a')) continue;
        scopy(disks[ndisk], n, 16); disk_mib[ndisk] = (uint32_t)block_sectors(n) / 2048; ndisk++;
    }
    if (sel >= ndisk) sel = 0;
}

/* ---------- Aufteilung ---------- */
static int auf_modus;                                   /* 0 = ganze Platte, 1 = eigene Groesse */
static uint32_t auf_mib;                                /* Groesse der Zynex-Daten bei eigener Groesse */
#define AUF_MIN 1024u
static uint32_t auf_efi(void) { return GPT_P2_START / 2048u; }
static uint32_t auf_modell(void) { return g_boot.model_size ? gpt_modell_sektoren(g_boot.model_size) / 2048u : 0; }
static uint32_t auf_max(void) { uint32_t t = ndisk ? disk_mib[sel] : 0, r = auf_efi() + auf_modell() + 2; return t > r ? t - r : 0; }
static int auf_eigen_moeglich(void) { return auf_max() >= AUF_MIN + 1024u; }
static uint32_t auf_schritt(void) { return auf_max() > 65536u ? 4096u : 1024u; }
static void auf_klemmen(void) { uint32_t mx = auf_max(); if (auf_mib > mx) auf_mib = mx; if (auf_mib < AUF_MIN) auf_mib = AUF_MIN; }
static void auf_init(void) { auf_modus = 0; uint32_t mx = auf_max(); auf_mib = (mx / 2) & ~1023u; auf_klemmen(); }
static void auf_aendern(int d) {
    if (!auf_modus) return;
    uint32_t st = auf_schritt();
    if (d > 0) auf_mib += st; else if (auf_mib > st) auf_mib -= st; else auf_mib = 0;
    auf_klemmen();
}
static void groesse_text(char* t, uint32_t mib, int max) {
    char d[12];
    if (mib >= 1024) { dec(d, mib / 1024); scopy(t, d, max); cat(t, ".", max); dec(d, (mib % 1024) * 10 / 1024); cat(t, d, max); cat(t, " GiB", max); }
    else { dec(d, mib); scopy(t, d, max); cat(t, " MiB", max); }
}

static void zeile_zus(int y, const char* label, const char* wert) {
    char t[64];
    gtxt(cx + 20, y, label, C_MUTED, 1);
    scopy(t, wert, 64); gtxt(cx + 200, y, t, C_TEXT, 1);
}

static void zeichne(void) {
    gfx_begin();
    nkn = 0; nfz = 0; nzl = 0;
    gfx_rect(0, 0, W, H, C_BG);
    int by = H - 72, bx = cx + cw;
    kprint("[GUI] Seite "); kprint_dec((uint32_t)(seite + 1)); kprint(": "); kprint(seitenname[seite]); kprint("\n");
    switch (seite) {
    case S_SPRACHE: {
        gfx_bitmap((W - zynex_logo_w) / 2, 22, zynex_logo_w, zynex_logo_h, zynex_logo);
        gfx_rect(0, 0, W, 3, C_ACC);
        int y = kopf(T(T_SPR_H), T(T_SPR_S), 22 + zynex_logo_h + 16);
        int cwid = (cw - 12) / 2;
        for (int i = 0; i < NL; i++) {
            int x = cx + (i % 2) * (cwid + 12), yy = y + (i / 2) * 52;
            chip(i, lang_name[i], i == lang, x, yy, cwid, 44);
            kprint("[GUI] Sprache "); kprint(lang_code[i]); kprint(" x="); kprint_dec((uint32_t)(x + cwid / 2)); kprint(" y="); kprint_dec((uint32_t)(yy + 22)); kprint("\n");
        }
        knopf(B_WEITER, T(T_B_WEITER), bx - 160, by, 160, 1, 1);
        break; }
    case S_TASTATUR: {
        int y = kopf(T(T_TAS_H), T(T_TAS_S), 70);
        int cwid = (cw - 12) / 2;
        for (int i = 0; i < KBD_LAYOUTS; i++) {
            int x = cx + (i % 2) * (cwid + 12), yy = y + (i / 2) * 52;
            chip(i, kbd_layouts[i].name, i == lay, x, yy, cwid, 44);
            kprint("[GUI] Tastatur "); kprint(kbd_layouts[i].code); kprint(" x="); kprint_dec((uint32_t)(x + cwid / 2)); kprint(" y="); kprint_dec((uint32_t)(yy + 22)); kprint("\n");
        }
        knopf(B_ERKENNEN, T(T_B_ERKENNEN), cx, y + ((KBD_LAYOUTS + 1) / 2) * 52 + 8, 200, 1, 0);
        knopf(B_ZURUECK, T(T_B_ZURUECK), bx - 340, by, 160, 1, 0);
        knopf(B_WEITER, T(T_B_WEITER), bx - 160, by, 160, 1, 1);
        break; }
    case S_ERKENN: {
        static const char* const zeichen[5] = { "Z", "\xC3\x9F", "\xC3\x91", "\xC3\xAC", "\xC2\xA3" };
        int y = kopf(T(T_ERK_H), det_ergebnis == -1 ? T(T_ERK_NONE) : det_ergebnis == -2 ? T(T_ERK_FAIL) : T(T_ERK_RES), 70);
        if (det_ergebnis == -1) {
            gtxt(cx, y + 4, T(T_ERK_Q), C_TEXT, 1);
            rrect_rand(cx + cw / 2 - 60, y + 40, 120, 120, 24, C_FIELD, C_ACC);
            const char* z = zeichen[det_schritt]; int tw = gtw(z, 6);
            gtxt(cx + cw / 2 - tw / 2, y + 40 + 12, z, C_TEXT, 6);
            knopf(B_ABBRUCH, T(T_B_ABBRUCH), bx - 160, by, 160, 1, 0);
        } else if (det_ergebnis >= 0) {
            gtxt(cx, y + 4, T(T_ERK_RES), C_MUTED, 1);
            gtxt(cx, y + 36, kbd_layouts[det_ergebnis].name, C_OK, 2);
            knopf(B_ABBRUCH, T(T_B_ABBRUCH), bx - 340, by, 160, 1, 0);
            knopf(B_UEBERNEHMEN, T(T_B_UEBERNEHMEN), bx - 160, by, 160, 1, 1);
            kprint("[GUI] Erkannt "); kprint(kbd_layouts[det_ergebnis].code); kprint("\n");
        } else {
            knopf(B_ABBRUCH, T(T_B_ABBRUCH), bx - 160, by, 160, 1, 1);
        }
        break; }
    case S_PLATTE: {
        int y = kopf(T(T_PLA_H), T(T_PLA_S), 70);
        platten_laden();
        if (!ndisk) gtxt(cx, y, T(T_PLA_NONE), C_ERR, 1);
        for (int i = 0; i < ndisk; i++) {
            kprint("[GUI] Platte "); kprint_dec((uint32_t)(i + 1)); kprint(" "); kprint(disks[i]); kprint("\n");
            char t[48]; char d[12]; scopy(t, disks[i], 48); cat(t, "    ", 48); dec(d, disk_mib[i]); cat(t, d, 48); cat(t, " MiB", 48);
            chip(i, t, i == sel, cx, y + i * 50, cw, 42);
        }
        gtxt(cx, by - 56, T(T_PLA_WARN), C_WARN, 1);
        if (meldung >= 0) gtxt(cx, by - 32, T(meldung), C_ERR, 1);
        knopf(B_ZURUECK, T(T_B_ZURUECK), bx - 340, by, 160, 1, 0);
        knopf(B_WEITER, T(T_B_WEITER), bx - 160, by, 160, ndisk > 0 && installer_bereit(), 1);
        break; }
    case S_AUFTEIL: {
        int y = kopf(T(T_AUF_H), T(T_AUF_S), 70);
        uint32_t total = ndisk ? disk_mib[sel] : 1, efi = auf_efi(), mod = auf_modell();
        uint32_t daten = auf_modus ? auf_mib : auf_max(); if (daten > total) daten = total;
        uint32_t used = efi + daten + mod, frei = total > used ? total - used : 0;
        kprint("[GUI] Aufteilung "); kprint(auf_modus ? "eigene Groesse " : "ganze Platte "); kprint_dec(daten); kprint(" MiB\n");
        chip(0, T(T_AUF_ALL), !auf_modus, cx, y, cw, 42);
        { char t[64]; scopy(t, T(T_AUF_EIGEN), 64); chip(1, t, auf_modus, cx, y + 50, cw, 42); }
        int by2 = y + 116, bw = cw - 8, x = cx + 4;
        rrect_rand(cx, by2 - 6, cw, 46, 12, C_FIELD, C_BORDER);
        uint32_t seg[4] = { efi, daten, mod, frei }; uint32_t col[4] = { C_MUTED, C_ACC, C_OK, C_BORDER };
        for (int i = 0; i < 4; i++) {
            if (!seg[i]) continue;
            int w = (int)((uint64_t)seg[i] * (uint64_t)bw / total); if (w < 4) w = 4;
            if (x + w > cx + 4 + bw) w = cx + 4 + bw - x;
            if (w > 0) gfx_rect(x, by2, w, 34, col[i]);
            x += w;
        }
        const char* nm[4] = { T(T_AUF_EFI), T(T_AUF_SYS), T(T_AUF_MODELL), T(T_AUF_FREI) };
        int lx = cx, ly = by2 + 52;
        for (int i = 0; i < 4; i++) {
            if (!seg[i]) continue;
            char t[48]; char g[24]; groesse_text(g, seg[i], 24); scopy(t, nm[i], 48); cat(t, "  ", 48); cat(t, g, 48);
            gfx_rect(lx, ly + 2, 12, 12, col[i]); gtxt(lx + 18, ly, t, C_TEXT, 1);
            lx += 18 + gtw(t, 1) + 22; if (lx > cx + cw - 160) { lx = cx; ly += 22; }
        }
        if (auf_modus) {
            char t[48], g[24]; groesse_text(g, auf_mib, 24); scopy(t, T(T_AUF_GR), 48); cat(t, " ", 48); cat(t, g, 48);
            int sy = ly + 40;
            gtxt(cx, sy + 12, t, C_TEXT, 1);
            knopf(B_WENIGER, "-", cx + cw - 110, sy, 50, 1, 0);
            knopf(B_MEHR, "+", cx + cw - 50, sy, 50, 1, 0);
        }
        if (frei) gtxt(cx, by - 56, T(T_AUF_NOTE), C_MUTED, 1);
        gtxt(cx, by - 32, T(T_PLA_WARN), C_WARN, 1);
        knopf(B_ZURUECK, T(T_B_ZURUECK), bx - 340, by, 160, 1, 0);
        knopf(B_WEITER, T(T_B_WEITER), bx - 160, by, 160, 1, 1);
        break; }
    case S_KONTO: {
        int y = kopf(T(T_KON_H), T(T_KON_S), 70);
        feld(0, T(T_KON_F1), fname, 0, y);
        feld(1, T(T_KON_F2), fpw, 1, y + 78);
        feld(2, T(T_KON_F3), fpw2, 1, y + 156);
        if (meldung >= 0) gtxt(cx, y + 236, T(meldung), C_ERR, 1);
        knopf(B_ZURUECK, T(T_B_ZURUECK), bx - 340, by, 160, 1, 0);
        knopf(B_WEITER, T(T_B_WEITER), bx - 160, by, 160, 1, 1);
        break; }
    case S_NETZ: {
        int y = kopf(T(T_NET_H), T(T_NET_S), 70);
        if (netz_zustand < 0) netz_zustand = net_status(netz_ip, 20);
        const char* t = netz_zustand == 0 ? T(T_NET_0) : netz_zustand == 1 ? T(T_NET_1) : T(T_NET_2);
        gtxt(cx, y, t, netz_zustand == 2 ? C_OK : C_WARN, 1);
        if (netz_zustand == 2) gfx_text(cx + gtw(t, 1) + 8, y, netz_ip, C_OK, 1);
        absatz(cx, y + 36, cw, T(T_NET_WLAN), C_MUTED);
        knopf(B_NETZTEST, T(T_NET_PRUEF), cx, y + 100, 220, 1, 0);
        knopf(B_ZURUECK, T(T_B_ZURUECK), bx - 340, by, 160, 1, 0);
        knopf(B_WEITER, T(T_B_WEITER), bx - 160, by, 160, 1, 1);
        break; }
    case S_ZUSAMMEN: {
        int y = kopf(T(T_ZUS_H), T(T_ZUS_S), 70);
        rrect_rand(cx, y, cw, 130, 14, C_CARD, C_BORDER);
        zeile_zus(y + 16, T(T_ZUS_SPR), lang_name[lang]);
        zeile_zus(y + 44, T(T_ZUS_TAS), kbd_layouts[lay].name);
        zeile_zus(y + 72, T(T_ZUS_PLA), ndisk ? disks[sel] : "?");
        zeile_zus(y + 100, T(T_ZUS_ADM), fname);
        gtxt(cx, y + 150, T(T_ZUS_W1), C_WARN, 1);
        gtxt(cx, y + 172, T(T_ZUS_W2), C_WARN, 1);
        knopf(B_ZURUECK, T(T_B_ZURUECK), bx - 360, by, 160, 1, 0);
        knopf(B_INSTALL, T(T_B_INSTALL), bx - 180, by, 180, 1, 1);
        break; }
    case S_INSTALL:
        kopf(T(T_INS_H), T(T_INS_S), 70);
        break;
    case S_FERTIG:
        if (!fehler_inst) {
            int y = kopf(T(T_FER_H), T(T_FER_S1), 70);
            gtxt(cx, y, T(T_FER_S2), C_MUTED, 1);
            knopf(B_NEUSTART, T(T_B_NEUSTART), bx - 180, by, 180, 1, 1);
        } else {
            kopf(T(T_FEH_H), T(T_FEH_S), 70);
            knopf(B_ZURUECK, T(T_B_ZURUECK), bx - 160, by, 160, 1, 1);
        }
        break;
    }
    gfx_end();
    hv_status_zeichnen();
}

static void balken(int p, const char* text) {
    gfx_begin();
    int y = 260;
    rrect_rand(cx, y, cw, 30, 15, C_FIELD, C_BORDER);
    if (p > 0) rrect(cx + 3, y + 3, (cw - 6) * p / 100 < 24 ? 24 : (cw - 6) * p / 100, 24, 12, C_ACC);
    gfx_rect(cx, y + 44, cw, 20, C_BG);
    gtxt(cx, y + 46, text, C_MUTED, 1);
    char t[8]; dec(t, (uint32_t)p); cat(t, " %", 8);
    gfx_rect(cx + cw - 70, y - 28, 70, 20, C_BG); gfx_text(cx + cw - gfx_text_w(t, 1), y - 26, t, C_TEXT, 1);
    gfx_end();
}
static void inst_cb(int p, const char* text) {
    balken(p, text);
    kprint("[GUI] Fortschritt "); kprint_dec((uint32_t)p); kprint(" "); kprint(text); kprint("\n");
}

static int name_ok(const char* n) {
    int l = slen(n); if (l < 1 || l > 15) return 0;
    for (int i = 0; i < l; i++) { char c = n[i]; if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.')) return 0; }
    return 1;
}

static void gehe(int neu) { meldung = -1; seite = neu; if (neu == S_NETZ) netz_zustand = -1; zeichne(); }

static void melde(int id, const char* logtext) {
    meldung = id; zeichne();
    kprint("[GUI] Meldung "); kprint(logtext); kprint("\n");
}

static void installieren(void) {
    static struct install_cfg cfg;
    scopy(cfg.disk, disks[sel], 16); scopy(cfg.user, fname, 16); scopy(cfg.pw, fpw, 40);
    scopy(cfg.layout, kbd_layouts[lay].code, 4); scopy(cfg.sprache, lang_code[lang], 3); cfg.daten_mib = auf_modus ? auf_mib : 0;
    seite = S_INSTALL; zeichne(); inst_cb(1, "Start");
    int r = installer_start(&cfg, inst_cb);
    for (int i = 0; i < 40; i++) { cfg.pw[i] = 0; fpw[i] = 0; fpw2[i] = 0; }
    fehler_inst = r != 0;
    kprint(r == 0 ? "[GUI] Installation fertig\n" : "[GUI] Installation fehlgeschlagen\n");
    seite = S_FERTIG; zeichne();
}

static void waehle_layout(int id) { lay = id; usb_set_layout(id); zeichne(); }

static void erkennung_start(void) { det_schritt = 0; det_ergebnis = -1; gehe(S_ERKENN); }

/* Entscheidungsbaum: gedrueckt wird die Taste mit dem gezeigten Zeichen; ausgewertet wird ihre Position (HID-Nummer) */
static void erkennung_taste(uint8_t k) {
    if (det_ergebnis != -1) return;
    int r = -3;                                            /* -3 = naechste Frage */
    switch (det_schritt) {
    case 0:  /* Z */  if (k == 0x1C) det_schritt = 1; else if (k == 0x1A) r = kbd_find("fr"); else if (k == 0x1D) det_schritt = 2; else r = -2; break;
    case 1:  /* ß */  r = kbd_find(k == 0x2D ? "de" : "ch"); break;
    case 2:  /* Ñ */  if (k == 0x33) r = kbd_find("es"); else det_schritt = 3; break;
    case 3:  /* ì */  if (k == 0x2E) r = kbd_find("it"); else det_schritt = 4; break;
    default: /* £ */  r = kbd_find(k == 0x20 ? "uk" : "us"); break;
    }
    if (r != -3) det_ergebnis = r;
    zeichne();
}

static void weiter(void) {
    if (seite == S_SPRACHE) gehe(S_TASTATUR);
    else if (seite == S_TASTATUR) gehe(S_PLATTE);
    else if (seite == S_PLATTE) {
        if (!installer_bereit()) { melde(T_PLA_NOPACK, "Kein Installationspaket vorhanden."); return; }
        if (ndisk <= 0) { melde(T_PLA_NOSEL, "Keine Platte gewaehlt."); return; }
        auf_init(); gehe(S_AUFTEIL);
    } else if (seite == S_AUFTEIL) { ffocus = 0; gehe(S_KONTO);
    } else if (seite == S_KONTO) {
        if (!name_ok(fname)) { melde(T_KON_E1, "Benutzername ungueltig"); return; }
        if (slen(fpw) < 6) { melde(T_KON_E2, "Passwort zu kurz"); return; }
        for (int i = 0; fpw[i] || fpw2[i]; i++) if (fpw[i] != fpw2[i]) { melde(T_KON_E3, "Die Passwoerter sind verschieden"); return; }
        gehe(S_NETZ);
    } else if (seite == S_NETZ) gehe(S_ZUSAMMEN);
}
static void zurueck(void) {
    if (seite == S_TASTATUR) gehe(S_SPRACHE); else if (seite == S_PLATTE) gehe(S_TASTATUR); else if (seite == S_AUFTEIL) gehe(S_PLATTE); else if (seite == S_KONTO) gehe(S_AUFTEIL);
    else if (seite == S_NETZ) gehe(S_KONTO); else if (seite == S_ZUSAMMEN) gehe(S_NETZ);
    else if (seite == S_ERKENN) gehe(S_TASTATUR);
    else if (seite == S_FERTIG && fehler_inst) gehe(S_ZUSAMMEN);
}

static void aktion(int id) {
    switch (id) {
    case B_WEITER: weiter(); break;
    case B_ZURUECK: case B_ABBRUCH: zurueck(); break;
    case B_INSTALL: installieren(); break;
    case B_ERKENNEN: erkennung_start(); break;
    case B_WENIGER: auf_aendern(-1); zeichne(); break;
    case B_MEHR: auf_aendern(1); zeichne(); break;
    case B_UEBERNEHMEN: waehle_layout(det_ergebnis); gehe(S_TASTATUR); break;
    case B_NEUSTART: kprint("[GUI] Neustart\n"); system_neustart(); break;
    case B_NETZTEST:
        gfx_begin(); gfx_rect(cx + 240, 304, cw - 240, 20, C_BG); gtxt(cx + 240, 304, T(T_NET_DHCP), C_MUTED, 1); gfx_end();
        net_dhcp(); netz_zustand = -1; zeichne(); break;
    }
}

static void waehle_sprache(int i) { lang = i; zeichne(); }

/* Taste (HID-Nummer; Zifferntasten und Steuertasten wirken in jeder Belegung gleich) */
static void taste(uint8_t k, int shift, int altgr) {
    if (seite == S_INSTALL) return;
    if (k == 0x29) { zurueck(); return; }                                  /* Esc */
    if (seite == S_ERKENN) { erkennung_taste(k); return; }
    int ziffer = (k >= 30 && k <= 38) ? k - 30 : -1;                       /* Tasten 1-9 */
    if (seite == S_SPRACHE) { if (ziffer >= 0 && ziffer < NL) waehle_sprache(ziffer); else if (k == 0x28) weiter(); return; }
    if (seite == S_TASTATUR) {
        if (ziffer >= 0 && ziffer < KBD_LAYOUTS) waehle_layout(ziffer);
        else if (k == 0x08) erkennung_start();                             /* E */
        else if (k == 0x28) weiter();
        return;
    }
    if (seite == S_PLATTE) {
        if (ziffer >= 0 && ziffer < ndisk) { sel = ziffer; zeichne(); }
        else if (k == 0x28) weiter();
        return;
    }
    if (seite == S_AUFTEIL) {
        if (ziffer == 0) { auf_modus = 0; zeichne(); }
        else if (ziffer == 1) { if (auf_eigen_moeglich()) { auf_modus = 1; auf_klemmen(); zeichne(); } }
        else if (k == 0x4F || k == 0x52 || k == 0x2E || k == 0x57) { auf_aendern(1); zeichne(); }
        else if (k == 0x50 || k == 0x51 || k == 0x2D || k == 0x56) { auf_aendern(-1); zeichne(); }
        else if (k == 0x28) weiter();
        return;
    }
    if (seite == S_KONTO) {
        char* t = ffocus == 0 ? fname : ffocus == 1 ? fpw : fpw2; int max = ffocus == 0 ? 15 : 39; int n = slen(t);
        if (k == 0x2B) { ffocus = (ffocus + 1) % 3; zeichne(); }
        else if (k == 0x28) { if (ffocus < 2) { ffocus++; zeichne(); } else weiter(); }
        else if (k == 0x2A) { if (n) { t[n - 1] = 0; zeichne(); } }
        else { char c = usb_key_char(k, shift, altgr); if (c >= 32 && c < 127 && n < max) { t[n] = c; t[n + 1] = 0; meldung = -1; zeichne(); } }
        return;
    }
    if (seite == S_NETZ) { if (k == 0x28) weiter(); return; }
    if (seite == S_ZUSAMMEN) { if (k == 0x28) installieren(); return; }
    if (seite == S_FERTIG) { if (k == 0x28) { if (fehler_inst) zurueck(); else aktion(B_NEUSTART); } return; }
}

static void klick(int x, int y) {
    if (seite == S_INSTALL) return;
    for (int i = 0; i < nkn; i++)
        if (x >= kn[i].x && x < kn[i].x + kn[i].w && y >= kn[i].y && y < kn[i].y + kn[i].h) { aktion(kn[i].id); return; }
    for (int i = 0; i < nzl; i++) {
        if (x < zl[i].x || x >= zl[i].x + zl[i].w || y < zl[i].y || y >= zl[i].y + zl[i].h) continue;
        if (seite == S_SPRACHE) waehle_sprache(zl[i].nr);
        else if (seite == S_TASTATUR) waehle_layout(zl[i].nr);
        else if (seite == S_PLATTE) { sel = zl[i].nr; zeichne(); }
        else if (seite == S_AUFTEIL) { if (zl[i].nr == 0) auf_modus = 0; else if (auf_eigen_moeglich()) { auf_modus = 1; auf_klemmen(); } zeichne(); }
        return;
    }
    if (seite == S_KONTO)
        for (int i = 0; i < nfz; i++) if (x >= fz[i].x && x < fz[i].x + fz[i].w && y >= fz[i].y && y < fz[i].y + fz[i].h) { ffocus = i; zeichne(); return; }
}

static void gui_rohtaste(uint8_t k, int shift, int altgr) { unsigned n = (qt + 1) % QN; if (n != qh) { kq[qt] = (uint16_t)(k | (shift << 8) | (altgr << 9)); qt = n; } }

static void gui_tick(void) {
    if (!aktiv) return;
    while (qh != qt) { uint16_t e = kq[qh]; qh = (qh + 1) % QN; taste((uint8_t)(e & 255), (e >> 8) & 1, (e >> 9) & 1); }
    const struct usb_pointer* p = usb_pointer_state();
    if (p && p->present) {
        int down = p->buttons & 1;
        if (down && !was_down) klick(p->x, p->y);
        was_down = down;
    }
}

int gui_start(void) {
    if (!gfx_available()) return -1;
    gfx_size(&W, &H);
    cw = W - 48 < 760 ? W - 48 : 760; cx = (W - cw) / 2;
    fname[0] = fpw[0] = fpw2[0] = 0; meldung = -1; ffocus = 0; seite = S_SPRACHE; sel = 0; qh = qt = 0; was_down = 0;
    lay = usb_get_layout(); lang = 0;
    aktiv = 1;
    gfx_console(0);
    usb_set_rawgrab(gui_rohtaste);                          /* Tasten als Positionen: Ziffernwahl und Erkennung gehen in jeder Belegung */
    shell_set_tick(gui_tick);
    kprint("[GUI] Bildschirm "); kprint_dec((uint32_t)W); kprint("x"); kprint_dec((uint32_t)H); kprint("\n");
    zeichne();
    return 0;
}
