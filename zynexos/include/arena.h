#pragma once
#include <stdint.h>
#include <stddef.h>

/* Simple contiguous memory arena – useful for AI / large buffers */

struct arena {
    uint8_t* base;
    uint32_t size;
    uint32_t used;
    const char* name;
};

void  arena_init(struct arena* a, void* mem, uint32_t size, const char* name);
void* arena_alloc(struct arena* a, uint32_t size);
void  arena_reset(struct arena* a);
void  arena_stats(struct arena* a);

/* Global AI working arena */
void  ai_arena_init(void);
void* ai_alloc(uint32_t size);
void  ai_reset(void);
void  ai_stats(void);
