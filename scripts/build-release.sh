#!/usr/bin/env bash
#
# build-release — build + package one platform's distributable Cryo toolchain.
#
#   scripts/build-release.sh linux     # static cryo + stdlib  -> dist/cryo-<ver>-linux-x86_64.tar.gz
#   scripts/build-release.sh windows   # cross cryo.exe + DLL   -> dist/cryo-<ver>-windows-x86_64.zip
#
# Produces the archive AND a sibling `.sha256` under dist/.  The version is
# read from compiler/src/main.cryo (the single source of truth).
#
# LINUX: builds the shipped `cryo` with `--release-static`, i.e. statically
# linked against libLLVM + the C++ runtime, so the binary has no libLLVM-20.so
# runtime dependency.  Honors the static-link env knobs, which is how the same
# script produces a fully-static MUSL binary when run inside an Alpine
# container (set before invoking):
#     CRYO_LLVM_CONFIG=llvm-config   # Alpine's musl-built llvm-config
#     CRYO_CC=clang                  # Alpine's musl clang as the link driver
# On a glibc host with neither set it produces a fully-static glibc binary.
#
# Host prerequisites:
#   linux:   llvm-20-dev + libpolly-20-dev (the static Polly libs llvm-config
#            lists), a C toolchain with static libstdc++ (gcc), make, tar.
#            (Alpine/musl: llvm20-static llvm20-dev clang lld musl-dev
#             libstdc++-dev + the static C++ runtime; gcompat to run the pin.)
#   windows: x86_64-w64-mingw32-gcc + the fetched .toolchains (run
#            scripts/fetch-windows-llvm.sh first), zip.
#
# WINDOWS is NOT static (no official static windows-gnu libLLVM): the zip
# ships cryo.exe next to LLVM-C.dll (its runtime dep).

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DIST="${ROOT}/dist"
PLATFORM="${1:-}"

case "$PLATFORM" in
    linux|windows) ;;
    *) echo "usage: build-release.sh <linux|windows>" >&2; exit 1 ;;
esac

# Single source of truth for the version.
VER="$(grep -oE 'const VERSION:[[:space:]]*string[[:space:]]*=[[:space:]]*"[^"]+"' \
        "${ROOT}/compiler/src/main.cryo" | sed -E 's/.*"([^"]+)".*/\1/')"
[ -n "$VER" ] || { echo "error: could not read VERSION from compiler/src/main.cryo" >&2; exit 1; }

echo "[build-release] cryo v${VER} -> ${PLATFORM}-x86_64"
mkdir -p "$DIST"

stage_common() {
    # $1 = staging dir.  Copies the stdlib (source + archive), LICENSE, VERSION.
    local stage="$1"
    mkdir -p "${stage}/stdlib"
    # stdlib source + the prebuilt host archive (libcryo.a) the linker pulls
    # in for user programs; exclude transient per-build .o trees but keep
    # .bin/libcryo.a.
    ( cd "${ROOT}/stdlib" && tar --exclude='.bin/obj' --exclude='.bin/win-*' \
        --exclude='.bin/self' -cf - . ) | ( cd "${stage}/stdlib" && tar -xf - )
    [ -f "${ROOT}/LICENSE" ] && cp "${ROOT}/LICENSE" "${stage}/LICENSE" || true
    stage_third_party_licenses "$stage"
    echo "$VER" > "${stage}/VERSION"
}

stage_third_party_licenses() {
    # libLLVM is redistributed (statically linked into the Linux cryo, shipped
    # as LLVM-C.dll on Windows). Its Apache-2.0-with-LLVM-exceptions license
    # MUST accompany the binary, so this is a hard requirement, not best-effort.
    local stage="$1"
    local llvm_lic="${ROOT}/LLVM-LICENSE.txt"
    [ -f "$llvm_lic" ] || { echo "error: ${llvm_lic} missing - libLLVM is redistributed and its license must ship" >&2; exit 1; }
    mkdir -p "${stage}/THIRD_PARTY_LICENSES"
    cp "$llvm_lic" "${stage}/THIRD_PARTY_LICENSES/LLVM-LICENSE.txt"
}

build_linux() {
    local stage="${DIST}/cryo-${VER}-linux-x86_64"
    rm -rf "$stage"; mkdir -p "${stage}/bin"

    echo "[build-release] building stdlib + bootstrap compiler"
    make -C "$ROOT" stdlib >/dev/null
    make -C "$ROOT" cryo   >/dev/null

    echo "[build-release] linking static cryo (--release-static)"
    rm -rf "${ROOT}/compiler/build/release-static"
    ( cd "${ROOT}/compiler" && CRYO_STDLIB="${ROOT}/stdlib" \
        ./build/cryo build --release-static --no-incremental \
        --build-dir=build/release-static )

    local out="${ROOT}/compiler/build/release-static/cryo"
    [ -x "$out" ] || { echo "error: static cryo not produced at $out" >&2; exit 1; }
    cp "$out" "${stage}/bin/cryo"
    strip "${stage}/bin/cryo" 2>/dev/null || true

    # Confirm it really is fully static: no libLLVM runtime dep, and no
    # dynamic loader at all (glibc ldd: "not a dynamic executable";
    # musl ldd: "not a valid dynamic program").
    if command -v ldd >/dev/null 2>&1; then
        local ldd_out
        ldd_out="$(ldd "${stage}/bin/cryo" 2>&1 || true)"
        if echo "$ldd_out" | grep -qi 'LLVM'; then
            echo "error: shipped cryo still links libLLVM dynamically" >&2; exit 1
        fi
        if ! echo "$ldd_out" | grep -qiE 'not a dynamic executable|not a valid dynamic program|statically linked'; then
            echo "error: shipped cryo is not fully static; ldd reports:" >&2
            echo "$ldd_out" >&2; exit 1
        fi
    fi

    stage_common "$stage"

    local archive="${DIST}/cryo-${VER}-linux-x86_64.tar.gz"
    ( cd "$DIST" && tar -czf "$archive" "cryo-${VER}-linux-x86_64" )
    ( cd "$DIST" && sha256sum "$(basename "$archive")" > "$(basename "$archive").sha256" )
    rm -rf "$stage"
    echo "[build-release] wrote ${archive#$ROOT/}"
}

build_windows() {
    local stage="${DIST}/cryo-${VER}-windows-x86_64"
    rm -rf "$stage"; mkdir -p "${stage}/bin"

    local dll="${ROOT}/.toolchains/llvm-win/bin/LLVM-C.dll"
    local clang_dll="${ROOT}/.toolchains/llvm-win/bin/libclang.dll"
    [ -f "$dll" ] || { echo "error: ${dll} missing - run scripts/fetch-windows-llvm.sh first" >&2; exit 1; }
    [ -f "$clang_dll" ] || { echo "error: ${clang_dll} missing - run scripts/fetch-windows-llvm.sh first" >&2; exit 1; }

    echo "[build-release] cross-building cryo.exe"
    make -C "$ROOT" cryo-exe >/dev/null

    local exe="${ROOT}/compiler/build/cryo.exe"
    [ -x "$exe" ] || [ -f "$exe" ] || { echo "error: cryo.exe not produced at $exe" >&2; exit 1; }
    cp "$exe" "${stage}/bin/cryo.exe"
    cp "$dll" "${stage}/bin/LLVM-C.dll"
    # libclang.dll: runtime dep of the C-import engine (Compiler::Bindgen).
    cp "$clang_dll" "${stage}/bin/libclang.dll"

    # Windows uses per-build stdlib .o (use_stdlib_archive:false for the
    # windows-gnu triple), so ship stdlib SOURCE only - no libcryo.a needed.
    mkdir -p "${stage}/stdlib"
    ( cd "${ROOT}/stdlib" && tar --exclude='.bin' -cf - . ) | ( cd "${stage}/stdlib" && tar -xf - )
    [ -f "${ROOT}/LICENSE" ] && cp "${ROOT}/LICENSE" "${stage}/LICENSE" || true
    stage_third_party_licenses "$stage"
    echo "$VER" > "${stage}/VERSION"

    local archive="${DIST}/cryo-${VER}-windows-x86_64.zip"
    rm -f "$archive"
    ( cd "$DIST" && zip -qr "$archive" "cryo-${VER}-windows-x86_64" )
    ( cd "$DIST" && sha256sum "$(basename "$archive")" > "$(basename "$archive").sha256" )
    rm -rf "$stage"
    echo "[build-release] wrote ${archive#$ROOT/}"
}

case "$PLATFORM" in
    linux)   build_linux ;;
    windows) build_windows ;;
esac
