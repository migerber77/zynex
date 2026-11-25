#pragma once
#include <stdint.h>
/*
 * Zynex Neuronales Netz: ein kleines Sprachnetz (Bengio-Art), das auf dem Geraet lernt.
 * Eingabe: die letzten 16 Bytes, je als 12-dimensionaler Vektor (Embedding); versteckte Schicht NN_NH Neuronen (1024) (tanh);
 * Ausgabe: Softmax ueber alle 256 Bytes. Training: Backpropagation mit Gradientenabstieg, Byte fuer Byte, online.
 * Gleitkomma nur innerhalb von nn.c (eigene SSE-Uebersetzung); die Schnittstelle ist ganzzahlig.
 */
#ifndef NN_NH
#define NN_NH 1024                              /* Neuronen der versteckten Schicht (vorher 256, dann 512) */
#endif
#define NN_PARAMS (256 * 12 + NN_NH * (16 * 12) + NN_NH + 256 * NN_NH + 256)
void nn_init(void);                              /* Gewichte zufaellig (fest vorgegebener Startwert) setzen */
void nn_vorbereiten(uint64_t hist, uint64_t hist2); /* hist = letzte 8 Bytes, hist2 = die 8 davor */              /* Verteilung des naechsten Bytes fuer diesen Kontext berechnen (merkt sich alles fuer nn_lerne) */
int  nn_bitprob(int c0);                         /* Wahrscheinlichkeit (1..4095 von 4096), dass das naechste Bit 1 ist, gegeben die schon bekannten Bits c0 (mit fuehrender 1) */
void nn_lerne(int c, int lr_tausendstel);        /* ein Lernschritt: Zielbyte c fuer den zuletzt vorbereiteten Kontext */
int  nn_verlust(void);                           /* mittlere Kreuzentropie der letzten Lernschritte in Bit x100 */
int  nn_exportieren(int16_t* out, int max);      /* Gewichte als int16 (Faktor 2048); Rueckgabe: Anzahl oder <0 */
int  nn_importieren(const int16_t* in, int n);   /* 0 = ok */
void nn_training(int an);                        /* 0 = nur vorhersagen (z. B. beim Einspielen des Protokolls) */
