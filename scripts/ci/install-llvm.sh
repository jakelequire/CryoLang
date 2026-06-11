#!/usr/bin/env bash
#
# install-llvm (CI) — install the LLVM toolchain on a GitHub Actions ubuntu
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

toolchain_ok() {
    "clang-${LLVM_MAJOR}" --version >/dev/null 2>&1 \
        && "llvm-config-${LLVM_MAJOR}" --libs core >/dev/null 2>&1
}

install_from_apt() {
    echo "[install-llvm] installing LLVM ${LLVM_MAJOR} from apt.llvm.org"
    wget -qO /tmp/llvm.sh https://apt.llvm.org/llvm.sh
    chmod +x /tmp/llvm.sh
    sudo /tmp/llvm.sh "${LLVM_MAJOR}"
    sudo apt-get install -y "llvm-${LLVM_MAJOR}-dev" "clang-${LLVM_MAJOR}" \
        "libpolly-${LLVM_MAJOR}-dev"
}

write_cache_tarball() {
    echo "[install-llvm] writing the toolchain tarball for the CI cache"
    mkdir -p "$CACHE_DIR"
    # Everything the build needs: the self-contained /usr/lib/llvm-X tree,
    # the runtime libLLVM.so in the multiarch dir, and the /usr/bin/<tool>-X
    # entry points into the tree.
    sudo tar --zstd -cf "$TARBALL" \
        "/usr/lib/llvm-${LLVM_MAJOR}" \
        /usr/lib/x86_64-linux-gnu/libLLVM*"${LLVM_MAJOR}"* \
        /usr/bin/*-"${LLVM_MAJOR}"
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
