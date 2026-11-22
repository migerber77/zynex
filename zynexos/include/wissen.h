#pragma once
/*
 * Wissen: Lernschleife des Zynex-Lernkerns. Frage -> erst im Lernkern nachsehen; unbekannt -> im Internet
 * nachschlagen (Wikipedia, Zusammenfassung der besten Treffer), den passenden Satz in den Lernkern schreiben und
 * antworten. Beim naechsten Mal kommt die Antwort direkt aus dem Lernkern.
 */
#define WISSEN_ENGINE 1
#define WISSEN_WEB 2
int  wissen_frage(const char* frage, const char* sprache, char* out, int max, int* herkunft);   /* Laenge, <0: -1 kein Netz, -2 nichts Passendes gefunden, -3 keine Frage */
int  wissen_merke(const char* frage, const char* antwort);                                       /* von Hand beibringen */
int  wissen_satz(const char* text, const char* frage, char* out, int max);                       /* bester Satz zur Frage; Rueckgabe: Trefferzahl (0 = nichts passt) */
int  wissen_ist_frage(const char* s);
