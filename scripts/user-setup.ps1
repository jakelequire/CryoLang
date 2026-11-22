<#
.SYNOPSIS
    Cryo - an install's changes outside its own tree.

.DESCRIPTION
    Shipped at the root of every Windows install, and run as the installing
    user by install.ps1 and by the MSI: -AddToPath once the install's files
    are in place, -RemoveFromPath and -RemoveArchives before they are
    removed.  The install's tree is the directory this script is in.

.PARAMETER AddToPath
    Add the install's bin directory to the User PATH, unless it is there.

.PARAMETER RemoveFromPath
    Remove the install's bin directory from the User PATH.

.PARAMETER RemoveArchives
    Delete the archives the install built in the user's cache (the
    compiler's cache root, below which `archives` holds them).

.PARAMETER CacheRoot
    The compiler's cache root, for -RemoveArchives, when the caller knows
    it: the MSI names the user's LocalAppData folder, since its actions
    run with the Windows Installer service's environment rather than the
    user's.  Without it, the root is read from the environment, as the
    compiler reads it.
#>

[CmdletBinding()]
param([switch]$AddToPath, [switch]$RemoveFromPath, [switch]$RemoveArchives, [string]$CacheRoot)

$ErrorActionPreference = "Stop"
$bindir = Join-Path $PSScriptRoot "bin"

function Get-UserPathEntries {
    $current = [Environment]::GetEnvironmentVariable("Path", "User")
    if (-not $current) { return @() }
    return @($current.Split(';') | Where-Object { $_ })
}

function Test-IsBinDirectory($entry) { return $entry.TrimEnd('\') -eq $bindir.TrimEnd('\') }

if ($AddToPath) {
    $entries = Get-UserPathEntries
    if (@($entries | Where-Object { Test-IsBinDirectory $_ }).Count -gt 0) {
        Write-Host "[info]  PATH already contains $bindir"
    } else {
        [Environment]::SetEnvironmentVariable("Path", (($entries + $bindir) -join ';'), "User")
        Write-Host "[ok]    added $bindir to your User PATH (open a new terminal for it to take effect)"
    }
}

if ($RemoveFromPath) {
    $entries = Get-UserPathEntries
    $others = @($entries | Where-Object { -not (Test-IsBinDirectory $_) })
    if ($others.Count -ne $entries.Count) {
        [Environment]::SetEnvironmentVariable("Path", ($others -join ';'), "User")
        Write-Host "[ok]    removed $bindir from your User PATH"
    }
}

if ($RemoveArchives) {
    if (-not $CacheRoot) {
        $CacheRoot = if ($env:CRYO_HOME) { Join-Path $env:CRYO_HOME "cache" }
                     elseif ($env:LOCALAPPDATA) { Join-Path $env:LOCALAPPDATA "cryo" }
                     else { Join-Path $env:USERPROFILE ".cache\cryo" }
    }
    $archives = Join-Path $CacheRoot "archives"
    if (Test-Path $archives) {
        Remove-Item -Recurse -Force $archives
        Write-Host "[ok]    removed $archives"
    }
}
