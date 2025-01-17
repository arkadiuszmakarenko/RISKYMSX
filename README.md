# RISKYMSX

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

You need WCHISPTool from https://wch-ic.com/ website.

USB A
Connect RISKY MSX to PC using USB A to USB A cable or serial.
For USB A Cable Dnld Port should change to USB.
For serial select your serial device COM
Select Chip Series - CH32V30x
Chip Model : CH32V303VCT6
Select Object File1 and pick up firmware .bin.
Under Chip Config select Chip Memory Allocation RAMX 32KB + ROM 288KB
Click Download

General troubleshooting.
If no LEDS are on during power up, You have issues with power lines or shorts.
If during power up and button pressed no LEDs are flashing your MPU doesn't start at all.
If you can download ROM any kind of .rom doesn't load then you have issues wih soldering of DATA or BUS lines.

## Using cartridge
You need to rename rom file to following name, based on mapper used

 | Mapper            | filename         | 
 |   :-----------:   | :--------------: | 
 | 32KB ROM          | cart.r32         | 
 | KONAMI without SSC| cart.kon         | 
 | KONAMI with SSC   | cart.scc         | 
 | ASCII8k           | cart.a8k         | 
 | ASCII16k          | cart.a16         | 

1. Copy file to root directory of a FAT32 formatted drive. It has to be FAT32, currently EX-FAT is not supported.
2. Power off your MSX with a cart inserted. 
3. Place flash drive in cartridge USB.
4. Hold button pressed
5. Power on MSX.
6. Cartridge should flash all LEDs and flashing should start.
7. When all 4 LEDs are on, you can power off and on your MSX again.

Cartridge will have various LED configurations based on type of mapper used.
