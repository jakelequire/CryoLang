#!/usr/bin/env bash
#
# build-deb - package a Linux release tarball as a Debian package.
#
#   scripts/build-deb.sh dist/cryo-<ver>-linux-x86_64.tar.gz [--revision N]
#       -> dist/cryo_<debian-version>_amd64.deb
#
# The package installs the release tree as /usr/lib/cryo, its licenses moved
# to /usr/share/doc/cryo/copyright where Debian keeps them, with
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
for required in bin/cryo bin/cryolsp stdlib/.bin/shipped runtime/.bin/shipped VERSION LICENSE; do
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
third_party=""
if [ -f "${package}/usr/lib/cryo/THIRD_PARTY_LICENSES" ]; then
    mv "${package}/usr/lib/cryo/THIRD_PARTY_LICENSES" "${work}/THIRD_PARTY_LICENSES"
    third_party="${work}/THIRD_PARTY_LICENSES"
fi
ln -s ../lib/cryo/bin/cryo "${package}/usr/bin/cryo"
ln -s ../lib/cryo/bin/cryolsp "${package}/usr/bin/cryolsp"

{
    echo "Format: https://www.debian.org/doc/packaging-manuals/copyright-format/1.0/"
    echo "Upstream-Name: cryo"
    echo "Source: https://github.com/jakelequire/CryoLang"
    echo
    echo "Files: *"
    echo "Copyright: The Cryo authors"
    echo "License: Apache-2.0"
    echo
    echo "License: Apache-2.0"
    echo " On Debian systems the full text of the Apache License, Version 2.0 is"
    echo " in /usr/share/common-licenses/Apache-2.0."
    if [ -n "$third_party" ]; then
        echo
        echo "Comment: the licenses of the third-party code the binaries link,"
        echo " LLVM's among them, are in /usr/share/doc/cryo/THIRD_PARTY_LICENSES."
    fi
} > "${package}/usr/share/doc/cryo/copyright"
[ -z "$third_party" ] || cp "$third_party" "${package}/usr/share/doc/cryo/THIRD_PARTY_LICENSES"
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
