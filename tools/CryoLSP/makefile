#*******************************************************************************
# CryoLSP - Language Server for Cryo
#*******************************************************************************

# Include shared configuration from root
include ../../makefile.config

# ============================================================================
# Platform Detection
# ============================================================================

ifeq ($(OS),Windows_NT)
    PLATFORM = windows
    MKDIR = if not exist "$(subst /,\,$1)" mkdir "$(subst /,\,$1)"
    RM_RF = if exist "$(subst /,\,$1)" rmdir /s /q "$(subst /,\,$1)"
    RM_F = if exist "$(subst /,\,$1)" del /q "$(subst /,\,$1)"
    BIN_SUFFIX ?= .exe
else
    PLATFORM = linux
    MKDIR = mkdir -p $1
    RM_RF = rm -rf $1
    RM_F = rm -f $1
    BIN_SUFFIX ?=
endif

# ============================================================================
# Paths
# ============================================================================

ROOT_DIR = ../..
SRC_DIR = src
INCLUDE_DIR = include
OBJ_DIR = build/.o
BIN_DIR = $(ROOT_DIR)/bin

# Output binary
TARGET = $(BIN_DIR)/cryolsp$(BIN_SUFFIX)

# ============================================================================
# Source files
# ============================================================================

SRCS = \
	$(SRC_DIR)/main.cpp \
	$(SRC_DIR)/Transport.cpp \
	$(SRC_DIR)/Protocol.cpp \
	$(SRC_DIR)/DocumentStore.cpp \
	$(SRC_DIR)/AnalysisEngine.cpp \
	$(SRC_DIR)/PositionFinder.cpp \
	$(SRC_DIR)/Server.cpp \
	$(SRC_DIR)/Providers/DiagnosticsProvider.cpp \
	$(SRC_DIR)/Providers/HoverProvider.cpp \
	$(SRC_DIR)/Providers/DefinitionProvider.cpp \
	$(SRC_DIR)/Providers/CompletionProvider.cpp \
	$(SRC_DIR)/Providers/SymbolProvider.cpp \
	$(SRC_DIR)/Providers/ReferencesProvider.cpp \
	$(SRC_DIR)/Providers/SignatureHelpProvider.cpp \
	$(SRC_DIR)/Providers/SemanticTokensProvider.cpp

OBJS = $(patsubst $(SRC_DIR)/%.cpp,$(OBJ_DIR)/%.o,$(SRCS))

# ============================================================================
# Include paths
# ============================================================================

LSP_INCLUDES = \
	-I$(INCLUDE_DIR) \
	-I$(ROOT_DIR)/include \
	-I$(ROOT_DIR)/libs/cjson/include

# ============================================================================
# Libraries to link
# ============================================================================

COMPILER_LIB = $(ROOT_DIR)/bin/libcryo-compiler.a
CJSON_LIB = $(ROOT_DIR)/libs/cjson/build/libcjson.a

# ============================================================================
# Platform-specific link flags
# ============================================================================

ifeq ($(PLATFORM),windows)
    PLATFORM_LDFLAGS = -lws2_32 -lshlwapi
else
    PLATFORM_LDFLAGS =
endif

# ============================================================================
# Build rules
# ============================================================================

.PHONY: all clean

all: $(TARGET)

# Create object directories
$(OBJ_DIR):
	@$(call MKDIR,$(OBJ_DIR)/Providers)

# Compile object files
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp | $(OBJ_DIR)
	@$(call MKDIR,$(dir $@))
	$(CXX) $(CXXFLAGS) $(LSP_INCLUDES) -c $< -o $@

# Link final binary
$(TARGET): $(OBJS)
	@$(call MKDIR,$(BIN_DIR))
	@echo "Linking CryoLSP..."
	$(CXX) $(OBJS) $(COMPILER_LIB) $(CJSON_LIB) -o $@ $(LDFLAGS) $(PLATFORM_LDFLAGS)
	@echo "CryoLSP built successfully: $(TARGET)"

# Clean
clean:
	$(call RM_RF,build)
	$(call RM_F,$(TARGET))