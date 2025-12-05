# Makefile - build both the guest (guest.bin)
# Usage:
#   make            # builds guest.bin
#   make guest.bin  # builds only guest binary
#   make clean
#
# Requires GNU binutils and gcc for aarch64:
#   sudo apt install build-essential gcc-aarch64-linux-gnu binutils-aarch64-linux-gnu

CROSS ?= aarch64-linux-gnu
AS      = $(CROSS)-as
LD      = $(CROSS)-ld
OBJCOPY = $(CROSS)-objcopy
CC      = gcc

ASFLAGS = -march=armv8-a
LDFLAGS = -T guest.ld

.PHONY: all clean guest

all: guest.bin

guest.o: guest.S
	$(AS) $(ASFLAGS) -o $@ $<

guest.elf: guest.o guest.ld
	$(LD) $(LDFLAGS) -o $@ guest.o

guest.bin: guest.elf
	$(OBJCOPY) -O binary $< $@

clean:
	rm -f guest.o guest.elf guest.bin

