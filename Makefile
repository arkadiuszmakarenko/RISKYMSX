# Top-level Makefile for RISKYMSX firmware
# Builds the firmware using the existing MRS-generated makefiles under firmware/obj
# and provides flashing via the packaged OpenOCD.

# Paths
ROOT             := $(CURDIR)
# Location of the MRS-generated build system (auto-detected; override with SUB_BUILD_DIR)
ifeq ($(wildcard $(ROOT)/firmware/build/makefile),)
  SUB_BUILD_DIR_DEFAULT := $(ROOT)/firmware/build
else
  SUB_BUILD_DIR_DEFAULT := $(ROOT)/firmware/build
endif
SUB_BUILD_DIR   ?= $(SUB_BUILD_DIR_DEFAULT)
# Where we expose/copy final artifacts for users (top-level build output)
OUT_DIR         ?= $(ROOT)/out# Artifacts as exposed by this top-level Makefile
ELF              := $(OUT_DIR)/RISKYMSXCART.elf
BIN              := $(OUT_DIR)/RISKYMSXCART.bin
HEX              := $(OUT_DIR)/RISKYMSXCART.hex
LST              := $(OUT_DIR)/RISKYMSXCART.lst

# Artifacts produced by the MRS sub-build
OBJ_ELF          := $(SUB_BUILD_DIR)/RISKYMSXCART.elf
OBJ_BIN          := $(SUB_BUILD_DIR)/RISKYMSXCART.bin
OBJ_HEX          := $(SUB_BUILD_DIR)/RISKYMSXCART.hex
OBJ_LST          := $(SUB_BUILD_DIR)/RISKYMSXCART.lst

# Toolchain (RISC-V Embedded GCC12)
TOOLCHAIN_DIR   ?= $(ROOT)/MRS_Toolchain_Linux_x64_V210/RISC-V Embedded GCC12
GCC_BIN          := $(TOOLCHAIN_DIR)/bin

# OpenOCD (kept for openocd-server and gdb targets)
OPENOCD_ROOT    ?= $(ROOT)/MRS_Toolchain_Linux_x64_V210/OpenOCD/OpenOCD
OPENOCD          ?= $(OPENOCD_ROOT)/bin/openocd
OPENOCD_SCRIPTS  ?= $(OPENOCD_ROOT)/share/openocd/scripts
# Default config for WCH-Link with CH32V30x RISC-V
# Uses custom wch-riscv.cfg in repo root
# Override on command line if needed: make openocd-server OPENOCD_CFG="..."
OPENOCD_CFG     ?= -f $(ROOT)/wch-riscv.cfg

# Ensure the toolchain is used by the sub-make (PATH includes spaces; this is okay)
export PATH := $(GCC_BIN):$(PATH)

.DEFAULT_GOAL := all

.PHONY: all elf bin hex lst size clean flash erase openocd-server gdb help copy-bin list-usb

all: elf copy-bin

# Build via the existing makefiles then copy artifacts into $(BUILD_DIR)
elf: $(ELF)

# Ensure sub-build happens and artifacts are copied into OUT_DIR
$(ELF): $(OBJ_ELF)
	@mkdir -p "$(OUT_DIR)"
	cp -f "$(OBJ_ELF)" "$(ELF)"
	@if [ -f "$(OBJ_BIN)" ]; then cp -f "$(OBJ_BIN)" "$(BIN)"; fi
	@if [ -f "$(OBJ_HEX)" ]; then cp -f "$(OBJ_HEX)" "$(HEX)"; fi
	@if [ -f "$(OBJ_LST)" ]; then cp -f "$(OBJ_LST)" "$(LST)"; fi

# Trigger the MRS sub-build which creates the object artifacts
$(OBJ_ELF):
	@if [ ! -d "$(SUB_BUILD_DIR)" ]; then \
		echo "ERROR: Sub-build directory '$(SUB_BUILD_DIR)' not found."; \
		echo "Set SUB_BUILD_DIR to the MRS build folder (where makefile resides), e.g.:"; \
		echo "  make SUB_BUILD_DIR=firmware/build"; \
		exit 2; \
	fi
	$(MAKE) -C "$(SUB_BUILD_DIR)" all

bin: $(BIN)
$(BIN): $(ELF)
	@:

hex: $(HEX)
$(HEX): $(ELF)
	@:

lst: $(LST)
$(LST): $(ELF)
	@:

size: $(ELF)
	@if [ -d "$(SUB_BUILD_DIR)" ]; then $(MAKE) -C "$(SUB_BUILD_DIR)" RISKYMSXCART.siz; else echo "Sub-build dir '$(SUB_BUILD_DIR)' not found"; fi


clean:
	@if [ -d "$(SUB_BUILD_DIR)" ]; then $(MAKE) -C "$(SUB_BUILD_DIR)" clean; fi
	@rm -f "$(ELF)" "$(BIN)" "$(HEX)" "$(LST)"

# Copy the generated BIN (from $(OUT_DIR)) to the repo root for convenience
copy-bin: $(BIN)
	cp -f "$(BIN)" "$(ROOT)/RISKYMSXCART.bin"

# Flash using minichlink (simple, reliable tool from ch32fun project)
flash: $(ELF)
	@echo "Flashing $(ELF) via WCH-Link using minichlink..."
	@bash "$(ROOT)/flash.sh" "$(ELF)"

# Erase entire chip using minichlink
erase:
	@echo "Erasing CH32V30x chip via WCH-Link using minichlink..."
	@echo "Note: Fault messages may appear but erase usually succeeds"
	@minichlink -E

# Start OpenOCD server (for GDB). Uses default config; override OPENOCD_CFG if needed.
openocd-server:
	"$(OPENOCD)" -s "$(OPENOCD_SCRIPTS)" $(OPENOCD_CFG)

# Quick GDB connect (expects OpenOCD already running on :3333)
gdb: $(ELF)
	riscv-wch-elf-gdb -q $(ELF) -ex "target extended-remote :3333"

# List USB devices to help identify programmer
list-usb:
	@echo "Connected USB devices:"
	@lsusb 2>/dev/null || echo "lsusb not available"
	@echo
	@echo "Looking for WCH devices (vendor ID 1a86):"
	@lsusb -d 1a86: 2>/dev/null || echo "No WCH devices found"

help:
	@echo "RISKYMSX firmware top-level Makefile"
	@echo
	@echo "Toolchain:"
	@echo "  TOOLCHAIN_DIR   = $(TOOLCHAIN_DIR)"
	@echo "  (PATH augmented with $$TOOLCHAIN_DIR/bin)"
	@echo
	@echo "OpenOCD:"
	@echo "  OPENOCD_ROOT    = $(OPENOCD_ROOT)"
	@echo "  OPENOCD         = $(OPENOCD)"
	@echo "  OPENOCD_SCRIPTS = $(OPENOCD_SCRIPTS)"
	@echo "  OPENOCD_CFG     = $(OPENOCD_CFG)"
	@echo
	@echo "Build directories:"
	@echo "  SUB_BUILD_DIR   = $(SUB_BUILD_DIR) (auto-detected; override if needed)"
	@echo "  OUT_DIR         = $(OUT_DIR) (artifacts copied here)"
	@echo
	@echo "Common targets:"
	@echo "  all (default)  - build firmware via sub-make (creates ELF, BIN, HEX, LST)"
	@echo "                   copies artifacts into $(OUT_DIR) and BIN to repo root"
	@echo "  clean          - clean firmware build"
	@echo "  size           - print section sizes"
	@echo "  flash          - program via minichlink (reliable WCH-Link tool)"
	@echo "  erase          - erase entire chip via minichlink"
	@echo "  openocd-server - start OpenOCD server (for GDB)"
	@echo "  gdb            - start GDB and connect to :3333"
	@echo "  list-usb       - show connected USB devices (helps identify programmer)"
	@echo
	@echo "Flash configuration:"
	@echo "  Uses minichlink from ch32fun project - simple and reliable for WCH-Link"
	@echo "  No configuration needed, works out-of-the-box with WCH-Link"
	@echo "  Use 'make erase' to clear entire chip before flashing if needed"
	@echo
	@echo "Note: CH32V30x is a RISC-V chip. Uses minichlink for reliable flashing."
	@echo
	@echo "Troubleshooting flash errors:"
	@echo "  'WCH-Link not found' - Ensure WCH-Link is connected and powered"
	@echo "  'Permission denied' - May need udev rules for USB access"
	@echo "  'Chip not detected' - Try power-cycling the target board"
	@echo "  'Fault on op' during erase - Normal, erase usually succeeds anyway"
	@echo "  Alternative: Use WCH-LinkUtility GUI tool for flashing"
	@echo
	@echo "Examples:"
	@echo "  make                    # build firmware"
	@echo "  make erase              # erase entire chip"
	@echo "  make flash              # program with default WCH-Link config"
	@echo "  make openocd-server     # start debug server"
