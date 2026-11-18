/* Zynies Gehirn, siehe include/gehirn.h. Wird mit SSE uebersetzt (Makefile); Interrupts fassen keine XMM-Register an. */
#include "gehirn.h"
#include "llm.h"
#include "multiboot.h"
#include "pmm.h"
#include "gpt.h"
#include "block.h"
#include "zyndb.h"

extern void kprint(const char* str);
extern void kprint_dec(uint32_t val);

#define ARENA_BYTES (320u * 1024u * 1024u)
#define KONTEXT 1024

static struct llm m;
static int bereit, fehler;
static uint8_t* arena; static uint64_t arena_pos;
static char kontext[620];
void gehirn_kontext(const char* t) { int i = 0; while (t && t[i] && i < 600) { kontext[i] = t[i]; i++; } kontext[i] = 0; }
static int prefix_pos;                                  /* Anzahl Token der festen Einleitung (System-Text) */
static const char* SYS = "system\nDu bist Zynie, der freundliche Assistent von Zynex OS. Antworte kurz.";

static void* arena_al(size_t n) {
    arena_pos = (arena_pos + 63) & ~63ull;
    if (arena_pos + n > ARENA_BYTES) return 0;
    void* p = arena + arena_pos; arena_pos += n; return p;
}
static int slen(const char* s) { int n = 0; while (s[n]) n++; return n; }

static const char* meldung = "nicht gestartet";
const char* gehirn_status(void) { return bereit ? "bereit" : meldung; }
int gehirn_bereit(void) { return bereit; }
int gehirn_vorhanden(void) { return bereit || g_boot.model_size || gpt_modell_partition(); }

static int prefill(int id, int pos) { return llm_forward(&m, id, pos) ? pos + 1 : -1; }

void gehirn_vergessen(void) {
    if (!bereit) return;
    m.pos = prefix_pos;
}

/* ---- Modellverwaltung: Modelle liegen auf Partitionen mit Kopf "ZXMODEL1"; gewaehlt wird in ZynDB (ki.modell) ---- */
static int modell_kopf(const char* dev, uint64_t* groesse) {
    static uint8_t kopf[512];
    if (block_dev_read(dev, 0, kopf, 1) < 0 || kopf[0] != 'Z' || kopf[1] != 'X' || kopf[2] != 'M' || kopf[3] != 'O' || kopf[4] != 'D') return 0;
    uint64_t g = 0; for (int i = 7; i >= 0; i--) g = g << 8 | kopf[8 + i];
    if (groesse) *groesse = g; return 1;
}
static const char* modell_wahl(void) {
    const char* w = zdb_get("ki.modell"); static char buf[24];
    if (w && w[0]) { int i = 0; while (w[i] && i < 23) { buf[i] = w[i]; i++; } buf[i] = 0; if (modell_kopf(buf, 0)) return buf; }
    return gpt_modell_partition();
}
int gehirn_modelle_zeigen(void) {
    int n = 0; const char* aktiv = modell_wahl();
    for (int i = 0, c = block_count(); i < c; i++) {
        const char* nm = block_name(i); uint64_t g;
        if (!nm || !modell_kopf(nm, &g)) continue;
        int gl = aktiv && slen(aktiv) == slen(nm); if (gl) for (int k = 0; nm[k]; k++) if (nm[k] != aktiv[k]) gl = 0;
        kprint("  "); kprint(nm); kprint("  "); kprint_dec((uint32_t)(g >> 20)); kprint(" MiB"); kprint(gl ? "  (wird beim Start geladen)" : ""); kprint("\n"); n++;
    }
    if (!n) kprint("  keine Modelle gefunden\n");
    return n;
}
static int modelle_zaehlen(void) { int n = 0; for (int i = 0, c = block_count(); i < c; i++) { const char* nm = block_name(i); if (nm && modell_kopf(nm, 0)) n++; } return n; }
int gehirn_modell_waehlen(const char* dev) { if (!modell_kopf(dev, 0)) return -1; return zdb_put("ki.modell", dev) == 0 ? 0 : -2; }
int gehirn_modell_loeschen(const char* dev) {
    if (!modell_kopf(dev, 0)) return -1;
    if (modelle_zaehlen() < 2) return -3;                               /* das letzte Modell bleibt: Zynie muss immer da sein */
    static uint8_t leer[512];
    if (block_dev_write(dev, 0, leer, 1) < 0) return -2;
    const char* w = zdb_get("ki.modell"); if (w && slen(w) == slen(dev)) { int gl = 1; for (int k = 0; dev[k]; k++) if (w[k] != dev[k]) gl = 0; if (gl) zdb_del("ki.modell"); }
    return 0;
}

int gehirn_start(void) {
    if (bereit) return 0;
    const uint8_t* daten = (const uint8_t*)(uintptr_t)g_boot.model_addr; uint64_t groesse = g_boot.model_size;
    if (!groesse) {                                             /* kein Stick-Modell: aus der Modell-Partition der Platte laden */
        const char* part = modell_wahl();
        static uint8_t kopf[512];
        if (!part || block_dev_read(part, 0, kopf, 1) < 0 || kopf[0] != 'Z' || kopf[1] != 'X' || kopf[2] != 'M' || kopf[3] != 'O' || kopf[4] != 'D') { fehler = 1; meldung = "kein Sprachmodell gefunden"; return 1; }
        for (int i = 7; i >= 0; i--) groesse = groesse << 8 | kopf[8 + i];
        uint8_t* ram = pmm_alloc_pages((size_t)((groesse + 4095) / 4096));
        if (!ram) { fehler = 107; meldung = "kein Arbeitsspeicher fuer das Modell"; return 107; }
        kprint("[KI] Lade Sprachmodell von "); kprint(part); kprint(" ...\n");
        uint64_t off = 0; uint32_t lba = GPT_MODELL_DATEN;
        while (off < groesse) {
            uint32_t n = (groesse - off) > 262144 ? 262144 : (uint32_t)(groesse - off); uint32_t sc = (n + 511) / 512;
            if (block_dev_read(part, lba, ram + off, sc) < 0) { fehler = 109; meldung = "Lesefehler beim Sprachmodell"; return 109; }
            off += n; lba += sc;
        }
        daten = ram;
    }
    uint32_t pages = ARENA_BYTES / 4096;
    arena = pmm_alloc_pages(pages);
    if (!arena) { fehler = 107; meldung = "kein Arbeitsspeicher"; return 107; }
    for (uint64_t i = 0; i < ARENA_BYTES; i++) arena[i] = 0;
    arena_pos = 0;
    int r = llm_load(&m, daten, groesse, arena_al, KONTEXT);
    if (r) { fehler = 100 - r; meldung = llm_fehler(r); kprint("[KI] Modell konnte nicht geladen werden: "); kprint(llm_fehler(r)); kprint("\n"); return fehler; }
    int ids[128], n = 0, pos = 0;
    ids[n++] = m.im_start; n += llm_encode(&m, SYS, slen(SYS), ids + n, 100); ids[n++] = m.im_end; n += llm_encode(&m, "\n", 1, ids + n, 4);
    for (int i = 0; i < n; i++) { pos = prefill(ids[i], pos); if (pos < 0) { fehler = 108; meldung = "Fehler beim Vorbereiten"; return 108; } }
    prefix_pos = pos; m.pos = pos; bereit = 1;
    kprint("[KI] Sprachmodell bereit: "); kprint_dec((uint32_t)m.n_layer); kprint(" Schichten, Breite "); kprint_dec((uint32_t)m.dim); kprint("\n");
    return 0;
}

int gehirn_frage(const char* frage, char* antwort, int max, void (*cb)(const char*, int)) {
    if (!bereit && gehirn_start() != 0) { kontext[0] = 0; return -1; }
    int pos = m.pos, kl = slen(kontext);
    if (kl || pos > KONTEXT - 400) { pos = prefix_pos; }                         /* mit Gedaechtnis-Kontext oder voll: ab dem festen Anfang (das Gedaechtnis ersetzt den Verlauf) */
    int ids[620], n = 0, fl = slen(frage); if (fl > 300) fl = 300;
    ids[n++] = m.im_start; n += llm_encode(&m, "user\n", 5, ids + n, 8);
    if (kl) { n += llm_encode(&m, kontext, kl, ids + n, 230); n += llm_encode(&m, "Frage: ", 7, ids + n, 8); }
    n += llm_encode(&m, frage, fl, ids + n, 330); ids[n++] = m.im_end;
    n += llm_encode(&m, "\n", 1, ids + n, 4); ids[n++] = m.im_start; n += llm_encode(&m, "assistant\n", 10, ids + n, 8);
    float* lg = 0;
    for (int i = 0; i < n; i++) { lg = llm_forward(&m, ids[i], pos++); if (!lg) { kontext[0] = 0; return -2; } }
    int o = 0, gen = 0;
    while (gen < 200 && pos < KONTEXT - 2) {
        int t = llm_sample(&m, lg, 0, 0);
        if (t == m.im_end || t == m.eos) break;
        char buf[64]; int k = llm_decode(&m, t, buf, 60);
        for (int i = 0; i < k && o < max - 1; i++) antwort[o++] = buf[i];
        if (cb && k) cb(buf, k);
        lg = llm_forward(&m, t, pos++); gen++;
        if (!lg) break;
    }
    /* Zug im Verlauf abschliessen: <|im_end|> und Zeilenumbruch */
    { int nl[4]; int k = llm_encode(&m, "\n", 1, nl, 4); llm_forward(&m, m.im_end, pos++); for (int i = 0; i < k && pos < KONTEXT - 1; i++) llm_forward(&m, nl[i], pos++); }
    antwort[o] = 0; m.pos = kl ? prefix_pos : pos; kontext[0] = 0;
    return o;
}
