/*
 * Zynex OS - VirtIO-Blk-Treiber (virtio 1.0, PCI "modern", Polling ohne Interrupts)
 * Fuer QEMU/KVM, Cloud-VMs und andere virtuelle Maschinen. Eine Warteschlange, bis 8 Sektoren je Anfrage.
 */
#include "virtioblk.h"
#include "pci.h"
#include "pmm.h"
#include "paging.h"
#include "block.h"
#include "timer.h"

extern void kprint(const char* str);
extern void kprint_dec(uint32_t val);

#define QS 16

struct vdesc { uint64_t addr; uint32_t len; uint16_t flags, next; } __attribute__((packed));

static volatile uint8_t* common;
static volatile uint8_t* notify;
static volatile uint8_t* devcfg;
static uint32_t notify_mult;
static uint16_t notify_off;
static struct vdesc* desc;
static volatile uint16_t* avail;
static volatile uint16_t* used;
static uint8_t* req;                  /* Seite: Kopf (16 B), Status (Offset 16) */
static uint8_t* bounce;
static uint16_t last_used = 0;
static uint32_t nsectors;
static int ready = 0;

static inline void fence(void) { __asm__ volatile ("mfence" : : : "memory"); }
static inline uint8_t  r8(uint32_t o)  { return *(volatile uint8_t*)(common + o); }
static inline void     w8(uint32_t o, uint8_t v)   { *(volatile uint8_t*)(common + o) = v; }
static inline uint16_t r16(uint32_t o) { return *(volatile uint16_t*)(common + o); }
static inline void     w16(uint32_t o, uint16_t v) { *(volatile uint16_t*)(common + o) = v; }
static inline uint32_t r32(uint32_t o) { return *(volatile uint32_t*)(common + o); }
static inline void     w32(uint32_t o, uint32_t v) { *(volatile uint32_t*)(common + o) = v; }
static inline void     w64(uint32_t o, uint64_t v) { w32(o, (uint32_t)v); w32(o + 4, (uint32_t)(v >> 32)); }

static void zero(void* p, uint32_t n) { uint8_t* b = (uint8_t*)p; for (uint32_t i = 0; i < n; i++) b[i] = 0; }

static int rw(int schreiben, uint32_t lba, void* buf, uint32_t count) {
    uint8_t* p = (uint8_t*)buf;
    while (count) {
        uint32_t n = count > 8 ? 8 : count;
        if (lba + n > nsectors) return -2;
        if (schreiben) for (uint32_t i = 0; i < n * 512; i++) bounce[i] = p[i];
        *(volatile uint32_t*)(req + 0) = schreiben ? 1 : 0;      /* VIRTIO_BLK_T_OUT / _IN */
        *(volatile uint32_t*)(req + 4) = 0;
        *(volatile uint64_t*)(req + 8) = lba;
        req[16] = 0xFF;
        desc[0].addr = (uint64_t)(uintptr_t)req;     desc[0].len = 16;      desc[0].flags = 1;                    desc[0].next = 1;
        desc[1].addr = (uint64_t)(uintptr_t)bounce;  desc[1].len = n * 512; desc[1].flags = (uint16_t)(1 | (schreiben ? 0 : 2)); desc[1].next = 2;
        desc[2].addr = (uint64_t)(uintptr_t)(req + 16); desc[2].len = 1;    desc[2].flags = 2;                    desc[2].next = 0;
        uint16_t idx = avail[1];
        avail[2 + (idx % QS)] = 0;
        fence();
        avail[1] = (uint16_t)(idx + 1);
        fence();
        *(volatile uint16_t*)(notify + (uint32_t)notify_off * notify_mult) = 0;
        uint32_t t0 = timer_get_ticks();
        for (;;) {
            fence();
            if (used[1] != last_used) break;
            if (timer_get_ticks() - t0 > 500) return -3;
        }
        last_used++;
        if (req[16] != 0) return -4;
        if (!schreiben) for (uint32_t i = 0; i < n * 512; i++) p[i] = bounce[i];
        p += n * 512; lba += n; count -= n;
    }
    return 0;
}

static int vb_read(uint32_t lba, void* buf, uint32_t count) { return ready && rw(0, lba, buf, count) == 0 ? (int)count : -1; }
static int vb_write(uint32_t lba, const void* buf, uint32_t count) { return ready && rw(1, lba, (void*)buf, count) == 0 ? (int)count : -1; }

static volatile uint8_t* karte(const struct pci_dev* d, uint8_t bar, uint32_t off, uint32_t len) {
    uint64_t b = pci_bar(d, bar);
    if (!b) return 0;
    uint64_t start = b + (off & ~0xFFFu), size = ((off & 0xFFF) + len + 0xFFF) & ~0xFFFu;
    if (paging_map_mmio(start, size) != 0) return 0;
    return (volatile uint8_t*)(uintptr_t)(b + off);
}

int virtioblk_init(void) {
    struct pci_dev* d = 0;
    for (int i = 0; i < pci_count(); i++) { struct pci_dev* x = pci_get(i); if (x->vendor == 0x1AF4 && (x->device == 0x1042 || x->device == 0x1001)) { d = x; break; } }
    if (!d) return -1;
    /* Fuer 0x1001 (Transitional) nutzen wir ebenfalls den modernen Pfad, falls das Geraet ihn anbietet. */
    pci_enable(d);
    uint32_t cp = pci_read32(d, 0x34) & 0xFF;
    int have_common = 0, have_notify = 0, have_dev = 0;
    for (int guard = 0; cp && guard < 48; guard++) {
        uint32_t h = pci_read32(d, (uint8_t)cp);
        uint8_t id = h & 0xFF, next = (h >> 8) & 0xFF;
        if (id == 9) {
            uint8_t type = (h >> 24) & 0xFF;
            uint8_t bar = pci_read32(d, (uint8_t)(cp + 4)) & 0xFF;
            uint32_t off = pci_read32(d, (uint8_t)(cp + 8)), len = pci_read32(d, (uint8_t)(cp + 12));
            if (type == 1) { common = karte(d, bar, off, len < 64 ? 64 : len); have_common = common != 0; }
            else if (type == 2) { notify_mult = pci_read32(d, (uint8_t)(cp + 16)); notify = karte(d, bar, off, len < 4 ? 4 : len); have_notify = notify != 0; }
            else if (type == 4) { devcfg = karte(d, bar, off, len < 16 ? 16 : len); have_dev = devcfg != 0; }
        }
        cp = next;
    }
    if (!have_common || !have_notify || !have_dev) { kprint("[VirtIO-Blk] Geraet ohne virtio-1.0-PCI-Zugang (Legacy nicht unterstuetzt)\n"); return -2; }

    w8(20, 0);                                   /* Reset */
    w8(20, 1); w8(20, 3);                        /* ACKNOWLEDGE, DRIVER */
    w32(8, 1); w32(12, 1);                       /* Features 32..63: nur VERSION_1 (Bit 32) */
    w32(8, 0); w32(12, 0);
    w8(20, 11);                                  /* FEATURES_OK */
    if (!(r8(20) & 8)) { kprint("[VirtIO-Blk] Features abgelehnt\n"); return -3; }

    uint8_t* q = (uint8_t*)pmm_alloc_page();
    req = (uint8_t*)pmm_alloc_page(); bounce = (uint8_t*)pmm_alloc_page();
    if (!q || !req || !bounce) { kprint("[VirtIO-Blk] Kein Speicher\n"); return -4; }
    zero(q, 4096); zero(req, 4096); zero(bounce, 4096);
    desc = (struct vdesc*)q; avail = (volatile uint16_t*)(q + 256); used = (volatile uint16_t*)(q + 512);

    w16(22, 0);                                  /* Queue 0 */
    uint16_t qs = r16(24);
    if (qs < QS) { kprint("[VirtIO-Blk] Warteschlange zu klein\n"); return -5; }
    w16(24, QS);
    w64(32, (uint64_t)(uintptr_t)desc); w64(40, (uint64_t)(uintptr_t)avail); w64(48, (uint64_t)(uintptr_t)used);
    notify_off = r16(30);
    w16(28, 1);
    w8(20, 15);                                  /* DRIVER_OK */

    uint64_t cap = *(volatile uint64_t*)devcfg;
    nsectors = cap > 0xFFFFFFFFULL ? 0xFFFFFFFFu : (uint32_t)cap;
    ready = 1;
    struct block_dev bd = { "vblk0", nsectors, vb_read, vb_write };
    block_register(&bd);
    kprint("[VirtIO-Blk] vblk0 bereit: "); kprint_dec(nsectors / 2048); kprint(" MiB\n");
    return 0;
}
