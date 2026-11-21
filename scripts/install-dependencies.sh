#!/usr/bin/env bash
#
# Cryo - make sure this machine can link the programs Cryo compiles.
#
# Shipped at the root of every Linux install, and run by install.sh after
# it puts the files in place.  The check is the real thing: the installed
# compiler builds and runs a hello-world.  When that fails for want of a C
# compiler or the C library's development files, this offers to install
# them with the system's package manager, and checks again.
#
# Usage: install-dependencies.sh [--yes]
#   -y, --yes   install what is missing without asking
#
# Exit status: 0 when the installed compiler builds and runs a program,
# 1 when it still cannot.

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ASSUME_YES=0
for arg in "$@"; do
    case "$arg" in
        -y|--yes) ASSUME_YES=1 ;;
        -h|--help) sed -n '3,17p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "install-dependencies.sh: unknown argument $arg (try --help)" >&2; exit 2 ;;
    esac
done

# Build and run a hello-world with the installed compiler, outside any
# project of the user's.  The compiler's own report is kept for the message.
REPORT=""
builds_a_program() {
    local work
    work="$(mktemp -d)"
    mkdir -p "${work}/src"
    printf '[project]\nproject_name = "linkcheck"\noutput_dir = "build"\nsource_dir = "src"\nentry_point = "src/main.cryo"\n' \
        > "${work}/cryoconfig"
    printf 'namespace Main;\n\nimport std::fmt;\n\nfunction main() -> int {\n    fmt::printf("cryo links\\n");\n    return 0;\n}\n' \
        > "${work}/src/main.cryo"
    if REPORT="$(cd "$work" && "${ROOT}/bin/cryo" build 2>&1)" \
            && [ "$("${work}/build/linkcheck" 2>&1)" = "cryo links" ]; then
        rm -rf "$work"
        return 0
    fi
    rm -rf "$work"
    return 1
}

# The package manager, and the packages that give it a C compiler and the C
# library's development files.
package_command() {
    if command -v apt-get >/dev/null 2>&1; then echo "apt-get install -y gcc libc6-dev"
    elif command -v dnf >/dev/null 2>&1;   then echo "dnf install -y gcc glibc-devel"
    elif command -v zypper >/dev/null 2>&1; then echo "zypper --non-interactive install gcc glibc-devel"
    elif command -v pacman >/dev/null 2>&1; then echo "pacman -S --needed --noconfirm gcc"
    elif command -v apk >/dev/null 2>&1;    then echo "apk add gcc musl-dev"
    fi
}

if builds_a_program; then
    echo "[ok]      this machine links Cryo programs"
    exit 0
fi

if ! printf '%s\n' "$REPORT" | grep -qi 'link'; then
    echo "[error]   the installed compiler could not build a test program, and not for want of a C compiler:" >&2
    printf '%s\n' "$REPORT" | tail -n 12 | sed 's/^/            /' >&2
    echo "          the install is damaged; install it again." >&2
    exit 1
fi
echo "[warn]    the installed compiler cannot link a program yet:"
printf '%s\n' "$REPORT" | tail -n 8 | sed 's/^/            /'

INSTALL="$(package_command)"
if [ -z "$INSTALL" ]; then
    echo "[error]   no package manager this script knows (apt, dnf, zypper, pacman, apk) is installed;" >&2
    echo "          install a C compiler (gcc or clang) and the C library's development files, then run" >&2
    echo "          ${ROOT}/install-dependencies.sh again" >&2
    exit 1
fi
# apt installs only what its package lists, refreshed, still name.
UPDATE=""
case "$INSTALL" in apt-get*) UPDATE="apt-get update" ;; esac
if [ "$(id -u)" -ne 0 ]; then
    command -v sudo >/dev/null 2>&1 \
        || { echo "[error]   installing the C compiler needs root, and sudo is not installed: run as root: ${INSTALL}" >&2; exit 1; }
    INSTALL="sudo ${INSTALL}"
    [ -z "$UPDATE" ] || UPDATE="sudo ${UPDATE}"
fi

if [ $ASSUME_YES -ne 1 ]; then
    # Asked on the terminal itself, so it works under `curl ... | bash` too.
    if ! { exec 3<>/dev/tty; } 2>/dev/null; then
        echo "[error]   Cryo needs a C compiler to link programs, and there is no terminal to ask on." >&2
        echo "          Run again with --yes to let it run: ${INSTALL}" >&2
        exit 1
    fi
    printf 'Cryo needs a C compiler to link programs.  Run "%s" now? [Y/n] ' "$INSTALL" >&3
    read -r reply <&3 || reply="n"
    exec 3>&-
    case "$reply" in
        ""|y|Y|yes|YES) ;;
        *) echo "[error]   not installed; Cryo cannot link programs until a C compiler is installed" >&2; exit 1 ;;
    esac
fi

if [ -n "$UPDATE" ]; then
    echo "[info]    ${UPDATE}"
    $UPDATE
fi
echo "[info]    ${INSTALL}"
$INSTALL

if builds_a_program; then
    echo "[ok]      this machine links Cryo programs"
    exit 0
fi
echo "[error]   the C compiler is installed, and the installed compiler still cannot link a program:" >&2
printf '%s\n' "$REPORT" | tail -n 12 | sed 's/^/            /' >&2
exit 1
