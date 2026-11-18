/* Kleiner Webserver in zynexd: Chat, Notizen und Gedaechtnis im Browser (Port 8080). */
#ifndef ZYNEX_WEB_H
#define ZYNEX_WEB_H

#include <pthread.h>
#include <stddef.h>

#include "zynex_llm.h"
#include "zynex_mem.h"

typedef struct {
    zx_mem *mem;                 /* Merkliste (geteilt mit der Bildschirm-Oberflaeche) */
    pthread_mutex_t *mem_mx;     /* schuetzt mem */
    const char *notizdatei;      /* z.B. /var/lib/zynex/notizen.txt */
    /* 1, wenn das Sprachmodell bereit ist */
    int (*bereit)(void);
    /* Erzeugt eine Antwort wie zx_llm_antwort (Modell-Sperre macht der Aufrufer-Code dahinter) */
    int (*frage)(const char *verlauf, const char *frage, zx_llm_stueck cb, void *cb_nutzer, double *tps);
} zx_web_host;

/* Startet den Server in einem eigenen Thread. 0 bei Erfolg. */
int zx_web_start(const zx_web_host *host, int port);

/* Schreibt "http://IP:PORT" der ersten Netzwerkkarte mit Adresse nach out. 1 bei Erfolg. */
int zx_web_adresse(char *out, size_t cap, int port);

#endif
