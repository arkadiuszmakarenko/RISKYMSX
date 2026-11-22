#ifndef USB_DISK_H
#define USB_DISK_H

#include <stdint.h>

/* Status definitions. */
#define DEF_SUCCESS 0x00     /* Operation success */
#define DEF_DEFAULT 0xFF     /* No change */
#define DEF_ERR_DETECT 0xF1  /* No USB device attached */
#define DEF_ERR_ENUM 0xF2    /* Enumeration failed */
#define DEF_ERR_FILE 0xF3    /* File missing or unreadable */
#define DEF_ERR_FLASH 0xF4   /* Flash operation failed */
#define DEF_ERR_VERIFY 0xF5  /* Flash verification failed */
#define DEF_ERR_LENGTH 0xF6  /* Image length out of range */

void USB_Initialization(void);
uint8_t USBH_EnumRootDevice(uint8_t usb_port);
uint8_t USBH_PreDeal(void);
uint8_t usb_scsi_read_capacity(uint32_t *block_count, uint32_t *block_size);
uint8_t usb_scsi_read_sector(uint32_t lba, uint8_t *buf, uint32_t block_size);
uint8_t usb_scsi_request_sense(uint8_t *buf, uint16_t len);
uint8_t msc_mass_storage_reset(void);
uint8_t USB_TryEnsureEnumerated(void);
void ClearUSB(void);

/* Details of the enumerated device, for reporting to the user. */
typedef struct {
    uint8_t enumerated;     /* non-zero when a mass storage device is ready */
    uint8_t speed;          /* USB_LOW_SPEED / USB_FULL_SPEED / USB_HIGH_SPEED */
    uint8_t device_address;
    uint8_t ep0_max_packet;
    uint8_t msc_interface;  /* interface number of the mass storage interface */
    uint8_t bulk_in_ep;
    uint8_t bulk_out_ep;
    uint8_t device_class;   /* bDeviceClass from the device descriptor */
    uint16_t usb_version;   /* bcdUSB from the device descriptor */
    uint32_t block_count;   /* number of 512 byte sectors, when known */
    uint32_t block_size;
    uint8_t capacity_known;
} UsbDeviceInfo;

const UsbDeviceInfo *UsbDisk_GetDeviceInfo(void);

#endif /* USB_DISK_H */
