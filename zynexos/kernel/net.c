/*
 * Zynex OS - Netzwerk
 * Treiber fuer Intel-Netzwerkkarten der e1000/e1000e-Familie (Legacy-Deskriptoren, Polling),
 * darueber ARP, IPv4 (ohne Fragmente), ICMP-Echo, UDP, DHCP-Client und DNS-Client.
 */
#include "net.h"
#include "fw.h"
#include "hyperv.h"
#include "pci.h"
#include "pmm.h"
#include "paging.h"
#include "timer.h"

extern void kprint(const char* str);
extern void kprint_dec(uint32_t val);
extern void kprint_hex(uint32_t val);
extern void kputchar(char c);

/* ---------- Hilfen ---------- */
static void mcopy(void* d, const void* s, int n) { uint8_t* a = d; const uint8_t* b = s; for (int i = 0; i < n; i++) a[i] = b[i]; }
static void mzero(void* d, int n) { uint8_t* a = d; for (int i = 0; i < n; i++) a[i] = 0; }
static int  mcmp(const void* d, const void* s, int n) { const uint8_t* a = d; const uint8_t* b = s; for (int i = 0; i < n; i++) if (a[i] != b[i]) return 1; return 0; }
static inline uint16_t be16(uint16_t v) { return (uint16_t)((v << 8) | (v >> 8)); }
static void put16(uint8_t* p, uint16_t v) { p[0] = v >> 8; p[1] = (uint8_t)v; }
static void put32(uint8_t* p, uint32_t v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = (uint8_t)v; }
static uint16_t get16(const uint8_t* p) { return (uint16_t)((p[0] << 8) | p[1]); }
static uint32_t get32(const uint8_t* p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }
static void kprint_ip(uint32_t ip) {
    kprint_dec(ip >> 24); kputchar('.'); kprint_dec((ip >> 16) & 255); kputchar('.');
    kprint_dec((ip >> 8) & 255); kputchar('.'); kprint_dec(ip & 255);
}
static void kprint_mac(const uint8_t* m) {
    static const char* h = "0123456789abcdef";
    for (int i = 0; i < 6; i++) { kputchar(h[m[i] >> 4]); kputchar(h[m[i] & 15]); if (i < 5) kputchar(':'); }
}
static uint16_t csum(const uint8_t* d, int n) {
    uint32_t s = 0;
    for (int i = 0; i + 1 < n; i += 2) s += (uint32_t)((d[i] << 8) | d[i + 1]);
    if (n & 1) s += (uint32_t)d[n - 1] << 8;
    while (s >> 16) s = (s & 0xFFFF) + (s >> 16);
    return (uint16_t)~s;
}
static inline void fence(void) { __asm__ volatile ("mfence" : : : "memory"); }

/* ---------- Netzwerkkarte (e1000-Familie) ---------- */
#define NRX 32
#define NTX 8
#define BUFSZ 2048

struct rxd { uint64_t addr; uint16_t len, csum; uint8_t status, err; uint16_t special; } __attribute__((packed));
struct txd { uint64_t addr; uint16_t len; uint8_t cso, cmd, status, css; uint16_t special; } __attribute__((packed));

static volatile uint8_t* R;
static struct rxd* rx;
static struct txd* tx;
static uint8_t* rxbuf;
static uint8_t* txbuf;
static uint16_t rx_cur, tx_cur;
static uint8_t tx_used[NTX];
static int nic_ok = 0;
static int nic_typ = 0;                 /* 1 = e1000-Familie, 2 = Realtek */
static struct rtd* rrx;
static struct rtd* rtx;
static uint8_t mymac[6];
/* USB-Netzwerk (CDC-ECM / RNDIS, z. B. USB-Tethering am Handy): nic_typ 4 */
extern int  usbnet_send(const uint8_t* f, int len);
extern int  wlan_senden(const uint8_t* eth, int len);
extern void wlan_poll(void (*cb)(const uint8_t* eth, int len));
extern void usbnet_poll(void (*cb)(const uint8_t*, int));
static uint8_t pci_mac[6]; static int pci_typ_prev = 0, pci_ok_prev = 0, usb_aktiv = 0, net_fertig = 0, dhcp_wunsch = 0;

static inline uint32_t rd(uint32_t o) { return *(volatile uint32_t*)(R + o); }
static inline void wr(uint32_t o, uint32_t v) { *(volatile uint32_t*)(R + o) = v; }

static int ist_e1000(uint16_t id) {
    switch (id) {
        case 0x100E: case 0x100F: case 0x1004: case 0x10D3: case 0x10EA: case 0x153A: case 0x153B:
        case 0x1559: case 0x15A0: case 0x15A1: case 0x15A2: case 0x15A3: case 0x156F: case 0x1570:
        case 0x15B7: case 0x15B8: case 0x15B9: case 0x15BB: case 0x15BC: case 0x15BD: case 0x15BE:
        case 0x15D6: case 0x15D7: case 0x15D8: case 0x15DF: case 0x15E0: case 0x15E1: case 0x15E2: case 0x15E3:
        case 0x1533: case 0x1539: case 0x1536: case 0x1521: case 0x10C9: case 0x10D5: case 0x10BD:
            return 1;
    }
    return 0;
}

static int nic_start(void) {
    struct pci_dev* d = 0;
    for (int i = 0; i < pci_count(); i++) {
        struct pci_dev* p = pci_get(i);
        if (p->cls == 0x02 && p->sub == 0x00 && p->vendor == 0x8086 && ist_e1000(p->device)) { d = p; break; }
    }
    if (!d) return -1;
    uint64_t bar = pci_bar(d, 0);
    if (!bar) return -2;
    pci_enable(d);
    if (paging_map_mmio(bar, 0x20000) != 0) return -3;
    R = (volatile uint8_t*)(uintptr_t)bar;

    wr(0xD8, 0xFFFFFFFFu);                              /* IMC: keine Interrupts */
    wr(0x100, 0); wr(0x400, 0);                         /* RX/TX aus */
    wr(0x00, rd(0x00) | (1u << 26));                    /* CTRL.RST */
    for (volatile int i = 0; i < 2000000 && (rd(0x00) & (1u << 26)); i++) {}
    wr(0xD8, 0xFFFFFFFFu); (void)rd(0xC0);              /* Interrupts aus, ICR loeschen */
    wr(0x00, (rd(0x00) | (1u << 6) | (1u << 5)) & ~((1u << 3) | (1u << 31)));   /* SLU, ASDE; kein LRST/PHY_RST */

    uint32_t ral = rd(0x5400), rah = rd(0x5404);
    for (int i = 0; i < 4; i++) mymac[i] = (uint8_t)(ral >> (8 * i));
    mymac[4] = (uint8_t)rah; mymac[5] = (uint8_t)(rah >> 8);
    int leer = 1; for (int i = 0; i < 6; i++) if (mymac[i]) leer = 0;
    if (leer) { static const uint8_t f[6] = {0x02, 0x5A, 0x59, 0x4E, 0x45, 0x58}; mcopy(mymac, f, 6); }
    wr(0x5400, (uint32_t)mymac[0] | (uint32_t)mymac[1] << 8 | (uint32_t)mymac[2] << 16 | (uint32_t)mymac[3] << 24);
    wr(0x5404, (uint32_t)mymac[4] | (uint32_t)mymac[5] << 8 | (1u << 31));
    for (int i = 0; i < 128; i++) wr(0x5200 + 4 * i, 0);        /* Multicast-Tabelle leeren */

    uint8_t* ring = (uint8_t*)pmm_alloc_page();
    rxbuf = (uint8_t*)pmm_alloc_pages((NRX * BUFSZ) / 4096);
    txbuf = (uint8_t*)pmm_alloc_pages((NTX * BUFSZ) / 4096);
    if (!ring || !rxbuf || !txbuf) return -4;
    mzero(ring, 4096);
    rx = (struct rxd*)ring; tx = (struct txd*)(ring + 2048);
    for (int i = 0; i < NRX; i++) rx[i].addr = (uint64_t)(uintptr_t)(rxbuf + i * BUFSZ);

    wr(0x2800, (uint32_t)(uintptr_t)rx); wr(0x2804, (uint32_t)((uint64_t)(uintptr_t)rx >> 32));
    wr(0x2808, NRX * 16); wr(0x2810, 0); wr(0x2818, NRX - 1);
    wr(0x2828, (1u << 24) | (1u << 16) | 4);                   /* RXDCTL: Granularitaet, WTHRESH=1 */
    wr(0x3800, (uint32_t)(uintptr_t)tx); wr(0x3804, (uint32_t)((uint64_t)(uintptr_t)tx >> 32));
    wr(0x3808, NTX * 16); wr(0x3810, 0); wr(0x3818, 0);
    wr(0x3828, (1u << 24) | (1u << 16) | 1);                   /* TXDCTL */
    wr(0x410, 0x0060200A);                                     /* TIPG */
    wr(0x400, (1u << 1) | (1u << 3) | (0x0Fu << 4) | (0x40u << 12));      /* TCTL: EN, PSP, CT, COLD */
    wr(0x100, (1u << 1) | (1u << 15) | (1u << 26));            /* RCTL: EN, BAM, SECRC, 2048-Byte-Puffer */
    rx_cur = 0; tx_cur = 0;
    nic_ok = 1;
    return 0;
}


/* ---------- Realtek r8168/r8169 (Gigabit, PCIe) ---------- */
struct rtd { uint32_t opts1, opts2; uint64_t addr; } __attribute__((packed));
#define RTL_OWN (1u << 31)
#define RTL_EOR (1u << 30)
#define RTL_FS  (1u << 29)
#define RTL_LS  (1u << 28)

static inline uint8_t  rd8(uint32_t o) { return *(volatile uint8_t*)(R + o); }
static inline void     wr8(uint32_t o, uint8_t v) { *(volatile uint8_t*)(R + o) = v; }

static int ist_realtek(uint16_t id) {
    return id == 0x8168 || id == 0x8169 || id == 0x8161 || id == 0x8167 || id == 0x8136;
}

static int nic_start_rtl(void) {
    struct pci_dev* d = 0;
    for (int i = 0; i < pci_count(); i++) {
        struct pci_dev* p = pci_get(i);
        if (p->cls == 0x02 && p->sub == 0x00 && p->vendor == 0x10EC && ist_realtek(p->device)) { d = p; break; }
    }
    if (!d) return -1;
    /* Die Register liegen je nach Modell in BAR2 (64 Bit) oder BAR1; erste Speicher-BAR nehmen */
    uint64_t bar = 0;
    for (int b = 0; b < 6 && !bar; b++) {
        uint32_t lo = pci_read32(d, (uint8_t)(0x10 + b * 4));
        if (lo & 1) continue;
        uint64_t a = pci_bar(d, b);
        uint32_t size_probe = 0; (void)size_probe;
        if (a && b != 0) bar = a;                       /* BAR0 ist bei diesen Karten die I/O-Variante */
        if (((lo >> 1) & 3) == 2) b++;
    }
    if (!bar) return -2;
    pci_enable(d);
    if (paging_map_mmio(bar, 0x1000) != 0) return -3;
    R = (volatile uint8_t*)(uintptr_t)bar;

    wr8(0x37, 0x10);                                    /* CR.RST */
    uint32_t t0 = timer_get_ticks();
    while (rd8(0x37) & 0x10) if (timer_get_ticks() - t0 > 100) return -5;
    for (int i = 0; i < 6; i++) mymac[i] = rd8((uint32_t)i);
    int leer = 1; for (int i = 0; i < 6; i++) if (mymac[i]) leer = 0;
    if (leer) return -6;

    uint8_t* ring = (uint8_t*)pmm_alloc_page();
    rxbuf = (uint8_t*)pmm_alloc_pages((NRX * BUFSZ) / 4096);
    txbuf = (uint8_t*)pmm_alloc_pages((NTX * BUFSZ) / 4096);
    if (!ring || !rxbuf || !txbuf) return -4;
    mzero(ring, 4096);
    rrx = (struct rtd*)ring; rtx = (struct rtd*)(ring + 1024);
    for (int i = 0; i < NRX; i++) {
        rrx[i].addr = (uint64_t)(uintptr_t)(rxbuf + i * BUFSZ);
        rrx[i].opts1 = RTL_OWN | (i == NRX - 1 ? RTL_EOR : 0) | BUFSZ;
    }
    rtx[NTX - 1].opts1 = RTL_EOR;

    wr8(0x50, 0xC0);                                    /* Cfg9346: Konfigurationsregister entsperren */
    wr(0x3C, 0);                                        /* keine Interrupts */
    *(volatile uint16_t*)(R + 0xDA) = 2047;             /* RxMaxSize */
    wr(0x20, (uint32_t)(uintptr_t)rtx); wr(0x24, (uint32_t)((uint64_t)(uintptr_t)rtx >> 32));
    wr(0xE4, (uint32_t)(uintptr_t)rrx); wr(0xE8, (uint32_t)((uint64_t)(uintptr_t)rrx >> 32));
    wr8(0x37, 0x0C);                                    /* RE | TE */
    wr(0x40, 0x03000700);                               /* TxConfig: IFG normal, DMA unbegrenzt */
    wr(0x44, 0x0000E70A);                               /* RxConfig: Broadcast + Physical-Match, DMA unbegrenzt */
    wr(0x08, 0); wr(0x0C, 0);                           /* Multicast-Filter leer */
    wr8(0xEC, 0x3F);                                    /* MaxTxPacketSize */
    *(volatile uint16_t*)(R + 0x3E) = 0xFFFF;           /* ISR loeschen */
    wr8(0x50, 0x00);                                    /* wieder sperren */
    rx_cur = 0; tx_cur = 0; nic_typ = 2; nic_ok = 1;
    return 0;
}

static void rx_frame(const uint8_t* f, int len);

/* ---------- virtio-net (virtio 1.0, PCI, fuer virtuelle Maschinen) ---------- */
#define VQN 16
struct vdesc { uint64_t addr; uint32_t len; uint16_t flags, next; } __attribute__((packed));
struct vq {
    struct vdesc* d; uint16_t* avail; volatile uint16_t* used;   /* used: idx bei [1], Eintraege ab [2] (je 8 Byte) */
    uint16_t n, avail_idx, last_used; volatile uint16_t* notify;
};
static struct vq vrx, vtx;
static volatile uint8_t* vcommon;
static uint8_t* vbuf_rx; static uint8_t* vbuf_tx;
static uint16_t vtx_inflight;
#define VHDR 12

static int vq_setup(struct vq* q, uint16_t idx, volatile uint8_t* notify_base, uint32_t mult) {
    *(volatile uint16_t*)(vcommon + 22) = idx;
    uint16_t n = *(volatile uint16_t*)(vcommon + 24);
    if (n == 0) return -1;
    if (n > VQN) n = VQN;
    *(volatile uint16_t*)(vcommon + 24) = n;
    uint8_t* mem = (uint8_t*)pmm_alloc_pages(2);
    if (!mem) return -2;
    mzero(mem, 8192);
    q->n = n;
    q->d = (struct vdesc*)mem;                          /* 16*n Byte */
    q->avail = (uint16_t*)(mem + 1024);                 /* flags, idx, ring[] */
    q->used = (volatile uint16_t*)(mem + 4096);         /* flags, idx, ring[] (id u32, len u32) */
    uint64_t a;
    a = (uint64_t)(uintptr_t)q->d;     *(volatile uint32_t*)(vcommon + 32) = (uint32_t)a; *(volatile uint32_t*)(vcommon + 36) = (uint32_t)(a >> 32);
    a = (uint64_t)(uintptr_t)q->avail; *(volatile uint32_t*)(vcommon + 40) = (uint32_t)a; *(volatile uint32_t*)(vcommon + 44) = (uint32_t)(a >> 32);
    a = (uint64_t)(uintptr_t)q->used;  *(volatile uint32_t*)(vcommon + 48) = (uint32_t)a; *(volatile uint32_t*)(vcommon + 52) = (uint32_t)(a >> 32);
    uint16_t noff = *(volatile uint16_t*)(vcommon + 30);
    q->notify = (volatile uint16_t*)(notify_base + (uint64_t)noff * mult);
    *(volatile uint16_t*)(vcommon + 28) = 1;            /* queue_enable */
    q->avail_idx = 0; q->last_used = 0;
    return 0;
}

static int nic_start_virtio(void) {
    struct pci_dev* d = 0;
    for (int i = 0; i < pci_count(); i++) {
        struct pci_dev* p = pci_get(i);
        if (p->cls == 0x02 && p->sub == 0x00 && p->vendor == 0x1AF4 && (p->device == 0x1041 || p->device == 0x1000)) { d = p; break; }
    }
    if (!d) return -1;
    /* Capabilities durchsuchen */
    uint32_t cc_bar = 0xFF, cc_off = 0, nt_bar = 0xFF, nt_off = 0, dv_bar = 0xFF, dv_off = 0, nt_mult = 0;
    uint32_t cp = pci_read32(d, 0x34) & 0xFF;
    for (int guard = 0; cp >= 0x40 && guard < 48; guard++) {
        uint32_t w0 = pci_read32(d, (uint8_t)cp);
        if ((w0 & 0xFF) == 0x09) {
            uint32_t typ = (w0 >> 24) & 0xFF;
            uint32_t w1 = pci_read32(d, (uint8_t)(cp + 4)), off = pci_read32(d, (uint8_t)(cp + 8));
            uint32_t bar = w1 & 0xFF;
            if (typ == 1) { cc_bar = bar; cc_off = off; }
            else if (typ == 2) { nt_bar = bar; nt_off = off; nt_mult = pci_read32(d, (uint8_t)(cp + 16)); }
            else if (typ == 4) { dv_bar = bar; dv_off = off; }
        }
        cp = (w0 >> 8) & 0xFF;
    }
    if (cc_bar > 5 || nt_bar > 5 || dv_bar > 5) return -2;       /* kein virtio-1.0-Geraet */
    pci_enable(d);
    uint64_t b[6]; for (int i = 0; i < 6; i++) b[i] = 0;
    uint32_t bars[3] = { cc_bar, nt_bar, dv_bar };
    for (int i = 0; i < 3; i++) {
        if (!b[bars[i]]) {
            b[bars[i]] = pci_bar(d, (int)bars[i]);
            if (!b[bars[i]] || paging_map_mmio(b[bars[i]], 0x10000) != 0) return -3;
        }
    }
    vcommon = (volatile uint8_t*)(uintptr_t)(b[cc_bar] + cc_off);
    volatile uint8_t* nbase = (volatile uint8_t*)(uintptr_t)(b[nt_bar] + nt_off);
    volatile uint8_t* dev = (volatile uint8_t*)(uintptr_t)(b[dv_bar] + dv_off);

    volatile uint8_t* st = vcommon + 20;
    *st = 0;
    uint32_t t0 = timer_get_ticks();
    while (*st != 0) if (timer_get_ticks() - t0 > 100) return -5;
    *st = 1; *st = 3;                                             /* ACKNOWLEDGE | DRIVER */
    *(volatile uint32_t*)(vcommon + 0) = 1;                       /* Geraete-Features Bits 32-63 */
    if (!(*(volatile uint32_t*)(vcommon + 4) & 1)) return -6;     /* VERSION_1 fehlt */
    *(volatile uint32_t*)(vcommon + 0) = 0;
    uint32_t f0 = *(volatile uint32_t*)(vcommon + 4);
    *(volatile uint32_t*)(vcommon + 8) = 0; *(volatile uint32_t*)(vcommon + 12) = f0 & (1u << 5);   /* nur MAC */
    *(volatile uint32_t*)(vcommon + 8) = 1; *(volatile uint32_t*)(vcommon + 12) = 1;                 /* VERSION_1 */
    *st = 0x0B;                                                   /* + FEATURES_OK */
    if (!(*st & 8)) return -7;
    if (vq_setup(&vrx, 0, nbase, nt_mult) != 0 || vq_setup(&vtx, 1, nbase, nt_mult) != 0) return -4;
    if (f0 & (1u << 5)) for (int i = 0; i < 6; i++) mymac[i] = dev[i];
    else { static const uint8_t f[6] = {0x02, 0x5A, 0x59, 0x4E, 0x45, 0x59}; mcopy(mymac, f, 6); }

    vbuf_rx = (uint8_t*)pmm_alloc_pages((VQN * BUFSZ) / 4096);
    vbuf_tx = (uint8_t*)pmm_alloc_pages((VQN * BUFSZ) / 4096);
    if (!vbuf_rx || !vbuf_tx) return -4;
    mzero(vbuf_tx, VQN * BUFSZ);
    for (uint16_t i = 0; i < vrx.n; i++) {
        vrx.d[i].addr = (uint64_t)(uintptr_t)(vbuf_rx + i * BUFSZ); vrx.d[i].len = BUFSZ; vrx.d[i].flags = 2; vrx.d[i].next = 0;
        vrx.avail[2 + i] = i;
    }
    fence();
    vrx.avail_idx = vrx.n; vrx.avail[1] = vrx.n;
    fence();
    *st = 0x0F;                                                   /* DRIVER_OK */
    *vrx.notify = 0;
    vtx_inflight = 0;
    nic_typ = 3; nic_ok = 1;
    return 0;
}

static int vio_send(const uint8_t* f, int len) {
    /* fertige Sendepuffer zurueckholen */
    while (vtx.last_used != vtx.used[1]) { vtx.last_used++; if (vtx_inflight) vtx_inflight--; }
    uint32_t t0 = timer_get_ticks();
    while (vtx_inflight >= vtx.n) {
        while (vtx.last_used != vtx.used[1]) { vtx.last_used++; if (vtx_inflight) vtx_inflight--; }
        if (vtx_inflight >= vtx.n && timer_get_ticks() - t0 > 20) return -2;
    }
    uint16_t i = vtx.avail_idx % vtx.n;
    uint8_t* b = vbuf_tx + i * BUFSZ;
    mzero(b, VHDR);
    mcopy(b + VHDR, f, len);
    vtx.d[i].addr = (uint64_t)(uintptr_t)b; vtx.d[i].len = (uint32_t)(len + VHDR); vtx.d[i].flags = 0; vtx.d[i].next = 0;
    vtx.avail[2 + (vtx.avail_idx % vtx.n)] = i;
    fence();
    vtx.avail_idx++; vtx.avail[1] = vtx.avail_idx;
    vtx_inflight++;
    fence();
    *vtx.notify = 1;
    return 0;
}

static void vio_poll(void) {
    for (int n = 0; n < VQN && vrx.last_used != vrx.used[1]; n++) {
        fence();
        uint32_t* e = (uint32_t*)(vrx.used + 2) + 2 * (vrx.last_used % vrx.n);
        uint32_t id = e[0], len = e[1];
        if (id < vrx.n && len > VHDR) rx_frame(vbuf_rx + id * BUFSZ + VHDR, (int)(len - VHDR));
        vrx.last_used++;
        vrx.avail[2 + (vrx.avail_idx % vrx.n)] = (uint16_t)id;
        fence();
        vrx.avail_idx++; vrx.avail[1] = vrx.avail_idx;
        fence();
        *vrx.notify = 0;
    }
}

static int link_up(void) { if (nic_typ == 6 || nic_typ == 5 || nic_typ == 4 || nic_typ == 3) return 1; if (nic_typ == 2) return (rd8(0x6C) & 2) != 0; return (rd(0x08) & 2) != 0; }

static int nic_send(const uint8_t* f, int len) {
    if (!nic_ok || len > 1514) return -1;
    if (nic_typ == 6) return wlan_senden(f, len);
    if (nic_typ == 5) return hv_net_senden(f, len < 60 ? 60 : len);
    if (nic_typ == 4) return usbnet_send(f, len < 60 ? 60 : len);
    if (nic_typ == 3) return vio_send(f, len < 60 ? 60 : len);
    if (nic_typ == 2) {
        uint16_t j = tx_cur;
        uint32_t t1 = timer_get_ticks();
        while (rtx[j].opts1 & RTL_OWN) if (timer_get_ticks() - t1 > 20) return -2;
        uint8_t* bb = txbuf + j * BUFSZ;
        mcopy(bb, f, len);
        if (len < 60) { mzero(bb + len, 60 - len); len = 60; }
        rtx[j].addr = (uint64_t)(uintptr_t)bb; rtx[j].opts2 = 0;
        fence();
        rtx[j].opts1 = RTL_OWN | RTL_FS | RTL_LS | (j == NTX - 1 ? RTL_EOR : 0) | (uint32_t)len;
        fence();
        tx_cur = (uint16_t)((j + 1) % NTX);
        wr8(0x38, 0x40);                                /* TPPoll.NPQ: Senden anstossen */
        return 0;
    }
    uint16_t i = tx_cur;
    if (tx_used[i]) {
        uint32_t t0 = timer_get_ticks();
        while (!(tx[i].status & 1)) if (timer_get_ticks() - t0 > 20) return -2;
    }
    uint8_t* b = txbuf + i * BUFSZ;
    mcopy(b, f, len);
    if (len < 60) { mzero(b + len, 60 - len); len = 60; }
    tx[i].addr = (uint64_t)(uintptr_t)b; tx[i].len = (uint16_t)len; tx[i].cso = 0;
    tx[i].cmd = 0x0B; tx[i].status = 0; tx[i].css = 0; tx[i].special = 0;   /* EOP | IFCS | RS */
    tx_used[i] = 1;
    fence();
    tx_cur = (uint16_t)((i + 1) % NTX);
    wr(0x3818, tx_cur);
    return 0;
}

/* ---------- Konfiguration ---------- */
static uint32_t my_ip, my_mask, my_gw, my_dns;
static const uint8_t bcast[6] = {255, 255, 255, 255, 255, 255};

/* ---------- ARP ---------- */
struct arpe { uint32_t ip; uint8_t mac[6]; int valid; };
static struct arpe arpc[8];
static int arp_next = 0;

static void arp_learn(uint32_t ip, const uint8_t* mac) {
    if (!ip) return;
    for (int i = 0; i < 8; i++) if (arpc[i].valid && arpc[i].ip == ip) { if (mcmp(arpc[i].mac, mac, 6) && !fw_arp(ip, arpc[i].mac, mac)) return; mcopy(arpc[i].mac, mac, 6); return; }
    arpc[arp_next].ip = ip; mcopy(arpc[arp_next].mac, mac, 6); arpc[arp_next].valid = 1;
    arp_next = (arp_next + 1) % 8;
}
static const uint8_t* arp_find(uint32_t ip) {
    for (int i = 0; i < 8; i++) if (arpc[i].valid && arpc[i].ip == ip) return arpc[i].mac;
    return 0;
}

static void eth_send(const uint8_t* dst, uint16_t type, const uint8_t* pl, int len) {
    static uint8_t f[1600];
    mcopy(f, dst, 6); mcopy(f + 6, mymac, 6); put16(f + 12, type);
    mcopy(f + 14, pl, len);
    nic_send(f, 14 + len);
}

static void arp_send(uint16_t op, const uint8_t* tmac, uint32_t tip, const uint8_t* dstmac) {
    uint8_t a[28];
    put16(a, 1); put16(a + 2, 0x0800); a[4] = 6; a[5] = 4; put16(a + 6, op);
    mcopy(a + 8, mymac, 6); put32(a + 14, my_ip);
    if (tmac) mcopy(a + 18, tmac, 6); else mzero(a + 18, 6);
    put32(a + 24, tip);
    eth_send(dstmac, 0x0806, a, 28);
}

void net_poll(void);

static const uint8_t* arp_resolve(uint32_t ip) {
    const uint8_t* m = arp_find(ip);
    if (m) return m;
    for (int versuch = 0; versuch < 4; versuch++) {
        arp_send(1, 0, ip, bcast);
        uint32_t t0 = timer_get_ticks();
        while (timer_get_ticks() - t0 < 25) {
            net_poll();
            m = arp_find(ip);
            if (m) return m;
        }
    }
    return 0;
}

/* ---------- IPv4 ---------- */
static uint16_t ip_id = 1;

static int ip_send(uint32_t dst, uint8_t proto, const uint8_t* pl, int len) {
    uint8_t p[1500];
    if (len > 1480) return -1;
    if (!fw_aus(dst, proto, pl, len)) return -4;
    p[0] = 0x45; p[1] = 0; put16(p + 2, (uint16_t)(20 + len)); put16(p + 4, ip_id++); put16(p + 6, 0);
    p[8] = 64; p[9] = proto; put16(p + 10, 0); put32(p + 12, my_ip); put32(p + 16, dst);
    put16(p + 10, csum(p, 20));
    mcopy(p + 20, pl, len);
    const uint8_t* mac;
    if (dst == 0xFFFFFFFFu) mac = bcast;
    else {
        uint32_t hop = ((dst & my_mask) == (my_ip & my_mask)) ? dst : my_gw;
        if (!hop) return -2;
        mac = arp_resolve(hop);
        if (!mac) return -3;
    }
    eth_send(mac, 0x0800, p, 20 + len);
    return 0;
}

static int udp_send(uint32_t dst, uint16_t sport, uint16_t dport, const uint8_t* pl, int len) {
    uint8_t u[1480];
    if (len > 1472) return -1;
    put16(u, sport); put16(u + 2, dport); put16(u + 4, (uint16_t)(8 + len)); put16(u + 6, 0);
    mcopy(u + 8, pl, len);
    return ip_send(dst, 17, u, 8 + len);
}

/* ---------- ICMP (Ping) ---------- */
static volatile int ping_got; static uint16_t ping_seq; static uint32_t ping_t1, ping_from;

/* ---------- DHCP ---------- */
static volatile int dhcp_type; static uint32_t d_yi, d_mask, d_gw, d_dns, d_srv, d_xid;

static void dhcp_rx(const uint8_t* d, int len) {
    if (len < 240 || d[0] != 2 || get32(d + 4) != d_xid) return;
    if (mcmp(d + 28, mymac, 6)) return;
    uint32_t yi = get32(d + 16), mask = 0, gw = 0, dns = 0, srv = 0; int type = 0;
    const uint8_t* o = d + 240; const uint8_t* e = d + len;
    while (o < e && *o != 255) {
        if (*o == 0) { o++; continue; }
        if (o + 1 >= e || o + 2 + o[1] > e) break;
        uint8_t c = o[0], l = o[1];
        if (c == 53 && l >= 1) type = o[2];
        else if (c == 1 && l >= 4) mask = get32(o + 2);
        else if (c == 3 && l >= 4) gw = get32(o + 2);
        else if (c == 6 && l >= 4) dns = get32(o + 2);
        else if (c == 54 && l >= 4) srv = get32(o + 2);
        o += 2 + l;
    }
    d_yi = yi; d_mask = mask; d_gw = gw; d_dns = dns; d_srv = srv; dhcp_type = type;
}

/* ---------- DNS ---------- */
static volatile int dns_got; static uint32_t dns_ip;
#define DNS_PORT 53053

static void dns_rx(const uint8_t* d, int len) {
    if (len < 12 || get16(d) != 0x5A59 || !(d[2] & 0x80) || (d[3] & 15) != 0) return;
    int qd = get16(d + 4), an = get16(d + 6);
    const uint8_t* p = d + 12; const uint8_t* e = d + len;
    for (int i = 0; i < qd; i++) {                       /* Frage ueberspringen */
        while (p < e && *p) { if ((*p & 0xC0) == 0xC0) { p++; break; } p += *p + 1; }
        p += 1 + 4;
    }
    for (int i = 0; i < an && p + 12 <= e; i++) {
        while (p < e && *p) { if ((*p & 0xC0) == 0xC0) { p++; break; } p += *p + 1; }
        p++;
        if (p + 10 > e) return;
        uint16_t t = get16(p), c = get16(p + 2), rl = get16(p + 8);
        p += 10;
        if (t == 1 && c == 1 && rl == 4 && p + 4 <= e) { dns_ip = get32(p); dns_got = 1; return; }
        p += rl;
    }
}


/* ---------- TCP (ein Client-Anschluss, Stop-and-Wait) ---------- */
#define TCP_RCAP (2u * 1024u * 1024u)
enum { T_CLOSED, T_SYN, T_EST };
static struct {
    int state; uint32_t rip; uint16_t lport, rport;
    uint32_t snd_nxt, snd_una, rcv_nxt;
    int fin, rst; uint32_t rlen;
} tc;
static uint8_t tcp_rbuf[TCP_RCAP];

static void tcp_seg(uint32_t seq, uint8_t flags, const uint8_t* data, int len) {
    uint8_t b[12 + 24 + 1460]; int hl = (flags & 2) ? 24 : 20;
    if (len > 1460) len = 1460;
    put32(b, my_ip); put32(b + 4, tc.rip); b[8] = 0; b[9] = 6; put16(b + 10, (uint16_t)(hl + len));
    uint8_t* t = b + 12;
    put16(t, tc.lport); put16(t + 2, tc.rport); put32(t + 4, seq); put32(t + 8, (flags & 0x10) ? tc.rcv_nxt : 0);
    t[12] = (uint8_t)((hl / 4) << 4); t[13] = flags;
    uint32_t fr = TCP_RCAP - tc.rlen; put16(t + 14, (uint16_t)(fr > 65535 ? 65535 : fr));
    put16(t + 16, 0); put16(t + 18, 0);
    if (flags & 2) { t[20] = 2; t[21] = 4; put16(t + 22, 1460); }
    for (int i = 0; i < len; i++) t[hl + i] = data[i];
    put16(t + 16, csum(b, 12 + hl + len));
    ip_send(tc.rip, 6, t, hl + len);
}

static void tcp_rx(uint32_t src, const uint8_t* s, int n) {
    if (tc.state == T_CLOSED || src != tc.rip || n < 20) return;
    if (get16(s) != tc.rport || get16(s + 2) != tc.lport) return;
    uint32_t seq = get32(s + 4), ack = get32(s + 8); uint8_t fl = s[13];
    int hl = (s[12] >> 4) * 4; if (hl < 20 || hl > n) return;
    const uint8_t* d = s + hl; int dl = n - hl;
    if (fl & 4) { tc.rst = 1; tc.state = T_CLOSED; return; }
    if (tc.state == T_SYN) {
        if ((fl & 0x12) == 0x12 && ack == tc.snd_nxt) {
            tc.rcv_nxt = seq + 1; tc.snd_una = ack; tc.state = T_EST;
            tcp_seg(tc.snd_nxt, 0x10, 0, 0);
        }
        return;
    }
    if (fl & 0x10) { int32_t da = (int32_t)(ack - tc.snd_una); if (da > 0 && (int32_t)(tc.snd_nxt - ack) >= 0) tc.snd_una = ack; }
    int neu = 0;
    if (dl > 0) {
        if (seq == tc.rcv_nxt && tc.rlen + (uint32_t)dl <= TCP_RCAP) {
            for (int i = 0; i < dl; i++) tcp_rbuf[tc.rlen + i] = d[i];
            tc.rlen += (uint32_t)dl; tc.rcv_nxt += (uint32_t)dl; neu = 1;
        }
        tcp_seg(tc.snd_nxt, 0x10, 0, 0);                  /* ACK (bei Luecke: doppelt, loest Wiederholung aus) */
    }
    if ((fl & 1) && seq + (uint32_t)dl == tc.rcv_nxt && !tc.fin) {
        tc.fin = 1; tc.rcv_nxt++; tcp_seg(tc.snd_nxt, 0x10, 0, 0);
    }
    (void)neu;
}

static int tcp_connect(uint32_t ip, uint16_t port) {
    mzero(&tc, sizeof(tc));
    tc.rip = ip; tc.rport = port; tc.lport = (uint16_t)(40000 + (timer_get_ticks() % 20000));
    uint32_t iss = timer_get_ticks() * 64013u + 12345u;
    tc.state = T_SYN;
    for (int v = 0; v < 4; v++) {
        tc.snd_nxt = iss; tcp_seg(iss, 0x02, 0, 0); tc.snd_nxt = iss + 1;
        uint32_t t0 = timer_get_ticks();
        while (timer_get_ticks() - t0 < 100 && tc.state == T_SYN) net_poll();
        if (tc.state == T_EST) return 0;
        if (tc.rst || tc.state == T_CLOSED) break;
    }
    tc.state = T_CLOSED;
    return -1;
}

static int tcp_write(const uint8_t* data, int len) {
    int off = 0;
    while (off < len) {
        int n = len - off > 1200 ? 1200 : len - off;
        uint32_t seq = tc.snd_nxt, ende = seq + (uint32_t)n; int ok = 0;
        for (int v = 0; v < 4 && !ok; v++) {
            tcp_seg(seq, 0x18, data + off, n);
            tc.snd_nxt = ende;
            uint32_t t0 = timer_get_ticks();
            while (timer_get_ticks() - t0 < 100 && tc.snd_una != ende && tc.state == T_EST) net_poll();
            if (tc.state != T_EST) return -1;
            ok = (tc.snd_una == ende);
        }
        if (!ok) return -2;
        off += n;
    }
    return 0;
}

static void tcp_close(void) {
    if (tc.state == T_EST) {
        tcp_seg(tc.snd_nxt, 0x11, 0, 0); tc.snd_nxt++;
        uint32_t t0 = timer_get_ticks();
        while (timer_get_ticks() - t0 < 50 && tc.snd_una != tc.snd_nxt) net_poll();
    }
    tc.state = T_CLOSED;
}

static int http_zeigen = 1; static uint32_t http_body, http_status;

int tcp_open(uint32_t ip, uint16_t port) { return tcp_connect(ip, port); }
int tcp_put(const uint8_t* d, int n) { return tcp_write(d, n); }
uint32_t tcp_have(void) { return tc.rlen; }
uint8_t* tcp_data(void) { return tcp_rbuf; }
void tcp_drop(uint32_t n) { if (n > tc.rlen) n = tc.rlen; for (uint32_t i = 0; i + n < tc.rlen; i++) tcp_rbuf[i] = tcp_rbuf[i + n]; tc.rlen -= n; }
int tcp_eof(void) { return tc.fin || tc.state != T_EST; }
void tcp_shut(void) { tcp_close(); }

#include "tls.h"
static const uint8_t* http_rb; static uint32_t http_len; static int http_complete;

int net_http(const char* url) { return net_http_fp(url, 0); }

int net_http_fp(const char* url, const char* fp) {
    __asm__ volatile ("sti");           /* Shell laeuft evtl. im PS/2-Interrupt: Zeit muss weiterlaufen */
    if (!net_up()) { kprint("Kein Netz (Befehl net / dhcp)\n"); return -1; }
    int tls = 0;
    if (url[0]=='h' && url[1]=='t' && url[2]=='t' && url[3]=='p' && url[4]==':' && url[5]=='/' && url[6]=='/') url += 7;
    else if (url[0]=='h' && url[1]=='t' && url[2]=='t' && url[3]=='p' && url[4]=='s' && url[5]==':' && url[6]=='/' && url[7]=='/') { url += 8; tls = 1; }
    char host[64]; int hn = 0; uint16_t port = tls ? 443 : 80;
    while (*url && *url != '/' && *url != ':' && hn < 63) host[hn++] = *url++;
    host[hn] = 0;
    if (*url == ':') { url++; uint32_t p = 0; while (*url >= '0' && *url <= '9') p = p * 10 + (uint32_t)(*url++ - '0'); if (p && p < 65536) port = (uint16_t)p; }
    const char* path = *url ? url : "/";
    uint32_t ip;
    if (net_parse_ip(host, &ip) != 0) {
        if (net_dns(host, &ip) != 0) { kprint("Name nicht aufloesbar\n"); return -2; }
    }
    kprint(tls ? "Verbinde (TLS) mit " : "Verbinde mit "); kprint_ip(ip); kputchar(':'); kprint_dec(port); kprint(" ...\n");
    static char rq[1000]; int n = 0;
    const char* teile[] = { "GET ", path, " HTTP/1.0\r\nHost: ", host, "\r\nUser-Agent: Mozilla/5.0 (X11; ZynexOS x86_64) ZynexBrowser/1.0\r\nAccept: text/html,application/xhtml+xml,image/png,image/jpeg,image/gif,*/*;q=0.5\r\nAccept-Language: de-CH,de;q=0.9,en;q=0.7\r\nConnection: close\r\n\r\n" };
    for (int i = 0; i < 5; i++) for (const char* c = teile[i]; *c && n < 999; c++) rq[n++] = *c;
    if (tls) {
        int r = tls_get(ip, port, host, (const uint8_t*)rq, (uint32_t)n, fp, &http_rb, &http_len, &http_complete);
        if (r != 0) return -3;
    } else {
        if (tcp_connect(ip, port) != 0) { kprint("Verbindung fehlgeschlagen\n"); return -3; }
        if (tcp_write((const uint8_t*)rq, n) != 0) { kprint("Senden fehlgeschlagen\n"); tcp_close(); return -4; }
        uint32_t t0 = timer_get_ticks(), letzte = t0, rl = 0;
        while (!tc.fin && tc.state == T_EST && timer_get_ticks() - t0 < 3000 && timer_get_ticks() - letzte < 300) {
            net_poll();
            if (tc.rlen != rl) { rl = tc.rlen; letzte = timer_get_ticks(); }
            if (tc.rlen >= TCP_RCAP) break;
        }
        tcp_close();
        http_rb = tcp_rbuf; http_len = tc.rlen; http_complete = tc.fin;
    }
    const uint8_t* rb = http_rb;
    uint32_t len = http_len, body = 0;
    for (uint32_t i = 0; i + 3 < len; i++)
        if (rb[i] == '\r' && rb[i+1] == '\n' && rb[i+2] == '\r' && rb[i+3] == '\n') { body = i + 4; break; }
    if (!len) { kprint("Keine Antwort\n"); return -5; }
    for (uint32_t i = 0; i < len && rb[i] != '\r' && rb[i] != '\n' && i < 80; i++) kputchar((char)rb[i]);
    kputchar('\n');
    uint32_t sum = 0; for (uint32_t i = body; i < len; i++) sum += rb[i];
    http_body = body; http_status = 0;
    if (len > 12 && rb[8] == ' ') for (int i = 9; i < 12 && rb[i] >= '0' && rb[i] <= '9'; i++) http_status = http_status * 10 + (uint32_t)(rb[i] - '0');
    if (!http_zeigen) return 0;
    kprint("Inhalt: "); kprint_dec(len - body); kprint(" Bytes, Pruefsumme "); kprint_dec(sum); kputchar('\n');
    uint32_t zeigen = len - body > 600 ? 600 : len - body;
    for (uint32_t i = 0; i < zeigen; i++) { char c = (char)rb[body + i]; if (c == '\n' || (c >= 32 && c < 127)) kputchar(c); }
    kputchar('\n');
    if (!http_complete) kprint("(abgebrochen/unvollstaendig)\n");
    return 0;
}

/* POST ueber TLS (fuer die Programmierschnittstellen externer Sprachmodelle); Antwort bleibt bis zum naechsten Abruf im Empfangspuffer */
int net_post_tls(const char* host, const char* path, const char* hdr, const char* body, const char** ant, uint32_t* alen, uint32_t* status) {
    __asm__ volatile ("sti");
    if (!net_up()) { kprint("Kein Netz\n"); return -1; }
    uint32_t ip; if (net_dns(host, &ip) != 0) { kprint("Name nicht aufloesbar\n"); return -2; }
    static char rq[4800]; int n = 0;
    #define ADD(t) do { for (const char* c_ = (t); *c_ && n < 4790; c_++) rq[n++] = *c_; } while (0)
    int bl = 0; while (body[bl]) bl++;
    char cl[12]; int k = 0; { int v = bl; char t[12]; int c = 0; do { t[c++] = (char)('0' + v % 10); v /= 10; } while (v); while (c) cl[k++] = t[--c]; cl[k] = 0; }
    ADD("POST "); ADD(path); ADD(" HTTP/1.0\r\nHost: "); ADD(host); ADD("\r\nUser-Agent: ZynexOS\r\nContent-Type: application/json\r\nContent-Length: "); ADD(cl); ADD("\r\nConnection: close\r\n"); ADD(hdr); ADD("\r\n"); ADD(body);
    #undef ADD
    int r = tls_get(ip, 443, host, (const uint8_t*)rq, (uint32_t)n, 0, &http_rb, &http_len, &http_complete);
    if (r != 0) return -3;
    const uint8_t* rb = http_rb; uint32_t len = http_len, bo = 0;
    for (uint32_t i = 0; i + 3 < len; i++) if (rb[i] == '\r' && rb[i+1] == '\n' && rb[i+2] == '\r' && rb[i+3] == '\n') { bo = i + 4; break; }
    if (!len || !bo) return -5;
    uint32_t st = 0; if (len > 12 && rb[8] == ' ') for (int i = 9; i < 12 && rb[i] >= '0' && rb[i] <= '9'; i++) st = st * 10 + (uint32_t)(rb[i] - '0');
    *status = st; *ant = (const char*)rb + bo; *alen = len - bo;
    return 0;
}

/* ---------- Empfang ---------- */
static void ip_rx(const uint8_t* f, int len) {
    const uint8_t* ip = f + 14;
    if (len < 34 || (ip[0] >> 4) != 4) return;
    int ihl = (ip[0] & 15) * 4;
    int tl = get16(ip + 2);
    if (ihl < 20 || tl < ihl || 14 + tl > len) return;
    if (get16(ip + 6) & 0x3FFF) return;                  /* Fragmente werden nicht unterstuetzt */
    uint32_t src = get32(ip + 12), dst = get32(ip + 16);
    if (my_ip && dst != my_ip && dst != 0xFFFFFFFFu) return;
    if (!fw_ein(ip, (uint32_t)tl)) return;
    if (my_ip && ((src & my_mask) == (my_ip & my_mask) || src == my_gw)) arp_learn(src, f + 6);
    const uint8_t* pl = ip + ihl; int pn = tl - ihl;
    if (ip[9] == 1 && pn >= 8) {                         /* ICMP */
        if (pl[0] == 8 && my_ip) {                       /* Echo-Anfrage -> Antwort */
            uint8_t r[1480]; if (pn > 1480) return;
            mcopy(r, pl, pn); r[0] = 0; put16(r + 2, 0); put16(r + 2, csum(r, pn));
            ip_send(src, 1, r, pn);
        } else if (pl[0] == 0 && get16(pl + 6) == ping_seq) {
            ping_from = src; ping_t1 = timer_get_ticks(); ping_got = 1;
        }
    } else if (ip[9] == 6) {                             /* TCP */
        tcp_rx(src, pl, pn);
    } else if (ip[9] == 17 && pn >= 8) {                 /* UDP */
        uint16_t dp = get16(pl + 2); int ul = get16(pl + 4);
        if (ul < 8 || ul > pn) return;
        if (dp == 68) dhcp_rx(pl + 8, ul - 8);
        else if (dp == DNS_PORT) dns_rx(pl + 8, ul - 8);
    }
}

static void rx_frame(const uint8_t* f, int len) {
    if (len < 14) return;
    uint16_t t = get16(f + 12);
    if (t == 0x0800) ip_rx(f, len);
    else if (t == 0x0806 && len >= 42 && get16(f + 14 + 6) != 0) {
        const uint8_t* a = f + 14; uint16_t op = get16(a + 6);
        uint32_t sip = get32(a + 14), tip = get32(a + 24);
        if (op == 2) arp_learn(sip, a + 8);
        else if (op == 1 && my_ip && tip == my_ip) { arp_learn(sip, a + 8); arp_send(2, a + 8, sip, a + 8); }
    }
}

void net_poll(void) {
    if (!nic_ok) return;
    if (nic_typ == 4 || nic_typ == 6) {
        if (nic_typ == 4) usbnet_poll(rx_frame); else wlan_poll(rx_frame);
        if (dhcp_wunsch) {                       /* nach dem Einstecken: Adresse holen (laeuft in der Hauptschleife) */
            dhcp_wunsch = 0;
            if (net_dhcp() == 0) { kprint("[NET] DHCP: IP "); kprint_ip(my_ip); kprint(" Router "); kprint_ip(my_gw); kprint("\n"); }
            else kprint("[NET] DHCP ohne Antwort\n");
        }
        return;
    }
    if (nic_typ == 3) { vio_poll(); return; }
    if (nic_typ == 5) { hv_net_poll(rx_frame); return; }
    if (nic_typ == 2) {
        for (int n = 0; n < NRX; n++) {
            struct rtd* d = &rrx[rx_cur];
            if (d->opts1 & RTL_OWN) break;
            uint32_t o = d->opts1; int len = (int)(o & 0x3FFF) - 4;
            if ((o & (RTL_FS | RTL_LS)) == (RTL_FS | RTL_LS) && !(o & (1u << 21)) && len > 0) rx_frame(rxbuf + rx_cur * BUFSZ, len);
            fence();
            d->opts1 = RTL_OWN | (rx_cur == NRX - 1 ? RTL_EOR : 0) | BUFSZ;
            rx_cur = (uint16_t)((rx_cur + 1) % NRX);
        }
        return;
    }
    for (int n = 0; n < NRX; n++) {
        struct rxd* d = &rx[rx_cur];
        if (!(d->status & 1)) break;
        int len = d->len;
        if ((d->status & 2) && !d->err) rx_frame(rxbuf + rx_cur * BUFSZ, len);
        d->status = 0;
        wr(0x2818, rx_cur);
        rx_cur = (uint16_t)((rx_cur + 1) % NRX);
    }
}

/* ---------- DHCP-Ablauf ---------- */
static void dhcp_send(int type) {
    uint8_t m[300]; mzero(m, sizeof(m));
    m[0] = 1; m[1] = 1; m[2] = 6; put32(m + 4, d_xid); put16(m + 10, 0x8000);   /* Broadcast-Antwort */
    mcopy(m + 28, mymac, 6);
    put32(m + 236, 0x63825363);
    uint8_t* o = m + 240;
    *o++ = 53; *o++ = 1; *o++ = (uint8_t)type;
    if (type == 3) {
        *o++ = 50; *o++ = 4; put32(o, d_yi); o += 4;
        *o++ = 54; *o++ = 4; put32(o, d_srv); o += 4;
    }
    *o++ = 55; *o++ = 3; *o++ = 1; *o++ = 3; *o++ = 6;
    *o++ = 255;
    uint32_t keep = my_ip; my_ip = 0;
    udp_send(0xFFFFFFFFu, 68, 67, m, (int)(o - m) < 300 ? 300 : (int)(o - m));
    my_ip = keep;
}

int net_dhcp(void) {
    __asm__ volatile ("sti");           /* Shell laeuft evtl. im PS/2-Interrupt: Zeit muss weiterlaufen */
    if (!nic_ok) return -1;
    my_ip = 0; my_mask = 0; my_gw = 0; my_dns = 0;
    d_xid = 0x5A594E00u ^ timer_get_ticks();
    for (int versuch = 0; versuch < 3; versuch++) {
        dhcp_type = 0; dhcp_send(1);
        uint32_t t0 = timer_get_ticks();
        while (timer_get_ticks() - t0 < 100 && dhcp_type != 2) net_poll();
        if (dhcp_type != 2) continue;
        dhcp_type = 0; dhcp_send(3);
        t0 = timer_get_ticks();
        while (timer_get_ticks() - t0 < 100 && dhcp_type != 5) net_poll();
        if (dhcp_type == 5) {
            my_ip = d_yi; my_mask = d_mask ? d_mask : 0xFFFFFF00u; my_gw = d_gw; my_dns = d_dns;
            return 0;
        }
    }
    return -2;
}

int net_up(void) { return nic_ok && my_ip != 0; }

void net_info(void) {
    if (!nic_ok) { kprint("Keine unterstuetzte Netzwerkkarte gefunden\n"); return; }
    kprint("MAC "); kprint_mac(mymac); kprint(link_up() ? "  Link: ja\n" : "  Link: nein\n");
    if (!my_ip) { kprint("Keine IP-Adresse (Befehl dhcp)\n"); return; }
    kprint("IP "); kprint_ip(my_ip); kprint("  Maske "); kprint_ip(my_mask);
    kprint("\nRouter "); kprint_ip(my_gw); kprint("  DNS "); kprint_ip(my_dns); kprint("\n");
}

int net_parse_ip(const char* s, uint32_t* ip) {
    uint32_t v = 0; int teile = 0;
    while (*s) {
        if (*s < '0' || *s > '9') return -1;
        uint32_t n = 0; int z = 0;
        while (*s >= '0' && *s <= '9') { n = n * 10 + (uint32_t)(*s - '0'); s++; z++; }
        if (!z || n > 255) return -1;
        v = (v << 8) | n; teile++;
        if (*s == '.') s++; else if (*s) return -1;
    }
    if (teile != 4) return -1;
    *ip = v; return 0;
}

int net_dns(const char* name, uint32_t* ip) {
    __asm__ volatile ("sti");           /* Shell laeuft evtl. im PS/2-Interrupt: Zeit muss weiterlaufen */
    if (!net_up() || !my_dns) return -1;
    uint8_t q[300]; int n = 12;
    put16(q, 0x5A59); put16(q + 2, 0x0100); put16(q + 4, 1); put16(q + 6, 0); put16(q + 8, 0); put16(q + 10, 0);
    const char* s = name;
    while (*s) {
        int l = 0; while (s[l] && s[l] != '.') l++;
        if (!l || l > 63 || n + l + 6 > 290) return -2;
        q[n++] = (uint8_t)l; for (int i = 0; i < l; i++) q[n++] = (uint8_t)s[i];
        s += l; if (*s == '.') s++;
    }
    q[n++] = 0; put16(q + n, 1); put16(q + n + 2, 1); n += 4;
    for (int versuch = 0; versuch < 2; versuch++) {
        dns_got = 0;
        if (udp_send(my_dns, DNS_PORT, 53, q, n) != 0) return -3;
        uint32_t t0 = timer_get_ticks();
        while (timer_get_ticks() - t0 < 200 && !dns_got) net_poll();
        if (dns_got) { *ip = dns_ip; return 0; }
    }
    return -4;
}

int net_ping(const char* ziel) {
    __asm__ volatile ("sti");           /* Shell laeuft evtl. im PS/2-Interrupt: Zeit muss weiterlaufen */
    if (!net_up()) { kprint("Kein Netz (Befehl net / dhcp)\n"); return -1; }
    uint32_t ip;
    if (net_parse_ip(ziel, &ip) != 0) {
        if (net_dns(ziel, &ip) != 0) { kprint("Name nicht aufloesbar\n"); return -2; }
        kprint(ziel); kprint(" = "); kprint_ip(ip); kprint("\n");
    }
    int ok = 0;
    for (int i = 1; i <= 4; i++) {
        uint8_t e[40]; mzero(e, sizeof(e));
        e[0] = 8; put16(e + 4, 0x5A59); put16(e + 6, (uint16_t)i);
        for (int k = 8; k < 40; k++) e[k] = (uint8_t)('a' + (k % 23));
        put16(e + 2, csum(e, 40));
        ping_seq = (uint16_t)i; ping_got = 0;
        uint32_t t0 = timer_get_ticks();
        if (ip_send(ip, 1, e, 40) != 0) { kprint("Senden fehlgeschlagen\n"); continue; }
        while (timer_get_ticks() - t0 < 100 && !ping_got) net_poll();
        if (ping_got) {
            kprint("Antwort von "); kprint_ip(ping_from); kprint(": seq="); kprint_dec((uint32_t)i);
            kprint(" zeit="); kprint_dec((ping_t1 - t0) * 10); kprint(" ms\n"); ok++;
        } else { kprint("Zeitueberschreitung seq="); kprint_dec((uint32_t)i); kprint("\n"); }
        uint32_t w = timer_get_ticks(); while (timer_get_ticks() - w < 30) net_poll();
    }
    return ok ? 0 : -3;
}

/* Aufruf durch den USB-Treiber, wenn ein USB-Netzwerkgeraet bereit ist bzw. entfernt wurde */
void net_usb_attach(const uint8_t* mac) {
    if (!usb_aktiv) { pci_typ_prev = nic_typ; pci_ok_prev = nic_ok; mcopy(pci_mac, mymac, 6); }
    usb_aktiv = 1; nic_typ = 4; nic_ok = 1; mcopy(mymac, mac, 6);
    my_ip = 0; my_mask = 0; my_gw = 0; my_dns = 0;
    if (net_fertig) { kprint("[NET] USB-Netzwerk bereit, MAC "); kprint_mac(mymac); kprint("\n"); dhcp_wunsch = 1; }
}
/* Aufruf durch die WLAN-Station nach erfolgreicher Anmeldung bzw. beim Trennen: wie ein USB-Netzwerkgeraet, aber ueber wlan_senden/wlan_poll */
void net_wlan_attach(const uint8_t* mac) { net_usb_attach(mac); nic_typ = 6; }
void net_wlan_detach(void) { net_usb_detach(); }
void net_usb_detach(void) {
    if (!usb_aktiv) return;
    usb_aktiv = 0; nic_typ = pci_typ_prev; nic_ok = pci_ok_prev; mcopy(mymac, pci_mac, 6);
    my_ip = 0; my_mask = 0; my_gw = 0; my_dns = 0; dhcp_wunsch = 0;
    kprint("[NET] USB-Netzwerk getrennt\n");
}

static int nic_start_hv(void) {
    if (!hv_net_bereit()) return -1;
    mcopy(mymac, hv_net_mac(), 6);
    nic_typ = 5; nic_ok = 1;
    return 0;
}

int net_init(void) {
    int r;
    if (usb_aktiv) r = 0;                        /* USB-Netzwerk war schon vor dem Netzstart da: bevorzugen */
    else {
        r = nic_start();
        if (r == 0) nic_typ = 1;
        else if (r == -1) { r = nic_start_rtl(); if (r == -1) r = nic_start_virtio(); if (r == -1) r = nic_start_hv(); }
    }
    if (r == -1) { kprint("[NET] Keine unterstuetzte Netzwerkkarte gefunden\n"); return -1; }
    if (r != 0) { kprint("[NET] Netzwerkkarte konnte nicht gestartet werden\n"); return r; }
    kprint("[NET] Karte bereit, MAC "); kprint_mac(mymac); kprint("\n");
    uint32_t t0 = timer_get_ticks();
    while (!link_up() && timer_get_ticks() - t0 < 200) {}
    if (!link_up()) { kprint("[NET] Kein Link\n"); return -2; }
    net_fertig = 1;
    if (net_dhcp() == 0) { kprint("[NET] DHCP: IP "); kprint_ip(my_ip); kprint(" Router "); kprint_ip(my_gw); kprint("\n"); }
    else kprint("[NET] DHCP ohne Antwort\n");
    return 0;
}

/* Seite laden und im SynFS speichern */
#include "synfs.h"
int net_wget(const char* url, const char* name, const char* fp) {
    http_zeigen = 0;
    int r = net_http_fp(url, fp);
    http_zeigen = 1;
    if (r != 0) return r;
    if (http_status != 200) { kprint("Server antwortet nicht mit 200 (Status "); kprint_dec(http_status); kprint(")\n"); return -6; }
    if (!http_complete) { kprint("Download unvollstaendig, nicht gespeichert\n"); return -7; }
    uint32_t n = http_len - http_body;
    synfs_create(name, SYNFS_FILE);
    int w = synfs_write(name, http_rb + http_body, n);
    if (w < 0) { kprint("Speichern im SynFS fehlgeschlagen\n"); return -8; }
    kprint("Gespeichert: "); kprint(name); kprint(" ("); kprint_dec(n); kprint(" Bytes)\n");
    return 0;
}

/* Seite fuer den Browser: Kopf und Inhalt bleiben bis zum naechsten Abruf im Empfangspuffer */
int net_seite(const char* url, const char** kopf, uint32_t* kopflen, const char** body, uint32_t* len, uint32_t* status) {
    http_zeigen = 0; int r = net_http_fp(url, 0); http_zeigen = 1;
    if (r != 0) return r;
    *status = http_status; *kopf = (const char*)http_rb; *kopflen = http_body; *body = (const char*)http_rb + http_body; *len = http_len - http_body;
    return 0;
}

/* Kurzer Netzstatus fuer die grafische Oberflaeche: 0 = keine Karte, 1 = Karte ohne Adresse, 2 = bereit (Text enthaelt die IP) */
int net_status(char* out, int max) {
    int n = 0;
    #define PUT(c) do { if (n < max - 1) out[n++] = (c); } while (0)
    if (!nic_ok) { out[0] = 0; return 0; }
    if (!my_ip) { out[0] = 0; return 1; }
    for (int i = 3; i >= 0; i--) {
        uint32_t b = (my_ip >> (8 * i)) & 255;
        if (b >= 100) PUT((char)('0' + b / 100));
        if (b >= 10) PUT((char)('0' + (b / 10) % 10));
        PUT((char)('0' + b % 10));
        if (i) PUT('.');
    }
    out[n] = 0;
    #undef PUT
    return 2;
}
