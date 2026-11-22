#include "appflash.h"
#include "ff.h"
#include "ch32v30x_flash.h"
#include "ch32v30x_rcc.h"
#include "debug.h"
#include <string.h>

/*
 * Programming of the application firmware into the main flash.
 *
 * Whether the main flash can be erased and programmed while executing from
 * the system boot flash is not documented by WCH, so both programming paths
 * are selectable at build time:
 *
 *   APPFLASH_USE_FAST_PROGRAMMING = 0  standard half word programming and
 *                                    4 KiB page erase (the documented default
 *                                    method, used by default).
 *   APPFLASH_USE_FAST_PROGRAMMING = 1  fast 256 byte page programming and
 *                                    page erase.
 *
 * The reference manual notes that flash erase related functions must be
 * placed in the zero-wait area flash, which the system boot flash is not
 * documented to be, so the standard path is the safer starting point. If the
 * erase or program step does not complete, the read-back check reports it and
 * the bootloader must not claim success.
 */

#ifndef APPFLASH_USE_FAST_PROGRAMMING
#define APPFLASH_USE_FAST_PROGRAMMING 0
#endif

#define PROGRAM_CHUNK 256
#define ERASE_PAGE_SIZE 0x1000

/* Flash status register bits (FLASH_STATR). */
#define APPFLASH_STATR_BSY 0x00000001u
#define APPFLASH_STATR_WRPRTERR 0x00000010u

static uint8_t chunk_buffer[PROGRAM_CHUNK];
static AppFlashProgress progress_hook;

/* Reads one byte from the main flash. Overridable so the read-back check
   can be exercised on a host. */
__attribute__((weak)) uint8_t AppFlash_ReadByte(uint32_t address)
{
    return *(volatile uint8_t *)address;
}

/* Flash operations must not run with an excessive access clock. */
static uint32_t slow_clock_saved;

static void flash_clock_slow(void)
{
    slow_clock_saved = RCC->CFGR0;
    RCC->CFGR0 = (RCC->CFGR0 & ~RCC_HPRE) | RCC_HPRE_DIV2;
    Delay_Us(20);
}

static void flash_clock_restore(void)
{
    RCC->CFGR0 = slow_clock_saved;
    Delay_Us(20);
}

/*
 * Leaves the flash enhanced read mode.
 *
 * The reference manual states that erase and program operations fail while
 * the enhanced read mode is active. The boot region is executed from flash,
 * so the mode has to be off for the programming steps below to work.
 */
static void flash_enhanced_mode_off(void)
{
    FLASH_Enhance_Mode(DISABLE);
    Delay_Us(20);
}

static uint8_t flash_read_byte(uint32_t address)
{
    return AppFlash_ReadByte(address);
}

const char *AppFlash_ResultText(AppFlashResult result)
{
    switch (result) {
    case APP_FLASH_OK:
        return "ok";
    case APP_FLASH_ERR_FILE:
        return "image file not found";
    case APP_FLASH_ERR_TOO_BIG:
        return "image does not fit application flash";
    case APP_FLASH_ERR_ERASE:
        return "erase failed";
    case APP_FLASH_ERR_WRITE:
        return "programming failed";
    case APP_FLASH_ERR_VERIFY:
        return "verification failed";
    default:
        return "unknown error";
    }
}

/* Erases the pages that cover the image. */
static AppFlashResult erase_region(uint32_t length)
{
    uint32_t pages = (length + ERASE_PAGE_SIZE - 1) / ERASE_PAGE_SIZE;

    FLASH_Unlock();
    flash_enhanced_mode_off();
    flash_clock_slow();

    for (uint32_t page = 0; page < pages; page++) {
#if APPFLASH_USE_FAST_PROGRAMMING
        /* The fast erase helper reports no status, so the error flags are
           checked on the flash controller instead. */
        FLASH_Unlock_Fast();
        FLASH_ErasePage_Fast(APP_FLASH_START + page * ERASE_PAGE_SIZE);
        if (FLASH->STATR & (APPFLASH_STATR_WRPRTERR | APPFLASH_STATR_BSY)) {
            flash_clock_restore();
            FLASH_Lock();
            return APP_FLASH_ERR_ERASE;
        }
#else
        FLASH_Status status = FLASH_ErasePage(APP_FLASH_START + page * ERASE_PAGE_SIZE);
        if (status != FLASH_COMPLETE) {
            flash_clock_restore();
            FLASH_Lock();
            return APP_FLASH_ERR_ERASE;
        }
#endif
    }

    flash_clock_restore();
    FLASH_Lock();
    return APP_FLASH_OK;
}

/* Programs one chunk of the image at the given address. */
static AppFlashResult program_chunk(uint32_t address, const uint8_t *data, uint32_t length)
{
    FLASH_Unlock();
    flash_enhanced_mode_off();
    flash_clock_slow();

#if APPFLASH_USE_FAST_PROGRAMMING
    FLASH_Unlock_Fast();
    FLASH_Status status = FLASH_ROM_WRITE(address, (uint32_t *)data, length);
    if (status != FLASH_COMPLETE) {
        flash_clock_restore();
        FLASH_Lock();
        return APP_FLASH_ERR_WRITE;
    }
#else
    for (uint32_t i = 0; i < length; i += 2) {
        uint16_t half = data[i];
        if (i + 1 < length) {
            half |= (uint16_t)data[i + 1] << 8;
        }
        FLASH_Status status = FLASH_ProgramHalfWord(address + i, half);
        if (status != FLASH_COMPLETE) {
            flash_clock_restore();
            FLASH_Lock();
            return APP_FLASH_ERR_WRITE;
        }
    }
#endif

    flash_clock_restore();
    FLASH_Lock();
    return APP_FLASH_OK;
}

AppFlashResult AppFlash_Program(const char *filename, AppFlashProgress progress,
                                 const char **detail)
{
    FIL file;
    FRESULT fres;
    UINT bytes_read;
    uint32_t file_size;
    uint32_t written = 0;
    uint32_t address = APP_FLASH_START;
    uint8_t last_percent = 0;

    progress_hook = progress;
    if (detail) {
        *detail = NULL;
    }

    fres = f_open(&file, filename, FA_READ);
    if (fres != FR_OK) {
        return APP_FLASH_ERR_FILE;
    }

    file_size = f_size(&file);
    if (file_size == 0) {
        f_close(&file);
        return APP_FLASH_ERR_FILE;
    }
    if (file_size > APP_FLASH_MAX_SIZE) {
        f_close(&file);
        return APP_FLASH_ERR_TOO_BIG;
    }

    AppFlashResult result = erase_region(file_size);
    if (result != APP_FLASH_OK) {
        f_close(&file);
        return result;
    }

    while (written < file_size) {
        uint32_t remaining = file_size - written;
        uint32_t to_read = (remaining > PROGRAM_CHUNK) ? PROGRAM_CHUNK : remaining;

        fres = f_read(&file, chunk_buffer, to_read, &bytes_read);
        if (fres != FR_OK || bytes_read != to_read) {
            f_close(&file);
            return APP_FLASH_ERR_FILE;
        }

        AppFlashResult write_result = program_chunk(address, chunk_buffer, bytes_read);
        if (write_result != APP_FLASH_OK) {
            f_close(&file);
            return write_result;
        }

        written += bytes_read;
        address += bytes_read;

        uint8_t percent = (uint8_t)((written * 100u) / file_size);
        if (percent > 100) {
            percent = 100;
        }
        if (progress && percent >= last_percent + 5) {
            last_percent = (uint8_t)(percent - (percent % 5));
            progress(last_percent);
        }
    }

    f_close(&file);

    /* Read the programmed area back and compare it with the file content. */
    fres = f_open(&file, filename, FA_READ);
    if (fres != FR_OK) {
        return APP_FLASH_ERR_VERIFY;
    }

    written = 0;
    address = APP_FLASH_START;
    while (written < file_size) {
        uint32_t remaining = file_size - written;
        uint32_t to_read = (remaining > PROGRAM_CHUNK) ? PROGRAM_CHUNK : remaining;

        fres = f_read(&file, chunk_buffer, to_read, &bytes_read);
        if (fres != FR_OK || bytes_read != to_read) {
            f_close(&file);
            return APP_FLASH_ERR_VERIFY;
        }

        for (uint32_t i = 0; i < bytes_read; i++) {
            if (flash_read_byte(address + i) != chunk_buffer[i]) {
                f_close(&file);
                return APP_FLASH_ERR_VERIFY;
            }
        }

        written += bytes_read;
        address += bytes_read;
    }

    f_close(&file);
    return APP_FLASH_OK;
}
