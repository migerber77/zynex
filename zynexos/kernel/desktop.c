/*
 * Zynex OS Quantum - Startbild und Desktop (Framebuffer, Maus + Tastatur)
 * Kein Startmenue, keine Programmliste: Mitte = Zynie (der Roboter), unten die Eingabeleiste. Was eingegeben wird, erkennt Zynie als Absicht
 * (Intent) und oeffnet das Passende: "text erstellen" -> Textverarbeitung, "rechne 12*7" -> Ergebnis, "system" -> Zustand usw.
 * Sprachen: Deutsch und Englisch (die gespeicherte Sprache de/en, alle anderen zeigen Englisch).
 */
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

static int W, H, lang;                                    /* lang: 0 Deutsch, 1 Englisch */
static const char* TR(const char* de, const char* en) { return lang == 0 ? de : en; }

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
    for (int i = 0; i < 2000000 && timer_get_ticks() - t0 < 90; i++) __asm__ volatile ("pause");
    splash_an = 0;
}

/* ---------- Zustand ---------- */
enum { Z_LOGIN, Z_DESK, Z_EDIT, Z_WEB };
static int zustand;
static char fname[16], fpw[40]; static int ffocus, fmsg;
static char eingabe[96]; static int einlen;
static char antwort[6][100]; static int nantwort;
#define QN 32
static uint16_t kq[QN]; static unsigned qh, qt; static int was_down;
static struct { int x, y, w, h, id; } knoepfe[80]; static int nkn;
static uint32_t robot_phase;
static uint32_t sprech_bis, letzte_eingabe;               /* Ticks (100 pro Sekunde) */

/* Textverarbeitung */
#define DOK_MAX 4000
static char dok[DOK_MAX + 1]; static int doklen, dokcur, dokscroll, dokgeaendert; static char dokname[24];
static int ed_x, ed_y, ed_w, ed_h, ed_cols, ed_rows, ed_s;

static void kn_reg(int id, int x, int y, int w, int h) { if (nkn < 80) { knoepfe[nkn].x = x; knoepfe[nkn].y = y; knoepfe[nkn].w = w; knoepfe[nkn].h = h; knoepfe[nkn].id = id; nkn++; } }
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
static void zeichne_login(void) {
    gfx_begin();
    gfx_rect(0, 0, W, H, C_BG);
    int cw = 360, cx = (W - cw) / 2, y = H / 2 - 250;
    gfx_bitmap((W - zynex_logo_w) / 2, y, zynex_logo_w, zynex_logo_h, zynex_logo);
    y += zynex_logo_h + 24;
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

static void zeichne_robot(void) {
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
    { "text erstellen", "write a text", 0 }, { "browser", "browser", 0 }, { "browser de.wikipedia.org", "browser en.wikipedia.org", 0 }, { "suche ", "search ", 0 }, { "rechne 12*7", "calc 12*7", 0 }, { "system", "system status", 0 }, { "dateien", "files", 0 },
    { "quanten", "quantum demo", 0 }, { "sicherheit", "security", 0 }, { "agenten", "agents", 0 }, { "briefing", "briefing", 0 },
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
static char sug[5][96]; static int sugtyp[5], sugpre[5], nsug, sugsel = -1;
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
static void aufg_laden(void) { int r = synfs_read("aufgaben.txt", aufg, sizeof(aufg) - 1); auflen = r > 0 ? r : 0; aufg[auflen] = 0; }
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
static void aufg_speichern(void) { synfs_create("aufgaben.txt", SYNFS_FILE); synfs_write("aufgaben.txt", aufg, (uint32_t)(auflen ? auflen : 1)); synfs_sync(); }
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
static const char* const ag_name[NAG][2] = { { "Morgen-Briefing", "Morning briefing" }, { "Sicherheits-Agent", "Security agent" }, { "Wartungs-Agent", "Maintenance agent" }, { "Aufgaben-Agent", "Task agent" } };
static const char* const ag_desc[NAG][2] = {
    { "Prueft Zeit, Speicher, Netzwerk, Dokumente und Aufgaben und fasst alles zusammen.", "Checks time, memory, network, documents and tasks and sums it up." },
    { "Prueft Startkette, Signatur, Benutzerrolle und Netzwerk.", "Checks boot chain, signature, user role and network." },
    { "Prueft den Speicher und schlaegt vor, das Dateisystem zu sichern (Freigabe noetig).", "Checks memory and proposes to sync the file system (needs approval)." },
    { "Liest deine Aufgaben und schlaegt vor, erledigte zu entfernen (Freigabe noetig).", "Reads your tasks and proposes to clear them (needs approval)." },
};
static char alog[10][100]; static int nalog, alauf = -1, pend;           /* pend: 0 keine, 1 sichern, 2 Aufgaben leeren */
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
static int flaeche;                                                  /* 0 Zynie, 1 System, 2 Agenten, 3 Aufgaben */
static const char* const fl_name[4][2] = { { "Zynie", "Zynie" }, { "System", "System" }, { "Agenten", "Agents" }, { "Aufgaben", "Tasks" } };
static void kn_loesche(int lo, int hi) { int k = 0; for (int i = 0; i < nkn; i++) if (knoepfe[i].id < lo || knoepfe[i].id > hi) knoepfe[k++] = knoepfe[i]; nkn = k; }
static void karte(int x, int y, int w, int h, const char* titel) {
    rrect_rand(x, y, w, h, 14, C_CARD, C_BORDER);
    gfx_rect(x + 16, y + 1, w - 32, 2, C_ACC);
    gtxt(x + 18, y + 14, titel, C_MUTED, 1);
}

static void zeichne_eingabe(void) {
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
    nkn = 0; lbw = 0; zeichne_leiste();
    if (flaeche == 0) { zeichne_robot(); zeichne_antwort(); zeichne_eingabe(); }
    else { gfx_begin(); if (flaeche == 1) zeichne_f_system(); else if (flaeche == 2) zeichne_f_agenten(); else zeichne_f_aufgaben(); gfx_end(); }
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
    wuerfel_dreh(nach);
    kprint("[DESKTOP] Arbeitsflaeche "); kprint_dec((uint32_t)nach + 1); kprint(": "); kprint(fl_name[nach][1]); kprint("\n");
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
    gfx_begin();
    if (voll) {
        gfx_rect(0, 0, W, H, C_BG);
        ed_s = W >= 1500 ? 2 : 1;
        ed_w = W - 120 < 980 ? W - 120 : 980; ed_h = H - 150; ed_x = (W - ed_w) / 2; ed_y = 60;
        ed_cols = (ed_w - 60) / (8 * ed_s); ed_rows = (ed_h - 150) / (20 * ed_s);
        rrect_rand(ed_x, ed_y, ed_w, ed_h, 16, C_CARD, C_BORDER);
        char t[60]; scopy(t, TR("Dokument  -  ", "Document  -  "), 60); cat(t, dokname, 60);
        gtxt(ed_x + 24, ed_y + 18, t, C_TEXT, 1);
        nkn = 0;
        knopf(K_SPEICHERN, TR("Speichern", "Save"), ed_x + ed_w - 3 * 130 - 16, ed_y + 10, 120, 1);
        knopf(K_NEU, TR("Neu", "New"), ed_x + ed_w - 2 * 130 - 16, ed_y + 10, 120, 0);
        knopf(K_SCHLIESSEN, TR("Schliessen", "Close"), ed_x + ed_w - 130 - 16, ed_y + 10, 120, 0);
        gfx_rect(ed_x + 20, ed_y + 56, ed_w - 40, 1, C_BORDER);
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
static void editor_oeffnen(void) { dok_neu(); zustand = Z_EDIT; zeichne_editor(1); }
static void editor_schliessen(void) {
    if (doklen > 0 && dokgeaendert) dok_speichern();
    zustand = Z_DESK; zeichne_desktop();
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
static void lz_laden(void) { int r = synfs_read("lesezeichen.txt", lesez, sizeof(lesez) - 1); lzlen = r > 0 ? r : 0; lesez[lzlen] = 0; }
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
    synfs_create("lesezeichen.txt", SYNFS_FILE); synfs_write("lesezeichen.txt", lesez, (uint32_t)lzlen); synfs_sync();
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
static void zeichne_web(int voll) {
    gfx_begin();
    if (voll) {
        gfx_rect(0, 0, W, H, C_BG); nkn = 0; zeichne_leiste();
        int fx = 24 + 3 * 42, fw = W - fx - 24 - 3 * 100;
        knopf(K_WB, "<", 24, 48, 36, 0); knopf(K_WV, ">", 66, 48, 36, 0); knopf(K_WR, "R", 108, 48, 36, 0);
        rrect_rand(fx, 48, fw, 34, 12, C_FIELD, C_ACC);
        { char a[200]; scopy(a, adr, 200); int mc = (fw - 24) / 8; if (slen(a) > mc) { for (int i = 0; i < mc - 1; i++) a[i] = adr[slen(adr) - (mc - 1) + i]; a[mc - 1] = 0; }
          if (sel1 && adrlen) rrect(fx + 8, 53, gtw(a, 1) + 8, 24, 6, mix(C_FIELD, C_ACC, 140));
          if (!adrlen) gtxt(fx + 14, 57, TR("Adresse oder Suchbegriff ...", "Address or search ..."), 0x00647488u, 1); else gfx_text(fx + 14, 57, a, C_TEXT, 1);
          gfx_rect(fx + 14 + slen(a) * 8, 54, 2, 22, C_TEXT); }
        knopf(K_WS, TR("Merken", "Save"), fx + fw + 10, 48, 90, 0); knopf(K_WX, TR("Schliessen", "Close"), fx + fw + 110, 48, 110, 0);
        wx0 = 24; wy0 = 92; ww = W - 48; wh = H - 92 - 44;
        wcols = (ww - 60) / 8; if (wcols > 110) wcols = 110; wrows = (wh - 24) / 20;
    }
    /* Seite */
    rrect_rand(wx0, wy0, ww, wh, 14, C_CARD, C_BORDER);
    if (wlauf) gtxt(wx0 + 24, wy0 + 24, TR("Seite wird geladen ...", "Loading page ..."), C_MUTED, 1);
    else if (!wok) {
        int y = wy0 + 20;
        if (meldung[0] && !wlauf) { gtxt(wx0 + 24, y, meldung, 0x00F59E0Bu, 1); y += 34; }
        gtxt(wx0 + 24, y, TR("Zynex Browser - Lesezeichen und Vorschlaege", "Zynex browser - bookmarks and suggestions"), C_TEXT, 1); y += 30;
        lz_laden(); int n = 0; char t[200];
        for (; lz_zeile(n, t, 200) && n < 10; n++) { gtxt(wx0 + 40, y + n * 28, t, C_ACC2, 1); kn_reg(K_LZ0 + n, wx0 + 30, y + n * 28 - 4, ww - 60, 26); }
        y += n * 28 + 8;
        for (int i = 0; web_vorgabe[i] && n < 14; i++, n++) { gtxt(wx0 + 40, y, web_vorgabe[i], C_MUTED, 1); kn_reg(K_LZ0 + 10 + i, wx0 + 30, y - 4, ww - 60, 26); y += 28; }
        int ns = net_status(t, 32); if (ns != 2) gtxt(wx0 + 24, wy0 + wh - 36, TR("Kein Netzwerk: zuerst verbinden (netz dhcp).", "No network: connect first (net dhcp)."), C_MUTED, 1);
        else gtxt(wx0 + 24, wy0 + wh - 36, TR("Tippe eine Adresse oder einen Suchbegriff. Rechtsklick: Menue.", "Type an address or a search term. Right click: menu."), C_MUTED, 1);
    } else {
        if (wscroll > web_nz - wrows) wscroll = web_nz - wrows;
        if (wscroll < 0) wscroll = 0;
        for (int r = 0; r < wrows && wscroll + r < web_nz; r++) {
            struct web_zeile* z = &web_z[wscroll + r]; int y = wy0 + 14 + r * 20, x = wx0 + 24;
            uint32_t c = z->stil == 1 ? C_TEXT : z->stil == 2 ? C_ACC2 : z->stil == 3 ? C_MUTED : C_TEXT;
            if (z->stil == 3) rrect(wx0 + 14, y - 2, ww - 40, 20, 4, C_FIELD);
            gfx_text(x, y, z->t, c, 1); if (z->stil == 1) gfx_text(x + 1, y, z->t, c, 1);
            for (int k = 0; k < z->nl; k++) {
                char seg[120]; int a = z->l[k].a, b = z->l[k].b, n = 0; for (int q = a; q < b && q < 115; q++) seg[n++] = z->t[q]; seg[n] = 0;
                rrect(x + a * 8 - 1, y, (b - a) * 8 + 2, 18, 0, C_CARD); gfx_text(x + a * 8, y, seg, C_ACC2, 1); gfx_rect(x + a * 8, y + 16, (b - a) * 8, 1, C_ACC2);
            }
        }
        int sbx = wx0 + ww - 18, sbh = wh - 24; rrect(sbx, wy0 + 12, 8, sbh, 4, C_FIELD);
        if (web_nz > wrows) { int kh = sbh * wrows / web_nz; if (kh < 24) kh = 24; int ky = wy0 + 12 + (sbh - kh) * wscroll / (web_nz - wrows); rrect(sbx, ky, 8, kh, 4, C_ACC); }
    }
    /* Statuszeile */
    gfx_rect(0, H - 40, W, 40, C_BG);
    { char s[140]; if (meldung[0] && wok) scopy(s, meldung, 140); else if (wok) { scopy(s, web_titel[0] ? web_titel : web_url, 140); } else s[0] = 0; gtxt(24, H - 30, s, C_MUTED, 1);
      char z[40], a[12]; scopy(z, "", 40); if (wok && web_nz > wrows) { dec(a, (uint32_t)wscroll + 1); cat(z, a, 40); cat(z, "-", 40); int e = wscroll + wrows; if (e > web_nz) e = web_nz; dec(a, (uint32_t)e); cat(z, a, 40); cat(z, " / ", 40); dec(a, (uint32_t)web_nz); cat(z, a, 40); }
      gtxt(W - 24 - gtw(z, 1), H - 30, z, C_MUTED, 1); }
    if (nsug > 0) {
        int fx = 24 + 3 * 42, fw = W - fx - 24 - 3 * 100, y0 = 86;
        rrect_rand(fx, y0, fw, nsug * 30 + 12, 14, C_CARD, C_ACC); kn_loesche(K_SUG0, K_SUG0 + 5);
        for (int i = 0; i < nsug; i++) {
            int ry = y0 + 6 + i * 30; if (i == sugsel) rrect(fx + 6, ry, fw - 12, 28, 10, mix(C_CARD, C_ACC, 120));
            char a[96]; int pl = sugpre[i] < 95 ? sugpre[i] : 95; for (int j = 0; j < pl; j++) a[j] = sug[i][j]; a[pl] = 0; int px = fx + 20;
            if (pl) { gtxt(px, ry + 6, a, C_TEXT, 1); px += gtw(a, 1); } gtxt(px, ry + 6, sug[i] + pl, i == sugsel ? C_TEXT : C_MUTED, 1);
            const char* tg = sugtyp[i] == 0 ? TR("Lesezeichen", "bookmark") : TR("Verlauf", "history"); gtxt(fx + fw - 20 - gtw(tg, 1), ry + 6, tg, C_MUTED, 1);
            kn_reg(K_SUG0 + i, fx + 6, ry, fw - 12, 28);
        }
    }
    gfx_end();
}
static void web_oeffnen(const char* url) {
    zustand = Z_WEB; whp = whp < 0 ? -1 : whp; nsug = 0; sugsel = -1; sel1 = 0; wlauf = 0; meldung[0] = 0;
    if (!adrlen) { adr[0] = 0; }
    kprint("[DESKTOP] Browser geoeffnet\n");
    zeichne_web(1);
    if (url && *url) web_gehe(url, 1);
}
static void web_schliessen(void) { zustand = Z_DESK; nsug = 0; sugsel = -1; sel1 = 0; zeichne_desktop(); }
static int web_link_bei(int x, int y) {                              /* Link unter der Maus oder -1 */
    if (!wok || x < wx0 + 24 || y < wy0 + 14) return -1;
    int r = (y - wy0 - 14) / 20; if (r < 0 || r >= wrows || wscroll + r >= web_nz) return -1;
    struct web_zeile* z = &web_z[wscroll + r]; int sp = (x - wx0 - 24) / 8;
    for (int k = 0; k < z->nl; k++) if (sp >= z->l[k].a && sp < z->l[k].b) return z->l[k].id;
    return -1;
}

/* ---------- Rechtsklick-Menue ---------- */
enum { M_AUSSCHN = 1, M_KOPIEREN, M_EINFUEGEN, M_ALLES, M_DRUCKEN, M_ZURUECK, M_VOR, M_NEULADEN, M_LINKOEFFNEN, M_LINKKOPIEREN, M_LESEZEICHEN, M_SEITEKOPIEREN, M_SEITESPEICHERN, M_SPEICHERN, M_SCHLIESSEN, M_NAECHSTE, M_VORIGE, M_LEEREN };
static struct { int offen, x, y, w, n, hover, ziel; struct { int id; const char* de; const char* en; } p[9]; } menu;
static int menu_link;
static void menu_add(int id, const char* de, const char* en) { if (menu.n < 9) { menu.p[menu.n].id = id; menu.p[menu.n].de = de; menu.p[menu.n].en = en; menu.n++; } }
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
static void redraw_alles(void) { if (zustand == Z_WEB) zeichne_web(1); else if (zustand == Z_EDIT) zeichne_editor(1); else if (zustand == Z_DESK) zeichne_desktop(); }
static void menu_schliessen(void) { if (!menu.offen) return; menu.offen = 0; redraw_alles(); }
static void menu_oeffnen(int x, int y) {
    menu.n = 0; menu.hover = -1; menu_link = -1;
    int eingabefeld = (zustand == Z_DESK && flaeche == 0 && x >= bar_x && x < bar_x + bar_w && y >= bar_y && y < bar_y + 52);
    if (zustand == Z_WEB) {
        int fx = 24 + 3 * 42, fw = W - fx - 24 - 3 * 100;
        if (x >= fx && x < fx + fw && y >= 48 && y < 82) { menu_add(M_AUSSCHN, "Ausschneiden", "Cut"); menu_add(M_KOPIEREN, "Kopieren", "Copy"); menu_add(M_EINFUEGEN, "Einfuegen", "Paste"); menu_add(M_ALLES, "Alles auswaehlen", "Select all"); }
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
    } else if (eingabefeld) {
        menu_add(M_AUSSCHN, "Ausschneiden", "Cut"); menu_add(M_KOPIEREN, "Kopieren", "Copy"); menu_add(M_EINFUEGEN, "Einfuegen", "Paste"); menu_add(M_ALLES, "Alles auswaehlen", "Select all"); menu_add(M_LEEREN, "Leeren", "Clear");
    } else {
        menu_add(M_NAECHSTE, "Naechste Arbeitsflaeche", "Next workspace"); menu_add(M_VORIGE, "Vorherige Arbeitsflaeche", "Previous workspace");
        if (flaeche == 0) menu_add(M_EINFUEGEN, "Einfuegen", "Paste");
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
enum { I_NICHTS, I_TEXT, I_RECHNEN, I_SYSTEM, I_DATEIEN, I_QUBIT, I_SICHER, I_ABMELDEN, I_NEUSTART, I_WARTUNG, I_HILFE, I_AGENT, I_AUFGABE, I_FLAECHE, I_WEB };
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
    static const char* const w_sec[] = { "sicher", "secur", "signatur", "signature", 0 };
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
static void ki_antwort(const char* frage) {
    ki_len = 0;
    reply1(TR("Moment, ich denke nach ...", "One moment, thinking ...")); zeichne_antwort(); zeichne_robot();
    static char roh[600];
    int n = gehirn_frage(frage, roh, sizeof(roh), ki_teil);
    if (n < 0) { reply1(TR("Mein Sprachmodell ist gerade nicht verfuegbar.", "My language model is not available right now.")); kprint("[DESKTOP] Zynie: Sprachmodell nicht verfuegbar: "); kprint(gehirn_status()); kprint("\n"); return; }
    ki_text[ki_len] = 0; ki_zeilen();
    sprech_bis = timer_get_ticks() + 100; kprint("[DESKTOP] Zynie antwortet: "); kprint(ki_text); kprint("\n");
}

static void ausfuehren(void) {
    char s[96]; int k = 0; for (int i = 0; eingabe[i] && k < 94; i++) { char c = eingabe[i]; s[k++] = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; } s[k] = 0;
    char roh[96]; scopy(roh, eingabe, 96);
    int in = erkenne(s);
    if (gehirn_vorhanden() && in != I_WARTUNG && in != I_RECHNEN && in != I_AGENT && in != I_AUFGABE && in != I_FLAECHE && in != I_WEB) { int w = 1; for (int i = 0; s[i]; i++) if (s[i] == ' ' && s[i + 1] && s[i + 1] != ' ') w++; if (w > 3) in = I_NICHTS; }   /* ganze Saetze gehen an das Sprachmodell */
    einlen = 0; eingabe[0] = 0; nsug = 0; sugsel = -1; sel1 = 0;
    if (!k) return;
    hist_add(roh);
    kprint("[DESKTOP] Intent '"); kprint(roh); kprint("' -> ");
    switch (in) {
    case I_TEXT: kprint("Textverarbeitung\n"); editor_oeffnen(); return;
    case I_RECHNEN: kprint("Rechnen\n"); intent_rechnen(roh); break;
    case I_SYSTEM: kprint("System\n"); intent_system(); break;
    case I_DATEIEN: kprint("Dateien\n"); intent_dateien(); break;
    case I_QUBIT: kprint("Quanten\n"); intent_qubit(); break;
    case I_SICHER: kprint("Sicherheit\n"); antwort_setze(TR("Sicherheit der Startkette:", "Boot chain security:"),
            (g_boot.flags & BOOT_SIGNIERT) ? TR("Kernel und Startzeile sind durch eine hash-basierte LMS-Signatur (quantensicher) geprueft.", "Kernel and boot line are verified by a hash-based LMS signature (quantum-safe).")
                                         : TR("ACHTUNG: Der Kernel wurde nicht durch eine Signatur geprueft.", "WARNING: the kernel was not verified by a signature."), 0, 0, 0, 0); break;
    case I_HILFE: kprint("Hilfe\n"); antwort_setze(TR("Sag mir einfach, was du tun moechtest. Zum Beispiel:", "Just tell me what you want to do. For example:"),
            TR("\"text erstellen\" - schreibt ein Dokument", "\"write a text\" - opens a document"), TR("\"rechne (12+3)*7\" - rechnet", "\"calc (12+3)*7\" - calculates"),
            TR("\"system\" - zeigt den Zustand, \"dateien\" - deine Dokumente", "\"system\" - shows the status, \"files\" - your documents"), TR("\"briefing\", \"agenten\", \"aufgabe ...\", \"quanten\", \"abmelden\" - oder frag mich einfach alles", "\"briefing\", \"agents\", \"task ...\", \"quantum\", \"sign out\" - or just ask me anything"), 0); break;
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
    case I_ABMELDEN: kprint("Abmelden\n"); benutzer_abmelden(); fname[0] = fpw[0] = 0; ffocus = 0; fmsg = 0; zustand = Z_LOGIN; zeichne_login(); return;
    case I_NEUSTART: kprint("Neustart\n"); system_neustart(); return;
    case I_WARTUNG:
        if (benutzer_rolle() != ROLLE_ADMIN) { kprint("abgelehnt\n"); reply1(TR("Wartung ist nur fuer Administratoren.", "Maintenance is for administrators only.")); break; }
        kprint("Wartungs-Konsole\n"); usb_set_rawgrab(0); shell_set_tick(0); hv_status_aus(); gfx_console(1); shell_wartung(); return;
    default:
        if (gehirn_vorhanden()) { kprint("Sprachmodell\n"); ki_antwort(roh); break; }
        kprint("nicht verstanden\n"); antwort_setze(TR("Das habe ich noch nicht verstanden.", "I did not understand that yet."), TR("Sag zum Beispiel: text erstellen, rechne 12*7, system oder hilfe.", "Try: write a text, calc 12*7, system or help."), 0, 0, 0, 0); break;
    }
    zeichne_antwort(); zeichne_eingabe();
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

static void taste(uint8_t k, int shift, int altgr) {
    if (menu.offen) { if (k == 0x29) menu_schliessen(); return; }
    if (zustand == Z_EDIT) { if (cur_ctrl && strg_taste(k)) return; editor_taste(k, shift, altgr); return; }
    if (zustand == Z_WEB) { web_taste(k, shift, altgr); return; }
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

static void klick(int x, int y) {
    for (int i = 0; i < nkn; i++) {
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
        if (rd && !was_r && zustand != Z_LOGIN) { if (menu.offen) menu_schliessen(); else menu_oeffnen(p->x, p->y); }
        was_r = rd;
        if (menu.offen) {
            int i = menu_treffer(p->x, p->y); if (i != menu.hover) { menu.hover = i; zeichne_menu(); }
            if (down && !was_down) { was_down = down; if (i >= 0) menu_aktion(menu.p[i].id); else menu_schliessen(); }
            was_down = down; return;
        }
        if (down && !was_down) {
            if (zustand == Z_DESK && flaeche == 0 && p->x >= zr_x() && p->x < zr_x() + zr_w() && p->y >= zr_y() && p->y < zr_y() + zr_h()) {
                if (p->x >= zr_x() + zr_w() - 24 && p->y >= zr_y() + zr_h() - 24) zdrag = 2;
                else { zdrag = 1; zdx = p->x - zx; zdy = p->y - zy; }
            } else klick(p->x, p->y);
        }
        if (down && zdrag && zustand == Z_DESK && flaeche == 0) {
            int bxl = zr_x(), byl = zr_y();
            if (zdrag == 1) { zx = p->x - zdx; zy = p->y - zdy; if (zy - SC(108) < 44) zy = 44 + SC(108); if (zx - SC(90) < 0) zx = SC(90); if (zx + SC(90) > W) zx = W - SC(90); if (zy + SC(124) > H) zy = H - SC(124); }
            else { int ns = (p->x - bxl) * 100 / 180; if (ns < 50) ns = 50; if (ns > 220) ns = 220; if (byl + ns * 232 / 100 <= H) { zs = ns; zx = bxl + SC(90); zy = byl + SC(108); } }
            zeichne_robot(); zeichne_antwort(); zeichne_eingabe(); zeichne_robot();
        }
        if (!down && zdrag) { zdrag = 0; kprint("[DESKTOP] Zynie: Position "); kprint_dec((uint32_t)zx); kprint(","); kprint_dec((uint32_t)zy); kprint(" Groesse "); kprint_dec((uint32_t)zs); kprint("%\n"); }
        was_down = down;
    }
    static int war_ruhe;
    int r = ruhend(); if (r != war_ruhe) { war_ruhe = r; kprint(r ? "[DESKTOP] Zynie: Ruhemodus\n" : "[DESKTOP] Zynie: wach\n"); }
    uint32_t ph = t / (t < sprech_bis ? 6 : r ? 30 : 12);
    if (ph != robot_phase) { robot_phase = ph; if (zustand == Z_DESK && !zdrag && flaeche == 0) zeichne_robot(); }
    { static uint32_t lsek; if (zustand == Z_DESK && flaeche == 1 && t / 100 != lsek) { lsek = t / 100; gfx_begin(); sys_k_zeit(); gfx_end(); } }
}

int desktop_start(void) {
    if (!gfx_available()) return -1;
    gfx_size(&W, &H);
    lang = 0;
    static char cfg[64]; int r = synfs_read("sys.config", cfg, sizeof(cfg) - 1);
    if (r > 0) { cfg[r] = 0; for (int i = 0; cfg[i]; i++) if (cfg[i] == 'l' && cfg[i + 1] == 'a' && cfg[i + 2] == 'n' && cfg[i + 3] == 'g' && cfg[i + 4] == '=') { lang = (cfg[i + 5] == 'd' ? 0 : 1); break; } }
    bar_w = W - 80 < 760 ? W - 80 : 760; bar_x = (W - bar_w) / 2; bar_y = H - 230;
    fname[0] = fpw[0] = 0; ffocus = 0; fmsg = 0; einlen = 0; eingabe[0] = 0; nsug = 0; sugsel = -1; flaeche = 0; alauf = -1; nalog = 0; pend = 0; nantwort = 0; qh = qt = 0; was_down = 0;
    zx = 24 + 90; zy = 56 + 108; zs = 100; lbw = 0; zdrag = 0; letzte_eingabe = timer_get_ticks();
    gfx_console(0);
    usb_set_rawgrab(rohtaste);
    shell_set_tick(tick);
    kprint("[DESKTOP] Zynex OS Quantum Desktop, Bildschirm "); kprint_dec((uint32_t)W); kprint("x"); kprint_dec((uint32_t)H); kprint("\n");
    if (benutzer_gesperrt()) { zustand = Z_LOGIN; kprint("[DESKTOP] Anmeldebildschirm\n"); zeichne_login(); }
    else { zustand = Z_DESK; reply1(TR("Hallo! Ich bin Zynie. Was moechtest du tun?", "Hello! I am Zynie. What would you like to do?")); zeichne_desktop(); }
    return 0;
}
