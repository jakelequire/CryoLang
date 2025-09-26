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

# Find all source files
LSP_SOURCES = $(wildcard $(LSP_SRC_DIR)/*.cpp)
LSP_OBJECTS = $(LSP_SOURCES:$(LSP_SRC_DIR)/%.cpp=$(LSP_OBJ_DIR)/%.o)

# Include paths - reuse from main project plus LSP includes
LSP_INCLUDES = -I$(LSP_INCLUDE_DIR) -I../../include

# Compiler settings - inherit from main project
LSP_CXXFLAGS = $(CXXFLAGS) $(LSP_INCLUDES)

# Link against main compiler libraries
MAIN_OBJ_DIR = ../../bin/.o
LSP_LIBS = \
	$(MAIN_OBJ_DIR)/AST/ASTBuilder.o \
	$(MAIN_OBJ_DIR)/AST/ASTContext.o \
	$(MAIN_OBJ_DIR)/AST/ASTDumper.o \
	$(MAIN_OBJ_DIR)/AST/ASTNode.o \
	$(MAIN_OBJ_DIR)/AST/SymbolTable.o \
	$(MAIN_OBJ_DIR)/AST/Type.o \
	$(MAIN_OBJ_DIR)/AST/TypeChecker.o \
	$(MAIN_OBJ_DIR)/Codegen/CodeGenerator.o \
	$(MAIN_OBJ_DIR)/Codegen/CodegenVisitor.o \
	$(MAIN_OBJ_DIR)/Codegen/Intrinsics.o \
	$(MAIN_OBJ_DIR)/Codegen/LLVMContext.o \
	$(MAIN_OBJ_DIR)/Codegen/TargetConfig.o \
	$(MAIN_OBJ_DIR)/Codegen/TypeMapper.o \
	$(MAIN_OBJ_DIR)/Codegen/ValueContext.o \
	$(MAIN_OBJ_DIR)/Linker/CryoLinker.o \
	$(MAIN_OBJ_DIR)/Compiler/CompilerInstance.o \
	$(MAIN_OBJ_DIR)/GDM/GDM.o \
	$(MAIN_OBJ_DIR)/Lexer/lexer.o \
	$(MAIN_OBJ_DIR)/Parser/Parser.o \
	$(MAIN_OBJ_DIR)/Utils/file.o \
	$(MAIN_OBJ_DIR)/Utils/RuntimeHeaderParser.o \
	$(MAIN_OBJ_DIR)/Utils/ModuleLoader.o

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
	$(CXX) $(LSP_CXXFLAGS) -c $< -o $@

# Link the LSP executable
$(LSP_BIN_DIR)/$(LSP_TARGET): $(LSP_OBJECTS) | check-main-libs
	@echo "Linking CryoLSP..."
	$(CXX) $(LSP_CXXFLAGS) $(LSP_OBJECTS) $(LSP_LIBS) -o $@ $(LDFLAGS) $(SOCKET_LIBS)
	@echo "✓ CryoLSP built successfully: $(LSP_BIN_DIR)/$(LSP_TARGET)"

# Check that main compiler libraries exist
check-main-libs:
	@echo "Checking main compiler libraries..."
ifeq ($(OS), Windows_NT)
	@if not exist "$(MAIN_OBJ_DIR)\AST\ASTBuilder.o" ( \
		echo "Error: Main compiler libraries not found. Please run 'make' in the root directory first." && \
		exit 1 \
	)
else
	@if [ ! -f "$(MAIN_OBJ_DIR)/AST/ASTBuilder.o" ]; then \
		echo "Error: Main compiler libraries not found. Please run 'make' in the root directory first."; \
		exit 1; \
	fi
endif

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
