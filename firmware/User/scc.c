#include "scc.h"
#include "emu2212.h"
#include "utils.h"
#include "scc.h"

void TIM6_IRQHandler (void) __attribute__ ((interrupt ("WCH-Interrupt-fast")));

/* Global define */
CircularBuffer cb;
uint32_t Dual_DAC_Value[1];


/* Global Variable */
SCC *scc;

void SCC_Init (void) {
    scc = SCC_new (3580000, 22050);
    SCC_reset (scc);
    SCC_set_quality (scc, 0);
    SCC_set_type (scc, SCC_STANDARD);

    initBuffer (&cb);

    GPIO_SetBits (GPIOA, GPIO_Pin_4);

    GPIO_InitTypeDef GPIO_InitStructure = {0};
    TIM_TimeBaseInitTypeDef TIM_TimeBaseStructure = {0};
    DAC_InitTypeDef DAC_InitType = {0};

    RCC_APB2PeriphClockCmd (RCC_APB2Periph_GPIOA, ENABLE);
    RCC_APB1PeriphClockCmd (RCC_APB1Periph_DAC, ENABLE);

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_4 | GPIO_Pin_5;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AIN;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init (GPIOA, &GPIO_InitStructure);
    GPIO_SetBits (GPIOA, GPIO_Pin_4);

    DAC_InitType.DAC_Trigger = DAC_Trigger_None;
    DAC_InitType.DAC_WaveGeneration = DAC_WaveGeneration_None;
    DAC_InitType.DAC_LFSRUnmask_TriangleAmplitude = DAC_LFSRUnmask_Bit0;
    DAC_InitType.DAC_OutputBuffer = DAC_OutputBuffer_Disable;
    DAC_Init (DAC_Channel_1, &DAC_InitType);
    DAC_Cmd (DAC_Channel_1, ENABLE);

    DAC_SetChannel1Data (DAC_Align_12b_R, 0);


    RCC_APB1PeriphClockCmd (RCC_APB1Periph_TIM6, ENABLE);

    TIM_TimeBaseStructInit (&TIM_TimeBaseStructure);
    TIM_TimeBaseStructure.TIM_Period = 1;
    TIM_TimeBaseStructure.TIM_Prescaler = 3311;
    TIM_TimeBaseStructure.TIM_ClockDivision = TIM_CKD_DIV1;
    TIM_TimeBaseStructure.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseInit (TIM6, &TIM_TimeBaseStructure);

    TIM_ITConfig (TIM6, TIM_IT_Update, ENABLE);
    NVIC_EnableIRQ (TIM6_IRQn);
    NVIC_SetPriority (TIM6_IRQn, 0xC0);
    SetVTFIRQ ((u32)TIM6_IRQHandler, TIM6_IRQn, 1, ENABLE);

    TIM_Cmd (TIM6, ENABLE);
}

void SCC_HandleBufer() {
    uint32_t address_data = 0;
    while (pop (&cb, &address_data) == 0) {
        uint32_t address = (address_data >> 16) & 0xFFFF;  // Extract the higher 16 bits
        uint32_t data = address_data & 0xFFFF;
        SCC_write (scc, address, data);
    }
}

void TIM6_IRQHandler() {

    uint32_t DacVal = (((SCC_calc (scc) + 0x8000) >> 4) & 0xFFF);
    DAC_SetChannel1Data (DAC_Align_12b_R, DacVal);
    TIM_ClearITPendingBit (TIM6, TIM_IT_Update);
}