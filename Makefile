################################################################################
# Universal Makefile for RISKYMSX firmware (no MRS-generated files required)
# - Discovers sources under firmware/
# - Builds into a local build directory
# - Keeps flash/erase helpers
################################################################################

.DEFAULT_GOAL := all

# Paths
ROOT       := $(CURDIR)
BUILD      ?= build-standalone
OUT_DIR    ?= $(ROOT)/out

PROJECT    := RISKYMSXCART
ELF        := $(BUILD)/$(PROJECT).elf
BIN        := $(BUILD)/$(PROJECT).bin
HEX        := $(BUILD)/$(PROJECT).hex
MAP        := $(BUILD)/$(PROJECT).map
LST        := $(BUILD)/$(PROJECT).lst

# Optional: use packaged MRS toolchain if present; otherwise rely on system toolchain
TOOLCHAIN_DIR ?= $(ROOT)/MRS_Toolchain_Linux_x64_V210/RISC-V Embedded GCC12
TOOLCHAIN_BIN := $(TOOLCHAIN_DIR)/bin
# Robust PATH prepend that tolerates spaces in paths
PREPEND_PATH  := $(shell if [ -d "$(TOOLCHAIN_BIN)" ]; then printf '%s' "$(TOOLCHAIN_BIN):"; fi)
export PATH   := $(PREPEND_PATH)$(PATH)

# Toolchain prefix
CROSS    ?= riscv-wch-elf
CC       := $(CROSS)-gcc
AS       := $(CROSS)-gcc
LD       := $(CROSS)-gcc
OBJCOPY  := $(CROSS)-objcopy
OBJDUMP  := $(CROSS)-objdump
SIZE     := $(CROSS)-size
GDB      := $(CROSS)-gdb

# Linker script
LDSCRIPT := firmware/Ld/Link.ld

# Includes
INCLUDES := \
  -Ifirmware/Debug \
  -Ifirmware/Core \
  -Ifirmware/User \
  -Ifirmware/User/USB_Host \
  -Ifirmware/User/FATFS \
  -Ifirmware/Peripheral/inc \
  -Ifirmware/Startup

# Flags
ARCHFLAGS := -march=rv32imacxw -mabi=ilp32
CWARN     := -Wunused -Wuninitialized
CDEFS     :=

CFLAGS := $(ARCHFLAGS) -msmall-data-limit=8 -msave-restore \
  -fmax-errors=20 -Oz -flto -fno-unwind-tables -fno-asynchronous-unwind-tables \
  -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common \
  -std=gnu99 $(CWARN) $(CDEFS) $(INCLUDES) -MMD -MP

# .S are preprocessed by gcc automatically; keep flags conservative
ASFLAGS := $(ARCHFLAGS) $(INCLUDES)

LDFLAGS := $(ARCHFLAGS) -T $(LDSCRIPT) -nostartfiles -Wl,--gc-sections -flto \
  -Wl,-Map,$(MAP) -Wl,--print-memory-usage --specs=nano.specs --specs=nosys.specs -Wl,-s

# Source discovery (exclude this Make's build dir and any legacy firmware/build)
FIND_EXCLUDES := \( -path $(BUILD) -o -path firmware/build \) -prune -o
SRCS_C := $(shell find firmware $(FIND_EXCLUDES) -name '*.c' -print)
SRCS_S := $(shell find firmware $(FIND_EXCLUDES) \( -name '*.S' -o -name '*.s' \) -print)

# Object paths mirror source tree under $(BUILD)/obj
OBJS_C := $(patsubst %.c,$(BUILD)/obj/%.o,$(SRCS_C))
OBJS_S := $(patsubst %.S,$(BUILD)/obj/%.o,$(SRCS_S))
OBJS_S := $(patsubst %.s,$(BUILD)/obj/%.o,$(OBJS_S))
OBJS   := $(OBJS_C) $(OBJS_S)
DEPS   := $(OBJS:.o=.d)

# Ensure directories exist
DIRS := $(sort $(dir $(OBJS)) $(BUILD) $(OUT_DIR))
$(DIRS):
	@mkdir -p "$@"

# ========================
# Cartridge payload assets
# ========================
ASSETS_DIR     ?= assets
FLASHCFG_SRC   ?= $(strip $(ASSETS_DIR)/flashcfg.bin)     # input config (<=256 B)
FLASHCART_SRC  ?= $(strip $(ASSETS_DIR)/flashcart.bin)    # input cart image (<=256 KiB)

CART_IMG       := $(OUT_DIR)/cart_image.bin
FLASHCFG_PAD   := $(ASSETS_DIR)/flashcfg.bin
FLASHCART_PAD  := $(ASSETS_DIR)/flashcart.bin

# Layout (align with Link.ld MEMORY map)
# Bootloader: 0x00000000 to 0x00000C00 (3K)
# Firmware FLASH: 0x00000C00 to 0x00007F00 
# FLASHCFG at end of internal FLASH (0x00007F00, 256 B)
# FLASHCART starts at 0x00008000 (256 KiB window)
BOOTLOADER_OFFSET ?= 0x00000000
FIRMWARE_OFFSET   ?= 0x00000C00
FLASHCFG_OFFSET   ?= 0x00007F00
FLASHCART_OFFSET  ?= 0x00008000

BOOTLOADER_SIZE   ?= 3072
FIRMWARE_MAX_SIZE ?= 29440    # 0x7300 (from 0xC00 to 0x7F00)
FLASHCFG_SIZE     ?= 256
FLASHCART_SIZE    ?= 262144


# Pad/truncate helpers (pads with 0xFF)
$(FLASHCFG_PAD): $(FLASHCFG_SRC) | $(OUT_DIR)
	@dd if=/dev/zero bs=1 count=$(FLASHCFG_SIZE) 2>/dev/null | tr '\000' '\377' > "$@"
	@dd if="$(FLASHCFG_SRC)" of="$@" conv=notrunc bs=1 count=$(FLASHCFG_SIZE) 2>/dev/null

$(FLASHCART_PAD): $(FLASHCART_SRC) | $(OUT_DIR)
	@dd if=/dev/zero bs=1 count=$(FLASHCART_SIZE) 2>/dev/null | tr '\000' '\377' > "$@"
	@dd if="$(FLASHCART_SRC)" of="$@" conv=notrunc bs=1 count=$(FLASHCART_SIZE) 2>/dev/null

# Combined image (memory map relative to 0x00000000):
#   cfg at FLASHCFG_OFFSET (0x00007F00), cart at FLASHCART_OFFSET (0x00008000)
#   padded with 0xFF for holes
$(CART_IMG): $(FLASHCFG_PAD) $(FLASHCART_PAD) | $(OUT_DIR)
	@# Create a fully padded image of size FLASHCART_OFFSET + FLASHCART_SIZE
	@dd if=/dev/zero bs=1 count=$$(( $(FLASHCART_OFFSET) + $(FLASHCART_SIZE) )) 2>/dev/null | tr '\000' '\377' > "$@"
	@# Write cfg and cart at their offsets
	@dd if="$(FLASHCFG_PAD)"  of="$@" bs=1 seek=$$(( $(FLASHCFG_OFFSET) ))  conv=notrunc 2>/dev/null
	@dd if="$(FLASHCART_PAD)" of="$@" bs=1 seek=$$(( $(FLASHCART_OFFSET) )) conv=notrunc 2>/dev/null
	@echo "Built $(CART_IMG)"
	@echo "  FLASHCFG  @ $(FLASHCFG_OFFSET) size $(FLASHCFG_SIZE)"
	@echo "  FLASHCART @ $(FLASHCART_OFFSET) size $(FLASHCART_SIZE)"

# High-level aliases
assets: $(CART_IMG)

# Create a full flash image combining firmware, flashcfg and flashcart at correct offsets
# The image layout follows firmware Link.ld:
#  - firmware BIN @ 0x00000800 (must fit before FLASHCFG_OFFSET)
#  - FLASHCFG @ $(FLASHCFG_OFFSET) (size $(FLASHCFG_SIZE))
#  - FLASHCART @ $(FLASHCART_OFFSET) (size $(FLASHCART_SIZE))
DD_OUT ?= $(OUT_DIR)/full_flash_image.bin
dd: $(BIN) $(FLASHCFG_PAD) $(FLASHCART_PAD) | $(OUT_DIR)
	@echo "Building full flash image -> $(DD_OUT)"
	@# create base image filled with 0xFF
	@dd if=/dev/zero bs=1 count=$$(( $(FLASHCART_OFFSET) + $(FLASHCART_SIZE) )) 2>/dev/null | tr '\000' '\377' > "$(DD_OUT)"
	@# ensure firmware fits into the reserved FLASH region
	@bsz=$$(stat -c%s "$(BIN)"); maxsz=$$(( $(FLASHCFG_OFFSET) - $(FIRMWARE_OFFSET) )); \
	if [ $$bsz -gt $$maxsz ]; then \
		echo "ERROR: firmware $(BIN) size ($$bsz) exceeds reserved FLASH space ($$maxsz) between $(FIRMWARE_OFFSET) and $(FLASHCFG_OFFSET)."; \
		exit 1; \
	fi
	@dd if="$(BIN)" of="$(DD_OUT)" bs=1 seek=$$(( $(FIRMWARE_OFFSET) )) conv=notrunc 2>/dev/null
	@dd if="$(FLASHCFG_PAD)" of="$(DD_OUT)" bs=1 seek=$$(( $(FLASHCFG_OFFSET) )) conv=notrunc 2>/dev/null
	@dd if="$(FLASHCART_PAD)" of="$(DD_OUT)" bs=1 seek=$$(( $(FLASHCART_OFFSET) )) conv=notrunc 2>/dev/null
	@echo "Full flash image written to $(DD_OUT)"

# Create a complete flash image including bootloader + firmware + flashcfg + flashcart
# This combines all sections according to the memory layout defined in linker scripts
COMBINED_OUT ?= $(OUT_DIR)/complete_flash_image.bin
combine: $(BOOTLOADER_BIN) $(BIN) $(FLASHCFG_PAD) $(FLASHCART_PAD) | $(OUT_DIR)
	@echo "Building complete flash image with bootloader -> $(COMBINED_OUT)"
	@# create base image filled with 0xFF
	@dd if=/dev/zero bs=1 count=$$(( $(FLASHCART_OFFSET) + $(FLASHCART_SIZE) )) 2>/dev/null | tr '\000' '\377' > "$(COMBINED_OUT)"
	@# check bootloader size
	@bsz=$$(stat -c%s "$(BOOTLOADER_BIN)"); maxsz=$(BOOTLOADER_SIZE); \
	if [ $$bsz -gt $$maxsz ]; then \
		echo "ERROR: bootloader $(BOOTLOADER_BIN) size ($$bsz) exceeds reserved space ($$maxsz)."; \
		exit 1; \
	fi
	@# check firmware size  
	@bsz=$$(stat -c%s "$(BIN)"); maxsz=$(FIRMWARE_MAX_SIZE); \
	if [ $$bsz -gt $$maxsz ]; then \
		echo "ERROR: firmware $(BIN) size ($$bsz) exceeds reserved FLASH space ($$maxsz)."; \
		exit 1; \
	fi
	@# write all sections at their correct offsets
	@dd if="$(BOOTLOADER_BIN)" of="$(COMBINED_OUT)" bs=1 seek=$$(( $(BOOTLOADER_OFFSET) )) conv=notrunc 2>/dev/null
	@dd if="$(BIN)" of="$(COMBINED_OUT)" bs=1 seek=$$(( $(FIRMWARE_OFFSET) )) conv=notrunc 2>/dev/null
	@dd if="$(FLASHCFG_PAD)" of="$(COMBINED_OUT)" bs=1 seek=$$(( $(FLASHCFG_OFFSET) )) conv=notrunc 2>/dev/null
	@dd if="$(FLASHCART_PAD)" of="$(COMBINED_OUT)" bs=1 seek=$$(( $(FLASHCART_OFFSET) )) conv=notrunc 2>/dev/null
	@echo "Complete flash image written to $(COMBINED_OUT)"
	@echo "Memory layout:"
	@echo "  BOOTLOADER @ $(BOOTLOADER_OFFSET) (size $$(stat -c%s "$(BOOTLOADER_BIN)") / $(BOOTLOADER_SIZE) bytes)"
	@echo "  FIRMWARE   @ $(FIRMWARE_OFFSET) (size $$(stat -c%s "$(BIN)") / $(FIRMWARE_MAX_SIZE) bytes)"
	@echo "  FLASHCFG   @ $(FLASHCFG_OFFSET) (size $(FLASHCFG_SIZE) bytes)"
	@echo "  FLASHCART  @ $(FLASHCART_OFFSET) (size $(FLASHCART_SIZE) bytes)"

# Flash the full combined image created by `make dd` to the device at 0x00000000
# Usage: make flash-image   # will flash $(DD_OUT)
#        make flash-image DD_OUT=/tmp/myimage.bin
flash-image: $(DD_OUT)
	@echo "Flashing $(DD_OUT) to device using $(OPENOCD) and config $(OPENOCD_TOOLCHAIN_BIN)/wch-riscv.cfg"
	"$(OPENOCD)" -f "$(OPENOCD_TOOLCHAIN_BIN)/wch-riscv.cfg" -c "init" -c "reset halt" -c "flash write_image erase $(DD_OUT) 0x00000000" -c "verify_image $(DD_OUT) 0x00000000" -c "reset run" -c "exit"
	@echo "Flashed $(DD_OUT)"

# Flash the complete combined image (with bootloader) created by `make combine`
# Usage: make flash-combined   # will flash $(COMBINED_OUT)
#        make flash-combined COMBINED_OUT=/tmp/myimage.bin
flash-combined: $(COMBINED_OUT)
	@echo "Flashing $(COMBINED_OUT) to device using $(OPENOCD) and config $(OPENOCD_TOOLCHAIN_BIN)/wch-riscv.cfg"
	"$(OPENOCD)" -f "$(OPENOCD_TOOLCHAIN_BIN)/wch-riscv.cfg" -c "init" -c "reset halt" -c "flash write_image erase $(COMBINED_OUT) 0x00000000" -c "verify_image $(COMBINED_OUT) 0x00000000" -c "reset run" -c "exit"
	@echo "Flashed $(COMBINED_OUT)"

# Flash complete combined image to MCU (alias for flash-combined for convenience)
# This target combines bootloader + firmware + config + cart into a single flash operation
flash-mpu: $(COMBINED_OUT)
	@echo "Flashing complete image to MCU using OpenOCD..."
	@echo "Image: $(COMBINED_OUT)"
	@echo "  - Bootloader at 0x00000000 ($(BOOTLOADER_SIZE) bytes)"
	@echo "  - Firmware at $(FIRMWARE_OFFSET) (max $(FIRMWARE_MAX_SIZE) bytes)"
	@echo "  - Config at $(FLASHCFG_OFFSET) ($(FLASHCFG_SIZE) bytes)"
	@echo "  - Cart at $(FLASHCART_OFFSET) ($(FLASHCART_SIZE) bytes)"
	"$(OPENOCD)" -f "$(OPENOCD_TOOLCHAIN_BIN)/wch-riscv.cfg" -c "init" -c "reset halt" -c "flash write_image erase $(COMBINED_OUT) 0x00000000" -c "verify_image $(COMBINED_OUT) 0x00000000" -c "reset run" -c "exit"
	@echo "Successfully flashed complete image to MCU!"

# Show what would be flashed without actually flashing (dry-run)
flash-mpu-info: $(COMBINED_OUT)
	@echo "=== Flash MPU Information ==="
	@echo "Combined image: $(COMBINED_OUT)"
	@echo "Total size: $$(stat -c%s "$(COMBINED_OUT)") bytes ($$(python3 -c "print($$(stat -c%s "$(COMBINED_OUT)")/1024)")K)"
	@echo ""
	@echo "Memory layout to be flashed:"
	@echo "  BOOTLOADER @ $(BOOTLOADER_OFFSET) (size $$(stat -c%s "$(BOOTLOADER_BIN)") / $(BOOTLOADER_SIZE) bytes)"
	@echo "  FIRMWARE   @ $(FIRMWARE_OFFSET) (size $$(stat -c%s "$(BIN)") / $(FIRMWARE_MAX_SIZE) bytes)"
	@echo "  FLASHCFG   @ $(FLASHCFG_OFFSET) (size $(FLASHCFG_SIZE) bytes)"
	@echo "  FLASHCART  @ $(FLASHCART_OFFSET) (size $(FLASHCART_SIZE) bytes)"
	@echo ""
	@echo "Flash command that would be executed:"
	@echo "$(OPENOCD) -f $(OPENOCD_TOOLCHAIN_BIN)/wch-riscv.cfg -c init -c 'reset halt' -c 'flash write_image erase $(COMBINED_OUT) 0x00000000' -c 'verify_image $(COMBINED_OUT) 0x00000000' -c 'reset run' -c exit"

# ========================
# Bootloader build targets
# ========================
BOOTLOADER_DIR     := bootloader
BOOTLOADER_BUILD   := build-bootloader
BOOTLOADER_PROJECT := Bootloader
BOOTLOADER_ELF     := $(BOOTLOADER_BUILD)/$(BOOTLOADER_PROJECT).elf
BOOTLOADER_BIN     := $(BOOTLOADER_BUILD)/$(BOOTLOADER_PROJECT).bin
BOOTLOADER_HEX     := $(BOOTLOADER_BUILD)/$(BOOTLOADER_PROJECT).hex
BOOTLOADER_MAP     := $(BOOTLOADER_BUILD)/$(BOOTLOADER_PROJECT).map
BOOTLOADER_LST     := $(BOOTLOADER_BUILD)/$(BOOTLOADER_PROJECT).lst

# Bootloader linker script
BOOTLOADER_LDSCRIPT := $(BOOTLOADER_DIR)/Ld/Link.ld

# Bootloader includes (similar structure to firmware)
BOOTLOADER_INCLUDES := \
  -I$(BOOTLOADER_DIR)/Debug \
  -I$(BOOTLOADER_DIR)/Core \
  -I$(BOOTLOADER_DIR)/User \
  -I$(BOOTLOADER_DIR)/Peripheral/inc \
  -I$(BOOTLOADER_DIR)/Startup

# Bootloader flags (same as firmware but different linker script)
BOOTLOADER_CFLAGS := $(ARCHFLAGS) -msmall-data-limit=8 -msave-restore \
  -fmax-errors=20 -Oz -flto -fno-unwind-tables -fno-asynchronous-unwind-tables \
  -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common \
  -std=gnu99 $(CWARN) $(CDEFS) $(BOOTLOADER_INCLUDES) -MMD -MP

BOOTLOADER_ASFLAGS := $(ARCHFLAGS) $(BOOTLOADER_INCLUDES)

BOOTLOADER_LDFLAGS := $(ARCHFLAGS) -T $(BOOTLOADER_LDSCRIPT) -nostartfiles -Wl,--gc-sections -flto \
  -Wl,-Map,$(BOOTLOADER_MAP) -Wl,--print-memory-usage --specs=nano.specs --specs=nosys.specs -Wl,-s

# Bootloader source discovery (exclude build directories and D8C startup file)
BOOTLOADER_FIND_EXCLUDES := \( -path $(BUILD) -o -path $(BOOTLOADER_BUILD) -o -path $(BOOTLOADER_DIR)/obj -o -name startup_ch32v30x_D8C.S \) -prune -o
BOOTLOADER_SRCS_C := $(shell find $(BOOTLOADER_DIR) $(BOOTLOADER_FIND_EXCLUDES) -name '*.c' -print)
BOOTLOADER_SRCS_S := $(shell find $(BOOTLOADER_DIR) $(BOOTLOADER_FIND_EXCLUDES) \( -name '*.S' -o -name '*.s' \) -print)

# Bootloader object paths
BOOTLOADER_OBJS_C := $(patsubst $(BOOTLOADER_DIR)/%.c,$(BOOTLOADER_BUILD)/obj/%.o,$(BOOTLOADER_SRCS_C))
BOOTLOADER_OBJS_S := $(patsubst $(BOOTLOADER_DIR)/%.S,$(BOOTLOADER_BUILD)/obj/%.o,$(BOOTLOADER_SRCS_S))
BOOTLOADER_OBJS_S := $(patsubst $(BOOTLOADER_DIR)/%.s,$(BOOTLOADER_BUILD)/obj/%.o,$(BOOTLOADER_OBJS_S))
BOOTLOADER_OBJS   := $(BOOTLOADER_OBJS_C) $(BOOTLOADER_OBJS_S)
BOOTLOADER_DEPS   := $(BOOTLOADER_OBJS:.o=.d)

# Bootloader directories (avoid duplicate $(OUT_DIR) target)
BOOTLOADER_DIRS := $(sort $(dir $(BOOTLOADER_OBJS)) $(BOOTLOADER_BUILD))
$(BOOTLOADER_DIRS):
	@mkdir -p "$@"

# Bootloader compile rules
$(BOOTLOADER_BUILD)/obj/%.o: $(BOOTLOADER_DIR)/%.c | $(BOOTLOADER_DIRS)
	$(CC) $(BOOTLOADER_CFLAGS) -c "$<" -o "$@"

$(BOOTLOADER_BUILD)/obj/%.o: $(BOOTLOADER_DIR)/%.S | $(BOOTLOADER_DIRS)
	$(AS) $(BOOTLOADER_ASFLAGS) -c "$<" -o "$@"

$(BOOTLOADER_BUILD)/obj/%.o: $(BOOTLOADER_DIR)/%.s | $(BOOTLOADER_DIRS)
	$(AS) $(BOOTLOADER_ASFLAGS) -c "$<" -o "$@"

# Bootloader link
$(BOOTLOADER_ELF): $(BOOTLOADER_OBJS)
	$(LD) $(BOOTLOADER_LDFLAGS) -o "$@" $(BOOTLOADER_OBJS)
	$(SIZE) --format=berkeley "$@"
	$(OBJDUMP) --all-headers --demangle --disassemble -M xw "$@" > "$(BOOTLOADER_LST)"

# Bootloader convert
$(BOOTLOADER_BIN): $(BOOTLOADER_ELF)
	$(OBJCOPY) -O binary "$<" "$@"

$(BOOTLOADER_HEX): $(BOOTLOADER_ELF)
	$(OBJCOPY) -O ihex "$<" "$@"

# Bootloader targets
bootloader: $(BOOTLOADER_ELF) $(BOOTLOADER_BIN) $(BOOTLOADER_HEX) copy-bootloader-bin

bootloader-size: $(BOOTLOADER_ELF)
	$(SIZE) --format=berkeley "$(BOOTLOADER_ELF)"

copy-bootloader-bin: $(BOOTLOADER_BIN) | $(OUT_DIR)
	cp -f "$(BOOTLOADER_BIN)" "$(ROOT)/$(BOOTLOADER_PROJECT).bin"
	cp -f "$(BOOTLOADER_ELF)" "$(OUT_DIR)/$(BOOTLOADER_PROJECT).elf"
	cp -f "$(BOOTLOADER_BIN)" "$(OUT_DIR)/$(BOOTLOADER_PROJECT).bin"
	cp -f "$(BOOTLOADER_HEX)" "$(OUT_DIR)/$(BOOTLOADER_PROJECT).hex"
	cp -f "$(BOOTLOADER_LST)" "$(OUT_DIR)/$(BOOTLOADER_PROJECT).lst"

# Flash bootloader
flash-bootloader: $(BOOTLOADER_BIN)
	"$(OPENOCD)" -f "$(OPENOCD_TOOLCHAIN_BIN)/wch-riscv.cfg" -c "init" -c "reset halt" -c "flash write_image erase $(BOOTLOADER_BIN) 0x00000000" -c "verify_image $(BOOTLOADER_BIN) 0x00000000" -c "reset run" -c "exit"

clean-bootloader:
	rm -rf "$(BOOTLOADER_BUILD)"

.PHONY: all clean size copy-bin help print-config assets flash erase dump dd flash-image combine flash-combined flash-mpu flash-mpu-info bootloader bootloader-size copy-bootloader-bin flash-bootloader clean-bootloader firmware-update

# Build everything: bootloader, firmware, and combined image
all: bootloader $(ELF) $(BIN) $(HEX) copy-bin combine
	@echo ""
	@echo "=== Build Complete ==="
	@echo "Built bootloader, firmware, and combined image"
	@echo "  Bootloader: $(BOOTLOADER_BIN)"
	@echo "  Firmware:   $(BIN)" 
	@echo "  Combined:   $(COMBINED_OUT)"
	@echo ""
	@echo "Ready to flash with: make flash-mpu"

# Compile rules
$(BUILD)/obj/%.o: %.c | $(DIRS)
	$(CC) $(CFLAGS) -c "$<" -o "$@"

$(BUILD)/obj/%.o: %.S | $(DIRS)
	$(AS) $(ASFLAGS) -c "$<" -o "$@"

$(BUILD)/obj/%.o: %.s | $(DIRS)
	$(AS) $(ASFLAGS) -c "$<" -o "$@"

# Link
$(ELF): $(OBJS)
	$(LD) $(LDFLAGS) -o "$@" $(OBJS)
	$(SIZE) --format=berkeley "$@"
	$(OBJDUMP) --all-headers --demangle --disassemble -M xw "$@" > "$(LST)"

# Convert
$(BIN): $(ELF)
	$(OBJCOPY) -O binary "$<" "$@"

$(HEX): $(ELF)
	$(OBJCOPY) -O ihex "$<" "$@"

size: $(ELF)
	$(SIZE) --format=berkeley "$(ELF)"

clean:
	rm -rf "$(BUILD)"

# Copy the generated BIN to repo root and out directory for convenience
copy-bin: $(BIN)
	cp -f "$(BIN)" "$(ROOT)/$(PROJECT).bin"
	cp -f "$(ELF)" "$(OUT_DIR)/$(PROJECT).elf"
	cp -f "$(BIN)" "$(OUT_DIR)/$(PROJECT).bin"
	cp -f "$(HEX)" "$(OUT_DIR)/$(PROJECT).hex"
	cp -f "$(LST)" "$(OUT_DIR)/$(PROJECT).lst"

# Create firmware update package - pad firmware to exactly 32KB with 0xFF and save as riskymsx.upd
FIRMWARE_UPDATE_SIZE ?= 32768    # 32KB exactly
FIRMWARE_UPDATE_FILE := $(OUT_DIR)/riskymsx.upd

firmware-update: $(BIN) | $(OUT_DIR)
	@echo "Creating firmware update package -> $(FIRMWARE_UPDATE_FILE)"
	@# Check firmware size doesn't exceed 32KB
	@bsz=$$(stat -c%s "$(BIN)"); maxsz=$(FIRMWARE_UPDATE_SIZE); \
	if [ $$bsz -gt $$maxsz ]; then \
		echo "ERROR: firmware $(BIN) size ($$bsz) exceeds firmware update package size ($$maxsz)."; \
		exit 1; \
	fi
	@# Create 32KB file filled with 0xFF
	@dd if=/dev/zero bs=1 count=$(FIRMWARE_UPDATE_SIZE) 2>/dev/null | tr '\000' '\377' > "$(FIRMWARE_UPDATE_FILE)"
	@# Copy firmware to the beginning of the update file
	@dd if="$(BIN)" of="$(FIRMWARE_UPDATE_FILE)" conv=notrunc bs=1 count=$$(stat -c%s "$(BIN)") 2>/dev/null
	@echo "Firmware update package created: $(FIRMWARE_UPDATE_FILE)"
	@echo "  Original firmware size: $$(stat -c%s "$(BIN)") bytes"
	@echo "  Update package size:    $(FIRMWARE_UPDATE_SIZE) bytes (32KB)"
	@echo "  Padding:               $$(( $(FIRMWARE_UPDATE_SIZE) - $$(stat -c%s "$(BIN)") )) bytes (0xFF)"

# Flash and erase using OpenOCD (WCH RISC-V)
# You can override OPENOCD or OPENOCD_CFG on the command line if needed
# Prefer the packaged OpenOCD in the repo if present
OPENOCD_TOOLCHAIN_BIN := $(ROOT)/MRS_Toolchain_Linux_x64_V210/OpenOCD/OpenOCD/bin
OPENOCD    ?= openocd
ifneq (,$(wildcard $(OPENOCD_TOOLCHAIN_BIN)/openocd))
OPENOCD     := $(OPENOCD_TOOLCHAIN_BIN)/openocd
endif
OPENOCD_CFG ?= $(ROOT)/wch-riscv.cfg

# Erase entire flash (bank 0)
erase:
	"$(OPENOCD)" -f "$(OPENOCD_TOOLCHAIN_BIN)/wch-riscv.cfg" -c "init" -c "reset halt" -c "flash erase_sector 0 0 last" -c "exit"

# Program firmware binary at 0x00000000 and verify
flash: $(BIN)
	"$(OPENOCD)" -f "$(OPENOCD_TOOLCHAIN_BIN)/wch-riscv.cfg" -c "init" -c "reset halt" -c "flash write_image erase $(BIN) 0x00000000" -c "verify_image $(BIN) 0x00000000" -c "reset run" -c "exit"

# Dump entire flash (bank 0) to a file in $(OUT_DIR)
# Usage: make dump            # writes to default DUMP_FILE
#        make dump DUMP_FILE=out/mydump.bin
DUMP_FILE ?= $(OUT_DIR)/chip_dump-$(shell date +%Y%m%d-%H%M%S).bin
dump:
	@mkdir -p "$(OUT_DIR)"
	@echo "Reading flash bank 0 to $(DUMP_FILE) using $(OPENOCD) and config $(OPENOCD_TOOLCHAIN_BIN)/wch-riscv.cfg"
	"$(OPENOCD)" -f "$(OPENOCD_TOOLCHAIN_BIN)/wch-riscv.cfg" -c "init" -c "reset halt" -c "flash read_bank 0 $(DUMP_FILE)" -c "exit"
	@echo "Dump saved to $(DUMP_FILE)"

print-config:
	@echo "CROSS=$(CROSS)"
	@echo "TOOLCHAIN_DIR=$(TOOLCHAIN_DIR)"
	@echo "BUILD=$(BUILD)"
	@echo "Sources: C=$(words $(SRCS_C)) S=$(words $(SRCS_S))"

help:
	@echo "RISKYMSX universal Makefile"
	@echo "  Build outputs in $(BUILD) and copied to $(OUT_DIR)"
	@echo "  Toolchain prefix: $(CROSS) (override with CROSS=...)"
	@echo
	@echo "Main targets:"
	@echo "  make            - build bootloader + firmware + combined image (complete build)"
	@echo "  make flash-mpu  - flash complete combined image to MCU"
	@echo
	@echo "Firmware targets:"
	@echo "  make size       - print firmware section sizes"
	@echo "  make clean      - remove firmware build directory"
	@echo "  make flash      - flash firmware only to device"
	@echo "  make dd         - create full flash image with firmware+config+cart"
	@echo "  make flash-image - flash full combined image"
	@echo "  make firmware-update - create 32KB firmware update package (riskymsx.upd)"
	@echo
	@echo "Bootloader targets:"
	@echo "  make bootloader      - build bootloader ELF/BIN/HEX and copy artifacts"
	@echo "  make bootloader-size - print bootloader section sizes"
	@echo "  make clean-bootloader - remove bootloader build directory"
	@echo "  make flash-bootloader - flash bootloader to device"
	@echo
	@echo "Combined image targets:"
	@echo "  make combine       - create complete flash image with bootloader+firmware+config+cart"
	@echo "  make flash-combined - flash complete combined image to device"
	@echo "  make flash-mpu     - flash complete combined image to MCU (alias with detailed info)"
	@echo "  make flash-mpu-info - show what would be flashed without actually flashing (dry-run)"
	@echo
	@echo "Other targets:"
	@echo "  v     - erase entire flash"
	@echo "  make dump       - dump flash contents to file"
	@echo "  make assets     - build cart image assets"
	@echo "  make print-config - show configuration"

# Dependency includes
-include $(DEPS)
-include $(BOOTLOADER_DEPS)
