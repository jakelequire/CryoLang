"""Which retyped functions have a body the check refuses.

  python attribute.py <list.tsv> <check-log> <repo-root> > <prologue-list.tsv>

Every refusal in the log that is NOT a call-site argument wanting `Text`
(those are the callers' to fix) is located in the listed function whose
body contains it; each such function's listed parameters are written out,
in the list's format, for `retype.py --prologue`.  A refusal no listed
function contains is reported on stderr.
"""
import re, sys
listfile, log, root = sys.argv[1:4]
rows = [l.rstrip("\n").split("\t") for l in open(listfile, encoding="utf-8") if l.strip()]
cache = {}
spans = []  # (file, start, end, row)
for r in rows:
    f, ln = r[0].rsplit(":", 1)
    if f not in cache:
        cache[f] = open(root + "/" + f, encoding="utf-8", errors="replace").read().split("\n")
    L = cache[f]
    head = int(ln) - 1
    while not (re.match(r"\s*(?:public\s+|private\s+)?(?:static\s+|function\s+)?%s\s*(<[^()]*>)?\s*\("
                        % re.escape(r[1]), L[head]) and not L[head].rstrip().endswith(";")):
        head += 1
    depth, started, i = 0, False, head
    while i < len(L):
        for ch in L[i]:
            if ch == "{":
                depth += 1; started = True
            elif ch == "}":
                depth -= 1
        if started and depth <= 0:
            break
        i += 1
    spans.append((f.lower(), head + 1, i + 1, r))
lines = open(log, encoding="utf-8", errors="replace").read().splitlines()
marked, orphan = {}, []
for i, l in enumerate(lines):
    if not l.startswith("error["):
        continue
    loc = lines[i + 1].strip()[3:].strip()
    note = next((lines[k] for k in range(i, min(i + 14, len(lines)))
                 if re.match(r"^\s*\|\s*\^~*\s+expected `", lines[k])), "")
    if l.startswith("error[E0214]") and "expected `utils::text::Text`, found `string`" in note:
        continue
    f, ln, col = loc.rsplit(":", 2)
    full = ("compiler/" + f).lower() if not f.lower().startswith("compiler/") else f.lower()
    hit = [s for s in spans if s[0] == full and s[1] <= int(ln) <= s[2]]
    if not hit:
        orphan.append(loc + "  " + l)
        continue
    for s in hit:
        marked[(s[0], s[1], s[3][2])] = s[3]
for r in marked.values():
    print("\t".join(r))
for o in orphan:
    sys.stderr.write("not in a listed body: " + o + "\n")
sys.stderr.write("%d parameter(s) to unwrap at entry\n" % len(marked))
