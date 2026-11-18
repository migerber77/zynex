/*
 * Zynex OS - PCI(e)-Aufzaehlung
 * Konfigurationszugriff ueber den Standard-Mechanismus 1 (Ports 0xCF8/0xCFC),
 * den jede x86-Plattform kennt. Es werden alle 256 Busse durchsucht.
 */
#include "pci.h"

extern void kprint(const char* str);
extern void kprint_dec(uint32_t val);
extern void kprint_hex(uint32_t val);

static struct pci_dev devs[PCI_MAX_DEVS];
static int ndevs = 0;

static inline void outl(uint16_t p, uint32_t v) { __asm__ volatile ("outl %0, %1" : : "a"(v), "Nd"(p)); }
static inline uint32_t inl(uint16_t p) { uint32_t r; __asm__ volatile ("inl %1, %0" : "=a"(r) : "Nd"(p)); return r; }

static uint32_t cfg_read(uint8_t b, uint8_t d, uint8_t f, uint8_t off) {
    outl(0xCF8, 0x80000000u | ((uint32_t)b << 16) | ((uint32_t)d << 11) | ((uint32_t)f << 8) | (off & 0xFC));
    return inl(0xCFC);
}
static void cfg_write(uint8_t b, uint8_t d, uint8_t f, uint8_t off, uint32_t v) {
    outl(0xCF8, 0x80000000u | ((uint32_t)b << 16) | ((uint32_t)d << 11) | ((uint32_t)f << 8) | (off & 0xFC));
    outl(0xCFC, v);
}

uint32_t pci_read32(const struct pci_dev* d, uint8_t off) { return cfg_read(d->bus, d->dev, d->fn, off); }
void pci_write32(const struct pci_dev* d, uint8_t off, uint32_t v) { cfg_write(d->bus, d->dev, d->fn, off, v); }

int pci_scan(void) {
    ndevs = 0;
    for (int b = 0; b < 256; b++) {
        for (int d = 0; d < 32; d++) {
            uint32_t id = cfg_read((uint8_t)b, (uint8_t)d, 0, 0);
            if ((id & 0xFFFF) == 0xFFFF) continue;
            uint32_t hdr = cfg_read((uint8_t)b, (uint8_t)d, 0, 0x0C);
            int nf = ((hdr >> 16) & 0x80) ? 8 : 1;
            for (int f = 0; f < nf; f++) {
                id = cfg_read((uint8_t)b, (uint8_t)d, (uint8_t)f, 0);
                if ((id & 0xFFFF) == 0xFFFF) continue;
                if (ndevs >= PCI_MAX_DEVS) return ndevs;
                uint32_t cc = cfg_read((uint8_t)b, (uint8_t)d, (uint8_t)f, 0x08);
                struct pci_dev* p = &devs[ndevs++];
                p->bus = (uint8_t)b; p->dev = (uint8_t)d; p->fn = (uint8_t)f;
                p->vendor = (uint16_t)(id & 0xFFFF);
                p->device = (uint16_t)(id >> 16);
                p->cls = (uint8_t)(cc >> 24);
                p->sub = (uint8_t)(cc >> 16);
                p->prog = (uint8_t)(cc >> 8);
            }
        }
    }
    kprint("[PCI] ");
    kprint_dec((uint32_t)ndevs);
    kprint(" Geraete gefunden\n");
    return ndevs;
}

int pci_count(void) { return ndevs; }
struct pci_dev* pci_get(int i) { return (i >= 0 && i < ndevs) ? &devs[i] : 0; }

struct pci_dev* pci_find_class(uint8_t cls, uint8_t sub, uint8_t prog, int nth) {
    for (int i = 0; i < ndevs; i++) {
        if (devs[i].cls == cls && devs[i].sub == sub && devs[i].prog == prog) {
            if (nth-- == 0) return &devs[i];
        }
    }
    return 0;
}

uint64_t pci_bar(const struct pci_dev* d, int idx) {
    uint32_t lo = pci_read32(d, (uint8_t)(0x10 + idx * 4));
    if (lo & 1) return 0;                                  /* I/O-BAR: nicht verwendet */
    uint64_t a = lo & ~0xFULL;
    if (((lo >> 1) & 3) == 2) a |= (uint64_t)pci_read32(d, (uint8_t)(0x14 + idx * 4)) << 32;
    return a;
}

void pci_enable(const struct pci_dev* d) {
    uint32_t cmd = pci_read32(d, 0x04);
    cmd |= 0x06;                                           /* Memory Space + Bus Master */
    cmd &= ~0x400u;                                        /* Interrupts nicht sperren */
    pci_write32(d, 0x04, cmd);
}

extern void kputchar(char c);
static void hexn(uint32_t v, int n) {
    const char* h = "0123456789abcdef";
    for (int i = n - 1; i >= 0; i--) kputchar(h[(v >> (i * 4)) & 0xF]);
}

static const char* klasse(const struct pci_dev* d) {
    if (d->cls == 0x01 && d->sub == 0x08) return "NVMe-Speicher";
    if (d->cls == 0x01 && d->sub == 0x06) return "SATA (AHCI)";
    if (d->cls == 0x01) return "Massenspeicher";
    if (d->cls == 0x02) return "Netzwerk";
    if (d->cls == 0x03) return "Grafik";
    if (d->cls == 0x06) return "Bruecke";
    if (d->cls == 0x0C && d->sub == 0x03 && d->prog == 0x30) return "USB 3 (xHCI)";
    if (d->cls == 0x0C && d->sub == 0x03) return "USB (alt)";
    return "";
}

void pci_list(void) {
    for (int i = 0; i < ndevs; i++) {
        const struct pci_dev* d = &devs[i];
        kprint("  "); hexn(d->bus, 2); kprint(":"); hexn(d->dev, 2); kprint(".");
        kprint_dec(d->fn); kprint("  ");
        hexn(d->vendor, 4); kprint(":"); hexn(d->device, 4);
        kprint("  "); kprint(klasse(d)); kprint("\n");
    }
}
