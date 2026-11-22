#ifndef APP_FLASH_H
#define APP_FLASH_H

#include <stdint.h>

/*
 * Application flash layout on the CH32V303VCT6.
 *
 * The bootloader lives in the 28 KiB system boot flash at 0x1FFF8000 and is
 * reached with BOOT0 high. The application firmware occupies the start of the
 * main flash, exactly where the cartridge firmware expects it, so the normal
 * firmware can start at its usual address with BOOT0 low.
 */
#define APP_FLASH_START 0x08000000u
/* The cartridge firmware image region: 256 KiB of main flash. */
#define APP_FLASH_END 0x08040000u
#define APP_FLASH_MAX_SIZE (APP_FLASH_END - APP_FLASH_START)

/* Results of a programming attempt. */
typedef enum {
    APP_FLASH_OK = 0,
    APP_FLASH_ERR_FILE,      /* image file missing or unreadable */
    APP_FLASH_ERR_TOO_BIG,   /* image does not fit the application area */
    APP_FLASH_ERR_ERASE,     /* erase failed */
    APP_FLASH_ERR_WRITE,     /* programming failed */
    APP_FLASH_ERR_VERIFY     /* read-back mismatch */
} AppFlashResult;

/* Progress callback, invoked with the percentage completed (0-100). */
typedef void (*AppFlashProgress)(uint8_t percent);

/* Reads one byte from the main flash. Used by the read-back check. */
uint8_t AppFlash_ReadByte(uint32_t address);

AppFlashResult AppFlash_Program(const char *filename, AppFlashProgress progress,
                                 const char **detail);
const char *AppFlash_ResultText(AppFlashResult result);

#endif /* APP_FLASH_H */
