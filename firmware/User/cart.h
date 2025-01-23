#ifndef __CART_H
#define __CART_H

#include "ch32v30x_rcc.h"
#include "utils.h"

typedef enum CartType {
    ROM32k,
    ROM48k,
    KonamiWithoutSCC,
    KonamiWithSCC,
    KonamiWithSCCNOSCC,
    ASCII8k,
    ASCII16k,
} CartType;

extern unsigned int __cart_section_start;
extern uint16_t __cfg_section_start;
extern CircularBuffer cb;

void Init_Cart (void);
void RunCart32k (void) __attribute__ ((interrupt ("WCH-Interrupt-fast")));
void RunCart48k (void) __attribute__ ((interrupt ("WCH-Interrupt-fast")));
void RunKonamiWithoutSCC (void) __attribute__ ((interrupt ("WCH-Interrupt-fast")));
void RunKonamiWithSCC (void) __attribute__ ((interrupt ("WCH-Interrupt-fast")));
void RunKonamiWithSCCNOSCC (void) __attribute__ ((interrupt ("WCH-Interrupt-fast")));
void Run8kASCII (void) __attribute__ ((interrupt ("WCH-Interrupt-fast")));
void Run16kASCII (void) __attribute__ ((interrupt ("WCH-Interrupt-fast")));

void RunNEO16 (void) __attribute__ ((interrupt ("WCH-Interrupt-fast")));

typedef struct _CART_CFG {
    uint32_t CartType;
    uint32_t CartSize;

} CART_CFG;


#endif
