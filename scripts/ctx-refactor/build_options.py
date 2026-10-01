"""Point every read of a build option on the compilation context at
`ctx.options`.

The options moved off `CompilationContext` into one `BuildOptions` value that
is complete before the context is made.  This rewrites each read through a
context receiver (`ctx`, `ctx_ptr`, `cg_ctx`, `this.ctx`, and `c` in a file
that declares `c: CompilationContext*`) and refuses to rewrite a write: the
driver's writes are removed by hand, so any write left is a site to look at.

    python scripts/ctx-refactor/build_options.py [--check]
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DIRS = ["compiler/src", "tools/CryoLSP/src"]
SKIP = {"compiler/src/compiler/compilation_context.cryo"}

FIELDS = {
    "mode":                 "options.mode",
    "debug_mode":           "options.verbose",
    "opt_level":            "options.opt_level",
    "emit_debug_info":      "options.debug_info",
    "project_no_std":       "options.no_std",
    "project_no_runtime":   "options.freestanding()",
    "project_panic_unwind": "options.unwinds()",
    "native_alloc":         "options.native_heap()",
    "stdlib_root":          "options.stdlib_root",
    "target_triple":        "options.target_triple",
    "release_static":       "options.static_link",
    "emit_facts":           "options.emit_facts",
}


def receivers(text):
    names = ["ctx", "ctx_ptr", "cg_ctx"]
    if re.search(r"\bc\s*:\s*CompilationContext\*", text):
        names.append("c")
    return names


def main():
    check = "--check" in sys.argv
    edits = 0
    writes = []
    for d in DIRS:
        for dp, _, fns in os.walk(os.path.join(ROOT, d)):
            if os.sep + "build" in dp:
                continue
            for fn in fns:
                if not fn.endswith(".cryo"):
                    continue
                path = os.path.join(dp, fn)
                rel = os.path.relpath(path, ROOT).replace(os.sep, "/")
                if rel in SKIP:
                    continue
                raw = open(path, "rb").read()
                text = raw.decode("utf-8")
                pat = re.compile(r"\b(%s)\.(%s)\b(?!\s*\()" % (
                    "|".join(receivers(text)), "|".join(FIELDS)))
                out = []
                n = 0
                for ln, line in enumerate(text.split("\n"), 1):
                    def rep(m):
                        nonlocal n
                        rest = line[m.end():]
                        if re.match(r"\s*(=(?!=)|\+=|-=)", rest):
                            writes.append("%s:%d: %s" % (rel, ln, line.strip()))
                            return m.group(0)
                        n += 1
                        return "%s.%s" % (m.group(1), FIELDS[m.group(2)])
                    out.append(pat.sub(rep, line))
                if n:
                    edits += n
                    print("%s: %d" % (rel, n))
                    if not check:
                        open(path, "wb").write("\n".join(out).encode("utf-8"))
    print("rewritten: %d" % edits)
    for w in writes:
        print("WRITE (not rewritten): " + w)
    return 1 if writes else 0


if __name__ == "__main__":
    sys.exit(main())
