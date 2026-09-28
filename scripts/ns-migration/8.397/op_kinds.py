"""Rewrite an operator compared by its spelling into one compared by its token kind.

    python scripts/ns-migration/8.397/op_kinds.py <file> <var> <first line> <last line>

Inside lines [first, last] (1-based, inclusive) of <file>, every
`<var> == "<op>"` / `<var> != "<op>"` becomes `<var> == TokenType::<Kind>` /
`<var> != TokenType::<Kind>`, where <var> is an expression such as `op` or
`u.op.kind` (the caller names the kind-valued expression to write; the
spelling-valued expression it replaces is given with --from).

    --from <expr>   the spelling-valued expression being replaced (default: <var>)

Refuses (exit 1, nothing written) when a compared spelling is not an operator
in the table below, or when the range holds no comparison at all.
"""
import re, sys

KIND = {
    "+": "Plus", "-": "Minus", "*": "Star", "/": "Slash", "%": "Percent",
    "==": "EqualEqual", "!=": "ExclaimEqual", "<": "LAngle", ">": "RAngle",
    "<=": "LessEqual", ">=": "GreaterEqual", "&&": "AmpAmp", "||": "PipePipe",
    "&": "Amp", "|": "Pipe", "^": "Caret", "<<": "LessLess", ">>": "GreaterGreater",
    "++": "PlusPlus", "--": "MinusMinus", "!": "Exclaim", "~": "Tilde", "=": "Equal",
}

def main():
    args = sys.argv[1:]
    src = None
    if "--from" in args:
        i = args.index("--from"); src = args[i + 1]; del args[i:i + 2]
    path, var, first, last = args[0], args[1], int(args[2]), int(args[3])
    src = src or var
    raw = open(path, "rb").read()
    crlf = b"\r\n" in raw
    lines = raw.decode("utf-8").split("\r\n" if crlf else "\n")
    pat = re.compile(r'%s\s*(==|!=)\s*"([^"]*)"' % re.escape(src))
    n = 0; bad = []
    for i in range(first - 1, last):
        def rep(m):
            nonlocal n
            if m.group(2) not in KIND: bad.append((i + 1, m.group(2))); return m.group(0)
            n += 1
            return "%s %s TokenType::%s" % (var, m.group(1), KIND[m.group(2)])
        lines[i] = pat.sub(rep, lines[i])
    if bad:
        for ln, s in bad: print("REFUSED %s:%d: not an operator spelling: %r" % (path, ln, s))
        sys.exit(1)
    if n == 0:
        print("REFUSED %s:%d-%d: no comparison of %s" % (path, first, last, src)); sys.exit(1)
    open(path, "wb").write(("\r\n" if crlf else "\n").join(lines).encode("utf-8"))
    print("%s: %d comparison(s) rewritten" % (path, n))

main()
