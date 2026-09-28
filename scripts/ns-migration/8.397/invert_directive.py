"""Proof by inversion for the directive name taking `Keyword`.

    python scripts/ns-migration/8.397/invert_directive.py <compiler exe> <out dir>

Each mutation is applied ALONE to a pristine copy of one file, the whole
compiler is type-checked (`cryo check src/main.cryo` in compiler/), the file
is restored, and the verdict line plus the first error are printed.  The
last run is the unmutated control.  Refuses to start if a target file has
uncommitted edits it would clobber on restore (it restores from a copy made
at start, so this is a guard against running over a half-edited tree).
"""
import os, subprocess, sys, shutil
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
CC, OUT = sys.argv[1], sys.argv[2]
MUTS = [
    ("A: a bare string written into the directive set's store",
     "compiler/src/compiler/passes/directive_processing.cryo",
     "                    kind: d.directive_kind,\n                    node: d,",
     "                    kind: d.directive_kind.as_string(),\n                    node: d,"),
    ("B: a spelling handed to a directive lookup",
     "compiler/src/compiler/passes/dead_code.cryo",
     'func.has_directive(Keyword::new("entry"))',
     'func.has_directive("entry")'),
    ("C: a spelling handed to the DirectiveNode constructor (expected to PASS: `new` arguments are unchecked)",
     "compiler/src/compiler/parser/parser.cryo",
     "new DirectiveNode(Keyword::new(kind_tok.lexeme),",
     "new DirectiveNode(kind_tok.lexeme,"),
    ("control: the tree unmutated", None, None, None),
]
env = dict(os.environ, CRYO_STDLIB=ROOT + "/stdlib", CRYO_CC="gcc")
os.makedirs(OUT, exist_ok=True)
for i, (label, rel, old, new) in enumerate(MUTS):
    keep = None
    if rel:
        p = os.path.join(ROOT, rel)
        keep = open(p, "rb").read()
        s = keep.decode("utf-8")
        assert s.count(old) >= 1, (label, s.count(old))
        open(p, "wb").write(s.replace(old, new, 1).encode("utf-8"))
    try:
        log = os.path.join(OUT, "invert-%d.out" % i)
        with open(log, "wb") as f:
            subprocess.run([CC, "check", "src/main.cryo"], cwd=os.path.join(ROOT, "compiler"),
                           stdout=f, stderr=subprocess.STDOUT, env=env)
    finally:
        if rel: open(p, "wb").write(keep)
    lines = open(log, encoding="utf-8", errors="replace").read().split("\n")
    verdict = [l for l in lines if l.startswith("Check failed") or l.startswith("No errors found")]
    at = next((k for k, l in enumerate(lines) if l.startswith("error[")), None)
    first = [lines[at].strip()] if at is not None else []
    where = [l.strip() for l in lines[at + 1:at + 3] if l.strip().startswith("-->")][:1] if at is not None else []
    note = [l.strip() for l in lines[at:at + 12] if "expected `" in l][:1] if at is not None else []
    print(label)
    print("   ", verdict[-1] if verdict else "(no verdict line)", "|", *(first + where + note))
