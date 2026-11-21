<#
.SYNOPSIS
    Cryo Programming Language - Windows installer.

.DESCRIPTION
    The one-liner target for Windows:

        irm https://cryolang.org/install.ps1 | iex

    To pass options through the one-liner (a version, -Uninstall):

        & ([scriptblock]::Create((irm https://cryolang.org/install.ps1))) -Version 1.0.0

    Downloads the Windows x86_64 release zip from GitHub Releases (or takes
    a local one with -Archive), verifies its sha256, installs it into
    %USERPROFILE%\.cryo, and adds %USERPROFILE%\.cryo\bin to the User PATH.
    An existing install is replaced whole: the new tree is put in place
    before the old one is removed.

    The install holds cryo.exe, the cryolsp.exe language server, the LLVM
    DLLs they load, the standard library and the runtime.  The compiler
    finds its standard library and runtime beside itself; no environment
    variable is set.

    Compiling a program links it with a C toolchain.  After installing, the
    installer builds a hello-world with the installed compiler; when that
    fails it offers to install llvm-mingw for the current user with winget
    (install-dependencies.ps1, shipped in the install).

.PARAMETER Version
    Install a specific version (e.g. 1.0.0). Default: latest GitHub release.

.PARAMETER Archive
    Install this local release zip instead of downloading one.  A sibling
    <zip>.sha256, when present, is checked.

.PARAMETER Prefix
    Install root. Default: $env:USERPROFILE\.cryo

.PARAMETER Uninstall
    Remove the install, its PATH entry, and the archives it built in the
    user's cache.

.PARAMETER NoModifyPath
    Do not modify the User PATH; print the directory to add instead.

.PARAMETER Yes
    Install a missing C toolchain without asking.
#>

[CmdletBinding()]
param(
    [string]$Version = "",
    [string]$Archive = "",
    [string]$Prefix  = "",
    [switch]$Uninstall,
    [switch]$NoModifyPath,
    [switch]$Yes
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

if (-not $Prefix) { $Prefix = Join-Path $env:USERPROFILE ".cryo" }
$Prefix = [System.IO.Path]::GetFullPath($Prefix)
$bindir = Join-Path $Prefix "bin"

# An install the MSI made is the MSI's to change: Apps & Features records
# it, and an upgrade or removal by this script would leave that record
# pointing at files it no longer describes.
$msiInstall = Get-ChildItem "HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall" -ErrorAction SilentlyContinue |
    ForEach-Object { Get-ItemProperty $_.PSPath } |
    Where-Object { $_.DisplayName -eq "Cryo" -and $_.InstallLocation -and
                   ([System.IO.Path]::GetFullPath($_.InstallLocation).TrimEnd('\') -eq $Prefix.TrimEnd('\')) }
if ($msiInstall) {
    Die ("Cryo is installed at $Prefix by its MSI. Upgrade it with a newer MSI or winget " +
         "(winget upgrade CryoLang.Cryo), and remove it from Settings > Apps.")
}

# The install's changes outside its tree (the PATH entry, the archives it
# builds in the user's cache) are made and undone by the user-setup.ps1 it
# ships, the script the MSI runs too.
$userSetup = Join-Path $Prefix "user-setup.ps1"

# --- uninstall ---------------------------------------------------------------
if ($Uninstall) {
    if (-not (Test-Path $Prefix)) {
        Info "nothing to remove at $Prefix"
        exit 0
    }
    $undo = @{ RemoveArchives = $true }
    if (-not $NoModifyPath) { $undo["RemoveFromPath"] = $true }
    & $userSetup @undo
    Remove-Item -Recurse -Force $Prefix
    Ok "removed $Prefix"
    Ok "Done. (uninstall)"
    exit 0
}

# --- the zip -----------------------------------------------------------------
$tmp = Join-Path ([System.IO.Path]::GetTempPath()) ("cryo-" + [System.IO.Path]::GetRandomFileName())
New-Item -ItemType Directory -Path $tmp | Out-Null
try {
    if ($Archive) {
        if (-not (Test-Path $Archive)) { Die "no archive at $Archive" }
        $zipPath = [System.IO.Path]::GetFullPath($Archive)
        $sumPath = "$zipPath.sha256"
        $sumText = if (Test-Path $sumPath) { Get-Content -Raw $sumPath } else { "" }
        Info "installing $zipPath"
    } else {
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
        $zip = "cryo-$ver-$target.zip"
        $url = "https://github.com/$RepoSlug/releases/download/v$ver/$zip"
        $zipPath = Join-Path $tmp $zip
        Info "downloading $url"
        Invoke-WebRequest -Uri $url -OutFile $zipPath -UseBasicParsing
        Info "downloading checksum"
        $sumText = (Invoke-WebRequest -Uri "$url.sha256" -UseBasicParsing).Content
        if ($sumText -is [byte[]]) { $sumText = [System.Text.Encoding]::ASCII.GetString($sumText) }
    }
    if ($sumText) {
        # sha256sum format: "<hex>  <filename>" (a leading '*' marks binary mode)
        $expected = ($sumText.Trim() -split '\s+')[0].ToLower()
        $actual = (Get-FileHash -Algorithm SHA256 -Path $zipPath).Hash.ToLower()
        if ($expected -ne $actual) {
            Die "checksum mismatch - refusing to install.`n  expected $expected`n  actual   $actual"
        }
        Ok "checksum verified"
    } elseif (-not $Archive) {
        Die "no checksum was published beside the release zip - refusing to install."
    } else {
        Warn "no $sumPath beside the archive; installing it unchecked"
    }

    # Extract (the zip wraps everything in one top dir) then swap into place.
    Info "extracting"
    $stage = Join-Path $tmp "stage"
    Expand-Archive -Path $zipPath -DestinationPath $stage
    $top = @(Get-ChildItem -Directory $stage)
    if ($top.Count -ne 1) {
        Die "unexpected archive layout (expected exactly one top-level directory, found $($top.Count))."
    }
    $top = $top[0]
    foreach ($required in @("bin\cryo.exe", "bin\cryolsp.exe", "bin\LLVM-C.dll", "bin\libclang.dll",
                            "stdlib\lib.cryo", "stdlib\.bin\shipped", "runtime\lib.cryo",
                            "runtime\.bin\shipped", "install-dependencies.ps1", "user-setup.ps1", "VERSION")) {
        if (-not (Test-Path (Join-Path $top.FullName $required))) {
            Die "archive is missing '$required' - corrupted or unexpected layout, refusing to install."
        }
    }
    $ver = (Get-Content -Raw (Join-Path $top.FullName "VERSION")).Trim()

    # The previous install is moved ASIDE (not deleted) until the new one is
    # in place, so a failed move can never destroy a working install.
    New-Item -ItemType Directory -Path (Split-Path $Prefix) -Force | Out-Null
    $incoming = "$Prefix.new"
    $outgoing = "$Prefix.old"
    if (Test-Path $incoming) { Remove-Item -Recurse -Force $incoming }
    if (Test-Path $outgoing) { Remove-Item -Recurse -Force $outgoing }
    Move-Item -Path $top.FullName -Destination $incoming
    if (Test-Path $Prefix) {
        try { Move-Item -Path $Prefix -Destination $outgoing }
        catch { Die "could not move the current install at $Prefix aside (is cryo.exe or cryolsp.exe running?): $_" }
    }
    try {
        Move-Item -Path $incoming -Destination $Prefix
    } catch {
        if (Test-Path $outgoing) { Move-Item -Path $outgoing -Destination $Prefix }
        Die "failed to move the new install into place: $_"
    }
    if (Test-Path $outgoing) {
        try { Remove-Item -Recurse -Force $outgoing }
        catch { Warn "could not remove the previous install at $outgoing (a cryo.exe may still be running) - delete it manually." }
    }
    Ok "installed cryo $ver to $Prefix"
} finally {
    Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue
}

& (Join-Path $bindir "cryo.exe") --version
if ($LASTEXITCODE -ne 0) {
    Die "post-install verification failed: 'cryo.exe --version' exited with code $LASTEXITCODE. The download may be corrupted - re-run the installer."
}

# --- PATH --------------------------------------------------------------------
if ($NoModifyPath) {
    Info "PATH not modified. Add this directory to your PATH: $bindir"
} else {
    & $userSetup -AddToPath
}

# --- the C toolchain ---------------------------------------------------------
$dependencyArguments = @{}
if ($Yes) { $dependencyArguments["Yes"] = $true }
& (Join-Path $Prefix "install-dependencies.ps1") @dependencyArguments
$linked = ($LASTEXITCODE -eq 0)

Write-Host ""
if ($linked) {
    Ok "Done.  cryo $ver is installed and links programs."
} else {
    Warn "cryo $ver is installed, but cannot link programs until the step above succeeds:"
    Warn "  $Prefix\install-dependencies.ps1"
}
Write-Host "  which install runs:  cryo version --verbose"
Write-Host "  uninstall:           & ([scriptblock]::Create((irm https://cryolang.org/install.ps1))) -Uninstall"
if (-not $linked) { exit 1 }
