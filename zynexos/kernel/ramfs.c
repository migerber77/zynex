/*
 * Zynex OS - Simple in-memory filesystem (RamFS)
 */

#include "ramfs.h"
#include "kheap.h"

extern void kprint(const char* str);
extern void kprint_dec(uint32_t val);

static struct ramfs_file files[RAMFS_MAX_FILES];

static int find_file(const char* name) {
    for (int i = 0; i < RAMFS_MAX_FILES; i++) {
        if (files[i].used) {
            const char* a = files[i].name;
            const char* b = name;
            while (*a && *b && *a == *b) { a++; b++; }
            if (*a == 0 && *b == 0) return i;
        }
    }
    return -1;
}

static void copy_name(char* dst, const char* src) {
    int i = 0;
    while (src[i] && i < RAMFS_MAX_NAME - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}

void ramfs_init(void) {
    for (int i = 0; i < RAMFS_MAX_FILES; i++) {
        files[i].used = 0;
        files[i].data = 0;
        files[i].size = 0;
        files[i].capacity = 0;
        files[i].name[0] = 0;
    }
    kprint("[RAMFS] In-memory filesystem ready (");
    kprint_dec(RAMFS_MAX_FILES);
    kprint(" file slots)\n");
}

int ramfs_create(const char* name) {
    if (!name || !name[0]) return -1;
    if (find_file(name) >= 0) return -2; /* exists */

    for (int i = 0; i < RAMFS_MAX_FILES; i++) {
        if (!files[i].used) {
            copy_name(files[i].name, name);
            files[i].data = (uint8_t*)kmalloc(RAMFS_MAX_SIZE);
            if (!files[i].data) return -3;
            files[i].size = 0;
            files[i].capacity = RAMFS_MAX_SIZE;
            files[i].used = 1;
            return 0;
        }
    }
    return -4; /* full */
}

int ramfs_write(const char* name, const void* data, uint32_t len) {
    int idx = find_file(name);
    if (idx < 0) {
        if (ramfs_create(name) != 0) return -1;
        idx = find_file(name);
        if (idx < 0) return -1;
    }

    if (len > files[idx].capacity) len = files[idx].capacity;

    const uint8_t* src = (const uint8_t*)data;
    for (uint32_t i = 0; i < len; i++) {
        files[idx].data[i] = src[i];
    }
    files[idx].size = len;
    return (int)len;
}

int ramfs_read(const char* name, void* buf, uint32_t max_len) {
    int idx = find_file(name);
    if (idx < 0) return -1;

    uint32_t len = files[idx].size;
    if (len > max_len) len = max_len;

    uint8_t* dst = (uint8_t*)buf;
    for (uint32_t i = 0; i < len; i++) {
        dst[i] = files[idx].data[i];
    }
    return (int)len;
}

int ramfs_delete(const char* name) {
    int idx = find_file(name);
    if (idx < 0) return -1;

    if (files[idx].data) {
        kfree(files[idx].data);
    }
    files[idx].used = 0;
    files[idx].data = 0;
    files[idx].size = 0;
    files[idx].name[0] = 0;
    return 0;
}

void ramfs_list(void) {
    kprint("RamFS files:\n");
    int count = 0;
    for (int i = 0; i < RAMFS_MAX_FILES; i++) {
        if (files[i].used) {
            kprint("  ");
            kprint(files[i].name);
            kprint("  (");
            kprint_dec(files[i].size);
            kprint(" bytes)\n");
            count++;
        }
    }
    if (count == 0) {
        kprint("  (empty)\n");
    }
}

int ramfs_exists(const char* name) {
    return find_file(name) >= 0;
}
