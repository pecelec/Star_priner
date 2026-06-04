# Makefile for local/cross builds.
#
# Native test build:
#   make
#
# Cross build:
#   make CC=mips-openwrt-linux-musl-gcc
#
# For an old ar71xx RutOS/OpenWrt SDK, use its staging_dir toolchain compiler.

CC ?= gcc
CFLAGS ?= -Os -Wall -Wextra
LDFLAGS ?=

all: osc_star_printer

osc_star_printer: osc_star_printer.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f osc_star_printer
