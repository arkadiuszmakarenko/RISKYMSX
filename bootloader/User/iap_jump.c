/********************************** (C) COPYRIGHT *******************************
 * File Name          : iap_jump.c
 * Author             : Simplified from WCH IAP example
 * Version            : V1.0.0
 * Date               : 2024/12/19
 * Description        : Simple IAP jump functionality for bootloader
 *********************************************************************************
 * Copyright (c) 2021 Nanjing Qinheng Microelectronics Co., Ltd.
 * Attention: This software (modified or not) and binary are used for
 * microcontroller manufactured by Nanjing Qinheng Microelectronics.
 *******************************************************************************/

#include "iap_jump.h"

/*********************************************************************
 * @fn      IAP_Jump_APP
 *
 * @brief   Start the Operation of jumping to user application
 *
 * @return  none
 */
void IAP_Jump_APP(void) {
   // lite_puts ("Jump attempt...\n");
    
    /* Jump Code for RISC-V core */
#if DEF_CORE_TYPE == DEF_CORE_RV
    /* Code for Jump, Enable Chip soft reset interrupt, jump to application code in soft reset interrupt */
    NVIC_EnableIRQ(Software_IRQn);
    NVIC_SetPendingIRQ(Software_IRQn);
#endif
    
    /* Should not reach here */
    while(1) {
     //   lite_puts ("Jump failed.");
        /* Minimal error handling without debug output */
    }
}