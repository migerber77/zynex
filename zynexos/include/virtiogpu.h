#pragma once
/* VirtIO-GPU-Treiber (virtio 1.0 ueber PCI, 2D): Anzeige fuer QEMU/KVM/Cloud-VMs, die nur ein virtio-gpu-pci haben.
 * Der Kernel zeichnet in einen eigenen Bildspeicher; der Treiber schickt ihn regelmaessig an den Bildschirm. */
int  virtiogpu_init(void);      /* 0 = aktiv, sonst kein virtio-gpu (oder VGA-Variante, die ohne Treiber geht) */
void virtiogpu_tick(void);      /* hoechstens alle 30 ms: Bildspeicher uebertragen und anzeigen */
int  virtiogpu_aktiv(void);
