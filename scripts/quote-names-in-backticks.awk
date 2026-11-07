# One-off codemod: diagnostics quote a name or token as `x`, not 'x'.
#
# Rewrites, in place (gawk -i inplace), each line named in SITES - a
# space-separated list of FILE:LINE - turning every 'x' whose x holds no
# quote, space or backslash into `x`.  The list is the message lines of
# compiler/src/compiler holding such a pair inside a string literal, minus
# the AST dumper's output, bindgen's import report, log lines, console
# printing and the one line whose quotes spell a character literal:
#
#   grep -rnE "\"[^\"]*'[^' \"\\\\]+'[^\"]*\"" --include=*.cryo compiler/src/compiler \
#     | grep -v "AST/dumper\|bindgen/\|Log::\|printf\|eprintln\|println\|lex/lexer.cryo:1031:" \
#     | grep -v "^\S*:[0-9]*:\s*//" | cut -d: -f1,2
#
# Run as:  gawk -i inplace -v SITES="..." -f scripts/quote-names-in-backticks.awk FILE...

BEGIN {
    n = split(SITES, list, " ")
    for (k = 1; k <= n; k++) wanted[list[k]] = 1
}
#
# The expectations follow: with -v AFTER="//~" only the text after the first
# AFTER on a line is rewritten, so a character literal in the code an
# annotation sits beside is left alone.  The negative files' lines are
#   grep -rn "//~.*'" tests/tests/negative | cut -d: -f1,2
# and the projects' test.json lines quoting a renamed message are listed by
# hand.
{
    if ((FILENAME ":" FNR) in wanted) {
        at = (AFTER == "") ? 0 : index($0, AFTER)
        head = substr($0, 1, at)
        tail = substr($0, at + 1)
        $0 = head gensub(/'([^' "\\\\]+)'/, "`\\1`", "g", tail)
    }
    print
}
