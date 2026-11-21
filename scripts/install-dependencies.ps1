<#
.SYNOPSIS
    Cryo - make sure this machine can link the programs Cryo compiles.

.DESCRIPTION
    Shipped at the root of every Windows install, and run by install.ps1 and
    by the MSI after the files are in place.  The check is the real thing:
    the installed compiler builds and runs a hello-world.  When that fails
    for want of a C toolchain, this offers to install llvm-mingw for the
    current user with winget, and checks again.

.PARAMETER Yes
    Install what is missing without asking.

.NOTES
    Exit status: 0 when the installed compiler builds and runs a program,
    1 when it still cannot.
#>

[CmdletBinding()]
param([switch]$Yes)

$ErrorActionPreference = "Stop"
$Root = $PSScriptRoot
$Toolchain = "MartinStorsjo.LLVM-MinGW.UCRT"

function Info($m) { Write-Host "[info]  $m" -ForegroundColor Cyan }
function Ok($m)   { Write-Host "[ok]    $m" -ForegroundColor Green }
function Warn($m) { Write-Host "[warn]  $m" -ForegroundColor Yellow }
function Fail($m) { Write-Host "[error] $m" -ForegroundColor Red; exit 1 }

# Build and run a hello-world with the installed compiler, outside any
# project of the user's.  Answers the compiler's report when it fails, and
# $null when the program built and ran.
function Test-LinksProgram {
    $work = Join-Path ([System.IO.Path]::GetTempPath()) ("cryo-linkcheck-" + [System.IO.Path]::GetRandomFileName())
    New-Item -ItemType Directory -Path (Join-Path $work "src") | Out-Null
    Set-Content -Encoding ascii -Path (Join-Path $work "cryoconfig") -Value @(
        '[project]', 'project_name = "linkcheck"', 'output_dir = "build"',
        'source_dir = "src"', 'entry_point = "src/main.cryo"')
    Set-Content -Encoding ascii -Path (Join-Path $work "src\main.cryo") -Value @(
        'namespace Main;', '', 'import std::fmt;', '', 'function main() -> int {',
        '    fmt::printf("cryo links\n");', '    return 0;', '}')
    Push-Location $work
    try {
        $ErrorActionPreference = "Continue"
        $report = (& (Join-Path $Root "bin\cryo.exe") build 2>&1 | Out-String)
        if ($LASTEXITCODE -ne 0) { return $report }
        $ran = (& (Join-Path $work "build\linkcheck.exe") 2>&1 | Out-String).Trim()
        if ($ran -ne "cryo links") { return "the program built but printed '$ran'`n$report" }
        return $null
    } finally {
        Pop-Location
        Remove-Item -Recurse -Force $work -ErrorAction SilentlyContinue
    }
}

$report = Test-LinksProgram
if ($null -eq $report) { Ok "this machine links Cryo programs"; exit 0 }

if ($report -notmatch 'link') {
    Write-Host "[error] the installed compiler could not build a test program, and not for want of a C toolchain:" -ForegroundColor Red
    ($report -split "`r?`n" | Where-Object { $_ } | Select-Object -Last 12) | ForEach-Object { Write-Host "          $_" }
    Write-Host "        the install is damaged; install it again." -ForegroundColor Red
    exit 1
}
Warn "the installed compiler cannot link a program yet:"
($report -split "`r?`n" | Where-Object { $_ } | Select-Object -Last 8) | ForEach-Object { Write-Host "          $_" }

if (-not (Get-Command winget -ErrorAction SilentlyContinue)) {
    Fail ("Cryo needs a C toolchain to link programs, and winget is not installed to fetch one. " +
          "Install 'App Installer' from the Microsoft Store, then run $Root\install-dependencies.ps1 again.")
}
$command = "winget install --exact --id $Toolchain --scope user"
if (-not $Yes) {
    if (-not [Environment]::UserInteractive) {
        Fail "Cryo needs a C toolchain to link programs, and there is no one to ask. Run again with -Yes to let it run: $command"
    }
    $reply = Read-Host "Cryo needs a C toolchain to link programs. Install llvm-mingw for this user now ($command)? [Y/n]"
    if ($reply -and ($reply -notmatch '^(y|yes)$')) {
        Fail "not installed; Cryo cannot link programs until a C toolchain is installed"
    }
}

Info $command
& winget install --exact --id $Toolchain --scope user --silent --accept-package-agreements --accept-source-agreements
# A toolchain winget reports as already installed is judged by the check
# below like a new one.
if ($LASTEXITCODE -ne 0) { Warn "winget exited $LASTEXITCODE installing $Toolchain" }

# winget puts the toolchain on the user's PATH for new processes; this one
# reads it again so the check below sees it.
$env:Path = [Environment]::GetEnvironmentVariable("Path", "User") + ";" + [Environment]::GetEnvironmentVariable("Path", "Machine")

$report = Test-LinksProgram
if ($null -eq $report) { Ok "this machine links Cryo programs (open a new terminal to use them there)"; exit 0 }
Write-Host "[error] $Toolchain is installed, and the installed compiler still cannot link a program:" -ForegroundColor Red
($report -split "`r?`n" | Where-Object { $_ } | Select-Object -Last 12) | ForEach-Object { Write-Host "          $_" }
exit 1
