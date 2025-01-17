/********************************** (C) COPYRIGHT *******************************
 * File Name          : main.c
 * Author             : WCH
 * Version            : V1.0.0
 * Date               : 2021/06/06
 * Description        : Main program body.
 *********************************************************************************
 * Copyright (c) 2021 Nanjing Qinheng Microelectronics Co., Ltd.
 * Attention: This software (modified or not) and binary are used for
 * microcontroller manufactured by Nanjing Qinheng Microelectronics.
 *******************************************************************************/

/*
 *@Note
 USART Print debugging routine:
 USART1_Tx(PA9).
 This example demonstrates using USART1(PA9) as a print debug port output.

 */

#include "debug.h"
#include "gpio.h"
#include "usb_host_iap.h"
#include "cart.h"
#include "set_memory_split.h"
/* Global typedef */

/* Global define */

/* Global define */

/* Global Variable */

/*********************************************************************
 * @fn      main
 *
 * @brief   Main program.
 *
 * @return  none
 */


/*********************************************************************
 * @fn      Timer4_Init
 *
 * @brief   Initializes TIM4
 *
 * @return  none
 */


int main(void) {
	NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);
    SystemCoreClockUpdate();

    //Check memory split 5 to 7th bit needs to be 111
    if ( (OB->USER & 0b1110000) >>4  != 0b111 )
    {
        //Configure 288kb Flash + 32k RAM.
        SetSplit();
    }

    GPIO_Config();
    Delay_Init();


    // Set up Cart in programming mode. It will program flash from 0x00008000 - 256K Flash Availiable.
    //It requires to configure chip during programming - for 288kb Flash + 32k RAM.
    if (GPIO_ReadInputDataBit(GPIOA, GPIO_Pin_8) == 1) {
        IAP_Initialization();
        GPIO_WriteBit(GPIOA, GPIO_Pin_0, Bit_RESET);
        GPIO_WriteBit(GPIOA, GPIO_Pin_1, Bit_RESET);
        GPIO_WriteBit(GPIOA, GPIO_Pin_2, Bit_RESET);
        GPIO_WriteBit(GPIOA, GPIO_Pin_3, Bit_RESET);
        Delay_Ms(200);
        GPIO_WriteBit(GPIOA, GPIO_Pin_1, Bit_SET);
        GPIO_WriteBit(GPIOA, GPIO_Pin_2, Bit_SET);
        GPIO_WriteBit(GPIOA, GPIO_Pin_3, Bit_SET);

        IAP_Configure("/CART.*", 0x08008000, 0x08048000);
        //read and flash
        uint32_t writeStatus = 0;
        while(writeStatus == 0 )
        {
            writeStatus = IAP_Main_Deal( );
        }
    }
    
    Init_Cart();

    while(1)
    {
        if (GPIO_ReadInputDataBit(GPIOA, GPIO_Pin_8) == 1) {
        //force mpu reset to allow cart programming
            PFIC->SCTLR|= (1<<31);
        }
    }

}
