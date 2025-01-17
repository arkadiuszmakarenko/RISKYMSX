
#ifndef __MEMORY_SPLIT_H
#define __MEMORY_SPLIT_H

#include <stdio.h>
#include "ch32v30x_flash.h"

/* FLASH Keys */
#define RDP_Key                    ((uint16_t)0x00A5)
#define FLASH_KEY1                 ((uint32_t)0x45670123)
#define FLASH_KEY2                 ((uint32_t)0xCDEF89AB)
#define ProgramTimeout             ((uint32_t)0x00005000)
#define EraseTimeout               ((uint32_t)0x000B0000)
#define CR_OPTER_Set               ((uint32_t)0x00000020)
#define CR_OPTER_Reset             ((uint32_t)0xFFFFFFDF)
#define CR_STRT_Set                ((uint32_t)0x00000040)
#define CR_OPTPG_Reset             ((uint32_t)0xFFFFFFEF)
#define CR_OPTPG_Set               ((uint32_t)0x00000010)

void SetSplit();


#endif