#include "debug.h"
#include "gpio.h"
#include "cart.h"
#include "set_memory_split.h"
#include "scc.h"
#include "MSXTerminal.h"
#include "usb_disk.h"

extern CartType type;

int main (void) {
    RCC_AdjustHSICalibrationValue (0x1F);
    NVIC_PriorityGroupConfig (NVIC_PriorityGroup_2);
    SystemCoreClockUpdate();
    Delay_Init();
    GPIO_Config();
    USB_Initialization();
    Init_Cart();

    if (type == MSXTERMINAL) {
        Init_MSXTerminal();
    }

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