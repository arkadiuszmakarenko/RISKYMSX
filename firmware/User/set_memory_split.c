// Example changing memory option bytes on ch32v307 to CODE:192kB / RAM:128kB
/* This shows how to use the option bytes.  I.e. how do you disable NRST?
   WARNING Portions of this code are under the following copyright.
*/
/********************************** (C) COPYRIGHT  *******************************
 * File Name          : ch32v00x_flash.c
 * Author             : WCH
 * Version            : V1.0.0
 * Date               : 2022/08/08
 * Description        : This file provides all the FLASH firmware functions.
 *********************************************************************************
 * Copyright (c) 2021 Nanjing Qinheng Microelectronics Co., Ltd.
 * Attention: This software (modified or not) and binary are used for 
 * microcontroller manufactured by Nanjing Qinheng Microelectronics.
 *******************************************************************************/

#include "set_memory_split.h"


uint32_t count;



void SetSplit()
{

	Delay_Ms( 100 );

	FLASH->OBKEYR = FLASH_KEY1;
	FLASH->OBKEYR = FLASH_KEY2;
	FLASH->KEYR = FLASH_KEY1;
	FLASH->KEYR = FLASH_KEY2;
	FLASH->MODEKEYR = FLASH_KEY1;
	FLASH->MODEKEYR = FLASH_KEY2;

//	printf( "Option bytes started as:%04x\n", OB->USER );

	uint16_t rdptmp = RDP_Key;


	int status = FLASH_WaitForLastOperation(EraseTimeout);
	if(status == FLASH_COMPLETE)
	{
		FLASH->OBKEYR = FLASH_KEY1;
		FLASH->OBKEYR = FLASH_KEY2;

		FLASH->CTLR |= CR_OPTER_Set;
		FLASH->CTLR |= CR_STRT_Set;
		status = FLASH_WaitForLastOperation(EraseTimeout);

		if(status == FLASH_COMPLETE)
		{
			FLASH->CTLR &= CR_OPTER_Reset;
			FLASH->CTLR |= CR_OPTPG_Set;
			OB->RDPR = (uint16_t)rdptmp;
			status = FLASH_WaitForLastOperation(ProgramTimeout);

			if(status != FLASH_TIMEOUT)
			{
				FLASH->CTLR &= CR_OPTPG_Reset;
			}
		}
		else
		{
			if(status != FLASH_TIMEOUT)
			{
				FLASH->CTLR &= CR_OPTPG_Reset;
			}
		}
	}


	//printf( "After Clear:%04x\n", OB->USER );

    FLASH->OBKEYR = FLASH_KEY1;
    FLASH->OBKEYR = FLASH_KEY2;
    status = FLASH_WaitForLastOperation(10000);

    if(status == FLASH_COMPLETE)
    {
        FLASH->CTLR |= CR_OPTPG_Set;
        OB->USER = 0xc0FF; // This sets the memory split.

        status = FLASH_WaitForLastOperation(10000);
        if(status != FLASH_TIMEOUT)
        {
            FLASH->CTLR &= CR_OPTPG_Reset;
        }
    }

//	printf( "After Write:%04x\n", OB->USER );

	
}

