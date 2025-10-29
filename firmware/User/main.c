#include "debug.h"
#include "gpio.h"
#include "cart.h"
#include "set_memory_split.h"
#include "scc.h"
#include "MSXTerminal.h"
#include "usb_disk.h"

extern CartType type;
extern uint16_t __cfg_section_start;

int main (void) {
    RCC_AdjustHSICalibrationValue (0x1F);
    NVIC_PriorityGroupConfig (NVIC_PriorityGroup_2);
    SystemCoreClockUpdate();
    Delay_Init();
    
    // Initialize lightweight printf for debug output
    USART_Printf_Init(115200);
        
    GPIO_Config();
    
    USB_Initialization();
    
    Init_Cart();

    if (type == MSXTERMINAL) {
        Init_MSXTerminal();
    }
    

        // Read and display CART_CFG values
    uint8_t *restrict cfgpnt = (uint8_t *)&__cfg_section_start;
    CART_CFG *cfg = (CART_CFG *)cfgpnt;
    
    lite_puts("\n=== CART_CFG Contents ===\n");
    lite_puts("Config address: 0x");
    lite_put_hex((uint32_t)cfgpnt, 8);
    lite_puts("\nCartType: ");
    lite_put_dec(cfg->CartType);
    lite_puts("\nCartSize: ");
    lite_put_dec(cfg->CartSize);
    lite_puts("\nFilename pointer: 0x");
    lite_put_hex((uint32_t)cfg->filename, 8);
    
    // Print raw bytes from config section to see actual layout
    lite_puts("\nRaw config data (first 32 bytes):\n");
    for (int i = 0; i < 32; i++) {
        if (i % 8 == 0) lite_puts("\n");
        lite_put_hex(cfgpnt[i], 2);
        lite_puts(" ");
    }
    
    // The filename likely starts at offset 8 (after CartType and CartSize)
    lite_puts("\nFilename (from offset 8): ");
    for (int i = 8; i < 72; i++) {  // Print up to 64 characters starting from offset 8
        uint8_t c = cfgpnt[i];
        if (c >= 32 && c <= 126) {  // Printable ASCII
            lite_putchar(c);
        } else if (c == 0) {
            break;  // Stop at null terminator if present
        } else {
            lite_putchar('.');  // Non-printable character
        }
    }
    
    // Display SCC status based on cart type
    lite_puts("\nSCC Status: ");
    if (type == KonamiWithSCC) {
        lite_puts("ENABLED (KonamiWithSCC)");
    } else if (type == KonamiWithSCCNOSCC) {
        lite_puts("DISABLED (KonamiWithSCCNOSCC)");
    } else {
        lite_puts("NOT APPLICABLE (");
        switch(type) {
            case ROM16k: lite_puts("ROM16k"); break;
            case ROM32k: lite_puts("ROM32k"); break;
            case ROM48k: lite_puts("ROM48k"); break;
            case KonamiWithoutSCC: lite_puts("KonamiWithoutSCC"); break;
            case ASCII8k: lite_puts("ASCII8k"); break;
            case ASCII16k: lite_puts("ASCII16k"); break;
            case NEO8: lite_puts("NEO8"); break;
            case NEO16: lite_puts("NEO16"); break;
            case MSXTERMINAL: lite_puts("MSXTERMINAL"); break;
            default: lite_puts("UNKNOWN"); break;
        }
        lite_puts(")");
    }
    
    lite_puts("\n========================\n");


    while (1) {
        // Check for MSX reset
        if (!GPIO_ReadInputDataBit (GPIOC, GPIO_Pin_6)) {
            Delay_Ms (50);
            PFIC->SCTLR |= (1 << 31);
        }
        if (type == MSXTERMINAL) {
            ProcessMSXTerminal();
        }
        
    }
}