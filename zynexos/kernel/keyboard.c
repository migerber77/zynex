/*
 * Zynex OS - PS/2-Tastatur und PS/2-Maus (i8042), gleiche Layouts wie die USB-Tastatur (Schweiz/US)
 * Notebooks (z. B. ThinkPad): Tastatur und TrackPoint haengen am i8042.
 */

#include "keyboard.h"
#include "shell.h"
#include "usb.h"
#include "pic.h"

extern void kprint(const char* s);

#define KBD_DATA 0x60
#define KBD_CMD  0x64

static int shift_pressed = 0, caps_lock = 0, altgr = 0, ext = 0, mouse_ok = 0;

/* Scancode-Satz 1 (Make-Code) -> USB-HID-Tastennummer (damit dieselbe Layout-Tabelle gilt) */
static const uint8_t sc2hid[0x59] = {
    0, 0x29, 0x1E, 0x1F, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x2D, 0x2E, 0x2A, 0x2B,      /* 00-0F */
    0x14, 0x1A, 0x08, 0x15, 0x17, 0x1C, 0x18, 0x0C, 0x12, 0x13, 0x2F, 0x30, 0x28, 0, 0x04, 0x16,      /* 10-1F */
    0x07, 0x09, 0x0A, 0x0B, 0x0D, 0x0E, 0x0F, 0x33, 0x34, 0x35, 0, 0x31, 0x1D, 0x1B, 0x06, 0x19,      /* 20-2F */
    0x05, 0x11, 0x10, 0x36, 0x37, 0x38, 0, 0, 0, 0x2C, 0, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E,               /* 30-3F (3B-3F: F1-F5) */
    0x3F, 0x40, 0x41, 0x42, 0x43, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,                                     /* 40-4F (F6-F10) */
    0, 0, 0, 0, 0, 0, 0x64, 0x44, 0x45,                                                                /* 50-58 (F11, F12) */
};

static inline uint8_t inb(uint16_t port) { uint8_t r; __asm__ volatile ("inb %1, %0" : "=a"(r) : "Nd"(port)); return r; }
static inline void outb(uint16_t port, uint8_t v) { __asm__ volatile ("outb %0, %1" : : "a"(v), "Nd"(port)); }

void keyboard_irq_handler(void) {
    uint8_t sc = inb(KBD_DATA);

    if (sc == 0xE0) { ext = 1; return; }
    if (sc == 0xE1 || sc == 0xFA || sc == 0xFE) return;           /* Pause-Praefix, ACK, Wiederholung (0xAA ist die Loslassen-Meldung der linken Umschalttaste) */
    int release = (sc & 0x80) != 0;
    uint8_t make = sc & 0x7F;
    if (ext) {                                                                  /* erweiterte Tasten: AltGr, rechte Strg, Pfeile, Pos1/Ende, Bild auf/ab, Einfg/Entf, Enter (Ziffernblock) */
        ext = 0;
        if (make == 0x38) { altgr = !release; return; }
        if (make == 0x1D) { usb_ctrl = !release; return; }
        if (release || make == 0x2A || make == 0x36) return;                    /* Schatten-Umschalttasten ignorieren */
        uint8_t k = make == 0x48 ? 0x52 : make == 0x50 ? 0x51 : make == 0x4B ? 0x50 : make == 0x4D ? 0x4F : make == 0x47 ? 0x4A : make == 0x4F ? 0x4D
                  : make == 0x49 ? 0x4B : make == 0x51 ? 0x4E : make == 0x52 ? 0x49 : make == 0x53 ? 0x4C : make == 0x1C ? 0x58 : 0;
        if (k) usb_key_event(k, shift_pressed, altgr);
        return;
    }
    if (make == 0x2A || make == 0x36) { shift_pressed = !release; return; }
    if (make == 0x1D) { usb_ctrl = !release; return; }
    if (release) return;
    if (make == 0x3A) { caps_lock = !caps_lock; return; }
    if (make >= sizeof(sc2hid)) return;
    uint8_t k = sc2hid[make];
    if (!k) return;
    int shift = shift_pressed;
    if (caps_lock && k >= 4 && k <= 29) shift = !shift;                        /* Feststelltaste wirkt nur auf Buchstaben */
    usb_key_event(k, shift, altgr);
}

/* ---- i8042-Controller ---- */
static int warte_ein(void) { for (int i = 0; i < 100000; i++) if (!(inb(KBD_CMD) & 2)) return 0; return -1; }
static int warte_aus(void) { for (int i = 0; i < 100000; i++) if (inb(KBD_CMD) & 1) return 0; return -1; }
static void leeren(void) { for (int i = 0; i < 32 && (inb(KBD_CMD) & 1); i++) inb(KBD_DATA); }
static int maus_befehl(uint8_t b) {
    if (warte_ein() || (outb(KBD_CMD, 0xD4), warte_ein())) return -1;
    outb(KBD_DATA, b);
    for (int i = 0; i < 4; i++) {                           /* bis zu vier Bytes lesen, bis das ACK kommt */
        if (warte_aus()) return -1;
        if (inb(KBD_DATA) == 0xFA) return 0;
    }
    return -1;
}

void keyboard_init(void) {
    if (inb(KBD_CMD) == 0xFF) return;                       /* kein i8042 (z. B. Hyper-V Generation 2) */
    pic_set_mask(1); pic_set_mask(12);                      /* waehrend der Einrichtung keine Unterbrechungen, sonst fressen sie die Antworten */                     
    leeren();
    if (warte_ein()) return;
    outb(KBD_CMD, 0xAE);                                    /* Tastaturanschluss einschalten */
    warte_ein(); outb(KBD_CMD, 0xA8);                       /* Mausanschluss einschalten */
    warte_ein(); outb(KBD_CMD, 0x20);
    if (warte_aus()) return;
    uint8_t cfg = inb(KBD_DATA);
    cfg |= 0x03;                                            /* IRQ 1 und IRQ 12 */
    cfg |= 0x40;                                            /* Scancode-Satz 1 (Umsetzung durch den Controller) */
    cfg &= (uint8_t)~0x30;                                  /* Takt beider Anschluesse an */
    warte_ein(); outb(KBD_CMD, 0x60);
    warte_ein(); outb(KBD_DATA, cfg);
    leeren();
    warte_ein(); outb(KBD_DATA, 0xF4);                      /* Tastatur: Abtastung einschalten */
    if (!warte_aus()) inb(KBD_DATA);
    /* Maus: Standardwerte, dann Datenstrom einschalten */
    if (maus_befehl(0xF6) == 0 && maus_befehl(0xF4) == 0) {
        mouse_ok = 1;
        pic_clear_mask(2); pic_clear_mask(12);
        kprint("[PS2] Maus/TrackPoint bereit\n");
    } else kprint("[PS2] Maus antwortet nicht\n");
    leeren();
    pic_clear_mask(1);
}

int ps2_mouse_present(void) { return mouse_ok; }

void mouse_irq_handler(void) {
    static uint8_t pk[3]; static int n = 0;
    if (!(inb(KBD_CMD) & 1)) return;
    uint8_t b = inb(KBD_DATA);
    if (n == 0 && (!(b & 8) || (b & 0xC0))) return;         /* kein gueltiges erstes Byte (ACK, Ueberlauf, ausser Takt) */
    pk[n++] = b;
    if (n < 3) return;
    n = 0;
    int dx = (int)pk[1] - (((int)pk[0] << 4) & 0x100);
    int dy = (int)pk[2] - (((int)pk[0] << 3) & 0x100);
    usb_ps2_mouse(dx, -dy, (uint8_t)(pk[0] & 7));
}
