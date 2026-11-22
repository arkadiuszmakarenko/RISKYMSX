TARGET := RISKYMSXBootloader
BUILD := build
ROOT := ..

ifndef TOOLCHAIN_DIR
TOOLCHAIN_DIR := $(abspath $(ROOT)/MRS_Toolchain_Linux_x64_V210)/RISC-V Embedded GCC12/bin
endif
PREFIX ?= riscv-wch-elf-
CC := $(TOOLCHAIN_DIR)/$(PREFIX)gcc
OBJCOPY := $(TOOLCHAIN_DIR)/$(PREFIX)objcopy
SIZE := $(TOOLCHAIN_DIR)/$(PREFIX)size
LOCAL_MINICHLINK := $(CURDIR)/build/tools/minichlink
# The programmer is kept outside the build directory so that "make clean" does
# not remove it and force a rebuild on the next use.
PROGRAMMER_DIR := $(CURDIR)/tools
LOCAL_MINICHLINK := $(PROGRAMMER_DIR)/minichlink
MINICHLINK ?= $(if $(wildcard $(LOCAL_MINICHLINK)),$(LOCAL_MINICHLINK),minichlink)
CH32FUN_DIR ?= $(abspath $(ROOT)/../ch32fun)
BACKUP_DIR ?= $(CURDIR)/backups
PORT ?= /dev/ttyACM0
BAUD ?= 115200

ARCH := -march=rv32imacxw -mabi=ilp32 -msmall-data-limit=8 -msave-restore
INCLUDES := -ICore -IDebug -IUser -IPeripheral/inc -IUser/FATFS -IUser/USB_Host
# 0 = standard half word programming, 1 = fast page programming. Which path
# works while executing from the system boot flash is not documented by WCH,
# so the standard path is the default.
FAST_PROGRAMMING ?= 0
DEBUG ?= 0
ifneq ($(filter $(DEBUG),0 1),$(DEBUG))
$(error DEBUG must be 0 or 1)
endif
DEFINES := -DAPPFLASH_USE_FAST_PROGRAMMING=$(FAST_PROGRAMMING) -DBOOTLOADER_DEBUG=$(DEBUG)
CFLAGS := $(ARCH) -Os -ffunction-sections -fdata-sections -fno-common -MMD -MP $(INCLUDES) $(DEFINES)
LDFLAGS := $(ARCH) -Os -nostartfiles -T Ld/Link.ld \
	-Wl,--gc-sections,-Map,$(BUILD)/$(TARGET).map \
	--specs=nano.specs --specs=nosys.specs
SOURCES := Core/core_riscv.c Debug/debug.c User/main.c User/msxbus.c User/termui.c \
	User/appflash.c User/system_ch32v30x.c \
	User/FATFS/ff.c User/FATFS/ffunicode.c User/FATFS/diskio.c User/FATFS/usb_disk.c \
	User/USB_Host/ch32v30x_usbfs_host.c \
	Peripheral/src/ch32v30x_gpio.c Peripheral/src/ch32v30x_rcc.c \
	Peripheral/src/ch32v30x_usart.c Peripheral/src/ch32v30x_misc.c \
	Peripheral/src/ch32v30x_flash.c Peripheral/src/ch32v30x_exti.c \
	Peripheral/src/ch32v30x_dbgmcu.c
OBJECTS := $(SOURCES:%.c=$(BUILD)/%.o) $(BUILD)/Startup/startup_ch32v30x_D8.o

.PHONY: all size setup-programmer backup flash restore monitor clean FORCE
all: $(BUILD)/$(TARGET).elf $(BUILD)/$(TARGET).bin

$(BUILD)/build-config: FORCE
	@mkdir -p "$(@D)"
	@printf '%s\n' '$(DEFINES)' > "$@.tmp"
	@cmp -s "$@.tmp" "$@" && rm -f "$@.tmp" || mv -f "$@.tmp" "$@"

$(BUILD)/%.o: %.c $(BUILD)/build-config
	@mkdir -p "$(@D)"
	"$(CC)" $(CFLAGS) -std=gnu99 -c "$<" -o "$@"

$(BUILD)/%.o: %.S $(BUILD)/build-config
	@mkdir -p "$(@D)"
	"$(CC)" $(CFLAGS) -x assembler-with-cpp -c "$<" -o "$@"

$(BUILD)/$(TARGET).elf: $(OBJECTS) Ld/Link.ld
	"$(CC)" $(LDFLAGS) -o "$@" $(OBJECTS)

$(BUILD)/$(TARGET).bin: $(BUILD)/$(TARGET).elf
	"$(OBJCOPY)" -O binary --gap-fill 0xff --pad-to 0x7000 "$<" "$@"
	@test "$$(wc -c < "$@")" -eq 28672 || { \
		echo "Error: image exceeds the 28 KiB boot region" >&2; rm -f "$@"; exit 1; }

size: $(BUILD)/$(TARGET).elf
	"$(SIZE)" "$<"

setup-programmer:
	@set -eu; \
	repo="$(CH32FUN_DIR)"; \
	git -C "$$repo" rev-parse --verify HEAD >/dev/null || { \
		echo "Set CH32FUN_DIR to a ch32fun Git checkout" >&2; exit 1; }; \
	mkdir -p "$(PROGRAMMER_DIR)"; \
	work=$$(mktemp -d "$(CURDIR)/.programmer-source.XXXXXXXX"); \
	trap 'rm -rf -- "$$work"' EXIT; \
	git -C "$$repo" archive -o "$$work/source.tar" HEAD minichlink ch32fun; \
	tar -xf "$$work/source.tar" -C "$$work"; \
	$(MAKE) -C "$$work/minichlink" minichlink; \
	strings "$$work/minichlink/minichlink" | \
		grep -F 'Curent data in BOOTLOADER will be rewritten.' >/dev/null || { \
		echo "This ch32fun revision lacks CH32V30x boot-region support" >&2; exit 1; }; \
	cp "$$work/minichlink/minichlink" "$(LOCAL_MINICHLINK)"; \
	echo "Programmer installed locally: $(LOCAL_MINICHLINK)"

# Builds the local programmer when it is missing, so that the flash and
# backup targets work without a separate setup step.
$(LOCAL_MINICHLINK):
	@$(MAKE) --no-print-directory setup-programmer

backup: $(LOCAL_MINICHLINK)
	@MINICHLINK="$(MINICHLINK)" BACKUP_DIR="$(BACKUP_DIR)" bash ./program-boot.sh backup

flash: $(BUILD)/$(TARGET).bin $(LOCAL_MINICHLINK)
	@MINICHLINK="$(MINICHLINK)" BACKUP_DIR="$(BACKUP_DIR)" bash ./program-boot.sh flash "$<"

restore: $(LOCAL_MINICHLINK)
	@test -n "$(BACKUP)" || { echo "Set BACKUP to a saved bootloader.bin" >&2; exit 1; }
	@MINICHLINK="$(MINICHLINK)" BACKUP_DIR="$(BACKUP_DIR)" bash ./program-boot.sh restore "$(BACKUP)"

monitor:
	@stty -F "$(PORT)" "$(BAUD)" raw -echo cs8 -cstopb -parenb -crtscts
	@echo "Monitoring $(PORT) at $(BAUD) baud; press Ctrl+C to stop."
	@cat "$(PORT)"

clean:
	rm -rf "$(BUILD)"

-include $(OBJECTS:.o=.d)
