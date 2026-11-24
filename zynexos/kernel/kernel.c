/*
 * Zynex OS - Kernel Entry
 */

#include "font_aa.h"
#include "fw.h"
#include "zyndb.h"
#include "gedaechtnis.h"
#include "krypto.h"
#include <stdint.h>
#include <stddef.h>
#include "multiboot.h"
#include "gdt.h"
#include "pmm.h"
#include "paging.h"
#include "idt.h"
#include "isr.h"
#include "timer.h"
#include "keyboard.h"
#include "task.h"
#include "kheap.h"
#include "ramfs.h"
#include "arena.h"
#include "capability.h"
#include "semantic.h"
#include "module.h"
#include "block.h"
#include "syscall.h"
#include "synfs.h"
#include "shell.h"
#include "desktop.h"
#include "benutzer.h"
#include "gpt.h"
#include "block.h"
#include "usb.h"
#include "hyperv.h"
#include "layouts.h"
#include "synbin.h"
#include "pci.h"
#include "nvme.h"
#include "virtioblk.h"
#include "virtiogpu.h"
#include "zlk.h"
#include "ahci.h"
#include "net.h"
#include "usb.h"
#include "pic.h"

#include "font8x16.h"

#define COM1 0x3F8

static inline void outb(uint16_t p, uint8_t v){__asm__ volatile("outb %0,%1"::"a"(v),"Nd"(p));}
static inline uint8_t inb(uint16_t p){uint8_t r;__asm__ volatile("inb %1,%0":"=a"(r):"Nd"(p));return r;}
static void serial_init(void){
    outb(COM1+1,0);outb(COM1+3,0x80);outb(COM1+0,3);outb(COM1+1,0);
    outb(COM1+3,3);outb(COM1+2,0xC7);outb(COM1+4,0x0B);
}
static void serial_putchar(char c){
    for(int i=0;i<100000 && !(inb(COM1+5)&0x20);i++){}
    outb(COM1,c);
}
static void serial_putchar_l1(char c){
    unsigned char u=(unsigned char)c;
    if(u>=0xA0){ serial_putchar((char)(0xC0|(u>>6))); serial_putchar((char)(0x80|(u&0x3F))); }
    else serial_putchar(c);
}

/* ---- Bildschirm: UEFI/GOP-Framebuffer (32 Bit) oder VGA-Text ---- */
static int mode = 0;                 /* 0 = nur seriell, 1 = Framebuffer, 2 = VGA-Text */
static uint8_t* fb;
static uint32_t fb_pitch, fb_w, fb_h;
static int cols, rows;
static int cursor_x=0, cursor_y=0;
#define FG 0x0000E060u
#define BG 0x00101018u

static int fb_hw = 0;                /* 1 = echter Bildspeicher des Grafikchips (nicht zwischengespeichert sichtbar): Cache-Zeilen muessen in den Speicher geschrieben werden */
static int fb_rgb = 0;               /* 1 = Pixelreihenfolge RGB statt BGR */
static inline uint32_t pc(uint32_t c){ return fb_rgb ? ((c&0xFFu)<<16)|(c&0xFF00u)|((c>>16)&0xFFu) : c; }

static void fb_glyph(int cx, int cy, char c){
    unsigned uc=(unsigned char)c;
    const uint8_t* g;
    if(uc>=0xA0) g=font8x16_l1[uc-0xA0];
    else { if(uc<32||uc>126) uc=' '; g=font8x16[uc-32]; }
    for(int y=0;y<16;y++){
        uint32_t* row=(uint32_t*)(fb+(uint32_t)(cy*16+y)*fb_pitch)+cx*8;
        uint8_t bits=g[y];
        for(int x=0;x<8;x++) row[x]=(bits&(0x80>>x))?pc(FG):pc(BG);
    }
}
static void fb_clear(void){
    for(uint32_t y=0;y<fb_h;y++){
        uint32_t* row=(uint32_t*)(fb+(uint64_t)y*fb_pitch);
        for(uint32_t x=0;x<fb_w;x++) row[x]=pc(BG);
    }
}
static void scroll(void){
    if(mode==1){
        uint32_t lines=(uint32_t)rows*16;
        for(uint32_t y=0;y+16<lines;y++){
            uint64_t* d=(uint64_t*)(fb+(uint64_t)y*fb_pitch);
            uint64_t* s=(uint64_t*)(fb+(uint64_t)(y+16)*fb_pitch);
            for(uint32_t x=0;x<fb_w/2;x++) d[x]=s[x];
        }
        for(uint32_t y=lines-16;y<lines;y++){
            uint32_t* row=(uint32_t*)(fb+(uint64_t)y*fb_pitch);
            for(uint32_t x=0;x<fb_w;x++) row[x]=pc(BG);
        }
    } else if(mode==2){
        volatile uint16_t* t=(volatile uint16_t*)fb;
        for(int i=0;i<cols*(rows-1);i++) t[i]=t[i+cols];
        for(int i=0;i<cols;i++) t[cols*(rows-1)+i]=(uint16_t)' '|(0x0A<<8);
    }
    cursor_y=rows-1;
}
/* Auf Notebooks liest die Grafikeinheit den Bildspeicher direkt aus dem RAM, waehrend die CPU ihre Schreibzugriffe noch im Cache haelt -> verschmierte Streifen.
 * Darum werden die Cache-Zeilen des Bildes regelmaessig (hoechstens alle ~40 ms) in den Speicher geschrieben. */
void gfx_flush(void){
    if(!fb_hw||mode!=1||!fb) return;
    static uint32_t last; uint32_t t=timer_get_ticks(); if(t&&t-last<4) return; last=t;
    const uint8_t* p=fb; const uint8_t* e=fb+(uint64_t)fb_pitch*fb_h;
    __asm__ volatile("mfence":::"memory");
    for(;p<e;p+=64) __asm__ volatile("clflush (%0)"::"r"(p):"memory");
    __asm__ volatile("mfence":::"memory");
}
extern void virtiogpu_tick(void);
static void newline(void){cursor_x=0;cursor_y++;if(cursor_y>=rows)scroll();virtiogpu_tick();gfx_flush();}
static void put_cell(int x,int y,char c){
    if(mode==1) fb_glyph(x,y,c);
    else if(mode==2) ((volatile uint16_t*)fb)[y*cols+x]=(uint16_t)(unsigned char)c|(0x0A<<8);
}
static void console_init(void){
    if(!g_boot.has_fb) return;
    fb=(uint8_t*)(uintptr_t)g_boot.fb_addr; fb_hw=1;
    fb_pitch=g_boot.fb_pitch; fb_w=g_boot.fb_width; fb_h=g_boot.fb_height; fb_rgb=(g_boot.flags&BOOT_RGB)!=0;
    if(g_boot.fb_type==2){ mode=2; cols=(int)fb_w; rows=(int)fb_h; }
    else if(g_boot.fb_type==1 && g_boot.fb_bpp==32){ mode=1; cols=(int)fb_w/8; rows=(int)fb_h/16; fb_clear(); }
}

/* ---- Zeiger: kleines Fadenkreuz, per XOR gezeichnet (zweites Zeichnen loescht es wieder) ---- */
static int cur_x=-1, cur_y=-1;
static void cursor_xor(int x,int y){
    if(mode!=1||x<0) return;
    for(int d=-6;d<=6;d++){
        int px=x+d, py=y+d;
        if(px>=0&&px<(int)fb_w) *(uint32_t*)(fb+(uint64_t)y*fb_pitch+(uint32_t)px*4)^=0x00FFFFFFu;
        if(d!=0&&py>=0&&py<(int)fb_h) *(uint32_t*)(fb+(uint64_t)py*fb_pitch+(uint32_t)x*4)^=0x00FFFFFFu;
    }
}
/* ---- Zeichenfunktionen fuer die grafische Oberflaeche (Installer) ---- */
static int con_aus = 0;                  /* 1 = Textkonsole nicht auf den Bildschirm zeichnen (nur seriell) */
int  gfx_available(void){ return mode==1; }
void gfx_size(int* w,int* h){ *w=(int)fb_w; *h=(int)fb_h; }
void gfx_console(int an){ con_aus=!an; if(an&&mode==1){ fb_clear(); cursor_x=0; cursor_y=0; } }
uint32_t* gfx_fbptr(uint32_t* pitch_px){ if(mode!=1) return 0; *pitch_px=fb_pitch/4; return (uint32_t*)fb; }
extern void virtiogpu_tick(void);
/* Anzeige umleiten (z. B. virtio-gpu): Bildinhalt in den neuen Speicher uebernehmen, ab jetzt wird dort gezeichnet */
void gfx_umleiten(void* neu,uint32_t pitch_bytes,uint32_t w,uint32_t h){
    fb_hw=0;
    if(mode==1){
        for(uint32_t y=0;y<fb_h&&y<h;y++){ uint32_t* s=(uint32_t*)(fb+(uint64_t)y*fb_pitch); uint32_t* d=(uint32_t*)((uint8_t*)neu+(uint64_t)y*pitch_bytes); for(uint32_t x=0;x<fb_w&&x<w;x++) d[x]=s[x]; }
        fb=(uint8_t*)neu; fb_pitch=pitch_bytes; fb_w=w; fb_h=h; cols=(int)w/8; rows=(int)h/16;
    } else if(mode==0){                                   /* kein Bildschirm vom Lader (z. B. nur virtio-gpu): neu aufsetzen */
        fb=(uint8_t*)neu; fb_pitch=pitch_bytes; fb_w=w; fb_h=h; fb_rgb=0; cols=(int)w/8; rows=(int)h/16; cursor_x=0; cursor_y=0; mode=1; fb_clear();
    }
}
void gfx_begin(void){ if(mode==1&&cur_x>=0) cursor_xor(cur_x,cur_y); }
void gfx_end(void){ if(mode==1&&cur_x>=0) cursor_xor(cur_x,cur_y); virtiogpu_tick(); gfx_flush(); }
void gfx_rect(int x,int y,int w,int h,uint32_t c){
    if(mode!=1) return;
    c=pc(c);
    if(x<0){w+=x;x=0;} if(y<0){h+=y;y=0;}
    if(x+w>(int)fb_w) w=(int)fb_w-x;
    if(y+h>(int)fb_h) h=(int)fb_h-y;
    for(int j=0;j<h;j++){ uint32_t* row=(uint32_t*)(fb+(uint64_t)(y+j)*fb_pitch)+x; for(int i=0;i<w;i++) row[i]=c; }
}
void gfx_bitmap(int x,int y,int w,int h,const uint32_t* px){
    if(mode!=1) return;
    for(int j=0;j<h;j++){ int yy=y+j; if(yy<0||yy>=(int)fb_h) continue; uint32_t* row=(uint32_t*)(fb+(uint64_t)yy*fb_pitch); for(int i=0;i<w;i++){ int xx=x+i; if(xx>=0&&xx<(int)fb_w) row[xx]=pc(px[j*w+i]); } }
}
static const uint8_t* glyph_of(unsigned char u){
    if(u>=0xA0) return font8x16_l1[u-0xA0];
    if(u<32||u>126) u=' ';
    return font8x16[u-32];
}
int gfx_text_w(const char* s,int scale){ int n=0; while(*s++) n++; return n*8*scale; }
/* geglaettete Schrift: Deckung je Pixel, mit dem vorhandenen Bild gemischt */
static void aa_glyph_zeichnen(int x,int y,const struct aa_glyph* g,uint32_t c){
    const uint8_t* a=aa_daten+g->off; int gx=x+g->x0, gy=y+g->y0;
    for(int j=0;j<g->h;j++){
        int yy=gy+j; if(yy<0||yy>=(int)fb_h) continue;
        uint32_t* row=(uint32_t*)(fb+(uint64_t)yy*fb_pitch);
        for(int i=0;i<g->w;i++){
            int xx=gx+i, al=a[j*g->w+i]; if(!al||xx<0||xx>=(int)fb_w) continue;
            if(al>=250){ row[xx]=c; continue; }
            uint32_t d=row[xx], r=0;
            for(int sh=0;sh<=16;sh+=8){ int dv=(int)((d>>sh)&255), cv=(int)((c>>sh)&255); r|=(uint32_t)(dv+(cv-dv)*al/255)<<sh; }
            row[xx]=r;
        }
    }
}
static int aa_schrift(int basis,int scale){ if(scale<1) scale=1; if(scale>4) scale=4; return basis+scale-1; }
void gfx_text(int x,int y,const char* s,uint32_t fg,int scale){
    if(mode!=1) return;
    if(scale>=1&&scale<=4){ const struct aa_glyph* f=aa_glyphen+aa_schrift(0,scale)*AA_ZEICHEN; uint32_t c=pc(fg); for(;*s;s++,x+=8*scale) aa_glyph_zeichnen(x,y,f+aa_index((unsigned char)*s),c); return; }
    for(;*s;s++,x+=8*scale){
        const uint8_t* g=glyph_of((unsigned char)*s);
        for(int gy=0;gy<16;gy++) for(int gx=0;gx<8;gx++)
            if(g[gy]&(0x80>>gx)) gfx_rect(x+gx*scale,y+gy*scale,scale,scale,fg);
    }
}

/* proportionale Schrift (Inter) fuer die Oberflaeche */
int gfx_text_pw(const char* s,int scale){ const struct aa_glyph* f=aa_glyphen+aa_schrift(4,scale)*AA_ZEICHEN; int w=0; for(;*s;s++) w+=f[aa_index((unsigned char)*s)].adv; return w; }
void gfx_text_p(int x,int y,const char* s,uint32_t fg,int scale){
    if(mode!=1) return;
    const struct aa_glyph* f=aa_glyphen+aa_schrift(4,scale)*AA_ZEICHEN; uint32_t c=pc(fg);
    for(;*s;s++){ const struct aa_glyph* g=f+aa_index((unsigned char)*s); aa_glyph_zeichnen(x,y,g,c); x+=g->adv; }
}
/* beliebige Schrift 0-12 (siehe font_aa.c), z. B. fuer Webseiten */
int gfx_text_fw(const char* s,int font){ if(font<0||font>12) font=4; const struct aa_glyph* f=aa_glyphen+font*AA_ZEICHEN; int w=0; for(;*s;s++) w+=f[aa_index((unsigned char)*s)].adv; return w; }
void gfx_text_f(int x,int y,const char* s,uint32_t fg,int font){
    if(mode!=1) return; if(font<0||font>12) font=4;
    const struct aa_glyph* f=aa_glyphen+font*AA_ZEICHEN; uint32_t c=pc(fg);
    for(;*s;s++){ const struct aa_glyph* g=f+aa_index((unsigned char)*s); aa_glyph_zeichnen(x,y,g,c); x+=g->adv; }
}
/* Zeichen mit Stil (Textverarbeitung): stil Bit0 = fett (doppelt gezeichnet), Bit1 = kursiv (Scherung). y = Oberkante der Schriftbox. */
int gfx_font_box(int font){ if(font>=0&&font<=3) return 16*(font+1); if(font>=4&&font<=7) return 16*(font-3); return 16; }
static void aa_glyph_schief(int x,int y,const struct aa_glyph* g,uint32_t c,int grund){
    const uint8_t* a=aa_daten+g->off; int gx=x+g->x0, gy=y+g->y0;
    for(int j=0;j<g->h;j++){
        int yy=gy+j; if(yy<0||yy>=(int)fb_h) continue;
        int sh=(y+grund-yy)/4;                                       /* je 4 Pixel ueber der Grundlinie 1 Pixel nach rechts */
        uint32_t* row=(uint32_t*)(fb+(uint64_t)yy*fb_pitch);
        for(int i=0;i<g->w;i++){
            int xx=gx+i+sh, al=a[j*g->w+i]; if(!al||xx<0||xx>=(int)fb_w) continue;
            uint32_t d=row[xx], r=0;
            for(int s2=0;s2<=16;s2+=8){ int dv=(int)((d>>s2)&255), cv=(int)((c>>s2)&255); r|=(uint32_t)(dv+(cv-dv)*al/255)<<s2; }
            row[xx]=r;
        }
    }
}
int gfx_zeichen_adv(unsigned char ch,int font,int stil){
    if(font<0||font>12) font=4;
    int adv=(aa_glyphen+font*AA_ZEICHEN+aa_index(ch))->adv;
    if(stil&1) adv+=gfx_font_box(font)>=32?2:1;
    return adv;
}
int gfx_zeichen_stil(int x,int y,unsigned char ch,uint32_t fg,int font,int stil){
    if(font<0||font>12) font=4;
    const struct aa_glyph* g=aa_glyphen+font*AA_ZEICHEN+aa_index(ch);
    int adv=g->adv;
    if(mode==1){
        uint32_t c=pc(fg); int box=gfx_font_box(font), grund=(box*77+50)/100, dicke=(stil&1)?(box>=32?2:1):0;
        for(int d=0;d<=dicke;d++){
            if(stil&2) aa_glyph_schief(x+d,y,g,c,grund); else aa_glyph_zeichnen(x+d,y,g,c);
        }
    }
    return adv+((stil&1)?(gfx_font_box(font)>=32?2:1):0);
}
/* Bild (0x00RRGGBB) mit Ausschnitt: nur Zeilen y0..y1 des Bildschirms */
void gfx_bild(int x,int y,int w,int h,const uint32_t* px,int stride,int y0,int y1){
    if(mode!=1) return;
    for(int j=0;j<h;j++){ int yy=y+j; if(yy<y0||yy>=y1||yy<0||yy>=(int)fb_h) continue; uint32_t* row=(uint32_t*)(fb+(uint64_t)yy*fb_pitch); const uint32_t* q=px+(uint64_t)j*stride; for(int i=0;i<w;i++){ int xx=x+i; if(xx>=0&&xx<(int)fb_w) row[xx]=pc(q[i]); } }
}
/* einen Pixel mit Deckung a (0-255) mischen (Kantenglaettung) */
void gfx_blend(int x,int y,uint32_t fg,int a){
    if(mode!=1||x<0||y<0||x>=(int)fb_w||y>=(int)fb_h||a<=0) return;
    uint32_t* p=(uint32_t*)(fb+(uint64_t)y*fb_pitch)+x, c=pc(fg);
    if(a>=255){ *p=c; return; }
    uint32_t d=*p, r=0;
    for(int sh=0;sh<=16;sh+=8){ int dv=(int)((d>>sh)&255), cv=(int)((c>>sh)&255); r|=(uint32_t)(dv+(cv-dv)*a/255)<<sh; }
    *p=r;
}
void fb_cursor_move(int x,int y){ cursor_xor(cur_x,cur_y); cur_x=x; cur_y=y; cursor_xor(cur_x,cur_y); }
void fb_get_size(int* w,int* h){ *w=mode==1?(int)fb_w:1024; *h=mode==1?(int)fb_h:768; }

static void kputchar_raw(char c){
    if(c=='\n')serial_putchar('\r');
    serial_putchar_l1(c);
    if(!mode||con_aus) return;
    if(c=='\n'){newline();return;}
    if(c=='\r'){cursor_x=0;return;}
    if(c=='\b'){if(cursor_x>0){cursor_x--;put_cell(cursor_x,cursor_y,' ');}return;}
    put_cell(cursor_x,cursor_y,c);
    cursor_x++;if(cursor_x>=cols)newline();
}
void kputchar(char c){
    if(mode==1&&cur_x>=0) cursor_xor(cur_x,cur_y);
    kputchar_raw(c);
    if(mode==1&&cur_x>=0) cursor_xor(cur_x,cur_y);
}
void kprint(const char* s){while(*s)kputchar(*s++);}
void kprint_hex(uint32_t v){const char* h="0123456789ABCDEF";for(int i=7;i>=0;i--)kputchar(h[(v>>(i*4))&0xF]);}
void kprint_dec(uint32_t v){char b[12];int i=0;if(!v){kputchar('0');return;}while(v){b[i++]='0'+(v%10);v/=10;}while(i--)kputchar(b[i]);}

static void keyboard_isr_wrapper(struct registers* r){(void)r;keyboard_irq_handler();}
static void mouse_isr_wrapper(struct registers* r){(void)r;mouse_irq_handler();}
static void demo_mod_init(void){ kprint("  [mod] demo init\n"); }
static void demo_mod_shutdown(void){ kprint("  [mod] demo shutdown\n"); }

void kernel_main(uint32_t magic, uint32_t mbi_addr){
    serial_init();
    if(magic==MULTIBOOT2_MAGIC) bootinfo_parse(mbi_addr);
    console_init();
    int systemmodus = 0;
    for (const char* c = g_boot.cmdline; *c; c++)
        if (c[0] == 'm' && c[1] == 'o' && c[2] == 'd' && c[3] == 'u' && c[4] == 's' && c[5] == '=' && c[6] == 's') systemmodus = 1;
    if (systemmodus) desktop_splash_start();

    kprint("========================================\n");
    kprint("            Z Y N E X   O S\n");
    kprint("     From-Scratch AI Operating System\n");
    kprint("========================================\n\n");

    if(magic!=MULTIBOOT2_MAGIC){kprint("[!] Not Multiboot2\n");for(;;) __asm__ volatile("hlt");}
    kprint("[+] Start ueber den Zynex-UEFI-Lader (64-bit)\n");
    kprint((g_boot.flags&BOOT_SECUREBOOT) ? "[SECURE] UEFI Secure Boot aktiv: Firmware hat den Zynex-Lader gegen ihre Schluessel geprueft\n" : "[SECURE] UEFI Secure Boot aus (Firmware prueft den Lader nicht)\n");
    kprint((g_boot.flags&BOOT_SIGNIERT) ? "[SECURE] Kernel durch hash-basierte Signatur (LMS/SHA-256, quantensicher) beglaubigt\n" : "[SECURE] WARNUNG: Kernel wurde nicht durch eine Signatur beglaubigt\n");
    if(mode==1){kprint("[+] Framebuffer ");kprint_dec(fb_w);kprint("x");kprint_dec(fb_h);kprint("\n");}
    else if(mode==2) kprint("[+] VGA text mode\n");
    else kprint("[+] Kein Bildschirm, nur seriell\n");

    gdt_init();
    pmm_init();
    paging_init();
    idt_init(); isr_init();
    timer_init(100);
    isr_register_handler(33, keyboard_isr_wrapper); isr_register_handler(44, mouse_isr_wrapper);
    desktop_splash(0, 25, "CPU, Speicher, Interrupts");
    keyboard_init();
    task_init();
    kheap_init();
    block_init();
    pci_scan();
    nvme_init();
    desktop_splash(-1, 30, "NVMe geprueft");
    ahci_init();
    virtioblk_init();
    virtiogpu_init();
    desktop_splash(-1, 34, "SATA geprueft");
    usb_init();
    desktop_splash(-1, 38, "USB geprueft");
    hv_init();
    desktop_splash(-1, 44, "Hyper-V geprueft");
    desktop_splash(-1, 46, "Netzwerk wird gestartet");
    net_init();
    if(usb_has_keyboard()){ pic_set_mask(1); kprint("[USB] USB-Tastatur aktiv, PS/2-Tastatur abgeschaltet\n"); }
    desktop_splash(1, 50, "Geraete: Speicher, USB, Netzwerk");
    syscall_init();
    ramfs_init();
    ai_arena_init();
    cap_init();
    sem_init();
    mod_init_system();
    mod_register("demo", demo_mod_init, demo_mod_shutdown);

    /* Partitionen aller Platten anmelden; im Systemmodus (installiertes System) die Zynex-Datenpartition einbinden */
    for (int i = 0, n = block_count(); i < n; i++) {
        const char* bn = block_name(i);
        if (bn && !block_is_part(bn) && !(bn[0] == 'r' && bn[1] == 'a')) gpt_scan(bn);
    }
    int gemountet = 0;
    if (systemmodus) {
        const char* zp = gpt_zynex_partition();
        if (zp && block_select(zp) == 0 && synfs_mount() == 0) {
            gemountet = 1;
            kprint("[SYSTEM] Installiertes System von "); kprint(zp); kprint(" gestartet\n");
            benutzer_laden(); zdb_init(); ged_neu_laden(); fw_laden(); zlk_laden();
            { const char* tl = zdb_get("system.tastatur"); if (tl) { int w = kbd_find(tl); if (w >= 0) usb_set_layout(w); } }
            static char cfg[64]; int r = synfs_read("sys.config", cfg, sizeof(cfg) - 1);
            if (r > 0) { cfg[r] = 0; for (int i = 0; cfg[i]; i++) if (cfg[i] == 'l' && cfg[i+1] == 'a' && cfg[i+6] == '=') { int w = kbd_find(cfg + i + 7); if (w >= 0) usb_set_layout(w); break; } }
        } else if (krypto_gesperrt_dev()[0]) kprint("[SYSTEM] Datenpartition ist VERSCHLUESSELT und gesperrt - Live-Betrieb. Entsperren: krypto entsperren\n");
        else kprint("[SYSTEM] Datenpartition nicht gefunden - Live-Betrieb\n");
    }
    if (!gemountet) {
        block_select("ramdisk");
        if (synfs_format("zynex-synfs") == 0) {
            synfs_mount();
            synbin_install_embedded();
            benutzer_laden();
        }
    }

    desktop_splash(2, 80, "Dateisystem SynFS");
    kprint("\n[+] All systems online\n");
    desktop_splash(3, 100, "Zynex OS Quantum bereit");
    desktop_splash_ende();
    shell_init();
    shell_run();
}
