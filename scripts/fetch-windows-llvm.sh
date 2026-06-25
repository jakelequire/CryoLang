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
# libclang drives the C-import engine (Compiler::Bindgen). Same provisioning
# pattern as LLVM-C: take the official msvc libclang.dll, synthesize a mingw
# import lib from its `clang_*` exports. The C ABI is identical across
# msvc/mingw on x64, and a cross-built cryo.exe needs the Win64 by-value-struct
# ABI for CXString/CXCursor/CXType (codegen's AbiKind::Win64 path).
CLANG_LIB="${PREFIX}/lib/libclang.dll.a"
CLANG_DLL="${PREFIX}/bin/libclang.dll"
MINGW_CLANG="${MINGW_PREFIX}/bin/clang.exe"
MINGW_AR="${MINGW_PREFIX}/bin/llvm-ar.exe"

# Verify a downloaded artifact against its pinned SHA-256 (the GitHub asset
# digest, recorded in llvm-version.env). A mismatch aborts BEFORE the bytes
# are extracted or used, so a corrupted download, a man-in-the-middle, or a
# silently re-tagged/substituted upstream release never enters the cross
# toolchain. An empty pin is a hard error, not a skip.
verify_sha256() {
    local file="$1" expected="$2" label="$3"
    if [[ -z "$expected" ]]; then
        echo "error: no pinned SHA-256 for '${label}' (set it in scripts/llvm-version.env)" >&2
        exit 1
    fi
    command -v sha256sum >/dev/null 2>&1 || {
        echo "error: missing required tool 'sha256sum'" >&2; exit 1; }
    local actual
    actual="$(sha256sum "$file" | awk '{print $1}')"
    if [[ "$actual" != "$expected" ]]; then
        echo "error: checksum mismatch for '${label}'" >&2
        echo "  expected sha256: ${expected}" >&2
        echo "  actual   sha256: ${actual}" >&2
        echo "  refusing to use a download that does not match the pinned digest." >&2
        exit 1
    fi
    echo "[fetch-windows-llvm] verified ${label} (sha256 ok)"
}

# ---------------------------------------------------------------------------
# Part 1: LLVM-C.dll + synthesized mingw import lib (.toolchains/llvm-win)
# ---------------------------------------------------------------------------
if [[ -f "$LIB" && -f "$DLL" && -f "$CLANG_LIB" && -f "$CLANG_DLL" \
      && -d "${PREFIX}/lib/clang" && "$force" -eq 0 ]]; then
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
    verify_sha256 "${work}/llvm.tar.xz" "${LLVM_WIN_SHA256:-}" "${ASSET}"

    echo "[fetch-windows-llvm] extracting LLVM-C.dll + libclang.dll…"
    base="clang+llvm-${LLVM_VERSION}-x86_64-pc-windows-msvc"
    tar xJf "${work}/llvm.tar.xz" -C "$work" --strip-components=2 --wildcards \
        "${base}/bin/LLVM-C.dll" "${base}/bin/libclang.dll"
    cp "${work}/LLVM-C.dll"  "$DLL"
    cp "${work}/libclang.dll" "$CLANG_DLL"

    # libclang's builtin resource headers (stddef.h / stdint.h / …).  The
    # Windows libclang must find these to parse any header that #includes them
    # — e.g. the compiler's own llvm_bindings.h.  Staged at lib/clang/<v>/include
    # so they sit at libclang.dll's default `<dll>/../lib/clang/<v>` resource
    # path; selfhost-check also exports CRYO_CLANG_RESOURCE_DIR to lib/clang/<v>
    # for the wine self-host, where libclang.dll is copied out of this tree and
    # the default walk no longer reaches here.
    echo "[fetch-windows-llvm] extracting clang resource headers…"
    rm -rf "${PREFIX}/lib/clang"
    tar xJf "${work}/llvm.tar.xz" -C "${PREFIX}/lib" --strip-components=2 --wildcards \
        "${base}/lib/clang"

    # synth_import_lib <dll> <export-name-prefix> <import-lib-out>
    # Dumps the DLL's COFF export table, keeps the exports whose name starts
    # with the given prefix, and synthesizes a mingw import lib from them.
    synth_import_lib() {
        local dll="$1" prefix="$2" out="$3"
        local dllbase def
        dllbase="$(basename "$dll")"
        def="${work}/$(basename "$out").def"
        "$READOBJ" --coff-exports "$dll" \
            | grep -oE "Name: ${prefix}[A-Za-z0-9_]+" | sed 's/Name: //' | sort -u > "${def}.exports"
        { echo "LIBRARY ${dllbase}"; echo "EXPORTS"; cat "${def}.exports"; } > "$def"
        "$DLLTOOL" --input-def "$def" --dllname "$dllbase" --output-lib "$out"
        echo "  $(basename "$out")  ($(grep -c . "${def}.exports") ${prefix}* exports)"
    }

    echo "[fetch-windows-llvm] synthesizing mingw import libs…"
    synth_import_lib "$DLL"       "LLVM"  "$LIB"
    synth_import_lib "$CLANG_DLL" "clang_" "$CLANG_LIB"

    echo "[fetch-windows-llvm] llvm-win done:"
    echo "  runtime DLLs: $DLL"
    echo "                $CLANG_DLL"
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
    verify_sha256 "${mwork}/llvm-mingw.zip" "${LLVM_MINGW_SHA256:-}" "${MINGW_ASSET}"

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
  system = ["LLVM-C", "clang"]
  search = ["../.toolchains/llvm-win/lib"]

Ship BOTH DLLs next to cryo.exe at runtime: LLVM-C.dll and libclang.dll.

Cross-build + self-host under wine:

  make selfhost-check            # runs the windows 6-stage gate after the linux one
  # or just smoke-test a cross-build:
  cd compiler && CRYO_STDLIB=\$PWD/../stdlib \\
    CRYO_CC=${MINGW_CLANG} CRYO_AR=${MINGW_AR} \\
    wine ../bin/cryo.exe build --no-incremental
EOF
