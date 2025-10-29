/********************************** (C) COPYRIGHT  *******************************
* File Name          : debug.h
* Author             : WCH
* Version            : V1.0.0
* Date               : 2021/06/06
* Description        : This file contains all the functions prototypes for UART
*                      Printf , Delay functions.
*********************************************************************************
* Copyright (c) 2021 Nanjing Qinheng Microelectronics Co., Ltd.
* Attention: This software (modified or not) and binary are used for 
* microcontroller manufactured by Nanjing Qinheng Microelectronics.
*******************************************************************************/
#ifndef __DEBUG_H
#define __DEBUG_H

#ifdef __cplusplus
 extern "C" {
#endif

#include "stdio.h"
#include "ch32v30x.h"

/* DEBUG UATR Definition */
#define DEBUG_UART1   1
#define DEBUG_UART2   2
#define DEBUG_UART3   3

/* If you want to use the SWD LOG function. */
/* LogInit(); Do not Initialize UART */
/* printf(); of DEBUG_UART1/2/3 */
#ifndef DEBUG
#define DEBUG   DEBUG_UART1
#endif

void Delay_Init(void);
void Delay_Us (uint32_t n);
void Delay_Ms (uint32_t n);

void USART_Printf_Init(uint32_t baudrate);

/* Lightweight printf alternatives (ultra-light implementation) */
void lite_putchar(char c);
void lite_puts(const char* str);
void lite_put_hex(uint32_t value, uint8_t digits);
void lite_put_dec(uint32_t value);


#ifdef __cplusplus
}
#endif

#endif 



