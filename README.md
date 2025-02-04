# RISKYMSX

## Acknowledgments

The following people contributed to this project and supported me at every stage of the work:
* Terrible Fire
* cnlohr 
* NYYRIKKI
* Aoineko
* BG Ollie
Thank you very much for your support.


![Rev1](https://github.com/user-attachments/assets/d9c3ec10-cc1b-43a5-8a99-4679dabca454)


This is a universal cartridge for MSX Comupter. It can hold up to 256KB ROMs, and emulate follwoign MEGAROM mappings
 * Standard 32KB ROMs
 * KONAMI without SSC
 * KONAMI with SSC
 * ASCII 8K
 * ASCII 16K

## Building cartridge

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
 * Enjoy 

General troubleshooting.
If no LEDS are on during power up, You have issues with power lines or shorts.
If during power up and button pressed no LEDs are flashing your MPU doesn't start at all.
If you can download ROM to cart but MSX doesn't load data from cart then you have issues wih soldering of ADDRESS, DATA or BUS lines.

## Flashing ROMs 
Press Flash button during MSX power up. Follow instructions on the screen.
You will be presented with list of files from root directory of the USB drive (currently DIR navigations are not supported). You can list more files by pressing <RIGHT> <LEFT> arrows.
You select file to flash by hitting <RETURN> then you need to select mapper type used for selected ROM.
Then flashing will take place, and cartridge and MSX should reboot after the process is completed.

## Auto flash files
If you want to load flash fast without need of using GUI then just place one file with below naming convention, and cart will pick it up after entering cart programming mode.

You need to rename rom file to following name, based on mapper used

 | Mapper               | filename         | 
 |   :-----------:      | :--------------: | 
 | 32KB ROM             | cart.r32         | 
 | KONAMI without SSC   | cart.ko4         | 
 | KONAMI with SSC(EN)  | cart.ko5         | 
 | KONAMI with SSC(DIS) | cart.kd5         | 
 | ASCII8k              | cart.a8k         | 
 | ASCII16k             | cart.a16         | 
 | NEO16                | cart.n16         | 
 | NEO8                 | cart.n8k         | 

1. Copy file to root directory of a FAT32 formatted drive. It has to be FAT32, currently EX-FAT is not supported.
2. Power off your MSX with a cart inserted. 
3. Place flash drive in cartridge USB.
4. Hold button pressed
5. Power on MSX.
6. Cartridge should display information about flashing. If flashing takes just few seconds (smaller ROMs) you may not see any messages.
7. MSX should reboot and game should load automatically.


Cartridge will have various LED configurations based on type of mapper used.


