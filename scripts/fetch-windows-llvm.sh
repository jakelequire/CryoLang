#!/usr/bin/env bash
#
# fetch-windows-llvm — provision a wine-runnable Windows toolchain for the Cryo
# compiler's windows-gnu cross-build + self-host.
#
# Two pieces land under the repo's gitignored .toolchains/:
#
#   1. .toolchains/llvm-win/  — the LLVM-C API the compiler links against.
#      A windows-gnu cross-build of cryo.exe must resolve the ~192 LLVM-C
#      symbols the codegen backend calls.  There is no official windows-*gnu*
#      libLLVM release, so this takes the official windows-*msvc* `LLVM-C.dll`,
#      dumps its export table, and synthesizes a mingw import library
#      (`libLLVM-C.dll.a`) from it with `dlltool`.  The C ABI is identical
#      across msvc/mingw on x64 (undecorated cdecl names), so an mingw-linked
#      cryo.exe resolves cleanly against the import lib and loads the DLL at
#      runtime.  The official DLL statically links the MSVC CRT (`/MT`), so it
#      only needs kernel32/advapi32/ntdll at runtime — all wine builtins — which
#      is why the result also runs under wine.  Pinned to LLVM 20 to match the
#      host build's `-lLLVM-20`.
#
#   2. .toolchains/llvm-mingw/ — mstorsjo's llvm-mingw (Windows-PE build): a
#      wine-runnable `clang.exe` + `ld.lld.exe` + a full mingw-w64 sysroot.
#      The msvc `LLVM-C.dll` gives us the API to LINK AGAINST, but the msvc
#      clang has no bundled linker, so it cannot produce an executable under
#      wine.  llvm-mingw's clang.exe defaults to the `x86_64-w64-windows-gnu`
#      target and finds ld.lld + the sysroot on its own, so it both
#      preprocesses the one `#include "llvm_bindings.h"` site (CRYO_CC) AND
#      links a real, wine-runnable cryo.exe with no extra flags.  `llvm-ar.exe`
#      is the archiver wine can launch (CRYO_AR).  Used purely as a
#      driver/linker/preprocessor — its own LLVM version is irrelevant; the
#      LLVM-C symbols come from the import lib in (1), and llvm_bindings.h is
#      self-contained (no system #includes).
#
# Output (default prefix .toolchains/, override with CRYO_WIN_LLVM_PREFIX /
# CRYO_WIN_MINGW_PREFIX):
#     <llvm-win>/lib/libLLVM-C.dll.a   — link against this (`-lLLVM-C`)
#     <llvm-win>/bin/LLVM-C.dll        — ship next to cryo.exe (runtime dep)
#     <llvm-mingw>/bin/clang.exe       — CRYO_CC (preprocess + link driver)
#     <llvm-mingw>/bin/llvm-ar.exe     — CRYO_AR (archiver, wine-runnable)
#
# scripts/selfhost-check.py points CRYO_CC/CRYO_AR at the llvm-mingw bits and
# runs the full 6-stage windows byte-identity self-host under wine.  Idempotent:
# re-running with the artifacts already in place is a no-op unless --force.
#
# Requires (host): curl, tar (xz), unzip, llvm-readobj-20,
#                  x86_64-w64-mingw32-dlltool.

set -euo pipefail

# Pin specific releases so cross-builds don't drift with upstream.
# Versions live in scripts/llvm-version.env (single source of truth, shared
# with CI): LLVM_WIN_VERSION + LLVM_MINGW_VERSION.
. "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/llvm-version.env"
LLVM_VERSION="${LLVM_WIN_VERSION}"
ASSET="clang+llvm-${LLVM_VERSION}-x86_64-pc-windows-msvc.tar.xz"
URL="https://github.com/llvm/llvm-project/releases/download/llvmorg-${LLVM_VERSION}/${ASSET}"

# llvm-mingw: the Windows-PE (ucrt, x86_64) build, so its tools run under wine.
MINGW_ASSET="llvm-mingw-${LLVM_MINGW_VERSION}-ucrt-x86_64.zip"
MINGW_URL="https://github.com/mstorsjo/llvm-mingw/releases/download/${LLVM_MINGW_VERSION}/${MINGW_ASSET}"

# Default install lives under the repo's gitignored .toolchains/ so the
# committed [link.windows] block can reference llvm-win with a stable
# repo-relative path (`../.toolchains/llvm-win/lib`, resolved from the
# compiler/ project dir).  Override with the CRYO_WIN_*_PREFIX vars.
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PREFIX="${CRYO_WIN_LLVM_PREFIX:-$ROOT/.toolchains/llvm-win}"
MINGW_PREFIX="${CRYO_WIN_MINGW_PREFIX:-$ROOT/.toolchains/llvm-mingw}"
READOBJ="${LLVM_READOBJ:-llvm-readobj-20}"
DLLTOOL="${DLLTOOL:-x86_64-w64-mingw32-dlltool}"

force=0
for arg in "$@"; do
    case "$arg" in
        -f|--force) force=1 ;;
        -h|--help)  sed -n '3,46p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "unknown arg: $arg (try --help)" >&2; exit 1 ;;
    esac
done

LIB="${PREFIX}/lib/libLLVM-C.dll.a"
DLL="${PREFIX}/bin/LLVM-C.dll"
MINGW_CLANG="${MINGW_PREFIX}/bin/clang.exe"
MINGW_AR="${MINGW_PREFIX}/bin/llvm-ar.exe"

# ---------------------------------------------------------------------------
# Part 1: LLVM-C.dll + synthesized mingw import lib (.toolchains/llvm-win)
# ---------------------------------------------------------------------------
if [[ -f "$LIB" && -f "$DLL" && "$force" -eq 0 ]]; then
    echo "[fetch-windows-llvm] llvm-win already present under $PREFIX (pass --force to rebuild)"
else
    for tool in curl tar "$READOBJ" "$DLLTOOL"; do
        command -v "$tool" >/dev/null 2>&1 || { echo "error: missing required tool '$tool'" >&2; exit 1; }
    done

    work="$(mktemp -d)"
    trap 'rm -rf "$work"' EXIT
    mkdir -p "${PREFIX}/lib" "${PREFIX}/bin"

    echo "[fetch-windows-llvm] downloading ${ASSET} (~896 MB)…"
    curl -L --fail --progress-bar -o "${work}/llvm.tar.xz" "$URL"

    echo "[fetch-windows-llvm] extracting LLVM-C.dll…"
    base="clang+llvm-${LLVM_VERSION}-x86_64-pc-windows-msvc"
    tar xJf "${work}/llvm.tar.xz" -C "$work" --strip-components=2 --wildcards \
        "${base}/bin/LLVM-C.dll"
    cp "${work}/LLVM-C.dll" "$DLL"

    echo "[fetch-windows-llvm] synthesizing mingw import lib…"
    "$READOBJ" --coff-exports "$DLL" \
        | grep -oE 'Name: LLVM[A-Za-z0-9_]+' | sed 's/Name: //' | sort -u > "${work}/exports.txt"
    { echo "LIBRARY LLVM-C.dll"; echo "EXPORTS"; cat "${work}/exports.txt"; } > "${work}/LLVM-C.def"
    "$DLLTOOL" --input-def "${work}/LLVM-C.def" --dllname LLVM-C.dll --output-lib "$LIB"

    echo "[fetch-windows-llvm] llvm-win done:"
    echo "  import lib: $LIB  ($(grep -c . "${work}/exports.txt") exports)"
    echo "  runtime:    $DLL"
fi

# ---------------------------------------------------------------------------
# Part 2: llvm-mingw (.toolchains/llvm-mingw) — wine-runnable clang + ld.lld
# ---------------------------------------------------------------------------
if [[ -f "$MINGW_CLANG" && -f "$MINGW_AR" && "$force" -eq 0 ]]; then
    echo "[fetch-windows-llvm] llvm-mingw already present under $MINGW_PREFIX (pass --force to rebuild)"
else
    for tool in curl unzip; do
        command -v "$tool" >/dev/null 2>&1 || { echo "error: missing required tool '$tool'" >&2; exit 1; }
    done

    mwork="$(mktemp -d)"
    trap 'rm -rf "${work:-}" "$mwork"' EXIT

    echo "[fetch-windows-llvm] downloading ${MINGW_ASSET} (~180 MB)…"
    curl -L --fail --progress-bar -o "${mwork}/llvm-mingw.zip" "$MINGW_URL"

    echo "[fetch-windows-llvm] extracting llvm-mingw toolchain…"
    unzip -q -o "${mwork}/llvm-mingw.zip" -d "$mwork"
    # The zip wraps everything in a single versioned top-level dir; flatten it
    # into a stable, version-independent path so the committed selfhost-check
    # constants (.toolchains/llvm-mingw/bin/clang.exe) don't carry the date.
    src="${mwork}/llvm-mingw-${LLVM_MINGW_VERSION}-ucrt-x86_64"
    rm -rf "$MINGW_PREFIX"
    mkdir -p "$(dirname "$MINGW_PREFIX")"
    mv "$src" "$MINGW_PREFIX"

    echo "[fetch-windows-llvm] llvm-mingw done:"
    echo "  driver/linker: $MINGW_CLANG  (CRYO_CC: windows-gnu default + bundled ld.lld)"
    echo "  archiver:      $MINGW_AR     (CRYO_AR)"
fi

cat <<EOF

[link.windows] in compiler/cryoconfig is already wired for this:

  [link.windows]
  system = ["LLVM-C"]
  search = ["../.toolchains/llvm-win/lib"]

Cross-build + self-host under wine:

  make selfhost-check            # runs the windows 6-stage gate after the linux one
  # or just smoke-test a cross-build:
  cd compiler && CRYO_STDLIB=\$PWD/../stdlib \\
    CRYO_CC=${MINGW_CLANG} CRYO_AR=${MINGW_AR} \\
    wine ../bin/cryo.exe build --no-incremental
EOF
