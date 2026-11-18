/* Gedaechtnis: Merkliste ueber den Nutzer (lokale Textdatei) und kurzer Gespraechsverlauf. */
#ifndef ZYNEX_MEM_H
#define ZYNEX_MEM_H

#include <stddef.h>

#define ZX_MEM_MAX 24      /* Anzahl Merkzettel */
#define ZX_MEM_LEN 160     /* Zeichen pro Merkzettel */
#define ZX_VERLAUF_N 4     /* so viele letzte Frage/Antwort-Paare gehen ans Modell */
#define ZX_VERLAUF_LEN 400

typedef struct {
    char zettel[ZX_MEM_MAX][ZX_MEM_LEN];
    int n;
    char pfad[200];
    int speicherbar;       /* 1, wenn die Datei beschrieben werden konnte */
    char frage[ZX_VERLAUF_N][ZX_VERLAUF_LEN];
    char antw[ZX_VERLAUF_N][ZX_VERLAUF_LEN];
    int nv;                /* Anzahl Eintraege im Verlauf */
} zx_mem;

/* pfad darf NULL sein (nur im Speicher). Liest vorhandene Datei. */
void zx_mem_init(zx_mem *m, const char *pfad);

/* Erkennt Gedaechtnis-Befehle ("merke dir ...", "was weisst du", "vergiss alles", "vergiss ...").
 * Rueckgabe 1 und Antworttext in out, wenn behandelt; sonst 0 (dann an das Modell weiter). */
int zx_mem_befehl(zx_mem *m, const char *text, char *out, size_t cap);

/* Schreibt System-Anweisung + Merkliste in out. */
void zx_mem_system(const zx_mem *m, const char *basis, char *out, size_t cap);

/* Schreibt den Verlauf als ChatML-Runden in out (leer, wenn keiner). */
void zx_mem_verlauf(const zx_mem *m, char *out, size_t cap);

/* Haengt ein Frage/Antwort-Paar an den Verlauf (aeltestes faellt weg). */
void zx_mem_verlauf_add(zx_mem *m, const char *frage, const char *antwort);

/* Merkzettel von Hand anlegen/loeschen (Weboberflaeche). Rueckgabe 1 bei Erfolg. */
int zx_mem_neu(zx_mem *m, const char *text);
int zx_mem_loesche(zx_mem *m, int index);
void zx_mem_alles_loeschen(zx_mem *m);

#endif
