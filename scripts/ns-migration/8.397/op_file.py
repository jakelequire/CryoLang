"""Whole-file form of op_kinds.py: an operator's spelling becomes its token kind.

    python scripts/ns-migration/8.397/op_file.py <file>...

In each file:
  * `<x>.op.lexeme == "<op>"` / `!=`  ->  `<x>.op.kind == TokenType::<Kind>`
  * `const op: string = <x>.op.lexeme;`  ->  `const op: TokenType = <x>.op.kind;`
  * and, only in a file where that declaration was rewritten,
    `op == "<op>"` / `op != "<op>"`  ->  `op == TokenType::<Kind>`
A bare `op` still holding a string is refused by the compiler (a `string`
compared with a `TokenType`), which is the check that this rewrite did not
touch a variable it should not have.  Refuses (exit 1, nothing written) on a
compared spelling that is not an operator.
"""
import re, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
KIND = {
    "+": "Plus", "-": "Minus", "*": "Star", "/": "Slash", "%": "Percent",
    "==": "EqualEqual", "!=": "ExclaimEqual", "<": "LAngle", ">": "RAngle",
    "<=": "LessEqual", ">=": "GreaterEqual", "&&": "AmpAmp", "||": "PipePipe",
    "&": "Amp", "|": "Pipe", "^": "Caret", "<<": "LessLess", ">>": "GreaterGreater",
    "++": "PlusPlus", "--": "MinusMinus", "!": "Exclaim", "~": "Tilde", "=": "Equal",
}

def rewrite(path):
    raw = open(path, "rb").read()
    crlf = b"\r\n" in raw
    text = raw.decode("utf-8")
    bad = []; counts = [0, 0, 0]

    def kind_of(s, where):
        if s not in KIND: bad.append((where, s)); return None
        return KIND[s]

    def rep_member(m):
        k = kind_of(m.group(3), m.group(0))
        if k is None: return m.group(0)
        counts[0] += 1
        return "%s.op.kind %s TokenType::%s" % (m.group(1), m.group(2), k)
    text = re.sub(r'\b(\w+)\.op\.lexeme\s*(==|!=)\s*"([^"]*)"', rep_member, text)

    text, nd = re.subn(r'const op: string = (\w+)\.op\.lexeme;', r'const op: TokenType = \1.op.kind;', text)
    counts[1] = nd
    if nd:
        def rep_bare(m):
            k = kind_of(m.group(2), m.group(0))
            if k is None: return m.group(0)
            counts[2] += 1
            return "op %s TokenType::%s" % (m.group(1), k)
        text = re.sub(r'(?<![\w.])op\s*(==|!=)\s*"([^"]*)"', rep_bare, text)
    if bad:
        for w, s in bad: print("REFUSED %s: not an operator spelling %r in %s" % (path, s, w))
        return False
    open(path, "wb").write((text.replace("\n", "\r\n") if crlf and "\r\n" not in text else text).encode("utf-8"))
    print("%s: %d member comparison(s), %d declaration(s), %d bare comparison(s)" % (path, *counts))
    return True

ok = all([rewrite(p) for p in sys.argv[1:]])
sys.exit(0 if ok else 1)
