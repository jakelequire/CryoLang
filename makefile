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

# --------------------------------------------- #
# `-O0`    - No optimization					#
# `-O1`    - Basic optimization					#
# `-O2`    - Further optimization				#
# `-O3`    - Maximum optimization				#
# `-Og`    - Optimize debugging experience		#
# `-Os`    - Optimize for size					#
# `-Ofast` - Optimize for speed					#
# `-Oz`    - Optimize for size					#
# --------------------------------------------- #
OPTIMIZATION =  -Og
NO_WARNINGS =   -w
DEBUG_FLAGS =   -D_CRT_SECURE_NO_WARNINGS  $(NO_WARNINGS)
C_STANDARD =    -std=c23
CXX_STANDARD =  -std=c++23

# Determine number of CPU cores
ifeq ($(OS), Windows_NT)
    NUM_CORES = $(NUMBER_OF_PROCESSORS)
else
    NUM_CORES = $(shell nproc)
endif

NUM_JOBS = $(shell expr $(NUM_CORES) + 1)

# OS-specific settings for compilers
ifeq ($(OS), Windows_NT)
# Windows settings
	C_COMPILER = C:/msys64/mingw64/bin/gcc
	CXX_COMPILER = C:/msys64/mingw64/bin/g++
else
# Linux settings
	C_COMPILER = clang-23
	CXX_COMPILER = clang++-23
endif

# >>=======--------------------------------------------------=======<< #
# >>=======                 Include Paths                    =======<< #
# >>=======--------------------------------------------------=======<< #

# Include paths
LINUX_INCLUDES =    -I./include

WIN_INCLUDES =      -I"C:/msys64/mingw64/include" -I./include

# >>=======--------------------------------------------------=======<< #
# >>=======       OS-specific settings for compilers         =======<< #
# >>=======--------------------------------------------------=======<< #

# OS-specific settings
ifeq ($(OS), Windows_NT)
    # Windows settings
    CC =            $(C_COMPILER) $(C_STANDARD) $(DEBUG_FLAGS) $(OPTIMIZATION)
    CXX =           $(CXX_COMPILER) $(CXX_STANDARD) $(DEBUG_FLAGS) $(OPTIMIZATION)
    CFLAGS =        $(WIN_INCLUDES) $(LLVM_CFLAGS) -fexceptions
    CXXFLAGS =      $(WIN_INCLUDES) $(LLVM_CXXFLAGS) -fexceptions
    LDFLAGS =       -L"C:/msys64/mingw64/lib" $(LLVM_LIBS) $(STDLIBS) -v
    LLVM_LIBS :=    -lLLVM 
    STDLIBS :=      -lmingw32 -lmingwex -lmsvcrt -lucrt -lpthread -lws2_32 -ladvapi32 -lshell32 -luser32 -lkernel32 -Wl,-subsystem,console
    MKDIR =         if not exist
    RMDIR =         rmdir /S /Q
    DEL =           del /Q
    BIN_SUFFIX =    .exe
else
    # Linux settings
    CC =            $(C_COMPILER) $(C_STANDARD) $(DEBUG_FLAGS) $(OPTIMIZATION)
    CXX =           $(CXX_COMPILER) $(CXX_STANDARD) $(DEBUG_FLAGS) $(OPTIMIZATION)
    CFLAGS =        $(LINUX_INCLUDES) $(LLVM_CFLAGS) -fexceptions
    CXXFLAGS =      $(LINUX_INCLUDES) $(LLVM_CXXFLAGS) -fexceptions
    LLVM_CONFIG =   llvm-config-20
    LLVM_CFLAGS =   $(shell $(LLVM_CONFIG) --cflags)
	LLVM_CXXFLAGS = $(shell $(LLVM_CONFIG) --cxxflags)
    LLVM_LDFLAGS =  $(shell $(LLVM_CONFIG) --ldflags) $(shell $(LLVM_CONFIG) --libs) $(shell $(LLVM_CONFIG) --system-libs)
    LDFLAGS =       $(LLVM_LDFLAGS) -lpthread -v
	STD_LIBS =      -lstdc++ -lm -lc -lgcc -lgcc_eh -lstdc++fs
    MKDIR =         mkdir -p
    RMDIR =         rm -rf
    DEL =           rm -f
    BIN_SUFFIX =
endif

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
MAIN_BIN = $(BIN_DIR)compiler$(BIN_SUFFIX)

# ---------------------------------------------
# Ensure OBJ_DIR exists
$(shell mkdir -p $(OBJ_DIR))

# ---------------------------------------------
# Define all source files
C_SRCS := $(shell find $(SRC_DIR) -name '*.c')
CPP_SRCS := $(shell find $(SRC_DIR) -name '*.cpp')

# ---------------------------------------------
# Define all object files
C_OBJS := $(patsubst $(SRC_DIR)%.c,$(OBJ_DIR)%.o,$(C_SRCS))
CPP_OBJS := $(patsubst $(SRC_DIR)%.cpp,$(OBJ_DIR)%.o,$(CPP_SRCS))

# ---------------------------------------------
# Combine all object files
ALL_OBJS := $(C_OBJS) $(CPP_OBJS)

# Add these directory rules
$(BIN_DIR) $(OBJ_DIR):
	@mkdir -p $@

# ---------------------------------------------
# Compile C source files
$(OBJ_DIR)%.o: $(SRC_DIR)%.c | $(OBJ_DIR)
	@mkdir -p $(dir $@)
	$(CXX) $(CFLAGS) -c $< -o $@

# ---------------------------------------------
# Compile C++ source files
$(OBJ_DIR)%.o: $(SRC_DIR)%.cpp | $(OBJ_DIR)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# >>=======--------------------------------------------------=======<< #
# >>=======                   Link Binaries                  =======<< #
# >>=======--------------------------------------------------=======<< #

# Main target
$(MAIN_BIN): $(ALL_OBJS)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(ALL_OBJS) -o $@ $(LDFLAGS)

# >>=======--------------------------------------------------=======<< #
# >>=======                     Commands                     =======<< #
# >>=======--------------------------------------------------=======<< #

# Build timer script
BUILD_TIMER = ./scripts/build_timer.py

# Timed build target
.PHONY: timed-build
timed-build:
	@python3 $(BUILD_TIMER)

# Clean and timed build
.PHONY: rebuild
rebuild:
	@python3 $(BUILD_TIMER) --clean

.PHONY: all
all: 
	@$(MAKE) timed-build
	
.PHONY: build
build: $(MAIN_BIN)

# Clean all components
clean:
	python3 ./scripts/clean.py

.PHONY: debug clean all 
.NOTPARALLEL: clean clean-% libs
