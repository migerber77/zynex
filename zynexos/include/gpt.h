#pragma once
#include <stdint.h>

/* GPT-Partitionstabelle lesen/schreiben. Partitionstyp "Zynex Daten" enthaelt SynFS. */
uint32_t crc32(const void* d, uint32_t n);
int  gpt_scan(const char* disk);                    /* meldet Partitionen als <disk>p<N> an; liefert Anzahl */
const char* gpt_zynex_partition(void);              /* Name der ersten Zynex-Datenpartition oder 0 */

/* Layout fuer die Installation (Sektoren): EFI-Systempartition (Lader, Kernel, Signatur) und Daten. Kein BIOS-Boot mehr. */
#define GPT_P1_START   2048u      /* EFI-Systempartition, 41 MiB */
#define GPT_P1_SECTS   83968u
#define GPT_P2_START   86016u     /* Zynex Daten */

/* schreibt Schutz-MBR (Sektor 0 wird nur angepasst) und GPT; data_sects 0 = Rest der Platte */
int  gpt_write(const char* disk, uint32_t data_sects, uint32_t model_sects, uint8_t mbr_sector[512]);

/* Modell-Partition (Sprachmodell): Sektor 0 = Kopf "ZXMODEL1" + Groesse (u64), Daten ab Sektor GPT_MODELL_DATEN */
#define GPT_MODELL_DATEN 8u
const char* gpt_modell_partition(void);               /* Name der Modell-Partition oder 0 */
uint32_t    gpt_modell_sektoren(uint64_t bytes);      /* benoetigte Partitionsgroesse in Sektoren (auf MiB aufgerundet) */
uint32_t    gpt_modell_start(const char* disk, uint32_t sects);   /* Startsektor der Modell-Partition am Plattenende, 0 = passt nicht */
