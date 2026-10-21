# Native Voyager core - builds with clang (or any C11 compiler) on Linux and macOS.
CC      ?= clang
CFLAGS  ?= -std=c11 -O2 -Wall -Wextra -Wpedantic
CPPFLAGS += -Iinclude -D_POSIX_C_SOURCE=200809L
SRC     = src/cpu.c src/machine.c src/sha256.c src/inspect.c
OBJ     = $(SRC:src/%.c=build/%.o)

all: build/libvoyagercore.a build/vcore build/test_lcd build/test_multi build/test_inspect build/test_pool15 build/test_mem16 build/test_matrix15 build/test_firmware

build/%.o: src/%.c src/cpu.h src/sha256.h src/internal.h src/optables.h include/voyager_core.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
build/libvoyagercore.a: $(OBJ)
	ar rcs $@ $^
build/test_multi: tests/test_multi.c build/libvoyagercore.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $< build/libvoyagercore.a -o $@
build/test_inspect: tests/test_inspect.c build/libvoyagercore.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $< build/libvoyagercore.a -lm -o $@
build/test_mem16: tests/test_mem16.c build/libvoyagercore.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $< build/libvoyagercore.a -o $@
build/test_pool15: tests/test_pool15.c build/libvoyagercore.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $< build/libvoyagercore.a -lm -o $@
build/test_matrix15: tests/test_matrix15.c build/libvoyagercore.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $< build/libvoyagercore.a -lm -o $@
build/test_firmware: tests/test_firmware.c build/libvoyagercore.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $< build/libvoyagercore.a -o $@
build/test_lcd: tests/test_lcd.c build/libvoyagercore.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $< build/libvoyagercore.a -o $@
build/vcore: tools/vcore.c build/libvoyagercore.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $< build/libvoyagercore.a -o $@
build:
	mkdir -p build
clean:
	rm -rf build
.PHONY: all clean
