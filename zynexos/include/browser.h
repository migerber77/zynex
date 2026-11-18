#pragma once
#include <stdint.h>
/* Zynex-Browser: ruft Seiten per HTTP/HTTPS ab und setzt HTML als Text mit klickbaren Links (ohne Bilder, CSS und Skripte). */
#define WEB_ZEILEN 900
#define WEB_LINKS  300
struct web_zeile { char t[116]; uint8_t stil; uint8_t nl; struct { uint8_t a, b; uint16_t id; } l[6]; };   /* stil: 0 Text, 1 Titel, 2 Ueberschrift, 3 Zitat/Code */
extern struct web_zeile web_z[WEB_ZEILEN];
extern int  web_nz;                       /* Zeilenzahl */
extern char web_titel[80], web_url[200], web_fehler[100];
extern char web_links[WEB_LINKS][160];
extern int  web_nlinks;
int  web_laden(const char* url, int spalten);      /* 0 = ok; sonst <0 und web_fehler */
int  web_html(const char* html, uint32_t len, const char* basis, int spalten);   /* nur Aufbereitung (Tests) */
void web_url_bauen(const char* eingabe, char* out, int max);   /* Eingabe -> Adresse (Suche, https:// ergaenzen) */
int  web_text(char* out, int max);                 /* ganzer Seitentext zum Kopieren/Drucken */
