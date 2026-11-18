#pragma once
#include <stdint.h>
#include <stddef.h>

#define RAMFS_MAX_FILES 16
#define RAMFS_MAX_NAME  32
#define RAMFS_MAX_SIZE  4096

struct ramfs_file {
    char     name[RAMFS_MAX_NAME];
    uint8_t* data;
    uint32_t size;
    uint32_t capacity;
    int      used;
};

void ramfs_init(void);
int  ramfs_create(const char* name);
int  ramfs_write(const char* name, const void* data, uint32_t len);
int  ramfs_read(const char* name, void* buf, uint32_t max_len);
int  ramfs_delete(const char* name);
void ramfs_list(void);
int  ramfs_exists(const char* name);
