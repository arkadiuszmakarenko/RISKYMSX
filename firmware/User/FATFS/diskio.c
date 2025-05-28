/*-----------------------------------------------------------------------*/
/* Low level disk I/O module SKELETON for FatFs     (C)ChaN, 2019        */
/*-----------------------------------------------------------------------*/
/* If a working storage control module is available, it should be        */
/* attached to the FatFs via a glue function rather than modifying it.   */
/* This is an example of glue functions to attach various exsisting      */
/* storage control modules to the FatFs module with a defined API.       */
/*-----------------------------------------------------------------------*/

#include "ff.h"     /* Obtains integer types */
#include "diskio.h" /* Declarations of disk functions */
#include "usb_disk.h"

/* Definitions of physical drive number for each drive */
#define DEV_RAM 0 /* Example: Map Ramdisk to physical drive 0 */
#define DEV_MMC 1 /* Example: Map MMC/SD card to physical drive 1 */
#define DEV_USB 2 /* Example: Map USB MSD to physical drive 2 */

static uint32_t block_count = 0, block_size = 0;

/*-----------------------------------------------------------------------*/
/* Get Drive Status                                                      */
/*-----------------------------------------------------------------------*/

DSTATUS disk_status (
    BYTE pdrv /* Physical drive nmuber to identify the drive */
) {
    uint8_t res = USBH_PreDeal();
    if (res == 0 || res == 0xFF)
        return 0;  // Disk OK
    else
        return STA_NOINIT;
}

/*-----------------------------------------------------------------------*/
/* Inidialize a Drive                                                    */
/*-----------------------------------------------------------------------*/

DSTATUS disk_initialize (
    BYTE pdrv /* Physical drive nmuber to identify the drive */
) {
    uint8_t res = USBH_PreDeal();
    if (res == 0 || res == 0xFF) {
        if (usb_scsi_read_capacity (&block_count, &block_size) != 0 || block_size != 512)
            return STA_NOINIT;
        return 0;  // Disk OK
    } else
        return STA_NOINIT;
}

/*-----------------------------------------------------------------------*/
/* Read Sector(s)                                                        */
/*-----------------------------------------------------------------------*/

DRESULT disk_read (
    BYTE pdrv,    /* Physical drive nmuber to identify the drive */
    BYTE *buff,   /* Data buffer to store read data */
    LBA_t sector, /* Start sector in LBA */
    UINT count    /* Number of sectors to read */
) {

    for (UINT i = 0; i < count; i++) {
        if (usb_scsi_read_sector (sector + i, buff + i * 512, block_size) != 0)
            return RES_ERROR;
    }
    return RES_OK;
}

/*-----------------------------------------------------------------------*/
/* Miscellaneous Functions                                               */
/*-----------------------------------------------------------------------*/

DRESULT disk_ioctl (
    BYTE pdrv, /* Physical drive nmuber (0..) */
    BYTE cmd,  /* Control code */
    void *buff /* Buffer to send/receive control data */
) {
    uint32_t block_count = 0, block_size = 0;
    switch (cmd) {
    case GET_SECTOR_COUNT:
        if (usb_scsi_read_capacity (&block_count, &block_size) == 0) {
            *(DWORD *)buff = block_count + 1;  // block_count is last LBA
            return RES_OK;
        }
        return RES_ERROR;
    case GET_SECTOR_SIZE:
        if (usb_scsi_read_capacity (&block_count, &block_size) == 0) {
            *(WORD *)buff = (WORD)block_size;
            return RES_OK;
        }
        return RES_ERROR;
    case GET_BLOCK_SIZE:
        *(DWORD *)buff = 1;  // Erase block size (not supported, return 1)
        return RES_OK;
    case CTRL_SYNC:
        return RES_OK;  // Nothing to do for USB
    default:
        return RES_PARERR;
    }
}
