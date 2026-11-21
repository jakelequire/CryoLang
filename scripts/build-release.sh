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
# linked against libLLVM, libclang and the C++ runtime, so the binary has no
# libLLVM-20.so or libclang runtime dependency.  Honors the static-link env knobs, which is how the same
# script produces a fully-static MUSL binary when run inside an Alpine
# container (set before invoking):
#     CRYO_LLVM_CONFIG=llvm-config   # Alpine's musl-built llvm-config
#     CRYO_CC=clang                  # Alpine's musl clang as the link driver
# On a glibc host with neither set it produces a fully-static glibc binary.
#
# Host prerequisites:
#   linux:   llvm-20-dev + libpolly-20-dev (the static Polly libs llvm-config
#            lists) + libclang-20-dev (the clang archives
#            scripts/build-static-libclang.sh merges), the archives of the
#            system libraries LLVM uses (libzstd-dev, zlib1g-dev,
#            libxml2-dev), a C toolchain with static libstdc++ (gcc, g++),
#            curl, make, tar.
#            (Alpine/musl: llvm20-static llvm20-dev clang lld musl-dev
#             libstdc++-dev + the static C++ runtime; gcompat to run the pin.)
#   windows: x86_64-w64-mingw32-gcc + binutils, the fetched .toolchains (run
#            scripts/fetch-windows-llvm.sh first; llvm-mingw's clang.exe
#            drives the smoke link under wine), wine, zip.
#
# WINDOWS is NOT static (no official static windows-gnu libLLVM): the zip
# ships cryo.exe next to LLVM-C.dll (its runtime dep).
#
# Both ship the stdlib archives of the default profiles (release and --dev),
# built by the shipped compiler under `stdlib/.bin/<triple>/prebuilt/`, and the
# runtime source with the archive of its default configuration (hosted,
# unwind) under `runtime/.bin/<triple>/`.  An archive's key is the compiler's
# bytes, the sources and the options, not where they live, so an installed
# compiler links them as they are; any other it builds on first use.

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DIST="${ROOT}/dist"
PLATFORM="${1:-}"
ALLOW_UNSMOKED=0
# --dynamic (linux): link cryo and cryolsp against the host's shared LLVM,
# for a host without the static LLVM libraries.  The tarball then runs only
# where those libraries are installed, so it is named `-dynamic` and no
# release ever ships it; `make install-check` builds one to test the
# installers with.
DYNAMIC=0
shift || true
for arg in "$@"; do
    case "$arg" in
        --allow-unsmoked) ALLOW_UNSMOKED=1 ;;
        --dynamic) DYNAMIC=1 ;;
        *) echo "build-release.sh: unknown argument $arg" >&2; exit 2 ;;
    esac
done

case "$PLATFORM" in
    linux|windows) ;;
    *) echo "usage: build-release.sh <linux|windows> [--allow-unsmoked] [--dynamic]" >&2; exit 1 ;;
esac

# Single source of truth for the version.
VER="$(grep -oE 'const VERSION:[[:space:]]*string[[:space:]]*=[[:space:]]*"[^"]+"' \
        "${ROOT}/compiler/src/main.cryo" | sed -E 's/.*"([^"]+)".*/\1/')"
[ -n "$VER" ] || { echo "error: could not read VERSION from compiler/src/main.cryo" >&2; exit 1; }

echo "[build-release] cryo v${VER} -> ${PLATFORM}-x86_64"
mkdir -p "$DIST"

stage_common() {
    # $1 = staging dir.  Copies the stdlib source, LICENSE, VERSION.
    local stage="$1"
    mkdir -p "${stage}/stdlib"
    ( cd "${ROOT}/stdlib" && tar --exclude='.bin' -cf - . ) | ( cd "${stage}/stdlib" && tar -xf - )
    [ -f "${ROOT}/LICENSE" ] && cp "${ROOT}/LICENSE" "${stage}/LICENSE" || true
    stage_third_party_licenses "$stage"
    echo "$VER" > "${stage}/VERSION"
}

stage_runtime() {
    # $1 = staging dir.  Copies the runtime source, beside the stdlib's.
    #
    # HARD requirement, not an extra: codegen emits a call to the external
    # `__cryo_panic` for every panic, and the definition comes from the
    # runtime archive on the link line, which the compiler builds from these
    # sources on first use (the `<bindir>/../runtime` it looks in).  A
    # distribution without them produces a compiler that cannot link ANY
    # program.  The smoke builds the archive of the default configuration,
    # and it ships beside the sources (smoke_stage).
    local stage="$1"
    [ -f "${ROOT}/runtime/lib.cryo" ] || { echo "error: no runtime source at ${ROOT}/runtime" >&2; exit 1; }
    mkdir -p "${stage}/runtime"
    ( cd "${ROOT}/runtime" && tar --exclude='.bin' -cf - . ) | ( cd "${stage}/runtime" && tar -xf - )
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
    # The first smoke builds the stdlib archives of the default profiles, and
    # the runtime archive of the default configuration, with the staged
    # compiler; they ship, and the trees are then marked shipped, so an
    # installed compiler only reads them and builds any other archive in the
    # user's cache.  The second runs from a copy of the tree in another
    # directory, as an install is, with an empty cache: an archive it built
    # there, or a file it changed in the tree, is one every installed
    # compiler would build again.
    rm -rf "${stage}/stdlib/.bin" "${stage}/runtime/.bin"
    make -C "$ROOT" cryogate >/dev/null
    # Named with the host's suffix: a checkout shared with another OS holds
    # that OS's cryogate beside this one's.
    local cryogate="${ROOT}/tools/CryoGate/build/cryogate"
    if host_is windows; then cryogate="${cryogate}.exe"; fi
    env "${smoke_env[@]}" "$cryogate" release-smoke "$stage" "$exe" "${runner[@]}"
    trim_stdlib_archives "$stage"
    trim_runtime_archives "$stage"
    touch "${stage}/stdlib/.bin/shipped" "${stage}/runtime/.bin/shipped"
    local moved="${stage}.moved" home="${stage}.home" home_spec shipped
    rm -rf "$moved" "$home"
    cp -R "$stage" "$moved"
    mkdir -p "$home"
    home_spec="$home"
    if [ "${runner[*]:-}" = wine ]; then home_spec="Z:$(printf '%s' "$home" | tr '/' '\\')"; fi
    shipped="$(cd "$stage" && find stdlib/.bin runtime/.bin -type f | sort | xargs sha256sum)"
    env "${smoke_env[@]}" "CRYO_HOME=${home_spec}" "$cryogate" release-smoke "$moved" "$exe" "${runner[@]}"
    [ "$shipped" = "$(cd "$moved" && find stdlib/.bin runtime/.bin -type f | sort | xargs sha256sum)" ] \
        || { echo "error: a copy of the staged tree elsewhere changed its shipped stdlib or runtime archives" >&2; exit 1; }
    local built
    built="$(find "$home" -name '*.manifest')"
    [ -z "$built" ] \
        || { echo "error: a copy of the staged tree elsewhere built archives it does not ship:" >&2; echo "$built" >&2; exit 1; }
    rm -rf "$moved" "$home"
}

trim_runtime_archives() {
    # $1 = staging dir.  Keeps what a program's link reads from each runtime
    # archive directory: the archive and its manifest.
    local stage="$1" home
    for home in "${stage}"/runtime/.bin/*/*/*/; do
        [ -f "${home}runtime.manifest" ] || { echo "error: ${home} holds no runtime manifest" >&2; exit 1; }
        find "$home" -type f ! -name libcryort.a ! -name runtime.manifest -delete
        find "$home" -type d -empty -delete
    done
}

trim_stdlib_archives() {
    # $1 = staging dir.  Keeps what a program's link reads from each archive
    # directory: the archive, its manifest, and the objects of the modules
    # with startup code, which the link names on its own.  The stdlib build's
    # other objects, its IR and its incremental records are left behind.
    local stage="$1" home keep file
    for home in "${stage}"/stdlib/.bin/*/prebuilt/*/; do
        [ -f "${home}stdlib.manifest" ] || { echo "error: ${home} holds no stdlib manifest" >&2; exit 1; }
        keep="$(awk '$1 == "module" && $3 == "1" { print $5 }' "${home}stdlib.manifest")"
        while IFS= read -r file; do
            case "${file#"$home"}" in
                libcryo.a|stdlib.manifest) ;;
                *) printf '%s\n' "$keep" | grep -qxF "${file#"$home"}" || rm -f "$file" ;;
            esac
        done < <(find "$home" -type f)
        find "$home" -type d -empty -delete
    done
    # What the smoke's link driver answered about itself describes the
    # build machine's toolchain, not the user's.
    rm -rf "${stage}"/stdlib/.bin/*/toolchain
}

stage_lsp() {
    # $1 = staging dir, $2 = the compiler that builds it, $3 = the binary's
    # name, then the build's own options.  The language server ships beside
    # the compiler, built by the compiler that ships, from the same tree.
    local stage="$1" compiler="$2" name="$3"
    shift 3
    echo "[build-release] building ${name}"
    rm -rf "${ROOT}/tools/CryoLSP/build/release"
    ( cd "${ROOT}/tools/CryoLSP" && CRYO_STDLIB="${ROOT}/stdlib" \
        "$compiler" build --no-incremental --build-dir=build/release "$@" )
    [ -f "${ROOT}/tools/CryoLSP/build/release/${name}" ] \
        || { echo "error: ${name} not produced at tools/CryoLSP/build/release" >&2; exit 1; }
    cp "${ROOT}/tools/CryoLSP/build/release/${name}" "${stage}/bin/${name}"
}

pack_zip() {
    # $1 = archive, $2 = directory below $DIST.  `zip` where it is installed;
    # a Windows host packs with the tar that ships with Windows, which
    # writes a zip for a .zip name.
    if command -v zip >/dev/null 2>&1; then
        ( cd "$DIST" && zip -qr "$1" "$2" )
    else
        ( cd "$DIST" && "${SYSTEMROOT:-C:/Windows}/System32/tar.exe" -a -c -f "$(basename "$1")" "$2" )
    fi
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

    echo "[build-release] building the bootstrap compiler"
    make -C "$ROOT" cryo >/dev/null

    local link=(--release-static) name="cryo-${VER}-linux-x86_64"
    if [ "$DYNAMIC" = 1 ]; then
        link=(--profile=release); name="${name}-dynamic"
    else
        # The static libclang `[link.release-static]` names.
        "${ROOT}/scripts/build-static-libclang.sh"
    fi
    echo "[build-release] linking cryo (${link[*]})"
    rm -rf "${ROOT}/compiler/build/release-static"
    ( cd "${ROOT}/compiler" && CRYO_STDLIB="${ROOT}/stdlib" \
        ./build/cryo build "${link[@]}" --no-incremental \
        --build-dir=build/release-static )

    local out="${ROOT}/compiler/build/release-static/cryo"
    [ -x "$out" ] || { echo "error: static cryo not produced at $out" >&2; exit 1; }
    cp "$out" "${stage}/bin/cryo"
    strip "${stage}/bin/cryo" 2>/dev/null || true
    # The checkout it was built from, for `cryo version`; `make cryo` and
    # `make cryo-exe` stamp the binaries they link the same way.  Stamped
    # after stripping: the stamp records the size and digest of the bytes
    # that ship, which the compiler then reads instead of hashing itself.
    ( cd "$ROOT" && "${ROOT}/bin/cryo" stamp "${stage}/bin/cryo" --profile=release )

    stage_lsp "$stage" "${ROOT}/compiler/build/cryo" cryolsp "${link[@]}"
    strip "${stage}/bin/cryolsp" 2>/dev/null || true

    # Confirm both really are fully static: no libLLVM runtime dep, and no
    # dynamic loader at all (glibc ldd: "not a dynamic executable";
    # musl ldd: "not a valid dynamic program").
    if [ "$DYNAMIC" = 0 ] && command -v ldd >/dev/null 2>&1; then
        local ldd_out binary
        for binary in cryo cryolsp; do
            ldd_out="$(ldd "${stage}/bin/${binary}" 2>&1 || true)"
            if echo "$ldd_out" | grep -qiE 'LLVM|clang'; then
                echo "error: shipped ${binary} still links libLLVM or libclang dynamically" >&2; exit 1
            fi
            if ! echo "$ldd_out" | grep -qiE 'not a dynamic executable|not a valid dynamic program|statically linked'; then
                echo "error: shipped ${binary} is not fully static; ldd reports:" >&2
                echo "$ldd_out" >&2; exit 1
            fi
        done
    fi

    stage_common "$stage"
    stage_runtime "$stage"
    cp "${ROOT}/scripts/install-dependencies.sh" "${stage}/install-dependencies.sh"
    chmod +x "${stage}/install-dependencies.sh"

    if host_is linux; then smoke_stage "$stage" "cryo" 0; else smoke_stage "$stage" "cryo" 1; fi

    local archive="${DIST}/${name}.tar.gz"
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

    # A Windows host builds cryo.exe and the server natively; any other
    # cross-builds them with the host compiler.
    local exe="${ROOT}/compiler/build/cryo.exe"
    if host_is windows; then
        echo "[build-release] building cryo.exe"
        make -C "$ROOT" cryo >/dev/null
        [ -f "$exe" ] || { echo "error: cryo.exe not produced at $exe" >&2; exit 1; }
        cp "$exe" "${stage}/bin/cryo.exe"
        stage_lsp "$stage" "$exe" cryolsp.exe
    else
        echo "[build-release] cross-building cryo.exe"
        make -C "$ROOT" cryo-exe >/dev/null
        [ -f "$exe" ] || { echo "error: cryo.exe not produced at $exe" >&2; exit 1; }
        cp "$exe" "${stage}/bin/cryo.exe"
        CRYO_CC="clang-20 --target=x86_64-w64-mingw32 -fuse-ld=lld" \
            stage_lsp "$stage" "${ROOT}/compiler/build/cryo" cryolsp.exe --target=x86_64-pc-windows-gnu
    fi
    cp "$dll" "${stage}/bin/LLVM-C.dll"
    # libclang.dll: runtime dep of the C-import engine (Compiler::Bindgen).
    cp "$clang_dll" "${stage}/bin/libclang.dll"

    stage_common "$stage"
    stage_runtime "$stage"
    cp "${ROOT}/scripts/install-dependencies.ps1" "${stage}/install-dependencies.ps1"
    cp "${ROOT}/scripts/user-setup.ps1" "${stage}/user-setup.ps1"

    if host_is windows; then
        smoke_stage "$stage" "cryo.exe" 0
    else
        smoke_stage "$stage" "cryo.exe" 1 "${ROOT}/.toolchains/llvm-mingw/bin/clang.exe"
    fi

    local archive="${DIST}/cryo-${VER}-windows-x86_64.zip"
    rm -f "$archive"
    pack_zip "$archive" "cryo-${VER}-windows-x86_64"
    ( cd "$DIST" && sha256sum "$(basename "$archive")" > "$(basename "$archive").sha256" )
    rm -rf "$stage"
    echo "[build-release] wrote ${archive#$ROOT/}"
}

case "$PLATFORM" in
    linux)   build_linux ;;
    windows) build_windows ;;
esac
