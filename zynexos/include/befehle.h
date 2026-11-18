#pragma once
/* Moderne Befehlssprache der Zynex-Shell: "bereich aktion ziel" (Deutsch oder Englisch), mit Tab-Vervollstaendigung und "Meintest du ...?".
 * Die alten Befehle (synls, whoami, ...) laufen weiter; neue Befehle werden in sie uebersetzt. */
int  befehl_uebersetze(const char* ein, char* aus, int max);               /* 1 = uebersetzt (mehrere Befehle durch ';' getrennt) */
void befehl_hilfe(const char* thema);
int  befehl_tab(char* buf, int* pos, int max, char* liste, int listmax);   /* Anzahl Kandidaten; Buffer wird erweitert */
const char* befehl_vorschlag(const char* ein, char* out, int max);         /* aehnlichster Befehl oder 0 */
int  befehl_bereich(const char* ein);                                      /* 1 = erstes Wort ist ein Bereich */
