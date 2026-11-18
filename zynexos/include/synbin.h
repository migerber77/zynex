#pragma once
#include <stdint.h>

/* .synbin - natives Programmformat von Zynex OS (Aufbau siehe tools/sync.py) */
#define SYNBIN_MAGIC "SYNAPBIN"
#define SYNBIN_TARGET_CPU 1
#define SYNBIN_TARGET_NPU 2

#define SYNCAP_CONSOLE 0x01
#define SYNCAP_TIMER   0x02
#define SYNCAP_ALL     0x03

struct synbin_header {
    char     magic[8];
    uint8_t  target;
    uint32_t intent[8];      /* 8 x float32, hier nur als Rohbits gelesen */
    uint8_t  caps;
    uint64_t payload_size;
} __attribute__((packed));

/* Was ein Programm vom Kernel aufrufen darf (Zeiger auf diese Tabelle kommt in rdi) */
struct syn_api {
    void     (*print)(const char* text);
    void     (*print_num)(uint64_t value);
    uint64_t (*ticks)(void);
};

void synbin_install_embedded(void);          /* eingebaute Programme ins SynFS legen */
int  synbin_info(const char* name);          /* Kopf anzeigen */
int  synbin_run(const char* name);           /* laden, pruefen, starten */
void synbin_set_allowed(uint8_t mask);
uint8_t synbin_get_allowed(void);
