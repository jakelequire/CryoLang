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
    C_SRCS := $(shell C:/msys64/usr/bin/find $(SRC_DIR) -name "*.c" -type f)
    CPP_SRCS := $(shell C:/msys64/usr/bin/find $(SRC_DIR) -name "*.cpp" -type f)
else
    # Linux - native find
    C_SRCS := $(shell find $(SRC_DIR) -name "*.c" -type f)
    CPP_SRCS := $(shell find $(SRC_DIR) -name "*.cpp" -type f)
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

# >>=======--------------------------------------------------=======<< #
# >>=======                     Commands                     =======<< #
# >>=======--------------------------------------------------=======<< #

# Build timer script
BUILD_TIMER = ./scripts/build_timer.py

# >>=======--------------------------------------------------=======<< #
# >>=======                    TOOLS                         =======<< #
# >>=======--------------------------------------------------=======<< #

# Tools targets - delegate to individual tool makefiles
.PHONY: tools lsp
tools: lsp

lsp: $(MAIN_BIN)
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
STDLIB_LIB = $(STDLIB_BUILD_DIR)/libcryostd.a

# Find all stdlib source files - using explicit list for Windows
ifeq ($(OS), Windows_NT)
    STDLIB_SRCS := core/types.cryo core/intrinsics.cryo core/syscall.cryo io/stdio.cryo strings/strings.cryo collections/array.cryo
else
    STDLIB_SRCS := $(shell find $(STDLIB_DIR) -name "*.cryo" -type f | grep -v test-cases | sed 's|$(STDLIB_DIR)/||')
endif

# Generate corresponding bitcode files
STDLIB_BC_FILES := $(patsubst %.cryo,$(STDLIB_BUILD_DIR)/%.bc,$(STDLIB_SRCS))

.PHONY: all
all: 
	@$(MAKE) timed-build
	@$(MAKE) stdlib
	@$(MAKE) tools

run: $(MAIN_BIN)
	@$(MAIN_BIN)
	
.PHONY: build
build: $(MAIN_BIN)

# Clean all components
clean:
	@$(PYTHON) ./scripts/clean.py
	@$(MAKE) -C tools/CryoLSP clean 2>/dev/null || true
ifeq ($(OS), Windows_NT)
	@if exist "$(subst /,\,$(STDLIB_BUILD_DIR))" rmdir /s /q "$(subst /,\,$(STDLIB_BUILD_DIR))"
else
	@rm -rf $(STDLIB_BUILD_DIR)
endif

# =================================================================
# STANDARD LIBRARY COMPILATION
# =================================================================

.PHONY: stdlib stdlib-clean
stdlib: $(STDLIB_LIB)

$(STDLIB_BUILD_DIR):
ifeq ($(OS), Windows_NT)
	@if not exist "$(subst /,\,$@)" mkdir "$(subst /,\,$@)"
	@if not exist "$(subst /,\,$@)\core" mkdir "$(subst /,\,$@)\core"
	@if not exist "$(subst /,\,$@)\io" mkdir "$(subst /,\,$@)\io"
	@if not exist "$(subst /,\,$@)\strings" mkdir "$(subst /,\,$@)\strings"
	@if not exist "$(subst /,\,$@)\collections" mkdir "$(subst /,\,$@)\collections"
else
	@mkdir -p $@
	@mkdir -p $@/core $@/io $@/strings $@/collections
endif

# Compile individual stdlib modules to LLVM bitcode
$(STDLIB_BUILD_DIR)/%.bc: $(STDLIB_DIR)/%.cryo $(MAIN_BIN) | $(STDLIB_BUILD_DIR)
	@echo "Compiling stdlib module: $(STDLIB_DIR)/$*.cryo"
ifeq ($(OS), Windows_NT)
	@if not exist "$(subst /,\,$(dir $@))" mkdir "$(subst /,\,$(dir $@))"
	@$(MAIN_BIN) $(STDLIB_DIR)/$*.cryo --emit-llvm -c -o $(STDLIB_BUILD_DIR)/$*.bc
else
	@mkdir -p $(dir $@)
	@$(MAIN_BIN) $(STDLIB_DIR)/$*.cryo --emit-llvm -c -o $(shell pwd)/$@
endif

# Link all stdlib modules into a single library
$(STDLIB_LIB): $(STDLIB_BC_FILES)
	@echo "Creating standard library: $(STDLIB_LIB)"
ifeq ($(OS), Windows_NT)
	@llvm-link $(STDLIB_BC_FILES) -o $(STDLIB_BUILD_DIR)/cryostd_combined.bc
	@llc -filetype=obj $(STDLIB_BUILD_DIR)/cryostd_combined.bc -o $(STDLIB_BUILD_DIR)/libcryostd.o
	@lib /OUT:$(STDLIB_LIB) $(STDLIB_BUILD_DIR)/libcryostd.o
else
	@llvm-link $(STDLIB_BC_FILES) -o $(STDLIB_BUILD_DIR)/cryostd_combined.bc
	@llc -filetype=obj $(STDLIB_BUILD_DIR)/cryostd_combined.bc -o $(STDLIB_BUILD_DIR)/libcryostd.o
	@ar rcs $(STDLIB_LIB) $(STDLIB_BUILD_DIR)/libcryostd.o
endif
	@echo "✅ Standard library created: $(STDLIB_LIB)"

stdlib-clean:
ifeq ($(OS), Windows_NT)
	@if exist "$(subst /,\,$(STDLIB_BUILD_DIR))" rmdir /s /q "$(subst /,\,$(STDLIB_BUILD_DIR))"
else
	@rm -rf $(STDLIB_BUILD_DIR)
endif

# Test targets
.PHONY: test test-quick test-verbose test-category test-file
test: $(MAIN_BIN)
	@echo "Running CryoLang test suite..."
	@$(PYTHON) test/test_runner.py

test-quick: $(MAIN_BIN)
	@echo "Running CryoLang test suite (fail-fast mode)..."
	@$(PYTHON) test/test_runner.py --fail-fast

test-verbose: $(MAIN_BIN)
	@echo "Running CryoLang test suite (verbose)..."
	@$(PYTHON) test/test_runner.py --verbose --show-failures

test-category: $(MAIN_BIN)
	@echo "Usage: make test-category CATEGORIES='functions control-flow'"
	@echo "Available categories: functions variables control-flow data-types generics memory"
ifdef CATEGORIES
	@$(PYTHON) test/test_runner.py --categories $(CATEGORIES)
else
	@echo "Please specify CATEGORIES variable"
endif

test-file: $(MAIN_BIN)
	@echo "Usage: make test-file FILE='test/test-cases/functions/basic_functions.cryo'"
ifdef FILE
	@$(PYTHON) test/test_runner.py --file $(FILE)
else
	@echo "Please specify FILE variable"
endif

.PHONY: debug clean all lsp tools test test-quick test-verbose test-category test-file
.NOTPARALLEL: clean clean-% libs
