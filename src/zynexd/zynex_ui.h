/* Oberflaeche im Stil einer Chat-App: Seitenleiste mit Gesicht und Reitern, Sprechblasen, Eingabefeld. */
#ifndef ZYNEX_UI_H
#define ZYNEX_UI_H

#include "zynex_face.h"
#include "zynex_mem.h"

#define ZX_UI_LOG 64
#define ZX_UI_LINE 1024

typedef struct {
    zx_face face;
    char log[ZX_UI_LOG][ZX_UI_LINE];
    unsigned char von_zynex[ZX_UI_LOG];
    int nlog;
    char eingabe[ZX_UI_LINE];
    int ansicht;                 /* 0 Chat, 1 Notizen, 2 Gedaechtnis (Tab wechselt) */
    int bereit;                  /* Sprachmodell geladen */
    char adresse[80];            /* Webadresse, leer solange unbekannt */
    char zettel[ZX_MEM_MAX][ZX_MEM_LEN];
    int nzettel;
    char notiz[24][120];         /* Notiz-Titel */
    int nnotiz;
} zx_ui;

void zx_ui_init(zx_ui *u, uint32_t now_ms);
/* Neue Zeile im Gespraech. Von Zynex: das Gesicht spricht sie. */
void zx_ui_say(zx_ui *u, int von_zynex, const char *text);
void zx_ui_draw(zx_ui *u, zx_canvas *c);

#endif
