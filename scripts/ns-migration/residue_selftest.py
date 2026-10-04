#!/usr/bin/env python3
"""Drive scripts/ns-migration/residue.py --check through a throwaway facts
file and a throwaway list, in both directions: the pair it must accept, and
for each thing the check asserts one mutation it must refuse, named.

The check has three inputs - the compiler's facts, the list and the
classifier - and each mutation moves one of them while the other two stand:

  * THE LIST.  A row deleted or duplicated is drift.  A row whose class is
    blanked, flipped to another class, or given a letter the classifier does
    not define is refused as not the classifier's.  The first check summed
    the letters as the list wrote them, so a J row flipped to N by hand read
    `OK ... J 51, N 118` and a row marked `X` read `OK ... X 1` - and the
    J count pinned in §0 is a grep over that line.
  * THE FACTS.  A holder read planted is drift; a call into a holder that
    sema left unpinned, a record of the wrong shape and a read method with
    no class are refused.  A registrar (`mut &this`), a call in the holder's
    own file, a call passing no key and a call outside compiler/src are no
    site.  A comparison reading a TABLE element at the innermost loop is a
    site (drift); the same element bound outside the innermost loop is the
    key searched for, and is no site; an element of an array placed as data
    is no site; two members of one element compared are one site.
  * THE CLASSIFIER.  Its verdict is the one the list is held to, so the
    classifier is not mutated here; the list mutations are its controls.
    Its one rule decided from the facts rather than from a table - a key
    that is string literals and nothing else is class L - is driven through
    RULE_CASES: keys it must take (one literal, several, one whose text
    holds an escaped quote and a comma, a literal into a door the tables
    class J, a literal into a door no table classes) and keys it must leave
    to the tables (a literal beside a parameter, either order; a literal
    through `intern`; a local initialised by one; a non-string literal; no
    key), and a site override on a literal site refused.

Which types hold declarations is placement, read from the compiler's
declaration records over a tree: the lane gate's own fixture
(scripts/lane-gate-selftest.py's FILES and DECL_FACTS, every store with its
map, a stub per exclusion).  The call facts are written here, in the
compiler's record format, naming methods the real classifier classifies.

Usage:
    python3 scripts/ns-migration/residue_selftest.py

Exit codes: 0 every case behaved; 1 otherwise, with the case named.
"""
import importlib.util
import io
import os
import shutil
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
import residue  # noqa: E402
import residue_classify as rc  # noqa: E402

# The fixture's two stamp-derived doors - an index read and the funnel's -
# and the funnel's forwarding call into the index: the S rows and the N
# override the baseline counts.  Classed here, by the fixture: no read in the
# compiler is stamp-derived any more, and the real classifier holds a class
# only for a method the tree declares.
FIXTURE_CLASSES = {
    "DeclarationIndex::lookup_type":
        ("S", "fixture: a type by a canonical name derived from a stamp"),
    "TypeUtils::lookup_type_exact":
        ("S", "fixture: the funnel's door onto the same"),
}
FIXTURE_OVERRIDES = {
    ("compiler/sema/type_utils.cryo", "DeclarationIndex::lookup_type", "param:name"):
        ("N", "fixture: the funnel's own forwarding body"),
}
for _k, _v in FIXTURE_CLASSES.items():
    assert _k not in rc.CLASS_OF_METHOD, _k
    rc.CLASS_OF_METHOD[_k] = _v
for _k, _v in FIXTURE_OVERRIDES.items():
    assert _k not in rc.SITE_OVERRIDES, _k
    rc.SITE_OVERRIDES[_k] = _v


def load(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


LANE = load(os.path.join(ROOT, "scripts", "lane-gate-selftest.py"), "lane_gate_selftest")
FILES = dict(LANE.FILES)

SYM = "compiler::resolver::symbol_str::SymbolStr"
TYREF = "compiler::types::type_ref::TypeRef"


def rec(kind, rel, line, callee_text="-", idx="0", ptype=SYM, prov="param:name",
        recv="param:this", pin="call", depth=0, col=9):
    """One record in the compiler's format (sema/call_facts.cryo)."""
    site = rel if rel.startswith("<") else "src/" + rel
    return "\t".join([kind, site, str(line), str(col), idx, ptype, prov, recv,
                      "C$fixture", pin, "src/compiler/decl_index.cryo:1", "C$caller",
                      callee_text, "fixture::Caller.walk(&this) -> void", str(depth)])


def door(owner, meth, params="&this, " + SYM, ret=TYREF):
    return "%s.%s(%s) -> %s" % (owner, meth, params, ret)


INDEX = "compiler::decl_index::DeclarationIndex"
FUNNEL = "compiler::sema::type_utils::TypeUtils"
GRAPH = "compiler::module_graph::ModuleGraph"
REGISTRY = "compiler::types::generic_registry::GenericRegistry"
EQUALS = SYM + ".equals(&this, " + SYM + ") -> boolean"
TRAIT_METHOD = ("field:compiler::ast::declaration::FunctionDeclNode*.name"
                "<-local:f@1=element@1:field:compiler::ast::declaration::TraitDeclNode*.methods<-param:td")
LOCAL_METHOD = ("field:compiler::ast::declaration::FunctionDeclNode*.name"
                "<-field:compiler::ast::declaration::MethodNode*.func<-element@1:param:methods")

# The baseline: five reads by call - two S (the index, the funnel), two J
# (a module, a hint), the funnel's own forwarding call into the index (N by
# the classifier's site override) - and one scan of a local method table.
BASE = [
    rec("arg", "compiler/sema/sema.cryo", 7, door(INDEX, "lookup_type")),
    rec("arg", "compiler/sema/sema.cryo", 8, door(FUNNEL, "lookup_type_exact")),
    rec("arg", "compiler/sema/sema.cryo", 9, door(GRAPH, "find_module_index")),
    rec("arg", "compiler/sema/sema.cryo", 10, door(REGISTRY, "find_trait_defining_method", ret=SYM)),
    rec("arg", "compiler/sema/type_utils.cryo", 40, door(INDEX, "lookup_type")),
    rec("arg", "compiler/sema/sema.cryo", 20, EQUALS, recv=LOCAL_METHOD, depth=1),
] + LANE.DECL_FACTS
BASE_SITES = 6
BASE_CLASSES = {"S": 2, "N": 1, "J": 2}
BASE_CLASSES[rc.CLASS_OF_METHOD["local::MethodNode[]"][0]] = \
    BASE_CLASSES.get(rc.CLASS_OF_METHOD["local::MethodNode[]"][0], 0) + 1
DRIFT_ONE = "DRIFT - tree %d, list %d" % (BASE_SITES + 1, BASE_SITES)

LIST_HEAD = """\
# fixture list

<!-- residue-table:begin -->
<!-- residue-table:end -->
"""


def write_tree(base, files):
    for rel, content in files.items():
        path = os.path.join(base, rel.replace("/", os.sep))
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w", encoding="utf-8", newline="\n") as fh:
            fh.write(content)


class Capture(object):
    def __init__(self):
        self.lines = []

    def __call__(self, s):
        self.lines.append(s)

    def text(self):
        return "\n".join(self.lines)


def run_check(src, facts, list_path):
    """residue.py --check over `facts` against `list_path`, in process: the
    population's refusals arrive as SystemExit, the check's as its code."""
    gate = residue.load_gate()
    cap = Capture()
    try:
        rows, _sets = residue.population(gate, src, facts)
    except SystemExit as e:
        return 1, str(e)
    code = residue.check(rows, list_path, out=cap)
    return code, cap.text()


# (name, records added to the baseline, expected exit, must-appear-in-output)
FACTS_MUTATIONS = [
    ("a holder read planted is drift",
     [rec("arg", "compiler/sema/sema.cryo", 30,
          door(INDEX, "lookup_func_type", "&this, compiler::resolver::res::FamilyOwner, " + SYM))],
     1, "DeclarationIndex::lookup_func_type (param:name): tree 1, list 0"),
    ("a static read on a holder is a site, and is drift",
     [rec("arg", "compiler/sema/sema.cryo", 31,
          "%s::lookup_type(%s) -> %s" % (INDEX, SYM, TYREF), recv="-")],
     1, DRIFT_ONE),
    ("a call into a holder that sema left unpinned is refused, not dropped",
     [rec("noparams", "compiler/sema/sema.cryo", 32, ".lookup_type", idx="-", ptype="-",
          prov="-", pin="none")],
     1, "sema left unpinned"),
    ("a record of the wrong shape is refused",
     [rec("arg", "compiler/sema/sema.cryo", 33, door(INDEX, "lookup_type")).rsplit("\t", 1)[0]],
     1, "not 15"),
    ("a read method the classifier has no class for is refused",
     [rec("arg", "compiler/sema/sema.cryo", 34, door("compiler::types::arena::TypeArena", "lookup_by_name"))],
     1, "no class in residue_classify.py: TypeArena::lookup_by_name"),
    ("a registrar (`mut &this`) is the write side: no site",
     [rec("arg", "compiler/sema/sema.cryo", 35, door(INDEX, "lookup_type", "mut &this, " + SYM))],
     0, "residue: OK"),
    ("a store's own calls are not the surface",
     [rec("arg", "compiler/decl_index.cryo", 50, door(INDEX, "lookup_type"))],
     0, "residue: OK"),
    ("a call passing no key is no site",
     [rec("arg", "compiler/sema/sema.cryo", 36, door(INDEX, "lookup_type"), ptype=TYREF)],
     0, "residue: OK"),
    ("a call outside compiler/src is no site",
     [rec("arg", "<stdlib>/core/cmp.cryo", 37, door(INDEX, "lookup_type"))],
     0, "residue: OK"),
    ("a comparison reading a member table's element at the innermost loop is a site, and is drift",
     [rec("cmp", "compiler/sema/sema.cryo", 38, idx="==", prov="param:name", recv=TRAIT_METHOD, depth=1)],
     1, "TraitDeclNode::methods[] (param:name): tree 1, list 0"),
    ("the same element bound outside the innermost loop is the key searched for: no site",
     [rec("cmp", "compiler/sema/sema.cryo", 39, idx="==", prov="param:name", recv=TRAIT_METHOD, depth=2)],
     0, "residue: OK"),
    ("an element of an array placed as data (a diagnostic's labels) is no site",
     [rec("cmp", "compiler/sema/sema.cryo", 41, idx="==", prov="param:name",
          recv="field:compiler::diag::diagnostic::Label.name<-element:field:compiler::diag::diagnostic::Diagnostic*.labels<-param:dg")],
     0, "residue: OK"),
    ("two members of one element compared are one site",
     [rec("cmp", "compiler/sema/sema.cryo", 42, idx="==",
          prov=TRAIT_METHOD.replace(".name<-", ".alias<-"), recv=TRAIT_METHOD, depth=1)],
     1, DRIFT_ONE),
    ("a literal key into a read method no table classes is classified by the rule: drift, not refused",
     [rec("arg", "compiler/sema/sema.cryo", 43, door("compiler::types::arena::TypeArena", "lookup_by_name"),
          prov='literal:"std::ops::Drop"')],
     1, 'TypeArena::lookup_by_name (literal:"std::ops::Drop"): tree 1, list 0'),
]

INTERN_OF = ("call:compiler::resolver::intern_table::InternTable.intern(mut &this, string) -> "
             "compiler::resolver::symbol_str::SymbolStr of ")
# (name, holder, method, key provenance, class the classifier must give).
# The holder's method is one the tables class, so a key the rule leaves
# alone reads that class, and one it takes reads L whatever the table says.
RULE_CASES = [
    ("one literal", "DeclarationIndex", "type_of_decl", 'literal:"codegen/declare struct type"', "L"),
    ("the empty literal", "ResolutionContext", "new", 'literal:""', "L"),
    ("two literals", "ModuleGraph", "find_module_index", 'literal:"std", literal:"MAX"', "L"),
    ("a literal whose text holds an escaped quote and a comma is ONE literal",
     "DeclarationIndex", "type_of_decl", r'literal:"a\", param:name"', "L"),
    ("a literal into a door the tables class J", "ModuleGraph", "find_module_index", 'literal:"std::core"', "L"),
    ("a literal beside a parameter", "ModuleGraph", "find_module_index", 'literal:"std", param:name',
     rc.CLASS_OF_METHOD["ModuleGraph::find_module_index"][0]),
    ("a parameter beside a literal", "ModuleGraph", "find_module_index", 'param:ns, literal:"MAX"',
     rc.CLASS_OF_METHOD["ModuleGraph::find_module_index"][0]),
    ("a literal through `intern`", "ModuleGraph", "find_module_index", INTERN_OF + 'literal:"std::core"',
     rc.CLASS_OF_METHOD["ModuleGraph::find_module_index"][0]),
    ("a local initialised by a literal", "ModuleGraph", "find_module_index", 'local:p@0=literal:"std::core"',
     rc.CLASS_OF_METHOD["ModuleGraph::find_module_index"][0]),
    ("a literal with text after its closing quote", "ModuleGraph", "find_module_index", 'literal:"a"x',
     rc.CLASS_OF_METHOD["ModuleGraph::find_module_index"][0]),
    ("a non-string literal", "ModuleGraph", "find_module_index", "literal:null",
     rc.CLASS_OF_METHOD["ModuleGraph::find_module_index"][0]),
    ("no key", "ModuleGraph", "find_module_index", "",
     rc.CLASS_OF_METHOD["ModuleGraph::find_module_index"][0]),
]


def rule_failures():
    failures = []
    for name, holder, meth, arg, want in RULE_CASES:
        classified, _unknown = rc.classify([("compiler/sema/sema.cryo", 1, holder, meth, arg)])
        got = classified[0][4]
        if got != want:
            failures.append("rule case (%s): `%s` into %s::%s classed %s, want %s"
                            % (name, arg, holder, meth, got, want))
    # A site override may not reclass a site the rule classifies.
    row = ("compiler/sema/sema.cryo", 1, "ModuleGraph", "find_module_index", 'literal:"std::core"')
    key = (row[0], "ModuleGraph::find_module_index", row[4])
    rc.SITE_OVERRIDES[key] = ("J", "fixture")
    try:
        if rc.override_conflicts([row]) != [key]:
            failures.append("rule case (a site override on a literal site): not refused")
        if rc.override_conflicts([row[:4] + ('param:name',)]):
            failures.append("rule case (a site override on a literal site): refused a site it does not name")
    finally:
        del rc.SITE_OVERRIDES[key]
    return failures

# The J row the list mutations edit: the graph's module read.
J_ROW_KEY = "| `compiler/sema/sema.cryo:9` | `ModuleGraph::find_module_index` | `param:name` | J |"


def list_mutations(text):
    line = [l for l in text.splitlines() if l.startswith(J_ROW_KEY)]
    assert len(line) == 1, "the fixture list has no single J row:\n" + text
    line = line[0]
    return [
        ("a row deleted from the list is drift",
         text.replace(line + "\n", ""), 1, "ModuleGraph::find_module_index (param:name): tree 1, list 0"),
        ("a row duplicated in the list is drift",
         text.replace(line + "\n", line + "\n" + line + "\n"), 1, "tree 1, list 2"),
        ("a row's class blanked to `?` is refused as not the classifier's",
         text.replace(line, line.replace("| J |", "| ? |")), 1,
         "the list says `?`, which is no class (JLNSBCFW); the classifier says J"),
        ("a J row flipped to N by hand is refused, not summed",
         text.replace(line, line.replace("| J |", "| N |")), 1,
         "the list says N, the classifier says J"),
        ("a class letter the classifier does not define is refused",
         text.replace(line, line.replace("| J |", "| X |")), 1,
         "the list says `X`, which is no class (JLNSBCFW)"),
    ]


def write_facts(path, records):
    with open(path, "w", encoding="utf-8", newline="\n") as fh:
        fh.write("".join(r + "\n" for r in records))


def main():
    work = tempfile.mkdtemp(prefix="residue-selftest-")
    failures = []
    try:
        base = os.path.join(work, "base")
        write_tree(base, FILES)
        facts = os.path.join(work, "base.facts")
        write_facts(facts, BASE)
        list_path = os.path.join(work, "residue.md")
        io.open(list_path, "w", encoding="utf-8", newline="\n").write(LIST_HEAD)

        # The baseline: the classifier writes the list for the fixture, and
        # the check then reads OK against it with the counts the fixture was
        # written to produce.
        gate = residue.load_gate()
        try:
            rows, _sets = residue.population(gate, base, facts)
        except SystemExit as e:
            failures.append("baseline: the population refused the fixture:\n%s" % e)
            rows = []
        classified, unknown = rc.classify(rows)
        if unknown or len(rows) != BASE_SITES:
            failures.append("baseline: %d sites (want %d), unknown %s:\n%s"
                            % (len(rows), BASE_SITES, sorted(unknown), rows))
        counts = {}
        for r in classified:
            counts[r[4]] = counts.get(r[4], 0) + 1
        if counts != BASE_CLASSES:
            failures.append("baseline: classes %s, want %s" % (counts, BASE_CLASSES))
        rc.write_list(list_path, classified)
        code, out = run_check(base, facts, list_path)
        if code != 0 or "residue: OK -- %d sites" % BASE_SITES not in out:
            failures.append("baseline: the check did not read OK against the list it wrote:\n%s" % out)
        text = io.open(list_path, encoding="utf-8").read()

        for i, (name, added, want_code, want_text) in enumerate(FACTS_MUTATIONS):
            mutated = os.path.join(work, "m%d.facts" % i)
            write_facts(mutated, BASE + added)
            code, out = run_check(base, mutated, list_path)
            if code != want_code or want_text not in out:
                failures.append("facts mutation %d (%s): expected exit %d with `%s`, got exit %d:\n%s"
                                % (i, name, want_code, want_text, code, out))

        for i, (name, mutated, want_code, want_text) in enumerate(list_mutations(text)):
            p = os.path.join(work, "list%d.md" % i)
            io.open(p, "w", encoding="utf-8", newline="\n").write(mutated)
            code, out = run_check(base, facts, p)
            if code != want_code or want_text not in out:
                failures.append("list mutation %d (%s): expected exit %d with `%s`, got exit %d:\n%s"
                                % (i, name, want_code, want_text, code, out))
    finally:
        shutil.rmtree(work, ignore_errors=True)
    failures.extend(rule_failures())

    if failures:
        sys.stderr.write("residue-selftest: %d FAILURE(S)\n\n" % len(failures))
        for f in failures:
            sys.stderr.write(f + "\n\n")
        return 1
    print("residue-selftest: OK -- baseline accepted, %d facts and %d list mutations behaved, %d literal-rule cases"
          % (len(FACTS_MUTATIONS), len(list_mutations(text)), len(RULE_CASES) + 1))
    return 0


if __name__ == "__main__":
    sys.exit(main())
