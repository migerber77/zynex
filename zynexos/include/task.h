#pragma once
#include <stdint.h>

#define MAX_TASKS 8
#define TASK_STACK_SIZE 4096

enum task_state {
    TASK_FREE = 0,
    TASK_READY,
    TASK_RUNNING,
    TASK_BLOCKED,
    TASK_DEAD
};

/* Compute class – NPU-first inspired */
enum task_class {
    TASK_CLASS_NORMAL = 0,
    TASK_CLASS_AI,          /* prefer for AI / heavy compute */
    TASK_CLASS_LATENCY,     /* low-latency / interrupt-like */
    TASK_CLASS_BACKGROUND
};

struct task {
    uint32_t id;
    enum task_state state;
    enum task_class class;
    int      priority;      /* higher = more preferred */
    uint32_t* stack;
    uint32_t stack_mem[TASK_STACK_SIZE / 4];
    void (*entry)(void);
    const char* name;
    uint32_t run_count;     /* simple predictive metric */
};

void task_init(void);
int  task_create(const char* name, void (*entry)(void));
int  task_create_ex(const char* name, void (*entry)(void), enum task_class cls, int prio);
void task_yield(void);
void task_schedule(void);
void task_exit(void);
uint32_t task_get_current_id(void);
const char* task_get_current_name(void);
void task_list(void);
