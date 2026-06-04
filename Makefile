CC ?= gcc
CFLAGS ?= -Os -Wall -Wextra
LDFLAGS ?=

all: osc_star_printer

osc_star_printer: osc_star_printer.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f osc_star_printer
