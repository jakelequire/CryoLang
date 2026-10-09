"""Make the async lowering's frame-root walks answer with the root USE.

Usage: python scripts/ns-migration/8.561/frame_roots.py [--apply]

`addr_place_root`, `call_frame_addr_root`, `frame_addr_root_expr`,
`frame_addr_root_stmt` and `assigned_frame_addr_root` in
`compiler/src/compiler/sema/async_lower.cryo` answer with the spelling of the
frame-resident local a place is rooted in, and the empty symbol for none.
This rewrites each to answer with the identifier node that names the root,
and null for none: within those methods `-> SymbolStr {` becomes
`-> IdentifierNode* {`, `SymbolStr::empty()` becomes `null`, a local declared
`: SymbolStr = this.<walk>(` becomes `: IdentifierNode* = ...`, and
`<x>.is_valid()` on such a local becomes `<x> != null`.  The identifier case
of `addr_place_root` - the one place a root is recognised - is converted by
hand.

Without --apply it prints the count per method and writes nothing.
"""
import re
import sys

PATH = "compiler/src/compiler/sema/async_lower.cryo"

METHODS = ["addr_place_root", "call_frame_addr_root", "frame_addr_root_expr",
           "frame_addr_root_stmt", "assigned_frame_addr_root"]

DECL = re.compile(r"const (\w+): SymbolStr = this\.(addr_place_root|call_frame_addr_root|"
                  r"frame_addr_root_expr|frame_addr_root_stmt)\(")


def main():
    apply = "--apply" in sys.argv
    with open(PATH, encoding="utf-8", newline="") as fh:
        lines = fh.read().split("\n")
    done = {}
    i = 0
    while i < len(lines):
        m = re.match(r"    ([a-z_0-9]+)\(", lines[i])
        if not m or m.group(1) not in METHODS:
            i += 1
            continue
        meth = m.group(1)
        if meth in done:
            sys.exit("method %s found twice" % meth)
        n = 0
        roots = set()
        j = i
        while True:
            line = lines[j]
            if j > i and line.rstrip("\r") == "    }":
                break
            new = line.replace("-> SymbolStr {", "-> IdentifierNode* {")
            new = new.replace("SymbolStr::empty()", "null")
            d = DECL.search(new)
            if d:
                roots.add(d.group(1))
                new = new.replace("const %s: SymbolStr =" % d.group(1),
                                  "const %s: IdentifierNode* =" % d.group(1))
            for r in roots:
                new = re.sub(r"\b%s\.is_valid\(\)" % r, "%s != null" % r, new)
            if new != line:
                n += 1
                lines[j] = new
            j += 1
        done[meth] = n
        i = j + 1
    missing = [m for m in METHODS if m not in done]
    if missing:
        sys.exit("not found: %s" % ", ".join(missing))
    for meth in METHODS:
        print("%-28s %d lines" % (meth, done[meth]))
    if apply:
        with open(PATH, "w", encoding="utf-8", newline="") as fh:
            fh.write("\n".join(lines))
        print("written")


if __name__ == "__main__":
    main()
