/*
 * Zynex OS - Fluidic Capability System
 * Dynamic permission tokens (approximation of fluidic protection rings)
 */

#include "capability.h"
#include "timer.h"

extern void kprint(const char* str);
extern void kprint_dec(uint32_t val);

static struct capability caps[CAP_MAX];
static uint32_t next_cap_id = 1;

void cap_init(void) {
    for (int i = 0; i < CAP_MAX; i++) {
        caps[i].valid = 0;
        caps[i].id = 0;
    }
    /* Bootstrap: give shell broad capabilities */
    cap_grant(CAP_ADMIN, 1, 0);
    cap_grant(CAP_MEM_READ, 1, 0);
    cap_grant(CAP_MEM_WRITE, 1, 0);
    cap_grant(CAP_FS, 1, 0);
    cap_grant(CAP_AI, 1, 0);
    cap_grant(CAP_TASK, 1, 0);
    cap_grant(CAP_MODULE, 1, 0);
    cap_grant(CAP_QUANTUM, 1, 0);

    kprint("[CAP] Fluidic capability system online\n");
}

int cap_grant(enum cap_type type, uint32_t task_id, uint32_t lifetime_ticks) {
    for (int i = 0; i < CAP_MAX; i++) {
        if (!caps[i].valid) {
            caps[i].id = next_cap_id++;
            caps[i].type = type;
            caps[i].owner_task = task_id;
            caps[i].valid = 1;
            caps[i].expires = lifetime_ticks ? (timer_get_ticks() + lifetime_ticks) : 0;
            return (int)caps[i].id;
        }
    }
    return -1;
}

int cap_revoke(uint32_t cap_id) {
    for (int i = 0; i < CAP_MAX; i++) {
        if (caps[i].valid && caps[i].id == cap_id) {
            caps[i].valid = 0;
            return 0;
        }
    }
    return -1;
}

int cap_check(enum cap_type type, uint32_t task_id) {
    for (int i = 0; i < CAP_MAX; i++) {
        if (!caps[i].valid) continue;
        if (caps[i].type != type && caps[i].type != CAP_ADMIN) continue;
        if (caps[i].owner_task != task_id && caps[i].owner_task != 0) continue;
        if (caps[i].expires && timer_get_ticks() > caps[i].expires) {
            caps[i].valid = 0;
            continue;
        }
        return 1;
    }
    return 0;
}

void cap_tick(uint32_t current_ticks) {
    for (int i = 0; i < CAP_MAX; i++) {
        if (caps[i].valid && caps[i].expires && current_ticks > caps[i].expires) {
            caps[i].valid = 0;
        }
    }
}

static const char* cap_name(enum cap_type t) {
    switch (t) {
        case CAP_MEM_READ:  return "MEM_READ";
        case CAP_MEM_WRITE: return "MEM_WRITE";
        case CAP_EXEC:      return "EXEC";
        case CAP_IO:        return "IO";
        case CAP_TASK:      return "TASK";
        case CAP_FS:        return "FS";
        case CAP_AI:        return "AI";
        case CAP_MODULE:    return "MODULE";
        case CAP_ADMIN:     return "ADMIN";
        case CAP_QUANTUM:   return "QUANTUM";
        default:            return "NONE";
    }
}

void cap_list(void) {
    kprint("Capabilities:\n");
    kprint("  ID   Type       Task  Expires\n");
    for (int i = 0; i < CAP_MAX; i++) {
        if (!caps[i].valid) continue;
        kprint("  ");
        kprint_dec(caps[i].id);
        kprint("   ");
        kprint(cap_name(caps[i].type));
        kprint("   ");
        kprint_dec(caps[i].owner_task);
        kprint("   ");
        if (caps[i].expires) kprint_dec(caps[i].expires);
        else kprint("never");
        kprint("\n");
    }
}
