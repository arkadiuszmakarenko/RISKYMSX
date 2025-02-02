/********************************** (C) COPYRIGHT  *******************************
 * File Name          : iap.c
 * Author             : WCH
 * Version            : V1.0.0
 * Date               : 2022/08/20
 * Description        : IAP
 *********************************************************************************
 * Copyright (c) 2021 Nanjing Qinheng Microelectronics Co., Ltd.
 * Attention: This software (modified or not) and binary are used for
 * microcontroller manufactured by Nanjing Qinheng Microelectronics.
 *******************************************************************************/

#include "usb_host_iap.h"
#include "cart.h"
#include <stdio.h>
#include "utils.h"


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
        buff[i] = *(__IO uint32_t *)address;  // 读指定地址的一个字的数据
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
    /* 根据芯片的flash编程流程修改此处代码 */
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


    /* USB Host Initialization */
    // printf( "USB Host & UDisk Lib Initialization. \r\n" );
    /* Initialize USBHS host */

    /* Initialize USBFS host */

    // DUG_PRINTF( "USBFS Host Init\r\n" );
    USBFS_RCC_Init();
    USBFS_Host_Init (ENABLE);
    memset (&RootHubDev[DEF_USB_PORT_FS].bStatus, 0, sizeof (struct _ROOT_HUB_DEVICE));
    memset (&HostCtl[DEF_USB_PORT_FS].InterfaceNum, 0, sizeof (struct __HOST_CTL));


    /* USB Libs Initialization */
    // printf( "UDisk library Initialization. \r\n" );
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
#if DEF_USB_PORT_FS_EN
        s = USBFSH_CheckRootHubPortStatus (RootHubDev[usb_port].bStatus);
#endif
    } else if (usb_port == DEF_USB_PORT_HS) {
#if DEF_USB_PORT_HS_EN
        s = USBHSH_CheckRootHubPortStatus (RootHubDev[usb_port].bStatus);
#endif
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
#if DEF_USB_PORT_FS_EN
        USBFSH_ResetRootHubPort (mode);
#endif
    } else if (usb_port == DEF_USB_PORT_HS) {
#if DEF_USB_PORT_HS_EN
        USBHSH_ResetRootHubPort (mode);
#endif
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
#if DEF_USB_PORT_FS_EN
        s = USBFSH_EnableRootHubPort (&RootHubDev[usb_port].bSpeed);
#endif
    } else if (usb_port == DEF_USB_PORT_HS) {
#if DEF_USB_PORT_HS_EN
        s = USBHSH_EnableRootHubPort (&RootHubDev[usb_port].bSpeed);
#endif
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
#if DEF_USB_PORT_FS_EN
        USBFSH_SetSelfSpeed (RootHubDev[usb_port].bSpeed);
#endif
    } else if (usb_port == DEF_USB_PORT_HS) {
#if DEF_USB_PORT_HS_EN
        USBHSH_SetSelfSpeed (RootHubDev[usb_port].bSpeed);
#endif
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
#if DEF_USB_PORT_FS_EN
        s = USBFSH_GetDeviceDescr (&RootHubDev[usb_port].bEp0MaxPks, DevDesc_Buf);
#endif
    } else if (usb_port == DEF_USB_PORT_HS) {
#if DEF_USB_PORT_HS_EN
        s = USBHSH_GetDeviceDescr (&RootHubDev[usb_port].bEp0MaxPks, DevDesc_Buf);
#endif
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
#if DEF_USB_PORT_FS_EN
        RootHubDev[usb_port].bAddress = (uint8_t)(DEF_USB_PORT_FS + USB_DEVICE_ADDR);
        s = USBFSH_SetUsbAddress (RootHubDev[usb_port].bEp0MaxPks, RootHubDev[usb_port].bAddress);
#endif
    } else if (usb_port == DEF_USB_PORT_HS) {
#if DEF_USB_PORT_HS_EN
        RootHubDev[usb_port].bAddress = (uint8_t)(DEF_USB_PORT_HS + USB_DEVICE_ADDR);
        s = USBHSH_SetUsbAddress (RootHubDev[usb_port].bEp0MaxPks, RootHubDev[usb_port].bAddress);
#endif
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
#if DEF_USB_PORT_FS_EN
        s = USBFSH_GetConfigDescr (RootHubDev[usb_port].bEp0MaxPks, Com_Buffer, DEF_COM_BUF_LEN, pcfg_len);
#endif
    } else if (usb_port == DEF_USB_PORT_HS) {
#if DEF_USB_PORT_HS_EN
        s = USBHSH_GetConfigDescr (RootHubDev[usb_port].bEp0MaxPks, Com_Buffer, DEF_COM_BUF_LEN, pcfg_len);
#endif
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
#if DEF_USB_PORT_FS_EN
        s = USBFSH_SetUsbConfig (RootHubDev[usb_port].bEp0MaxPks, cfg_val);
#endif
    } else if (usb_port == DEF_USB_PORT_HS) {
#if DEF_USB_PORT_HS_EN
        s = USBHSH_SetUsbConfig (RootHubDev[usb_port].bEp0MaxPks, cfg_val);
#endif
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

    //  DUG_PRINTF( "Enum:\r\n" );

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
    // DUG_PRINTF("Get DevDesc: ");
    s = USBH_GetDeviceDescr (usb_port);
    if (s == ERR_SUCCESS) {
        /* Print USB device device descriptor */
        // #if DEF_DEBUG_PRINTF
        //         for( i = 0; i < 18; i++ )
        //         {
        //             DUG_PRINTF( "%02x ", DevDesc_Buf[ i ] );
        //         }
        //         DUG_PRINTF("\n");
        // #endif
    } else {
        /* Determine whether the maximum number of retries has been reached, and retry if not reached */
        //  DUG_PRINTF( "Err(%02x)\n", s );
        if (enum_cnt <= 5) {
            goto ENUM_START;
        }
        return DEF_DEV_DESCR_GETFAIL;
    }

    /* Set the USB device address */
    // DUG_PRINTF("Set DevAddr: ");
    s = USBH_SetUsbAddress (usb_port);
    if (s == ERR_SUCCESS) {
        //  DUG_PRINTF( "OK\n" );
    } else {
        /* Determine whether the maximum number of retries has been reached, and retry if not reached */
        // DUG_PRINTF( "Err(%02x)\n", s );
        if (enum_cnt <= 5) {
            goto ENUM_START;
        }
        return DEF_DEV_ADDR_SETFAIL;
    }
    Delay_Ms (5);

    /* Get the USB device configuration descriptor */
    // DUG_PRINTF("Get CfgDesc: ");
    s = USBH_GetConfigDescr (usb_port, &len);
    if (s == ERR_SUCCESS) {
        cfg_val = ((PUSB_CFG_DESCR)Com_Buffer)->bConfigurationValue;

        /* Print USB device configuration descriptor  */
#if DEF_DEBUG_PRINTF
        for (i = 0; i < len; i++) {
            //  DUG_PRINTF( "%02x ", Com_Buffer[ i ] );
        }
        // DUG_PRINTF("\n");
#endif
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

void MapperCode_Write (CartType type, uint32_t cartSize) {
    uint32_t cfg_address = 0x08007F00;
    uint16_t volatile cartSizekB = ((cartSize + 512) / 1024);

    uint32_t cfg[64];
    cfg[0] = type;
    cfg[1] = cartSizekB;
    cfg[2] = cartSize;
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

void ListFiles() {
    volatile uint32_t ret;
    if ((CHRV3DiskStatus >= DISK_MOUNTED)) {


        int index = 1;
        for (index = 1; index < 100; index++) {
            strcpy ((char *)mCmdParam.Open.mPathName, "/*");
            ret = strlen ((char *)mCmdParam.Open.mPathName);
            mCmdParam.Open.mPathName[ret] = 0xFF;  // Replace the terminator with the search number according to the length of the string, from 0 to 254,if it is 0xFF that is 255, then the search number is in the CHRV3vFileSize variable
            CHRV3vFileSize = index;                // look for first occurance of file with cart.
            /* open file */
            ret = CHRV3FileOpen();
            if (ret == ERR_MISS_DIR || ret == ERR_MISS_FILE) {
                return;
            }
            if (ret == ERR_FOUND_NAME) {

                ret = CHRV3FileOpen();
                if (ret != ERR_OPEN_DIR) {

                    uint8_t ret;
                    uint16_t j;

                    ret = CHRV3GetLongName();
                    if (ret == ERR_SUCCESS) {

                        char str[5];
                        intToString (index, str);

                        appendString (&scb, str);
                        appendString (&scb, "-");

                        //  int position = findLastCharPositionOfSubstring ((const char *)LongNameBuf, 50, "ROM");

                        for (j = 0; j != 40; j++) {
                            if (isPrintableCharacter (LongNameBuf[j])) {
                                append (&scb, LongNameBuf[j]);
                            }
                        }

                        for (j = 0; j != LONG_NAME_BUF_LEN; j++) {
                            LongNameBuf[j] = 0x20;
                        }


                        append (&scb, 0x0D);
                        append (&scb, 0x0A);
                    }
                }
            }
        }
    }
}

void ProgramCart (uint32_t FileIndex, CartType cartType) {
    uint32_t totalcount, t;
    uint16_t i, ret;
    char message[10];

    if ((CHRV3DiskStatus >= DISK_MOUNTED)) {
        /* Make sure the flash operation is correct */
        Flash_Operation_Key0 = DEF_FLASH_OPERATION_KEY_CODE_0;

        strcpy ((char *)mCmdParam.Open.mPathName, "/*");
        ret = strlen ((char *)mCmdParam.Open.mPathName);
        mCmdParam.Open.mPathName[ret] = 0xFF;  // Replace the terminator with the search number according to the length of the string, from 0 to 254,if it is 0xFF that is 255, then the search number is in the CHRV3vFileSize variable
        CHRV3vFileSize = FileIndex;            // look for first occurance of file with cart.
        /* open file */
        ret = CHRV3FileOpen();
        /* file or directory not found */
        if (ret == ERR_MISS_DIR || ret == ERR_MISS_FILE) {
            appendString (&scb, "FileNotFound!");  // file not found
        }
        /* Found file, start IAP processing */
        else {
            appendString (&scb, "FileFound ");
            appendString (&scb, (const char *)mCmdParam.Open.mPathName);

            ret = CHRV3FileOpen();
            //  appendString (&scb, "FileFound");
            /* Read File Size */
            totalcount = CHRV3vFileSize;
            File_Length = totalcount;
            intToString (totalcount, message);
            // appendString (&scb, message);
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

                        // handle leds to indicate cart programming


                        if (IAP_WriteIn_Count >= ((totalcount * 50) / 100)) {
                            GPIO_WriteBit (GPIOA, GPIO_Pin_1, Bit_RESET);
                        }
                        if (IAP_WriteIn_Count >= totalcount) {
                            GPIO_WriteBit (GPIOA, GPIO_Pin_2, Bit_RESET);
                        }
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
                MapperCode_Write (cartType, File_Length);
                FLASH_Lock_Fast();
                GPIO_WriteBit (GPIOA, GPIO_Pin_3, Bit_RESET);
                appendString (&scb, "100% ");
            } else {
                /* IAP length checksum error */
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
                        if ((index + 26) > LONG_NAME_BUF_LEN)
                            return ERR_BUF_OVER;

                        for (i = 0; i != 5; i++) {
#if UNICODE_ENDIAN == 1
                            LongNameBuf[index++] =
                                DISK_BASE_BUF[offset + i * 2 + 2];
                            LongNameBuf[index++] =
                                DISK_BASE_BUF[offset + i * 2 + 1];
#else
                            LongNameBuf[index++] =
                                DISK_BASE_BUF[offset + i * 2 + 1];
                            LongNameBuf[index++] =
                                DISK_BASE_BUF[offset + i * 2 + 2];
#endif
                        }

                        for (i = 0; i != 6; i++) {
#if UNICODE_ENDIAN == 1
                            LongNameBuf[index++] =
                                DISK_BASE_BUF[offset + 14 + i * 2 + 1];
                            LongNameBuf[index++] =
                                DISK_BASE_BUF[offset + +14 + i * 2];
#else
                            LongNameBuf[index++] =
                                DISK_BASE_BUF[offset + +14 + i * 2];
                            LongNameBuf[index++] =
                                DISK_BASE_BUF[offset + 14 + i * 2 + 1];
#endif
                        }

                        for (i = 0; i != 2; i++) {
#if UNICODE_ENDIAN == 1
                            LongNameBuf[index++] =
                                DISK_BASE_BUF[offset + 28 + i * 2 + 1];
                            LongNameBuf[index++] =
                                DISK_BASE_BUF[offset + 28 + i * 2];
#else
                            LongNameBuf[index++] =
                                DISK_BASE_BUF[offset + 28 + i * 2];
                            LongNameBuf[index++] =
                                DISK_BASE_BUF[offset + 28 + i * 2 + 1];
#endif
                        }

                        if (DISK_BASE_BUF[offset] & 0X40) {
                            if (!(((LongNameBuf[index - 1] == 0x00) && (LongNameBuf[index - 2] == 0x00)) || ((LongNameBuf[index - 1] == 0xFF) && (LongNameBuf[index - 2] == 0xFF)))) {
                                if (index + 52 > LONG_NAME_BUF_LEN)
                                    return ERR_BUF_OVER;
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
                        if (index == 0)
                            return ERR_NO_NAME;
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