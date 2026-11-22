#!/usr/bin/env bash
#
# build-winget - the winget manifests of one release, for its MSI.
#
#   scripts/build-winget.sh dist/cryo-<ver>-windows-x86_64.msi
#       -> dist/winget/manifests/c/CryoLang/Cryo/<ver>/CryoLang.Cryo{,.installer,.locale.en-US}.yaml
#
# The layout is the one a pull request to the winget package repository
# takes.  The installer entry points at the MSI on the release's GitHub page,
# with the MSI's sha256 and product code, installs for the current user, and
# names llvm-mingw as a dependency, so winget installs the C toolchain every
# link needs along with Cryo.  `winget validate --manifest <dir>` checks
# them (Windows).
#
# Needs msiinfo (Debian/Ubuntu: msitools) and sha256sum; run on Linux.

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
REPO_SLUG="jakelequire/CryoLang"
MSI="${1:-}"
[ -n "$MSI" ] && [ -f "$MSI" ] || { echo "usage: build-winget.sh <release msi>" >&2; exit 2; }
command -v msiinfo >/dev/null 2>&1 || { echo "error: msiinfo is required" >&2; exit 1; }

name="$(basename "$MSI")"
version="${name#cryo-}"; version="${version%-windows-x86_64.msi}"
product_code="$(msiinfo export "$MSI" Property | awk -F'\t' '$1 == "ProductCode" { print $2 }' | tr -d '\r')"
[ -n "$product_code" ] || { echo "error: ${MSI} names no ProductCode" >&2; exit 1; }
sha="$(sha256sum "$MSI" | cut -d' ' -f1 | tr 'a-f' 'A-F')"
out="${ROOT}/dist/winget/manifests/c/CryoLang/Cryo/${version}"
mkdir -p "$out"

cat > "${out}/CryoLang.Cryo.yaml" <<EOF
PackageIdentifier: CryoLang.Cryo
PackageVersion: ${version}
DefaultLocale: en-US
ManifestType: version
ManifestVersion: 1.6.0
EOF

cat > "${out}/CryoLang.Cryo.installer.yaml" <<EOF
PackageIdentifier: CryoLang.Cryo
PackageVersion: ${version}
InstallerType: wix
Scope: user
InstallModes:
- interactive
- silent
UpgradeBehavior: install
Dependencies:
  PackageDependencies:
  - PackageIdentifier: MartinStorsjo.LLVM-MinGW.UCRT
ProductCode: '${product_code}'
Installers:
- Architecture: x64
  InstallerUrl: https://github.com/${REPO_SLUG}/releases/download/v${version}/${name}
  InstallerSha256: ${sha}
ManifestType: installer
ManifestVersion: 1.6.0
EOF

cat > "${out}/CryoLang.Cryo.locale.en-US.yaml" <<EOF
PackageIdentifier: CryoLang.Cryo
PackageVersion: ${version}
PackageLocale: en-US
Publisher: CryoLang
PublisherUrl: https://cryolang.org
PackageName: Cryo
PackageUrl: https://cryolang.org
License: Apache-2.0
LicenseUrl: https://github.com/${REPO_SLUG}/blob/main/LICENSE
ShortDescription: The Cryo programming language - compiler, standard library and language server.
Moniker: cryo
Tags:
- compiler
- programming-language
ManifestType: defaultLocale
ManifestVersion: 1.6.0
EOF
echo "[build-winget] wrote ${out#$ROOT/}"
