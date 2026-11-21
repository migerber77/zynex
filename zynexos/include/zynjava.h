#pragma once
#include <stdint.h>
/*
 * ZynJava: kleine Java-aehnliche Sprache mit eigenem Uebersetzer und Bytecode-Maschine fuer Programme in Webseiten.
 * Quelltext steht in <applet>, <zynjava> oder <script type="text/java"> und laeuft in einer Sandbox (nur Zeichenflaeche,
 * Tastatur, Maus). Nur ganze Zahlen, boolean, String, int[] und int[][]; Funktionen, if/while/for/switch, Java-Schreibweise
 * (class, public static, g.fillRect, System.out.println, Math.abs ...) wird akzeptiert.
 */
#define ZJ_MAX 2
#define ZJ_MAXW 640
#define ZJ_MAXH 400
enum { ZJ_START = 0, ZJ_PAINT, ZJ_TICK, ZJ_MDOWN, ZJ_MUP, ZJ_KEY, ZJ_MMOVE };

int zj_laden(int slot, const char* quelltext, int w, int h);   /* uebersetzen und starten; 0 ok, sonst Fehler (zj_fehler) */
const char* zj_fehler(int slot);                                /* "" wenn alles in Ordnung */
int zj_ereignis(int slot, int art, int a, int b);              /* Ereignis ausfuehren, danach paint(); 1 = Bild kann sich geaendert haben */
const uint32_t* zj_pixel(int slot);                            /* Zeichenflaeche (Breite w, Zeilenabstand w) */
int zj_hat(int slot, int art);                                 /* 1 = Programm hat diese Funktion */
int zj_breite(int slot);
int zj_hoehe(int slot);
void zj_beenden(int slot);
