/* Firewall mit lokaler Anomalie-Erkennung (siehe fw.h). */
#include "fw.h"
#include "net.h"
#include "synfs.h"
#include "timer.h"

extern void kprint(const char* str);
extern void kprint_dec(uint32_t val);

#define MAXR 32
#define MAXQ 16
#define FENSTER 10            /* Sekunden je Beobachtungsfenster */
#define SCHWELLE 12           /* Punkte im Fenster -> Quarantaene */
#define QUARANTAENE 300       /* Sekunden */
#define LOGN 16

struct regel { uint8_t richtung, erlauben, proto, benutzt; uint16_t port; uint32_t ip, maske; };
struct quelle { uint32_t ip, t0, bis; uint32_t punkte; uint8_t icmp, syn, udp, pad; uint64_t ports; uint8_t benutzt; };

static int modus = FW_NORMAL;
static struct regel R[MAXR]; static int nr;
static struct quelle Q[MAXQ];
static uint32_t verworfen, zaehler_q;
static struct { uint32_t t; char text[72]; } lg[LOGN]; static int lgn;
/* ausgehend: neue Ziele je Fenster */
static uint32_t au_t0, au_ziele[24]; static int au_n;

static uint32_t jetzt(void) { return timer_get_seconds(); }
static uint16_t g16(const uint8_t* p) { return (uint16_t)((p[0] << 8) | p[1]); }
static uint32_t g32(const uint8_t* p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }
static int slen(const char* s) { int n = 0; while (s[n]) n++; return n; }
static int gleich(const char* a, const char* b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }
static int beginnt(const char* s, const char* p) { while (*p) { if (*s++ != *p++) return 0; } return 1; }

static void ip_text(uint32_t ip, char* o) {
    int n = 0;
    for (int b = 3; b >= 0; b--) {
        uint32_t v = (ip >> (b * 8)) & 255; char t[4]; int k = 0;
        do { t[k++] = (char)('0' + v % 10); v /= 10; } while (v);
        while (k) o[n++] = t[--k];
        if (b) o[n++] = '.';
    }
    o[n] = 0;
}
static void logge(const char* a, uint32_t ip, const char* b) {
    int i = lgn % LOGN; lgn++;
    char t[20]; ip_text(ip, t);
    int n = 0; for (const char* p = a; *p && n < 40; p++) lg[i].text[n++] = *p;
    lg[i].text[n++] = ' '; for (const char* p = t; *p && n < 58; p++) lg[i].text[n++] = *p;
    if (b[0]) { lg[i].text[n++] = ' '; for (const char* p = b; *p && n < 70; p++) lg[i].text[n++] = *p; }
    lg[i].text[n] = 0; lg[i].t = jetzt();
    kprint("[FIREWALL] "); kprint(lg[i].text); kprint("\n");
}

static struct quelle* hole(uint32_t ip) {
    uint32_t t = jetzt(); struct quelle* frei = 0; struct quelle* alt = &Q[0];
    for (int i = 0; i < MAXQ; i++) {
        if (Q[i].benutzt && Q[i].ip == ip) {
            if (t - Q[i].t0 >= FENSTER) { Q[i].t0 = t; Q[i].punkte = 0; Q[i].icmp = Q[i].syn = Q[i].udp = 0; Q[i].ports = 0; }
            return &Q[i];
        }
        if (!Q[i].benutzt && !frei) frei = &Q[i];
        if (Q[i].t0 < alt->t0 && Q[i].bis <= t) alt = &Q[i];
    }
    struct quelle* q = frei ? frei : alt;
    q->ip = ip; q->t0 = t; q->bis = 0; q->punkte = 0; q->icmp = q->syn = q->udp = 0; q->ports = 0; q->benutzt = 1;
    return q;
}
/* Punkte vergeben; ab der Schwelle Quarantaene */
static void bewerte(struct quelle* q, uint32_t p, const char* grund) {
    q->punkte += p;
    if (q->punkte >= SCHWELLE && q->bis <= jetzt()) {
        q->bis = jetzt() + QUARANTAENE; zaehler_q++;
        logge("Quarantaene fuer", q->ip, grund);
    }
}

static int passt(const struct regel* r, int richtung, uint32_t ip, uint8_t proto, uint16_t port) {
    if (!(r->richtung & richtung)) return 0;
    if ((ip & r->maske) != (r->ip & r->maske)) return 0;
    if (r->proto && r->proto != proto) return 0;
    if (r->port && r->port != port) return 0;
    return 1;
}
static int regel_urteil(int richtung, uint32_t ip, uint8_t proto, uint16_t port) {   /* 1 erlaubt, 0 blockiert, -1 keine Regel */
    for (int i = 0; i < nr; i++) if (passt(&R[i], richtung, ip, proto, port)) { R[i].benutzt = 1; return R[i].erlauben; }
    return -1;
}

int fw_ein(const uint8_t* ip, uint32_t len) {
    if (modus == FW_AUS) return 1;
    if (len < 20) return 1;
    uint32_t src = g32(ip + 12); uint8_t proto = ip[9]; int ihl = (ip[0] & 15) * 4;
    struct quelle* q = hole(src);
    if (q->bis > jetzt()) { verworfen++; return 0; }
    const uint8_t* pl = ip + ihl; int pn = (int)len - ihl;
    if (ihl < 20 || pn < 0) { bewerte(q, 6, "kaputte Pakete von"); verworfen++; return 0; }
    uint16_t sp = 0, dp = 0; uint8_t flags = 0;
    if ((proto == 6 && pn >= 20) || (proto == 17 && pn >= 8)) { sp = g16(pl); dp = g16(pl + 2); if (proto == 6) flags = pl[13]; }
    int u = regel_urteil(FW_EIN, src, proto, proto == 6 || proto == 17 ? dp : 0);
    if (u == 0) { verworfen++; return 0; }
    if (u == 1) return 1;
    if (proto == 1 && pn >= 8) {                                     /* ICMP */
        if (pl[0] == 8) { if (++q->icmp > 5) { bewerte(q, 4, "ICMP-Flut von"); verworfen++; return 0; } }
        return 1;
    }
    if (proto == 6) {
        if (pn < 20) { bewerte(q, 6, "kaputte TCP-Pakete von"); verworfen++; return 0; }
        if ((flags & 0x02) && !(flags & 0x10)) {                      /* SYN ohne ACK = ungefragter Verbindungsversuch */
            q->syn++; q->ports |= 1ull << (dp & 63);
            int verschieden = 0; for (int i = 0; i < 64; i++) if (q->ports >> i & 1) verschieden++;
            bewerte(q, verschieden > 3 ? 4 : 2, verschieden > 3 ? "Portscan von" : "ungefragter Verbindungsversuch von");
            verworfen++; return 0;
        }
        if ((flags & 0x3F) == 0 || (flags & 0x03) == 0x03 || (flags & 0x29) == 0x29) {   /* NULL-, SYN+FIN-, Xmas-Scan */
            bewerte(q, 6, "Scan-Pakete von"); verworfen++; return 0;
        }
        return 1;                                                   /* Antwort auf eigene Verbindung */
    }
    if (proto == 17) {
        if (sp == 53 || sp == 67 || dp == 68) return 1;             /* DNS-/DHCP-Antworten */
        if (++q->udp > 8) bewerte(q, 3, "UDP-Flut von");
        else bewerte(q, 1, "ungefragtes UDP von");
        verworfen++; return 0;
    }
    bewerte(q, 2, "unbekanntes Protokoll von"); verworfen++; return 0;
}

int fw_aus(uint32_t ziel, uint8_t proto, const uint8_t* pl, int len) {
    if (modus == FW_AUS) return 1;
    uint16_t dp = (proto == 6 || proto == 17) && len >= 4 ? g16(pl + 2) : 0;
    int u = regel_urteil(FW_AUSG, ziel, proto, dp);
    if (u == 0) { verworfen++; return 0; }
    if (u == 1 || ziel == 0xFFFFFFFFu) return 1;
    uint32_t t = jetzt();
    if (t - au_t0 >= FENSTER) { au_t0 = t; au_n = 0; }
    int bekannt = 0; for (int i = 0; i < au_n; i++) if (au_ziele[i] == ziel) bekannt = 1;
    if (!bekannt) {
        if (au_n < 24) au_ziele[au_n++] = ziel;
        if (au_n == 20) logge("Auffaellig viele neue Ziele, zuletzt", ziel, "");
    }
    if (modus == FW_STRENG) {
        if (proto == 17 && dp == 53) return 1;                      /* Namensaufloesung bleibt moeglich */
        if (proto == 1) return 1;
        logge("streng: ausgehend blockiert nach", ziel, ""); verworfen++; return 0;
    }
    return 1;
}

int fw_arp(uint32_t ip, const uint8_t* alt, const uint8_t* neu) {
    if (modus == FW_AUS) return 1;
    int gleichmac = 1; for (int i = 0; i < 6; i++) if (alt[i] != neu[i]) gleichmac = 0;
    if (gleichmac) return 1;
    logge("ARP-Faelschung abgewehrt:", ip, "wechselt die MAC"); verworfen++;
    return 0;
}

void fw_modus(int m) { modus = m; }
int fw_modus_get(void) { return modus; }
uint32_t fw_verworfen(void) { return verworfen; }
int fw_regeln_anzahl(void) { return nr; }
void fw_regeln_loeschen(void) { nr = 0; }
int fw_regel(int richtung, int erlauben, uint32_t ip, uint32_t maske, uint8_t proto, uint16_t port) {
    if (nr >= MAXR) return -1;
    R[nr].richtung = (uint8_t)richtung; R[nr].erlauben = (uint8_t)erlauben; R[nr].ip = ip; R[nr].maske = maske; R[nr].proto = proto; R[nr].port = port; R[nr].benutzt = 0; nr++;
    return 0;
}
void fw_freigeben(uint32_t ip) { for (int i = 0; i < MAXQ; i++) if (Q[i].benutzt && (!ip || Q[i].ip == ip)) { Q[i].bis = 0; Q[i].punkte = 0; } }

static void zahl(uint32_t v) { kprint_dec(v); }
void fw_status(void) {
    kprint("Firewall: "); kprint(modus == FW_AUS ? "AUS" : modus == FW_STRENG ? "STRENG (ausgehend nur erlaubte Ziele)" : "normal (ungefragtes von aussen wird verworfen)");
    kprint("\n  verworfene Pakete: "); zahl(verworfen); kprint(", Quarantaenen: "); zahl(zaehler_q); kprint(", Regeln: "); zahl((uint32_t)nr); kprint("\n");
    int n = 0; uint32_t t = jetzt();
    for (int i = 0; i < MAXQ; i++) if (Q[i].benutzt && Q[i].bis > t) {
        char a[20]; ip_text(Q[i].ip, a); kprint("  in Quarantaene: "); kprint(a); kprint(" noch "); zahl(Q[i].bis - t); kprint(" s\n"); n++;
    }
    if (!n) kprint("  keine Quarantaene\n");
}
void fw_log_zeigen(void) {
    int n = lgn < LOGN ? lgn : LOGN;
    if (!n) { kprint("Noch keine Ereignisse\n"); return; }
    for (int k = n; k > 0; k--) { int i = (lgn - k) % LOGN; kprint("  ["); zahl(lg[i].t); kprint(" s] "); kprint(lg[i].text); kprint("\n"); }
}
void fw_regeln_zeigen(void) {
    if (!nr) { kprint("Keine eigenen Regeln\n"); return; }
    for (int i = 0; i < nr; i++) {
        char a[20]; ip_text(R[i].ip, a);
        kprint("  "); zahl((uint32_t)i + 1); kprint(". "); kprint(R[i].erlauben ? "erlaube " : "blockiere ");
        kprint(R[i].richtung == FW_EIN ? "eingehend " : R[i].richtung == FW_AUSG ? "ausgehend " : "beide Richtungen ");
        kprint(R[i].ip || R[i].maske ? a : "alle"); if (R[i].maske != 0xFFFFFFFFu && R[i].maske) kprint("/..");
        if (R[i].proto) kprint(R[i].proto == 6 ? " tcp" : R[i].proto == 17 ? " udp" : " icmp");
        if (R[i].port) { kprint(" Port "); zahl(R[i].port); }
        kprint("\n");
    }
}

/* Regeln in SynFS ("firewall.cfg") */
void fw_speichern(void) {
    static uint8_t b[8 + MAXR * sizeof(struct regel)]; b[0] = 'F'; b[1] = 'W'; b[2] = '1'; b[3] = (uint8_t)modus; b[4] = (uint8_t)nr;
    uint8_t* p = (uint8_t*)R; for (uint32_t i = 0; i < (uint32_t)nr * sizeof(struct regel); i++) b[8 + i] = p[i];
    synfs_write("firewall.cfg", b, 8 + (uint32_t)nr * (uint32_t)sizeof(struct regel));
}
void fw_laden(void) {
    static uint8_t b[8 + MAXR * sizeof(struct regel)];
    int r = synfs_read("firewall.cfg", b, sizeof(b));
    if (r < 8 || b[0] != 'F' || b[1] != 'W' || b[2] != '1') return;
    int n = b[4]; if (n > MAXR || 8 + (uint32_t)n * sizeof(struct regel) > (uint32_t)r) return;
    modus = b[3] <= FW_STRENG ? b[3] : FW_NORMAL; nr = n;
    uint8_t* p = (uint8_t*)R; for (uint32_t i = 0; i < (uint32_t)n * sizeof(struct regel); i++) p[i] = b[8 + i];
}

/* Shell: firewall [status|log|regeln|an|aus|streng|normal|erlaube|blockiere|freigeben|loeschen] ... */
static const char* ueberspringe(const char* s) { while (*s == ' ') s++; return s; }
static int wort(const char** s, char* o, int max) { const char* p = ueberspringe(*s); int n = 0; while (*p && *p != ' ' && n < max - 1) o[n++] = *p++; o[n] = 0; *s = p; return n; }
void fw_befehl(const char* arg) {
    char w[24]; arg = ueberspringe(arg); wort(&arg, w, sizeof(w));
    if (!w[0] || gleich(w, "status")) { fw_status(); return; }
    if (gleich(w, "log")) { fw_log_zeigen(); return; }
    if (gleich(w, "regeln")) { fw_regeln_zeigen(); return; }
    if (gleich(w, "an") || gleich(w, "normal")) { modus = FW_NORMAL; fw_speichern(); kprint("Firewall an (normal)\n"); return; }
    if (gleich(w, "aus")) { modus = FW_AUS; fw_speichern(); kprint("Firewall AUS - ungeschuetzt\n"); return; }
    if (gleich(w, "streng")) { modus = FW_STRENG; fw_speichern(); kprint("Firewall streng: ausgehend nur an erlaubte Ziele (firewall erlaube <ip>), DNS und Ping gehen weiter\n"); return; }
    if (gleich(w, "loeschen")) { nr = 0; fw_speichern(); kprint("Alle Regeln geloescht\n"); return; }
    if (gleich(w, "freigeben")) {
        char a[24]; uint32_t ip = 0;
        if (wort(&arg, a, sizeof(a)) && net_parse_ip(a, &ip) != 0) { kprint("Adresse nicht lesbar\n"); return; }
        fw_freigeben(ip); kprint("Quarantaene aufgehoben\n"); return;
    }
    int erl = gleich(w, "erlaube"), blk = gleich(w, "blockiere");
    if (erl || blk) {
        char a[24], z1[16], z2[16]; uint32_t ip = 0, maske = 0xFFFFFFFFu; int ri = erl ? FW_AUSG : FW_BEIDE; uint8_t pr = 0; uint16_t port = 0;
        if (!wort(&arg, a, sizeof(a))) { kprint("firewall erlaube|blockiere <ip> [ein|aus] [tcp|udp|icmp] [port]\n"); return; }
        if (gleich(a, "alle")) { ip = 0; maske = 0; }
        else if (net_parse_ip(a, &ip) != 0) { kprint("Adresse nicht lesbar (nur IP, z. B. 1.2.3.4)\n"); return; }
        while (wort(&arg, z1, sizeof(z1))) {
            if (gleich(z1, "ein")) ri = FW_EIN; else if (gleich(z1, "aus")) ri = FW_AUSG; else if (gleich(z1, "beide")) ri = FW_BEIDE;
            else if (gleich(z1, "tcp")) pr = 6; else if (gleich(z1, "udp")) pr = 17; else if (gleich(z1, "icmp")) pr = 1;
            else { uint32_t v = 0; int ok = z1[0] != 0; for (int i = 0; z1[i]; i++) { if (z1[i] < '0' || z1[i] > '9') ok = 0; else v = v * 10 + (uint32_t)(z1[i] - '0'); } if (ok && v < 65536) port = (uint16_t)v; else { kprint("Unbekannt: "); kprint(z1); kprint("\n"); return; } }
        }
        (void)z2;
        if (fw_regel(ri, erl, ip, maske, pr, port) != 0) { kprint("Zu viele Regeln (max. 32)\n"); return; }
        fw_speichern(); kprint(erl ? "Regel angelegt: erlaubt\n" : "Regel angelegt: blockiert\n"); return;
    }
    kprint("firewall: status | log | regeln | an | aus | streng | erlaube <ip> [ein|aus] [tcp|udp|icmp] [port] | blockiere <ip> ... | freigeben [ip] | loeschen\n");
    (void)beginnt; (void)slen;
}
