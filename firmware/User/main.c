#include "debug.h"
#include "gpio.h"
#include "cart.h"
#include "set_memory_split.h"
#include "scc.h"
#include "MSXTerminal.h"
#include "usb_disk.h"
#include "nextor.h"

extern CartType type;

int main (void) {
    RCC_AdjustHSICalibrationValue (0x1F);
    NVIC_PriorityGroupConfig (NVIC_PriorityGroup_2);
    SystemCoreClockUpdate();
    Delay_Init();
    GPIO_Config();
    VBUS_Enable();   // CH217K: drive PB12 (EN#) low to power the USB host port
    USB_Initialization();
    Init_Cart();

    if (type == MSXTERMINAL) {
        Init_MSXTerminal();
    }

    uint8_t vbus_faulted = 0;

    while (1) {
        // Check for MSX reset
        if (!GPIO_ReadInputDataBit (GPIOC, GPIO_Pin_6)) {
            Delay_Ms (50);
            PFIC->SCTLR |= (1 << 31);
        }

        // CH217K over-current / over-temperature fault: cut VBUS and fast-flash
        // the activity LED (~200 ms) to signal the fault. Latched until the MSX
        // reset reboots the cartridge.
        if (!vbus_faulted && VBUS_Fault()) {
            vbus_faulted = 1;
            VBUS_Disable();
        }
        if (vbus_faulted) {
            GPIO_WriteBit (GPIOA, GPIO_Pin_8, Bit_RESET);  // LED on (active-low)
            Delay_Ms (100);
            GPIO_WriteBit (GPIOA, GPIO_Pin_8, Bit_SET);    // LED off
            Delay_Ms (100);
            continue;
        }

        if (type == MSXTERMINAL) {
            ProcessMSXTerminal();
        } else if (type == NEXTOR) {
            Nextor_Service();
        }
    }
}