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
TEST_TARGET := $(BUILD_DIR)/test_storage
SOURCES := main.c blocks.c world.c save.c
LIB_SOURCES := blocks.c world.c save.c
OBJECTS := $(addprefix $(BUILD_DIR)/,$(SOURCES:.c=.o))
TEST_SOURCES := test_storage.c $(LIB_SOURCES)
TEST_OBJECTS := $(addprefix $(BUILD_DIR)/,$(TEST_SOURCES:.c=.o))
DEPS := $(OBJECTS:.o=.d) $(TEST_OBJECTS:.o=.d)

all: $(TARGET)

$(TARGET): $(OBJECTS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(OBJECTS) -o $@ $(RAYLIB_LIBS) -lm

$(TEST_TARGET): $(TEST_OBJECTS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(TEST_OBJECTS) -o $@ $(RAYLIB_LIBS) -lm

$(BUILD_DIR)/%.o: %.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(RAYLIB_CFLAGS) -MMD -MP -c $< -o $@

$(BUILD_DIR):
	mkdir -p $@

test: $(TEST_TARGET)
	./$(TEST_TARGET)

clean:
	rm -rf $(BUILD_DIR)

-include $(DEPS)

.PHONY: all clean test
