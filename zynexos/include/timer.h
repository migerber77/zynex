#pragma once
#include <stdint.h>

void timer_init(uint32_t frequency);
uint32_t timer_get_ticks(void);
uint32_t timer_get_seconds(void);
void timer_sleep(uint32_t ms);
/* Hardware-Uhr: ymd = JJJJMMTT, hm = HHMM; 0 wenn die Uhr unplausibel ist (Jahr < 2025) */
int rtc_get(uint32_t* ymd, uint32_t* hm);
