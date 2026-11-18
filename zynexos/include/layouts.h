#pragma once
#include <stdint.h>
/* Tastaturbelegungen: HID-Tastennummer + Umschalt/AltGr -> Zeichen (Latin-1) */
#define KBD_LAYOUTS 8
struct kbd_layout { const char* code; const char* name; uint8_t t[4][54]; };
extern const struct kbd_layout kbd_layouts[KBD_LAYOUTS];

int         kbd_find(const char* code);                 /* Kuerzel ("ch","us",...) -> Nummer, -1 = unbekannt */
char        kbd_char(int id, uint8_t hid, int shift, int altgr);
