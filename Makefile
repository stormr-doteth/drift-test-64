# make DEBUG=1  -> on-screen boot steps, frame counter, and libdragon's
#                  development IPL3 (reports boot errors over USB).
ifeq ($(DEBUG),1)
ROM = drift-test-64-debug
BUILD_DIR = build-debug
else
ROM = drift-test-64
BUILD_DIR = build
endif
SOURCE_DIR = src
include $(N64_INST)/include/n64.mk

# Pad to 1 MiB: some flashcart menus expect at least that much ROM.
N64_TOOLFLAGS += --size 1M

ifeq ($(DEBUG),1)
N64_CFLAGS += -DDEBUG_BOOT
LIBDRAGON_SRC ?= $(HOME)/n64dev/libdragon
N64_ROM_HEADER = $(LIBDRAGON_SRC)/boot/bin/ipl3_dev.z64
endif

all: $(ROM).z64

$(BUILD_DIR)/$(ROM).elf: $(BUILD_DIR)/main.o $(BUILD_DIR)/report.o

$(ROM).z64: N64_ROM_TITLE = "Drift Test 64"
$(ROM).z64: N64_ROM_RTC = true

# PC-side sample report (for previewing the printable layout)
sample-report.html: tools/sample_report.c src/report.c src/report.h
	mkdir -p build
	gcc -O2 -Isrc -o build/sample_report tools/sample_report.c src/report.c -lm
	./build/sample_report > $@

clean:
	rm -rf build build-debug drift-test-64.z64 drift-test-64-debug.z64

-include $(wildcard $(BUILD_DIR)/*.d)

.PHONY: all clean
