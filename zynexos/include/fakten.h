#pragma once
/* Kleine Faktentabelle fuer Zynos (Hauptstaedte): sichere Antworten ohne Sprachmodell, damit das kleine lokale Modell hier nicht raten muss. */
int fakt_antwort(const char* frage, int lang, char* out, int max);     /* 1 = beantwortet (out gefuellt), 0 = nicht zustaendig */
