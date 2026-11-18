#pragma once
#include <stdint.h>

struct pci_dev {
    uint8_t  bus, dev, fn;
    uint16_t vendor, device;
    uint8_t  cls, sub, prog;
};

#define PCI_MAX_DEVS 64

int  pci_scan(void);
int  pci_count(void);
struct pci_dev* pci_get(int i);
struct pci_dev* pci_find_class(uint8_t cls, uint8_t sub, uint8_t prog, int nth);
uint32_t pci_read32(const struct pci_dev* d, uint8_t off);
void     pci_write32(const struct pci_dev* d, uint8_t off, uint32_t v);
uint64_t pci_bar(const struct pci_dev* d, int idx);       /* Basisadresse (Speicher-BAR) */
void     pci_enable(const struct pci_dev* d);             /* Speicher + Bus-Master einschalten */
void     pci_list(void);
