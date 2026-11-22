/*
 * Host test shim for the FatFs API used by the bootloader.
 *
 * Only the read-only file operations the bootloader needs are declared here,
 * so the flash programming logic can be exercised on a workstation.
 */
#ifndef FF_HOST_SHIM_H
#define FF_HOST_SHIM_H

#include <stdint.h>

typedef uint8_t BYTE;
typedef uint16_t UINT;
typedef uint32_t DWORD;
typedef uint32_t LBA_t;
typedef char TCHAR;
typedef int FRESULT;

#define FR_OK 0
#define FR_NO_FILE 3
#define FA_READ 0x01

typedef struct {
    uint32_t fptr;
    uint32_t objsize;
} FIL;

FRESULT f_open(FIL *fp, const TCHAR *path, BYTE mode);
FRESULT f_read(FIL *fp, void *buff, UINT btr, UINT *br);
FRESULT f_close(FIL *fp);

#define f_size(fp) ((fp)->objsize)

#endif /* FF_HOST_SHIM_H */