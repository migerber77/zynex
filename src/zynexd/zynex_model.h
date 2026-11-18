/* Schnittstelle zum Sprachmodell.
 * Heute: ein einfacher Platzhalter mit Stichworten.
 * Spaeter: llama.cpp mit einem austauschbaren 3B-Modell hinter derselben Funktion. */
#ifndef ZYNEX_MODEL_H
#define ZYNEX_MODEL_H

#include <stddef.h>

/* Erkennt die Absicht eines Satzes und schreibt den Aktionsnamen (z.B.
 * "termine.zeigen") nach aktion. Rueckgabe 1 bei Treffer, sonst 0. */
int zx_model_intent(const char *text, char *aktion, size_t cap);

/* System-Anweisung fuer das Sprachmodell (Deutsch, kurz, Aktionen nur aus der Liste). */
const char *zx_model_system(void);

/* Zerlegt eine Modellantwort: schneidet eine Zeile "AKTION: name" ab (reply wird gekuerzt,
 * Zeilenumbrueche werden zu Leerzeichen) und schreibt den Namen nach aktion.
 * Rueckgabe 1, wenn eine gueltige Aktion gefunden wurde. */
int zx_model_split(char *reply, char *aktion, size_t cap);

#endif
