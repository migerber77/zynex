#pragma once
#include <stdint.h>
/* Hyper-V (Generation 2): Tastatur und Maus kommen nur ueber VMBus. Auf anderer Hardware tut der Treiber nichts. */
int         hv_init(void);                 /* 1 = laeuft unter Hyper-V (Eingabegeraete werden versucht), 0 = nicht Hyper-V */
void        hv_poll(void);                 /* regelmaessig aufrufen (aus usb_poll) */
const char* hv_status(void);               /* Statuszeile fuer den Bildschirm, "" ausserhalb von Hyper-V */
void        hv_status_zeichnen(void);      /* Statuszeile unten links zeichnen (auf Grafikbildschirmen) */
void        hv_status_aus(void);           /* nicht mehr zeichnen (Text-Konsole) */
int         hv_aktiv(void);                /* 1 = Hyper-V erkannt: Hauptschleife darf nicht mit hlt schlafen (kein Zeitgeber-Interrupt garantiert) */
/* Hyper-V-Netzwerkkarte (netvsc) */
int            hv_net_bereit(void);
const uint8_t* hv_net_mac(void);
int            hv_net_senden(const uint8_t* rahmen, int laenge);
void           hv_net_poll(void (*rx)(const uint8_t* rahmen, int laenge));
