# One-off codemod for `fmt::format` returning an owned `String`.
#
# Rewrites a Cryo source file in place (gawk -i inplace): every call
# `fmt::format(...)` becomes `fmt::format(...).into_raw()`, which hands the
# text on as the raw `string` the call produced before, unless the innermost
# call the format is an argument of is one of OWNING (below) - a receiver
# whose message parameter takes any `Display` value and keeps its own copy,
# so the owned `String` is moved in and released there.
#
# Run as:  gawk -i inplace -v OWNING="name name ..." -f scripts/format-into-raw.awk FILE...
# With OWNING empty every site keeps the raw string: the rewrite changes no
# behaviour, which is the baseline the owning rewrite is measured against.
#
# A name is matched by spelling, so a call of another function that shares
# one (an `Array<string>`'s `push` beside a String's) is mis-sorted; the
# compiler refuses each of those, and a second pass with
# -v FORCE="FILE:LINE:COLUMN ..." (the refusals' own positions, OWNING empty)
# gives exactly those calls the raw string.
#
# Strings, character literals and comments are skipped, so a parenthesis
# inside one is not counted.

BEGIN {
    RS = "^$"
    n_owning = split(OWNING, owning_list, " ")
    for (k = 1; k <= n_owning; k++) owning[owning_list[k]] = 1
    n_force = split(FORCE, force_list, " ")
    for (k = 1; k <= n_force; k++) forced[force_list[k]] = 1
}

# Line and column (1-based) of the character at `at` in `text`.  Positions
# are asked for in increasing order, so the newlines are counted once.
function position_of(text, at,    k) {
    for (k = scanned; k < at; k++) {
        if (substr(text, k, 1) == "\n") { line_number++; last_newline = k }
    }
    scanned = at
    return line_number ":" (at - last_newline)
}

{
    text = $0
    n = length(text)
    scanned = 1
    line_number = 1
    last_newline = 0
    out = ""
    depth = 0
    word = ""
    i = 1
    while (i <= n) {
        c = substr(text, i, 1)
        two = substr(text, i, 2)
        if (two == "//") {
            j = index(substr(text, i), "\n")
            if (j == 0) j = n - i + 2
            out = out substr(text, i, j - 1)
            i += j - 1
            word = ""
            continue
        }
        if (two == "/*") {
            j = index(substr(text, i + 2), "*/")
            if (j == 0) j = n - i
            out = out substr(text, i, j + 3)
            i += j + 3
            word = ""
            continue
        }
        if (c == "\"") {
            j = i + 1
            while (j <= n) {
                d = substr(text, j, 1)
                if (d == "\\") { j += 2; continue }
                if (d == "\"") break
                j++
            }
            out = out substr(text, i, j - i + 1)
            i = j + 1
            word = ""
            continue
        }
        if (c == "'") {
            len = (substr(text, i + 1, 1) == "\\") ? 4 : 3
            out = out substr(text, i, len)
            i += len
            word = ""
            continue
        }
        if (c ~ /[A-Za-z0-9_:]/) { word = word c; out = out c; i++; continue }
        if (c == "(") {
            depth++
            name[depth] = word
            raw[depth] = 0
            if (word == "fmt::format") {
                enclosing = ""
                for (k = depth - 1; k >= 1; k--) {
                    if (name[k] != "") { enclosing = name[k]; break }
                }
                if (n_force > 0) {
                    at = FILENAME ":" position_of(text, i - length(word))
                    raw[depth] = (at in forced)
                } else {
                    raw[depth] = !(enclosing in owning)
                }
            }
            word = ""
            out = out c
            i++
            continue
        }
        if (c == ")") {
            out = out c
            if (depth > 0) {
                if (raw[depth]) out = out ".into_raw()"
                depth--
            }
            word = ""
            i++
            continue
        }
        word = ""
        out = out c
        i++
    }
    printf "%s", out
}
