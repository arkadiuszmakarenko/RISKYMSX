#ifndef __SCC_H
#define __SCC_H

#include "ch32v30x_gpio.h"
#include "ch32v30x_rcc.h"

void SCC_Init (void);
void SCC_HandleBufer (void);
void TIM6_IRQHandler();

#endif
