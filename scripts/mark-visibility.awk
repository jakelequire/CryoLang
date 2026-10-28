# Writes a visibility keyword onto every standard library item and member
# that has none, as a decision list says.  Usage:
#   awk -v marks=LIST -f mark.awk FILE...          (rewrite FILE.marked)
#   awk -v marks=LIST -v dry=1 -f mark.awk FILE... (list only)
# LIST lines: `FILE KEYWORD` sets the file's default; `FILE NAME KEYWORD`
# overrides one item (`Item`), field (`Type.field`) or method (`Type.method()`).  Members are those
# at depth 1 of a `type struct|union` or a non-trait `implement` block;
# trait members and trait implementations take no keyword, nor does a
# member under a `public:`/`private:`/`internal:` section label.  An unmarked
# declaration with no decision is reported and the file is left alone.
function strip(s) { sub(/\/\/.*/, "", s); gsub(/"([^"\\]|\\.)*"/, "\"\"", s); gsub(/'([^'\\]|\\.)*'/, "''", s); return s }
function declared_name(s,   t) {
    t = s
    sub(/^[ \t]*/, "", t)
    sub(/^(public |private |internal )/, "", t)
    sub(/^(type (struct|union|enum|trait|class) |function |const |mut |intrinsic function |async function |static |async )/, "", t)
    sub(/^(function )/, "", t)
    match(t, /^[A-Za-z_][A-Za-z_0-9]*/)
    return substr(t, RSTART, RLENGTH)
}
function flush(   i) {
    if (current == "") return
    if (failed[current]) { print "LEFT ALONE: " current; }
    else if (!dry) {
        for (i = 1; i <= count; i++) print buffer[i] > (current ".marked")
        close(current ".marked")
    }
    count = 0
}
BEGIN {
    while ((getline entry < marks) > 0) {
        if (entry ~ /^#/ || entry ~ /^[ \t]*$/) continue
        n = split(entry, part, /[ \t]+/)
        if (n == 2) default_keyword[part[1]] = part[2]
        else if (n == 3) override[part[1] " " part[2]] = part[3]
        else { print "BAD LINE: " entry; exit 1 }
    }
}
FNR == 1 { flush(); current = FILENAME; depth = 0; ctx = ""; owner = "" }
{
    line = strip($0)
    text = $0
    key = ""
    if (depth == 0) {
        if (line ~ /^(type |function |const |mut [A-Za-z_]+ *:|intrinsic function |async function )/) key = declared_name(line)
        if (line ~ /^(public |private |internal )?type (struct|union) / || line ~ /^implement (struct|enum|union) /) {
            ctx = "member"; labelled = 0
            t = line; sub(/^(public |private |internal )?(type|implement) (struct|union|enum) /, "", t)
            match(t, /^[A-Za-z_][A-Za-z_0-9]*/); owner = substr(t, RSTART, RLENGTH)
        } else if (line ~ /[{]/) ctx = "other"
    } else if (depth == 1 && ctx == "member") {
        if (line ~ /^(public|private|internal):/) labelled = 1
        else if (!labelled && line ~ /^    (static |async |[A-Za-z_][A-Za-z_0-9]*(<[^>]*>)? *\(|[A-Za-z_][A-Za-z_0-9]* *:)/) {
            key = owner "." declared_name(line)
            if (line !~ /^    [A-Za-z_][A-Za-z_0-9]* *:/) key = key "()"
        }
    }
    if (key != "") {
        keyword = ""
        if ((FILENAME " " key) in override) keyword = override[FILENAME " " key]
        else if (FILENAME in default_keyword) keyword = default_keyword[FILENAME]
        if (keyword == "") { print "UNDECIDED: " FILENAME ":" FNR ": " key; failed[FILENAME] = 1 }
        else if (keyword != "none") {
            used[FILENAME " " key] = 1
            if (dry) print keyword "\t" FILENAME ":" FNR "\t" key
            match(text, /^[ \t]*/)
            text = substr(text, 1, RLENGTH) keyword " " substr(text, RLENGTH + 1)
        }
    }
    buffer[++count] = text
    o = gsub(/[{]/, "{", line); c = gsub(/[}]/, "}", line)
    depth += o - c
    if (depth <= 0) { depth = 0 }
}
END {
    flush()
    for (k in override) if (!used[k]) print "UNUSED OVERRIDE: " k
}
