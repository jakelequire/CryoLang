# CryoLSP Server Makefile
# Language Server Protocol implementation for CryoLang

# Compiler settings
CXX = C:/msys64/mingw64/bin/clang++
CXXFLAGS = -std=c++23 -D_CRT_SECURE_NO_WARNINGS -DNOMINMAX -DWIN32_LEAN_AND_MEAN -w -g -Og -I"C:/msys64/mingw64/include" -fexceptions
INCLUDES = -I../../include -I./include
LIBS = -L../../bin -lcryo-compiler -LC:/msys64/mingw64/lib "C:/msys64/mingw64/lib/libLLVM-20.dll.a" -lws2_32

# Directories
SRC_DIR = src
INCLUDE_DIR = include
BUILD_DIR = ../../bin/.o/lsp
TARGET_DIR = ../../bin
TARGET = $(TARGET_DIR)/cryolsp.exe

# Source files
SOURCES = $(wildcard $(SRC_DIR)/*.cpp)
OBJECTS = $(SOURCES:$(SRC_DIR)/%.cpp=$(BUILD_DIR)/%.o)

.PHONY: all clean

# Default target
all: $(TARGET)

# Create build directory
$(BUILD_DIR):
	@if not exist "$(BUILD_DIR)" mkdir "$(BUILD_DIR)"

# Compile source files
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

# Link executable
$(TARGET): $(OBJECTS)
	$(CXX) $(OBJECTS) $(LIBS) -o $@
	@echo "✓ CryoLSP server built successfully: $(TARGET)"

# Clean build artifacts
clean:
	@if exist "$(BUILD_DIR)" rmdir /s /q "$(BUILD_DIR)"
	@if exist "$(TARGET)" del /f "$(TARGET)"
	@echo "✓ Cleaned CryoLSP build artifacts"

# Debug target with verbose output
debug: CXXFLAGS += -DDEBUG -v
debug: $(TARGET)

# Install target (copy to system path if needed)
install: $(TARGET)
	@echo "CryoLSP server is ready at: $(TARGET)"