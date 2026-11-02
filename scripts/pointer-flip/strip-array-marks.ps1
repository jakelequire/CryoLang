# Remove `mut ` in front of an array-type pointee (`mut [T]*` -> `[T]*`) in
# the given tree, which the pin cannot parse, and list each site.  Casts
# (`as mut [T]*`) parse already and are left alone.  Byte-preserving: reads
# and writes UTF-8 without a BOM, line endings untouched.
param([string]$Root)
$utf8 = New-Object System.Text.UTF8Encoding($false)
$re = [regex]'(?<!\bas )mut (\[[^\]\r\n]*\]\*)'
Get-ChildItem -Path $Root -Recurse -Filter *.cryo | ForEach-Object {
    $text = [IO.File]::ReadAllText($_.FullName, $utf8)
    $ms = $re.Matches($text)
    if ($ms.Count -gt 0) {
        foreach ($m in $ms) {
            $line = ($text.Substring(0, $m.Index).Split("`n")).Count
            "SITE`t$($_.FullName)`t$line`t$($m.Value)"
        }
        [IO.File]::WriteAllText($_.FullName, $re.Replace($text, '$1'), $utf8)
    }
}
