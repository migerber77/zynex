#pragma once
#include <stdint.h>
#include <stddef.h>

/*
 * SynFS – Synaptic File System
 * Fast, journaled, extent-based, AI-oriented.
 *
 * Layout on block device (sector = 512 B):
 *   0          Superblock
 *   1..N       Journal
 *   ...        Inode table
 *   ...        Bitmap
 *   ...        Data / extents
 */

#define SYNFS_MAGIC          0x53594E46u  /* 'SYNF' */
#define SYNFS_VERSION        4
#define SYNFS_SNAPS          4        /* Snapshot-Plaetze */
#define SYNFS_SNAP_MAGIC     0x534E4150u /* 'SNAP' */
#define SYNFS_NAME_LEN       32
#define SYNFS_MAX_TAGS       8
#define SYNFS_TAG_LEN        16
#define SYNFS_MAX_INODES     1024     /* Obergrenze; die tatsaechliche Zahl steht im Superblock (inode_count) */
#define SYNFS_MIN_INODES     64
#define SYNFS_UNIT           8        /* Zuteilungseinheit: 8 Sektoren = 4 KiB (1 Bit der Bitmap) */
#define SYNFS_BITMAP_MAX     2048     /* Bitmap-Sektoren (1 MiB) -> bis 32 GiB Daten */
#define SYNFS_MAX_EXTENTS    4
#define SYNFS_JOURNAL_SECTS  32
#define SYNFS_SECTOR_SIZE    512

enum synfs_inode_type {
    SYNFS_FILE = 1,
    SYNFS_DIR  = 2,
    SYNFS_AI   = 3   /* large contiguous / model blob */
};

struct synfs_extent {
    uint32_t start_sector;
    uint32_t length;       /* sectors */
} __attribute__((packed));

struct synfs_inode {
    uint32_t magic;        /* 0x494E4F44 'INOD' when used */
    uint32_t id;
    uint8_t  type;
    uint8_t  tags_count;
    uint16_t flags;
    uint32_t size;         /* bytes */
    uint32_t ctime;
    uint32_t mtime;
    char     name[SYNFS_NAME_LEN];
    char     tags[SYNFS_MAX_TAGS][SYNFS_TAG_LEN];
    struct synfs_extent extents[SYNFS_MAX_EXTENTS];
    uint32_t extent_count;
    uint32_t chg;          /* logische Aenderungsnummer (Zeitleiste) */
    uint32_t wall_ymd;     /* JJJJMMTT der letzten Aenderung, 0 = Uhr unbekannt */
    uint32_t wall_hm;      /* HHMM */
    uint32_t reserved;
} __attribute__((packed));
_Static_assert(sizeof(struct synfs_inode) <= 256, "Inode muss in 256 Byte passen (Snapshots)");

struct synfs_super {
    uint32_t magic;
    uint32_t version;
    uint32_t total_sectors;
    uint32_t journal_start;
    uint32_t journal_sectors;
    uint32_t inode_start;
    uint32_t inode_sectors;
    uint32_t bitmap_start;
    uint32_t bitmap_sectors;
    uint32_t data_start;
    uint32_t data_sectors;
    uint32_t inode_count;
    uint32_t free_inodes;
    uint32_t free_data_sectors;
    uint32_t seq;              /* journal sequence */
    uint32_t mounted;
    char     label[16];
    uint32_t snap_start;       /* ab Version 2: Snapshot-Bereich */
    uint32_t snap_sects;       /* ab Version 4: Sektoren je Snapshot-Platz (1 Kopf + inode_count/2 + 1) */
    uint8_t  reserved[392];
} __attribute__((packed));

struct synfs_snap_head {
    uint32_t magic;
    uint32_t seq;
    char     name[16];
} __attribute__((packed));

/* Journal entry */
#define SYNFS_J_MAGIC 0x4A4E4C31u /* 'JNL1' */
enum synfs_j_op {
    SYNFS_J_NOP = 0,
    SYNFS_J_INODE = 1,
    SYNFS_J_BITMAP = 2,
    SYNFS_J_COMMIT = 3
};

struct synfs_jentry {
    uint32_t magic;
    uint32_t seq;
    uint32_t op;
    uint32_t target;     /* inode id or bitmap sector */
    uint32_t data_size;
    uint8_t  data[480];  /* payload */
} __attribute__((packed));

int  synfs_format(const char* label);
int  synfs_mount(void);
int  synfs_sync(void);
int  synfs_create(const char* name, uint8_t type);
int  synfs_write(const char* name, const void* data, uint32_t len);
int  synfs_read(const char* name, void* buf, uint32_t max_len);
int  synfs_delete(const char* name);
int  synfs_tag(const char* name, const char* tag);
int  synfs_find_tag(const char* tag, char* out_name, uint32_t out_len);
void synfs_list(void);
void synfs_info(void);
int  synfs_is_mounted(void);

/* Snapshots (Copy-on-Write: Dateien werden bei jedem Schreiben in neue Sektoren gelegt, alte bleiben fuer Snapshots erhalten) */
int  synfs_snap_create(const char* name);
int  synfs_snap_restore(const char* name);
int  synfs_snap_delete(const char* name);
void synfs_snap_list(void);
/* Objekt-Abfrage nach Tag: gibt alle Treffer aus, liefert die Anzahl */
int  synfs_list_tag(const char* tag);
/* Mehrere Tags (UND-Verknuepfung), liefert Anzahl der Treffer */
int  synfs_list_tags(const char* const* tags, int n);
int  synfs_untag(const char* name, const char* tag);
/* Zeitleiste: die zuletzt geaenderten Dateien, neueste zuerst */
void synfs_timeline(int max);
