@echo off
rem ---------------------------------------------------------------------
rem selfhost-check-windows.cmd — Windows-host wrapper for `make selfhost-check`.
rem
rem Checks for a python interpreter + wsl.exe on PATH, then drives the
rem main script.  scripts/selfhost-check.py is host-aware: when it sees a
rem Windows host it runs a native cryo.exe pre-check first and then
rem delegates the full 6-stage Linux byte-identity chain to WSL.
rem
rem Invoked from the Windows branch of Makefile so the recipe is a single
rem line (works under both cmd-shell and bash-shell make).
rem ---------------------------------------------------------------------

where wsl.exe >NUL 2>&1
if errorlevel 1 (
    echo ERROR: wsl.exe not found on PATH
    echo        'make selfhost-check' on a Windows host drives the Linux
    echo        6-stage chain via WSL after the native pre-check.  Install
    echo        WSL with 'wsl --install' and a Linux distro that has
    echo        cryo's toolchain, then retry.
    exit /b 1
)

set "PY="
where python.exe >NUL 2>&1 && set "PY=python.exe"
if not defined PY (
    where python3.exe >NUL 2>&1 && set "PY=python3.exe"
)
if not defined PY (
    echo ERROR: no python interpreter on PATH ^(need python or python3^)
    exit /b 1
)

"%PY%" scripts\selfhost-check.py %*
exit /b %ERRORLEVEL%
