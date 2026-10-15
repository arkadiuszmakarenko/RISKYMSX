# Makefile for RISKYMSXCART firmware (CH32V303VCT6, RISC-V)
#
# Replaces the MounRiver Studio IDE build with a plain make-based workflow.
# Targets:
#   make            - build ELF/HEX/BIN
#   make flash      - build and program the chip via minichlink (WCH-Link)
#   make flash-openocd - build and program via OpenOCD using wch-riscv.cfg
#   make size       - show program/data size
#   make clean      - remove build artifacts
#
# Configuration:
#   TOOLCHAIN_DIR  - directory containing the RISC-V toolchain (default: MounRiver)
#   PREFIX         - toolchain prefix (default: riscv-wch-elf-)
#   FLASH_ADDR     - flash base address (default: 0x00000000)

# ---- Toolchain ---------------------------------------------------------------
# Auto-detect RISC-V toolchain in this order:
#   1. Bundled MounRiver toolchain (../MRS_Toolchain_Linux_x64_V210/...)
#   2. riscv-wch-elf-*    (used by MounRiver Studio)
#   3. riscv-none-embed-* (xPack)
#   4. riscv64-unknown-elf-* (system package)
# Override with: make TOOLCHAIN_DIR=/opt/x PREFIX=riscv-none-embed-
#
# Spaces in paths are handled via shell substitution to avoid make word-splitting.

ifndef TOOLCHAIN_DIR
    MRS_TOOLCHAIN_BIN := $(shell \
        path="$$(cd ../MRS_Toolchain_Linux_x64_V210 2>/dev/null && \
                 pwd)/RISC-V Embedded GCC12/bin"; \
        if [ -x "$$path/riscv-wch-elf-gcc" ]; then echo "$$path"; fi)
    ifneq ($(MRS_TOOLCHAIN_BIN),)
        TOOLCHAIN_DIR := $(MRS_TOOLCHAIN_BIN)
        PREFIX        := riscv-wch-elf-
    else ifneq ($(shell which riscv-wch-elf-gcc 2>/dev/null),)
        TOOLCHAIN_DIR := $(dir $(shell which riscv-wch-elf-gcc))
        PREFIX        := riscv-wch-elf-
    else ifneq ($(shell which riscv-none-embed-gcc 2>/dev/null),)
        TOOLCHAIN_DIR := $(dir $(shell which riscv-none-embed-gcc))
        PREFIX        := riscv-none-embed-
    else ifneq ($(shell which riscv64-unknown-elf-gcc 2>/dev/null),)
        TOOLCHAIN_DIR := $(dir $(shell which riscv64-unknown-elf-gcc))
        PREFIX        := riscv64-unknown-elf-
    else
        $(error No RISC-V toolchain found. Install one or set TOOLCHAIN_DIR/PREFIX.)
    endif
endif

ifndef PREFIX
    PREFIX := riscv-wch-elf-
endif

CC      := $(TOOLCHAIN_DIR)/$(PREFIX)gcc
OBJCOPY := $(TOOLCHAIN_DIR)/$(PREFIX)objcopy
OBJDUMP := $(TOOLCHAIN_DIR)/$(PREFIX)objdump
SIZE    := $(TOOLCHAIN_DIR)/$(PREFIX)size

# ---- Project configuration ---------------------------------------------------
TARGET   := RISKYMSXCART
BUILD    := build
SRC_DIR  := .
LINKER   := $(SRC_DIR)/Ld/Link.ld
FLASH_ADDR := 0x00000000

# CH32V303VCT6: rv32imacxw (I,M,A,C,XW) with ilp32 ABI - matches MounRiver
ARCH_FLAGS  := -march=rv32imacxw -mabi=ilp32 -msmall-data-limit=8 -msave-restore

# ---- Sources -----------------------------------------------------------------
# Source globs (path -> list of files). Add new directories here.
C_DIRS  := Core Debug Peripheral/src User User/FATFS User/USB_Host
ASM_DIRS := Startup

C_SRCS   := $(foreach d,$(C_DIRS),$(wildcard $(SRC_DIR)/$d/*.c))
ASM_SRCS := $(foreach d,$(ASM_DIRS),$(wildcard $(SRC_DIR)/$d/*.S))

C_OBJS   := $(patsubst $(SRC_DIR)/%.c,$(BUILD)/%.o,$(C_SRCS))
ASM_OBJS := $(patsubst $(SRC_DIR)/%.S,$(BUILD)/%.o,$(ASM_SRCS))
OBJS     := $(C_OBJS) $(ASM_OBJS)
DEPS     := $(OBJS:.o=.d)

# ---- Include paths -----------------------------------------------------------
INCLUDES := \
    -I$(SRC_DIR)/Debug \
    -I$(SRC_DIR)/Core \
    -I$(SRC_DIR)/User \
    -I$(SRC_DIR)/Peripheral/inc \
    -I$(SRC_DIR)/User/USB_Host \
    -I$(SRC_DIR)/User/FATFS \
    -I$(SRC_DIR)/Startup

# ---- Defines -----------------------------------------------------------------
# Mirrors the MounRiver build. Add -D flags here (e.g. -DUSE_FATFS_READONLY)
DEFINES :=

# ---- Compiler / linker flags -------------------------------------------------
COMMON_FLAGS := $(ARCH_FLAGS) -Os -fmessage-length=0 -fsigned-char \
                -ffunction-sections -fdata-sections -fno-common \
                -fmax-errors=20 -MMD -MP

CFLAGS   := $(COMMON_FLAGS) $(INCLUDES) $(DEFINES) -std=gnu99
ASFLAGS  := $(COMMON_FLAGS) $(INCLUDES) -x assembler-with-cpp

LDFLAGS  := $(ARCH_FLAGS) -T "$(LINKER)" -nostartfiles \
            -Xlinker --gc-sections -Xlinker --print-memory-usage \
            -Wl,-Map,"$(BUILD)/$(TARGET).map" \
            --specs=nano.specs --specs=nosys.specs

# ---- Build rules -------------------------------------------------------------
.PHONY: all flash flash-openocd size clean

all: $(BUILD)/$(TARGET).elf $(BUILD)/$(TARGET).hex $(BUILD)/$(TARGET).bin

$(BUILD)/$(TARGET).elf: $(OBJS) | $(BUILD)
	@echo "  LD      $@"
	@"$(CC)" $(LDFLAGS) -o "$@" $(OBJS)

$(BUILD)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(@D)
	@echo "  CC      $<"
	@"$(CC)" $(CFLAGS) -c -o "$@" "$<"

$(BUILD)/%.o: $(SRC_DIR)/%.S
	@mkdir -p $(@D)
	@echo "  AS      $<"
	@"$(CC)" $(ASFLAGS) -c -o "$@" "$<"

$(BUILD)/%.hex: $(BUILD)/%.elf
	@echo "  OBJCOPY $@"
	@"$(OBJCOPY)" -O ihex "$<" "$@"

$(BUILD)/%.bin: $(BUILD)/%.elf
	@echo "  OBJCOPY $@"
	@"$(OBJCOPY)" -O binary "$<" "$@"

# Build directory creation (supports nested subdirs like User/FATFS)
$(BUILD):
	@mkdir -p $@

# Per-source directory order-only prerequisite
OBJ_DIRS := $(sort $(dir $(OBJS)))
$(OBJ_DIRS):
	@mkdir -p $@

# ---- Flash targets -----------------------------------------------------------
MINICHLINK ?= minichlink
OPENOCD   ?= openocd
OPENOCD_IFACE := wlink
OPENOCD_CFG  := $(abspath $(CURDIR)/../wch-riscv.cfg)

flash: $(BUILD)/$(TARGET).bin
	@echo ">>> Flashing $(BUILD)/$(TARGET).bin to $(FLASH_ADDR) via minichlink"
	@$(MINICHLINK) -w $< $(FLASH_ADDR)

flash-openocd: $(BUILD)/$(TARGET).elf
	@echo ">>> Flashing $(BUILD)/$(TARGET).elf via OpenOCD (WCH-Link)"
	@cd $(abspath $(CURDIR)/..) && \
	    $(OPENOCD) -f $(OPENOCD_CFG) \
	               -c "program $(abspath $<) $(FLASH_ADDR) verify reset exit"

# ---- Utilities ---------------------------------------------------------------
size: $(BUILD)/$(TARGET).elf
	@"$(SIZE)" --format=berkeley "$<"

clean:
	@echo "  CLEAN"
	@rm -rf $(BUILD)

# ---- Dependencies ------------------------------------------------------------
-include $(DEPS)
