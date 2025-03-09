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
    USART_Printf_Init (115200);

    printf ("SystemClk:%d\r\n", SystemCoreClock);
    printf ("ChipID:%08x\r\n", DBGMCU_GetCHIPID());

    printf ("GPIO Toggle TEST\r\n");


    RCC_APB1PeriphClockCmd (RCC_APB1Periph_TIM2, ENABLE);
    TIM_TimeBaseInitTypeDef TIM_TimeBaseStructure = {0};
    TIM_TimeBaseStructInit (&TIM_TimeBaseStructure);
    TIM_TimeBaseStructure.TIM_Period = 0xFFFF;
    TIM_TimeBaseStructure.TIM_Prescaler = SystemCoreClock / 1000000 - 1;
    TIM_TimeBaseStructure.TIM_ClockDivision = TIM_CKD_DIV1;
    TIM_TimeBaseStructure.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseInit (TIM2, &TIM_TimeBaseStructure);

    TIM_Cmd (TIM2, ENABLE);

    // Check for MSX reset early
    if (!GPIO_ReadInputDataBit (GPIOC, GPIO_Pin_6)) {
        PFIC->SCTLR |= (1 << 31);
    }

    // Check memory split 5 to 7th bit needs to be 111
    // if ((OB->USER & 0b1110000) >> 4 != 0b111) {
    //     // Configure 288kb Flash + 32k RAM.
    //     SetSplit();
    // }
    SCC_Init();
    Init_Cart (0);

    printf ("Test");

    if (type == MSXTERMINAL) {

        Init_MSXTerminal();
    }


    while (1) {
        // Check for MSX reset
        if (!GPIO_ReadInputDataBit (GPIOC, GPIO_Pin_6)) {
            PFIC->SCTLR |= (1 << 31);
        }
        SCC_HandleBufer();
        switch (type) {
        case KonamiWithSCC:

            break;

        case MSXTERMINAL:
            ProcessMSXTerminal();
            break;

        default:
            break;
        }
    }
}