#include "scc.h"
#include "emu2212.h"
#include "utils.h"
#include "scc.h"

void DMA2_Channel3_IRQHandler (void) __attribute__ ((interrupt ("WCH-Interrupt-fast")));

/* Global define */
CircularBuffer cb;
uint32_t Dual_DAC_Value[1];


/* Global Variable */
SCC *scc;

void SCC_Init (void) {
    scc = SCC_new (3554685, 112005);
    SCC_reset (scc);
    SCC_set_quality (scc, 0);
    SCC_set_type (scc, SCC_STANDARD);

    GPIO_SetBits (GPIOA, GPIO_Pin_4);
    initBuffer (&cb);
    GPIO_InitTypeDef GPIO_InitStructure = {0};
    DAC_InitTypeDef DAC_InitType = {0};

    RCC_APB2PeriphClockCmd (RCC_APB2Periph_GPIOA, ENABLE);
    RCC_APB1PeriphClockCmd (RCC_APB1Periph_DAC, ENABLE);

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_4 | GPIO_Pin_5;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AIN;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init (GPIOA, &GPIO_InitStructure);
    GPIO_SetBits (GPIOA, GPIO_Pin_4);

    DAC_InitType.DAC_Trigger = DAC_Trigger_T4_TRGO;
    DAC_InitType.DAC_WaveGeneration = DAC_WaveGeneration_None;
    DAC_InitType.DAC_LFSRUnmask_TriangleAmplitude = DAC_LFSRUnmask_Bit0;
    DAC_InitType.DAC_OutputBuffer = DAC_OutputBuffer_Disable;
    DAC_Init (DAC_Channel_1, &DAC_InitType);
    DAC_Init (DAC_Channel_2, &DAC_InitType);

    DAC_Cmd (DAC_Channel_1, ENABLE);
    DAC_Cmd (DAC_Channel_2, ENABLE);

    DAC_DMACmd (DAC_Channel_1, ENABLE);
    DAC_DMACmd (DAC_Channel_2, ENABLE);

    DAC_SetDualChannelData (DAC_Align_12b_R, 0x123, 0x321);


    DMA_InitTypeDef DMA_InitStructure = {0};
    RCC_AHBPeriphClockCmd (RCC_AHBPeriph_DMA2, ENABLE);

    DMA_StructInit (&DMA_InitStructure);
    DMA_InitStructure.DMA_PeripheralBaseAddr = (uint32_t) & (DAC->RD12BDHR);
    DMA_InitStructure.DMA_MemoryBaseAddr = (uint32_t)&Dual_DAC_Value;
    DMA_InitStructure.DMA_DIR = DMA_DIR_PeripheralDST;
    DMA_InitStructure.DMA_BufferSize = 1;
    DMA_InitStructure.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
    DMA_InitStructure.DMA_MemoryInc = DMA_MemoryInc_Enable;
    DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Word;
    DMA_InitStructure.DMA_MemoryDataSize = DMA_MemoryDataSize_Word;
    DMA_InitStructure.DMA_Mode = DMA_Mode_Circular;
    DMA_InitStructure.DMA_Priority = DMA_Priority_VeryHigh;
    DMA_InitStructure.DMA_M2M = DMA_M2M_Disable;

    DMA_Init (DMA2_Channel3, &DMA_InitStructure);
    DMA_Cmd (DMA2_Channel3, ENABLE);

    TIM_TimeBaseInitTypeDef TIM_TimeBaseStructure = {0};
    RCC_APB1PeriphClockCmd (RCC_APB1Periph_TIM4, ENABLE);

    TIM_TimeBaseStructInit (&TIM_TimeBaseStructure);
    TIM_TimeBaseStructure.TIM_Period = 1;
    TIM_TimeBaseStructure.TIM_Prescaler = 1286;
    TIM_TimeBaseStructure.TIM_ClockDivision = TIM_CKD_DIV1;
    TIM_TimeBaseStructure.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseInit (TIM4, &TIM_TimeBaseStructure);

    TIM_SelectOutputTrigger (TIM4, TIM_TRGOSource_Update);
    TIM_Cmd (TIM4, ENABLE);


    DMA_ITConfig (DMA2_Channel3, DMA_IT_TC, ENABLE);
    NVIC_EnableIRQ (DMA2_Channel3_IRQn);
    NVIC_SetPriority (DMA2_Channel3_IRQn, 0xC0);
    SetVTFIRQ ((u32)DMA2_Channel3_IRQHandler, DMA2_Channel3_IRQn, 1, ENABLE);
}

void SCC_HandleBufer() {
}

void DMA2_Channel3_IRQHandler() {

    if (DMA_GetITStatus (DMA2_IT_TC3)) {
        uint32_t address_data = 0;
        while (pop (&cb, &address_data) == 0) {
            uint32_t address = (address_data >> 16) & 0xFFFF;  // Extract the higher 16 bits
            uint32_t data = address_data & 0xFFFF;
            SCC_write (scc, address, data);
        }

        uint16_t calval = (SCC_calc (scc) + 0x8000);
        // scale to 12bit
        Dual_DAC_Value[0] = (calval * 4095) / 65535;
        DMA_ClearITPendingBit (DMA2_IT_TC3);
    }
}