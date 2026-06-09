@echo off
rem ---------------------------------------------------------------------
rem pin-windows.cmd — Windows-host wrapper for `make pin`.
rem
rem `make pin` on a Windows host can't run the Linux build (no native ELF
rem toolchain) and can't currently run the cryo.exe link locally either
rem (the mingw link command exceeds cmd.exe's 8KB limit).  Both work fine
rem in WSL, so this wrapper delegates the whole job: it checks for wsl.exe
rem on PATH, asks WSL for the Linux-side path of the current repo, then
rem re-enters this same Makefile inside WSL and runs the Linux branch of
rem the `pin` target there.
rem
rem Invoked from the Windows branch of Makefile so the recipe is a single
rem line (works under both cmd-shell and bash-shell make).  Exits non-zero
rem with an actionable message when WSL or the WSL path resolution fails.
rem ---------------------------------------------------------------------

where wsl.exe >NUL 2>&1
if errorlevel 1 (
    echo ERROR: wsl.exe not found on PATH
    echo        'make pin' on a Windows host needs WSL to drive both the
    echo        Linux self-host build ^(bin/cryo^) and the mingw cross-link
    echo        for bin/cryo.exe.  Install WSL with 'wsl --install' and a
    echo        Linux distro that has cryo's cross-build toolchain
    echo        ^(clang-20, x86_64-w64-mingw32-gcc, python3^), then retry.
    exit /b 1
)

rem `wslpath -a` converts an absolute Windows path to its WSL view
rem (e.g. C:\Programming\apps\CryoLang -> /mnt/c/Programming/apps/CryoLang).
rem `%CD%` is the repo root because `make` runs us with cwd == repo root.
for /f "usebackq tokens=*" %%i in (`wsl.exe -- wslpath -a "%CD%" 2^>NUL`) do set "WSL_ROOT=%%i"

if not defined WSL_ROOT (
    echo ERROR: could not resolve a WSL path for %CD%
    echo        Is the repo visible inside your WSL distro?
    exit /b 1
)

echo ==^> [windows host] refreshing both pins via WSL ^(%WSL_ROOT%^)
wsl.exe -- bash -lc "cd '%WSL_ROOT%' && make pin"
exit /b %ERRORLEVEL%
