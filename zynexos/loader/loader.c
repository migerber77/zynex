/*
 * Zynex OS - eigener UEFI-Lader (ersetzt GRUB, nur UEFI, kein BIOS)
 * 1. liest \boot\kernel.bin, \boot\kernel.sig, \boot\zynex.cfg (Kernel-Kommandozeile), optional \boot\zynex-install.pack
 * 2. prueft die hash-basierte (quantensichere) LMS-Signatur ueber Kernel + Kommandozeile; ungueltig = kein Start
 * 3. laedt den Kernel (ELF64) an seine Adresse, waehlt einen Grafikmodus, baut die Boot-Informationen (Multiboot2-Format)
 * 4. beendet die UEFI-Dienste und springt in den 64-Bit-Einstieg des Kernels
 */
#include "uefi.h"
#include "../include/lms.h"
#include "zynex_key.h"          /* vom Build erzeugt: zynex_pubkey[56], zynex_key_gesetzt */

static EFI_SYSTEM_TABLE* ST; static EFI_BOOT_SERVICES* BS;

void* memcpy(void* d, const void* s, unsigned long long n) { unsigned char* a = d; const unsigned char* b = s; while (n--) *a++ = *b++; return d; }
void* memset(void* d, int c, unsigned long long n) { unsigned char* a = d; while (n--) *a++ = (unsigned char)c; return d; }
int memcmp(const void* x, const void* y, unsigned long long n) { const unsigned char* a = x; const unsigned char* b = y; while (n--) { if (*a != *b) return *a - *b; a++; b++; } return 0; }

static void say(const char* s) {
    CHAR16 b[2]; b[1] = 0;
    for (; *s; s++) {
        if (*s == '\n') { b[0] = '\r'; ST->ConOut->OutputString(ST->ConOut, b); }
        b[0] = (unsigned char)*s; ST->ConOut->OutputString(ST->ConOut, b);
    }
}
static void say_dec(UINT64 v) { char t[24]; int n = 0; if (!v) t[n++] = '0'; while (v) { t[n++] = (char)('0' + v % 10); v /= 10; } char o[24]; int k = 0; while (n) o[k++] = t[--n]; o[k] = 0; say(o); }
static void stop(const char* msg) { say("[LOADER] FEHLER: "); say(msg); say("\n[LOADER] Start abgebrochen.\n"); for (;;) __asm__ volatile ("hlt"); }

static EFI_GUID G_LOADED = { 0x5B1B31A1, 0x9562, 0x11d2, { 0x8E, 0x3F, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B } };
static EFI_GUID G_SFS = { 0x964e5b22, 0x6459, 0x11d2, { 0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b } };
static EFI_GUID G_GLOBAL = { 0x8BE4DF61, 0x93CA, 0x11d2, { 0xAA, 0x0D, 0x00, 0xE0, 0x98, 0x03, 0x2B, 0x8C } };
static EFI_GUID G_BLOCKIO = { 0x964e5b21, 0x6459, 0x11d2, { 0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b } };
typedef struct { UINT32 MediaId; UINT8 Removable, Present, LogicalPartition, ReadOnly, WriteCaching, p1, p2, p3; UINT32 BlockSize, IoAlign; UINT64 LastBlock; } ZX_BLOCK_MEDIA;
typedef struct ZX_BLOCK_IO { UINT64 Revision; ZX_BLOCK_MEDIA* Media; void* Reset; EFI_STATUS (EFIAPI *ReadBlocks)(struct ZX_BLOCK_IO*, UINT32, UINT64, UINTN, void*); } ZX_BLOCK_IO;
/* Gewuenschte Aufloesung: Sektor 20 der EFI-Partition (FAT32-Reservebereich), vom Kernel geschrieben: "ZXAUFL1\0" + Breite + Hoehe */
static void wunsch_lesen(EFI_HANDLE dev, unsigned* w, unsigned* h) {
    ZX_BLOCK_IO* bio = 0; *w = *h = 0;
    if (BS->HandleProtocol(dev, &G_BLOCKIO, (void**)&bio) != 0 || !bio || !bio->Media || bio->Media->BlockSize != 512) return;
    static unsigned char b[512];
    if (bio->ReadBlocks(bio, bio->Media->MediaId, 0, 512, b) != 0) return;
    if ((b[14] | b[15] << 8) < 24 || b[510] != 0x55 || b[511] != 0xAA) return;          /* nur FAT mit genug Reservesektoren */
    if (bio->ReadBlocks(bio, bio->Media->MediaId, 20, 512, b) != 0) return;
    if (memcmp(b, "ZXAUFL1", 8) != 0) return;
    *w = b[8] | b[9] << 8 | b[10] << 16 | (unsigned)b[11] << 24; *h = b[12] | b[13] << 8 | b[14] << 16 | (unsigned)b[15] << 24;
}
static EFI_GUID G_GOP = { 0x9042a9de, 0x23dc, 0x4a38, { 0x96, 0xfb, 0x7a, 0xde, 0xd0, 0x80, 0x51, 0x6a } };

static EFI_FILE_PROTOCOL* root;

/* Datei vollstaendig lesen; liefert Zeiger (Seiten unter 4 GiB) und Groesse, 0 wenn nicht vorhanden */
static unsigned char* lies(const CHAR16* name, UINT64* groesse, int muss) {
    EFI_FILE_PROTOCOL* f;
    if (root->Open(root, &f, (CHAR16*)name, 1, 0) != 0) {
        if (muss) { say("Datei fehlt: "); { char t[2] = {0, 0}; for (const CHAR16* p = name; *p; p++) { t[0] = (char)*p; say(t); } } say("\n"); stop("Startdatei nicht gefunden"); }
        *groesse = 0; return 0;
    }
    UINT64 n = 0; f->SetPosition(f, ~0ULL); f->GetPosition(f, &n); f->SetPosition(f, 0);
    EFI_PHYSICAL_ADDRESS a = 0xFFFFFFFFull;
    if (BS->AllocatePages(AllocateMaxAddress, EfiLoaderData, (n + 4095) / 4096 + 1, &a) != 0) stop("Kein Speicher fuer Startdatei");
    UINT64 got = n; if (f->Read(f, &got, (void*)(UINTN)a) != 0 || got != n) stop("Lesefehler");
    f->Close(f);
    *groesse = n; return (unsigned char*)(UINTN)a;
}

/* grosse Datei (Sprachmodell) lesen: beliebige Adresse, in 8-MiB-Stuecken; 0 wenn nicht vorhanden */
static unsigned char* lies_gross(const CHAR16* name, UINT64* groesse) {
    EFI_FILE_PROTOCOL* f;
    if (root->Open(root, &f, (CHAR16*)name, 1, 0) != 0) { *groesse = 0; return 0; }
    UINT64 n = 0; f->SetPosition(f, ~0ULL); f->GetPosition(f, &n); f->SetPosition(f, 0);
    EFI_PHYSICAL_ADDRESS a = 0;
    if (BS->AllocatePages(AllocateAnyPages, EfiLoaderData, (n + 4095) / 4096, &a) != 0) stop("Kein Speicher fuer das Sprachmodell");
    say("[LOADER] Lese Sprachmodell ");
    UINT64 pos = 0, naechste = 0;
    while (pos < n) {
        UINT64 got = n - pos > 8388608ULL ? 8388608ULL : n - pos;
        if (f->Read(f, &got, (void*)(UINTN)(a + pos)) != 0 || got == 0) stop("Lesefehler beim Sprachmodell");
        pos += got;
        if (pos >= naechste) { say("."); naechste += 268435456ULL; }
    }
    f->Close(f); say(" fertig\n");
    *groesse = n; return (unsigned char*)(UINTN)a;
}

struct elf_ph { unsigned int type, flags; unsigned long long off, vaddr, paddr, filesz, memsz, align; };

static void put32(unsigned char* p, unsigned v) { p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8); p[2] = (unsigned char)(v >> 16); p[3] = (unsigned char)(v >> 24); }
static void put64(unsigned char* p, unsigned long long v) { put32(p, (unsigned)v); put32(p + 4, (unsigned)(v >> 32)); }

/* GOP-Pixelformat pruefen: 0 = nicht nutzbar, 1 = BGR (8 Bit je Farbe), 2 = RGB. Format 2 (Bitmaske) haben manche Notebooks/Grafikkarten. */
static int pixel_format(const EFI_GRAPHICS_OUTPUT_MODE_INFORMATION* mi) {
    if (mi->PixelFormat == 1) return 1;
    if (mi->PixelFormat == 0) return 2;
    if (mi->PixelFormat == 2) {
        if (mi->Mask[0] == 0x00FF0000u && mi->Mask[1] == 0x0000FF00u && mi->Mask[2] == 0x000000FFu) return 1;
        if (mi->Mask[0] == 0x000000FFu && mi->Mask[1] == 0x0000FF00u && mi->Mask[2] == 0x00FF0000u) return 2;
    }
    return 0;
}

EFI_STATUS efi_main(EFI_HANDLE image, EFI_SYSTEM_TABLE* st) {
    ST = st; BS = st->BootServices;
    BS->SetWatchdogTimer(0, 0, 0, 0);
    st->ConOut->ClearScreen(st->ConOut);
    say("[LOADER] Zynex OS Lader (UEFI)\n");

    /* UEFI Secure Boot: Variable "SecureBoot" (1 = aktiv). Die Firmware hat den Lader selbst dann schon gegen ihre Schluesseldatenbank geprueft. */
    int sb = 0;
    {
        typedef EFI_STATUS (EFIAPI *GetVar)(CHAR16*, EFI_GUID*, UINT32*, UINTN*, void*);
        GetVar gv = (GetVar)((void**)((char*)st->RuntimeServices + 24))[6];
        static CHAR16 nm[] = { 'S','e','c','u','r','e','B','o','o','t',0 }; UINT8 v = 0; UINTN sz = 1;
        if (gv && gv(nm, &G_GLOBAL, 0, &sz, &v) == 0 && v == 1) sb = 1;
    }
    say(sb ? "[LOADER] UEFI Secure Boot: aktiv (Lader von der Firmware geprueft)\n" : "[LOADER] UEFI Secure Boot: aus\n");

    /* Startmedium oeffnen */
    EFI_LOADED_IMAGE* li; EFI_SIMPLE_FILE_SYSTEM* sfs;
    if (BS->HandleProtocol(image, &G_LOADED, (void**)&li) != 0) stop("Loaded-Image-Protokoll fehlt");
    if (BS->HandleProtocol(li->DeviceHandle, &G_SFS, (void**)&sfs) != 0) stop("Dateisystem des Startmediums nicht lesbar");
    if (sfs->OpenVolume(sfs, &root) != 0) stop("Startmedium nicht zu oeffnen");

    UINT64 kn, sn, cn, pn;
    unsigned char* kern = lies(L"\\boot\\kernel.bin", &kn, 1);
    unsigned char* sig = lies(L"\\boot\\kernel.sig", &sn, 0);
    unsigned char* cfg = lies(L"\\boot\\zynex.cfg", &cn, 0);
    unsigned char* pack = lies(L"\\boot\\zynex-install.pack", &pn, 0);
    UINT64 mn = 0; unsigned char* modell = lies_gross(L"\\boot\\modell.gguf", &mn);
    say("[LOADER] Kernel "); say_dec(kn); say(" Byte gelesen\n");

    /* Kommandozeile: erste Zeile von zynex.cfg */
    char cmd[128]; int cl = 0;
    if (cfg) { while (cl < 127 && (UINT64)cl < cn && cfg[cl] != '\n' && cfg[cl] != '\r' && cfg[cl]) { cmd[cl] = (char)cfg[cl]; cl++; } }
    cmd[cl] = 0;

    /* Signatur: gepruefte Nachricht = "ZXBOOT1" | u32 Kernel-Laenge | Kernel | Kommandozeile */
    unsigned flags = sb ? 4u : 0u;
    if (zynex_key_gesetzt) {
        if (!sig) stop("Signaturdatei kernel.sig fehlt");
        static unsigned char kopf[12]; memcpy(kopf, "ZXBOOT1\0", 8); put32(kopf + 8, (unsigned)kn);
        /* 3 Teile: Kopf + Kernel + Kommandozeile; lms_verify nimmt zwei, daher Kopf mit Kernel gemeinsam hashen ueber einen kleinen Umweg */
        static unsigned char* zusammen; EFI_PHYSICAL_ADDRESS za = 0xFFFFFFFFull;
        if (BS->AllocatePages(AllocateMaxAddress, EfiLoaderData, (12 + kn + cl + 4095) / 4096, &za) != 0) stop("Kein Speicher fuer Signaturpruefung");
        zusammen = (unsigned char*)(UINTN)za; memcpy(zusammen, kopf, 12); memcpy(zusammen + 12, kern, kn); memcpy(zusammen + 12 + kn, cmd, (unsigned long long)cl);
        say("[LOADER] Pruefe LMS-Signatur (hash-basiert, quantensicher) ...\n");
        int r = lms_verify(zynex_pubkey, sig, (uint32_t)sn, zusammen, (uint32_t)(12 + kn + cl), 0, 0);
        if (r != 0) { say("[LOADER] Signatur ungueltig, Code "); say_dec((UINT64)(-r)); say("\n"); stop("Kernel oder Kommandozeile wurde veraendert (Signatur ungueltig)"); }
        say("[LOADER] Signatur gueltig\n");
        flags |= 1;
    } else say("[LOADER] WARNUNG: kein Schluessel im Lader, Kernel wird NICHT geprueft\n");

    /* Kernel (ELF64) laden */
    if (kn < 64 || kern[0] != 0x7F || kern[1] != 'E' || kern[2] != 'L' || kern[3] != 'F' || kern[4] != 2) stop("Kernel ist kein ELF64");
    UINT64 phoff = *(UINT64*)(kern + 32); unsigned phnum = *(unsigned short*)(kern + 56), phsz = *(unsigned short*)(kern + 54);
    UINT64 lo = ~0ULL, hi = 0;
    for (unsigned i = 0; i < phnum; i++) { struct elf_ph* p = (struct elf_ph*)(kern + phoff + (UINT64)i * phsz); if (p->type != 1) continue; if (p->paddr < lo) lo = p->paddr; if (p->paddr + p->memsz > hi) hi = p->paddr + p->memsz; }
    if (lo >= hi) stop("Kernel ohne ladbare Segmente");
    lo &= ~4095ULL; UINT64 pages = (hi - lo + 4095) / 4096;
    /* Der Kernel ist positionsunabhaengig: irgendwo unter 4 GiB ablegen (Adresse 1 MiB ist auf echter Hardware oft belegt) */
    EFI_PHYSICAL_ADDRESS ka = 0xFFFFFFFFull;
    if (BS->AllocatePages(AllocateMaxAddress, EfiLoaderData, pages, &ka) != 0) stop("Kein zusammenhaengender Speicher fuer den Kernel unter 4 GiB");
    UINT64 delta = ka - lo;                                              /* neue Adresse = Link-Adresse + delta */
    memset((void*)(UINTN)ka, 0, pages * 4096);
    for (unsigned i = 0; i < phnum; i++) { struct elf_ph* p = (struct elf_ph*)(kern + phoff + (UINT64)i * phsz); if (p->type == 1 && p->filesz) memcpy((void*)(UINTN)(p->paddr + delta), kern + p->off, p->filesz); }
    /* Relokationen (R_X86_64_RELATIVE) aus der Tabelle des dynamischen Abschnitts anwenden */
    UINT64 rela = 0, relasz = 0, nrel = 0;
    for (unsigned i = 0; i < phnum; i++) {
        struct elf_ph* p = (struct elf_ph*)(kern + phoff + (UINT64)i * phsz); if (p->type != 2) continue;
        for (UINT64* d = (UINT64*)(UINTN)(p->vaddr + delta); d[0] != 0; d += 2) { if (d[0] == 7) rela = d[1]; else if (d[0] == 8) relasz = d[1]; }
    }
    if (relasz) {
        for (UINT64* r = (UINT64*)(UINTN)(rela + delta); (UINT64)((unsigned char*)r - (unsigned char*)(UINTN)(rela + delta)) < relasz; r += 3) {
            if ((unsigned)(r[1] & 0xFFFFFFFFu) != 8) stop("Unbekannte Relokation im Kernel");
            *(UINT64*)(UINTN)(r[0] + delta) = r[2] + delta; nrel++;
        }
    }
    say("[LOADER] Kernel bei Adresse "); say_dec(ka); say(", "); say_dec(nrel); say(" Relokationen\n");
    /* Einstiegspunkt: Markierung "ZXEFIENT" + 64-Bit-Adresse am Anfang des Kernels */
    UINT64 entry = 0;
    for (UINT64 o = 0; o + 16 <= hi - lo && o < 1048576; o += 8) if (memcmp((void*)(UINTN)(ka + o), "ZXEFIENT", 8) == 0) { entry = *(UINT64*)(UINTN)(ka + o + 8); break; }
    if (!entry) stop("Kernel hat keinen UEFI-Einstieg (ZXEFIENT)");
    say("[LOADER] Kernel geladen, Einstieg 0x"); { const char* hx = "0123456789abcdef"; char b[17]; for (int i = 0; i < 16; i++) b[i] = hx[(entry >> (60 - 4 * i)) & 15]; b[16] = 0; say(b); } say("\n");

    /* Grafik: gewuenschte Aufloesung (Einstellungen), sonst groesster Modus bis 2560x1440 im Format RGB/BGR 8 Bit */
    EFI_GRAPHICS_OUTPUT_PROTOCOL* gop = 0; int have_fb = 0;
    UINT64 fb_addr = 0; unsigned fb_pitch = 0, fb_w = 0, fb_h = 0;
    static unsigned modi[32][2]; unsigned nmodi = 0, ww = 0, wh = 0;
    wunsch_lesen(li->DeviceHandle, &ww, &wh);
    if (BS->LocateProtocol(&G_GOP, 0, (void**)&gop) == 0 && gop) {
        UINT32 best = gop->Mode->Mode; UINT64 bestarea = 0; int wunsch = -1;
        for (UINT32 m = 0; m < gop->Mode->MaxMode; m++) {
            UINTN sz; EFI_GRAPHICS_OUTPUT_MODE_INFORMATION* mi;
            if (gop->QueryMode(gop, m, &sz, &mi) != 0) continue;
            if (!pixel_format(mi)) continue;
            unsigned w = mi->HorizontalResolution, h = mi->VerticalResolution, neu = 1;
            for (unsigned k = 0; k < nmodi; k++) if (modi[k][0] == w && modi[k][1] == h) neu = 0;
            if (neu && w >= 640 && h >= 480 && w <= 2560 && h <= 1440) {
                /* Liste nach Flaeche aufsteigend halten (der Kernel nimmt den letzten Eintrag als "groesste"); bei Ueberlauf faellt der kleinste weg */
                UINT64 fl = (UINT64)w * h; unsigned pos = nmodi; int ok = 1;
                if (nmodi == 32) {
                    if (fl <= (UINT64)modi[0][0] * modi[0][1]) ok = 0;
                    else { for (unsigned k = 0; k + 1 < 32; k++) { modi[k][0] = modi[k + 1][0]; modi[k][1] = modi[k + 1][1]; } pos = 31; }
                } else nmodi++;
                if (ok) {
                    while (pos > 0 && (UINT64)modi[pos - 1][0] * modi[pos - 1][1] > fl) { modi[pos][0] = modi[pos - 1][0]; modi[pos][1] = modi[pos - 1][1]; pos--; }
                    modi[pos][0] = w; modi[pos][1] = h;
                }
            }
            if (w == ww && h == wh) wunsch = (int)m;
            if (w <= 2560 && h <= 1440) {
                UINT64 a = (UINT64)w * h;
                if (a > bestarea) { bestarea = a; best = m; }
            }
        }
        if (ww) { say("[LOADER] Gewuenschte Aufloesung "); say_dec(ww); say("x"); say_dec(wh); say(wunsch >= 0 ? " wird eingestellt\n" : " gibt es hier nicht\n"); }
        if (wunsch >= 0) { best = (UINT32)wunsch; bestarea = 1; }
        else if (gop->Mode->Info && gop->Mode->Info->HorizontalResolution >= 1024 && gop->Mode->Info->HorizontalResolution <= 2560 && pixel_format(gop->Mode->Info)) bestarea = 0;   /* Notebooks: der von der Firmware eingestellte (native) Modus ist am sichersten; die Liste bleibt fuer die Einstellungen */
        if (bestarea && best != gop->Mode->Mode) gop->SetMode(gop, best);
        EFI_GRAPHICS_OUTPUT_MODE_INFORMATION* mi = gop->Mode->Info;
        int pf = pixel_format(mi);
        if (pf) {
            have_fb = 1; fb_addr = gop->Mode->FrameBufferBase; fb_pitch = mi->PixelsPerScanLine * 4; fb_w = mi->HorizontalResolution; fb_h = mi->VerticalResolution;
            if (pf == 2) flags |= 2;                                      /* RGB-Reihenfolge: Kernel muss Rot/Blau tauschen */
        }
    }
    say("[LOADER] Aufloesungen: "); say_dec(nmodi); say("\n");
    say("[LOADER] Bildschirm "); say_dec(fb_w); say("x"); say_dec(fb_h); say(", Zeilenbreite "); say_dec(fb_pitch); say(" Byte, Format "); say_dec(gop && gop->Mode->Info ? gop->Mode->Info->PixelFormat : 9); say("\n");

    /* Boot-Informationen im Multiboot2-Format (unter 4 GiB) */
    EFI_PHYSICAL_ADDRESS ia = 0xFFFFFFFFull; UINT64 ipages = 64;
    if (BS->AllocatePages(AllocateMaxAddress, EfiLoaderData, ipages, &ia) != 0) stop("Kein Speicher fuer Boot-Informationen");
    unsigned char* info = (unsigned char*)(UINTN)ia; memset(info, 0, ipages * 4096);
    unsigned o = 8;
    put32(info + o, 1); put32(info + o + 4, 8 + (unsigned)cl + 1); memcpy(info + o + 8, cmd, (unsigned long long)cl); o = (o + 8 + (unsigned)cl + 1 + 7) & ~7u;       /* Kommandozeile */
    if (pack) { put32(info + o, 3); put32(info + o + 4, 16 + 8); put32(info + o + 8, (unsigned)(UINTN)pack); put32(info + o + 12, (unsigned)((UINTN)pack + pn)); memcpy(info + o + 16, "pack", 5); o += 24; }
    if (nmodi) { put32(info + o, 0x7A5A); put32(info + o + 4, 16 + nmodi * 8); put32(info + o + 8, nmodi); put32(info + o + 12, 0); for (unsigned k = 0; k < nmodi; k++) { put32(info + o + 16 + k * 8, modi[k][0]); put32(info + o + 20 + k * 8, modi[k][1]); } o = (o + 16 + nmodi * 8 + 7) & ~7u; }   /* verfuegbare Bildschirmmodi */
    if (modell) { put32(info + o, 0x7A59); put32(info + o + 4, 24); put64(info + o + 8, (unsigned long long)(UINTN)modell); put64(info + o + 16, mn); o += 24; }   /* Sprachmodell (64 Bit) */
    if (have_fb) { put32(info + o, 8); put32(info + o + 4, 32); put64(info + o + 8, fb_addr); put32(info + o + 16, fb_pitch); put32(info + o + 20, fb_w); put32(info + o + 24, fb_h); info[o + 28] = 32; info[o + 29] = 1; o += 32; }
    put32(info + o, 0x7A58); put32(info + o + 4, 16); put32(info + o + 8, flags); o += 16;                                         /* Zynex-Marke: Bit0 Signatur geprueft, Bit1 RGB */
    unsigned mmap_off = o;

    /* Speicherkarte holen und UEFI beenden */
    UINTN msz = 0, mkey, dsz; UINT32 dver;
    static unsigned char mbuf[65536];
    for (int versuch = 0; versuch < 8; versuch++) {
        msz = sizeof(mbuf);
        if (BS->GetMemoryMap(&msz, (EFI_MEMORY_DESCRIPTOR*)mbuf, &mkey, &dsz, &dver) != 0) stop("Speicherkarte nicht lesbar");
        if (BS->ExitBootServices(image, mkey) == 0) goto ende;
    }
    stop("ExitBootServices fehlgeschlagen");
ende:;
    /* ab hier keine UEFI-Dienste mehr; Speicherkarte in Multiboot2-Form (benachbarte gleiche Bereiche verschmelzen) */
    unsigned n = 0; unsigned char* ent = info + mmap_off + 16;
    unsigned long long cur_a = 0, cur_l = 0; unsigned cur_t = 0; int have = 0;
    for (UINTN off = 0; off + dsz <= msz; off += dsz) {
        EFI_MEMORY_DESCRIPTOR* d = (EFI_MEMORY_DESCRIPTOR*)(mbuf + off);
        unsigned t = (d->Type == 7 || d->Type == 3 || d->Type == 4) ? 1 : d->Type == 9 ? 3 : d->Type == 10 ? 4 : 2;
        unsigned long long a = d->PhysicalStart, l = d->NumberOfPages * 4096ULL;
        if (have && t == cur_t && a == cur_a + cur_l) { cur_l += l; continue; }
        if (have) { put64(ent + n * 24, cur_a); put64(ent + n * 24 + 8, cur_l); put32(ent + n * 24 + 16, cur_t); n++; }
        cur_a = a; cur_l = l; cur_t = t; have = 1;
        if (n >= 400) break;
    }
    if (have) { put64(ent + n * 24, cur_a); put64(ent + n * 24 + 8, cur_l); put32(ent + n * 24 + 16, cur_t); n++; }
    put32(info + mmap_off, 6); put32(info + mmap_off + 4, 16 + n * 24); put32(info + mmap_off + 8, 24); put32(info + mmap_off + 12, 0);
    o = (mmap_off + 16 + n * 24 + 7) & ~7u;
    put32(info + o, 0); put32(info + o + 4, 8); o += 8;
    put32(info, o); put32(info + 4, 0);

    __asm__ volatile ("cli; mov %0, %%rax; mov %1, %%rsi; mov $0x36D76289, %%edi; jmp *%%rax" : : "r"(entry), "r"((unsigned long long)(UINTN)info) : "rax", "rsi", "rdi", "memory");
    for (;;) {}
}
