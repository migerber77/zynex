/*
 * Zynex OS - WLAN-Station (chip-unabhaengig). Siehe include/wlan.h.
 * Ablauf: Suchen (Probe-Request je Kanal, Beacons/Probe-Responses sammeln) -> Auth (offenes System) -> Assoc (mit RSN-Element)
 * -> EAPOL-4-Wege-Handshake (WPA2-PSK, CCMP) -> Datenrahmen in Software ver-/entschluesseln. Die rohen 802.11-Rahmen
 * liefert und nimmt der Chip-Treiber (struct wlan_hw).
 */
#include "wlan.h"
#include "wpa.h"
#include "crypto.h"
#include "timer.h"

extern void kprint(const char* s);
extern void kprint_dec(uint32_t v);
extern void kprint_hex(uint32_t v);
extern void net_wlan_attach(const uint8_t* mac);
extern void net_wlan_detach(void);

static void mcpy(void* d, const void* s, uint32_t n) { uint8_t* a = d; const uint8_t* b = s; while (n--) *a++ = *b++; }
static void mset(void* d, uint8_t v, uint32_t n) { uint8_t* a = d; while (n--) *a++ = v; }
static int  mcmp(const void* x, const void* y, uint32_t n) { const uint8_t* a = x; const uint8_t* b = y; while (n--) { if (*a != *b) return *a < *b ? -1 : 1; a++; b++; } return 0; }
static uint32_t slen(const char* s) { uint32_t n = 0; while (s[n]) n++; return n; }
static int be16(const uint8_t* p) { return (p[0] << 8) | p[1]; }
static void put_be16(uint8_t* p, int v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }

static const uint8_t BC[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
#define MAXAP 16
struct ap { uint8_t bssid[6]; char ssid[33]; uint8_t ch, sec, grp; int rssi; };       /* sec: 0 offen, 1 WPA2-PSK/CCMP, 2 nicht unterstuetzt */
enum { S_AUS, S_AUTH, S_ASSOC, S_HS, S_OK };

static const struct wlan_hw* hw;
static struct ap aps[MAXAP]; static int nap;
static int st, kanal_jetzt, auth_ok, assoc_ok, assoc_status;
static uint8_t bssid[6]; static char c_ssid[33]; static uint8_t c_sec, c_grp;
static uint8_t pmk[32], ptk[48], anonce[32], snonce[32], tk[16], gtk[16], rsn[24]; static int rsn_len;
static int have_tk, have_gtk; static uint64_t replay_last, tx_pn, rx_pn;
static uint16_t seqnr;
static int net_koppeln = 1, dbg;
static void (*eth_cb)(const uint8_t*, int);

/* ---------- Rahmen bauen ---------- */
static int mgmt_kopf(uint8_t* b, int subtype, const uint8_t* da) {
    mset(b, 0, 24); b[0] = (uint8_t)(subtype << 4);
    mcpy(b + 4, da, 6); mcpy(b + 10, hw->mac, 6); mcpy(b + 16, subtype == 4 ? BC : bssid, 6);
    seqnr++; b[22] = (uint8_t)(seqnr << 4); b[23] = (uint8_t)(seqnr >> 4);
    return 24;
}
static int ie_ssid(uint8_t* b, const char* s) { int l = (int)slen(s); b[0] = 0; b[1] = (uint8_t)l; mcpy(b + 2, s, (uint32_t)l); return 2 + l; }
static int ie_rates(uint8_t* b) { static const uint8_t r[8] = { 0x82, 0x84, 0x8B, 0x96, 0x0C, 0x12, 0x18, 0x24 }; b[0] = 1; b[1] = 8; mcpy(b + 2, r, 8); return 10; }

static void sende_probe(void) {
    uint8_t b[96]; int n = mgmt_kopf(b, 4, BC);
    n += ie_ssid(b + n, ""); n += ie_rates(b + n);
    hw->tx(b, n);
}
static void sende_auth(void) {
    uint8_t b[40]; int n = mgmt_kopf(b, 11, bssid);
    b[n++] = 0; b[n++] = 0; b[n++] = 1; b[n++] = 0; b[n++] = 0; b[n++] = 0;          /* offenes System, Folge 1, Status 0 */
    hw->tx(b, n);
}
static void baue_rsn(void) {
    static const uint8_t t[20] = { 0x30, 0x12, 0x01, 0x00, 0x00, 0x0F, 0xAC, 0x04, 0x01, 0x00, 0x00, 0x0F, 0xAC, 0x04, 0x01, 0x00, 0x00, 0x0F, 0xAC, 0x02 };
    mcpy(rsn, t, 20); rsn[7] = c_grp ? c_grp : 4; rsn[20] = 0; rsn[21] = 0; rsn[1] = 20; rsn_len = 22;          /* Gruppenchiffre wie beim Zugangspunkt */
}
static void sende_assoc(void) {
    uint8_t b[160]; int n = mgmt_kopf(b, 0, bssid);
    b[n++] = 0x01; b[n++] = 0x04; b[n++] = 10; b[n++] = 0;                              /* Faehigkeiten (ESS, kurze Slotzeit), Listen-Intervall */
    n += ie_ssid(b + n, c_ssid); n += ie_rates(b + n);
    if (c_sec == 1) { baue_rsn(); mcpy(b + n, rsn, (uint32_t)rsn_len); n += rsn_len; }
    hw->tx(b, n);
}

/* ---------- CCMP ---------- */
static void ccmp_aad(const uint8_t* f, int qos, int a4, uint8_t* aad, int* al) {
    int n = 0;
    aad[n++] = (uint8_t)(f[0] & (qos ? ~0x70 : 0xFF)); aad[n++] = (uint8_t)((f[1] & ~(0x08 | 0x10 | 0x20)) & (qos ? ~0x80 : 0xFF));
    mcpy(aad + n, f + 4, 18); n += 18;                                                  /* A1, A2, A3 */
    aad[n++] = (uint8_t)(f[22] & 0x0F); aad[n++] = 0;                                   /* Folgenummer maskiert */
    if (a4) { mcpy(aad + n, f + 24, 6); n += 6; }
    if (qos) { aad[n++] = (uint8_t)(f[24 + (a4 ? 6 : 0)] & 0x0F); aad[n++] = 0; }
    *al = n;
}
static void ccmp_nonce(uint8_t* nc, int tid, const uint8_t* a2, uint64_t pn) {
    nc[0] = (uint8_t)tid; mcpy(nc + 1, a2, 6);
    for (int i = 0; i < 6; i++) nc[7 + i] = (uint8_t)(pn >> (8 * (5 - i)));
}

/* Ethernet-Nutzlast als Datenrahmen zum Zugangspunkt (To-DS); protect = 1: CCMP */
static int tx_daten(const uint8_t* da, int ethertype, const uint8_t* pl, int len, int protect) {
    static uint8_t b[2400]; uint8_t llc[8] = { 0xAA, 0xAA, 0x03, 0, 0, 0, (uint8_t)(ethertype >> 8), (uint8_t)ethertype };
    if (len > 1500) return -1;
    mset(b, 0, 24); b[0] = 0x08; b[1] = (uint8_t)(0x01 | (protect ? 0x40 : 0));
    mcpy(b + 4, bssid, 6); mcpy(b + 10, hw->mac, 6); mcpy(b + 16, da, 6);
    seqnr++; b[22] = (uint8_t)(seqnr << 4); b[23] = (uint8_t)(seqnr >> 4);
    int n = 24;
    if (!protect) { mcpy(b + n, llc, 8); mcpy(b + n + 8, pl, (uint32_t)len); return hw->tx(b, n + 8 + len); }
    uint8_t plain[1520]; mcpy(plain, llc, 8); mcpy(plain + 8, pl, (uint32_t)len);
    tx_pn++;
    b[n] = (uint8_t)tx_pn; b[n + 1] = (uint8_t)(tx_pn >> 8); b[n + 2] = 0; b[n + 3] = 0x20; b[n + 4] = (uint8_t)(tx_pn >> 16); b[n + 5] = (uint8_t)(tx_pn >> 24); b[n + 6] = (uint8_t)(tx_pn >> 32); b[n + 7] = (uint8_t)(tx_pn >> 40);
    uint8_t nc[13], aad[32]; int al; ccmp_nonce(nc, 0, hw->mac, tx_pn); ccmp_aad(b, 0, 0, aad, &al);
    ccm_seal(tk, nc, aad, (uint32_t)al, plain, (uint32_t)(8 + len), b + n + 8);
    return hw->tx(b, n + 8 + 8 + len + 8);
}

/* ---------- EAPOL ---------- */
#define EK_HDR 99
static int mic_pruefen(const uint8_t* e, int n) {
    uint8_t k[512], m[16]; if (n > 512) return 0;
    mcpy(k, e, (uint32_t)n); mset(k + 81, 0, 16);
    wpa_eapol_mic(ptk, 2, k, (uint32_t)n, m);
    return mcmp(m, e + 81, 16) == 0;
}
static void eapol_senden(int ki, const uint8_t* rc, const uint8_t* nonce, const uint8_t* data, int dl) {
    uint8_t e[256]; mset(e, 0, EK_HDR); e[0] = 2; e[1] = 3; put_be16(e + 2, 95 + dl); e[4] = 2; put_be16(e + 5, ki);
    mcpy(e + 9, rc, 8); if (nonce) mcpy(e + 17, nonce, 32);
    put_be16(e + 97, dl); mcpy(e + EK_HDR, data, (uint32_t)dl);
    uint8_t m[16]; wpa_eapol_mic(ptk, 2, e, (uint32_t)(EK_HDR + dl), m); mcpy(e + 81, m, 16);
    tx_daten(bssid, 0x888E, e, EK_HDR + dl, have_tk && st == S_OK);
}
static int gtk_suchen(const uint8_t* d, int n) {                                         /* Schluesseldaten nach GTK-KDE durchsuchen */
    while (n >= 2) {
        int t = d[0], l = d[1]; if (t == 0xDD && l == 0) break; if (l + 2 > n) break;
        if (t == 0xDD && l >= 22 && d[2] == 0x00 && d[3] == 0x0F && d[4] == 0xAC && d[5] == 1) { mcpy(gtk, d + 8, 16); have_gtk = 1; return 0; }
        d += l + 2; n -= l + 2;
    }
    return -1;
}
static void eapol_rx(const uint8_t* e, int n) {
    if (n < EK_HDR || e[1] != 3 || (e[4] != 2 && e[4] != 254)) return;
    int ki = be16(e + 5), dl = be16(e + 97); if (EK_HDR + dl > n) return;
    uint64_t rc = 0; for (int i = 0; i < 8; i++) rc = (rc << 8) | e[9 + i];
    if (ki & 8) {                                                                       /* paarweise */
        if (!(ki & 0x80)) return;
        if (!(ki & 0x100)) {                                                            /* Nachricht 1 */
            if (st < S_HS || c_sec != 1) return;
            mcpy(anonce, e + 17, 32); krand(snonce, 32); replay_last = rc; have_tk = 0;
            wpa_ptk(pmk, bssid, hw->mac, anonce, snonce, 0, ptk);
            baue_rsn();
            eapol_senden(0x010A, e + 9, snonce, rsn, rsn_len);                          /* Nachricht 2 */
        } else {                                                                        /* Nachricht 3 */
            if (st != S_HS || rc <= replay_last || mcmp(anonce, e + 17, 32) != 0 || !mic_pruefen(e, EK_HDR + dl)) return;
            replay_last = rc;
            uint8_t kd[200];
            if ((ki & 0x1000) && dl >= 24 && dl <= 208) { if (aes_unwrap(ptk + 16, e + EK_HDR, (uint32_t)dl, kd) != 0) return; gtk_suchen(kd, dl - 8); }
            eapol_senden(0x030A, e + 9, 0, 0, 0);                                       /* Nachricht 4 (noch unverschluesselt) */
            mcpy(tk, ptk + 32, 16); have_tk = 1; tx_pn = 0; rx_pn = 0; st = S_OK;
        }
    } else if ((ki & 0x80) && (ki & 0x100) && st == S_OK) {                              /* Gruppenschluessel erneuern */
        if (rc <= replay_last || !mic_pruefen(e, EK_HDR + dl)) return;
        replay_last = rc;
        uint8_t kd[200];
        if ((ki & 0x1000) && dl >= 24 && dl <= 208) { if (aes_unwrap(ptk + 16, e + EK_HDR, (uint32_t)dl, kd) != 0) return; gtk_suchen(kd, dl - 8); }
        eapol_senden(0x0302, e + 9, 0, 0, 0);
    }
}

/* ---------- Empfang ---------- */
static void ies_lesen(const uint8_t* p, int n, struct ap* a) {
    int ccmp = 0, psk = 0, pmf = 0, rsn_da = 0; a->grp = 4;
    while (n >= 2) {
        int id = p[0], l = p[1]; if (l + 2 > n) break;
        if (id == 0 && l <= 32) { mcpy(a->ssid, p + 2, (uint32_t)l); a->ssid[l] = 0; }
        else if (id == 3 && l >= 1) a->ch = p[2];
        else if (id == 48 && l >= 20) {
            const uint8_t* r = p + 2; int k = 2; rsn_da = 1;
            a->grp = r[k + 3]; k += 4;
            int pc = r[k] | (r[k + 1] << 8); k += 2;
            for (int i = 0; i < pc && k + 4 <= l; i++, k += 4) if (r[k + 3] == 4) ccmp = 1;
            int ac = (k + 2 <= l) ? (r[k] | (r[k + 1] << 8)) : 0; k += 2;
            for (int i = 0; i < ac && k + 4 <= l; i++, k += 4) if (r[k + 3] == 2) psk = 1;
            if (k + 2 <= l && (r[k] & 0x40)) pmf = 1;                                  /* geschuetzte Verwaltungsrahmen Pflicht: nicht unterstuetzt */
        }
        p += l + 2; n -= l + 2;
    }
    a->sec = rsn_da ? ((ccmp && psk && !pmf) ? 1 : 2) : 0;
}
static void ap_merken(const uint8_t* f, int n, int rssi) {
    if (n < 38) return;
    struct ap a; mset(&a, 0, sizeof(a)); mcpy(a.bssid, f + 16, 6); a.ch = (uint8_t)kanal_jetzt; a.rssi = rssi;
    ies_lesen(f + 36, n - 36, &a);
    if (!(a.ssid[0])) return;                                                           /* versteckte Netze ignorieren */
    if (!(f[34] & 0x01)) return;                                                        /* kein Infrastruktur-Netz */
    if (a.sec == 0 && (f[34] & 0x10)) a.sec = 2;                                        /* Datenschutz ohne RSN: WEP/WPA1 */
    for (int i = 0; i < nap; i++) if (!mcmp(aps[i].bssid, a.bssid, 6)) { if (rssi > aps[i].rssi || !aps[i].rssi) aps[i] = a; return; }
    if (nap < MAXAP) aps[nap++] = a;
}
static void daten_rx(const uint8_t* f, int n) {
    if (st < S_HS || n < 32 || (f[1] & 3) != 2 || mcmp(f + 10, bssid, 6) != 0) return;     /* nur vom Zugangspunkt (From-DS) */
    int sub = f[0] >> 4, qos = (sub & 8) != 0, hl = 24 + (qos ? 2 : 0), uni = !(f[4] & 1);
    if (sub & 4) return;                                                                /* Null-Rahmen */
    if (uni && mcmp(f + 4, hw->mac, 6) != 0) return;
    static uint8_t pt[2400]; const uint8_t* p; int pl;
    if (f[1] & 0x40) {
        if (n < hl + 8 + 8 + 8) return;
        const uint8_t* k = uni ? tk : gtk; if (uni ? !have_tk : !have_gtk) return;
        if (!(f[hl + 3] & 0x20)) return;
        uint64_t pn = (uint64_t)f[hl] | ((uint64_t)f[hl + 1] << 8) | ((uint64_t)f[hl + 4] << 16) | ((uint64_t)f[hl + 5] << 24) | ((uint64_t)f[hl + 6] << 32) | ((uint64_t)f[hl + 7] << 40);
        if (uni && pn <= rx_pn) return;
        uint8_t nc[13], aad[32]; int al; ccmp_nonce(nc, qos ? (f[24] & 0x0F) : 0, f + 10, pn); ccmp_aad(f, qos, 0, aad, &al);
        pl = n - hl - 8 - 8; if (pl < 8 || pl > 2300) return;
        if (ccm_open(k, nc, aad, (uint32_t)al, f + hl + 8, (uint32_t)(pl + 8), pt) != 0) return;
        if (uni) rx_pn = pn;
        p = pt;
    } else { p = f + hl; pl = n - hl; }
    if (pl < 8 || p[0] != 0xAA || p[1] != 0xAA || p[2] != 3) return;
    int et = be16(p + 6);
    if (et == 0x888E) { eapol_rx(p + 8, pl - 8); return; }
    if (st != S_OK || (c_sec == 1 && !(f[1] & 0x40)) || !eth_cb) return;                 /* im WPA2-Netz nur verschluesselte Nutzdaten weitergeben */
    static uint8_t eth[1600]; if (pl - 8 > 1500) return;
    mcpy(eth, f + 4, 6); mcpy(eth + 6, f + 16, 6); put_be16(eth + 12, et); mcpy(eth + 14, p + 8, (uint32_t)(pl - 8));
    eth_cb(eth, 14 + pl - 8);
}
static void behandle(const uint8_t* f, int n, int rssi) {
    if (n < 24) return;
    int type = (f[0] >> 2) & 3, sub = f[0] >> 4;
    if (type == 2) { daten_rx(f, n); return; }
    if (type != 0) return;
    if (sub == 8 || sub == 5) { ap_merken(f, n, rssi); return; }
    if (mcmp(f + 10, bssid, 6) != 0 || st == S_AUS) return;
    if (sub == 11 && st == S_AUTH && n >= 30) { if (be16(f + 24) == 0 && f[26] == 2 && f[28] == 0 && f[29] == 0) auth_ok = 1; }
    else if (sub == 1 && st == S_ASSOC && n >= 30) { assoc_status = f[26] | (f[27] << 8); if (assoc_status == 0) { assoc_ok = 1; st = c_sec == 1 ? S_HS : S_OK; } else assoc_ok = -1; }
    else if (sub == 12 || sub == 10) { if (st != S_AUS) { st = S_AUS; have_tk = have_gtk = 0; kprint("[WLAN] Verbindung vom Zugangspunkt getrennt\n"); if (net_koppeln) net_wlan_detach(); } }
}
static void pumpe(void) {
    static uint8_t b[2400]; int rssi = 0, n, guard = 0;
    while (guard++ < 64 && (n = hw->rx(b, sizeof(b), &rssi)) > 0) behandle(b, n, rssi);
}

/* ---------- Oeffentliche Funktionen ---------- */
int wlan_attach(const struct wlan_hw* h) { hw = h; st = S_AUS; nap = 0; return 0; }
void wlan_detach(void) { if (st == S_OK && net_koppeln) net_wlan_detach(); hw = 0; st = S_AUS; }
int wlan_vorhanden(void) { return hw != 0; }
int wlan_verbunden(void) { return hw && st == S_OK; }

static void warte(uint32_t ticks, int* flag) {
    uint32_t t0 = timer_get_ticks();
    for (uint32_t it = 0; it < 20000000u && timer_get_ticks() - t0 < ticks; it++) { pumpe(); if (flag && *flag) return; }       /* it: Notbremse, falls die Uhr stehen bleibt */
}
static void sec_text(int s) { kprint(s == 0 ? "offen" : s == 1 ? "WPA2-PSK" : "nicht unterstuetzt"); }

static void suche_durchlauf(void) {
    nap = 0; st = S_AUS;
    for (int ch = 1; ch <= 13; ch++) {
        kanal_jetzt = ch; if (hw->set_channel(ch) != 0) continue;
        if (dbg) { kprint("[WLAN] Kanal "); kprint_dec((uint32_t)ch); kprint(" t="); kprint_dec(timer_get_ticks()); kprint("\n"); }
        sende_probe(); warte(12, 0);
    }
}
int wlan_suchen(void) {
    if (!hw) { kprint("Kein WLAN-Adapter.\n"); return -1; }
    wlan_trennen(); suche_durchlauf();
    kprint("WLAN-Netze: "); kprint_dec((uint32_t)nap); kprint("\n");
    for (int i = 0; i < nap; i++) { kprint("  "); kprint(aps[i].ssid); kprint("  Kanal "); kprint_dec(aps[i].ch); kprint("  Signal "); kprint_dec((uint32_t)(aps[i].rssi < 0 ? -aps[i].rssi : aps[i].rssi)); kprint(aps[i].rssi < 0 ? " dBm minus  " : "  "); sec_text(aps[i].sec); kprint("\n"); }
    return nap;
}
void wlan_trennen(void) {
    if (!hw) return;
    if (st >= S_AUTH) { uint8_t b[32]; int n = mgmt_kopf(b, 12, bssid); b[n++] = 3; b[n++] = 0; hw->tx(b, n); }
    if (st == S_OK && net_koppeln) net_wlan_detach();
    st = S_AUS; have_tk = have_gtk = 0;
}
int wlan_verbinden(const char* ssid, const char* pass) {
    if (!hw) return -1;
    wlan_trennen();
    int best = -1;
    for (int pass_n = 0; pass_n < 2 && best < 0; pass_n++) {
        for (int i = 0; i < nap; i++) if (slen(ssid) == slen(aps[i].ssid) && mcmp(ssid, aps[i].ssid, slen(ssid)) == 0 && (best < 0 || aps[i].rssi > aps[best].rssi)) best = i;
        if (best < 0) suche_durchlauf();
    }
    if (best < 0) return -2;
    struct ap* a = &aps[best];
    if (a->sec == 2) return -3;
    if (a->sec == 1) { uint32_t pl = pass ? slen(pass) : 0; if (pl < 8 || pl > 63) return -4; wpa_psk(pass, a->ssid, pmk); }
    mcpy(bssid, a->bssid, 6); mcpy(c_ssid, a->ssid, 33); c_sec = a->sec; c_grp = a->grp; kanal_jetzt = a->ch;
    have_tk = have_gtk = 0; replay_last = 0; auth_ok = 0; assoc_ok = 0;
    if (hw->set_channel(a->ch) != 0) return -5;
    st = S_AUTH;
    for (int t = 0; t < 4 && !auth_ok; t++) { sende_auth(); warte(30, &auth_ok); }
    if (!auth_ok) { st = S_AUS; return -6; }
    st = S_ASSOC;
    for (int t = 0; t < 4 && !assoc_ok; t++) { sende_assoc(); warte(30, &assoc_ok); }
    if (assoc_ok <= 0) { st = S_AUS; return assoc_ok < 0 ? -7 : -8; }
    if (c_sec == 1) { uint32_t t0 = timer_get_ticks(); while (timer_get_ticks() - t0 < 400 && st != S_OK) pumpe(); }
    if (st != S_OK) { st = S_AUS; return -9; }
    if (net_koppeln) net_wlan_attach(hw->mac);
    return 0;
}
int wlan_senden(const uint8_t* eth, int len) {
    if (!hw || st != S_OK || len < 14) return -1;
    return tx_daten(eth, be16(eth + 12), eth + 14, len - 14, c_sec == 1);                  /* Ziel = DA; Quelle ist unsere MAC */
}
void wlan_poll(void (*cb)(const uint8_t* eth, int len)) { if (!hw) return; eth_cb = cb; pumpe(); eth_cb = 0; }

static void mac_aus(const uint8_t* m) { const char* h = "0123456789abcdef"; char t[3]; t[2] = 0; for (int i = 0; i < 6; i++) { t[0] = h[m[i] >> 4]; t[1] = h[m[i] & 15]; kprint(t); if (i < 5) kprint(":"); } }
void wlan_befehl(const char* a) {
    while (*a == ' ') a++;
    if (!hw) { kprint("Kein WLAN-Adapter gefunden. (Unterstuetzte Chips: siehe Befehl 'wifi'.)\n"); return; }
    if (!*a || a[0] == 's') {
        if (a[0] == 's' && a[1] == 'u') { wlan_suchen(); return; }                       /* suchen */
        kprint("WLAN: "); kprint(hw->name); kprint(", MAC "); mac_aus(hw->mac);
        if (st == S_OK) { kprint(", verbunden mit "); kprint(c_ssid); kprint(" ("); mac_aus(bssid); kprint(")"); } else kprint(", nicht verbunden");
        kprint("\n  Befehle: wlan suchen | verbinden <Name> [Passwort] | trennen\n"); return;
    }
    if (a[0] == 't') { wlan_trennen(); kprint("WLAN getrennt.\n"); return; }
    if (a[0] == 'v') {
        while (*a && *a != ' ') a++; while (*a == ' ') a++;
        char ss[34], pw[66]; int i = 0;
        if (*a == '"') { a++; while (*a && *a != '"' && i < 32) ss[i++] = *a++; if (*a == '"') a++; } else while (*a && *a != ' ' && i < 32) ss[i++] = *a++;
        ss[i] = 0; while (*a == ' ') a++;
        i = 0; while (*a && i < 64) pw[i++] = *a++; pw[i] = 0;
        kprint("Verbinde mit "); kprint(ss); kprint(" ...\n");
        int r = wlan_verbinden(ss, pw);
        if (r == 0) kprint("WLAN verbunden.\n");
        else { kprint("Verbindung fehlgeschlagen ("); kprint(r == -2 ? "Netz nicht gefunden" : r == -3 ? "Verschluesselung nicht unterstuetzt" : r == -4 ? "Passwort muss 8-63 Zeichen haben" : r == -6 ? "keine Antwort bei der Anmeldung" : r == -7 ? "Zugangspunkt lehnt ab" : r == -9 ? "Schluesselaustausch gescheitert, Passwort falsch?" : "Fehler"); kprint(")\n"); }
        return;
    }
    kprint("wlan: suchen | verbinden <Name> [Passwort] | trennen | status\n");
}

/* ================= Selbsttest mit simuliertem Zugangspunkt ================= */
static const uint8_t T_AP[6] = { 2, 0, 0, 0, 0, 1 }, T_STA[6] = { 2, 0, 0, 0, 0, 2 };
static uint8_t tq[16][700]; static int tql[16], tqn, tqr;
static int t_ch, t_keys, t_got_ping, t_phase;
static uint8_t t_pmk[32], t_ptk[48], t_gtk[16], t_an[32]; static uint64_t t_pn, t_gpn;
static const char* T_SSID = "ZynexTest"; static const char* T_PASS = "geheim1234";
static const uint8_t T_RSN[22] = { 0x30, 0x14, 1, 0, 0, 0x0F, 0xAC, 4, 1, 0, 0, 0x0F, 0xAC, 4, 1, 0, 0, 0x0F, 0xAC, 2, 0, 0 };

static void t_push(const uint8_t* f, int n) { if (tqn - tqr >= 16) return; mcpy(tq[tqn & 15], f, (uint32_t)n); tql[tqn & 15] = n; tqn++; }
static int t_mgmt(uint8_t* b, int sub, const uint8_t* da) { mset(b, 0, 24); b[0] = (uint8_t)(sub << 4); mcpy(b + 4, da, 6); mcpy(b + 10, T_AP, 6); mcpy(b + 16, T_AP, 6); return 24; }
static int t_beacon(uint8_t* b, int sub) {
    int n = t_mgmt(b, sub, sub == 5 ? T_STA : BC); mset(b + n, 0, 8); n += 8; b[n++] = 100; b[n++] = 0; b[n++] = 0x11; b[n++] = 0x04;
    b[n++] = 0; b[n++] = (uint8_t)slen(T_SSID); mcpy(b + n, T_SSID, slen(T_SSID)); n += (int)slen(T_SSID);
    n += ie_rates(b + n); b[n++] = 3; b[n++] = 1; b[n++] = 6; mcpy(b + n, T_RSN, 22); n += 22; return n;
}
static void t_data_aus(const uint8_t* da, const uint8_t* sa, int prot, const uint8_t* key, uint64_t* pn, int et, const uint8_t* pl, int len) {
    uint8_t b[700], plain[600]; mset(b, 0, 24); b[0] = 0x08; b[1] = (uint8_t)(0x02 | (prot ? 0x40 : 0));
    mcpy(b + 4, da, 6); mcpy(b + 10, T_AP, 6); mcpy(b + 16, sa, 6);
    uint8_t llc[8] = { 0xAA, 0xAA, 3, 0, 0, 0, (uint8_t)(et >> 8), (uint8_t)et };
    if (!prot) { mcpy(b + 24, llc, 8); mcpy(b + 32, pl, (uint32_t)len); t_push(b, 32 + len); return; }
    mcpy(plain, llc, 8); mcpy(plain + 8, pl, (uint32_t)len); (*pn)++;
    uint64_t v = *pn; b[24] = (uint8_t)v; b[25] = (uint8_t)(v >> 8); b[27] = 0x20; b[28] = (uint8_t)(v >> 16); b[29] = (uint8_t)(v >> 24); b[30] = (uint8_t)(v >> 32); b[31] = (uint8_t)(v >> 40);
    uint8_t nc[13], aad[32]; int al; ccmp_nonce(nc, 0, T_AP, v); ccmp_aad(b, 0, 0, aad, &al);
    ccm_seal(key, nc, aad, (uint32_t)al, plain, (uint32_t)(8 + len), b + 32);
    t_push(b, 32 + 8 + len + 8);
}
static void t_eapol(int ki, int keylen, uint64_t rc, const uint8_t* nonce, const uint8_t* data, int dl, const uint8_t* kck) {
    uint8_t e[300]; mset(e, 0, EK_HDR); e[0] = 2; e[1] = 3; put_be16(e + 2, 95 + dl); e[4] = 2; put_be16(e + 5, ki); put_be16(e + 7, keylen);
    for (int i = 0; i < 8; i++) e[9 + i] = (uint8_t)(rc >> (8 * (7 - i)));
    mcpy(e + 17, nonce, 32); put_be16(e + 97, dl); mcpy(e + EK_HDR, data, (uint32_t)dl);
    if (kck) { uint8_t m[16]; wpa_eapol_mic(kck, 2, e, (uint32_t)(EK_HDR + dl), m); mcpy(e + 81, m, 16); }
    t_data_aus(T_STA, T_AP, 0, 0, 0, 0x888E, e, EK_HDR + dl);
}
static void t_ap_rx(const uint8_t* f, int n) {
    uint8_t b[700]; int type = (f[0] >> 2) & 3, sub = f[0] >> 4;
    if (type == 0 && sub == 4) { if (t_ch == 6) t_push(b, t_beacon(b, 5)); }
    else if (type == 0 && sub == 11) { int m = t_mgmt(b, 11, T_STA); b[m++] = 0; b[m++] = 0; b[m++] = 2; b[m++] = 0; b[m++] = 0; b[m++] = 0; t_push(b, m); }
    else if (type == 0 && sub == 0) {
        int m = t_mgmt(b, 1, T_STA); b[m++] = 0x11; b[m++] = 0x04; b[m++] = 0; b[m++] = 0; b[m++] = 1; b[m++] = 0xC0; m += ie_rates(b + m); t_push(b, m);
        uint8_t an[32]; for (int i = 0; i < 32; i++) an[i] = (uint8_t)(0x40 + i); mcpy(t_an, an, 32);
        t_eapol(0x008A, 16, 1, an, 0, 0, 0);
    }
    else if (type == 2 && (f[1] & 3) == 1 && n >= 32) {
        if (!(f[1] & 0x40)) {                                                            /* EAPOL */
            const uint8_t* e = f + 32; int ki = be16(e + 5), dl = be16(e + 97);
            if (ki == 0x010A) {                                                          /* Nachricht 2 */
                wpa_psk(T_PASS, T_SSID, t_pmk); wpa_ptk(t_pmk, T_AP, T_STA, t_an, e + 17, 0, t_ptk);
                uint8_t k[300], m[16]; mcpy(k, e, (uint32_t)(EK_HDR + dl)); mset(k + 81, 0, 16); wpa_eapol_mic(t_ptk, 2, k, (uint32_t)(EK_HDR + dl), m);
                if (mcmp(m, e + 81, 16) != 0) return;                                    /* falsches Passwort: keine Antwort */
                uint8_t pl[48], ct[56]; mcpy(pl, T_RSN, 22); pl[22] = 0xDD; pl[23] = 22; pl[24] = 0; pl[25] = 0x0F; pl[26] = 0xAC; pl[27] = 1; pl[28] = 1; pl[29] = 0;
                for (int i = 0; i < 16; i++) t_gtk[i] = (uint8_t)(0xA0 + i);
                mcpy(pl + 30, t_gtk, 16); pl[46] = 0xDD; pl[47] = 0; aes_wrap(t_ptk + 16, pl, 48, ct);
                t_eapol(0x13CA, 16, 2, t_an, ct, 56, t_ptk);
            } else if (ki == 0x030A) {                                                   /* Nachricht 4 */
                uint8_t k[300], m[16]; mcpy(k, e, (uint32_t)(EK_HDR + dl)); mset(k + 81, 0, 16); wpa_eapol_mic(t_ptk, 2, k, (uint32_t)(EK_HDR + dl), m);
                if (mcmp(m, e + 81, 16) == 0) t_keys = 1;
            }
        } else if (t_keys && n > 32 + 8 + 8) {                                           /* verschluesselte Daten von der Station */
            uint64_t pn = (uint64_t)f[24] | ((uint64_t)f[25] << 8) | ((uint64_t)f[28] << 16) | ((uint64_t)f[29] << 24) | ((uint64_t)f[30] << 32) | ((uint64_t)f[31] << 40);
            uint8_t nc[13], aad[32], pt[700]; int al; ccmp_nonce(nc, 0, f + 10, pn); ccmp_aad(f, 0, 0, aad, &al);
            int pl = n - 32 - 8; if (ccm_open(t_ptk + 32, nc, aad, (uint32_t)al, f + 32, (uint32_t)(pl + 8), pt) != 0) return;
            if (pl == 8 + 10 && mcmp(pt + 8, "PING-ZYNEX", 10) == 0) {
                t_got_ping = 1;
                t_data_aus(T_STA, T_AP, 1, t_ptk + 32, &t_pn, 0x0800, (const uint8_t*)"PONG-ZYNEX", 10);
                t_data_aus(BC, T_AP, 1, t_gtk, &t_gpn, 0x0806, (const uint8_t*)"GRUPPE", 6);
            }
        }
    }
}
static int t_set(int ch) { t_ch = ch; return 0; }
static int t_tx(const uint8_t* f, int n) { t_ap_rx(f, n); return 0; }
static int t_rx(uint8_t* out, int max, int* rssi) { if (tqr >= tqn) return 0; int n = tql[tqr & 15]; if (n > max) n = max; mcpy(out, tq[tqr & 15], (uint32_t)n); tqr++; *rssi = -40; return n; }
static const struct wlan_hw T_HW = { "Testadapter", { 2, 0, 0, 0, 0, 2 }, t_set, t_tx, t_rx };
static int t_pong, t_gruppe;
static void t_cb(const uint8_t* e, int n) {
    if (n == 14 + 10 && be16(e + 12) == 0x0800 && mcmp(e + 14, "PONG-ZYNEX", 10) == 0 && mcmp(e, T_STA, 6) == 0 && mcmp(e + 6, T_AP, 6) == 0) t_pong = 1;
    if (n == 14 + 6 && be16(e + 12) == 0x0806 && mcmp(e + 14, "GRUPPE", 6) == 0 && mcmp(e, BC, 6) == 0) t_gruppe = 1;
}
static int t_fehler;
static void t_erg(const char* name, int ok) { kprint("wlan: "); kprint(name); kprint(ok ? " OK\n" : " FEHLER\n"); if (!ok) t_fehler++; }

int wlan_selbsttest(void) {
    const struct wlan_hw* alt = hw; int alt_st = st; int alt_koppeln = net_koppeln;
    kprint("wlan: Selbsttest startet\n");
    t_fehler = 0; net_koppeln = 0; dbg = 1; tqn = tqr = 0; t_keys = 0; t_got_ping = 0; t_pn = 0; t_gpn = 0; t_pong = t_gruppe = 0;
    wlan_attach(&T_HW);
    int n = wlan_suchen();
    t_erg("Netzsuche findet das Testnetz (Kanal 6, WPA2)", n == 1 && nap == 1 && aps[0].ch == 6 && aps[0].sec == 1 && mcmp(aps[0].ssid, T_SSID, slen(T_SSID) + 1) == 0);
    int r = wlan_verbinden(T_SSID, "falschespw");
    t_erg("falsches Passwort wird abgelehnt (kein Handshake)", r == -9 && !t_keys);
    tqn = tqr = 0; t_keys = 0;
    r = wlan_verbinden(T_SSID, T_PASS);
    t_erg("Anmeldung und 4-Wege-Handshake", r == 0 && t_keys && st == S_OK && have_tk && have_gtk);
    t_erg("Gruppenschluessel aus Nachricht 3 uebernommen", have_gtk && mcmp(gtk, t_gtk, 16) == 0);
    uint8_t eth[64]; mcpy(eth, T_AP, 6); mcpy(eth + 6, T_STA, 6); put_be16(eth + 12, 0x0800); mcpy(eth + 14, "PING-ZYNEX", 10);
    r = wlan_senden(eth, 24);
    t_erg("verschluesseltes Paket zum Zugangspunkt (CCMP)", r == 0 && t_got_ping);
    wlan_poll(t_cb);
    t_erg("verschluesselte Antwort (Einzelschluessel) entschluesselt", t_pong);
    t_erg("verschluesselter Rundruf (Gruppenschluessel) entschluesselt", t_gruppe);
    wlan_trennen();
    hw = alt; st = alt_st; net_koppeln = alt_koppeln; dbg = 0;
    kprint(t_fehler ? "wlan: Selbsttest FEHLGESCHLAGEN\n" : "wlan: Selbsttest bestanden\n");
    return t_fehler;
}
