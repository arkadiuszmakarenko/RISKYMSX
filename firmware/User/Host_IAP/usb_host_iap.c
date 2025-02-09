#include "usb_host_iap.h"
#include "cart.h"
#include <stdio.h>
#include "utils.h"
#include "MSXTerminal.h"

/*Cart buffer*/
extern CircularBuffer scb;
uint8_t LongNameBuf[LONG_NAME_BUF_LEN];

/* Variable */
__attribute__ ((aligned (4))) uint8_t IAPLoadBuffer[DEF_MAX_IAP_BUFFER_LEN];
__attribute__ ((aligned (4))) uint8_t Com_Buffer[DEF_COM_BUF_LEN];  // even address , used for host enumcation and udisk operation
__attribute__ ((aligned (4))) uint8_t DevDesc_Buf[18];              // Device Descriptor Buffer
volatile uint32_t IAP_Load_Addr_Offset;
volatile uint32_t IAP_WriteIn_Length;
volatile uint32_t IAP_WriteIn_Count;
volatile uint32_t File_Length;

volatile uint32_t start_address = 0x08008000;
volatile uint32_t end_address = 0x08048000;

struct _ROOT_HUB_DEVICE RootHubDev[DEF_TOTAL_ROOT_HUB];
struct __HOST_CTL HostCtl[DEF_TOTAL_ROOT_HUB * DEF_ONE_USB_SUP_DEV_TOTAL];

/* Flash Operation Key */
volatile uint32_t Flash_Operation_Key0;
volatile uint32_t Flash_Operation_Key1;

/*********************************************************************
 * @fn      FLASH_ReadByte
 *
 * @brief   Read Flash In byte(8 bits)
 *
 * @return  8bits data readout
 */
uint8_t FLASH_ReadByte (uint32_t address) {
    return *(__IO uint8_t *)address;
}

/*********************************************************************
 * @fn      FLASH_ReadHalfWord
 *
 * @brief   Read Flash In HalfWord(16 bits)
 *
 * @return  16bits data readout
 */
uint16_t FLASH_ReadHalfWord (uint32_t address) {
    return *(__IO uint16_t *)address;
}

/*********************************************************************
 * @fn      FLASH_ReadWord
 *
 * @brief   Read Flash In Word(32 bits).
 *
 * @return  32bits data readout
 */
uint32_t FLASH_ReadWord (uint32_t address) {
    return *(__IO uint32_t *)address;
}

/*********************************************************************
 * @fn      FLASH_ReadWordAdd
 *
 * @brief   Read Flash In Word(32 bits),Specify length & address
 *
 * @return  none
 */
void FLASH_ReadWordAdd (uint32_t address, u32 *buff, uint16_t length) {
    uint16_t i;

    for (i = 0; i < length; i++) {
        buff[i] = *(__IO uint32_t *)address;
        address += 4;
    }
}

/*********************************************************************
 * @fn      IAP_Flash_Read
 *
 * @brief   Read Flash In bytes(8 bits),Specify length & address,
 *          With address protection and program runaway protection.
 *
 * @return  0: Operation Success
 *          See notes for other errors
 */
uint8_t IAP_Flash_Read (uint32_t address, uint8_t *buff, uint32_t length) {
    uint32_t i;
    uint32_t read_addr;
    uint32_t read_len;
    read_addr = address;
    read_len = length;


    /* Verify Address, No flash operation if the address is out of range */
    if (((read_addr >= start_address) && (read_addr <= end_address))) {
        /* Available Data */
        for (i = 0; i < read_len; i++) {
            buff[i] = *(__IO uint8_t *)read_addr;  // Read one word of data at the specified address
            read_addr++;
        }

        if (i != read_len) {
            /* Incorrect read length */
            return 0xFD;
        }
    } else {
        /* address Out Of Range */
        return 0xFE;
    }

    return 0;
}

/*********************************************************************
 * @fn      mFLASH_ProgramPage_Fast
 *
 * @brief   Fast programming, input variable addresses and data buffers,
 *          no longer than one page at a time.
 *          The content of the functions can be written according to the
 *          chip flash fast programming process by yourself
 *
 * @return  0: Operation Success
 *          See notes for other errors
 */
uint8_t mFLASH_ProgramPage_Fast (uint32_t addr, uint32_t *buffer) {
    FLASH_ProgramPage_Fast (addr, buffer);
    return 0;
}

/*********************************************************************
 * @fn      IAP_Flash_Write
 *
 * @brief   Write Flash In bytes(8 bits),Specify length & address,
 *          Based On Fast Flash Operation,
 *          With address protection and program runaway protection.
 *
 * @return  0: Operation Success
 *          See notes for other errors
 */
uint8_t IAP_Flash_Write (uint32_t address, uint8_t *buff, uint32_t length) {
    uint32_t i, j;
    uint32_t write_addr;
    uint32_t write_len;
    uint16_t write_len_once;
    uint32_t write_cnts;
    volatile uint32_t page_cnts;
    volatile uint32_t page_addr;
    uint8_t temp_buf[DEF_FLASH_PAGE_SIZE];

    /* Set initial value */
    write_addr = address;
    page_addr = address;
    write_len = length;
    if ((write_len % DEF_FLASH_PAGE_SIZE) == 0) {
        page_cnts = write_len / DEF_FLASH_PAGE_SIZE;
    } else {
        page_cnts = (write_len / DEF_FLASH_PAGE_SIZE) + 1;
    }
    write_cnts = 0;

    /* Verify Keys, No flash operation if keys are not correct */
    if ((Flash_Operation_Key0 != DEF_FLASH_OPERATION_KEY_CODE_0) || (Flash_Operation_Key1 != DEF_FLASH_OPERATION_KEY_CODE_1)) {
        /* ERR: Risk of code running away */
        return 0xFF;
    }

    /* Verify Address, No flash operation if the address is out of range */
    if (((write_addr >= start_address) && (write_addr <= end_address))) {
        for (i = 0; i < page_cnts; i++) {
            /* Verify Keys, No flash operation if keys are not correct */
            if (Flash_Operation_Key0 != DEF_FLASH_OPERATION_KEY_CODE_0) {
                /* ERR: Risk of code running away */
                return 0xFF;
            }
            /* Determine if the length of the written packet is less than the page size */
            /* If it is less than, the original content of the memory needs to be read out first and modified before the operation can be performed */
            write_len_once = DEF_FLASH_PAGE_SIZE;
            FLASH_Unlock_Fast();
            FLASH_ErasePage_Fast (page_addr);
            if (write_len < DEF_FLASH_PAGE_SIZE) {
                FLASH_Unlock_Fast();
                IAP_Flash_Read (page_addr, temp_buf, DEF_FLASH_PAGE_SIZE);

                for (j = 0; j < write_len; j++) {
                    temp_buf[j] = buff[DEF_FLASH_PAGE_SIZE * i + j];
                }
                write_len_once = write_len;
                mFLASH_ProgramPage_Fast (page_addr, (u32 *)&temp_buf[0]);
            } else {
                mFLASH_ProgramPage_Fast (page_addr, (u32 *)&buff[DEF_FLASH_PAGE_SIZE * i]);
            }
            page_addr += DEF_FLASH_PAGE_SIZE;
            write_len -= write_len_once;
            write_cnts++;
        }
        if (i != write_cnts) {
            /* Incorrect read length */
            return 0xFD;
        }
        FLASH_Lock_Fast();
    } else {
        /* address Out Of Range */
        return 0xFE;
    }

    return 0;
}

/*********************************************************************
 * @fn      IAP_Flash_Erase
 *
 * @brief   Erase Flash In page(256 bytes),Specify length & address,
 *          Based On Fast Flash Operation,
 *          With address protection and program runaway protection.
 *
 * @return  0: Operation Success
 *          See notes for other errors
 */
uint8_t IAP_Flash_Erase (uint32_t address, uint32_t length) {
    uint32_t i;
    uint32_t erase_addr;
    uint32_t erase_len;
    volatile uint32_t page_cnts;
    volatile uint32_t page_addr;

    /* Set initial value */
    erase_addr = address;
    page_addr = address;
    erase_len = length;
    if ((erase_len % DEF_FLASH_PAGE_SIZE) == 0) {
        page_cnts = erase_len / DEF_FLASH_PAGE_SIZE;
    } else {
        page_cnts = (erase_len / DEF_FLASH_PAGE_SIZE) + 1;
    }

    /* Verify Keys, No flash operation if keys are not correct */
    if ((Flash_Operation_Key0 != DEF_FLASH_OPERATION_KEY_CODE_0) || (Flash_Operation_Key1 != DEF_FLASH_OPERATION_KEY_CODE_1)) {
        /* ERR: Risk of code running away */
        return 0xFF;
    }
    /* Verify Address, No flash operation if the address is out of range */
    if (((erase_addr >= start_address) && (erase_addr <= end_address))) {
        for (i = 0; i < page_cnts; i++) {
            /* Verify Keys, No flash operation if keys are not correct */
            if (Flash_Operation_Key0 != DEF_FLASH_OPERATION_KEY_CODE_0) {
                /* ERR: Risk of code running away */
                return 0xFF;
            }
            FLASH_Unlock_Fast();
            FLASH_ErasePage_Fast (page_addr);
            page_addr += DEF_FLASH_PAGE_SIZE;
        }
    }

    return 0;
}

/*********************************************************************
 * @fn      IAP_Flash_Verify
 *
 * @brief   verify Flash In bytes(8 bits),Specify length & address,
 *          With address protection and program runaway protection.
 *
 * @return  0: Operation Success
 *          See notes for other errors
 */
uint32_t IAP_Flash_Verify (uint32_t address, uint8_t *buff, uint32_t length) {
    uint32_t i;
    uint32_t read_addr;
    uint32_t read_len;

    /* Set initial value */
    read_addr = address;
    read_len = length;

    /* Verify Keys, No flash operation if keys are not correct */
    if ((Flash_Operation_Key0 != DEF_FLASH_OPERATION_KEY_CODE_0) || (Flash_Operation_Key1 != DEF_FLASH_OPERATION_KEY_CODE_1)) {
        /* ERR: Risk of code running away */
        return 0xFFFFFFFF;
    }

    /* Verify Address, No flash operation if the address is out of range */
    if (((read_addr >= start_address) && (read_addr <= end_address))) {
        for (i = 0; i < read_len; i++) {
            if (FLASH_ReadByte (read_addr) != buff[i]) {
                /* To prevent 0-length errors, the returned position +1 */
                return i + 1;
            }
            read_addr++;
        }
    }

    return 0;
}

/*********************************************************************
 * @fn      IAP_Flash_Program
 *
 * @brief   IAP programming code, including write and verify,
 *          Specify length & address, Based On Fast Flash Operation,
 *          With address protection and program runaway protection.
 *
 * @return  ret : The meaning of 'ret' can be found in the notes of the
 *          corresponding function.
 */
uint32_t IAP_Flash_Program (uint32_t address, uint8_t *buff, uint32_t length) {
    uint32_t ret;
    /* IAP Write */
    Flash_Operation_Key1 = DEF_FLASH_OPERATION_KEY_CODE_1;
    ret = IAP_Flash_Write (address, buff, length);
    Flash_Operation_Key1 = 0;
    if (ret != 0) {
        return ret;
    }
    /* IAP Verify */
    Flash_Operation_Key1 = DEF_FLASH_OPERATION_KEY_CODE_1;
    ret = IAP_Flash_Verify (address, buff, length);
    Flash_Operation_Key1 = 0;
    if (ret != 0) {
        return ret;
    }

    return ret;
}

/*********************************************************************
 * @fn      mStopIfError
 *
 * @brief   Checking the operation status, displaying the error code and stopping if there is an error
 *          input : iError - Error code input
 *
 * @return  none
 */
void mStopIfError (uint8_t iError) {
    if (iError == ERR_SUCCESS) {
        /* operation success, return */
        return;
    }
    appendString (&scb, "ERROR:");
    char error[5];
    intToString (iError, error);
    appendString (&scb, error);
    /* Display the errors */
    // DUG_PRINTF( "Error:%02x\r\n", iError );
    /* After encountering an error, you should analyze the error code and CHRV3DiskStatus status, for example,
     * call CHRV3DiskReady to check whether the current USB disk is connected or not,
     * if the disk is disconnected then wait for the disk to be plugged in again and operate again,
     * Suggested steps to follow after an error:
     *     1, call CHRV3DiskReady once, if successful, then continue the operation, such as Open, Read/Write, etc.
     *     2?If CHRV3DiskReady is not successful, then the operation will be forced to start from the beginning.
     */
    while (1) { }
}

/*********************************************************************
 * @fn      IAP_Initialization
 *
 * @brief   IAP process Initialization, include usb-host initialization
 *          usb libs initialization, iap-related values Initialization
 *          IAP verify-code inspection
 *
 * @return  none
 */
void IAP_Initialization (void) {
    /* IAP verify-code inspection */
    Flash_Operation_Key0 = DEF_FLASH_OPERATION_KEY_CODE_0;

    USBFS_RCC_Init();
    USBFS_Host_Init (ENABLE);
    memset (&RootHubDev[DEF_USB_PORT_FS].bStatus, 0, sizeof (struct _ROOT_HUB_DEVICE));
    memset (&HostCtl[DEF_USB_PORT_FS].InterfaceNum, 0, sizeof (struct __HOST_CTL));


    /* USB Libs Initialization */
    CHRV3LibInit();

    /* IAP-related variable initialization  */
    IAP_Load_Addr_Offset = 0;
    IAP_WriteIn_Length = 0;
    IAP_WriteIn_Count = 0;
}

/*********************************************************************
 * @fn      USBH_CheckRootHubPortStatus
 *
 * @brief   Check status of USB port.
 *
 * @para    index: USB host port
 *
 * @return  The current status of the port.
 */
uint8_t USBH_CheckRootHubPortStatus (uint8_t usb_port) {
    uint8_t s = ERR_USB_UNSUPPORT;

    if (usb_port == DEF_USB_PORT_FS) {
        s = USBFSH_CheckRootHubPortStatus (RootHubDev[usb_port].bStatus);
    }

    return s;
}

/*********************************************************************
 * @fn      USBH_ResetRootHubPort
 *
 * @brief   Reset USB port.
 *
 * @para    index: USB host port
 *          mod: Reset host port operating mode.
 *               0 -> reset and wait end
 *               1 -> begin reset
 *               2 -> end reset
 *
 * @return  none
 */
void USBH_ResetRootHubPort (uint8_t usb_port, uint8_t mode) {
    if (usb_port == DEF_USB_PORT_FS) {
        USBFSH_ResetRootHubPort (mode);
    }
}

/*********************************************************************
 * @fn      USBH_EnableRootHubPort
 *
 * @brief   Enable USB host port.
 *
 * @para    index: USB host port
 *
 * @return  none
 */
uint8_t USBH_EnableRootHubPort (uint8_t usb_port) {
    uint8_t s = ERR_USB_UNSUPPORT;

    if (usb_port == DEF_USB_PORT_FS) {
        s = USBFSH_EnableRootHubPort (&RootHubDev[usb_port].bSpeed);
    }

    return s;
}

/*********************************************************************
 * @fn      USBH_SetSelfSpeed
 *
 * @brief   Set USB speed.
 *
 * @para    index: USB host port
 *
 * @return  none
 */
void USBH_SetSelfSpeed (uint8_t usb_port) {
    if (usb_port == DEF_USB_PORT_FS) {
        USBFSH_SetSelfSpeed (RootHubDev[usb_port].bSpeed);
    }
}

/*********************************************************************
 * @fn      USBH_GetDeviceDescr
 *
 * @brief   Get the device descriptor of the USB device.
 *
 * @para    index: USB host port
 *
 * @return  none
 */
uint8_t USBH_GetDeviceDescr (uint8_t usb_port) {
    uint8_t s = ERR_USB_UNSUPPORT;

    if (usb_port == DEF_USB_PORT_FS) {
        s = USBFSH_GetDeviceDescr (&RootHubDev[usb_port].bEp0MaxPks, DevDesc_Buf);
    }

    return s;
}

/*********************************************************************
 * @fn      USBH_SetUsbAddress
 *
 * @brief   Set USB device address.
 *
 * @para    index: USB host port
 *
 * @return  none
 */
uint8_t USBH_SetUsbAddress (uint8_t usb_port) {
    uint8_t s = ERR_USB_UNSUPPORT;

    if (usb_port == DEF_USB_PORT_FS) {
        RootHubDev[usb_port].bAddress = (uint8_t)(DEF_USB_PORT_FS + USB_DEVICE_ADDR);
        s = USBFSH_SetUsbAddress (RootHubDev[usb_port].bEp0MaxPks, RootHubDev[usb_port].bAddress);
    }

    return s;
}

/*********************************************************************
 * @fn      USBH_GetConfigDescr
 *
 * @brief   Get the configuration descriptor of the USB device.
 *
 * @para    index: USB host port
 *
 * @return  none
 */
uint8_t USBH_GetConfigDescr (uint8_t usb_port, uint16_t *pcfg_len) {
    uint8_t s = ERR_USB_UNSUPPORT;

    if (usb_port == DEF_USB_PORT_FS) {
        s = USBFSH_GetConfigDescr (RootHubDev[usb_port].bEp0MaxPks, Com_Buffer, DEF_COM_BUF_LEN, pcfg_len);
    }
    return s;
}

/*********************************************************************
 * @fn      USBFSH_SetUsbConfig
 *
 * @brief   Set USB configuration.
 *
 * @para    index: USB host port
 *
 * @return  none
 */
uint8_t USBH_SetUsbConfig (uint8_t usb_port, uint8_t cfg_val) {
    uint8_t s = ERR_USB_UNSUPPORT;

    if (usb_port == DEF_USB_PORT_FS) {
        s = USBFSH_SetUsbConfig (RootHubDev[usb_port].bEp0MaxPks, cfg_val);
    }

    return s;
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
    enum_cnt = 0;
ENUM_START:
    /* Delay and wait for the device to stabilize */
    Delay_Ms (100);
    enum_cnt++;
    Delay_Ms (8 << enum_cnt);

    /* Reset the USB device and wait for the USB device to reconnect */
    USBH_ResetRootHubPort (usb_port, 0);
    for (i = 0, s = 0; i < DEF_RE_ATTACH_TIMEOUT; i++) {
        if (USBH_EnableRootHubPort (usb_port) == ERR_SUCCESS) {
            i = 0;
            s++;
            if (s > 6) {
                break;
            }
        }
        Delay_Ms (1);
    }
    if (i) {
        /* Determine whether the maximum number of retries has been reached, and retry if not reached */
        if (enum_cnt <= 5) {
            goto ENUM_START;
        }
        return ERR_USB_DISCON;
    }

    /* Select USB speed */
    USBH_SetSelfSpeed (usb_port);

    /* Get USB device device descriptor */
    s = USBH_GetDeviceDescr (usb_port);
    if (s == ERR_SUCCESS) {
    } else {
        /* Determine whether the maximum number of retries has been reached, and retry if not reached */
        if (enum_cnt <= 5) {
            goto ENUM_START;
        }
        return DEF_DEV_DESCR_GETFAIL;
    }

    /* Set the USB device address */
    s = USBH_SetUsbAddress (usb_port);
    if (s == ERR_SUCCESS) {
    } else {
        /* Determine whether the maximum number of retries has been reached, and retry if not reached */
        if (enum_cnt <= 5) {
            goto ENUM_START;
        }
        return DEF_DEV_ADDR_SETFAIL;
    }
    Delay_Ms (5);

    /* Get the USB device configuration descriptor */
    s = USBH_GetConfigDescr (usb_port, &len);
    if (s == ERR_SUCCESS) {
        cfg_val = ((PUSB_CFG_DESCR)Com_Buffer)->bConfigurationValue;
    } else {
        /* Determine whether the maximum number of retries has been reached, and retry if not reached */
        /// DUG_PRINTF( "Err(%02x)\n", s );
        if (enum_cnt <= 5) {
            goto ENUM_START;
        }
        return DEF_CFG_DESCR_GETFAIL;
    }

    /* Set USB device configuration value */
    ///  DUG_PRINTF("Set Cfg: ");
    s = USBH_SetUsbConfig (usb_port, cfg_val);
    if (s == ERR_SUCCESS) {
        //  DUG_PRINTF( "OK\n" );
    } else {
        /* Determine whether the maximum number of retries has been reached, and retry if not reached */
        // DUG_PRINTF( "Err(%02x)\n", s );
        if (enum_cnt <= 5) {
            goto ENUM_START;
        }
        return ERR_USB_UNSUPPORT;
    }

    return ERR_SUCCESS;
}

/*********************************************************************
 * @fn      IAP_USBH_PreDeal
 *
 * @brief   usb host preemption operations,
 *         including detecting device insertion and enumerating device information
 *
 * @return  none
 */
uint8_t IAP_USBH_PreDeal (void) {
    uint8_t usb_port;
    uint8_t index;
    uint8_t ret;
    usb_port = DEF_USB_PORT_FS;

    ret = USBH_CheckRootHubPortStatus (usb_port);
    if (ret == ROOT_DEV_CONNECTED) {
        USBH_CheckRootHubPortStatus (usb_port);
        RootHubDev[usb_port].bStatus = ROOT_DEV_CONNECTED;  // Set connection status_
        RootHubDev[usb_port].DeviceIndex = usb_port * DEF_ONE_USB_SUP_DEV_TOTAL;

        /* Enumerate root device */
        ret = USBH_EnumRootDevice (usb_port);
        if (ret == ERR_SUCCESS) {
            RootHubDev[usb_port].bStatus = ROOT_DEV_SUCCESS;
            return DEF_IAP_SUCCESS;
        } else {
            RootHubDev[usb_port].bStatus = ROOT_DEV_FAILED;
            return DEF_IAP_ERR_ENUM;
        }
    } else if (ret == ROOT_DEV_DISCONNECT) {
        /* Clear parameters */
        index = RootHubDev[usb_port].DeviceIndex;
        memset (&RootHubDev[usb_port].bStatus, 0, sizeof (struct _ROOT_HUB_DEVICE));
        memset (&HostCtl[index].InterfaceNum, 0, sizeof (struct __HOST_CTL));
        return DEF_IAP_ERR_DETECT;
    }
    return DEF_IAP_DEFAULT;
}

void MapperCode_Write (CartType type, uint32_t cartSize, char *Filename) {
    uint32_t cfg_address = 0x08007F00;
    uint16_t volatile cartSizekB = ((cartSize + 512) / 1024);
    uint32_t cfg[64];

    cfg[0] = type;
    cfg[1] = cartSizekB;

    strcpy ((char *)&cfg[2], Filename);

    // Copy data to flash.
    FLASH_Unlock_Fast();
    FLASH_ErasePage_Fast (cfg_address);
    FLASH_ProgramPage_Fast (cfg_address, cfg);
    FLASH_Lock_Fast();
}

void MapperCode_Update (CartType type) {
    uint32_t cfg_address = 0x08007F00;
    uint32_t cfg[64];


    // Create a pointer to the specified memory address
    uint32_t *src_ptr = (uint32_t *)cfg_address;

    // Copy 64 uint32_t values from the memory address to the cfg array
    for (int i = 0; i < 64; i++) {
        cfg[i] = src_ptr[i];
    }
    // Update mapper
    cfg[0] = type;

    FLASH_Unlock_Fast();
    FLASH_ErasePage_Fast (cfg_address);
    FLASH_ProgramPage_Fast (cfg_address, cfg);
    FLASH_Lock_Fast();
}

int MountDrive() {
    int i, ret;
    static int op_flag = 0;
    ret = IAP_USBH_PreDeal();
    if (ret == DEF_IAP_SUCCESS) {
        /* Wait for uDisk Ready */
        CHRV3DiskStatus = DISK_USB_ADDR;
        for (i = 0; i != 10; i++) {
            ret = CHRV3DiskReady();
            if (ret == ERR_SUCCESS) {
                /* Disk Ready */
                op_flag = 1;
                break;
            } else {
            }
            Delay_Ms (50);
        }
    }
    return op_flag;
}

int isFile (uint8_t Filename[64]) {
    char *str = NULL;

    str = strstr ((char *)Filename, "/..");
    if (str != NULL) {
        return 0;
    }

    str = strstr ((char *)Filename, "/.");
    if (str != NULL) {
        return 0;
    }

    str = strchr ((char *)Filename, '.');
    if (str != NULL) {
        return 1;
    }

    return 0;
}

int printFilename (uint8_t FileArray[64]) {
    char *str = NULL;
    uint8_t FileName[64];
    uint8_t FileNameSize[5];
    uint8_t isFolder = 0;

    str = strstr ((char *)FileArray, "/..");
    if (str != NULL) {
        appendString (&scb, " ..");
        return 0;
    }

    str = strstr ((char *)FileArray, "/.");
    if (str != NULL) {
        appendString (&scb, " .");
        return 0;
    }


    for (int j = 0; j != LONG_NAME_BUF_LEN; j++) {
        LongNameBuf[j] = 0x00;
    }

    char *ptr = strchr ((char *)FileArray, '.');
    if (ptr == NULL) {
        isFolder = 1;
    }

    // clear buffers
    for (int x = 0; x < 50; x++) {
        FileName[x] = 0x20;
    }
    for (int x = 0; x < 5; x++) {
        FileNameSize[x] = 0x20;
    }

    if ((CHRV3DiskStatus >= DISK_MOUNTED)) {
        CHRV3DirtyBuffer();
        strcpy ((char *)mCmdParam.Open.mPathName, (char *)FileArray);
        /* open file */
        CHRV3FileOpen();

        uint32_t FileSize = CHRV3vFileSize;

        CHRV3GetLongName();

        int PositionIndex = 0;
        for (int j = 0; j != LONG_NAME_BUF_LEN; j = j + 2) {
            if ((LongNameBuf[j] == 0x00) && (LongNameBuf[j + 1] == 0x00)) {
                break;
            }
            if (LongNameBuf[j] != 0x00)
                FileName[(PositionIndex++)] = LongNameBuf[j];
        }

        if (PositionIndex == 0) {
            int length = strlen ((char *)mCmdParam.Open.mPathName);
            strcpy ((char *)FileName, (char *)mCmdParam.Open.mPathName + 1);
            FileName[length - 1] = 0x20;
        }

        uint32_t sizeAdjusted = 0;
        if (FileSize >= 1073741824) {  // 1 GB = 1073741824 bytes
            sizeAdjusted = FileSize / 1073741824;
            intToString (sizeAdjusted, (char *)FileNameSize);
            FileNameSize[3] = 0x47;        // G
            FileNameSize[4] = 0x42;        // B

        } else if (FileSize >= 1048576) {  // 1 MB = 1048576 bytes
            sizeAdjusted = FileSize / 1048576;
            intToString (sizeAdjusted, (char *)FileNameSize);
            FileNameSize[3] = 0x4D;     // M
            FileNameSize[4] = 0x42;     // B
        } else if (FileSize >= 1024) {  // 1 KB = 1024 bytes
            sizeAdjusted = FileSize / 1024;
            intToString (sizeAdjusted, (char *)FileNameSize);
            FileNameSize[3] = 0x4B;  // K
            FileNameSize[4] = 0x42;  // B
        } else {
            intToString (FileSize, (char *)FileNameSize);
            FileNameSize[4] = 0x42;  // B
        }

        append (&scb, 0x20);
        for (int x = 0; x < 25; x++) {
            append (&scb, FileName[x]);
        }
        if (isFolder) {
            strncpy ((char *)FileNameSize, "<DIR>", 5);
        }

        for (int x = 0; x < 5; x++) {
            append (&scb, FileNameSize[x]);
        }
        CHRV3FileClose();
        return 1;
    }
    return 0;
}

int listFiles (uint8_t folder[64], uint8_t *FileArray[20], int page) {
    volatile uint32_t ret;
    int firstItem = (page - 1) * 20;
    int endItem = page * 20;
    int size = 0;
    for (int i = 0; i < 20; i++) {
        for (int j = 0; j < 64; j++) {
            FileArray[i][j] = 0x00;
        }
    }

    if ((CHRV3DiskStatus >= DISK_MOUNTED)) {

        for (int index = firstItem; index < endItem; index++) {
            CHRV3DirtyBuffer();
            strcpy ((char *)mCmdParam.Open.mPathName, (char *)folder);
            ret = strlen ((char *)mCmdParam.Open.mPathName);
            mCmdParam.Open.mPathName[ret] = 0xFF;  // Replace the terminator with the search number according to the length of the string, from 0 to 254,if it is 0xFF that is 255, then the search number is in the CHRV3vFileSize variable
            CHRV3vFileSize = index;                // look for first occurance of file with cart.
            /* open file */
            ret = CHRV3FileOpen();
            if (ret == ERR_MISS_DIR || ret == ERR_MISS_FILE) {
                CHRV3FileClose();
                continue;
            }

            strcpy ((char *)FileArray[size], (char *)mCmdParam.Open.mPathName);
            size++;
            CHRV3FileClose();
        }
    }
    return size;
}

void ProgramCart (CartType cartType, char *Filename) {
    uint32_t totalcount, t;
    uint16_t i, ret;
    uint8_t *filenamecart = (uint8_t *)malloc (64 * sizeof (uint8_t));

    // clear buffers
    for (int x = 0; x < LONG_NAME_BUF_LEN; x++) {
        LongNameBuf[x] = 0x20;
    }
    CHRV3DirtyBuffer();

    if ((CHRV3DiskStatus >= DISK_MOUNTED)) {
        /* Make sure the flash operation is correct */
        Flash_Operation_Key0 = DEF_FLASH_OPERATION_KEY_CODE_0;

        strcpy ((char *)mCmdParam.Open.mPathName, Filename);
        strcpy ((char *)filenamecart, Filename);
        /* open file */
        ret = CHRV3FileOpen();

        /* file or directory not found */
        if (ret == ERR_MISS_DIR || ret == ERR_MISS_FILE) {
            appendString (&scb, Filename);
            NewLine();
            appendString (&scb, "File Not Found.");
            NewLine();
        }
        /* Found file, start IAP processing */
        else {
            if (CHRV3vFileSize > 262144)  // 256K limit check
            {
                NewLine();
                appendString (&scb, "256KB limit exceeded!");
                Delay_Ms (2000);
                PrintMainMenu (0);
                return;
            }

            /* Read File Size */
            totalcount = CHRV3vFileSize;
            File_Length = totalcount;
            NewLine();
            NewLine();
            appendString (&scb, " Programming!");
            NewLine();
            append (&scb, 0x20);
            /* Make sure the flash operation is correct */
            Flash_Operation_Key0 = DEF_FLASH_OPERATION_KEY_CODE_0;
            IAP_Load_Addr_Offset = 0;
            IAP_WriteIn_Length = 0;
            IAP_WriteIn_Count = 0;
            /* Binary file read & iap write in */
            while (totalcount) {

                /* If the file is large and cannot be read at once, you can call CH103ByteRead again to continue reading, and the file pointer will move backward automatically.*/
                /* MAX_PATH_LEN: the maximum path length, including all slash separators and decimal point separators and path terminator 00H */
                if (totalcount > (MAX_PATH_LEN - 1)) {
                    t = MAX_PATH_LEN - 1;                     // The length of a single read/write cannot exceed sizeof( mCmdParam.Other.mBuffer ) if the remaining data is large.
                } else {
                    t = totalcount;                           // Last remaining bytes
                }
                mCmdParam.ByteRead.mByteCount = t;            // Request to read out tens of bytes of data
                mCmdParam.ByteRead.mByteBuffer = &Com_Buffer[0];
                ret = CHRV3ByteRead();                        // Read the data block in bytes, the length of a single read/write cannot exceed MAX_BYTE_IO, and the second call is followed by a backward read.
                mStopIfError (ret);
                totalcount -= mCmdParam.ByteRead.mByteCount;  // Counting, subtracting the number of characters that have actually been read out
                for (i = 0; i < mCmdParam.ByteRead.mByteCount; i++) {
                    IAPLoadBuffer[IAP_WriteIn_Length] = mCmdParam.ByteRead.mByteBuffer[i];
                    IAP_WriteIn_Length++;

                    /* The whole package part of the IAP user file */
                    if (IAP_WriteIn_Length == DEF_MAX_IAP_BUFFER_LEN) {
                        /* Write Data In Flash */

                        ret = IAP_Flash_Program (start_address + IAP_Load_Addr_Offset, IAPLoadBuffer, IAP_WriteIn_Length);
                        mStopIfError (ret);

                        IAP_Load_Addr_Offset += DEF_MAX_IAP_BUFFER_LEN;
                        IAP_WriteIn_Count += IAP_WriteIn_Length;
                        IAP_WriteIn_Length = 0;
                    }
                }

                if (mCmdParam.ByteRead.mByteCount < t)  // The actual number of characters read is less than the number of characters requested, which means that the end of the file has been reached.
                {
                    break;
                }
            }
            /* Close the file be operated now */
            CHRV3FileClose();

            /* Disposal of remaining package length  */
            ret = IAP_Flash_Program (start_address + IAP_Load_Addr_Offset, IAPLoadBuffer, IAP_WriteIn_Length);
            mStopIfError (ret);
            IAP_WriteIn_Count += IAP_WriteIn_Length;
            /* Check actual write length and file length */
            if (CHRV3vFileSize == IAP_WriteIn_Count) {
                MapperCode_Write (cartType, File_Length, (char *)filenamecart);
                free (filenamecart);
                FLASH_Lock_Fast();
                NewLine();
                appendString (&scb, " Done. Rebooting...");
                Reset();

            } else {
                /* IAP length checksum error */
                appendString (&scb, " Check sum error, programming failed");
                FLASH_Lock_Fast();
            }
        }
    }
}

int8_t CHRV3GetLongName (void) {
    uint8_t i;
    uint16_t index;
    uint32_t BackFdtSector;
    uint8_t sum;
    // uint16_t  Backoffset;
    uint16_t offset;
    uint8_t FirstBit;
    uint8_t BackPathBuf[MAX_PATH_LEN];

    i = CHRV3FileOpen();
    if ((i == ERR_SUCCESS) || (i == ERR_OPEN_DIR)) {
        for (i = 0; i != MAX_PATH_LEN; i++)
            BackPathBuf[i] = mCmdParam.Open.mPathName[i];
        sum = CheckNameSum (&DISK_BASE_BUF[CHRV3vFdtOffset]);
        index = 0;
        FirstBit = FALSE;
        //  Backoffset = CHRV3vFdtOffset;
        BackFdtSector = CHRV3vFdtLba;
        if (CHRV3vFdtOffset == 0) {
            if (FirstBit == FALSE)
                FirstBit = TRUE;
            i = GetUpSectorData (&BackFdtSector);
            if (i == ERR_SUCCESS) {
                CHRV3vFdtOffset = CHRV3vSectorSize;
                goto P_NEXT1;
            }
        } else {

P_NEXT1:
            offset = CHRV3vFdtOffset;
            while (1) {
                if (offset != 0) {
                    offset = offset - 32;
                    if ((DISK_BASE_BUF[offset + 11] == ATTR_LONG_NAME) && (DISK_BASE_BUF[offset + 13] == sum)) {
                        // if ((index + 26) > LONG_NAME_BUF_LEN)
                        //     return ERR_BUF_OVER;

                        for (i = 0; i != 5; i++) {

                            LongNameBuf[index++] =
                                DISK_BASE_BUF[offset + i * 2 + 1];
                            LongNameBuf[index++] =
                                DISK_BASE_BUF[offset + i * 2 + 2];
                        }

                        for (i = 0; i != 6; i++) {
                            LongNameBuf[index++] =
                                DISK_BASE_BUF[offset + +14 + i * 2];
                            LongNameBuf[index++] =
                                DISK_BASE_BUF[offset + 14 + i * 2 + 1];
                        }

                        for (i = 0; i != 2; i++) {
                            LongNameBuf[index++] =
                                DISK_BASE_BUF[offset + 28 + i * 2];
                            LongNameBuf[index++] =
                                DISK_BASE_BUF[offset + 28 + i * 2 + 1];
                        }

                        if (DISK_BASE_BUF[offset] & 0X40) {
                            if (!(((LongNameBuf[index - 1] == 0x00) && (LongNameBuf[index - 2] == 0x00)) || ((LongNameBuf[index - 1] == 0xFF) && (LongNameBuf[index - 2] == 0xFF)))) {
                                //  if (index + 52 > LONG_NAME_BUF_LEN)
                                //      return ERR_BUF_OVER;
                                LongNameBuf[index] = 0x00;
                                LongNameBuf[index + 1] = 0x00;
                            }
                            return ERR_SUCCESS;
                        }
                    } else
                        return ERR_NO_NAME;
                } else {
                    if (FirstBit == FALSE)
                        FirstBit = TRUE;
                    else {
                        for (i = 0; i != MAX_PATH_LEN; i++)
                            mCmdParam.Open.mPathName[i] = BackPathBuf[i];
                    }
                    i = GetUpSectorData (&BackFdtSector);
                    if (i == ERR_SUCCESS) {
                        CHRV3vFdtOffset = CHRV3vSectorSize;
                        goto P_NEXT1;
                    } else
                        return i;
                }
            }
        }
    }
    return i;
}

uint8_t CheckNameSum (uint8_t *p) {
    uint8_t FcbNameLen;
    uint8_t Sum;

    Sum = 0;
    for (FcbNameLen = 0; FcbNameLen != 11; FcbNameLen++)
        Sum = ((Sum & 1) ? 0x80 : 0) + (Sum >> 1) + *p++;
    return Sum;
}

uint8_t GetUpSectorData (uint32_t *NowSector) {
    uint8_t i;
    uint8_t len;
    uint32_t index;

    index = 0;
    for (len = 0; len != MAX_PATH_LEN; len++) {
        if (mCmdParam.Open.mPathName[len] == 0)
            break;
    }

    for (i = len - 1; i != 0xff; i--) {
        if ((mCmdParam.Open.mPathName[i] == '\\') || (mCmdParam.Open.mPathName[i] == '/'))
            break;
    }
    mCmdParam.Open.mPathName[i] = 0x00;

    if (i == 0) {
        mCmdParam.Open.mPathName[0] = '/';
        mCmdParam.Open.mPathName[1] = 0;
        i = CHRV3FileOpen();
        if (i == ERR_OPEN_DIR)
            goto P_NEXT0;
    } else {
        i = CHRV3FileOpen();
        if (i == ERR_OPEN_DIR) {
            while (1) {
P_NEXT0:
                mCmdParam.Locate.mSectorOffset = index;
                i = CHRV3FileLocate();
                if (i == ERR_SUCCESS) {
                    if (*NowSector == mCmdParam.Locate.mSectorOffset) {
                        // if (index == 0)
                        //   return ERR_NO_NAME;
                        mCmdParam.Locate.mSectorOffset = --index;
                        i = CHRV3FileLocate();
                        if (i == ERR_SUCCESS) {
                            *NowSector = mCmdParam.Locate.mSectorOffset;
                            mCmdParam.Read.mSectorCount = 1;
                            mCmdParam.Read.mDataBuffer = &DISK_BASE_BUF[0];
                            i = CHRV3FileRead();
                            return i;
                        } else
                            return i;
                    }
                } else
                    return i;
                index++;
            }
        }
    }
    return i;
}