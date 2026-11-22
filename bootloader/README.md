# RISKYMSXBootloader

This is a custom bootloader for the RISKYMSX cartridge. It replaces the
**factory ISP bootloader** in the CH32V303's 28 KiB system boot flash, not the
normal cartridge firmware. It lets the cartridge update its own application
firmware from a USB stick and reports the progress on the MSX screen.

> [!CAUTION]
> Replacing the factory bootloader is an untested, hardware-dependent operation:
> a failed write or a nonfunctional backup can leave factory USB/USART ISP
> unavailable. Keep a working WCH-Link and access to BOOT0, and do not assume
> recovery will work until it has been tried on a disposable device.

## Memory layout

| Region | Address | Size | Contents |
| --- | --- | --- | --- |
| System boot flash | `0x1FFF8000`-`0x1FFFEFFF` | 28 KiB | this bootloader |
| User option bytes | `0x1FFFF800` | 128 B | untouched |
| Main flash | `0x08000000` | 256 KiB | application firmware |

The [reference manual](https://ch32-riscv-ug.github.io/CH32V307/datasheet_en/CH32FV2x_V3xRM.PDF)
sections 1.2-1.3 describe the 28 KiB system memory and the BOOT0/BOOT1 boot
selection. With BOOT0 high and BOOT1 low the system flash is mapped to address
zero and this bootloader runs; with BOOT0 low the application flash starts at
`0x08000000` and the cartridge firmware runs unchanged. The bootloader does not
change boot pins or option bytes.

The firmware image is written to the start of the main flash, so the normal
cartridge firmware does not have to be relocated.

## Workflow

1. Copy `RISKYMSXCART.BIN` to the root of a FAT formatted USB stick. The
   bootloader also accepts `FIRMWARE.BIN` and `UPDATE.BIN`, in that order.
2. With the MSX powered off, close the B0 header so BOOT0 is high. Insert the
   cartridge and USB stick, then power on the MSX.
3. The MSX side program starts and displays the bootloader version.
4. The bootloader reports USB detection, basic device details, and whether
   the drive mounted. On failure it reports that firmware was not checked.
5. It checks the supported firmware names in order, reporting missing, empty,
   or found files and the selected image size.
6. Press Return to program, or Esc to cancel.
7. The screen clears and shows erase, programming percentage, verification,
   and the outcome at fixed positions near the middle of the screen.
8. Power off the MSX, open B0 so BOOT0 is low, and power on to run the
   firmware. Never insert or remove the cartridge while the MSX is powered on.

The bootloader only reports success after reading the programmed flash back and
comparing it with the file. Every failure path reports what went wrong: no USB
drive, no readable filesystem, no image file, an image that does not fit, an
erase failure, a programming failure or a verification mismatch.
The output queue is drained before erase and after progress updates to reduce
the chance of losing screen commands during flash operations. Flash access and
MSX bus interrupt timing during programming still require hardware testing.

### The MSX side program

The bootloader serves its own copy of the MSX side program,
`MSXTerminal/msxterm_boot.bin`, assembled from `MSXTerminal/msxterminal_boot.asm`.
It differs from the cartridge firmware's copy (`msxterm.bin`) in one place: the
firmware's version checks the GRAPH key at startup and jumps straight to the
cartridge ROM when it is held, while the bootloader's version always starts the
terminal. Holding GRAPH would otherwise skip the bootloader terminal
entirely.

The two programs share the bus protocol, so the bootloader and the cartridge
firmware agree on it:

| MSX address | Direction | Purpose |
| --- | --- | --- |
| `#7FFD` | MSX to MCU | keyboard bytes |
| `#7FFE` | MSX to MCU | mode: 0 starts the terminal, non-zero asks for the ROM |
| `#7FFF` | MCU to MSX | output bytes, 0 when the queue is empty |
| `#4000`-`#40CC` | MCU to MSX | the MSX side program itself |

When the MSX side asks for the ROM, the bootloader sets the handover flag at
`0x20007C00` and resets. The cartridge firmware reads the same flag at startup
and starts the ROM instead of the terminal, so the application still runs
after the bootloader has done its work. The bootloader's RAM layout leaves
that address free: its RAM is 31 KiB, exactly like the cartridge firmware's,
and the stack ends where the flag begins.

The bootloader's copy of the program is assembled with
`MSXTerminal/sjasmplus.exe`, which needs Wine on Linux. The assembler
reproduces the committed `msxterm.bin` byte for byte, so the variant is
built from a known-good source.

## Requirements

* GNU make, Python 3, and the WCH RISC-V GCC toolchain. The default path is a
  local `MRS_Toolchain_Linux_x64_V210/RISC-V Embedded GCC12/bin` directory at
  the repository root; the toolchain is not included in the repository.
  Override `TOOLCHAIN_DIR` and `PREFIX` if needed.
* A WCH-Link and a **compatible minichlink build** with CH32V20x/30x boot
  region erase and programming support. The script checks for the corresponding
  diagnostic in the executable; this is not a guarantee of correct operation.
  The system-installed minichlink may be too old. Pass its path as
  `MINICHLINK=/path/to/minichlink`. See
  [minichlink source](https://github.com/cnlohr/ch32fun/blob/master/minichlink/minichlink.c).
* Run programming operations from an interactive terminal. Do not use USB ISP
  as the programmer for replacing the bootloader itself.
* A properly powered target and common ground with the WCH-Link. On rev 2.2,
  the cartridge USB host port is behind the CH217K and cannot be relied on
  to power the target from a PC. A standalone target-power connection has not
  yet been validated; check the schematic and power setup before proceeding.
  See the [application programming instructions](../README.md#programming-the-firmware).
* The supplied programming script requires the programmer to identify a
  CH32V303; it refuses to operate on other chips, including CH32V307.

To build a local programmer without replacing the system executable, use an
existing ch32fun Git checkout and its committed `HEAD`:

```sh
make setup-programmer CH32FUN_DIR=/path/to/ch32fun
```

By default this looks for `ch32fun` alongside the RISKYMSX checkout. It
requires a C compiler, Git, libusb and libudev development libraries. The
result is placed in `tools/minichlink` and used automatically by subsequent
make commands. Uncommitted changes in the ch32fun checkout are not included.
An explicit `MINICHLINK` setting still overrides it.

The programmer lives in `tools/`, not in `build/`, so `make clean` does not
remove it. The `make backup`, `make flash`, and `make restore` targets build
it automatically when it is missing, even if `MINICHLINK` is explicitly set.
They therefore require a ch32fun checkout and build dependencies for that first
build. The script falls back to the local executable when the selected
programmer is an older build without CH32V30x boot-region support.

## Build, back up, and replace the bootloader

From the repository's `bootloader/` directory:

```sh
make
make backup
make flash
```

`make` produces `build/RISKYMSXBootloader.elf` and the 28 KiB
`build/RISKYMSXBootloader.bin` boot-region image. `make flash` uses the
binary automatically.

`make backup` saves two independently read copies' comparison result, the raw
28 KiB image and its SHA-256 checksum in a newly created `backups/boot-*`
directory, never overwriting earlier backups. A uniform or inconsistent read
is rejected. A matching pair does **not** prove the data is the original
factory bootloader or that it can be restored. Save the backup elsewhere; the
directory is excluded from Git and is not removed by `make clean`.
Read protection or programmer limitations can prevent a useful backup; do not
disable protection automatically, since doing so may erase data.

`make flash` creates another backup before asking for explicit confirmation.
The programmer asks for a second confirmation and erases the whole boot region.
After programming, the script reads back all 28 KiB and compares against the
image. Do not proceed if either backup or verification fails.

To run the bootloader, select system boot with BOOT0=1 and BOOT1=0, then
power-cycle or reset. The original factory ISP interface is no longer available
while the bootloader is installed. Restore normal boot by setting BOOT0=0 and
resetting.

## Attempt restoration

Using the original backup directory, not the later backup of your custom image:

```sh
make restore MINICHLINK=/path/to/minichlink BACKUP=/absolute/path/to/boot-.../bootloader.bin
```

The checksum is checked, then restoration requires explicit confirmation and a
complete read-back comparison. This can only restore what was actually read;
it cannot guarantee the backup is factory code or that the device is recoverable.
Keep the original backup and checksum even if restoration fails.

## USB host port power

The board feeds the USB host port through a CH217K current limited switch.
Its `EN#` input is active low and is wired to PB12, so the port stays
unpowered until the firmware drives PB12 low. `FLAG#` is an active low open
drain fault output on PB13.

The bootloader enables the switch before it initialises the USB host
controller. Without that the switch is off, the bus reads as empty and the
bootloader reports "Cannot continue without a USB drive" even with a stick
plugged in. The bootloader also checks `FLAG#` and reports a power fault
separately, so an over-current condition is not mistaken for a missing drive.

`tests/test_vbus.py` checks that the bootloader drives the same pins, in the
same polarity, as the cartridge firmware in `firmware/User/gpio.c`, and that
the switch is enabled before the USB host is initialised.

## Flash programming path

The reference manual states that erase and program operations on the main
flash fail while the flash enhanced read mode is active. The hardware clears
that mode after a reset, and `User/appflash.c` disables it again right before
each erase or program step.

The manual also notes that flash erase related functions can only be placed in
the zero-wait area flash. The system boot flash is not documented to be in
that area, so the standard programming path is the default and the fast path
is opt-in.

Whether the main flash can be erased and programmed while executing from the
system boot flash is **not documented by WCH**. The reference manual states
that flash erase functions must be placed in the zero-wait area flash, and the
system boot flash is not documented to be part of that area. The manual's
statement that the main memory cannot be erased or programmed from the system
boot code area appears under the heading "Under the read protection state", so
it is expected to apply only to a read-protected chip.

Both programming paths are therefore selectable at build time:

```sh
make                                  # standard half word programming (default)
make FAST_PROGRAMMING=1               # fast 256 byte page programming
```

The standard path is the documented default method and the safer starting
point. If the erase or program step does not complete on real hardware, the
read-back check reports it and the bootloader does not claim success. If
neither path works from the system boot flash, the flash routines have to be
copied to RAM and executed there, or the bootloader has to move into the main
flash (which is what WCH's own IAP example and the community bootloaders do,
at the cost of application flash space and the need for write protection).

## Source layout

| File | Purpose |
| --- | --- |
| `User/main.c` | Bootloader flow: USB check, image discovery, user confirmation, programming, reporting |
| `User/msxbus.c` | Cartridge bus interception and the `#7FFD`/`#7FFE`/`#7FFF` terminal protocol |
| `User/termui.c` | Terminal rendering, status line and progress display |
| `User/appflash.c` | Erasing, programming and verifying the application flash |
| `User/FATFS/` | FatFs (read only, FAT32 and exFAT) and the USB mass storage glue |
| `User/USB_Host/` | CH32V30x USB host stack, ported from the cartridge firmware |

The FatFs and USB host sources are copies of the cartridge firmware's, with
the write paths and the Nextor raw disk layer removed.

## Tests

```sh
python3 -m unittest discover -s tests
```

The host tests compile the flash programming routine against a stub of the
flash controller and check the erase, program, verify and error reporting
paths, and they simulate the backup, flash and restore workflow with a stub
programmer. A further test checks that the terminal image embedded in
`User/msxbus.c` still matches `MSXTerminal/msxterm_boot.bin` and that the
bootloader variant differs from the firmware variant. They do not exercise the
hardware.

## Current size

The bootloader fits in the 28 KiB system boot flash. Enabling USART diagnostics
pulls in the `printf` formatting code. FatFs and the USB host stack are the largest parts; dropping the
exFAT support (`FF_FS_EXFAT` in `User/FATFS/ffconf.h`) and long file names
(`FF_USE_LFN`) would shrink it further if a FAT32-only, 8.3-name filesystem is
acceptable.

## Clock source

The bootloader runs from the internal HSI, the same way the cartridge firmware
does, because the RISKYMSX board has no external crystal. This matters: with
an HSE setting the clock setup never completes, the core stays on the 8 MHz
internal oscillator while `SystemCoreClock` still reports the HSE frequency,
and every timing derived from it is wrong by the same factor. The USART baud
rate and the delay timers would then be off by a factor of twelve, which
looks exactly like a bootloader that produces no output at all.

`tests/test_clock_source.py` checks that the bootloader's clock setting stays
on the internal oscillator and matches the cartridge firmware's.

## Monitor USART output

USART diagnostics are disabled by default; MSX screen output is unaffected.
To enable the startup `printf` messages and mirror MSX status and progress to
USART1 at 115200 baud, build and flash with:

```sh
make DEBUG=1
make flash DEBUG=1
```

Changing `DEBUG` rebuilds the affected image automatically; use `make flash`
without `DEBUG=1` to build and flash the default, silent version. Screen
control sequences are not sent to the serial monitor. Connect the WCH-Link
serial interface and run:

```sh
make monitor
```

Set `PORT` if the device is not `/dev/ttyACM0`, for example
`make monitor PORT=/dev/serial/by-id/your-device`. Press Ctrl+C to stop.