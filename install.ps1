<#
.SYNOPSIS
    Cryo Programming Language - Windows installer.

.DESCRIPTION
    The one-liner target for Windows:

        irm https://cryo-lang.org/install.ps1 | iex

    Detects the host (Windows x86_64 for v1), downloads the matching release
    zip from GitHub Releases, verifies its sha256, extracts a toolchain into
    %USERPROFILE%\.cryo, and adds %USERPROFILE%\.cryo\bin to the User PATH.

    The Windows build ships cryo.exe alongside LLVM-C.dll (its LLVM runtime
    dependency) - both live in .cryo\bin so the loader finds the DLL.

    NOTE: compiling YOUR programs shells out to a C compiler/linker for the
    final link. Install a mingw-w64 gcc (e.g. via MSYS2 or scoop) and put it
    on PATH; the cryo.exe itself needs nothing beyond the bundled DLL.

.PARAMETER Version
    Install a specific version (e.g. 1.0.0). Default: latest GitHub release.

.PARAMETER Prefix
    Install root. Default: $env:USERPROFILE\.cryo

.PARAMETER Uninstall
    Remove the install and the PATH entry.

.PARAMETER NoModifyPath
    Do not modify the User PATH; print the directory to add instead.
#>

[CmdletBinding()]
param(
    [string]$Version = "",
    [string]$Prefix  = "",
    [switch]$Uninstall,
    [switch]$NoModifyPath
)

$ErrorActionPreference = "Stop"
$RepoSlug = "jakelequire/CryoLang"

function Info($m) { Write-Host "[info]  $m" -ForegroundColor Cyan }
function Ok($m)   { Write-Host "[ok]    $m" -ForegroundColor Green }
function Warn($m) { Write-Host "[warn]  $m" -ForegroundColor Yellow }
function Die($m)  { Write-Host "[error] $m" -ForegroundColor Red; exit 1 }

# --- target / arch -----------------------------------------------------------
$arch = $env:PROCESSOR_ARCHITECTURE
if ($arch -ne "AMD64") {
    Die "unsupported architecture '$arch'. v1 ships Windows x86_64 only."
}
$target = "windows-x86_64"

if (-not $Prefix) {
    if ($env:CRYO_HOME) { $Prefix = $env:CRYO_HOME } else { $Prefix = Join-Path $env:USERPROFILE ".cryo" }
}

# --- uninstall ---------------------------------------------------------------
function Remove-FromUserPath($dir) {
    $cur = [Environment]::GetEnvironmentVariable("Path", "User")
    if (-not $cur) { return }
    $parts = $cur.Split(';') | Where-Object { $_ -and ($_ -ne $dir) }
    [Environment]::SetEnvironmentVariable("Path", ($parts -join ';'), "User")
}

if ($Uninstall) {
    if (Test-Path $Prefix) { Remove-Item -Recurse -Force $Prefix; Ok "removed $Prefix" }
    else { Info "nothing to remove at $Prefix" }
    Remove-FromUserPath (Join-Path $Prefix "bin")
    Ok "Done. (uninstall)"
    exit 0
}

# --- resolve version ---------------------------------------------------------
if ($Version) {
    $ver = $Version.TrimStart('v')
} else {
    Info "resolving latest release..."
    try {
        $rel = Invoke-RestMethod -Uri "https://api.github.com/repos/$RepoSlug/releases/latest" -Headers @{ "User-Agent" = "cryo-install" }
        $ver = ($rel.tag_name).TrimStart('v')
    } catch {
        Die "could not resolve the latest release. Pass -Version X.Y.Z explicitly."
    }
}

$zip    = "cryo-$ver-$target.zip"
$url    = "https://github.com/$RepoSlug/releases/download/v$ver/$zip"
$sumUrl = "$url.sha256"

Write-Host ""
Write-Host "This installer will:" -ForegroundColor Cyan
Write-Host "  - download $zip (v$ver, $target)"
Write-Host "  - verify its sha256 checksum"
Write-Host "  - install to $Prefix"
Write-Host "  - add $Prefix\bin to your User PATH"
Write-Host ""

# --- download + verify -------------------------------------------------------
$tmp = Join-Path ([System.IO.Path]::GetTempPath()) ("cryo-" + [System.IO.Path]::GetRandomFileName())
New-Item -ItemType Directory -Path $tmp | Out-Null
try {
    $zipPath = Join-Path $tmp $zip
    Info "downloading $url"
    Invoke-WebRequest -Uri $url -OutFile $zipPath -UseBasicParsing

    Info "downloading checksum"
    $sumText = (Invoke-WebRequest -Uri $sumUrl -UseBasicParsing).Content
    # sha256sum format: "<hex>  <filename>"
    $expected = ($sumText -split '\s+')[0].ToLower()
    $actual = (Get-FileHash -Algorithm SHA256 -Path $zipPath).Hash.ToLower()
    if ($expected -ne $actual) {
        Die "checksum mismatch - refusing to install.`n  expected $expected`n  actual   $actual"
    }
    Ok "checksum verified"

    # Extract (zip wraps everything in one top dir) then swap into place.
    Info "extracting"
    $stage = Join-Path $tmp "stage"
    Expand-Archive -Path $zipPath -DestinationPath $stage -Force
    $top = Get-ChildItem -Directory $stage | Select-Object -First 1
    if (-not $top) { Die "unexpected archive layout (no top-level directory)." }

    if (Test-Path $Prefix) { Remove-Item -Recurse -Force $Prefix }
    New-Item -ItemType Directory -Path (Split-Path $Prefix) -Force | Out-Null
    Move-Item -Path $top.FullName -Destination $Prefix
    Ok "installed to $Prefix"
} finally {
    Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue
}

# --- PATH --------------------------------------------------------------------
$bindir = Join-Path $Prefix "bin"
if ($NoModifyPath) {
    Info "PATH not modified. Add this directory to your PATH: $bindir"
} else {
    $cur = [Environment]::GetEnvironmentVariable("Path", "User")
    if (-not $cur) { $cur = "" }
    if (($cur.Split(';')) -notcontains $bindir) {
        $new = if ($cur) { "$cur;$bindir" } else { $bindir }
        [Environment]::SetEnvironmentVariable("Path", $new, "User")
        [Environment]::SetEnvironmentVariable("CRYO_HOME", $Prefix, "User")
        Ok "added $bindir to your User PATH"
        Info "open a new terminal for PATH changes to take effect"
    } else {
        Info "PATH already contains $bindir"
    }
}

Write-Host ""
Ok "Done.  cryo v$ver installed."
& (Join-Path $bindir "cryo.exe") --version
Write-Host ""
Write-Host "Next:" -ForegroundColor Cyan
Write-Host "  - open a new terminal, then:  cryo --version"
Write-Host "  - compiling a program needs a mingw-w64 gcc on PATH for the final link"
Write-Host "    (e.g. install MSYS2 and 'pacman -S mingw-w64-x86_64-gcc', or 'scoop install gcc')"
Write-Host ""
Write-Host "https://github.com/$RepoSlug" -ForegroundColor Cyan
