#!/bin/bash

# Build script for CryoFormat

set -e

echo "Building CryoFormat..."

# Check if Rust is installed
if ! command -v cargo &> /dev/null; then
    echo "Error: Cargo not found. Please install Rust: https://rustup.rs/"
    exit 1
fi

# Build in release mode for performance
echo "Building in release mode..."
cargo build --release

echo "Running tests..."
cargo test

echo "Running clippy (linter)..."
cargo clippy -- -D warnings

echo "Checking formatting..."
cargo fmt --check

echo "Build completed successfully!"
echo "Binary available at: target/release/cryofmt"

# Optionally install to system
if [ "$1" == "install" ]; then
    echo "Installing cryofmt to system..."
    cargo install --path .
    echo "cryofmt installed successfully!"
fi