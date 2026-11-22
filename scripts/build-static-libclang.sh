#!/usr/bin/env bash
#
# build-static-libclang - build libclang's C API as one static archive, for
# the `--release-static` Linux compiler.
#
#   scripts/build-static-libclang.sh            # -> .toolchains/libclang-static/libclang.a
#   scripts/build-static-libclang.sh --force    # rebuild it
#
# The C-import engine (Compiler::Bindgen) calls libclang's C API.  The LLVM
# apt packages ship that API only as a shared object (libclang-N.so.1);
# libclang-N-dev ships the archives of the clang libraries underneath it but
# not the C API itself.  So this compiles the C API's sources
# (clang/tools/libclang, from the official clang source release of the SAME
# version as the installed LLVM) against the installed clang headers, and
# merges those objects with every clang archive they need into ONE archive.
# One archive needs no link order: the linker rescans an archive until nothing
# new resolves, and the clang libraries refer to each other in cycles.
#
# The archive holds no LLVM: the static link takes LLVM from llvm-config's
# component archives, as it does for the compiler's own LLVM calls.
#
# The compiler's `[link.release-static]` section (compiler/cryoconfig and
# tools/CryoLSP/cryoconfig) names the archive this writes.
#
# Requires (host): llvm-N-dev, libclang-N-dev, a C++ compiler (c++ / CXX),
# curl, tar (xz), ar.  N and the source digest come from
# scripts/llvm-version.env.

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# shellcheck source=llvm-version.env
. "${ROOT}/scripts/llvm-version.env"

PREFIX="${ROOT}/.toolchains/libclang-static"
ARCHIVE="${PREFIX}/libclang.a"
# The llvm-config the `--release-static` link reads its LLVM archives from.
LLVM_CONFIG="${CRYO_LLVM_CONFIG:-llvm-config-${LLVM_MAJOR}}"
CXX="${CXX:-c++}"

force=0
for arg in "$@"; do
    case "$arg" in
        -f|--force) force=1 ;;
        -h|--help)  sed -n '3,27p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "build-static-libclang: unknown argument $arg (try --help)" >&2; exit 2 ;;
    esac
done

if [ -f "$ARCHIVE" ] && [ "$force" = 0 ]; then
    echo "[libclang-static] ${ARCHIVE} already built (--force rebuilds it)"
    exit 0
fi

command -v "$LLVM_CONFIG" >/dev/null 2>&1 \
    || { echo "error: ${LLVM_CONFIG} not found - install llvm-${LLVM_MAJOR}-dev or set CRYO_LLVM_CONFIG" >&2; exit 1; }
INSTALLED="$("$LLVM_CONFIG" --version)"
# The C API's sources must be the installed clang's own: they include clang's
# internal headers, whose declarations change between releases.
if [ "$INSTALLED" != "$CLANG_SOURCE_VERSION" ]; then
    echo "error: the installed LLVM is ${INSTALLED}, but scripts/llvm-version.env pins the clang source ${CLANG_SOURCE_VERSION}; update CLANG_SOURCE_VERSION and CLANG_SOURCE_SHA256 together" >&2
    exit 1
fi
LIBDIR="$("$LLVM_CONFIG" --libdir)"
TARGETS="${LIBDIR}/cmake/clang/ClangTargets.cmake"
[ -f "$TARGETS" ] && [ -f "${LIBDIR}/libclangAST.a" ] \
    || { echo "error: the clang static libraries are missing under ${LIBDIR} - install libclang-${LLVM_MAJOR}-dev" >&2; exit 1; }

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

ASSET="clang-${CLANG_SOURCE_VERSION}.src.tar.xz"
URL="https://github.com/llvm/llvm-project/releases/download/llvmorg-${CLANG_SOURCE_VERSION}/${ASSET}"
echo "[libclang-static] fetching ${ASSET}"
curl -fsSL -o "${WORK}/${ASSET}" "$URL"
echo "${CLANG_SOURCE_SHA256}  ${WORK}/${ASSET}" | sha256sum -c --quiet - \
    || { echo "error: ${ASSET} does not match CLANG_SOURCE_SHA256" >&2; exit 1; }
SRC="${WORK}/clang-${CLANG_SOURCE_VERSION}.src"
tar -xJf "${WORK}/${ASSET}" -C "$WORK" \
    "clang-${CLANG_SOURCE_VERSION}.src/tools/libclang" "clang-${CLANG_SOURCE_VERSION}.src/include"
LIBCLANG_SRC="${SRC}/tools/libclang"

# The C API's sources, from its CMakeLists `SOURCES` list (the lines between
# `set(SOURCES` and `ADDITIONAL_HEADERS`).
mapfile -t sources < <(awk '/^set\(SOURCES/ {on=1; next} /ADDITIONAL_HEADERS/ {on=0} on && /\.cpp/ {print $1}' \
    "${LIBCLANG_SRC}/CMakeLists.txt")
[ "${#sources[@]}" -gt 0 ] || { echo "error: no sources read from ${LIBCLANG_SRC}/CMakeLists.txt" >&2; exit 1; }

# The clang libraries the C API links: its CMakeLists `LIBS` list, closed over
# each library's INTERFACE_LINK_LIBRARIES in the installed ClangTargets.cmake.
# Only clang* entries are followed; LLVM comes from llvm-config at the link.
mapfile -t pending < <(awk '/^set\(LIBS/ {on=1; next} on && /^\)/ {on=0} on {print $1}' \
    "${LIBCLANG_SRC}/CMakeLists.txt")
if grep -q '^#define CLANG_ENABLE_ARCMT 1' "$("$LLVM_CONFIG" --includedir)/clang/Config/config.h"; then
    pending+=(clangARCMigrate)
fi
declare -A seen=()
libraries=()
while [ "${#pending[@]}" -gt 0 ]; do
    library="${pending[0]}"; pending=("${pending[@]:1}")
    case "$library" in clang*) ;; *) continue ;; esac
    [ -n "${seen[$library]:-}" ] && continue
    seen[$library]=1
    [ -f "${LIBDIR}/lib${library}.a" ] \
        || { echo "error: ${LIBDIR}/lib${library}.a missing (needed by libclang)" >&2; exit 1; }
    libraries+=("$library")
    interface="$(awk -v target="set_target_properties(${library} PROPERTIES" \
        'index($0, target) == 1 {on=1; next} on && /INTERFACE_LINK_LIBRARIES/ {gsub(/.*INTERFACE_LINK_LIBRARIES "|".*/, ""); print; exit} on && /^\)/ {exit}' \
        "$TARGETS")"
    IFS=';' read -r -a dependencies <<< "$interface"
    pending+=("${dependencies[@]}")
done

echo "[libclang-static] compiling ${#sources[@]} C API sources"
# _CINDEX_LIB_ marks the C API's own definitions, as libclang's build does.
# The source's own include/ comes first: the packages leave out headers that
# are not generated (clang/ExtractAPI/APIRecords.inc), and the generated ones
# (the .inc tables) are found in the installed include directory after it.
read -r -a cxxflags <<< "$("$LLVM_CONFIG" --cxxflags)"
objects=()
for source in "${sources[@]}"; do
    object="${WORK}/${source%.cpp}.o"
    "$CXX" -I"${SRC}/include" "${cxxflags[@]}" -O2 -D_CINDEX_LIB_ -I"$LIBCLANG_SRC" \
        -c "${LIBCLANG_SRC}/${source}" -o "$object"
    objects+=("$object")
done

echo "[libclang-static] merging with ${#libraries[@]} clang libraries"
mkdir -p "$PREFIX"
rm -f "${ARCHIVE}.tmp"
{
    echo "create ${ARCHIVE}.tmp"
    for object in "${objects[@]}"; do echo "addmod ${object}"; done
    for library in "${libraries[@]}"; do echo "addlib ${LIBDIR}/lib${library}.a"; done
    echo "save"
    echo "end"
} > "${WORK}/merge.mri"
ar -M < "${WORK}/merge.mri"
mv "${ARCHIVE}.tmp" "$ARCHIVE"
echo "[libclang-static] wrote ${ARCHIVE}"
