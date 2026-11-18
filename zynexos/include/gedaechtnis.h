#pragma once
/*
 * Synapsen-Gedaechtnis: Zynies Langzeitgedaechtnis als kleines Netz aus Neuronen und Synapsen.
 *  - Jedes Wort (auf den Stamm gekuerzt) ist ein Neuron; Woerter, die zusammen vorkommen, werden durch eine Synapse verbunden
 *    (Hebb: "was gemeinsam feuert, verdrahtet sich"). Je oefter, desto staerker - bis zur Saettigung.
 *  - Erinnerungen (Saetze, die du gesagt hast, oder "merk dir ...") haengen an ihren Neuronen.
 *  - Abruf durch Aktivierungsausbreitung: die Woerter deiner Frage feuern, die Erregung laeuft ueber starke Synapsen weiter,
 *    die am staerksten erregten Erinnerungen kommen zurueck - und werden dabei selbst staerker (Langzeitverstaerkung).
 *  - Vergessen: Synapsen und unwichtige Erinnerungen verblassen mit der Zeit; was oft gebraucht oder wichtig ist, bleibt.
 *    "Schlafen" wiederholt die wichtigen Erinnerungen und raeumt auf (Konsolidierung).
 *  - Arbeitsgedaechtnis: die letzten Fragen und Antworten, damit Folgefragen verstanden werden.
 * Alles liegt verschluesselt in ZynDB (ged.*). Es ist kein trainiertes neuronales Netz, sondern ein lernendes Assoziationsnetz.
 */
void ged_init(void);                                                      /* aus ZynDB laden (macht jede Funktion bei Bedarf selbst) */
int  ged_merke(const char* text, int wichtig);                            /* "merk dir ...": 0 ok, -1 leer, -2 heikel (Passwort/Schluessel), -3 Speicher */
int  ged_vergiss(const char* text);                                       /* Anzahl vergessener Erinnerungen */
void ged_lerne(const char* nutzer, const char* antwort);                  /* nach jedem Gespraech: Neuronen, Synapsen, Arbeitsgedaechtnis, ggf. Erinnerung */
int  ged_kontext(const char* frage, char* out, int max, int erinnerungen);   /* Erinnerungen (wenn erinnerungen != 0) + letzter Austausch fuer das Sprachmodell; 0 = nichts */
void ged_neu_laden(void);                                                 /* nach jedem zdb_init() (Neustart, Entsperren) aufrufen */
int  ged_schlafen(void);                                                  /* Konsolidierung; liefert die Zahl vergessener Verbindungen */
void ged_loeschen(void);                                                  /* alles vergessen */
int  ged_heikel(const char* text);                                        /* enthaelt der Text ein Passwort, einen Schluessel o. ae.? (wird nie gemerkt und nicht im Chat-Verlauf abgelegt) */
void ged_befehl(const char* arg);                                         /* Shell: Text hinter "gedaechtnis" */
/* fuer die Anzeige */
void ged_zahlen(int* neuronen, int* synapsen, int* erinnerungen, int* takt);
int  ged_netz(int maxn, char names[][13], int* nutz, int* erregt, int maxs, int* sa, int* sb, int* sw, int* ns);   /* wichtigste Neuronen (Rang 0 = haeufigstes) und Synapsen zwischen ihnen; liefert die Zahl der Neuronen */
int  ged_erinnerung(int i, char* text, int max, int* art, int* staerke);  /* i-te Erinnerung, staerkste zuerst; 0 = keine */
void ged_erregung_loesche(void);                                          /* Erregung der Anzeige zuruecksetzen */
