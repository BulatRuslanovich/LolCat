CC ?= gcc
CFLAGS ?= -std=c11 -Wall -Wextra -O3 
LIBS := -lm
GEN_NAME = xterm256PaletteGen

all: lolcat

xterm256Palette.h: 
	@mcs $(GEN_NAME).cs
	@mono $(GEN_NAME).exe > $@
	@rm -rf $(GEN_NAME).exe

lolcat: lolcat.c xterm256Palette.h
	$(CC) $(CFLAGS) -o $@ $< $(LIBS)

clear : 
	@rm -rf lolcat