/*
 * Zynex OS - Simple Kernel Heap (first-fit free list)
 */

#include "kheap.h"
#include "pmm.h"

extern void kprint(const char* str);
extern void kprint_dec(uint32_t val);
extern void kprint_hex(uint32_t val);

#define HEAP_SIZE    0x2000000     /* 32 MiB heap */
#define HEAP_MAGIC   0xZNXH4E4P    /* won't compile, use number */
#undef  HEAP_MAGIC
#define HEAP_MAGIC   0x5A4E5848    /* "ZNXH" */

struct heap_block {
    uint32_t magic;
    uint32_t size;          /* usable size */
    uint8_t  free;
    struct heap_block* next;
};

static struct heap_block* heap_head = 0;
static uint32_t heap_used = 0;
static uint32_t heap_total = HEAP_SIZE;

void kheap_init(void) {
    /* Heap aus dem Seitenverwalter (Identity-Map deckt alles ab) */
    void* mem = pmm_alloc_pages(HEAP_SIZE / PAGE_SIZE);
    if (!mem) { kprint("[KHEAP] Kein Speicher fuer den Heap!\n"); return; }
    heap_head = (struct heap_block*)mem;
    heap_head->magic = HEAP_MAGIC;
    heap_head->size  = HEAP_SIZE - sizeof(struct heap_block);
    heap_head->free  = 1;
    heap_head->next  = 0;
    heap_used = 0;

    kprint("[KHEAP] Kernel heap initialized at 0x");
    kprint_hex((uint32_t)(uintptr_t)mem);
    kprint(" (");
    kprint_dec(HEAP_SIZE / 1024);
    kprint(" KiB)\n");
}

static void split_block(struct heap_block* block, size_t size) {
    if (block->size < size + sizeof(struct heap_block) + 16) {
        return; /* too small to split */
    }

    struct heap_block* new_block = (struct heap_block*)((uint8_t*)block + sizeof(struct heap_block) + size);
    new_block->magic = HEAP_MAGIC;
    new_block->size  = block->size - size - sizeof(struct heap_block);
    new_block->free  = 1;
    new_block->next  = block->next;

    block->size = size;
    block->next = new_block;
}

void* kmalloc(size_t size) {
    if (size == 0) return 0;
    /* Align to 8 bytes */
    size = (size + 7) & ~7;

    struct heap_block* curr = heap_head;
    while (curr) {
        if (curr->free && curr->size >= size) {
            split_block(curr, size);
            curr->free = 0;
            heap_used += curr->size + sizeof(struct heap_block);
            return (void*)(curr + 1);
        }
        curr = curr->next;
    }
    return 0; /* out of memory */
}

void* kmalloc_aligned(size_t size, size_t align) {
    if (align < 8) align = 8;
    size_t total = size + align + sizeof(struct heap_block);
    void* raw = kmalloc(total);
    if (!raw) return 0;

    uintptr_t addr = (uintptr_t)raw;
    uintptr_t aligned = (addr + align - 1) & ~(align - 1);
    return (void*)aligned;
}

void kfree(void* ptr) {
    if (!ptr) return;

    struct heap_block* block = ((struct heap_block*)ptr) - 1;
    if (block->magic != HEAP_MAGIC) {
        kprint("[KHEAP] Invalid free!\n");
        return;
    }

    block->free = 1;
    if (heap_used >= block->size + sizeof(struct heap_block)) {
        heap_used -= block->size + sizeof(struct heap_block);
    }

    /* Simple coalescing with next */
    if (block->next && block->next->free) {
        block->size += sizeof(struct heap_block) + block->next->size;
        block->next = block->next->next;
    }
}

void kheap_stats(void) {
    kprint("Kernel Heap:\n");
    kprint("  Total : ");
    kprint_dec(heap_total / 1024);
    kprint(" KiB\n");
    kprint("  Used  : ");
    kprint_dec(heap_used / 1024);
    kprint(" KiB\n");
    kprint("  Free  : ");
    kprint_dec((heap_total - heap_used) / 1024);
    kprint(" KiB\n");
}
