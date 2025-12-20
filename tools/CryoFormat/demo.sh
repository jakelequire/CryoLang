#!/bin/bash

# Demo script for CryoFormat
# Shows the formatter in action with various examples

echo "=== CryoFormat Demo ==="
echo ""

# Build the project
echo "Building CryoFormat..."
cargo build --release

if [ $? -ne 0 ]; then
    echo "Build failed! Please fix the errors and try again."
    exit 1
fi

echo "Build successful!"
echo ""

# Create a test file with unformatted code
cat > demo_input.cryo << 'EOF'
// Unformatted CryoLang code
const   x:int=42;
mut     y:string="hello world";

function main()->int{return 0;}
function add(a:int,b:int)->int{
if(a>0&&b>0){
return a+b;
}else{
return 0;
}
}

type struct Point{x:int,y:int}
EOF

echo "=== Original unformatted code ==="
cat demo_input.cryo
echo ""

echo "=== Running CryoFormat ==="
echo "Command: ./target/release/cryofmt demo_input.cryo"
./target/release/cryofmt demo_input.cryo

echo ""
echo "=== Formatted code ==="
cat demo_input.cryo
echo ""

echo "=== Testing diff mode ==="
# Reset the file
cat > demo_input.cryo << 'EOF'
const   x:int=42;
function main()->int{return 0;}
EOF

echo "Command: ./target/release/cryofmt --diff demo_input.cryo"
./target/release/cryofmt --diff demo_input.cryo
echo ""

echo "=== Testing check mode ==="
echo "Command: ./target/release/cryofmt --check demo_input.cryo"
./target/release/cryofmt --check demo_input.cryo
echo "Exit code: $?"
echo ""

echo "=== Testing stdin formatting ==="
echo "Command: echo 'const x:int=42;' | ./target/release/cryofmt"
echo 'const x:int=42;' | ./target/release/cryofmt
echo ""

echo "=== Performance test with larger file ==="
# Create a larger test file
{
    for i in {1..50}; do
        echo "const var$i:int=$i;"
        echo "function func$i()->int{return var$i;}"
    done
} > large_test.cryo

echo "Formatting file with 100 lines..."
time ./target/release/cryofmt large_test.cryo
echo ""

echo "=== Configuration test ==="
cat > demo_config.toml << 'EOF'
[indent]
use_tabs = true
tab_width = 2

[spacing]
binary_operators = false
EOF

echo "Using custom configuration (tabs, no operator spacing):"
./target/release/cryofmt --config demo_config.toml demo_input.cryo
cat demo_input.cryo
echo ""

# Cleanup
rm -f demo_input.cryo large_test.cryo demo_config.toml

echo "=== Demo complete! ==="
echo ""
echo "Available commands:"
echo "  ./target/release/cryofmt file.cryo          # Format file"
echo "  ./target/release/cryofmt --diff file.cryo   # Show diff"
echo "  ./target/release/cryofmt --check file.cryo  # Check formatting"
echo "  ./target/release/cryofmt -r src/            # Format directory"
echo "  ./target/release/cryofmt --help             # Show all options"