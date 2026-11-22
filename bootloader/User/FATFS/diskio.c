#include "diskio.h"
#include "usb_disk.h"

/*
 * FatFs disk I/O for the bootloader. The USB mass storage device is the only
 * medium; the read path is all the bootloader needs because the image is read
 * from the stick and written to the main flash.
 */

static uint32_t block_count = 0;
static uint32_t block_size = 0;

DSTATUS disk_initialize(BYTE pdrv)
{
    (void)pdrv;

    ClearUSB();
    uint8_t res = USBH_PreDeal();
    if (res == 0 || res == 0xFF) {
        if (usb_scsi_read_capacity(&block_count, &block_size) != 0 || block_size != 512) {
            return STA_NOINIT;
        }
        return 0;
    }
    return STA_NOINIT;
}

DSTATUS disk_status(BYTE pdrv)
{
    (void)pdrv;
    return RES_OK;
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count)
{
    (void)pdrv;

    for (UINT i = 0; i < count; i++) {
        if (usb_scsi_read_sector(sector + i, buff + i * 512, block_size) != 0) {
            return RES_ERROR;
        }
    }
    return RES_OK;
}

/* The bootloader never writes to the stick. */
DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count)
{
    (void)pdrv;
    (void)buff;
    (void)sector;
    (void)count;
    return RES_WRERR;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
    (void)pdrv;
    uint32_t count = 0;
    uint32_t size = 0;

    switch (cmd) {
    case GET_SECTOR_COUNT:
        if (usb_scsi_read_capacity(&count, &size) == 0) {
            *(DWORD *)buff = count + 1; /* count is the last LBA */
            return RES_OK;
        }
        return RES_ERROR;
    case GET_SECTOR_SIZE:
        if (usb_scsi_read_capacity(&count, &size) == 0) {
            *(WORD *)buff = (WORD)size;
            return RES_OK;
        }
        return RES_ERROR;
    case GET_BLOCK_SIZE:
        *(DWORD *)buff = 1;
        return RES_OK;
    case CTRL_SYNC:
        return RES_OK;
    default:
        return RES_PARERR;
    }
}
