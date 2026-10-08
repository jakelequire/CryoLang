"""Classify every pending `lookup_by_spelling` allow in compiler/src by what
the function's spelling parameters reach, read from .facts/compiler.facts.

Usage: python .objcmp/s123/allows.py [--tsv OUT]

Per function: its spelling parameters (SymbolStr / QualifiedName, by value,
behind a reference, or as an array element) and, for each, the records in
the function whose provenance starts at that parameter:
  door       an argument of a door
  map-read   a map/set read's key       (a lookup)
  map-write  a map/set write's key      (a registration)
  compare    a comparison / equals      (a scan when in a loop over elements)
  match      a match over it            (a lookup by literal table)
  store      a struct literal's field   (carried)
  pass:F     an argument of another pending function F
  call:F     an argument of any other function F
Class, transitively over pass: edges:
  none       no spelling parameter at all: the allow is dead
  lookup     reaches map-read, compare, match, or a door (through any pending callee)
  register   reaches map-write, no lookup
  display    reaches only display/format/intern-table text and diagnostics
  carry      everything else: stored, handed to non-pending functions
"""
import collections, os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
FACTS = os.path.join(ROOT, ".facts", "compiler.facts")
SRC = sys.argv[sys.argv.index("--src") + 1] if "--src" in sys.argv else os.path.join(ROOT, "compiler", "src")
MAP_READ = (".get", ".get_ref", ".get_mut", ".contains_key", ".contains")
MAP_WRITE = (".insert", ".remove")
EQ = re.compile(r"(SymbolStr\.equals|QualifiedName\.equals|ModulePath\.equals|string\.eq|for string>\.equals|SymbolStr\.eq)\(")
DISPLAY = re.compile(r"(InternTable\.(resolve|shown)|resolve_str|fmt::format|Diagnostic|Text::new|\.label\(|\.note\(|\.help\(|emit_|report|warn|error|format_|display|mangle|Mangled|symbol_name|to_string)")
SPELL_T = ("SymbolStr", "QualifiedName")
DECL = re.compile(r"^\s*(?:public\s+|private\s+)?(?:static\s+)?(?:function\s+)?([A-Za-z_][A-Za-z0-9_]*)\s*(?:<[^>]*>)?\s*\(")


def door_symbols(recs):
    rows = []
    in_table = False
    for l in open(os.path.join(ROOT, "docs", "resolution-rules.md"), encoding="utf-8"):
        if l.startswith("<!-- doors -->"):
            in_table = True
        elif l.startswith("<!-- /doors -->"):
            break
        elif in_table and l.startswith("| `"):
            c = [x.strip().strip("`") for x in l.split("|")]
            parts = c[2].split("::")
            rows.append((parts[-1], parts[-2] if len(parts) > 1 else "-", c[3]))
    out = {}
    for r in recs:
        if r[0] != "fn":
            continue
        for leaf, owner, rel in rows:
            if r[13] == leaf and ("compiler/" + r[1]).lower() == rel.lower() and \
                    r[12].split("::")[-1] == owner.split("::")[-1]:
                out[r[8]] = c_leaf(r)
    return out


def c_leaf(r):
    return r[12].split("::")[-1] + "." + r[13] if r[12] != "-" else r[13]


def main():
    recs = [l.rstrip("\n").split("\t") for l in open(FACTS, encoding="utf-8")]
    fns = {}
    for r in recs:
        if r[0] == "fn" and r[1].startswith("src/"):
            fns.setdefault((r[1].lower(), int(r[2])), r)
    params = collections.defaultdict(list)
    for r in recs:
        if r[0] == "param":
            params[r[8]].append(r)
    uses = collections.defaultdict(list)
    for r in recs:
        if r[0] in ("arg", "sarg", "cmp", "match") and r[1].startswith("src/"):
            uses[r[11]].append(r)
    doors = door_symbols(recs)

    # every pending allow -> function record
    pending = []
    for dp, _, fs in os.walk(SRC):
        for f in fs:
            if not f.endswith(".cryo"):
                continue
            p = os.path.join(dp, f)
            rel = "src/" + os.path.relpath(p, SRC).replace("\\", "/")
            lines = open(p, encoding="utf-8", errors="replace").read().split("\n")
            for i, l in enumerate(lines):
                if "allow(lookup_by_spelling" in l and "pending" in l:
                    name, rec, dline = "?", None, None
                    for j in range(i + 1, min(i + 8, len(lines))):
                        m = DECL.match(lines[j])
                        if m:
                            name, dline = m.group(1), j + 1
                            break
                    for j in range(i + 1, min(i + 8, len(lines))):
                        if (rel.lower(), j + 1) in fns:
                            rec = fns[(rel.lower(), j + 1)]
                            break
                    pending.append((rel, i + 1, name, rec))
    psyms = {p[3][8]: p for p in pending if p[3] is not None}

    def param_uses(sym):
        out = []
        sp = [p for p in params.get(sym, []) if any(t in p[5] for t in SPELL_T)]
        for p in sp:
            pname = p[13].split(":", 1)[1]
            pat = re.compile(r"param:(?:mut )?" + re.escape(pname) + r"(?![A-Za-z0-9_])")
            kinds = []
            for u in uses.get(sym, []):
                prov = u[6] + " " + (u[7] if u[0] == "cmp" else "")
                if not pat.search(prov):
                    continue
                if u[0] == "match":
                    kinds.append("match")
                elif u[0] == "cmp":
                    kinds.append("compare")
                else:
                    op = u[12].split("(")[0]
                    if u[8] == "?" and op in MAP_READ:
                        kinds.append("map-read")
                    elif u[8] == "?" and op in MAP_WRITE:
                        kinds.append("map-write")
                    elif u[8] == "?" and u[9] == "none":
                        kinds.append("store:" + u[12].split("::")[-1])
                    elif EQ.search(u[12]):
                        kinds.append("compare")
                    elif u[8] in doors:
                        kinds.append("door:" + doors[u[8]])
                    elif u[8] in psyms:
                        kinds.append("pass:" + u[8])
                    else:
                        kinds.append("call:" + u[12].split("(")[0])
            out.append((pname, p[5].split("::")[-1], sorted(set(kinds))))
        return out

    callers = collections.defaultdict(list)
    for r in recs:
        if r[0] in ("arg", "sarg") and r[4].isdigit():
            callers[(r[8], int(r[4]))].append(r)

    def origin_of(prov):
        if re.search(r"DeclName\.spelling|DefTable\.(path_of|leaf_of)|InternTable\.shown|family_owner_path|ModulePath\.as_sym", prov):
            return "identity"
        if prov.startswith("param:"):
            return "param"
        if prov.startswith("field:compiler::ast") or "field:compiler::ast" in prov.split(" of ")[-1]:
            return "ast"
        if prov.startswith("literal") or "intern(" in prov and "literal:" in prov:
            return "literal"
        if prov.startswith("field:"):
            return "field"
        if prov.startswith("element"):
            return "element"
        return prov.split(":", 1)[0]

    def caller_origins(sym):
        sp = [p for p in params.get(sym, []) if any(t in p[5] for t in SPELL_T)]
        out = collections.Counter()
        for p in sp:
            for c in callers.get((sym, int(p[4])), []):
                out[origin_of(c[6])] += 1
        return out

    info = {}
    for rel, line, name, rec in pending:
        if rec is None:
            info[(rel, line)] = (name, None, "unmatched", [])
            continue
        info[(rel, line)] = (name, rec[8], None, param_uses(rec[8]))

    by_sym = {v[1]: v for v in info.values() if v[1]}
    memo = {}

    def klass(sym, stack=()):
        if sym in memo:
            return memo[sym]
        if sym in stack:
            return set()
        v = by_sym[sym]
        if not v[3]:
            memo[sym] = {"none"}
            return memo[sym]
        s = set()
        for _, _, ks in v[3]:
            for k in ks:
                if k in ("map-read", "compare", "match") or k.startswith("door:"):
                    s.add("lookup")
                elif k == "map-write":
                    s.add("register")
                elif k.startswith("pass:"):
                    sub = klass(k[5:], stack + (sym,)) - {"none"}
                    s |= sub if sub else {"carry"}
                elif k.startswith("call:") and DISPLAY.search(k):
                    s.add("display")
                else:
                    s.add("carry")
        if not s:
            s = {"carry"}
        memo[sym] = s
        return s

    def direct(pu):
        return any(k in ("map-read", "compare", "match") or k.startswith("door:") for _, _, ks in pu for k in ks)

    rows = []
    counts = collections.Counter()
    for (rel, line), (name, sym, err, pu) in sorted(info.items()):
        if err:
            c = err
        elif sym in doors:
            c = "door"
        else:
            ks = klass(sym)
            if ks == {"none"}:
                c = "none"
            elif "lookup" in ks:
                c = "lookup" if direct(pu) else "lookup-via-callee"
            elif "register" in ks:
                c = "register"
            elif ks == {"display"}:
                c = "display"
            elif rel.startswith("src/compiler/AST/") and all(not k for _, _, k in pu):
                c = "ast-carrier"
            else:
                c = "carry"
        counts[c] += 1
        detail = "; ".join("%s:%s=[%s]" % (pn, pt, ",".join(k.split("::")[-1] if not k.startswith("pass:") else "pass:" + by_sym[k[5:]][0] for k in ks)) for pn, pt, ks in pu)
        co = caller_origins(sym) if sym else {}
        rows.append((c, rel, line, name, detail, ",".join("%s=%d" % kv for kv in sorted(co.items()))))
    for c, n in counts.most_common():
        print("%5d  %s" % (n, c))
    print("%5d  TOTAL" % sum(counts.values()))
    if "--tsv" in sys.argv:
        with open(sys.argv[sys.argv.index("--tsv") + 1], "w", encoding="utf-8") as fh:
            for r in sorted(rows):
                fh.write("\t".join(str(x) for x in r) + "\n")
    if "--md" in sys.argv:
        order = ["door", "lookup", "lookup-via-callee", "register", "display", "ast-carrier", "carry", "none", "unmatched"]
        with open(sys.argv[sys.argv.index("--md") + 1], "w", encoding="utf-8") as fh:
            fh.write("# Pending `lookup_by_spelling` allows in compiler/src\n\n")
            fh.write("Generated by `.objcmp/s123/allows.py --md` from `.facts/compiler.facts`; do not edit.\n\n")
            fh.write(__doc__.split("\n\n", 1)[1].replace("\n", "\n    ").join(["    ", "\n"]) + "\n")
            fh.write("Extra classes: `door` is a function the door table lists; `lookup` looks up in its own\n"
                     "body, `lookup-via-callee` only through a pending callee; `ast-carrier` is an AST\n"
                     "constructor or setter whose parameter is stored into the node (nothing recorded).\n"
                     "`callers` counts, per spelling parameter, where each caller's argument came from:\n"
                     "`identity` = text read off a definition (`path_of`, `leaf_of`, `DeclName`),\n"
                     "`ast` = a syntax node's written name, `param`/`local`/`field`/`element`/`call`/`literal`\n"
                     "as the facts record it.\n\n")
            fh.write("| class | functions |\n|---|---|\n")
            for c in order:
                if counts.get(c):
                    fh.write("| %s | %d |\n" % (c, counts[c]))
            fh.write("| total | %d |\n\n" % sum(counts.values()))
            for c in order:
                sel = [r for r in rows if r[0] == c]
                if not sel:
                    continue
                fh.write("## %s (%d)\n\n| site | function | parameter uses | callers |\n|---|---|---|---|\n" % (c, len(sel)))
                for r in sorted(sel, key=lambda r: (r[1], r[2])):
                    fh.write("| `%s:%d` | `%s` | %s | %s |\n" % (r[1], r[2], r[3], r[4].replace("|", "/") or "-", r[5] or "-"))
                fh.write("\n")


main()
