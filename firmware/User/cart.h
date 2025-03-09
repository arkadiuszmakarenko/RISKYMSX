#ifndef __CART_H
#define __CART_H

#include "ch32v30x_rcc.h"
#include "utils.h"

typedef enum CartType {
    ROM16k,
    ROM32k,
    ROM48k,
    KonamiWithoutSCC,
    KonamiWithSCC,
    KonamiWithSCCNOSCC,
    ASCII8k,
    ASCII16k,
    NEO8,
    NEO16,
    MSXTERMINAL,
} CartType;

extern unsigned int __cart_section_start;
extern uint16_t __cfg_section_start;


void Init_Cart (uint8_t CartEmulation);

void RunKonamiWithSCC (void) __attribute__ ((interrupt ("WCH-Interrupt-fast")));
void RunKonamiWithSCCNOSCC (void) __attribute__ ((interrupt ("WCH-Interrupt-fast")));

void RunMSXTerminal (void) __attribute__ ((interrupt ("WCH-Interrupt-fast")));

typedef struct _CART_CFG {
    uint32_t CartType;
    uint32_t CartSize;
    char *filename;

} CART_CFG;


#endif
