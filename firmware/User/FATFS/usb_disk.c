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
    return USBFSH_SendEndpData (usb_out_ep, &out_tog, (uint8_t *)cbw, plen);
}

// Helper: Receive CSW
static uint8_t usb_recv_csw (CSW_t *csw) {
    uint16_t plen = sizeof (CSW_t);
    return USBFSH_GetEndpData (usb_in_ep, &in_tog, (uint8_t *)csw, &plen);
}

void USB_Initialization (void) {


    /* USB Host Initialization */
    // printf ("USB Host & UDisk Lib Initialization. \r\n");
    /* Initialize USBFS host */
    // printf ("USBFS Host Init\r\n");
    USBFS_RCC_Init();
    // printf ("Init READY\r\n");
    USBFS_Host_Init (ENABLE);
    // printf ("Host Init enable READY\r\n");
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

    // printf ("[MSC] Enum Start\r\n");

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
            // printf ("[MSC] Device not attached, retrying...\r\n");
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
        // printf ("Err(%02x)\n", s);
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
        // printf ("Err(%02x)\n", s);
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
        // printf ("Err(%02x)\n", s);
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
                interface_class = p[5];
                interface_subclass = p[6];
                interface_protocol = p[7];
                // printf ("[MSC] Interface: class=0x%02X subclass=0x%02X protocol=0x%02X\r\n",
                //    interface_class, interface_subclass, interface_protocol);
                // Check for MSC interface: class 0x08, subclass 0x06, protocol 0x50
                if (interface_class == 0x08 && interface_subclass == 0x06 && interface_protocol == 0x50) {
                    msc_interface_found = 1;
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
        // printf ("Err(%02x)\n", s);
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
            // printf ("USB Dev In.\n");
            RootHubDev[usb_port].bStatus = ROOT_DEV_CONNECTED;
            RootHubDev[usb_port].DeviceIndex = usb_port * DEF_ONE_USB_SUP_DEV_TOTAL;

            // Enumerate root device
            ret = USBH_EnumRootDevice (usb_port);
            if (ret == ERR_SUCCESS) {
                // printf ("USB Port %02x Device Enumeration Succeed\r\n", usb_port);
                RootHubDev[usb_port].bStatus = ROOT_DEV_SUCCESS;
                return DEF_SUCCESS;
            } else {
                // printf ("USB Port %02x Device Enumeration ERR %02x.\r\n", usb_port, ret);
                RootHubDev[usb_port].bStatus = ROOT_DEV_FAILED;
                return DEF_ERR_ENUM;
            }
        } else {
            // Already enumerated and connected
            return DEF_SUCCESS;
        }
    } else if (ret == ROOT_DEV_DISCONNECT) {
        // printf ("USB Port %02x Device Out.\r\n", usb_port);
        //  Clear parameters
        index = RootHubDev[usb_port].DeviceIndex;
        memset (&RootHubDev[usb_port].bStatus, 0, sizeof (struct _ROOT_HUB_DEVICE));
        memset (&HostCtl[index].InterfaceNum, 0, sizeof (struct __HOST_CTL));
        return DEF_ERR_DETECT;
    }

    // No change
    return DEF_DEFAULT;
}

// SCSI READ CAPACITY (returns block count and block size)
uint8_t usb_scsi_read_capacity (uint32_t *block_count, uint32_t *block_size) {
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

    Delay_Ms (1);

    plen = 8;
    res = USBFSH_GetEndpData (usb_in_ep, &in_tog, cap_buf, &plen);
    if (res != ERR_SUCCESS || plen != 8)
        return 2;

    Delay_Ms (1);

    res = usb_recv_csw (&csw);
    if (res != ERR_SUCCESS || csw.bCSWStatus != 0)
        return 3;
    Delay_Ms (1);
    // Parse capacity
    *block_count = (cap_buf[0] << 24) | (cap_buf[1] << 16) | (cap_buf[2] << 8) | cap_buf[3];
    *block_size = (cap_buf[4] << 24) | (cap_buf[5] << 16) | (cap_buf[6] << 8) | cap_buf[7];
    return 0;
}

uint8_t usb_scsi_read_sector (uint32_t lba, uint8_t *buf, uint32_t block_size) {
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
    } while (csw_retries < 20);
    if (res != ERR_SUCCESS || csw.bCSWStatus != 0) {
        return 3;
    }

    return 0;
}