#!/usr/bin/env bash
#
# fetch-windows-llvm — provision libLLVM's C API for a windows-gnu cross-link.
#
# A windows-gnu cross-build of the Cryo compiler (cryo.exe) must link against
# libLLVM for the 192 LLVM-C symbols the codegen backend calls.  There is no
# official windows-*gnu* libLLVM release, so this script takes the official
# windows-*msvc* `LLVM-C.dll`, dumps its export table, and synthesizes a
# mingw import library (`libLLVM-C.dll.a`) from it with `dlltool`.  The C ABI
# is identical across msvc/mingw on x64 (undecorated cdecl names), so an
# mingw-linked cryo.exe resolves cleanly against the import lib and loads the
# DLL at runtime.  The official DLL statically links the MSVC CRT (`/MT`), so
# at runtime it only needs kernel32/advapi32/ntdll — all wine builtins — which
# is why the result also runs under wine.
#
# Output (default prefix $HOME/.local/llvm-20-mingw, override with
# CRYO_WIN_LLVM_PREFIX):
#     <prefix>/lib/libLLVM-C.dll.a   — link against this (`-lLLVM-C -L<prefix>/lib`)
#     <prefix>/bin/LLVM-C.dll        — ship next to cryo.exe (runtime dependency)
#
# It then prints the exact `[link.windows]` block to paste into
# compiler/cryoconfig.  Idempotent: re-running with the artifacts already in
# place is a no-op unless --force is given.
#
# Requires (host): curl, tar (xz), llvm-readobj-20, x86_64-w64-mingw32-dlltool.
#
# NOTE: a possible cleaner alternative is mstorsjo/llvm-mingw (see
# scripts/fetch-windows-toolchain.sh), which ships mingw-native LLVM libs in
# lib/libLLVM* and could allow a fully static link with no runtime DLL.  That
# path is unverified; this DLL+import-lib route is the one proven end-to-end
# (cryo.exe runs under wine and emits valid COFF objects).

set -euo pipefail

# Pin a specific LLVM release so cross-builds don't drift with upstream.
LLVM_VERSION="20.1.8"
ASSET="clang+llvm-${LLVM_VERSION}-x86_64-pc-windows-msvc.tar.xz"
URL="https://github.com/llvm/llvm-project/releases/download/llvmorg-${LLVM_VERSION}/${ASSET}"

PREFIX="${CRYO_WIN_LLVM_PREFIX:-$HOME/.local/llvm-20-mingw}"
READOBJ="${LLVM_READOBJ:-llvm-readobj-20}"
DLLTOOL="${DLLTOOL:-x86_64-w64-mingw32-dlltool}"

force=0
for arg in "$@"; do
    case "$arg" in
        -f|--force) force=1 ;;
        -h|--help)  sed -n '3,30p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "unknown arg: $arg (try --help)" >&2; exit 1 ;;
    esac
done

LIB="${PREFIX}/lib/libLLVM-C.dll.a"
DLL="${PREFIX}/bin/LLVM-C.dll"

if [[ -f "$LIB" && -f "$DLL" && "$force" -eq 0 ]]; then
    echo "[fetch-windows-llvm] already present under $PREFIX (pass --force to rebuild)"
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
    tar xJf "${work}/llvm.tar.xz" -C "$work" --strip-components=2 --wildcards \
        "clang+llvm-${LLVM_VERSION}-x86_64-pc-windows-msvc/bin/LLVM-C.dll"
    cp "${work}/LLVM-C.dll" "$DLL"

    echo "[fetch-windows-llvm] synthesizing mingw import lib…"
    "$READOBJ" --coff-exports "$DLL" \
        | grep -oE 'Name: LLVM[A-Za-z0-9_]+' | sed 's/Name: //' | sort -u > "${work}/exports.txt"
    { echo "LIBRARY LLVM-C.dll"; echo "EXPORTS"; cat "${work}/exports.txt"; } > "${work}/LLVM-C.def"
    "$DLLTOOL" --input-def "${work}/LLVM-C.def" --dllname LLVM-C.dll --output-lib "$LIB"

    echo "[fetch-windows-llvm] done:"
    echo "  import lib: $LIB  ($(grep -c . "${work}/exports.txt") exports)"
    echo "  runtime:    $DLL"
fi

cat <<EOF

Paste into compiler/cryoconfig (replace the commented [link.windows] block):

[link.windows]
system = ["LLVM-C"]
search = ["${PREFIX}/lib"]

Then cross-build and test:

  cd compiler && CRYO_STDLIB=\$PWD/../stdlib ../bin/cryo build --target=x86_64-pc-windows-gnu --no-incremental
  cp "${DLL}" compiler/build/          # cryo.exe needs LLVM-C.dll alongside it
  wine compiler/build/cryo.exe --version
EOF
