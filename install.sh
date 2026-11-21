#!/usr/bin/env bash
#
# Cryo Programming Language - installer.
#
# TWO modes:
#
#   PRODUCTION (default) - the one-liner target:
#       curl -fsSL https://cryolang.org/install.sh | bash
#     Downloads the Linux x86_64 release tarball from GitHub Releases (or
#     takes a local one with --archive), verifies its sha256, installs it
#     into ~/.cryo, and adds ~/.cryo/bin to PATH via your shell rc.  An
#     existing install is replaced whole.  The shipped `cryo` and `cryolsp`
#     are static binaries.  No repo checkout required.
#
#     Compiling a program links it with a C compiler.  After installing, the
#     installer builds a hello-world with the installed compiler; when that
#     fails it offers to install gcc with your package manager
#     (install-dependencies.sh, shipped in the install).
#
#   DEV (--dev) - for working in a cloned repo:
#       ./install.sh --dev
#     Symlinks the committed <repo>/bin/cryo + <repo>/stdlib into a prefix
#     (default /usr/local).  This is what `make install` runs.  No download.
#
# The compiler finds its stdlib and runtime beside itself
# (<bindir>/../stdlib, <bindir>/../runtime); no environment variable is set.
# `cryo version --verbose` names the binary that runs and the stdlib it uses.
#
# Usage: install.sh [options]
#   PRODUCTION:
#       --version=X.Y.Z   Install a specific version (default: latest release)
#       --archive=FILE    Install this local release tarball (a sibling
#                         FILE.sha256, when present, is checked)
#       --prefix=DIR      Install root (default: ~/.cryo)
#       --uninstall       Remove the install, the PATH rc block, and the
#                         archives it built in the user's cache
#       --no-modify-path  Don't touch shell rc files (print the line instead)
#   DEV (--dev):
#       --prefix=DIR      Symlink prefix (default: /usr/local)
#       --no-lsp          Skip the cryolsp symlink
#       --uninstall       Remove the dev symlinks
#   COMMON:
#   -y, --yes             Don't ask: confirm the install, and install a
#                         missing C compiler
#   -h, --help            Show usage and exit
#

set -euo pipefail

REPO_SLUG="jakelequire/CryoLang"

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

usage() {
    sed -n '3,46p' "$0" | sed 's/^# \{0,1\}//'
    exit 0
}

# ----------------------------------------------------------------------------
# Args
# ----------------------------------------------------------------------------
ASSUME_YES=0
ACTION="install"
MODE="prod"
PREFIX=""               # mode-specific default resolved later
INSTALL_LSP=1
REQ_VERSION=""
ARCHIVE=""
MODIFY_PATH=1

for arg in "$@"; do
    case $arg in
        -y|--yes)         ASSUME_YES=1 ;;
        --dev|--from-source) MODE="dev" ;;
        --prefix=*)       PREFIX="${arg#--prefix=}" ;;
        --archive=*)      ARCHIVE="${arg#--archive=}" ;;
        --version=*)      REQ_VERSION="${arg#--version=}" ;;
        --no-lsp)         INSTALL_LSP=0 ;;
        --no-modify-path) MODIFY_PATH=0 ;;
        --uninstall)      ACTION="uninstall" ;;
        -h|--help)        usage ;;
        *)                die "unknown argument: $arg (try --help)" ;;
    esac
done

# ============================================================================
# PRODUCTION MODE - download a release tarball into ~/.cryo
# ============================================================================
prod_detect_target() {
    local os arch
    case "$(uname -s)" in
        Linux) os="linux" ;;
        Darwin) die "macOS is not supported by the v1 installer yet (no prebuilt artifact). Build from source: clone the repo and run './install.sh --dev'." ;;
        *) die "unsupported OS '$(uname -s)'. v1 ships Linux x86_64 and Windows x86_64 (use install.ps1 on Windows)." ;;
    esac
    case "$(uname -m)" in
        x86_64|amd64) arch="x86_64" ;;
        *) die "unsupported architecture '$(uname -m)'. v1 ships x86_64 only." ;;
    esac
    echo "${os}-${arch}"
}

prod_latest_version() {
    # Resolve the latest release tag from the GitHub API; strip a leading 'v'.
    command -v curl >/dev/null 2>&1 || die "curl is required to resolve the latest version (or pass --version=X.Y.Z)."
    local tag
    tag="$(curl -fsSL "https://api.github.com/repos/${REPO_SLUG}/releases/latest" 2>/dev/null \
            | grep -oE '"tag_name"[[:space:]]*:[[:space:]]*"[^"]+"' | head -1 \
            | sed -E 's/.*"tag_name"[[:space:]]*:[[:space:]]*"([^"]+)".*/\1/')"
    [ -n "$tag" ] || die "could not resolve the latest release tag from GitHub. Pass --version=X.Y.Z explicitly."
    echo "${tag#v}"
}

prod_shell_rc() {
    # Best-effort shell rc for the user's login shell.
    case "$(basename "${SHELL:-/bin/bash}")" in
        zsh)  echo "$HOME/.zshrc" ;;
        bash) [ -f "$HOME/.bashrc" ] && echo "$HOME/.bashrc" || echo "$HOME/.profile" ;;
        *)    echo "$HOME/.profile" ;;
    esac
}

prod_add_path() {
    local bindir="$1" rc line marker
    rc="$(prod_shell_rc)"
    marker="# >>> cryo install (managed) >>>"
    line="export PATH=\"${bindir}:\$PATH\""
    if [ $MODIFY_PATH -eq 0 ]; then
        log_info "PATH not modified (--no-modify-path). Add this to your shell rc:"
        echo "    ${line}"
        return
    fi
    if [ -f "$rc" ] && grep -qF "$marker" "$rc"; then
        log_info "PATH entry already present in ${rc/#$HOME/\~}"
        return
    fi
    {
        echo "$marker"
        echo "$line"
        echo "# <<< cryo install (managed) <<<"
    } >> "$rc"
    log_ok "added ${bindir/#$HOME/\~} to PATH in ${rc/#$HOME/\~}"
    log_info "open a new shell or run:  source ${rc/#$HOME/\~}"
}

prod_install() {
    command -v tar  >/dev/null 2>&1 || die "tar is required."
    command -v sha256sum >/dev/null 2>&1 || die "sha256sum is required."

    # `tmp` is global: the EXIT trap that removes it runs after this returns.
    local target ver tarball url source
    target="$(prod_detect_target)"
    if [ -n "$ARCHIVE" ]; then
        [ -f "$ARCHIVE" ] || die "no archive at ${ARCHIVE}"
        tarball="$(basename "$ARCHIVE")"
        source="${ARCHIVE}"
    else
        command -v curl >/dev/null 2>&1 || die "curl is required."
        if [ -n "$REQ_VERSION" ]; then ver="${REQ_VERSION#v}"; else
            log_info "resolving latest release..."
            ver="$(prod_latest_version)"
        fi
        tarball="cryo-${ver}-${target}.tar.gz"
        url="https://github.com/${REPO_SLUG}/releases/download/v${ver}/${tarball}"
        source="${url}"
    fi

    echo "This installer will:"
    echo "  • install ${source}"
    echo "  • verify its sha256 checksum"
    echo "  • replace anything at ${INSTALL_ROOT} with it"
    echo "  • add ${INSTALL_ROOT}/bin to PATH via your shell rc"
    echo "  • check that this machine links programs, and offer to install a C compiler if not"
    echo
    if [ $ASSUME_YES -ne 1 ] && [ -t 0 ]; then
        read -r -p "Continue? [Y/n] " reply
        case "$reply" in n|N|no|NO) die "cancelled" ;; *) : ;; esac
    fi

    tmp="$(mktemp -d)"
    trap 'rm -rf "$tmp"' EXIT

    if [ -n "$ARCHIVE" ]; then
        cp "$ARCHIVE" "${tmp}/${tarball}"
        if [ -f "${ARCHIVE}.sha256" ]; then
            cp "${ARCHIVE}.sha256" "${tmp}/${tarball}.sha256"
        else
            log_warn "no ${ARCHIVE}.sha256 beside the archive; installing it unchecked"
        fi
    else
        log_info "downloading ${url}"
        curl -fSL --progress-bar -o "${tmp}/${tarball}" "$url" \
            || die "download failed - is v${ver} a published release for ${target}?"
        log_info "downloading checksum"
        curl -fsSL -o "${tmp}/${tarball}.sha256" "${url}.sha256" \
            || die "checksum download failed (${url}.sha256)."
    fi

    if [ -f "${tmp}/${tarball}.sha256" ]; then
        log_info "verifying checksum"
        ( cd "$tmp" && sha256sum -c "${tarball}.sha256" >/dev/null 2>&1 ) \
            || die "checksum mismatch - refusing to install ${tarball}."
        log_ok "checksum verified"
    fi

    # Extract into a staging dir (tarball wraps everything in one top dir),
    # then atomically swap it into place so an upgrade never leaves a
    # half-written install.
    log_info "extracting"
    mkdir -p "${tmp}/stage"
    tar -xzf "${tmp}/${tarball}" -C "${tmp}/stage" --strip-components=1

    # Refuse to install a tarball that doesn't contain what we expect.
    local required
    for required in bin/cryo bin/cryolsp stdlib/lib.cryo stdlib/.bin/shipped runtime/lib.cryo \
                    runtime/.bin/shipped install-dependencies.sh VERSION; do
        [ -e "${tmp}/stage/${required}" ] \
            || die "archive is missing ${required} - corrupted or unexpected layout, refusing to install."
    done
    ver="$(cat "${tmp}/stage/VERSION")"

    mkdir -p "$(dirname "$INSTALL_ROOT")"
    rm -rf "${INSTALL_ROOT}.old"
    [ -e "$INSTALL_ROOT" ] && mv "$INSTALL_ROOT" "${INSTALL_ROOT}.old"
    mv "${tmp}/stage" "$INSTALL_ROOT"
    rm -rf "${INSTALL_ROOT}.old"
    chmod +x "${INSTALL_ROOT}/bin/cryo" 2>/dev/null || true
    log_ok "installed to ${INSTALL_ROOT}"

    # Post-install verification: the installed binary must actually run.
    local verstr
    verstr="$("${INSTALL_ROOT}/bin/cryo" --version 2>/dev/null)" \
        || die "post-install verification failed: '${INSTALL_ROOT}/bin/cryo --version' did not run. The download may be corrupted - re-run the installer."

    prod_add_path "${INSTALL_ROOT}/bin"

    local linked=1 dependency_arguments=()
    [ $ASSUME_YES -eq 1 ] && dependency_arguments=(--yes)
    "${INSTALL_ROOT}/install-dependencies.sh" "${dependency_arguments[@]}" || linked=0

    echo
    if [ $linked -eq 1 ]; then
        echo "${GREEN}${BOLD}Done.${RESET}  cryo ${ver} is installed and links programs."
    else
        log_warn "cryo ${ver} is installed, but cannot link programs until this succeeds:"
        log_warn "  ${INSTALL_ROOT}/install-dependencies.sh"
    fi
    echo
    echo "  ${verstr}"
    echo "  open a new shell (or source your rc) to put cryo on PATH"
    echo "  which install runs:  cryo version --verbose"
    echo "  uninstall:           curl -fsSL https://cryolang.org/install.sh | bash -s -- --uninstall"
    echo
    echo "${TEAL}${BOLD}https://github.com/${REPO_SLUG}${RESET}"
    [ $linked -eq 1 ]
}

# The archives an install builds on demand live in the user's cache, below
# the directory the compiler's cache root names.
prod_cache_archives() {
    if [ -n "${CRYO_HOME:-}" ]; then echo "${CRYO_HOME}/cache/archives"
    elif [ -n "${XDG_CACHE_HOME:-}" ]; then echo "${XDG_CACHE_HOME}/cryo/archives"
    else echo "${HOME}/.cache/cryo/archives"
    fi
}

prod_uninstall() {
    if [ -e "$INSTALL_ROOT" ]; then
        rm -rf "$INSTALL_ROOT"
        log_ok "removed ${INSTALL_ROOT}"
    else
        log_info "nothing to remove at ${INSTALL_ROOT}"
    fi
    local archives
    archives="$(prod_cache_archives)"
    if [ -e "$archives" ]; then
        rm -rf "$archives"
        log_ok "removed ${archives}"
    fi
    [ $MODIFY_PATH -eq 1 ] || return 0
    # Strip the managed PATH block from common rc files.
    local rc
    for rc in "$HOME/.bashrc" "$HOME/.zshrc" "$HOME/.profile"; do
        [ -f "$rc" ] || continue
        if grep -qF "# >>> cryo install (managed) >>>" "$rc"; then
            sed -i '/# >>> cryo install (managed) >>>/,/# <<< cryo install (managed) <<</d' "$rc"
            log_ok "removed PATH block from ${rc/#$HOME/\~}"
        fi
    done
}

# ============================================================================
# DEV MODE - symlink the repo's pinned binary + stdlib (the old behavior)
# ============================================================================
dev_run() {
    [ -z "$PREFIX" ] && PREFIX="/usr/local"

    local SCRIPT_PATH REPO_ROOT SRC_BIN SRC_STDLIB SRC_LSP
    SCRIPT_PATH="$(readlink -f "$0")"
    REPO_ROOT="$(dirname "$SCRIPT_PATH")"
    SRC_BIN="${REPO_ROOT}/bin/cryo"
    SRC_STDLIB="${REPO_ROOT}/stdlib"
    SRC_LSP=""
    if [ -x "${REPO_ROOT}/bin/cryolsp" ]; then
        SRC_LSP="${REPO_ROOT}/bin/cryolsp"
    elif [ -x "${REPO_ROOT}/tools/CryoLSP/build/cryolsp" ]; then
        SRC_LSP="${REPO_ROOT}/tools/CryoLSP/build/cryolsp"
    fi
    [ -f "${REPO_ROOT}/Makefile" ] || die "could not find Makefile at ${REPO_ROOT} - run --dev from a cloned repo (install.sh next to the Makefile)."

    local DEST_BIN DEST_LSP DEST_SHARE DEST_STDLIB
    DEST_BIN="${PREFIX}/bin/cryo"
    DEST_LSP="${PREFIX}/bin/cryolsp"
    DEST_SHARE="${PREFIX}/share/cryo"
    DEST_STDLIB="${DEST_SHARE}/stdlib"

    need_sudo_for() {
        local target_dir; target_dir="$(dirname "$1")"
        if [ -w "$target_dir" ] || [ -w "$1" ]; then return 1; fi
        if [ -e "$target_dir" ]; then return 0; fi
        local cur="$target_dir"
        while [ ! -e "$cur" ]; do cur="$(dirname "$cur")"; done
        if [ -w "$cur" ]; then return 1; fi
        return 0
    }
    local USE_SUDO=0
    run() { if [ $USE_SUDO -eq 1 ]; then sudo "$@"; else "$@"; fi; }

    if [ "$ACTION" = "install" ]; then
        [ -x "$SRC_BIN" ] || die "$SRC_BIN is missing or not executable. Run 'make pin' first."
        [ -f "$SRC_STDLIB/lib.cryo" ] || die "$SRC_STDLIB/lib.cryo not found - is the stdlib in place?"
        echo "Dev install (symlinks):"
        echo "  ${DEST_BIN} → ${SRC_BIN}"
        [ $INSTALL_LSP -eq 1 ] && [ -n "$SRC_LSP" ] && echo "  ${DEST_LSP} → ${SRC_LSP}"
        echo "  ${DEST_STDLIB} → ${SRC_STDLIB}"
        echo "  Prefix: ${PREFIX}"
        echo
    else
        echo "Dev uninstall: remove symlinks under ${PREFIX}"
        echo
    fi

    if [ $ASSUME_YES -ne 1 ] && [ -t 0 ]; then
        read -r -p "Continue? [y/N] " reply
        case "$reply" in y|Y|yes|YES) ;; *) die "cancelled" ;; esac
    fi

    if need_sudo_for "$DEST_BIN" || need_sudo_for "$DEST_STDLIB"; then
        USE_SUDO=1
        log_info "prefix '${PREFIX}' is not writable; using sudo."
        command -v sudo >/dev/null 2>&1 || die "sudo not found and prefix not writable - try --prefix=\$HOME/.local."
    fi

    if [ "$ACTION" = "install" ]; then
        run mkdir -p "$(dirname "$DEST_BIN")" "$DEST_SHARE"
        run ln -sfn "$SRC_BIN" "$DEST_BIN"
        log_ok "linked: ${DEST_BIN}"
        if [ $INSTALL_LSP -eq 1 ] && [ -n "$SRC_LSP" ]; then
            run ln -sfn "$SRC_LSP" "$DEST_LSP"; log_ok "linked: ${DEST_LSP}"
        fi
        run ln -sfn "$SRC_STDLIB" "$DEST_STDLIB"
        log_ok "linked: ${DEST_STDLIB}"
        "$DEST_BIN" --version >/dev/null 2>&1 && log_ok "cryo --version: $("$DEST_BIN" --version)" || log_warn "binary in place but --version failed."
        echo
        echo "${GREEN}${BOLD}Done.${RESET}  (dev symlink install)"
        command -v cryo >/dev/null 2>&1 || log_warn "add to PATH: export PATH=\"$(dirname "$DEST_BIN"):\$PATH\""
    else
        for p in "$DEST_BIN" "$DEST_LSP" "$DEST_STDLIB"; do
            { [ -L "$p" ] || [ -e "$p" ]; } && run rm -f "$p" && log_ok "removed: ${p}"
        done
        [ -d "$DEST_SHARE" ] && [ -z "$(ls -A "$DEST_SHARE" 2>/dev/null || true)" ] && run rmdir "$DEST_SHARE"
        echo; echo "${GREEN}${BOLD}Done.${RESET}  (dev uninstall)"
    fi
}

# ============================================================================
# Dispatch
# ============================================================================
print_banner

if [ "$MODE" = "dev" ]; then
    dev_run
else
    INSTALL_ROOT="${PREFIX:-$HOME/.cryo}"
    case "$ACTION" in
        install)   prod_install ;;
        uninstall) prod_uninstall ;;
    esac
fi
