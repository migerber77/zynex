#pragma once
/* AHCI/SATA-Treiber (Polling, ein Befehlsplatz, LBA48-DMA). Meldet "sata0", "sata1" bei der Blockschicht an. */
int ahci_init(void);
