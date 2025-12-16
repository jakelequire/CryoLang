# CryoLSP Makefile
# Simple Language Server Protocol implementation for CryoLang

# Include shared configuration from parent
include ../../makefile.config

# Use the same compiler and flags as parent makefile
CXX = $(CXX_COMPILER)
# CXXFLAGS is inherited from makefile.config and includes LLVM flags

# Directories
SRCDIR = src
BUILDDIR = build
# Use root bin directory - CRYO_SRC env var or relative path to root
ifdef CRYO_SRC
    BINDIR = $(CRYO_SRC)/bin
else
    BINDIR = ../../bin
endif
TARGET = $(BINDIR)/cryolsp

# Source files
SOURCES = $(wildcard $(SRCDIR)/*.cpp)
OBJECTS = $(SOURCES:$(SRCDIR)/%.cpp=$(BUILDDIR)/%.o)

# Use the exact same LLVM configuration as the main compiler
# These are already defined in makefile.config and imported above
# LLVM_CONFIG, LLVM_CXXFLAGS, and LLVM_LDFLAGS are inherited from parent

# Additional includes specific to LSP (CXXFLAGS already includes LLVM includes)
INCLUDES = -Iinclude -I../../include -I../../libs/cjson/include
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
	@if exist "$(TARGET)" del /q "$(TARGET)"
else
	@rm -rf $(BUILDDIR)
	@rm -f $(TARGET)
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
