#include "usb_disk.h"
#include "debug.h"
#include "ch32v30x_usbfs_host.h"
#include "usb_host_config.h"
#include <string.h>

/*
 * Read-only USB mass storage access for the bootloader.
 *
 * Ported from the cartridge firmware's usb_disk.c. The write paths, the
 * Nextor raw disk layer and the multi-block helpers are omitted: the
 * bootloader only reads the update image from the USB stick.
 */

__attribute__((aligned(4))) uint8_t Com_Buffer[DEF_COM_BUF_LEN];
__attribute__((aligned(4))) uint8_t DevDesc_Buf[18];
struct _ROOT_HUB_DEVICE RootHubDev[DEF_TOTAL_ROOT_HUB];
struct __HOST_CTL HostCtl[DEF_TOTAL_ROOT_HUB * DEF_ONE_USB_SUP_DEV_TOTAL];

uint8_t usb_out_ep;
uint8_t out_tog;
uint8_t usb_in_ep;
uint8_t in_tog;
static uint8_t msc_interface_number = 0;

static UsbDeviceInfo device_info;

const UsbDeviceInfo *UsbDisk_GetDeviceInfo(void)
{
    return &device_info;
}

typedef struct {
    uint32_t dCBWSignature;
    uint32_t dCBWTag;
    uint32_t dCBWDataTransferLength;
    uint8_t bmCBWFlags;
    uint8_t bCBWLUN;
    uint8_t bCBWCBLength;
    uint8_t CBWCB[16];
} __attribute__((packed)) CBW_t;

typedef struct {
    uint32_t dCSWSignature;
    uint32_t dCSWTag;
    uint32_t dCSWDataResidue;
    uint8_t bCSWStatus;
} __attribute__((packed)) CSW_t;

static uint8_t usb_send_cbw(const CBW_t *cbw)
{
    uint16_t plen = sizeof(CBW_t);
    uint8_t res;

    for (int tries = 0; tries < 40; tries++) {
        res = USBFSH_SendEndpData(usb_out_ep, &out_tog, (uint8_t *)cbw, plen);
        if (res == ERR_SUCCESS) return res;
        if ((tries % 10) == 9) {
            uint8_t ep0 = RootHubDev[DEF_USB_PORT_FS].bEp0MaxPks;
            (void)USBFSH_ClearEndpStall(ep0, usb_out_ep);
            out_tog = 0;
            Delay_Ms(2);
        }
        Delay_Ms(1);
    }
    return res;
}

static uint8_t usb_recv_csw(CSW_t *csw)
{
    uint16_t plen = sizeof(CSW_t);
    uint8_t res;

    for (int tries = 0; tries < 60; tries++) {
        res = USBFSH_GetEndpData(usb_in_ep, &in_tog, (uint8_t *)csw, &plen);
        if (res == ERR_SUCCESS) return res;
        if ((tries % 10) == 9) {
            uint8_t ep0 = RootHubDev[DEF_USB_PORT_FS].bEp0MaxPks;
            (void)USBFSH_ClearEndpStall(ep0, (uint8_t)(0x80 | usb_in_ep));
            in_tog = 0;
            Delay_Ms(2);
        }
        Delay_Ms(1);
    }
    return res;
}

static uint8_t bulk_in_read_retry(uint8_t *buf, uint16_t *plen, int max_retries)
{
    uint8_t res;
    int tries = 0;

    do {
        res = USBFSH_GetEndpData(usb_in_ep, &in_tog, buf, &plen[0]);
        if (res == ERR_SUCCESS) return res;
        if ((tries % 10) == 9) {
            uint8_t ep0 = RootHubDev[DEF_USB_PORT_FS].bEp0MaxPks;
            (void)USBFSH_ClearEndpStall(ep0, (uint8_t)(0x80 | usb_in_ep));
            in_tog = 0;
            Delay_Ms(2);
        }
        Delay_Ms(1);
        tries++;
    } while (tries < max_retries);
    return res;
}

static uint8_t msc_class_request(uint8_t bmRequestType, uint8_t bRequest,
                                 uint16_t wValue, uint16_t wIndex,
                                 uint8_t *buf, uint16_t *plen)
{
    pUSBFS_SetupRequest->bRequestType = bmRequestType;
    pUSBFS_SetupRequest->bRequest = bRequest;
    pUSBFS_SetupRequest->wValue = wValue;
    pUSBFS_SetupRequest->wIndex = wIndex;
    pUSBFS_SetupRequest->wLength = (plen != NULL && buf != NULL) ? *plen : 0;
    uint8_t ep0 = RootHubDev[DEF_USB_PORT_FS].bEp0MaxPks;
    return USBFSH_CtrlTransfer(ep0, buf, plen);
}

uint8_t msc_mass_storage_reset(void)
{
    uint8_t rc = msc_class_request(0x21, 0xFF, 0x0000, msc_interface_number, NULL, NULL);

    if (rc == ERR_SUCCESS) {
        uint8_t ep0 = RootHubDev[DEF_USB_PORT_FS].bEp0MaxPks;
        (void)USBFSH_ClearEndpStall(ep0, (uint8_t)(0x80 | usb_in_ep));
        Delay_Ms(2);
        (void)USBFSH_ClearEndpStall(ep0, usb_out_ep);
        in_tog = 0;
        out_tog = 0;
        Delay_Ms(20);
        return 0;
    }
    return 1;
}

void USB_Initialization(void)
{
    USBFS_RCC_Init();
    USBFS_Host_Init(ENABLE);
    memset(&RootHubDev[DEF_USB_PORT_FS].bStatus, 0, sizeof(struct _ROOT_HUB_DEVICE));
    memset(&HostCtl[DEF_USB_PORT_FS].InterfaceNum, 0, sizeof(struct __HOST_CTL));
}

uint8_t USBH_EnumRootDevice(uint8_t usb_port)
{
    uint8_t s;
    uint8_t enum_cnt;
    uint8_t cfg_val;
    uint16_t i;
    uint16_t len;

    enum_cnt = 0;
ENUM_START:
    Delay_Ms(100);
    enum_cnt++;
    Delay_Ms(8 << enum_cnt);

    USBFSH_ResetRootHubPort(0);
    for (i = 0, s = 0; i < DEF_RE_ATTACH_TIMEOUT; i++) {
        if (USBFSH_EnableRootHubPort(&RootHubDev[usb_port].bSpeed) == ERR_SUCCESS) {
            i = 0;
            s++;
            if (s > 6) break;
        }
        Delay_Ms(1);
    }
    if (i) {
        if (enum_cnt <= 5) goto ENUM_START;
        return ERR_USB_DISCON;
    }

    USBFSH_SetSelfSpeed(RootHubDev[usb_port].bSpeed);

    s = USBFSH_GetDeviceDescr(&RootHubDev[usb_port].bEp0MaxPks, DevDesc_Buf);
    if (s != ERR_SUCCESS) {
        if (enum_cnt <= 5) goto ENUM_START;
        return DEF_DEV_DESCR_GETFAIL;
    }

    /* Keep the device descriptor details for reporting to the user. */
    device_info.device_class = DevDesc_Buf[4];
    device_info.usb_version = (uint16_t)(DevDesc_Buf[2] | ((uint16_t)DevDesc_Buf[3] << 8));

    RootHubDev[usb_port].bAddress = (uint8_t)(DEF_USB_PORT_FS + USB_DEVICE_ADDR);
    s = USBFSH_SetUsbAddress(RootHubDev[usb_port].bEp0MaxPks, RootHubDev[usb_port].bAddress);
    if (s != ERR_SUCCESS) {
        if (enum_cnt <= 5) goto ENUM_START;
        return DEF_DEV_ADDR_SETFAIL;
    }
    Delay_Ms(5);

    s = USBFSH_GetConfigDescr(RootHubDev[usb_port].bEp0MaxPks, Com_Buffer, DEF_COM_BUF_LEN, &len);
    if (s != ERR_SUCCESS) {
        if (enum_cnt <= 5) goto ENUM_START;
        return DEF_CFG_DESCR_GETFAIL;
    }
    cfg_val = ((PUSB_CFG_DESCR)Com_Buffer)->bConfigurationValue;

    s = USBFSH_SetUsbConfig(RootHubDev[usb_port].bEp0MaxPks, cfg_val);
    if (s != ERR_SUCCESS) {
        if (enum_cnt <= 5) goto ENUM_START;
        return ERR_USB_UNSUPPORT;
    }

    /* Locate the mass storage interface and its bulk endpoints. */
    uint8_t *p = Com_Buffer;
    uint8_t *end = Com_Buffer + len;
    uint8_t msc_found = 0;
    uint8_t in_found = 0;
    uint8_t out_found = 0;

    usb_in_ep = 0;
    usb_out_ep = 0;
    in_tog = 0;
    out_tog = 0;

    while (p < end) {
        if (p[0] == 0) break;
        if (p[1] == 0x04) {
            uint8_t class = p[5];
            uint8_t subclass = p[6];
            uint8_t protocol = p[7];
            if (class == 0x08 && subclass == 0x06 && protocol == 0x50) {
                msc_found = 1;
                msc_interface_number = p[2];
            }
        }
        if (p[1] == 0x05 && msc_found) {
            uint8_t ep_addr = p[2];
            uint8_t ep_attr = p[3];
            if ((ep_attr & 0x03) == 0x02) {
                if (ep_addr & 0x80) {
                    usb_in_ep = ep_addr & 0x0F;
                    in_tog = 0;
                    in_found = 1;
                } else {
                    usb_out_ep = ep_addr & 0x0F;
                    out_tog = 0;
                    out_found = 1;
                }
            }
        }
        p += p[0];
    }

    if (!msc_found || !in_found || !out_found) {
        return ERR_USB_UNSUPPORT;
    }

    device_info.enumerated = 1;
    device_info.speed = RootHubDev[usb_port].bSpeed;
    device_info.device_address = RootHubDev[usb_port].bAddress;
    device_info.ep0_max_packet = RootHubDev[usb_port].bEp0MaxPks;
    device_info.msc_interface = msc_interface_number;
    device_info.bulk_in_ep = usb_in_ep;
    device_info.bulk_out_ep = usb_out_ep;

    return ERR_SUCCESS;
}

uint8_t USBH_PreDeal(void)
{
    uint8_t usb_port = DEF_USB_PORT_FS;
    uint8_t index;
    uint8_t ret;

    ret = USBFSH_CheckRootHubPortStatus(RootHubDev[usb_port].bStatus);

    if (ret == ROOT_DEV_CONNECTED) {
        if (RootHubDev[usb_port].bStatus != ROOT_DEV_SUCCESS) {
            RootHubDev[usb_port].bStatus = ROOT_DEV_CONNECTED;
            RootHubDev[usb_port].DeviceIndex = usb_port * DEF_ONE_USB_SUP_DEV_TOTAL;

            ret = USBH_EnumRootDevice(usb_port);
            if (ret == ERR_SUCCESS) {
                RootHubDev[usb_port].bStatus = ROOT_DEV_SUCCESS;
                return DEF_SUCCESS;
            }
            RootHubDev[usb_port].bStatus = ROOT_DEV_FAILED;
            return DEF_ERR_ENUM;
        }
        return DEF_SUCCESS;
    } else if (ret == ROOT_DEV_DISCONNECT) {
        index = RootHubDev[usb_port].DeviceIndex;
        memset(&RootHubDev[usb_port].bStatus, 0, sizeof(struct _ROOT_HUB_DEVICE));
        memset(&HostCtl[index].InterfaceNum, 0, sizeof(struct __HOST_CTL));
        return DEF_ERR_DETECT;
    }

    return DEF_DEFAULT;
}

void ClearUSB(void)
{
    uint8_t usb_port = DEF_USB_PORT_FS;
    uint8_t index = RootHubDev[usb_port].DeviceIndex;

    memset(&RootHubDev[usb_port].bStatus, 0, sizeof(struct _ROOT_HUB_DEVICE));
    memset(&HostCtl[index].InterfaceNum, 0, sizeof(struct __HOST_CTL));
    memset(&device_info, 0, sizeof(device_info));
}

static uint8_t scsi_read_capacity10_once(uint32_t *block_count, uint32_t *block_size)
{
    CBW_t cbw;
    CSW_t csw;
    uint8_t cap_buf[8];
    uint8_t res;
    uint16_t plen;

    memset(&cbw, 0, sizeof(cbw));
    cbw.dCBWSignature = 0x43425355;
    cbw.dCBWTag = 0x11223344;
    cbw.dCBWDataTransferLength = 8;
    cbw.bmCBWFlags = 0x80;
    cbw.bCBWCBLength = 10;
    cbw.CBWCB[0] = 0x25;

    res = usb_send_cbw(&cbw);
    if (res != ERR_SUCCESS) return 1;

    Delay_Ms(2);
    plen = 8;
    res = bulk_in_read_retry(cap_buf, &plen, 40);
    if (res != ERR_SUCCESS || plen != 8) return 2;

    Delay_Ms(2);
    res = usb_recv_csw(&csw);
    if (res != ERR_SUCCESS || csw.bCSWStatus != 0) return 3;

    Delay_Ms(1);
    *block_count = (cap_buf[0] << 24) | (cap_buf[1] << 16) | (cap_buf[2] << 8) | cap_buf[3];
    *block_size = (cap_buf[4] << 24) | (cap_buf[5] << 16) | (cap_buf[6] << 8) | cap_buf[7];

    device_info.block_count = *block_count + 1;
    device_info.block_size = *block_size;
    device_info.capacity_known = 1;
    return 0;
}

static uint8_t scsi_read_capacity16_once(uint32_t *block_count, uint32_t *block_size)
{
    CBW_t cbw;
    CSW_t csw;
    uint8_t res;
    uint16_t plen = 32;
    uint8_t cap_buf[32];

    memset(&cbw, 0, sizeof(cbw));
    cbw.dCBWSignature = 0x43425355;
    cbw.dCBWTag = 0x11223366;
    cbw.dCBWDataTransferLength = 32;
    cbw.bmCBWFlags = 0x80;
    cbw.bCBWCBLength = 16;
    cbw.CBWCB[0] = 0x9E;
    cbw.CBWCB[1] = 0x10;
    cbw.CBWCB[10] = 0x00;
    cbw.CBWCB[11] = 0x00;
    cbw.CBWCB[12] = 0x00;
    cbw.CBWCB[13] = 0x20;

    res = usb_send_cbw(&cbw);
    if (res != ERR_SUCCESS) return 1;
    Delay_Ms(1);
    res = bulk_in_read_retry(cap_buf, &plen, 40);
    if (res != ERR_SUCCESS || plen < 12) return 2;
    Delay_Ms(1);
    res = usb_recv_csw(&csw);
    if (res != ERR_SUCCESS || csw.bCSWStatus != 0) return 3;

    uint32_t lba_hi = (cap_buf[0] << 24) | (cap_buf[1] << 16) | (cap_buf[2] << 8) | cap_buf[3];
    uint32_t lba_lo = (cap_buf[4] << 24) | (cap_buf[5] << 16) | (cap_buf[6] << 8) | cap_buf[7];
    *block_count = (lba_hi != 0) ? 0xFFFFFFFF : lba_lo;
    *block_size = (cap_buf[8] << 24) | (cap_buf[9] << 16) | (cap_buf[10] << 8) | cap_buf[11];

    device_info.block_count = *block_count + 1;
    device_info.block_size = *block_size;
    device_info.capacity_known = 1;
    return 0;
}

uint8_t usb_scsi_read_capacity(uint32_t *block_count, uint32_t *block_size)
{
    for (int attempt = 0; attempt < 2; attempt++) {
        uint8_t r = scsi_read_capacity10_once(block_count, block_size);
        if (r == 0) return 0;

        uint8_t sense[18] = {0};
        (void)usb_scsi_request_sense(sense, sizeof(sense));
        (void)msc_mass_storage_reset();
        Delay_Ms(5);
    }

    uint8_t r16 = scsi_read_capacity16_once(block_count, block_size);
    if (r16 == 0) return 0;
    return 3;
}

static uint8_t scsi_read_sector_once(uint32_t lba, uint8_t *buf, uint32_t block_size)
{
    CBW_t cbw;
    CSW_t csw;
    uint8_t res;
    uint16_t plen;
    uint32_t bytes_received = 0;

    memset(&cbw, 0, sizeof(cbw));
    cbw.dCBWSignature = 0x43425355;
    cbw.dCBWTag = 0xCAFEBABE;
    cbw.dCBWDataTransferLength = block_size;
    cbw.bmCBWFlags = 0x80;
    cbw.bCBWCBLength = 10;
    cbw.CBWCB[0] = 0x28;
    cbw.CBWCB[2] = (lba >> 24) & 0xFF;
    cbw.CBWCB[3] = (lba >> 16) & 0xFF;
    cbw.CBWCB[4] = (lba >> 8) & 0xFF;
    cbw.CBWCB[5] = (lba) & 0xFF;
    cbw.CBWCB[8] = 1;

    res = usb_send_cbw(&cbw);
    if (res != ERR_SUCCESS) return 1;

    while (bytes_received < block_size) {
        plen = block_size - bytes_received;
        if (plen > 64) plen = 64;

        int nak_retries = 0;
        do {
            res = USBFSH_GetEndpData(usb_in_ep, &in_tog, buf + bytes_received, &plen);
            if (res != ERR_SUCCESS) {
                Delay_Ms(1);
                nak_retries++;
                if (nak_retries >= 20) return 2;
            }
        } while (res != ERR_SUCCESS);

        if (plen == 0) return 2;
        bytes_received += plen;
    }

    int csw_retries = 0;
    do {
        res = usb_recv_csw(&csw);
        if (res == ERR_SUCCESS && csw.bCSWStatus == 0) break;
        Delay_Ms(1);
        csw_retries++;
    } while (csw_retries < 40);

    if (res != ERR_SUCCESS || csw.bCSWStatus != 0) return 3;
    return 0;
}

uint8_t usb_scsi_read_sector(uint32_t lba, uint8_t *buf, uint32_t block_size)
{
    for (int attempt = 0; attempt < 2; attempt++) {
        uint8_t r = scsi_read_sector_once(lba, buf, block_size);
        if (r == 0) return 0;

        uint8_t sense[18] = {0};
        (void)usb_scsi_request_sense(sense, sizeof(sense));
        (void)msc_mass_storage_reset();
        Delay_Ms(5);
    }
    return 3;
}

uint8_t usb_scsi_request_sense(uint8_t *buf, uint16_t len)
{
    CBW_t cbw;
    CSW_t csw;
    uint8_t res;
    uint16_t plen = len;

    memset(&cbw, 0, sizeof(cbw));
    cbw.dCBWSignature = 0x43425355;
    cbw.dCBWTag = 0x53454E53;
    cbw.dCBWDataTransferLength = len;
    cbw.bmCBWFlags = 0x80;
    cbw.bCBWCBLength = 6;
    cbw.CBWCB[0] = 0x03;
    cbw.CBWCB[4] = (uint8_t)len;

    res = usb_send_cbw(&cbw);
    if (res != ERR_SUCCESS) return 1;
    Delay_Ms(1);
    res = bulk_in_read_retry(buf, &plen, 40);
    if (res != ERR_SUCCESS) return 2;
    Delay_Ms(1);
    res = usb_recv_csw(&csw);
    if (res != ERR_SUCCESS || csw.bCSWStatus != 0) return 3;
    return 0;
}

/* Enumerates the attached stick if needed. Returns DEF_SUCCESS when a device
 * is ready, DEF_ERR_DETECT when nothing is attached and DEF_ERR_ENUM when
 * enumeration failed. */
uint8_t USB_TryEnsureEnumerated(void)
{
    uint8_t r = DEF_ERR_ENUM;

    for (int tries = 0; tries < 5; tries++) {
        r = USBH_PreDeal();
        if (r == DEF_ERR_DETECT) return DEF_ERR_DETECT;
        if (r == DEF_SUCCESS) break;
        if (RootHubDev[DEF_USB_PORT_FS].bStatus == ROOT_DEV_SUCCESS) {
            r = DEF_SUCCESS;
            break;
        }
        if (USBH_EnumRootDevice(DEF_USB_PORT_FS) == ERR_SUCCESS) {
            RootHubDev[DEF_USB_PORT_FS].bStatus = ROOT_DEV_SUCCESS;
            r = DEF_SUCCESS;
            break;
        }
        Delay_Ms(100);
    }

    return r;
}