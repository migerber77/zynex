/*
 * Zynex OS - Hardware-Uebersicht: welche PCI-Geraete gibt es, und welcher Treiber (Standard-Protokoll) bedient sie.
 * Die Treiber selbst liegen in nvme.c, ahci.c, usb.c (xHCI), net.c (e1000/RTL/virtio), hda.c, wifi.c.
 * Firmware-Blobs (WLAN u.a.) liegen im SynFS unter dem Namen  firmware/pci-VVVV-DDDD  (Hersteller-/Geraete-ID, hex, klein).
 */
#include "pci.h"
#include "synfs.h"

extern void kprint(const char* str);
extern void kprint_dec(uint32_t val);
extern void kputchar(char c);

static void hx(uint32_t v, int n) { const char* h = "0123456789abcdef"; for (int i = n - 1; i >= 0; i--) kputchar(h[(v >> (i * 4)) & 0xF]); }

struct hw_zeile { const char* protokoll; const char* treiber; int ja; };

static struct hw_zeile bewerte(const struct pci_dev* d) {
    struct hw_zeile z = { "Systemgeraet", "kein Treiber noetig", 1 };
    if (d->cls == 0x01 && d->sub == 0x08) { z.protokoll = "NVMe (PCIe)"; z.treiber = "nvme"; return z; }
    if (d->cls == 0x01 && d->sub == 0x06 && d->prog == 0x01) { z.protokoll = "AHCI (SATA)"; z.treiber = "ahci"; return z; }
    if (d->cls == 0x01 && d->vendor == 0x1AF4) { z.protokoll = "VirtIO-Blk"; z.treiber = "virtioblk (modern, vblk0)"; return z; }
    if (d->cls == 0x01) { z.protokoll = "Massenspeicher (IDE/RAID/anderes)"; z.treiber = "keiner (nur NVMe, AHCI)"; z.ja = 0; return z; }
    if (d->cls == 0x0C && d->sub == 0x03 && d->prog == 0x30) { z.protokoll = "xHCI (USB 3)"; z.treiber = "usb (HID-Tastatur/Maus, Speicher)"; return z; }
    if (d->cls == 0x0C && d->sub == 0x03) { z.protokoll = "USB (EHCI/OHCI/UHCI)"; z.treiber = "keiner (nur xHCI)"; z.ja = 0; return z; }
    if (d->cls == 0x02 && d->sub == 0x00) {
        if (d->vendor == 0x8086) { z.protokoll = "Ethernet (Intel e1000-Familie)"; z.treiber = "net (e1000)"; return z; }
        if (d->vendor == 0x10EC) { z.protokoll = "Ethernet (Realtek RTL8168)"; z.treiber = "net (rtl)"; return z; }
        if (d->vendor == 0x1AF4) { z.protokoll = "VirtIO-Net"; z.treiber = "net (virtio)"; return z; }
        if (d->vendor == 0x1414) { z.protokoll = "Hyper-V Netzwerk"; z.treiber = "net (hv)"; return z; }
        z.protokoll = "Ethernet (anderer Chip)"; z.treiber = "keiner (e1000, RTL8168, VirtIO)"; z.ja = 0; return z;
    }
    if ((d->cls == 0x02 && d->sub == 0x80) || d->cls == 0x0D) { z.protokoll = "WLAN"; z.treiber = "wifi (Treiberrahmen, Firmware aus SynFS)"; return z; }
    if (d->cls == 0x03) { z.protokoll = "Grafik"; z.treiber = "UEFI-Framebuffer (GOP), keine 3D-Beschleunigung"; return z; }
    if (d->cls == 0x04 && d->sub == 0x03) { z.protokoll = "HD-Audio"; z.treiber = "hda"; return z; }
    if (d->cls == 0x06) { z.protokoll = "PCI-Bruecke"; z.treiber = "kein Treiber noetig"; return z; }
    z.protokoll = "anderes Geraet"; z.treiber = "keiner"; z.ja = 0; return z;
}

static int firmware_da(const struct pci_dev* d) {
    char n[40] = "firmware/pci-"; int k = 13; const char* h = "0123456789abcdef";
    for (int i = 3; i >= 0; i--) n[k++] = h[(d->vendor >> (i * 4)) & 0xF];
    n[k++] = '-';
    for (int i = 3; i >= 0; i--) n[k++] = h[(d->device >> (i * 4)) & 0xF];
    n[k] = 0;
    if (!synfs_is_mounted()) return -1;
    uint8_t b[4]; return synfs_read(n, b, 4) >= 0 ? 1 : 0;
}

void hardware_info(void) {
    pci_scan();
    kprint("Hardware-Uebersicht (PCI-Aufzaehlung, Standard-Protokolle):\n");
    int ohne = 0;
    for (int i = 0; i < pci_count(); i++) {
        const struct pci_dev* d = pci_get(i); struct hw_zeile z = bewerte(d);
        if (d->cls == 0x06 && d->sub != 0x80) continue;              /* Bruecken nicht einzeln zeigen */
        kprint("  "); hx(d->vendor, 4); kprint(":"); hx(d->device, 4); kprint("  "); kprint(z.protokoll); kprint(" -> ");
        kprint(z.ja ? "Treiber: " : "NICHT unterstuetzt: "); kprint(z.treiber);
        int f = (d->cls == 0x02 && d->sub == 0x80) || d->cls == 0x0D ? firmware_da(d) : -1;
        if (f == 1) kprint("  [Firmware im SynFS]"); else if (f == 0) kprint("  [Firmware fehlt: firmware/pci-VVVV-DDDD]");
        kprint("\n"); if (!z.ja) ohne++;
    }
    kprint("  "); kprint_dec((uint32_t)ohne); kprint(" Geraet(e) ohne Treiber. Unterstuetzt: NVMe, AHCI, xHCI, e1000/RTL8168/VirtIO-Net, HD-Audio, WLAN-Rahmen.\n");
}
