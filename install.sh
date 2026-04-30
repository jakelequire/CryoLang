#!/usr/bin/env bash
#
# Cryo Programming Language — installer (v0.1.0).
#
# Scope: this script is a PATH wrapper, not a system install. It builds the
# self-hosted cryo compiler in place and exposes `cryo` on your PATH by
# appending an export to a shell rc file. It does NOT copy binaries to
# /usr/local/bin or install the stdlib outside the repo. See the
# "in-tree limitation" note at the end of this script for why.
#
# Usage: ./install.sh [options]
#   -y, --yes          Skip the interactive confirmation
#       --no-build     Assume compiler/build/bin/cryo already exists
#       --shell=NAME   Pick which shell rc to update: bash | zsh | fish | none
#                      Default: detect from $SHELL.
#   -h, --help         Show usage and exit
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

log_info()    { echo "${BLUE}${BOLD}[info]${RESET}    $*"; }
log_ok()      { echo "${GREEN}${BOLD}[ok]${RESET}      $*"; }
log_warn()    { echo "${YELLOW}${BOLD}[warn]${RESET}    $*"; }
log_error()   { echo "${RED}${BOLD}[error]${RESET}   $*" >&2; }

die() { log_error "$*"; exit 1; }

# ----------------------------------------------------------------------------
# Banner — the only piece kept from the original install.sh
# ----------------------------------------------------------------------------
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
DO_BUILD=1
SHELL_TARGET=""

usage() {
    sed -n '3,18p' "$0" | sed 's/^# \{0,1\}//'
    exit 0
}

for arg in "$@"; do
    case $arg in
        -y|--yes)        ASSUME_YES=1 ;;
        --no-build)      DO_BUILD=0 ;;
        --shell=*)       SHELL_TARGET="${arg#--shell=}" ;;
        -h|--help)       usage ;;
        *)               die "unknown argument: $arg (try --help)" ;;
    esac
done

# ----------------------------------------------------------------------------
# Locate repo root (resolve the script's own directory, follow symlinks)
# ----------------------------------------------------------------------------
SCRIPT_PATH="$(readlink -f "$0" 2>/dev/null || python3 -c 'import os,sys; print(os.path.realpath(sys.argv[1]))' "$0")"
REPO_ROOT="$(dirname "$SCRIPT_PATH")"
CRYO_BIN="${REPO_ROOT}/compiler/build/bin/cryo"
BIN_DIR="$(dirname "$CRYO_BIN")"

[ -f "${REPO_ROOT}/Makefile" ] || die "could not find Makefile at ${REPO_ROOT} — is this script next to it?"

# ----------------------------------------------------------------------------
# Pre-flight summary
# ----------------------------------------------------------------------------
print_banner

echo "This installer will:"
echo "  1. Verify your toolchain (clang++-20, LLVM 20, GNU make)"
if [ $DO_BUILD -eq 1 ]; then
    echo "  2. Build the self-hosted cryo compiler via 'make cryo'  (~5 min the first time)"
    echo "  3. Add ${BIN_DIR} to your PATH via your shell rc"
else
    echo "  2. (skipping build — --no-build was passed)"
    echo "  3. Add ${BIN_DIR} to your PATH via your shell rc"
fi
echo
echo "Repo root:       ${REPO_ROOT}"
echo "Compiler binary: ${CRYO_BIN}"
echo

if [ $ASSUME_YES -ne 1 ]; then
    read -r -p "Continue? [y/N] " reply
    case "$reply" in
        y|Y|yes|YES) ;;
        *) die "cancelled" ;;
    esac
fi

# ----------------------------------------------------------------------------
# OS check
# ----------------------------------------------------------------------------
case "$(uname -s)" in
    Linux)   OS="linux" ;;
    Darwin)  OS="macos"; log_warn "macOS support is untested on this revision; proceeding anyway." ;;
    *)       die "unsupported OS: $(uname -s)" ;;
esac
log_info "OS detected: $OS"

# ----------------------------------------------------------------------------
# Toolchain checks (verify only — never auto-install)
# ----------------------------------------------------------------------------
have() { command -v "$1" >/dev/null 2>&1; }

require_cmd() {
    if have "$1"; then
        log_ok "found: $1  ($(command -v "$1"))"
    else
        log_error "missing required command: $1"
        echo "    Install hint:"
        case "$1" in
            clang++-20) echo "      Debian/Ubuntu: see https://apt.llvm.org/  ('wget -O - https://apt.llvm.org/llvm.sh | sudo bash -s -- 20')" ;;
            llvm-config-20) echo "      Debian/Ubuntu: 'sudo apt-get install llvm-20-dev'" ;;
            make)       echo "      'sudo apt-get install build-essential'  /  'brew install make'" ;;
        esac
        return 1
    fi
}

missing=0
require_cmd make            || missing=1
require_cmd clang++-20      || missing=1
require_cmd llvm-config-20  || missing=1

if [ $missing -ne 0 ]; then
    die "one or more required tools are missing — install them and re-run."
fi

# ----------------------------------------------------------------------------
# Build
# ----------------------------------------------------------------------------
if [ $DO_BUILD -eq 1 ]; then
    if [ -x "$CRYO_BIN" ]; then
        log_ok "cryo binary already exists at ${CRYO_BIN}; will re-run 'make cryo' for a no-op confirmation."
    fi
    log_info "running 'make cryo' (this is the slow step — ~5 min on a first build)..."
    ( cd "$REPO_ROOT" && make cryo )
    [ -x "$CRYO_BIN" ] || die "build finished but ${CRYO_BIN} is missing — something is wrong."
    log_ok "build complete: ${CRYO_BIN}"
else
    [ -x "$CRYO_BIN" ] || die "--no-build was passed but ${CRYO_BIN} does not exist."
    log_ok "using existing binary: ${CRYO_BIN}"
fi

# ----------------------------------------------------------------------------
# PATH wiring
# ----------------------------------------------------------------------------
detect_shell_rc() {
    local target="$1"
    if [ -z "$target" ]; then
        case "$(basename "${SHELL:-}")" in
            zsh)  target="zsh" ;;
            fish) target="fish" ;;
            *)    target="bash" ;;
        esac
    fi
    case "$target" in
        bash) echo "$HOME/.bashrc" ;;
        zsh)  echo "$HOME/.zshrc" ;;
        fish) echo "$HOME/.config/fish/config.fish" ;;
        none) echo "" ;;
        *)    die "unknown --shell value: $target  (expected: bash|zsh|fish|none)" ;;
    esac
}

RC_FILE="$(detect_shell_rc "$SHELL_TARGET")"

if [ -z "$RC_FILE" ]; then
    log_info "skipping PATH wiring (--shell=none). Add this to your shell rc manually:"
    echo "    export PATH=\"${BIN_DIR}:\$PATH\""
else
    MARKER="# Added by Cryo installer"
    if [ -f "$RC_FILE" ] && grep -Fq "$BIN_DIR" "$RC_FILE"; then
        log_ok "PATH entry already present in ${RC_FILE}"
    else
        mkdir -p "$(dirname "$RC_FILE")"
        {
            echo
            echo "$MARKER"
            if [[ "$RC_FILE" == *fish* ]]; then
                echo "set -gx PATH ${BIN_DIR} \$PATH"
            else
                echo "export PATH=\"${BIN_DIR}:\$PATH\""
            fi
        } >> "$RC_FILE"
        log_ok "appended PATH entry to ${RC_FILE}"
        log_info "open a new shell or run: source \"${RC_FILE}\""
    fi
fi

# ----------------------------------------------------------------------------
# Verify
# ----------------------------------------------------------------------------
if "${CRYO_BIN}" --version >/dev/null 2>&1; then
    log_ok "cryo --version: $("${CRYO_BIN}" --version)"
else
    log_warn "cryo --version exited non-zero — binary built but version check failed."
fi

# ----------------------------------------------------------------------------
# Done
# ----------------------------------------------------------------------------
echo
echo "${GREEN}${BOLD}Done.${RESET}"
echo
echo "${BOLD}In-tree limitation (read this):${RESET}"
echo "  cryo currently locates the standard library via a relative path"
echo "  ('<project_root>/../stdlib'). That means the compiler binary expects"
echo "  the repo's stdlib/ tree to live next to your project. Until that"
echo "  resolution is fixed (see the 0.1.0 distribution plan), this install is"
echo "  effectively 'cryo lives in this repo, and your projects must be set up"
echo "  so that ../stdlib/ resolves to ${REPO_ROOT}/stdlib'."
echo
echo "  For now, the safest pattern is to put your projects inside this repo"
echo "  (e.g. ${REPO_ROOT}/sandbox/myapp) so ../stdlib resolves correctly."
echo
echo "Try it out:"
echo "  cryo --help"
echo
echo "${TEAL}${BOLD}https://github.com/jakelequire/cryo${RESET}"
echo
