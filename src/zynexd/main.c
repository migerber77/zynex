/* zynexd: das Hauptprogramm von Zynex.
 * Stand 0.2: Gesicht auf dem Bildschirm (Framebuffer), Textgespraech, Regelschicht.
 * Das echte Sprachmodell folgt; bis dahin erkennt ein Stichwort-Platzhalter die Absicht. */
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include "zynex_fb.h"
#include "zynex_llm.h"
#include "zynex_mem.h"
#include "zynex_model.h"
#include "zynex_web.h"
#include "zynex_rules.h"
#include "zynex_ui.h"

#define KDSETMODE 0x4B3A
#define KD_GRAPHICS 0x01

static int g_grafik; /* 1 = Framebuffer aktiv */

static uint32_t jetzt_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000u + (uint32_t)ts.tv_nsec / 1000000u);
}

static void kernel_log(const char *text)
{
    /* Erscheint auch auf der seriellen Konsole (fuer den Boottest). */
    int fd = open("/dev/kmsg", O_WRONLY);
    if (fd >= 0) {
        char buf[128];
        int n = snprintf(buf, sizeof(buf), "<6>%s\n", text);
        if (n > 0)
            (void)!write(fd, buf, (size_t)n);
        close(fd);
    }
}

static void zynex_sagt(zx_ui *u, const char *text, zx_mood m)
{
    zx_ui_say(u, 1, text);
    zx_face_set_mood(&u->face, m);
    if (!g_grafik)
        printf("Zynex: %s\n", text);
}


/* ---- Sprachmodell im Hintergrund, damit das Gesicht fluessig bleibt ---- */
static struct {
    pthread_t th;
    zx_llm *modell;
    char pfad[256];
    char eingabe[ZX_UI_LINE];
    char system[2048];
    char verlauf[ZX_VERLAUF_N * (2 * ZX_VERLAUF_LEN + 120) + 8];
    char antwort[ZX_UI_LINE];
    char fehler[160];
    double tps;
    volatile int laedt;       /* Lade-Thread laeuft */
    volatile int geladen;     /* Modell bereit */
    volatile int rechnet;     /* Antwort-Thread laeuft */
    volatile int antwort_da;  /* Antwort fertig, noch nicht abgeholt */
    int thread_offen;
} ki;

static zx_mem g_mem;
static pthread_mutex_t g_mem_mx = PTHREAD_MUTEX_INITIALIZER; /* schuetzt g_mem */
static pthread_mutex_t g_ki_mx = PTHREAD_MUTEX_INITIALIZER;  /* ein Modell, eine Antwort zur Zeit */
static int g_web_port = 8080;

static int stueck_cb(const char *s, void *n)
{
    size_t l = strlen(ki.antwort);
    (void)n;
    if (l + strlen(s) + 1 < sizeof(ki.antwort))
        strcat(ki.antwort, s);
    return 0;
}

static void *lade_thread(void *arg)
{
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    (void)arg;
    ki.modell = zx_llm_open(ki.pfad, 4096, n > 0 ? (int)n : 2, ki.fehler, sizeof(ki.fehler));
    __atomic_store_n(&ki.geladen, ki.modell != NULL, __ATOMIC_RELEASE);
    __atomic_store_n(&ki.laedt, 0, __ATOMIC_RELEASE);
    return NULL;
}

static void *frage_thread(void *arg)
{
    (void)arg;
    int n;
    ki.antwort[0] = '\0';
    pthread_mutex_lock(&g_ki_mx);
    n = zx_llm_antwort(ki.modell, ki.system, ki.verlauf, ki.eingabe, 160, stueck_cb, NULL, &ki.tps);
    pthread_mutex_unlock(&g_ki_mx);
    if (n < 0)
        snprintf(ki.antwort, sizeof(ki.antwort), "Hm, dabei ist mir ein Fehler passiert.");
    __atomic_store_n(&ki.antwort_da, 1, __ATOMIC_RELEASE);
    __atomic_store_n(&ki.rechnet, 0, __ATOMIC_RELEASE);
    return NULL;
}

static int web_bereit(void) { return getenv("ZYNEX_ATTRAPPE") != NULL || __atomic_load_n(&ki.geladen, __ATOMIC_ACQUIRE); }

static int web_frage(const char *verlauf, const char *frage, zx_llm_stueck cb, void *cbn, double *tps)
{
    char sys[2048];
    int n;
    if (getenv("ZYNEX_ATTRAPPE")) { /* Test ohne Modell: Frage zurueckgeben */
        char t[600];
        snprintf(t, sizeof(t), "Echo: %.200s | Verlauf: %zu Zeichen", frage, strlen(verlauf));
        cb(t, cbn);
        if (tps) *tps = 1.5;
        return 1;
    }
    pthread_mutex_lock(&g_mem_mx);
    zx_mem_system(&g_mem, zx_model_system(), sys, sizeof(sys));
    pthread_mutex_unlock(&g_mem_mx);
    pthread_mutex_lock(&g_ki_mx);
    n = zx_llm_antwort(ki.modell, sys, verlauf, frage, 220, cb, cbn, tps);
    pthread_mutex_unlock(&g_ki_mx);
    return n;
}

static void web_starten(void)
{
    static zx_web_host h;
    const char *p = getenv("ZYNEX_WEB_PORT");
    const char *nd = getenv("ZYNEX_NOTIZEN");
    if (p && atoi(p) > 0)
        g_web_port = atoi(p);
    h.mem = &g_mem;
    h.mem_mx = &g_mem_mx;
    h.notizdatei = nd ? nd : "/var/lib/zynex/notizen.txt";
    h.bereit = web_bereit;
    h.frage = web_frage;
    if (zx_web_start(&h, g_web_port) == 0)
        kernel_log("zynexd: web bereit");
}

static void ki_starte_laden(void)
{
    const char *p = getenv("ZYNEX_MODELL");
    snprintf(ki.pfad, sizeof(ki.pfad), "%s", p ? p : "/usr/share/zynex/modell.gguf");
    if (access(ki.pfad, R_OK) != 0 || !zx_llm_verfuegbar())
        return;
    ki.laedt = 1;
    if (pthread_create(&ki.th, NULL, lade_thread, NULL) == 0)
        ki.thread_offen = 1;
    else
        ki.laedt = 0;
}

/* ---------- Installation auf Festplatte (ruft /usr/bin/zynex-setup) ---------- */
static pid_t g_setup_pid;
static char g_ziel[32];           /* Vorschlag, wartet auf Bestaetigung */
static char g_ziele[8][32];
static char g_ziel_text[8][140];
static int g_nziele;
static char g_status_alt[64];

static void install_laden(void)
{
    FILE *p = popen("/usr/bin/zynex-setup liste 2>/dev/null", "r");
    char z[200];
    g_nziele = 0;
    if (!p)
        return;
    while (g_nziele < 8 && fgets(z, sizeof(z), p)) {
        char *a = strchr(z, '|'), *b;
        if (!a)
            continue;
        *a++ = '\0';
        b = strchr(a, '|');
        if (!b)
            continue;
        *b++ = '\0';
        b[strcspn(b, "\r\n")] = '\0';
        if (strcmp(z, "EFI") == 0)
            continue;
        snprintf(g_ziele[g_nziele], sizeof(g_ziele[0]), "%.31s", z);
        snprintf(g_ziel_text[g_nziele], sizeof(g_ziel_text[0]), "%.100s, %ld GB", b, (atol(a) + 512) / 1024);
        g_nziele++;
    }
    pclose(p);
}

/* nummer 0 = automatisch; sonst 1..n. Schreibt, was Zynex sagt. */
static void install_vorschlag(int nummer, char *out, size_t cap)
{
    int i;
    size_t o = 0;
    g_ziel[0] = '\0';
    install_laden();
    if (g_nziele == 0) {
        snprintf(out, cap, "Ich finde keine passende Festplatte. Sie muss gross genug sein und darf nicht der Stick sein, von dem ich gerade laufe. (Installation geht nur im BIOS-Startmodus.)");
        return;
    }
    if (nummer == 0 && g_nziele == 1)
        nummer = 1;
    if (nummer >= 1 && nummer <= g_nziele) {
        snprintf(g_ziel, sizeof(g_ziel), "%.31s", g_ziele[nummer - 1]);
        snprintf(out, cap, "Ich wuerde mich auf %.100s installieren (%.31s). ALLES darauf wird geloescht. Schreib 'ja, installieren' zum Bestaetigen oder 'nein'.", g_ziel_text[nummer - 1], g_ziele[nummer - 1]);
        return;
    }
    o += (size_t)snprintf(out, cap, "Auf welche Festplatte? ");
    for (i = 0; i < g_nziele && o < cap; i++)
        o += (size_t)snprintf(out + o, cap - o, "%d: %.100s. ", i + 1, g_ziel_text[i]);
    if (o < cap)
        snprintf(out + o, cap - o, "Schreib zum Beispiel 'installieren 1'.");
}

static int install_start(char *out, size_t cap)
{
    pid_t p = fork();
    if (p < 0) {
        snprintf(out, cap, "Ich konnte die Installation nicht starten.");
        return 0;
    }
    if (p == 0) {
        execl("/usr/bin/zynex-setup", "zynex-setup", "installieren", g_ziel, (char *)NULL);
        _exit(127);
    }
    g_setup_pid = p;
    g_status_alt[0] = '\0';
    g_ziel[0] = '\0';
    snprintf(out, cap, "Okay, ich installiere mich jetzt. Das dauert einige Minuten. Bitte nicht ausschalten.");
    return 1;
}

/* Rueckgabe 1, wenn die Zeile ein Installations-/Neustart-Befehl war. */
static int install_befehl(const char *zeile, char *out, size_t cap)
{
    char k[200];
    size_t i;
    for (i = 0; zeile[i] && i < sizeof(k) - 1; i++)
        k[i] = (zeile[i] >= 'A' && zeile[i] <= 'Z') ? (char)(zeile[i] + 32) : zeile[i];
    k[i] = '\0';
    if (g_setup_pid) {
        snprintf(out, cap, "Die Installation laeuft noch. Bitte warten.");
        return 1;
    }
    if (strcmp(k, "neustart") == 0) {
        snprintf(out, cap, "Ich starte neu.");
        if (system("reboot") != 0) { }
        return 1;
    }
    if (strcmp(k, "herunterfahren") == 0) {
        snprintf(out, cap, "Ich fahre herunter.");
        if (system("poweroff") != 0) { }
        return 1;
    }
    if (g_ziel[0] && strstr(k, "ja") && strstr(k, "installier"))
        return install_start(out, cap);
    if (g_ziel[0] && (strcmp(k, "nein") == 0 || strcmp(k, "abbrechen") == 0)) {
        g_ziel[0] = '\0';
        snprintf(out, cap, "Gut, ich installiere nichts.");
        return 1;
    }
    if (strncmp(k, "installier", 10) == 0 || strcmp(k, "setup") == 0) {
        const char *z = strchr(k, ' ');
        install_vorschlag(z ? atoi(z + 1) : 0, out, cap);
        return 1;
    }
    return 0;
}

/* Fortschritt der laufenden Installation melden. Rueckgabe: Text oder NULL. */
static const char *install_fortschritt(char *puf, size_t cap)
{
    FILE *f;
    char zeile[160] = "", letzt[160] = "";
    int fertig_prozess = 0, status;

    if (!g_setup_pid)
        return NULL;
    if (waitpid(g_setup_pid, &status, WNOHANG) == g_setup_pid) {
        g_setup_pid = 0;
        fertig_prozess = 1;
    }
    f = fopen("/run/zynex-setup.status", "r");
    if (f) {
        while (fgets(zeile, sizeof(zeile), f))
            snprintf(letzt, sizeof(letzt), "%s", zeile);
        fclose(f);
    }
    letzt[strcspn(letzt, "\r\n")] = '\0';
    if (strcmp(letzt, g_status_alt) == 0 && !fertig_prozess)
        return NULL;
    snprintf(g_status_alt, sizeof(g_status_alt), "%.63s", letzt);
    {
        char kl[100];
        snprintf(kl, sizeof(kl), "zynexd: setup %.80s", letzt);
        kernel_log(kl);
    }
    if (strcmp(letzt, "kopiere") == 0)
        return "Ich kopiere mich auf die Festplatte. Das ist der lange Teil.";
    if (strcmp(letzt, "partition") == 0)
        return "Kopiert. Ich passe jetzt die Partition an.";
    if (strcmp(letzt, "dateisystem") == 0)
        return "Ich vergroessere mein Dateisystem auf die ganze Platte.";
    if (strcmp(letzt, "start") == 0)
        return "Ich richte den Start ein.";
    if (strcmp(letzt, "fertig") == 0)
        return "Fertig! Ich bin installiert. Nimm jetzt den USB-Stick (oder die DVD) heraus und schreib 'neustart'.";
    if (strncmp(letzt, "fehler", 6) == 0) {
        snprintf(puf, cap, "Die Installation ist fehlgeschlagen (%s). Es wurde nichts am laufenden System veraendert.", letzt);
        return puf;
    }
    if (fertig_prozess)
        return "Die Installation wurde unerwartet beendet.";
    return NULL;
}

/* Aktion der Regelschicht unterwerfen. Gibt den Text zurueck, den Zynex dazu sagt. */
static void aktion_text(const char *aktion, char *offen, size_t offen_cap, char *out, size_t cap, zx_mood *m)
{
    switch (zx_rules_check(aktion)) {
    case ZX_FREI:
        snprintf(out, cap, "Gerne. %s (simuliert, Regel: frei).", aktion);
        *m = ZX_FREUNDLICH;
        break;
    case ZX_NACHFRAGEN:
        snprintf(offen, offen_cap, "%s", aktion);
        snprintf(out, cap, "Ich wuerde %s tun. Soll ich? Antworte mit ja oder nein.", aktion);
        *m = ZX_DENKT;
        break;
    default:
        snprintf(out, cap, "Das tue ich nicht. Die Regelschicht erlaubt es nie.");
        *m = ZX_UEBERRASCHT;
        break;
    }
}

static void verarbeite(zx_ui *u, const char *zeile, char *offen, size_t offen_cap)
{
    char aktion[64], antwort[200];

    zx_ui_say(u, 0, zeile);
    if (offen[0] != '\0') {
        if (strcmp(zeile, "ja") == 0 || strcmp(zeile, "Ja") == 0) {
            snprintf(antwort, sizeof(antwort), "Erledigt (simuliert): %s.", offen);
            zynex_sagt(u, antwort, ZX_FREUNDLICH);
        } else {
            zynex_sagt(u, "Gut, ich lasse es.", ZX_NEUTRAL);
        }
        offen[0] = '\0';
        return;
    }

    {
        char itext[500];
        if (install_befehl(zeile, itext, sizeof(itext))) {
            zynex_sagt(u, itext, ZX_FREUNDLICH);
            return;
        }
    }

    {
        char gtext[ZX_UI_LINE + 200];
        int gb;
        pthread_mutex_lock(&g_mem_mx);
        gb = zx_mem_befehl(&g_mem, zeile, gtext, sizeof(gtext));
        pthread_mutex_unlock(&g_mem_mx);
        if (!gb && (strstr(zeile, "adresse") || strstr(zeile, "Adresse") || strstr(zeile, "webseite") || strstr(zeile, "Webseite"))) {
            char url[80];
            if (zx_web_adresse(url, sizeof(url), g_web_port))
                snprintf(gtext, sizeof(gtext), "Meine Webseite erreichst du im Browser unter %s", url);
            else
                snprintf(gtext, sizeof(gtext), "Ich habe noch keine Netzwerkadresse. Ist ein Kabel angesteckt?");
            gb = 1;
        }
        if (gb) {
            zynex_sagt(u, gtext, ZX_FREUNDLICH);
            return;
        }
    }

    if (__atomic_load_n(&ki.geladen, __ATOMIC_ACQUIRE)) {
        if (__atomic_load_n(&ki.rechnet, __ATOMIC_ACQUIRE)) {
            zynex_sagt(u, "Einen Moment, ich denke noch.", ZX_DENKT);
            return;
        }
        if (ki.thread_offen) {
            pthread_join(ki.th, NULL);
            ki.thread_offen = 0;
        }
        snprintf(ki.eingabe, sizeof(ki.eingabe), "%s", zeile);
        pthread_mutex_lock(&g_mem_mx);
        zx_mem_system(&g_mem, zx_model_system(), ki.system, sizeof(ki.system));
        zx_mem_verlauf(&g_mem, ki.verlauf, sizeof(ki.verlauf));
        pthread_mutex_unlock(&g_mem_mx);
        __atomic_store_n(&ki.rechnet, 1, __ATOMIC_RELEASE);
        if (pthread_create(&ki.th, NULL, frage_thread, NULL) == 0) {
            ki.thread_offen = 1;
            zx_face_set_mood(&u->face, ZX_DENKT);
        } else {
            ki.rechnet = 0;
            zynex_sagt(u, "Ich konnte nicht nachdenken.", ZX_DENKT);
        }
        return;
    }

    if (!zx_model_intent(zeile, aktion, sizeof(aktion))) {
        zynex_sagt(u, "Das habe ich noch nicht verstanden.", ZX_DENKT);
        return;
    }
    {
        zx_mood m;
        aktion_text(aktion, offen, offen_cap, antwort, sizeof(antwort), &m);
        zynex_sagt(u, antwort, m);
    }
}

/* Fertige Modellantwort abholen und sprechen. */
static void ki_abholen(zx_ui *u, char *offen, size_t offen_cap)
{
    char aktion[64], zusatz[200], text[ZX_UI_LINE + 220];
    zx_mood m = ZX_FREUNDLICH;

    if (ki.thread_offen) {
        pthread_join(ki.th, NULL);
        ki.thread_offen = 0;
    }
    __atomic_store_n(&ki.antwort_da, 0, __ATOMIC_RELEASE);
    zusatz[0] = '\0';
    if (zx_model_split(ki.antwort, aktion, sizeof(aktion)))
        aktion_text(aktion, offen, offen_cap, zusatz, sizeof(zusatz), &m);
    if (ki.antwort[0]) {
        pthread_mutex_lock(&g_mem_mx);
        zx_mem_verlauf_add(&g_mem, ki.eingabe, ki.antwort);
        pthread_mutex_unlock(&g_mem_mx);
    }
    if (ki.antwort[0] && zusatz[0])
        snprintf(text, sizeof(text), "%s %s", ki.antwort, zusatz);
    else
        snprintf(text, sizeof(text), "%s", ki.antwort[0] ? ki.antwort : (zusatz[0] ? zusatz : "Dazu fehlen mir die Worte."));
    zynex_sagt(u, text, m);
}

/* Kopiert Merkzettel, Notiz-Titel und Modellstatus in die Oberflaeche. */
static void ui_auffrischen(zx_ui *u)
{
    FILE *f;
    const char *nd = getenv("ZYNEX_NOTIZEN");
    int i;

    u->bereit = __atomic_load_n(&ki.geladen, __ATOMIC_ACQUIRE);
    pthread_mutex_lock(&g_mem_mx);
    u->nzettel = g_mem.n;
    for (i = 0; i < g_mem.n; i++)
        snprintf(u->zettel[i], sizeof(u->zettel[i]), "%s", g_mem.zettel[i]);
    pthread_mutex_unlock(&g_mem_mx);
    u->nnotiz = 0;
    f = fopen(nd ? nd : "/var/lib/zynex/notizen.txt", "r");
    if (f) {
        char zeile[400];
        while (u->nnotiz < 24 && fgets(zeile, sizeof(zeile), f)) {
            char *tab = strchr(zeile, '\t');
            if (!tab)
                continue;
            *tab = '\0';
            snprintf(u->notiz[u->nnotiz++], sizeof(u->notiz[0]), "%.119s", zeile);
        }
        fclose(f);
    }
}

/* Ein Byte der Tastatur verarbeiten. Gibt 1 zurueck, wenn eine Zeile fertig ist. */
static int taste(zx_ui *u, unsigned char b, int *esc)
{
    size_t n = strlen(u->eingabe);

    if (*esc > 0) { /* Pfeiltasten etc. verschlucken */
        if (*esc == 1 && b != '[' && b != 'O')
            *esc = 0;
        else if (*esc >= 2 && ((b >= 'A' && b <= 'Z') || b == '~'))
            *esc = 0;
        else
            (*esc)++;
        return 0;
    }
    if (b == 0x1b) {
        *esc = 1;
        return 0;
    }
    if (b == '\t') {
        u->ansicht = (u->ansicht + 1) % 3;
        return 0;
    }
    if (b == '\r' || b == '\n')
        return 1;
    if (b == 0x7f || b == 0x08) {
        while (n > 0 && (u->eingabe[n - 1] & 0xC0) == 0x80)
            u->eingabe[--n] = '\0';
        if (n > 0)
            u->eingabe[n - 1] = '\0';
        return 0;
    }
    if (b >= 32 && n < ZX_UI_LINE - 2) {
        u->eingabe[n] = (char)b;
        u->eingabe[n + 1] = '\0';
    }
    return 0;
}

static int schreibe_ppm(const zx_canvas *c, const char *pfad)
{
    FILE *f = fopen(pfad, "wb");
    int i;
    if (!f)
        return -1;
    fprintf(f, "P6\n%d %d\n255\n", c->w, c->h);
    for (i = 0; i < c->w * c->h; i++) {
        unsigned char rgb[3] = {(unsigned char)(c->px[i] >> 16), (unsigned char)(c->px[i] >> 8), (unsigned char)c->px[i]};
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
    return 0;
}

/* Fuer Entwicklung: ein Standbild ohne Bildschirm erzeugen.
 * zynexd --standbild datei.ppm [ms_nach_sprechbeginn] [stimmung 0-5] */
static int standbild(int argc, char **argv)
{
    zx_ui u;
    zx_canvas c = {NULL, 1024, 768};
    uint32_t ms = argc > 3 ? (uint32_t)atoi(argv[3]) : 300;
    uint32_t t;

    c.px = calloc((size_t)c.w * (size_t)c.h, sizeof(uint32_t));
    if (!c.px)
        return 1;
    zx_ui_init(&u, 0);
    zx_ui_say(&u, 0, "zeig mir meine termine");
    zx_ui_say(&u, 1, "Gerne. Hallo Welt, ich bin Zynex und rede jetzt mit dir ueber alles Moegliche.");
    for (t = 0; t <= ms; t += 16)
        zx_face_update(&u.face, t);
    if (argc > 4)
        zx_face_set_mood(&u.face, (zx_mood)atoi(argv[4]));
    for (t = ms; t <= ms + 600; t += 16)
        zx_face_update(&u.face, t);
    strcpy(u.eingabe, "wie geht es dir");
    zx_ui_draw(&u, &c);
    return schreibe_ppm(&c, argv[2]) == 0 ? 0 : 1;
}


static int ausgabe_cb(const char *s, void *n)
{
    (void)n;
    fputs(s, stdout);
    fflush(stdout);
    return 0;
}

/* Fuer Entwicklung und CI: zynexd --llmtest modell.gguf "Frage" */
static int llmtest(int argc, char **argv)
{
    char err[160] = "";
    double tps = 0.0;
    int n;
    zx_llm *mo = zx_llm_open(argv[2], 2048, (int)sysconf(_SC_NPROCESSORS_ONLN), err, sizeof(err));

    if (!mo) {
        printf("FEHLER: %s\n", err);
        return 1;
    }
    printf("Frage: %s\nAntwort: ", argc > 3 ? argv[3] : "Hallo");
    n = zx_llm_antwort(mo, zx_model_system(), "", argc > 3 ? argv[3] : "Hallo", 96, ausgabe_cb, NULL, &tps);
    printf("\n[%d Tokens, %.2f Tokens/s]\n", n, tps);
    zx_llm_close(mo);
    return n > 0 ? 0 : 1;
}

int main(int argc, char **argv)
{
    zx_ui u;
    zx_fb fb;
    struct termios alt, neu;
    int tty = isatty(0), hat_termios = 0, esc = 0, war_sprechend = 0, lade_gemeldet = 0, adr_gemeldet = 0;
    unsigned adr_zaehler = 0, ui_zaehler = 0, inst_zaehler = 0;
    char offen[64] = "";

    if (argc > 2 && strcmp(argv[1], "--standbild") == 0)
        return standbild(argc, argv);
    if (argc > 2 && strcmp(argv[1], "--llmtest") == 0)
        return llmtest(argc, argv);

    setvbuf(stdout, NULL, _IOLBF, 0);
    zx_ui_init(&u, jetzt_ms());

    if (zx_fb_open(&fb, "/dev/fb0") == 0) {
        g_grafik = 1;
        if (tty) {
            (void)ioctl(0, KDSETMODE, KD_GRAPHICS);
            if (tcgetattr(0, &alt) == 0) {
                neu = alt;
                neu.c_lflag &= (tcflag_t)~(ICANON | ECHO);
                neu.c_cc[VMIN] = 0;
                neu.c_cc[VTIME] = 0;
                tcsetattr(0, TCSANOW, &neu);
                hat_termios = 1;
            }
        }
    } else {
        puts("==============================");
        puts("  Z Y N E X   (Prototyp 0.2, Textmodus)");
        puts("==============================");
    }

    {
        const char *mp = getenv("ZYNEX_GEDAECHTNIS");
        zx_mem_init(&g_mem, mp ? mp : "/var/lib/zynex/gedaechtnis.txt");
    }
    web_starten();
    kernel_log("zynexd: bereit");
    ki_starte_laden();
    if (ki.laedt) {
        zynex_sagt(&u, "Hallo, ich bin Zynex. Ich wache gerade auf, einen Moment bitte.", ZX_DENKT);
    } else {
        lade_gemeldet = 1;
        zynex_sagt(&u, "Hallo, ich bin Zynex. Sag mir, was du brauchst.", ZX_FREUNDLICH);
    }

    {   /* Start aus dem Boot-Menue "Zynex installieren": Vorschlag gleich machen */
        FILE *cf = fopen("/proc/cmdline", "r");
        char cl[1024] = "";
        if (cf) {
            if (!fgets(cl, sizeof(cl), cf))
                cl[0] = '\0';
            fclose(cf);
        }
        if (strstr(cl, "zynex.install=1")) {
            char t[500];
            install_vorschlag(0, t, sizeof(t));
            if (strstr(cl, "zynex.autoinstall=1") && g_ziel[0]) { /* nur fuer den CI-Test */
                kernel_log("zynexd: autoinstall");
                install_start(t, sizeof(t));
            }
            zynex_sagt(&u, "Willkommen beim Zynex-Setup.", ZX_FREUNDLICH);
            zynex_sagt(&u, t, ZX_FREUNDLICH);
        }
    }

    for (;;) {
        fd_set fds;
        struct timeval tv = {0, 33000};

        int r;

        FD_ZERO(&fds);
        FD_SET(0, &fds);
        r = select(1, &fds, NULL, NULL, &tv);

        if (r > 0 && FD_ISSET(0, &fds)) {
            unsigned char buf[64];
            ssize_t n = read(0, buf, sizeof(buf));
            ssize_t i;
            if (n <= 0) {
                if (!g_grafik)
                    sleep(1);
            } else {
                for (i = 0; i < n; i++) {
                    if (taste(&u, buf[i], &esc)) {
                        char zeile[ZX_UI_LINE];
                        snprintf(zeile, sizeof(zeile), "%s", u.eingabe);
                        u.eingabe[0] = '\0';
                        if (zeile[0] != '\0')
                            verarbeite(&u, zeile, offen, sizeof(offen));
                    }
                }
            }
        }

        if (!lade_gemeldet && !__atomic_load_n(&ki.laedt, __ATOMIC_ACQUIRE)) {
            lade_gemeldet = 1;
            if (ki.thread_offen) {
                pthread_join(ki.th, NULL);
                ki.thread_offen = 0;
            }
            if (ki.geladen) {
                kernel_log("zynexd: modell geladen");
                zynex_sagt(&u, "Ich bin wach und bereit. Frag mich etwas.", ZX_FREUNDLICH);
            } else {
                zynex_sagt(&u, "Mein Sprachmodell fehlt oder ist nicht ladbar. Ich antworte nur mit Stichworten.", ZX_DENKT);
            }
        }
        if (g_setup_pid && ++inst_zaehler % 30 == 0) {
            char ip[300];
            const char *t = install_fortschritt(ip, sizeof(ip));
            if (t)
                zynex_sagt(&u, t, ZX_FREUNDLICH);
        }
        /* Anzeige-Daten fuer Seitenleiste und Listen (etwa jede halbe Sekunde) */
        if (++ui_zaehler % 15 == 0)
            ui_auffrischen(&u);
        /* Sobald das Netzwerk eine Adresse hat: einmal ansagen, wo die Webseite ist */
        if (!adr_gemeldet && lade_gemeldet && !ki.rechnet && ++adr_zaehler % 60 == 0) {
            char url[80], t[160];
            if (zx_web_adresse(url, sizeof(url), g_web_port)) {
                adr_gemeldet = 1;
                snprintf(u.adresse, sizeof(u.adresse), "%s", url);
                snprintf(t, sizeof(t), "Meine Webseite: %s", url);
                kernel_log(t);
                zynex_sagt(&u, t, ZX_FREUNDLICH);
            }
        }
        if (__atomic_load_n(&ki.antwort_da, __ATOMIC_ACQUIRE))
            ki_abholen(&u, offen, sizeof(offen));

        if (g_grafik) {
            zx_face_update(&u.face, jetzt_ms());
            if (war_sprechend && !zx_face_is_speaking(&u.face))
                zx_face_set_mood(&u.face, ZX_NEUTRAL);
            if (!zx_face_is_speaking(&u.face) && u.eingabe[0] != '\0')
                zx_face_set_mood(&u.face, ZX_ZUHOEREN);
            war_sprechend = zx_face_is_speaking(&u.face);
            zx_ui_draw(&u, &fb.cv);
            zx_fb_flip(&fb);
        }
        (void)hat_termios;
    }
}
