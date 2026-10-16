# Makefile for the RISKYMSX2 Nextor driver + kernel ROM pipeline.
#
#   make            build the flashable Nextor ROM (Nextor-3.0.RISKYMSX2.ROM)
#   make embed      build the ROM, then regenerate firmware/User/nextor_rom.c
#                   (delegates to the firmware Makefile, the sole writer)
#   make test       build the ROM and boot-test it in openMSX (C-BIOS; the
#                   driver's mailbox handshake fails there -> kernel boots
#                   without the driver = the plan's graceful-degradation test)
#   make clean      remove build artifacts
#
# Tools: N80 (vendored, needs a .NET runtime; DOTNET_ROLL_FORWARD covers a
# newer runtime than N80 was built against) and mknexrom (vendored).
#
# To build the ROM AND the firmware in one shot, just run `make` in
# firmware/ - it depends on every *.asm in this directory.
#
# Pipeline (docs/NEXTOR_PLAN.md D3):
#   N80 driver.asm -> driver.bin (exactly 16080 bytes, 0x4100..0x7FCF)
#   N80 chgbnk.asm -> chgbnk.bin (ASCII16 bank-switch tail, 0x7FD0..0x7FFF)
#   mknexrom <base.dat> <ROM> /d:driver.bin /m:chgbnk.bin
#   -> 8 banks x 16 KB = 128 KB ROM (7 kernel banks + driver bank 7)
#   tools/rom2c.py --nextor -> firmware/User/nextor_rom.c (.cartrom embed)
#   (run by the firmware Makefile, which owns that output)

NAME      := RISKYMSX2
ROM       := Nextor-3.0.RISKYMSX2.ROM

VENDOR    := vendor
NEXTOR_SDK  := $(VENDOR)/nextor-sdk/sdk
# Build against the beta2 kernel base. The USB root contains the matching
# Nextor-3.0 beta2 NEXTOR.SYS; using the beta1 base here makes the kernel
# reject that system file after reading its first and last sectors.
NEXTOR_BASE ?= $(firstword $(wildcard ../Nextor/Nextor/bin/kernel-base/Nextor-3.0.0-beta2.base.dat) \
                           $(wildcard $(VENDOR)/Nextor-3.0.0-beta2.base.dat) \
                           $(VENDOR)/Nextor-3.0.0-beta1.base.dat)
N80         := $(VENDOR)/n80/N80
MKNEXROM    := $(VENDOR)/mknexrom

# Debug probes. The per-sector value markers and the per-READ_WRITE frame
# trace are full mailbox round trips sitting in the middle of the read
# hot path, so they are off unless asked for. `make PROBES=1` (in this
# directory or in firmware/, which forwards the knob) assembles them back
# in, and the firmware logs the matching lines when built with QUIET unset.
PROBES ?=
ifeq ($(PROBES),1)
N80_PROBES := -ds RW_PROBES
endif

N80_FLAGS := --no-string-escapes --no-show-banner --verbosity 0 \
             --include-directory $(NEXTOR_SDK) $(N80_PROBES)

# NOTE: this Makefile does NOT write firmware/User/nextor_rom.c.
# The firmware Makefile owns that generated source and regenerates it
# from $(ROM) as a proper file target, so `make` in firmware/ picks up a
# driver.asm edit in one pass. Having two Makefiles write the same file
# made them overwrite each other with different header text, which is
# what made the embed look non-deterministic.
export DOTNET_ROLL_FORWARD := Major

.PHONY: all clean embed test

all: $(ROM)

# `make embed` now just delegates to the firmware build, which is the
# only writer of nextor_rom.c.
embed: $(ROM)
	$(MAKE) -C ../firmware User/nextor_rom.c

$(ROM): driver.bin chgbnk.bin $(NEXTOR_BASE)
	@test -f "$(NEXTOR_BASE)" || { \
	    echo "*** Kernel base file not found: $(NEXTOR_BASE)"; exit 1; }
	$(MKNEXROM) $(NEXTOR_BASE) $@ /d:driver.bin /m:chgbnk.bin
	@echo "Built $@"

# Mirror of the firmware's QUIET mode stamp: switching PROBES flips this
# file so `make PROBES=1` cannot silently reuse a probe-free driver.bin.
PROBE_STAMP := .probes-$(if $(N80_PROBES),on,off)
$(PROBE_STAMP):
	@rm -f .probes-on .probes-off
	@touch "$@"

driver.bin: driver.asm $(PROBE_STAMP)
	$(N80) driver.asm driver.bin $(N80_FLAGS)
	@test $$(stat -c%s driver.bin) -eq 16336 || { \
	    echo "*** driver.bin is not 16336 bytes (expected 0x4000..0x7FCF: 256 dummy + 16080 code)"; exit 1; }

chgbnk.bin: chgbnk.asm
	$(N80) chgbnk.asm chgbnk.bin $(N80_FLAGS)

test: $(ROM)
	/tmp/opencode/msx/xvfb-openmsx.sh -machine C-BIOS_MSX2+ \
	    -carta $(CURDIR)/$(ROM) -romtype ASCII16 \
	    -command "after time 15 { screenshot /tmp/opencode/msx/nextor-test.png } ; after time 16 exit"

clean:
	rm -f driver.bin chgbnk.bin $(ROM) .probes-on .probes-off
