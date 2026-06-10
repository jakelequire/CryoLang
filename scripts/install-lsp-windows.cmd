@echo off
rem ---------------------------------------------------------------------
rem install-lsp-windows.cmd - Windows-host wrapper for `make install-lsp`.
rem
rem Packages tools\CryoAnalyzer into a .vsix and (re)installs it into VS
rem Code, evicting any previously installed copy and stale .vsix artifacts.
rem Mirrors the POSIX `install-lsp` recipe in the Makefile, which cmd cannot
rem parse (`command -v`, `{ ... }`, `[ -d ]`, `rm`, `./node_modules/.bin`).
rem
rem `code` / `npm` / `vsce` are .cmd shims on Windows; each must be invoked
rem with `call` so this batch file isn't terminated when the shim returns.
rem Paths are derived from %~dp0 (this script's dir) so the cwd is irrelevant.
rem ---------------------------------------------------------------------
setlocal
set "EXT_DIR=%~dp0..\tools\CryoAnalyzer"
set "EXT_ID=cryolang.cryo-analyzer"
set "EXT_VSIX=%EXT_DIR%\cryo-analyzer.vsix"

where code >NUL 2>&1
if errorlevel 1 (
    echo ERROR: 'code' CLI not found on PATH
    echo        Install VS Code, then run the Command Palette action
    echo        "Shell Command: Install 'code' command in PATH".
    exit /b 1
)

echo ==^> Ensuring CryoAnalyzer node_modules are present
if not exist "%EXT_DIR%\node_modules" (
    pushd "%EXT_DIR%" || exit /b 1
    call npm install
    if errorlevel 1 ( popd & exit /b 1 )
    popd
)

echo ==^> Uninstalling previously installed %EXT_ID% (if any)
call code --uninstall-extension %EXT_ID% >NUL 2>&1

echo ==^> Removing cached .vsix artifacts
del /Q "%EXT_DIR%\*.vsix" >NUL 2>&1

echo ==^> Packaging CryoAnalyzer
pushd "%EXT_DIR%" || exit /b 1
call "%EXT_DIR%\node_modules\.bin\vsce.cmd" package --out "%EXT_VSIX%"
if errorlevel 1 ( popd & exit /b 1 )
popd

echo ==^> Installing %EXT_VSIX%
call code --install-extension "%EXT_VSIX%" --force
if errorlevel 1 exit /b 1

echo ==^> CryoAnalyzer extension installed
endlocal
