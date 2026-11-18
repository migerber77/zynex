#pragma once
/* Zynex OS Quantum: Startbild und Desktop ohne Apps. Der Benutzer sagt oder schreibt, was er tun moechte (Intent), Zynie versteht es
 * und oeffnet das Passende (z. B. "text erstellen" oeffnet die Textverarbeitung). */
void desktop_splash_start(void);
void desktop_splash(int qubit, int prozent, const char* text);   /* qubit 0..3 leuchtet auf, -1 = keiner */
void desktop_splash_info(const char* text);                         /* Zusatzzeile am unteren Rand des Startbilds (Geraete-Fortschritt) */
void desktop_splash_ende(void);                                    /* Mindestanzeigedauer abwarten */
int  desktop_start(void);                                          /* 0 = Desktop laeuft (Anmeldung, dann Desktop), <0 kein Grafikmodus */
