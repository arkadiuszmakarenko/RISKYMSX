# RISKYMSX

## Acknowledgments

The following people contributed to this project and supported me with technical aspects of this project.
* Terrible Fire
* cnlohr 
* NYYRIKKI
* Aoineko
* BG Ollie
* MadHacker

Testing team:
* PhilC
* Higgy
* ahmsx

  
Thank you very much for your support.

Confirmed working on following MSXs:
* Toshiba HX-10
* Sony HB-101P
* Sony HB-501F
* Philips VG8235/00
* Panasonic FS-A1WSX
* Omega Home Computer
* Tides Rider
* TFMSX Rev 1, Rev 2


![RISKYMSXCart](https://github.com/user-attachments/assets/93ff6b32-1f0f-49db-b7b6-2c4e8e2ec17c)


This is a universal cartridge for MSX series of computes. It can run up to 256KB ROMs, and emulate follwing mappings:
 * Standard 16KB ROMs
 * Standard 32KB ROMs
 * Standard 48KB /64KB ROMs
 * KONAMI without SSC
 * KONAMI with SSC (EN - SCC emulation enabled)
 * KONAMI with SSC (DIS - SCC emulation disabled)
 * ASCII 8K
 * ASCII 16K
 * Neo 8k
 * Neo 16k

## Building cartridge
There is no need for flash button or crystal oscillator.

Programming
This chip can be programed using USB A to USB A cable, serial cable (under Windows) or WCH LinkE programmer (Windows and Linux).

### Windows
You need WCHISPTool from https://wch-ic.com/ website.

#### USB A - USB A Cable or Serial adapter
 * Close jumper B0.
 * Connect RISKY MSX to PC using USB A to USB A cable or serial.
 * For USB A Cable Dnld Port should change to USB.
 * For serial select your serial device COM
 * Select Chip Series - CH32V30x
 * Chip Model : CH32V303VCT6
 * Select Object File1 and pick up firmware .bin.
 * Under Chip Config select Chip Memory Allocation RAMX 32KB + ROM 288KB
 * Click Deprotect
 * Click Download

### Linux
You need WCHISPTool_CMD from https://wch-ic.com/ website.
Install driver by going to Linux/driver and running
`sudo make install`

#### USB A - USB A Cable 
 * Copy RISKYMSX.INI file from repository upgrade folder
 * Close jumper B0.
 * Connect RISKY MSX to PC using USB A to USB A cable or serial.
 * Rename firmware file to something simplier eg. RISKYMSX.bin
 * Check if following file (or very similar) is being created when you plug in cart to PC `/dev/ch37x0`
 * Adapt and run command 
   `sudo ./WCHISPTool_CMD -p /dev/ch37x0 -c RISKYMSX.INI -o program -f RISKYMSX.bin -r 1`
   this command line is case sensitive, so make sure filenames and extentions are in right case.

## General troubleshooting.
If no LEDS are on during power up, You have issues with power lines or shorts.
If during power up and button pressed no LEDs are flashing your MPU doesn't start at all.
If you can download ROM to cart but MSX doesn't load data from cart then you have issues wih soldering of ADDRESS, DATA or BUS lines.

## Flashing ROMs 
Press and hold GRAPH button during MSX power up, until all LEDs on cart turn on.
There might be some text appearing on the screen. This is section checking for auto flashing files.
Follow instructions on the screen.
You will be presented with list of files from root directory of the USB drive (currently DIR navigations are not supported). You can list more files by pressing <RIGHT> <LEFT> arrows.
You select file to flash by hitting <RETURN> then you need to select mapper type used for selected ROM.
Then flashing will take place, and cartridge and MSX should reboot after the process is completed.

<ESC> Reloads USB Flash drive data. This allows hot swapping USB stick during operation.
<1> Change mapper - You can change mapper on a ROM that is already loaded to cart flash.
<9> Changes Long filenames to short filenames. There are some issues with some longfile decoding, if you have issues with it, use short filenames. You can permanently turn this on my adding jumper on TxRx.
<1> Mapper menu. This page allows to change mapper setting for a rom already flashed on the device. It allows quick change of mapper in case wrong one was picked up during ROM flashing.

## Auto flash files
If you want to load flash fast without need of using GUI then just place one file with below naming convention, and cart will pick it up after entering cart programming mode.

You need to rename rom file to following name, based on mapper used

 | Mapper               | filename         | 
 |   :-----------:      | :--------------: | 
 | 16KB ROM             | cart.r16         | 
 | 32KB ROM             | cart.r32         | 
 | KONAMI without SSC   | cart.ko4         | 
 | KONAMI with SSC(EN)  | cart.ko5         | 
 | KONAMI with SSC(DIS) | cart.kd5         | 
 | ASCII8k              | cart.a8k         | 
 | ASCII16k             | cart.a16         | 
 | NEO16                | cart.n16         | 
 | NEO8                 | cart.n8k         | 

1. Copy file to root directory of a FAT32 formatted drive. It has to be FAT32, EX-FAT is NOT supported.
2. Power off your MSX with a cart inserted. 
3. Place flash drive in cartridge USB.
4. Hold button pressed
5. Power on MSX.
6. Cartridge should display information about flashing. If flashing takes just few seconds (smaller ROMs) you may not see any messages.
7. MSX should reboot and game should load automatically.


Cartridge will have various LED configurations based on type of mapper used.


