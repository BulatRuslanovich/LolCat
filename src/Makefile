CC ?= gcc
CFLAGS ?= -std=c11 -Wall -Wextra -O3 
LIBS := -lm
GEN_NAME = xterm256PaletteGen
BUILD_DIR = build
INSTALL_DIR = $(HOME)/lolCat

all: $(BUILD_DIR) lolcat

install: $(BUILD_DIR) lolcat
	@mkdir -p $(HOME)/LolCat/bin/
	@cp $(BUILD_DIR)/lolcat $(INSTALL_DIR)/bin/
	@rm -rf $(BUILD_DIR)

uninstall:
	@if [ -d "$(INSTALL_DIR)" ]; then \
		rm -rf $(INSTALL_DIR); \
		echo "-- Uninstalling: $(INSTALL_DIR)"; \
	else \
		echo "-- Uninstalling: Application don't install"; \
   	fi

# генерить это не нужно
xterm256Palette.h:
	@mcs $(GEN_NAME).cs
	@mono $(GEN_NAME).exe > $@
	@rm -rf $(GEN_NAME).exe

lolcat: lolcat.c
	@$(CC) $(CFLAGS) -o $(addprefix $(BUILD_DIR)/, $@) $< $(LIBS)
$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)
clear : 
	@#rm -rf lolcat
	@rm -rf $(BUILD_DIR)
