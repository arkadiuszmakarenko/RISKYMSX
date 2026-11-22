/* Host test shim for the WCH flash controller API. */
#ifndef FLASH_HOST_SHIM_H
#define FLASH_HOST_SHIM_H

#include <stdint.h>

typedef enum {
    DISABLE = 0,
    ENABLE = 1
} FunctionalState;

typedef enum {
    FLASH_COMPLETE = 0,
    FLASH_BUSY = 1,
    FLASH_ERROR_PG = 2,
    FLASH_ERROR_WRP = 3,
    FLASH_TIMEOUT = 4,
    FLASH_ADR_RANGE_ERROR = 5,
    FLASH_ALIGN_ERROR = 6,
    FLASH_OP_RANGE_ERROR = 7
} FLASH_Status;

void FLASH_Unlock(void);
void FLASH_Lock(void);
void FLASH_Unlock_Fast(void);
void FLASH_Lock_Fast(void);
void FLASH_Enhance_Mode(FunctionalState new_state);
FLASH_Status FLASH_ErasePage(uint32_t page_address);
FLASH_Status FLASH_ProgramHalfWord(uint32_t address, uint16_t data);
void FLASH_ErasePage_Fast(uint32_t page_address);
FLASH_Status FLASH_ROM_WRITE(uint32_t start_address, uint32_t *data, uint32_t length);

#endif /* FLASH_HOST_SHIM_H */