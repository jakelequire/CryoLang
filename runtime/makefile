#*******************************************************************************
#  CryoLang Runtime Library Makefile
#  
#  Builds the CryoLang standard library runtime as both static and shared
#  libraries for linking with compiled CryoLang programs.
#*******************************************************************************

# Include parent configuration
include ../makefile.config

# Runtime-specific settings
RUNTIME_NAME = cryoruntime
STATIC_LIB = lib$(RUNTIME_NAME).a
SHARED_LIB = lib$(RUNTIME_NAME).so
HEADER_FILE = include/cryo_runtime.h

# Directories
SRC_DIR = src
INCLUDE_DIR = include
BUILD_DIR = build
INSTALL_DIR = ../bin

# Source files
C_SOURCES = cryo_runtime.c
OBJECTS = $(BUILD_DIR)/cryo_runtime.o

# Compiler flags specific to runtime
RUNTIME_CFLAGS = $(CFLAGS) -fPIC -I$(INCLUDE_DIR)
RUNTIME_LDFLAGS = -lm  # Link math library

# OS-specific library extension
ifeq ($(OS), Windows_NT)
    SHARED_LIB = lib$(RUNTIME_NAME).dll
    RUNTIME_LDFLAGS += -shared
else
    SHARED_LIB = lib$(RUNTIME_NAME).so
    RUNTIME_LDFLAGS += -shared -fPIC
endif

# Create build directory
$(BUILD_DIR):
	@$(MKDIR) $(BUILD_DIR)

# Build object files
$(BUILD_DIR)/%.o: %.c | $(BUILD_DIR)
	@echo "Compiling runtime: $<"
	$(CC) $(RUNTIME_CFLAGS) -c $< -o $@

# Build static library
$(BUILD_DIR)/$(STATIC_LIB): $(OBJECTS)
	@echo "Creating static library: $(STATIC_LIB)"
	ar rcs $@ $(OBJECTS)

# Build shared library  
$(BUILD_DIR)/$(SHARED_LIB): $(OBJECTS)
	@echo "Creating shared library: $(SHARED_LIB)"
	$(CC) -shared $(OBJECTS) -o $@ $(RUNTIME_LDFLAGS)

# Default target: build both static and shared libraries
.PHONY: all
all: $(BUILD_DIR)/$(STATIC_LIB) $(BUILD_DIR)/$(SHARED_LIB)
	@echo "✓ CryoLang runtime libraries built successfully"

# Install libraries to main bin directory
.PHONY: install
install: all
	@echo "Installing runtime libraries to $(INSTALL_DIR)..."
	@$(MKDIR) $(INSTALL_DIR)
	cp $(BUILD_DIR)/$(STATIC_LIB) $(INSTALL_DIR)/
	cp $(BUILD_DIR)/$(SHARED_LIB) $(INSTALL_DIR)/
	cp $(HEADER_FILE) $(INSTALL_DIR)/
	@echo "✓ Runtime libraries installed"

# Clean build artifacts
.PHONY: clean
clean:
	@echo "Cleaning runtime build artifacts..."
	@$(RMDIR) $(BUILD_DIR) 2>/dev/null || true
	@echo "✓ Runtime clean completed"

# Test the runtime library (future implementation)
.PHONY: test
test: all
	@echo "Runtime testing not yet implemented"

# Development targets
.PHONY: static shared
static: $(BUILD_DIR)/$(STATIC_LIB)
shared: $(BUILD_DIR)/$(SHARED_LIB)

# Show information about the build
.PHONY: info
info:
	@echo "CryoLang Runtime Build Information:"
	@echo "  Static Library: $(BUILD_DIR)/$(STATIC_LIB)"
	@echo "  Shared Library: $(BUILD_DIR)/$(SHARED_LIB)"
	@echo "  Header File: $(HEADER_FILE)"
	@echo "  C Compiler: $(CC)"
	@echo "  C Flags: $(RUNTIME_CFLAGS)"
	@echo "  LD Flags: $(RUNTIME_LDFLAGS)"

.PHONY: help
help:
	@echo "CryoLang Runtime Makefile Usage:"
	@echo "  make all      - Build both static and shared libraries"
	@echo "  make static   - Build only static library"
	@echo "  make shared   - Build only shared library"
	@echo "  make install  - Install libraries to bin directory"
	@echo "  make clean    - Remove build artifacts"
	@echo "  make test     - Run runtime tests (future)"
	@echo "  make info     - Show build configuration"
	@echo "  make help     - Show this help message"