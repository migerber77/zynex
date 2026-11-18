#pragma once
#include <stdint.h>
#include <stddef.h>

void  kheap_init(void);
void* kmalloc(size_t size);
void* kmalloc_aligned(size_t size, size_t align);
void  kfree(void* ptr);
void  kheap_stats(void);
