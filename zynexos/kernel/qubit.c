/*
 * Zynex OS - Quanten-Simulator (1-4 Qubits) als feste Kernel-Ressource.
 * Exakte Zustandsvektor-Simulation in Festkomma Q16.16. Das ist eine Simulation auf normalen Bits:
 * sie rechnet korrekt, bringt aber keinen Geschwindigkeitsvorteil echter Quantenhardware.
 */
#include "qubit.h"
#include "capability.h"
#include "task.h"

extern void kprint(const char* str);
extern void kprint_dec(uint32_t val);
extern void krand(uint8_t* p, uint32_t n);

struct amp { int32_t re, im; };
static struct amp a[16];
static int nq = 0;
#define R2 46341                                       /* 1/Wurzel(2) in Q16 */

static int32_t mul(int32_t x, int32_t c) { return (int32_t)(((int64_t)x * c) >> 16); }
static uint64_t isqrt(uint64_t v) { uint64_t r = 0, bit = 1ULL << 62; while (bit > v) bit >>= 2; while (bit) { if (v >= r + bit) { v -= r + bit; r = (r >> 1) + bit; } else r >>= 1; bit >>= 2; } return r; }
static int32_t prob_von(int i) { return (int32_t)(((int64_t)a[i].re * a[i].re + (int64_t)a[i].im * a[i].im) >> 16); }
static uint32_t zufall16(void) { uint8_t b[4]; krand(b, 4); return ((uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24)); }

int qubit_reset(int n) {
    if (n < 1 || n > QUBIT_MAX) return -1;
    nq = n;
    for (int i = 0; i < 16; i++) { a[i].re = 0; a[i].im = 0; }
    a[0].re = Q_EINS;
    return 0;
}
int qubit_anzahl(void) { return nq; }
static int ok(int q) { return nq > 0 && q >= 0 && q < nq; }

int qubit_gate(char g, int q) {
    if (!ok(q)) return -1;
    int m = 1 << q, dim = 1 << nq;
    for (int i = 0; i < dim; i++) {
        if (i & m) continue;
        struct amp x = a[i], y = a[i | m];
        switch (g) {
        case 'h': a[i].re = mul(x.re + y.re, R2); a[i].im = mul(x.im + y.im, R2); a[i | m].re = mul(x.re - y.re, R2); a[i | m].im = mul(x.im - y.im, R2); break;
        case 'x': a[i] = y; a[i | m] = x; break;
        case 'y': a[i].re = y.im; a[i].im = -y.re; a[i | m].re = -x.im; a[i | m].im = x.re; break;   /* [[0,-i],[i,0]] */
        case 'z': a[i | m].re = -y.re; a[i | m].im = -y.im; break;
        case 's': a[i | m].re = -y.im; a[i | m].im = y.re; break;                                     /* Phase i */
        case 't': a[i | m].re = mul(y.re - y.im, R2); a[i | m].im = mul(y.re + y.im, R2); break;       /* Phase e^(i*pi/4) */
        default: return -2;
        }
    }
    return 0;
}
int qubit_cnot(int c, int t) {
    if (!ok(c) || !ok(t) || c == t) return -1;
    for (int i = 0; i < (1 << nq); i++) if ((i >> c & 1) && !(i >> t & 1)) { struct amp x = a[i]; a[i] = a[i | (1 << t)]; a[i | (1 << t)] = x; }
    return 0;
}
int qubit_cz(int p, int q) {
    if (!ok(p) || !ok(q) || p == q) return -1;
    for (int i = 0; i < (1 << nq); i++) if ((i >> p & 1) && (i >> q & 1)) { a[i].re = -a[i].re; a[i].im = -a[i].im; }
    return 0;
}
int qubit_swap(int p, int q) {
    if (!ok(p) || !ok(q) || p == q) return -1;
    for (int i = 0; i < (1 << nq); i++) if ((i >> p & 1) && !(i >> q & 1)) { int j = (i ^ (1 << p)) | (1 << q); struct amp x = a[i]; a[i] = a[j]; a[j] = x; }
    return 0;
}

static void normiere(void) {
    int64_t s = 0; for (int i = 0; i < (1 << nq); i++) s += prob_von(i);
    if (s <= 0) return;
    uint64_t w = isqrt((uint64_t)s << 16);                       /* Wurzel der Norm in Q16 */
    int64_t f = ((int64_t)Q_EINS << 16) / (int64_t)w;              /* 1/Wurzel in Q16 */
    for (int i = 0; i < (1 << nq); i++) { a[i].re = (int32_t)(((int64_t)a[i].re * f) >> 16); a[i].im = (int32_t)(((int64_t)a[i].im * f) >> 16); }
}

int qubit_messen(int q) {
    if (!ok(q)) return -1;
    int64_t p1 = 0, tot = 0;
    for (int i = 0; i < (1 << nq); i++) { int32_t p = prob_von(i); tot += p; if (i >> q & 1) p1 += p; }
    if (tot <= 0) return -1;
    int64_t r = (int64_t)(zufall16() & 0xFFFF) * tot / Q_EINS;     /* gleichverteilt in [0, tot) */
    int res = r < p1 ? 1 : 0;
    for (int i = 0; i < (1 << nq); i++) if ((i >> q & 1) != res) { a[i].re = 0; a[i].im = 0; }
    normiere();
    return res;
}
int qubit_messen_alle(void) {
    if (nq < 1) return -1;
    int64_t tot = 0; for (int i = 0; i < (1 << nq); i++) tot += prob_von(i);
    if (tot <= 0) return -1;
    int64_t r = (int64_t)(zufall16() & 0xFFFF) * tot / Q_EINS, acc = 0; int res = (1 << nq) - 1;
    for (int i = 0; i < (1 << nq); i++) { acc += prob_von(i); if (r < acc) { res = i; break; } }
    for (int i = 0; i < (1 << nq); i++) { a[i].re = 0; a[i].im = 0; }
    a[res].re = Q_EINS;
    return res;
}
int32_t qubit_wahrscheinlichkeit(int z) { if (nq < 1 || z < 0 || z >= (1 << nq)) return -1; return prob_von(z); }

void qubit_ausgabe(void) {
    if (nq < 1) { kprint("Quantenregister: leer (qubit reset <1-4>)\n"); return; }
    kprint("Quantenregister: "); kprint_dec((uint32_t)nq); kprint(" Qubit(s)\n");
    for (int i = 0; i < (1 << nq); i++) {
        int32_t p = prob_von(i);
        kprint("  |"); for (int b = nq - 1; b >= 0; b--) kprint(i >> b & 1 ? "1" : "0"); kprint(">  ");
        uint32_t pm = (uint32_t)(((int64_t)p * 1000 + Q_EINS / 2) / Q_EINS);       /* Promille */
        kprint_dec(pm / 10); kprint("."); kprint_dec(pm % 10); kprint(" %\n");
    }
}

/* Systemaufruf: op 1 reset(n) 2 gate(code,q) 3 cnot(c,t) 4 messen(q) 5 wahrscheinlichkeit(zustand) 6 messen_alle 7 cz 8 swap */
int64_t qubit_syscall(uint64_t op, uint64_t x, uint64_t y) {
    if (!cap_check(CAP_QUANTUM, task_get_current_id())) return -100;                 /* Capability fehlt */
    switch (op) {
    case 1: return qubit_reset((int)x);
    case 2: return qubit_gate((char)x, (int)y);
    case 3: return qubit_cnot((int)x, (int)y);
    case 4: return qubit_messen((int)x);
    case 5: return qubit_wahrscheinlichkeit((int)x);
    case 6: return qubit_messen_alle();
    case 7: return qubit_cz((int)x, (int)y);
    case 8: return qubit_swap((int)x, (int)y);
    }
    return -1;
}

/* ---- Selbsttest mit bekannten Ergebnissen ---- */
static int t_ok, t_n;
static void pruefe(const char* name, int ok) { t_n++; if (ok) t_ok++; kprint("[QUBIT] "); kprint(name); kprint(ok ? ": OK\n" : ": FEHLT\n"); }
static int nahe(int32_t v, int32_t soll, int32_t tol) { int32_t d = v - soll; return d < 0 ? -d <= tol : d <= tol; }

void qubit_selbsttest(void) {
    t_ok = t_n = 0;
    qubit_reset(1); qubit_gate('h', 0); qubit_gate('h', 0);
    pruefe("H*H = Identitaet", nahe(qubit_wahrscheinlichkeit(0), Q_EINS, 40));
    qubit_reset(1); qubit_gate('x', 0);
    pruefe("X kippt |0> nach |1>", nahe(qubit_wahrscheinlichkeit(1), Q_EINS, 2));
    qubit_reset(1); qubit_gate('y', 0);
    pruefe("Y kippt |0> nach |1>", nahe(qubit_wahrscheinlichkeit(1), Q_EINS, 2));
    qubit_reset(1); qubit_gate('h', 0);
    pruefe("H gibt 50 % / 50 %", nahe(qubit_wahrscheinlichkeit(0), Q_EINS / 2, 40) && nahe(qubit_wahrscheinlichkeit(1), Q_EINS / 2, 40));
    qubit_reset(1); qubit_gate('h', 0); for (int i = 0; i < 8; i++) qubit_gate('t', 0); qubit_gate('h', 0);
    pruefe("T hoch 8 = Identitaet", nahe(qubit_wahrscheinlichkeit(0), Q_EINS, 200));
    qubit_reset(1); qubit_gate('h', 0); qubit_gate('s', 0); qubit_gate('s', 0); qubit_gate('h', 0);
    pruefe("S*S = Z (H S S H = X)", nahe(qubit_wahrscheinlichkeit(1), Q_EINS, 200));
    qubit_reset(2); qubit_gate('h', 0); qubit_cnot(0, 1);
    pruefe("Bell-Zustand: nur 00 und 11 je 50 %", nahe(qubit_wahrscheinlichkeit(0), Q_EINS / 2, 64) && nahe(qubit_wahrscheinlichkeit(3), Q_EINS / 2, 64) && qubit_wahrscheinlichkeit(1) < 8 && qubit_wahrscheinlichkeit(2) < 8);
    qubit_reset(4); qubit_gate('h', 0); qubit_cnot(0, 1); qubit_cnot(1, 2); qubit_cnot(2, 3);
    pruefe("GHZ mit 4 Qubits: nur 0000 und 1111", nahe(qubit_wahrscheinlichkeit(0), Q_EINS / 2, 100) && nahe(qubit_wahrscheinlichkeit(15), Q_EINS / 2, 100) && qubit_wahrscheinlichkeit(5) < 8);
    qubit_reset(2); qubit_gate('x', 0); qubit_swap(0, 1);
    pruefe("SWAP tauscht Qubits (|01> -> |10>)", nahe(qubit_wahrscheinlichkeit(2), Q_EINS, 2));
    qubit_reset(2); qubit_gate('x', 0); qubit_gate('h', 1); qubit_cz(0, 1); qubit_gate('h', 1);              /* H CZ H = CNOT: |01> -> |11> */
    pruefe("CZ-Gatter (H CZ H = CNOT)", nahe(qubit_wahrscheinlichkeit(3), Q_EINS, 100));
    int gleich = 0, elfen = 0, norm = 1, n = 200;
    for (int i = 0; i < n; i++) {
        qubit_reset(2); qubit_gate('h', 0); qubit_cnot(0, 1);
        int m0 = qubit_messen(0), m1 = qubit_messen(1);
        if (m0 == m1) gleich++;
        if (m0 == 1) elfen++;
        int32_t s = 0; for (int z = 0; z < 4; z++) s += qubit_wahrscheinlichkeit(z);
        if (!nahe(s, Q_EINS, 200)) norm = 0;
    }
    pruefe("Bell-Messung: beide Qubits immer gleich (200 Versuche)", gleich == n);
    pruefe("Messung zufaellig verteilt (70 bis 130 von 200 mal 1)", elfen >= 70 && elfen <= 130);
    pruefe("Zustand bleibt nach der Messung normiert", norm);
    qubit_reset(3); qubit_gate('h', 0); qubit_gate('h', 1); qubit_gate('h', 2);
    int alle_ok = 1; for (int i = 0; i < 40; i++) { qubit_reset(3); qubit_gate('h', 0); qubit_gate('h', 1); qubit_gate('h', 2); int r = qubit_messen_alle(); if (r < 0 || r > 7 || qubit_wahrscheinlichkeit(r) < Q_EINS - 2) alle_ok = 0; }
    pruefe("Messen aller Qubits kollabiert auf einen Basiszustand", alle_ok);
    pruefe("Falsche Eingaben werden abgelehnt", qubit_reset(5) < 0 && qubit_gate('h', 9) < 0 && qubit_cnot(1, 1) < 0);
    nq = 0;
    kprint("[QUBIT] Selbsttest: "); kprint_dec((uint32_t)t_ok); kprint("/"); kprint_dec((uint32_t)t_n); kprint(t_ok == t_n ? " bestanden\n" : " FEHLER\n");
}
