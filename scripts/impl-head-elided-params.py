#!/usr/bin/env python3
"""List every impl head that names a generic template without writing its
type parameters - the spelling the impl-head ruling refuses.

A bare `implement trait Display for String`, with `String<A = GlobalAlloc>`,
has two readings - the template, generic over `A`, or the default
instantiation `String<GlobalAlloc>` - and the tree gave it both, chosen by
the module the impl was written in.  The ruling is Rust's: the writer says
which, `implement<A> trait Display for String<A>` or `implement trait
Display for String<GlobalAlloc>`, and the bare form is an error.

Two shapes are counted apart, because the ruling names the first:

* BARE: no `<...>` at all on a target that is a generic template
  (`implement trait Display for String`, `implement String`).
* PARTIAL: some arguments written, the trailing defaulted ones elided
  (`implement<T> trait Display for Array<T>` with `Array<T, A = GlobalAlloc>`).
  Rust fills a default here; whether Cryo does is not decided by the ruling.

Matching is by LEAF, narrowed by locality: a head's own file decides
first (a non-generic `Counter` declared beside the head is the target,
not another test's `Counter<T>`), then the head's directory, then the
head's top-level tree (`stdlib/`, whose modules import each other; a
`tests/` file is its own module and stops at its directory).  A head
whose leaf matches a template only elsewhere is listed as UNMATCHED, so
an elision the narrowing hid is visible rather than silently dropped.

    python scripts/impl-head-elided-params.py            # listing + counts
    python scripts/impl-head-elided-params.py --count    # one token: bare=N,partial=M,unmatched=K
"""
import os, re, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

NONGEN = re.compile(
    r"^\s*(?:public\s+|private\s+)?type\s+(?:struct|class|enum|union)\s+([A-Za-z_]\w*)\s*[{:;]",
    re.M)
DECL = re.compile(
    r"^\s*(?:public\s+|private\s+)?type\s+(?:struct|class|enum|union)\s+([A-Za-z_]\w*)\s*<([^>]*)>",
    re.M)
HEAD = re.compile(
    r"^\s*implement\s*(?:<[^>]*>)?\s*(?:trait\s+[A-Za-z_][\w:]*(?:<[^>]*>)?\s+for\s+)?"
    r"(?:struct\s+|class\s+|enum\s+|union\s+)?([A-Za-z_][\w:]*)(<[^>{]*>)?\s*(?:where\b|\{)",
    re.M)


def files():
    # A listing that FAILED is not an empty tree: read as one, every count
    # below is 0 and `bare=0,partial=0` is the value that means "done".
    r = subprocess.run(["git", "ls-files", "*.cryo"], cwd=ROOT,
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if r.returncode != 0:
        sys.stderr.write("impl-head-elided-params: git ls-files failed (exit %d): %s\n"
                         % (r.returncode, r.stderr.decode(errors="replace").strip()))
        sys.exit(2)
    out = r.stdout.decode().split()
    if not out:
        sys.stderr.write("impl-head-elided-params: git ls-files listed no .cryo file\n")
        sys.exit(2)
    return [f for f in out if not f.startswith("legacy/")]


def main():
    templates = {}   # leaf -> [(param count, defaulted count, file)]
    nongen = set()   # (leaf, file) of a non-generic declaration
    heads = []
    for f in files():
        src = open(os.path.join(ROOT, f), encoding="utf-8", errors="replace").read()
        for m in NONGEN.finditer(src):
            nongen.add((m.group(1), f))
        for m in DECL.finditer(src):
            params = [p.strip() for p in m.group(2).split(",") if p.strip()]
            templates.setdefault(m.group(1), []).append(
                (len(params), sum("=" in p for p in params), f))
        for m in HEAD.finditer(src):
            leaf = m.group(1).split("::")[-1]
            args = m.group(2)
            line = src.count("\n", 0, m.start()) + 1
            heads.append((leaf, args, f, line))

    def pick(leaf, f):
        cands = templates.get(leaf, [])
        same_file = [c for c in cands if c[2] == f]
        if same_file:
            return same_file[0], None
        if (leaf, f) in nongen:
            return None, None
        d = os.path.dirname(f)
        same_dir = [c for c in cands if os.path.dirname(c[2]) == d]
        if same_dir:
            return same_dir[0], None
        top = f.split("/")[0]
        if top != "tests":
            same_tree = [c for c in cands if c[2].split("/")[0] == top]
            if same_tree:
                return same_tree[0], None
        return None, cands

    bare, partial, unmatched = [], [], []
    for leaf, args, f, line in heads:
        t, elsewhere = pick(leaf, f)
        if t is None:
            if elsewhere and args is None:
                unmatched.append((leaf, f, line, elsewhere[0][2]))
            continue
        n, ndef, tf = t
        if args is None:
            bare.append((leaf, f, line, tf))
        else:
            written = [a for a in args[1:-1].split(",") if a.strip()]
            if len(written) < n:
                partial.append((leaf, f, line, tf, len(written), n))

    if "--count" in sys.argv:
        print("bare=%d,partial=%d,unmatched=%d" % (len(bare), len(partial), len(unmatched)))
        return 0
    print("templates with a defaulted parameter: %d"
          % sum(1 for v in templates.values() for c in v if c[1]))
    print("BARE heads (no arguments on a generic template): %d" % len(bare))
    for leaf, f, line, tf in sorted(bare, key=lambda x: (x[1], x[2])):
        print("  %s:%d  %s  (template in %s)" % (f, line, leaf, tf))
    print("PARTIAL heads (defaulted trailing arguments elided): %d" % len(partial))
    for leaf, f, line, tf, w, n in sorted(partial, key=lambda x: (x[1], x[2])):
        print("  %s:%d  %s  %d of %d written  (template in %s)" % (f, line, leaf, w, n, tf))
    print("UNMATCHED bare heads (a same-leaf template exists only elsewhere): %d" % len(unmatched))
    for leaf, f, line, tf in sorted(unmatched, key=lambda x: (x[1], x[2])):
        print("  %s:%d  %s  (template in %s)" % (f, line, leaf, tf))
    return 0


if __name__ == "__main__":
    sys.exit(main())
