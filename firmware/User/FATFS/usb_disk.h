#ifndef __USB_DISK_H
#define __USB_DISK_H


#include "debug.h"
#include "stdio.h"
#include "string.h"
#include "ch32v30x.h"
#include "ch32v30x_usbfs_host.h"
#include "usb_host_config.h"
#include "ff.h"

/* Status Definitions */
#define DEF_SUCCESS 0x00    /* Operation Success */
#define DEF_DEFAULT 0xFF    /* Operation Default Status */
#define DEF_ERR_DETECT 0xF1 /* Operation, USB device not detected */
#define DEF_ERR_ENUM 0xF2   /* Operation, Host enumeration failure */
#define DEF_ERR_FILE 0xF3   /* Operation, File name incorrect or no such file */
#define DEF_ERR_FLASH 0xF4  /* Operation, Flash operation failure */
#define DEF_ERR_VERIFY 0xF5 /* Operation, Flash data verify error */
#define DEF_ERR_LENGTH 0xF6 /* Operation, Flash data length verify error */

/* USB MSC SCSI functions */


void USB_Initialization (void);
uint8_t USBH_EnumRootDevice (uint8_t usb_port);
uint8_t USBH_PreDeal (void);
uint8_t usb_scsi_test_unit_ready (void);
uint8_t usb_scsi_read_capacity (uint32_t *block_count, uint32_t *block_size);
uint8_t usb_scsi_read_sector (uint32_t lba, uint8_t *buf, uint32_t block_size);
void ClearUSB (void);

#endif
