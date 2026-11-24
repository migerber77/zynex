#pragma once
#include <stdint.h>

/* Benutzerverwaltung: Konten liegen in der SynFS-Datei "sys.benutzer" (Passwort = PBKDF2-HMAC-SHA256, gesalzen) */
#define ROLLE_GAST     0
#define ROLLE_BENUTZER 1
#define ROLLE_ADMIN    2

int  benutzer_laden(void);               /* nach dem Einbinden von SynFS aufrufen; Sitzung wird beendet */
int  benutzer_anzahl(void);
int  benutzer_gesperrt(void);            /* es gibt Konten, aber niemand ist angemeldet */
int  benutzer_neu(const char* name, const char* pw, int rolle);   /* 0 ok, <0 Fehler */
int  benutzer_loeschen(const char* name);
int  benutzer_passwort(const char* name, const char* pw);
int  benutzer_login(const char* name, const char* pw);            /* 0 ok */
void benutzer_abmelden(void);
const char* benutzer_name(void);         /* "" wenn niemand angemeldet */
int  benutzer_rolle(void);               /* ohne Konten: ROLLE_ADMIN (Einrichtungsmodus) */
const char* benutzer_rollentext(int rolle);
int  benutzer_rolle_aus_text(const char* t);                      /* -1 unbekannt */
int  benutzer_darf(int cap_type);        /* Rechtepruefung nach Rolle (Werte aus enum cap_type) */
void benutzer_liste(void);
int  benutzer_nth(int n, char* name, int* rolle);                  /* n-tes Konto (0..), 1 = vorhanden */
int  benutzer_umbenennen(const char* alt, const char* neu);       /* 0 ok, <0 Fehler */
int  benutzer_rolle_setzen(const char* name, int rolle);          /* 0 ok, -6 = letzter Admin */

/* Rad-Schluessel (passwortlose Anmeldung ueber eine Symbolfolge; Passwort bleibt als Alternative) */
int  benutzer_rad_hat(const char* name);
int  benutzer_rad_setzen(const char* name, const char* code);     /* code = mind. 6 Zeichen; 0 ok */
int  benutzer_rad_setzen2(const char* name, const char* code, int maske, int laenge, int nur_rad); /* maske: Bits 0-11 = Symbole, laenge 3..9, nur_rad = Passwort abgeschaltet */
int  benutzer_rad_info(const char* name, int* maske, int* laenge, int* nur_rad);
int  benutzer_rad_entfernen(const char* name);
int  benutzer_rad_login(const char* name, const char* code);      /* 0 ok, -1 falsch, -7 gesperrt (Wartezeit) */
int  benutzer_rad_sperre(void);                                   /* verbleibende Sekunden der Sperre */
