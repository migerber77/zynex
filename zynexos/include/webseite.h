#pragma once
#include <stdint.h>
/*
 * Zynex-Browser-Engine: HTML -> Bausteine -> Layout (Pixel) mit Schriftgroessen, fett, Farben, Links, Listen, Tabellen,
 * Bildern (PNG/JPEG/GIF, nachgeladen) und einfachen Formularen (GET). Kein JavaScript, keine Stilvorlagen-Dateien.
 */
enum { WE_TEXT = 1, WE_LINIE, WE_BILD, WE_FELD, WE_PUNKT, WE_RAHMEN, WE_FLAECHE };
struct wel { uint8_t typ, font, deko, pad; int16_t link, ref; int32_t x, y; int16_t w, h; uint32_t farbe; uint32_t off; };
struct wbild { char url[300]; int16_t aw, ah;      /* Groesse laut HTML (0 = unbekannt) */
               int16_t dw, dh;                      /* Anzeigegroesse */
               uint8_t status;                      /* 0 offen, 1 geladen, 2 Fehler, 3 uebersprungen */
               char alt[80]; uint32_t* px; };
struct wfeld { int16_t form; uint8_t typ;           /* 0 Text, 1 versteckt, 2 Senden */
               char name[48]; char wert[200]; char ph[64]; int16_t breite; };
struct wform { char action[300]; uint8_t post; };

#define WD_EL    90000
#define WD_LINKS 900
#define WD_BILDER 80
#define WD_FELDER 96
#define WD_FORMS 24
extern struct wel wd_el[]; extern int wd_nel;
extern char wd_txt[];                                 /* Woerter, je mit 0 abgeschlossen (wel.off) */
extern char wd_links[WD_LINKS][300]; extern int wd_nlinks;
extern struct wbild wd_bild[]; extern int wd_nbild;
extern struct wfeld wd_feld[]; extern int wd_nfeld;
extern struct wform wd_form[]; extern int wd_nform;
extern int wd_hoehe;                                  /* Gesamthoehe nach dem Layout */
extern uint32_t wd_hg;                                /* Seitenhintergrund */
extern char wd_titel[100], wd_url[300], wd_fehler[120];

int  wd_laden(const char* url);                       /* Seite holen und zerlegen (Weiterleitungen); 0 ok */
int  wd_html(const char* html, uint32_t len, const char* basis);   /* nur zerlegen (Tests) */
void wd_layout(int breite);                           /* Bausteine in Pixel setzen (Inhaltsbreite) */
int  wd_bild_naechstes(int y0, int y1);               /* naechstes offenes Bild im Bereich, sonst -1 */
int  wd_bild_laden(int i);                            /* holen, dekodieren, skalieren; 0 ok */
int  wd_form_url(int feld, char* out, int max);       /* Formular des Felds als GET-Adresse; 0 ok, -1 POST, -2 kein Formular */
int  wd_text(char* out, int max);                     /* Seitentext */
void wd_aufloesen(const char* basis, const char* href, char* out, int max);
