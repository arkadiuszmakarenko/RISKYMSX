FIRMWARE_BUILD := firmware/build
FIRMWARE_BIN := RISKYMSXCART.bin
FIRMWARE_HEX := RISKYMSXCART.hex
BOOTLOADER_BIN := Bootloader.bin
NEXTOR_NXT := CART.NXT

.PHONY: all firmware bootloader nextor clean

all: firmware bootloader nextor

firmware:
	$(MAKE) -C firmware build/RISKYMSXCART.bin build/RISKYMSXCART.hex
	cp "$(FIRMWARE_BUILD)/RISKYMSXCART.bin" "$(FIRMWARE_BIN)"
	cp "$(FIRMWARE_BUILD)/RISKYMSXCART.hex" "$(FIRMWARE_HEX)"

bootloader:
	$(MAKE) -C bootloader
	cp bootloader/build/RISKYMSXBootloader.bin "$(BOOTLOADER_BIN)"

nextor:
	$(MAKE) -C NextorDriver
	cp NextorDriver/Nextor-3.0.RISKYMSX2.ROM "$(NEXTOR_NXT)"

clean:
	$(MAKE) -C firmware clean
	$(MAKE) -C bootloader clean
	rm -f "$(FIRMWARE_BIN)" "$(FIRMWARE_HEX)" "$(BOOTLOADER_BIN)"
