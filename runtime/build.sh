#!/bin/bash
# Cryo Runtime Build Script
#
# This script builds the Cryo runtime library from source.
# It compiles each module to LLVM bitcode, links them together,
# and creates a static library.
#
# Usage: ./build.sh [options]
#   --debug     Enable debug output
#   --clean     Clean build directory before building
#   --verbose   Show verbose output

set -e

# Configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CRYO_ROOT="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$SCRIPT_DIR/build"
OUTPUT_LIB="$BUILD_DIR/libcryoruntime.a"
OUTPUT_OBJ="$BUILD_DIR/runtime.o"

# Compiler location
CRYO_COMPILER="$CRYO_ROOT/bin/cryo"

# Parse arguments
DEBUG=""
CLEAN=false
VERBOSE=false

for arg in "$@"; do
    case $arg in
        --debug)
            DEBUG="--debug"
            ;;
        --clean)
            CLEAN=true
            ;;
        --verbose)
            VERBOSE=true
            ;;
    esac
done

# Helper functions
log() {
    if [ "$VERBOSE" = true ] || [ "$1" = "ERROR" ]; then
        echo "[RUNTIME] $2"
    fi
}

log_always() {
    echo "[RUNTIME] $1"
}

# Check for compiler
if [ ! -f "$CRYO_COMPILER" ]; then
    echo "Error: Cryo compiler not found at $CRYO_COMPILER"
    echo "Please build the compiler first with 'make build'"
    exit 1
fi

# Clean if requested
if [ "$CLEAN" = true ]; then
    log_always "Cleaning build directory..."
    rm -rf "$BUILD_DIR"
fi

# Create build directory
mkdir -p "$BUILD_DIR"
mkdir -p "$BUILD_DIR/platform"

log_always "Building Cryo Runtime..."
log_always "  Source: $SCRIPT_DIR"
log_always "  Output: $OUTPUT_LIB"

# Runtime source files in dependency order
SOURCES=(
    "version.cryo"
    "platform/linux.cryo"
    "platform/_module.cryo"
    "platform.cryo"
    "memory.cryo"
    "atexit.cryo"
    "signal.cryo"
    "panic.cryo"
    "init.cryo"
    "entry.cryo"
    "lib.cryo"
)

# Compile each source file to bitcode
BITCODE_FILES=()
FAILED=0

for src in "${SOURCES[@]}"; do
    src_path="$SCRIPT_DIR/$src"
    bc_path="$BUILD_DIR/${src%.cryo}.bc"

    if [ ! -f "$src_path" ]; then
        log "WARNING" "Source file not found: $src_path"
        continue
    fi

    log "INFO" "Compiling $src..."

    # Create output directory if needed
    mkdir -p "$(dirname "$bc_path")"

    # Compile with stdlib-mode and emit LLVM bitcode
    if $CRYO_COMPILER "$src_path" --emit-llvm -c --stdlib-mode $DEBUG -o "$bc_path" 2>&1; then
        log "SUCCESS" "  ✓ $src"
        BITCODE_FILES+=("$bc_path")
    else
        log "ERROR" "  ✗ Failed to compile $src"
        FAILED=$((FAILED + 1))
    fi
done

# Check for failures
if [ $FAILED -gt 0 ]; then
    log_always "Warning: $FAILED file(s) failed to compile"
fi

if [ ${#BITCODE_FILES[@]} -eq 0 ]; then
    log_always "Error: No bitcode files were generated"
    exit 1
fi

log_always "Linking ${#BITCODE_FILES[@]} bitcode files..."

# Link all bitcode files together
COMBINED_BC="$BUILD_DIR/runtime_combined.bc"
llvm-link "${BITCODE_FILES[@]}" -o "$COMBINED_BC"

if [ ! -f "$COMBINED_BC" ]; then
    log_always "Error: Failed to link bitcode files"
    exit 1
fi

log_always "Compiling to object file..."

# Compile to object file with PIC (position-independent code)
llc -filetype=obj -relocation-model=pic "$COMBINED_BC" -o "$OUTPUT_OBJ"

if [ ! -f "$OUTPUT_OBJ" ]; then
    log_always "Error: Failed to create object file"
    exit 1
fi

log_always "Creating static library..."

# Create static library
llvm-ar rcs "$OUTPUT_LIB" "$OUTPUT_OBJ"

if [ ! -f "$OUTPUT_LIB" ]; then
    log_always "Error: Failed to create library"
    exit 1
fi

log_always "✓ Runtime built successfully!"
log_always "  Library: $OUTPUT_LIB"
log_always "  Object:  $OUTPUT_OBJ"

# Show size information
if [ "$VERBOSE" = true ]; then
    echo ""
    log_always "File sizes:"
    ls -lh "$OUTPUT_LIB" "$OUTPUT_OBJ" 2>/dev/null | awk '{print "  " $5 " " $9}'
fi

