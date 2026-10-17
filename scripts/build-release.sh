#!/usr/bin/env bash
#
# build-release - build + package one platform's distributable Cryo toolchain.
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
#   windows: x86_64-w64-mingw32-gcc + binutils (the archiver for the windows
#            stdlib archive), the fetched .toolchains (run
#            scripts/fetch-windows-llvm.sh first; llvm-mingw's clang.exe
#            drives the smoke link under wine), wine, zip.
#
# WINDOWS is NOT static (no official static windows-gnu libLLVM): the zip
# ships cryo.exe next to LLVM-C.dll (its runtime dep).  Its stdlib archive is
# cross-built here for the windows triple.

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DIST="${ROOT}/dist"
PLATFORM="${1:-}"
ALLOW_UNSMOKED=0
shift || true
for arg in "$@"; do
    case "$arg" in
        --allow-unsmoked) ALLOW_UNSMOKED=1 ;;
        *) echo "build-release.sh: unknown argument $arg" >&2; exit 2 ;;
    esac
done

case "$PLATFORM" in
    linux|windows) ;;
    *) echo "usage: build-release.sh <linux|windows> [--allow-unsmoked]" >&2; exit 1 ;;
esac

# Single source of truth for the version.
VER="$(grep -oE 'const VERSION:[[:space:]]*string[[:space:]]*=[[:space:]]*"[^"]+"' \
        "${ROOT}/compiler/src/main.cryo" | sed -E 's/.*"([^"]+)".*/\1/')"
[ -n "$VER" ] || { echo "error: could not read VERSION from compiler/src/main.cryo" >&2; exit 1; }

echo "[build-release] cryo v${VER} -> ${PLATFORM}-x86_64"
mkdir -p "$DIST"

stage_common() {
    # $1 = staging dir, $2 = the stdlib archive built for the archive's target.
    # Copies the stdlib (source + archive), LICENSE, VERSION.
    #
    # The archive is a hard requirement on BOTH platforms: a host build - which
    # is what the shipped compiler does on the machine it is installed on -
    # puts `<stdlib>/.bin/libcryo.a` on every link line, so a tree without it
    # cannot link a program.
    local stage="$1" archive="$2"
    [ -f "$archive" ] || { echo "error: stdlib archive missing at ${archive} - user programs would not link" >&2; exit 1; }
    mkdir -p "${stage}/stdlib"
    # stdlib SOURCE only; the archive is placed explicitly below.
    ( cd "${ROOT}/stdlib" && tar --exclude='.bin' -cf - . ) | ( cd "${stage}/stdlib" && tar -xf - )
    # The build tree keeps one archive per target under `.bin/<triple>/`, but a
    # release holds exactly ONE target, so it is staged at the flat path -- the
    # layout the compiler falls back to and the one an installed tree has by
    # construction.  Staging flat rather than shipping the per-triple tree is
    # what keeps the release working when the installing host's toolchain probe
    # names its triple differently from the build host's.
    mkdir -p "${stage}/stdlib/.bin"
    cp "$archive" "${stage}/stdlib/.bin/libcryo.a"
    [ -f "${ROOT}/LICENSE" ] && cp "${ROOT}/LICENSE" "${stage}/LICENSE" || true
    stage_third_party_licenses "$stage"
    echo "$VER" > "${stage}/VERSION"
}

host_stdlib_archive() {
    # The archive `make stdlib` built for this host, at `.bin/<triple>/`.
    local triple
    triple="$("${ROOT}/bin/cryo" version --triple 2>/dev/null || true)"
    [ -n "$triple" ] || { echo "error: bin/cryo printed no triple" >&2; exit 1; }
    echo "${ROOT}/stdlib/.bin/${triple}/libcryo.a"
}

build_windows_stdlib() {
    # Cross-builds the stdlib archive for the windows triple, with the
    # compiler cryo.exe was built from, into `.bin/<triple>/` beside the host's.
    # `--target` compiles with the windows side of every config gate, so the
    # archive holds the same modules and symbols as one built on Windows.  The
    # mingw archiver writes the symbol index in the format the objects are in.
    local triple="x86_64-pc-windows-gnu"
    local cryo="${ROOT}/compiler/build/cryo"
    echo "[build-release] building the stdlib archive for ${triple}" >&2
    rm -rf "${ROOT}/stdlib/.bin/${triple}"
    ( cd "${ROOT}/stdlib" && CRYO_AR=x86_64-w64-mingw32-ar "$cryo" build --target="$triple" \
        --no-incremental --build-dir=".bin/${triple}" >/dev/null )
    echo "${ROOT}/stdlib/.bin/${triple}/libcryo.a"
}

stage_runtime_tiers() {
    # $1 = staging dir, $2 = target triple to build the tiers for.
    #
    # HARD requirement, not an extra: codegen emits a call to the external
    # `__cryo_panic_at` for every panic, and the definition comes from a panic tier
    # archive on the link line.  A distribution without these archives produces a
    # compiler that cannot link ANY program - it fails on an undefined
    # `__cryo_panic_at`.  They go in lib/cryo/<triple>/, which is the
    # `<bindir>/../lib/cryo` candidate the tier locator already searches, under
    # the per-triple subdirectory it prefers.
    #
    # The hosted CORE tier is checked separately below, because its absence fails
    # differently and worse: the locator would decline the directory, the link
    # would omit both the archive and `-Wl,-e,__cryo_entry`, and the program would
    # quietly go back to starting at crt0 rather than fail.
    local stage="$1" triple="$2"
    local cryo="${ROOT}/compiler/build/cryo"
    local dest="${stage}/lib/cryo/${triple}"

    echo "[build-release] building runtime tiers for ${triple}"
    ( cd "${ROOT}/runtime"        && "$cryo" build --target="$triple" >/dev/null )
    ( cd "${ROOT}/runtime/hosted" && "$cryo" build --target="$triple" >/dev/null )

    mkdir -p "$dest"
    local a found=0
    for a in "${ROOT}"/runtime/.bin/libcryort-*.a; do
        [ -f "$a" ] || continue
        cp "$a" "$dest/"; found=1
    done
    [ "$found" = 1 ] || { echo "error: no runtime tier archives produced for ${triple}" >&2; exit 1; }
    [ -f "${dest}/libcryort-panic-abort-hosted.a" ] \
        || { echo "error: hosted panic tier missing for ${triple} - user programs would not link" >&2; exit 1; }
    [ -f "${dest}/libcryort-core-hosted.a" ] \
        || { echo "error: hosted core tier missing for ${triple} - user programs would not start at the Cryo entry" >&2; exit 1; }
    [ -f "${dest}/libcryort-core.a" ] \
        || { echo "error: freestanding core tier missing for ${triple}" >&2; exit 1; }
}

smoke_stage() {
    # $1 = staging dir, $2 = exe name, $3 = 1 when the artifact is foreign here,
    # $4 = the link driver a foreign compiler runs, when it needs one.
    #
    # Nothing executed a release artifact before it was published: the tag
    # workflow builds the archive, uploads it and attaches it, and the verify
    # job beside it exercises a DIFFERENT binary built by a different link.
    # The one place an artifact is run at all asks it for `--version`, which is
    # whether it starts and not whether it works.
    #
    # This compiles a hello-world with the staged tree and runs it, before the
    # archive is packaged, so a tree that cannot compile is never wrapped up.
    local stage="$1" exe="$2" foreign="$3" driver="${4:-}"
    local runner=() smoke_env=()
    if [ "$foreign" = 1 ]; then
        if command -v wine >/dev/null 2>&1; then
            runner=(wine)
            # The staged cryo.exe builds as a Windows host, whose link driver
            # is `cc`, and wine has none.  A Windows program has to drive the
            # link: the compiler launches it through `cmd /c`, so its path is
            # spelled as wine's Z: drive sees the host filesystem.
            if [ -n "$driver" ]; then
                [ -f "$driver" ] || { echo "error: link driver ${driver} missing - run scripts/fetch-windows-llvm.sh first" >&2; exit 1; }
                smoke_env=("CRYO_CC=Z:$(printf '%s' "$driver" | tr '/' '\\')")
            fi
        elif [ "$ALLOW_UNSMOKED" = 1 ]; then
            echo "[build-release] NOT SMOKED: ${exe} cannot run on this host and"                  "no wine is installed (--allow-unsmoked).  The archive is being"                  "packaged without ever having compiled anything." >&2
            return 0
        else
            echo "error: cannot run ${exe} here to smoke the archive (no wine)." >&2
            echo "  Install wine, build on the target OS, or pass --allow-unsmoked" >&2
            echo "  and accept that the archive ships unexercised." >&2
            exit 1
        fi
    fi
    env "${smoke_env[@]}" bash "${ROOT}/scripts/release-smoke.sh" "$stage" "$exe" "${runner[@]}"
}

host_is() {   # $1 = linux|windows
    case "$(uname -s 2>/dev/null || echo unknown)" in
        Linux)                     [ "$1" = linux ] ;;
        MINGW*|MSYS*|CYGWIN*)      [ "$1" = windows ] ;;
        *)                         false ;;
    esac
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
    # The checkout it was built from, for `cryo version`; `make cryo` and
    # `make cryo-exe` stamp the binaries they link the same way.
    ( cd "$ROOT" && "${ROOT}/bin/cryo" stamp "$out" --profile=release )
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

    local archive
    archive="$(host_stdlib_archive)"
    stage_common "$stage" "$archive"
    stage_runtime_tiers "$stage" "x86_64-unknown-linux-gnu"

    if host_is linux; then smoke_stage "$stage" "cryo" 0; else smoke_stage "$stage" "cryo" 1; fi

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

    local stdlib_archive
    stdlib_archive="$(build_windows_stdlib)"
    stage_common "$stage" "$stdlib_archive"
    stage_runtime_tiers "$stage" "x86_64-pc-windows-gnu"

    if host_is windows; then
        smoke_stage "$stage" "cryo.exe" 0
    else
        smoke_stage "$stage" "cryo.exe" 1 "${ROOT}/.toolchains/llvm-mingw/bin/clang.exe"
    fi

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
