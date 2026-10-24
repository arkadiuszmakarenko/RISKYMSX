;*****************************************************************************
; driver.asm - Nextor 3.0 disk driver for the RISKYMSX2 cartridge.
;
; One device: the whole USB stick, as a raw block device.
;
; The Nextor kernel runs this driver out of the cartridge's driver bank
; (paged in at page 1 = 0x4000-0x7FFF) through the standard Nextor 3
; query / READ_WRITE contract; see the Nextor 3.0 Driver Development
; Guide (vendored, docs/) for the normative text and docs/NEXTOR_PLAN.md
; for the wiring. The shape of the driver - entry points, jump table,
; which queries exist, the "no writable variables" constraint on how
; results are moved - follows the SDK's own template and the SunriseIDE
; driver that the plan named as the model.
;
;-----------------------------------------------------------------------------
; What the drive is
;-----------------------------------------------------------------------------
; There is no filesystem anywhere in this path. The firmware exposes the
; stick as a plain SCSI medium and answers sector requests out of raw
;_disk.c; the driver is a thin translator between the kernel's queries
; and that mailbox.
;
; That has one consequence the kernel cares about a great deal and that
; is easy to get wrong, so it is stated here and repeated at
; params_build()'s wire counterpart (firmware/User/nextor.c):
;
;   the device is NOT floppy-flagged.
;
; A floppy-flagged device makes the kernel map a drive straight onto
; sector 0 and skip the partition scan entirely. The stick has a
; partition table at sector 0, so flagging it as a floppy is precisely
; what makes the kernel read the MBR as a FAT boot sector and find
; nothing. Getting the flag right is most of what makes a raw stick
; bootable through Nextor.
;
;-----------------------------------------------------------------------------
; The mailbox
;-----------------------------------------------------------------------------
; All hardware access goes through the cart-bus window at 0x7FF0..0x7FF5,
; decoded BY ADDRESS by the firmware's Cart_EXTI0_Nextor_Handler in every
; paged-in bank:
;
;   0x7FF0  read  STATUS   bit7 READY (fw alive) / bit6 DONE /
;                          bit5 ERR / bit4 RX_AVAIL
;          write CMD      starts a command (firmware clears DONE)
;   0x7FF1  DATA           read: pop result byte / write: push arg byte
;   0x7FF2  RXCNT_LO      result bytes still to drain (informational)
;   0x7FF3  RXCNT_HI
;   0x7FF4  ERR            0 none / 1 no medium / 2 I/O failed / 3 reserved
;   0x7FF5  VER            protocol revision byte
;
; Protocol revision 4 ("RNX4"). Write the command byte to CMD, then its
; arguments to DATA; when DONE appears in STATUS, read the answer by
; popping DATA the number of times the command produces. MB_POLL bounds
; the wait (~2.9 s), so a firmware that does not answer costs a timeout,
; never a hang.
;
;   0x00 HANDSHAKE  ->      5 bytes: "RNX4" + revision
;   0x01 CAPACITY   ->     12 bytes: the Nextor device parameter block
;   0x02 STATUS     <-      1 byte:  0 none / 1 ready / 2 changed
;   0x03 READ       <- LBA(4)              -> 512 bytes
;   0x04 WRITE      <- LBA(4) + 512 bytes -> nothing
;   0x05 ABORT      ->      nothing
;   0x06 STAPEEK    <-      1 byte, does NOT consume the changed latch
;   0x07 IDENT      ->     28 bytes: INQUIRY vendor(8) + product(20)
;   0x08 WRITEM     <- LBA(4) + count(1) + count*512 -> nothing
;   0x09 READM      <- LBA(4) + count(1) -> count*512 bytes
;
; No device byte on any command. There is one device, so the byte had
; nothing to select; dropping it is what makes revision 3 rather than 2,
; and the revision byte in the handshake is what stops a stale v2 driver
; from driving this firmware (or the reverse) and silently decoding every
; argument in the wrong place.
;
; Revision 3 was ONE SECTOR PER COMMAND, and deliberately so: the per-BYTE
; cart-bus transfer (~4 ms per 512) dominated a CBW+CSW round trip
; measured in hundreds of microseconds, so a wider command saved only the
; small part. That arithmetic inverted on the test stick this firmware
; was tuned against: its FTL charges its WRITE(10) commit 210-1075 ms PER
; SCSI COMMAND (rare stalls past 4.5 s), so one commit per sector made a
; file copy crawl at ~1 sector per second while the bus transfer was a
; rounding error. Revision 4 adds the bulk pair: up to MB_BULK_MAX
; sectors in ONE command, sized so the ~125 ms data push plus the
; firmware's single ~2 s attempt answers inside the ~2.9 s MB_POLL
; window. The one-sector commands remain as the FALLBACK - on any bulk
; failure READ_WRITE retries the same range sector by sector, and they
; are the whole protocol for a revision-3 pair.
;
;-----------------------------------------------------------------------------
; The ROM driver constraint
;-----------------------------------------------------------------------------
; A ROM driver has no writable variables. A `ds` buffer inside this
; image is not a buffer: the Z80 discards `ld (hl),a` into ROM, so the
; destination keeps whatever the ROM file contains. Everything here obeys
; that:
;
;   - results that must be KEPT are written to RAM the kernel handed us
;     (the 12-byte parameter block, the GET_STRING buffer, and the
;     sector buffer in READ_WRITE), never to a local `ds`;
;   - results that only have to be CHECKED are compared against ROM as
;     they stream past (MB_RESULT_IS, used by the handshake);
;   - values that have to survive a call are carried in registers or on
;     the stack, never in a static.
;
; This is not a stylistic preference: a `ds`-and-then-write design is
; exactly the bug that shipped in the previous revision, where the
; parameter block was "filled" at a ROM address, always arrived at the
; kernel as the twelve zeros baked into the image, and the kernel read
; it as "no media" forever.
;*****************************************************************************

	INCLUDE asm/macros/undoc.inc	;documented-only expansions (unused here)
	INCLUDE asm/constants/driver_result_codes.inc
	INCLUDE asm/constants/dos_errors.inc

	module DRIVER_QUERY
	INCLUDE asm/constants/driver_driver_queries.inc
	endmod

	module DEVICE_QUERY
	INCLUDE asm/constants/driver_device_queries.inc
	endmod

;-----------------------------------------------------------------------------
; Mailbox window
;-----------------------------------------------------------------------------

MBOX_CMD	equ	7FF0h	;write = command byte
MBOX_STAT	equ	7FF0h	;read = STATUS bits
MBOX_DATA	equ	7FF1h	;data pop/push port
MBOX_RXCL	equ	7FF2h	;result bytes available, low
MBOX_RXCH	equ	7FF3h	;result bytes available, high
MBOX_ERR	equ	7FF4h	;last error code
MBOX_VER	equ	7FF5h	;protocol revision byte

; STATUS bits. Only MBST_DONE is read here - the driver polls for the
; flag and looks at nothing else - but all four are named so this copy
; of the protocol is complete and can be checked against nextor.h by
; reading rather than by memory.

MBST_READY	equ	80h
MBST_DONE	equ	40h
MBST_ERR	equ	20h
MBST_RXAVL	equ	10h

; Commands. MB_ABORT is never issued: no mailbox request is ever left
; in flight, because MB_POLL is bounded and the firmware is not allowed
; to make the Z80 wait unboundedly for anything.

MB_HANDSHAKE	equ	00h
MB_CAPACITY	equ	01h
MB_STATUS	equ	02h
MB_READ		equ	03h
MB_WRITE	equ	04h
MB_WRITEM	equ	08h		;bulk: LBA(4)+count(1)+count*512 -> none
MB_READM	equ	09h		;bulk: LBA(4)+count(1) -> count*512
MB_ABORT	equ	05h
MB_STAPEEK	equ	06h
MB_IDENT	equ	07h

INIT_STATUS_RETRIES	equ	0Ah

; Breadcrumbs use a reserved mailbox command followed by ONE byte in B -
; the register value the step is about - with no result data. MB_MARK is
; intentionally silent in normal builds; diagnostic MB_MARK_WAIT waits for
; DONE so the firmware can log each marker without racing the next request.
;
; They exist because a driver that dies half way through a routine
; produces NO mailbox traffic at all, and "no traffic" cannot be told
; apart from "the kernel never called in". With these, the difference is
; one line.
;
; The byte is the second half of the value. A bare name says "the kernel
; asked for a device that does not exist"; a name plus a value says which
; device number the kernel actually asked for, which is the difference
; between a fix and a guess. Every marker below therefore states in its
; comment what B carries, and the two tables - these and s_mark_name[] in
; firmware/User/nextor.c - have to stay in step.
;
; Their wire command bytes occupy 0x20..0x5F, clear of the eight real
; commands and the 0x08..0x1F range a corrupted stack is likelier to
; produce. Keep these and s_mark_name[] in firmware/User/nextor.c in step.
MB_MK_RW_ENTER		equ	00h	; B = device number the kernel passed
MB_MK_RW_FRAME		equ	01h	; B = sector count requested
MB_MK_RW_DIR		equ	02h	; B = direction read from C bit 0
MB_MK_RW_MEDIUM		equ	03h	; B = the STATUS byte
MB_MK_RW_SECTOR		equ	04h	; B = low byte of the sector number
MB_MK_RW_ALLDONE	equ	05h	; B = sectors actually transferred
MB_MK_RW_IDEVN		equ	06h	; B = the rejected device number
MB_MK_RW_NRDY		equ	07h	; B = the device number
MB_MK_RW_DISK		equ	08h	; B = the firmware's error code
MB_MK_RW_COERCE		equ	09h	; B = the number we are about to override
MB_MK_DEVQ		equ	0Ah	; B = device query index
MB_MK_DQ_PARAMS		equ	0Bh	; B = 0 if HL=0, nonzero if buffer supplied
MB_MK_DQ_PARAMS_HL0	equ	0Ch
MB_MK_DQ_PARAMS_MB	equ	0Dh
MB_MK_DQ_PARAMS_TO	equ	0Eh
MB_MK_DQ_STATUS		equ	0Fh	; B = device number
MB_MK_DQ_AVAIL		equ	10h	; B = device number
MB_MK_DRVQ		equ	11h	; B = driver query index
MB_MK_RW_BUF0		equ	12h	; B = byte 0 of the sector just read back
MB_MK_RW_BUFN		equ	13h	; B = byte 511 of that same sector
MB_MK_RW_BUFL		equ	14h	; B = low byte of the destination address
MB_MK_RW_BUFH		equ	15h	; B = high byte of the destination address
MB_MK_RW_STAT		equ	16h	; B = MBR partition status at +446
MB_MK_RW_TYPE		equ	17h	; B = MBR partition type at +450
MB_MK_RW_START		equ	18h	; B = first partition-LBA byte at +454
MB_MK_RW_SIGL		equ	19h	; B = sector byte at +510
MB_MK_RW_SIGH		equ	1Ah	; B = sector byte at +511
MB_MK_RW_BADBUFL	equ	1Bh	; B = low byte of an invalid buffer
MB_MK_RW_BADBUFH	equ	1Ch	; B = high byte of an invalid buffer
MB_MK_RW_BUFPG		equ	1Dh	; B = high byte of the caller's HL buffer.
				;     81h = the page-2 view of a direct
				;     to-DTA transfer (RW_MANY), anything
				;     else is a kernel buffer (SECBUF) -

; Initialization diagnostics (enabled with PROBES=1).
MB_MK_DQ_INIT_DEVICE equ 1Eh ; B = device number on device query 2
MB_MK_DQ_INIT_BUFFER equ 1Fh ; B = 0 validate-only / 1 buffer supplied
MB_MK_DQ3_DRIVES     equ 20h ; B = available drive count at driver query 3
MB_MK_DQ3_FLAGS      equ 21h ; B = input flags at driver query 3
MB_MK_DQ4_FLAGS      equ 22h ; B = input flags at driver query 4
MB_MK_DQ5_MAXDEV     equ 23h ; B = maximum device count returned
MB_MK_RW_BULKW       equ 24h ; B = sectors in the attempted bulk write
MB_MK_RW_BULKR       equ 25h ; B = sectors in the attempted bulk read

;answer sizes. CAPACITY is the Nextor device parameter block verbatim;
; IDENT is INQUIRY vendor + product, both space-padded by the firmware so
;the driver can treat them as fixed-width strings.
MB_PARAMS_LEN	equ	12
MB_HS_LEN	equ	5
MB_IDENT_VENDOR_LEN	equ	8
MB_IDENT_MODEL_LEN	equ	20
MB_IDENT_LEN	equ	MB_IDENT_VENDOR_LEN + MB_IDENT_MODEL_LEN

;sector size. This is the driver's own sector size and therefore the
;mailbox's transfer width; the firmware's READ CAPACITY handling rejects
;a stick whose block size is anything else, so the two cannot disagree.
MB_SECTOR_SIZE	equ	200h	;512

;The firmware's bulk cap (nextor.h NEXTOR_BULK_MAX_SECTORS). Was 32 sectors
;= 16 KiB per bulk command: the data push is ~125 ms at ~8 us/byte and
;the firmware's single WRITE(10) attempt is bounded at ~2 s, so the
;answer always lands inside the ~2.9 s MB_POLL window even on the slow
;test stick. Must match nextor.h - a mismatch means every bulk command
;times out and falls back, which still works, but silently.
MB_BULK_MAX	equ	08h		;8 sectors per bulk command (CH32V30x RAM)

;Device numbers. Both the kernel and driver query 5 count up from 1, so
;MB_DEVICE_COUNT is a count AND a maximum, and it must equal
;NEXTOR_DEVICE_COUNT in firmware/User/nextor.h.
MB_DEV_FIRST	equ	1
MB_DEVICE_COUNT	equ	1

;mailbox ERR port values. Only the first three are reachable; the fourth
;is the firmware's reserved slot.
MBERR_NONE	equ	00h
MBERR_NOMEDIA	equ	01h
MBERR_IO	equ	02h

;firmware handshake magic and the revision this driver speaks.
MB_MAGIC0	equ	"R"
MB_MAGIC1	equ	"N"
MB_MAGIC2	equ	"X"
MB_MAGIC3	equ	"4"		;the revision character - moves with the
					;protocol revision below

;protocol revision this driver is written against. The firmware answers
;the handshake with ITS revision byte and MB_EXPECT compares it, so a
;driver built against one revision talking to a firmware built against
;another is rejected at boot - loudly, with the kernel skipping this
;driver - instead of answering every command for the wrong device.
;Must equal NEXTOR_VERSION in firmware/User/nextor.h. Revision 4 adds
;the bulk transfer pair (0x08 WRITEM / 0x09 READM).
MB_PROTOCOL_V4	equ	04h

;-----------------------------------------------------------------------------
; ROM driver boilerplate
;-----------------------------------------------------------------------------
;
; mknexrom expects the driver file to start with 256 dummy bytes (it
; overwrites them with the kernel's common bank header code), so the
; actual driver code starts at 4100h.

	org	4000h
	ds	4100h-$,0

DRIVER_START:

	;Driver signature

	db	"NEXTORv3_DRIVER",0

	;Jump table

	jp	TIMER_INT
	jp	OEMSTAT
	jp	BASDEV
	jp	EXTBIO
	jp	DRIVER_QUERY
	jp	DEVICE_QUERY
	jp	CUSTOM_DRIVER_QUERY
	jp	CUSTOM_DEVICE_QUERY
	jp	READ_WRITE
	jp	RESERVED_0
	jp	RESERVED_1
	jp	RESERVED_2
	jp	DIRECT_0
	jp	DIRECT_1
	jp	DIRECT_2
	jp	DIRECT_3
	jp	DIRECT_4

	;--- Timer interrupt routine: not hooked (query 3 returns B=0).

TIMER_INT:
	ret
	ret
	ret

	;--- BASIC expanded statement ("CALL") handler: none.

OEMSTAT:
	scf
	ret
	ret

	;--- BASIC expanded devices: none.

BASDEV:
	scf
	ret
	ret

	;--- Extended BIOS hook: not hooked (query 3 returns B=0).

EXTBIO:
	ret
	ret
	ret

	;* Jump table entries reserved for future use.

RESERVED_0:
RESERVED_1:
RESERVED_2:
	ret

	;* Direct call entry points: none.

DIRECT_0:
DIRECT_1:
DIRECT_2:
DIRECT_3:
DIRECT_4:
	ret

;-----------------------------------------------------------------------------
; Driver queries
;-----------------------------------------------------------------------------
;
;   A = query index
;
; Queries 1-5 are the ones this Nextor 3.0 kernel issues. Device query 8
; (READ_BEFORE_INIT, which some later Nextor builds ask for) is NOT
; implemented: this kernel's source issues only device queries 1-4, and a
; query that is never asked for does not need a handler. It falls off the
; end of the device-query dispatcher into RESULT_NOT_IMPLEMENTED, which
; is the correct answer if a future kernel does ask - the guide defines
; that result as "treat as success with default values".

DRIVER_QUERY:
	push	bc		;A = the query index and B is the index
	push	af		;to report, so both are pushed
	ld	b,a
	ld	a,MB_MK_DRVQ
	call	MB_MARK
	pop	af
	pop	bc
	dec	a
	jp	z,DO_DRVQ_GET_VERSION
	dec	a
	jp	z,DO_DRVQ_GET_STRING
	dec	a
	jp	z,DO_DRVQ_GET_INIT_PARAMS
	dec	a
	jp	z,DO_DRVQ_INIT
	dec	a
	jp	z,DO_DRVQ_GET_MAX_DEVICE
	ld	a,RESULT_NOT_IMPLEMENTED
	ret


; Driver query 1: Get driver version number
; Out: A = RESULT_OK, version in B.C.D = 1.0.1

DO_DRVQ_GET_VERSION:
	ld	bc,0100h
	ld	d,1
	xor	a
	ret


; Driver query 2: Get driver information string
; In:  B = string index (1 = driver name), D = buffer size, HL = buffer
; Out: A = RESULT_OK / RESULT_TRUNCATED_STRING / RESULT_NOT_IMPLEMENTED
;
; All five documented indices come from ROM: this driver identifies
; itself with fixed strings and has no updatable identity.

DO_DRVQ_GET_STRING:
	ld	a,b
	ld	b,d
	ex	de,hl
	dec	a
	ld	hl,MSG_DRIVER_NAME
	jp	z,OUTPUT_STRING
	dec	a
	ld	hl,MSG_DRIVER_AUTHOR
	jp	z,OUTPUT_STRING
	dec	a
	ld	hl,MSG_HARDWARE_NAME
	jp	z,OUTPUT_STRING
	dec	a
	ld	hl,MSG_HARDWARE_AUTHOR
	jp	z,OUTPUT_STRING
	dec	a
	ld	hl,MSG_SERIAL_NUMBER
	jp	z,OUTPUT_STRING
	ld	a,RESULT_NOT_IMPLEMENTED
	ret


; Driver query 3: Get driver initialization parameters
;
; In:  HL = available work area size, B = available drives, C = flags,
;      DE = print-character routine
; Out: A = RESULT_OK / RESULT_INIT_ERROR, B = flags (0 = no hooks),
;      HL = page-3 space required (0)
;
; The mailbox handshake IS the hardware detection: on a foreign cart
; (openMSX generic ASCII16, another flash cart, no firmware) the CMD write
; lands on ordinary ROM data, nothing answers, MB_POLL times out and we
; return RESULT_INIT_ERROR - the kernel then skips this driver gracefully
; instead of wedging the boot. That is the one behaviour worth keeping
; from the previous revision, where it was verified against a cart that
; has no mailbox at all.
;
; DE is the print callback and MB_HANDSHAKE_CHK destroys it while handing
; back its diagnosis in B/C, so it is saved across the call: the success
; path pops it here, the failure path pops it in DO_DRVQ_INIT_FAIL, and
; the kernel's stack is balanced on both exits.

DO_DRVQ_GET_INIT_PARAMS:
	push	de
	IFDEF RW_PROBES
	push	bc		;preserve the kernel's inputs across the handshake
	ENDIF
	call	MB_HANDSHAKE_CHK
	jr	nc,DO_DRVQ_GET_INIT_PARAMS_HS_OK
	IFDEF RW_PROBES
	pop	hl		;discard saved inputs without losing failure B/C
	ENDIF
	jp	DO_DRVQ_INIT_FAIL
DO_DRVQ_GET_INIT_PARAMS_HS_OK:
	IFDEF RW_PROBES
	pop	bc
	ENDIF
	pop	de
IFDEF RW_PROBES
	; Record the kernel's allocation inputs only after confirming that the
	; mailbox answers. C bit 5 is the reduced-drive-count boot option.
	push	bc
	push	de
	ld	a,MB_MK_DQ3_DRIVES
	call	MB_MARK_WAIT
	pop	de
	pop	bc
	push	bc
	push	de
	ld	b,c
	ld	a,MB_MK_DQ3_FLAGS
	call	MB_MARK_WAIT
	pop	de
	pop	bc
ENDIF
	; The mailbox is alive, so query the cached USB-medium state once
	; during boot. A zero means no usable USB drive was discovered.
	; Keep the driver installed so later STATUS polling can detect insertion.
	ld	b,INIT_STATUS_RETRIES
DO_DRVQ_INIT_STATUS:
	push	bc
	push	de
	ld	a,MB_STATUS
	call	MB_CMD0_1BYTE
	pop	de
	pop	bc
	jr	c,DO_DRVQ_INIT_STATUS_NEXT
	or	a
	jr	nz,DO_DRVQ_INIT_OK
DO_DRVQ_INIT_STATUS_NEXT:
	dec	b
	jr	z,DO_DRVQ_INIT_STATUS_FAIL
	push	bc
	push	de
	ld	hl,MSG_MEDIA_RETRY
	call	PRINT_WITH_DE
	pop	de
	pop	bc
	jr	DO_DRVQ_INIT_STATUS_WAIT
DO_DRVQ_INIT_STATUS_FAIL:
	ld	hl,MSG_NO_MEDIA
	call	PRINT_WITH_DE
	jr	DO_DRVQ_INIT_OK
DO_DRVQ_INIT_STATUS_WAIT:
	; Give the firmware main loop time to complete USB discovery.
	; Roughly 1 second at 3.58 MHz between status attempts.
	push	bc
	push	de
	ld	d,4
DO_DRVQ_INIT_STATUS_WAIT_OUTER:
	ld	c,0FFh
	; C counts the inner delay batches; do not reinitialise it while
	; counting down, or this loop never completes.
DO_DRVQ_INIT_STATUS_WAIT_MIDDLE:
	ld	b,0
DO_DRVQ_INIT_STATUS_WAIT_INNER:
	djnz	DO_DRVQ_INIT_STATUS_WAIT_INNER
	dec	c
	jr	nz,DO_DRVQ_INIT_STATUS_WAIT_MIDDLE
	dec	d
	jr	nz,DO_DRVQ_INIT_STATUS_WAIT_OUTER
	pop	de
	pop	bc
	jr	DO_DRVQ_INIT_STATUS
DO_DRVQ_INIT_OK:
	call	INIT_PARTITION_REPORT
	xor	a		;RESULT_OK
	ld	b,0		;no TIMER_INT / EXTBIO hooks
	ld	hl,0		;no page-3 work area
	ret


;--- Report raw primary-partition candidates during driver query 3.
;    This driver can read the MBR before Nextor's automapper runs, but the
;    driver API provides no callback with the final drive-letter mapping.
;    Therefore each line means only that an active FAT12/16 primary MBR
;    entry was found; the kernel validates the filesystem and MAP shows the
;    actual letters.
;
;    The MBR is read twice because this ROM driver has no writable sector
;    buffer: the first pass validates 55AA, and the second streams the four
;    status/type pairs directly from the mailbox without storing the sector.

INIT_PARTITION_REPORT:
	ld	a,MB_STAPEEK
	call	MB_CMD0_1BYTE
	jr	c,IPR_DONE
	or	a
	jr	z,IPR_DONE
	call	INIT_MBR_READ
	jr	c,IPR_DONE
	ld	c,0FFh		;skip 510 bytes, leaving the MBR signature
	call	MB_DROP
	ld	c,0FFh
	call	MB_DROP
	ld	a,(MBOX_DATA)
	cp	55h
	jr	nz,IPR_DONE
	ld	a,(MBOX_DATA)
	cp	0AAh
	jr	nz,IPR_DONE

	call	INIT_MBR_READ
	jr	c,IPR_DONE
	ld	c,0FFh		;446 bytes to the first partition entry
	call	MB_DROP
	ld	c,0BFh
	call	MB_DROP
	ld	hl,MSG_MBR_P1
	call	INIT_REPORT_MBR_ENTRY
	ld	hl,MSG_MBR_P2
	call	INIT_REPORT_MBR_ENTRY
	ld	hl,MSG_MBR_P3
	call	INIT_REPORT_MBR_ENTRY
	ld	hl,MSG_MBR_P4
	call	INIT_REPORT_MBR_ENTRY
	ret
IPR_DONE:
	ret

;--- Issue a raw one-sector read of LBA 0. Cy=1 means timeout or I/O error.
INIT_MBR_READ:
	ld	hl,MB_ZERO_LBA
	ld	a,MB_READ
	ld	b,4
	call	MB_SEND
	call	MB_POLL
	ret	c
	ld	a,(MBOX_STAT)
	and	MBST_ERR
	jr	nz,IMR_ERROR
	or	a
	ret
IMR_ERROR:
	scf
	ret

;--- Stream one 16-byte primary MBR entry. Print its size and a Pn
;    initialized line only for an active FAT12/16 type. HL points at the
;    line prefix on entry.
INIT_REPORT_MBR_ENTRY:
	push	iy		;preserve caller IY; IY holds the MiB result below
	push	hl		;line prefix survives while the entry is streamed
	ld	a,(MBOX_DATA)	;partition status byte
	push	af
	ld	c,3
	call	MB_DROP		;skip starting CHS
	ld	a,(MBOX_DATA)	;partition type
	push	af
	ld	c,7
	call	MB_DROP		;skip end CHS and starting LBA
	ld	a,(MBOX_DATA)	;partition size, little-endian sectors
	ld	l,a
	ld	a,(MBOX_DATA)
	ld	h,a
	ld	a,(MBOX_DATA)
	ld	c,a
	ld	a,(MBOX_DATA)
	ld	b,a
	ld	a,b
	and	0F8h		;MiB above 65535 cannot fit the decimal helper
	jr	nz,IRME_LARGE
	ld	a,h		;MiB low byte = sector-count bits 11..18
	srl	a
	srl	a
	srl	a
	ld	l,a
	ld	a,c
	and	07h
	rlca
	rlca
	rlca
	rlca
	rlca
	or	l
	ld	l,a
	ld	a,c		;MiB high byte = sector-count bits 19..26
	srl	a
	srl	a
	srl	a
	ld	h,a
	ld	a,b
	and	07h
	rlca
	rlca
	rlca
	rlca
	rlca
	or	h
	ld	h,a
	push	hl
	pop	iy
	jr	IRME_SIZE_READY
IRME_LARGE:
	ld	iy,0FFFFh	;print the explicit lower-bound marker below
IRME_SIZE_READY:
	pop	af		;type byte
	cp	01h		;FAT12
	jr	z,IRME_TYPE_OK
	cp	04h		;FAT16 small
	jr	z,IRME_TYPE_OK
	cp	06h		;FAT16
	jr	z,IRME_TYPE_OK
	cp	0Eh		;FAT16 LBA
	jr	z,IRME_TYPE_OK
	pop	af		;unsupported type: discard saved status
	pop	hl		;discard line prefix
	pop	iy		;restore caller IY
	xor	a
	ret
IRME_TYPE_OK:
	pop	af		;status byte: bit 7 is active
	and	80h
	jr	z,IRME_NOT_ACTIVE
	pop	hl		;line prefix
	call	PRINT_WITH_DE
	push	iy
	pop	hl
	ld	a,h
	cp	0FFh
	jr	nz,IRME_PRINT_SIZE
	ld	a,l
	cp	0FFh
	jr	z,IRME_PRINT_LARGE
IRME_PRINT_SIZE:
	call	PRINT_MIB16_DE
	ld	hl,MSG_MIB_SUFFIX
	call	PRINT_WITH_DE
	jr	IRME_PRINTED
IRME_PRINT_LARGE:
	ld	hl,MSG_MIB_TOO_LARGE
	call	PRINT_WITH_DE
IRME_PRINTED:
	pop	iy		;restore caller IY
	ld	a,1
	ret
IRME_NOT_ACTIVE:
	pop	hl		;discard line prefix
	pop	iy		;restore caller IY
	xor	a
	ret


;--- Reached from query 3 AND query 4, and DE holds the print callback in
;    both, so this is the one place that can report "the driver ran but
;    the mailbox did not answer", whichever query got that far.
;
;    A on entry is the MB_HANDSHAKE_CHK failure reason (0 = no DONE,
;    1 = wrong bytes); the two get distinct messages because they are
;    different faults and the fix differs.
;
;    The "got / wanted" pair is fiddly to print and worth spelling out.
;    MB_RESULT_IS left the first mismatching byte's index in B and the
;    byte it read in C, and the byte that SHOULD have been there is
;    MB_EXPECT[B] - read back out of ROM here, because D is not an option:
;    D is half of DE and DE is the callback this whole routine prints
;    through.
;
;    The three print helpers clobber everything except DE (restored around
;    its stack trampoline) and IY: PRINT_HEXA_DE writes A, B and C on the
;    way to turning a nibble into two digits. So the "wanted" byte is read
;    out of ROM and pushed BEFORE the first print and popped before the
;    last, and B and C are only read in the gaps the earlier prints left
;    them in. Printing them in the obvious order instead reports
;    MB_EXPECT[0] every time, which is the one value the reader already
;    knows and the least useful one on the line.
;
;    The callback itself is on the stack: both callers push DE before the
;    handshake and this entry pops it.
;
;    Note this also means a failing query 3 keeps query 4 from ever
;    running: the kernel prints its own "Driver initialization failed!"
;    and jumps to NO_DRIVE, which is why a bad handshake shows up as no
;    banner and no wait at all.

DO_DRVQ_INIT_FAIL:
	pop	de		;the callback the caller saved before the handshake
	or	a		;Cy=1: A was 0 (no DONE) or 1 (bad bytes)
	jr	nz,DO_DRVQ_INIT_BADMAGIC
	ld	hl,MSG_NO_ANSWER
	call	PRINT_WITH_DE
	jr	DO_DRVQ_INIT_RETURN
DO_DRVQ_INIT_BADMAGIC:
	;Report index / got / wanted. Three bytes say as much as a dump of all
	;five and, unlike a dump, they survive: the driver has nowhere to put
	;a dump, because every buffer it could use is in ROM.
	;
	;The "wanted" byte is resolved and stacked FIRST, because B and C do
	;not survive the two print helpers - see the note above.
	ld	hl,MB_EXPECT		;still in ROM: only ever read
	ld	a,b		;B = the index MB_RESULT_IS left there
	ld	b,0
	ld	c,a
	add	hl,bc
	ld	a,(hl)
	push	af			;stacked across the prints

	ld	hl,MSG_BAD_MAGIC
	call	PRINT_WITH_DE
	ld	a,b			;still the index: no print has run yet
	call	PRINT_HEXA_DE		;index of the first differing byte
	ld	hl,MSG_GOT
	call	PRINT_WITH_DE
	ld	a,c			;still the byte that came back:
					;PRINT_WITH_DE does not write C
	call	PRINT_HEXA_DE
	ld	hl,MSG_WANT
	call	PRINT_WITH_DE
	pop	af
	call	PRINT_HEXA_DE		;the byte that should have been there
DO_DRVQ_INIT_RETURN:
	;Every print above is immediately preceded by its own ld hl, so HL is
	;always a known-good string pointer here; PRINT_WITH_DE walks HL and
	;would otherwise run off the end of the message.
	ld	hl,MSG_CRLF
	call	PRINT_WITH_DE
	ld	a,RESULT_INIT_ERROR
	ld	b,0
	ld	hl,0
	ret


; Driver query 4: Initialize driver
;
; Prints the banner BEFORE the handshake gate, deliberately. The banner
; is the proof that the kernel reached this driver at all; the handshake
; verdict that follows is the proof that the firmware mailbox answered.
; Printing after the gate collapses those two facts into one, which is
; exactly the ambiguity this ordering removes:
;
;   banner + OK                 -> driver runs, mailbox answers, all good
;   banner + "no mailbox"       -> driver runs, firmware not answering
;   nothing at all              -> driver not called, or the print path
;                                  itself is broken

DO_DRVQ_INIT:
IFDEF RW_PROBES
	push	bc		;preserve query 4's flags across the handshake
ENDIF
	ld	hl,INIT_MSG
	call	PRINT_WITH_DE		;unconditional
	push	de			;same reason as in query 3 above
	call	MB_HANDSHAKE_CHK	;re-verify (queries 3 and 4 run back to back)
	jr	nc,DO_DRVQ_INIT_HS_OK
	IFDEF RW_PROBES
	pop	de		;save the callback for DO_DRVQ_INIT_FAIL
	pop	hl		;discard saved flags without losing failure B/C
	push	de
	ENDIF
	jp	DO_DRVQ_INIT_FAIL
DO_DRVQ_INIT_HS_OK:
	pop	de
	IFDEF RW_PROBES
	pop	bc
	; Query 4 receives the same boot-option flags as query 3.
	push	bc
	push	de
	ld	b,c
	ld	a,MB_MK_DQ4_FLAGS
	call	MB_MARK_WAIT
	pop	de
	pop	bc
	ENDIF
	xor	a		;RESULT_OK
	ret


; Driver query 5: Get maximum supported device number
;
; The kernel counts UP from 1 to this answer, so it is a count AND a
; maximum: device numbers are 1..MB_DEVICE_COUNT with no gaps. Keep this,
; DEVICE_QUERY's own range test and the READ_WRITE range test in step -
; the firmware has its own copy of the count in NEXTOR_DEVICE_COUNT, and
; the handshake is what keeps the two from drifting.

DO_DRVQ_GET_MAX_DEVICE:
	ld	a,RESULT_OK
	ld	b,MB_DEVICE_COUNT
IFDEF RW_PROBES
	push	af
	push	bc
	push	de
	ld	a,MB_MK_DQ5_MAXDEV
	call	MB_MARK_WAIT
	pop	de
	pop	bc
	pop	af
ENDIF
	ret


;-----------------------------------------------------------------------------
; Device queries
;-----------------------------------------------------------------------------

;--- DEVICE_QUERY dispatcher.
;    In:  A = query index, C = device number.
;    The device number is validated BEFORE the index is consumed
;    (.IDEVN -> RESULT_INVALID_DEVICE regardless of the query index), and
;    C is left intact so each device query still knows which device it is
;    for - the kernel walks devices 1..MAX_DEVICE by calling this with a
;    different C, and a driver that answered every C alike would look
;    like it had that many identical drives.
;
;    The one handler that gives C up is device query 1, which uses it as
;    the "how many answer bytes to skip" counter (DQS_INQ). Nothing
;    downstream of it in that handler needs the device number, and the
;    range test above has already consumed it.

DEVICE_QUERY:
	push	af		;save the query index
	ld	a,c
	cp	MB_DEV_FIRST	;C < 1: device numbers start at 1
	jr	c,DQ_DEVN
	cp	MB_DEVICE_COUNT+1
	jr	nc,DQ_DEVN
	pop	af		;recover the query index
	push	bc		;B is an INPUT to device query 1 (the
	push	af		;string index) and C is the device
	ld	b,a			;number, so both have to survive the
					;marker. The query index gets its own
					;log line because dispatching on it
					;IS this routine's whole job: a log
					;that never names the index cannot
					;tell "the kernel asked for query 2
					;and we answered" from "the kernel
					;asked for something else".
	ld	a,MB_MK_DEVQ
	call	MB_MARK		;the kernel reached a device query at all
	pop	af
	pop	bc
	dec	a
	jp	z,DO_DEVQ_GET_STRING
	dec	a
	jp	z,DO_DEVQ_GET_PARAMS
	dec	a
	jp	z,DO_DEVQ_GET_STATUS
	dec	a
	jp	z,DO_DEVQ_GET_AVAILABILITY
	dec	a
	jp	z,DO_DEVQ_GET_FORMAT_CHOICES
	dec	a
	jp	z,DO_DEVQ_DO_FORMAT
	dec	a
	jp	z,DO_DEVQ_STOP_MOTOR
	ld	a,RESULT_NOT_IMPLEMENTED
	ret
DQ_DEVN:
	pop	af		;discard (keep the stack clean)
	ld	a,RESULT_INVALID_DEVICE
	ret


;--- Device query 1: Get device information string.
;     In:  B = string index, D = buffer size, HL = buffer, C = device
;     Out: A = RESULT_OK / RESULT_TRUNCATED_STRING / RESULT_NOT_IMPLEMENTED
;
;     1 = manufacturer, 2 = medium name: taken from USB INQUIRY, which the
;     firmware read once and hands back with IDENT. These are the only
;     strings that can say anything useful about a stick, and they are
;     the ones Nextor's device screen shows.
;     3 = serial number: MSC has no standard place to put one, so this is
;         RESULT_NOT_IMPLEMENTED and the kernel substitutes a placeholder.
;     4 = device name: this driver's own name for the drive, from ROM.

DO_DEVQ_GET_STRING:
	ld	a,b
	or	a
	jr	z,DQS_NONE		;index 0 is not a documented index
	dec	a
	jr	z,DQS_MANUF		;1: INQUIRY vendor, answer bytes 0..7
	dec	a
	jr	z,DQS_MEDIUM		;2: INQUIRY product, answer bytes 8..27
	dec	a
	dec	a
	jr	nz,DQS_NONE		;4: the driver's own device name
	ld	a,b
	ld	b,d
	ex	de,hl
	ld	hl,MSG_DEVICE_NAME
	jp	OUTPUT_STRING
DQS_NONE:
	ld	a,RESULT_NOT_IMPLEMENTED
	ret

DQS_MANUF:
	ld	c,0			;C, not A: DQS_INQ reads the skip count
	ld	e,MB_IDENT_VENDOR_LEN	;from C on the way in
	jr	DQS_INQ
DQS_MEDIUM:
	ld	c,MB_IDENT_VENDOR_LEN	;vendor first, then product
	ld	e,MB_IDENT_MODEL_LEN

;--- Shared body of the two INQUIRY-backed strings.
;     On entry: C = how many answer bytes to skip, E = field length.
;               HL = the kernel's buffer, D = its size.
;
;     The answer is 28 bytes and we want a slice of it, so the routine
;     drains what it does not want. It has to be a slice-and-drain rather
;     than a straight copy because a ROM driver cannot buffer the answer:
;     the only RAM in reach is the kernel's own buffer, and it is the
;     caller's, sized for the string, not for the whole INQUIRY.

DQS_INQ:
	;Result code first, and pushed: MB_SEND / MB_POLL / MB_DROP /
	;MB_GETRES below clobber every register that could carry it.
	;
	;A full field needs E characters plus a terminator, so the answer
	;counts as truncated unless the buffer is strictly larger than the
	;field. D=0 means "do not fill the buffer at all, just tell me
	;whether the string exists", per the guide, so it is a truncation
	;like any other - and the only case where nothing is written.
	ld	a,d
	cp	e
	jp	c,DQS_NOTFIT
	ld	a,d
	cp	e
	jp	z,DQS_NOTFIT
	xor	a			;RESULT_OK
	jp	DQS_FLAG
DQS_NOTFIT:
	ld	a,RESULT_TRUNCATED_STRING
DQS_FLAG:
	push	af

	;Ask for the whole INQUIRY. Both fields come out of one answer, so
	;the two string indices cannot disagree about what the stick called
	;itself - which they would the moment each asked for its own.
	ld	a,MB_IDENT
	ld	b,0
	ld	hl,MB_NOARGS
	; MB_POLL clobbers BC; preserve C, the pre-field skip count.
	push	bc
	call	MB_SEND
	call	MB_POLL
	pop	bc
	jp	c,DQS_NOANSWER

	ld	a,d
	or	a
	jp	z,DQS_EOF		;D=0: not one byte may be written.
					;The 28 answer bytes are deliberately
					;left unread - the firmware's
					;res_begin() clears the FIFO on the
					;next command byte, so they are dropped
					;rather than delivered to the next
					;request, exactly as MB_RESULT_IS
					;leaves the tail on a mismatch.

	;Skip the answer bytes that precede the wanted field.
	ld	a,c
	or	a
	jr	z,DQS_COPY
DQS_DROP:
	ld	a,(MBOX_DATA)
	dec	c
	jr	nz,DQS_DROP

DQS_COPY:
	;B = bytes to copy = min(field length, buffer size - 1)
	ld	a,d
	cp	e
	jr	c,DQS_COPY_ALL	;D < E
	ld	b,e			;D > E: room for the whole field
	jr	DQS_COPY_GO
DQS_COPY_ALL:
	ld	a,d
	dec	a			;D-1 usable bytes
	ld	b,a
DQS_COPY_GO:
	;Copy the requested head first, then drain the unused tail. The
	;mailbox result is sequential; dropping the tail first returns bytes
	;after the field when the caller's buffer is truncated.
	ld	a,e
	sub	b
	ld	c,a
	push	bc		;B = copy count, C = tail count
	ld	c,b
	ld	b,0
	call	MB_GETRES		;(HL) is the kernel's RAM, see MB_GETRES
	pop	bc
	call	MB_DROP
	xor	a
	ld	(hl),a			;NUL-terminate: the guide requires it,
					;and the space padding the firmware uses is
					;not a terminator
	jp	DQS_EOF

;--- The firmware did not answer IDENT. That is not "the string is too
;     long", it is "there is no stick", and the guide's answer for a
;     device whose query cannot be served is RESULT_NOT_IMPLEMENTED -
;     which the kernel turns into a placeholder rather than an error.
DQS_NOANSWER:
	pop	af			;discard the code decided above
	ld	a,RESULT_NOT_IMPLEMENTED
	ret

DQS_EOF:
	pop	af
	ret


;--- Device query 2: Get device parameters.
;     In:  HL = destination buffer, or 0 to only validate the device.
;     Out: A = RESULT_OK / RESULT_INVALID_DEVICE / RESULT_NOT_IMPLEMENTED
;
;     The 12 bytes are the firmware's own Nextor device parameter block,
;     copied verbatim - the two sides cannot assemble it differently
;     because one of them never assembles it at all. The load-bearing
;     byte is the flags byte: removable set, read-only clear, FLOPPY
;     CLEAR (see the note at the head of this file).
;
;     HL=0 is the "validate only" form the guide defines, and the kernel
;     does use it - its device-counting loop asks for every device with
;     HL=0 and counts the RESULT_OK answers. Answering it from the
;     mailbox would cost a round trip per device during boot for an
;     answer that cannot change: the device exists regardless of whether
;     the stick is plugged in right now, and "no medium" is expressed as
;     a sector count of 0 in the parameter block, not as an error.

DO_DEVQ_GET_PARAMS:
IFDEF RW_PROBES
	; Log the device Nextor is enumerating and whether this is the
	; validate-only call (HL=0) or the real 12-byte parameter request.
	push	af
	push	bc
	push	de
	push	hl
	ld	b,c
	ld	a,MB_MK_DQ_INIT_DEVICE
	call	MB_MARK_WAIT
	pop	hl
	pop	de
	pop	bc
	pop	af
	push	af
	push	bc
	push	de
	push	hl
	ld	b,1
	ld	a,h
	or	l
	jr	nz,DQP_TRACE_BUFFER
	ld	b,0
DQP_TRACE_BUFFER:
	ld	a,MB_MK_DQ_INIT_BUFFER
	call	MB_MARK_WAIT
	pop	hl
	pop	de
	pop	bc
	pop	af
ENDIF
	ld	a,h
	or	l
	ld	b,a			;HL=0 means validate only; any nonzero
					;HL is the caller's RAM buffer. Checking
					;only H misclassifies addresses 0001h..00FFh.
	ld	a,MB_MK_DQ_PARAMS
	call	MB_MARK
	ld	a,b
	or	a
	jr	nz,DQP_FILL
	ld	a,MB_MK_DQ_PARAMS_HL0
	ld	b,0
	call	MB_MARK		;the HL=0 shortcut, which the kernel's
					;automap pass uses - this marker is
					;the ONLY trace of that call, since
					;it sends no mailbox command
	xor	a		;RESULT_OK, device exists
	ret
DQP_FILL:
	ld	a,MB_MK_DQ_PARAMS_MB
	ld	b,0
	call	MB_MARK		;going to the mailbox for the real
					;parameter block
	push	hl
	ld	hl,MB_NOARGS
	ld	a,MB_CAPACITY
	ld	b,0
	call	MB_SEND
	pop	hl
	call	MB_POLL
	jr	c,DQP_TIMEOUT
	ld	bc,MB_PARAMS_LEN
	call	MB_GETRES		;HL is the kernel's buffer
	xor	a
	ret
DQP_TIMEOUT:
	ld	a,MB_MK_DQ_PARAMS_TO
	ld	b,0
	call	MB_MARK
	;RESULT_NOT_IMPLEMENTED means "block device, 512-byte sectors,
	;everything else zero" - i.e. a device that exists with no media.
	;That is the right thing to claim here, and it is recoverable: the
	;kernel keeps the device and asks again once the status machinery
	;reports a change, whereas an error result makes it drop the device
	;and it would never come back.
	ld	a,RESULT_NOT_IMPLEMENTED
	ret


;--- Device query 3: Get device status.
;     Out: A = RESULT_OK, B = 0 no medium / 1 ready / 2 ready and changed.
;     Also A = RESULT_NOT_IMPLEMENTED when the firmware does not answer,
;     which the guide defines as "ok, B = 1" - a reachable medium is the
;     safe default, because a removable device that is reported present
;     costs a retry and a device reported absent costs the drive.
;
;     The firmware's STATUS byte already IS the guide's encoding, so it
;     is passed straight through with no translation. That is why the
;     firmware has a latch at all: without it the kernel could never see
;     the "2" and would sit at "1" forever, which is how a stick plugged
;     in after boot stays invisible.

DO_DEVQ_GET_STATUS:
	;STATUS may be polled by the kernel in a tight loop (often every
	;VBL) once a medium is reported absent. Going through the full
	;mailbox transaction for each poll starves the firmware service loop
	;and prevents any other command from being answered. The media byte
	;is the result of MB_STATUS anyway, so issue STATUS directly and wait
	;for the single byte answer.
	ld	a,MB_STATUS
	call	MB_CMD0_1BYTE
	jr	c,DQS_STAT_DEFAULT
	ld	b,a
	xor	a
	ret
DQS_STAT_DEFAULT:
	ld	a,RESULT_NOT_IMPLEMENTED
	ret


;--- Device query 4: Get device availability.
;     Out: A = RESULT_OK, B = 0 / 1 (0 = present but no medium).
;
;     STAPEEK rather than STATUS on purpose: availability must not
;     consume the changed latch, or a media check between two status
;     requests would swallow the change and the drive would appear
;     unchanged forever.

DO_DEVQ_GET_AVAILABILITY:
	ld	b,c			;as in DO_DEVQ_GET_STATUS
	ld	a,MB_MK_DQ_AVAIL
	call	MB_MARK
	ld	a,MB_STAPEEK
	call	MB_CMD0_1BYTE
	jr	c,DQA_DEFAULT
	or	a		;0 = no medium, anything else = usable
	jr	nz,DQA_YES
	xor	a
	ret
DQA_YES:
	ld	b,1
	xor	a
	ret
DQA_DEFAULT:
	ld	a,RESULT_NOT_IMPLEMENTED
	ret


;--- Device queries 5, 6, 7: format choices, format, stop motor.
;     All three are floppy-only and this is a fixed disk, so
;     RESULT_NOT_IMPLEMENTED is the documented correct answer for all of
;     them (it means "no such media / nothing to do"), not a shortcut.

DO_DEVQ_GET_FORMAT_CHOICES:
	ld	a,RESULT_NOT_IMPLEMENTED
	ret

DO_DEVQ_DO_FORMAT:
	ld	a,RESULT_NOT_IMPLEMENTED
	ret

DO_DEVQ_STOP_MOTOR:
	ld	a,RESULT_NOT_IMPLEMENTED
	ret


;--- Custom queries: none. Both entry points exist because the jump table
;    has slots for them, and the documented answer for a driver with no
;    extensions is RESULT_NOT_IMPLEMENTED.

CUSTOM_DRIVER_QUERY:
	ld	a,RESULT_NOT_IMPLEMENTED
	ret

CUSTOM_DEVICE_QUERY:
	ld	a,RESULT_NOT_IMPLEMENTED
	ret


;-----------------------------------------------------------------------------
; READ_WRITE
;-----------------------------------------------------------------------------
;
;   In:  Cy = 0 read, 1 write
;        A  = device number
;        B  = sector count
;        C  = media descriptor (ignored - not a floppy)
;        HL = sector buffer
;        DE = pointer to the 32-bit sector number, little-endian
;   Out: A = error code (0 = ok), B = sectors actually transferred
;
; There is one device and it is fixed, so the only pre-flight check that
; can fail here is the medium being absent. It is asked for up front, once
; for the whole request: .NRDY is the correct answer for "no stick", and
; finding that out from the first sector's ERR port would cost a wasted
; 512-byte transfer to learn the same thing.
;
; Note there is deliberately NO range check against the sector count here.
; The driver has no cheap way to know it - query 2 is a mailbox round trip
; the kernel does not expect in the middle of a read - and the firmware
; checks every LBA against the medium anyway (raw_disk.c), answering an
; out-of-range request as a failed transfer, which becomes .DISK here.
; Checking twice would buy nothing but a second place to be wrong.
;
;-----------------------------------------------------------------------------
; The frame
;-----------------------------------------------------------------------------
; MB_SEND and MB_POLL trash HL and BC, which are exactly the registers
; holding the buffer pointer, the sector count and (for a write) the data
; source. A ROM driver cannot park them in variables, so they live in a
; stack frame addressed by IY:
;
;   IY+0   F        the caller's flags; bit 0 (Cy) is the direction
;   IY+1   A        device number
;   IY+2   C        the caller's media descriptor, reused as the
;                    sectors-still-to-do counter
;   IY+3   B        sector count, never modified - an error can cut a
;                    request short and the caller has to be told how far
;                    it got
;   IY+4   L,H      buffer pointer, read back out once at the start
;   IY+6   E,D      the caller's sector-number pointer, used once to seed
;   IY+8   ...      our own copy of the 32-bit sector number
;   IY+12  ...      saved caller IY
;   IY+14  ...      saved caller IX
;   IY+16  ...      saved caller HL (sector buffer)
;
; The sector number is COPIED rather than advanced where the caller left
; it, because DE points at the kernel's own variable. Writing to it would
; work - it is RAM - but it is the caller's data and the kernel is
; entitled to read it back after the call.
;
; The buffer pointer does NOT live in the frame. It lives in DE, because
; DE is the one register pair that survives every primitive this path
; calls: MB_SEND touches A/B/HL, MB_PUSH and MB_GETRES touch A/BC/HL, and
; MB_POLL touches A/BC while preserving the caller's shadow BC - none of
; them writes DE. `push de / pop hl`
; is therefore the buffer fetch, and after a transfer the same instruction
; pair stores the already-advanced HL back into it. Both primitives happen
; to advance HL by exactly the number of bytes they moved, which is the
; advance wanted for the next sector, so no arithmetic is needed at all.
;
; Which register walks where is not a free choice. IY holds the frame for
; the whole call, so every `(iy+n)` access stays correct across the loop;
; the sector number, which is the one field that DOES move, is reached
; through IX, which nothing else here touches. Doing it the other way
; round - advancing IY through the 32-bit increment and leaving it there -
; silently invalidates the frame base for the next iteration, which is a
; corruption with no symptom other than "the second sector of every
; request reads from the wrong place".
;
; Only 8-bit indexed accesses are used. `ld hl,(iy+n)` and `ld (iy+n),hl`
; are not encodable on a Z80 - there is no such opcode - so a 16-bit field
; in a frame has to be read a byte at a time. FRAME_GET is that, once,
; rather than at every use.

READ_WRITE:
	push	af			;A = device number, F = direction
	push	bc			;B is the caller's sector count and C the
					;media descriptor. The marker below needs
					;B for its payload, so both have to survive
					;it: the frame stores B at iy+3 and every
					;transfer decision reads the count back
					;out of it. Losing it here made every
					;request look like a one-sector read.
	ld	b,a
	ld	a,MB_MK_RW_ENTER	;so the log says WHICH device was asked for
	call	MB_MARK			;before the frame exists to read it from
	pop	bc			;restore the real count / media
	pop	af
	push	hl			;saved caller HL / sector buffer
	push	ix			;saved caller IX
	push	iy			;saved caller IY
	dec	sp
	dec	sp
	dec	sp
	dec	sp			;reserve the sector-number copy (frame top)
	push	de			;the caller's sector-number pointer
	push	hl			;the sector buffer
	push	bc			;sector count, media descriptor
	push	af			;device number, direction
	ld	hl,0
	add	hl,sp			;HL = frame base
	push	hl
	pop	iy

	;--- Seed our copy of the sector number from the caller's DE block.
	;    DE is the documented pointer to the 32-bit LBA and is still live
	;    at this point. Reading it directly avoids depending on a second
	;    interpretation of the saved register-pair frame.
	push	de
	pop	hl			;HL = the caller's LBA block address
	ld	a,8
	call	FRAME_IX		;IX = &our copy
	push	ix
	pop	de			;DE = our copy, HL still the source
	ld	bc,4
	ldir				;the 4 bytes, without an intermediate

	;--- The buffer pointer, live in DE from here to the end. Use the
	;    separately saved original HL, exactly as supplied by DEV_READ.
	ld	e,(iy+16)
	ld	d,(iy+17)

	;--- Past the frame. A crash between here and the first real command
	;    would produce no mailbox traffic at all, so this is the marker
	;    that separates "the kernel never called READ_WRITE" from "it did
	;    and the driver died in its first dozen instructions".
IFDEF RW_PROBES
	ld	b,(iy+3)		;report the caller's requested sector count
	ld	a,MB_MK_RW_FRAME
	call	MB_FRAME_TRACE
	ld	b,d			;report the destination's high byte too:
	ld	a,MB_MK_RW_BUFPG	;81h = direct-to-DTA (RW_MANY), else a
	call	MB_FRAME_TRACE	;kernel buffer - tells the two callers apart
ENDIF

	;--- Device range. This driver exposes one physical USB device and the
	;    mailbox protocol has no device byte. Some kernels pass a value
	;    other than 1 in A on the ROM-driver path (currently observed as
	;    6Ah). Rejecting it as .IDEVN prevents the first READ from reaching
	;    USB and traps boot in the kernel retry loop.
	;
	;    Zero is diagnosed and coerced to the only device. Any other value
	;    is accepted for this single-device implementation; values outside
	;    1..MB_DEVICE_COUNT get an explicit breadcrumb for diagnosis.
	ld	a,(iy+1)
	or	a
	jp	nz,RW_DEV_NONZERO
	ld	a,MB_MK_RW_COERCE
	ld	b,0			;the rejected number, verbatim
	call	MB_MARK
	jp	RW_DEV_OK
RW_DEV_NONZERO:
	ld	a,(iy+1)
	cp	MB_DEVICE_COUNT+1
	jr	c,RW_DEV_OK
	ld	a,MB_MK_RW_COERCE
	ld	b,(iy+1)
	call	MB_MARK

RW_DEV_OK:
	;--- Direction, read from the saved flags byte BEFORE iy+2 stops being C.
	;
	;    The Nextor 3 guide and kernel source define the direction in Cy:
	;    Cy=0 reads and Cy=1 writes. C is the media descriptor and is zero
	;    for a non-floppy block device. The incoming F byte was saved at
	;    IY+0 when the frame was built, so use its carry bit here.
	;
	;    Taking it from C would make every non-floppy request look like a
	;    read, including writes, because the kernel supplies C=0.
	;
	;    The direction is NOT cached in the frame. IY+0 is the caller's
	;    saved flags, nothing ever overwrites it, and re-reading it is
	;    both cheaper and safer than parking a copy: the frame below
	;    IY+8 holds the caller's saved registers, and a cached direction
	;    written over one of those bytes is silently a corrupted IY or IX
	;    handed back to the kernel at the return. The value only has to
	;    survive long enough to be reported here, so it is reported from
	;    the flags directly.
	ld	a,(iy+0)
	and	1
	ld	b,a			;B = the payload, Cy as 0 or 1
	ld	a,MB_MK_RW_DIR
	call	MB_MARK

	;--- Only now is C free to be the outstanding-sector counter.
	ld	a,(iy+3)		;sector count into the IY+2 counter
	ld	(iy+2),a

	;--- Medium present?
	ld	a,MB_STATUS
	call	MB_CMD0_1BYTE		;trashes AF, BC, HL - none of them live
	jp	c,RW_NOTREADY		;cannot even ask: treat as absent
	or	a
	jp	z,RW_NOTREADY
	ld	b,a			;the STATUS byte, which is the answer
	ld	a,MB_MK_RW_MEDIUM
	call	MB_MARK		;the status query itself got a mailbox
					;answer, and the answer was "present"

RW_LOOP:
	ld	a,(iy+2)		;sectors still to do. Bit 7 is not a
	and	7Fh			;count: it is the "bulk already failed
	jp	z,RW_OUT_OK		;once this request" latch (see below)
	cp	2			;bulk only pays for 2+ sectors
	jr	c,RW_RW1
	ld	a,(iy+2)		;the latch itself
	and	80h
	jr	nz,RW_RW1		;bulk failed once: per-sector from here
	ld	a,(iy+0)		;Cy straight out of the caller's flags
	and	1
	jp	z,RW_MANY_READ		;the bulk pair; ONE mailbox command
	jp	RW_MANY_WRITE		;covers up to MB_BULK_MAX sectors
RW_RW1:
	ld	a,(iy+0)		;Cy straight out of the caller's flags
	and	1
	or	a			;again: the frame holds no cached copy
	jp	z,RW_ONE_READ

RW_ONE_WRITE:
	ld	b,(ix+0)
	ld	a,MB_MK_RW_SECTOR
	call	MB_MARK		;log the copied LBA before issuing the command
	push	ix
	pop	hl			;HL = &our sector number
	ld	a,MB_WRITE
	ld	b,4
	call	MB_SEND		;command + the four LBA bytes; HL past them
	push	de
	pop	hl			;HL = the buffer (DE survived MB_SEND)
	ld	bc,MB_SECTOR_SIZE
	call	MB_PUSH		;512 arg bytes, no command byte
	push	hl
	pop	de			;HL ended one sector on: DE follows it
	call	MB_POLL
	jp	c,RW_IOERR
	ld	a,(MBOX_STAT)
	and	MBST_ERR
	jp	nz,RW_IOERR
	jp	RW_SECTOR_OK

RW_ONE_READ:
	ld	b,(ix+0)
	ld	a,MB_MK_RW_SECTOR
	call	MB_MARK		;log the copied LBA before issuing the command
	push	ix
	pop	hl			;HL = &our sector number
	ld	a,MB_READ
	ld	b,4
	call	MB_SEND		;command + the four LBA bytes
	call	MB_POLL		;does not care about HL or DE
	jp	c,RW_IOERR
	ld	a,(MBOX_STAT)
	and	MBST_ERR
	jp	nz,RW_IOERR
	;Mask before loading the 16-bit count. DE is the live destination
	;pointer and has already advanced by 512 for each previous sector in
	;this READ_WRITE request. Do NOT reload DE from the frame here: that
	;would make a multi-sector transfer overwrite the same sector buffer.
	di
	;READ_WRITE buffers must be outside page 1. If the destination ever
	;lands in 4000h-7FFFh, the following LDIR-like store would write into
	;the mailbox at 7FF0h and turn sector data into invalid commands.
	ld	a,d
	cp	40h
	jr	c,RW_BUF_OK
	cp	80h
	jr	nc,RW_BUF_OK
	ld	b,e
	ld	a,MB_MK_RW_BADBUFL
	call	MB_MARK
	ld	b,d
	ld	a,MB_MK_RW_BADBUFH
	call	MB_MARK
	ld	a,.DISK
	ei
	jp	RW_OUT
RW_BUF_OK:
	;--- Fast drain. The result port is a single fixed address, so read
	;    it through HL with `ld a,(hl)` (7 T-states) instead of the
	;    absolute form (13 T-states), and unroll 8x so the DJNZ is paid
	;    once per eight bytes. This is the dominant Z80-side cost after
	;    the USB sector has arrived.
	;    DE is the caller's live per-sector pointer and IS the
	;    destination, so the drain advances DE rather than HL; HL is free
	;    for the port address. The buffer origin the probes below need is
	;    recovered from the frame (iy+16), not from HL.
	ld	hl,MBOX_DATA
	ld	b,64			;64 x 8 = 512 bytes
RW_DRAIN_L:
	ld	a,(hl)
	ld	(de),a
	inc	de
	ld	a,(hl)
	ld	(de),a
	inc	de
	ld	a,(hl)
	ld	(de),a
	inc	de
	ld	a,(hl)
	ld	(de),a
	inc	de
	ld	a,(hl)
	ld	(de),a
	inc	de
	ld	a,(hl)
	ld	(de),a
	inc	de
	ld	a,(hl)
	ld	(de),a
	inc	de
	ld	a,(hl)
	ld	(de),a
	inc	de
	djnz	RW_DRAIN_L
	ei

IFDEF RW_PROBES
	;--- Prove the sector actually landed, instead of assuming it did.
	;
	;    Everything above reports success identically whether 512 bytes
	;    arrived or the result FIFO was empty: the STATUS carries no
	;    error, the driver reports one sector transferred, and the kernel
	;    is handed a buffer full of 0FFh. That is exactly the shape of
	;    the failure this driver was stuck on - Nextor parsing a buffer
	;    that does not hold the MBR, finding no partition, and reading
	;    LBA 0 again forever. Two markers settle it in one run: the
	;    firmware answers an empty FIFO with 0FFh, so seeing 0FFh here
	;    says the read raced the answer, while seeing 0FAh/0AAh says the
	;    data is in the caller's buffer and the kernel is what rejects it.
	;
	;    Byte 0 comes from the frame rather than from HL. Only HL is
	;    touched: DE has to survive as the next sector's buffer pointer,
	;    and IX and IY are still live.
	push	de			;the next-sector buffer pointer survives probes
	ld	a,(iy+16)
	ld	l,a
	ld	a,(iy+17)
	ld	h,a
	ld	a,(hl)
	ld	b,a
	ld	a,MB_MK_RW_BUF0
	call	MB_MARK_WAIT

	;The unpolled marker stream can overwrite itself while the firmware
	;is printing. These probes wait for DONE so every diagnostic byte is
	;captured, and also show whether the second call supplied the same
	;SECBUF address as the first one.
	ld	a,(iy+16)
	ld	b,a
	ld	a,MB_MK_RW_BUFL
	call	MB_MARK_WAIT
	ld	a,(iy+17)
	ld	b,a
	ld	a,MB_MK_RW_BUFH
	call	MB_MARK_WAIT

	ld	a,(iy+16)
	ld	l,a
	ld	a,(iy+17)
	ld	h,a
	ld	de,446
	add	hl,de
	ld	b,(hl)
	ld	a,MB_MK_RW_STAT
	call	MB_MARK_WAIT
	inc	hl
	inc	hl
	inc	hl
	inc	hl
	ld	b,(hl)
	ld	a,MB_MK_RW_TYPE
	call	MB_MARK_WAIT
	ld	de,4
	add	hl,de
	ld	b,(hl)
	ld	a,MB_MK_RW_START
	call	MB_MARK_WAIT
	ld	de,56
	add	hl,de
	ld	b,(hl)
	ld	a,MB_MK_RW_SIGL
	call	MB_MARK_WAIT
	inc	hl
	ld	b,(hl)
	ld	a,MB_MK_RW_SIGH
	call	MB_MARK_WAIT
	pop	de
ENDIF

;--- Bulk write. One WRITE-MULTI mailbox command covers up to MB_BULK_MAX
;    sectors: the firmware runs ONE multi-block WRITE(10) for the whole
;    chunk, and a flash stick charges its commit (measured 210-1075 ms on
;    the test stick, rare stalls past 4.5 s) once per SCSI COMMAND, not
;    per sector - so N sectors pay one commit instead of N.
;
;    The chunk is sized so the whole exchange - the ~125 ms data push plus
;    the firmware's single ~2 s attempt - answers inside the ~2.9 s
;    MB_POLL window: DONE or ERR lands while this driver is still
;    polling. On any failure the fallback IS the retry: DE is restored
;    to the chunk start, bit 7 of (iy+2) latches "no more bulk in this
;    request" (the ROM driver has no statics, so the latch rides the one
;    frame byte whose top bit a sector count can never need: the kernel's
;    B is 8-bit), and the proven per-sector loop takes over from the same
;    sector. A latched latch never retries bulk within the request - the
;    only failure that repeats is a slow stick, and per-sector commands
;    are exactly the slow stick's worst case, so one bulk failure per
;    request is the right budget.
;
;    The chunk count rides the stack on top of the saved DE: a push af
;    leaves A at SP+1, and that byte is both the count byte MB_PUSH
;    reads back out of memory (a ROM driver has nowhere static to put
;    it) and the count the success path pops back into A, while the
;    saved DE underneath is the failure path's restore anchor.
;
;    Register discipline is the frame's throughout: IY = frame, IX = &the
;    sector-number copy, DE = the live buffer pointer (the one pair every
;    mailbox primitive preserves).

RW_MANY_WRITE:
	ld	a,(iy+2)
	and	7Fh			;remaining, without the latch bit
	cp	MB_BULK_MAX+1
	jr	c,RW_MW_CNT
	ld	a,MB_BULK_MAX		;cap the chunk at the firmware's bound
RW_MW_CNT:
	ld	b,a			;the marker payload: the driver's chunk
	ld	a,MB_MK_RW_BULKW
	call	MB_MARK_BULK		;REAL: the log must show the chunk the
					;driver computed, next to the count
					;the firmware captures
	ld	a,(iy+2)			;MB_MARK_BULK trashed A: recompute
	and	7Fh
	cp	MB_BULK_MAX+1
	jr	c,RW_MW_CNT2
	ld	a,MB_BULK_MAX
RW_MW_CNT2:
	push	de			;the chunk start, for the fallback
	push	af			;[chunk|F] on top of it: the count
					;byte MB_PUSH reads back out of
					;memory (a ROM driver has no statics)
	push	ix
	pop	hl			;HL = &our sector number copy
	ld	a,MB_WRITEM
	ld	b,4
	call	MB_SEND			;command + LBA(4)
	ld	hl,1
	add	hl,sp			;HL = &the pushed chunk byte (push af
					;leaves A at SP+1, F at SP)
	ld	bc,1
	call	MB_PUSH			;the count byte; the firmware now knows
					;how much data follows and armed nothing
	ld	hl,1
	add	hl,sp
	ld	a,(hl)			;the chunk count again
	ld	l,a
	ld	h,0
	add	hl,hl			;x2
	add	hl,hl			;x4
	add	hl,hl			;x8
	add	hl,hl			;x16
	add	hl,hl			;x32
	add	hl,hl			;x64
	add	hl,hl			;x128
	add	hl,hl			;x256
	add	hl,hl			;x512
	push	hl
	pop	bc			;BC = chunk*512
	push	de
	pop	hl			;HL = the chunk's data source
	call	MB_PUSH			;the data burst; HL ends one chunk on
	push	hl
	pop	de			;DE follows it (the success path)
	call	MB_POLL			;Cy=1 timeout; A = status otherwise
	jr	c,RW_MW_FAIL
	and	MBST_ERR
	jr	nz,RW_MW_FAIL
	pop	af			;A = chunk
	pop	hl			;discard the chunk-start DE; the
					;success path needs neither back
	ld	c,a			;C = chunk for the advance loop
	jp	RW_ADV_BULK
RW_MW_FAIL:
	pop	bc			;discard [chunk|F] (BC scratch)
	pop	de			;DE = the chunk start again
	ld	a,(iy+2)
	or	80h			;latch: no more bulk this request
	ld	(iy+2),a
	jp	RW_ONE_WRITE		;the fallback is the retry

;--- Bulk read. One READ-MULTI mailbox command: the firmware answers with
;    count*512 bytes staged whole, and the drain below moves them into
;    the caller's buffer one 512-byte DI window at a time - the same
;    window the single-sector path uses, just not interleaved with a
;    per-sector command each time. On failure DE has not moved (nothing
;    was drained), so no restore is needed and the fallback is per-sector
;    reads from the same LBA.

RW_MANY_READ:
	ld	a,(iy+2)
	and	7Fh
	cp	MB_BULK_MAX+1
	jr	c,RW_MR_CNT
	ld	a,MB_BULK_MAX
RW_MR_CNT:
	ld	b,a
	ld	a,MB_MK_RW_BULKR
	call	MB_MARK_BULK		;REAL: as the write-side marker
	ld	a,(iy+2)			;MB_MARK_BULK trashed A: recompute
	and	7Fh
	cp	MB_BULK_MAX+1
	jr	c,RW_MR_CNT2
	ld	a,MB_BULK_MAX
RW_MR_CNT2:
	push	af			;[chunk|F] (A at SP+1, F at SP)
	push	ix
	pop	hl			;HL = &our sector number copy
	ld	a,MB_READM
	ld	b,4
	call	MB_SEND			;command + LBA(4)
	ld	hl,1
	add	hl,sp			;HL = &the pushed chunk byte
	ld	bc,1
	call	MB_PUSH			;the count byte
	call	MB_POLL			;the whole answer is staged before
					;DONE - the drain needs all of it
	jr	c,RW_MR_FAIL
	ld	a,(MBOX_STAT)
	and	MBST_ERR
	jr	nz,RW_MR_FAIL
	pop	af			;A = chunk; stack balanced
	ld	c,a			;C = the drain loop's sector counter
RW_MR_DRAIN:
	di
	;The single-sector path's destination range check, every 512 bytes:
	;the drain is a store loop, and a destination inside 4000h-7FFFh
	;would write into the mailbox at 7FF0h and turn sector data into
	;invalid commands.
	ld	a,d
	cp	40h
	jr	c,RW_MR_BUF
	cp	80h
	jr	nc,RW_MR_BUF
	ei
	jr	RW_MR_BADBUF
RW_MR_BUF:
	ld	hl,MBOX_DATA
	ld	b,64			;64 x 8 = 512 bytes
RW_MR_DL:
	ld	a,(hl)
	ld	(de),a
	inc	de
	ld	a,(hl)
	ld	(de),a
	inc	de
	ld	a,(hl)
	ld	(de),a
	inc	de
	ld	a,(hl)
	ld	(de),a
	inc	de
	ld	a,(hl)
	ld	(de),a
	inc	de
	ld	a,(hl)
	ld	(de),a
	inc	de
	ld	a,(hl)
	ld	(de),a
	inc	de
	ld	a,(hl)
	ld	(de),a
	inc	de
	djnz	RW_MR_DL
	ei
	dec	c
	jr	nz,RW_MR_DRAIN
	;The chunk count once more for the advance loop - recomputed, like
	;everywhere in this driver, because no register is free to carry it
	;across the drain.
	ld	a,(iy+2)
	and	7Fh
	cp	MB_BULK_MAX+1
	jr	c,RW_MR_AC
	ld	a,MB_BULK_MAX
RW_MR_AC:
	ld	c,a
	jr	RW_ADV_BULK
RW_MR_FAIL:
	pop	af			;stack balanced
	ld	a,(iy+2)
	or	80h
	ld	(iy+2),a		;latch: no more bulk this request
	jp	RW_ONE_READ
RW_MR_BADBUF:
	ld	b,e
	ld	a,MB_MK_RW_BADBUFL
	call	MB_MARK
	ld	b,d
	ld	a,MB_MK_RW_BADBUFH
	call	MB_MARK
	ld	a,.DISK
	jp	RW_OUT

;--- Advance the frame by C sectors after a successful bulk chunk: the
;    32-bit sector-number ripple once per sector and the outstanding
;    counter down once per sector. The latch bit is clear on every path
;    that reaches this (bulk succeeded), so the plain decrement is exact.

RW_ADV_BULK:
	inc	(ix+0)
	jr	nz,RW_AB2
	inc	(ix+1)
	jr	nz,RW_AB2
	inc	(ix+2)
	jr	nz,RW_AB2
	inc	(ix+3)
RW_AB2:
	ld	a,(iy+2)
	dec	a
	ld	(iy+2),a
	dec	c
	jr	nz,RW_ADV_BULK
	jp	RW_LOOP

RW_SECTOR_OK:
	;32-bit increment of the sector number through IX, little-endian, so
	;the low byte first. IY is deliberately not touched: it is the frame.
	;Do not increment IX itself: IX must remain the address of the copy.
	inc	(ix+0)
	jr	nz,RW_ADV_DONE
	inc	(ix+1)
	jr	nz,RW_ADV_DONE
	inc	(ix+2)
	jr	nz,RW_ADV_DONE
	inc	(ix+3)
RW_ADV_DONE:
	;The outstanding counter decrements with the latch bit preserved:
	;bit 7 of (iy+2) is "bulk failed once this request", and dropping it
	;here would re-arm bulk for the next sector and turn every later
	;failure back into a ~2.9 s timeout.
	ld	a,(iy+2)
	and	80h			;A = the latch, if set
	ld	c,a			;C is scratch here: every primitive in
					;the transfer paths trashes BC
	ld	a,(iy+2)
	and	7Fh
	dec	a			;the count; was >= 1, cannot wrap
	or	c			;recombine: (count-1) | latch
	ld	(iy+2),a
	jp	RW_LOOP

RW_BADDEV:
	;Device number out of range. The guide is emphatic that .IDEVN is
	;for numbers that do not exist and .NRDY for a device that exists but
	;has no medium; getting that backwards makes the kernel report a
	;missing drive as an invalid one, which no recovery path retries.
	ld	b,(iy+1)		;the number the kernel actually passed
	ld	a,MB_MK_RW_IDEVN
	call	MB_MARK
	ld	a,.IDEVN
	jp	RW_OUT

RW_NOTREADY:
	ld	b,(iy+1)		;the device number, for the same reason
	ld	a,MB_MK_RW_NRDY
	call	MB_MARK
	ld	a,.NRDY
	jp	RW_OUT

RW_IOERR:
	;Ask the firmware what actually went wrong. Only the no-medium case
	;is reported as .NRDY; everything else - a SCSI failure, a short
	;transfer, a firmware-side timeout - becomes .DISK, which is the
	;answer that keeps the drive mounted. Claiming .NRDY for a failed
	;transfer would make the kernel unmap a drive that is present.
	ld	a,(MBOX_ERR)
	ld	b,a			;the firmware's own error code. 1 = no
					;medium, 2 = the transfer failed.
	ld	a,MB_MK_RW_DISK
	call	MB_MARK
	ld	a,b
	cp	MBERR_NOMEDIA
	jr	nz,RW_DISK
	ld	a,.NRDY
	jp	RW_OUT
RW_DISK:
	ld	a,.DISK

RW_OUT:
	push	af
	jp	RW_OUT2

RW_OUT_OK:
	xor	a
	push	af
	ld	a,(iy+3)		;sectors requested, which at this point
	ld	b,a			;all came back: the count is the answer
	ld	a,MB_MK_RW_ALLDONE
	call	MB_MARK
	;Falls into RW_OUT2 with the (zero) result code already on the stack,
	;so the marker cannot disturb what the kernel is about to be told.

RW_OUT2:
	;Stack: AF = the result code. A = sectors actually transferred, which
	;is requested minus outstanding: the sector in flight when the error
	;hit does not count, because its data never made it back to the caller.
	ld	a,(iy+2)		;sectors still outstanding
	and	7Fh			;mask the bulk latch out of it
	ld	c,a
	ld	a,(iy+3)		;the sector count as the caller gave it
	or	a			;Cy=0 for the subtraction
	sub	c
	ld	b,a
	pop	af
	;Release the complete frame before returning. The original caller's
	;return address is 18 bytes above the frame base: four reserved bytes,
	;the saved HL/IX/IY pairs, and the pushed DE, HL, BC and AF pairs. Leaving
	;the frame allocated
	;made RET consume frame data and sent the kernel back through driver
	;initialization after the first successful sector.
	;Recover the caller's original IY and IX while SP still points at the
	;frame. POPs below do not modify A, so the result code remains intact.
	ld	hl,12
	add	hl,sp
	ld	e,(hl)
	inc	hl
	ld	d,(hl)			;DE = caller IY
	inc	hl
	ld	c,(hl)
	inc	hl
	ld	b,(hl)			;BC = caller IX
	push	bc
	pop	ix
	push	de
	pop	iy
	ld	hl,0
	add	hl,sp
	ld	de,18
	add	hl,de
	ld	sp,hl
	ret


;--- FRAME_IX: IX = the address of the frame field A bytes along.
;     Used once, to point IX at the sector-number copy; from there IX
;     advances itself and IY keeps the frame.
;     Trashes AF, DE, IX.

FRAME_IX:
	push	iy
	pop	ix
	ld	d,0
	ld	e,a
	add	ix,de
	ret


;--- FRAME_GET: HL = the 16-bit frame field A bytes along. IN: A = offset.
;     Exists because `ld hl,(iy+n)` does not exist: the Z80 has 8-bit
;     indexed loads only, so a word in a frame is two byte loads.
;     Trashes AF, BC, HL. HL is an INPUT (unpreserved) and an OUTPUT.
;
;     The frame base comes from IY, never from the incoming HL. An earlier
;     version took the base from HL on the assumption that HL WAS the frame
;     pointer, which happened to be true at the first of the three call
;     sites (HL had just been loaded with SP) and false at the second:
;     LDIR had advanced HL four bytes past the caller's sector-number block
;     by then. The routine faithfully read two bytes from THAT address - a
;     word of kernel memory - and returned it as the frame field. The
;     buffer pointer the read path then used was a live kernel word rather
;     than the sector buffer the caller passed in HL, so the 512-byte
;     MB_GETRES wrote the sector wherever that word happened to point and
;     scribbled over the machine. Taking the base from IY makes the routine
;     correct at every call site by construction, which is the whole reason
;     the frame is addressed through IY in the first place.

FRAME_GET:
	push	iy
	pop	hl		;HL = IY
	ld	d,0
	ld	e,a		;DE = offset
	add	hl,de		;HL = &frame[offset]
	ld	a,(hl)
	ld	c,a
	inc	hl
	ld	a,(hl)
	ld	h,a
	ld	l,c
	ret


;-----------------------------------------------------------------------------
; Mailbox primitives
;-----------------------------------------------------------------------------

;--- MB_MARK: leave a breadcrumb (A = one of the MB_MK_* indices, B = the
;    register value the step is about).
;
;    Two stores: the marker byte to the command register, then the payload
;    to the data register. The firmware counts a marker as a one-argument
;    command, so the byte lands in its argument buffer and is printed
;    alongside the name. No answer, nothing read back.
;
;    Deliberately NOT wrapped in DI/EI, unlike MB_SEND. This is a two-byte
;    burst, so it is not atomic as a unit. If a Z80 ISR lands between the
;    two stores the firmware latches the marker and then receives the
;    payload late or not at all, and the log shows a wrong value or a
;    stale one. That is acceptable here and only here: a wrong number in a
;    debug line costs one re-run, whereas the same window in MB_SEND would
;    put argument bytes of a real transfer in the wrong order. Every
;    command that MOVES DATA is DI-wrapped.
;
;    B is chosen by each call site and is the whole point of the value
;    half: "the kernel asked for a device that does not exist" is a
;    symptom, "the kernel asked for device 0" is a diagnosis.
;
;    In: A = marker index, B = payload. Trashes AF only - A comes back as
;    the payload, B and C are untouched. Callers that need A afterwards
;    still have to save it; MB_MARK is three real instructions, not a
;    register-neutral no-op.

MB_MARK:
	ret


;--- MB_MARK_WAIT: an ACKNOWLEDGED marker - emit and wait for DONE.
;    Unlike the fire-and-forget MB_MARK (which stays a silent no-op so the
;    old breadcrumb storm cannot return), this one performs the whole
;    command/argument/poll transaction itself, so the value probes can
;    never overwrite a pending real request and every diagnostic byte is
;    captured. DI protects the two-byte command/argument burst from a Z80
;    ISR clobbering B between the two writes. Only diagnostic probes use it.
;    In: A = marker index, B = payload. Trashes AF, BC, HL.

MB_MARK_WAIT:
	di
	add	a,20h
	ld	(MBOX_CMD),a
	ld	a,b
	ld	(MBOX_DATA),a
	ei
	call	MB_POLL
	ret


;--- MB_FRAME_TRACE: a single acknowledged frame-count marker per
;    READ_WRITE call. Unlike the old fire-and-forget breadcrumbs this waits
;    for the firmware to consume the marker before any real mailbox request
;    can follow, so it cannot overwrite a pending command. In: B=count.

MB_FRAME_TRACE:
	di
	add	a,20h
	ld	(MBOX_CMD),a
	ld	a,b
	ld	(MBOX_DATA),a
	ei
	call	MB_POLL
	ret

;--- MB_MARK_BULK: a REAL fire-and-forget marker for the two bulk-chunk
;    sites only. MB_MARK stays a no-op so the old per-sector breadcrumb
;    storm cannot return, but the bulk chase needs the driver's own chunk
;    count in the log next to the WRITEM the firmware captures: if the
;    marker says 2 and the WRITEM arrives with count=1, the count byte
;    corrupted between the Z80 push and the firmware's EXTI store, which
;    no amount of ROM reading can prove or disprove from either side.
;    Fire-and-forget on purpose: no MB_POLL, because the next real
;    request is the bulk command itself and DONE for the marker would be
;    overwritten by it anyway - the firmware logs the marker's argument
;    when it dispatches it (log_request's marker branch).
;    In: A = marker index, B = payload. Trashes AF only.

MB_MARK_BULK:
	add	a,20h
	ld	(MBOX_CMD),a
	ld	a,b
	ld	(MBOX_DATA),a
	ret

;--- MB_SEND: send a command byte plus B argument bytes copied from (HL).
;    B = 0 is legal (command with no arguments).
;    The DI/EI wrap is belt and braces: the command+args burst must not
;    be split by anything else touching the window (nothing in the BIOS
;    interrupt path does, but the cost is a few microseconds).
;    In:  A = command, B = arg count, HL = arg source
;    Out: HL advanced past the args - so HL is an OUTPUT even when B = 0.
;         Trashes AF, B.
;
;    That last point is the trap. `ld hl,MB_NOARGS / ld b,0 / call MB_SEND`
;    reads like "send a command with no arguments" and also silently
;    overwrites whatever the caller had in HL - which is why every
;    READ_WRITE call site reloads its pointers from the frame afterwards
;    instead of trusting HL to come back.
;
;    Note also that a Z80 `di` does NOT stop the firmware seeing the bus
;    writes; it only keeps a Z80-side ISR out of the middle of the burst.

MB_SEND:
	di
	ld	(MBOX_CMD),a
	ld	a,b
	or	a
	jr	z,MB_SEND_Z
MB_SEND_L:
	ld	a,(hl)
	inc	hl
	ld	(MBOX_DATA),a
	djnz	MB_SEND_L
MB_SEND_E:
	ei
	ret
MB_SEND_Z:
	ei
	ret


;--- MB_PUSH: push B argument bytes from (HL) to DATA, WITHOUT writing a
;    command byte.
;
;    This exists for the 512-byte half of a WRITE, which has to follow the
;    4-byte LBA burst in the same command. MB_SEND cannot do it because
;    MB_SEND always writes CMD, and CMD is what makes the firmware latch a
;    new request and clear DONE - a second one would throw the first half
;    away.
;
;    Deliberately NOT wrapped in DI/EI, unlike MB_SEND. The burst is 512
;    bytes, about 3 ms of Z80 bus cycles, and holding the Z80's
;    interrupts off across that is long enough to lose VBlank on any
;    machine that cares. Nothing else uses the window, so there is nothing
;    to interleave with; the one instruction that must not be interrupted
;    is the MB_SEND before it, which is where the DI is.
;
;    In:  BC = arg count, HL = arg source. Trashes AF, BC, HL.
;
;    BC and not B, because the only thing this is used for is a 512-byte
;    sector and B tops out at 255. MB_SEND keeps its B-sized count for the
;    short bursts it was written for (a command byte plus four LBA bytes)
;    and this handles the wide one.

MB_PUSH:
	ld	a,c
	or	a
	jr	nz,MB_PUSH_L
	ld	a,b
	or	a
	ret	z
MB_PUSH_L:
	ld	a,(hl)
	inc	hl
	ld	(MBOX_DATA),a
	dec	bc
	ld	a,b
	or	c
	jr	nz,MB_PUSH_L
	ret


;--- MB_POLL: wait for DONE in STATUS.
;    Bounded: 3 passes of a 16-bit poll counter. The loop body is ~53
;    T-states (ld a,(nn) 13 + and 7 + jr 7 + dec bc 6 + ld 4 + or 4 +
;    jr 12), so a pass is 65535 x 53 ~= 3.47M cycles and the bound is
;    ~10.4M cycles ~= 2.9 s at 3.58 MHz. (The old "~0.8 s" note here
;    under-counted the loop body; firmware command budgets must fit
;    inside ~2.5 s or the answer lands after this poll has failed.)
;    Out: on DONE: A = STATUS bits, Cy=0.  On timeout: Cy=1.
;    Trashes AF, BC. The pass counter uses B' because BC is the main-set
;    poll counter. The kernel parks its live registers in the shadow set
;    across inter-slot driver calls, so preserve shadow BC around the poll.

MB_POLL:
	exx
	push	bc			;save caller's shadow BC, including kernel B'
	ld	b,3
	exx
MB_POLL_P:
	ld	bc,0FFFFh
MB_POLL_L:
	ld	a,(MBOX_STAT)
	and	MBST_DONE
	jr	nz,MB_POLL_OK
	dec	bc
	ld	a,b
	or	c
	jr	nz,MB_POLL_L
	exx
	djnz	MB_POLL_GO	;inner pass done, next pass
	pop	bc			;restore caller's shadow BC (still in shadow set)
	exx
	scf			;passes exhausted: timeout
	ret
MB_POLL_GO:
	exx
	jr	MB_POLL_P
MB_POLL_OK:
	exx
	pop	bc			;restore caller's shadow BC on success too
	exx
	or	a		;clear Cy (A = STATUS bits, DONE set)
	ret


;--- MB_CMD0_1BYTE: send a zero-argument command and pop its 1-byte answer.
;    In:  A = command byte
;    Out: A = the answer, Cy=0.  Cy=1 on timeout.
;    Trashes AF, BC, HL.
;
;    One primitive rather than four call sites of "MB_SEND / MB_POLL /
;    pop one byte": the pattern is identical every time and the register
;    contract it leaves behind is what the callers' surrounding code is
;    written against, so having it named once keeps that contract in one
;    place.

MB_CMD0_1BYTE:
	ld	hl,MB_NOARGS
	ld	b,0
	call	MB_SEND
	call	MB_POLL
	ret	c
	ld	a,(MBOX_DATA)
	or	a		;clear Cy: the answer is a success
	ret


;--- MB_DROP: discard C result bytes from the DATA port.
;    Only AF and C are touched - deliberately not B, because DQS_COPY
;    keeps its byte count in B across this call.

MB_DROP:
	ld	a,c
	or	a
	ret	z
MB_DROP_L:
	ld	a,(MBOX_DATA)
	dec	c
	jr	nz,MB_DROP_L
	ret


;--- MB_RESULT_IS: consume A result bytes from the DATA port, comparing each
;    one against the matching byte at (HL) as it arrives.
;
;    In:  A = result byte count, HL = pointer to the A expected bytes
;    Out: Cy=0 = every byte matched.
;         Cy=1 = mismatch: B = its 0-based index, C = the byte that was
;               read, D = the byte that was expected.
;    Trashes: AF, BC, DE, HL.  E is left as the original count.
;
;    Deliberately storage-free, and that is the whole point. The obvious
;    shape - pop the bytes into a buffer, then compare - cannot work in
;    this driver: a ROM driver has no writable variables, so the buffer
;    would be a `ds` inside the cartridge image, and the Z80 discards
;    `ld (hl),a` into ROM. The buffer would keep the zeros baked into
;    the ROM file and the comparison would run against those (see the note
;    on MB_GETRES - that is exactly the bug that made the handshake
;    report "bad magic" forever). Comparing as the bytes stream past
;    needs no storage at all, so it is correct at driver-query time, when
;    the driver has no RAM of its own yet.
;
;    The expected bytes may live in ROM: they are only ever read.
;
;    Bailing out on the first mismatch leaves the rest of the result
;    FIFO unread. That is safe: the firmware's res_begin() resets the
;    FIFO on the next command byte, so the stale bytes are dropped
;    rather than delivered to the following request.

MB_RESULT_IS:
	ld	e,a		;E = count
	ld	b,0		;B = index of the byte under test
MB_RI_L:
	ld	a,(MBOX_DATA)
	cp	(hl)
	jr	nz,MB_RI_NO
	inc	hl
	inc	b
	dec	e
	jr	nz,MB_RI_L
	xor	a		;Cy=0: all bytes matched
	ret
MB_RI_NO:
	ld	c,a		;C = the byte we read
	ld	d,(hl)		;D = the byte we wanted
	scf			;Cy=1: mismatch
	ret


;--- MB_GETRES: pop A result bytes from the DATA port into (HL).
;
;    *** (HL) MUST BE WRITABLE RAM. ***
;    This routine is NOT safe to point at a `ds` buffer in this driver:
;    the driver is a ROM image, so `ld (hl),a` into it is discarded by
;    the Z80 and the destination keeps whatever the ROM file contains
;    (zeros, for a `ds`). Use MB_RESULT_IS when the result only has to
;    be checked, and hand MB_GETRES a kernel-supplied RAM pointer when
;    the bytes have to be kept - which is every call site here.
;    In:  BC = byte count (16-bit: a sector is 512, which does not fit in
;         B), HL = destination.
;    Trashes AF, BC, and advances HL past the last byte written.
;
;    Interrupts are masked for the drain. The loop state is in BC/HL and
;    the result FIFO must be consumed as one contiguous answer; allowing
;    the kernel's timer interrupt to run here can return with a shortened
;    or redirected drain while READ_WRITE still reports 512 bytes done.
;    MB_SEND already applies the same rule to command bursts.

MB_GETRES:
	di
	ld	a,c
	or	a
	jr	nz,MB_GR_L
	ld	a,b
	or	a
	jr	nz,MB_GR_L
	ei
	ret


;--- MB_GETRES_ATOMIC: same transfer loop, but the caller owns IFF.
;    Entry must be with interrupts disabled; return leaves them disabled.
;    This is used by READ_WRITE, where the DI must cover pointer/count setup
;    as well as all 512 DATA reads. Keeping the generic helper above separate
;    avoids changing the query routines' interrupt behavior.

MB_GETRES_ATOMIC:
	ld	a,c
	or	a
	jr	nz,MB_GRA_L
	ld	a,b
	or	a
	ret	z
MB_GRA_L:
	ld	a,(MBOX_DATA)
	ld	(hl),a
	inc	hl
	dec	bc
	ld	a,b
	or	c
	jr	nz,MB_GRA_L
	ret
MB_GR_L:
	ld	a,(MBOX_DATA)
	ld	(hl),a
	inc	hl
	dec	bc
	ld	a,b
	or	c
	jr	nz,MB_GR_L
	ei
	ret


;--- MB_HANDSHAKE_CHK: the one hardware detection step.
;     Send HANDSHAKE and compare the answer against MB_EXPECT as it
;     streams past.
;     Out: Cy=0 = the firmware mailbox is alive and speaks revision 3.
;          Cy=1 = A = 0 (no DONE: nothing answered at all)
;                     A = 1 (answered, wrong bytes: B = index, C = got)
;     Trashes: AF, BC, DE, HL.
;
;     MB_EXPECT may live in ROM and does, because it is only ever read -
;     the same reason MB_RESULT_IS exists.
;
;     DE must be preserved by the caller, not by this routine: both
;     callers hand DE to the kernel's print callback straight afterwards,
;     and MB_RESULT_IS is documented as trashing it.

MB_HANDSHAKE_CHK:
	ld	a,MB_HANDSHAKE		;A is the COMMAND and B the arg count,
	ld	b,0			;which is why the expected bytes are
	ld	hl,MB_NOARGS		;named here rather than passed in
	call	MB_SEND
	call	MB_POLL
	jp	c,MB_HS_NODONE
	ld	hl,MB_EXPECT
	ld	a,MB_HS_LEN
	call	MB_RESULT_IS
	ret	z
	ld	a,1
	scf
	ret
MB_HS_NODONE:
	xor	a
	scf
	ret


;-----------------------------------------------------------------------------
; Print helpers (DE = the kernel's character-print routine)
;-----------------------------------------------------------------------------

;--- PRINT_WITH_DE: print the NUL-terminated string at (HL) through the
;     callback in DE.
;     The callback is called as `call 0C300h`, which is the address the
;     kernel hands over in query 3/4, so PRINT_HL builds a call to it on
;     the stack and jumps to (IX).

PRINT_WITH_DE:
	push	de
	ld	de,0C300h
	push	de
	ld	ix,1
	add	ix,sp
	call	PRINT_HL
	pop	de
	pop	de
	ret

PRINT_HL:
	ld	a,(hl)
	or	a
	ret	z
	call	JP_IX
	inc	hl
	jr	PRINT_HL

JP_IX:	jp	(ix)


;--- PRINT_HEXA_DE: print A as two uppercase hex digits through DE.
;     Trashes AF, HL, IX.

PRINT_HEXA_DE:
	push	af
	rrca
	rrca
	rrca
	rrca
	call	PRINT_HEXNIBBLE_DE
	pop	af
PRINT_HEXNIBBLE_DE:
	and	0Fh
	add	a,'0'
	cp	'9'+1
	jr	c,PRINT_HEX_EMIT_DE
	add	a,7
PRINT_HEX_EMIT_DE:
	ld	hl,HEXDIGITS
	ld	c,a
	ld	b,0
	add	hl,bc
	ld	a,(hl)
	call	PRINT_WITH_DE
	ret


;--- PRINT_MIB16_DE: print IY as a five-character decimal MiB field.
;    Leading zeroes are spaces; PRINT_MIB_CHAR_DE preserves the digit
;    counter and IY while printing each digit through PRINT_WITH_DE.
PRINT_MIB16_DE:
	ld	b,0		;leading-zero suppression flag
	ld	c,0
	push	bc
	ld	bc,10000
	call	PRINT_MIB_PLACE
	pop	bc
	call	PRINT_MIB_DIGIT
	push	bc
	ld	bc,1000
	call	PRINT_MIB_PLACE
	pop	bc
	call	PRINT_MIB_DIGIT
	push	bc
	ld	bc,100
	call	PRINT_MIB_PLACE
	pop	bc
	call	PRINT_MIB_DIGIT
	push	bc
	ld	bc,10
	call	PRINT_MIB_PLACE
	pop	bc
	call	PRINT_MIB_DIGIT
	push	bc
	ld	bc,1
	call	PRINT_MIB_PLACE
	pop	bc
	add	a,'0'		;always print the units digit
	call	PRINT_MIB_CHAR_DE
	ret

PRINT_MIB_PLACE:
	xor	a		;digit count
PRINT_MIB_PLACE_LOOP:
	push	af
	push	iy
	pop	hl
	or	a		;clear carry for the subtraction
	sbc	hl,bc
	jr	c,PRINT_MIB_PLACE_DONE
	push	hl
	pop	iy
	pop	af
	inc	a
	jr	PRINT_MIB_PLACE_LOOP
PRINT_MIB_PLACE_DONE:
	pop	af
	ret

PRINT_MIB_DIGIT:
	or	a
	jr	nz,PRINT_MIB_NONZERO
	ld	a,b
	or	a
	jr	nz,PRINT_MIB_ZERO
	ld	a,' '
	call	PRINT_MIB_CHAR_DE
	ret
PRINT_MIB_ZERO:
	xor	a
	jr	PRINT_MIB_EMIT
PRINT_MIB_NONZERO:
	ld	b,1
PRINT_MIB_EMIT:
	add	a,'0'
	call	PRINT_MIB_CHAR_DE
	ret

;--- Emit decimal digit/space A through the string callback.
PRINT_MIB_CHAR_DE:
	push	bc
	push	iy
	cp	' '
	jr	z,PRINT_MIB_SPACE
	sub	'0'
	ld	c,a
	sla	c
	ld	b,0
	ld	hl,MIB_DIGITS
	add	hl,bc
	call	PRINT_WITH_DE
	jr	PRINT_MIB_CHAR_DONE
PRINT_MIB_SPACE:
	ld	hl,MSG_SPACE
	call	PRINT_WITH_DE
PRINT_MIB_CHAR_DONE:
	pop	iy
	pop	bc
	ret

; One byte, indexed by nibble value 0..15. The arithmetic version above
; (add '0', then +7 past '9') is the same table in fewer bytes, but a
; table is the easier thing to read here and the ROM has room to spare.
HEXDIGITS:
	db	"0123456789ABCDEF"

MIB_DIGITS:
	db	"0",0,"1",0,"2",0,"3",0,"4",0
	db	"5",0,"6",0,"7",0,"8",0,"9",0
MSG_SPACE:
	db	" ",0


;-----------------------------------------------------------------------------
; Strings
;-----------------------------------------------------------------------------

MB_NOARGS:			;MB_SEND with B=0 never reads (HL); this is
	db	0			;a real ROM address for it to be, and it
					;keeps that routine usable in place.

MB_ZERO_LBA:
	db	0,0,0,0

;What the firmware must answer the handshake with. A label and not an
;equ so MB_EXPECT[B] can read a single byte back out of it later - the
;wrong-magic report prints the byte that SHOULD have been there, and D
;is not available for it at that point because D is half of the print
;callback in DE.
MB_EXPECT:
	db	MB_MAGIC0,MB_MAGIC1,MB_MAGIC2,MB_MAGIC3
	db	MB_PROTOCOL_V4

INIT_MSG:
	db	13,10
	db	"  RISKYMSX2 USB drive driver 1.1.4"
	db	13,10,13,10,0

MSG_MBR_P1:
	db	"P1 ",0
MSG_MBR_P2:
	db	"P2 ",0
MSG_MBR_P3:
	db	"P3 ",0
MSG_MBR_P4:
	db	"P4 ",0
MSG_MIB_SUFFIX:
	db	" MB initialized",13,10,0
MSG_MIB_TOO_LARGE:
	db	">65535 MB initialized",13,10,0

MSG_DRIVER_NAME:
	db	"RISKYMSX2 USB drive",0

MSG_DRIVER_AUTHOR:
	db	"RISKYMSX2",0

MSG_HARDWARE_NAME:
	db	"USB MSC mass storage",0

MSG_HARDWARE_AUTHOR:
	db	"Arek Makarenko",0

MSG_SERIAL_NUMBER:
	db	"00001",0

MSG_DEVICE_NAME:
	db	"USB drive",0

MSG_NO_ANSWER:
	db	" - no answer from the cartridge mailbox",0

MSG_NO_MEDIA:
	db	" - USB drive not discovered; connect a USB drive",13,10,0

MSG_MEDIA_RETRY:
	db	" - USB drive not discovered; retrying...",13,10,0

MSG_BAD_MAGIC:
	db	" - mailbox answered with the wrong bytes at index ",0

MSG_GOT:
	db	", got ",0

MSG_WANT:
	db	", wanted ",0

MSG_CRLF:
	db	13,10,0


;--- The kernel's copy() primitive, taken from the SDK. Every string query
;    ends in it, and re-implementing it would be both longer and a chance
;    to get the truncation rule wrong.

	INCLUDE asm/code/output_string.asm


;--- Pad the ROM driver up to the bank switching code area at 7FD0h
;    (assembly fails if the driver outgrows the bank).

	ds	7FD0h-$,0FFh

	end
