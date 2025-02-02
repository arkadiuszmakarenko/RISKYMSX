#include "cart.h"
#include "scc.h"

// GPIOE Pins    0 - 16      Address
// GPIOD Pins    0 - 8       Data

// GPIOB Pin     3           SLT   0x0008
// GPIOB Pin     4           WR    0x0010
// GPIOB Pin     5           RD    0x0020

// GPIOB Pin     6           CS2   0x0040
// GPIOB Pin     7           CS1   0x0080
// GPIOB Pin     8           CS12  0x0100

// GPIOB Pin     9           M1    0x0200
// GPIOB Pin     10          MREQ  0x0400
// GPIOB Pin     11          RESH  0x0800

// GPIOC Pin     6           RESET 0x0040
// GPIOC Pin     7           WAIT  0x0080
// GPIOC Pin     8           IORQ  0x0100
// GPIOC Pin     9           BDIR  0x0200

#pragma GCC push_options
#pragma GCC optimize("Ofast")

extern CircularBuffer scb;
extern CircularBuffer icb;

static unsigned char msxterm[117] = {
    'A',
    'B',
    04,
    '@',
    '>',
    ' ',
    '2',
    0257,
    0363,
    '!',
    017,
    0,
    '"',
    0352,
    0363,
    '"',
    0351,
    0363,
    '>',
    01,
    0315,
    '_',
    0,
    '!',
    'm',
    '@',
    021,
    0,
    '8',
    01,
    010,
    0,
    0315,
    0134,
    0,
    '>',
    010,
    '!',
    03,
    033,
    0315,
    'M',
    0,
    '!',
    '8',
    '@',
    021,
    0,
    0340,
    01,
    '5',
    0,
    0325,
    0355,
    0260,
    0311,
    ':',
    0377,
    0177,
    0376,
    03,
    0312,
    0,
    0,
    0376,
    04,
    '(',
    026,
    0247,
    0314,
    ' ',
    0340,
    0304,
    0242,
    0,
    0315,
    0234,
    0,
    '(',
    0350,
    0315,
    0237,
    0,
    '2',
    0375,
    0177,
    030,
    0340,
    'v',
    0311,
    ':',
    0377,
    0177,
    '!',
    01,
    033,
    0315,
    'M',
    0,
    ':',
    0377,
    0177,
    '=',
    '-',
    0315,
    'M',
    0,
    030,
    0313,
    ' ',
    '0',
    '8',
    '<',
    '8',
    '0',
    ' ',
    0,
};

// MSX State bank offsets.
struct MSXState {
    uint32_t bankOffsets[16];
} state;

// global variables#
CartType volatile type;
volatile struct MSXState *state_pointer;
volatile uint8_t *restrict cartpnt;
volatile CircularBuffer *buf;
volatile CircularBuffer *sbuf;
volatile CircularBuffer *ibuf;

// Config Cart emulation hardware.
void Init_Cart (void) {

    state_pointer = &state;
    cartpnt = (uint8_t *)&__cart_section_start;
    buf = &cb;
    sbuf = &scb;
    ibuf = &icb;


    FLASH_Enhance_Mode (ENABLE);

    CART_CFG volatile *cfg;
    // Read config from config flash location.
    uint8_t *restrict cfgpnt = (uint8_t *)&__cfg_section_start;
    cfg = (CART_CFG volatile *)cfgpnt;
    type = cfg->CartType;

    if (GPIO_ReadInputDataBit (GPIOA, GPIO_Pin_8) != 0) {
        type = MSXTERMINAL;
    }

    switch (type) {
    case ROM32k:
        state.bankOffsets[0] = -0x4000;
        GPIO_WriteBit (GPIOA, GPIO_Pin_0, Bit_RESET);  // 1 - 0001
        NVIC_EnableIRQ (EXTI3_IRQn);
        SetVTFIRQ ((u32)RunCart32k, EXTI3_IRQn, 0, ENABLE);
        break;
    case ROM48k:
        GPIO_WriteBit (GPIOA, GPIO_Pin_1, Bit_RESET);  // 2 - 0010
        NVIC_EnableIRQ (EXTI3_IRQn);
        SetVTFIRQ ((u32)RunCart48k, EXTI3_IRQn, 0, ENABLE);
        break;
    case KonamiWithoutSCC:
        GPIO_WriteBit (GPIOA, GPIO_Pin_0, Bit_RESET);  // 3 - 0011
        GPIO_WriteBit (GPIOA, GPIO_Pin_1, Bit_RESET);

        // configure initial banks state for Konami without SCC
        state.bankOffsets[2] = -0x4000;
        state.bankOffsets[3] = -0x8000;
        state.bankOffsets[4] = 0x0;
        state.bankOffsets[5] = 0x0;
        NVIC_EnableIRQ (EXTI3_IRQn);
        SetVTFIRQ ((u32)RunKonamiWithoutSCC, EXTI3_IRQn, 0, ENABLE);
        break;
    case KonamiWithSCC:
        SCC_Init();
        GPIO_WriteBit (GPIOA, GPIO_Pin_2, Bit_RESET);  // 4 - 0100
        // configure initial banks state for Konami with SCC
        state.bankOffsets[2] = -0x4000;  // 0x5000
        state.bankOffsets[3] = -0x6000;  // 0x7000
        state.bankOffsets[4] = -0x8000;  // 0x9000
        state.bankOffsets[5] = -0xA000;  // 0xB000

        NVIC_EnableIRQ (EXTI3_IRQn);
        NVIC_SetPriority (EXTI3_IRQn, 0);
        SetVTFIRQ ((u32)RunKonamiWithSCC, EXTI3_IRQn, 0, ENABLE);
        break;
    case KonamiWithSCCNOSCC:
        GPIO_WriteBit (GPIOA, GPIO_Pin_0, Bit_RESET);  // binary 5 0101
        GPIO_WriteBit (GPIOA, GPIO_Pin_2, Bit_RESET);

        // configure initial banks state for Konami with SCC
        state.bankOffsets[2] = -0x4000;  // 0x5000
        state.bankOffsets[3] = -0x6000;  // 0x7000
        state.bankOffsets[4] = -0x8000;  // 0x9000
        state.bankOffsets[5] = -0xA000;  // 0xB000

        NVIC_EnableIRQ (EXTI3_IRQn);
        NVIC_SetPriority (EXTI3_IRQn, 0);
        SetVTFIRQ ((u32)RunKonamiWithSCCNOSCC, EXTI3_IRQn, 0, ENABLE);
        break;

    case ASCII8k:
        GPIO_WriteBit (GPIOA, GPIO_Pin_1, Bit_RESET);  //  6 - 0110
        GPIO_WriteBit (GPIOA, GPIO_Pin_2, Bit_RESET);

        // configure initial banks state for ASCII8K
        state.bankOffsets[0] = -0x4000;  // switch address 6000h  -0x4000;
        state.bankOffsets[1] = -0x6000;  // switch address 6800h  -0x6000;
        state.bankOffsets[2] = -0x8000;  // switch address 7000h  -0x8000;
        state.bankOffsets[3] = -0xA000;  // switch address 7800h  -0xA000;

        NVIC_EnableIRQ (EXTI3_IRQn);
        SetVTFIRQ ((u32)Run8kASCII, EXTI3_IRQn, 0, ENABLE);
        break;
    case ASCII16k:
        GPIO_WriteBit (GPIOA, GPIO_Pin_0, Bit_RESET);  //  7 - 0111
        GPIO_WriteBit (GPIOA, GPIO_Pin_1, Bit_RESET);
        GPIO_WriteBit (GPIOA, GPIO_Pin_2, Bit_RESET);
        state.bankOffsets[0] = -0x4000;
        state.bankOffsets[8] = -0x8000;
        NVIC_EnableIRQ (EXTI3_IRQn);
        SetVTFIRQ ((u32)Run16kASCII, EXTI3_IRQn, 0, ENABLE);
        break;

    case NEO8:
        GPIO_WriteBit (GPIOA, GPIO_Pin_3, Bit_RESET);  // 8 - 1000

        state.bankOffsets[0] = 0;
        state.bankOffsets[1] = 0;
        state.bankOffsets[2] = 0;
        NVIC_EnableIRQ (EXTI3_IRQn);
        SetVTFIRQ ((u32)RunNEO8, EXTI3_IRQn, 0, ENABLE);
        break;

    case NEO16:
        GPIO_WriteBit (GPIOA, GPIO_Pin_0, Bit_RESET);  // 9 - 1001
        GPIO_WriteBit (GPIOA, GPIO_Pin_3, Bit_RESET);

        state.bankOffsets[0] = 0;
        state.bankOffsets[1] = 0;
        state.bankOffsets[2] = 0;
        NVIC_EnableIRQ (EXTI3_IRQn);
        SetVTFIRQ ((u32)RunNEO16, EXTI3_IRQn, 0, ENABLE);
        break;

    case MSXTERMINAL:
        GPIO_WriteBit (GPIOA, GPIO_Pin_0, Bit_RESET);  //  7 - 0111
        GPIO_WriteBit (GPIOA, GPIO_Pin_1, Bit_RESET);
        GPIO_WriteBit (GPIOA, GPIO_Pin_2, Bit_RESET);
        GPIO_WriteBit (GPIOA, GPIO_Pin_3, Bit_RESET);
        NVIC_EnableIRQ (EXTI3_IRQn);
        SetVTFIRQ ((u32)RunMSXTerminal, EXTI3_IRQn, 0, ENABLE);
        break;


    default:
        break;
    }
}

void RunCart32k (void) {

    // check if SLT and CS12 lines are enabled
    if (((uint16_t)GPIOB->INDR & 0x0008) == 0) {
        // Read address lines and load data from flash for that address, and load data to data port gpio
        GPIOD->OUTDR = *(cartpnt + ((uint16_t)GPIOE->INDR + state.bankOffsets[0]));
        // set data port as pull push to place data on the bus
        GPIOD->CFGLR = 0x33333333;
        // wait till end of read cycle
        while ((GPIOB->INDR & 0x0008) == 0) { };
        // change data port back to input / floating
        GPIOD->CFGLR = 0x44444444;
    }
    EXTI->INTFR = EXTI_Line3;
    return;
}

void RunCart48k (void) {

    // check if SLT and CS12 lines are enabled
    if (((uint16_t)GPIOB->INDR & 0x0008) == 0) {
        // Read address lines and load data from flash for that address, and load data to data port gpio
        GPIOD->OUTDR = *(cartpnt + ((uint16_t)GPIOE->INDR));
        // set data port as pull push to place data on the bus
        GPIOD->CFGLR = 0x33333333;
        // wait till end of read cycle
        while ((GPIOB->INDR & 0x0008) == 0) { };
        // change data port back to input / floating
        GPIOD->CFGLR = 0x44444444;
    }
    EXTI->INTFR = EXTI_Line3;
    return;
}

void RunKonamiWithoutSCC (void) {
    volatile uint16_t address;
    volatile uint8_t WriteData;


    // clear interrupt flag
    EXTI->INTFR = EXTI_Line3;
    address = GPIOE->INDR;
    WriteData = ((uint16_t)GPIOD->INDR);  // read data from data bus - can I read early to avoid

    // loop - we need to do wait for potential write
    while (((uint16_t)GPIOB->INDR & 0x0008) == 0) {
        // Handle read cycle
        if (((uint16_t)GPIOB->INDR & 0x0020) == 0)  // check for reads
        {
            // read data from flash
            GPIOD->OUTDR = *(cartpnt + state_pointer->bankOffsets[address >> 13] + address);
            // Change GPIO Mode from Input (floating) to PushPull
            GPIOD->CFGLR = 0x33333333;
            // wait till end of slot enable signal
            while ((GPIOB->INDR & 0x0008) == 0) { };
            // change back GPIO to floating (input mode)
            GPIOD->CFGLR = 0x44444444;

            return;
        }

        // Handle write
        if ((((uint16_t)GPIOB->INDR & 0x0010) == 0) && ((uint16_t)GPIOB->INDR & 0x0200))  // check for writes (WE and not M1)
        {
            switch (address) {
            case 0x6000: state_pointer->bankOffsets[3] = (WriteData << 13) - 0x6000; break;
            case 0x8000: state_pointer->bankOffsets[4] = (WriteData << 13) - 0x8000; break;
            case 0xA000: state_pointer->bankOffsets[5] = (WriteData << 13) - 0xA000; break;
            }
            return;
        }
    }
    return;
}

void RunKonamiWithSCC (void) {
    volatile uint16_t address;
    volatile uint8_t WriteData;

    // clear interrupt flag
    EXTI->INTFR = EXTI_Line3;
    // read address
    address = (uint16_t)GPIOE->INDR;
    // read data from the data bus very early to give as much time in write cycle as possible
    WriteData = ((uint16_t)GPIOD->INDR);

    // loop - we need to do wait for potential write

    // Handle read cycle
    if (((uint16_t)GPIOB->INDR & 0x0020) == 0)  // check for reads
    {
        // Decode addresses and Load data from flash memory
        GPIOD->OUTDR = *(cartpnt + (state_pointer->bankOffsets[address >> 13] + address));
        // Change GPIO Mode from Input (floating) to PushPull
        GPIOD->CFGLR = 0x33333333;
        // wait till cs12 is enabled
        while ((GPIOB->INDR & 0x0008) == 0) { };
        GPIOD->CFGLR = 0x44444444;
        return;
    }
    while (((uint16_t)GPIOB->INDR & 0x0008) == 0) {
        // Handle Write cycle
        if ((((uint16_t)GPIOB->INDR & 0x0010) == 0))  // check for writes (WE and not M1)
        {
            if (address > 0xB000)
                return;
            state_pointer->bankOffsets[address >> 13] = (WriteData << 13) - (address - 0x1000);
            uint8_t next;
            if (buf->head + 1 >= BUFFER_SIZE) {
                next = 0;
            } else {
                next = buf->head + 1;
            }
            buf->buffer[buf->head] = (((uint32_t)address << 16) | WriteData);
            buf->head = next;

            return;
        }
    }

    return;
}

void RunKonamiWithSCCNOSCC (void) {
    volatile uint16_t address;
    volatile uint8_t WriteData;

    // clear interrupt flag
    EXTI->INTFR = EXTI_Line3;
    // read address
    address = (uint16_t)GPIOE->INDR;
    // read data from the data bus very early to give as much time in write cycle as possible
    WriteData = ((uint16_t)GPIOD->INDR);

    // loop - we need to do wait for potential write
    while (((uint16_t)GPIOB->INDR & 0x0008) == 0) {
        // Handle read cycle
        if (((uint16_t)GPIOB->INDR & 0x0020) == 0)  // check for reads
        {
            // Decode addresses and Load data from flash memory
            GPIOD->OUTDR = *(cartpnt + (state_pointer->bankOffsets[address >> 13] + address));
            // Change GPIO Mode from Input (floating) to PushPull
            GPIOD->CFGLR = 0x33333333;
            // wait till cs12 is enabled
            while ((GPIOB->INDR & 0x0008) == 0) { };
            GPIOD->CFGLR = 0x44444444;
            return;
        }

        // Handle Write cycle
        if ((((uint16_t)GPIOB->INDR & 0x0010) == 0) && ((uint16_t)GPIOB->INDR & 0x0200))  // check for writes (WE and not M1)
        {
            if (address >= 0x5000 && address <= 0xB000) {
                state_pointer->bankOffsets[address >> 13] = (WriteData << 13) - (address - 0x1000);
            }
            return;
        }
    }

    return;
}

void Run8kASCII (void) {
    volatile uint32_t address;
    volatile uint8_t WriteData;

    // clear interrupt flag
    EXTI->INTFR = EXTI_Line3;

    // Read address lines
    address = GPIOE->INDR;
    // Read data from the bus
    WriteData = ((uint8_t)GPIOD->INDR);

    // loop - we need to do wait for potential write
    while (((uint16_t)GPIOB->INDR & 0x0008) == 0) {
        // Handle read cycle
        if (((uint16_t)GPIOB->INDR & 0x0020) == 0)  // check for reads
        {
            // Decode mappings and Load data from flash to GPIO port
            GPIOD->OUTDR = *(cartpnt + state_pointer->bankOffsets[(((address >> 12) - 4) >> 1)] + address);
            // Change GPIO Mode from Input (floating) to PushPull
            GPIOD->CFGLR = 0x33333333;
            // wait till cs12 is enabled - end of cycle
            while ((GPIOB->INDR & 0x0008) == 0) { };
            // Change GPIO Mode back to Input Mode - floating
            GPIOD->CFGLR = 0x44444444;
            // Escape while loop early

            return;
        }

        // Detect writes inside SLT enable
        // Handle Write cycle
        if ((((uint16_t)GPIOB->INDR & 0x0010) == 0) && ((uint16_t)GPIOB->INDR & 0x0200))  // check for writes (WE and not M1)
        {

            // state_pointer->bankOffsets[slot] = (WriteData<<13) - (0x4000 + (0x2000 * slot));
            switch (address) {
            case 0x6000: state_pointer->bankOffsets[0] = (WriteData << 13) - 0x4000; break;
            case 0x6800: state_pointer->bankOffsets[1] = (WriteData << 13) - 0x6000; break;
            case 0x7000: state_pointer->bankOffsets[2] = (WriteData << 13) - 0x8000; break;
            case 0x7800: state_pointer->bankOffsets[3] = (WriteData << 13) - 0xA000; break;
            }
            return;
        }
    }
    return;
}

void Run16kASCII (void) {
    volatile uint32_t address;
    volatile uint8_t WriteData;
    EXTI->INTFR = EXTI_Line3;

    address = (uint16_t)GPIOE->INDR;
    WriteData = ((uint16_t)GPIOD->INDR);  // read data from data bus

    // loop - we need to do wait for potential write
    while (((uint16_t)GPIOB->INDR & 0x0008) == 0) {
        if (((uint16_t)GPIOB->INDR & 0x0020) == 0)  // check for reads
        {
            // ( (address >> 12) & 0x8 ) selects location 0 when address is less than 0x7FFF and 8 when 0x8000 or above. It is to line up with write logic.
            GPIOD->OUTDR = *(cartpnt + state_pointer->bankOffsets[((address >> 12) & 0x8)] + address);

            // Change GPIO Mode from Input (floating) to PushPull
            GPIOD->CFGLR = 0x33333333;
            // wait till cs12 is enabled - end of cycle
            while ((GPIOB->INDR & 0x0008) == 0) { };
            // Change GPIO Mode back to Input Mode - floating
            GPIOD->CFGLR = 0x44444444;
            // clear the IRQ flag and escape while loop early

            return;
        }

        // Handle Write cycle
        if ((((uint16_t)GPIOB->INDR & 0x0010) == 0) && ((uint16_t)GPIOB->INDR & 0x0200))  // check for writes (WE and not M1)
        {
            switch (address) {
            case 0x6000: state_pointer->bankOffsets[0] = (WriteData << 14) - 0x4000; break;

            case 0x7000:
            case 0x77FF: state_pointer->bankOffsets[8] = (WriteData << 14) - 0x8000; break;
            }
            return;
        }
    }
    return;
}

void RunNEO16 (void) {
    volatile uint16_t address;
    volatile uint8_t WriteData;

    // clear interrupt flag
    EXTI->INTFR = EXTI_Line3;
    // read address
    address = (uint16_t)GPIOE->INDR;
    // read data from the data bus very early to give as much time in write cycle as possible
    WriteData = ((uint16_t)GPIOD->INDR);

    if (((uint16_t)GPIOB->INDR & 0x0020) == 0)  // check for reads
    {
        GPIOD->CFGLR = 0x33333333;

        uint8_t bank = address >> 14;
        if (bank > 2) {
            GPIOD->OUTDR = 0xFF;  // skip
        } else {
            uint32_t offset = ((state_pointer->bankOffsets[bank]) << 14) + (address & 0x3FFF);
            GPIOD->OUTDR = *(cartpnt + offset);
        }

        while ((GPIOB->INDR & 0x0008) == 0) { };
        GPIOD->CFGLR = 0x44444444;
        return;
    }
    while (((uint16_t)GPIOB->INDR & 0x0008) == 0) {
        if ((((uint16_t)GPIOB->INDR & 0x0010) == 0))  // check for writes (WE and not M1)
        {
            if (address > 0xB000)
                return;

            uint8_t bank = ((address >> 12) & 0x03) - 1;
            if (bank > 2)
                return;       // skip
            if (address & 1)  // Set bank register MSB
                state_pointer->bankOffsets[bank] = ((WriteData & 0x0F) << 8) | (state_pointer->bankOffsets[bank] & 0x00FF);
            else              // Set bank register LSB
                state_pointer->bankOffsets[bank] = (state_pointer->bankOffsets[bank] & 0xFF00) | (WriteData);

            return;
        }
    }

    return;
}

void RunNEO8 (void) {
    volatile uint16_t address;
    volatile uint8_t WriteData;

    // clear interrupt flag
    EXTI->INTFR = EXTI_Line3;
    // read address
    address = (uint16_t)GPIOE->INDR;
    // read data from the data bus very early to give as much time in write cycle as possible
    WriteData = ((uint16_t)GPIOD->INDR);

    if (((uint16_t)GPIOB->INDR & 0x0020) == 0)  // check for reads
    {
        GPIOD->CFGLR = 0x33333333;

        uint8_t bank = address >> 13;
        if (bank > 5) {
            GPIOD->OUTDR = 0xFF;
        } else {
            uint32_t offset = ((state_pointer->bankOffsets[bank]) << 13) + (address & 0x1FFF);
            GPIOD->OUTDR = *(cartpnt + offset);
        }
        while ((GPIOB->INDR & 0x0008) == 0) { };
        GPIOD->CFGLR = 0x44444444;
        return;
    }
    while (((uint16_t)GPIOB->INDR & 0x0008) == 0) {
        if ((((uint16_t)GPIOB->INDR & 0x0010) == 0))  // check for writes (WE and not M1)
        {
            if (address > 0xB000)
                return;
            uint8_t bank = ((address >> 11) & 0x07) - 2;
            if (bank > 5)
                return;       // skip
            if (address & 1)  // Set bank register MSB
                state_pointer->bankOffsets[bank] = ((WriteData & 0x0F) << 8) | (state_pointer->bankOffsets[bank] & 0x00FF);
            else              // Set bank register LSB
                state_pointer->bankOffsets[bank] = (state_pointer->bankOffsets[bank] & 0xFF00) | (WriteData);
            return;
        }
    }

    return;
}

void RunMSXTerminal (void) {
    volatile uint16_t address;
    // volatile uint8_t WriteData;
    // static uint8_t msxvar = 0x20;

    // clear interrupt flag
    EXTI->INTFR = EXTI_Line3;
    // read address
    address = (uint16_t)GPIOE->INDR;


    if (((uint16_t)GPIOB->INDR & 0x0020) == 0)  // check for reads
    {
        GPIOD->CFGLR = 0x33333333;

        if (address == 0x7FFF) {
            if (sbuf->head == sbuf->tail) {
                GPIOD->OUTDR = 0x00;
            } else {
                GPIOD->OUTDR = sbuf->buffer[sbuf->tail];
                sbuf->tail = (sbuf->tail + 1) & (BUFFER_SIZE - 1);
            }
        }


        // Read address lines and load data from flash for that address, and load data to data port gpio
        if (address >= 0x4000 && address < 0x4FFF) {
            GPIOD->OUTDR = *(msxterm + (address - 0x4000));
        }

        // wait till end of read cycle
        while ((GPIOB->INDR & 0x0008) == 0) { };
        // change data port back to input / floating
        GPIOD->CFGLR = 0x44444444;
    }

    while (((uint16_t)GPIOB->INDR & 0x0008) == 0) {
        if ((((uint16_t)GPIOB->INDR & 0x0010) == 0))  // check for writes (WE and not M1)
        {
            uint8_t WriteData = ((uint16_t)GPIOD->INDR);
            if (address == 0x7FFD) {
                uint8_t next = (ibuf->head + 1) & (BUFFER_MINI_SIZE - 1);
                ibuf->buffer[ibuf->head] = WriteData;
                ibuf->head = next;
            }

            return;
        }
    }


    return;
}

#pragma GCC pop_options
