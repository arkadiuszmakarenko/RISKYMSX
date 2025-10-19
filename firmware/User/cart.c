#include "cart.h"


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
extern CircularBuffer cb;
extern uint32_t enableTerminal;

static const uint8_t msxterm[213] = {
    'A',
    'B',
    '&',
    '@',
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    'R',
    'I',
    'S',
    'K',
    'Y',
    '_',
    'B',
    'O',
    'O',
    'T',
    0,
    '1',
    '.',
    '0',
    0,
    0,
    '>',
    04,
    '2',
    0376,
    0177,
    0311,
    '!',
    '3',
    '@',
    021,
    0,
    0340,
    01,
    0232,
    0,
    0325,
    0355,
    0260,
    0311,
    '>',
    06,
    0315,
    'A',
    01,
    0346,
    04,
    '2',
    0376,
    0177,
    ' ',
    'Y',
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
    0315,
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
    ':',
    0377,
    0177,
    0376,
    03,
    '(',
    '+',
    0376,
    04,
    '(',
    024,
    0247,
    0314,
    0217,
    0340,
    0304,
    0242,
    0,
    0315,
    0234,
    0,
    '(',
    0351,
    0315,
    0237,
    0,
    '2',
    0375,
    0177,
    030,
    0341,
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
    0316,
    0315,
    0217,
    0340,
    0315,
    0217,
    0340,
    '*',
    0261,
    0366,
    021,
    0364,
    0377,
    031,
    0345,
    '^',
    '#',
    'V',
    '!',
    0243,
    '}',
    0347,
    0341,
    ' ',
    014,
    021,
    0204,
    0340,
    's',
    '#',
    'r',
    0311,
    0301,
    0341,
    0303,
    0204,
    '}',
    036,
    'R',
    0315,
    0261,
    0377,
    0307,
    0365,
    0373,
    '!',
    0236,
    0374,
    '~',
    0276,
    '(',
    0375,
    0361,
    0311,
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
    uint32_t BusOn;
    uint32_t BusOff;
    uint32_t IRQLine3;
} state;

// global variables#
CartType type;
struct MSXState *state_pointer;
uint8_t *restrict cartpnt;
uint8_t *rampnt;
CircularBuffer *buf;
CircularBuffer *sbuf;
CircularBuffer *ibuf;

// Config Cart emulation hardware.
void Init_Cart() {

    state_pointer = &state;
    cartpnt = (uint8_t *)&__cart_section_start;
    rampnt = (uint8_t *)0x20007C00;
    buf = &cb;
    sbuf = &scb;
    ibuf = &icb;

    FLASH_Enhance_Mode (ENABLE);
    CART_CFG *cfg;
    uint8_t *restrict cfgpnt = (uint8_t *)&__cfg_section_start;
    cfg = (CART_CFG *)cfgpnt;
    type = cfg->CartType;

    uint8_t value = *rampnt;
    if (value != 1) {
        type = MSXTERMINAL;
    }
    *rampnt = 0;


    state_pointer->BusOn = 0x33333333;
    state_pointer->BusOff = 0x44444444;
    state_pointer->IRQLine3 = EXTI_Line3;

    switch (type) {
    case ROM16k:
        state_pointer->bankOffsets[0] = -0x4000;
        state_pointer->bankOffsets[1] = -0x8000;
        GPIO_WriteBit (GPIOA, GPIO_Pin_0, Bit_RESET);  // 1 - 0001
        NVIC_EnableIRQ (EXTI3_IRQn);
        SetVTFIRQ ((u32)RunCart16k, EXTI3_IRQn, 0, ENABLE);
        break;

    case ROM32k:
        state_pointer->bankOffsets[0] = -0x4000;
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
        state_pointer->bankOffsets[2] = -0x4000;
        state_pointer->bankOffsets[3] = -0x8000;
        state_pointer->bankOffsets[4] = 0x0;
        state_pointer->bankOffsets[5] = 0x0;
        NVIC_EnableIRQ (EXTI3_IRQn);
        SetVTFIRQ ((u32)RunKonamiWithoutSCC, EXTI3_IRQn, 0, ENABLE);
        break;
    case KonamiWithSCC:
        SCC_Init();
        GPIO_WriteBit (GPIOA, GPIO_Pin_2, Bit_RESET);  // 4 - 0100
        state_pointer->bankOffsets[2] = -0x4000;
        state_pointer->bankOffsets[3] = -0x6000;
        state_pointer->bankOffsets[4] = -0x8000;
        state_pointer->bankOffsets[5] = -0xA000;

        NVIC_EnableIRQ (EXTI3_IRQn);
        NVIC_SetPriority (EXTI3_IRQn, 0);
        SetVTFIRQ ((u32)RunKonamiWithSCC, EXTI3_IRQn, 0, ENABLE);
        break;
    case KonamiWithSCCNOSCC:
        GPIO_WriteBit (GPIOA, GPIO_Pin_0, Bit_RESET);
        GPIO_WriteBit (GPIOA, GPIO_Pin_2, Bit_RESET);

        state_pointer->bankOffsets[2] = -0x4000;
        state_pointer->bankOffsets[3] = -0x6000;
        state_pointer->bankOffsets[4] = -0x8000;
        state_pointer->bankOffsets[5] = -0xA000;

        NVIC_EnableIRQ (EXTI3_IRQn);
        NVIC_SetPriority (EXTI3_IRQn, 0);
        SetVTFIRQ ((u32)RunKonamiWithSCCNOSCC, EXTI3_IRQn, 0, ENABLE);
        break;

    case ASCII8k:
        GPIO_WriteBit (GPIOA, GPIO_Pin_1, Bit_RESET);  //  6 - 0110
        GPIO_WriteBit (GPIOA, GPIO_Pin_2, Bit_RESET);

        // configure initial banks state for ASCII8K
        state_pointer->bankOffsets[0] = -0x4000;  // switch address 6000h  -0x4000;
        state_pointer->bankOffsets[1] = -0x6000;  // switch address 6800h  -0x6000;
        state_pointer->bankOffsets[2] = -0x8000;  // switch address 7000h  -0x8000;
        state_pointer->bankOffsets[3] = -0xA000;  // switch address 7800h  -0xA000;

        NVIC_EnableIRQ (EXTI3_IRQn);
        SetVTFIRQ ((u32)Run8kASCII, EXTI3_IRQn, 0, ENABLE);
        break;
    case ASCII16k:
        GPIO_WriteBit (GPIOA, GPIO_Pin_0, Bit_RESET);  //  7 - 0111
        GPIO_WriteBit (GPIOA, GPIO_Pin_1, Bit_RESET);
        GPIO_WriteBit (GPIOA, GPIO_Pin_2, Bit_RESET);
        state_pointer->bankOffsets[0] = -0x4000;
        state_pointer->bankOffsets[8] = -0x8000;
        NVIC_EnableIRQ (EXTI3_IRQn);
        SetVTFIRQ ((u32)Run16kASCII, EXTI3_IRQn, 0, ENABLE);
        break;

    case NEO8:
        GPIO_WriteBit (GPIOA, GPIO_Pin_3, Bit_RESET);  // 8 - 1000

        state_pointer->bankOffsets[0] = 0;
        state_pointer->bankOffsets[1] = 0;
        state_pointer->bankOffsets[2] = 0;
        NVIC_EnableIRQ (EXTI3_IRQn);
        SetVTFIRQ ((u32)RunNEO8, EXTI3_IRQn, 0, ENABLE);
        break;

    case NEO16:
        GPIO_WriteBit (GPIOA, GPIO_Pin_0, Bit_RESET);  // 9 - 1001
        GPIO_WriteBit (GPIOA, GPIO_Pin_3, Bit_RESET);

        state_pointer->bankOffsets[0] = 0;
        state_pointer->bankOffsets[1] = 0;
        state_pointer->bankOffsets[2] = 0;
        NVIC_EnableIRQ (EXTI3_IRQn);
        SetVTFIRQ ((u32)RunNEO16, EXTI3_IRQn, 0, ENABLE);
        break;

    case MSXTERMINAL:
        NVIC_EnableIRQ (EXTI3_IRQn);
        SetVTFIRQ ((u32)RunMSXTerminal, EXTI3_IRQn, 0, ENABLE);
        break;

    default:
        break;
    }
}

void RunCart16k (void) {

    uint16_t address = (uint16_t)GPIOE->INDR;
    if (((uint16_t)GPIOB->INDR & 0x0008) == 0) {
        GPIOD->CFGLR = state_pointer->BusOn;
        if (address < 0x8000) {
            GPIOD->OUTDR = *(cartpnt + (address + state_pointer->bankOffsets[0]));
        } else {
            GPIOD->OUTDR = *(cartpnt + (address + state_pointer->bankOffsets[1]));
        }
        EXTI->INTFR = state_pointer->IRQLine3;
        while ((GPIOB->INDR & 0x0008) == 0) { };
        GPIOD->CFGLR = state_pointer->BusOff;
    }
    return;
}

void RunCart32k (void) {

    if (((uint16_t)GPIOB->INDR & 0x0008) == 0) {
        GPIOD->CFGLR = state_pointer->BusOn;
        GPIOD->OUTDR = *(cartpnt + ((uint16_t)GPIOE->INDR + state_pointer->bankOffsets[0]));
        EXTI->INTFR = state_pointer->IRQLine3;
        while ((GPIOB->INDR & 0x0008) == 0) { };
        GPIOD->CFGLR = state_pointer->BusOff;
    }
    return;
}

void RunCart48k (void) {

    if (((uint16_t)GPIOB->INDR & 0x0008) == 0) {
        GPIOD->CFGLR = state_pointer->BusOn;
        GPIOD->OUTDR = *(cartpnt + ((uint16_t)GPIOE->INDR));
        EXTI->INTFR = state_pointer->IRQLine3;
        while ((GPIOB->INDR & 0x0008) == 0) { };
        GPIOD->CFGLR = state_pointer->BusOff;
    }
    return;
}

void RunKonamiWithoutSCC (void) {

    uint16_t address = (uint16_t)GPIOE->INDR;

    if (((uint16_t)GPIOB->INDR & 0x0020) == 0)  // check for reads
    {
        GPIOD->CFGLR = state_pointer->BusOn;
        GPIOD->OUTDR = *(cartpnt + state_pointer->bankOffsets[address >> 13] + address);
        EXTI->INTFR = state_pointer->IRQLine3;
        while ((GPIOB->INDR & 0x0008) == 0) { };
        GPIOD->CFGLR = state_pointer->BusOff;

        return;
    }

    EXTI->INTFR = state_pointer->IRQLine3;
    uint8_t WriteData = ((uint16_t)GPIOD->INDR);

    while (((uint16_t)GPIOB->INDR & 0x0008) == 0) {
        if ((((uint16_t)GPIOB->INDR & 0x0010) == 0)) {

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

    uint16_t address = (uint16_t)GPIOE->INDR;
    uint32_t slot = address >> 13;

    if (((uint16_t)GPIOB->INDR & 0x0020) == 0) {
        GPIOD->CFGLR = state_pointer->BusOn;
        GPIOD->OUTDR = *(cartpnt + (state_pointer->bankOffsets[slot] + address));
        EXTI->INTFR = state_pointer->IRQLine3;
        while ((GPIOB->INDR & 0x0008) == 0) { };
        GPIOD->CFGLR = state_pointer->BusOff;

        return;
    }

    EXTI->INTFR = state_pointer->IRQLine3;
    //  if (address > 0xB000)
    //      return;

    uint8_t WriteData = ((uint16_t)GPIOD->INDR);
    uint32_t AddressData = (((uint32_t)address << 16) | WriteData);
    uint8_t next;
    if (buf->head + 1 >= BUFFER_SIZE) {
        next = 0;
    } else {
        next = buf->head + 1;
    }

    while (((uint16_t)GPIOB->INDR & 0x0008) == 0) {
        if ((((uint16_t)GPIOB->INDR & 0x0010) == 0)) {
            state_pointer->bankOffsets[slot] = (WriteData << 13) - (address - 0x1000);
            buf->buffer[buf->head] = AddressData;
            buf->head = next;

            return;
        }
    }
    return;
}

void RunKonamiWithSCCNOSCC (void) {

    uint16_t address = (uint16_t)GPIOE->INDR;

    if (((uint16_t)GPIOB->INDR & 0x0020) == 0) {
        GPIOD->CFGLR = state_pointer->BusOn;
        GPIOD->OUTDR = *(cartpnt + (state_pointer->bankOffsets[address >> 13] + address));
        EXTI->INTFR = state_pointer->IRQLine3;
        while ((GPIOB->INDR & 0x0008) == 0) { };
        GPIOD->CFGLR = state_pointer->BusOff;
        return;
    }

    EXTI->INTFR = state_pointer->IRQLine3;
    if (address > 0xB000)
        return;
    uint8_t WriteData = ((uint16_t)GPIOD->INDR);


    while (((uint16_t)GPIOB->INDR & 0x0008) == 0) {

        if ((((uint16_t)GPIOB->INDR & 0x0010) == 0)) {
            state_pointer->bankOffsets[address >> 13] = (WriteData << 13) - (address - 0x1000);
            return;
        }
    }
    return;
}

void Run8kASCII (void) {

    uint16_t address = GPIOE->INDR;

    if (((uint16_t)GPIOB->INDR & 0x0020) == 0) {
        GPIOD->CFGLR = state_pointer->BusOn;
        GPIOD->OUTDR = *(cartpnt + state_pointer->bankOffsets[(((address >> 12) - 4) >> 1)] + address);
        EXTI->INTFR = state_pointer->IRQLine3;
        while ((GPIOB->INDR & 0x0008) == 0) { };
        GPIOD->CFGLR = state_pointer->BusOff;

        return;
    }

    EXTI->INTFR = state_pointer->IRQLine3;
    if (address > 0xB000)
        return;
    uint8_t WriteData = ((uint8_t)GPIOD->INDR);

    while (((uint16_t)GPIOB->INDR & 0x0008) == 0) {
        if ((((uint16_t)GPIOB->INDR & 0x0010) == 0)) {
            int slot = (((address >> 11) & 0x3));
            state_pointer->bankOffsets[slot] = (WriteData << 13) - (0x4000 + (0x2000 * slot));

            return;
        }
    }
    return;
}

void Run16kASCII (void) {

    uint16_t address = (uint16_t)GPIOE->INDR;

    if (((uint16_t)GPIOB->INDR & 0x0020) == 0) {
        GPIOD->CFGLR = state_pointer->BusOn;

        if (address < 0x8000) {
            GPIOD->OUTDR = *(cartpnt + state_pointer->bankOffsets[0] + address);
        } else {

            GPIOD->OUTDR = *(cartpnt + state_pointer->bankOffsets[8] + address);
        }

        // GPIOD->OUTDR = *(cartpnt + state_pointer->bankOffsets[((address >> 12) & 0x8)] + address);


        EXTI->INTFR = state_pointer->IRQLine3;
        while ((GPIOB->INDR & 0x0008) == 0) { };
        GPIOD->CFGLR = state_pointer->BusOff;

        return;
    }

    EXTI->INTFR = state_pointer->IRQLine3;
    if (address > 0xB000)
        return;

    uint8_t WriteData = ((uint16_t)GPIOD->INDR);
    uint32_t Bank0 = (WriteData << 14) - 0x4000;
    uint32_t Bank8 = (WriteData << 14) - 0x8000;

    while (((uint16_t)GPIOB->INDR & 0x0008) == 0) {
        if ((((uint16_t)GPIOB->INDR & 0x0010) == 0)) {
            {
                switch (address) {
                case 0x6000:
                    state_pointer->bankOffsets[0] = Bank0;
                    return;
                    break;

                case 0x7000:
                case 0x77FF:
                    state_pointer->bankOffsets[8] = Bank8;
                    return;
                    break;

                default:
                    return;
                    break;
                }
                return;
            }
        }
        return;
    }
}

void RunNEO16 (void) {
    uint16_t address = (uint16_t)GPIOE->INDR;

    if (((uint16_t)GPIOB->INDR & 0x0020) == 0)  // check for reads
    {
        GPIOD->CFGLR = state_pointer->BusOn;

        uint8_t bank = address >> 14;
        if (bank > 2) {
            GPIOD->OUTDR = 0xFF;  // skip
        } else {
            uint32_t offset = ((state_pointer->bankOffsets[bank]) << 14) + (address & 0x3FFF);
            GPIOD->OUTDR = *(cartpnt + offset);
        }
        EXTI->INTFR = state_pointer->IRQLine3;
        while ((GPIOB->INDR & 0x0008) == 0) { };
        GPIOD->CFGLR = state_pointer->BusOff;
        return;
    }

    EXTI->INTFR = state_pointer->IRQLine3;
    if (address > 0xB000)
        return;
    uint8_t WriteData = ((uint16_t)GPIOD->INDR);

    while (((uint16_t)GPIOB->INDR & 0x0008) == 0) {
        if ((((uint16_t)GPIOB->INDR & 0x0010) == 0)) {
            uint8_t bank = ((address >> 12) & 0x03) - 1;
            if (bank > 2)
                return;
            if (address & 1)
                state_pointer->bankOffsets[bank] = ((WriteData & 0x0F) << 8) | (state_pointer->bankOffsets[bank] & 0x00FF);
            else
                state_pointer->bankOffsets[bank] = (state_pointer->bankOffsets[bank] & 0xFF00) | (WriteData);

            return;
        }
    }
    return;
}

void RunNEO8 (void) {

    uint16_t address = (uint16_t)GPIOE->INDR;

    if (((uint16_t)GPIOB->INDR & 0x0020) == 0) {
        GPIOD->CFGLR = state_pointer->BusOn;

        uint8_t bank = address >> 13;
        if (bank > 5) {
            GPIOD->OUTDR = 0xFF;
        } else {
            uint32_t offset = ((state_pointer->bankOffsets[bank]) << 13) + (address & 0x1FFF);
            GPIOD->OUTDR = *(cartpnt + offset);
        }

        EXTI->INTFR = state_pointer->IRQLine3;
        while ((GPIOB->INDR & 0x0008) == 0) { };
        GPIOD->CFGLR = state_pointer->BusOff;
        return;
    }

    EXTI->INTFR = state_pointer->IRQLine3;
    if (address > 0xB000)
        return;
    uint8_t WriteData = ((uint16_t)GPIOD->INDR);

    while (((uint16_t)GPIOB->INDR & 0x0008) == 0) {
        if ((((uint16_t)GPIOB->INDR & 0x0010) == 0)) {

            uint8_t bank = ((address >> 11) & 0x07) - 2;
            if (bank > 5)
                return;
            if (address & 1)
                state_pointer->bankOffsets[bank] = ((WriteData & 0x0F) << 8) | (state_pointer->bankOffsets[bank] & 0x00FF);
            else
                state_pointer->bankOffsets[bank] = (state_pointer->bankOffsets[bank] & 0xFF00) | (WriteData);
            return;
        }
    }
    return;
}

void RunMSXTerminal (void) {

    uint16_t address = (uint16_t)GPIOE->INDR;

    if (((uint16_t)GPIOB->INDR & 0x0020) == 0) {
        GPIOD->CFGLR = state_pointer->BusOn;
        if (address == 0x7FFF) {
            if (sbuf->head == sbuf->tail) {
                GPIOD->OUTDR = 0x00;
            } else {
                GPIOD->OUTDR = sbuf->buffer[sbuf->tail];
                sbuf->tail = (sbuf->tail + 1) & (BUFFER_SIZE - 1);
            }
        }

        if (address >= 0x4000 && address < 0x4FFF) {
            GPIOD->OUTDR = *(msxterm + (address - 0x4000));
        }
        EXTI->INTFR = state_pointer->IRQLine3;
        while ((GPIOB->INDR & 0x0008) == 0) { };
        GPIOD->CFGLR = state_pointer->BusOff;
    }

    EXTI->INTFR = state_pointer->IRQLine3;
    uint8_t WriteData = ((uint16_t)GPIOD->INDR);

    while (((uint16_t)GPIOB->INDR & 0x0008) == 0) {
        if ((((uint16_t)GPIOB->INDR & 0x0010) == 0)) {
            if (address == 0x7FFE) {
                if (WriteData != 0) {
                    GPIO_WriteBit (GPIOA, GPIO_Pin_0, Bit_SET);
                    GPIO_WriteBit (GPIOA, GPIO_Pin_1, Bit_SET);
                    GPIO_WriteBit (GPIOA, GPIO_Pin_2, Bit_SET);
                    GPIO_WriteBit (GPIOA, GPIO_Pin_3, Bit_SET);

                    *rampnt = 1;
                    PFIC->SCTLR |= (1 << 31);
                } else {
                    enableTerminal = 1;
                }
                return;
            }
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
