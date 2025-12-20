#!/usr/bin/env bash

# Quick test to validate the CryoFormat project builds

cd /workspaces/CryoLang/tools/CryoFormat

echo "Testing CryoFormat build..."

# Check if we can build
if cargo check 2>&1; then
    echo "✓ Cargo check passed"
else
    echo "✗ Cargo check failed"
    echo "Attempting to run cargo build to see detailed errors..."
    cargo build
fi