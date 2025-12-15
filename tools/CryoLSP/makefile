# CryoLSP Makefile
# Simple Language Server Protocol implementation for CryoLang

# Include shared configuration from parent
include ../../makefile.config

# Directories
SRCDIR = src
BUILDDIR = build
BINDIR = bin
TARGET = $(BINDIR)/cryolsp

# Source files
SOURCES = $(wildcard $(SRCDIR)/*.cpp)
OBJECTS = $(SOURCES:$(SRCDIR)/%.cpp=$(BUILDDIR)/%.o)

# Get LLVM configuration
LLVM_CONFIG = llvm-config

# Platform-specific LLVM flags
ifeq ($(OS), Windows_NT)
    LLVM_LDFLAGS = $(shell $(LLVM_CONFIG) --ldflags) C:/msys64/mingw64/lib/libLLVM-20.dll.a
else
    LLVM_LDFLAGS = $(shell $(LLVM_CONFIG) --ldflags) $(shell $(LLVM_CONFIG) --libs) $(shell $(LLVM_CONFIG) --system-libs)
endif

# Includes and libs
INCLUDES += -Iinclude -I../../include -I../../libs/cjson/include
LIBS = -pthread ../../bin/libcryo-compiler.a ../../libs/cjson/build/libcjson.a $(LLVM_LDFLAGS)
ifeq ($(OS), Windows_NT)
    LIBS += -lws2_32 -lwsock32
endif

# Default target
all: $(TARGET)

# Create directories
$(BUILDDIR) $(BINDIR):
ifeq ($(OS), Windows_NT)
	@if not exist "$(subst /,\,$@)" mkdir "$(subst /,\,$@)"
else
	@$(MKDIR) $@
endif

# Build target
$(TARGET): $(OBJECTS) | $(BINDIR)
	$(CXX) $(CXXFLAGS) $(OBJECTS) -o $@ $(LIBS)
	@echo "✓ CryoLSP built: $@"

# Compile source files
$(BUILDDIR)/%.o: $(SRCDIR)/%.cpp | $(BUILDDIR)
ifeq ($(OS), Windows_NT)
	@if not exist "$(subst /,\,$(dir $@))" mkdir "$(subst /,\,$(dir $@))"
else
	@$(MKDIR) $(dir $@)
endif
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

# Debug build
debug: CXXFLAGS += -g -O0 -DDEBUG
debug: $(TARGET)

# Clean
clean:
ifeq ($(OS), Windows_NT)
	@if exist "$(BUILDDIR)" rmdir /s /q "$(BUILDDIR)"
	@if exist "$(BINDIR)" rmdir /s /q "$(BINDIR)"
else
	@rm -rf $(BUILDDIR) $(BINDIR)
endif
	@echo "✓ CryoLSP cleaned"

# Run LSP server
run: $(TARGET)
	./$(TARGET) --port 7777 --log-level debug

# Test basic functionality
test: $(TARGET)
	./$(TARGET) --version
	./$(TARGET) --help

.PHONY: all debug clean run test
