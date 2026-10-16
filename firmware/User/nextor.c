/*
 * nextor.c - mailbox engine behind the Nextor mapper (see nextor.h).
 *
 * Two contexts share the state in s_mb:
 *   - Nextor_ReadByte / Nextor_WriteByte run in the bus interrupt
 *     (Cart_EXTI0_Nextor_Handler). They only move bytes and flip flags.
 *   - Nextor_Service runs from main() and does all the blocking USB work.
 */

#include "nextor.h"
#include "raw_disk.h"
#include "ch32v30x_conf.h"
#include <string.h>

#define NEXTOR_SECTOR_SIZE   512U
#define NEXTOR_PARAMS_SIZE   12U
#define NEXTOR_IDENT_VENDOR  8U
#define NEXTOR_IDENT_MODEL   20U
#define NEXTOR_IDENT_SIZE    (NEXTOR_IDENT_VENDOR + NEXTOR_IDENT_MODEL)
#define NEXTOR_CMD_NONE      NEXTOR_CMD_MAX

/* Argument sink and bulk/sector result source. SRAM, not flash: the sink is
 * filled one byte at a time from the bus interrupt. */
static __attribute__ ((aligned (4))) uint8_t s_buf[NEXTOR_BULK_ARG_MAX];

/* Argument bytes expected after each command byte. WRITEM/READM start with
 * the 5-byte header; WRITEM grows once the sector count is known. */
static const uint16_t s_cmd_args[NEXTOR_CMD_MAX] = {
    0U, 0U, 0U, 4U, (uint16_t)(4U + NEXTOR_SECTOR_SIZE),
    0U, 0U, 0U, NEXTOR_BULK_HDR, NEXTOR_BULK_HDR,
};

nextor_mb_t g_nextor_mb;
#define s_mb g_nextor_mb

static uint8_t is_mark (uint8_t cmd) {
    return (uint8_t)(cmd >= NEXTOR_CMD_MARK_FIRST
                     && cmd < (NEXTOR_CMD_MARK_FIRST + NEXTOR_CMD_MARK_COUNT));
}

/* --- Time -----------------------------------------------------------------
 * Free-running 16-bit TIM2 at 4 kHz (wraps every ~16 s). The core has no
 * usable cycle counter and Delay_Ms owns SysTick. Differences are masked to
 * 16 bits, so every interval below must stay well under 16 s. */

#define TICKS_PER_MS 4U

static uint32_t rdcycle (void) {
    return (uint32_t)TIM2->CNT;
}

static void cyc_calibrate (void) {
    RCC->APB1PCENR |= RCC_APB1Periph_TIM2;
    TIM2->PSC  = (uint16_t)(SystemCoreClock / (TICKS_PER_MS * 1000U) - 1U);
    TIM2->ATRLR = 0xFFFFU;
    TIM2->SWEVGR = TIM_PSCReloadMode_Immediate;
    TIM2->CTLR1 |= TIM_CEN;
}

static volatile uint32_t s_last_cmd_cyc;

/* Probe pacing: wait for the Z80 to go idle and for the stick to settle. */
#define NEXTOR_PROBE_DELAY_MS      200U
#define NEXTOR_PROBE_RETRY_MS     2000U
#define NEXTOR_PROBE_IDLE_MS       250U

static uint32_t s_boot_cyc;
static uint8_t  s_boot_done;
static uint32_t s_probe_cyc;
static uint8_t  s_probe_pending;
static volatile uint8_t s_probe_forced;

void Nextor_ForceProbe (void) {
    s_probe_forced = 1U;
}

uint8_t Nextor_RequestPending (void) {
    return (s_mb.armed != 0U || s_mb.argp != s_buf) ? 1U : 0U;
}

static void raw_probe (void) {
    const uint32_t now = rdcycle();
    const uint8_t forced = s_probe_forced;

    /* The driver polls STATUS continuously while no disk is present, so only
     * honour the idle window when the probe was not explicitly requested. */
    if (forced == 0U
        && ((now - s_last_cmd_cyc) & 0xFFFFU) < TICKS_PER_MS * NEXTOR_PROBE_IDLE_MS) return;
    if (s_boot_done == 0U) {
        if (((now - s_boot_cyc) & 0xFFFFU) < TICKS_PER_MS * NEXTOR_PROBE_DELAY_MS) return;
        s_boot_done = 1U;
    }
    if (RawDisk_IsPresent ()) return;
    if (s_probe_pending != 0U
        && ((now - s_probe_cyc) & 0xFFFFU) < TICKS_PER_MS * NEXTOR_PROBE_RETRY_MS) {
        return;
    }
    s_probe_cyc = now;
    s_probe_pending = 1U;
    s_probe_forced = 0U;
    if (Nextor_RequestPending () != 0U) return;
    (void)RawDisk_Probe ();
}

void Nextor_ReadDone (void) {
    if (s_mb.res_r == s_mb.res_end) {
        s_mb.status &= (uint8_t)~NEXTOR_STAT_RX_AVAIL;
    }
}

void Nextor_CacheInvalidate (void) {
    RawDisk_Invalidate ();
}

void Nextor_Init (void) {
    s_mb.argp    = s_buf;
    s_mb.argend  = s_buf;
    s_mb.res_r   = s_mb.res;
    s_mb.res_end = s_mb.res;
    s_mb.cmd     = NEXTOR_CMD_NONE;
    s_mb.armed   = 0U;
    s_mb.seq     = 0U;
    s_mb.status  = NEXTOR_STAT_READY;
    s_mb.err     = NEXTOR_ERR_NONE;
    cyc_calibrate ();
    s_boot_cyc = rdcycle ();
    s_last_cmd_cyc = s_boot_cyc;
    s_probe_pending = 0U;
    s_boot_done = 0U;
}

static uint32_t buf_lba (void) {
    return  (uint32_t)s_buf[0]
         | ((uint32_t)s_buf[1] << 8)
         | ((uint32_t)s_buf[2] << 16)
         | ((uint32_t)s_buf[3] << 24);
}

static void params_build (uint8_t *out) {
    uint16_t cyl = 0U;
    uint8_t  head = 0U;
    uint8_t  spt = 0U;
    const uint32_t sectors = RawDisk_IsPresent () ? RawDisk_SectorCount () : 0U;
    const uint16_t ssize = RawDisk_SectorSize ();

    if (sectors != 0U) {
        RawDisk_Geometry (&cyl, &head, &spt);
    }
    out[0]  = 0U;                     /* medium type */
    out[1]  = (uint8_t)(ssize & 0xFFU);
    out[2]  = (uint8_t)(ssize >> 8);
    out[3]  = (uint8_t)sectors;
    out[4]  = (uint8_t)(sectors >> 8);
    out[5]  = (uint8_t)(sectors >> 16);
    out[6]  = (uint8_t)(sectors >> 24);
    out[7]  = 0x01U;                  /* flags: removable */
    out[8]  = (uint8_t)(cyl & 0xFFU);
    out[9]  = (uint8_t)(cyl >> 8);
    out[10] = head;
    out[11] = spt;
}

static void ident_pad (uint8_t *out, const char *src, uint8_t width) {
    uint8_t n = 0U;
    while (n < width && src[n] != '\0') {
        out[n] = (uint8_t)src[n];
        n++;
    }
    while (n < width) {
        out[n++] = (uint8_t)' ';
    }
}

/* Main-loop service: run the command the Z80 armed and publish the answer. */
void Nextor_Service (void) {
    uint8_t  cmd;
    uint8_t  seq;
    uint32_t lba;
    uint32_t n;

    if (s_mb.armed == 0U) {
        raw_probe ();
        return;
    }

    /* Take a consistent snapshot; a newer request may overwrite it. */
    {
        unsigned tries = 8U;
        for (;;) {
            const uint8_t seq0 = s_mb.seq;
            cmd = s_mb.cmd;
            lba = buf_lba ();
            n   = s_buf[4];
            seq = seq0;
            if (s_mb.seq == seq0 && s_mb.armed != 0U) {
                s_mb.armed = 0U;
                break;
            }
            if (--tries == 0U) return;
        }
    }

    s_mb.res_r   = s_mb.res;
    s_mb.res_end = s_mb.res;
    s_mb.err     = NEXTOR_ERR_NONE;
    s_mb.status &= (uint8_t)~(NEXTOR_STAT_RX_AVAIL | NEXTOR_STAT_ERR);

    switch (cmd) {
    case NEXTOR_CMD_HANDSHAKE:
        memcpy (s_mb.res, NEXTOR_MAGIC, 4U);
        s_mb.res[4]  = NEXTOR_VERSION;
        s_mb.res_end = s_mb.res + 5U;
        break;
    case NEXTOR_CMD_CAPACITY:
        params_build (s_mb.res);
        s_mb.res_end = s_mb.res + NEXTOR_PARAMS_SIZE;
        break;
    case NEXTOR_CMD_IDENT:
        ident_pad (&s_mb.res[0], RawDisk_Manufacturer (), NEXTOR_IDENT_VENDOR);
        ident_pad (&s_mb.res[NEXTOR_IDENT_VENDOR], RawDisk_Model (), NEXTOR_IDENT_MODEL);
        s_mb.res_end = s_mb.res + NEXTOR_IDENT_SIZE;
        break;
    case NEXTOR_CMD_STATUS:
    case NEXTOR_CMD_STAPEEK: {
        const uint8_t st = RawDisk_MediaState ();
        if (cmd == NEXTOR_CMD_STATUS) {
            RawDisk_MediaLatchConsume ();
        }
        if (st == 0U) {
            Nextor_ForceProbe ();
        }
        s_mb.res[0]  = st;
        s_mb.res_end = s_mb.res + 1U;
        break;
    }
    case NEXTOR_CMD_ABORT:
        break;
    case NEXTOR_CMD_READ:
        if (RawDisk_IsPresent () == 0U) {
            s_mb.err = NEXTOR_ERR_NO_MEDIA;
        } else if (RawDisk_ReadSectors (lba, 1U, s_buf) == 0U) {
            s_mb.err = NEXTOR_ERR_IO;
        } else {
            s_mb.res_r   = s_buf;
            s_mb.res_end = s_buf + NEXTOR_SECTOR_SIZE;
        }
        break;
    case NEXTOR_CMD_WRITE:
        if (RawDisk_IsPresent () == 0U) {
            s_mb.err = NEXTOR_ERR_NO_MEDIA;
        } else if (RawDisk_WriteSectors (lba, 1U, &s_buf[4]) == 0U) {
            s_mb.err = NEXTOR_ERR_IO;
        }
        break;
    case NEXTOR_CMD_WRITEM:
    case NEXTOR_CMD_READM:
        /* count 1 is never sent by the driver and the bulk header layout
         * would be ambiguous: answer ERR so it falls back per sector. */
        if (RawDisk_IsPresent () == 0U) {
            s_mb.err = NEXTOR_ERR_NO_MEDIA;
        } else if (n < 2U || n > NEXTOR_BULK_MAX_SECTORS) {
            s_mb.err = NEXTOR_ERR_IO;
        } else if (cmd == NEXTOR_CMD_WRITEM) {
            if (RawDisk_WriteSectors (lba, (uint16_t)n, &s_buf[NEXTOR_BULK_HDR]) == 0U) {
                s_mb.err = NEXTOR_ERR_IO;
            }
        } else if (RawDisk_ReadSectors (lba, (uint16_t)n, s_buf) == 0U) {
            s_mb.err = NEXTOR_ERR_IO;
        } else {
            s_mb.res_r   = s_buf;
            s_mb.res_end = s_buf + n * NEXTOR_SECTOR_SIZE;
        }
        break;
    default:
        /* Breadcrumb markers are acknowledged and otherwise ignored. */
        if (is_mark (s_mb.cmd) == 0U && cmd != NEXTOR_CMD_NONE) {
            s_mb.err = NEXTOR_ERR_IO;
        }
        break;
    }

    /* A newer request arrived while this one ran: drop the stale answer. */
    if ((uint8_t)(s_mb.seq - seq) != 0U) {
        return;
    }
    __asm__ volatile ("fence rw, rw" ::: "memory");
    if (s_mb.res_end != s_mb.res_r) {
        s_mb.status |= NEXTOR_STAT_RX_AVAIL;
    }
    if (s_mb.err != NEXTOR_ERR_NONE) {
        s_mb.status |= NEXTOR_STAT_ERR;
    }
    s_mb.status |= NEXTOR_STAT_DONE;
}

/* --- Bus-interrupt entries ----------------------------------------------- */

void Nextor_WriteByte (uint16_t address, uint8_t value) {
    const uint8_t reg = NEXTOR_MBOX_REG (address);

    if (reg == NEXTOR_MBOX_CMD) {
        const uint8_t cmd = value;
        s_mb.status &= (uint8_t)~NEXTOR_STAT_DONE;
        if (cmd >= NEXTOR_CMD_MAX) {
            s_mb.cmd    = NEXTOR_CMD_NONE;
            s_mb.argend = is_mark (cmd) ? (s_buf + NEXTOR_MARK_ARGC) : s_buf;
        } else {
            s_mb.cmd    = cmd;
            s_mb.argend = s_buf + s_cmd_args[cmd];
        }
        s_mb.argp = s_buf;
        if (s_mb.argp == s_mb.argend) {
            s_mb.armed = 1U;
            s_mb.seq++;
        } else {
            s_mb.armed = 0U;
        }
        s_last_cmd_cyc = rdcycle();
        return;
    }
    if (reg == NEXTOR_MBOX_DATA) {
        uint8_t *p   = s_mb.argp;
        uint8_t *end = s_mb.argend;
        if (p < end) {
            *p = value;
            s_mb.argp = ++p;
            if (p == end) {
                if (s_mb.cmd == NEXTOR_CMD_WRITEM
                    && end == s_buf + NEXTOR_BULK_HDR) {
                    /* Header complete: extend the sink by the payload. */
                    const uint8_t n = s_buf[4];
                    if (n >= 1U && (uint16_t)n <= NEXTOR_BULK_MAX_SECTORS) {
                        s_mb.argend = s_buf + NEXTOR_BULK_HDR
                                    + (uint32_t)n * NEXTOR_SECTOR_SIZE;
                        return;
                    }
                }
                s_mb.armed = 1U;
                s_mb.seq++;
            }
        }
    }
}

uint8_t Nextor_ReadByte (uint16_t address) {
    const uint8_t reg = NEXTOR_MBOX_REG (address);

    switch (reg) {
    case NEXTOR_MBOX_CMD:
        return s_mb.status;
    case NEXTOR_MBOX_DATA: {
        uint8_t *p = s_mb.res_r;
        if (p < s_mb.res_end) {
            const uint8_t v = *p;
            s_mb.res_r = ++p;
            if (p == s_mb.res_end) {
                s_mb.status &= (uint8_t)~NEXTOR_STAT_RX_AVAIL;
            }
            return v;
        }
        return 0U;  /* empty FIFO reads as RXCNT_LO, like the original */
    }
    case NEXTOR_MBOX_RXCNT_LO:
        return (uint8_t)(s_mb.res_end - s_mb.res_r);
    case NEXTOR_MBOX_RXCNT_HI:
        return (uint8_t)((s_mb.res_end - s_mb.res_r) >> 8);
    case NEXTOR_MBOX_ERR:
        return s_mb.err;
    case NEXTOR_MBOX_VER:
        return NEXTOR_VERSION;
    default:
        return 0xFFU;
    }
}
