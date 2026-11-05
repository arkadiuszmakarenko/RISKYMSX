#ifndef __GPIO_H
#define __GPIO_H

#include "ch32v30x_gpio.h"
#include "ch32v30x_rcc.h"

void GPIO_Config();

/* CH217K USB power switch control.
 * EN# (active-low) is wired to PB12; FLAG# (active-low, open-drain) to PB13.
 * PB12 low  -> EN# low  -> switch ON  (VBUS enabled)
 * PB12 high -> EN# high -> switch OFF (VBUS disabled)
 */
void VBUS_Enable(void);
void VBUS_Disable(void);
uint8_t VBUS_Fault(void);   /* 1 while CH217K asserts FLAG# (over-current/over-temp) */

#endif
