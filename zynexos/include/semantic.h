#pragma once
#include <stdint.h>
#include <stddef.h>

#define SEM_MAX_ENTRIES 32
#define SEM_MAX_TAGS    4
#define SEM_TAG_LEN     24
#define SEM_NAME_LEN    32

struct sem_entry {
    char     name[SEM_NAME_LEN];
    char     tags[SEM_MAX_TAGS][SEM_TAG_LEN];
    int      tag_count;
    void*    data;
    uint32_t size;
    int      used;
    int      in_arena;   /* 1 = allocated from AI arena */
};

void  sem_init(void);
int   sem_store(const char* name, const char* tag1, const char* tag2, const void* data, uint32_t size);
int   sem_store_arena(const char* name, const char* tag1, const char* tag2, const void* data, uint32_t size);
int   sem_find_by_tag(const char* tag, char* out_name, uint32_t out_len);
int   sem_load(const char* name, void* buf, uint32_t max_len);
void* sem_get_ptr(const char* name, uint32_t* out_size);  /* direct pointer (AI use) */
void  sem_list(void);
int   sem_delete(const char* name);
