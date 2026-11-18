/*
 * Zynex OS - .synbin-Lader
 * Liest ein Programm aus SynFS, prueft Kopf und Faehigkeiten und startet es.
 *
 * Wichtig: Das Programm laeuft im Kernel-Modus (Ring 0), ohne Speicherschutz.
 * Die Faehigkeitsmaske entscheidet, ob der Lader es startet und welche
 * Kernel-Funktionen es ueber die API-Tabelle sieht. Sie ist keine Sandbox.
 */

#include "synbin.h"
#include "synfs.h"
#include "kheap.h"
#include "timer.h"

extern void kprint(const char* str);
extern void kprint_dec(uint32_t val);
extern void kprint_hex(uint32_t val);

#define SYNBIN_MAX_FILE 8192
#define HDR_SIZE 50

static uint8_t allowed_caps = SYNCAP_CONSOLE;   /* Standard: nur Konsole */

void synbin_set_allowed(uint8_t mask) { allowed_caps = mask & SYNCAP_ALL; }
uint8_t synbin_get_allowed(void) { return allowed_caps; }

/* ---- Kernel-Funktionen fuer Programme ---- */
static void api_print(const char* t) { kprint(t); }
static void api_print_num(uint64_t v) {
    char b[24]; int i = 0;
    if (!v) { kprint("0"); return; }
    while (v) { b[i++] = (char)('0' + (v % 10)); v /= 10; }
    char o[24]; int n = 0;
    while (i) o[n++] = b[--i];
    o[n] = 0;
    kprint(o);
}
static uint64_t api_ticks(void) { return timer_get_ticks(); }

static const struct syn_api api = { api_print, api_print_num, api_ticks };

/* ---- Eingebaute Programme (vom Compiler beim Bauen erzeugt) ---- */
struct synbin_embedded { const char* name; const uint8_t* start; const uint8_t* end; };
extern const struct synbin_embedded synbin_embedded_list[];
extern const int synbin_embedded_count;

void synbin_install_embedded(void) {
    int ok = 0;
    for (int i = 0; i < synbin_embedded_count; i++) {
        const struct synbin_embedded* e = &synbin_embedded_list[i];
        if (synfs_create(e->name, SYNFS_FILE) != 0) continue;
        if (synfs_write(e->name, e->start, (uint32_t)(e->end - e->start)) < 0) continue;
        synfs_tag(e->name, "synbin");
        ok++;
    }
    kprint("[SYNBIN] ");
    kprint_dec((uint32_t)ok);
    kprint(" Programme im SynFS installiert\n");
}

static int streq8(const char* a, const char* b) {
    for (int i = 0; i < 8; i++) if (a[i] != b[i]) return 0;
    return 1;
}

/* Datei lesen und Kopf pruefen. Gibt Puffer zurueck (mit kfree freigeben) oder 0. */
static uint8_t* lade(const char* name, int* laenge, const struct synbin_header** kopf) {
    uint8_t* buf = (uint8_t*)kmalloc(SYNBIN_MAX_FILE);
    if (!buf) { kprint("[SYNBIN] Kein Speicher\n"); return 0; }
    int r = synfs_read(name, buf, SYNBIN_MAX_FILE);
    if (r < 0) { kprint("[SYNBIN] Datei nicht gefunden\n"); kfree(buf); return 0; }
    if (r < HDR_SIZE) { kprint("[SYNBIN] Datei zu kurz fuer einen Kopf\n"); kfree(buf); return 0; }
    const struct synbin_header* h = (const struct synbin_header*)buf;
    if (!streq8(h->magic, SYNBIN_MAGIC)) { kprint("[SYNBIN] Kein .synbin (Magic falsch)\n"); kfree(buf); return 0; }
    if (h->payload_size == 0 || h->payload_size != (uint64_t)(r - HDR_SIZE)) {
        kprint("[SYNBIN] Groesse im Kopf passt nicht zur Datei\n"); kfree(buf); return 0;
    }
    *laenge = r;
    *kopf = h;
    return buf;
}

int synbin_info(const char* name) {
    int len; const struct synbin_header* h;
    uint8_t* buf = lade(name, &len, &h);
    if (!buf) return -1;
    kprint(name); kprint(":\n  Ziel: ");
    kprint(h->target == SYNBIN_TARGET_CPU ? "CPU (x86-64)" : h->target == SYNBIN_TARGET_NPU ? "NPU (noch nicht unterstuetzt)" : "unbekannt");
    kprint("\n  Faehigkeiten: 0x"); kprint_hex(h->caps);
    kprint((h->caps & SYNCAP_CONSOLE) ? " Konsole" : "");
    kprint((h->caps & SYNCAP_TIMER) ? " Timer" : "");
    kprint("\n  Code: "); kprint_dec((uint32_t)h->payload_size); kprint(" Byte\n  Intent: ");
    for (int i = 0; i < 2; i++) { kprint_hex(h->intent[i]); kprint(" "); }
    kprint("...\n");
    kfree(buf);
    return 0;
}

int synbin_run(const char* name) {
    int len; const struct synbin_header* h;
    uint8_t* buf = lade(name, &len, &h);
    if (!buf) return -1;

    if (h->target != SYNBIN_TARGET_CPU) {
        kprint("[SYNBIN] Ziel nicht unterstuetzt (nur CPU x86-64)\n");
        kfree(buf); return -2;
    }
    if (h->caps & ~allowed_caps) {
        kprint("[SYNBIN] Berechtigung fehlt: Programm braucht 0x"); kprint_hex(h->caps);
        kprint(", erlaubt ist 0x"); kprint_hex(allowed_caps);
        kprint(". Mit 'syngrant 3' freigeben.\n");
        kfree(buf); return -3;
    }

    /* Code an eine 16-Byte-ausgerichtete Stelle kopieren und dort starten */
    uint8_t* mem = (uint8_t*)kmalloc((size_t)h->payload_size + 32);
    if (!mem) { kprint("[SYNBIN] Kein Speicher\n"); kfree(buf); return -4; }
    uint8_t* code = (uint8_t*)(((uintptr_t)mem + 15) & ~(uintptr_t)15);
    const uint8_t* src = buf + HDR_SIZE;
    for (uint64_t i = 0; i < h->payload_size; i++) code[i] = src[i];

    uint64_t (*fn)(const struct syn_api*) = (uint64_t (*)(const struct syn_api*))(uintptr_t)code;
    uint64_t rc = fn(&api);

    kprint("[SYNBIN] beendet, Rueckgabe ");
    api_print_num(rc);
    kprint("\n");
    kfree(mem);
    kfree(buf);
    return 0;
}
