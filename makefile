#*******************************************************************************
#  Copyright 2025 Jacob LeQuire                                                *
#  SPDX-License-Identifier: Apache-2.0                                         *
#    Licensed under the Apache License, Version 2.0 (the "License");           *
#    you may not use this file except in compliance with the License.          *
#    You may obtain a copy of the License at                                   *
#                                                                              *
#    http://www.apache.org/licenses/LICENSE-2.0                                *
#                                                                              *
#    Unless required by applicable law or agreed to in writing, software       *
#    distributed under the License is distributed on an "AS IS" BASIS,         *
#    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  *
#    See the License for the specific language governing permissions and       *
#    limitations under the License.                                            *
#                                                                              *
#*******************************************************************************

#*******************************************************************************
# Cryo Compiler Makefile                                                       *
#*******************************************************************************

# Include shared configuration
include makefile.config

# Determine number of CPU cores
ifeq ($(OS), Windows_NT)
    NUM_CORES = $(NUMBER_OF_PROCESSORS)
else
    NUM_CORES = $(shell nproc)
endif

NUM_JOBS = $(shell expr $(NUM_CORES) + 1)

# >>=======--------------------------------------------------=======<< #
# >>=======                 Include Paths                    =======<< #
# >>=======--------------------------------------------------=======<< #

# >>=======--------------------------------------------------=======<< #
# >>=======                  Define Paths                    =======<< #
# >>=======--------------------------------------------------=======<< #

# ---------------------------------------------
# Binary directory
BIN_DIR =       ./bin/
OBJ_DIR =       $(BIN_DIR).o/
DEBUG_BIN_DIR = $(BIN_DIR)debug/

# ---------------------------------------------
# Source directory
SRC_DIR =   ./src/
MAIN_FILE = $(SRC_DIR)main.cpp

# >>=======--------------------------------------------------=======<< #
# >>=======                 Compilation Rules                =======<< #
# >>=======--------------------------------------------------=======<< #

# ---------------------------------------------
# Main binary
MAIN_BIN = $(BIN_DIR)cryo$(BIN_SUFFIX)

# ---------------------------------------------
# Ensure OBJ_DIR exists
# Create directories at makefile parse time
ifeq ($(OS), Windows_NT)
    $(shell if not exist "bin" mkdir "bin")
    $(shell if not exist "bin\.o" mkdir "bin\.o")
else
    $(shell $(MKDIR) $(OBJ_DIR))
endif

# ---------------------------------------------
# Define all source files
ifeq ($(OS), Windows_NT)
    # Windows - using MSYS2 find with full path
    C_SRCS := $(shell C:/msys64/usr/bin/find $(SRC_DIR) -name "*.c" -type f ! -path "*/wasm/*")
    CPP_SRCS := $(shell C:/msys64/usr/bin/find $(SRC_DIR) -name "*.cpp" -type f ! -path "*/wasm/*")
else
    # Linux - native find
    C_SRCS := $(shell find $(SRC_DIR) -name "*.c" -type f ! -path "*/wasm/*")
    CPP_SRCS := $(shell find $(SRC_DIR) -name "*.cpp" -type f ! -path "*/wasm/*")
endif

# ---------------------------------------------
# Define all object files
ifeq ($(OS), Windows_NT)
    C_OBJS := $(patsubst $(SRC_DIR)%, $(OBJ_DIR)%, $(C_SRCS:.c=.o))
    CPP_OBJS := $(patsubst $(SRC_DIR)%, $(OBJ_DIR)%, $(CPP_SRCS:.cpp=.o))
else
    C_OBJS := $(patsubst $(SRC_DIR)%.c,$(OBJ_DIR)%.o,$(C_SRCS))
    CPP_OBJS := $(patsubst $(SRC_DIR)%.cpp,$(OBJ_DIR)%.o,$(CPP_SRCS))
endif

# ---------------------------------------------
# Combine all object files
ALL_OBJS := $(C_OBJS) $(CPP_OBJS)

# ---------------------------------------------
# Compiler library object files (exclude main.cpp)
LIB_OBJS := $(filter-out $(OBJ_DIR)main.o,$(ALL_OBJS))
COMPILER_LIB := $(BIN_DIR)libcryo-compiler.a

# Add these directory rules
$(BIN_DIR) $(OBJ_DIR):
ifeq ($(OS), Windows_NT)
	@if not exist "$(subst /,\,$@)" mkdir "$(subst /,\,$@)"
else
	@$(MKDIR) $@
endif

# ---------------------------------------------
# Compile C source files
$(OBJ_DIR)%.o: $(SRC_DIR)%.c | $(OBJ_DIR)
ifeq ($(OS), Windows_NT)
	@if not exist "$(subst /,\,$(dir $@))" mkdir "$(subst /,\,$(dir $@))"
else
	@$(MKDIR) $(dir $@)
endif
	$(CXX) $(CFLAGS) -c $< -o $@

# ---------------------------------------------
# Compile C++ source files
$(OBJ_DIR)%.o: $(SRC_DIR)%.cpp | $(OBJ_DIR)
ifeq ($(OS), Windows_NT)
	@if not exist "$(subst /,\,$(dir $@))" mkdir "$(subst /,\,$(dir $@))"
else
	@$(MKDIR) $(dir $@)
endif
	$(CXX) $(CXXFLAGS) -c $< -o $@

# >>=======--------------------------------------------------=======<< #
# >>=======                   Link Binaries                  =======<< #
# >>=======--------------------------------------------------=======<< #

# Main target
$(MAIN_BIN): $(ALL_OBJS)
ifeq ($(OS), Windows_NT)
	@if not exist "$(subst /,\,$(dir $@))" $(MKDIR) "$(subst /,\,$(dir $@))"
else
	@$(MKDIR) $(dir $@)
endif
	$(CXX) $(CXXFLAGS) $(ALL_OBJS) -o $@ $(LDFLAGS)

# Compiler library target (for LSP integration)
$(COMPILER_LIB): $(LIB_OBJS)
ifeq ($(OS), Windows_NT)
	@if not exist "$(subst /,\,$(dir $@))" $(MKDIR) "$(subst /,\,$(dir $@))"
else
	@$(MKDIR) $(dir $@)
endif
	@echo "Creating Cryo compiler library: $(COMPILER_LIB)"
	ar rcs $@ $^
	@echo "✓ Compiler library created successfully"

# >>=======--------------------------------------------------=======<< #
# >>=======                     Commands                     =======<< #
# >>=======--------------------------------------------------=======<< #

# Build timer script
BUILD_TIMER = ./scripts/build_timer.py

# >>=======--------------------------------------------------=======<< #
# >>=======                    TOOLS                         =======<< #
# >>=======--------------------------------------------------=======<< #

# Tools targets - delegate to individual tool makefiles
.PHONY: tools lsp compiler-lib
tools: lsp

# Compiler library target for LSP integration
compiler-lib: $(COMPILER_LIB)

lsp: $(COMPILER_LIB)
	@echo "Building cjson library..."
	@$(MAKE) -C libs/cjson static
	@echo "Building CryoLSP..."
	@$(MAKE) -C tools/CryoLSP

compiler: $(MAIN_BIN)
	@echo "Building Cryo Compiler..."
	@$(MAKE) -C .

# Timed build target
.PHONY: timed-build
timed-build:
	@$(PYTHON) $(BUILD_TIMER)

# Clean and timed build
.PHONY: rebuild
rebuild:
	@$(PYTHON) $(BUILD_TIMER) --clean

# Standard library compilation
STDLIB_DIR = ./stdlib
STDLIB_BUILD_DIR = $(BIN_DIR)stdlib
STDLIB_LIB = $(STDLIB_BUILD_DIR)/libcryo.a
STDLIB_FLAGS ?= # Additional flags for stdlib compilation (e.g., --debug --verbose)

# Find all stdlib source files - using dynamic discovery for both Windows and Linux
# Exclude the runtime directory from stdlib compilation
ifeq ($(OS), Windows_NT)
    # Use MSYS2 find for reliable file discovery on Windows
    STDLIB_SRCS := $(shell C:/msys64/usr/bin/find $(STDLIB_DIR) -name "*.cryo" -type f ! -path "*/test-cases/*" ! -path "*/runtime/*" | sed 's|$(STDLIB_DIR)/||')
else
    STDLIB_SRCS := $(shell find $(STDLIB_DIR) -name "*.cryo" -type f | grep -v test-cases | grep -v runtime | sed 's|$(STDLIB_DIR)/||')
endif

# Generate corresponding bitcode files
STDLIB_BC_FILES := $(patsubst %.cryo,$(STDLIB_BUILD_DIR)/%.bc,$(STDLIB_SRCS))

# Runtime compilation
RUNTIME_DIR = ./stdlib/runtime
RUNTIME_BUILD_DIR = $(BIN_DIR)stdlib/runtime
RUNTIME_LIB = $(BIN_DIR)stdlib/runtime.a

# Find all runtime source files
ifeq ($(OS), Windows_NT)
    # Use MSYS2 find for reliable file discovery on Windows (handle missing dir gracefully)
    RUNTIME_SRCS := $(shell C:/msys64/usr/bin/find $(RUNTIME_DIR) -name "*.cryo" -type f 2>/dev/null | sed 's|$(RUNTIME_DIR)/||')
else
    RUNTIME_SRCS := $(shell find $(RUNTIME_DIR) -name "*.cryo" -type f 2>/dev/null | sed 's|$(RUNTIME_DIR)/||')
endif

# Generate corresponding bitcode files
RUNTIME_BC_FILES := $(patsubst %.cryo,$(RUNTIME_BUILD_DIR)/%.bc,$(RUNTIME_SRCS))

.PHONY: all
all: 
	@$(MAKE) timed-build
#	@$(MAKE) stdlib
#	@$(MAKE) tools

run: $(MAIN_BIN)
	@$(MAIN_BIN)
	
.PHONY: build
build: $(MAIN_BIN)

# Clean all components
clean:
	@$(PYTHON) ./scripts/clean.py
ifeq ($(OS), Windows_NT)
	@$(MAKE) -C tools/CryoLSP clean || cd .
	@if exist "$(subst /,\,$(STDLIB_BUILD_DIR))" rmdir /s /q "$(subst /,\,$(STDLIB_BUILD_DIR))"
	@if exist "$(subst /,\,$(RUNTIME_BUILD_DIR))" rmdir /s /q "$(subst /,\,$(RUNTIME_BUILD_DIR))"
else
	@$(MAKE) -C tools/CryoLSP clean 2>/dev/null || true
	@rm -rf $(STDLIB_BUILD_DIR)
	@rm -rf $(RUNTIME_BUILD_DIR)
endif

# =================================================================
# STANDARD LIBRARY COMPILATION
# =================================================================

.PHONY: stdlib stdlib-clean
stdlib: 
	@$(MAKE) stdlib-clean
	@$(MAKE) $(STDLIB_LIB)

$(STDLIB_BUILD_DIR):
ifeq ($(OS), Windows_NT)
	@if not exist "$(subst /,\,$@)" mkdir "$(subst /,\,$@)"
	@if not exist "$(subst /,\,$@)\core" mkdir "$(subst /,\,$@)\core"
	@if not exist "$(subst /,\,$@)\io" mkdir "$(subst /,\,$@)\io"
	@if not exist "$(subst /,\,$@)\strings" mkdir "$(subst /,\,$@)\strings"
	@if not exist "$(subst /,\,$@)\collections" mkdir "$(subst /,\,$@)\collections"
	@if not exist "$(subst /,\,$@)\net" mkdir "$(subst /,\,$@)\net"
else
	@mkdir -p $@
	@mkdir -p $@/core $@/io $@/strings $@/collections $@/net
endif

# Use Python script to build the standard library
$(STDLIB_LIB): $(MAIN_BIN) | $(STDLIB_BUILD_DIR)
ifeq ($(OS), Windows_NT)
	@python scripts\build-stdlib.py .\bin\cryo.exe $(STDLIB_DIR) $(STDLIB_BUILD_DIR) $(STDLIB_LIB) $(STDLIB_FLAGS)
else
	@python3 scripts/build-stdlib.py $(MAIN_BIN) $(STDLIB_DIR) $(STDLIB_BUILD_DIR) $(STDLIB_LIB) $(STDLIB_FLAGS)
endif

stdlib-clean:
ifeq ($(OS), Windows_NT)
	@if exist "$(subst /,\,$(STDLIB_BUILD_DIR))" rmdir /s /q "$(subst /,\,$(STDLIB_BUILD_DIR))"
else
	@rm -rf $(STDLIB_BUILD_DIR)
endif

# stdlib individual module targets for debugging

stdlib-intrinsics:
	@./bin/cryo${BIN_SUFFIX} $(STDLIB_DIR)/core/intrinsics.cryo --debug --emit-llvm -c --stdlib-mode -o $(STDLIB_BUILD_DIR)/core/intrinsics.bc

stdlib-types:
	@./bin/cryo${BIN_SUFFIX} $(STDLIB_DIR)/core/types.cryo --debug --emit-llvm -c --stdlib-mode -o $(STDLIB_BUILD_DIR)/core/types.bc

stdlib-stdio:
	@./bin/cryo${BIN_SUFFIX} $(STDLIB_DIR)/io/stdio.cryo --emit-llvm -c --stdlib-mode --ir -o $(STDLIB_BUILD_DIR)/io/stdio.bc

stdlib-memory:
	@./bin/cryo${BIN_SUFFIX} $(STDLIB_DIR)/core/memory.cryo --emit-llvm -c --stdlib-mode --ir -o $(STDLIB_BUILD_DIR)/core/memory.bc

stdlib-http:
	@./bin/cryo${BIN_SUFFIX} $(STDLIB_DIR)/net/http.cryo --debug --emit-llvm -c --stdlib-mode --ir -o $(STDLIB_BUILD_DIR)/net/http.bc

stdlib-tcp:
	@./bin/cryo${BIN_SUFFIX} $(STDLIB_DIR)/net/tcp.cryo --debug --emit-llvm -c --stdlib-mode --ir -o $(STDLIB_BUILD_DIR)/net/tcp.bc

stdlib-signal:
	@./bin/cryo${BIN_SUFFIX} $(STDLIB_DIR)/process/signal.cryo --debug --emit-llvm -c --stdlib-mode --ir -o $(STDLIB_BUILD_DIR)/process/signal.bc

stdlib-hashmap:
	@./bin/cryo${BIN_SUFFIX} $(STDLIB_DIR)/collections/hashmap.cryo --debug --emit-llvm -c --stdlib-mode --ir -o $(STDLIB_BUILD_DIR)/collections/hashmap.bc

stdlib-pair:
	@./bin/cryo${BIN_SUFFIX} $(STDLIB_DIR)/collections/pair.cryo --debug --emit-llvm -c --stdlib-mode --ir -o $(STDLIB_BUILD_DIR)/collections/pair.bc

stdlib-env:
	@./bin/cryo${BIN_SUFFIX} $(STDLIB_DIR)/env/env.cryo --debug --emit-llvm -c --stdlib-mode --ir -o $(STDLIB_BUILD_DIR)/env/env.bc

stdlib-strings:
	@./bin/cryo${BIN_SUFFIX} $(STDLIB_DIR)/strings/strings.cryo --debug --emit-llvm -c --stdlib-mode --ir -o $(STDLIB_BUILD_DIR)/strings/strings.bc

stdlib-tcp-gdb:
	@echo "Building stdlib TCP module through gdb for debugging..."
	@gdb -batch \
		-ex "run" \
		-ex "thread apply all bt full" \
		-ex "info registers" \
		-ex "quit" \
		--args $(MAIN_BIN) $(STDLIB_DIR)/net/tcp.cryo --debug --emit-llvm -c --stdlib-mode --ir -o $(STDLIB_BUILD_DIR)/net/tcp.bc

gdb-runtime:
	@gdb --batch --ex "handle SIGTRAP stop print" --ex run --ex "bt full" --ex "info registers" --ex "x/10i $rip" --ex quit --args $(MAIN_BIN) $(RUNTIME_DIR)/runtime.cryo --debug --emit-llvm -c --stdlib-mode --ir -o $(RUNTIME_BUILD_DIR)/runtime.bc

ast-runtime:
	@echo "Building runtime with AST dump for debugging..."
	@$(MAIN_BIN) $(RUNTIME_DIR)/runtime.cryo --ast -c --stdlib-mode -o $(RUNTIME_BUILD_DIR)/runtime.bc

# runtime: 
# ifeq ($(OS), Windows_NT)
# 	@if exist "$(subst /,\,$(RUNTIME_BUILD_DIR))" rmdir /s /q "$(subst /,\,$(RUNTIME_BUILD_DIR))"
# else
# 	@if [ -d "$(RUNTIME_BUILD_DIR)" ]; then \
# 		echo "Cleaning existing runtime build directory..."; \
# 		rm -rf $(RUNTIME_BUILD_DIR); \
# 	fi
# endif
# 	@$(MAKE) $(RUNTIME_LIB)

# $(RUNTIME_BUILD_DIR):
# ifeq ($(OS), Windows_NT)
# 	@if not exist "$(subst /,\,$@)" mkdir "$(subst /,\,$@)"
# else
# 	@mkdir -p $@
# endif

# $(RUNTIME_BUILD_DIR)/%.bc: $(RUNTIME_DIR)/%.cryo $(MAIN_BIN) | $(RUNTIME_BUILD_DIR)
# 	@echo "Compiling runtime module: $(RUNTIME_DIR)/$*.cryo"
# ifeq ($(OS), Windows_NT)
# 	@if not exist "$(subst /,\,$(dir $@))" mkdir "$(subst /,\,$(dir $@))"
# 	@echo "[RUNTIME] Generating IR and dumping to console for $(RUNTIME_DIR)/$*.cryo"
# 	@.\bin\cryo.exe $(RUNTIME_DIR)/$*.cryo --emit-llvm -c --stdlib-mode --debug --ir -o $(RUNTIME_BUILD_DIR)/$*.bc || ( \
# 		echo "[RUNTIME] Compilation failed for $*.cryo" && \
# 		exit 1 \
# 	)
# else
# 	@mkdir -p $(dir $@)
# 	@echo "[RUNTIME] Generating IR and dumping to console for $(RUNTIME_DIR)/$*.cryo"
# 	@$(MAIN_BIN) $(RUNTIME_DIR)/$*.cryo --emit-llvm -c --stdlib-mode --debug -o $(shell pwd)/$@ || ( \
# 		echo "[RUNTIME] Compilation failed for $*.cryo" && \
# 		exit 1 \
# 	)
# endif

# Link all runtime modules into a single library
# $(RUNTIME_LIB): $(RUNTIME_BC_FILES)
# 	@echo "Creating runtime library: $(RUNTIME_LIB)"
# 	@llvm-link $(RUNTIME_BC_FILES) -o $(RUNTIME_BUILD_DIR)/runtime_combined.bc
# ifeq ($(OS), Windows_NT)
# 	@llc -filetype=obj $(RUNTIME_BUILD_DIR)/runtime_combined.bc -o $(BIN_DIR)stdlib/runtime.o
# else
# 	@llc -filetype=obj -relocation-model=pic $(RUNTIME_BUILD_DIR)/runtime_combined.bc -o $(BIN_DIR)stdlib/runtime.o
# endif
# 	@llvm-ar rcs $(RUNTIME_LIB) $(BIN_DIR)stdlib/runtime.o
# 	@echo -e "Runtime library created successfully: $(RUNTIME_LIB)"

# Create runtime object file directly (used for linking main executable)
# $(BIN_DIR)stdlib/runtime.o: $(RUNTIME_BC_FILES)
# 	@echo "Creating runtime object: $(BIN_DIR)stdlib/runtime.o"
# 	@llvm-link $(RUNTIME_BC_FILES) -o $(RUNTIME_BUILD_DIR)/runtime_combined.bc
# ifeq ($(OS), Windows_NT)
# 	@llc -filetype=obj $(RUNTIME_BUILD_DIR)/runtime_combined.bc -o $(BIN_DIR)stdlib/runtime.o
# else
# 	@llc -filetype=obj -relocation-model=pic $(RUNTIME_BUILD_DIR)/runtime_combined.bc -o $(BIN_DIR)stdlib/runtime.o
# endif
# 	@echo "Runtime object created successfully: $(BIN_DIR)stdlib/runtime.o"


.PHONY: debug clean all lsp tools test test-tier1 test-tier2 test-negative test-verbose test-list test-clean
.NOTPARALLEL: clean clean-% libs

# Help command
help:
	@echo "CryoLang Makefile Commands:"
	@echo "  make all            - Build the Cryo compiler"
	@echo "  make run            - Run the Cryo compiler"
	@echo "  make rebuild        - Clean and rebuild the Cryo compiler"
	@echo "  make timed-build    - Build with timing information"
	@echo "  make stdlib         - Build the standard library"
	@echo "  make clean          - Clean all build artifacts"
	@echo "  make tools          - Build all Cryo tools (e.g., CryoLSP)"
	@echo "  make lsp            - Build the Cryo Language Server (CryoLSP)"
	@echo "  make compiler-lib   - Build the Cryo compiler library for LSP integration"
	@echo "  make runtime        - Build the runtime library"
	@echo "  make runtime-clean  - Clean runtime build artifacts"
	@echo "  make test           - Run the full E2E test suite"
	@echo "  make test-tier1     - Run tier 1 (core language) tests"
	@echo "  make test-tier2     - Run tier 2 (type system) tests"
	@echo "  make test-negative  - Run negative (expected failure) tests"
	@echo "  make test-verbose   - Run tests with verbose output"
	@echo "  make test-list      - List all discovered tests"
	@echo "  make test-clean     - Clean test build artifacts"

# ============================================================
# Test Targets
# ============================================================
PYTHON ?= python3
TEST_BUILD_DIR = tests/.build

test: $(MAIN_BIN)
	@$(PYTHON) tests/run_tests.py --cryo $(MAIN_BIN) --build-dir $(TEST_BUILD_DIR)

test-tier1: $(MAIN_BIN)
	@$(PYTHON) tests/run_tests.py --cryo $(MAIN_BIN) --build-dir $(TEST_BUILD_DIR) --tier 1

test-tier2: $(MAIN_BIN)
	@$(PYTHON) tests/run_tests.py --cryo $(MAIN_BIN) --build-dir $(TEST_BUILD_DIR) --tier 2

test-negative: $(MAIN_BIN)
	@$(PYTHON) tests/run_tests.py --cryo $(MAIN_BIN) --build-dir $(TEST_BUILD_DIR) --category negative

test-verbose: $(MAIN_BIN)
	@$(PYTHON) tests/run_tests.py --cryo $(MAIN_BIN) --build-dir $(TEST_BUILD_DIR) --verbose

test-list:
	@$(PYTHON) tests/run_tests.py --list

test-clean:
	@rm -rf $(TEST_BUILD_DIR)
