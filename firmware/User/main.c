#include "debug.h"
#include "gpio.h"
#include "usb_host_iap.h"
#include "cart.h"
#include "set_memory_split.h"
#include "scc.h"
#include "MSXTerminal.h"

extern CartType type;

int main (void) {
    RCC_AdjustHSICalibrationValue (0x1F);
    NVIC_PriorityGroupConfig (NVIC_PriorityGroup_2);
    SystemCoreClockUpdate();
    Delay_Init();
    GPIO_Config();

    // Check for MSX reset early
    if (!GPIO_ReadInputDataBit (GPIOC, GPIO_Pin_6)) {
        PFIC->SCTLR |= (1 << 31);
    }

    // Check memory split 5 to 7th bit needs to be 111
    if ((OB->USER & 0b1110000) >> 4 != 0b111) {
        // Configure 288kb Flash + 32k RAM.
        SetSplit();
    }

    Init_Cart();

    if (type == MSXTERMINAL) {

        Init_MSXTerminal();
    }


    while (1) {
        // Check for MSX reset
        if (!GPIO_ReadInputDataBit (GPIOC, GPIO_Pin_6)) {
            PFIC->SCTLR |= (1 << 31);
        }
        switch (type) {
        case KonamiWithSCC:
            SCC_HandleBufer();
            break;

        case MSXTERMINAL:
            ProcessMSXTerminal();
            break;

        default:
            break;
        }
    }
}