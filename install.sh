#!/usr/bin/env bash
#
# Cryo Programming Language — installer (v0.2.0).
#
# Symlinks the committed self-hosted compiler at <repo>/bin/cryo into a
# system bindir, and the standard library at <repo>/stdlib into a sibling
# share dir.  No build step — the pinned binary is self-contained.  No
# shell-rc edits — the install prefix is expected to already be on PATH.
#
# Default layout (override with --prefix=…):
#   /usr/local/bin/cryo            → <repo>/bin/cryo
#   /usr/local/bin/cryolsp         → <repo>/bin/cryolsp     (or build output)
#   /usr/local/share/cryo/stdlib   → <repo>/stdlib
#
# The cryolsp symlink is created when a built LSP binary is found in the
# repo (preferring <repo>/bin/cryolsp, then <repo>/tools/CryoLSP/build/bin/
# cryolsp).  Skip with --no-lsp.  When installed, the VS Code Cryo
# Analyzer extension auto-detects cryolsp on $PATH from any project.
#
# Stdlib lookup at runtime, in priority order:
#   1. --stdlib=PATH                   (one-off CLI override)
#   2. $CRYO_STDLIB                    (explicit stdlib path env override)
#   3. $CRYO_HOME/stdlib               (install-root env; cross-platform)
#      $CRYO_HOME/share/cryo/stdlib    (FHS layout under install root)
#   4. <bindir>/../stdlib              (Linux only, via /proc/self/exe)
#      <bindir>/../share/cryo/stdlib   (Linux only, via /proc/self/exe)
#   5. [project] stdlib_root           (per-project pin in cryoconfig)
#   6. <project_root>/../stdlib        (legacy in-tree fallback)
#
# On Linux, this installer's symlink layout makes (4) work without any
# environment setup — running `/usr/local/bin/cryo` resolves to the
# repo's stdlib via /proc/self/exe.  On macOS or any non-Linux, (4)
# fails (no /proc); set $CRYO_HOME to the install share dir (we print
# the exact line at the end of `install`).
#
# Usage: ./install.sh [options]
#   -y, --yes        Skip the interactive confirmation
#       --prefix=DIR Install prefix (default: /usr/local)
#       --no-lsp     Skip the cryolsp symlink even if a build is present
#       --uninstall  Remove previously-installed symlinks
#   -h, --help       Show usage and exit
#

set -euo pipefail

# ----------------------------------------------------------------------------
# Colors
# ----------------------------------------------------------------------------
RED=$'\033[0;31m'
GREEN=$'\033[0;32m'
TEAL=$'\033[0;36m'
BLUE=$'\033[0;34m'
YELLOW=$'\033[0;33m'
BOLD=$'\033[1m'
RESET=$'\033[0m'

log_info()  { echo "${BLUE}${BOLD}[info]${RESET}    $*"; }
log_ok()    { echo "${GREEN}${BOLD}[ok]${RESET}      $*"; }
log_warn()  { echo "${YELLOW}${BOLD}[warn]${RESET}    $*"; }
log_error() { echo "${RED}${BOLD}[error]${RESET}   $*" >&2; }

die() { log_error "$*"; exit 1; }

print_banner() {
    echo -e "${TEAL}"
    echo -e "                  #               "
    echo -e "                = #^.             "
    echo -e "      =        ^# # #.            "
    echo -e "       ## ^##^# ##### #           "
    echo -e "       # ## # ## ### ## #         "
    echo -e "        ###^^# #(###=# #(=#       "
    echo -e "        ## # ## #   # ## #.#<     :::::::::  :::   :::  ::::::::   "
    echo -e "      ## # ## #       # ## # #.   :+:    :+: :+:   :+: :+:    :+:  "
    echo -e "   # # #-## #^                    +:+    +:+  +:+ +:+  +:+    +:+  "
    echo -e "   # # #=}# #<                    +#++:++#:    +#++:   +#+    +:+  "
    echo -e "      ## # ## #       # ## # #.   +#+    +#+    +#+    +#+    +#+  "
    echo -e "        ## # ## #   # ## #-#<     #+#    #+#    #+#    #+#    #+#  "
    echo -e "        ###<=# #(###=# #<^#       ###    ###    ###     ########   "
    echo -e "       # ## # ## ### ## #         "
    echo -e "       ## =##(# ##@## # "
    echo -e "      =        ^# # #.  "
    echo -e "                =.#<-  "
    echo -e "                  #   "
    echo -e "${RESET}"
    echo "${TEAL}${BOLD}                         Cryo Programming Language Installer${RESET}"
    echo
}

# ----------------------------------------------------------------------------
# Args
# ----------------------------------------------------------------------------
ASSUME_YES=0
PREFIX="/usr/local"
ACTION="install"
INSTALL_LSP=1

usage() {
    sed -n '3,41p' "$0" | sed 's/^# \{0,1\}//'
    exit 0
}

for arg in "$@"; do
    case $arg in
        -y|--yes)    ASSUME_YES=1 ;;
        --prefix=*)  PREFIX="${arg#--prefix=}" ;;
        --no-lsp)    INSTALL_LSP=0 ;;
        --uninstall) ACTION="uninstall" ;;
        -h|--help)   usage ;;
        *)           die "unknown argument: $arg (try --help)" ;;
    esac
done

# ----------------------------------------------------------------------------
# Locate repo root (resolve script's own dir, follow symlinks)
# ----------------------------------------------------------------------------
SCRIPT_PATH="$(readlink -f "$0" 2>/dev/null || python3 -c 'import os,sys; print(os.path.realpath(sys.argv[1]))' "$0")"
REPO_ROOT="$(dirname "$SCRIPT_PATH")"
SRC_BIN="${REPO_ROOT}/bin/cryo"
SRC_STDLIB="${REPO_ROOT}/stdlib"
SRC_STDLIB_ARCHIVE="${SRC_STDLIB}/.bin/libcryo.a"

# Locate cryolsp source.  Prefer the pinned-style path next to bin/cryo,
# fall back to the build output that `cryo build` produces.  Empty when
# neither exists — install.sh treats that as "no LSP to install".
SRC_LSP=""
if [ -x "${REPO_ROOT}/bin/cryolsp" ]; then
    SRC_LSP="${REPO_ROOT}/bin/cryolsp"
elif [ -x "${REPO_ROOT}/tools/CryoLSP/build/bin/cryolsp" ]; then
    SRC_LSP="${REPO_ROOT}/tools/CryoLSP/build/bin/cryolsp"
fi

[ -f "${REPO_ROOT}/Makefile" ] || die "could not find Makefile at ${REPO_ROOT} — is install.sh next to it?"

# ----------------------------------------------------------------------------
# Destination paths
# ----------------------------------------------------------------------------
DEST_BIN="${PREFIX}/bin/cryo"
DEST_LSP="${PREFIX}/bin/cryolsp"
DEST_SHARE="${PREFIX}/share/cryo"
DEST_STDLIB="${DEST_SHARE}/stdlib"

# ----------------------------------------------------------------------------
# Helpers
# ----------------------------------------------------------------------------
need_sudo_for() {
    # Returns 0 (true) iff the install prefix isn't writable by the current
    # user — in which case we'll prefix mutating commands with sudo.
    local target_dir
    target_dir="$(dirname "$1")"
    [ -w "$target_dir" ] || [ -w "$1" ] && return 1 || true
    if [ -e "$target_dir" ]; then
        [ -w "$target_dir" ] && return 1
        return 0
    fi
    # Walk up to the nearest existing ancestor.
    local cur="$target_dir"
    while [ ! -e "$cur" ]; do
        cur="$(dirname "$cur")"
    done
    [ -w "$cur" ] && return 1
    return 0
}

run() {
    if [ $USE_SUDO -eq 1 ]; then
        sudo "$@"
    else
        "$@"
    fi
}

# ----------------------------------------------------------------------------
# Pre-flight summary
# ----------------------------------------------------------------------------
print_banner

if [ "$ACTION" = "install" ]; then
    [ -x "$SRC_BIN" ] || die "$SRC_BIN is missing or not executable. Run 'make pin-cryo' to refresh the pinned binary, or check out a revision that has bin/cryo committed."
    [ -f "$SRC_STDLIB/lib.cryo" ] || die "$SRC_STDLIB/lib.cryo not found — is the stdlib in place?"

    NEED_STDLIB_BUILD=0
    [ -f "$SRC_STDLIB_ARCHIVE" ] || NEED_STDLIB_BUILD=1

    echo "This installer will:"
    if [ $NEED_STDLIB_BUILD -eq 1 ]; then
        echo "  • build the stdlib archive (${SRC_STDLIB_ARCHIVE#$REPO_ROOT/}) — ~1 second"
    fi
    echo "  • create symlinks:"
    echo "      ${DEST_BIN}"
    echo "        → ${SRC_BIN}"
    if [ $INSTALL_LSP -eq 1 ] && [ -n "$SRC_LSP" ]; then
        echo "      ${DEST_LSP}"
        echo "        → ${SRC_LSP}"
    fi
    echo "      ${DEST_STDLIB}"
    echo "        → ${SRC_STDLIB}"
    if [ $INSTALL_LSP -eq 1 ] && [ -z "$SRC_LSP" ]; then
        echo
        echo "  (no cryolsp build found in repo; LSP symlink will be skipped."
        echo "   build with 'cryo build' inside tools/CryoLSP to enable it.)"
    fi
    echo
    echo "Repo root: ${REPO_ROOT}"
    echo "Prefix:    ${PREFIX}"
    echo
else
    echo "This installer will remove the symlinks:"
    echo "  ${DEST_BIN}"
    echo "  ${DEST_LSP}        (if present)"
    echo "  ${DEST_STDLIB}"
    echo "  ${DEST_SHARE}      (only if empty after the stdlib symlink is gone)"
    echo
    echo "Prefix: ${PREFIX}"
    echo
fi

if [ $ASSUME_YES -ne 1 ]; then
    read -r -p "Continue? [y/N] " reply
    case "$reply" in
        y|Y|yes|YES) ;;
        *) die "cancelled" ;;
    esac
fi

# ----------------------------------------------------------------------------
# Sudo decision
# ----------------------------------------------------------------------------
USE_SUDO=0
if need_sudo_for "$DEST_BIN" || need_sudo_for "$DEST_LSP" || need_sudo_for "$DEST_STDLIB"; then
    USE_SUDO=1
    log_info "prefix '${PREFIX}' is not writable; will use sudo for install steps."
    if ! command -v sudo >/dev/null 2>&1; then
        die "sudo not found and prefix is not writable — re-run with --prefix=\$HOME/.local or as root."
    fi
fi

# ----------------------------------------------------------------------------
# OS check (warn-only on macOS — /proc/self/exe is Linux-only)
# ----------------------------------------------------------------------------
case "$(uname -s)" in
    Linux)  : ;;
    Darwin) log_warn "macOS install: /proc/self/exe-based stdlib lookup is Linux-only. After install, add 'export CRYO_HOME=${DEST_SHARE}' to your shell profile (or 'export CRYO_STDLIB=${DEST_STDLIB}')." ;;
    *)      log_warn "unsupported OS: $(uname -s) — proceeding, but stdlib auto-lookup may fail. Set CRYO_HOME=${DEST_SHARE} in your shell profile." ;;
esac

# ----------------------------------------------------------------------------
# Install / uninstall
# ----------------------------------------------------------------------------
do_install() {
    # Build the stdlib archive in the repo if it's missing.  The linker
    # reads <stdlib_root>/.bin/libcryo.a at link time; on a fresh clone
    # the archive doesn't exist yet, so we build it here once.  Runs in
    # the repo (no sudo) — only the symlink steps need elevated perms.
    if [ ! -f "$SRC_STDLIB_ARCHIVE" ]; then
        log_info "stdlib archive not found at ${SRC_STDLIB_ARCHIVE#$REPO_ROOT/} — building via 'make stdlib'..."
        ( cd "$REPO_ROOT" && make stdlib ) >/dev/null || die "'make stdlib' failed; re-run with the make output visible to debug."
        [ -f "$SRC_STDLIB_ARCHIVE" ] || die "make stdlib finished but ${SRC_STDLIB_ARCHIVE} is missing — something is wrong."
        log_ok "built: ${SRC_STDLIB_ARCHIVE#$REPO_ROOT/}"
    fi

    run mkdir -p "$(dirname "$DEST_BIN")" "$DEST_SHARE"

    # Replace any existing symlink/file at the destination with a fresh symlink.
    run ln -sfn "$SRC_BIN" "$DEST_BIN"
    log_ok "linked: ${DEST_BIN} → ${SRC_BIN}"

    if [ $INSTALL_LSP -eq 1 ] && [ -n "$SRC_LSP" ]; then
        run ln -sfn "$SRC_LSP" "$DEST_LSP"
        log_ok "linked: ${DEST_LSP} → ${SRC_LSP}"
    elif [ $INSTALL_LSP -eq 1 ]; then
        log_info "skipping cryolsp symlink — no built binary at <repo>/bin/cryolsp or <repo>/tools/CryoLSP/build/bin/cryolsp."
        log_info "build it with 'cryo build' in tools/CryoLSP, then re-run install.sh."
    fi

    run ln -sfn "$SRC_STDLIB" "$DEST_STDLIB"
    log_ok "linked: ${DEST_STDLIB} → ${SRC_STDLIB}"

    # Sanity: confirm the new binary runs.
    if "$DEST_BIN" --version >/dev/null 2>&1; then
        log_ok "cryo --version: $("$DEST_BIN" --version)"
    else
        log_warn "cryo --version exited non-zero — symlink in place but the binary failed to run."
    fi
}

do_uninstall() {
    if [ -L "$DEST_BIN" ] || [ -e "$DEST_BIN" ]; then
        run rm -f "$DEST_BIN"
        log_ok "removed: ${DEST_BIN}"
    else
        log_info "nothing to remove at ${DEST_BIN}"
    fi
    if [ -L "$DEST_LSP" ] || [ -e "$DEST_LSP" ]; then
        run rm -f "$DEST_LSP"
        log_ok "removed: ${DEST_LSP}"
    else
        log_info "nothing to remove at ${DEST_LSP}"
    fi
    if [ -L "$DEST_STDLIB" ] || [ -e "$DEST_STDLIB" ]; then
        run rm -f "$DEST_STDLIB"
        log_ok "removed: ${DEST_STDLIB}"
    else
        log_info "nothing to remove at ${DEST_STDLIB}"
    fi
    # Tidy empty share dir.
    if [ -d "$DEST_SHARE" ] && [ -z "$(ls -A "$DEST_SHARE" 2>/dev/null || true)" ]; then
        run rmdir "$DEST_SHARE" && log_ok "removed empty: ${DEST_SHARE}"
    fi
}

case "$ACTION" in
    install)   do_install ;;
    uninstall) do_uninstall ;;
esac

# ----------------------------------------------------------------------------
# Done
# ----------------------------------------------------------------------------
echo
echo "${GREEN}${BOLD}Done.${RESET}"
echo

if [ "$ACTION" = "install" ]; then
    if ! command -v cryo >/dev/null 2>&1; then
        log_warn "cryo is installed at ${DEST_BIN} but not on your PATH."
        log_warn "add this to your shell profile:  export PATH=\"$(dirname "$DEST_BIN"):\$PATH\""
    else
        echo "Try it out:"
        echo "  cryo --help"
        echo "  cryo --version"
    fi
    if [ $INSTALL_LSP -eq 1 ] && [ -n "$SRC_LSP" ]; then
        echo
        echo "cryolsp is on PATH — the VS Code Cryo Analyzer extension will pick it up"
        echo "automatically in any project (no cryo.languageServer.path setting needed)."
    fi
    echo
    # Cross-platform stdlib setup hint.  On Linux the symlink layout +
    # /proc/self/exe make this optional, but setting CRYO_HOME makes
    # things resilient against running the binary from a path that
    # doesn't have <bindir>/../stdlib (custom prefixes, copied binaries,
    # macOS, etc.) and is the canonical way to point Cryo at its
    # install root.
    echo "Recommended: pin Cryo's install root in your shell profile:"
    echo "  export CRYO_HOME=\"${DEST_SHARE}\""
    echo "(Optional on Linux when the symlink install is on PATH; required on macOS / other.)"
    echo
fi

echo "${TEAL}${BOLD}https://github.com/jakelequire/cryo${RESET}"
echo
