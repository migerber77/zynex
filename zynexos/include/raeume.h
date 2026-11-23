#pragma once
#include <stdint.h>

/*
 * Raeume und Schluessel - Zynex-eigenes Ordner- und Rechtemodell (liegt als Schicht ueber dem flachen SynFS,
 * Datei "sys.raeume"; das SynFS-Format bleibt unveraendert).
 *   Raum      = Ordner, beliebig verschachtelt (Raum 0 = Wurzel "Zynex").
 *   Schluessel = Zugriffsstufe fuer eine Person oder fuer alle ("*") auf einen Raum; gilt fuer alle Unterraeume,
 *               bis ein naeherer Schluessel etwas anderes sagt.
 *   Stufen    : 0 Gesperrt, 1 Sehen, 2 Mitarbeiten (anlegen/aendern/loeschen), 3 Verwalten (Schluessel, Raum loeschen).
 *   Rolle als Deckel: Gast hoechstens Sehen. Besitzer eines Raums und Administratoren haben immer Verwalten.
 *   Neue Raeume sind privat (nur Besitzer und Admins), bis man sie teilt.
 */
#define RA_MAX      40
#define RA_NAME     20
#define ST_GESPERRT 0
#define ST_SEHEN    1
#define ST_MITARB   2
#define ST_VERW     3

void        raum_laden(void);                                       /* nach dem Einbinden von SynFS */
const char* raum_name(int id);                                      /* "Zynex" fuer 0, 0 wenn unbekannt */
const char* raum_besitzer(int id);
int         raum_parent(int id);                                    /* -1 wenn unbekannt */
int         raum_pfad(int id, char* out, int max);                  /* "Zynex/Projekte/Alpha" */
int         raum_stufe(int id, const char* wer, int rolle);         /* wirksame Stufe 0..3 */
int         raum_unter(int parent, int n, const char* wer, int rolle);   /* n-ter sichtbarer Unterraum (Stufe >= 1) oder -1 */
int         raum_neu(int parent, const char* name, const char* besitzer, int rolle);  /* id oder <0 (-1 Name/leer, -2 voll, -3 existiert, -4 keine Berechtigung) */
int         raum_loeschen(int id, const char* wer, int rolle);      /* 0 ok, -1 nicht leer, -2 keine Berechtigung */
int         raum_sicht(int id);                                     /* 0 privat, 1 alle sehen, 2 alle arbeiten mit */
void        raum_sicht_setzen(int id, int s);
int         raum_schluessel(int id, const char* wer);               /* eigener Schluessel dieser Person an genau diesem Raum, -1 keiner */
void        raum_schluessel_setzen(int id, const char* wer, int stufe);   /* stufe -1 = Schluessel entfernen */
int         datei_raum(const char* name);                           /* 0 = Wurzel */
void        datei_zuordnen(const char* name, int raum);
void        datei_vergessen(const char* name);
int         datei_nth(int raum, const char* const* endungen, int n, char* out, int max);   /* n-te Datei im Raum (ohne Systemdateien) */
int         datei_zahl(int raum, const char* const* endungen);
