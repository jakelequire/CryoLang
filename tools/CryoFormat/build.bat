@echo off
REM Build script for CryoFormat on Windows

echo Building CryoFormat...

REM Check if Rust is installed
where cargo >nul 2>nul
if %errorlevel% neq 0 (
    echo Error: Cargo not found. Please install Rust: https://rustup.rs/
    exit /b 1
)

REM Build in release mode for performance
echo Building in release mode...
cargo build --release
if %errorlevel% neq 0 (
    echo Build failed!
    exit /b 1
)

echo Running tests...
cargo test
if %errorlevel% neq 0 (
    echo Tests failed!
    exit /b 1
)

echo Running clippy (linter)...
cargo clippy -- -D warnings
if %errorlevel% neq 0 (
    echo Clippy check failed!
    exit /b 1
)

echo Checking formatting...
cargo fmt --check
if %errorlevel% neq 0 (
    echo Format check failed!
    exit /b 1
)

echo Build completed successfully!
echo Binary available at: target\release\cryofmt.exe

REM Optionally install to system
if "%1"=="install" (
    echo Installing cryofmt to system...
    cargo install --path .
    echo cryofmt installed successfully!
)