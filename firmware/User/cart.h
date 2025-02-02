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
    NEO16,
    NEO8,
    MSXTERMINAL,
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
void RunNEO8 (void) __attribute__ ((interrupt ("WCH-Interrupt-fast")));
void RunMSXTerminal (void) __attribute__ ((interrupt ("WCH-Interrupt-fast")));

typedef struct _CART_CFG {
    uint32_t CartType;
    uint32_t CartSize;

} CART_CFG;

static unsigned char msxterm[117] = {
    'A',
    'B',
    04,
    '@',
    '>',
    ' ',
    '2',
    0257,
    0363,
    '!',
    017,
    0,
    '"',
    0352,
    0363,
    '"',
    0351,
    0363,
    '>',
    01,
    0315,
    '_',
    0,
    '!',
    'm',
    '@',
    021,
    0,
    '8',
    01,
    010,
    0,
    0315,
    0134,
    0,
    '>',
    010,
    '!',
    03,
    033,
    0315,
    'M',
    0,
    '!',
    '8',
    '@',
    021,
    0,
    0340,
    01,
    '5',
    0,
    0325,
    0355,
    0260,
    0311,
    ':',
    0377,
    0177,
    0376,
    03,
    0312,
    0,
    0,
    0376,
    04,
    '(',
    026,
    0247,
    0314,
    ' ',
    0340,
    0304,
    0242,
    0,
    0315,
    0234,
    0,
    '(',
    0350,
    0315,
    0237,
    0,
    '2',
    0375,
    0177,
    030,
    0340,
    'v',
    0311,
    ':',
    0377,
    0177,
    '!',
    01,
    033,
    0315,
    'M',
    0,
    ':',
    0377,
    0177,
    '=',
    '-',
    0315,
    'M',
    0,
    030,
    0313,
    ' ',
    '0',
    '8',
    '<',
    '8',
    '0',
    ' ',
    0,
};

#endif
