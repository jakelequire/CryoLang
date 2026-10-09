"""Dead-code finder over the compiler's own `--emit=facts` call records.

Reads .facts/compiler.facts and .facts/lsp.facts.  A function is a node
(keyed by its linker symbol); a `call` record is an edge enclosing -> callee.
Roots:
  * every function declared outside compiler/src (stdlib, LSP sources),
  * `main`,
  * destructors, constructors of classes reached by `new`? (no: constructors
    are called like any other function and get call records),
  * methods inside an `implement trait ... for` block (trait dispatch),
  * a class method whose name an ancestor class also declares (virtual
    dispatch through the base),
  * an enclosing symbol with no `fn` record (a lambda or synthesized body):
    its callees are reported separately as 'via-unknown'.
Output: TSV of unreachable compiler/src functions with file, line, owner,
name, role, plus a textual-reference count of the leaf name across
compiler/src, tools/CryoLSP/src, stdlib and tests.

usage: python dead.py <out.tsv>
"""
import os
import re
import sys
from collections import defaultdict

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))


def norm(path):
    p = path.replace("\\", "/")
    p = re.sub(r"^(\.\./)+compiler/", "", p)
    if p.startswith("src/") and not os.path.exists(os.path.join(ROOT, "compiler", p)):
        pass
    return p.lower()


def is_compiler_file(p):
    return p.startswith("src/") and os.path.exists(os.path.join(ROOT, "compiler", p))


fns = {}            # symbol -> dict
edges = defaultdict(set)
types = {}          # path -> (kind, base)
unknown_encl = set()

for name in ("compiler.facts", "lsp.facts"):
    lsp = name == "lsp.facts"
    with open(os.path.join(ROOT, ".facts", name), encoding="utf-8", errors="replace") as fh:
        for line in fh:
            c = line.rstrip("\n").split("\t")
            if len(c) < 15:
                continue
            kind = c[0]
            if kind == "fn":
                f = c[1].replace("\\", "/")
                comp = False
                rel = None
                if f.startswith("../../compiler/src/"):
                    rel = f[len("../../compiler/"):]
                    comp = True
                elif f.startswith("src/") and not lsp:
                    rel = f
                    comp = True
                sym = c[8]
                if sym not in fns:
                    fns[sym] = dict(file=rel if comp else f, line=int(c[2]), owner=c[12],
                                    name=c[13], role=c[7], comp=comp, lsp=lsp)
            elif kind == "type":
                types[c[12]] = (c[7], c[5])
            elif kind == "call":
                callee, encl = c[8], c[11]
                if callee not in ("-", "?"):
                    edges[encl].add(callee)

# Methods per owner, for virtual dispatch.
methods_of = defaultdict(set)
for s, d in fns.items():
    methods_of[d["owner"]].add(d["name"])


def ancestors(path):
    seen = []
    cur = types.get(path, (None, "-"))[1]
    while cur and cur != "-" and cur not in seen:
        seen.append(cur)
        cur = types.get(cur, (None, "-"))[1]
    return seen


# Trait-impl blocks by source scan: file -> list of (start_line, is_trait).
impl_cache = {}


def in_trait_impl(rel, line):
    if rel not in impl_cache:
        spans = []
        path = os.path.join(ROOT, "compiler", rel)
        try:
            with open(path, encoding="utf-8", errors="replace") as fh:
                for i, l in enumerate(fh, 1):
                    m = re.match(r"\s*implement\s+(trait\s+)?", l)
                    if m and l.lstrip().startswith("implement"):
                        spans.append((i, bool(m.group(1))))
                    elif re.match(r"\S", l) and not l.startswith("}") and not l.startswith("//") \
                            and not l.startswith("///") and not l.startswith("implement"):
                        spans.append((i, False))
        except OSError:
            pass
        impl_cache[rel] = spans
    best = False
    for (s, t) in impl_cache[rel]:
        if s <= line:
            best = t
        else:
            break
    return best


roots = set()
for s, d in fns.items():
    if not d["comp"]:
        roots.add(s)
        continue
    if d["name"] == "main" and d["role"] == "free":
        roots.add(s)
    if d["role"] == "destructor":
        roots.add(s)
    if d["role"] in ("method", "static") and in_trait_impl(d["file"], d["line"]):
        roots.add(s)
    if d["role"] == "method":
        for a in ancestors(d["owner"]):
            if d["name"] in methods_of.get(a, ()):
                roots.add(s)
                break
# Roots the call records cannot see: a function taken as a VALUE (a callback
# handed to C, a function pointer stored in a table) or called from a place
# the instrument records no call for.  One `owner<TAB>name` per line, each
# found by a build that refused its deletion; `# why` comments allowed.
keep_path = os.path.join(os.path.dirname(__file__), "keep.txt")
if os.path.exists(keep_path):
    with open(keep_path, encoding="utf-8") as fh:
        keep = set(l.split("#", 1)[0].strip() for l in fh if l.split("#", 1)[0].strip())
    for s, d in fns.items():
        leaf_owner = d["owner"].split("::")[-1]
        if d["comp"] and (leaf_owner + "::" + d["name"] in keep
                          or (d["role"] == "free" and d["name"] in keep)):
            roots.add(s)
for encl in list(edges):
    if encl not in fns:
        unknown_encl.add(encl)
        roots.add(encl)

live = set()
stack = list(roots)
while stack:
    s = stack.pop()
    if s in live:
        continue
    live.add(s)
    stack.extend(edges.get(s, ()))

# Text references of each leaf name.
corpus = []
for base in ("compiler/src", "tools/CryoLSP/src", "stdlib", "tests"):
    for dp, dn, fnames in os.walk(os.path.join(ROOT, base)):
        if ".bin" in dp or "build" in dp.split(os.sep):
            continue
        for fn in fnames:
            if fn.endswith(".cryo"):
                p = os.path.join(dp, fn)
                with open(p, encoding="utf-8", errors="replace") as fh:
                    corpus.append((os.path.relpath(p, ROOT).replace("\\", "/"), fh.read().split("\n")))

dead = [(s, d) for s, d in fns.items() if d["comp"] and s not in live]
dead.sort(key=lambda x: (x[1]["file"], x[1]["line"]))
names = set(d["name"] for _, d in dead)
refs = defaultdict(list)
pat = re.compile(r"\b(" + "|".join(re.escape(n) for n in names) + r")\b") if names else None
if pat:
    for path, lines in corpus:
        for i, l in enumerate(lines, 1):
            s = l.split("//", 1)[0]
            for m in pat.finditer(s):
                refs[m.group(1)].append((path, i))

with open(sys.argv[1], "w", encoding="utf-8") as out:
    out.write("file\tline\towner\tname\trole\ttextrefs\n")
    for s, d in dead:
        r = [x for x in refs[d["name"]] if not (x[0] == "compiler/" + d["file"] and x[1] == d["line"])]
        out.write("%s\t%d\t%s\t%s\t%s\t%d\n" % (d["file"], d["line"], d["owner"], d["name"], d["role"], len(r)))

# Candidates: unreachable, and not a constructor (`new T(...)` leaves no
# call record).  Each gets its source span; a candidate is CLEAN when every
# textual reference to its leaf name lies inside its own span or the span of
# another clean candidate (a fixpoint), and COLLIDING otherwise.
sys.path.insert(0, os.path.dirname(__file__))
from spans import span  # noqa: E402

src_lines = {}
for path, lines in corpus:
    src_lines[path] = lines
cands = []
for s, d in dead:
    if d["role"] == "constructor":
        continue
    path = "compiler/" + d["file"]
    real = next((p for p in src_lines if p.lower() == path.lower()), None)
    sp = span(src_lines[real], d["line"]) if real else None
    if sp is None:
        print("no span:", d["file"], d["line"], d["name"])
        continue
    cands.append(dict(d, path=real, first=sp[0], last=sp[1]))

clean = set(range(len(cands)))
while True:
    drop = set()
    for k in clean:
        c = cands[k]
        for (p, ln) in refs[c["name"]]:
            inside = any(cands[j]["path"] == p and cands[j]["first"] <= ln <= cands[j]["last"]
                         for j in clean)
            if not inside:
                drop.add(k)
                break
    if not drop:
        break
    clean -= drop

with open(sys.argv[1].replace(".tsv", ".cand.tsv"), "w", encoding="utf-8") as out:
    out.write("path\tfirst\tlast\tdecl\towner\tname\trole\tclass\n")
    for k, c in enumerate(cands):
        out.write("%s\t%d\t%d\t%d\t%s\t%s\t%s\t%s\n" % (c["path"], c["first"], c["last"], c["line"],
                  c["owner"], c["name"], c["role"], "clean" if k in clean else "colliding"))
print("candidates %d (constructors excluded), clean %d, colliding %d"
      % (len(cands), len(clean), len(cands) - len(clean)))

comp_total = sum(1 for d in fns.values() if d["comp"])
print("compiler fns %d, live %d, dead %d, unknown enclosing symbols %d"
      % (comp_total, sum(1 for s, d in fns.items() if d["comp"] and s in live), len(dead), len(unknown_encl)))
