#!/usr/bin/env python3
"""Where a spelling reaches a lookup outside a door (rule three of
`docs/resolution-rules.md`), and where text an identity handed back reaches a
lookup or a door at all (rule four), read from the compiler's `--emit=facts`
report and checked against a committed list of what is still outstanding.

The facts writer (`compiler/src/compiler/sema/call_facts.cryo`) records every
argument, comparison and `match` whose value is a spelling, with where the
value came from (its provenance) and the function it is in.  A provenance
that stops at `param:<name>` is chained here to every call that passes that
parameter - an argument record names the callee's linker symbol and the
position, and the parameter record the same pair - so a spelling is followed
across function boundaries back to where it started.

A LOOKUP is one of:

    map      a spelling as the key of a map or set read (`.get`, `.get_ref`,
             `.get_mut`, `.contains_key`, `.contains`)
    write    a spelling as the key of a map write (`.insert`, `.remove`) -
             rule two's question, a store keyed by text, traced the same way
    scan     a spelling compared, inside a loop, with a value read out of an
             array element (`==`/`!=`, `equals`, `eq`) - a hand-written search
    match    a `match` over a spelling - a table written as literal arms

Rule three: a lookup is allowed when every way its key arrives passes through
a door - the lookup is in a door, or the key reaches it only as a parameter
of functions whose callers are, in the end, in doors.  Anything else is a
VIOLATION, recorded at its ORIGIN: the record, in a function outside every
door, where the spelling started its trip (a literal, a field or array read,
a call's return, a composed expression, a global).

Rule four: a lookup, or a door's spelling argument, is a violation whenever a
way its key arrives starts at text an identity handed back (`IDENTITY_TEXT`),
doors or not.

Where the trace cannot see is listed in `scripts/facts-blind-spots.py`, and
this reads what the facts record, so it inherits every entry there: a key
held in an integer local or a struct field, the callee of a generic,
function-pointer or closure call (a parameter reached only through one has no
recorded caller, reported as `chain-lost`), and the parts of a composed key
(`expr:` origins; a formatted key's first argument only).

A local's provenance names its initializer only.  Every later assignment of a
spelling to a local or parameter by name has an `assign` record, and where
the trace passes a local or a parameter it follows each assignment to it in
the same function as well - joined by the function and the local's name, so
two locals of one name in one function share their assignments (the join
over-reports there, never under-reports).

The outstanding list, `scripts/spelling-flow.outstanding.tsv`, is every
violation as (rule, lookup kind, key class, lookup function, origin kind,
origin function, origin text) with a count - no line numbers, so an edit
elsewhere in a file moves nothing.  `--check` fails in both directions: a
violation the list does not have is new, and an entry the tree no longer has
is stale and comes off the list in the commit that fixed it.

Usage:
    python scripts/spelling-flow.py                      summary
    python scripts/spelling-flow.py --check              the tree against the list
    python scripts/spelling-flow.py --write              rewrite the list from the tree
    python scripts/spelling-flow.py --violations         one line per violation, with lines
    python scripts/spelling-flow.py --selftest --cryo C  the fixture, and each rule removed
"""
import argparse
import collections
import importlib.util
import os
import re
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LIST = os.path.join(ROOT, "scripts", "spelling-flow.outstanding.tsv")
FIXTURE = os.path.join(ROOT, "tests", "fixtures", "spelling-flow")
SCRATCH = os.path.join(ROOT, ".verify", "spelling-flow")
STDLIB = os.path.join(ROOT, "stdlib").replace("\\", "/")

MAP_READ = (".get", ".get_ref", ".get_mut", ".contains_key", ".contains")
MAP_WRITE = (".insert", ".remove")
EQ_CALL = re.compile(r"^(compiler::resolver::symbol_str::SymbolStr\.equals|"
                     r"compiler::resolver::qualified_name::QualifiedName\.equals|"
                     r"compiler::module_graph::ModulePath\.equals|string\.eq|"
                     r"<std::core::cmp::Eq for string>\.equals)\(")
NAME_TYPES = ("SymbolStr", "SymbolStr.id", "QualifiedName", "ModulePath")

# Calls whose return is the same text as their (first) argument in another
# representation: the trace continues through them.
PASS_THROUGH = (
    "compiler::resolver::intern_table::InternTable.resolve(",
    "compiler::resolver::intern_table::InternTable.intern(",
    "compiler::compilation_context::CompilationContext.intern(",
    "compiler::compilation_context::CompilationContext.resolve_str(",
    "utils::text::Text::new(", "utils::text::Text.as_string(",
    "utils::keyword::Keyword::new(", "utils::keyword::Keyword.as_string(",
    "compiler::resolver::qualified_name::QualifiedName::leaf_of(",
    "compiler::resolver::qualified_name::QualifiedName::parent_of(",
    "compiler::resolver::qualified_name::QualifiedName::head_of(",
    "string.trim(", "string.substr(",
)

# Fields that are the same spelling as the value they are read off: the
# number behind a `SymbolStr` is its interned text.
SAME_SPELLING_FIELDS = ("compiler::resolver::symbol_str::SymbolStr.id",)

# Text an identity hands back.  Each is a call on, or a field of, a record
# that stands for a declaration, a module or a type.
IDENTITY_TEXT = (
    ("call", "compiler::resolver::res::DefTable.path_of(", "a definition's path"),
    ("call", "compiler::resolver::res::DefTable.leaf_of(", "a definition's leaf"),
    ("call", "compiler::module_graph::ModulePath.as_sym(", "a module's path"),
    ("call", "compiler::decl_index::DeclarationIndex.family_owner_path(", "a family owner's path"),
    ("call", "compiler::sema::type_utils::TypeUtils.type_display_name(", "a type's display name"),
    ("call", "compiler::types::arena::TypeArena.format_display(", "a type's display name"),
    ("call", "compiler::types::arena::TypeArena.resolve_display_name(", "a type's display name"),
    ("call", "compiler::resolver::mangled_name::MangledName.as_string(", "a link symbol's text"),
    ("field", "compiler::resolver::symbol::Symbol.name", "a binding's recorded name"),
    ("field", "compiler::types::generic_registry::TemplateEntry.qualified_name", "a template's path"),
)

# The tracing rules, each switchable so the self-test can show the case
# written for it failing without it.
RULES = ("doors", "chain", "same-spelling-field", "pass-through", "identity-text",
         "reassign", "map", "write", "scan", "match")

LOCAL_NAME = re.compile(r"^(local|param):(?:mut )?([A-Za-z_0-9]+)")


def split_call(body):
    """`F(...) -> T of P` -> (F(...) -> T, P or None)."""
    depth = 0
    for i, c in enumerate(body):
        if c == "(":
            depth += 1
        elif c == ")":
            depth -= 1
        elif depth == 0 and body.startswith(" of ", i):
            return body[:i], body[i + 4:]
    return body, None


def step(prov, rules):
    """One provenance -> ('param', name) | ('next', inner) | ('origin', kind, text)."""
    if prov.startswith("param:"):
        return ("param", prov[6:])
    if prov.startswith("local:"):
        body = prov[6:]
        eq = body.find("=")
        if eq < 0:
            return ("origin", "local", prov)
        return ("next", body[eq + 1:])
    if prov.startswith("field:"):
        head, _, inner = prov[6:].partition("<-")
        if "same-spelling-field" in rules and head in SAME_SPELLING_FIELDS and inner:
            return ("next", inner)
        return ("origin", "field", head)
    if prov.startswith("element"):
        return ("origin", "element", prov.split(":", 2)[-1][:80])
    if prov.startswith("call:"):
        callee, inner = split_call(prov[5:])
        if "pass-through" in rules and inner is not None and callee.startswith(PASS_THROUGH):
            return ("next", inner)
        return ("origin", "call", callee.split("(")[0])
    for k in ("literal", "global", "sourceloc", "name", "expr"):
        if prov.startswith(k + ":"):
            return ("origin", k, prov[len(k) + 1:][:60])
    return ("origin", "other", prov[:60])


def identity_text(prov, rules):
    """The IDENTITY_TEXT entry a provenance chain passes, or None."""
    if "identity-text" not in rules:
        return None
    for kind, text, why in IDENTITY_TEXT:
        if kind == "call" and ("call:" + text) in prov:
            return text.rstrip("(").split("::")[-1]
        if kind == "field" and ("field:" + text + "<-") in prov:
            return text.split("::")[-1]
    return None


def fn_label(rec):
    return rec[13].split("(")[0]


class Facts:
    def __init__(self, path, door_fns, rules=RULES, only=None):
        self.rules = set(rules)
        self.only = only
        self.recs = []
        with open(path, encoding="utf-8") as fh:
            for line in fh:
                f = line.rstrip("\n").split("\t")
                if len(f) != 15:
                    raise SystemExit("spelling-flow: %s: a record with %d fields, not 15; the facts "
                                     "format has changed and this reader has not" % (path, len(f)))
                self.recs.append(f)
        self.param_index = {}
        fn_at = collections.defaultdict(list)
        for r in self.recs:
            if r[0] == "param":
                name = r[13].split(":", 1)[1] if ":" in r[13] else r[13]
                self.param_index[(r[8], name)] = int(r[4])
            elif r[0] == "fn":
                fn_at[(r[1].lower(), r[13])].append(r[8])
        self.callers = collections.defaultdict(list)
        self.assigns = collections.defaultdict(list)
        for r in self.recs:
            if r[0] in ("arg", "sarg") and r[4].isdigit():
                self.callers[(r[8], int(r[4]))].append(r)
            elif r[0] == "assign":
                m = LOCAL_NAME.match(r[7])
                if m:
                    self.assigns[(r[11], m.group(1), m.group(2))].append(r)
        self.doors = {}
        for leaf, relpath in door_fns:
            hits = fn_at.get((relpath.lower(), leaf), [])
            if len(hits) != 1:
                raise SystemExit("spelling-flow: FAIL -- door `%s` in %s matches %d function records"
                                 % (leaf, relpath, len(hits)))
            self.doors[hits[0]] = leaf

    def in_scope(self, rec):
        return rec[1].startswith(self.only) if self.only else True

    def origins(self, rec, prov, seen=frozenset(), ignore_doors=False):
        """(origin record, kind, text) for every way `prov`, the value of `rec`, arrives."""
        if not ignore_doors and "doors" in self.rules and rec[11] in self.doors:
            return [(rec, "door", self.doors[rec[11]])]
        out = []
        while True:
            assigned = self.assigned(rec, prov)
            if assigned:
                key, recs = assigned
                if key not in seen:
                    for a in recs:
                        out += self.origins(a, a[6], seen | {key}, ignore_doors)
            s = step(prov, self.rules)
            if s[0] == "next":
                prov = s[1]
                continue
            if s[0] == "origin":
                if s[1] == "local" and assigned:
                    return out      # declared with no initializer: its assignments are its values
                return out + [(rec, s[1], s[2])]
            name = s[1]
            if name == "this":
                return out + [(rec, "receiver", "this")]
            if "chain" not in self.rules:
                return out + [(rec, "param", name)]
            idx = self.param_index.get((rec[11], name))
            if idx is None:
                return out + [(rec, "chain-lost", "param %s has no record" % name)]
            key = (rec[11], idx)
            if key in seen:
                return out
            calls = self.callers.get(key, [])
            if not calls:
                return out + [(rec, "chain-lost", "no recorded caller passes %s" % name)]
            for c in calls:
                out += self.origins(c, c[6], seen | {key}, ignore_doors)
            return out

    def assigned(self, rec, prov):
        """(seen-key, the `assign` records) when `prov` is a local or parameter of `rec`'s
        function that something is assigned to after its initializer, else None."""
        if "reassign" not in self.rules:
            return None
        m = LOCAL_NAME.match(prov)
        if not m:
            return None
        key = (rec[11], m.group(1), m.group(2))
        recs = self.assigns.get(key)
        return (("assign",) + key, recs) if recs else None

    def lookups(self):
        """(kind, record, the provenance of the spelling) for every lookup in scope."""
        out = []
        for r in self.recs:
            if not self.in_scope(r):
                continue
            op = r[12].split("(")[0]
            if r[0] == "sarg" and r[8] == "?" and op in MAP_READ:
                kind, prov = "map", r[6]
            elif r[0] == "sarg" and r[8] == "?" and op in MAP_WRITE:
                kind, prov = "write", r[6]
            elif r[0] == "match":
                kind, prov = "match", r[6]
            elif r[0] == "cmp" and r[14] != "0" and ("element" in r[7] or "element" in r[6]):
                kind, prov = "scan", (r[6] if "element" in r[7] else r[7])
            elif r[0] == "arg" and r[14] != "0" and EQ_CALL.match(r[12]) \
                    and ("element" in r[7] or "element" in r[6]):
                kind, prov = "scan", (r[6] if "element" in r[7] else r[7])
            else:
                continue
            if kind in self.rules:
                out.append((kind, r, prov))
        return out

    def violations(self):
        """[(rule, lookup kind, lookup record, origin record, origin kind, origin text)]."""
        found = []
        for kind, r, prov in self.lookups():
            for o, okind, otext in self.origins(r, prov):
                if okind != "door":
                    found.append(("3", kind, r, o, okind, otext))
            if r[11] in self.doors:
                continue    # a door's own lookup: its arguments are asked below
            for o, okind, otext in self.origins(r, prov, ignore_doors=True):
                why = identity_text(o[6], self.rules)
                if why:
                    found.append(("4", kind, r, o, "identity", why))
        for r in self.recs:
            if r[0] in ("arg", "sarg") and self.in_scope(r) and r[8] in self.doors:
                for o, okind, otext in self.origins(r, r[6], ignore_doors=True):
                    why = identity_text(o[6], self.rules)
                    if why:
                        found.append(("4", "door:" + self.doors[r[8]], r, o, "identity", why))
        return found


def key_class(r):
    t = r[5].split("::")[-1].replace("&", "").replace("[]", "")
    return "name" if t in NAME_TYPES else "string"


def entry(v):
    rule, kind, r, o, okind, otext = v
    return (rule, kind, key_class(r), fn_label(r), okind, fn_label(o), otext.replace("\t", " "))


def load_module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def door_functions():
    mod = load_module("resolution_doors", os.path.join(ROOT, "scripts", "resolution-doors.py"))
    with open(os.path.join(ROOT, mod.DOC), encoding="utf-8") as fh:
        rows, problems = mod.table(fh.read())
    if problems:
        raise SystemExit("spelling-flow: FAIL -- " + "; ".join(problems))
    return [(leaf, path[len("compiler/"):]) for _d, leaf, path, _w in rows]


def read_list():
    got = collections.Counter()
    with open(LIST, encoding="utf-8") as fh:
        for n, line in enumerate(fh, 1):
            if line.startswith("#") or not line.strip():
                continue
            f = line.rstrip("\n").split("\t")
            if len(f) != 8 or not f[0].isdigit():
                raise SystemExit("spelling-flow: FAIL -- %s:%d: not `count TAB rule TAB kind TAB class "
                                 "TAB lookup TAB origin-kind TAB origin TAB text`" % (LIST, n))
            got[tuple(f[1:])] += int(f[0])
    return got


LIST_HEADER = """\
# Every place a spelling reaches a lookup outside a door (rule 3) or text an
# identity handed back reaches a lookup or a door (rule 4), as
# `scripts/spelling-flow.py` finds them in the compiler's facts.  Generated by
# `python scripts/spelling-flow.py --write`; `--check` refuses the tree when
# it has a violation this list does not (new) or this list has one the tree
# does not (stale - remove it in the commit that fixed it).
#
# count  rule  lookup-kind  key-class  lookup-function  origin-kind  origin-function  origin-text
"""


# (case, what the line must show, the rule the case is written for).
#   allowed    a lookup on the line, every way its key arrives through a door
#   violation  a lookup on the line with a rule-three violation
#   origin     a rule-three violation starts on the line
#   identity   a rule-four violation starts on the line
#   none       no lookup on the line
CASES = [
    ("door-interior", "allowed", "doors"),
    ("door-body-chain", "allowed", "chain"),
    ("door-body-number", "allowed", "same-spelling-field"),
    ("map-outside", "violation", "map"),
    ("map-resolved", "violation", "map"),
    ("scan-outside", "violation", "scan"),
    ("match-outside", "violation", "match"),
    ("write-outside", "violation", "write"),
    ("not-a-lookup", "none", None),
    ("origin-literal", "origin", "chain"),
    ("origin-through-resolve", "origin", "pass-through"),
    ("origin-reassigned", "origin", "reassign"),
    ("identity-into-door", "identity", "identity-text"),
    ("identity-into-lookup", "identity", "identity-text"),
]
FIXTURE_DOORS = [("door_find", "src/main.cryo"), ("door_text", "src/main.cryo")]
MARK = re.compile(r"//\s*flow:\s*([a-z-]+)\s*$")


def fixture_marks():
    found = {}
    with open(os.path.join(FIXTURE, "src", "main.cryo"), encoding="utf-8") as fh:
        for n, line in enumerate(fh, 1):
            m = MARK.search(line)
            if m:
                found[m.group(1)] = str(n)
    return found


def judge(facts, marks):
    """{case: True when the line shows what the case says}."""
    looks = collections.defaultdict(list)
    for kind, r, prov in facts.lookups():
        if r[1] == "src/main.cryo":
            looks[r[2]].append((kind, r, prov))
    viol = facts.violations()
    origin3 = {v[3][2] for v in viol if v[0] == "3" and v[3][1] == "src/main.cryo"}
    origin4 = {v[3][2] for v in viol if v[0] == "4" and v[3][1] == "src/main.cryo"}
    bad3 = {v[2][2] for v in viol if v[0] == "3" and v[2][1] == "src/main.cryo"}
    out = {}
    for case, want, _rule in CASES:
        line = marks.get(case)
        if want == "allowed":
            out[case] = bool(looks[line]) and line not in bad3
        elif want == "violation":
            out[case] = bool(looks[line]) and line in bad3
        elif want == "origin":
            out[case] = line in origin3
        elif want == "identity":
            out[case] = line in origin4
        else:
            out[case] = not looks[line]
    return out


def selftest(cryo):
    marks = fixture_marks()
    missing = [c for c, _w, _r in CASES if c not in marks] + [m for m in marks if m not in {c[0] for c in CASES}]
    if missing:
        print("spelling-flow: selftest FAIL -- cases and fixture marks disagree: %s" % ", ".join(missing))
        return 1
    shutil.rmtree(SCRATCH, ignore_errors=True)
    os.makedirs(SCRATCH)
    p = subprocess.run([cryo, "build", "--emit=facts", "--build-dir=" + SCRATCH.replace("\\", "/"),
                        "--stdlib=" + STDLIB], cwd=FIXTURE, env=dict(os.environ, CRYO_STDLIB=STDLIB),
                       capture_output=True, text=True, errors="replace")
    facts_path = os.path.join(SCRATCH, "spelling_flow.facts")
    if p.returncode != 0 or not os.path.isfile(facts_path):
        sys.stdout.write(p.stdout + p.stderr)
        print("spelling-flow: selftest FAIL -- the fixture did not build with --emit=facts (exit %d)"
              % p.returncode)
        return 1
    failed = 0
    full = judge(Facts(facts_path, FIXTURE_DOORS, RULES, "src/main.cryo"), marks)
    for case, want, _rule in CASES:
        ok = full[case]
        print("  %-4s %-24s %s" % ("ok" if ok else "FAIL", case, want))
        failed += 0 if ok else 1
    for rule in RULES:
        without = judge(Facts(facts_path, FIXTURE_DOORS, [x for x in RULES if x != rule], "src/main.cryo"),
                        marks)
        written = [c for c, _w, r in CASES if r == rule]
        caught = [c for c in written if not without[c]]
        ok = bool(caught)
        print("  %-4s without `%s`: %s" % ("ok" if ok else "FAIL", rule,
              ("fails " + ", ".join(caught)) if caught else
              ("no case is written for it" if not written else "every case still passes")))
        failed += 0 if ok else 1
    if failed:
        print("spelling-flow: selftest FAIL -- %d check(s)" % failed)
        return 1
    print("spelling-flow: selftest OK -- %d cases, each of %d rules removed fails its own"
          % (len(CASES), len(RULES)))
    return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--facts")
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--write", action="store_true")
    ap.add_argument("--violations", action="store_true")
    ap.add_argument("--selftest", action="store_true")
    ap.add_argument("--cryo")
    args = ap.parse_args()
    if args.selftest:
        if not args.cryo:
            ap.error("--selftest needs --cryo")
        return selftest(os.path.abspath(args.cryo))
    path = args.facts
    if not path:
        path = load_module("facts", os.path.join(ROOT, "scripts", "facts.py")).facts_path("compiler")
    facts = Facts(path, door_functions(), RULES, "src/")
    viol = facts.violations()
    tree = collections.Counter(entry(v) for v in viol)

    if args.violations:
        for v in sorted(viol, key=lambda v: (v[0], v[3][1], int(v[3][2]))):
            rule, kind, r, o, okind, otext = v
            print("rule%s\t%s:%s\t%s\t%s\t%s\t-> %s %s %s:%s\t%s" % (
                rule, o[1], o[2], fn_label(o), okind, otext[:60], kind, key_class(r), r[1], r[2],
                fn_label(r)))
    if args.write:
        with open(LIST, "w", encoding="utf-8", newline="\n") as fh:
            fh.write(LIST_HEADER)
            for k in sorted(tree):
                fh.write("%d\t%s\n" % (tree[k], "\t".join(k)))
    looks = facts.lookups()
    kinds = collections.Counter(k for k, _r, _p in looks)
    bad = {(v[1], v[2][1], v[2][2], v[2][3]) for v in viol if v[0] == "3"}
    per = collections.Counter(k[0] for k in bad)
    print("spelling-flow: %d lookups outside the doors of %d (%s); rule three: %d violation origins, "
          "rule four: %d; %d door functions" % (
              len(bad), len(looks), ", ".join("%s %d/%d" % (k, per[k], kinds[k]) for k in sorted(kinds)),
              sum(n for k, n in tree.items() if k[0] == "3"),
              sum(n for k, n in tree.items() if k[0] == "4"), len(facts.doors)))
    if args.check:
        listed = read_list()
        new = tree - listed
        stale = listed - tree
        if new or stale:
            print("spelling-flow: FAIL -- the tree and %s disagree" % os.path.relpath(LIST, ROOT))
            for k in sorted(new):
                print("  new    %d  %s" % (new[k], "  ".join(k)))
            for k in sorted(stale):
                print("  stale  %d  %s" % (stale[k], "  ".join(k)))
            return 1
        print("spelling-flow: OK -- %d outstanding, as listed" % sum(tree.values()))
    return 0


if __name__ == "__main__":
    sys.exit(main())
