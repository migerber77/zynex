#pragma once
#include <stdint.h>

/* Blockschicht: mehrere Geraete (Ramdisk, NVMe, ...), eines ist "aktiv" (dort liegt SynFS). */

#define BLOCK_SECTOR_SIZE 512
#define BLOCK_MAX_DEVS 16

struct block_dev {
    const char* name;
    uint32_t    sectors;
    int         (*read)(uint32_t lba, void* buf, uint32_t count);
    int         (*write)(uint32_t lba, const void* buf, uint32_t count);
    int         parent;     /* Partition: Index+1 des Datentraegers, 0 = ganzes Geraet */
    uint32_t    offset;     /* Partition: Startsektor auf dem Datentraeger */
};

void block_init(void);
int  block_register(const struct block_dev* dev);   /* Geraet anmelden */
int  block_select(const char* name);                /* aktives Geraet waehlen */
struct block_dev* block_get(void);                  /* aktives Geraet */
int  block_read(uint32_t lba, void* buf, uint32_t count);
int  block_write(uint32_t lba, const void* buf, uint32_t count);
/* Partitionen (Ansicht auf einen Ausschnitt eines Datentraegers) und direkter Zugriff ohne Auswahl */
int  block_register_part(const char* name, const char* parent, uint32_t offset, uint32_t sectors);
int  block_sectors(const char* name);               /* -1 = unbekannt */
int  block_is_part(const char* name);
int  block_dev_read(const char* name, uint32_t lba, void* buf, uint32_t count);
int  block_dev_write(const char* name, uint32_t lba, const void* buf, uint32_t count);
int  block_forget_parts(const char* disk);          /* Partitionen dieses Datentraegers abmelden (vor erneutem Einlesen) */
int  block_count(void);
const char* block_name(int i);
void block_info(void);                              /* alle Geraete auflisten */

int  ramdisk_init(uint32_t sectors);
