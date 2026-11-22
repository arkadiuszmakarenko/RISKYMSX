/* Host test shim for the debug helpers used by the flash routines. */
#ifndef DEBUG_HOST_SHIM_H
#define DEBUG_HOST_SHIM_H

#include <stdint.h>

void Delay_Us(uint32_t n);
void Delay_Ms(uint32_t n);

#endif /* DEBUG_HOST_SHIM_H */