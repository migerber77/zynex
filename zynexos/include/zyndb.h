#pragma once
#include <stdint.h>
/*
 * ZynDB - die Gedaechtnis-Datenbank von Zynex OS (weder SQL noch Tabellen):
 *  - Alles ist ein benannter Fakt ("netz.dns" = "1.1.1.1"); jede Aenderung haengt eine neue Version an, nichts geht verloren
 *    (Zeitreise: verlauf, zurueck). Das System legt hier seine Konfiguration ab.
 *  - Aehnlichkeitssuche statt Abfragesprache: "suche wlan passwort" findet Fakten nach Bedeutung/Schreibweise (Vektor-Einbettung
 *    aus Zeichen-Trigrammen und Woertern, Kosinus-Aehnlichkeit) - tippfehlertolerant, ohne Schema.
 *  - Immer verschluesselt und manipulationssicher: ChaCha20-Poly1305 ueber den ganzen Bestand, 256-Bit-Datenschluessel,
 *    der mit dem Plattenschluessel (krypto) verpackt wird. Ohne Plattenverschluesselung ist der Schutz nur Verschleierung.
 */
int         zdb_init(void);                     /* nach dem Einbinden von SynFS; 0 ok, -2 gesperrt, -3 beschaedigt/manipuliert */
int         zdb_offen(void);
int         zdb_put(const char* key, const char* val);
const char* zdb_get(const char* key);           /* aktuelle Fassung oder 0 */
int         zdb_geti(const char* key, int def);
int         zdb_del(const char* key);
void        zdb_neu_verpacken(void);            /* nach Einschalten der Plattenverschluesselung aufrufen */
void        zdb_befehl(const char* arg);        /* Shell: Text hinter "db" */
