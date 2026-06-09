#!/usr/bin/env bash
#
# fetch-windows-toolchain — populate .toolchains/llvm-mingw-* on demand.
#
# Downloads a pre-built llvm-mingw distribution (LLVM 20.1.8 + mingw-w64
# ucrt) so a Linux machine can cross-compile Cryo for x86_64-pc-windows-gnu.
# The tarball is ~75 MB compressed, ~600 MB extracted — too large to
# commit, so we git-ignore .toolchains/ and fetch on demand instead.
#
# Idempotent: re-running is a no-op when the toolchain is already
# extracted.
#
# Once you have the toolchain, the build flow is:
#
#     scripts/fetch-windows-toolchain.sh           # one-time fetch
#     cd stdlib  && cryo build --target=x86_64-pc-windows-gnu
#     cd compiler && cryo build --target=x86_64-pc-windows-gnu
#     <link the resulting COFF objects against Windows libLLVM-20>
#
# Then `make pin` seals both pins in one shot (host-aware).
#
# Pinning the version: this script targets a *specific* llvm-mingw
# release so reproducible builds don't drift when upstream cuts a new
# tarball.  Bump RELEASE_TAG when you intentionally update LLVM.

set -euo pipefail

# Usage: scripts/fetch-windows-toolchain.sh [--force]
RELEASE_TAG="20250709"   # llvm-mingw 20250709 — ships LLVM 20.1.8
TARBALL="llvm-mingw-${RELEASE_TAG}-ucrt-ubuntu-22.04-x86_64.tar.xz"
URL="https://github.com/mstorsjo/llvm-mingw/releases/download/${RELEASE_TAG}/${TARBALL}"
EXPECTED_DIR="llvm-mingw-${RELEASE_TAG}-ucrt-ubuntu-22.04-x86_64"

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEST="${ROOT}/.toolchains"
TOOLCHAIN="${DEST}/${EXPECTED_DIR}"

force=0
for arg in "$@"; do
    case "$arg" in
        -f|--force) force=1 ;;
        -h|--help)
            sed -n '3,25p' "$0" | sed 's/^# \{0,1\}//'
            exit 0
            ;;
        *) echo "unknown arg: $arg (try --help)" >&2; exit 1 ;;
    esac
done

mkdir -p "$DEST"

if [[ -d "$TOOLCHAIN" && "$force" -eq 0 ]]; then
    echo "[fetch-windows-toolchain] already present: $TOOLCHAIN"
    echo "  (pass --force to redownload)"
    exit 0
fi

if [[ "$force" -eq 1 ]]; then
    rm -rf "$TOOLCHAIN"
fi

echo "[fetch-windows-toolchain] downloading $TARBALL (~75 MB)…"
cd "$DEST"
curl -L --fail --progress-bar -o "$TARBALL" "$URL"
echo "[fetch-windows-toolchain] extracting…"
tar -xJf "$TARBALL"
rm -f "$TARBALL"

echo "[fetch-windows-toolchain] ready: $TOOLCHAIN"
echo "  cross-clang:  $TOOLCHAIN/bin/x86_64-w64-mingw32-clang"
echo "  cross-gcc:    $TOOLCHAIN/bin/x86_64-w64-mingw32-gcc"
echo "  llvm libs:    $TOOLCHAIN/lib/libLLVM*"
