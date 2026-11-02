# Turn the audit compiler's E0312 (trait method parameter differs) into AUDIT
# lines: when the implementation is marked `mut` and the trait is not, the
# trait's declaration of that parameter; when the trait is marked and the
# implementation is not, the implementation's.  Run from the directory the
# audit ran in; -Root is where the sources are searched for the trait.
param([string]$InputFile, [string]$Root)
$lines = Get-Content $InputFile
$utf8 = New-Object System.Text.UTF8Encoding($false)
$traitFiles = @{}
function Find-TraitFile([string]$trait) {
    if ($traitFiles.ContainsKey($trait)) { return $traitFiles[$trait] }
    $hit = Get-ChildItem -Path $Root -Recurse -Filter *.cryo | Select-String -Pattern "type trait $trait\b" | Select-Object -First 1
    $traitFiles[$trait] = $hit
    return $hit
}
function Param-Span([string]$text, [int]$index) {
    # The `index`-th parameter after the receiver; answers start/end columns (1-based, end exclusive) of its type.
    $open = $text.IndexOf('(')
    $depth = 0; $starts = @(); $s = $open + 1
    for ($i = $open + 1; $i -lt $text.Length; $i++) {
        $c = $text[$i]
        if ($c -eq '(' -or $c -eq '<' -or $c -eq '[') { $depth++ }
        elseif ($c -eq ')' -or $c -eq '>' -or $c -eq ']') {
            if ($depth -eq 0) { $starts += ,@($s, $i); break }
            $depth--
        }
        elseif ($c -eq ',' -and $depth -eq 0) { $starts += ,@($s, $i); $s = $i + 1 }
    }
    $hasThis = $text.Substring($starts[0][0], $starts[0][1] - $starts[0][0]) -match 'this'
    $k = if ($hasThis) { $index } else { $index - 1 }
    if ($k -ge $starts.Count) { return $null }
    $seg = $starts[$k]
    $param = $text.Substring($seg[0], $seg[1] - $seg[0])
    $colon = $param.IndexOf(':')
    $typeStart = $seg[0] + $colon + 1
    while ($text[$typeStart] -eq ' ') { $typeStart++ }
    $typeEnd = $seg[1]
    while ($text[$typeEnd - 1] -eq ' ') { $typeEnd-- }
    return @(($typeStart + 1), ($typeEnd + 1))
}
for ($i = 0; $i -lt $lines.Count; $i++) {
    $m = [regex]::Match($lines[$i], '^error\[E0312\]: parameter (\d+) of `(\w+)` is `([^`]*)` in trait `(\w+)`, but `([^`]*)` here')
    if (-not $m.Success) { continue }
    $index = [int]$m.Groups[1].Value; $method = $m.Groups[2].Value
    $inTrait = $m.Groups[3].Value; $trait = $m.Groups[4].Value; $here = $m.Groups[5].Value
    $loc = [regex]::Match($lines[$i + 1], '--> (.+):(\d+):(\d+)')
    if ($inTrait.StartsWith('const ') -and -not $here.StartsWith('const ')) {
        $hit = Find-TraitFile $trait
        if ($hit -eq $null) { "NOTRAIT $trait"; continue }
        $tf = $hit.Path
        $all = [IO.File]::ReadAllLines($tf, $utf8)
        $n = 0
        for ($j = $hit.LineNumber; $j -lt $all.Count; $j++) {
            if ($all[$j] -match "^\s+$method\(") { $n = $j + 1; break }
        }
        if ($n -eq 0) { "NOMETHOD $trait $method"; continue }
        $span = Param-Span $all[$n - 1] $index
        if ($span -eq $null) { "NOPARAM $tf $n $method $index"; continue }
        $rel = Resolve-Path -Relative $tf
        "AUDIT`t$($rel.Replace('\','/').TrimStart('.').TrimStart('/'))`t$n`t$($span[0])`t$n`t$($span[1])"
    } elseif (-not $inTrait.StartsWith('const ') -and $here.StartsWith('const ')) {
        $file = $loc.Groups[1].Value; $n = [int]$loc.Groups[2].Value
        $all = [IO.File]::ReadAllLines((Join-Path (Get-Location) $file), $utf8)
        $line = $all[$n - 1]
        $span = Param-Span $line $index
        if ($span -eq $null) { "NOPARAM $file $n $method $index"; continue }
        "AUDIT`t$file`t$n`t$($span[0])`t$n`t$($span[1])"
    } else {
        "OTHER $($lines[$i])"
    }
}
