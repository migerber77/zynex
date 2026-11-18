/* Schnittstelle zum lokalen Sprachmodell (llama.cpp, als Bibliothek eingebettet, kein Server).
 * Das Modell ist eine austauschbare GGUF-Datei mit ChatML-Format (SmolLM3, Qwen, ...). */
#ifndef ZYNEX_LLM_H
#define ZYNEX_LLM_H

#include <stddef.h>

typedef struct zx_llm zx_llm;

/* Gibt NULL zurueck und schreibt eine Meldung nach err, wenn das Modell nicht geladen werden kann. */
zx_llm *zx_llm_open(const char *pfad, int n_ctx, int n_threads, char *err, size_t err_cap);
void zx_llm_close(zx_llm *m);

/* Wird fuer jedes erzeugte Textstueck aufgerufen. Rueckgabe != 0 bricht die Erzeugung ab. */
typedef int (*zx_llm_stueck)(const char *stueck, void *nutzer);

/* Erzeugt eine Antwort. Rueckgabe: Anzahl erzeugter Tokens, negativ bei Fehler.
 * tok_pro_s (darf NULL sein) bekommt die Erzeugungsgeschwindigkeit. */
/* verlauf: fertige ChatML-Runden vor der Frage (darf NULL/leer sein). */
int zx_llm_antwort(zx_llm *m, const char *system, const char *verlauf, const char *nutzer, int max_tokens,
                   zx_llm_stueck cb, void *cb_nutzer, double *tok_pro_s);

/* 1, wenn mit echter llama.cpp-Unterstuetzung gebaut. */
int zx_llm_verfuegbar(void);

#endif
