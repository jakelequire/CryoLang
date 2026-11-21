<#
.SYNOPSIS
    Cryo - the MSI's changes outside its own files.

.DESCRIPTION
    Shipped at the root of an install the MSI made, and run by the MSI
    itself as the installing user: -AddToPath after the files are in place,
    -RemoveFromPath before they are removed.  Windows Installer removes only
    the files it installed, so removal also deletes the archives the install
    built in the user's cache.

.PARAMETER AddToPath
    Add the install's bin directory to the User PATH.

.PARAMETER RemoveFromPath
    Remove it, and the archives the install built in the user's cache.
#>

[CmdletBinding()]
param([switch]$AddToPath, [switch]$RemoveFromPath)

$ErrorActionPreference = "Stop"
$bindir = Join-Path $PSScriptRoot "bin"
$current = [Environment]::GetEnvironmentVariable("Path", "User")
if (-not $current) { $current = "" }
$others = @($current.Split(';') | Where-Object { $_ -and ($_.TrimEnd('\') -ne $bindir.TrimEnd('\')) })

if ($AddToPath) {
    [Environment]::SetEnvironmentVariable("Path", (($others + $bindir) -join ';'), "User")
}
if ($RemoveFromPath) {
    [Environment]::SetEnvironmentVariable("Path", ($others -join ';'), "User")
    $archives = if ($env:LOCALAPPDATA) { Join-Path $env:LOCALAPPDATA "cryo\archives" }
                else { Join-Path $env:USERPROFILE ".cache\cryo\archives" }
    if (Test-Path $archives) { Remove-Item -Recurse -Force $archives }
}
