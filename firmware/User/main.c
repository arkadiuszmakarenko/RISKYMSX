#include "debug.h"
#include "gpio.h"
#include "usb_host_iap.h"
#include "cart.h"
#include "set_memory_split.h"
#include "scc.h"

int main (void) {
    NVIC_PriorityGroupConfig (NVIC_PriorityGroup_2);
    SystemCoreClockUpdate();
    Delay_Init();
    GPIO_Config();

    // Check memory split 5 to 7th bit needs to be 111
    if ((OB->USER & 0b1110000) >> 4 != 0b111) {
        // Configure 288kb Flash + 32k RAM.
        SetSplit();
    }

    // Set up Cart in programming mode. It will program flash from 0x00008000 - 256K Flash Availiable.
    // It requires to configure chip during programming - for 288kb Flash + 32k RAM.
    if (GPIO_ReadInputDataBit (GPIOA, GPIO_Pin_8) == 1) {
        IAP_Initialization();
        GPIO_WriteBit (GPIOA, GPIO_Pin_0, Bit_RESET);
        GPIO_WriteBit (GPIOA, GPIO_Pin_1, Bit_RESET);
        GPIO_WriteBit (GPIOA, GPIO_Pin_2, Bit_RESET);
        GPIO_WriteBit (GPIOA, GPIO_Pin_3, Bit_RESET);
        Delay_Ms (300);
        GPIO_WriteBit (GPIOA, GPIO_Pin_1, Bit_SET);
        GPIO_WriteBit (GPIOA, GPIO_Pin_2, Bit_SET);
        GPIO_WriteBit (GPIOA, GPIO_Pin_3, Bit_SET);

        IAP_Configure ("/CART.*", 0x08008000, 0x08048000);
        // read and flash
        uint32_t writeStatus = 0;
        while (writeStatus == 0) {
            writeStatus = IAP_Main_Deal();
        }
    }

    Init_Cart();

    while (1) {
        if (GPIO_ReadInputDataBit (GPIOA, GPIO_Pin_8) == 1) {
            // force mpu reset to allow cart programming
            PFIC->SCTLR |= (1 << 31);
        }

        SCC_HandleBufer();
    }
}