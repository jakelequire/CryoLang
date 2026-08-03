#!/usr/bin/env bash
#
# install-llvm (CI) - install the LLVM toolchain on a GitHub Actions ubuntu
# runner, with a tarball cache so warm runs never touch apt.llvm.org.
#
# The workflows restore ~/.cache/cryo-ci via actions/cache (keyed on
# scripts/llvm-version.env + the runner OS/arch), then run this script:
#
#   cache hit  -> unpack the toolchain tarball into / (no network)
#   cache miss -> install from apt.llvm.org, then write the tarball so the
#                 post-job cache step uploads it for next time
#
# Always installs llvm-X-dev, clang-X AND libpolly-X-dev (the static Polly
# libs `llvm-config --link-static` lists) so a single cache entry serves
# every job, including the --release-static release build.
#
# The version comes from scripts/llvm-version.env (single source of truth).

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
# shellcheck source=../llvm-version.env
. "${ROOT}/scripts/llvm-version.env"

CACHE_DIR="${CRYO_CI_CACHE_DIR:-$HOME/.cache/cryo-ci}"
TARBALL="${CACHE_DIR}/llvm-${LLVM_MAJOR}-toolchain.tar.zst"

# The C-import engine (Compiler::Bindgen) drives libclang, and libclang locates
# its resource directory - the one holding stddef.h - relative to the shared
# object it was loaded from, NOT from /usr/lib/llvm-N.  Loading the multiarch
# /usr/lib/x86_64-linux-gnu/libclang-N.so.1 therefore resolves it to
# /usr/lib/clang/N, a path outside the llvm-N tree.
#
# This matters because compiler/llvm_bindings.h includes <stddef.h> so `size_t`
# maps to u64.  With the resource headers missing the include silently fails,
# libclang falls back to implicit-int, and every size_t parameter becomes i32 -
# surfacing much later as `error[E0214]: expected i32, found u64` on the length
# arguments of LLVMGetInlineAsm and friends, with nothing pointing at the cache.
# Probe libclang, NOT the clang driver.  The driver finds its resource dir from
# /usr/bin/clang-N and keeps working when libclang's own lookup is broken, so a
# `clang -fsyntax-only` check passes on exactly the restores that then fail the
# build.  c-index-test links the same libclang the C-import engine loads, so it
# fails in the cases that matter.
clang_resource_ok() {
    printf '#include <stddef.h>\nsize_t probe(void);\n' > /tmp/cryo-cimport-probe.c
    if ! command -v "c-index-test-${LLVM_MAJOR}" >/dev/null 2>&1; then
        return 1
    fi
    ! "c-index-test-${LLVM_MAJOR}" -test-load-source all /tmp/cryo-cimport-probe.c 2>&1 \
        | grep -qi 'error\|fatal'
}

toolchain_ok() {
    "clang-${LLVM_MAJOR}" --version >/dev/null 2>&1 \
        && "llvm-config-${LLVM_MAJOR}" --libs core >/dev/null 2>&1 \
        && [ -e "/usr/lib/llvm-${LLVM_MAJOR}/lib/libclang-${LLVM_MAJOR}.so.1" ] \
        && clang_resource_ok
}

install_from_apt() {
    echo "[install-llvm] installing LLVM ${LLVM_MAJOR} from apt.llvm.org"
    wget -qO /tmp/llvm.sh https://apt.llvm.org/llvm.sh
    chmod +x /tmp/llvm.sh
    sudo /tmp/llvm.sh "${LLVM_MAJOR}"
    # libclang1-N provides the libclang-N.so the C-import engine (Compiler::Bindgen)
    # links against - /usr/lib/llvm-N/lib/libclang-N.so.1 (a symlink into the
    # multiarch dir, captured by write_cache_tarball below).
    sudo apt-get install -y "llvm-${LLVM_MAJOR}-dev" "clang-${LLVM_MAJOR}" \
        "libpolly-${LLVM_MAJOR}-dev" "libclang1-${LLVM_MAJOR}"
}

write_cache_tarball() {
    echo "[install-llvm] writing the toolchain tarball for the CI cache"
    mkdir -p "$CACHE_DIR"
    # Ask dpkg which files the packages own rather than hand-maintaining a list
    # of paths.  Guessing paths is what produced a tarball that restored a
    # toolchain whose libclang could not resolve <stddef.h>: clang's resource
    # headers are not all under /usr/lib/llvm-N, and exactly where they land
    # varies with the packaging.  Capturing the package file lists makes a
    # restore equal to the fresh install by construction.
    local pkgs=(
        "llvm-${LLVM_MAJOR}" "llvm-${LLVM_MAJOR}-dev" "llvm-${LLVM_MAJOR}-runtime"
        "clang-${LLVM_MAJOR}" "libclang1-${LLVM_MAJOR}"
        "libclang-common-${LLVM_MAJOR}-dev" "libclang-cpp${LLVM_MAJOR}"
        "libpolly-${LLVM_MAJOR}-dev" "libllvm${LLVM_MAJOR}"
    )
    local present=()
    local p
    for p in "${pkgs[@]}"; do
        if dpkg -s "$p" >/dev/null 2>&1; then present+=("$p"); fi
    done
    # Regular files and symlinks only: `dpkg -L` also prints the directories
    # holding them, and archiving a directory would sweep in unrelated siblings.
    dpkg -L "${present[@]}" \
        | while IFS= read -r f; do
              if [ -f "$f" ] || [ -L "$f" ]; then printf '%s\n' "$f"; fi
          done > /tmp/llvm-cache-files.txt
    echo "[install-llvm] archiving $(wc -l < /tmp/llvm-cache-files.txt) files" \
         "from ${#present[@]} package(s)"
    sudo tar --zstd -cf "$TARBALL" -T /tmp/llvm-cache-files.txt
    sudo chmod 644 "$TARBALL"
}

if [ -f "$TARBALL" ]; then
    echo "[install-llvm] cache hit: unpacking ${TARBALL}"
    sudo tar --zstd -xf "$TARBALL" -C /
    sudo ldconfig
    if ! toolchain_ok; then
        # A runner-image update can theoretically strand a cached toolchain;
        # fall back to a fresh install rather than failing the job.
        echo "[install-llvm] cached toolchain failed its smoke test; reinstalling"
        install_from_apt
    fi
else
    echo "[install-llvm] cache miss"
    install_from_apt
    write_cache_tarball
fi

echo "[install-llvm] $("clang-${LLVM_MAJOR}" --version | head -1)"
echo "[install-llvm] llvm-config: $("llvm-config-${LLVM_MAJOR}" --version)"
