#pragma once
#include <stdint.h>
/* Zynies Gehirn: lokales Sprachmodell (GGUF, Qwen2/Llama, Q8_0) im Kernel */
int         gehirn_start(void);                       /* Modell einrichten; 0 = bereit, sonst Fehlercode (siehe gehirn_status) */
int         gehirn_bereit(void);
const char* gehirn_status(void);
/* beantwortet eine Frage (merkt sich den Gespraechsverlauf); cb wird fuer jedes neue Textstueck gerufen. Rueckgabe: Laenge der Antwort */
int         gehirn_frage(const char* frage, char* antwort, int max, void (*cb)(const char* teil, int n));
void        gehirn_vergessen(void);
int         gehirn_vorhanden(void);                   /* gibt es ein Modell (vom Stick oder auf der Platte)? */
int         gehirn_modelle_zeigen(void);              /* alle Modelle auf den Platten auflisten (liefert die Anzahl) */
int         gehirn_modell_waehlen(const char* dev);   /* beim naechsten Start laden; 0 ok */
int         gehirn_modell_loeschen(const char* dev);  /* 0 ok, -3 = letztes Modell (bleibt) */
