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
SOURCES := main.c blocks.c world.c worldgen.c save.c player.c mesher.c ui.c audio.c settings.c
LIB_SOURCES := blocks.c world.c worldgen.c save.c player.c
OBJECTS := $(addprefix $(BUILD_DIR)/,$(SOURCES:.c=.o))
LIB_OBJECTS := $(addprefix $(BUILD_DIR)/,$(LIB_SOURCES:.c=.o))
TEST_BINS := $(BUILD_DIR)/test_blocks $(BUILD_DIR)/test_storage $(BUILD_DIR)/test_player $(BUILD_DIR)/test_worldgen
DEPS := $(OBJECTS:.o=.d) $(addsuffix .d,$(TEST_BINS))

all: $(TARGET)

$(TARGET): $(OBJECTS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(OBJECTS) -o $@ $(RAYLIB_LIBS) -lm

$(BUILD_DIR)/test_blocks: $(BUILD_DIR)/test_blocks.o $(LIB_OBJECTS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $^ -o $@ $(RAYLIB_LIBS) -lm

$(BUILD_DIR)/test_storage: $(BUILD_DIR)/test_storage.o $(LIB_OBJECTS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $^ -o $@ $(RAYLIB_LIBS) -lm

$(BUILD_DIR)/test_player: $(BUILD_DIR)/test_player.o $(LIB_OBJECTS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $^ -o $@ $(RAYLIB_LIBS) -lm

$(BUILD_DIR)/test_worldgen: $(BUILD_DIR)/test_worldgen.o $(LIB_OBJECTS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $^ -o $@ $(RAYLIB_LIBS) -lm

$(BUILD_DIR)/%.o: %.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(RAYLIB_CFLAGS) -MMD -MP -c $< -o $@

$(BUILD_DIR):
	mkdir -p $@

test: $(TEST_BINS)
	./$(BUILD_DIR)/test_blocks
	./$(BUILD_DIR)/test_storage
	./$(BUILD_DIR)/test_player
	./$(BUILD_DIR)/test_worldgen

clean:
	rm -rf $(BUILD_DIR)

-include $(DEPS)

.PHONY: all clean test
