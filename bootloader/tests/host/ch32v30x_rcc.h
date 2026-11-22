/* Host test shim for the RCC clock registers used by the flash routines. */
#ifndef RCC_HOST_SHIM_H
#define RCC_HOST_SHIM_H

#include <stdint.h>

#define RCC_HPRE 0x000000F0u
#define RCC_HPRE_DIV2 0x00000080u

typedef struct {
    volatile uint32_t CFGR0;
} RCC_TypeDef;

extern RCC_TypeDef fake_rcc;

#define RCC (&fake_rcc)

#endif /* RCC_HOST_SHIM_H */