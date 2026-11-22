#pragma once
#include <stdint.h>
/*
 * Zynex Lernkern (ZLK): eigene, von Transformern unabhaengige Lern-Engine, startet ohne jedes Vorwissen.
 * Prinzip: Bitweise Vorhersage mit Context-Mixing (wie in den staerksten verlustfreien Kompressoren): viele
 * kleine Gedaechtnisse (Zeichen-Kontexte der Ordnung 0-6 und Wort-Kontexte) sagen das naechste Bit voraus, ein
 * lernender Mischer gewichtet sie. Jede Eingabe trainiert sofort (online), ganzzahlig, ohne Gleitkomma.
 * Es kann Text vorhersagen, ergaenzen und (zufaellig) fortsetzen; es versteht nichts, es lernt Muster.
 */
void zlk_init(void);                                   /* nur Start/Laden; Anwender koennen das Modell nicht zuruecksetzen */
void zlk_lerne(const char* text, int len);             /* trainiert auf dem Text (len<0: bis Endnull) */
int  zlk_ergaenze(const char* anfang, char* out, int max, int temp);   /* haengt Fortsetzung an; temp 0 = wahrscheinlichster Weg, sonst 1..200 (Prozent). Rueckgabe: Laenge */
uint32_t zlk_gelernt(void);                            /* Anzahl gelernter Zeichen */
int  zlk_pruefe(const char* text);                     /* mittlere Bits (x100) je Zeichen, mit denen der Lernkern diesen Text erwartet, ohne zu lernen */
int  zlk_sicherheit(void);                            /* mittlere Bits je Zeichen (x100) der letzten Ergaenzung: klein = sicher gewusst */
int  zlk_kosten(void);                                 /* mittlere Bits je Zeichen der letzten Lerneingabe (x100); 800 = nichts gelernt */
int  zlk_speichern(void);                              /* Modell als modell.zyki im SynFS sichern (falls eingehaengt) */
void zlk_laden(void);                                  /* modell.zyki wieder einspielen */
#include "nn.h"
#define ZLK_ZYKI_MAX (65536 + 16 + 8 + 2 * NN_PARAMS + 16)   /* groesste moegliche .zyki-Datei */
int  zlk_zyki_bauen(uint8_t* out, int max);            /* Modell als .zyki-Bytes; Laenge oder <0 */
int  zlk_zyki_einlesen(const uint8_t* in, int len, int zusammenfuehren);   /* 1 = mit dem eigenen Modell zusammenfuehren; <0 ungueltig */
void zlk_loeschen(void);                               /* Modell samt Datei entfernen: nur durch einen Administrator */
void zlk_info(void);
