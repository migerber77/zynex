/*
 * Zynex OS - AHCI-Treiber (SATA-Festplatten und SSDs ueber den Standard-Controller)
 * Polling ohne Interrupts, Befehlsplatz 0, Datenuebertragung ueber einen 4-KiB-Zwischenpuffer
 * (bis 8 Sektoren je Befehl). ATAPI (CD/DVD) wird uebersprungen. Bis zu 2 Platten.
 */
#include "ahci.h"
#include "pci.h"
#include "pmm.h"
#include "paging.h"
#include "block.h"
#include "timer.h"

extern void kprint(const char* str);
extern void kprint_dec(uint32_t val);
extern void kprint_hex(uint32_t val);

#define MAX_DISKS 2

struct disk {
    volatile uint8_t* port;
    uint8_t* clb;          /* Befehlsliste (1 KiB) + Empfangs-FIS (256 B) */
    uint8_t* ctab;         /* Befehlstabelle: CFIS, PRDT */
    uint8_t* buf;          /* Zwischenpuffer */
    uint32_t sectors;
    int      ok;
};

static struct disk disks[MAX_DISKS];
static int ndisks = 0;

static inline uint32_t rd(volatile uint8_t* b, uint32_t o) { return *(volatile uint32_t*)(b + o); }
static inline void wr(volatile uint8_t* b, uint32_t o, uint32_t v) { *(volatile uint32_t*)(b + o) = v; }
static inline void fence(void) { __asm__ volatile ("mfence" : : : "memory"); }
static void zero(void* p, uint32_t n) { uint8_t* b = (uint8_t*)p; for (uint32_t i = 0; i < n; i++) b[i] = 0; }
static uint8_t* seite(void) { uint8_t* p = (uint8_t*)pmm_alloc_page(); if (p) zero(p, 4096); return p; }

#define P_CLB 0x00
#define P_CLBU 0x04
#define P_FB 0x08
#define P_FBU 0x0C
#define P_IS 0x10
#define P_CMD 0x18
#define P_TFD 0x20
#define P_SIG 0x24
#define P_SSTS 0x28
#define P_SERR 0x30
#define P_SACT 0x34
#define P_CI 0x38

static int warte_bis_aus(volatile uint8_t* p, uint32_t reg, uint32_t maske) {
    uint32_t t0 = timer_get_ticks();
    while (rd(p, reg) & maske) if (timer_get_ticks() - t0 > 100) return -1;     /* 1 s */
    return 0;
}

static void port_stop(volatile uint8_t* p) {
    wr(p, P_CMD, rd(p, P_CMD) & ~1u);                    /* ST aus */
    warte_bis_aus(p, P_CMD, 1u << 15);                   /* CR */
    wr(p, P_CMD, rd(p, P_CMD) & ~(1u << 4));             /* FRE aus */
    warte_bis_aus(p, P_CMD, 1u << 14);                   /* FR */
}

static int befehl(struct disk* d, uint8_t cmd, uint64_t lba, uint32_t count, int schreiben) {
    volatile uint8_t* p = d->port;
    uint8_t* t = d->ctab;
    zero(t, 0x100);
    t[0] = 0x27; t[1] = 0x80; t[2] = cmd; t[7] = 0x40;
    t[4] = lba & 0xFF; t[5] = (lba >> 8) & 0xFF; t[6] = (lba >> 16) & 0xFF;
    t[8] = (lba >> 24) & 0xFF; t[9] = (lba >> 32) & 0xFF; t[10] = (lba >> 40) & 0xFF;
    t[12] = count & 0xFF; t[13] = (count >> 8) & 0xFF;
    *(uint64_t*)(t + 0x80) = (uint64_t)(uintptr_t)d->buf;                     /* PRDT 0 */
    *(uint32_t*)(t + 0x8C) = (count * 512u - 1u) | (1u << 31);                /* Laenge-1, IOC */
    uint32_t* h = (uint32_t*)d->clb;
    h[0] = 5u | (schreiben ? (1u << 6) : 0) | (1u << 16);                     /* CFL=5 DW, PRDTL=1 */
    h[1] = 0;
    h[2] = (uint32_t)(uintptr_t)t; h[3] = (uint32_t)((uint64_t)(uintptr_t)t >> 32);

    if (warte_bis_aus(p, P_TFD, 0x88) != 0) return -1;                         /* BSY | DRQ */
    wr(p, P_IS, 0xFFFFFFFFu);
    fence();
    wr(p, P_CI, 1);
    uint32_t t0 = timer_get_ticks();
    while (rd(p, P_CI) & 1) {
        if (rd(p, P_IS) & (1u << 30)) return -2;                               /* Task File Error */
        if (timer_get_ticks() - t0 > 500) return -3;
    }
    if (rd(p, P_IS) & (1u << 30)) return -2;
    if (rd(p, P_TFD) & 0x01) return -2;                                        /* ERR */
    return 0;
}

static int disk_rw(int idx, uint32_t lba, void* buf, uint32_t count, int schreiben) {
    struct disk* d = &disks[idx];
    if (!d->ok || lba + count > d->sectors || lba + count < lba) return -1;
    uint8_t* b = (uint8_t*)buf;
    while (count) {
        uint32_t n = count > 8 ? 8 : count;
        if (schreiben) for (uint32_t i = 0; i < n * 512; i++) d->buf[i] = b[i];
        if (befehl(d, schreiben ? 0x35 : 0x25, lba, n, schreiben) != 0) return -1;   /* WRITE/READ DMA EXT */
        if (!schreiben) for (uint32_t i = 0; i < n * 512; i++) b[i] = d->buf[i];
        lba += n; b += n * 512; count -= n;
    }
    return 0;
}

static int rd0(uint32_t l, void* b, uint32_t c) { return disk_rw(0, l, b, c, 0); }
static int wr0(uint32_t l, const void* b, uint32_t c) { return disk_rw(0, l, (void*)b, c, 1); }
static int rd1(uint32_t l, void* b, uint32_t c) { return disk_rw(1, l, b, c, 0); }
static int wr1(uint32_t l, const void* b, uint32_t c) { return disk_rw(1, l, (void*)b, c, 1); }

static int port_start(volatile uint8_t* p, struct disk* d) {
    port_stop(p);
    uint8_t* mem = seite();
    d->ctab = seite();
    d->buf = seite();
    if (!mem || !d->ctab || !d->buf) return -1;
    d->clb = mem;                                         /* 0x000 Befehlsliste, 0x400 Empfangs-FIS */
    wr(p, P_CLB, (uint32_t)(uintptr_t)mem); wr(p, P_CLBU, (uint32_t)((uint64_t)(uintptr_t)mem >> 32));
    wr(p, P_FB, (uint32_t)(uintptr_t)(mem + 0x400)); wr(p, P_FBU, (uint32_t)((uint64_t)(uintptr_t)(mem + 0x400) >> 32));
    wr(p, P_SERR, 0xFFFFFFFFu);
    wr(p, P_IS, 0xFFFFFFFFu);
    wr(p, P_CMD, rd(p, P_CMD) | (1u << 4));               /* FRE */
    if (warte_bis_aus(p, P_TFD, 0x88) != 0) return -2;
    wr(p, P_CMD, rd(p, P_CMD) | 1u);                      /* ST */
    d->port = p;
    return 0;
}

int ahci_init(void) {
    int gefunden = 0;
    for (int n = 0; ndisks < MAX_DISKS; n++) {
        struct pci_dev* c = pci_find_class(0x01, 0x06, 0x01, n);
        if (!c) break;
        uint64_t bar = pci_bar(c, 5);
        if (!bar) continue;
        gefunden++;
        pci_enable(c);
        if (paging_map_mmio(bar, 0x2000) != 0) { kprint("[AHCI] MMIO-Mapping fehlgeschlagen\n"); continue; }
        volatile uint8_t* hba = (volatile uint8_t*)(uintptr_t)bar;
        wr(hba, 0x04, rd(hba, 0x04) | (1u << 31));        /* GHC.AE */
        uint32_t pi = rd(hba, 0x0C);
        for (int i = 0; i < 32 && ndisks < MAX_DISKS; i++) {
            if (!(pi & (1u << i))) continue;
            volatile uint8_t* p = hba + 0x100 + 0x80 * i;
            uint32_t ssts = rd(p, P_SSTS);
            if ((ssts & 0xF) != 3 || ((ssts >> 8) & 0xF) != 1) continue;     /* kein Geraet / keine Verbindung */
            if (rd(p, P_SIG) != 0x00000101) continue;                         /* nur ATA, kein ATAPI */
            struct disk* d = &disks[ndisks];
            if (port_start(p, d) != 0) { kprint("[AHCI] Port-Start fehlgeschlagen\n"); continue; }
            if (befehl(d, 0xEC, 0, 1, 0) != 0) { kprint("[AHCI] Identify fehlgeschlagen\n"); continue; }
            uint16_t* w = (uint16_t*)d->buf;
            if (!(w[83] & (1u << 10))) { kprint("[AHCI] Kein LBA48, nicht unterstuetzt\n"); continue; }
            if ((w[106] & 0xC000) == 0x4000 && (w[106] & (1u << 12))) {
                uint32_t ls = (uint32_t)w[117] | ((uint32_t)w[118] << 16);
                if (ls != 256) { kprint("[AHCI] Sektorgroesse != 512 Byte, nicht unterstuetzt\n"); continue; }
            }
            uint64_t sz = (uint64_t)w[100] | ((uint64_t)w[101] << 16) | ((uint64_t)w[102] << 32) | ((uint64_t)w[103] << 48);
            d->sectors = sz > 0xFFFFFFFFULL ? 0xFFFFFFFFu : (uint32_t)sz;
            d->ok = 1;
            static const char* namen[MAX_DISKS] = { "sata0", "sata1" };
            struct block_dev bd = { namen[ndisks], d->sectors, ndisks == 0 ? rd0 : rd1, ndisks == 0 ? wr0 : wr1 };
            block_register(&bd);
            kprint("[AHCI] "); kprint(namen[ndisks]); kprint(" bereit: ");
            kprint_dec(d->sectors / 2048); kprint(" MiB (Port "); kprint_dec(i); kprint(")\n");
            ndisks++;
        }
    }
    if (!ndisks) kprint(gefunden ? "[AHCI] Keine SATA-Platte angeschlossen\n" : "[AHCI] Kein Controller gefunden\n");
    return ndisks ? 0 : -1;
}
