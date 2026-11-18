/* Zynies Gehirn, siehe include/gehirn.h. Wird mit SSE uebersetzt (Makefile); Interrupts fassen keine XMM-Register an. */
#include "gehirn.h"
#include "llm.h"
#include "multiboot.h"
#include "pmm.h"
#include "gpt.h"
#include "block.h"

extern void kprint(const char* str);
extern void kprint_dec(uint32_t val);

#define ARENA_BYTES (320u * 1024u * 1024u)
#define KONTEXT 1024

static struct llm m;
static int bereit, fehler;
static uint8_t* arena; static uint64_t arena_pos;
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

int gehirn_start(void) {
    if (bereit) return 0;
    const uint8_t* daten = (const uint8_t*)(uintptr_t)g_boot.model_addr; uint64_t groesse = g_boot.model_size;
    if (!groesse) {                                             /* kein Stick-Modell: aus der Modell-Partition der Platte laden */
        const char* part = gpt_modell_partition();
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
    if (!bereit && gehirn_start() != 0) return -1;
    int pos = m.pos;
    if (pos > KONTEXT - 400) { pos = prefix_pos; }                              /* Verlauf voll: von vorn */
    int ids[400], n = 0, fl = slen(frage); if (fl > 300) fl = 300;
    ids[n++] = m.im_start; n += llm_encode(&m, "user\n", 5, ids + n, 8); n += llm_encode(&m, frage, fl, ids + n, 330); ids[n++] = m.im_end;
    n += llm_encode(&m, "\n", 1, ids + n, 4); ids[n++] = m.im_start; n += llm_encode(&m, "assistant\n", 10, ids + n, 8);
    float* lg = 0;
    for (int i = 0; i < n; i++) { lg = llm_forward(&m, ids[i], pos++); if (!lg) return -2; }
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
    antwort[o] = 0; m.pos = pos;
    return o;
}
