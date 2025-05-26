/********************************** (C) COPYRIGHT  *******************************
 * File Name          : iap.h
 * Author             : WCH
 * Version            : V1.0.0
 * Date               : 2020/12/16
 * Description        : IAP
 *********************************************************************************
 * Copyright (c) 2021 Nanjing Qinheng Microelectronics Co., Ltd.
 * Attention: This software (modified or not) and binary are used for
 * microcontroller manufactured by Nanjing Qinheng Microelectronics.
 *******************************************************************************/
#ifndef __USB_HOST_IAP_H
#define __USB_HOST_IAP_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stdio.h"
#include "string.h"
#include "ch32v30x.h"
#include "ch32v30x_usbfs_host.h"
#include "usb_host_config.h"
#include "CHRV3UFI.h"
#include "cart.h"
/*******************************************************************************/
/* File Descripton */
/*
 *@Note
    Suitable for CH32 series microcontrollers, including CH32V series with risc-v core and CH32F series with Cortex-m3 core
    The iap flash. c/iap flash. h file includes the start and end definitions of iap region codes required for iap and apps, as well as the start and end definitions of app region codes,
    The operation function of chip flash.
    ***June 13, 2022***
    1. Version 0.1,
    The complete code includes flash words, half words, byte reads, iap specific flash erasure, writing, programming, reading, and verification.
    All operations involving flash reading, writing, and erasing are protected with DEF-FLASHT-OPERATION_KEY-CODE, DEF-FLASHT-OPERATION_KEY-CODE-0
    Write at the beginning of the main function, and DEF-FLASHT-OPERATION_KEY-CODE_1 is written before each flash operation.
    Use macro definitions to distinguish between iap code and app code, and unify the code for flash operations in iap and app into one file
    IAP code opens all function calls, while app code only opens IAP_VerifyCode-Erase(); Function.
*/

/*******************************************************************************/
/* Macro Definitions */
#define DEF_CORE_RV 0x01
#define DEF_CORE_CM3 0x10
#define DEF_CORE_TYPE DEF_CORE_RVend_address

/* IAP Status Definitions */
#define DEF_IAP_SUCCESS 0x00    /* IAP Operation Success */
#define DEF_IAP_DEFAULT 0xFF    /* IAP Operation Default Status */
#define DEF_IAP_ERR_DETECT 0xF1 /* IAP Operation, USB device not detected */
#define DEF_IAP_ERR_ENUM 0xF2   /* IAP Operation, Host enumeration failure */
#define DEF_IAP_ERR_FILE 0xF3   /* IAP Operation, File name incorrect or no such file */
#define DEF_IAP_ERR_FLASH 0xF4  /* IAP Operation, Flash operation failure */
#define DEF_IAP_ERR_VERIFY 0xF5 /* IAP Operation, Flash data verify error */
#define DEF_IAP_ERR_LENGTH 0xF6 /* IAP Operation, Flash data length verify error */

/* IAP Load buffer Definitions */
#define DEF_MAX_IAP_BUFFER_LEN 1024 /* IAP Load buffer size */

/* Flash page size */
#define DEF_FLASH_PAGE_SIZE 0x100 /* Flash Page size, refer to the data-sheet (ch32vf2x_3xRM.pdf) for details */
                                  /* Please refer to link.ld file for accuracy flash size, the size here is the smallest available size */
/* Flash Operation Key Setting */
#define DEF_FLASH_OPERATION_KEY_CODE_0 0x1A86FF00 /* IAP Flash operation Key-code 0 */
#define DEF_FLASH_OPERATION_KEY_CODE_1 0x55AA55AA /* IAP Flash operation Key-code 1 */

/*******************************************************************************/
/* Variable Extrapolation */
/* Flash Operation Key Variables, Operation with DEF_FLASH_OPERATION_KEY_CODE_x to ensure the correctness of flash operation*/
extern volatile uint32_t Flash_Operation_Key0; /* IAP Flash operation Key-code Variables 0 */
extern volatile uint32_t Flash_Operation_Key1; /* IAP Flash operation Key-code Variables 1 */

/*******************************************************************************/

#define LONG_NAME_BUF_LEN (20 * 26)
#define UNICODE_ENDIAN 0
#define ERR_NO_NAME 0X44
#define ERR_BUF_OVER 0X45
#define ERR_LONG_NAME 0X46
#define ERR_NAME_EXIST 0X47

#define LongName_Len 124
#define TRUE 1
#define FALSE 0

#define FILENAME_LENGTH 35

/*******************************************************************************/
/* Function Extrapolation */
/* Lower operation */
extern uint8_t
FLASH_ReadByte (uint32_t address);
extern uint16_t FLASH_ReadHalfWord (uint32_t address);
extern uint32_t FLASH_ReadWord (uint32_t address);
extern uint8_t IAP_Flash_Erase (uint32_t address, uint32_t length);
extern uint8_t IAP_Flash_Read (uint32_t address, uint8_t *buff, uint32_t length);
extern uint8_t IAP_Flash_Write (uint32_t address, uint8_t *buff, uint32_t length);
extern uint32_t IAP_Flash_Verify (uint32_t address, uint8_t *buff, uint32_t length);
extern uint32_t IAP_Flash_Program (uint32_t address, uint8_t *buff, uint32_t length);
extern void FLASH_ReadWordAdd (uint32_t address, u32 *buff, uint16_t length);

/* upper operation */
extern void IAP_Initialization (void);
extern uint8_t IAP_USBH_PreDeal (void);
extern void MapperCode_Write (CartType type, uint32_t cartSize, char *Filename);
extern void MapperCode_Update (CartType type);

extern int MountDrive (void);
extern int isFile (uint8_t Filename[64]);
extern int printFilename (uint8_t FileArray[64], uint32_t ShortNames);
extern int listFiles (uint8_t folder[64], uint8_t *FileArray[20], int page);
extern void ProgramCart (CartType cartType, char *Filename);

extern uint8_t CHRV3GetLongName (void);
extern uint8_t CheckNameSum (uint8_t *p);
extern uint8_t GetUpSectorData (uint32_t *NowSector);

#ifdef __cplusplus
}
#endif

#endif
