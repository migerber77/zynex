#pragma once
#include <stdint.h>

/* xHCI-USB mit HID (Tastatur, Maus, Touch/Tablet). Nur Geraete direkt an den Root-Ports (kein Hub). */
int  usb_init(void);          /* 0 = Controller gefunden und gestartet */
void usb_poll(void);          /* Ereignisse abholen (regelmaessig aufrufen) */
void usb_list(void);
int  usb_has_keyboard(void);

/* Zeigerzustand (Bildschirmkoordinaten) */
struct usb_pointer { int x, y; uint8_t buttons; uint8_t down; uint8_t present; uint32_t events; int wheel; };   /* wheel: aufgelaufene Mausrad-Raster (+ = nach oben/vorn), siehe usb_wheel_take */
int  usb_wheel_take(void);                      /* Mausrad-Raster seit dem letzten Aufruf abholen */
void usb_wheel_add(int d);                      /* vom Maustreiber (USB, Hyper-V) */
const struct usb_pointer* usb_pointer_state(void);

/* vom Bildschirmtreiber (kernel.c) */
void fb_cursor_move(int x, int y);
void fb_get_size(int* w, int* h);

/* USB-Netzwerk (CDC-ECM, RNDIS: USB-Tethering, USB-Ethernet-Adapter) und Hot-Plug */
void usb_hotplug(void);                 /* aus der Hauptschleife aufrufen: neue/entfernte Geraete verarbeiten */
void usb_net_prefer(int rndis);         /* 0 = ECM bevorzugen, 1 = RNDIS; nimmt das Netzwerkgeraet neu auf */
int  usbnet_present(void);

char usb_key_char(uint8_t k, int shift, int altgr);   /* HID-Tastennummer -> Zeichen (aktuelles Layout) */
void usb_ps2_mouse(int dx, int dy, uint8_t buttons);  /* aus dem PS/2-Mausinterrupt */

void usb_set_layout(int id);   /* Nummer der Tastaturbelegung (layouts.h), 0 = Schweiz (Standard) */
char usb_key_char(uint8_t k, int shift, int altgr);
extern int usb_ctrl;                                           /* Strg gedrueckt (fuer Kurztasten im Desktop) */
void usb_key_event(uint8_t k, int shift, int altgr);            /* Tastenereignis von USB/PS2 */
void usb_set_rawgrab(void (*fn)(uint8_t k, int shift, int altgr));         /* Roh-Tastennummern abgreifen (0 = aus) */
int  usb_get_layout(void);

/* USB-Audio (UAC1, PCM 16 Bit, isochron): Headset, USB-Mikrofon, USB-Lautsprecher. Ausgabe 48 kHz stereo-Rahmen (Mono-Geraete bekommen den Mittelwert), Aufnahme Mono in der Geraetefrequenz. */
int         usbaud_hat_ausgabe(void);
int         usbaud_hat_eingabe(void);
const char* usbaud_info(void);
int         usbaud_spielen_strom(uint32_t (*fill)(int16_t* stereo, uint32_t frames, void* ctx), void* ctx);   /* 0 = ok */
int         usbaud_aufnehmen(int16_t* mono, uint32_t max_frames, uint32_t* rate);                             /* Anzahl Werte, < 0 Fehler */
int         usbaud_fehler(void);                                                                              /* Pakete mit Fehler/Unterlauf seit dem letzten Start */
