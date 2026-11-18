/*
 * Zynex OS Quantum - Startbild und Desktop (Framebuffer, Maus + Tastatur)
 * Kein Startmenue, keine Programmliste: Mitte = Zynie (der Roboter), unten die Eingabeleiste. Was eingegeben wird, erkennt Zynie als Absicht
 * (Intent) und oeffnet das Passende: "text erstellen" -> Textverarbeitung, "rechne 12*7" -> Ergebnis, "system" -> Zustand usw.
 * Sprachen: Deutsch und Englisch (die gespeicherte Sprache de/en, alle anderen zeigen Englisch).
 */
#include "desktop.h"
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

#define C_BG     0x000B1220u
#define C_CARD   0x00162038u
#define C_BORDER 0x002B3A57u
#define C_TEXT   0x00E6EDF7u
#define C_MUTED  0x008DA0BDu
#define C_ACC    0x003B82F6u
#define C_CYAN   0x0000F2FEu
#define C_VIOLET 0x009D4EDDu
#define C_OK     0x0034D399u
#define C_ERR    0x00EF4444u
#define C_FIELD  0x000F172Au
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
    gfx_end();
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
enum { Z_LOGIN, Z_DESK, Z_EDIT };
static int zustand;
static char fname[16], fpw[40]; static int ffocus, fmsg;
static char eingabe[96]; static int einlen;
static char antwort[6][100]; static int nantwort;
#define QN 32
static uint16_t kq[QN]; static unsigned qh, qt; static int was_down;
static struct { int x, y, w, h, id; } knoepfe[12]; static int nkn;
static uint32_t robot_phase;
static uint32_t sprech_bis, letzte_eingabe;               /* Ticks (100 pro Sekunde) */

/* Textverarbeitung */
#define DOK_MAX 4000
static char dok[DOK_MAX + 1]; static int doklen, dokcur, dokscroll, dokgeaendert; static char dokname[24];
static int ed_x, ed_y, ed_w, ed_h, ed_cols, ed_rows, ed_s;

static void kn_reg(int id, int x, int y, int w, int h) { if (nkn < 12) { knoepfe[nkn].x = x; knoepfe[nkn].y = y; knoepfe[nkn].w = w; knoepfe[nkn].h = h; knoepfe[nkn].id = id; nkn++; } }
static void knopf(int id, const char* label, int x, int y, int w, int primaer) {
    rrect_rand(x, y, w, 34, 10, primaer ? C_ACC : C_CARD, primaer ? C_ACC : C_BORDER);
    gtxt(x + (w - gtw(label, 1)) / 2, y + 9, label, C_TEXT, 1);
    kn_reg(id, x, y, w, 34);
}
enum { K_CHIP0 = 1, K_SPEICHERN = 20, K_NEU, K_SCHLIESSEN };

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
}

/* ---------- Desktop ---------- */
static const char* const chip_text[5] = { "text erstellen", "rechne 12*7", "system", "dateien", "quanten" };
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
static void zeichne_eingabe(void) {
    gfx_begin();
    gfx_rect(bar_x - 10, bar_y - 6, bar_w + 20, 130, C_BG);
    rrect_rand(bar_x, bar_y, bar_w, 52, 18, C_FIELD, C_ACC);
    rrect(bar_x + 14, bar_y + 20, 12, 12, 6, C_OK);
    if (einlen == 0) gtxt(bar_x + 40, bar_y + 18, TR("Sag Zynie, was du tun moechtest ...", "Tell Zynie what you want to do ..."), 0x00647488u, 1);
    else gfx_text(bar_x + 40, bar_y + 18, eingabe, C_TEXT, 1);
    gfx_rect(bar_x + 40 + einlen * 8, bar_y + 14, 2, 24, C_TEXT);
    nkn = 0;
    int x = bar_x;
    for (int i = 0; i < 5; i++) {
        int w = gtw(chip_text[i], 1) + 24;
        rrect_rand(x, bar_y + 66, w, 30, 15, C_CARD, C_BORDER);
        gtxt(x + 12, bar_y + 73, chip_text[i], C_MUTED, 1);
        kn_reg(K_CHIP0 + i, x, bar_y + 66, w, 30);
        x += w + 8;
    }
    gfx_end();
}
static void zeichne_leiste(void) {
    gfx_begin();
    gfx_rect(0, 0, W, 40, C_BG);
    gtxt(16, 12, "Zynex OS Quantum", C_CYAN, 1);
    char r[48]; scopy(r, benutzer_name(), 20); cat(r, " (", 48); cat(r, benutzer_rollentext(benutzer_rolle()), 48); cat(r, ")", 48);
    uint32_t ymd, hm; char zt[12] = "";
    if (rtc_get(&ymd, &hm) == 0 || hm) { char a[8]; dec(a, hm / 100); if (hm / 100 < 10) { zt[0] = '0'; zt[1] = 0; } cat(zt, a, 12); cat(zt, ":", 12); dec(a, hm % 100); if (hm % 100 < 10) cat(zt, "0", 12); cat(zt, a, 12); }
    int x = W - 16;
    for (int i = 3; i >= 0; i--) { rrect(x - 10, 15, 10, 10, 5, C_CYAN); x -= 16; }
    x -= 8; x -= gtw(r, 1); gtxt(x, 12, r, C_MUTED, 1);
    if (zt[0]) { x -= 16 + gtw(zt, 1); gtxt(x, 12, zt, C_TEXT, 1); }
    gfx_end();
}
static void zeichne_desktop(void) {
    gfx_begin(); gfx_rect(0, 0, W, H, C_BG); gfx_end();
    lbw = 0; zeichne_leiste(); zeichne_robot(); zeichne_antwort(); zeichne_eingabe();
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

/* ---------- Intents ---------- */
enum { I_NICHTS, I_TEXT, I_RECHNEN, I_SYSTEM, I_DATEIEN, I_QUBIT, I_SICHER, I_ABMELDEN, I_NEUSTART, I_WARTUNG, I_HILFE };
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
    if (gehirn_vorhanden() && in != I_WARTUNG && in != I_RECHNEN) { int w = 1; for (int i = 0; s[i]; i++) if (s[i] == ' ' && s[i + 1] && s[i + 1] != ' ') w++; if (w > 3) in = I_NICHTS; }   /* ganze Saetze gehen an das Sprachmodell */
    einlen = 0; eingabe[0] = 0;
    if (!k) return;
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
            TR("\"system\" - zeigt den Zustand, \"dateien\" - deine Dokumente", "\"system\" - shows the status, \"files\" - your documents"), TR("\"quanten\" - Quanten-Demo, \"abmelden\" - sperrt den Bildschirm", "\"quantum\" - quantum demo, \"sign out\" - locks the screen"), 0); break;
    case I_ABMELDEN: kprint("Abmelden\n"); benutzer_abmelden(); fname[0] = fpw[0] = 0; ffocus = 0; fmsg = 0; zustand = Z_LOGIN; zeichne_login(); return;
    case I_NEUSTART: kprint("Neustart\n"); system_neustart(); return;
    case I_WARTUNG:
        if (benutzer_rolle() != ROLLE_ADMIN) { kprint("abgelehnt\n"); reply1(TR("Wartung ist nur fuer Administratoren.", "Maintenance is for administrators only.")); break; }
        kprint("Wartungs-Konsole\n"); usb_set_rawgrab(0); shell_set_tick(0); gfx_console(1); shell_wartung(); return;
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
    if (zustand == Z_EDIT) { editor_taste(k, shift, altgr); return; }
    if (zustand == Z_LOGIN) {
        char* t = ffocus == 0 ? fname : fpw; int max = ffocus == 0 ? 15 : 39; int n = slen(t);
        if (k == 0x2B || k == 0x51 || k == 0x52) { ffocus = !ffocus; zeichne_login(); }
        else if (k == 0x28 || k == 0x58) { if (ffocus == 0) { ffocus = 1; zeichne_login(); } else anmelden(); }
        else if (k == 0x2A) { if (n) { t[n - 1] = 0; fmsg = 0; zeichne_login(); } }
        else { char c = usb_key_char(k, shift, altgr); if (c >= 32 && c < 127 && n < max) { t[n] = c; t[n + 1] = 0; fmsg = 0; zeichne_login(); } }
        return;
    }
    if (k == 0x28 || k == 0x58) { ausfuehren(); return; }
    if (k == 0x2A) { if (einlen) { eingabe[--einlen] = 0; zeichne_eingabe(); } return; }
    if (k == 0x29) { einlen = 0; eingabe[0] = 0; zeichne_eingabe(); return; }
    char c = usb_key_char(k, shift, altgr);
    if (((c >= 32 && c < 127) || (unsigned char)c >= 0xA0) && einlen < 80) { eingabe[einlen++] = c; eingabe[einlen] = 0; zeichne_eingabe(); }
}

static void klick(int x, int y) {
    for (int i = 0; i < nkn; i++) {
        if (x < knoepfe[i].x || x >= knoepfe[i].x + knoepfe[i].w || y < knoepfe[i].y || y >= knoepfe[i].y + knoepfe[i].h) continue;
        int id = knoepfe[i].id;
        if (zustand == Z_DESK && id >= K_CHIP0 && id < K_CHIP0 + 5) { scopy(eingabe, chip_text[id - K_CHIP0], 96); einlen = slen(eingabe); ausfuehren(); }
        else if (zustand == Z_EDIT && id == K_SPEICHERN) { dok_speichern(); zeichne_editor(0); }
        else if (zustand == Z_EDIT && id == K_NEU) { if (doklen > 0 && dokgeaendert) dok_speichern(); dok_neu(); zeichne_editor(1); }
        else if (zustand == Z_EDIT && id == K_SCHLIESSEN) editor_schliessen();
        return;
    }
}

static void rohtaste(uint8_t k, int shift, int altgr) { unsigned n = (qt + 1) % QN; if (n != qh) { kq[qt] = (uint16_t)(k | (shift << 8) | (altgr << 9)); qt = n; } }

static void tick(void) {
    uint32_t t = timer_get_ticks();
    if (qh != qt) letzte_eingabe = t;
    while (qh != qt) { uint16_t e = kq[qh]; qh = (qh + 1) % QN; taste((uint8_t)(e & 255), (e >> 8) & 1, (e >> 9) & 1); }
    const struct usb_pointer* p = usb_pointer_state();
    if (p && p->present) {
        static int lpx, lpy;
        if (p->x != lpx || p->y != lpy) { lpx = p->x; lpy = p->y; letzte_eingabe = t; }
        int down = p->buttons & 1;
        if (down && !was_down) {
            if (zustand == Z_DESK && p->x >= zr_x() && p->x < zr_x() + zr_w() && p->y >= zr_y() && p->y < zr_y() + zr_h()) {
                if (p->x >= zr_x() + zr_w() - 24 && p->y >= zr_y() + zr_h() - 24) zdrag = 2;
                else { zdrag = 1; zdx = p->x - zx; zdy = p->y - zy; }
            } else klick(p->x, p->y);
        }
        if (down && zdrag && zustand == Z_DESK) {
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
    if (ph != robot_phase) { robot_phase = ph; if (zustand == Z_DESK && !zdrag) zeichne_robot(); }
}

int desktop_start(void) {
    if (!gfx_available()) return -1;
    gfx_size(&W, &H);
    lang = 0;
    static char cfg[64]; int r = synfs_read("sys.config", cfg, sizeof(cfg) - 1);
    if (r > 0) { cfg[r] = 0; for (int i = 0; cfg[i]; i++) if (cfg[i] == 'l' && cfg[i + 1] == 'a' && cfg[i + 2] == 'n' && cfg[i + 3] == 'g' && cfg[i + 4] == '=') { lang = (cfg[i + 5] == 'd' ? 0 : 1); break; } }
    bar_w = W - 80 < 760 ? W - 80 : 760; bar_x = (W - bar_w) / 2; bar_y = H - 150;
    fname[0] = fpw[0] = 0; ffocus = 0; fmsg = 0; einlen = 0; eingabe[0] = 0; nantwort = 0; qh = qt = 0; was_down = 0;
    zx = 24 + 90; zy = 56 + 108; zs = 100; lbw = 0; zdrag = 0; letzte_eingabe = timer_get_ticks();
    gfx_console(0);
    usb_set_rawgrab(rohtaste);
    shell_set_tick(tick);
    kprint("[DESKTOP] Zynex OS Quantum Desktop, Bildschirm "); kprint_dec((uint32_t)W); kprint("x"); kprint_dec((uint32_t)H); kprint("\n");
    if (benutzer_gesperrt()) { zustand = Z_LOGIN; kprint("[DESKTOP] Anmeldebildschirm\n"); zeichne_login(); }
    else { zustand = Z_DESK; reply1(TR("Hallo! Ich bin Zynie. Was moechtest du tun?", "Hello! I am Zynie. What would you like to do?")); zeichne_desktop(); }
    return 0;
}
