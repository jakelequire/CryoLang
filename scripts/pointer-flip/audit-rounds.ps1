# Run audit -> (E0312 conversion) -> codemod rounds over one project until it
# checks clean or a round yields nothing to mark.  -Scratch holds the audit
# compiler (audit-build/cryo.exe), the codemod (codemod-build/) and the logs.
param([string]$Dir, [string[]]$AuditArgs, [string]$Scratch, [int]$Rounds = 12, [string]$Tag = "r")
$scratch = $Scratch
$audit = "$scratch\audit-build\cryo.exe"
$codemod = "$scratch\codemod-build\pointer_codemod.exe"
$e0312 = Join-Path $PSScriptRoot "e0312-audit.ps1"
$utf8 = New-Object System.Text.UTF8Encoding($false)
Set-Location $Dir
for ($r = 1; $r -le $Rounds; $r++) {
    $out = "$scratch\logs\$Tag-$r.out"; $err = "$scratch\logs\$Tag-$r.err"
    New-Item -ItemType Directory -Force "$scratch\logs" | Out-Null
    Start-Process -FilePath $audit -ArgumentList $AuditArgs -WorkingDirectory $Dir -NoNewWindow -Wait `
        -RedirectStandardOutput $out -RedirectStandardError $err
    $all = @(Get-Content $out) + @(Get-Content $err)
    $errors = $all | Where-Object { $_ -match "^error\[" }
    $codes = $errors | ForEach-Object { ($_ -split "\]")[0] } | Group-Object | ForEach-Object { "$($_.Name.Substring(6)) x$($_.Count)" }
    $audits = ($all | Where-Object { $_ -like "AUDIT`t*" }).Count
    $misses = ($all | Where-Object { $_ -like "AUDIT-MISS*" }).Count
    "round $r : errors $($errors.Count) [$($codes -join ', ')] audit $audits miss $misses"
    if ($errors.Count -eq 0) {
        # A build that failed with no error line (no library found, a link
        # failure) is not a clean audit.
        if ($all | Where-Object { $_ -match "(compilation|Check) failed" }) { "FAILED without an error line: see $out"; break }
        "CLEAN"; break
    }
    $combined = "$scratch\logs\$Tag-$r.all"
    [IO.File]::WriteAllLines($combined, [string[]]$all, $utf8)
    $inputs = @($combined)
    if (($errors | Where-Object { $_ -match "^error\[E0312" }).Count -gt 0) {
        $conv = & $e0312 -InputFile $combined -Root $Dir
        $convFile = "$scratch\logs\$Tag-$r.e0312"
        [IO.File]::WriteAllLines($convFile, [string[]]@($conv), $utf8)
        $conv | Where-Object { $_ -notlike "AUDIT*" } | Select-Object -First 5
        $inputs += $convFile
        $audits += ($conv | Where-Object { $_ -like "AUDIT*" }).Count
    }
    if ($audits -eq 0) { "STUCK: nothing to mark"; break }
    & $codemod @inputs | Select-Object -Last 3
}
