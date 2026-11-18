/*
 * Zynex OS - GPT (GUID-Partitionstabelle): lesen, Partitionen anmelden, schreiben
 */
#include "gpt.h"
#include "block.h"
#include "crypto.h"

extern void kprint(const char* str);
extern void kprint_dec(uint32_t val);

uint32_t crc32(const void* d, uint32_t n) {
    const uint8_t* p = d; uint32_t c = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < n; i++) {
        c ^= p[i];
        for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1)));
    }
    return ~c;
}

static const uint8_t GUID_ZYNEX[16] = { 0x58,0x4E,0x59,0x5A, 0x53,0x4F, 0x41,0x44, 0x41,0x54, 0x00,0x00,0x00,0x00,0x00,0x01 };
static const uint8_t GUID_ESP[16] = { 0x28,0x73,0x2A,0xC1, 0x1F,0xF8, 0xD2,0x11, 0xBA,0x4B, 0x00,0xA0,0xC9,0x3E,0xC9,0x3B };

static uint32_t rd32(const uint8_t* p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static void wr32(uint8_t* p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }
static void wr64(uint8_t* p, uint64_t v) { wr32(p, (uint32_t)v); wr32(p + 4, (uint32_t)(v >> 32)); }
static int geq(const uint8_t* a, const uint8_t* b) { for (int i = 0; i < 16; i++) if (a[i] != b[i]) return 0; return 1; }
static void mz(void* d, uint32_t n) { uint8_t* p = d; for (uint32_t i = 0; i < n; i++) p[i] = 0; }
static void mc(void* d, const void* s, uint32_t n) { uint8_t* a = d; const uint8_t* b = s; for (uint32_t i = 0; i < n; i++) a[i] = b[i]; }

static const uint8_t GUID_MODELL[16] = { 0x58,0x4E,0x59,0x5A, 0x53,0x4F, 0x4D,0x4F, 0x44,0x45, 0x00,0x00,0x00,0x00,0x00,0x02 };
static char zynex_part[20], modell_part[20];
const char* gpt_modell_partition(void) { return modell_part[0] ? modell_part : 0; }
uint32_t gpt_modell_sektoren(uint64_t bytes) { uint64_t s = GPT_MODELL_DATEN + (bytes + 511) / 512; return (uint32_t)((s + 2047) & ~2047ull); }
uint32_t gpt_modell_start(const char* disk, uint32_t sects) {
    int total = block_sectors(disk); if (total < 0) return 0;
    uint32_t last_usable = (uint32_t)total - 34;
    if (sects + GPT_P2_START + 65536 > last_usable) return 0;
    return ((last_usable + 1 - sects) & ~2047u);
}

const char* gpt_zynex_partition(void) { return zynex_part[0] ? zynex_part : 0; }

static uint8_t sec[512], ent[128 * 128];

int gpt_scan(const char* disk) {
    block_forget_parts(disk);
    { int same = 1; for (int i = 0; disk[i]; i++) if (zynex_part[i] != disk[i]) { same = 0; break; } if (same) zynex_part[0] = 0; { int sm = 1; for (int i = 0; disk[i]; i++) if (modell_part[i] != disk[i]) { sm = 0; break; } if (sm) modell_part[0] = 0; } }
    if (block_dev_read(disk, 1, sec, 1) < 0) return 0;
    if (sec[0] != 'E' || sec[1] != 'F' || sec[2] != 'I' || sec[3] != ' ' || sec[4] != 'P' || sec[5] != 'A' || sec[6] != 'R' || sec[7] != 'T') return 0;
    uint32_t hs = rd32(sec + 12), hcrc = rd32(sec + 16);
    if (hs < 92 || hs > 512) return 0;
    wr32(sec + 16, 0);
    if (crc32(sec, hs) != hcrc) return 0;
    uint32_t elba = rd32(sec + 72), n = rd32(sec + 80), es = rd32(sec + 84), ecrc = rd32(sec + 88);
    if (es != 128 || n == 0 || n > 128) return 0;
    uint32_t secs = (n * es + 511) / 512;
    if (block_dev_read(disk, elba, ent, secs) < 0) return 0;
    if (crc32(ent, n * es) != ecrc) return 0;
    int gefunden = 0;
    for (uint32_t i = 0; i < n; i++) {
        const uint8_t* e = ent + i * es;
        uint8_t leer = 0; for (int k = 0; k < 16; k++) leer |= e[k];
        if (!leer) continue;
        uint64_t first = (uint64_t)rd32(e + 32) | (uint64_t)rd32(e + 36) << 32, last = (uint64_t)rd32(e + 40) | (uint64_t)rd32(e + 44) << 32;
        if (last < first || last > 0xFFFFFFFFull) continue;
        char name[20]; int k = 0;
        while (disk[k] && k < 14) { name[k] = disk[k]; k++; }
        name[k++] = 'p';
        if (i + 1 >= 10) name[k++] = (char)('0' + (i + 1) / 10);
        name[k++] = (char)('0' + (i + 1) % 10); name[k] = 0;
        if (block_register_part(name, disk, (uint32_t)first, (uint32_t)(last - first + 1)) == 0) {
            gefunden++;
            if (geq(e, GUID_ZYNEX) && !zynex_part[0]) mc(zynex_part, name, (uint32_t)k + 1);
            if (geq(e, GUID_MODELL) && !modell_part[0]) mc(modell_part, name, (uint32_t)k + 1);
        }
    }
    return gefunden;
}

int gpt_write(const char* disk, uint32_t data_sects, uint32_t model_sects, uint8_t mbr[512]) {
    int total = block_sectors(disk);
    if (total < 0) return -1;
    uint32_t last = (uint32_t)total - 1, first_usable = 34, last_usable = last - 33, echt_last_usable = last - 33;
    uint32_t p2_end;
    uint32_t p3_start = 0;
    if (model_sects) { p3_start = gpt_modell_start(disk, model_sects); if (!p3_start) return -2; last_usable = p3_start - 1; }
    if (data_sects) p2_end = GPT_P2_START + data_sects - 1; else p2_end = last_usable & ~2047u;     /* auf MiB abrunden */
    if (p2_end > last_usable) p2_end = last_usable;
    if (p2_end < GPT_P2_START + 65536) return -2;                                                       /* mindestens 32 MiB Daten */

    mz(ent, sizeof(ent));
    uint8_t diskguid[16]; krand(diskguid, 16);
    struct { const uint8_t* typ; uint32_t a, b; const char* name; } p[3] = {
        { GUID_ESP, GPT_P1_START, GPT_P1_START + GPT_P1_SECTS - 1, "ESP" },
        { GUID_ZYNEX, GPT_P2_START, p2_end, "Zynex" },
        { GUID_MODELL, p3_start, p3_start + model_sects - 1, "Zynex Modell" } };
    for (int i = 0; i < (model_sects ? 3 : 2); i++) {
        uint8_t* e = ent + i * 128;
        mc(e, p[i].typ, 16); krand(e + 16, 16);
        wr64(e + 32, p[i].a); wr64(e + 40, p[i].b);
        for (int k = 0; p[i].name[k] && k < 35; k++) e[56 + 2 * k] = (uint8_t)p[i].name[k];
    }
    uint32_t ecrc = crc32(ent, sizeof(ent));

    /* Schutz-MBR: ein Eintrag Typ 0xEE ueber die ganze Platte */
    uint8_t* t = mbr + 446; mz(t, 64);
    t[0] = 0; t[1] = 0; t[2] = 2; t[3] = 0; t[4] = 0xEE; t[5] = 0xFF; t[6] = 0xFF; t[7] = 0xFF;
    wr32(t + 8, 1); wr32(t + 12, last);
    mbr[510] = 0x55; mbr[511] = 0xAA;

    uint8_t h[512];
    for (int pass = 0; pass < 2; pass++) {                 /* 0 = Haupt-Tabelle, 1 = Sicherung am Plattenende */
        mz(h, 512);
        mc(h, "EFI PART", 8); wr32(h + 8, 0x00010000u); wr32(h + 12, 92);
        wr64(h + 24, pass ? last : 1); wr64(h + 32, pass ? 1 : last);
        wr64(h + 40, first_usable); wr64(h + 48, echt_last_usable);
        mc(h + 56, diskguid, 16);
        wr64(h + 72, pass ? last - 32 : 2); wr32(h + 80, 128); wr32(h + 84, 128); wr32(h + 88, ecrc);
        wr32(h + 16, crc32(h, 92));
        uint32_t elba = pass ? last - 32 : 2;
        if (block_dev_write(disk, elba, ent, 32) < 0) return -3;
        if (block_dev_write(disk, pass ? last : 1, h, 1) < 0) return -3;
    }
    if (block_dev_write(disk, 0, mbr, 1) < 0) return -3;
    return 0;
}
