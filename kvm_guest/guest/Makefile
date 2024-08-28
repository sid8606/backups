#
# Copyright © 2023 inpyjama.com. All Rights Reserved.
# Author: Piyush Itankar <piyush@inpyjama.com>
#

INCLUDES := -Ilib

img:
	@echo "Building image for RPi 4+"
	gcc $(INCLUDES) -Wall -nostdlib -nostartfiles -ffreestanding -g -c kernel.c -o kernel.o
	gcc $(INCLUDES) -Wall -nostdlib -nostartfiles -ffreestanding -g -c start.S -o start.o
	ld -g -m aarch64elf -nostdlib -T linker.ld kernel.o start.o -o kernel.bin

clean:
	@rm -rf *.bin *.o
