/*
 * raw_disk.h - the USB stick as a raw 512-byte-sector disk for Nextor.
 *
 * No filesystem is involved: sectors are addressed by LBA and go straight to
 * SCSI READ(10)/WRITE(10) through usb_disk.c.
 */

#ifndef __RAW_DISK_H
#define __RAW_DISK_H

#include <stdint.h>

#define RAW_DISK_SECTOR_SIZE   512U

/* Media state codes returned by the mailbox STATUS command. */
#define RAW_MEDIA_NONE          0U
#define RAW_MEDIA_READY         1U
#define RAW_MEDIA_CHANGED       2U

/* Drop the cached capacity/INQUIRY and mark the device absent. */
void RawDisk_Invalidate (void);

/* Enumerate the stick and read its capacity. Can take hundreds of
 * milliseconds: main loop only, with no mailbox request in flight. */
uint8_t RawDisk_Probe (void);
uint8_t RawDisk_HasProbed (void);

uint8_t  RawDisk_IsPresent (void);
uint32_t RawDisk_SectorCount (void);
uint16_t RawDisk_SectorSize (void);

/* Synthesised CHS geometry. */
void RawDisk_Geometry (uint16_t *cylinders, uint8_t *heads,
                       uint8_t *sectors_per_track);

uint8_t RawDisk_MediaState (void);
void    RawDisk_MediaLatchConsume (void);

/* INQUIRY strings, NUL-terminated, "" until the first successful probe. */
const char *RawDisk_Manufacturer (void);
const char *RawDisk_Model (void);

/* Transfer `count` sectors (1 or more). Return 1 on success, 0 on failure. */
uint8_t RawDisk_ReadSectors (uint32_t lba, uint16_t count, uint8_t *dst);
uint8_t RawDisk_WriteSectors (uint32_t lba, uint16_t count, const uint8_t *src);

#endif /* __RAW_DISK_H */
