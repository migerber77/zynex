#pragma once
#include <stdint.h>
/*
 * Festplattenverschluesselung fuer SynFS.
 * Jeder Sektor (ausser dem Superblock in Sektor 0) wird als Ganzes (512 Byte) mit einer 4-Runden-Feistel-Chiffre verschluesselt:
 * Rundenfunktion = ChaCha20(HMAC-SHA-256(Rundenschluessel, Sektornummer || Halbblock)). 256-Bit-Schluessel, kein Muster
 * bei gleichen Daten, jede Aenderung eines Bytes veraendert den ganzen Sektor. Grover-fest (256 Bit -> 128 Bit Sicherheit).
 * Der Datenschluessel ist zufaellig und mit dem Passwort (PBKDF2-HMAC-SHA-256) verpackt, das Passwort laesst sich also aendern.
 */
void krypto_sektor(int verschluesseln, const uint8_t rk[4][32], uint32_t lba, uint8_t s[512]);   /* reine Funktion (Test) */
int  krypto_aktiv_fuer(const char* dev);                 /* Schluessel fuer dieses Geraet geladen? */
void krypto_lesen(uint32_t lba, uint8_t* buf, uint32_t count);          /* nach dem Lesen: entschluesseln */
int  krypto_schreibbar(uint32_t lba);                    /* 1 = Sektor wird beim Schreiben verschluesselt */
void krypto_schreiben(uint32_t lba, uint8_t* sektor);    /* einen Sektor in-place verschluesseln (falls noetig) */
int  krypto_einrichten(const char* pw);                  /* laufendes SynFS verschluesseln; 0 = fertig */
int  krypto_fortsetzen(void);                            /* unterbrochene Verschluesselung weiterfuehren */
int  krypto_entsperren(const char* dev, const char* pw); /* 0 ok, -1 falsches Passwort, -2 nicht verschluesselt */
int  krypto_passwort(const char* alt, const char* neu);  /* 0 ok */
void krypto_sperren(void);
void krypto_vergessen(const char* dev);                  /* Schluessel loeschen, falls er zu diesem Geraet gehoert */
void krypto_merke_gesperrt(const char* dev);
const char* krypto_gesperrt_dev(void);                   /* Geraet, das beim Einbinden gesperrt war, sonst "" */
void krypto_status(void);
