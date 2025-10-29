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
#include "iap_jump.h"

/* Global typedef */

/* Global define */

/* Global Variable */


/*********************************************************************
 * @fn      copy_firmware_from_cart
 *
 * @brief   Copy firmware from FLASHCART to FLASH firmware area
 *
 * @return  none
 */
void copy_firmware_from_cart(void) {
    uint32_t src_addr = 0x08008000;  // FLASHCART origin
    uint32_t dest_addr = 0x08000C00; // FLASH firmware origin
    uint32_t firmware_size = 0x7300; // FLASH firmware length (29440 bytes)
    
    lite_puts("Starting firmware copy...\n");
    
    // Unlock flash for writing
    FLASH_Unlock_Fast();
    
    // Erase firmware area first
    lite_puts("Erasing firmware area...\n");
    FLASH_ROM_ERASE(dest_addr, firmware_size);
    
    // Copy data in chunks (use smaller chunks to avoid large stack usage)
    uint32_t chunk_size = 256; // 256 bytes per chunk
    uint32_t buffer[64]; // 256 bytes buffer (64 x 4-byte words)
    
    lite_puts("Copying firmware data...\n");
    for (uint32_t offset = 0; offset < firmware_size; offset += chunk_size) {
        // Read from source (FLASHCART)
        uint32_t *src_ptr = (uint32_t *)(src_addr + offset);
        
        // Copy to buffer
        for (int i = 0; i < 64; i++) {
            buffer[i] = src_ptr[i];
        }
        
        // Write to destination (FLASH firmware area)
        FLASH_ROM_WRITE(dest_addr + offset, buffer, chunk_size);
    }
    
    // Lock flash
    FLASH_Lock_Fast();
    
    lite_puts("Firmware copy completed!\n");
    
    // Update configuration to clear firmware flashing flag
    lite_puts("Updating configuration...\n");
    uint32_t cfg_address = 0x08007F00;
    uint32_t cfg_buffer[64]; // 256 bytes config buffer
    
    // Read current configuration
    uint32_t *cfg_src = (uint32_t *)cfg_address;
    for (int i = 0; i < 64; i++) {
        cfg_buffer[i] = cfg_src[i];
    }
    
    // Change type from 0xFF to 0x00
    cfg_buffer[0] = 0x04;
    
    // Write updated configuration back to flash
    FLASH_Unlock_Fast();
    FLASH_ROM_ERASE(cfg_address, 256);  // Erase config page
    FLASH_ROM_WRITE(cfg_address, cfg_buffer, 256);  // Write updated config
    FLASH_Lock_Fast();
    
    lite_puts("Configuration updated!\n");
}

/*********************************************************************
 * @fn      main
 *
 * @brief   Main program.
 *
 * @return  none
 */
int main (void) {
    NVIC_PriorityGroupConfig (NVIC_PriorityGroup_2);
    SystemCoreClockUpdate();
    // Delay_Init();
    USART_Printf_Init (115200);
   // lite_puts ("SystemClk:");
   // lite_put_dec (SystemCoreClock);
    // lite_puts("\r\n");
    // lite_puts("ChipID:");
    // lite_put_hex(DBGMCU_GetCHIPID(), 8);
    // lite_puts("\r\n");
    lite_puts ("Lightweight bootloader ready\n");
    
    /* Read configuration from flash */
    uint32_t cfg_address = 0x08007F00;
    uint32_t *cfg = (uint32_t *)cfg_address;
    
    /* Check if type is 0xFF (firmware flashing) */
    if (cfg[0] == 0x09) {
        lite_puts ("firmware flashing\n");
        
        /* Copy firmware from FLASHCART to FLASH firmware area */
        copy_firmware_from_cart();
        
        /* Reset system to boot with new firmware */
        lite_puts("Resetting system...\n");
        NVIC_SystemReset();
        
    }
    else{

        IAP_Jump_APP();
    }
    /* Small delay to ensure bootloader initialization is complete */
  //  Delay_Ms(100);
    
    /* Jump to application firmware unconditionally */
    

    while (1) {
    }
}
