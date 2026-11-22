/*
 * Zynex OS - VirtIO-GPU (2D). Ablauf: Ressource anlegen, eigenen Bildspeicher als Hintergrund anhaengen, Scanout 0 darauf setzen,
 * danach regelmaessig TRANSFER_TO_HOST_2D + RESOURCE_FLUSH. Der Kernel zeichnet danach in diesen Speicher (gfx_umleiten).
 */
#include "virtiogpu.h"
#include "pci.h"
#include "pmm.h"
#include "paging.h"
#include "timer.h"

extern void kprint(const char* str);
extern void kprint_dec(uint32_t val);
extern int  gfx_available(void);
extern void gfx_size(int* w, int* h);
extern void gfx_umleiten(void* neu, uint32_t pitch_bytes);

#define QS 16
struct vdesc { uint64_t addr; uint32_t len; uint16_t flags, next; } __attribute__((packed));

static volatile uint8_t *common, *notify;
static uint32_t notify_mult; static uint16_t notify_off;
static struct vdesc* desc; static volatile uint16_t *avail, *used;
static uint8_t* req;                       /* Anfrage ab 0, Antwort ab 2048 */
static uint16_t last_used;
static int ready, W, H;
static uint32_t lastflush;

static inline void fence(void) { __asm__ volatile ("mfence" : : : "memory"); }
static inline uint8_t  r8(uint32_t o)  { return *(volatile uint8_t*)(common + o); }
static inline void     w8(uint32_t o, uint8_t v)   { *(volatile uint8_t*)(common + o) = v; }
static inline uint16_t r16(uint32_t o) { return *(volatile uint16_t*)(common + o); }
static inline void     w16(uint32_t o, uint16_t v) { *(volatile uint16_t*)(common + o) = v; }
static inline void     w32(uint32_t o, uint32_t v) { *(volatile uint32_t*)(common + o) = v; }
static inline void     w64(uint32_t o, uint64_t v) { w32(o, (uint32_t)v); w32(o + 4, (uint32_t)(v >> 32)); }
static void zero(void* p, uint32_t n) { uint8_t* b = (uint8_t*)p; for (uint32_t i = 0; i < n; i++) b[i] = 0; }

static volatile uint8_t* karte(const struct pci_dev* d, uint8_t bar, uint32_t off, uint32_t len) {
    uint64_t b = pci_bar(d, bar); if (!b) return 0;
    uint64_t start = b + (off & ~0xFFFu), size = ((off & 0xFFF) + len + 0xFFF) & ~0xFFFu;
    if (paging_map_mmio(start, size) != 0) return 0;
    return (volatile uint8_t*)(uintptr_t)(b + off);
}

/* Befehl senden (w = Woerter, n = Anzahl), auf die Antwort warten; 0 = OK_NODATA (0x1100) */
static int cmd(const uint32_t* w, int n) {
    uint32_t* r = (uint32_t*)req;
    zero(req, 4096);
    for (int i = 0; i < n; i++) r[i] = w[i];
    desc[0].addr = (uint64_t)(uintptr_t)req;          desc[0].len = (uint32_t)(n * 4); desc[0].flags = 1; desc[0].next = 1;
    desc[1].addr = (uint64_t)(uintptr_t)(req + 2048); desc[1].len = 24;                desc[1].flags = 2; desc[1].next = 0;
    uint16_t idx = avail[1];
    avail[2 + (idx % QS)] = 0;
    fence(); avail[1] = (uint16_t)(idx + 1); fence();
    *(volatile uint16_t*)(notify + (uint32_t)notify_off * notify_mult) = 0;
    uint32_t t0 = timer_get_ticks();
    for (;;) { fence(); if (used[1] != last_used) break; if (timer_get_ticks() - t0 > 300) return -3; }
    last_used++;
    return *(volatile uint32_t*)(req + 2048) == 0x1100u ? 0 : -4;
}

int virtiogpu_aktiv(void) { return ready; }

int virtiogpu_init(void) {
    struct pci_dev* d = 0;
    for (int i = 0; i < pci_count(); i++) { struct pci_dev* x = pci_get(i); if (x->vendor == 0x1AF4 && x->device == 0x1050 && !(x->cls == 3 && x->sub == 0)) { d = x; break; } }
    if (!d || !gfx_available()) return -1;
    pci_enable(d);
    uint32_t cp = pci_read32(d, 0x34) & 0xFF; int hc = 0, hn = 0;
    for (int guard = 0; cp && guard < 48; guard++) {
        uint32_t h = pci_read32(d, (uint8_t)cp); uint8_t id = h & 0xFF, next = (h >> 8) & 0xFF;
        if (id == 9) {
            uint8_t type = (h >> 24) & 0xFF, bar = pci_read32(d, (uint8_t)(cp + 4)) & 0xFF;
            uint32_t off = pci_read32(d, (uint8_t)(cp + 8)), len = pci_read32(d, (uint8_t)(cp + 12));
            if (type == 1) { common = karte(d, bar, off, len < 64 ? 64 : len); hc = common != 0; }
            else if (type == 2) { notify_mult = pci_read32(d, (uint8_t)(cp + 16)); notify = karte(d, bar, off, len < 4 ? 4 : len); hn = notify != 0; }
        }
        cp = next;
    }
    if (!hc || !hn) { kprint("[VirtIO-GPU] Geraet ohne virtio-1.0-PCI-Zugang\n"); return -2; }
    w8(20, 0); w8(20, 1); w8(20, 3);
    w32(8, 1); w32(12, 1); w32(8, 0); w32(12, 0);
    w8(20, 11);
    if (!(r8(20) & 8)) { kprint("[VirtIO-GPU] Features abgelehnt\n"); return -3; }
    uint8_t* q = (uint8_t*)pmm_alloc_page(); req = (uint8_t*)pmm_alloc_page();
    if (!q || !req) { kprint("[VirtIO-GPU] Kein Speicher\n"); return -4; }
    zero(q, 4096); desc = (struct vdesc*)q; avail = (volatile uint16_t*)(q + 256); used = (volatile uint16_t*)(q + 512);
    w16(22, 0);
    if (r16(24) < QS) { kprint("[VirtIO-GPU] Warteschlange zu klein\n"); return -5; }
    w16(24, QS);
    w64(32, (uint64_t)(uintptr_t)desc); w64(40, (uint64_t)(uintptr_t)avail); w64(48, (uint64_t)(uintptr_t)used);
    notify_off = r16(30); w16(28, 1);
    w8(20, 15);

    gfx_size(&W, &H);
    uint32_t pages = ((uint32_t)W * (uint32_t)H * 4 + 4095) / 4096;
    uint8_t* buf = (uint8_t*)pmm_alloc_pages(pages);
    if (!buf) { kprint("[VirtIO-GPU] Kein Speicher fuer den Bildspeicher\n"); return -6; }
    zero(buf, pages * 4096);
    /* RESOURCE_CREATE_2D: Ressource 1, Format B8G8R8X8 (2) */
    uint32_t c1[10] = { 0x0101, 0, 0, 0, 0, 0, 1, 2, (uint32_t)W, (uint32_t)H };
    /* RESOURCE_ATTACH_BACKING: ein Eintrag (Adresse, Laenge) */
    uint32_t c2[12] = { 0x0106, 0, 0, 0, 0, 0, 1, 1, (uint32_t)(uintptr_t)buf, (uint32_t)((uint64_t)(uintptr_t)buf >> 32), pages * 4096, 0 };
    /* SET_SCANOUT: Rechteck, Scanout 0, Ressource 1 */
    uint32_t c3[12] = { 0x0103, 0, 0, 0, 0, 0, 0, 0, (uint32_t)W, (uint32_t)H, 0, 1 };
    if (cmd(c1, 10) || cmd(c2, 12) || cmd(c3, 12)) { kprint("[VirtIO-GPU] Anzeige konnte nicht eingerichtet werden\n"); return -7; }
    gfx_umleiten(buf, (uint32_t)W * 4);
    ready = 1; lastflush = 0;
    virtiogpu_tick();
    kprint("[VirtIO-GPU] Anzeige "); kprint_dec((uint32_t)W); kprint("x"); kprint_dec((uint32_t)H); kprint(" bereit (eigener Bildspeicher, Scanout 0)\n");
    return 0;
}

void virtiogpu_tick(void) {
    if (!ready) return;
    uint32_t t = timer_get_ticks();
    if (t - lastflush < 3) return;
    lastflush = t;
    uint32_t tr[14] = { 0x0105, 0, 0, 0, 0, 0, 0, 0, (uint32_t)W, (uint32_t)H, 0, 0, 1, 0 };   /* TRANSFER_TO_HOST_2D: Rechteck, Offset 0, Ressource 1 */
    uint32_t fl[12] = { 0x0104, 0, 0, 0, 0, 0, 0, 0, (uint32_t)W, (uint32_t)H, 1, 0 };          /* RESOURCE_FLUSH */
    if (cmd(tr, 14) == 0) cmd(fl, 12);
}
