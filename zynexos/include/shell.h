#pragma once
#include <stdint.h>

void shell_init(void);
void shell_run(void);
void shell_set_grab(void (*f)(char));   /* Tasten an eine andere Oberflaeche (GUI) leiten, 0 = Shell */
void shell_set_tick(void (*f)(void));   /* Funktion, die die Hauptschleife regelmaessig aufruft */
void shell_handle_key(char c);
void shell_wartung(void);               /* vom Desktop (nur Administrator): Text-Konsole zeigen */
