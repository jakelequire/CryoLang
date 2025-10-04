#*******************************************************************************
#  Copyright 2025 Jacob LeQuire                                                *
#  SPDX-License-Identifier: Apache-2.0                                         *
#*******************************************************************************

#*******************************************************************************
# CryoLSP (Language Server Protocol) Makefile                                  *
#*******************************************************************************

# Get settings from parent makefile
include ../../makefile.config

# LSP-specific settings
LSP_TARGET = cryo-lsp$(BIN_SUFFIX)
LSP_BIN_DIR = ../../bin
LSP_OBJ_DIR = ../../bin/.o/LSP

# Source directories
LSP_SRC_DIR = src
LSP_INCLUDE_DIR = include

# Find all source files recursively
LSP_SOURCES = $(shell find $(LSP_SRC_DIR) -name "*.cpp" 2>/dev/null || powershell -Command "Get-ChildItem -Path $(LSP_SRC_DIR) -Recurse -Filter '*.cpp' | ForEach-Object { $$_.FullName -replace '.*\\$(LSP_SRC_DIR)\\', '$(LSP_SRC_DIR)/' -replace '\\', '/' }")
LSP_OBJECTS = $(patsubst $(LSP_SRC_DIR)/%.cpp,$(LSP_OBJ_DIR)/%.o,$(LSP_SOURCES))

# Include paths - reuse from main project plus LSP includes
LSP_INCLUDES = -I$(LSP_INCLUDE_DIR) -I../../include

# Compiler settings - inherit from main project
LSP_CXXFLAGS = $(CXXFLAGS) $(LSP_INCLUDES)

# Link against minimal dependencies (standalone LSP server)
LSP_LIBS = 

# OS-specific socket libraries
ifeq ($(OS), Windows_NT)
    SOCKET_LIBS = -lws2_32 -lwsock32
else
    SOCKET_LIBS = 
endif

.PHONY: all clean help

# Default target
all: $(LSP_BIN_DIR)/$(LSP_TARGET)

# Create output directories
$(LSP_OBJ_DIR):
	@echo "Creating LSP object directory..."
ifeq ($(OS), Windows_NT)
	@if not exist "$(LSP_OBJ_DIR)" mkdir "$(subst /,\,$(LSP_OBJ_DIR))"
else
	@mkdir -p $(LSP_OBJ_DIR)
endif

# Compile LSP source files
$(LSP_OBJ_DIR)/%.o: $(LSP_SRC_DIR)/%.cpp | $(LSP_OBJ_DIR)
	@echo "Compiling LSP: $<"
ifeq ($(OS), Windows_NT)
	@if not exist "$(dir $@)" mkdir "$(subst /,\,$(dir $@))"
else
	@mkdir -p $(dir $@)
endif
	$(CXX) $(LSP_CXXFLAGS) -c $< -o $@

# Link the LSP executable
$(LSP_BIN_DIR)/$(LSP_TARGET): $(LSP_OBJECTS)
	@echo "Linking CryoLSP..."
	$(CXX) $(LSP_CXXFLAGS) $(LSP_OBJECTS) $(LSP_LIBS) -o $@ $(LDFLAGS) $(SOCKET_LIBS)
	@echo "✓ CryoLSP built successfully: $(LSP_BIN_DIR)/$(LSP_TARGET)"



# Clean LSP build artifacts
clean:
	@echo "Cleaning LSP build artifacts..."
ifeq ($(OS), Windows_NT)
	@if exist "$(LSP_OBJ_DIR)" rmdir /S /Q "$(subst /,\,$(LSP_OBJ_DIR))"
	@if exist "$(LSP_BIN_DIR)\$(LSP_TARGET)" del /Q "$(subst /,\,$(LSP_BIN_DIR)\$(LSP_TARGET))"
else
	@rm -rf $(LSP_OBJ_DIR)
	@rm -f $(LSP_BIN_DIR)/$(LSP_TARGET)
endif
	@echo "✓ LSP clean completed"

# Help target
help:
	@echo "CryoLSP Build System"
	@echo "===================="
	@echo "Targets:"
	@echo "  all     - Build the CryoLSP server (default)"
	@echo "  clean   - Clean LSP build artifacts"
	@echo "  help    - Show this help message"
	@echo ""
	@echo "Output:"
	@echo "  Binary: $(LSP_BIN_DIR)/$(LSP_TARGET)"
	@echo "  Objects: $(LSP_OBJ_DIR)/"