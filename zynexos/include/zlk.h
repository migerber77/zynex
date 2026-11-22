#pragma once
#include <stdint.h>
/*
 * Zynex Lernkern (ZLK): eigene, von Transformern unabhaengige Lern-Engine, startet ohne jedes Vorwissen.
 * Prinzip: Bitweise Vorhersage mit Context-Mixing (wie in den staerksten verlustfreien Kompressoren): viele
 * kleine Gedaechtnisse (Zeichen-Kontexte der Ordnung 0-6 und Wort-Kontexte) sagen das naechste Bit voraus, ein
 * lernender Mischer gewichtet sie. Jede Eingabe trainiert sofort (online), ganzzahlig, ohne Gleitkomma.
 * Es kann Text vorhersagen, ergaenzen und (zufaellig) fortsetzen; es versteht nichts, es lernt Muster.
 */
void zlk_init(void);                                   /* auf null zuruecksetzen */
void zlk_lerne(const char* text, int len);             /* trainiert auf dem Text (len<0: bis Endnull) */
int  zlk_ergaenze(const char* anfang, char* out, int max, int temp);   /* haengt Fortsetzung an; temp 0 = wahrscheinlichster Weg, sonst 1..200 (Prozent). Rueckgabe: Laenge */
uint32_t zlk_gelernt(void);                            /* Anzahl gelernter Zeichen */
int  zlk_pruefe(const char* text);                     /* mittlere Bits (x100) je Zeichen, mit denen der Lernkern diesen Text erwartet, ohne zu lernen */
int  zlk_sicherheit(void);                            /* mittlere Bits je Zeichen (x100) der letzten Ergaenzung: klein = sicher gewusst */
int  zlk_kosten(void);                                 /* mittlere Bits je Zeichen der letzten Lerneingabe (x100); 800 = nichts gelernt */
void zlk_speichern(void);                              /* Lernprotokoll im SynFS sichern (falls eingehaengt) */
void zlk_laden(void);                                  /* Lernprotokoll wieder einspielen */
void zlk_info(void);
