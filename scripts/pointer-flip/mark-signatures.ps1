# Mark `[X*]` element pointers `mut` in the signatures of the named
# functions (from the declaration line to the line ending the signature), and
# on the given single lines.  X is one of the AST node types whose arrays are
# written through, which arrays being invariant in their elements forces on
# every function they are passed to.  Byte-preserving.
param([string]$Root, [string[]]$Functions, [string[]]$Lines = @())
$utf8 = New-Object System.Text.UTF8Encoding($false)
$types = 'TypeAnnotation|MatchArmNode|StaticMatchArmNode|ExpressionNode|ImplBlock|DeclarationNode|CallExpr|EnumPattern'
$re = [regex]"\[(($types)\*)\]"
function Mark([string]$s) { return $re.Replace($s, '[mut $1]') }
$byFile = @{}
Get-ChildItem -Path $Root -Recurse -Filter *.cryo | ForEach-Object {
    $text = [IO.File]::ReadAllText($_.FullName, $utf8)
    $nl = if ($text.Contains("`r`n")) { "`r`n" } else { "`n" }
    $rows = $text -split "`r?`n"
    $changed = 0
    for ($i = 0; $i -lt $rows.Count; $i++) {
        foreach ($fn in $Functions) {
            if ($rows[$i] -match "^\s+(public |private |internal |static )*$fn\(") {
                for ($j = $i; $j -lt $rows.Count -and $j -lt $i + 8; $j++) {
                    $new = Mark $rows[$j]
                    if ($new -ne $rows[$j]) { $rows[$j] = $new; $changed++ }
                    if ($rows[$j] -match '[{;]\s*$') { break }
                }
            }
        }
    }
    $rel = $_.FullName.Substring($Root.Length + 1).Replace('\', '/')
    foreach ($l in $Lines) {
        $p = $l -split ':'
        if ($p[0] -eq $rel) {
            $k = [int]$p[1] - 1
            $new = Mark $rows[$k]
            if ($new -ne $rows[$k]) { $rows[$k] = $new; $changed++ }
        }
    }
    if ($changed -gt 0) {
        [IO.File]::WriteAllText($_.FullName, ($rows -join $nl), $utf8)
        "$rel $changed"
    }
}
