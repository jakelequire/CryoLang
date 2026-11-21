#!/usr/bin/env bash
#
# build-msi - package the Windows release zip as a per-user MSI, with wixl.
#
#   scripts/build-msi.sh dist/cryo-<ver>-windows-x86_64.zip
#       -> dist/cryo-<ver>-windows-x86_64.msi (and its .sha256)
#
# The MSI holds the zip's tree, unchanged, and installs it for the current
# user into %USERPROFILE%\.cryo (scripts/packaging/cryo.wxs says what it does
# beyond its files, through the tree's own user-setup.ps1).  wixl-heat harvests the
# tree into components; wixl compiles both.  An MSI version is numeric, so a
# pre-release's suffix is dropped from it (1.0.0-alpha -> 1.0.0).
#
# Needs wixl, wixl-heat and unzip (Debian/Ubuntu: wixl, unzip); run on Linux.

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DIST="${ROOT}/dist"
ZIP="${1:-}"
[ -n "$ZIP" ] && [ -f "$ZIP" ] || { echo "usage: build-msi.sh <windows release zip>" >&2; exit 2; }
for tool in wixl wixl-heat unzip; do
    command -v "$tool" >/dev/null 2>&1 || { echo "error: ${tool} is required" >&2; exit 1; }
done

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
unzip -q "$ZIP" -d "${work}/zip"
tree="$(find "${work}/zip" -mindepth 1 -maxdepth 1 -type d)"
[ "$(printf '%s\n' "$tree" | wc -l)" -eq 1 ] || { echo "error: the zip does not hold exactly one top directory" >&2; exit 1; }
for required in bin/cryo.exe bin/cryolsp.exe bin/LLVM-C.dll bin/libclang.dll stdlib/.bin/shipped runtime/.bin/shipped \
                install-dependencies.ps1 user-setup.ps1 VERSION; do
    [ -e "${tree}/${required}" ] || { echo "error: the zip holds no ${required}" >&2; exit 1; }
done

release="$(cat "${tree}/VERSION")"
find "$tree" -type f | sort \
    | wixl-heat -p "${tree}/" --directory-ref INSTALLDIR --component-group CryoFiles --var var.SourceDir --win64 \
    > "${work}/files.wxs"

mkdir -p "$DIST"
msi="${DIST}/$(basename "$tree").msi"
wixl -a x64 -D Win64=yes -D SourceDir="$tree" -D Version="${release%%-*}" -o "$msi" \
    "${ROOT}/scripts/packaging/cryo.wxs" "${work}/files.wxs"
( cd "$DIST" && sha256sum "$(basename "$msi")" > "$(basename "$msi").sha256" )
echo "[build-msi] wrote ${msi#$ROOT/}"
