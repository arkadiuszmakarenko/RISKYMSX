# RISKYMSX

The RISKYMSXCart by Arkadiusz Makarenko is a compact MSX flash ROM cartridge based on the inexpensive CH32V30xVCT6 RISC-V MCU which can be easily programmed from a MSX computer using a FAT formatted USB pen drive containing ROM files.

[<img src="images/RISKYMSXCart-Rev2.1-front-populated-1024px.png" width="400"/>](images/RISKYMSXCart-Rev2.1-front-populated-1024px.png)


## Main Features

* Support for up to 256KB ROMs
* SCC emulation
* Firmware updates using a USB-A cable (or alternatively a WCH-LinkE programmer)
* ROM Flashing:
  * No external flash programmer is needed
  * A ROM can be flashed in-system using a MSX computer and a FAT-formatted USB pen drive
  * `ROM Flashing Mode` is activated by pressing GRAPH key during MSX boot
  * Automatic flashing of ROMs from USB can be achieved via specially named ROM archives
  * Manually flashing of ROMs from USB is done using a simple interactive menu
  * FAT long filenames are supported
* Supported mappers:
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


## Hardware

The RISKYMSXCart is made up of a 4-layer PCB with a reduced set of SMD components and through-hole connectors:
* A WCH [CH32V303VCT6](https://www.wch-ic.com/products/CH32V303.html) MCU (or alternatively a [CH32V307VCT6](https://www.wch-ic.com/products/CH32V307.html))
* Two LEDs, one to indicate power on state, and one lit during `ROM Flashing Mode`
* A [LM1117](https://www.ti.com/lit/ds/symlink/lm1117.pdf) voltage regulator to provide +3V3 to the MCU from the +5V rail of the MSX cartridge slot
* A [MCP6001](https://ww1.microchip.com/downloads/aemDocuments/documents/MSLD/ProductDocuments/DataSheets/MCP6001-1R-1U-2-4-1-MHz-Low-Power-Op-Amp-DS20001733L.pdf) operational amplifier to enhance the SCC audio signal fed into the MSX cartridge SOUNDIN pin
* Three headers:
  * B0: open by default, closed only while programming the MCU using the `WCHISPTool_CMD` tool
  * DAT/CLK: unconnected by default, used to program the MCU using the [`WCH-LinkE`](https://www.wch-ic.com/products/WCH-Link.html) programmer via SWDIO/SWCLK
  * RX/TX: unconnected by default, serial debug console
* One USB Type-A female connector
* Several passives required for proper operation of the above components

### Revisions

There are several hardware revisions of the cartridge. The main ones are listed here in reverse chronological order.

#### Rev 2.1 (current)

This version introduces the following changes:
* the flash button is removed, as the flashing interface can be accessed by pressing the GRAPH key on the MSX keyboard during boot
* unnecessary leds are removed, leaving just the Flash and Power leds
* the audio amplifier circuit is enhanced with a low pass filter 

See the [Bill Of Materials (BoM)](https://html-preview.github.io/?url=https://raw.githubusercontent.com/arkadiuszmakarenko/RISKYMSX/main/board/bom/ibom.html) generated from the KiCad files for information about the specific components used.

Have a look at the [schematic and PCB](https://kicanvas.org/?github=https%3A%2F%2Fgithub.com%2Farkadiuszmakarenko%2FRISKYMSX%2Ftree%2Fmain%2Fboard) using [KiCanvas](https://kicanvas.org).

[<img src="images/RISKYMSXCart-Rev2.1-front-render.png" width="512"/>](images/RISKYMSXCart-Rev2.1-front-render.png)
[<img src="images/RISKYMSXCart-Rev2.1-back-render.png" width="512"/>](images/images/RISKYMSXCart-Rev2.1-back-render.png)

#### Rev 2 (obsolete)

This version introduces the following changes:
* the LM1117 voltage regulator and related passives are moved to the back of the PCB
* the B1 header is removed as it has no practical use
* a simple audio amplifier circuit is added for SCC sound

[<img src="images/RISKYMSXCart-Rev2-front-render.png" width="256"/>](images/RISKYMSXCart-Rev2-front-render.png)|[<img src="images/RISKYMSXCart-Rev2-back-render.png" width="256"/>](images/RISKYMSXCart-Rev2-back-render.png)
|-|-|
|RISKYMSX Rev 2 front render|RISKYMSX Rev 2 rear render|

#### Rev 1 (obsolete)

This is the initial revision of the RISKYMSX cartridge.

[<img src="images/RISKYMSXCart-Rev1-front-render.png" width="256"/>](images/RISKYMSXCart-Rev1-front-render.png)|[<img src="images/RISKYMSXCart-Rev1-back-render.png" width="256"/>](images/RISKYMSXCart-Rev1-back-render.png)
|-|-|
|RISKYMSX Rev 1 front render|RISKYMSX Rev 1 rear render|


## Firmware

The firmware implements two different modes of operation for the RISKYMSXCart:
* `Normal Mode`. In this mode a ROM cartridge is emulated according to the last ROM flashed using the `ROM Flashing Mode`.
* `ROM Flashing Mode`. In this mode a ROM can be flashed manually or automatically into the cartridge by using a FAT formatted USB pen drive.


## Building a RISKYMSX cartridge

You will need good soldering skills and specific equipment to build a RISKYMSX cartridge.
As you probably saw on the Bill Of Materials, there are quite small SMD components like 0603 passives and a LQFP-100 package with 0.5mm pitch that need to be soldered.

> [!WARNING]
> If you are building new boards, please use the current available board revision.

### PCB

The PCB can be fabricated by your favorite PCB manufacturer from the [supplied gerber files](https://github.com/arkadiuszmakarenko/RISKYMSX/blob/main/RiskyMSXv21PCB.zip).

### Suggested tools

These are example tools used during the manual building of the RISKYMSX cartridge:

* silicone heat-resistant mat (TE-616 400x290mm), to avoid burning your precious desk
* electronic microscope with 2 LEDs (KKmoon 3" LCD 10X-300X 3.0MP 1080P HDMI electronic digital video microscope), to be able to solder those small 0.5mm pitch pins
* temperature controlled soldering station (Yihua 908D), to make good soldering joints
* T-1C "horseshoe", T-I and T-1.6D soldering tips (900M series), use the best fitting one for each soldering job
* copper sponge, for soldering head cleaning
* flux (Mechanic UV80 flux paste), to help keep components in place and achieve good soldering joints
* solder (0.5mm, for hobby projects you may use 60% Sn / 40% Pb Flux 2% at your own risk)
* desoldering wick (2.5mm is fine), for correcting mistakes if you are human)

### Component shopping list

> [!WARNING]
> Aliexpress links may not work, as sellers come and go.

| **ref**                                   | **component**  | **req_pcs** | **item_description**                                                                                                             | **item_option**      | **pcs_per_item** | **req_items** | **link**                                                                                                       |
| ----------------------------------------- | -------------- | ----------- | -------------------------------------------------------------------------------------------------------------------------------- | -------------------- | ---------------- | ------------- | -------------------------------------------------------------------------------------------------------------- |
| C2, C4, C5, C6, C7, C8, C9, C10, C11, C19 | 0603 100nF     | 10          | 100pcs 0603 SMD Chip Multilayer Ceramic Capacitor 0.5pF - 22uF 10pF 22pF 100pF 1nF 10nF 15nF 100nF 0.1uF 1uF 2.2uF 4.7uF 10uF    | 0603 100nF 50V       | 100              | 1             | [aliexpress](https://aliexpress.com/item/32966526545.html)                   |
| C18                                       | 0603 1uF       | 1           | 100pcs 0603 SMD Chip Multilayer Ceramic Capacitor 0.5pF - 22uF 10pF 22pF 100pF 1nF 10nF 15nF 100nF 0.1uF 1uF 2.2uF 4.7uF 10uF    | 0603 1uF 25V         | 100              | 1             | [aliexpress](https://aliexpress.com/item/32966526545.html)                   |
| C12, C20                                  | 0603 4.7uF     | 2           | 100pcs 0603 SMD Chip Multilayer Ceramic Capacitor 0.5pF - 22uF 10pF 22pF 100pF 1nF 10nF 15nF 100nF 0.1uF 1uF 2.2uF 4.7uF 10uF    | 0603 4.7uF 10V       | 100              | 1             | [aliexpress](https://aliexpress.com/item/32966526545.html)                   |
| C3                                        | 0603 10uF      | 1           | 100pcs 0603 SMD Chip Multilayer Ceramic Capacitor 0.5pF - 22uF 10pF 22pF 100pF 1nF 10nF 15nF 100nF 0.1uF 1uF 2.2uF 4.7uF 10uF    | 0603 10uF 10V        | 100              | 1             | [aliexpress](https://aliexpress.com/item/32966526545.html)                   |
| C1                                        | 1206 100nF     | 1           | 100pcs 1206 SMD Chip Multilayer Ceramic Capacitor 0.5pF - 100uF 10pF 100pF 1nF 10nF 15nF 100nF 0.1uF 1uF 2.2uF 4.7uF 10uF 47uF   | 1206 100nF 50V       | 100              | 1             | [aliexpress](https://aliexpress.com/item/32966490820.html)                   |
| C13                                       | 1206 1uF       | 1           | 100pcs 1206 SMD Chip Multilayer Ceramic Capacitor 0.5pF - 100uF 10pF 100pF 1nF 10nF 15nF 100nF 0.1uF 1uF 2.2uF 4.7uF 10uF 47uF   | 1206 1uF 25V         | 100              | 1             | [aliexpress](https://aliexpress.com/item/32966490820.html)                   |
| R1, R2, R4, R9, R10                       | 0603 10K       | 5           | 100PCS 0603 1% SMD resistor 0R ~ 10M 1/10W 0 1 10 100 150 220 330 470 ohm 1K 2.2K 10K 100K 1M 0R 1R 10R 100R 150R 220R 330R 470R | 10K                  | 100              | 1             | [aliexpress](https://aliexpress.com/item/1005005677654015.html)         |
| R3, R5                                    | 0603 220       | 2           | 100PCS 0603 1% SMD resistor 0R ~ 10M 1/10W 0 1 10 100 150 220 330 470 ohm 1K 2.2K 10K 100K 1M 0R 1R 10R 100R 150R 220R 330R 470R | 220R                 | 100              | 1             | [aliexpress](https://aliexpress.com/item/1005005677654015.html)         |
| D1                                        | 0805 green LED | 1           | 5 colors x20pcs =100pcs SMD 0805 led kit Red/Green/Blue/Yellow/White LED Light Diode Free Shipping! KIT                          | n/a                  | 20               | 1             | [aliexpress](https://aliexpress.com/item/32288211535.html)                   |
| D5                                        | 0805 red LED   | 1           | 5 colors x20pcs =100pcs SMD 0805 led kit Red/Green/Blue/Yellow/White LED Light Diode Free Shipping! KIT                          | n/a                  | 20               | 0             |                                                                                                                |
| U1                                        | CH32V307VCT6   | 1           | 1-50Pcs New CH32V303VCT6 CH32V303 VCT6 CH32V LQFP-100 Microcontroller IC Chip In Stock Wholesale                                 | 2Pcs CH32V303VCT6    | 2                | 1             | [aliexpress](https://aliexpress.com/item/1005006687185523.html)         |
| U2                                        | MCP6001-OT     | 1           | 20PCS 100% New MCP6001T-I/OT SOT23-5 MCP6001T-I SOT23 MCP6001 SMD MCP6001T Chipset                                               | n/a                  | 20               | 1             | [aliexpress](https://aliexpress.com/item/1005006809254782.html)         |
| F2                                        | 3216 0.5A      | 1           | 3216 1206 0.05A 0.1A 0.12A 0.16A 0.2A 0.25A 0.5A 0.75A 1.1A 2A SMD Resettable Fuse PPTC PolySwitch Self-Recovery Fuse            | 500MA, 1206, 20 Pcs  | 20               | 1             | [aliexpress](https://aliexpress.com/item/1005004273302126.html) |
| IC8                                       | LM1117MPX-3.3  | 1           | 5PCs LM1117MPX-1.8 N12A LM1117MPX-2.5 N13A LM1117MPX-3.3 N05A/B LM1117MPX-5.0 N06A LM1117MPX-AJ N03A SOT-223 Linear Regulator    | LM1117MPX-3.3        | 5                | 1             | [aliexpress](https://aliexpress.com/item/1005007393149096.html)         |
| J1, J2, J8                                | Conn_01x02     | 1           | 10pcs 40 Pin 1x40 Single Row Male and Female 2.54 Breakable Pin Header PCB JST Connector Strip for arduino DIY Kit               | Male Color 2pcs each | 10               | 1             | [aliexpress](https://aliexpress.com/item/1005007324368709.html) |
| J7                                        | USB_1          | 1           | Hot Sale USB Type A Standard Port Female Solder Jacks Connector PCB Socket Type B 90 Degree Horizontal Straight Insertion Patch  | 10PCS USB-02         | 10               | 1             | [aliexpress](https://aliexpress.com/item/1005005865561469.html) |

Beware of fake chips and components:
* If you receive MCP6001 op-amps marked as SA10, they are probably MCP601 op-amps.
* MCP6001 op-amps marked as AAYS should be fine.
* LM1117MPX-3.3 voltage regulators marked as N05A should be fine.


### Assembling the hardware

The central piece of the cartridge is the CH32V30xVCT6 MCU which is at the same time the most difficult element to solder.

You can solder components in the following suggested order, or do as you feel comfortable:
* U1 CH32V30xVCT6
* D1,D5 leds
* F2 fuse
* R3, R5 led resistors
* R1 boot0 pull-down resistor
* IC8 LM1117MPX-3.3
* C3 10uF capacitor
* C7 and C1 100nF capacitors
* C13 1uF capacitor
* C4, C2, C6, C8, C5, C11 and C10 100nF capacitors
* R4 and C9 do not need to be populated (confirmation needed)
* U2 MCP6001-OT op-amp
* C12 and C20 capacitors
* C19 capacitor
* C18 capacitor
* R10, R9 and R2 resistors
* C14 and C17 capacitors

> [!WARNING]
> C5 is not marked on the PCB, it is the capacitor to the right of the C11 marking (C11 mark is closer to C5 than to C11)
>
> C11 is the capacitor to the right of the C5 capacitor

### Programming the firmware

TBE

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


## Cartridge Operation

### Normal Mode

TBD

### ROM Flashing Mode

TBE

Press and hold GRAPH button during MSX power up, until all LEDs on cart turn on.
There might be some text appearing on the screen. This is section checking for auto flashing files.
Follow instructions on the screen.
You will be presented with list of files from root directory of the USB drive. You can list more files by pressing <RIGHT> <LEFT> arrows.
You select file to flash by hitting <RETURN> then you need to select mapper type used for selected ROM.
Then flashing will take place, and cartridge and MSX should reboot after the process is completed.

<ESC> Reloads USB Flash drive data. This allows hot swapping USB stick during operation.
<1> Mapper menu. This page allows to change mapper setting for a rom already flashed on the device. It allows quick change of mapper in case wrong one was picked up during ROM flashing.

#### Auto flash files
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
4. Press and hold the GRAPH key
5. Power on MSX.
6. Cartridge should display information about flashing. If flashing takes just few seconds (smaller ROMs) you may not see any messages.
7. MSX should reboot and game should load automatically.

Cartridge will have various LED configurations based on type of mapper used.


## General troubleshooting.
If no LEDS are on during power up, You have issues with power lines or shorts.
If during power up and button pressed no LEDs are flashing your MPU doesn't start at all.
If you can download ROM to cart but MSX doesn't load data from cart then you have issues wih soldering of ADDRESS, DATA or BUS lines.


## Compatibility

The RISKYMSXCart has been tested on the following MSX computers:

| **Model**                | **RISKYMSX**              |
|--------------------------|---------------------------|
| Sony MSX HB-101P         |          OK               |
| Sony MSX HB-501F         |          OK               |
| Toshiba MSX HX-10        |          OK               |
| Philips MSX2 VG-8235     |          OK               |
| Panasonic MSX2+ FS-A1WSX |          OK               |
| Omega MSX2+              |          OK               |
| MSXVR                    |          `NOK`            |

Other MSX compatible computers will probably work too, but have not been specifically tested.


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



