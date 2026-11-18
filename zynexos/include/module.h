#pragma once
#include <stdint.h>

/* Hot-swappable Modules
 * Approximation of self-mutating periphery:
 * Modules can be registered, listed, and "reloaded" from RamFS.
 */

#define MOD_MAX 8
#define MOD_NAME_LEN 24

typedef void (*mod_init_fn)(void);
typedef void (*mod_shutdown_fn)(void);

struct module {
    char name[MOD_NAME_LEN];
    mod_init_fn     init;
    mod_shutdown_fn shutdown;
    int  active;
    int  used;
};

void mod_init_system(void);
int  mod_register(const char* name, mod_init_fn init, mod_shutdown_fn shutdown);
int  mod_unload(const char* name);
int  mod_reload(const char* name);   /* shutdown + init again */
void mod_list(void);
