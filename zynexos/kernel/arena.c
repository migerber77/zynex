/*
 * Zynex OS - Contiguous Memory Arena (AI foundation)
 */

#include "arena.h"
#include "kheap.h"

extern void kprint(const char* str);
extern void kprint_dec(uint32_t val);
extern void kprint_hex(uint32_t val);

void arena_init(struct arena* a, void* mem, uint32_t size, const char* name) {
    a->base = (uint8_t*)mem;
    a->size = size;
    a->used = 0;
    a->name = name ? name : "arena";
}

void* arena_alloc(struct arena* a, uint32_t size) {
    /* 16-byte align for nicer SIMD / tensor friendliness later */
    size = (size + 15) & ~15;
    if (a->used + size > a->size) return 0;

    void* ptr = a->base + a->used;
    a->used += size;
    return ptr;
}

void arena_reset(struct arena* a) {
    a->used = 0;
}

void arena_stats(struct arena* a) {
    kprint("Arena '");
    kprint(a->name);
    kprint("':\n");
    kprint("  Base : 0x");
    kprint_hex((uint32_t)(uintptr_t)a->base);
    kprint("\n");
    kprint("  Size : ");
    kprint_dec(a->size / 1024);
    kprint(" KiB\n");
    kprint("  Used : ");
    kprint_dec(a->used / 1024);
    kprint(" KiB\n");
    kprint("  Free : ");
    kprint_dec((a->size - a->used) / 1024);
    kprint(" KiB\n");
}

/* Global AI arena – 2 MiB contiguous */
#define AI_ARENA_SIZE (2 * 1024 * 1024)
static uint8_t* ai_mem = 0;
static struct arena ai_arena;

void ai_arena_init(void) {
    ai_mem = (uint8_t*)kmalloc(AI_ARENA_SIZE);
    if (!ai_mem) {
        kprint("[AI] Failed to allocate AI arena\n");
        return;
    }
    arena_init(&ai_arena, ai_mem, AI_ARENA_SIZE, "ai-working");
    kprint("[AI] Contiguous AI arena ready (");
    kprint_dec(AI_ARENA_SIZE / 1024);
    kprint(" KiB)\n");
}

void* ai_alloc(uint32_t size) {
    return arena_alloc(&ai_arena, size);
}

void ai_reset(void) {
    arena_reset(&ai_arena);
}

void ai_stats(void) {
    arena_stats(&ai_arena);
}
