#pragma once
#include <stdint.h>

/* Fluidic Capability System
 * Lightweight approximation of dynamic permission tokens.
 * No fixed rings – permissions are granted/revoked as tokens.
 */

#define CAP_MAX 32

enum cap_type {
    CAP_NONE = 0,
    CAP_MEM_READ,
    CAP_MEM_WRITE,
    CAP_EXEC,
    CAP_IO,
    CAP_TASK,
    CAP_FS,
    CAP_AI,
    CAP_MODULE,
    CAP_ADMIN,
    CAP_QUANTUM
};

struct capability {
    uint32_t id;
    enum cap_type type;
    uint32_t owner_task;   /* 0 = system */
    uint32_t valid;        /* 1 = active */
    uint32_t expires;      /* tick when it expires, 0 = never */
};

void  cap_init(void);
int   cap_grant(enum cap_type type, uint32_t task_id, uint32_t lifetime_ticks);
int   cap_revoke(uint32_t cap_id);
int   cap_check(enum cap_type type, uint32_t task_id);
void  cap_list(void);
void  cap_tick(uint32_t current_ticks);  /* expire old ones */
