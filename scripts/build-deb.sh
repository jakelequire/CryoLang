#!/usr/bin/env bash
#
# build-deb - package a Linux release tarball as a Debian package.
#
#   scripts/build-deb.sh dist/cryo-<ver>-linux-x86_64.tar.gz [--revision N]
#       -> dist/cryo_<debian-version>_amd64.deb
#
# The package installs the release tree as /usr/lib/cryo, its licenses moved
# to /usr/share/doc/cryo where Debian keeps them (the copyright file names
# Cryo's and the linked LLVM's), the manual pages scripts/packaging/*.1, with
# /usr/bin/cryo and /usr/bin/cryolsp linking into it: the compiler finds its
# stdlib and runtime beside the binary the link names, and the tree is
# marked shipped, so a build only reads it and builds any archive it lacks
# in the user's cache.  The C toolchain every link needs is a dependency, so
# apt installs it with the package.
#
# The Debian version is the release's with `-` before a pre-release written
# `~` (1.0.0-alpha -> 1.0.0~alpha, which sorts before 1.0.0), and the
# revision (default 1) after a `-`.
#
# Needs dpkg-deb, readelf and gzip; run on Linux.

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DIST="${ROOT}/dist"
TARBALL=""
REVISION=1
MAINTAINER="${DEB_MAINTAINER:-CryoLang <packages@cryolang.org>}"

while [ $# -gt 0 ]; do
    case "$1" in
        --revision) REVISION="$2"; shift 2 ;;
        --revision=*) REVISION="${1#--revision=}"; shift ;;
        -h|--help) sed -n '3,19p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        -*) echo "build-deb.sh: unknown argument $1" >&2; exit 2 ;;
        *) TARBALL="$1"; shift ;;
    esac
done
[ -n "$TARBALL" ] && [ -f "$TARBALL" ] || { echo "usage: build-deb.sh <linux release tarball> [--revision N]" >&2; exit 2; }
command -v dpkg-deb >/dev/null 2>&1 || { echo "error: dpkg-deb is required" >&2; exit 1; }

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
mkdir -p "${work}/tree"
tar -xzf "$TARBALL" -C "${work}/tree" --strip-components=1
for required in bin/cryo bin/cryolsp stdlib/.bin/shipped runtime/.bin/shipped VERSION LICENSE \
                THIRD_PARTY_LICENSES/LLVM-LICENSE.txt; do
    [ -e "${work}/tree/${required}" ] || { echo "error: the tarball holds no ${required}" >&2; exit 1; }
done

RELEASE="$(cat "${work}/tree/VERSION")"
VERSION="$(printf '%s' "$RELEASE" | sed 's/-/~/')-${REVISION}"
# A tarball from `build-release.sh linux --dynamic` loads LLVM's shared
# libraries, which the package then depends on; it is named `-dynamic` so
# no release ships it.
DEPENDS="gcc, libc6-dev"
SUFFIX=""
needed="$(readelf -d "${work}/tree/bin/cryo" | grep NEEDED || true)"
case "$needed" in *libLLVM*) DEPENDS="${DEPENDS}, libllvm20"; SUFFIX="-dynamic" ;; esac
case "$needed" in *libclang*) DEPENDS="${DEPENDS}, libclang1-20"; SUFFIX="-dynamic" ;; esac
package="${work}/package"
mkdir -p "${package}/DEBIAN" "${package}/usr/lib" "${package}/usr/bin" "${package}/usr/share/doc/cryo"
mv "${work}/tree" "${package}/usr/lib/cryo"
# The licenses live in the package's documentation, where Debian looks.
mv "${package}/usr/lib/cryo/LICENSE" "${work}/LICENSE"
third_party="${work}/THIRD_PARTY_LICENSES"
mv "${package}/usr/lib/cryo/THIRD_PARTY_LICENSES" "$third_party"
ln -s ../lib/cryo/bin/cryo "${package}/usr/bin/cryo"
ln -s ../lib/cryo/bin/cryolsp "${package}/usr/bin/cryolsp"

llvm_license="${third_party}/LLVM-LICENSE.txt"
{
    echo "Format: https://www.debian.org/doc/packaging-manuals/copyright-format/1.0/"
    echo "Upstream-Name: cryo"
    echo "Upstream-Contact: ${MAINTAINER}"
    echo "Source: https://github.com/jakelequire/CryoLang"
    echo
    echo "Files: *"
    echo "Copyright: 2025-2026 Jacob LeQuire"
    echo "License: Apache-2.0"
    echo
    echo "Files: usr/lib/cryo/bin/cryo usr/lib/cryo/bin/cryolsp"
    echo "Copyright: 2003-2025 LLVM Project contributors"
    echo "License: Apache-2.0 with LLVM exception"
    echo "Comment: cryo and cryolsp link LLVM and libclang.  The LLVM Project's"
    echo " whole license, with its legacy terms and the notices of the third-party"
    echo " code it holds, is /usr/share/doc/cryo/THIRD_PARTY_LICENSES/LLVM-LICENSE.txt."
    echo
    echo "License: Apache-2.0"
    echo " On Debian systems the full text of the Apache License, Version 2.0 is"
    echo " in /usr/share/common-licenses/Apache-2.0."
    echo
    echo "License: Apache-2.0 with LLVM exception"
    echo " On Debian systems the full text of the Apache License, Version 2.0 is"
    echo " in /usr/share/common-licenses/Apache-2.0.  LLVM's exceptions to it:"
    # The exceptions as LLVM's license states them, from their heading to the
    # next ==== rule: each line indented one space, a blank line written ` .`.
    awk '/^---- LLVM Exceptions/ {on=1}
         on && /^====/ {exit}
         on && /^[[:space:]]*$/ {blank=1; next}
         on {print (blank ? " .\n " : " ") $0; blank=0}' "$llvm_license"
} > "${package}/usr/share/doc/cryo/copyright"
cp -r "$third_party" "${package}/usr/share/doc/cryo/THIRD_PARTY_LICENSES"
mkdir -p "${package}/usr/share/man/man1"
for page in cryo cryolsp; do
    gzip -9n < "${ROOT}/scripts/packaging/${page}.1" > "${package}/usr/share/man/man1/${page}.1.gz"
done
# A release's binaries are linked statically on purpose, LLVM's zlib with
# them, so that one file runs on any glibc Linux; lintian is told so.
if [ -z "$SUFFIX" ]; then
    mkdir -p "${package}/usr/share/lintian/overrides"
    {
        echo "# cryo and cryolsp are self-contained by design: LLVM, libclang and"
        echo "# the libraries they use (zlib among them) are linked in."
        for binary in cryo cryolsp; do
            echo "cryo: statically-linked-binary [usr/lib/cryo/bin/${binary}]"
            echo "cryo: embedded-library zlib [usr/lib/cryo/bin/${binary}]"
        done
    } > "${package}/usr/share/lintian/overrides/cryo"
fi
{
    echo "cryo (${VERSION}) unstable; urgency=medium"
    echo
    echo "  * Release ${RELEASE}."
    echo
    echo " -- ${MAINTAINER}  $(date -R)"
} | gzip -9n > "${package}/usr/share/doc/cryo/changelog.Debian.gz"

installed_kb="$(du -sk --exclude=DEBIAN "$package" | cut -f1)"
cat > "${package}/DEBIAN/control" <<EOF
Package: cryo
Version: ${VERSION}
Architecture: amd64
Maintainer: ${MAINTAINER}
Installed-Size: ${installed_kb}
Depends: ${DEPENDS}
Section: devel
Priority: optional
Homepage: https://cryolang.org
Description: compiler for the Cryo programming language
 The Cryo compiler, its standard library and runtime, and the cryolsp
 language server.  Programs are linked with the system's C compiler.
EOF

chmod -R u=rwX,go=rX "$package"
chmod 0755 "${package}/usr/lib/cryo/bin/cryo" "${package}/usr/lib/cryo/bin/cryolsp" "${package}/usr/lib/cryo/install-dependencies.sh"
mkdir -p "$DIST"
deb="${DIST}/cryo_${VERSION}_amd64${SUFFIX}.deb"
dpkg-deb --root-owner-group -Zxz --build "$package" "$deb"
( cd "$DIST" && sha256sum "$(basename "$deb")" > "$(basename "$deb").sha256" )
echo "[build-deb] wrote ${deb#$ROOT/}"
