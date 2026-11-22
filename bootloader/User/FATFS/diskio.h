/* Low level disk I/O for the bootloader: the USB stick is the only medium. */
#ifndef _DISKIO_DEFINED
#define _DISKIO_DEFINED

#include "ff.h"

typedef BYTE DSTATUS;

typedef enum {
    RES_OK = 0,
    RES_ERROR,
    RES_WRERR,
    RES_ABORT,
    RES_NOTRDY,
    RES_PARERR
} DRESULT;

DSTATUS disk_initialize(BYTE pdrv);
DSTATUS disk_status(BYTE pdrv);
DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count);
DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count);
DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff);

#define STA_NOINIT 0x01
#define STA_NODISK 0x02
#define STA_PROTECT 0x04

#define CTRL_SYNC 0
#define GET_SECTOR_COUNT 1
#define GET_SECTOR_SIZE 2
#define GET_BLOCK_SIZE 3
#define CTRL_TRIM 4
#define CTRL_POWER 5
#define CTRL_LOCK 6
#define CTRL_EJECT 7
#define CTRL_FORMAT 8

#endif
