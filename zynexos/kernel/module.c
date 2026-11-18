/*
 * Zynex OS - Hot-swappable Module System
 * Simple runtime load/unload/reload (self-mutating periphery approximation)
 */

#include "module.h"

extern void kprint(const char* str);
extern void kprint_dec(uint32_t val);

static struct module modules[MOD_MAX];

static int streq(const char* a, const char* b) {
    while (*a && *b) { if (*a != *b) return 0; a++; b++; }
    return *a == *b;
}

static void copy_str(char* dst, const char* src, int max) {
    int i = 0;
    while (src && src[i] && i < max - 1) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

void mod_init_system(void) {
    for (int i = 0; i < MOD_MAX; i++) {
        modules[i].used = 0;
        modules[i].active = 0;
        modules[i].init = 0;
        modules[i].shutdown = 0;
    }
    kprint("[MOD] Hot-swappable module system online\n");
}

int mod_register(const char* name, mod_init_fn init, mod_shutdown_fn shutdown) {
    if (!name) return -1;
    for (int i = 0; i < MOD_MAX; i++) {
        if (!modules[i].used) {
            copy_str(modules[i].name, name, MOD_NAME_LEN);
            modules[i].init = init;
            modules[i].shutdown = shutdown;
            modules[i].used = 1;
            modules[i].active = 0;
            if (init) {
                init();
                modules[i].active = 1;
            }
            return 0;
        }
    }
    return -1;
}

int mod_unload(const char* name) {
    for (int i = 0; i < MOD_MAX; i++) {
        if (modules[i].used && streq(modules[i].name, name)) {
            if (modules[i].active && modules[i].shutdown) {
                modules[i].shutdown();
            }
            modules[i].active = 0;
            modules[i].used = 0;
            return 0;
        }
    }
    return -1;
}

int mod_reload(const char* name) {
    for (int i = 0; i < MOD_MAX; i++) {
        if (modules[i].used && streq(modules[i].name, name)) {
            if (modules[i].active && modules[i].shutdown) {
                modules[i].shutdown();
            }
            modules[i].active = 0;
            if (modules[i].init) {
                modules[i].init();
                modules[i].active = 1;
            }
            return 0;
        }
    }
    return -1;
}

void mod_list(void) {
    kprint("Modules:\n");
    int count = 0;
    for (int i = 0; i < MOD_MAX; i++) {
        if (!modules[i].used) continue;
        kprint("  ");
        kprint(modules[i].name);
        kprint(modules[i].active ? "  [active]" : "  [inactive]");
        kprint("\n");
        count++;
    }
    if (!count) kprint("  (none)\n");
}
