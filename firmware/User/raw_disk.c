/*
 * raw_disk.c - raw-sector view of the USB mass-storage stick for Nextor.
 */

#include "usb_disk.h"
#include "raw_disk.h"
#include "nextor.h"

static struct {
    uint8_t  probed;
    uint8_t  present;
    uint8_t  changed;
    uint32_t sectors;
    char     manufacturer[9];
    char     model[17];
} s;

void RawDisk_Invalidate (void) {
    if (s.probed != 0U) {
        s.changed = 1U;
    }
    s.present = 0U;
    s.sectors = 0U;
    s.manufacturer[0] = '\0';
    s.model[0] = '\0';
}

uint8_t RawDisk_HasProbed (void) {
    return s.probed;
}

// Copy an INQUIRY field up to the first space or non-printable byte.
static void inq_copy (char *dst, const uint8_t *src, uint8_t width) {
    uint8_t n = 0U;
    while (n < width && src[n] > 0x20U && src[n] < 0x7FU) {
        dst[n] = (char)src[n];
        n++;
    }
    dst[n] = '\0';
}

uint8_t RawDisk_Probe (void) {
    uint32_t last_lba = 0U;
    uint32_t bsize = 0U;
    uint8_t  inq[36] = {0};

    if (Nextor_RequestPending () != 0U) return 0U;
    s.probed = 1U;
    s.present = 0U;
    s.sectors = 0U;

    if (USB_TryEnsureEnumerated () != DEF_SUCCESS) return 0U;
    if (Nextor_RequestPending () != 0U) return 0U;
    if (usb_scsi_read_capacity (&last_lba, &bsize) != 0U
        || bsize != RAW_DISK_SECTOR_SIZE) {
        return 0U;
    }

    // usb_scsi_read_capacity() reports the last LBA, not the block count.
    s.sectors = (last_lba == 0xFFFFFFFFU) ? last_lba : last_lba + 1U;

    if (usb_scsi_inquiry (inq, sizeof (inq)) == 0U) {
        inq_copy (s.manufacturer, &inq[8], 8U);
        inq_copy (s.model, &inq[16], 16U);
    } else {
        s.manufacturer[0] = '\0';
        s.model[0] = '\0';
    }
    s.present = 1U;
    s.changed = 1U;
    return 1U;
}

uint8_t RawDisk_IsPresent (void) {
    return (uint8_t)((s.present != 0U && s.sectors != 0U) ? 1U : 0U);
}

uint32_t RawDisk_SectorCount (void) {
    return s.sectors;
}

uint16_t RawDisk_SectorSize (void) {
    return RAW_DISK_SECTOR_SIZE;
}

void RawDisk_Geometry (uint16_t *cylinders, uint8_t *heads,
                       uint8_t *sectors_per_track) {
    uint32_t cyl = s.sectors / (16U * 63U);
    if (cyl > 0xFFFFU) cyl = 0xFFFFU;
    if (cyl == 0U) cyl = 1U;
    *cylinders = (uint16_t)cyl;
    *heads = 16U;
    *sectors_per_track = 63U;
}

uint8_t RawDisk_MediaState (void) {
    if (RawDisk_IsPresent () == 0U) return RAW_MEDIA_NONE;
    return (s.changed != 0U) ? RAW_MEDIA_CHANGED : RAW_MEDIA_READY;
}

void RawDisk_MediaLatchConsume (void) {
    s.changed = 0U;
}

const char *RawDisk_Manufacturer (void) {
    return s.manufacturer;
}

const char *RawDisk_Model (void) {
    return s.model;
}

static uint8_t range_ok (uint32_t lba, uint16_t count) {
    return (uint8_t)(RawDisk_IsPresent () != 0U && count != 0U
                     && lba < s.sectors && count <= (s.sectors - lba));
}

uint8_t RawDisk_ReadSectors (uint32_t lba, uint16_t count, uint8_t *dst) {
    if (range_ok (lba, count) == 0U) return 0U;
    return (uint8_t)(usb_scsi_read_blocks (lba, dst, RAW_DISK_SECTOR_SIZE, count) == 0U);
}

uint8_t RawDisk_WriteSectors (uint32_t lba, uint16_t count, const uint8_t *src) {
    if (range_ok (lba, count) == 0U) return 0U;
    return (uint8_t)(usb_scsi_write_blocks (lba, src, RAW_DISK_SECTOR_SIZE, count) == 0U);
}
