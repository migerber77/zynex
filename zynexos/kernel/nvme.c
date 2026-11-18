/*
 * Zynex OS - NVMe-Treiber (NVM Express 1.x, PCIe)
 * Ein Controller, Namespace 1, 512-Byte-Sektoren, Polling ohne Interrupts.
 * Datenuebertragung ueber einen 4-KiB-Zwischenpuffer (eine PRP-Seite, bis 8 Sektoren je Befehl).
 */
#include "nvme.h"
#include "pci.h"
#include "pmm.h"
#include "paging.h"
#include "block.h"
#include "timer.h"

extern void kprint(const char* str);
extern void kprint_dec(uint32_t val);
extern void kprint_hex(uint32_t val);

#define QD 16                           /* Eintraege je Warteschlange */

struct nvme_cmd {
    uint8_t  opc, fuse;
    uint16_t cid;
    uint32_t nsid;
    uint64_t rsvd, mptr, prp1, prp2;
    uint32_t cdw10, cdw11, cdw12, cdw13, cdw14, cdw15;
} __attribute__((packed));

struct nvme_cqe {
    uint32_t dw0, dw1;
    uint16_t sqhd, sqid, cid, status;
} __attribute__((packed));

struct queue {
    struct nvme_cmd* sq;
    volatile struct nvme_cqe* cq;
    uint16_t tail, head, phase, id;
};

static volatile uint8_t* regs;
static uint32_t stride;
static struct queue adm, io;
static uint16_t next_cid = 1;
static uint8_t* bounce;
static uint32_t nsectors;
static int ready = 0;

static inline uint32_t rd32(uint32_t o) { return *(volatile uint32_t*)(regs + o); }
static inline void wr32(uint32_t o, uint32_t v) { *(volatile uint32_t*)(regs + o) = v; }
static inline uint64_t rd64(uint32_t o) { return *(volatile uint64_t*)(regs + o); }
static inline void wr64(uint32_t o, uint64_t v) { *(volatile uint64_t*)(regs + o) = v; }
static inline void fence(void) { __asm__ volatile ("mfence" : : : "memory"); }

static void zero(void* p, uint32_t n) { uint8_t* b = (uint8_t*)p; for (uint32_t i = 0; i < n; i++) b[i] = 0; }

static int warte_rdy(int wert) {
    uint32_t t0 = timer_get_ticks();
    while (((rd32(0x1C) & 1) != (uint32_t)wert)) {
        if (timer_get_ticks() - t0 > 500) return -1;       /* 5 s */
    }
    return 0;
}

/* Befehl absenden und auf Abschluss warten. Rueckgabe: 0 = ok, sonst Statuscode (oder -1 Zeitueberschreitung). */
static int submit(struct queue* q, struct nvme_cmd* c) {
    c->cid = next_cid++;
    q->sq[q->tail] = *c;
    q->tail = (uint16_t)((q->tail + 1) % QD);
    fence();
    wr32(0x1000 + (2 * q->id) * stride, q->tail);
    uint32_t t0 = timer_get_ticks();
    for (;;) {
        fence();
        volatile struct nvme_cqe* e = &q->cq[q->head];
        if ((e->status & 1) == q->phase) {
            uint16_t st = (uint16_t)(e->status >> 1);
            q->head = (uint16_t)((q->head + 1) % QD);
            if (q->head == 0) q->phase ^= 1;
            wr32(0x1000 + (2 * q->id + 1) * stride, q->head);
            return st & 0x7FF;
        }
        if (timer_get_ticks() - t0 > 500) return -1;
    }
}

static int io_rw(int schreiben, uint32_t lba, void* buf, uint32_t count) {
    uint8_t* p = (uint8_t*)buf;
    while (count) {
        uint32_t n = count > 8 ? 8 : count;
        if (lba + n > nsectors) return -2;
        if (schreiben) for (uint32_t i = 0; i < n * 512; i++) bounce[i] = p[i];
        struct nvme_cmd c; zero(&c, sizeof(c));
        c.opc = schreiben ? 0x01 : 0x02;
        c.nsid = 1;
        c.prp1 = (uint64_t)(uintptr_t)bounce;
        c.cdw10 = lba;
        c.cdw11 = 0;
        c.cdw12 = n - 1;
        if (submit(&io, &c) != 0) return -3;
        if (!schreiben) for (uint32_t i = 0; i < n * 512; i++) p[i] = bounce[i];
        p += n * 512; lba += n; count -= n;
    }
    return 0;
}

static int nvme_read(uint32_t lba, void* buf, uint32_t count) {
    if (!ready) return -1;
    return io_rw(0, lba, buf, count) == 0 ? (int)count : -1;
}
static int nvme_write(uint32_t lba, const void* buf, uint32_t count) {
    if (!ready) return -1;
    return io_rw(1, lba, (void*)buf, count) == 0 ? (int)count : -1;
}

static void* seite(void) {
    void* p = pmm_alloc_page();
    if (p) zero(p, 4096);
    return p;
}

int nvme_init(void) {
    struct pci_dev* d = pci_find_class(0x01, 0x08, 0x02, 0);
    if (!d) { kprint("[NVMe] Kein Controller gefunden\n"); return -1; }
    kprint("[NVMe] Controller bei PCI "); kprint_dec(d->bus); kprint(":"); kprint_dec(d->dev); kprint("\n");

    uint64_t bar = pci_bar(d, 0);
    kprint("[NVMe] BAR0=0x"); kprint_hex((uint32_t)(bar >> 32)); kprint_hex((uint32_t)bar); kprint("\n");
    if (!bar) { kprint("[NVMe] BAR0 fehlt\n"); return -2; }
    pci_enable(d);
    if (paging_map_mmio(bar, 0x4000) != 0) { kprint("[NVMe] MMIO-Mapping fehlgeschlagen\n"); return -3; }
    regs = (volatile uint8_t*)(uintptr_t)bar;

    uint64_t cap = rd64(0x00);
    stride = 4u << ((cap >> 32) & 0xF);
    if (((cap >> 48) & 0xF) != 0) { kprint("[NVMe] Seitengroesse 4 KiB nicht unterstuetzt\n"); return -4; }

    /* Controller anhalten, Verwaltungs-Warteschlangen einrichten, starten */
    wr32(0x14, 0);
    if (warte_rdy(0) != 0) { kprint("[NVMe] Controller stoppt nicht\n"); return -5; }

    adm.sq = (struct nvme_cmd*)seite();
    adm.cq = (volatile struct nvme_cqe*)seite();
    io.sq  = (struct nvme_cmd*)seite();
    io.cq  = (volatile struct nvme_cqe*)seite();
    bounce = (uint8_t*)seite();
    uint8_t* ident = (uint8_t*)seite();
    if (!adm.sq || !adm.cq || !io.sq || !io.cq || !bounce || !ident) { kprint("[NVMe] Kein Speicher\n"); return -6; }
    adm.id = 0; adm.phase = 1; adm.tail = adm.head = 0;
    io.id = 1;  io.phase = 1;  io.tail = io.head = 0;

    wr32(0x24, ((QD - 1) << 16) | (QD - 1));                  /* AQA */
    wr64(0x28, (uint64_t)(uintptr_t)adm.sq);                  /* ASQ */
    wr64(0x30, (uint64_t)(uintptr_t)adm.cq);                  /* ACQ */
    wr32(0x14, (6u << 16) | (4u << 20) | 1u);                 /* IOSQES=64 B, IOCQES=16 B, EN */
    if (warte_rdy(1) != 0) { kprint("[NVMe] Controller wird nicht bereit\n"); return -7; }

    /* Identify Namespace 1 */
    struct nvme_cmd c; zero(&c, sizeof(c));
    c.opc = 0x06; c.nsid = 1; c.prp1 = (uint64_t)(uintptr_t)ident; c.cdw10 = 0;
    if (submit(&adm, &c) != 0) { kprint("[NVMe] Identify fehlgeschlagen\n"); return -8; }
    uint64_t nsze = *(uint64_t*)ident;
    uint8_t flbas = ident[26] & 0xF;
    uint32_t lbaf = *(uint32_t*)(ident + 128 + 4 * flbas);
    uint32_t lbads = (lbaf >> 16) & 0xFF;
    if (lbads != 9) {
        kprint("[NVMe] Sektorgroesse != 512 Byte, nicht unterstuetzt\n");
        return -9;
    }
    nsectors = nsze > 0xFFFFFFFFULL ? 0xFFFFFFFFu : (uint32_t)nsze;

    /* I/O-Warteschlange 1 anlegen (erst CQ, dann SQ) */
    zero(&c, sizeof(c));
    c.opc = 0x05; c.prp1 = (uint64_t)(uintptr_t)io.cq; c.cdw10 = ((QD - 1) << 16) | 1; c.cdw11 = 1;
    if (submit(&adm, &c) != 0) { kprint("[NVMe] CQ anlegen fehlgeschlagen\n"); return -10; }
    zero(&c, sizeof(c));
    c.opc = 0x01; c.prp1 = (uint64_t)(uintptr_t)io.sq; c.cdw10 = ((QD - 1) << 16) | 1; c.cdw11 = (1u << 16) | 1;
    if (submit(&adm, &c) != 0) { kprint("[NVMe] SQ anlegen fehlgeschlagen\n"); return -11; }

    ready = 1;
    struct block_dev bd = { "nvme0", nsectors, nvme_read, nvme_write };
    block_register(&bd);

    kprint("[NVMe] nvme0 bereit: ");
    kprint_dec(nsectors / 2048);
    kprint(" MiB\n");
    return 0;
}
