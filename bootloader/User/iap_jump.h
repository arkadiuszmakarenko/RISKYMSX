/********************************** (C) COPYRIGHT *******************************
 * File Name          : iap_jump.h
 * Author             : Simplified from WCH IAP example
 * Version            : V1.0.0
 * Date               : 2024/12/19
 * Description        : Simple IAP jump functionality for bootloader
*********************************************************************************
* Copyright (c) 2021 Nanjing Qinheng Microelectronics Co., Ltd.
* Attention: This software (modified or not) and binary are used for 
* microcontroller manufactured by Nanjing Qinheng Microelectronics.
*******************************************************************************/
#ifndef __IAP_JUMP_H
#define __IAP_JUMP_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ch32v30x.h"

/*******************************************************************************/
/* Macro Definitions */
#define DEF_CORE_RV                       0x01
#define DEF_CORE_CM3                      0x10
#define DEF_CORE_TYPE                     DEF_CORE_RV

/* APP CODE ADDR Setting */
#define DEF_APP_CODE_START_ADDR           0x08006000                             /* User code start address */

/*******************************************************************************/
/* Function Extrapolation */
extern void IAP_Jump_APP(void);

#ifdef __cplusplus
}
#endif

#endif