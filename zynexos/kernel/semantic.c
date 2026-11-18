/*
 * Zynex OS - Semantic Memory (linked with AI Arena)
 */

#include "semantic.h"
#include "kheap.h"
#include "arena.h"

extern void kprint(const char* str);
extern void kprint_dec(uint32_t val);

static struct sem_entry entries[SEM_MAX_ENTRIES];

static int streq(const char* a, const char* b) {
    while (*a && *b) { if (*a != *b) return 0; a++; b++; }
    return *a == *b;
}

static void copy_str(char* dst, const char* src, int max) {
    int i = 0;
    while (src && src[i] && i < max - 1) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

void sem_init(void) {
    for (int i = 0; i < SEM_MAX_ENTRIES; i++) {
        entries[i].used = 0;
        entries[i].data = 0;
        entries[i].tag_count = 0;
        entries[i].in_arena = 0;
    }
    kprint("[SEM] Semantic memory + AI arena bridge online\n");
}

static int find_or_alloc(const char* name) {
    int free_slot = -1;
    for (int i = 0; i < SEM_MAX_ENTRIES; i++) {
        if (entries[i].used && streq(entries[i].name, name)) return i;
        if (!entries[i].used && free_slot < 0) free_slot = i;
    }
    return free_slot;
}

static int store_internal(const char* name, const char* tag1, const char* tag2,
                          const void* data, uint32_t size, int use_arena) {
    if (!name || !data || size == 0) return -1;
    int idx = find_or_alloc(name);
    if (idx < 0) return -2;

    if (entries[idx].used && entries[idx].data && !entries[idx].in_arena) {
        kfree(entries[idx].data);
    }

    void* mem = 0;
    if (use_arena) {
        mem = ai_alloc(size);
        entries[idx].in_arena = 1;
    } else {
        mem = kmalloc(size);
        entries[idx].in_arena = 0;
    }
    if (!mem) return -3;

    uint8_t* d = (uint8_t*)mem;
    const uint8_t* s = (const uint8_t*)data;
    for (uint32_t i = 0; i < size; i++) d[i] = s[i];

    copy_str(entries[idx].name, name, SEM_NAME_LEN);
    entries[idx].data = mem;
    entries[idx].size = size;
    entries[idx].tag_count = 0;
    if (tag1 && tag1[0]) {
        copy_str(entries[idx].tags[entries[idx].tag_count++], tag1, SEM_TAG_LEN);
    }
    if (tag2 && tag2[0] && entries[idx].tag_count < SEM_MAX_TAGS) {
        copy_str(entries[idx].tags[entries[idx].tag_count++], tag2, SEM_TAG_LEN);
    }
    entries[idx].used = 1;
    return 0;
}

int sem_store(const char* name, const char* tag1, const char* tag2,
              const void* data, uint32_t size) {
    return store_internal(name, tag1, tag2, data, size, 0);
}

int sem_store_arena(const char* name, const char* tag1, const char* tag2,
                    const void* data, uint32_t size) {
    return store_internal(name, tag1, tag2, data, size, 1);
}

int sem_find_by_tag(const char* tag, char* out_name, uint32_t out_len) {
    if (!tag) return -1;
    for (int i = 0; i < SEM_MAX_ENTRIES; i++) {
        if (!entries[i].used) continue;
        for (int t = 0; t < entries[i].tag_count; t++) {
            if (streq(entries[i].tags[t], tag)) {
                if (out_name && out_len) copy_str(out_name, entries[i].name, (int)out_len);
                return i;
            }
        }
    }
    return -1;
}

int sem_load(const char* name, void* buf, uint32_t max_len) {
    for (int i = 0; i < SEM_MAX_ENTRIES; i++) {
        if (!entries[i].used || !streq(entries[i].name, name)) continue;
        uint32_t n = entries[i].size < max_len ? entries[i].size : max_len;
        uint8_t* d = (uint8_t*)buf;
        uint8_t* s = (uint8_t*)entries[i].data;
        for (uint32_t j = 0; j < n; j++) d[j] = s[j];
        return (int)n;
    }
    return -1;
}

void* sem_get_ptr(const char* name, uint32_t* out_size) {
    for (int i = 0; i < SEM_MAX_ENTRIES; i++) {
        if (entries[i].used && streq(entries[i].name, name)) {
            if (out_size) *out_size = entries[i].size;
            return entries[i].data;
        }
    }
    return 0;
}

void sem_list(void) {
    kprint("Semantic store:\n");
    int count = 0;
    for (int i = 0; i < SEM_MAX_ENTRIES; i++) {
        if (!entries[i].used) continue;
        kprint("  ");
        kprint(entries[i].name);
        kprint("  [");
        for (int t = 0; t < entries[i].tag_count; t++) {
            kprint(entries[i].tags[t]);
            if (t + 1 < entries[i].tag_count) kprint(", ");
        }
        kprint("] ");
        kprint_dec(entries[i].size);
        kprint(entries[i].in_arena ? " B (arena)\n" : " B\n");
        count++;
    }
    if (!count) kprint("  (empty)\n");
}

int sem_delete(const char* name) {
    for (int i = 0; i < SEM_MAX_ENTRIES; i++) {
        if (entries[i].used && streq(entries[i].name, name)) {
            if (entries[i].data && !entries[i].in_arena) kfree(entries[i].data);
            /* arena memory is bulk-reset, not per-object freed */
            entries[i].used = 0;
            entries[i].data = 0;
            return 0;
        }
    }
    return -1;
}
