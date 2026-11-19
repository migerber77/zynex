#pragma once
#include <stdint.h>
/* Intel High Definition Audio (Lautsprecher/Kopfhoerer und Mikrofon im Laptop oder PC).
 * Format fest: 48 kHz, 16 Bit. Ausgabe stereo, Aufnahme wird zu Mono (Mittelwert beider Kanaele) gemacht. Polling, keine Interrupts. */
int         hda_init(void);                 /* 0 = bereit (Controller + Codec + mind. ein Pfad); wird bei Bedarf automatisch aufgerufen */
int         hda_bereit(void);               /* 1 = bereit */
const char* hda_info(void);                 /* kurzer Zustandstext */
int         hda_hat_ausgabe(void);
int         hda_hat_eingabe(void);
/* Ausgabe: fill() schreibt bis zu 'frames' Stereo-Rahmen (L,R) nach 'stereo' und gibt die Anzahl zurueck; 0 = Ende (weniger als verlangt: Rest des Stuecks wird still). */
int         hda_spielen_strom(uint32_t (*fill)(int16_t* stereo, uint32_t frames, void* ctx), void* ctx);
/* Aufnahme: bis zu max_frames Mono-Abtastwerte (48 kHz); Rueckgabe = Anzahl, < 0 = Fehler. Dauert so lange wie die Aufnahme. */
int         hda_aufnehmen(int16_t* mono, uint32_t max_frames);
void        audio_befehl(const char* arg);  /* Shell: audio status | ton [Hz] [ms] | aufnahme [ms] */
