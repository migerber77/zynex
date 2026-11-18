#pragma once
#include <stdint.h>
/* Externe Sprachmodelle (ChatGPT, Claude, Gemini) ueber HTTPS-API. Zynie (intern) bleibt immer als Rueckfall bereit.
   Schluessel und Modellnamen liegen verschluesselt in ZynDB (extern.*). */
int         ext_quelle(void);                       /* 0 = intern, 1 = ChatGPT, 2 = Claude, 3 = Gemini */
void        ext_setze_quelle(int q);
const char* ext_name(int q);                        /* Anzeigename */
const char* ext_kennung(int q);                     /* openai | claude | gemini | intern */
int         ext_schluessel_da(int q);
void        ext_schluessel_setze(int q, const char* key);
void        ext_modell_setze(int q, const char* name);
const char* ext_modell(int q);
int         ext_frage(const char* frage, char* out, int max);   /* Laenge, sonst <0 (out enthaelt dann die Fehlermeldung) */
int         ext_json_text(const char* buf, uint32_t len, const char* feld, char* out, int max);   /* ersten String-Wert des Feldes lesen (Test) */
