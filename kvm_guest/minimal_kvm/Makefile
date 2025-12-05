# Makefile to build kvm userspace program only
# Location: /home/sid/minimal_kvm

# Optional: set CROSS=aarch64-linux-gnu- when cross-compiling.
CROSS ?=

CC      := $(CROSS)gcc
CFLAGS  := -Wall -Wextra -O2
LDFLAGS := 

TARGET  := kvm
SRC     := kvm.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET) $(LDFLAGS)

clean:
	rm -f $(TARGET)

.PHONY: all clean

