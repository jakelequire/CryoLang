<#
.SYNOPSIS
    Cryo - make sure this machine can link the programs Cryo compiles.

.DESCRIPTION
    Shipped at the root of every Windows install, and run by install.ps1 and
    by the MSI after the files are in place.  The check is the real thing:
    the installed compiler builds and runs a hello-world.  When that fails
    for want of a C toolchain, this offers to install llvm-mingw for the
    current user with winget, and checks again.

    The check runs with the PATH a new terminal gets, read from the user's
    and the machine's environment settings, not with the PATH of whatever
    process started it: an installer's actions inherit an environment that
    is not the user's, and the question is whether the user's next terminal
    can link a program.

.PARAMETER Yes
    Install what is missing without asking.

.PARAMETER No
    Install nothing; only report whether this machine links programs.

.PARAMETER Log
    Also write the report to this file (the MSI shows it when the step
    fails).

.NOTES
    Exit status: 0 when the installed compiler builds and runs a program,
    1 when it still cannot.
#>

[CmdletBinding()]
param([switch]$Yes, [switch]$No, [string]$Log)

$ErrorActionPreference = "Stop"
$Root = $PSScriptRoot
$Toolchain = "MartinStorsjo.LLVM-MinGW.UCRT"

if ($Log) {
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Log) | Out-Null
    Set-Content -Encoding utf8 -Path $Log -Value @()
}
function Report($m, $colour) {
    Write-Host $m -ForegroundColor $colour
    if ($Log) { Add-Content -Encoding utf8 -Path $Log -Value $m }
}
function Info($m) { Report "[info]  $m" Cyan }
function Ok($m)   { Report "[ok]    $m" Green }
function Warn($m) { Report "[warn]  $m" Yellow }
function Fail($m) { Report "[error] $m" Red; exit 1 }
function Show-Lines($text, $count) {
    ($text -split "`r?`n" | Where-Object { $_ } | Select-Object -Last $count) | ForEach-Object { Report "          $_" Gray }
}

if ($Yes -and $No) { Fail "-Yes and -No ask for opposite things; pass one" }

# The PATH a new terminal starts with.
function Read-UserPath {
    $env:Path = [Environment]::GetEnvironmentVariable("Path", "Machine") + ";" + [Environment]::GetEnvironmentVariable("Path", "User")
}

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

Read-UserPath
$report = Test-LinksProgram
if ($null -eq $report) { Ok "this machine links Cryo programs"; exit 0 }

# The compiler reports E0705 when it finds no C toolchain to link with; any
# other failure is not one a toolchain install answers.
if ($report -notmatch 'E0705') {
    Report "[error] the installed compiler could not build a test program, and not for want of a C toolchain:" Red
    Show-Lines $report 12
    exit 1
}
Warn "the installed compiler cannot link a program yet:"
Show-Lines $report 8

$command = "winget install --exact --id $Toolchain --scope user"
if ($No) {
    Fail "Cryo needs a C toolchain to link programs, and none was installed. Install one (for example: $command), then open a new terminal."
}
if (-not (Get-Command winget -ErrorAction SilentlyContinue)) {
    Fail ("Cryo needs a C toolchain to link programs, and winget is not installed to fetch one. " +
          "Install 'App Installer' from the Microsoft Store, then run $Root\install-dependencies.ps1 again.")
}
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

# winget puts the toolchain on the user's PATH for new terminals.
Read-UserPath
$report = Test-LinksProgram
if ($null -eq $report) { Ok "this machine links Cryo programs (open a new terminal to use them there)"; exit 0 }
Report "[error] $Toolchain is installed, and the installed compiler still cannot link a program:" Red
Show-Lines $report 12
exit 1
