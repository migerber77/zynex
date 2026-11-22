#pragma once
#include <stdint.h>
/*
 * Zynex Neuronales Netz: ein kleines Sprachnetz (Bengio-Art), das auf dem Geraet lernt.
 * Eingabe: die letzten 8 Bytes, je als 16-dimensionaler Vektor (Embedding); versteckte Schicht 128 Neuronen (tanh);
 * Ausgabe: Softmax ueber alle 256 Bytes. Training: Backpropagation mit Gradientenabstieg, Byte fuer Byte, online.
 * Gleitkomma nur innerhalb von nn.c (eigene SSE-Uebersetzung); die Schnittstelle ist ganzzahlig.
 */
#define NN_PARAMS (256 * 16 + 128 * 128 + 128 + 256 * 128 + 256)
void nn_init(void);                              /* Gewichte zufaellig (fest vorgegebener Startwert) setzen */
void nn_vorbereiten(uint64_t hist);              /* Verteilung des naechsten Bytes fuer diesen Kontext berechnen (merkt sich alles fuer nn_lerne) */
int  nn_bitprob(int c0);                         /* Wahrscheinlichkeit (1..4095 von 4096), dass das naechste Bit 1 ist, gegeben die schon bekannten Bits c0 (mit fuehrender 1) */
void nn_lerne(int c, int lr_tausendstel);        /* ein Lernschritt: Zielbyte c fuer den zuletzt vorbereiteten Kontext */
int  nn_verlust(void);                           /* mittlere Kreuzentropie der letzten Lernschritte in Bit x100 */
int  nn_exportieren(int16_t* out, int max);      /* Gewichte als int16 (Faktor 2048); Rueckgabe: Anzahl oder <0 */
int  nn_importieren(const int16_t* in, int n);   /* 0 = ok */
void nn_training(int an);                        /* 0 = nur vorhersagen (z. B. beim Einspielen des Protokolls) */
