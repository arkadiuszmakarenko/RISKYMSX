#include "msxbus.h"
#include "ch32v30x.h"
#include <string.h>

/*
 * MSX cartridge bus interception.
 *
 * GPIOE carries the address, GPIOD the data bus. PB3 is SLT, PB4 WR and PB5
 * RD, all active low. The MSX side program (MSXTerminal/msxterminal.asm)
 * reads a byte from #7FFF and writes a byte to #7FFD. #7FFE selects the
 * terminal (0) or the cartridge ROM (non-zero).
 */

MsxOutQueue msx_out;
MsxInQueue msx_in;
volatile uint8_t msx_terminal_requested;

/*
 * Handover flag for switching from the terminal to the cartridge ROM.
 *
 * The cartridge firmware reads this byte at startup: a non-zero value means
 * the terminal handed over to the ROM, a zero value means the terminal mode
 * was requested. The address is the same one the cartridge firmware uses, so
 * the two agree on the protocol across the reset.
 */
#define MSX_CART_RAM_FLAG ((volatile uint8_t *)0x20007C00u)

static const uint8_t msxterm[205] = {
    'A', 'B', '&', '@', 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    'R', 'I', 'S', 'K', 'Y', '_', 'B', 'O', 'O', 'T', 0x00, '1', '.', '0', 0x00, 0x00, '>',
    0x04, '2', 0xfe, 0x7f, 0xc9, '!', '3', '@', 0x11, 0x00, 0xe0, 0x01, 0x92, 0x00, 0xd5, 0xed,
    0xb0, 0xc9, 0xaf, '2', 0xfe, 0x7f, '>', ' ', '2', 0xaf, 0xf3, '!', 0x0f, 0x00, '"', 0xea,
    0xf3, '"', 0xe9, 0xf3, '>', 0x01, 0xcd, '_', 0x00, '!', 0xc5, '@', 0x11, 0x00, '8', 0x01,
    0x08, 0x00, 0xcd, 0x5c, 0x00, '>', 0x08, '!', 0x03, 0x1b, 0xcd, 'M', 0x00, ':', 0xff, 0x7f,
    0xfe, 0x03, '(', '+', 0xfe, 0x04, '(', 0x14, 0xa7, 0xcc, 0x87, 0xe0, 0xc4, 0xa2, 0x00,
    0xcd, 0x9c, 0x00, '(', 0xe9, 0xcd, 0x9f, 0x00, '2', 0xfd, 0x7f, 0x18, 0xe1, ':', 0xff,
    0x7f, '!', 0x01, 0x1b, 0xcd, 'M', 0x00, ':', 0xff, 0x7f, '=', '-', 0xcd, 'M', 0x00, 0x18,
    0xce, 0xcd, 0x87, 0xe0, 0xcd, 0x87, 0xe0, '*', 0xb1, 0xf6, 0x11, 0xf4, 0xff, 0x19, 0xe5,
    '^', '#', 'V', '!', 0xa3, '}', 0xe7, 0xe1, ' ', 0x0c, 0x11, '|', 0xe0, 's', '#', 'r', 0xc9,
    0xc1, 0xe1, 0xc3, 0x84, '}', 0x1e, 'R', 0xcd, 0xb1, 0xff, 0xc7, 0xf5, 0xfb, '!', 0x9e,
    0xfc, '~', 0xbe, '(', 0xfd, 0xf1, 0xc9, ' ', '0', '8', '<', '8', '0', ' ', 0x00
};

void MsxBus_Init(void)
{
    msx_out.head = 0;
    msx_out.tail = 0;
    msx_in.head = 0;
    msx_in.tail = 0;
    msx_terminal_requested = 0;

    /* Clear the handover flag: the application firmware must not see a stale
       value from an earlier boot. */
    *MSX_CART_RAM_FLAG = 0;

    RCC->APB2PCENR |= RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB |
                      RCC_APB2Periph_GPIOC | RCC_APB2Periph_GPIOD |
                      RCC_APB2Periph_GPIOE | RCC_APB2Periph_AFIO;

    /* Address (GPIOE) and data (GPIOD) lines are inputs; the bus is driven
       only while a read cycle is being serviced. */
    GPIOE->CFGLR = 0x44444444;
    GPIOD->CFGLR = MSX_BUS_OFF;

    GPIO_InitTypeDef gpio;
    gpio.GPIO_Pin = GPIO_Pin_8;
    gpio.GPIO_Mode = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &gpio);

    EXTI_InitTypeDef exti = {0};
    GPIO_EXTILineConfig(GPIO_PortSourceGPIOB, GPIO_PinSource3);
    exti.EXTI_Line = EXTI_Line3;
    exti.EXTI_Mode = EXTI_Mode_Interrupt;
    exti.EXTI_Trigger = EXTI_Trigger_Falling;
    exti.EXTI_LineCmd = ENABLE;
    EXTI_Init(&exti);

    NVIC_EnableIRQ(EXTI3_IRQn);
    SetVTFIRQ((uint32_t)MsxBus_Service, EXTI3_IRQn, 0, ENABLE);
}

void MsxBus_QueueByte(uint8_t value)
{
    uint8_t next = (uint8_t)((msx_out.head + 1) & (MSX_OUT_SIZE - 1));
    /* Wait for the MSX side to drain the queue instead of dropping the byte.
       A dropped byte in the middle of an escape sequence leaves the terminal
       in a broken state, and a dropped newline makes the output run together
       on one line. The cartridge firmware blocks the same way. */
    while (next == msx_out.tail) {
    }
    msx_out.data[msx_out.head] = value;
    msx_out.head = next;
}

void MsxBus_QueueString(const char *text)
{
    while (*text) {
        MsxBus_QueueByte((uint8_t)*text++);
    }
}

void MsxBus_Flush(void)
{
    while (msx_out.head != msx_out.tail) {
    }
}

int MsxBus_TakeInput(uint8_t *value)
{
    if (msx_in.head == msx_in.tail) {
        return 0;
    }
    *value = msx_in.data[msx_in.tail];
    msx_in.tail = (uint8_t)((msx_in.tail + 1) & (MSX_IN_SIZE - 1));
    return 1;
}

/*
 * The cartridge firmware checks this flag at startup: a non-zero value means
 * the terminal handed over to the ROM. The bootloader clears it on start, so
 * the application firmware does not mistake a bootloader run for a terminal
 * request.
 */
int MsxBus_RomModeRequested(void)
{
    return *MSX_CART_RAM_FLAG != 0;
}

void Vbus_Init(void)
{
    GPIO_InitTypeDef gpio;

    RCC->APB2PCENR |= RCC_APB2Periph_GPIOB;

    /* PB12 drives the CH217K EN# input: push pull output, initially high so
       the port stays unpowered until it is explicitly enabled. */
    gpio.GPIO_Pin = GPIO_Pin_12;
    gpio.GPIO_Mode = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &gpio);
    GPIO_SetBits(GPIOB, GPIO_Pin_12);

    /* PB13 reads the CH217K FLAG# output: active low, open drain, held high
       by an internal pull-up when there is no fault. */
    gpio.GPIO_Pin = GPIO_Pin_13;
    gpio.GPIO_Mode = GPIO_Mode_IPU;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &gpio);
}

void Vbus_Enable(void)
{
    /* EN# is active low: drive PB12 low to power the USB host port. */
    GPIO_ResetBits(GPIOB, GPIO_Pin_12);
}

void Vbus_Disable(void)
{
    GPIO_SetBits(GPIOB, GPIO_Pin_12);
}

int Vbus_Faulted(void)
{
    /* FLAG# is active low: a low level means over-current or over-temperature. */
    return GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_13) == Bit_RESET;
}

/*
 * Runs from the EXTI3 vector (SLT falling edge). It services a read cycle and
 * then captures a write cycle, matching the bus timing of the cartridge
 * firmware. Keeping this in one place lets the bootloader reuse the exact
 * protocol the MSX side program expects.
 */
void MsxBus_Service(void)
{
    uint16_t address = (uint16_t)GPIOE->INDR;

    if (((uint16_t)GPIOB->INDR & MSX_RD_BIT) == 0) {
        GPIOD->CFGLR = MSX_BUS_ON;
        if (address == 0x7FFF) {
            if (msx_out.head == msx_out.tail) {
                GPIOD->OUTDR = 0x00;
            } else {
                GPIOD->OUTDR = msx_out.data[msx_out.tail];
                msx_out.tail = (uint8_t)((msx_out.tail + 1) & (MSX_OUT_SIZE - 1));
            }
        }
        if (address >= 0x4000 && address < 0x4000 + sizeof(msxterm)) {
            GPIOD->OUTDR = msxterm[address - 0x4000];
        }
        EXTI->INTFR = EXTI_Line3;
        while ((GPIOB->INDR & MSX_SLT_BIT) == 0) {
        }
        GPIOD->CFGLR = MSX_BUS_OFF;
    }

    EXTI->INTFR = EXTI_Line3;
    uint8_t write_data = (uint8_t)GPIOD->INDR;

    while ((GPIOB->INDR & MSX_SLT_BIT) == 0) {
        if (((uint16_t)GPIOB->INDR & MSX_WR_BIT) == 0) {
            if (address == 0x7FFE) {
                if (write_data != 0) {
                    /* The MSX side asked for the cartridge ROM: hand over to
                       the application firmware and reset. */
                    GPIO_WriteBit(GPIOA, GPIO_Pin_0, Bit_SET);
                    GPIO_WriteBit(GPIOA, GPIO_Pin_1, Bit_SET);
                    GPIO_WriteBit(GPIOA, GPIO_Pin_2, Bit_SET);
                    GPIO_WriteBit(GPIOA, GPIO_Pin_3, Bit_SET);
                    *MSX_CART_RAM_FLAG = 1;
                    PFIC->SCTLR |= (1u << 31);
                } else {
                    msx_terminal_requested = 1;
                }
                return;
            }
            if (address == 0x7FFD) {
                uint8_t next = (uint8_t)((msx_in.head + 1) & (MSX_IN_SIZE - 1));
                if (next != msx_in.tail) {
                    msx_in.data[msx_in.head] = write_data;
                    msx_in.head = next;
                }
            }
            return;
        }
    }
}
