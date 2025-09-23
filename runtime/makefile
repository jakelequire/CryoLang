# CryoLang Runtime Library Makefile

CC = clang
CFLAGS = -std=c11 -fPIC -O2 -Wall -Wextra
AR = ar
ARFLAGS = rcs

BUILD_DIR = build
TARGET_STATIC = $(BUILD_DIR)/libcryoruntime.a
TARGET_SHARED = $(BUILD_DIR)/libcryoruntime.so
SOURCES = cryo_runtime.c
OBJECTS = $(SOURCES:%.c=$(BUILD_DIR)/%.o)
HEADERS = cryo_runtime.h

.PHONY: all clean shared static

all: static shared

static: $(TARGET_STATIC)

shared: $(TARGET_SHARED)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/%.o: %.c $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET_STATIC): $(OBJECTS)
	$(AR) $(ARFLAGS) $@ $^

$(TARGET_SHARED): $(OBJECTS)
	$(CC) $(CFLAGS) -shared -o $@ $^

clean:
	rm -rf $(BUILD_DIR)

install: all
	# TODO: Add installation logic if needed

.DEFAULT_GOAL = all