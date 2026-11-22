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
# tree into components; wixl compiles both.
#
# An MSI's version is three numbers (major and minor up to 255, build up to
# 65535) compared in order, and an upgrade must find the newer one larger.
# A release X.Y.Z is itself.  A pre-release of X.Y.0, `X.Y.0-<label>.N`,
# must sort below X.Y.0 and above the pre-releases numbered below N, so it
# takes the place just below X.Y.0: X.(Y-1).N, or (X-1).255.N when Y is 0
# (1.0.0-alpha.4081 -> 0.255.4081).  A pre-release of a patch release has
# no such place and is refused.
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
                install-dependencies.ps1 user-setup.ps1 VERSION LICENSE; do
    [ -e "${tree}/${required}" ] || { echo "error: the zip holds no ${required}" >&2; exit 1; }
done

release="$(cat "${tree}/VERSION")"
core="${release%%-*}"
IFS=. read -r major minor patch <<< "$core"
if [ "$core" = "$release" ]; then
    product_version="$core"
else
    number="${release##*.}"
    case "$number" in ''|*[!0-9]*) echo "error: the pre-release ${release} is not numbered (<label>.<N>)" >&2; exit 1 ;; esac
    [ "$patch" -eq 0 ] || { echo "error: ${release} is a pre-release of a patch release, which has no MSI version below ${core}" >&2; exit 1; }
    [ "$number" -le 65535 ] || { echo "error: ${release} is numbered past an MSI version's 65535" >&2; exit 1; }
    if [ "$minor" -gt 0 ]; then
        product_version="${major}.$((minor - 1)).${number}"
    else
        [ "$major" -gt 0 ] || { echo "error: ${release} is a pre-release of 0.0.0" >&2; exit 1; }
        product_version="$((major - 1)).255.${number}"
    fi
fi
find "$tree" -type f | sort \
    | wixl-heat -p "${tree}/" --directory-ref INSTALLDIR --component-group CryoFiles --var var.SourceDir --win64 \
    > "${work}/files.wxs"

# The wizard's license page shows the tree's LICENSE as rich text: each
# line a paragraph, with the characters rich text reserves escaped.
{
    printf '{\\rtf1\\ansi\\deff0{\\fonttbl{\\f0\\fmodern Consolas;}}\\f0\\fs16\n'
    sed -e 's/\\/\\\\/g' -e 's/{/\\{/g' -e 's/}/\\}/g' -e 's/$/\\par/' "${tree}/LICENSE"
    printf '}\n'
} > "${work}/License.rtf"

mkdir -p "$DIST"
msi="${DIST}/$(basename "$tree").msi"
wixl -a x64 --ext ui -D Win64=yes -D SourceDir="$tree" -D Version="${product_version}" \
    -D LicenseRtf="${work}/License.rtf" -D DependencyStepScript="${ROOT}/scripts/packaging/dependency-step.js" \
    -o "$msi" "${ROOT}/scripts/packaging/cryo.wxs" "${ROOT}/scripts/packaging/cryo-ui.wxs" "${work}/files.wxs"
( cd "$DIST" && sha256sum "$(basename "$msi")" > "$(basename "$msi").sha256" )
echo "[build-msi] wrote ${msi#$ROOT/}"
