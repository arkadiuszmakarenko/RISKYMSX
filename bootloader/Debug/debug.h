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
#include "stddef.h"
#include "ch32v30x.h"

/* Size optimization options */
#define USE_LIGHTWEIGHT_PRINT 0 /* 1 = Use lightweight print functions, 0 = Use standard printf */

/* UART Printf Definition */
#define DEBUG_UART1 1
#define DEBUG_UART2 2
#define DEBUG_UART3 3

/* DEBUG UATR Definition */
#ifndef DEBUG
#define DEBUG DEBUG_UART1
#endif

/* SDI Printf Definition */
#define SDI_PR_CLOSE 0
#define SDI_PR_OPEN 1

#ifndef SDI_PRINT
#define SDI_PRINT SDI_PR_CLOSE
#endif


void Delay_Init (void);
void Delay_Us (uint32_t n);
void Delay_Ms (uint32_t n);
void USART_Printf_Init (uint32_t baudrate);
void SDI_Printf_Enable (void);

/* Lightweight print functions (minimal printf alternatives) */
void lite_putchar (char c);
void lite_puts (const char *str);
void lite_put_hex (uint32_t value, uint8_t digits);
void lite_put_dec (uint32_t value);

/* Convenience macros for easy migration */
#if USE_LIGHTWEIGHT_PRINT
#define debug_print(str) lite_puts (str)
#define debug_print_dec(val) lite_put_dec (val)
#define debug_print_hex(val) lite_put_hex (val, 8)
#define debug_print_hex4(val) lite_put_hex (val, 4)
#define debug_print_hex2(val) lite_put_hex (val, 2)
#else
#define debug_print(str) printf (str)
#define debug_print_dec(val) printf ("%u", val)
#define debug_print_hex(val) printf ("%08X", val)
#define debug_print_hex4(val) printf ("%04X", val)
#define debug_print_hex2(val) printf ("%02X", val)
#endif

#ifdef __cplusplus
}
#endif

#endif
