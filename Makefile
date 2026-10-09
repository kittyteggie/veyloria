CC ?= cc
PKG_CONFIG ?= pkg-config
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Wpedantic
RAYLIB_CFLAGS := $(shell $(PKG_CONFIG) --cflags raylib 2>/dev/null)
RAYLIB_LIBS := $(shell $(PKG_CONFIG) --libs raylib 2>/dev/null)

ifeq ($(strip $(RAYLIB_LIBS)),)
RAYLIB_LIBS := -lraylib
endif

BUILD_DIR := bin
TARGET := $(BUILD_DIR)/veyloria
SOURCES := main.c blocks.c
OBJECTS := $(addprefix $(BUILD_DIR)/,$(SOURCES:.c=.o))

all: $(TARGET)

$(TARGET): $(OBJECTS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(OBJECTS) -o $@ $(RAYLIB_LIBS) -lm

$(BUILD_DIR)/%.o: %.c blocks.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(RAYLIB_CFLAGS) -c $< -o $@

$(BUILD_DIR):
	mkdir -p $@

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all clean
