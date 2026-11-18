/*
 * Zynex OS - PIT + preemptive tick hook
 */

#include "timer.h"
#include "isr.h"
#include "pic.h"
#include "capability.h"
#include "task.h"

extern void kprint(const char* str);
extern void kprint_dec(uint32_t val);

#define PIT_CHANNEL0 0x40
#define PIT_COMMAND  0x43
#define PIT_BASE_FREQ 1193182

static volatile uint32_t timer_ticks = 0;
static uint32_t timer_frequency = 100;
static uint32_t slice_counter = 0;
#define TIMESLICE 10   /* yield every 10 ticks (~100ms at 100Hz) */

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static void timer_callback(struct registers* regs) {
    (void)regs;
    timer_ticks++;
    slice_counter++;

    /* Expire capabilities */
    cap_tick(timer_ticks);

    /* Simple preemption hint: cooperative yield on timeslice */
    if (slice_counter >= TIMESLICE) {
        slice_counter = 0;
        /* Only yield if there are other ready tasks – task_yield is safe */
        task_schedule();
    }
}

void timer_init(uint32_t frequency) {
    timer_frequency = frequency ? frequency : 100;
    uint32_t divisor = PIT_BASE_FREQ / timer_frequency;
    if (divisor == 0) divisor = 1;
    if (divisor > 65535) divisor = 65535;

    outb(PIT_COMMAND, 0x36);
    outb(PIT_CHANNEL0, (uint8_t)(divisor & 0xFF));
    outb(PIT_CHANNEL0, (uint8_t)((divisor >> 8) & 0xFF));

    isr_register_handler(32, timer_callback);
    pic_clear_mask(0);

    kprint("[TIMER] PIT ");
    kprint_dec(timer_frequency);
    kprint(" Hz + preemptive timeslice\n");
}

uint32_t timer_get_ticks(void) { return timer_ticks; }
uint32_t timer_get_seconds(void) { return timer_ticks / timer_frequency; }

void timer_sleep(uint32_t ms) {
    uint32_t target = timer_ticks + (ms * timer_frequency) / 1000;
    while (timer_ticks < target) __asm__ volatile ("hlt");
}

/* ---- Hardware-Uhr (CMOS) ---- */
static uint8_t cmos_rd(uint8_t r) {
    uint8_t v;
    __asm__ volatile ("outb %0, %1" : : "a"(r), "Nd"((uint16_t)0x70));
    __asm__ volatile ("inb %1, %0" : "=a"(v) : "Nd"((uint16_t)0x71));
    return v;
}
int rtc_get(uint32_t* ymd, uint32_t* hm) {
    *ymd = 0; *hm = 0;
    for (int i = 0; i < 100000 && (cmos_rd(0x0A) & 0x80); i++) {}
    uint8_t s = cmos_rd(0), mi = cmos_rd(2), h = cmos_rd(4), d = cmos_rd(7), mo = cmos_rd(8), y = cmos_rd(9), b = cmos_rd(0x0B);
    (void)s;
    if (!(b & 4)) {
        mi = (uint8_t)((mi & 15) + (mi >> 4) * 10); h = (uint8_t)((h & 15) + ((h & 0x70) >> 4) * 10 + (h & 0x80));
        d = (uint8_t)((d & 15) + (d >> 4) * 10); mo = (uint8_t)((mo & 15) + (mo >> 4) * 10); y = (uint8_t)((y & 15) + (y >> 4) * 10);
    }
    uint32_t jahr = 2000u + y;
    if (jahr < 2025 || mo < 1 || mo > 12 || d < 1 || d > 31 || h > 23 || mi > 59) return -1;
    *ymd = jahr * 10000 + (uint32_t)mo * 100 + d; *hm = (uint32_t)h * 100 + mi;
    return 0;
}
