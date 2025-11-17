#!/bin/bash
# Emscripten Setup Script for CryoLang WASM Build
# This script helps users set up Emscripten for building CryoLang to WebAssembly

set -e

echo "🔧 Setting up Emscripten for CryoLang WASM build..."

# Check if we're in the right directory
if [ ! -f "wasm.makefile.config" ]; then
    echo "❌ Error: Please run this script from the CryoLang root directory"
    exit 1
fi

# Function to check if emcc is available
check_emcc() {
    if command -v emcc >/dev/null 2>&1; then
        echo "✅ Emscripten found in system PATH"
        emcc --version
        return 0
    else
        return 1
    fi
}

# Function to setup local emsdk
setup_local_emsdk() {
    echo "📦 Installing Emscripten locally..."
    
    if [ ! -d "emsdk" ]; then
        echo "Cloning emsdk..."
        git clone https://github.com/emscripten-core/emsdk.git
    fi
    
    cd emsdk
    
    # Install and activate the latest version
    echo "Installing latest Emscripten..."
    ./emsdk install latest
    ./emsdk activate latest
    
    echo "Setting up environment..."
    source ./emsdk_env.sh
    
    cd ..
    
    echo "✅ Emscripten installed locally"
    echo "🔄 To use it, run: source ./emsdk/emsdk_env.sh"
}

# Check if EMSDK is already set up
if [ -n "$EMSDK" ]; then
    echo "✅ EMSDK environment variable found: $EMSDK"
    if [ -x "$EMSDK/upstream/emscripten/emcc" ]; then
        echo "✅ Emscripten is available via EMSDK"
        "$EMSDK/upstream/emscripten/emcc" --version
    else
        echo "⚠️  EMSDK variable set but emcc not found"
        check_emcc || setup_local_emsdk
    fi
elif check_emcc; then
    echo "✅ Using system Emscripten"
else
    echo "⚠️  Emscripten not found in system"
    echo "Would you like to install it locally? (y/n)"
    read -r response
    if [[ "$response" =~ ^([yY][eE][sS]|[yY])$ ]]; then
        setup_local_emsdk
    else
        echo "❌ Emscripten is required for WASM builds"
        echo ""
        echo "To install Emscripten manually:"
        echo "1. Visit: https://emscripten.org/docs/getting_started/downloads.html"
        echo "2. Or run: ./scripts/setup-emscripten.sh"
        exit 1
    fi
fi

echo ""
echo "🎉 Emscripten setup complete!"
echo ""
echo "You can now build CryoLang for WebAssembly:"
echo "  make -f wasm.makefile.config wasm"
echo ""
echo "Or build and test:"
echo "  make -f wasm.makefile.config wasm-test"