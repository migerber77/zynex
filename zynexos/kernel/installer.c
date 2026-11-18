/*
 * Zynex OS - Installation auf eine Platte
 * Das Installationspaket (Multiboot-Modul "zynex-install.pack") enthaelt Startsektor, eigenen UEFI-Lader und den Kernel
 * als Liste nicht-leerer Blocks. Der Installer legt eine GPT an (EFI-Systempartition, Zynex-Daten), spielt das
 * Paket ein, formatiert SynFS auf der Datenpartition und legt das erste Konto an.
 */
#include "installer.h"
#include "multiboot.h"
#include "block.h"
#include "gpt.h"
#include "synfs.h"
#include "synbin.h"
#include "benutzer.h"
#include "crypto.h"

extern void kprint(const char* str);
extern void kprint_dec(uint32_t val);

static uint32_t rd32(const uint8_t* p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static int seq(const char* a, const char* b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }
static void mc(void* d, const void* s, uint32_t n) { uint8_t* a = d; const uint8_t* b = s; for (uint32_t i = 0; i < n; i++) a[i] = b[i]; }

int installer_bereit(void) {
    const uint8_t* m = (const uint8_t*)(uintptr_t)g_boot.mod_addr;
    return g_boot.mod_size >= 8 && m[0] == 'Z' && m[1] == 'P' && m[2] == 'K' && m[3] == '1';
}

int installer_pruefe_platte(const char* disk, const char** fehler) {
    int s = block_sectors(disk);
    if (s < 0 || block_is_part(disk) || seq(disk, "ramdisk")) { *fehler = "Unbekannte Platte (siehe 'block')"; return -1; }
    if ((uint32_t)s < GPT_P2_START + 65536 + 64 + (g_boot.model_size ? gpt_modell_sektoren(g_boot.model_size) : 0)) { *fehler = "Platte zu klein (mindestens 80 MiB)"; return -2; }
    *fehler = 0;
    return 0;
}

void system_neustart(void) {
    __asm__ volatile ("cli");
    for (int i = 0; i < 100000; i++) { uint8_t s; __asm__ volatile ("inb %1, %0" : "=a"(s) : "Nd"((uint16_t)0x64)); if (!(s & 2)) break; }
    __asm__ volatile ("outb %0, %1" : : "a"((uint8_t)0xFE), "Nd"((uint16_t)0x64));        /* Reset ueber den Tastaturcontroller */
    for (volatile int i = 0; i < 1000000; i++) {}
    struct { uint16_t l; uint64_t b; } __attribute__((packed)) leer = { 0, 0 };           /* sonst: Dreifachfehler */
    __asm__ volatile ("lidt %0; int3" : : "m"(leer));
    for (;;) __asm__ volatile ("hlt");
}

static uint8_t bounce[4096] __attribute__((aligned(4096)));

static int schreibe(const char* disk, uint32_t lba, const uint8_t* d, uint32_t sects) {
    while (sects) {
        uint32_t n = sects > 8 ? 8 : sects;
        mc(bounce, d, n * 512);
        if (block_dev_write(disk, lba, bounce, n) < 0) return -1;
        lba += n; d += n * 512; sects -= n;
    }
    return 0;
}

int installer_start(const struct install_cfg* c, install_fortschritt cb) {
    __asm__ volatile ("sti");
    const char* f;
    if (!installer_bereit()) { kprint("Kein Installationspaket (nur von der Installations-ISO aus moeglich)\n"); return -1; }
    if (installer_pruefe_platte(c->disk, &f)) { kprint(f); kprint("\n"); return -2; }
    if (cb) cb(2, "Partitionen vorbereiten");

    const uint8_t* pk = (const uint8_t*)(uintptr_t)g_boot.mod_addr;
    uint32_t nrec = rd32(pk + 4), off = 8, total = (uint32_t)g_boot.mod_size;
    static uint8_t mbr[512];
    for (int i = 0; i < 512; i++) mbr[i] = 0;

    /* Bereich der beiden Systempartitionen leeren (alte Daten koennen sonst als FAT-Muell stehen bleiben) */
    static uint8_t nullen[4096] __attribute__((aligned(4096)));
    for (uint32_t lba = 34; lba < GPT_P2_START; lba += 8) {
        if (block_dev_write(c->disk, lba, nullen, 8) < 0) { kprint("Schreibfehler beim Leeren der Platte\n"); return -3; }
        if (cb && ((lba - 34) % 8192) == 0) cb(2 + (int)((lba - 34) * 28ull / GPT_P2_START), "Platte leeren");
    }
    for (uint32_t lba = GPT_P2_START; lba < GPT_P2_START + 64; lba += 8) block_dev_write(c->disk, lba, nullen, 8);   /* alter SynFS-Kopf */

    /* Paket einspielen */
    for (uint32_t r = 0; r < nrec; r++) {
        if (off + 8 > total) { kprint("Installationspaket defekt\n"); return -4; }
        uint32_t lba = rd32(pk + off), n = rd32(pk + off + 4); off += 8;
        if (off + n * 512 > total) { kprint("Installationspaket defekt\n"); return -4; }
        if (lba == 0) { mc(mbr, pk + off, 512); }
        else if (schreibe(c->disk, lba, pk + off, n) != 0) { kprint("Schreibfehler\n"); return -3; }
        off += n * 512;
        if (cb && (r % 64) == 0) cb(30 + (int)(r * 50ull / (nrec ? nrec : 1)), "Systemdateien kopieren");
    }
    /* Sprachmodell (falls vom Stick mitgegeben) in die Modell-Partition am Plattenende */
    uint32_t msects = g_boot.model_size ? gpt_modell_sektoren(g_boot.model_size) : 0;
    if (msects) {
        uint32_t mstart = gpt_modell_start(c->disk, msects);
        if (!mstart) { kprint("Platte zu klein fuer das Sprachmodell\n"); return -2; }
        static uint8_t kopf[512];
        for (int i = 0; i < 512; i++) kopf[i] = 0;
        mc(kopf, "ZXMODEL1", 8); for (int i = 0; i < 8; i++) kopf[8 + i] = (uint8_t)(g_boot.model_size >> (8 * i));
        if (schreibe(c->disk, mstart, kopf, 1) != 0) { kprint("Schreibfehler (Modell-Kopf)\n"); return -3; }
        const uint8_t* md = (const uint8_t*)(uintptr_t)g_boot.model_addr; uint64_t rest = g_boot.model_size, done = 0; uint32_t lba = mstart + GPT_MODELL_DATEN;
        while (rest) {
            uint32_t n = rest > 65536 ? 65536 : (uint32_t)rest; uint32_t sc = (n + 511) / 512;
            if (n % 512) { static uint8_t letzte[65536]; for (uint32_t i = 0; i < sc * 512; i++) letzte[i] = i < n ? md[done + i] : 0; if (schreibe(c->disk, lba, letzte, sc) != 0) { kprint("Schreibfehler (Modell)\n"); return -3; } }
            else if (schreibe(c->disk, lba, md + done, sc) != 0) { kprint("Schreibfehler (Modell)\n"); return -3; }
            lba += sc; done += n; rest -= n;
            if (cb && ((done >> 16) % 256) == 0) cb(40 + (int)(done * 40 / g_boot.model_size), "Sprachmodell kopieren");
        }
    }
    if (cb) cb(82, "Partitionstabelle schreiben");
    if (gpt_write(c->disk, c->daten_mib ? c->daten_mib * 2048u : 0, msects, mbr) != 0) { kprint("GPT konnte nicht geschrieben werden\n"); return -5; }
    if (gpt_scan(c->disk) < 2) { kprint("Partitionen nicht erkannt\n"); return -6; }

    const char* part = gpt_zynex_partition();
    if (!part) { kprint("Datenpartition nicht gefunden\n"); return -6; }
    if (cb) cb(88, "Dateisystem einrichten");
    struct block_dev* alt = block_get(); const char* altname = alt->name;
    if (block_select(part) != 0 || synfs_format("zynex-system") != 0 || synfs_mount() != 0) { kprint("SynFS-Einrichtung fehlgeschlagen\n"); return -7; }
    synbin_install_embedded();
    char conf[48] = "layout="; int k = 7; for (int i = 0; c->layout[i] && i < 3; i++) conf[k++] = c->layout[i]; conf[k++] = '\n';
    if (c->sprache[0]) { const char* l = "lang="; for (int i = 0; l[i]; i++) conf[k++] = l[i]; for (int i = 0; c->sprache[i] && i < 2; i++) conf[k++] = c->sprache[i]; conf[k++] = '\n'; }
    conf[k] = 0;
    synfs_write("sys.config", conf, (uint32_t)k);
    if (cb) cb(94, "Benutzerkonto anlegen");
    benutzer_laden();
    int br = benutzer_neu(c->user, c->pw, ROLLE_ADMIN);
    synfs_sync();
    /* zurueck auf das Live-System (Ramdisk) */
    block_select(altname); synfs_mount(); benutzer_laden();
    if (br != 0) { kprint("Benutzerkonto konnte nicht angelegt werden\n"); return -8; }
    if (cb) cb(100, "Fertig");
    return 0;
}
