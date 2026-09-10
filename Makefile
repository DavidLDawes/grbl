#  Part of Grbl
#
#  Copyright (c) 2009-2011 Simen Svale Skogsrud
#  Copyright (c) 2012-2016 Sungeun K. Jeon for Gnea Research LLC
#
#  Grbl is free software: you can redistribute it and/or modify
#  it under the terms of the GNU General Public License as published by
#  the Free Software Foundation, either version 3 of the License, or
#  (at your option) any later version.
#
#  Grbl is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU General Public License for more details.
#
#  You should have received a copy of the GNU General Public License
#  along with Grbl.  If not, see <http://www.gnu.org/licenses/>.


# This is a prototype Makefile. Modify it according to your needs.
# You should at least check the settings for
# DEVICE ....... The AVR device you compile for
# CLOCK ........ Target AVR clock rate in Hertz
# OBJECTS ...... The object files created from your source files. This list is
#                usually the same as the list of source files with suffix ".o".
# PROGRAMMER ... Options to avrdude which define the hardware you use for
#                uploading to the AVR and the interface where this hardware
#                is connected.
# FUSES ........ Parameters for avrdude to flash the fuses appropriately.

DEVICE     ?= atmega328p
CLOCK      = 16000000
PROGRAMMER ?= -c avrisp2 -P usb
SOURCE    = main.c motion_control.c gcode.c spindle_control.c coolant_control.c serial.c \
             protocol.c stepper.c eeprom.c settings.c planner.c nuts_bolts.c limits.c jog.c\
             print.c probe.c report.c system.c
BUILDDIR = build
SOURCEDIR = grbl
# FUSES      = -U hfuse:w:0xd9:m -U lfuse:w:0x24:m
FUSES      = -U hfuse:w:0xd2:m -U lfuse:w:0xff:m

# Tune the lines below only if you know what you are doing:

AVRDUDE = avrdude $(PROGRAMMER) -p $(DEVICE) -B 10 -F

# Compile flags for avr-gcc v4.8.1. Does not produce -flto warnings.
# COMPILE = avr-gcc -Wall -Os -DF_CPU=$(CLOCK) -mmcu=$(DEVICE) -I. -ffunction-sections

# Compile flags for avr-gcc v4.9.2 compatible with the IDE. Or if you don't care about the warnings. 
COMPILE = avr-gcc -Wall -Os -DF_CPU=$(CLOCK) -mmcu=$(DEVICE) -I. -ffunction-sections -flto


OBJECTS = $(addprefix $(BUILDDIR)/,$(notdir $(SOURCE:.c=.o)))

.PHONY: all clean flash fuse install load disasm cpp size test test-clean

# symbolic targets:
all:	grbl.hex

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

$(BUILDDIR)/%.o: $(SOURCEDIR)/%.c | $(BUILDDIR)
	$(COMPILE) -MMD -MP -c $< -o $@

$(BUILDDIR)/%.o: $(SOURCEDIR)/%.S | $(BUILDDIR)
	$(COMPILE) -x assembler-with-cpp -c $< -o $@
# "-x assembler-with-cpp" should not be necessary since this is the default
# file type for the .S (with capital S) extension. However, upper case
# characters are not always preserved on Windows. To ensure WinAVR
# compatibility define the file type manually.

#.c.s:
	$(COMPILE) -S $< -o $(BUILDDIR)/$@

flash:	all
	$(AVRDUDE) -U flash:w:grbl.hex:i

fuse:
	$(AVRDUDE) $(FUSES)

# Xcode uses the Makefile targets "", "clean" and "install"
install: flash fuse

# if you use a bootloader, change the command below appropriately:
load: all
	bootloadHID grbl.hex

clean:
	rm -f grbl.hex $(BUILDDIR)/*.o $(BUILDDIR)/*.d $(BUILDDIR)/*.elf

# file targets:
$(BUILDDIR)/main.elf: $(OBJECTS)
	$(COMPILE) -o $(BUILDDIR)/main.elf $(OBJECTS) -lm -Wl,--gc-sections

grbl.hex: $(BUILDDIR)/main.elf
	rm -f grbl.hex
	avr-objcopy -j .text -j .data -O ihex $(BUILDDIR)/main.elf grbl.hex
	avr-size --format=berkeley $(BUILDDIR)/main.elf
# If you have an EEPROM section, you must also create a hex file for the
# EEPROM and add it to the "flash" target.

# Targets for code debugging and analysis:
disasm:	$(BUILDDIR)/main.elf
	avr-objdump -d $(BUILDDIR)/main.elf

cpp:
	$(COMPILE) -E $(SOURCEDIR)/main.c

# ---------------------------------------------------------------------------
# Flash/SRAM size regression guard (PLAN.md Fix 4.2). Budget chosen to still
# leave real margin on the tightest supported target -- an old-bootloader
# Nano has only 30720 bytes of flash available (32768 - 2048 for the
# bootloader) -- while catching a silent size creep well before it matters.
# Current baseline is ~29916 bytes text / 1633 bytes bss; see PLAN.md
# section 1.4 for the full Uno-vs-old-Nano budget breakdown. Override on the
# command line (e.g. `make size MAX_FLASH_BYTES=31000`) if a deliberate
# feature addition needs more room and you've confirmed it still fits your
# actual target board.
# ---------------------------------------------------------------------------
MAX_FLASH_BYTES ?= 30500
MAX_SRAM_BYTES  ?= 1800

size: $(BUILDDIR)/main.elf
	@SIZES=$$(avr-size --format=berkeley $(BUILDDIR)/main.elf | tail -1); \
	TEXT=$$(echo "$$SIZES" | awk '{print $$1}'); \
	DATA=$$(echo "$$SIZES" | awk '{print $$2}'); \
	BSS=$$(echo "$$SIZES" | awk '{print $$3}'); \
	echo "text=$$TEXT data=$$DATA bss=$$BSS  (budget: text<=$(MAX_FLASH_BYTES) bss<=$(MAX_SRAM_BYTES))"; \
	FAIL=0; \
	if [ "$$TEXT" -gt "$(MAX_FLASH_BYTES)" ]; then \
	  echo "FAIL: flash usage $$TEXT bytes exceeds budget of $(MAX_FLASH_BYTES) bytes"; \
	  FAIL=1; \
	fi; \
	if [ "$$BSS" -gt "$(MAX_SRAM_BYTES)" ]; then \
	  echo "FAIL: SRAM usage $$BSS bytes exceeds budget of $(MAX_SRAM_BYTES) bytes"; \
	  FAIL=1; \
	fi; \
	if [ "$$FAIL" -eq 1 ]; then exit 1; fi; \
	echo "OK: within budget"

# ---------------------------------------------------------------------------
# Host-side test harness (test/). Compiles gcode.c, planner.c, and
# nuts_bolts.c natively (not with avr-gcc) against a stubbed hardware
# layer, so the harness needs only a host C compiler and libm -- no AVR
# toolchain, no board. See test/README.md for what it covers and why.
# ---------------------------------------------------------------------------
HOSTCC ?= gcc
TEST_BUILDDIR = test/build
TEST_BIN = $(TEST_BUILDDIR)/grbl_test
TEST_SOURCES = grbl/nuts_bolts.c grbl/planner.c grbl/gcode.c \
               test/grbl_stubs.c test/test_main.c \
               test/test_nuts_bolts.c test/test_planner.c test/test_gcode.c

test:
	mkdir -p $(TEST_BUILDDIR)
	$(HOSTCC) -std=c99 -Wall -Wextra -Wno-implicit-fallthrough -DF_CPU=16000000UL \
		-I test/avr_shim -I grbl -I test \
		$(TEST_SOURCES) -o $(TEST_BIN) -lm
	$(TEST_BIN)

test-clean:
	rm -f $(TEST_BIN) $(TEST_BIN).exe $(TEST_BUILDDIR)/*.o

# include generated header dependencies
-include $(OBJECTS:.o=.d)
