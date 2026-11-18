#pragma once
#include <stdint.h>

void keyboard_init(void);          /* i8042 einrichten: Tastatur und PS/2-Maus (TrackPoint/Touchpad) */
/* Called from IRQ handler */
void keyboard_irq_handler(void);
void mouse_irq_handler(void);      /* IRQ 12 */
int  ps2_mouse_present(void);
