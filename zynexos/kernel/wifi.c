/*
 * Zynex OS - WLAN-Rahmen (Treiberverwaltung). Siehe include/wifi.h.
 * Es ist noch kein Chip-Treiber enthalten: wifi_info() zeigt die Kennungen der gefundenen Funkkarten,
 * damit der passende Treiber gezielt geschrieben werden kann.
 */
#include "wifi.h"
#include "synfs.h"
#include "kheap.h"

extern void kprint(const char* str);
extern void kprint_dec(uint32_t val);
extern void kprint_hex(uint32_t val);

#define MAX_DRV 8
#define FW_MAX (4u * 1024 * 1024)
static const struct wifi_drv* drv[MAX_DRV]; static int ndrv;
struct fund { struct pci_dev* d; const struct wifi_drv* t; int zustand; };   /* 0 kein Treiber, 1 Firmware fehlt, 2 probe fehlgeschlagen, 3 laeuft */
static struct fund funde[8]; static int nfunde;
static const struct wifi_drv* aktiv;

int wifi_register(const struct wifi_drv* d) { if (ndrv >= MAX_DRV) return -1; drv[ndrv++] = d; return 0; }
int wifi_aktiv(void) { return aktiv != 0; }

static int ist_funkkarte(const struct pci_dev* d) {
    if (d->cls == 0x02 && d->sub == 0x80) return 1;                         /* Netzwerk, anderes: WLAN */
    if (d->cls == 0x0D) return 1;                                           /* drahtlose Controller */
    return 0;
}
static const struct wifi_drv* passend(const struct pci_dev* d) {
    for (int i = 0; i < ndrv; i++) for (int k = 0; k < drv[i]->nids; k++)
        if (drv[i]->ids[k].vendor == d->vendor && drv[i]->ids[k].device == d->device) return drv[i];
    return 0;
}

void wifi_init(void) {
    nfunde = 0; aktiv = 0;
    for (int i = 0; i < pci_count() && nfunde < 8; i++) {
        struct pci_dev* d = pci_get(i);
        if (!d || !ist_funkkarte(d)) continue;
        struct fund* f = &funde[nfunde++]; f->d = d; f->t = passend(d); f->zustand = 0;
        if (!f->t) continue;
        uint8_t* fw = 0; int fwlen = 0;
        if (f->t->firmware) {
            fw = (uint8_t*)kmalloc(FW_MAX);
            fwlen = fw ? synfs_read(f->t->firmware, fw, FW_MAX) : -1;
            if (fwlen <= 0) { f->zustand = 1; continue; }
        }
        pci_enable(d);
        if (f->t->probe(d, fw, (uint32_t)fwlen) != 0) { f->zustand = 2; continue; }
        f->zustand = 3; if (!aktiv) aktiv = f->t;
    }
}

static void hex4(uint16_t v) { const char* h = "0123456789abcdef"; char t[5] = { h[v >> 12], h[(v >> 8) & 15], h[(v >> 4) & 15], h[v & 15], 0 }; kprint(t); }

void wifi_info(void) {
    wifi_init();
    if (!nfunde) { kprint("Keine WLAN-Karte am PCI-Bus gefunden (USB-WLAN wird noch nicht gesucht).\n"); return; }
    for (int i = 0; i < nfunde; i++) {
        struct fund* f = &funde[i];
        kprint("WLAN-Karte "); hex4(f->d->vendor); kprint(":"); hex4(f->d->device); kprint(" (Bus "); kprint_dec(f->d->bus); kprint(":"); kprint_dec(f->d->dev); kprint(".");
        kprint_dec(f->d->fn); kprint(") - ");
        if (f->zustand == 0) kprint("noch kein Treiber\n");
        else if (f->zustand == 1) { kprint("Treiber "); kprint(f->t->name); kprint(", Firmware fehlt: "); kprint(f->t->firmware); kprint("\n"); }
        else if (f->zustand == 2) { kprint("Treiber "); kprint(f->t->name); kprint(", Start fehlgeschlagen\n"); }
        else { kprint("Treiber "); kprint(f->t->name); kprint(" laeuft\n"); }
    }
}
