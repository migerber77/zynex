#pragma once
#include <stdint.h>
/*
 * WLAN-Station (chip-unabhaengig): Netze suchen, Anmelden (offen oder WPA2-PSK/CCMP), 4-Wege-Handshake, Gruppenschluessel,
 * Verschluesselung der Datenrahmen in Software, Umsetzung zwischen 802.11- und Ethernet-Rahmen.
 * Ein Chip-Treiber (USB-Stick oder PCI-Karte) meldet sich mit einer wlan_hw-Beschreibung an und liefert/nimmt rohe 802.11-Rahmen
 * (ohne Funkkopf und ohne FCS; Entschluesselung im Chip abgeschaltet). Nur 2,4 GHz, Kanaele 1-13.
 */
struct wlan_hw {
    const char* name;
    uint8_t     mac[6];
    int (*set_channel)(int ch);                         /* 0 = ok */
    int (*tx)(const uint8_t* frame, int len);           /* rohen 802.11-Rahmen senden, 0 = ok */
    int (*rx)(uint8_t* out, int max, int* rssi);        /* naechsten empfangenen Rahmen holen, Laenge oder 0 */
};

int  wlan_attach(const struct wlan_hw* hw);             /* Treiber meldet sich an */
void wlan_detach(void);
int  wlan_vorhanden(void);
int  wlan_suchen(void);                                 /* alle Kanaele absuchen und die Netze auflisten; Anzahl */
int  wlan_verbinden(const char* ssid, const char* pass);   /* 0 = verbunden (pass leer = offenes Netz) */
void wlan_trennen(void);
int  wlan_verbunden(void);
int  wlan_senden(const uint8_t* eth, int len);          /* Ethernet-Rahmen senden */
void wlan_poll(void (*cb)(const uint8_t* eth, int len));   /* Rahmen holen, Handshake bearbeiten, Ethernet-Rahmen an cb */
void wlan_befehl(const char* arg);                      /* Shell: wlan suchen | verbinden <ssid> [passwort] | trennen | status */
int  wlan_selbsttest(void);                             /* simulierter Zugangspunkt: Suche, Anmeldung, Handshake, Daten; 0 = ok */
