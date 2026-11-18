#pragma once
/* Chat-Verlauf mit Anpinnen und Projekten; alles liegt verschluesselt in ZynDB (chat.*). */
int         chat_neu(const char* titel);                       /* neuer Chat, wird aktiv; liefert id (>0) oder <0 */
int         chat_aktiv(void);
void        chat_waehle(int id);
int         chat_liste(int* ids, int max, const char* projekt); /* gepinnte zuerst, dann neueste zuerst; projekt "" = alle */
const char* chat_titel(int id);
const char* chat_projekt(int id);                              /* "" wenn keins */
int         chat_gepinnt(int id);
void        chat_pin_umschalten(int id);
void        chat_projekt_setze(int id, const char* name);
int         chat_loeschen(int id);
void        chat_anhaengen(int id, char rolle, const char* text);   /* rolle 'U' = du, 'A' = Zynie */
int         chat_text(int id, char* out, int max);             /* Zeilen "U:..." und "A:..." */
int         chat_projekte(char names[][20], int max);          /* verschiedene Projektnamen */
void        chat_log(const char* frage, const char* antwort);  /* Frage/Antwort im aktiven Chat ablegen (legt ihn bei Bedarf an) */
