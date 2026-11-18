#pragma once
#include <stdint.h>

struct install_cfg {
    char disk[16];          /* Zielplatte, z.B. "sata0" oder "nvme0" */
    uint32_t daten_mib;     /* Groesse der Datenpartition in MiB, 0 = Rest der Platte */
    char user[16];          /* Name des ersten Kontos (Admin) */
    char pw[40];
    char layout[4];         /* Kuerzel der Tastaturbelegung, z.B. "ch", "us", "chf" */
    char sprache[3];        /* Sprache der Oberflaeche: "de","en","fr","it","es" */
};

typedef void (*install_fortschritt)(int prozent, const char* text);

int  installer_bereit(void);                                  /* Installationspaket (Multiboot-Modul) vorhanden? */
int  installer_pruefe_platte(const char* disk, const char** fehler);
int  installer_start(const struct install_cfg* cfg, install_fortschritt cb);   /* 0 = ok */
void system_neustart(void);
