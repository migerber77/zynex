/*
 * Zynex OS - Block device layer + Ramdisk
 * Clean interface; VirtIO can plug in later as another backend.
 */

#include "block.h"
#include "krypto.h"
#include "kheap.h"

extern void kprint(const char* str);
extern void kprint_dec(uint32_t val);

static struct block_dev active;
static struct block_dev devs[BLOCK_MAX_DEVS];
static int ndevs = 0;

static int streq(const char* a, const char* b) {
    while (*a && *b) { if (*a != *b) return 0; a++; b++; }
    return *a == *b;
}

static char partnamen[BLOCK_MAX_DEVS][20];

int block_register(const struct block_dev* d) {
    if (ndevs >= BLOCK_MAX_DEVS) return -1;
    devs[ndevs] = *d;
    devs[ndevs].parent = 0; devs[ndevs].offset = 0;
    ndevs++;
    return 0;
}

static int find_dev(const char* name) {
    for (int i = 0; i < ndevs; i++) if (streq(devs[i].name, name)) return i;
    return -1;
}

int block_register_part(const char* name, const char* parent, uint32_t offset, uint32_t sectors) {
    int p = find_dev(parent);
    if (p < 0 || ndevs >= BLOCK_MAX_DEVS || find_dev(name) >= 0) return -1;
    if ((uint64_t)offset + sectors > devs[p].sectors) return -2;
    char* n = partnamen[ndevs]; int i = 0;
    while (name[i] && i < 19) { n[i] = name[i]; i++; }
    n[i] = 0;
    devs[ndevs].name = n; devs[ndevs].sectors = sectors;
    devs[ndevs].read = 0; devs[ndevs].write = 0;
    devs[ndevs].parent = p + 1; devs[ndevs].offset = offset;
    ndevs++;
    return 0;
}
int block_forget_parts(const char* disk) {
    int p = find_dev(disk); if (p < 0) return -1;
    int w = 0;
    for (int i = 0; i < ndevs; i++) {
        if (devs[i].parent == p + 1) continue;
        if (w != i) { devs[w] = devs[i]; if (devs[w].parent) { /* Namenspuffer mitnehmen */ int k = 0; while (devs[i].name[k] && k < 19) { partnamen[w][k] = devs[i].name[k]; k++; } partnamen[w][k] = 0; devs[w].name = partnamen[w]; } }
        w++;
    }
    ndevs = w;
    return 0;
}
int block_sectors(const char* name) { int i = find_dev(name); return i < 0 ? -1 : (int)devs[i].sectors; }
int block_is_part(const char* name) { int i = find_dev(name); return i >= 0 && devs[i].parent; }
int block_count(void) { return ndevs; }
const char* block_name(int i) { return i >= 0 && i < ndevs ? devs[i].name : 0; }

static int dev_rw(const struct block_dev* d, int w, uint32_t lba, void* buf, uint32_t count) {
    if (d->parent) {
        if ((uint64_t)lba + count > d->sectors) return -2;
        return dev_rw(&devs[d->parent - 1], w, d->offset + lba, buf, count);
    }
    if (w) return d->write ? d->write(lba, buf, count) : -1;
    return d->read ? d->read(lba, buf, count) : -1;
}
int block_dev_read(const char* name, uint32_t lba, void* buf, uint32_t count) { int i = find_dev(name); return i < 0 ? -1 : dev_rw(&devs[i], 0, lba, buf, count); }
int block_dev_write(const char* name, uint32_t lba, const void* buf, uint32_t count) { int i = find_dev(name); return i < 0 ? -1 : dev_rw(&devs[i], 1, lba, (void*)buf, count); }

int block_select(const char* name) {
    for (int i = 0; i < ndevs; i++) {
        if (streq(devs[i].name, name)) { active = devs[i]; return 0; }
    }
    return -1;
}
static uint8_t* ramdisk_data = 0;
static uint32_t ramdisk_sectors = 0;

static int ramdisk_read(uint32_t lba, void* buf, uint32_t count) {
    if (!ramdisk_data) return -1;
    if (lba + count > ramdisk_sectors) return -2;
    uint8_t* dst = (uint8_t*)buf;
    uint8_t* src = ramdisk_data + lba * BLOCK_SECTOR_SIZE;
    uint32_t n = count * BLOCK_SECTOR_SIZE;
    for (uint32_t i = 0; i < n; i++) dst[i] = src[i];
    return (int)count;
}

static int ramdisk_write(uint32_t lba, const void* buf, uint32_t count) {
    if (!ramdisk_data) return -1;
    if (lba + count > ramdisk_sectors) return -2;
    uint8_t* dst = ramdisk_data + lba * BLOCK_SECTOR_SIZE;
    const uint8_t* src = (const uint8_t*)buf;
    uint32_t n = count * BLOCK_SECTOR_SIZE;
    for (uint32_t i = 0; i < n; i++) dst[i] = src[i];
    return (int)count;
}

int ramdisk_init(uint32_t sectors) {
    if (sectors == 0) sectors = 2048; /* 1 MiB default */
    uint32_t bytes = sectors * BLOCK_SECTOR_SIZE;
    ramdisk_data = (uint8_t*)kmalloc(bytes);
    if (!ramdisk_data) return -1;
    for (uint32_t i = 0; i < bytes; i++) ramdisk_data[i] = 0;
    ramdisk_sectors = sectors;

    active.name = "ramdisk";
    active.sectors = sectors;
    active.read = ramdisk_read;
    active.write = ramdisk_write;
    block_register(&active);

    kprint("[BLOCK] Ramdisk online (");
    kprint_dec(sectors);
    kprint(" sectors, ");
    kprint_dec(bytes / 1024);
    kprint(" KiB)\n");
    return 0;
}

void block_init(void) {
    active.name = "none";
    active.sectors = 0;
    active.read = 0;
    active.write = 0;
    ramdisk_init(32768);                 /* 16 MiB */
}

struct block_dev* block_get(void) { return &active; }

int block_read(uint32_t lba, void* buf, uint32_t count) {
    int r = dev_rw(&active, 0, lba, buf, count);
    if (r >= 0 && krypto_aktiv_fuer(active.name)) krypto_lesen(lba, (uint8_t*)buf, count);
    return r;
}
int block_write(uint32_t lba, const void* buf, uint32_t count) {
    if (!krypto_aktiv_fuer(active.name)) return dev_rw(&active, 1, lba, (void*)buf, count);
    static uint8_t t[BLOCK_SECTOR_SIZE];
    for (uint32_t i = 0; i < count; i++) {
        const uint8_t* q = (const uint8_t*)buf + i * BLOCK_SECTOR_SIZE;
        for (int k = 0; k < BLOCK_SECTOR_SIZE; k++) t[k] = q[k];
        krypto_schreiben(lba + i, t);
        if (dev_rw(&active, 1, lba + i, t, 1) < 0) return -1;
    }
    return (int)count;
}

void block_info(void) {
    kprint("Block devices (* = active, SynFS):\n");
    for (int i = 0; i < ndevs; i++) {
        kprint(streq(devs[i].name, active.name) ? "  * " : "    ");
        kprint(devs[i].name);
        if (devs[i].parent) { kprint(" (Partition auf "); kprint(devs[devs[i].parent - 1].name); kprint(")"); }
        kprint("  ");
        kprint_dec(devs[i].sectors);
        kprint(" sectors x ");
        kprint_dec(BLOCK_SECTOR_SIZE);
        kprint(" B (");
        kprint_dec(devs[i].sectors / 2048);
        kprint(" MiB)\n");
    }
}
