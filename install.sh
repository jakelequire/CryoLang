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
#   /usr/local/share/cryo/stdlib   → <repo>/stdlib
#
# The compiler reads /proc/self/exe at startup and looks for stdlib at
#   <bindir>/../stdlib            (this layout's repo-pointing symlink)
#   <bindir>/../share/cryo/stdlib (this layout's share-pointing symlink)
# in that order, so either resolution path lands on the repo's stdlib.
# Set $CRYO_STDLIB to override.
#
# Usage: ./install.sh [options]
#   -y, --yes        Skip the interactive confirmation
#       --prefix=DIR Install prefix (default: /usr/local)
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

usage() {
    sed -n '3,25p' "$0" | sed 's/^# \{0,1\}//'
    exit 0
}

for arg in "$@"; do
    case $arg in
        -y|--yes)    ASSUME_YES=1 ;;
        --prefix=*)  PREFIX="${arg#--prefix=}" ;;
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

[ -f "${REPO_ROOT}/Makefile" ] || die "could not find Makefile at ${REPO_ROOT} — is install.sh next to it?"

# ----------------------------------------------------------------------------
# Destination paths
# ----------------------------------------------------------------------------
DEST_BIN="${PREFIX}/bin/cryo"
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

    echo "This installer will create symlinks:"
    echo "  ${DEST_BIN}"
    echo "    → ${SRC_BIN}"
    echo "  ${DEST_STDLIB}"
    echo "    → ${SRC_STDLIB}"
    echo
    echo "Repo root: ${REPO_ROOT}"
    echo "Prefix:    ${PREFIX}"
    echo
else
    echo "This installer will remove the symlinks:"
    echo "  ${DEST_BIN}"
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
if need_sudo_for "$DEST_BIN" || need_sudo_for "$DEST_STDLIB"; then
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
    Darwin) log_warn "macOS install: the compiler's /proc/self/exe-based stdlib lookup is Linux-only. Set CRYO_STDLIB=${SRC_STDLIB} in your shell profile to use this install on macOS." ;;
    *)      log_warn "unsupported OS: $(uname -s) — proceeding, but stdlib lookup may fail. Set CRYO_STDLIB to override." ;;
esac

# ----------------------------------------------------------------------------
# Install / uninstall
# ----------------------------------------------------------------------------
do_install() {
    run mkdir -p "$(dirname "$DEST_BIN")" "$DEST_SHARE"

    # Replace any existing symlink/file at the destination with a fresh symlink.
    run ln -sfn "$SRC_BIN" "$DEST_BIN"
    log_ok "linked: ${DEST_BIN} → ${SRC_BIN}"

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
    echo
fi

echo "${TEAL}${BOLD}https://github.com/jakelequire/cryo${RESET}"
echo
