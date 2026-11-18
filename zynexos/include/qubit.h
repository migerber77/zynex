#pragma once
#include <stdint.h>
/* Quanten-Simulator im Kernel: 1 bis 4 Qubits, Amplituden in Festkomma Q16.16 (im Kernel gibt es kein Fliesskomma).
 * Qubit 0 ist das niederwertigste Bit des Zustandsindex. Messungen nutzen den kryptografischen Zufall (krand). */
#define QUBIT_MAX 4
#define Q_EINS 65536                                   /* 1.0 in Q16 (Wahrscheinlichkeiten 0..65536) */

int      qubit_reset(int n);                           /* n Qubits im Zustand |0..0>, 0 = ok */
int      qubit_anzahl(void);
int      qubit_gate(char g, int q);                    /* g: 'h','x','y','z','s','t'  (0 = ok) */
int      qubit_cnot(int steuer, int ziel);
int      qubit_cz(int a, int b);
int      qubit_swap(int a, int b);
int      qubit_messen(int q);                          /* misst ein Qubit (0/1), Zustand kollabiert; <0 Fehler */
int      qubit_messen_alle(void);                      /* misst alle, liefert den Basiszustand */
int32_t  qubit_wahrscheinlichkeit(int zustand);        /* Q16 (0..65536), <0 Fehler */
void     qubit_selbsttest(void);
void     qubit_ausgabe(void);                          /* Zustandsvektor auf der Konsole */
int64_t  qubit_syscall(uint64_t op, uint64_t a, uint64_t b);
