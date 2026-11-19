/*
 * Zynex OS - xHCI-Treiber (USB 3.x Host Controller) mit HID
 * Ablauf: BIOS-Uebergabe, Reset, Ringe einrichten, Root-Ports zuruecksetzen, Geraete adressieren,
 * Beschreibungen lesen, HID-Geraete konfigurieren und ueber Interrupt-IN per Polling abfragen.
 * Nicht enthalten: Hubs, Hot-Plug, Massenspeicher, Interrupts/MSI.
 */
#include "usb.h"
#include "hyperv.h"
#include "layouts.h"
#include "pci.h"
#include "pmm.h"
#include "paging.h"
#include "timer.h"
#include "shell.h"
#include "multiboot.h"

extern void kprint(const char* str);
extern void kprint_dec(uint32_t val);
extern void kputchar(char c);
extern void kprint_hex(uint32_t val);

/* ---------- Grundtypen ---------- */
struct trb { uint64_t param; uint32_t status; uint32_t control; } __attribute__((packed));

#define RING_N 64                 /* Eintraege je Transfer-/Befehlsring (letzter = Link) */
#define EVT_N  256
#define MAX_DEV 8

struct ring { struct trb* t; uint32_t enq; uint32_t cycle; uint32_t n; };      /* n = 0: RING_N Eintraege */

struct hid_field { int valid; uint32_t off, size, count; int32_t lmin, lmax; int rel; };
struct hid_ptr {
    struct hid_field x, y, btn, tip, wheel;
    int has_id; uint8_t id;
};

struct usbdev {
    int used;
    uint8_t slot, port, speed;
    uint16_t vid, pid;
    int kind;                      /* 0 unbekannt, 1 Tastatur, 2 Zeiger */
    uint8_t dci;
    uint16_t mps;
    struct ring ep0, intr;
    uint8_t* buf;                  /* Empfangspuffer fuer Interrupt-IN */
    uint32_t buflen;
    struct hid_ptr hp;
    uint8_t prev[8];               /* letzter Tastaturbericht */
    /* USB-Netzwerk (kind 3) */
    int net;                       /* 1 = CDC-ECM, 2 = RNDIS */
    uint8_t dci_in, dci_out;
    uint16_t bmps;
    struct ring bin, bout;
    uint8_t *rxb, *txb;            /* je eine Seite */
    volatile int rx_done, tx_done, tx_ok;
    volatile uint32_t rx_len;
    uint8_t mac[6];
    /* USB-Audio (kind 4): je ein isochroner Kanal fuer Aufnahme (ain) und Wiedergabe (aout) */
    struct audep { int ok; uint8_t iface, alt, addr, dci, ch, ival; uint16_t mps; uint32_t rate, stride; struct ring r; uint8_t* buf; int aktiv; } ain, aout;
    uint8_t *inctx, *outctx;
};
#define NET_BUF 4096

static volatile uint8_t *cap, *op, *rt, *db;
static uint32_t ctxsz;
static uint32_t maxslots, maxports;
static uint64_t* dcbaa;
static struct ring cmdr;
static struct trb* evt;
static uint32_t evt_deq, evt_cycle;
static struct usbdev devs[MAX_DEV];
static int ready = 0;
static int have_kbd = 0;
static volatile int port_change = 0, scan_busy = 0, net_reenum = 0;
static uint8_t tried[64];
static int net_pref = 0;                    /* 0 = CDC-ECM bevorzugt, 1 = RNDIS bevorzugt */
static struct usbdev* netdev = 0;
static struct usbdev* auddev = 0;           /* USB-Audiogeraet (Mikrofon/Headset) */
extern void net_usb_attach(const uint8_t* mac);
extern void net_usb_detach(void);
static struct usb_pointer ptr;

/* Ergebnisse, auf die gewartet wird */
static volatile int cmd_done; static uint32_t cmd_code, cmd_slot;
static volatile int xfer_done; static uint32_t xfer_code, xfer_slot, xfer_dci;

static inline uint32_t r32(volatile uint8_t* b, uint32_t o) { return *(volatile uint32_t*)(b + o); }
static inline void w32(volatile uint8_t* b, uint32_t o, uint32_t v) { *(volatile uint32_t*)(b + o) = v; }
static inline void w64(volatile uint8_t* b, uint32_t o, uint64_t v) { w32(b, o, (uint32_t)v); w32(b, o + 4, (uint32_t)(v >> 32)); }
static inline void fence(void) { __asm__ volatile ("mfence" : : : "memory"); }

static void zero(void* p, uint32_t n) { uint8_t* b = (uint8_t*)p; for (uint32_t i = 0; i < n; i++) b[i] = 0; }
static void* seite(void) { void* p = pmm_alloc_page(); if (p) zero(p, 4096); return p; }
static void warte_ms(uint32_t ms) { uint32_t t0 = timer_get_ticks(); uint32_t n = (ms + 9) / 10; while (timer_get_ticks() - t0 < n) __asm__ volatile ("hlt"); }

/* ---------- Ringe ---------- */
static void ring_init(struct ring* r) {
    r->t = (struct trb*)seite();
    r->enq = 0; r->cycle = 1;
}

static void ring_push(struct ring* r, uint64_t param, uint32_t status, uint32_t control) {
    uint32_t n = r->n ? r->n : RING_N;
    struct trb* t = &r->t[r->enq];
    t->param = param; t->status = status;
    fence();
    t->control = control | r->cycle;
    r->enq++;
    if (r->enq == n - 1) {                         /* Link-TRB zurueck an den Anfang */
        struct trb* l = &r->t[n - 1];
        l->param = (uint64_t)(uintptr_t)r->t; l->status = 0;
        fence();
        l->control = (6u << 10) | (1u << 1) | r->cycle;   /* Link, Toggle Cycle */
        r->cycle ^= 1;
        r->enq = 0;
    }
}

/* ---------- HID ---------- */
static uint32_t bits(const uint8_t* d, uint32_t off, uint32_t n) {
    uint32_t v = 0;
    for (uint32_t i = 0; i < n; i++) if (d[(off + i) >> 3] & (1u << ((off + i) & 7))) v |= 1u << i;
    return v;
}
static int32_t sbits(const uint8_t* d, const struct hid_field* f, uint32_t idx) {
    uint32_t v = bits(d, f->off + idx * f->size, f->size);
    if (f->lmin < 0 && f->size < 32 && (v & (1u << (f->size - 1)))) return (int32_t)(v | (~0u << f->size));
    return (int32_t)v;
}

static void hid_parse(const uint8_t* d, int len, struct hid_ptr* p) {
    uint32_t page = 0, rsize = 0, rcount = 0, bitpos = 0;
    int32_t lmin = 0, lmax = 0;
    uint32_t usages[16]; int nus = 0; uint32_t umin = 0; int range = 0;
    uint8_t rid = 0;
    int i = 0;
    while (i < len) {
        uint8_t b = d[i++];
        if (b == 0xFE) { if (i < len) i += 2 + d[i]; continue; }   /* langes Element ueberspringen */
        int sz = b & 3; if (sz == 3) sz = 4;
        int type = (b >> 2) & 3, tag = b >> 4;
        int32_t v = 0; uint32_t uv = 0;
        for (int k = 0; k < sz && i + k < len; k++) uv |= (uint32_t)d[i + k] << (8 * k);
        v = (int32_t)uv;
        if (sz == 1) v = (int8_t)uv; else if (sz == 2) v = (int16_t)uv;
        i += sz;
        if (type == 1) {                                       /* Global */
            if (tag == 0) page = uv;
            else if (tag == 1) lmin = v;
            else if (tag == 2) lmax = v;
            else if (tag == 7) rsize = uv;
            else if (tag == 9) rcount = uv;
            else if (tag == 8) { rid = (uint8_t)uv; bitpos = 8; if (!p->has_id && !p->x.valid) { p->has_id = 1; p->id = rid; } }
        } else if (type == 2) {                                /* Local */
            if (tag == 0 && nus < 16) usages[nus++] = uv;
            else if (tag == 1) { umin = uv; range = 1; }
            else if (tag == 2) { range = 1; }
        } else if (type == 0) {                                /* Main */
            if (tag == 8) {                                    /* Input */
                int konst = uv & 1, rel = (uv >> 2) & 1;
                (void)rid;
                for (uint32_t k = 0; k < rcount; k++) {
                    uint32_t us = 0;
                    if (!konst) {
                        if (nus > 0) us = usages[k < (uint32_t)nus ? k : (uint32_t)nus - 1];
                        else if (range) us = umin + k;
                    }
                    uint32_t off = bitpos + k * rsize;
                    if (!konst) {
                        struct hid_field* f = 0;
                        if (page == 1 && us == 0x30 && !p->x.valid) f = &p->x;
                        else if (page == 1 && us == 0x31 && !p->y.valid) f = &p->y;
                        else if (page == 9 && us == 1 && !p->btn.valid) f = &p->btn;
                        else if (page == 0x0D && us == 0x42 && !p->tip.valid) f = &p->tip;
                        else if (page == 1 && us == 0x38 && !p->wheel.valid) f = &p->wheel;
                        if (f) { f->valid = 1; f->off = off; f->size = rsize; f->lmin = lmin; f->lmax = lmax; f->rel = rel; f->count = 1; }
                    }
                }
                bitpos += rcount * rsize;
            }
            nus = 0; range = 0;
        }
    }
}

/* Tastaturbelegung: Nummer in layouts.c (0 = Schweiz, Standard) */
static int layout_id = 0;
void usb_set_layout(int id) { if (id >= 0 && id < KBD_LAYOUTS) layout_id = id; }
int  usb_get_layout(void) { return layout_id; }
char usb_key_char(uint8_t k, int shift, int altgr) { return kbd_char(layout_id, k, shift, altgr); }

/* Tastenereignis (USB und PS/2): normal -> Zeichen an die Shell, mit Roh-Abgriff -> Tastennummer an den Empfaenger (Layout-Erkennung) */
static void (*raw_grab)(uint8_t k, int shift, int altgr) = 0;
int usb_ctrl;
void usb_set_rawgrab(void (*fn)(uint8_t k, int shift, int altgr)) { raw_grab = fn; }
void usb_key_event(uint8_t k, int shift, int altgr) {
    if (raw_grab) { raw_grab(k, shift, altgr); return; }
    char c = usb_key_char(k, shift, altgr);
    if (c) shell_handle_key(c);
}

/* PS/2-Maus (TrackPoint/Touchpad im Mausmodus): der Interrupt sammelt nur, die Hauptschleife wendet an */
static volatile int ps2_dx = 0, ps2_dy = 0, ps2_new = 0; static volatile uint8_t ps2_btn = 0;
void usb_ps2_mouse(int dx, int dy, uint8_t buttons) { ps2_dx += dx; ps2_dy += dy; ps2_btn = buttons; ps2_new = 1; }
static void ps2_apply(void) {
    if (!ps2_new) return;
    __asm__ volatile ("cli");
    int dx = ps2_dx, dy = ps2_dy; ps2_dx = 0; ps2_dy = 0; ps2_new = 0;
    __asm__ volatile ("sti");
    int w, h; fb_get_size(&w, &h);
    if (w <= 0 || h <= 0) return;
    if (!ptr.present) { ptr.x = w / 2; ptr.y = h / 2; }
    ptr.x += dx; ptr.y += dy;
    if (ptr.x < 0) ptr.x = 0;
    if (ptr.x > w - 1) ptr.x = w - 1;
    if (ptr.y < 0) ptr.y = 0;
    if (ptr.y > h - 1) ptr.y = h - 1;
    ptr.buttons = ps2_btn; ptr.down = ps2_btn & 1;
    ptr.present = 1; ptr.events++;
    fb_cursor_move(ptr.x, ptr.y);
}

static void key_report(struct usbdev* d, const uint8_t* r) {
    int shift = (r[0] & 0x22) != 0;
    int altgr = (r[0] & 0x40) != 0;
    usb_ctrl = (r[0] & 0x11) != 0;
    for (int i = 2; i < 8; i++) {
        uint8_t k = r[i];
        if (k < 4) continue;
        int schon = 0;
        for (int j = 2; j < 8; j++) if (d->prev[j] == k) schon = 1;
        if (schon) continue;
        usb_key_event(k, shift, altgr);
    }
    for (int i = 0; i < 8; i++) d->prev[i] = r[i];
}

static void ptr_report(struct usbdev* d, const uint8_t* r) {
    struct hid_ptr* p = &d->hp;
    if (p->has_id && r[0] != p->id) return;
    int w, h; fb_get_size(&w, &h);
    if (p->x.valid) {
        int32_t v = sbits(r, &p->x, 0);
        if (p->x.rel) { ptr.x += v; }
        else if (p->x.lmax > p->x.lmin) ptr.x = (int)((int64_t)(v - p->x.lmin) * (w - 1) / (p->x.lmax - p->x.lmin));
    }
    if (p->y.valid) {
        int32_t v = sbits(r, &p->y, 0);
        if (p->y.rel) { ptr.y += v; }
        else if (p->y.lmax > p->y.lmin) ptr.y = (int)((int64_t)(v - p->y.lmin) * (h - 1) / (p->y.lmax - p->y.lmin));
    }
    if (ptr.x < 0) ptr.x = 0;
    if (ptr.x > w - 1) ptr.x = w - 1;
    if (ptr.y < 0) ptr.y = 0;
    if (ptr.y > h - 1) ptr.y = h - 1;
    if (p->wheel.valid) { int32_t v = sbits(r, &p->wheel, 0); if (v > 8) v = 8; if (v < -8) v = -8; ptr.wheel += v; }
    uint8_t b = 0;
    if (p->btn.valid) b = (uint8_t)(bits(r, p->btn.off, 3));
    ptr.buttons = b;
    ptr.down = p->tip.valid ? (uint8_t)bits(r, p->tip.off, 1) : (b & 1);
    ptr.present = 1;
    ptr.events++;
    fb_cursor_move(ptr.x, ptr.y);
}

/* ---------- Ereignisse ---------- */
static void intr_queue(struct usbdev* d) {
    ring_push(&d->intr, (uint64_t)(uintptr_t)d->buf, d->buflen, (1u << 10) | (1u << 5) | (1u << 2));
    fence();
    w32(db, d->slot * 4, d->dci);
}

static void rx_queue(struct usbdev* d) {
    ring_push(&d->bin, (uint64_t)(uintptr_t)d->rxb, NET_BUF, (1u << 10) | (1u << 5) | (1u << 2));
    fence();
    w32(db, d->slot * 4, d->dci_in);
}

static void aud_event(struct usbdev* d, const struct trb* e, uint32_t dci, uint32_t code);
static void handle_event(const struct trb* e) {
    uint32_t type = (e->control >> 10) & 0x3F;
    uint32_t code = e->status >> 24;
    if (type == 34) { port_change = 1; return; }          /* Port-Statusaenderung: Hot-Plug */
    if (type == 33) {                                   /* Befehl abgeschlossen */
        cmd_code = code; cmd_slot = e->control >> 24; cmd_done = 1;
    } else if (type == 32) {                            /* Transfer abgeschlossen */
        uint32_t slot = e->control >> 24, dci = (e->control >> 16) & 0x1F;
        struct usbdev* d = 0;
        for (int i = 0; i < MAX_DEV; i++) if (devs[i].used && devs[i].slot == slot) d = &devs[i];
        if (d && d->kind == 4 && ((d->ain.aktiv && dci == d->ain.dci) || (d->aout.aktiv && dci == d->aout.dci))) { aud_event(d, e, dci, code); }
        else if (d && d->kind == 4) { /* verspaetetes Ereignis eines angehaltenen Kanals: ignorieren */ }
        else if (d && d->kind == 3 && (dci == d->dci_in || dci == d->dci_out)) {
            if (dci == d->dci_in) {
                if (code == 1 || code == 13) { d->rx_len = NET_BUF - (e->status & 0xFFFFFFu); d->rx_done = 1; }
                else rx_queue(d);
            } else { d->tx_ok = (code == 1 || code == 13); d->tx_done = 1; }
        } else if (d && d->kind && d->kind != 3 && dci == d->dci) {
            if (code == 1 || code == 13) {
                if (d->kind == 1) key_report(d, d->buf); else ptr_report(d, d->buf);
            }
            intr_queue(d);
        } else {
            xfer_code = code; xfer_slot = slot; xfer_dci = dci; xfer_done = 1;
        }
    }
}

void usb_abs_pointer(int x, int y, uint8_t buttons) {                  /* absolute Zeigerposition (Hyper-V-Maus) */
    ptr.x = x; ptr.y = y; ptr.buttons = buttons; ptr.down = buttons & 1; ptr.present = 1; ptr.events++;
    fb_cursor_move(ptr.x, ptr.y);
}

void usb_poll(void) {
    hv_poll();
    ps2_apply();
    if (!ready) return;
    for (int n = 0; n < 64; n++) {
        struct trb* e = &evt[evt_deq];
        fence();
        if ((e->control & 1) != evt_cycle) break;
        struct trb kopie = *e;
        evt_deq++;
        if (evt_deq == EVT_N) { evt_deq = 0; evt_cycle ^= 1; }
        w64(rt, 0x38, ((uint64_t)(uintptr_t)&evt[evt_deq]) | 8);      /* ERDP + EHB */
        handle_event(&kopie);
    }
}

static int warte_cmd(void) {
    uint32_t t0 = timer_get_ticks();
    while (!cmd_done) {
        usb_poll();
        if (timer_get_ticks() - t0 > 300) return -1;
    }
    return (cmd_code == 1) ? 0 : -(int)cmd_code;
}

static int befehl(uint64_t param, uint32_t status, uint32_t control, uint32_t* slot_out) {
    cmd_done = 0;
    ring_push(&cmdr, param, status, control);
    fence();
    w32(db, 0, 0);
    int r = warte_cmd();
    if (slot_out) *slot_out = cmd_slot;
    return r;
}

static void ep0_reset(struct usbdev* d) {
    befehl(0, 0, (14u << 10) | ((uint32_t)d->slot << 24) | (1u << 16), 0);             /* Reset Endpoint (DCI 1) */
    ring_init(&d->ep0);
    befehl((uint64_t)(uintptr_t)d->ep0.t | 1, 0, (16u << 10) | ((uint32_t)d->slot << 24) | (1u << 16), 0);   /* Set TR Dequeue Pointer */
}

/* Steuer-Transfer auf EP0. dir_in: 1 = Geraet -> Host */
static int ctrl(struct usbdev* d, uint8_t bm, uint8_t br, uint16_t wv, uint16_t wi, uint16_t wl, void* buf) {
    int in = (bm & 0x80) != 0;
    uint64_t setup = (uint64_t)bm | ((uint64_t)br << 8) | ((uint64_t)wv << 16) | ((uint64_t)wi << 32) | ((uint64_t)wl << 48);
    uint32_t trt = wl ? (in ? 3u : 2u) : 0u;
    ring_push(&d->ep0, setup, 8, (2u << 10) | (1u << 6) | (trt << 16));
    if (wl) ring_push(&d->ep0, (uint64_t)(uintptr_t)buf, wl, (3u << 10) | ((in ? 1u : 0u) << 16));
    ring_push(&d->ep0, 0, 0, (4u << 10) | (1u << 5) | ((wl && in) ? 0u : (1u << 16)));
    fence();
    xfer_done = 0;
    w32(db, d->slot * 4, 1);
    uint32_t t0 = timer_get_ticks();
    while (!xfer_done) {
        usb_poll();
        if (timer_get_ticks() - t0 > 300) return -1;
    }
    if (xfer_code == 1 || xfer_code == 13) return 0;
    if (xfer_code == 6) ep0_reset(d);                   /* STALL: Endpunkt zuruecksetzen, damit es weitergeht */
    return -(int)xfer_code;
}

/* ---------- Kontexte ---------- */
static uint32_t* cx(uint8_t* base, int idx) { return (uint32_t*)(base + (uint32_t)idx * ctxsz); }

static int adressiere(struct usbdev* d, uint8_t* inctx, uint8_t* outctx) {
    zero(inctx, 4096);
    uint32_t* icc = cx(inctx, 0);
    icc[1] = 3;                                           /* Add: Slot + EP0 */
    uint32_t* sl = cx(inctx, 1);
    sl[0] = ((uint32_t)d->speed << 20) | (1u << 27);
    sl[1] = (uint32_t)(d->port + 1) << 16;
    uint32_t* e0 = cx(inctx, 2);
    e0[1] = (3u << 1) | (4u << 3) | ((uint32_t)d->mps << 16);
    uint64_t trp = (uint64_t)(uintptr_t)d->ep0.t | 1;
    e0[2] = (uint32_t)trp; e0[3] = (uint32_t)(trp >> 32);
    e0[4] = 8;
    dcbaa[d->slot] = (uint64_t)(uintptr_t)outctx;
    return befehl((uint64_t)(uintptr_t)inctx, 0, (11u << 10) | ((uint32_t)d->slot << 24), 0);
}

static int konfiguriere_hid(struct usbdev* d, uint8_t* inctx, uint8_t* outctx, uint8_t ep_addr, uint16_t mps, uint8_t interval) {
    uint8_t dci = (uint8_t)((ep_addr & 0xF) * 2 + 1);       /* IN */
    d->dci = dci;
    zero(inctx, 4096);
    uint32_t* icc = cx(inctx, 0);
    icc[1] = 1u | (1u << dci);
    uint32_t* sl = cx(inctx, 1);
    for (uint32_t i = 0; i < 8; i++) sl[i] = cx(outctx, 0)[i];       /* Slot-Kontext uebernehmen */
    sl[0] = (sl[0] & 0x07FFFFFFu) | ((uint32_t)dci << 27);
    uint32_t* ep = cx(inctx, 1 + dci);
    uint32_t iv;
    if (d->speed == 1 || d->speed == 2) {                   /* Voll-/Niedriggeschwindigkeit: Ziel in 125-us-Schritten */
        uint32_t ms = interval ? interval : 1, e = 0;
        while ((1u << (e + 1)) <= ms) e++;
        iv = e + 3; if (iv > 10) iv = 10;
    } else {
        iv = interval ? (uint32_t)interval - 1 : 0;
    }
    ep[0] = iv << 16;
    ep[1] = (3u << 1) | (7u << 3) | ((uint32_t)mps << 16);
    ring_init(&d->intr);
    uint64_t trp = (uint64_t)(uintptr_t)d->intr.t | 1;
    ep[2] = (uint32_t)trp; ep[3] = (uint32_t)(trp >> 32);
    ep[4] = 8 | ((uint32_t)mps << 16);
    return befehl((uint64_t)(uintptr_t)inctx, 0, (12u << 10) | ((uint32_t)d->slot << 24), 0);
}

/* ---------- Geraet einrichten ---------- */
static int geraet(int port);
static int net_versuch(struct usbdev* d, uint8_t* inctx, uint8_t* outctx, uint8_t* tmp);
static int aud_versuch(struct usbdev* d, uint8_t* tmp);

static int geraet(int port) {
    int idx = -1;
    for (int i = 0; i < MAX_DEV; i++) if (!devs[i].used) { idx = i; break; }
    if (idx < 0) return -1;
    struct usbdev* d = &devs[idx];
    zero(d, sizeof(*d));
    d->port = (uint8_t)port;

    uint32_t sc = r32(op, 0x400 + 0x10 * (uint32_t)port);
    d->speed = (uint8_t)((sc >> 10) & 0xF);
    d->mps = d->speed == 4 ? 512 : d->speed == 3 ? 64 : 8;

    uint32_t slot;
    if (befehl(0, 0, 9u << 10, &slot) != 0 || slot == 0 || slot > maxslots) { kprint("[USB] Kein Slot\n"); return -2; }
    d->slot = (uint8_t)slot;
    ring_init(&d->ep0);
    uint8_t* inctx = (uint8_t*)seite();
    uint8_t* outctx = (uint8_t*)seite();
    d->buf = (uint8_t*)seite();
    uint8_t* tmp = d->buf + 2048;                          /* Arbeitsbereich fuer Beschreibungen */
    if (!inctx || !outctx || !d->buf || !d->ep0.t) return -3;
    if (adressiere(d, inctx, outctx) != 0) { kprint("[USB] Adressieren fehlgeschlagen\n"); return -4; }
    d->used = 1;
    d->inctx = inctx; d->outctx = outctx;

    if (ctrl(d, 0x80, 6, 0x0100, 0, 18, tmp) != 0) { kprint("[USB] Geraetebeschreibung nicht lesbar\n"); d->used = 0; return -5; }
    d->vid = (uint16_t)(tmp[8] | (tmp[9] << 8));
    d->pid = (uint16_t)(tmp[10] | (tmp[11] << 8));
    if (d->speed != 3 && d->speed != 4 && tmp[7] > 8 && d->speed == 1) { /* Voll: echte EP0-Groesse */ d->mps = tmp[7]; }

    {
        int nr = net_versuch(d, inctx, outctx, tmp);                 /* zuerst: Netzwerkgeraet (ECM/RNDIS)? */
        if (nr < 0) { kprint("[USB] Netzwerkgeraet konnte nicht gestartet werden ("); kprint_dec((uint32_t)-nr); kprint(")\n"); d->used = 0; return nr; }
        if (nr > 0) return 0;
    }
    if (aud_versuch(d, tmp) > 0) return 0;                          /* USB-Audio (Mikrofon, Headset, Lautsprecher)? */
    if (ctrl(d, 0x80, 6, 0x0200, 0, 9, tmp) != 0) { d->used = 0; return -6; }
    uint16_t total = (uint16_t)(tmp[2] | (tmp[3] << 8));
    if (total > 1024) total = 1024;
    if (ctrl(d, 0x80, 6, 0x0200, 0, total, tmp) != 0) { d->used = 0; return -7; }
    uint8_t cfgval = tmp[5];

    /* Schnittstellen durchsehen: erste HID-Schnittstelle mit Interrupt-IN-Endpunkt */
    int iface = -1, sub = 0, prot = 0, rdlen = 0;
    uint8_t epa = 0, epi = 0; uint16_t epm = 0;
    int cur = -1, cursub = 0, curprot = 0, currd = 0;
    for (int o = 0; o + 2 <= total && tmp[o] >= 2; o += tmp[o]) {
        uint8_t ty = tmp[o + 1];
        if (ty == 4) {
            cur = (tmp[o + 5] == 3) ? tmp[o + 2] : -1;
            cursub = tmp[o + 6]; curprot = tmp[o + 7]; currd = 0;
        } else if (ty == 0x21 && cur >= 0) {
            currd = tmp[o + 7] | (tmp[o + 8] << 8);
        } else if (ty == 5 && cur >= 0 && iface < 0 && (tmp[o + 2] & 0x80) && (tmp[o + 3] & 3) == 3) {
            iface = cur; sub = cursub; prot = curprot; rdlen = currd;
            epa = tmp[o + 2]; epm = (uint16_t)(tmp[o + 4] | (tmp[o + 5] << 8)) & 0x7FF; epi = tmp[o + 6];
        }
    }
    if (iface < 0) { kprint("[USB] Geraet ohne HID-Schnittstelle (nicht unterstuetzt)\n"); d->used = 0; return 0; }

    if (sub == 1 && prot == 1) d->kind = 1;                /* Boot-Tastatur */
    else {
        if (rdlen > 1024) rdlen = 1024;
        if (rdlen == 0 || ctrl(d, 0x81, 6, 0x2200, (uint16_t)iface, (uint16_t)rdlen, tmp) != 0) { d->used = 0; return -8; }
        hid_parse(tmp, rdlen, &d->hp);
        if (!d->hp.x.valid || !d->hp.y.valid) { kprint("[USB] HID-Geraet ohne X/Y (nicht unterstuetzt)\n"); d->used = 0; return 0; }
        d->kind = 2;
    }
    if (ctrl(d, 0x00, 9, cfgval, 0, 0, 0) != 0) { d->used = 0; return -9; }
    if (d->kind == 1) ctrl(d, 0x21, 0x0B, 0, (uint16_t)iface, 0, 0);       /* Boot-Protokoll */
    ctrl(d, 0x21, 0x0A, 0, (uint16_t)iface, 0, 0);                          /* Idle: nur bei Aenderung */

    d->buflen = epm > 64 ? 64 : epm;
    if (d->buflen == 0) d->buflen = 8;
    zero(d->buf, 64);
    if (konfiguriere_hid(d, inctx, outctx, epa, epm, epi) != 0) { kprint("[USB] Endpunkt-Konfiguration fehlgeschlagen\n"); d->used = 0; return -10; }
    intr_queue(d);
    if (d->kind == 1) have_kbd = 1;
    kprint("[USB] ");
    kprint(d->kind == 1 ? "Tastatur" : "Zeiger/Touch");
    kprint(" an Port "); kprint_dec((uint32_t)port + 1);
    kprint(" (Slot "); kprint_dec(slot); kprint(")\n");
    return 0;
}


/* ---------- USB-Netzwerk: CDC-ECM und RNDIS (USB-Tethering) ---------- */
struct netinfo { int proto; uint8_t cfg, ctl_if, data_if, data_alt, ep_in, ep_out, mac_idx; uint16_t mps; };

static uint32_t le32(const uint8_t* p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static void put_le32(uint8_t* p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }

/* Konfigurationsbeschreibung nach ECM/RNDIS durchsuchen; liefert 1 = ECM, 2 = RNDIS, 0 = keins */
static int net_parse(const uint8_t* c, int total, struct netinfo* out) {
    int ecm = -1, rn = -1, cur = -1, curalt = 0, curcls = 0; uint8_t mac_idx = 0;
    int data_if = -1, data_alt = 0; uint8_t ein = 0, eout = 0; uint16_t mps = 0;
    int cand = -1, calt = 0; uint8_t cin = 0, cout = 0; uint16_t cm = 0;
    for (int o = 0; o + 2 <= total && c[o] >= 2; o += c[o]) {
        uint8_t ty = c[o + 1];
        if (ty == 4 && c[o] >= 9) {
            if (cand >= 0 && cin && cout && data_if < 0) { data_if = cand; data_alt = calt; ein = cin; eout = cout; mps = cm; }
            cur = c[o + 2]; curalt = c[o + 3]; curcls = c[o + 5];
            uint8_t sub = c[o + 6], pro = c[o + 7];
            if (curcls == 2 && sub == 6 && curalt == 0) ecm = cur;
            else if (curalt == 0 && ((curcls == 0xE0 && sub == 1 && pro == 3) || (curcls == 2 && sub == 2 && pro == 0xFF))) rn = cur;
            if (curcls == 0x0A) { cand = cur; calt = curalt; cin = cout = 0; cm = 0; } else cand = -1;
        } else if (ty == 0x24 && c[o] >= 5 && c[o + 2] == 0x0F && cur == ecm) {
            mac_idx = c[o + 3];
        } else if (ty == 5 && c[o] >= 7 && cand >= 0 && (c[o + 3] & 3) == 2) {
            if (c[o + 2] & 0x80) cin = c[o + 2]; else cout = c[o + 2];
            cm = (uint16_t)(c[o + 4] | (c[o + 5] << 8)) & 0x7FF;
        }
    }
    if (cand >= 0 && cin && cout && data_if < 0) { data_if = cand; data_alt = calt; ein = cin; eout = cout; mps = cm; }
    if (data_if < 0 || (ecm < 0 && rn < 0)) return 0;
    int proto = (ecm >= 0 && rn >= 0) ? (net_pref ? 2 : 1) : (ecm >= 0 ? 1 : 2);
    out->proto = proto; out->ctl_if = (uint8_t)(proto == 1 ? ecm : rn); out->data_if = (uint8_t)data_if; out->data_alt = (uint8_t)data_alt;
    out->ep_in = ein; out->ep_out = eout; out->mps = mps; out->mac_idx = mac_idx;
    return proto;
}

static int konfiguriere_bulk(struct usbdev* d, uint8_t* inctx, uint8_t* outctx, uint8_t ep_in, uint8_t ep_out, uint16_t mps) {
    uint8_t di = (uint8_t)((ep_in & 0xF) * 2 + 1), dout = (uint8_t)((ep_out & 0xF) * 2);
    d->dci_in = di; d->dci_out = dout; d->bmps = mps;
    uint8_t maxd = di > dout ? di : dout;
    zero(inctx, 4096);
    uint32_t* icc = cx(inctx, 0);
    icc[1] = 1u | (1u << di) | (1u << dout);
    uint32_t* sl = cx(inctx, 1);
    for (uint32_t i = 0; i < 8; i++) sl[i] = cx(outctx, 0)[i];
    sl[0] = (sl[0] & 0x07FFFFFFu) | ((uint32_t)maxd << 27);
    ring_init(&d->bin); ring_init(&d->bout);
    if (!d->bin.t || !d->bout.t) return -1;
    uint32_t* ei = cx(inctx, 1 + di);
    ei[0] = 0; ei[1] = (3u << 1) | (6u << 3) | ((uint32_t)mps << 16);
    uint64_t t1 = (uint64_t)(uintptr_t)d->bin.t | 1; ei[2] = (uint32_t)t1; ei[3] = (uint32_t)(t1 >> 32); ei[4] = 1024;
    uint32_t* eo = cx(inctx, 1 + dout);
    eo[0] = 0; eo[1] = (3u << 1) | (2u << 3) | ((uint32_t)mps << 16);
    uint64_t t2 = (uint64_t)(uintptr_t)d->bout.t | 1; eo[2] = (uint32_t)t2; eo[3] = (uint32_t)(t2 >> 32); eo[4] = 1024;
    return befehl((uint64_t)(uintptr_t)inctx, 0, (12u << 10) | ((uint32_t)d->slot << 24), 0);
}

/* RNDIS-Steuerbefehl senden und auf die Antwort warten (Antwort bei tmp + 1024) */
static int rndis_cmd(struct usbdev* d, uint8_t ctl_if, uint8_t* tmp, uint32_t len, uint32_t want) {
    if (ctrl(d, 0x21, 0x00, 0, ctl_if, (uint16_t)len, tmp) != 0) return -1;
    for (int t = 0; t < 60; t++) {
        zero(tmp + 1024, 64);
        if (ctrl(d, 0xA1, 0x01, 0, ctl_if, 1024, tmp + 1024) == 0 && le32(tmp + 1024) == want) return 0;
        warte_ms(10);
    }
    return -2;
}

static int rndis_start(struct usbdev* d, const struct netinfo* ni, uint8_t* tmp) {
    zero(tmp, 64);
    put_le32(tmp, 2); put_le32(tmp + 4, 24); put_le32(tmp + 8, 1); put_le32(tmp + 12, 1); put_le32(tmp + 16, 0); put_le32(tmp + 20, NET_BUF);
    if (rndis_cmd(d, ni->ctl_if, tmp, 24, 0x80000002u) != 0 || le32(tmp + 1024 + 12) != 0) return -1;
    zero(tmp, 64);
    put_le32(tmp, 4); put_le32(tmp + 4, 28); put_le32(tmp + 8, 2); put_le32(tmp + 12, 0x01010101u);       /* OID_802_3_PERMANENT_ADDRESS */
    if (rndis_cmd(d, ni->ctl_if, tmp, 28, 0x80000004u) != 0 || le32(tmp + 1024 + 12) != 0) return -2;
    uint32_t ol = le32(tmp + 1024 + 16), oo = le32(tmp + 1024 + 20);
    if (ol < 6 || oo > 200) return -3;
    for (int i = 0; i < 6; i++) d->mac[i] = tmp[1024 + 8 + oo + (uint32_t)i];
    zero(tmp, 64);
    put_le32(tmp, 5); put_le32(tmp + 4, 32); put_le32(tmp + 8, 3); put_le32(tmp + 12, 0x0001010Eu);       /* OID_GEN_CURRENT_PACKET_FILTER */
    put_le32(tmp + 16, 4); put_le32(tmp + 20, 20); put_le32(tmp + 28, 0x0F);
    if (rndis_cmd(d, ni->ctl_if, tmp, 32, 0x80000005u) != 0 || le32(tmp + 1024 + 12) != 0) return -4;
    return 0;
}

static int hexw(uint8_t c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'A' && c <= 'F' ? c - 'A' + 10 : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1; }

/* Rueckgabe: 0 = kein Netzwerkgeraet, 1 = aufgenommen, <0 = Fehler */
static int net_versuch(struct usbdev* d, uint8_t* inctx, uint8_t* outctx, uint8_t* tmp) {
    uint8_t numcfg = tmp[17]; if (!numcfg || numcfg > 4) numcfg = 1;
    struct netinfo best = {0};
    for (uint8_t ci = 0; ci < numcfg; ci++) {
        if (ctrl(d, 0x80, 6, (uint16_t)(0x0200 | ci), 0, 9, tmp) != 0) break;
        uint16_t total = (uint16_t)(tmp[2] | (tmp[3] << 8)); if (total > 1024) total = 1024;
        if (ctrl(d, 0x80, 6, (uint16_t)(0x0200 | ci), 0, total, tmp) != 0) break;
        struct netinfo ni = {0}; uint8_t cfg = tmp[5];
        int p = net_parse(tmp, total, &ni);
        if (p && (!best.proto || (p == (net_pref ? 2 : 1) && best.proto != p))) { best = ni; best.cfg = cfg; }
    }
    if (!best.proto) return 0;
    d->net = best.proto;
    if (ctrl(d, 0x00, 9, best.cfg, 0, 0, 0) != 0) return -20;
    if (best.proto == 1) {
        zero(tmp, 128);
        if (!best.mac_idx || ctrl(d, 0x80, 6, (uint16_t)(0x0300 | best.mac_idx), 0x0409, 64, tmp) != 0) return -21;
        for (int i = 0; i < 6; i++) {
            int h = hexw(tmp[2 + 4 * i]), l = hexw(tmp[2 + 4 * i + 2]);
            if (h < 0 || l < 0) return -22;
            d->mac[i] = (uint8_t)(h * 16 + l);
        }
        if (ctrl(d, 0x01, 11, best.data_alt, best.data_if, 0, 0) != 0) return -23;
        ctrl(d, 0x21, 0x43, 0x000E, best.ctl_if, 0, 0);                    /* Paketfilter: direkt, Broadcast, alle Multicast */
    } else {
        int r = rndis_start(d, &best, tmp);
        if (r != 0) { kprint("[USB] RNDIS-Start fehlgeschlagen ("); kprint_dec((uint32_t)-r); kprint(")\n"); return -30 + r; }
    }
    d->rxb = (uint8_t*)seite(); d->txb = (uint8_t*)seite();
    if (!d->rxb || !d->txb) return -24;
    if (konfiguriere_bulk(d, inctx, outctx, best.ep_in, best.ep_out, best.mps) != 0) { kprint("[USB] Bulk-Endpunkte nicht konfigurierbar\n"); return -25; }
    d->kind = 3;
    d->rx_done = 0; d->tx_done = 0;
    rx_queue(d);
    netdev = d;
    kprint("[USB] Netzwerk ("); kprint(best.proto == 1 ? "CDC-ECM" : "RNDIS"); kprint(") an Port "); kprint_dec((uint32_t)d->port + 1);
    kprint(", MAC ");
    for (int i = 0; i < 6; i++) { kputchar("0123456789abcdef"[d->mac[i] >> 4]); kputchar("0123456789abcdef"[d->mac[i] & 15]); if (i < 5) kputchar(':'); }
    kprint("\n");
    net_usb_attach(d->mac);
    return 1;
}

int usbnet_present(void) { return netdev != 0; }

int usbnet_send(const uint8_t* f, int len) {
    struct usbdev* d = netdev;
    if (!d || !d->used || len <= 0 || len > 1600) return -1;
    uint8_t* p = d->txb; uint32_t n = (uint32_t)len;
    if (d->net == 2) {
        zero(p, 44); put_le32(p, 1); put_le32(p + 4, 44 + (uint32_t)len); put_le32(p + 8, 36); put_le32(p + 12, (uint32_t)len);
        for (int i = 0; i < len; i++) p[44 + i] = f[i];
        n = 44 + (uint32_t)len;
    } else for (int i = 0; i < len; i++) p[i] = f[i];
    int zlp = d->bmps && (n % d->bmps) == 0;
    d->tx_done = 0;
    ring_push(&d->bout, (uint64_t)(uintptr_t)p, n, (1u << 10) | (zlp ? 0u : (1u << 5)));
    if (zlp) ring_push(&d->bout, (uint64_t)(uintptr_t)p, 0, (1u << 10) | (1u << 5));
    fence();
    w32(db, d->slot * 4, d->dci_out);
    uint32_t t0 = timer_get_ticks();
    while (!d->tx_done) { usb_poll(); if (timer_get_ticks() - t0 > 50) return -2; }
    return d->tx_ok ? 0 : -3;
}

void usbnet_poll(void (*cb)(const uint8_t*, int)) {
    usb_poll();
    struct usbdev* d = netdev;
    if (!d || !d->used || !d->rx_done) return;
    uint32_t n = d->rx_len; d->rx_done = 0;
    if (d->net == 1) { if (n >= 14) cb(d->rxb, (int)n); }
    else {
        uint32_t off = 0;
        while (off + 44 <= n) {
            uint32_t ty = le32(d->rxb + off), ln = le32(d->rxb + off + 4);
            if (ln < 8 || off + ln > n) break;
            if (ty == 1) {
                uint32_t doff = le32(d->rxb + off + 8), dl = le32(d->rxb + off + 12);
                if (8 + doff + dl <= ln && dl >= 14) cb(d->rxb + off + 8 + doff, (int)dl);
            }
            off += ln;
        }
    }
    rx_queue(d);
}

/* ---------- Hot-Plug ---------- */
static int port_aufnehmen(uint32_t pt) {
    uint32_t off = 0x400 + 0x10 * pt;
    uint32_t sc = r32(op, off);
    if (!(sc & 1)) return -1;
    w32(op, off, (sc & ~((1u << 1) | (0x7Fu << 17))) | (1u << 4));      /* Port-Reset */
    uint32_t t0 = timer_get_ticks();
    while (!(r32(op, off) & (1u << 21)) && timer_get_ticks() - t0 < 100) {}
    sc = r32(op, off);
    w32(op, off, (sc & ~((1u << 1) | (0x7Fu << 17))) | (1u << 21));    /* PRC loeschen */
    warte_ms(20);
    sc = r32(op, off);
    if (!(sc & 2)) return -2;
    return geraet((int)pt);
}

static void entfernen(struct usbdev* d) {
    kprint("[USB] Geraet an Port "); kprint_dec((uint32_t)d->port + 1); kprint(" entfernt\n");
    if (d->kind == 3) { netdev = 0; net_usb_detach(); }
    if (d->kind == 4 && auddev == d) auddev = 0;
    d->used = 0;
    befehl(0, 0, (10u << 10) | ((uint32_t)d->slot << 24), 0);           /* Disable Slot */
    dcbaa[d->slot] = 0;
}

void usb_hotplug(void) {
    if (!ready || !port_change || scan_busy) return;
    scan_busy = 1; port_change = 0;
    if (net_reenum) {
        net_reenum = 0;
        for (int i = 0; i < MAX_DEV; i++) if (devs[i].used && devs[i].kind == 3) { tried[devs[i].port] = 0; entfernen(&devs[i]); }
    }
    for (uint32_t pt = 0; pt < maxports && pt < 64; pt++) {
        uint32_t off = 0x400 + 0x10 * pt, sc = r32(op, off);
        w32(op, off, (sc & (1u << 9)) | (sc & (0x7Fu << 17)));          /* Aenderungsbits quittieren, Port-Strom behalten */
        int dev = -1;
        for (int i = 0; i < MAX_DEV; i++) if (devs[i].used && devs[i].port == pt) dev = i;
        if (!(sc & 1)) { tried[pt] = 0; if (dev >= 0) entfernen(&devs[dev]); }
        else if (dev < 0 && !tried[pt]) { tried[pt] = 1; warte_ms(100); port_aufnehmen(pt); }
    }
    scan_busy = 0;
}

/* Bevorzugtes Protokoll waehlen (0 = ECM, 1 = RNDIS) und das Netzwerkgeraet neu aufnehmen */
void usb_net_prefer(int rndis) { net_pref = rndis ? 1 : 0; net_reenum = 1; port_change = 1; }

/* ---------- USB-Audio (Klasse 1, UAC1, PCM 16 Bit, isochron) ---------- */
#define AUD_N 256                                  /* Ringgroesse (eine Seite): 255 Pakete = 255 ms Puffer */
static volatile uint32_t a_erster_code, a_codes[32];
static volatile int a_ein_n, a_aus_gepostet, a_aus_fertig, a_aus_ende, a_fehler;
static int16_t* a_rec; static uint32_t a_rec_max, a_rec_n;
static uint32_t (*a_fill)(int16_t*, uint32_t, void*); static void* a_ctx;
static char aud_info[96] = "kein USB-Audiogeraet";

static void aud_kopie_info(const char* s) { int i = 0; while (s[i] && i < 95) { aud_info[i] = s[i]; i++; } aud_info[i] = 0; }

/* Beschreibung durchsehen: je Richtung die erste passende Alternativeinstellung (16 Bit PCM, 1-2 Kanaele, bevorzugt 48 kHz) */
static void aud_parse(const uint8_t* c, int total, struct usbdev* d) {
    int iface = -1, alt = -1, asif = 0, ch = 0, bits = 0, ftyp = -1, fo = 0; int ok_fmt = 0; uint32_t rate = 0;
    for (int o = 0; o + 2 <= total && c[o] >= 2; o += c[o]) {
        uint8_t ty = c[o + 1];
        if (ty == 4) {
            asif = (c[o + 5] == 1 && c[o + 6] == 2 && c[o + 7] == 0);                 /* Audio-Streaming, UAC1 */
            iface = c[o + 2]; alt = c[o + 3]; ok_fmt = 0;
        } else if (ty == 0x24 && asif && alt > 0 && c[o + 2] == 2 && c[o + 3] == 1) {  /* FORMAT_TYPE_I */
            ch = c[o + 4]; bits = c[o + 6]; ftyp = c[o + 7]; fo = o; ok_fmt = 0; rate = 0;
            if (c[o + 5] != 2 || bits != 16 || ch < 1 || ch > 2) continue;
            if (ftyp == 0) { uint32_t lo = (uint32_t)c[o + 8] | ((uint32_t)c[o + 9] << 8) | ((uint32_t)c[o + 10] << 16), hi = (uint32_t)c[o + 11] | ((uint32_t)c[o + 12] << 8) | ((uint32_t)c[o + 13] << 16); rate = (lo <= 48000 && 48000 <= hi) ? 48000 : lo; ok_fmt = 1; }
            else { for (int k = 0; k < ftyp && fo + 8 + 3 * k + 2 < o + c[o] + 1; k++) { uint32_t f = (uint32_t)c[o + 8 + 3 * k] | ((uint32_t)c[o + 9 + 3 * k] << 8) | ((uint32_t)c[o + 10 + 3 * k] << 16); if (f == 48000 || !rate) rate = f; if (f == 48000) break; } ok_fmt = rate != 0; }
        } else if (ty == 5 && asif && alt > 0 && ok_fmt && (c[o + 3] & 3) == 1) {      /* isochroner Endpunkt */
            struct audep* e = (c[o + 2] & 0x80) ? &d->ain : &d->aout;
            uint16_t mps = (uint16_t)((c[o + 4] | (c[o + 5] << 8)) & 0x7FF);
            if (e->ok || mps == 0 || mps > 1000) continue;
            e->ok = 1; e->iface = (uint8_t)iface; e->alt = (uint8_t)alt; e->addr = c[o + 2]; e->dci = (uint8_t)((c[o + 2] & 0xF) * 2 + ((c[o + 2] & 0x80) ? 1 : 0));
            e->ch = (uint8_t)ch; e->mps = mps; e->ival = c[o + 6] ? c[o + 6] : 1; e->rate = rate;
            if (!(c[o + 2] & 0x80) && rate != 48000) e->ok = 0;                         /* Ausgabe nur mit 48 kHz */
        }
    }
}

static int aud_versuch(struct usbdev* d, uint8_t* tmp) {
    if (ctrl(d, 0x80, 6, 0x0200, 0, 9, tmp) != 0) return 0;
    uint16_t total = (uint16_t)(tmp[2] | (tmp[3] << 8)); if (total > 1024) total = 1024;
    if (ctrl(d, 0x80, 6, 0x0200, 0, total, tmp) != 0) return 0;
    uint8_t cfg = tmp[5];
    aud_parse(tmp, total, d);
    if (!d->ain.ok && !d->aout.ok) return 0;
    if (ctrl(d, 0x00, 9, cfg, 0, 0, 0) != 0) { zero(&d->ain, sizeof(d->ain)); zero(&d->aout, sizeof(d->aout)); return 0; }
    d->kind = 4; auddev = d;
    kprint("[USB] Audio an Port "); kprint_dec((uint32_t)d->port + 1); kprint(":");
    if (d->aout.ok) { kprint(" Wiedergabe "); kprint_dec(d->aout.ch); kprint("ch 48000 Hz"); }
    if (d->ain.ok) { kprint(" Aufnahme "); kprint_dec(d->ain.ch); kprint("ch "); kprint_dec(d->ain.rate); kprint(" Hz"); }
    kprint("\n");
    aud_kopie_info((d->ain.ok && d->aout.ok) ? "USB-Audio: Wiedergabe und Aufnahme" : d->ain.ok ? "USB-Audio: nur Aufnahme" : "USB-Audio: nur Wiedergabe");
    return 1;
}

static void aud_push(struct usbdev* d, struct audep* e, uint32_t len) {
    uint32_t pos = e->r.enq;
    ring_push(&e->r, (uint64_t)(uintptr_t)(e->buf + pos * e->stride), len, (5u << 10) | (1u << 5) | (1u << 31));   /* Isoch-TRB, IOC, SIA */
}

static int aud_ep_an(struct usbdev* d, struct audep* e, int ausgabe) {
    if (ctrl(d, 0x01, 11, e->alt, e->iface, 0, 0) != 0) return -1;                  /* Alternativeinstellung mit Daten */
    if (e->rate) { uint8_t f[3] = { (uint8_t)e->rate, (uint8_t)(e->rate >> 8), (uint8_t)(e->rate >> 16) }; uint8_t* t = d->buf + 3072; t[0] = f[0]; t[1] = f[1]; t[2] = f[2]; ctrl(d, 0x22, 1, 0x0100, e->addr, 3, t); }   /* Abtastrate setzen (Fehler egal) */
    if (!e->r.t) {
        e->r.n = AUD_N; e->r.t = (struct trb*)seite();
        e->stride = (uint32_t)((e->mps + 63) & ~63);
        uint32_t seiten = ((AUD_N - 1) * e->stride + 4095) / 4096;
        e->buf = (uint8_t*)pmm_alloc_pages(seiten);
        if (!e->r.t || !e->buf) return -2;
    } else zero(e->r.t, 4096);
    e->r.enq = 0; e->r.cycle = 1;
    zero(d->inctx, 4096);
    uint32_t* icc = cx(d->inctx, 0);
    icc[1] = 1u | (1u << e->dci);
    uint32_t* sl = cx(d->inctx, 1);
    for (uint32_t i = 0; i < 8; i++) sl[i] = cx(d->outctx, 0)[i];
    uint32_t cur = sl[0] >> 27; if (cur < e->dci) cur = e->dci;
    sl[0] = (sl[0] & 0x07FFFFFFu) | (cur << 27);
    uint32_t* ep = cx(d->inctx, 1 + e->dci);
    uint32_t iv = (d->speed == 1 || d->speed == 2) ? (uint32_t)e->ival + 2 : (uint32_t)e->ival - 1;
    if (iv > 15) iv = 15;
    ep[0] = iv << 16;
    ep[1] = (ausgabe ? 1u : 5u) << 3 | ((uint32_t)e->mps << 16);                   /* Isoch OUT = 1, Isoch IN = 5, CErr 0 */
    uint64_t trp = (uint64_t)(uintptr_t)e->r.t | 1;
    ep[2] = (uint32_t)trp; ep[3] = (uint32_t)(trp >> 32);
    ep[4] = (uint32_t)e->mps | ((uint32_t)e->mps << 16);
    if (befehl((uint64_t)(uintptr_t)d->inctx, 0, (12u << 10) | ((uint32_t)d->slot << 24), 0) != 0) return -3;
    return 0;
}

static void aud_ep_aus(struct usbdev* d, struct audep* e) {
    e->aktiv = 0;
    befehl(0, 0, (15u << 10) | ((uint32_t)d->slot << 24) | ((uint32_t)e->dci << 16), 0);        /* Stop Endpoint */
    zero(d->inctx, 4096);
    uint32_t* icc = cx(d->inctx, 0);
    icc[0] = 1u << e->dci; icc[1] = 1;
    uint32_t* sl = cx(d->inctx, 1);
    for (uint32_t i = 0; i < 8; i++) sl[i] = cx(d->outctx, 0)[i];
    befehl((uint64_t)(uintptr_t)d->inctx, 0, (12u << 10) | ((uint32_t)d->slot << 24), 0);       /* Endpunkt entfernen */
    ctrl(d, 0x01, 11, 0, e->iface, 0, 0);                                                        /* Alternativeinstellung 0: kein Strom */
}

static uint32_t aud_fuell_paket(struct usbdev* d, struct audep* e) {                              /* 1 ms Wiedergabe in den naechsten Puffer */
    int16_t st[96]; uint32_t n = a_fill ? a_fill(st, 48, a_ctx) : 0;
    if (n > 48) n = 48;
    if (n < 48) { for (uint32_t i = n * 2; i < 96; i++) st[i] = 0; a_aus_ende = 1; }
    int16_t* o = (int16_t*)(e->buf + e->r.enq * e->stride);
    if (e->ch == 2) for (int i = 0; i < 96; i++) o[i] = st[i];
    else for (int i = 0; i < 48; i++) o[i] = (int16_t)(((int)st[2 * i] + st[2 * i + 1]) / 2);
    return 48u * e->ch * 2;
}

static void aud_event(struct usbdev* d, const struct trb* ev, uint32_t dci, uint32_t code) {
    int ok = (code == 1 || code == 13 || code == 14 || code == 15 || code == 23);          /* Erfolg, kurzes Paket, Unter-/Ueberlauf, verpasst: weiter */
    a_codes[code & 31]++;
    if (!(code == 1 || code == 13)) { a_fehler++; if (!a_erster_code) a_erster_code = code; }
    if (dci == d->ain.dci && d->ain.aktiv) {
        struct audep* e = &d->ain;
        uint64_t off = (uint64_t)ev->param - (uint64_t)(uintptr_t)e->r.t;
        uint32_t pos = (uint32_t)(off / 16);
        if ((code == 1 || code == 13) && pos < AUD_N - 1 && a_rec) {
            uint32_t got = e->mps - (ev->status & 0xFFFFFFu); if (got > e->mps) got = 0;
            const int16_t* sp = (const int16_t*)(e->buf + pos * e->stride);
            uint32_t fr = got / (2u * e->ch);
            for (uint32_t i = 0; i < fr && a_rec_n < a_rec_max; i++)
                a_rec[a_rec_n++] = e->ch == 2 ? (int16_t)(((int)sp[2 * i] + sp[2 * i + 1]) / 2) : sp[i];
        }
        if (ok && a_rec_n < a_rec_max) { aud_push(d, e, e->mps); fence(); w32(db, d->slot * 4, e->dci); }
        a_ein_n++;
    } else if (dci == d->aout.dci && d->aout.aktiv) {
        struct audep* e = &d->aout;
        a_aus_fertig++;
        if (ok && !a_aus_ende) { uint32_t l = aud_fuell_paket(d, e); aud_push(d, e, l); a_aus_gepostet++; fence(); w32(db, d->slot * 4, e->dci); }
    }
}

int usbaud_hat_ausgabe(void) { return auddev && auddev->used && auddev->aout.ok; }
int usbaud_hat_eingabe(void) { return auddev && auddev->used && auddev->ain.ok; }
const char* usbaud_info(void) { return auddev && auddev->used ? aud_info : "kein USB-Audiogeraet"; }

int usbaud_spielen_strom(uint32_t (*fill)(int16_t* stereo, uint32_t frames, void* ctx), void* ctx) {
    struct usbdev* d = auddev; if (!d || !d->used || !d->aout.ok) return -1;
    struct audep* e = &d->aout;
    a_fill = fill; a_ctx = ctx; a_aus_gepostet = 0; a_aus_fertig = 0; a_aus_ende = 0; a_fehler = 0; a_erster_code = 0;
    int r = aud_ep_an(d, e, 1); if (r) { ctrl(d, 0x01, 11, 0, e->iface, 0, 0); return -10 + r; }
    e->aktiv = 1;
    for (int i = 0; i < 100 && !a_aus_ende; i++) { uint32_t l = aud_fuell_paket(d, e); aud_push(d, e, l); a_aus_gepostet++; }
    fence(); w32(db, d->slot * 4, e->dci);
    uint32_t t0 = timer_get_ticks(), letzt = t0; int last = 0;
    while (a_aus_fertig < a_aus_gepostet) {
        usb_poll();
        if (a_aus_fertig != last) { last = a_aus_fertig; letzt = timer_get_ticks(); }
        if (timer_get_ticks() - letzt > 100) { kprint("[USB-Audio] Zeitueberschreitung: "); kprint_dec((uint32_t)a_aus_fertig); kprint(" von "); kprint_dec((uint32_t)a_aus_gepostet); kprint(" Paketen fertig, erster Fehlercode "); kprint_dec(a_erster_code); kprint("\n"); aud_ep_aus(d, e); return -5; }                   /* 1 s ohne Fortschritt */
    }
    aud_ep_aus(d, e);
    if (a_fehler) { kprint("[USB-Audio] "); kprint_dec((uint32_t)a_fehler); kprint(" von "); kprint_dec((uint32_t)a_aus_fertig); kprint(" Paketen mit Fehler/Unterlauf, erster Code "); kprint_dec(a_erster_code); kprint("\n"); }
    return 0;
}

int usbaud_aufnehmen(int16_t* mono, uint32_t max_frames, uint32_t* rate) {
    struct usbdev* d = auddev; if (!d || !d->used || !d->ain.ok) return -1;
    struct audep* e = &d->ain;
    a_rec = mono; a_rec_max = max_frames; a_rec_n = 0; a_ein_n = 0; a_fehler = 0;
    int r = aud_ep_an(d, e, 0); if (r) { ctrl(d, 0x01, 11, 0, e->iface, 0, 0); return -10 + r; }
    e->aktiv = 1;
    for (int i = 0; i < 100; i++) aud_push(d, e, e->mps);
    fence(); w32(db, d->slot * 4, e->dci);
    uint32_t t0 = timer_get_ticks(), letzt = t0; uint32_t last = 0;
    while (a_rec_n < a_rec_max) {
        usb_poll();
        if (a_rec_n != last) { last = a_rec_n; letzt = timer_get_ticks(); }
        if (timer_get_ticks() - letzt > 100) break;                                              /* 1 s ohne Daten */
    }
    aud_ep_aus(d, e);
    if (rate) *rate = e->rate;
    a_rec = 0;
    return (int)a_rec_n;
}
int usbaud_fehler(void) { return a_fehler; }

/* ---------- Controller ---------- */
int usb_init(void) {
    struct pci_dev* p = pci_find_class(0x0C, 0x03, 0x30, 0);
    if (!p) { kprint("[USB] Kein xHCI-Controller gefunden\n"); return -1; }
    uint64_t bar = pci_bar(p, 0);
    kprint("[USB] xHCI BAR0=0x"); kprint_hex((uint32_t)(bar >> 32)); kprint_hex((uint32_t)bar); kprint("\n");
    if (!bar) { kprint("[USB] BAR0 leer\n"); return -2; }
    pci_enable(p);
    if (paging_map_mmio(bar, 0x10000) != 0) { kprint("[USB] MMIO-Mapping fehlgeschlagen\n"); return -3; }
    cap = (volatile uint8_t*)(uintptr_t)bar;
    uint32_t caplen = cap[0];
    op = cap + caplen;
    rt = cap + (r32(cap, 0x18) & ~0x1Fu);
    db = cap + (r32(cap, 0x14) & ~3u);
    uint32_t hcs1 = r32(cap, 4), hcs2 = r32(cap, 8), hcc = r32(cap, 0x10);
    maxslots = hcs1 & 0xFF; maxports = hcs1 >> 24;
    ctxsz = (hcc & 4) ? 64 : 32;

    /* BIOS/UEFI-Firmware den Controller abgeben lassen (Legacy-Support-Capability) */
    uint32_t xecp = (hcc >> 16) & 0xFFFF;
    while (xecp) {
        uint32_t c = r32(cap, xecp * 4);
        if ((c & 0xFF) == 1) {
            w32(cap, xecp * 4, c | (1u << 24));
            uint32_t t0 = timer_get_ticks();
            while ((r32(cap, xecp * 4) & (1u << 16)) && timer_get_ticks() - t0 < 100) {}
        }
        uint32_t next = (c >> 8) & 0xFF;
        if (!next) break;
        xecp += next;
    }

    /* anhalten und zuruecksetzen */
    w32(op, 0, r32(op, 0) & ~1u);
    uint32_t t0 = timer_get_ticks();
    while (!(r32(op, 4) & 1) && timer_get_ticks() - t0 < 100) {}
    w32(op, 0, r32(op, 0) | 2u);
    t0 = timer_get_ticks();
    while (((r32(op, 0) & 2) || (r32(op, 4) & (1u << 11))) && timer_get_ticks() - t0 < 200) {}
    if (r32(op, 0) & 2) { kprint("[USB] Controller-Reset haengt\n"); return -4; }

    if (maxslots > 32) maxslots = 32;
    w32(op, 0x38, maxslots);

    dcbaa = (uint64_t*)seite();
    uint32_t nsp = ((hcs2 >> 21) & 0x1F) << 5 | ((hcs2 >> 27) & 0x1F);
    if (nsp) {
        uint64_t* arr = (uint64_t*)seite();
        for (uint32_t i = 0; i < nsp && i < 512; i++) arr[i] = (uint64_t)(uintptr_t)seite();
        dcbaa[0] = (uint64_t)(uintptr_t)arr;
    }
    w64(op, 0x30, (uint64_t)(uintptr_t)dcbaa);

    ring_init(&cmdr);
    w64(op, 0x18, (uint64_t)(uintptr_t)cmdr.t | 1);

    evt = (struct trb*)seite();
    uint64_t* erst = (uint64_t*)seite();
    erst[0] = (uint64_t)(uintptr_t)evt; erst[1] = EVT_N;
    evt_deq = 0; evt_cycle = 1;
    w32(rt, 0x28, 1);                                        /* ERSTSZ */
    w64(rt, 0x38, (uint64_t)(uintptr_t)evt);                 /* ERDP */
    w64(rt, 0x30, (uint64_t)(uintptr_t)erst);                /* ERSTBA */

    w32(op, 0, r32(op, 0) | 1u);                             /* Start */
    t0 = timer_get_ticks();
    while ((r32(op, 4) & 1) && timer_get_ticks() - t0 < 100) {}
    ready = 1;
    kprint("[USB] xHCI bereit: "); kprint_dec(maxports); kprint(" Ports, "); kprint_dec(maxslots); kprint(" Slots\n");

    warte_ms(100);                                           /* Geraete anlaufen lassen */
    for (uint32_t pt = 0; pt < maxports; pt++) {
        uint32_t sc = r32(op, 0x400 + 0x10 * pt);
        if (!(sc & 1)) continue;                             /* nichts angesteckt */
        if (pt < 64) tried[pt] = 1;
        port_aufnehmen(pt);
    }
    port_change = 1;                                         /* beim ersten Durchlauf Anschluss-Aenderungen quittieren */
    return 0;
}

int usb_has_keyboard(void) { return have_kbd; }
const struct usb_pointer* usb_pointer_state(void) { return &ptr; }
int usb_wheel_take(void) { int d = ptr.wheel; ptr.wheel = 0; return d; }
void usb_wheel_add(int d) { ptr.wheel += d; if (ptr.wheel > 64) ptr.wheel = 64; if (ptr.wheel < -64) ptr.wheel = -64; }

void usb_list(void) {
    if (!ready) { kprint("Kein USB-Controller\n"); return; }
    int n = 0;
    for (int i = 0; i < MAX_DEV; i++) {
        if (!devs[i].used) continue;
        n++;
        kprint("  Port "); kprint_dec(devs[i].port + 1);
        kprint(" Slot "); kprint_dec(devs[i].slot);
        kprint(devs[i].kind == 1 ? "  Tastatur" : devs[i].kind == 3 ? (devs[i].net == 1 ? "  Netzwerk (CDC-ECM)" : "  Netzwerk (RNDIS)") : "  Zeiger/Touch");
        kprint(" Speed "); kprint_dec(devs[i].speed);
        kprint("\n");
    }
    if (!n) kprint("  (keine Geraete)\n");
}
