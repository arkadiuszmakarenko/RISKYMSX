#include "gpio.h"

void GPIO_Config() {
    RCC->APB2PCENR |= RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB | RCC_APB2Periph_GPIOC | RCC_APB2Periph_GPIOD | RCC_APB2Periph_GPIOE | RCC_APB2Periph_AFIO;
    RCC->APB1PCENR |= RCC_APB1Periph_DAC;
    /*Configure GPIO pin Output Level */
    GPIOA->BSHR |= GPIO_Pin_0 | GPIO_Pin_1 | GPIO_Pin_2 | GPIO_Pin_3 | GPIO_Pin_8 | GPIO_Pin_9;

    // GPIOA->CFGLR = 0x44403333;
    GPIOA->CFGLR = 0;
    GPIOA->CFGLR |= GPIO_CFGLR_MODE0;
    GPIOA->CFGLR |= GPIO_CFGLR_MODE1;
    GPIOA->CFGLR |= GPIO_CFGLR_MODE2;
    GPIOA->CFGLR |= GPIO_CFGLR_MODE3;
    // set up DAC GPIO 4 - leave 0;
    GPIOA->CFGLR |= GPIO_CFGLR_CNF5_0;
    GPIOA->CFGLR |= GPIO_CFGLR_CNF6_0;
    GPIOA->CFGLR |= GPIO_CFGLR_CNF7_0;

    GPIO_InitTypeDef GPIO_InitStructure;
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_8;  //| GPIO_Pin_9;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init (GPIOA, &GPIO_InitStructure);

    // GPIO_InitStructure.GPIO_Pin = GPIO_Pin_10;
    // GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPD;
    // GPIO_Init (GPIOA, &GPIO_InitStructure);


    EXTI_InitTypeDef EXTI_InitStructure = {0};
    /* GPIOA ----> EXTI_Line0 */
    GPIO_EXTILineConfig (GPIO_PortSourceGPIOB, GPIO_PinSource3);
    EXTI_InitStructure.EXTI_Line = EXTI_Line3;
    EXTI_InitStructure.EXTI_Mode = EXTI_Mode_Interrupt;
    EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Falling;
    EXTI_InitStructure.EXTI_LineCmd = ENABLE;
    EXTI_Init (&EXTI_InitStructure);

    // Set up DAC to remove hiss
    DAC_InitTypeDef DAC_InitType = {0};

    DAC_InitType.DAC_Trigger = DAC_Trigger_T4_TRGO;
    DAC_InitType.DAC_WaveGeneration = DAC_WaveGeneration_None;
    DAC_InitType.DAC_LFSRUnmask_TriangleAmplitude = DAC_LFSRUnmask_Bit0;
    DAC_InitType.DAC_OutputBuffer = DAC_OutputBuffer_Enable;
    DAC_Init (DAC_Channel_1, &DAC_InitType);

    DAC_Cmd (DAC_Channel_1, ENABLE);
    DAC_DMACmd (DAC_Channel_1, ENABLE);
    DAC_SetChannel1Data (DAC_Align_12b_R, 0x00);
}
