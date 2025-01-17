#include "cart.h"

//GPIOE Pins    0 - 16      Address
//GPIOD Pins    0 - 8       Data

//GPIOB Pin     3           SLT   0x0008
//GPIOB Pin     4           WR    0x0010
//GPIOB Pin     5           RD    0x0020

//GPIOB Pin     6           CS2   0x0040
//GPIOB Pin     7           CS1   0x0080
//GPIOB Pin     8           CS12  0x0100

//GPIOB Pin     9           M1    0x0200
//GPIOB Pin     10          MREQ  0x0400
//GPIOB Pin     11          RESH  0x0800

//GPIOC Pin     6           RESET 0x0040
//GPIOC Pin     7           WAIT  0x0080
//GPIOC Pin     8           IORQ  0x0100
//GPIOC Pin     9           BDIR  0x0200

#pragma GCC push_options
#pragma GCC optimize ("Ofast")

//MSX State bank offsets.
struct MSXState
{
    uint32_t bankOffsets[16];
}state;

//global variables
uint8_t *restrict cartpnt = (uint8_t *) &__cart_section_start;
volatile struct MSXState * state_pointer;

//Config Cart emulation hardware.
void Init_Cart(void) {

state_pointer = &state;
cartpnt = (uint8_t *) &__cart_section_start;

 FLASH_Enhance_Mode(ENABLE);
    CartType volatile type;
    CART_CFG volatile *cfg ;
    //Read config from config flash location.
    uint8_t * restrict cfgpnt = (uint8_t *) &__cfg_section_start;
    cfg = (CART_CFG volatile *)cfgpnt;
    type = cfg->CartType;

 switch (type) {
    case ROM32k: 
        state.bankOffsets[0] = -0x4000;
        GPIO_WriteBit(GPIOA, GPIO_Pin_0, Bit_RESET); // binary 1 0001 
        NVIC_EnableIRQ(EXTI3_IRQn);
        SetVTFIRQ((u32) RunCart32k, EXTI3_IRQn, 0, ENABLE);
        break;
    case KonamiWithoutSCC:
        GPIO_WriteBit(GPIOA, GPIO_Pin_2, Bit_RESET); // binary 4 0100
        //configure initial banks state for Konami without SCC
        state.bankOffsets[2] = -0x4000;
        state.bankOffsets[3] = -0x8000;
        state.bankOffsets[4] = 0x0;
        state.bankOffsets[5] = 0x0;
        NVIC_EnableIRQ(EXTI3_IRQn);
        SetVTFIRQ((u32) RunKonamiWithoutSCC, EXTI3_IRQn, 0, ENABLE);
        break;
    case KonamiWithSCC:
        GPIO_WriteBit(GPIOA, GPIO_Pin_0, Bit_RESET); //binary 5 0101
        GPIO_WriteBit(GPIOA, GPIO_Pin_2, Bit_RESET);
       //configure initial banks state for Konami with SCC
        state.bankOffsets[2] = -0x4000; //0x5000
        state.bankOffsets[3] = -0x6000; //0x7000
        state.bankOffsets[4] = -0x8000; //0x9000
        state.bankOffsets[5] = -0xA000; //0xB000

        NVIC_EnableIRQ(EXTI3_IRQn);
        SetVTFIRQ((u32) RunKonamiWithSCC, EXTI3_IRQn, 0, ENABLE);
        break;
    case ASCII8k:
        GPIO_WriteBit(GPIOA, GPIO_Pin_1, Bit_RESET);  //binary 6 0110
        GPIO_WriteBit(GPIOA, GPIO_Pin_2, Bit_RESET);
        // configure initial banks state for ASCII8K
        state.bankOffsets[0] = -0x4000; //switch address 6000h  -0x4000; 
        state.bankOffsets[1] = -0x6000; //switch address 6800h  -0x6000; 
        state.bankOffsets[2] = -0x8000; //switch address 7000h  -0x8000; 
        state.bankOffsets[3] = -0xA000; //switch address 7800h  -0xA000; 

        NVIC_EnableIRQ(EXTI3_IRQn);
        SetVTFIRQ((u32) Run8kASCII, EXTI3_IRQn, 0, ENABLE);
        break;
    case ASCII16k:
        GPIO_WriteBit(GPIOA, GPIO_Pin_0, Bit_RESET);  //binary 7 0111
        GPIO_WriteBit(GPIOA, GPIO_Pin_1, Bit_RESET);
        GPIO_WriteBit(GPIOA, GPIO_Pin_2, Bit_RESET);
        state.bankOffsets[0] = -0x4000; 
        state.bankOffsets[8] = -0x8000; 
        NVIC_EnableIRQ(EXTI3_IRQn);
        SetVTFIRQ((u32) Run16kASCII, EXTI3_IRQn, 0, ENABLE);
        break;
    default:
        NVIC_EnableIRQ(EXTI3_IRQn);
        SetVTFIRQ((u32) RunCart32k, EXTI3_IRQn, 0, ENABLE);
    }
}

#pragma GCC push_options
#pragma GCC optimize ("Ofast")

void RunCart32k(void) {

    //check if SLT and CS12 lines are enabled
    if(((uint16_t) GPIOB->INDR & 0x108) == 0) {
        //Read address lines and load data from flash for that address, and load data to data port gpio
        GPIOD->OUTDR = *(cartpnt + ((uint16_t) GPIOE->INDR +state.bankOffsets[0]));
        //set data port as pull push to place data on the bus
        GPIOD->CFGLR = 0x33333333;
        //wait till end of read cycle
        while ((GPIOB->INDR & 0x100) == 0 ) {};
        //change data port back to input / floating
        GPIOD->CFGLR =  0x44444444;
    }
    EXTI->INTFR = EXTI_Line3;
    return;
}


void RunKonamiWithoutSCC(void) {
volatile uint32_t address;
volatile uint8_t WriteData;
volatile uint16_t portb;

    // loop - we need to do wait for potential write
    while (((portb = (uint16_t)GPIOB->INDR) & 0x108) == 0) {
    
        WriteData = ((uint16_t)GPIOD->INDR); //read data from data bus - can I read early to avoid
        address = GPIOE->INDR;

        //Handle read cycle
        if( (portb & 0x0020) == 0 )
        {
            //read data from flash
            GPIOD->OUTDR = *(cartpnt + state_pointer->bankOffsets[address>>13] + address );
            //Change GPIO Mode from Input (floating) to PushPull
            GPIOD->CFGLR = 0x33333333;
            //wait till end of slot enable signal
            while ((GPIOB->INDR & 0x100) == 0 ) {};
            //change back GPIO to floating (input mode)
            GPIOD->CFGLR = 0x44444444;
            EXTI->INTFR = EXTI_Line3;
            return;
        }

        //Handle write
        if( (portb & 0x0010) == 0 )//check for writes
        {
            state_pointer->bankOffsets[address>>13] = (WriteData<<13) - address;
            EXTI->INTFR = EXTI_Line3;
            return;
        }
    }
    EXTI->INTFR = EXTI_Line3;
    return;
}

void RunKonamiWithSCC(void) {
volatile uint32_t address;
volatile uint8_t WriteData;
volatile uint16_t portb;

    //read address
    address = GPIOE->INDR;
    //read data from the data bus very early to give as much time in write cycle as possible
    WriteData = ((uint16_t)GPIOD->INDR);

    // loop - we need to do wait for potential write
    while (((portb = (uint16_t)GPIOB->INDR) & 0x108) == 0  ) 
    {

        //Handle read cycle
        if( (portb & 0x0020) == 0 )//check for reads
        {         
            //Decode addresses and Load data from flash memory 
            GPIOD->OUTDR = *( cartpnt + (state_pointer->bankOffsets[address>>13] + address) );
            //Change GPIO Mode from Input (floating) to PushPull
            GPIOD->CFGLR = 0x33333333;
            //wait till cs12 is enabled
            while ((GPIOB->INDR & 0x100) == 0 ) {};
            GPIOD->CFGLR = 0x44444444;
            EXTI->INTFR = EXTI_Line3;
            return;
        }

        //Handle Write cycle
        if( (portb & 0x0010) == 0 )//check for writes
        {
            //Decode addresses
            state_pointer->bankOffsets[address>>13] = (WriteData << 13) - (address - 0x1000);  
            EXTI->INTFR = EXTI_Line3;
            return;
        }
    }
    EXTI->INTFR = EXTI_Line3;
    return;
}

void Run8kASCII(void) {
volatile uint32_t address;
volatile uint8_t WriteData;
volatile uint16_t portb;

    // loop - we need to do wait for potential write
    while (((portb = (uint16_t)GPIOB->INDR) & 0x108) == 0) 
    {
        //Read address lines
        address = GPIOE->INDR;
        if( (portb & 0x020) == 0 ) //check for reads
        {
            //Decode mappings and Load data from flash to GPIO port
            GPIOD->OUTDR = *( cartpnt + state_pointer->bankOffsets[(((address >>12) -4) >>1)] + address ); 
            //Change GPIO Mode from Input (floating) to PushPull
            GPIOD->CFGLR = 0x33333333;
            //wait till cs12 is enabled - end of cycle
            while ((GPIOB->INDR & 0x100) == 0 ) {};
            // Change GPIO Mode back to Input Mode - floating
            GPIOD->CFGLR = 0x44444444;
           // Escape while loop early
            EXTI->INTFR = EXTI_Line3;
            return;
        }

        //Detect writes inside SLT enable
        if( (portb & 0x010) == 0 ) //check for writes
        {
            //Read data from the bus
            WriteData = ((uint8_t)GPIOD->INDR);
            int slot = (((address >>11) &0x3));
            state_pointer->bankOffsets[slot] = (WriteData<<13) - (0x4000 + (0x2000 * slot));
            EXTI->INTFR = EXTI_Line3;
            return;
        }
    }
    EXTI->INTFR = EXTI_Line3;
    return;
}

void Run16kASCII(void) {
volatile uint32_t address;
volatile uint8_t WriteData;
volatile uint16_t portb;

    // loop - we need to do wait for potential write
    while (((portb = (uint16_t)GPIOB->INDR) & 0x108) == 0) 
    {
        if( (portb & 0x0020) == 0 ) //check for reads
        {
            // ( (address >> 12) & 0x8 ) selects location 0 when address is less than 0x7FFF and 8 when 0x8000 or above. It is to line up with write logic.
            address = (uint16_t) GPIOE->INDR;
            GPIOD->OUTDR = *( cartpnt + state_pointer->bankOffsets[( (address >> 12) & 0x8 )] + address );
            
            //Change GPIO Mode from Input (floating) to PushPull
            GPIOD->CFGLR = 0x33333333;
            //wait till cs12 is enabled - end of cycle
            while ((GPIOB->INDR & 0x0100) == 0 ) {};
            // Change GPIO Mode back to Input Mode - floating
            GPIOD->CFGLR = 0x44444444;
            //clear the IRQ flag and escape while loop early
            EXTI->INTFR = EXTI_Line3;
            return;
        }

        if( (portb & 0x0010) == 0 ) //check for writes
        {
            address = (uint16_t) GPIOE->INDR;
            WriteData = ((uint16_t)GPIOD->INDR); //read data from data bus
            // ((address >>12) + 1) & 0x8 - ensures that bank switching address 0x6000 that it is selecting Bank 0 and when it is >0x7000 it selects location 8
            //This logic is designed to line up with read section, it trasfroms locations 
            // Branchless programming - ( 0x4000 + ((0x4000) & -((((address>>12) +1 ) & 0x8)  != 0)) );   
            // It is to provide 0x4000 address when address 0x6000 is written to and 0x8000 when 0x7000 or above is acessed.
            state_pointer->bankOffsets[(((address>>12) +1 ) & 0x8) ] =  (WriteData<<14) - ( 0x4000 + ((0x4000) & -((((address>>12) +1 ) & 0x8)  != 0)) );           
            EXTI->INTFR = EXTI_Line3;
            return;              
        }
    }
    EXTI->INTFR = EXTI_Line3;
    return;
}