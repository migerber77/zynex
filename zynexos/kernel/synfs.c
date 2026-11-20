/*
 * SynFS – Synaptic File System
 * Journaled, extent-based, AI-friendly.
 */

#include "synfs.h"
#include "block.h"
#include "timer.h"
#include "krypto.h"

extern void kprint(const char* str);
extern void kprint_dec(uint32_t val);
extern void kprint_hex(uint32_t val);

#define INODE_MAGIC 0x494E4F44u

static struct synfs_super sb;
static struct synfs_inode inode_cache[SYNFS_MAX_INODES];
#define BM_BYTES (SYNFS_BITMAP_MAX * SYNFS_SECTOR_SIZE)
static uint8_t bitmap[BM_BYTES];   /* 1 Bit je Zuteilungseinheit (4 KiB) */
static uint8_t holdbm[BM_BYTES];   /* Einheiten, die ein Snapshot festhaelt */
static uint8_t bmdirty[SYNFS_BITMAP_MAX]; /* geaenderte Bitmap-Sektoren (nur diese werden geschrieben) */
static int mounted = 0;
static uint8_t sector_buf[SYNFS_SECTOR_SIZE];

static int streq(const char* a, const char* b) {
    while (*a && *b) { if (*a != *b) return 0; a++; b++; }
    return *a == *b;
}

static void copy_str(char* d, const char* s, int max) {
    int i = 0;
    while (s && s[i] && i < max - 1) { d[i] = s[i]; i++; }
    d[i] = 0;
}

static void mem_set(void* p, uint8_t v, uint32_t n) {
    uint8_t* d = (uint8_t*)p;
    for (uint32_t i = 0; i < n; i++) d[i] = v;
}

static void mem_cpy(void* d, const void* s, uint32_t n) {
    uint8_t* dd = (uint8_t*)d;
    const uint8_t* ss = (const uint8_t*)s;
    for (uint32_t i = 0; i < n; i++) dd[i] = ss[i];
}

/* --- bitmap helpers (data region) --- */
static int bm_test(uint32_t bit) {
    return (bitmap[bit / 8] >> (bit % 8)) & 1;
}
static void bm_set(uint32_t bit) {
    bitmap[bit / 8] |= (uint8_t)(1u << (bit % 8));
    bmdirty[bit / 8 / SYNFS_SECTOR_SIZE] = 1;
}
static void bm_clear(uint32_t bit) {
    bitmap[bit / 8] &= (uint8_t)~(1u << (bit % 8));
    bmdirty[bit / 8 / SYNFS_SECTOR_SIZE] = 1;
}
static uint32_t units_total(void) { return sb.data_sectors / SYNFS_UNIT; }
static uint32_t bm_bytes(void) { return sb.bitmap_sectors * SYNFS_SECTOR_SIZE; }

static int alloc_sectors(uint32_t count, uint32_t* out_start) {
    if (count == 0) return -1;
    uint32_t units = (count + SYNFS_UNIT - 1) / SYNFS_UNIT;
    uint32_t max = units_total();
    for (uint32_t i = 0; i + units <= max; ) {
        uint32_t bad = units;                       /* erste belegte Einheit im Fenster */
        for (uint32_t j = 0; j < units; j++) if (bm_test(i + j)) { bad = j; break; }
        if (bad == units) {
            for (uint32_t j = 0; j < units; j++) bm_set(i + j);
            sb.free_data_sectors -= units * SYNFS_UNIT;
            *out_start = sb.data_start + i * SYNFS_UNIT;
            return 0;
        }
        i += bad + 1;
    }
    return -1;
}

static void free_sectors(uint32_t start, uint32_t count) {
    if (start < sb.data_start || count == 0) return;
    uint32_t first = (start - sb.data_start) / SYNFS_UNIT;
    uint32_t last = (start - sb.data_start + count - 1) / SYNFS_UNIT;
    for (uint32_t u = first; u <= last && u < units_total(); u++) {
        if (!((holdbm[u / 8] >> (u % 8)) & 1) && bm_test(u)) {
            bm_clear(u);
            sb.free_data_sectors += SYNFS_UNIT;
        }
    }
}

/* --- journal --- */
static int journal_write(uint32_t op, uint32_t target, const void* data, uint32_t size) {
    if (!mounted && op != SYNFS_J_NOP) {
        /* allow during format path via direct writes */
    }
    struct synfs_jentry je;
    mem_set(&je, 0, sizeof(je));
    je.magic = SYNFS_J_MAGIC;
    je.seq = ++sb.seq;
    je.op = op;
    je.target = target;
    je.data_size = size > sizeof(je.data) ? sizeof(je.data) : size;
    if (data && je.data_size) mem_cpy(je.data, data, je.data_size);

    uint32_t slot = (je.seq) % sb.journal_sectors;
    mem_set(sector_buf, 0, SYNFS_SECTOR_SIZE);
    mem_cpy(sector_buf, &je, sizeof(je) < SYNFS_SECTOR_SIZE ? sizeof(je) : SYNFS_SECTOR_SIZE);
    if (block_write(sb.journal_start + slot, sector_buf, 1) < 0) return -1;

    /* commit marker */
    je.op = SYNFS_J_COMMIT;
    je.data_size = 0;
    mem_set(sector_buf, 0, SYNFS_SECTOR_SIZE);
    mem_cpy(sector_buf, &je, sizeof(je) < SYNFS_SECTOR_SIZE ? sizeof(je) : SYNFS_SECTOR_SIZE);
    uint32_t cslot = (je.seq + 1) % sb.journal_sectors;
    if (block_write(sb.journal_start + cslot, sector_buf, 1) < 0) return -1;
    return 0;
}

static int write_super(void) {
    mem_set(sector_buf, 0, SYNFS_SECTOR_SIZE);
    mem_cpy(sector_buf, &sb, sizeof(sb) < SYNFS_SECTOR_SIZE ? sizeof(sb) : SYNFS_SECTOR_SIZE);
    return block_write(0, sector_buf, 1) < 0 ? -1 : 0;
}

static int write_bitmap(void) {
    uint32_t need = sb.bitmap_sectors;
    for (uint32_t i = 0; i < need; i++) {
        if (!bmdirty[i]) continue;
        mem_cpy(sector_buf, bitmap + i * SYNFS_SECTOR_SIZE, SYNFS_SECTOR_SIZE);
        if (block_write(sb.bitmap_start + i, sector_buf, 1) < 0) return -1;
        bmdirty[i] = 0;
    }
    return 0;
}

static int write_inode(uint32_t idx) {
    if (idx >= sb.inode_count) return -1;
    /* one inode per sector for simplicity/stability */
    /* Erst das Journal schreiben: journal_write benutzt denselben sector_buf. */
    journal_write(SYNFS_J_INODE, idx, &inode_cache[idx], sizeof(struct synfs_inode));
    mem_set(sector_buf, 0, SYNFS_SECTOR_SIZE);
    mem_cpy(sector_buf, &inode_cache[idx], sizeof(struct synfs_inode));
    return block_write(sb.inode_start + idx, sector_buf, 1) < 0 ? -1 : 0;
}

static int load_inode_table(void) {
    for (uint32_t i = 0; i < sb.inode_count; i++) {
        if (block_read(sb.inode_start + i, sector_buf, 1) < 0) return -1;
        mem_cpy(&inode_cache[i], sector_buf, sizeof(struct synfs_inode));
    }
    return 0;
}

static int load_bitmap(void) {
    mem_set(bitmap, 0, BM_BYTES);
    mem_set(bmdirty, 0, sizeof(bmdirty));
    for (uint32_t i = 0; i < sb.bitmap_sectors && i < SYNFS_BITMAP_MAX; i++) {
        if (block_read(sb.bitmap_start + i, sector_buf, 1) < 0) return -1;
        mem_cpy(bitmap + i * SYNFS_SECTOR_SIZE, sector_buf, SYNFS_SECTOR_SIZE);
    }
    return 0;
}

/* --- Snapshots --- */
static void hold_mark(const struct synfs_inode* in) {
    if (in->magic != INODE_MAGIC) return;
    for (uint32_t e = 0; e < in->extent_count && e < SYNFS_MAX_EXTENTS; e++)
        for (uint32_t j = 0; j < in->extents[e].length; j++) {
            if (in->extents[e].start_sector < sb.data_start) continue;
            if (j % SYNFS_UNIT && j) continue;       /* je Einheit einmal */
            uint32_t bit = (in->extents[e].start_sector - sb.data_start + j) / SYNFS_UNIT;
            if (bit < units_total()) holdbm[bit / 8] |= (uint8_t)(1u << (bit % 8));
        }
}
static uint32_t snap_slot(uint32_t i) { return sb.snap_start + i * sb.snap_sects; }
static int snap_head(uint32_t i, struct synfs_snap_head* h) {
    if (block_read(snap_slot(i), sector_buf, 1) < 0) return 0;
    mem_cpy(h, sector_buf, sizeof(*h));
    return h->magic == SYNFS_SNAP_MAGIC;
}
static int snap_find(const char* name) {
    struct synfs_snap_head h;
    for (uint32_t i = 0; i < SYNFS_SNAPS; i++) if (snap_head(i, &h) && streq(h.name, name)) return (int)i;
    return -1;
}
static int snap_table(uint32_t i, struct synfs_inode* out) {
    for (uint32_t k = 1; k <= sb.inode_count / 2; k++) {
        if (block_read(snap_slot(i) + k, sector_buf, 1) < 0) return -1;
        mem_cpy(&out[2 * (k - 1)], sector_buf, sizeof(struct synfs_inode));
        mem_cpy(&out[2 * (k - 1) + 1], sector_buf + 256, sizeof(struct synfs_inode));
    }
    return 0;
}
static void load_hold(void) {
    static struct synfs_inode tab[SYNFS_MAX_INODES];
    struct synfs_snap_head h;
    mem_set(holdbm, 0, BM_BYTES);
    for (uint32_t i = 0; i < SYNFS_SNAPS; i++) {
        if (!snap_head(i, &h)) continue;
        if (snap_table(i, tab) < 0) continue;
        for (uint32_t n = 0; n < sb.inode_count; n++) hold_mark(&tab[n]);
    }
}
/* Bitmap neu aufbauen: belegt = von lebenden Dateien oder Snapshots benutzt */
static void rebuild_bitmap(void) {
    uint32_t max = units_total();
    mem_set(bitmap, 0, BM_BYTES);
    mem_cpy(bitmap, holdbm, bm_bytes());
    for (uint32_t n = 0; n < sb.inode_count; n++) {
        const struct synfs_inode* in = &inode_cache[n];
        if (in->magic != INODE_MAGIC) continue;
        for (uint32_t e = 0; e < in->extent_count && e < SYNFS_MAX_EXTENTS; e++) {
            if (in->extents[e].start_sector < sb.data_start || !in->extents[e].length) continue;
            uint32_t first = (in->extents[e].start_sector - sb.data_start) / SYNFS_UNIT;
            uint32_t last = (in->extents[e].start_sector - sb.data_start + in->extents[e].length - 1) / SYNFS_UNIT;
            for (uint32_t u = first; u <= last && u < max; u++) bm_set(u);
        }
    }
    mem_set(bmdirty, 1, sb.bitmap_sectors);
    uint32_t frei = 0; for (uint32_t b = 0; b < max; b++) if (!bm_test(b)) frei++;
    sb.free_data_sectors = frei * SYNFS_UNIT;
    uint32_t fi = 0; for (uint32_t n = 0; n < sb.inode_count; n++) if (inode_cache[n].magic != INODE_MAGIC) fi++;
    sb.free_inodes = fi;
}

static void touch(struct synfs_inode* in) {
    uint32_t ymd, hm;
    in->chg = ++sb.seq;
    if (rtc_get(&ymd, &hm) == 0) { in->wall_ymd = ymd; in->wall_hm = hm; } else { in->wall_ymd = 0; in->wall_hm = 0; }
}

static int find_inode(const char* name) {
    for (uint32_t i = 0; i < sb.inode_count; i++) {
        if (inode_cache[i].magic == INODE_MAGIC && streq(inode_cache[i].name, name))
            return (int)i;
    }
    return -1;
}

static int alloc_inode(void) {
    for (uint32_t i = 0; i < sb.inode_count; i++) {
        if (inode_cache[i].magic != INODE_MAGIC) {
            mem_set(&inode_cache[i], 0, sizeof(struct synfs_inode));
            inode_cache[i].magic = INODE_MAGIC;
            inode_cache[i].id = i;
            if (sb.free_inodes) sb.free_inodes--;
            return (int)i;
        }
    }
    return -1;
}

/* --- public API --- */

int synfs_format(const char* label) {
    struct block_dev* dev = block_get();
    if (!dev || dev->sectors < 512) {
        kprint("[SynFS] Block device too small\n");
        return -1;
    }

    krypto_vergessen(dev->name);
    mem_set(&sb, 0, sizeof(sb));
    sb.magic = SYNFS_MAGIC;
    sb.version = SYNFS_VERSION;
    sb.total_sectors = dev->sectors;
    sb.journal_start = 1;
    sb.journal_sectors = SYNFS_JOURNAL_SECTS;
    sb.inode_start = sb.journal_start + sb.journal_sectors;
    /* Aufteilung aus der Groesse: etwa 1 Inode je 32 KiB, 64..1024 Stueck */
    uint32_t ni = sb.total_sectors / 64;
    if (ni > SYNFS_MAX_INODES) ni = SYNFS_MAX_INODES;
    if (ni < SYNFS_MIN_INODES) ni = SYNFS_MIN_INODES;
    ni -= ni % 64;
    sb.inode_count = ni;
    sb.inode_sectors = ni;                      /* 1 Sektor je Inode */
    sb.bitmap_start = sb.inode_start + sb.inode_sectors;
    sb.bitmap_sectors = (sb.total_sectors / SYNFS_UNIT + 4095) / 4096;   /* 1 Bit je 4 KiB */
    if (sb.bitmap_sectors < 1) sb.bitmap_sectors = 1;
    if (sb.bitmap_sectors > SYNFS_BITMAP_MAX) sb.bitmap_sectors = SYNFS_BITMAP_MAX;
    sb.snap_sects = 1 + ni / 2 + 1;             /* Kopf + 2 Inodes je Sektor + Reserve */
    sb.snap_start = sb.bitmap_start + sb.bitmap_sectors;
    sb.data_start = sb.snap_start + SYNFS_SNAPS * sb.snap_sects;
    sb.data_start = (sb.data_start + SYNFS_UNIT - 1) / SYNFS_UNIT * SYNFS_UNIT;   /* 4-KiB-ausgerichtet */
    if (sb.data_start + 64 >= sb.total_sectors) return -1;
    sb.data_sectors = sb.total_sectors - sb.data_start;
    if (sb.data_sectors > sb.bitmap_sectors * SYNFS_SECTOR_SIZE * 8 * SYNFS_UNIT)
        sb.data_sectors = sb.bitmap_sectors * SYNFS_SECTOR_SIZE * 8 * SYNFS_UNIT;
    sb.data_sectors -= sb.data_sectors % SYNFS_UNIT;
    sb.free_inodes = ni;
    sb.free_data_sectors = sb.data_sectors;
    sb.seq = 1;
    sb.mounted = 0;
    copy_str(sb.label, label ? label : "zynex-synfs", 16);

    /* clear journal */
    mem_set(sector_buf, 0, SYNFS_SECTOR_SIZE);
    for (uint32_t i = 0; i < sb.journal_sectors; i++)
        block_write(sb.journal_start + i, sector_buf, 1);

    /* clear inodes */
    for (uint32_t i = 0; i < sb.inode_count; i++) {
        mem_set(&inode_cache[i], 0, sizeof(inode_cache[i]));
        mem_set(sector_buf, 0, SYNFS_SECTOR_SIZE);
        block_write(sb.inode_start + i, sector_buf, 1);
    }

    mem_set(bitmap, 0, BM_BYTES);
    mem_set(holdbm, 0, BM_BYTES);
    mem_set(bmdirty, 1, sb.bitmap_sectors);
    mem_set(sector_buf, 0, SYNFS_SECTOR_SIZE);
    for (uint32_t i = 0; i < SYNFS_SNAPS; i++) block_write(snap_slot(i), sector_buf, 1);
    write_bitmap();
    write_super();

    mounted = 0;
    kprint("[SynFS] Formatted '");
    kprint(sb.label);
    kprint("' (");
    kprint_dec(sb.data_sectors);
    kprint(" data sectors, journal=");
    kprint_dec(sb.journal_sectors);
    kprint(")\n");
    return 0;
}

struct synfs_krypto* synfs_krypto_hdr(void) { return &sb.kr; }
int synfs_krypto_sichern(void) { return write_super(); }
uint32_t synfs_ende(void) { return sb.data_start + sb.data_sectors; }

int synfs_mount(void) {
    if (block_read(0, sector_buf, 1) < 0) return -1;
    mem_cpy(&sb, sector_buf, sizeof(sb));
    if (sb.magic != SYNFS_MAGIC) {
        kprint("[SynFS] No valid superblock (format first)\n");
        return -1;
    }
    if (sb.version != SYNFS_VERSION) {
        kprint("[SynFS] Altes Format - bitte neu formatieren (synformat)\n");
        return -1;
    }
    if (sb.kr.magic == SYNFS_KRY_MAGIC) {
        if (!krypto_aktiv_fuer(block_get()->name)) {
            krypto_merke_gesperrt(block_get()->name);
            kprint("[SynFS] Verschluesselt und gesperrt - entsperren mit: krypto entsperren\n");
            return -2;
        }
    } else krypto_vergessen(block_get()->name);
    if (load_inode_table() < 0) return -1;
    if (load_bitmap() < 0) return -1;
    load_hold();
    sb.mounted = 1;
    write_super();
    mounted = 1;
    kprint("[SynFS] Mounted '");
    kprint(sb.label);
    kprint("'\n");
    return 0;
}

int synfs_sync(void) {
    if (!mounted) return -1;
    for (uint32_t i = 0; i < sb.inode_count; i++) {
        if (inode_cache[i].magic == INODE_MAGIC)
            write_inode(i);
    }
    write_bitmap();
    write_super();
    kprint("[SynFS] Synced\n");
    return 0;
}

int synfs_create(const char* name, uint8_t type) {
    if (!mounted || !name || !name[0]) return -1;
    if (find_inode(name) >= 0) return -2;
    int idx = alloc_inode();
    if (idx < 0) return -3;

    struct synfs_inode* in = &inode_cache[idx];
    copy_str(in->name, name, SYNFS_NAME_LEN);
    in->type = type ? type : SYNFS_FILE;
    in->size = 0;
    in->ctime = in->mtime = timer_get_seconds();
    in->extent_count = 0;
    in->tags_count = 0;
    touch(in);

    /* AI type: prefer pre-reserving a small contiguous hint (optional) */
    if (in->type == SYNFS_AI) {
        uint32_t start;
        if (alloc_sectors(8, &start) == 0) {
            in->extents[0].start_sector = start;
            in->extents[0].length = 8;
            in->extent_count = 1;
        }
    }

    write_inode((uint32_t)idx);
    write_bitmap();
    write_super();
    return 0;
}

int synfs_write(const char* name, const void* data, uint32_t len) {
    if (!mounted || !name || !data) return -1;
    int idx = find_inode(name);
    if (idx < 0) {
        if (synfs_create(name, SYNFS_FILE) < 0) return -2;
        idx = find_inode(name);
        if (idx < 0) return -2;
    }

    struct synfs_inode* in = &inode_cache[idx];

    /* free old extents */
    for (uint32_t e = 0; e < in->extent_count; e++)
        free_sectors(in->extents[e].start_sector, in->extents[e].length);
    in->extent_count = 0;

    uint32_t need = (len + SYNFS_SECTOR_SIZE - 1) / SYNFS_SECTOR_SIZE;
    if (need == 0) need = 1;

    /* prefer single contiguous extent (AI-friendly) */
    uint32_t start;
    if (alloc_sectors(need, &start) != 0) return -3;
    in->extents[0].start_sector = start;
    in->extents[0].length = need;
    in->extent_count = 1;
    in->size = len;
    in->mtime = timer_get_seconds();
    touch(in);

    const uint8_t* src = (const uint8_t*)data;
    uint32_t left = len;
    for (uint32_t s = 0; s < need; s++) {
        mem_set(sector_buf, 0, SYNFS_SECTOR_SIZE);
        uint32_t chunk = left > SYNFS_SECTOR_SIZE ? SYNFS_SECTOR_SIZE : left;
        if (chunk) mem_cpy(sector_buf, src, chunk);
        if (block_write(start + s, sector_buf, 1) < 0) return -4;
        src += chunk;
        left -= chunk;
    }

    write_inode((uint32_t)idx);
    write_bitmap();
    write_super();
    return (int)len;
}

int synfs_read(const char* name, void* buf, uint32_t max_len) {
    if (!mounted || !name || !buf) return -1;
    int idx = find_inode(name);
    if (idx < 0) return -2;
    struct synfs_inode* in = &inode_cache[idx];
    if (in->extent_count == 0 || in->size == 0) return 0;

    uint32_t to_read = in->size < max_len ? in->size : max_len;
    uint8_t* dst = (uint8_t*)buf;
    uint32_t done = 0;
    uint32_t start = in->extents[0].start_sector;
    uint32_t sect = 0;
    while (done < to_read) {
        if (block_read(start + sect, sector_buf, 1) < 0) return -3;
        uint32_t chunk = to_read - done;
        if (chunk > SYNFS_SECTOR_SIZE) chunk = SYNFS_SECTOR_SIZE;
        mem_cpy(dst + done, sector_buf, chunk);
        done += chunk;
        sect++;
    }
    return (int)done;
}

int synfs_delete(const char* name) {
    if (!mounted || !name) return -1;
    int idx = find_inode(name);
    if (idx < 0) return -2;
    struct synfs_inode* in = &inode_cache[idx];
    for (uint32_t e = 0; e < in->extent_count; e++)
        free_sectors(in->extents[e].start_sector, in->extents[e].length);
    mem_set(in, 0, sizeof(*in));
    sb.free_inodes++;
    write_inode((uint32_t)idx);
    write_bitmap();
    write_super();
    return 0;
}

int synfs_tag(const char* name, const char* tag) {
    if (!mounted || !name || !tag) return -1;
    int idx = find_inode(name);
    if (idx < 0) return -2;
    struct synfs_inode* in = &inode_cache[idx];
    for (uint32_t t = 0; t < in->tags_count; t++) if (streq(in->tags[t], tag)) return 0;     /* schon vorhanden */
    if (in->tags_count >= SYNFS_MAX_TAGS) return -3;
    copy_str(in->tags[in->tags_count], tag, SYNFS_TAG_LEN);
    in->tags_count++;
    touch(in);
    write_inode((uint32_t)idx);
    write_super();
    return 0;
}

int synfs_find_tag(const char* tag, char* out_name, uint32_t out_len) {
    if (!mounted || !tag) return -1;
    for (uint32_t i = 0; i < sb.inode_count; i++) {
        if (inode_cache[i].magic != INODE_MAGIC) continue;
        for (uint32_t t = 0; t < inode_cache[i].tags_count; t++) {
            if (streq(inode_cache[i].tags[t], tag)) {
                if (out_name && out_len) copy_str(out_name, inode_cache[i].name, (int)out_len);
                return (int)i;
            }
        }
    }
    return -1;
}

/* n-te Datei (Name), deren Name auf eine der Endungen endet; 0 = keine (fuer Dateiauswahl) */
int synfs_nte_datei(int n, const char* const* endungen, char* out, int max) {
    if (!mounted) return 0;
    for (uint32_t i = 0; i < sb.inode_count; i++) {
        if (inode_cache[i].magic != INODE_MAGIC) continue;
        const char* nm = inode_cache[i].name; int l = 0; while (nm[l]) l++;
        int passt = 0;
        for (int e = 0; endungen[e] && !passt; e++) { int el = 0; while (endungen[e][el]) el++; if (l >= el) { passt = 1; for (int k = 0; k < el; k++) { char a = nm[l - el + k], b = endungen[e][k]; if (a >= 'A' && a <= 'Z') a = (char)(a + 32); if (a != b) { passt = 0; break; } } } }
        if (!passt) continue;
        if (n-- == 0) { int k = 0; while (nm[k] && k < max - 1) { out[k] = nm[k]; k++; } out[k] = 0; return 1; }
    }
    return 0;
}
void synfs_list(void) {
    if (!mounted) {
        kprint("[SynFS] Not mounted\n");
        return;
    }
    kprint("SynFS files:\n");
    int n = 0;
    for (uint32_t i = 0; i < sb.inode_count; i++) {
        if (inode_cache[i].magic != INODE_MAGIC) continue;
        kprint("  ");
        kprint(inode_cache[i].name);
        kprint("  ");
        kprint_dec(inode_cache[i].size);
        kprint(" B");
        if (inode_cache[i].type == SYNFS_AI) kprint(" [AI]");
        if (inode_cache[i].tags_count) {
            kprint(" tags:");
            for (uint32_t t = 0; t < inode_cache[i].tags_count; t++) {
                kprint(" ");
                kprint(inode_cache[i].tags[t]);
            }
        }
        kprint("\n");
        n++;
    }
    if (!n) kprint("  (empty)\n");
}

void synfs_info(void) {
    if (!mounted && sb.magic != SYNFS_MAGIC) {
        kprint("[SynFS] Not formatted\n");
        return;
    }
    kprint("SynFS:\n");
    kprint("  Label: "); kprint(sb.label); kprint("\n");
    kprint("  Journal: "); kprint_dec(sb.journal_sectors); kprint(" sectors\n");
    kprint("  Data free: "); kprint_dec(sb.free_data_sectors); kprint(" sectors\n");
    kprint("  Free inodes: "); kprint_dec(sb.free_inodes); kprint(" of "); kprint_dec(sb.inode_count); kprint("\n");
    kprint("  Data size: "); kprint_dec(sb.data_sectors / 2048); kprint(" MiB, free "); kprint_dec(sb.free_data_sectors / 2048); kprint(" MiB\n");
    kprint("  Mounted: "); kprint(mounted ? "yes" : "no"); kprint("\n");
    kprint("  Seq: "); kprint_dec(sb.seq); kprint("\n");
}

int synfs_is_mounted(void) { return mounted; }

/* ---- Snapshots und Objekt-Abfrage ---- */
int synfs_snap_create(const char* name) {
    if (!mounted || !name || !name[0]) return -1;
    struct synfs_snap_head h; int frei = -1;
    for (uint32_t i = 0; i < SYNFS_SNAPS; i++) {
        if (snap_head(i, &h)) { if (streq(h.name, name)) return -2; }
        else if (frei < 0) frei = (int)i;
    }
    if (frei < 0) return -3;
    for (uint32_t k = 1; k <= sb.inode_count / 2; k++) {
        mem_set(sector_buf, 0, SYNFS_SECTOR_SIZE);
        mem_cpy(sector_buf, &inode_cache[2 * (k - 1)], sizeof(struct synfs_inode));
        mem_cpy(sector_buf + 256, &inode_cache[2 * (k - 1) + 1], sizeof(struct synfs_inode));
        if (block_write(snap_slot((uint32_t)frei) + k, sector_buf, 1) < 0) return -4;
    }
    mem_set(&h, 0, sizeof(h));
    h.magic = SYNFS_SNAP_MAGIC; h.seq = ++sb.seq; copy_str(h.name, name, 16);
    mem_set(sector_buf, 0, SYNFS_SECTOR_SIZE);
    mem_cpy(sector_buf, &h, sizeof(h));
    if (block_write(snap_slot((uint32_t)frei), sector_buf, 1) < 0) return -4;     /* Kopf zuletzt = Snapshot gilt erst jetzt */
    for (uint32_t n = 0; n < sb.inode_count; n++) hold_mark(&inode_cache[n]);
    write_super();
    return 0;
}

int synfs_snap_restore(const char* name) {
    if (!mounted || !name) return -1;
    int i = snap_find(name);
    if (i < 0) return -2;
    if (snap_table((uint32_t)i, inode_cache) < 0) return -3;
    rebuild_bitmap();
    for (uint32_t n = 0; n < sb.inode_count; n++) write_inode(n);
    write_bitmap();
    write_super();
    return 0;
}

int synfs_snap_delete(const char* name) {
    if (!mounted || !name) return -1;
    int i = snap_find(name);
    if (i < 0) return -2;
    mem_set(sector_buf, 0, SYNFS_SECTOR_SIZE);
    if (block_write(snap_slot((uint32_t)i), sector_buf, 1) < 0) return -3;
    load_hold();
    rebuild_bitmap();
    write_bitmap();
    write_super();
    return 0;
}

void synfs_snap_list(void) {
    if (!mounted) { kprint("[SynFS] Not mounted\n"); return; }
    struct synfs_snap_head h; int n = 0;
    kprint("Snapshots:\n");
    for (uint32_t i = 0; i < SYNFS_SNAPS; i++) {
        if (!snap_head(i, &h)) continue;
        kprint("  "); kprint(h.name); kprint("  (Nr. "); kprint_dec(h.seq); kprint(")\n");
        n++;
    }
    if (!n) kprint("  (keine)\n");
}

int synfs_list_tag(const char* tag) {
    if (!mounted || !tag) return -1;
    int n = 0;
    for (uint32_t i = 0; i < sb.inode_count; i++) {
        if (inode_cache[i].magic != INODE_MAGIC) continue;
        for (uint32_t t = 0; t < inode_cache[i].tags_count; t++)
            if (streq(inode_cache[i].tags[t], tag)) { kprint("  "); kprint(inode_cache[i].name); kprint("\n"); n++; break; }
    }
    return n;
}

int synfs_untag(const char* name, const char* tag) {
    if (!mounted || !name || !tag) return -1;
    int idx = find_inode(name);
    if (idx < 0) return -2;
    struct synfs_inode* in = &inode_cache[idx];
    for (uint32_t t = 0; t < in->tags_count; t++) {
        if (!streq(in->tags[t], tag)) continue;
        for (uint32_t k = t; k + 1 < in->tags_count; k++) mem_cpy(in->tags[k], in->tags[k + 1], SYNFS_TAG_LEN);
        in->tags_count--;
        mem_set(in->tags[in->tags_count], 0, SYNFS_TAG_LEN);
        touch(in);
        write_inode((uint32_t)idx);
        write_super();
        return 0;
    }
    return -3;
}

int synfs_list_tags(const char* const* tags, int n) {
    if (!mounted || !tags || n <= 0) return -1;
    int treffer = 0;
    for (uint32_t i = 0; i < sb.inode_count; i++) {
        const struct synfs_inode* in = &inode_cache[i];
        if (in->magic != INODE_MAGIC) continue;
        int alle = 1;
        for (int k = 0; k < n && alle; k++) {
            int da = 0;
            for (uint32_t t = 0; t < in->tags_count; t++) if (streq(in->tags[t], tags[k])) { da = 1; break; }
            if (!da) alle = 0;
        }
        if (!alle) continue;
        kprint("  "); kprint(in->name); kprint("\n"); treffer++;
    }
    return treffer;
}

static void print2(uint32_t v) { if (v < 10) kprint("0"); kprint_dec(v); }

void synfs_timeline(int max) {
    if (!mounted) { kprint("[SynFS] Not mounted\n"); return; }
    uint32_t grenze = 0xFFFFFFFFu; int n = 0;
    kprint("Zeitleiste (neueste zuerst):\n");
    while (n < max) {
        int best = -1;
        for (uint32_t i = 0; i < sb.inode_count; i++) {
            const struct synfs_inode* in = &inode_cache[i];
            if (in->magic != INODE_MAGIC || in->chg >= grenze) continue;
            if (best < 0 || in->chg > inode_cache[best].chg) best = (int)i;
        }
        if (best < 0) break;
        const struct synfs_inode* in = &inode_cache[best];
        grenze = in->chg;
        kprint("  #"); kprint_dec(in->chg); kprint("  ");
        if (in->wall_ymd) {
            kprint_dec(in->wall_ymd / 10000); kprint("-"); print2(in->wall_ymd / 100 % 100); kprint("-"); print2(in->wall_ymd % 100);
            kprint(" "); print2(in->wall_hm / 100); kprint(":"); print2(in->wall_hm % 100);
        } else kprint("(Uhrzeit unbekannt)");
        kprint("  "); kprint(in->name); kprint("  "); kprint_dec(in->size); kprint(" B\n");
        n++;
    }
    if (!n) kprint("  (leer)\n");
}
