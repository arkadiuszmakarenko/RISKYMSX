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
# FLASHCFG at end of internal FLASH (0x00007F00, 256 B)
# FLASHCART starts at 0x00008000 (256 KiB window)
FLASHCFG_OFFSET ?= 0x00007F00
FLASHCART_OFFSET?= 0x00008000

FLASHCFG_SIZE   ?= 256
FLASHCART_SIZE  ?= 262144


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
#  - firmware BIN @ 0x00000000 (must fit before FLASHCFG_OFFSET)
#  - FLASHCFG @ $(FLASHCFG_OFFSET) (size $(FLASHCFG_SIZE))
#  - FLASHCART @ $(FLASHCART_OFFSET) (size $(FLASHCART_SIZE))
DD_OUT ?= $(OUT_DIR)/full_flash_image.bin
dd: $(BIN) $(FLASHCFG_PAD) $(FLASHCART_PAD) | $(OUT_DIR)
	@echo "Building full flash image -> $(DD_OUT)"
	@# create base image filled with 0xFF
	@dd if=/dev/zero bs=1 count=$$(( $(FLASHCART_OFFSET) + $(FLASHCART_SIZE) )) 2>/dev/null | tr '\000' '\377' > "$(DD_OUT)"
	@# ensure firmware fits into the reserved FLASH region
	@bsz=$$(stat -c%s "$(BIN)"); maxsz=$$(( $(FLASHCFG_OFFSET) )); \
	if [ $$bsz -gt $$maxsz ]; then \
		echo "ERROR: firmware $(BIN) size ($$bsz) exceeds reserved FLASH before FLASHCFG ($(FLASHCFG_OFFSET))."; \
		exit 1; \
	fi
	@dd if="$(BIN)" of="$(DD_OUT)" conv=notrunc bs=1 2>/dev/null
	@dd if="$(FLASHCFG_PAD)" of="$(DD_OUT)" bs=1 seek=$$(( $(FLASHCFG_OFFSET) )) conv=notrunc 2>/dev/null
	@dd if="$(FLASHCART_PAD)" of="$(DD_OUT)" bs=1 seek=$$(( $(FLASHCART_OFFSET) )) conv=notrunc 2>/dev/null
	@echo "Full flash image written to $(DD_OUT)"

# Flash the full combined image created by `make dd` to the device at 0x00000000
# Usage: make flash-image   # will flash $(DD_OUT)
#        make flash-image DD_OUT=/tmp/myimage.bin
flash-image: $(DD_OUT)
	@echo "Flashing $(DD_OUT) to device using $(OPENOCD) and config $(OPENOCD_TOOLCHAIN_BIN)/wch-riscv.cfg"
	"$(OPENOCD)" -f "$(OPENOCD_TOOLCHAIN_BIN)/wch-riscv.cfg" -c "init" -c "reset halt" -c "flash write_image erase $(DD_OUT) 0x00000000" -c "verify_image $(DD_OUT) 0x00000000" -c "reset run" -c "exit"
	@echo "Flashed $(DD_OUT)"

.PHONY: all clean size copy-bin help print-config assets flash erase dump dd flash-image

all: $(ELF) $(BIN) $(HEX) copy-bin

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
	@echo "Common targets:"
	@echo "  make            - build ELF/BIN/HEX and copy artifacts"
	@echo "  make size       - print section sizes"
	@echo "  make clean      - remove build directory"
	@echo "  make list-usb   - list USB devices (helps identify programmer)"
	@echo "  make print-config - show configuration"

# Dependency includes
-include $(DEPS)
