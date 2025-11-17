@echo off
:: Emscripten Setup Script for CryoLang WASM Build (Windows)
:: This script helps users set up Emscripten for building CryoLang to WebAssembly

echo 🔧 Setting up Emscripten for CryoLang WASM build...

:: Check if we're in the right directory
if not exist "wasm.makefile.config" (
    echo ❌ Error: Please run this script from the CryoLang root directory
    exit /b 1
)

:: Check if emcc is available in PATH
where emcc >nul 2>&1
if %errorlevel% == 0 (
    echo ✅ Emscripten found in system PATH
    emcc --version
    goto :success
)

:: Check if EMSDK environment variable is set
if defined EMSDK (
    echo ✅ EMSDK environment variable found: %EMSDK%
    if exist "%EMSDK%\upstream\emscripten\emcc.bat" (
        echo ✅ Emscripten is available via EMSDK
        "%EMSDK%\upstream\emscripten\emcc.bat" --version
        goto :success
    ) else (
        echo ⚠️ EMSDK variable set but emcc not found
        goto :install_local
    )
)

:install_local
echo ⚠️ Emscripten not found in system
echo Would you like to install it locally? (y/n)
set /p response=
if /i "%response%" == "y" goto :setup_local
if /i "%response%" == "yes" goto :setup_local

echo ❌ Emscripten is required for WASM builds
echo.
echo To install Emscripten manually:
echo 1. Visit: https://emscripten.org/docs/getting_started/downloads.html
echo 2. Or run: scripts\setup-emscripten.bat
exit /b 1

:setup_local
echo 📦 Installing Emscripten locally...

if not exist "emsdk" (
    echo Cloning emsdk...
    git clone https://github.com/emscripten-core/emsdk.git
    if %errorlevel% neq 0 (
        echo ❌ Failed to clone emsdk. Please check your git installation.
        exit /b 1
    )
)

cd emsdk

echo Installing latest Emscripten...
call emsdk.bat install latest
if %errorlevel% neq 0 (
    echo ❌ Failed to install Emscripten
    exit /b 1
)

call emsdk.bat activate latest
if %errorlevel% neq 0 (
    echo ❌ Failed to activate Emscripten
    exit /b 1
)

echo Setting up environment...
call emsdk_env.bat

cd ..

echo ✅ Emscripten installed locally
echo 🔄 To use it in future sessions, run: emsdk\emsdk_env.bat

:success
echo.
echo 🎉 Emscripten setup complete!
echo.
echo You can now build CryoLang for WebAssembly:
echo   make -f wasm.makefile.config wasm
echo.
echo Or build and test:
echo   make -f wasm.makefile.config wasm-test