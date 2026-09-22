"""Find hand-written name searches over record arrays in compiler/src: a
`for` loop whose body compares `<arr>[i].name` (or `.name.id`) with a
spelling.  These are the shape the residue's rule-1b population cannot see
(it places METHOD calls, not loops).  Prints file:line, the loop header
and the compare line.

The review board's instrument (`docs/name-resolution-endgame.md` §4), kept
verbatim as the independent count rule 1c is cross-checked against in
§8.296; the one change is that the tree is an optional argument (default
`compiler/src` of the repository this file sits in)."""
import os, re, sys
root = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
    os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))),
    "compiler", "src")
pat_cmp = re.compile(r"\[\w+\]\.(name|variant_name|method_name|field_name|param_name)(\.id)?\s*(\.equals\(|==)")
pat_for = re.compile(r"^\s*for\s*\(")
hits = []
for dp, dn, fn in os.walk(root):
    for f in fn:
        if not f.endswith(".cryo"):
            continue
        p = os.path.join(dp, f)
        lines = open(p, encoding="utf-8", errors="replace").read().splitlines()
        for i, l in enumerate(lines):
            if pat_cmp.search(l) and not l.strip().startswith("//"):
                # find the nearest enclosing for within 6 lines above
                j = i
                hdr = None
                while j >= 0 and i - j <= 6:
                    if pat_for.match(lines[j]):
                        hdr = lines[j].strip()
                        break
                    j -= 1
                if hdr:
                    rel = os.path.relpath(p, root).replace("\\", "/")
                    hits.append((rel, i + 1, hdr[:70], l.strip()[:110]))
for h in hits:
    print("%s:%d\n    %s\n    %s" % h)
print("total", len(hits))
