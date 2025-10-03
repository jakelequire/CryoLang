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
STDLIB_LIB = $(STDLIB_BUILD_DIR)/libcryo.a

# Find all stdlib source files - using dynamic discovery for both Windows and Linux
# Exclude the runtime directory from stdlib compilation
ifeq ($(OS), Windows_NT)
    # Use PowerShell to find all .cryo files recursively, excluding runtime directory
    STDLIB_SRCS := $(shell powershell -Command "Get-ChildItem -Path './stdlib' -Recurse -Filter '*.cryo' | Where-Object { $$_.FullName -notlike '*runtime*' } | ForEach-Object { $$_.FullName.Replace('$(shell powershell -Command "(Get-Location).Path")', '.').Replace('\', '/') } | ForEach-Object { $$_.Replace('./stdlib/', '') }")
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
    # Use PowerShell to find all .cryo files in runtime directory
    RUNTIME_SRCS := $(shell powershell -Command "Get-ChildItem -Path './stdlib/runtime' -Filter '*.cryo' | ForEach-Object { $$_.Name }")
else
    RUNTIME_SRCS := $(shell find $(RUNTIME_DIR) -name "*.cryo" -type f | sed 's|$(RUNTIME_DIR)/||')
endif

# Generate corresponding bitcode files
RUNTIME_BC_FILES := $(patsubst %.cryo,$(RUNTIME_BUILD_DIR)/%.bc,$(RUNTIME_SRCS))

.PHONY: all
all: 
	@$(MAKE) timed-build
	@$(MAKE) stdlib
	@$(MAKE) runtime
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
	@if exist "$(subst /,\,$(RUNTIME_BUILD_DIR))" rmdir /s /q "$(subst /,\,$(RUNTIME_BUILD_DIR))"
else
	@rm -rf $(STDLIB_BUILD_DIR)
	@rm -rf $(RUNTIME_BUILD_DIR)
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
	@echo "[STDLIB] Generating IR and dumping to console for $(STDLIB_DIR)/$*.cryo"
	@.\bin\cryo.exe $(STDLIB_DIR)/$*.cryo --emit-llvm -c --stdlib-mode -o $(STDLIB_BUILD_DIR)/$*.bc || ( \
		echo "[STDLIB] Compilation failed, creating stub file..." && \
		echo "; Compilation failed for $*.cryo" > $(STDLIB_BUILD_DIR)/$*.bc && \
		echo "; Stub file created to satisfy build system" >> $(STDLIB_BUILD_DIR)/$*.bc \
	)
else
	@mkdir -p $(dir $@)
	@echo "[STDLIB] Generating IR and dumping to console for $(STDLIB_DIR)/$*.cryo"
	@$(MAIN_BIN) $(STDLIB_DIR)/$*.cryo --emit-llvm -c --stdlib-mode -o $(shell pwd)/$@ || ( \
		echo "[STDLIB] Compilation failed, creating stub file..." && \
		echo "; Compilation failed for $*.cryo" > $@ && \
		echo "; Stub file created to satisfy build system" >> $@ \
	)
endif

# Link all stdlib modules into a single library
$(STDLIB_LIB): $(STDLIB_BC_FILES)
	@echo "Creating standard library: $(STDLIB_LIB)"
	@llvm-link $(STDLIB_BC_FILES) -o $(STDLIB_BUILD_DIR)/cryo_combined.bc
ifeq ($(OS), Windows_NT)
	@llc -filetype=obj $(STDLIB_BUILD_DIR)/cryo_combined.bc -o $(STDLIB_BUILD_DIR)/libcryo.o
else
	@llc -filetype=obj -relocation-model=pic $(STDLIB_BUILD_DIR)/cryo_combined.bc -o $(STDLIB_BUILD_DIR)/libcryo.o
endif
	@llvm-ar rcs $(STDLIB_LIB) $(STDLIB_BUILD_DIR)/libcryo.o
	@echo "Standard library created Successfully: $(STDLIB_LIB)"

stdlib-clean:
ifeq ($(OS), Windows_NT)
	@if exist "$(subst /,\,$(STDLIB_BUILD_DIR))" rmdir /s /q "$(subst /,\,$(STDLIB_BUILD_DIR))"
else
	@rm -rf $(STDLIB_BUILD_DIR)
endif

stdlib-types:
	@./bin/cryo${BIN_SUFFIX} $(STDLIB_DIR)/core/types.cryo --emit-llvm -c --stdlib-mode -o $(STDLIB_BUILD_DIR)/core/types.bc

stdlib-stdio:
	@./bin/cryo${BIN_SUFFIX} $(STDLIB_DIR)/io/stdio.cryo --emit-llvm -c --stdlib-mode --ir -o $(STDLIB_BUILD_DIR)/io/stdio.bc

stdlib-memory:
	@./bin/cryo${BIN_SUFFIX} $(STDLIB_DIR)/core/memory.cryo --emit-llvm -c --stdlib-mode --ir -o $(STDLIB_BUILD_DIR)/core/memory.bc

# =================================================================
# RUNTIME COMPILATION
# =================================================================

.PHONY: runtime runtime-clean
runtime: $(RUNTIME_LIB)

$(RUNTIME_BUILD_DIR):
ifeq ($(OS), Windows_NT)
	@if not exist "$(subst /,\,$@)" mkdir "$(subst /,\,$@)"
else
	@mkdir -p $@
endif

# Compile individual runtime modules to LLVM bitcode
$(RUNTIME_BUILD_DIR)/%.bc: $(RUNTIME_DIR)/%.cryo $(MAIN_BIN) | $(RUNTIME_BUILD_DIR)
	@echo "Compiling runtime module: $(RUNTIME_DIR)/$*.cryo"
ifeq ($(OS), Windows_NT)
	@if not exist "$(subst /,\,$(dir $@))" mkdir "$(subst /,\,$(dir $@))"
	@echo "[RUNTIME] Generating IR and dumping to console for $(RUNTIME_DIR)/$*.cryo"
	@.\bin\cryo.exe $(RUNTIME_DIR)/$*.cryo --emit-llvm -c --stdlib-mode --ir -o $(RUNTIME_BUILD_DIR)/$*.bc || ( \
		echo "[RUNTIME] Compilation failed, creating stub file..." && \
		echo "; Compilation failed for $*.cryo" > $(RUNTIME_BUILD_DIR)/$*.bc && \
		echo "; Stub file created to satisfy build system" >> $(RUNTIME_BUILD_DIR)/$*.bc \
	)
else
	@mkdir -p $(dir $@)
	@echo "[RUNTIME] Generating IR and dumping to console for $(RUNTIME_DIR)/$*.cryo"
	@$(MAIN_BIN) $(RUNTIME_DIR)/$*.cryo --emit-llvm -c --stdlib-mode -o $(shell pwd)/$@ || ( \
		echo "[RUNTIME] Compilation failed, creating stub file..." && \
		echo "; Compilation failed for $*.cryo" > $@ && \
		echo "; Stub file created to satisfy build system" >> $@ \
	)
endif

# Link all runtime modules into a single library
$(RUNTIME_LIB): $(RUNTIME_BC_FILES)
	@echo "Creating runtime library: $(RUNTIME_LIB)"
	@llvm-link $(RUNTIME_BC_FILES) -o $(RUNTIME_BUILD_DIR)/runtime_combined.bc
ifeq ($(OS), Windows_NT)
	@llc -filetype=obj $(RUNTIME_BUILD_DIR)/runtime_combined.bc -o $(BIN_DIR)stdlib/runtime.o
else
	@llc -filetype=obj -relocation-model=pic $(RUNTIME_BUILD_DIR)/runtime_combined.bc -o $(BIN_DIR)stdlib/runtime.o
endif
	@llvm-ar rcs $(RUNTIME_LIB) $(BIN_DIR)stdlib/runtime.o
	@echo "Runtime library created successfully: $(RUNTIME_LIB)"

# Create runtime object file directly (used for linking main executable)
$(BIN_DIR)stdlib/runtime.o: $(RUNTIME_BC_FILES)
	@echo "Creating runtime object: $(BIN_DIR)stdlib/runtime.o"
	@llvm-link $(RUNTIME_BC_FILES) -o $(RUNTIME_BUILD_DIR)/runtime_combined.bc
ifeq ($(OS), Windows_NT)
	@llc -filetype=obj $(RUNTIME_BUILD_DIR)/runtime_combined.bc -o $(BIN_DIR)stdlib/runtime.o
else
	@llc -filetype=obj -relocation-model=pic $(RUNTIME_BUILD_DIR)/runtime_combined.bc -o $(BIN_DIR)stdlib/runtime.o
endif
	@echo "Runtime object created successfully: $(BIN_DIR)stdlib/runtime.o"

runtime-clean:
ifeq ($(OS), Windows_NT)
	@if exist "$(subst /,\,$(RUNTIME_BUILD_DIR))" rmdir /s /q "$(subst /,\,$(RUNTIME_BUILD_DIR))"
else
	@rm -rf $(RUNTIME_BUILD_DIR)
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

.PHONY: debug clean all lsp tools test test-quick test-verbose test-category test-file runtime runtime-clean
.NOTPARALLEL: clean clean-% libs
