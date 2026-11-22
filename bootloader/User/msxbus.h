#ifndef MSX_BUS_H
#define MSX_BUS_H

#include <stdint.h>

/* Output queue: bytes the MSX side reads from #7FFF. */
#define MSX_OUT_SIZE 256
/* Input queue: bytes the MSX side writes to #7FFD. */
#define MSX_IN_SIZE 16

typedef struct {
    uint8_t data[MSX_OUT_SIZE];
    volatile uint8_t head;
    volatile uint8_t tail;
} MsxOutQueue;

typedef struct {
    uint8_t data[MSX_IN_SIZE];
    volatile uint8_t head;
    volatile uint8_t tail;
} MsxInQueue;

extern MsxOutQueue msx_out;
extern MsxInQueue msx_in;
extern volatile uint8_t msx_terminal_requested;

/* Bus signals, active low. */
#define MSX_SLT_BIT 0x0008 /* PB3 */
#define MSX_WR_BIT 0x0010  /* PB4 */
#define MSX_RD_BIT 0x0020  /* PB5 */

#define MSX_BUS_ON 0x33333333u
#define MSX_BUS_OFF 0x44444444u

void MsxBus_Init(void);
void MsxBus_QueueByte(uint8_t value);
void MsxBus_QueueString(const char *text);
void MsxBus_Flush(void);
int MsxBus_TakeInput(uint8_t *value);

/* Returns non-zero when the cartridge firmware asked for the ROM mode. */
int MsxBus_RomModeRequested(void);

/*
 * USB host port power.
 *
 * The RISKYMSX board feeds the USB host port through a CH217K current
 * limited switch. Its EN# input is active low and is wired to PB12, so the
 * port stays unpowered until the firmware drives PB12 low. FLAG# is an
 * active low open drain fault output on PB13.
 */
void Vbus_Init(void);
void Vbus_Enable(void);
void Vbus_Disable(void);
int Vbus_Faulted(void);

void MsxBus_Service(void) __attribute__((interrupt("WCH-Interrupt-fast")));

#endif /* MSX_BUS_H */
