/*
 * Zynex OS - Weighted / Predictive Cooperative Scheduler
 * NPU-first inspired: AI-class and higher priority tasks preferred.
 */

#include "task.h"

extern void kprint(const char* str);
extern void kprint_dec(uint32_t val);

static struct task tasks[MAX_TASKS];
static int current_task = -1;
static int task_count = 0;
static uint32_t next_id = 1;

void task_init(void) {
    for (int i = 0; i < MAX_TASKS; i++) {
        tasks[i].state = TASK_FREE;
        tasks[i].id = 0;
        tasks[i].name = 0;
        tasks[i].entry = 0;
        tasks[i].class = TASK_CLASS_NORMAL;
        tasks[i].priority = 0;
        tasks[i].run_count = 0;
    }

    tasks[0].id = next_id++;
    tasks[0].state = TASK_RUNNING;
    tasks[0].name = "shell";
    tasks[0].class = TASK_CLASS_LATENCY;
    tasks[0].priority = 10;
    current_task = 0;
    task_count = 1;

    kprint("[TASK] Weighted scheduler online (AI-class preferred)\n");
}

int task_create(const char* name, void (*entry)(void)) {
    return task_create_ex(name, entry, TASK_CLASS_NORMAL, 5);
}

int task_create_ex(const char* name, void (*entry)(void), enum task_class cls, int prio) {
    if (!entry) return -1;
    for (int i = 0; i < MAX_TASKS; i++) {
        if (tasks[i].state == TASK_FREE) {
            tasks[i].id = next_id++;
            tasks[i].state = TASK_READY;
            tasks[i].name = name ? name : "unnamed";
            tasks[i].entry = entry;
            tasks[i].class = cls;
            tasks[i].priority = prio;
            tasks[i].run_count = 0;
            tasks[i].stack = &tasks[i].stack_mem[TASK_STACK_SIZE / 4 - 1];
            task_count++;
            return (int)tasks[i].id;
        }
    }
    return -1;
}

/* Score for predictive selection: higher is better */
static int task_score(const struct task* t) {
    int score = t->priority * 10;
    if (t->class == TASK_CLASS_AI) score += 30;
    if (t->class == TASK_CLASS_LATENCY) score += 20;
    if (t->class == TASK_CLASS_BACKGROUND) score -= 10;
    /* Slight preference for less-run tasks (fairness) */
    if (t->run_count < 5) score += 5;
    return score;
}

void task_yield(void) {
    if (current_task < 0) return;
    tasks[current_task].state = TASK_READY;

    int best = -1;
    int best_score = -999999;

    for (int i = 0; i < MAX_TASKS; i++) {
        if (tasks[i].state != TASK_READY) continue;
        int s = task_score(&tasks[i]);
        if (s > best_score) {
            best_score = s;
            best = i;
        }
    }

    if (best < 0) {
        tasks[current_task].state = TASK_RUNNING;
        return;
    }

    current_task = best;
    tasks[current_task].state = TASK_RUNNING;
    tasks[current_task].run_count++;

    if (tasks[current_task].entry) {
        void (*fn)(void) = tasks[current_task].entry;
        tasks[current_task].entry = 0;
        fn();
        tasks[current_task].state = TASK_DEAD;
        if (task_count > 0) task_count--;
    }
}

void task_schedule(void) { task_yield(); }

void task_exit(void) {
    if (current_task >= 0) {
        tasks[current_task].state = TASK_DEAD;
        if (task_count > 0) task_count--;
        current_task = -1;
        task_yield();
    }
}

uint32_t task_get_current_id(void) {
    return (current_task >= 0) ? tasks[current_task].id : 0;
}

const char* task_get_current_name(void) {
    if (current_task < 0) return "none";
    return tasks[current_task].name ? tasks[current_task].name : "unnamed";
}

void task_list(void) {
    kprint("Tasks (weighted/predictive):\n");
    kprint("  ID  State    Class   Prio  Runs  Name\n");
    for (int i = 0; i < MAX_TASKS; i++) {
        if (tasks[i].state == TASK_FREE) continue;
        kprint("  ");
        kprint_dec(tasks[i].id);
        kprint("  ");
        switch (tasks[i].state) {
            case TASK_READY:   kprint("READY  "); break;
            case TASK_RUNNING: kprint("RUN    "); break;
            case TASK_BLOCKED: kprint("BLOCK  "); break;
            case TASK_DEAD:    kprint("DEAD   "); break;
            default:           kprint("?      "); break;
        }
        switch (tasks[i].class) {
            case TASK_CLASS_AI:         kprint("AI    "); break;
            case TASK_CLASS_LATENCY:    kprint("LAT   "); break;
            case TASK_CLASS_BACKGROUND: kprint("BG    "); break;
            default:                    kprint("NORM  "); break;
        }
        kprint_dec((uint32_t)tasks[i].priority);
        kprint("    ");
        kprint_dec(tasks[i].run_count);
        kprint("    ");
        kprint(tasks[i].name ? tasks[i].name : "?");
        kprint("\n");
    }
}
