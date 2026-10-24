/*
 * nextor.h - Nextor kernel mapper for the RISKYMSX2 cartridge.
 *
 * The mapper is ASCII16K + a mailbox window punched out of the bank:
 *
 *   0x4000..0x7FFF : 16 KiB bank n of the embedded kernel ROM
 *                    (nextor_rom[], flash-resident)
 *   0x6000 (W)     : select bank n for page 1
 *   0x8000..0xBFFF : 16 KiB bank m of the same ROM
 *   0x7000 (W)     : select bank m for page 2
 *   0x77FF (W)     : alias of 0x7000 (some Nextor drivers use it)
 *   0x7FF0..0x7FF5 : mailbox, decoded by address in every paged-in bank
 *
 * nextor_rom[] is the 128 KiB image built by
 * `make -C MSXSoftware/NextorDriver` (Nextor-3.0.RISKYMSX2.ROM) and
 * embedded by the firmware Makefile's regenerate_nextor_rom target.
 * 128 KiB = 8 banks x 16 KiB, so the bank number in a 0x6000 / 0x7000
 * write is 0..7.
 *
 * The kernel's own driver (MSXSoftware/NextorDriver/driver.asm) runs
 * from page 1 and reaches the disk ONLY through the mailbox. It has no
 * other hardware interface: no I/O ports, no ATA task file, no Z80 RAM
 * work area. That is why the mailbox is the single extension point.
 *
 * Bus-cycle routing and the ASCII16K bank decode live in
 * cart.c::Cart_EXTI0_Nextor_Handler; everything behind the mailbox
 * (command dispatch, result FIFO, disk access) lives in nextor.c.
 *
 * ---------------------------------------------------------------------------
 * The device
 * ---------------------------------------------------------------------------
 * The driver reports ONE device to the kernel (driver query 5 answers 1, and
 * the jump table in driver.asm is the only place that number is written
 * down). That one device IS the USB stick: the raw SCSI medium, addressed by
 * LBA, with its capacity and block size taken from READ CAPACITY during
 * enumeration. There is no filesystem anywhere in the data path - no FatFs,
 * no f_lseek, no file handle - and no container image standing in for the
 * medium. Backed by raw_disk.c.
 *
 * That means the drive looks to Nextor exactly as an IDE disk looks, and the
 * kernel's own machinery does the rest: sector 0 is an MBR, the MBR's
 * partition entries lead to a FAT volume, and the volume's boot sector is
 * what gets executed. driver.asm is written in the shape of the reference
 * Sunrise IDE driver for Nextor v3 for the same reason - it is the case this
 * hardware actually is.
 *
 * Having exactly one device is also why the wire protocol carries NO device
 * byte. Version 2 prepended one to every device-scoped command so that two
 * devices could share one command set; with one device it is a field that
 * can only ever be wrong, and the boot log showed it going wrong.
 * Reintroducing it (and the dispatch table behind it) is a clean future step
 * once a second medium exists, not something to carry now.
 *
 * ---------------------------------------------------------------------------
 * Version 3 of the wire protocol
 * ---------------------------------------------------------------------------
 * v1 had a single device and no device byte. v2 added the device byte
 * anyway, for a second device that has since been replaced by the raw stick
 * itself. v3 drops the byte again and changes two answers:
 *
 *   - CAPACITY now returns the whole 12-byte Nextor parameter block
 *     (medium type, sector size, sector count, flags, CHS) instead of
 *     blocks + blocksize. The kernel wants all twelve bytes anyway, so this
 *     removes the driver assembling byte by byte a block it could only
 *     partly invent, and turns "no medium" into an ordinary answer
 *     (sector count 0) instead of a special path through the driver.
 *
 *   - IDENT is new: it returns the stick's INQUIRY strings, so Nextor's
 *     device screen names the actual drive instead of a hardcoded label.
 *
 * The handshake answer carries the version byte and driver.asm checks it
 * against MB_EXPECT, so a driver built against one revision and a firmware
 * built against the other fails the handshake at boot - loudly, with the
 * kernel skipping this driver - rather than answering every command with
 * arguments in the wrong places.
 *
 * The mailbox window itself has NOT moved. It is decoded by address in
 * every paged-in bank and the window test in cart.c keeps A0..A3, so the
 * register layout is tied to the low address bits and changing it would mean
 * changing the decode. It has been 0x7FF0..0x7FF5 since v1 and stays there.
 */

#ifndef __NEXTOR_H
#define __NEXTOR_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================
 * Cart window geometry
 *
 * Fixed contract - MSXSoftware/NextorDriver/driver.asm is assembled
 * against these addresses, so they must not drift.
 * ====================================================================== */

/* Bank-select registers. A write here latches the 16 KiB bank for that
 * page; the value written IS the bank number (no bit-reversing, no
 * masking). Page 3 mirrors page 2 by the MSX slot logic. */
#define NEXTOR_BANK_REG_PAGE1     0x6000U
#define NEXTOR_BANK_REG_PAGE2     0x7000U
#define NEXTOR_BANK_REG_PAGE2_ALT 0x77FFU

/* Bank size and origin, used to turn a bank number into a byte offset
 * into nextor_rom[]:  off = (bank << 14) + (address & 0x3FFF). */
#define NEXTOR_BANK_SHIFT         14
#define NEXTOR_BANK_SIZE          (1U << NEXTOR_BANK_SHIFT)   /* 16 KiB */

/* Mailbox window. The whole 0x7FF0..0x7FFF page slice is decoded by
 * the handler (one AND + compare) and handed to nextor.c, which
 * interprets the individual registers. The six registers below are the
 * ones driver.asm defines; 0x7FF6..0x7FFF are decoded by the handler
 * but unused by the protocol.
 *
 * The register index is therefore a straight 1:1 with the low address
 * byte, and the window test has to keep A0..A3. That is safe because
 * A0 and A1 really are on the bus: the bank registers in cart.c are
 * compared exactly (0x6000, 0x7000, and 0x77FF which pins A0..A7
 * individually), and bank switching works, so the low address bits
 * arrive intact. A 4-byte-stride layout would tolerate their absence
 * but is not needed and would break the existing window test. */
#define NEXTOR_MBOX_BASE          0x7FF0U
#define NEXTOR_MBOX_END           0x7FF5U

/* Mailbox register indices (address - NEXTOR_MBOX_BASE). */
#define NEXTOR_MBOX_CMD           0x00U   /* W: start a command          */
#define NEXTOR_MBOX_STAT          0x00U   /* R: status bits              */
#define NEXTOR_MBOX_DATA          0x01U   /* R: pop result / W: push arg */
#define NEXTOR_MBOX_RXCNT_LO      0x02U   /* R: result bytes available  */
#define NEXTOR_MBOX_RXCNT_HI      0x03U
#define NEXTOR_MBOX_ERR           0x04U   /* R: last error code         */
#define NEXTOR_MBOX_VER           0x05U   /* R: firmware version byte   */

/* Decode a cart address to a register index. Reached only for addresses
 * inside the window, so no further range check is needed. */
#define NEXTOR_MBOX_REG(address) \
    ((uint8_t)((uint16_t)(address) - NEXTOR_MBOX_BASE))

/* Is `address` inside the 16-byte mailbox window at NEXTOR_MBOX_BASE?
 *
 * Written as a right-shift compare instead of the obvious
 * `(address & 0xFFF0) == NEXTOR_MBOX_BASE`, because 0x7FF0 does not fit
 * a 12-bit signed immediate: the masked form makes the compiler
 * materialise 0xFFF0 and 0x7FF0 as two separate constants before the
 * AND and the compare (six instructions), while
 * `(address >> 4) == 0x7FF` needs a shift plus one immediate compare
 * (three instructions) - 0x7FF is exactly the largest value a 12-bit
 * signed immediate can hold. The two predicates agree for every 16-bit
 * address: both are true exactly on 0x7FF0..0x7FFF.
 *
 * This test sits in front of every ROM read and every bank write in the
 * Nextor/Slotted handlers, and a hit is rare (the Z80 only reaches
 * 0x7FF0..0x7FF5 for mailbox traffic), so the three-instruction miss
 * path is what the hot cycles actually pay. */
#define NEXTOR_IN_MBOX_WINDOW(address) \
    (((address) >> 4) == (NEXTOR_MBOX_BASE >> 4))

/* STATUS bits (bit7 MSB first - the driver tests them as a byte). */
#define NEXTOR_STAT_READY         0x80U   /* firmware alive            */
#define NEXTOR_STAT_DONE          0x40U   /* command finished           */
#define NEXTOR_STAT_ERR           0x20U   /* command failed             */
#define NEXTOR_STAT_RX_AVAIL      0x10U   /* result FIFO not empty      */

/* ERR port values.
 *
 * Deliberately three, not five. The v2 set also had READONLY and NODEV,
 * and with the raw drive both became unraisable: there is no read-only
 * device to report (the stick is writable and says so in its parameter
 * block, so the kernel never sends a write it expects to be refused) and
 * no device byte that could name a device this firmware does not have.
 * An ERR code nothing can raise is a code the driver cannot be written
 * against correctly, so they are gone rather than left as 0xFF. */
#define NEXTOR_ERR_NONE           0x00U
/* No medium. Distinct from IO because the driver maps them to different
 * DOS errors - .NRDY ("insert disk") versus .DISK - and reporting a
 * failed transfer as "no medium" makes the kernel unmap a drive that is
 * present. */
#define NEXTOR_ERR_NO_MEDIA       0x01U
/* The transfer itself failed: a SCSI error, a bus error, or a range
 * check rejection. */
#define NEXTOR_ERR_IO             0x02U
/* Reserved. The driver's MB_POLL has its own timeout and answers with a
 * Nextor error code of its own; nothing in the firmware raises this. */
#define NEXTOR_ERR_TIMEOUT        0x03U

/* The one and only device number. Both the kernel and driver.asm count up
 * from 1, so this is a count AND a maximum. It is written down in three
 * places (this, driver.asm's jump-table answer and driver.asm's device
 * check) and the handshake is what catches a disagreement between the
 * driver and this file. */
#define NEXTOR_DEVICE_COUNT       1U

/* Command bytes written to NEXTOR_MBOX_CMD.
 *
 * NONE of them takes a device number - see the header note on why. HANDSHAKE
 * and ABORT take no arguments at all; CAPACITY, STATUS and STAPEEK take no
 * arguments either and are pure queries about the one device. */
#define NEXTOR_CMD_HANDSHAKE      0x00U
#define NEXTOR_CMD_CAPACITY       0x01U   /* -> 12 bytes: the whole Nextor
                                                *    device parameter block */
#define NEXTOR_CMD_STATUS         0x02U   /* -> 1 byte, CONSUMES the media
                                                *    change latch            */
#define NEXTOR_CMD_READ           0x03U   /* <- LBA(4) -> 512 bytes       */
#define NEXTOR_CMD_WRITE          0x04U   /* <- LBA(4) + 512 bytes        */
#define NEXTOR_CMD_ABORT          0x05U
#define NEXTOR_CMD_STAPEEK        0x06U   /* -> 1 byte, does NOT consume
                                                *    the media change latch  */
#define NEXTOR_CMD_IDENT          0x07U   /* -> 28 bytes: INQUIRY vendor(8)
                                                *    + product(20), space-padded */
/* Revision 4: the bulk commands. A flash stick charges its WRITE(10)
 * commit (measured 210 ms-1075 ms on the test stick) once per SCSI
 * COMMAND, not per sector, so N sectors in one command pay ONE commit
 * instead of N. The single-sector commands above remain the fallback
 * path - and are the whole protocol for revision-3 pairs, which is what
 * the version bump in the handshake enforces. */
#define NEXTOR_CMD_WRITEM         0x08U   /* <- LBA(4) + count(1) +
                                          *    count*512 bytes           */
#define NEXTOR_CMD_READM          0x09U   /* <- LBA(4) + count(1)
                                          *    -> count*512 bytes        */
#define NEXTOR_CMD_MAX            0x0AU

/* Bulk command sizing. 32 sectors = 16 KiB keeps the whole exchange
 * inside the Z80's ~2.9 s MB_POLL window even on a slow stick: the data
 * push costs ~8 us/byte (~125 ms for 16 KiB) and the firmware answers
 * after ONE WRITE(10) budget (~2 s), so DONE or ERR always lands while
 * the driver is still polling. On failure the driver falls back to
 * per-sector commands for the same range - they are the retry. */
#define NEXTOR_BULK_MAX_SECTORS   8U
#define NEXTOR_BULK_HDR           5U     /* LBA(4) + count(1)           */
#define NEXTOR_BULK_ARG_MAX       (NEXTOR_BULK_HDR \
                                   + NEXTOR_BULK_MAX_SECTORS * 512U)

/* --- Breadcrumbs -------------------------------------------------------
 *
 * Reserved command bytes the driver writes to NEXTOR_MBOX_CMD to say
 * "I got this far", with one diagnostic argument and no result data. They
 * exist because everything else the Z80 does is invisible from here: it
 * has no console the firmware can read, and a driver that dies half way through
 * READ_WRITE produces NO mailbox traffic at all - not one line, because
 * the last thing it managed to do was not send a command. In that
 * situation the log cannot tell "the kernel never called READ_WRITE" from
 * "READ_WRITE crashed on its third instruction", and those need opposite
 * fixes.
 *
 * The lightweight markers are fire-and-forget; the detailed PROBES build
 * sends an argument and waits for DONE so its diagnostic traffic cannot
 * overwrite a pending request. Neither form changes the mailbox protocol.
 *
 * Bytes 0x20..0x5F, chosen to be far from the 8 real commands and from
 * the 0x08..0x1F range a corrupted stack is more likely to produce, so a
 * marker in the log is a marker and not a wild write. */
#define NEXTOR_CMD_MARK_FIRST      0x20U
#define NEXTOR_CMD_MARK_COUNT      0x40U
#define NEXTOR_CMD_MARK(n)         (NEXTOR_CMD_MARK_FIRST + (n))

/* A breadcrumb carries ONE argument: the register value the step is about.
 * The device number for a bad-device exit, the query index for a
 * dispatcher, the direction bit for a transfer. A breadcrumb with a name
 * but no value answers "which step", which leaves the interesting question
 * - "which <em>value</em>" - open; the argument closes it for the price of
 * one more bus write and one more byte in args[].
 *
 * argend is therefore args+1 for a marker, not args, so the byte the driver
 * pushes after the command byte lands in the argument buffer instead of
 * being dropped by the overflow guard. See Nextor_WriteByte(). */
#define NEXTOR_MARK_ARGC           1U

/* Handshake reply: "RNX4" + version byte. The magic's last character and
 * the version byte move together; the driver's MB_EXPECT holds the same
 * pair, so a mismatched driver/firmware pair fails at boot instead of
 * silently decoding arguments in the wrong places. */
#define NEXTOR_MAGIC              "RNX4"
#define NEXTOR_VERSION            0x04U

/* ========================================================================
 * Public API
 * ====================================================================== */

/* Initialise the mapper + mailbox to power-on state. Called from main()
 * and from Cart_SetMapper() on every swap to CART_MAP_NEXTOR. */
void Nextor_Init (void);

/* Main-loop service hook: drains the command in flight into the result
 * FIFO. Must be called from main(), never from IRQ context - it is the
 * only place blocking disk work is allowed. */
/* Nonzero while the Z80 driver has a mailbox request waiting or a burst
 * in flight. Long background work (USB probes, enumeration) polls this
 * between its blocking steps to yield the service loop immediately. */
uint8_t Nextor_RequestPending (void);
void Nextor_Service (void);

/* Called by the USB layer when the stick is removed/re-plugged (or the
 * volume is unmounted for any other reason). Drops the cached capacity and
 * INQUIRY strings so the next main-loop idle pass re-probes the new stick
 * and the kernel is told about the media change. Main-loop or service
 * context only - NOT IRQ safe. */
void Nextor_CacheInvalidate (void);

/* Ask the service loop to run a probe on its next idle pass, bypassing
 * the rate-limit window a missing-stick STATUS would otherwise sit
 * behind. Called from the STATUS/STAPEEK command path when the medium
 * is reported absent. IRQ-safe. */
void Nextor_ForceProbe (void);

/* --- Code entries for Cart_EXTI0_Nextor_Handler (IRQ context) ------- */

/* Mailbox state, exported so the bus ISR can serve DATA/STATUS reads inline
 * (the call into nextor.c is too slow for the Z80 read window). */
#define NEXTOR_RES_SMALL_LEN 28U
typedef struct {
    uint8_t * volatile argp;
    uint8_t * volatile argend;
    volatile uint8_t   cmd;
    volatile uint8_t   armed;
    volatile uint8_t   seq;
    uint8_t * volatile res_r;
    uint8_t * volatile res_end;
    volatile uint8_t   status;
    uint8_t            err;
    uint8_t            res[NEXTOR_RES_SMALL_LEN];
} nextor_mb_t;
extern nextor_mb_t g_nextor_mb;

/* Call after the bus is released: finishes a DATA pop (RX_AVAIL). */
void Nextor_ReadDone (void);

/* Read entry. The handler calls this ONLY for addresses inside the
 * mailbox window (0x7FF0..0x7FF5); every other cart read is served
 * from nextor_rom[] by the handler itself. Returns the mailbox register
 * selected by (address - NEXTOR_MBOX_BASE).
 *
 * MUST be IRQ-safe - no blocking, no printf, no long loops. */
uint8_t Nextor_ReadByte (uint16_t address);

/* Write entry. The handler calls this ONLY for addresses inside the
 * mailbox window. Same constraints as the read entry. */
void Nextor_WriteByte (uint16_t address, uint8_t value);

/* -------------------------------------------------------------------- */

#ifdef __cplusplus
}
#endif

#endif /* __NEXTOR_H */
