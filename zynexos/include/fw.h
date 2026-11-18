#pragma once
#include <stdint.h>
/*
 * Firewall mit lokaler KI-Erkennung (keine Cloud, kein Modell noetig):
 *  - Regeln (erlauben/blockieren nach Adresse, Protokoll, Port), erste passende gewinnt
 *  - Standard: ungefragte Verbindungen von aussen werden verworfen, eigene Antworten kommen durch
 *  - Anomalie-Bewertung je Absender (Portscan, SYN-/ICMP-/UDP-Flut, kaputte Pakete) -> Punkte -> automatische Quarantaene
 *  - ARP-Faelschung (Gateway wechselt die MAC-Adresse) wird abgewehrt
 *  - Modus "streng": ausgehend nur an erlaubte Ziele (Schutz davor, dass ein Agent Daten nach draussen schickt)
 */
enum { FW_AUS = 0, FW_NORMAL = 1, FW_STRENG = 2 };
enum { FW_EIN = 1, FW_AUSG = 2, FW_BEIDE = 3 };

int  fw_ein(const uint8_t* ip, uint32_t laenge);                          /* IPv4-Paket von aussen: 1 durchlassen, 0 verwerfen */
int  fw_aus(uint32_t ziel, uint8_t proto, const uint8_t* pl, int laenge);  /* eigenes Paket: 1 senden, 0 verwerfen */
int  fw_arp(uint32_t ip, const uint8_t* alt_mac, const uint8_t* neu_mac);  /* Eintrag ueberschreiben? 1 ja, 0 nein */
void fw_modus(int m);
int  fw_modus_get(void);
int  fw_regel(int richtung, int erlauben, uint32_t ip, uint32_t maske, uint8_t proto, uint16_t port);   /* 0 ok */
int  fw_regeln_anzahl(void);
void fw_regeln_loeschen(void);
void fw_freigeben(uint32_t ip);                                           /* Quarantaene aufheben (0 = alle) */
void fw_status(void);
void fw_log_zeigen(void);
void fw_regeln_zeigen(void);
void fw_speichern(void);
void fw_laden(void);
void fw_befehl(const char* arg);                                          /* Shell: Text hinter "firewall" */
uint32_t fw_verworfen(void);
