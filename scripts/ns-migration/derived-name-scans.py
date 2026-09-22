#!/usr/bin/env python3
"""Loops that compare a name DERIVED into a `string`, which rule 1c cannot see.

`scripts/lane-gate.py`'s rule 1c finds a name-keyed read written as a loop by
looking for the ELEMENT'S OWN interned field compared with `.equals(`, `==` or
`!=`.  A loop that resolves the name to a `string` first and compares THAT is
the same read and matches nothing:

    for (mut i: i64 = 0; i < this.arena.types.length; i++) {
        const leaf: string = QualifiedName::leaf_of(this.intern.resolve(qname));
        if (leaf != needle) { continue; }        // a bare-leaf search
    }

This reports every such loop in `compiler/src`.  It is a MEASUREMENT, not a
gate: what the residue counts is pinned by §0 and is Jake's to move.

    python3 scripts/ns-migration/derived-name-scans.py            # report
    python3 scripts/ns-migration/derived-name-scans.py --selftest # prove it can see and can refuse

The self-test is the control.  A scan of this kind is rare enough that a report
of one hit reads exactly like a parser that matched nothing, and `grep -P`
silently reporting nothing on Git Bash is how this tree has been fooled before.
"""
import os
import re
import sys

DERIVE = re.compile(r"const\s+(\w+)\s*:\s*string\s*=\s*.*?(intern\w*\.resolve\(|"
                    r"\.resolve\(|leaf_of\(|bare_name_of\()")
CMP = re.compile(r"\b(\w+)\s*(==|!=)\s*(\w+)\b")
FOR = re.compile(r"\bfor\s*\(")


def scan_lines(lines):
    """(line number, text) for each comparison on a name derived inside a loop."""
    out = []
    depth = 0
    loops = []          # [brace depth at entry, {names derived inside}]
    for idx, line in enumerate(lines, 1):
        if FOR.search(line):
            loops.append([depth, set()])
        m = DERIVE.search(line)
        if m and loops:
            loops[-1][1].add(m.group(1))
        if loops:
            for c in CMP.finditer(line):
                if c.group(1) in loops[-1][1] or c.group(3) in loops[-1][1]:
                    out.append((idx, line.strip()))
                    break
        depth += line.count("{") - line.count("}")
        while loops and depth <= loops[-1][0]:
            loops.pop()
    return out


def scan_tree(root):
    hits = []
    src = os.path.join(root, "compiler", "src")
    for dirpath, _dirs, files in os.walk(src):
        for fn in sorted(files):
            if not fn.endswith(".cryo"):
                continue
            path = os.path.join(dirpath, fn)
            with open(path, encoding="utf-8", errors="replace") as fh:
                lines = fh.readlines()
            rel = os.path.relpath(path, root).replace("\\", "/")
            for idx, text in scan_lines(lines):
                hits.append((rel, idx, text))
    return hits


SEES = """
function shadowed(&this, needle: string) -> void {
    for (mut i: i64 = 0; i < this.arena.types.length; i++) {
        const leaf: string = QualifiedName::leaf_of(this.intern.resolve(qn));
        if (leaf != needle) { continue; }
    }
}
function by_scope(&this, want: string) -> void {
    for (mut i: i64 = 0; i < this.scope.symbols.length; i++) {
        const nm: string = this.intern_table.resolve(s.name);
        if (nm == want) { return; }
    }
}
""".splitlines(keepends=True)

REFUSES = """
function outside_any_loop(&this, needle: string) -> void {
    const leaf: string = this.intern.resolve(x.name);
    if (leaf != needle) { return; }
}
function compares_the_interned_field(&this) -> void {
    for (mut i: i64 = 0; i < this.a.length; i++) {
        if (this.a[i].name.equals(other)) { return; }
    }
}
function derived_after_the_loop_closed(&this, needle: string) -> void {
    for (mut i: i64 = 0; i < this.a.length; i++) { }
    const leaf: string = this.intern.resolve(x.name);
    if (leaf != needle) { return; }
}
""".splitlines(keepends=True)


def selftest():
    seen = scan_lines(SEES)
    missed = scan_lines(REFUSES)
    ok = True
    if len(seen) != 2:
        print("selftest: FAIL -- expected 2 hits in the shapes it must SEE, got %d"
              % len(seen))
        ok = False
    if missed:
        print("selftest: FAIL -- %d hit(s) in the shapes it must REFUSE: %s"
              % (len(missed), missed))
        ok = False
    if ok:
        print("selftest: OK -- 2 derived-name scans seen, 3 near-misses refused")
    return 0 if ok else 1


def main():
    if "--selftest" in sys.argv:
        return selftest()
    root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    hits = scan_tree(root)
    for rel, idx, text in hits:
        print("%s:%d\t%s" % (rel, idx, text))
    print("derived-name scans: %d" % len(hits))
    return 0


if __name__ == "__main__":
    sys.exit(main())
