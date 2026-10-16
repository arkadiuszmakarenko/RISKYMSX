#include "usb_disk.h"

/* Variable */
__attribute__ ((aligned (4))) uint8_t Com_Buffer[DEF_COM_BUF_LEN];  // even address , used for host enumcation and udisk operation
__attribute__ ((aligned (4))) uint8_t DevDesc_Buf[18];              // Device Descriptor Buffer
struct _ROOT_HUB_DEVICE RootHubDev[DEF_TOTAL_ROOT_HUB];
struct __HOST_CTL HostCtl[DEF_TOTAL_ROOT_HUB * DEF_ONE_USB_SUP_DEV_TOTAL];

// You need to set these according to your device after enumeration
uint8_t usb_out_ep;  // OUT endpoint address
uint8_t out_tog;     // OUT endpoint toggle
uint8_t usb_in_ep;   // IN endpoint address
uint8_t in_tog;      // IN endpoint toggle
static uint8_t msc_interface_number = 0; // interface number for class-specific requests
// Send a class-specific control request over EP0
static uint8_t msc_class_request (uint8_t bmRequestType, uint8_t bRequest, uint16_t wValue, uint16_t wIndex, uint8_t *buf, uint16_t *plen) {
    // Build setup packet directly in USBFS_TX_Buf via pUSBFS_SetupRequest
    pUSBFS_SetupRequest->bRequestType = bmRequestType;
    pUSBFS_SetupRequest->bRequest = bRequest;
    pUSBFS_SetupRequest->wValue = wValue;
    pUSBFS_SetupRequest->wIndex = wIndex;
    pUSBFS_SetupRequest->wLength = (plen != NULL && buf != NULL) ? *plen : 0;
    // Use current EP0 size from RootHubDev
    uint8_t ep0 = RootHubDev[DEF_USB_PORT_FS].bEp0MaxPks;
    return USBFSH_CtrlTransfer (ep0, buf, plen);
}

// (Removed test helper: Get Max LUN)

// Mass Storage: Bulk-Only Mass Storage Reset (bRequest=0xFF, bmRequestType=00100001b)
uint8_t msc_mass_storage_reset (void) {
    uint8_t rc = msc_class_request (0x21, 0xFF, 0x0000, msc_interface_number, NULL, NULL);
    // After reset, per spec, host should clear HALT on bulk IN/OUT endpoints and reset data toggles
    if (rc == ERR_SUCCESS) {
        // Clear HALT (STALL) on bulk endpoints if any and reset data toggles
        uint8_t ep0 = RootHubDev[DEF_USB_PORT_FS].bEp0MaxPks;
        // IN endpoint has direction bit set in wIndex
        (void)USBFSH_ClearEndpStall (ep0, (uint8_t)(0x80 | usb_in_ep));
        Delay_Ms (2);
        (void)USBFSH_ClearEndpStall (ep0, usb_out_ep);
        in_tog = 0; out_tog = 0;
        Delay_Ms (20);
        return 0;
    }
    return 1;
}

// SCSI WRITE(10) command block wrapper (CBW) structure
typedef struct {
    uint32_t dCBWSignature;
    uint32_t dCBWTag;
    uint32_t dCBWDataTransferLength;
    uint8_t bmCBWFlags;
    uint8_t bCBWLUN;
    uint8_t bCBWCBLength;
    uint8_t CBWCB[16];
} __attribute__ ((packed)) CBW_t;

// SCSI Command Status Wrapper (CSW) structure
typedef struct {
    uint32_t dCSWSignature;
    uint32_t dCSWTag;
    uint32_t dCSWDataResidue;
    uint8_t bCSWStatus;
} __attribute__ ((packed)) CSW_t;

// Helper: Send CBW
static uint8_t usb_send_cbw (const CBW_t *cbw) {
    uint16_t plen = sizeof (CBW_t);
    uint8_t res;
    for (int tries = 0; tries < 40; tries++) {
        res = USBFSH_SendEndpData (usb_out_ep, &out_tog, (uint8_t *)cbw, plen);
        if (res == ERR_SUCCESS) return res;
        // Occasionally clear HALT and reset toggle on persistent errors
        if ((tries % 10) == 9) {
            uint8_t ep0 = RootHubDev[DEF_USB_PORT_FS].bEp0MaxPks;
            (void)USBFSH_ClearEndpStall (ep0, usb_out_ep);
            out_tog = 0;
            Delay_Ms (2);
        }
        Delay_Ms (1);
    }
    return res;
}

// Helper: Receive CSW
static uint8_t usb_recv_csw (CSW_t *csw) {
    uint16_t plen = sizeof (CSW_t);
    uint8_t res;
    for (int tries = 0; tries < 60; tries++) {
        res = USBFSH_GetEndpData (usb_in_ep, &in_tog, (uint8_t *)csw, &plen);
        if (res == ERR_SUCCESS) return res;
        if ((tries % 10) == 9) {
            uint8_t ep0 = RootHubDev[DEF_USB_PORT_FS].bEp0MaxPks;
            (void)USBFSH_ClearEndpStall (ep0, (uint8_t)(0x80 | usb_in_ep));
            in_tog = 0;
            Delay_Ms (2);
        }
        Delay_Ms (1);
    }
    return res;
}

// Helper: Bulk IN with NAK retry
static uint8_t bulk_in_read_retry (uint8_t *buf, uint16_t *plen, int max_retries) {
    uint8_t res;
    int tries = 0;
    do {
        res = USBFSH_GetEndpData (usb_in_ep, &in_tog, buf, &plen[0]);
        if (res == ERR_SUCCESS) return res;
        if ((tries % 10) == 9) {
            uint8_t ep0 = RootHubDev[DEF_USB_PORT_FS].bEp0MaxPks;
            (void)USBFSH_ClearEndpStall (ep0, (uint8_t)(0x80 | usb_in_ep));
            in_tog = 0;
            Delay_Ms (2);
        }
        Delay_Ms (1);
        tries++;
    } while (tries < max_retries);
    return res;
}

void USB_Initialization (void) {


    /* USB Host Initialization */
    USBFS_RCC_Init();
    USBFS_Host_Init (ENABLE);
    memset (&RootHubDev[DEF_USB_PORT_FS].bStatus, 0, sizeof (struct _ROOT_HUB_DEVICE));
    memset (&HostCtl[DEF_USB_PORT_FS].InterfaceNum, 0, sizeof (struct __HOST_CTL));
}

/*********************************************************************
 * @fn      USBH_EnumRootDevice
 *
 * @brief   Generally enumerate a device connected to host port.
 *
 * @para    index: USB host port
 *
 * @return  Enumeration result
 */
uint8_t USBH_EnumRootDevice (uint8_t usb_port) {
    uint8_t s;
    uint8_t enum_cnt;
    uint8_t cfg_val;
    uint16_t i;
    uint16_t len;
    uint8_t msc_interface_found = 0;
    uint8_t msc_in_ep_found = 0, msc_out_ep_found = 0;


    enum_cnt = 0;
ENUM_START:
    /* Delay and wait for the device to stabilize */
    Delay_Ms (100);
    enum_cnt++;
    Delay_Ms (8 << enum_cnt);

    /* Reset the USB device and wait for the USB device to reconnect */
    USBFSH_ResetRootHubPort (0);
    for (i = 0, s = 0; i < DEF_RE_ATTACH_TIMEOUT; i++) {
        if (USBFSH_EnableRootHubPort (&RootHubDev[usb_port].bSpeed) == ERR_SUCCESS) {
            i = 0;
            s++;
            if (s > 6) {
                break;
            }
        }
        Delay_Ms (1);
    }
    if (i) {
        if (enum_cnt <= 5) {
            goto ENUM_START;
        }
        // printf ("[MSC] Device not attached after retries.\r\n");
        return ERR_USB_DISCON;
    }

    /* Select USB speed */
    USBFSH_SetSelfSpeed (RootHubDev[usb_port].bSpeed);

    /* Get USB device device descriptor */
    // printf ("[MSC] Get DevDesc: ");
    s = USBFSH_GetDeviceDescr (&RootHubDev[usb_port].bEp0MaxPks, DevDesc_Buf);
    if (s == ERR_SUCCESS) {
        //  printf ("OK\r\n");
#if DEF_DEBUG_PRINTF
        for (i = 0; i < 18; i++) {
            // printf ("%02x ", DevDesc_Buf[i]);
        }
        // printf ("\n");
#endif
    } else {
        if (enum_cnt <= 5) {
            goto ENUM_START;
        }
        return DEF_DEV_DESCR_GETFAIL;
    }

    /* Set the USB device address */
    // printf ("[MSC] Set DevAddr: ");
    RootHubDev[usb_port].bAddress = (uint8_t)(DEF_USB_PORT_FS + USB_DEVICE_ADDR);
    s = USBFSH_SetUsbAddress (RootHubDev[usb_port].bEp0MaxPks, RootHubDev[usb_port].bAddress);
    if (s == ERR_SUCCESS) {
        // printf ("OK\n");
    } else {
        if (enum_cnt <= 5) {
            goto ENUM_START;
        }
        return DEF_DEV_ADDR_SETFAIL;
    }
    Delay_Ms (5);

    /* Get the USB device configuration descriptor */
    // printf ("[MSC] Get CfgDesc: ");
    s = USBFSH_GetConfigDescr (RootHubDev[usb_port].bEp0MaxPks, Com_Buffer, DEF_COM_BUF_LEN, &len);
    if (s == ERR_SUCCESS) {
        cfg_val = ((PUSB_CFG_DESCR)Com_Buffer)->bConfigurationValue;
        //  printf ("OK\r\n");
#if DEF_DEBUG_PRINTF
        for (i = 0; i < len; i++) {
            //  printf ("%02x ", Com_Buffer[i]);
        }
        // printf ("\n");
#endif
    } else {
        if (enum_cnt <= 5) {
            goto ENUM_START;
        }
        return DEF_CFG_DESCR_GETFAIL;
    }

    /* Set USB device configuration value */
    // printf ("[MSC] Set Cfg: ");
    s = USBFSH_SetUsbConfig (RootHubDev[usb_port].bEp0MaxPks, cfg_val);
    if (s == ERR_SUCCESS) {
        // printf ("OK\r\n");
        //  Parse configuration descriptor for MSC interface and endpoints
        uint8_t *p = Com_Buffer;
        uint8_t *end = Com_Buffer + len;
        usb_in_ep = 0;
        usb_out_ep = 0;
        in_tog = 0;
        out_tog = 0;
        msc_interface_found = 0;
        msc_in_ep_found = 0;
        msc_out_ep_found = 0;
        uint8_t interface_class = 0, interface_subclass = 0, interface_protocol = 0;
    while (p < end) {
            if (p[1] == 0x04) {  // INTERFACE descriptor
        // bInterfaceNumber = p[2]
                interface_class = p[5];
                interface_subclass = p[6];
                interface_protocol = p[7];
                // printf ("[MSC] Interface: class=0x%02X subclass=0x%02X protocol=0x%02X\r\n",
                //    interface_class, interface_subclass, interface_protocol);
                // Check for MSC interface: class 0x08, subclass 0x06, protocol 0x50
                if (interface_class == 0x08 && interface_subclass == 0x06 && interface_protocol == 0x50) {
                    msc_interface_found = 1;
                    msc_interface_number = p[2];
                    //  printf ("[MSC] Mass Storage Class interface found.\r\n");
                }
            }
            if (p[1] == 0x05 && msc_interface_found) {  // ENDPOINT descriptor type
                uint8_t ep_addr = p[2];
                uint8_t ep_attr = p[3];
                if ((ep_attr & 0x03) == 0x02) {      // BULK
                    if (ep_addr & 0x80) {
                        usb_in_ep = ep_addr & 0x0F;  // IN endpoint
                        in_tog = 0;
                        msc_in_ep_found = 1;
                        // printf ("[MSC] Found BULK IN endpoint: 0x%02X\r\n", ep_addr);
                    } else {
                        usb_out_ep = ep_addr & 0x0F;  // OUT endpoint
                        out_tog = 0;
                        msc_out_ep_found = 1;
                        // printf ("[MSC] Found BULK OUT endpoint: 0x%02X\r\n", ep_addr);
                    }
                }
            }
            if (p[0] == 0)
                break;  // Prevent infinite loop on malformed descriptors
            p += p[0];  // Move to next descriptor
        }
        // printf ("[MSC] usb_in_ep=%u, usb_out_ep=%u\r\n", usb_in_ep, usb_out_ep);

        // Final check: is this a valid MSC device?
        if (!msc_interface_found || !msc_in_ep_found || !msc_out_ep_found) {
            //  printf ("[MSC] ERROR: Device is not a valid USB MSC device or missing endpoints.\r\n");
            return ERR_USB_UNSUPPORT;
        } else {
            //  printf ("[MSC] USB MSC device enumeration complete and endpoints assigned.\r\n");
        }
    } else {
        if (enum_cnt <= 5) {
            goto ENUM_START;
        }
        return ERR_USB_UNSUPPORT;
    }

    return ERR_SUCCESS;
}

/*********************************************************************
 * @fn      USBH_PreDeal
 *
 * @brief   usb host preemption operations,
 *         including detecting device insertion and enumerating device information
 *
 * @return  none
 */
uint8_t USBH_PreDeal (void) {
    uint8_t usb_port = DEF_USB_PORT_FS;
    uint8_t index;
    uint8_t ret;

    // Check current port status
    ret = USBFSH_CheckRootHubPortStatus (RootHubDev[usb_port].bStatus);

    if (ret == ROOT_DEV_CONNECTED) {
        // Only enumerate if not already enumerated
        if (RootHubDev[usb_port].bStatus != ROOT_DEV_SUCCESS) {
            RootHubDev[usb_port].bStatus = ROOT_DEV_CONNECTED;
            RootHubDev[usb_port].DeviceIndex = usb_port * DEF_ONE_USB_SUP_DEV_TOTAL;

            // Enumerate root device
            ret = USBH_EnumRootDevice (usb_port);
            if (ret == ERR_SUCCESS) {
                RootHubDev[usb_port].bStatus = ROOT_DEV_SUCCESS;
                return DEF_SUCCESS;
            } else {
                RootHubDev[usb_port].bStatus = ROOT_DEV_FAILED;
                return DEF_ERR_ENUM;
            }
        } else {

            return DEF_SUCCESS;
        }
    } else if (ret == ROOT_DEV_DISCONNECT) {
        //  Clear parameters
        index = RootHubDev[usb_port].DeviceIndex;
        memset (&RootHubDev[usb_port].bStatus, 0, sizeof (struct _ROOT_HUB_DEVICE));
        memset (&HostCtl[index].InterfaceNum, 0, sizeof (struct __HOST_CTL));
        return DEF_ERR_DETECT;
    }

    // No change
    return DEF_DEFAULT;
}

void ClearUSB() {
    uint8_t usb_port = DEF_USB_PORT_FS;
    uint8_t index;

    index = RootHubDev[usb_port].DeviceIndex;
    memset (&RootHubDev[usb_port].bStatus, 0, sizeof (struct _ROOT_HUB_DEVICE));
    memset (&HostCtl[index].InterfaceNum, 0, sizeof (struct __HOST_CTL));
}

// (Removed test helper: TEST UNIT READY)

// Single attempt: READ CAPACITY(10)
static uint8_t scsi_read_capacity10_once (uint32_t *block_count, uint32_t *block_size) {
    CBW_t cbw;
    CSW_t csw;
    uint8_t cap_buf[8];
    uint8_t res;
    uint16_t plen;

    memset (&cbw, 0, sizeof (cbw));
    cbw.dCBWSignature = 0x43425355;
    cbw.dCBWTag = 0x11223344;
    cbw.dCBWDataTransferLength = 8;
    cbw.bmCBWFlags = 0x80;  // IN
    cbw.bCBWLUN = 0;
    cbw.bCBWCBLength = 10;
    cbw.CBWCB[0] = 0x25;  // READ CAPACITY(10)


    res = usb_send_cbw (&cbw);
    if (res != ERR_SUCCESS)
        return 1;

    Delay_Ms (2);

    plen = 8;
    res = bulk_in_read_retry (cap_buf, &plen, 40);
    if (res != ERR_SUCCESS || plen != 8)
        return 2;

    Delay_Ms (2);

    res = usb_recv_csw (&csw);
    if (res != ERR_SUCCESS || csw.bCSWStatus != 0)
        return 3;
    Delay_Ms (1);
    // Parse capacity
    *block_count = (cap_buf[0] << 24) | (cap_buf[1] << 16) | (cap_buf[2] << 8) | cap_buf[3];
    *block_size = (cap_buf[4] << 24) | (cap_buf[5] << 16) | (cap_buf[6] << 8) | cap_buf[7];
    return 0;
}

// Single attempt: READ CAPACITY(16) fallback
static uint8_t scsi_read_capacity16_once (uint32_t *block_count, uint32_t *block_size) {
    CBW_t cbw; CSW_t csw; uint8_t res; uint16_t plen = 32; uint8_t cap_buf[32];
    memset (&cbw, 0, sizeof (cbw));
    cbw.dCBWSignature = 0x43425355;
    cbw.dCBWTag = 0x11223366;
    cbw.dCBWDataTransferLength = 32;
    cbw.bmCBWFlags = 0x80;  // IN
    cbw.bCBWLUN = 0;
    cbw.bCBWCBLength = 16;
    cbw.CBWCB[0] = 0x9E;    // READ CAPACITY(16)
    cbw.CBWCB[1] = 0x10;    // Service action
    // LBA bytes [2..9] = 0
    cbw.CBWCB[10] = 0x00;   // Alloc length MSB
    cbw.CBWCB[11] = 0x00;
    cbw.CBWCB[12] = 0x00;
    cbw.CBWCB[13] = 0x20;   // 32 bytes
    // [14],[15] control = 0
    res = usb_send_cbw (&cbw);
    if (res != ERR_SUCCESS) return 1;
    Delay_Ms (1);
    res = bulk_in_read_retry (cap_buf, &plen, 40);
    if (res != ERR_SUCCESS || plen < 12) return 2;
    Delay_Ms (1);
    res = usb_recv_csw (&csw);
    if (res != ERR_SUCCESS || csw.bCSWStatus != 0) return 3;
    // Parse: last LBA [0..7], block length [8..11]
    uint32_t lba_hi = (cap_buf[0] << 24) | (cap_buf[1] << 16) | (cap_buf[2] << 8) | cap_buf[3];
    uint32_t lba_lo = (cap_buf[4] << 24) | (cap_buf[5] << 16) | (cap_buf[6] << 8) | cap_buf[7];
    if (lba_hi != 0) {
        *block_count = 0xFFFFFFFF; // saturate if >32-bit
    } else {
        *block_count = lba_lo;
    }
    *block_size = (cap_buf[8] << 24) | (cap_buf[9] << 16) | (cap_buf[10] << 8) | cap_buf[11];
    return 0;
}

// Public: READ CAPACITY with sense+reset retry and CAP16 fallback
uint8_t usb_scsi_read_capacity (uint32_t *block_count, uint32_t *block_size) {
    for (int attempt = 0; attempt < 2; attempt++) {
        uint8_t r = scsi_read_capacity10_once (block_count, block_size);
        if (r == 0) return 0;
        // Attempt recovery on first failure
        uint8_t sense[18] = {0};
        (void)usb_scsi_request_sense (sense, sizeof(sense));
        (void)msc_mass_storage_reset();
        Delay_Ms (5);
    }
    // Fallback to READ CAPACITY(16)
    uint8_t r16 = scsi_read_capacity16_once (block_count, block_size);
    if (r16 == 0) return 0;
    return 3; // status error
}

// Single attempt: READ(10) one block
static uint8_t scsi_read_sector_once (uint32_t lba, uint8_t *buf, uint32_t block_size) {
    CBW_t cbw;
    CSW_t csw;
    uint8_t res;
    uint16_t plen;
    uint32_t bytes_received = 0;
    uint32_t transfer_len = block_size;

    memset (&cbw, 0, sizeof (cbw));
    cbw.dCBWSignature = 0x43425355;
    cbw.dCBWTag = 0xCAFEBABE;
    cbw.dCBWDataTransferLength = transfer_len;
    cbw.bmCBWFlags = 0x80;  // IN
    cbw.bCBWLUN = 0;
    cbw.bCBWCBLength = 10;
    cbw.CBWCB[0] = 0x28;  // READ(10)
    cbw.CBWCB[2] = (lba >> 24) & 0xFF;
    cbw.CBWCB[3] = (lba >> 16) & 0xFF;
    cbw.CBWCB[4] = (lba >> 8) & 0xFF;
    cbw.CBWCB[5] = (lba)&0xFF;
    cbw.CBWCB[7] = (1 >> 8) & 0xFF;  // Transfer length: 1 block
    cbw.CBWCB[8] = (1) & 0xFF;


    int cbw_send_retries = 0;
    do {
        res = usb_send_cbw (&cbw);
        if (res == ERR_SUCCESS)
            break;
        Delay_Ms (1);
        cbw_send_retries++;
    } while (cbw_send_retries < 20);
    if (res != ERR_SUCCESS) {
        return 1;
    }


    while (bytes_received < block_size) {
        plen = block_size - bytes_received;
        if (plen > 64)
            plen = 64;  // USB FS bulk max packet size
        int nak_retries = 0;
        do {
            res = USBFSH_GetEndpData (usb_in_ep, &in_tog, buf + bytes_received, &plen);
            if (res != ERR_SUCCESS) {  // USB_PID_NAK
                Delay_Ms (1);
                nak_retries++;
                if (nak_retries >= 20) {
                    return 2;
                }
            }
        } while (res != ERR_SUCCESS);

        if (res != ERR_SUCCESS || plen == 0) {
            return 2;
        }
        bytes_received += plen;
    }

    int csw_retries = 0;
    do {
        res = usb_recv_csw (&csw);
        if (res == ERR_SUCCESS && csw.bCSWStatus == 0)
            break;
        Delay_Ms (1);
        csw_retries++;
    } while (csw_retries < 40);
    if (res != ERR_SUCCESS || csw.bCSWStatus != 0) {
        return 3;
    }

    return 0;
}

// Public: READ(10) with sense+reset retry
uint8_t usb_scsi_read_sector (uint32_t lba, uint8_t *buf, uint32_t block_size) {
    for (int attempt = 0; attempt < 2; attempt++) {
        uint8_t r = scsi_read_sector_once (lba, buf, block_size);
        if (r == 0) return 0;
        // Recovery on first failure
        uint8_t sense[18] = {0};
        (void)usb_scsi_request_sense (sense, sizeof(sense));
        (void)msc_mass_storage_reset();
        Delay_Ms (5);
    }
    return 3;
}

// (Removed test helper: INQUIRY)

// SCSI REQUEST SENSE
uint8_t usb_scsi_request_sense (uint8_t *buf, uint16_t len) {
    CBW_t cbw; CSW_t csw; uint8_t res; uint16_t plen = len;
    memset (&cbw, 0, sizeof (cbw));
    cbw.dCBWSignature = 0x43425355;
    cbw.dCBWTag = 0x53454E53; // 'SENS'
    cbw.dCBWDataTransferLength = len;
    cbw.bmCBWFlags = 0x80;
    cbw.bCBWLUN = 0;
    cbw.bCBWCBLength = 6;
    cbw.CBWCB[0] = 0x03;
    cbw.CBWCB[4] = (uint8_t)len;
    res = usb_send_cbw (&cbw);
    if (res != ERR_SUCCESS) return 1;
    Delay_Ms (1);
    res = bulk_in_read_retry (buf, &plen, 40);
    if (res != ERR_SUCCESS) return 2;
    Delay_Ms (1);
    res = usb_recv_csw (&csw);
    if (res != ERR_SUCCESS || csw.bCSWStatus != 0) return 3;
    return 0;
}

// (Removed test helper: MODE SENSE(6))

// (Removed test helper: READ FORMAT CAPACITIES)

// (Removed test helper: START/STOP UNIT)

// (Removed test helper: PREVENT/ALLOW MEDIUM REMOVAL)
/* ---------------------------------------------------------------------- */
/* Raw-disk (Nextor) additions: WRITE(10), multi-block transfers, INQUIRY, */
/* TEST UNIT READY and a lazy enumeration helper.                          */
/* ---------------------------------------------------------------------- */

#include "nextor.h"

#define MSC_ERR_STALL     ((uint8_t)(ERR_USB_TRANSFER | USB_PID_STALL))
#define MSC_BULK_PACKET   64U
/* Budget for a flash stick to commit a WRITE(10) (~1 ms per poll). It stays
 * below the Z80 driver's mailbox poll window. */
#define WR10_CSW_MAX_TRIES 1800U

static void cbw_rw10 (CBW_t *cbw, uint32_t tag, uint8_t opcode, uint8_t dir_in,
                      uint32_t lba, uint16_t count, uint32_t block_size) {
    memset (cbw, 0, sizeof (*cbw));
    cbw->dCBWSignature = 0x43425355;
    cbw->dCBWTag = tag;
    cbw->dCBWDataTransferLength = (uint32_t)count * block_size;
    cbw->bmCBWFlags = dir_in ? 0x80 : 0x00;
    cbw->bCBWCBLength = 10;
    cbw->CBWCB[0] = opcode;
    cbw->CBWCB[2] = (lba >> 24) & 0xFF;
    cbw->CBWCB[3] = (lba >> 16) & 0xFF;
    cbw->CBWCB[4] = (lba >> 8) & 0xFF;
    cbw->CBWCB[5] = lba & 0xFF;
    cbw->CBWCB[7] = (count >> 8) & 0xFF;
    cbw->CBWCB[8] = count & 0xFF;
}

// Wait for a CSW, tolerating long NAK periods while the stick commits a write.
static uint8_t csw_wait (CSW_t *csw, uint16_t max_tries, uint16_t *tries_out) {
    uint8_t res = ERR_USB_TRANSFER;
    uint8_t ep0 = RootHubDev[DEF_USB_PORT_FS].bEp0MaxPks;
    for (uint16_t t = 0; t < max_tries; t++) {
        uint16_t plen = sizeof (CSW_t);
        res = USBFSH_GetEndpData (usb_in_ep, &in_tog, (uint8_t *)csw, &plen);
        if (res == ERR_SUCCESS && plen >= sizeof (CSW_t)) {
            if (csw->dCSWSignature != 0x53425355U) return ERR_USB_TRANSFER;
            if (tries_out) *tries_out = t;
            return ERR_SUCCESS;
        }
        if (res == ERR_USB_DISCON) break;
        if (res == MSC_ERR_STALL) {
            (void)USBFSH_ClearEndpStall (ep0, (uint8_t)(0x80 | usb_in_ep));
            in_tog = 0;
            Delay_Ms (2);
            continue;
        }
        Delay_Ms (1);
    }
    if (tries_out) *tries_out = max_tries;
    return res == ERR_SUCCESS ? ERR_USB_TRANSFER : res;
}

static uint8_t bulk_out_stream (const uint8_t *buf, uint32_t total) {
    uint8_t ep0 = RootHubDev[DEF_USB_PORT_FS].bEp0MaxPks;
    uint32_t sent = 0;
    while (sent < total) {
        uint16_t chunk = (total - sent) > MSC_BULK_PACKET ? MSC_BULK_PACKET : (uint16_t)(total - sent);
        int retries = 0;
        uint8_t res;
        do {
            res = USBFSH_SendEndpData (usb_out_ep, &out_tog, (uint8_t *)(buf + sent), chunk);
            if (res == ERR_SUCCESS) break;
            if (res == ERR_USB_DISCON) return res;
            if (res == MSC_ERR_STALL) {
                (void)USBFSH_ClearEndpStall (ep0, usb_out_ep);
                out_tog = 0;
                Delay_Ms (2);
            } else {
                Delay_Ms (1);
            }
        } while (++retries < 20);
        if (res != ERR_SUCCESS) return res;
        sent += chunk;
    }
    return ERR_SUCCESS;
}

static uint8_t bulk_in_stream (uint8_t *buf, uint32_t total) {
    uint32_t got = 0;
    while (got < total) {
        uint16_t plen = (total - got) > MSC_BULK_PACKET ? MSC_BULK_PACKET : (uint16_t)(total - got);
        int retries = 0;
        uint8_t res;
        do {
            res = USBFSH_GetEndpData (usb_in_ep, &in_tog, buf + got, &plen);
            if (res == ERR_SUCCESS) break;
            if (res == ERR_USB_DISCON) return res;
            Delay_Ms (1);
        } while (++retries < 40);
        if (res != ERR_SUCCESS || plen == 0) return ERR_USB_TRANSFER;
        got += plen;
    }
    return ERR_SUCCESS;
}

/* Returns 0 ok, 1 CBW failed, 2 data stage failed, 3 no CSW, 4 CHECK CONDITION. */
static uint8_t scsi_write_once (uint32_t lba, const uint8_t *buf, uint32_t block_size,
                                uint16_t count, uint16_t *csw_tries) {
    CBW_t cbw;
    CSW_t csw;
    cbw_rw10 (&cbw, 0xCAFEBA5E, 0x2A, 0, lba, count, block_size);
    if (usb_send_cbw (&cbw) != ERR_SUCCESS) return 1;
    if (bulk_out_stream (buf, (uint32_t)count * block_size) != ERR_SUCCESS) return 2;
    if (csw_wait (&csw, WR10_CSW_MAX_TRIES, csw_tries) != ERR_SUCCESS) return 3;
    return csw.bCSWStatus != 0 ? 4 : 0;
}

// One attempt; the caller falls back to per-sector commands on failure.
uint8_t usb_scsi_write_blocks (uint32_t lba, const uint8_t *buf, uint32_t block_size, uint16_t count) {
    uint16_t tries = 0;
    uint8_t r = scsi_write_once (lba, buf, block_size, count, &tries);
    if (r == 0) return 0;
    if (r == 4) {
        uint8_t sense[18] = {0};
        (void)usb_scsi_request_sense (sense, sizeof (sense));
    } else {
        (void)msc_mass_storage_reset();
    }
    return r;
}

uint8_t usb_scsi_read_blocks (uint32_t lba, uint8_t *buf, uint32_t block_size, uint16_t count) {
    CBW_t cbw;
    CSW_t csw;
    uint8_t res = ERR_USB_TRANSFER;
    cbw_rw10 (&cbw, 0xCAFEBABE, 0x28, 1, lba, count, block_size);
    for (int i = 0; i < 20; i++) {
        res = usb_send_cbw (&cbw);
        if (res == ERR_SUCCESS) break;
        Delay_Ms (1);
    }
    if (res != ERR_SUCCESS) return 1;
    if (bulk_in_stream (buf, (uint32_t)count * block_size) != ERR_SUCCESS) {
        (void)msc_mass_storage_reset();
        return 2;
    }
    if (csw_wait (&csw, 2000, (uint16_t *)0) != ERR_SUCCESS || csw.bCSWStatus != 0) {
        (void)msc_mass_storage_reset();
        return 3;
    }
    return 0;
}

uint8_t usb_scsi_inquiry (uint8_t *buf, uint16_t len) {
    CBW_t cbw; CSW_t csw; uint16_t plen = len;
    memset (&cbw, 0, sizeof (cbw));
    cbw.dCBWSignature = 0x43425355;
    cbw.dCBWTag = 0xCAFEBAB0;
    cbw.dCBWDataTransferLength = len;
    cbw.bmCBWFlags = 0x80;
    cbw.bCBWCBLength = 6;
    cbw.CBWCB[0] = 0x12;
    cbw.CBWCB[4] = (uint8_t)len;
    if (usb_send_cbw (&cbw) != ERR_SUCCESS) return 1;
    Delay_Ms (1);
    if (bulk_in_read_retry (buf, &plen, 40) != ERR_SUCCESS) return 2;
    Delay_Ms (1);
    if (usb_recv_csw (&csw) != ERR_SUCCESS || csw.bCSWStatus != 0) return 3;
    return 0;
}

/* Poll the root hub and enumerate if needed. Returns DEF_SUCCESS when a
 * device is enumerated; yields at once if the Z80 has a mailbox request. */
uint8_t USB_TryEnsureEnumerated (void) {
    uint8_t r = DEF_ERR_ENUM;
    for (int tries = 0; tries < 5; tries++) {
        if (Nextor_RequestPending() != 0U) return DEF_ERR_ENUM;
        r = USBH_PreDeal();
        if (r == DEF_ERR_DETECT) {
            Nextor_CacheInvalidate();
            return DEF_ERR_DETECT;
        }
        if (r == DEF_SUCCESS) break;
        // PreDeal only reports a port change once; an already-enumerated stick returns DEF_DEFAULT.
        if (RootHubDev[DEF_USB_PORT_FS].bStatus == ROOT_DEV_SUCCESS) {
            r = DEF_SUCCESS;
            break;
        }
        if (USBH_EnumRootDevice (DEF_USB_PORT_FS) == ERR_SUCCESS) {
            RootHubDev[DEF_USB_PORT_FS].bStatus = ROOT_DEV_SUCCESS;
            r = DEF_SUCCESS;
            break;
        }
        r = DEF_ERR_ENUM;
        Delay_Ms (100);
    }
    return r == DEF_SUCCESS ? DEF_SUCCESS : DEF_ERR_ENUM;
}
