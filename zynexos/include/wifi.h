#pragma once
#include <stdint.h>
#include "pci.h"
/*
 * WLAN-Rahmen: Bus-Scan (PCI) -> Kennung vendor:device -> passender Treiber -> Firmware aus SynFS (z. B. fw/iwl9560.ucode)
 * -> Treiber-probe() -> Netzwerkgeraet. Jeder Chip-Treiber meldet sich mit einer wifi_drv-Beschreibung an.
 */
struct wifi_id { uint16_t vendor, device; };

struct wifi_drv {
    const char*           name;                              /* z. B. "iwl9000" */
    const struct wifi_id* ids;                               /* unterstuetzte Kennungen */
    int                   nids;
    const char*           firmware;                          /* SynFS-Dateiname (max. 31 Zeichen), 0 = keine Firmware noetig */
    int  (*probe)(struct pci_dev* dev, const uint8_t* fw, uint32_t fwlen);   /* Chip starten; 0 = ok */
    int  (*scan)(void);                                      /* Netze suchen (Ergebnis ueber wifi_ap_melden) */
    int  (*connect)(const char* ssid, const char* psk);      /* verbinden (WPA2-PSK) */
    int  (*send)(const uint8_t* frame, int len);             /* Ethernet-Rahmen senden */
    int  (*poll)(uint8_t* out, int max);                     /* Ethernet-Rahmen holen, Laenge oder 0 */
    int  (*link)(void);                                      /* 1 = verbunden */
};

int  wifi_register(const struct wifi_drv* drv);
void wifi_init(void);                    /* PCI durchsuchen, Treiber zuordnen, Firmware laden, probe() */
void wifi_info(void);                    /* Befehl "wifi": gefundene Funkkarten und Zustand */
int  wifi_aktiv(void);                   /* 1 = ein Treiber laeuft */
