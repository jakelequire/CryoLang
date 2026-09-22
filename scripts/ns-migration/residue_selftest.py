#!/usr/bin/env python3
"""Drive scripts/ns-migration/residue.py --check through a throwaway tree and
a throwaway list, in both directions: the pair it must accept, and for each
thing the check asserts one mutation it must refuse, named.

The check has three inputs - the tree, the list and the classifier - and
each mutation moves one of them while the other two stand:

  * THE LIST.  A row deleted or duplicated is drift.  A row whose class is
    blanked, flipped to another class, or given a letter the classifier does
    not define is refused as not the classifier's.  The first check summed
    the letters as the list wrote them, so a J row flipped to N by hand read
    `OK ... J 51, N 118` and a row marked `X` read `OK ... X 1` - and the
    J count pinned in §0 is a grep over that line.
  * THE TREE.  A read planted on a placed receiver is drift.  A receiver
    whose type cannot be read, a receiver no pattern reaches (a
    parenthesized expression, a call with arguments) and a read method with
    no class are each refused.  A cast receiver is placed by the cast's
    type; a turbofish static is placed by its owner.  A receiver misread as
    a type that holds nothing - a local shadowed by a same-named binding of
    another type in a closed block above - moves the elsewhere table.
  * THE CLASSIFIER.  Its verdict is the one the list is held to, so the
    classifier is not mutated here; the list mutations are its controls.

The fixture is the lane gate's own (scripts/lane-gate-selftest.py's FILES:
every store with its map, a stub per exclusion), with the caller rewritten
to reach only methods residue_classify.py classifies, so the classifier's
real tables decide the fixture's classes.

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


def load(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


LANE = load(os.path.join(ROOT, "scripts", "lane-gate-selftest.py"), "lane_gate_selftest")
FILES = dict(LANE.FILES)

# The caller: one call per class the fixture can reach through the real
# classifier - S (the index, the funnel), J module (the graph), B (the
# registry) - one call on a type that holds nothing (the scope manager: the
# elsewhere table's one row), and one turbofish static on a non-holder
# (`new` is a population name, `ResolutionContext::new`'s; a static pattern
# that stops at the first `::` reads this as a call it cannot place).
CALLER = """\
type struct Sema {
    ctx:    CompilationContext*;
    types:  TypeUtils;
    scopes: ScopeManager;

    walk(&this, name: SymbolStr, t: TypeRef) -> void {
        this.ctx.decl_index.lookup_type(name);
        this.types.lookup_type_exact(name);
        this.ctx.module_graph.find_module_index(name);
        this.ctx.generic_registry.get_template(name);
        this.scopes.lookup_type(name);
        this.pairs.push(Pair::<SymbolStr, TypeRef>::new(name, t));
    }
}
"""
FILES["compiler/sema/sema.cryo"] = CALLER
# The resolver's driving pass calls nothing by name here: `Resolver::lookup`
# has no class, and its call from an owner file is not a site anyway.
FILES["compiler/resolver/name_resolution.cryo"] = """\
type struct NameResolver {
    ctx: CompilationContext*;

    bind(mut &this, name: SymbolStr) -> void {}
}
"""
# `ResolutionContext::new(source_file)` is the N-classed constructor whose
# leaf `new` puts every `Owner::<T>::new(` in the tree under the static
# pattern; the fixture declares it so the turbofish control is real.
FILES["compiler/types/resolver.cryo"] = FILES["compiler/types/resolver.cryo"].replace(
    "    index_of(&this, name: SymbolStr) -> i64 { return -1; }\n",
    "    index_of(&this, name: SymbolStr) -> i64 { return -1; }\n"
    "    static new(source_file: string) -> ResolutionContext { return ResolutionContext { names: [] }; }\n", 1)

# What the fixture must produce: 5 call sites (the funnel's own forwarding
# call into the index is the fifth, N by override), plus one inline scan
# per LOCAL table the gate lists (the lane fixture carries a stub scan for
# each, and a local table has no owner file to be excluded by), each with
# the class the real classifier gives it; 1 elsewhere row.
GATE_MOD = residue.load_gate()
LOCAL_TABLES = sorted(label.split(".", 1)[1] for label, sc in GATE_MOD.SCANNED_ARRAYS.items()
                      if label.startswith(GATE_MOD.LOCAL + ".") and sc.kind == GATE_MOD.TABLE)
BASE_SITES = 5 + len(LOCAL_TABLES)
BASE_CLASSES = {"S": 2, "N": 1, "J": 1, "B": 1}
for _elem in LOCAL_TABLES:
    _cls = rc.CLASS_OF_METHOD["%s::%s[]" % (GATE_MOD.LOCAL, _elem)][0]
    BASE_CLASSES[_cls] = BASE_CLASSES.get(_cls, 0) + 1
BASE_ELSEWHERE = {("ScopeManager", "lookup_type"): 1}
DRIFT_ONE = "DRIFT - tree %d, list %d" % (BASE_SITES + 1, BASE_SITES)

LIST_HEAD = """\
# fixture list

<!-- residue-table:begin -->
<!-- residue-table:end -->

<!-- residue-elsewhere:begin -->
<!-- residue-elsewhere:end -->
"""


def caller_with(extra_lines):
    marker = "        this.scopes.lookup_type(name);\n"
    assert marker in CALLER
    return CALLER.replace(marker, marker + "".join("        %s\n" % l for l in extra_lines))


def write_tree(base, files):
    for rel, content in files.items():
        path = os.path.join(base, rel.replace("/", os.sep))
        if content is None:
            if os.path.exists(path):
                os.remove(path)
            continue
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


def run_check(src, list_path):
    """residue.py --check over `src` against `list_path`, in process: the
    population's refusals arrive as SystemExit, the check's as its code."""
    gate = residue.load_gate()
    cap = Capture()
    try:
        rows, _sets, elsewhere = residue.population(gate, src)
    except SystemExit as e:
        return 1, str(e)
    code = residue.check(rows, elsewhere, list_path, out=cap)
    return code, cap.text()


# (name, {relpath: content}, expected exit, must-appear-in-output)
TREE_MUTATIONS = [
    ("a read planted on a placed receiver is drift",
     {"compiler/sema/sema.cryo": caller_with(["this.ctx.decl_index.lookup_func_type(name);"])},
     1, "DeclarationIndex::lookup_func_type (name): tree 1, list 0"),
    ("a read planted through a local under a new spelling is placed by its annotation, and is drift",
     {"compiler/sema/sema.cryo": caller_with(["const idx: DeclarationIndex* = this.ctx.decl_index;",
                                              "idx.lookup_type(name);"])},
     1, DRIFT_ONE),
    ("a cast receiver is placed by the type the cast names",
     {"compiler/sema/sema.cryo": caller_with(["(this.ctx.something as DeclarationIndex*).lookup_type(name);"])},
     1, DRIFT_ONE),
    ("a receiver whose type cannot be read is refused, not dropped",
     {"compiler/sema/sema.cryo": caller_with(["const g = mystery();", "g.get_template(name);"])},
     1, "receiver whose type cannot be read"),
    ("a parenthesized receiver is one no pattern reaches: refused",
     {"compiler/sema/sema.cryo": caller_with(["(this.ctx.decl_index).lookup_type(name);"])},
     1, "no simple receiver"),
    ("a receiver that is a call with arguments is one no pattern reaches: refused",
     {"compiler/sema/sema.cryo": caller_with(["this.index_for(name).lookup_type(name);"])},
     1, "no simple receiver"),
    ("a cast whose operand carries parentheses is refused, not guessed",
     {"compiler/sema/sema.cryo": caller_with(["(this.ctx.get_arena() as DeclarationIndex*).lookup_type(name);"])},
     1, "no simple receiver"),
    ("a read method the classifier has no class for is refused",
     {"compiler/sema/sema.cryo": caller_with(["this.ctx.type_arena.lookup_by_name(name);"])},
     1, "no class in residue_classify.py: TypeArena::lookup_by_name"),
    ("a receiver shadowed by a same-named binding of another type in a closed block above is "
     "misread as that type, and the elsewhere table moves",
     {"compiler/sema/sema.cryo": caller_with(["{ const di: SymbolStr = name; }",
                                              "const di: DeclarationIndex* = this.ctx.decl_index;",
                                              "{ const di: TypeRef = t; }",
                                              "di.lookup_type(name);"])},
     1, "elsewhere TypeRef::lookup_type: tree 1, list 0"),
    ("a same-named read on a type that holds nothing, added, moves the elsewhere table",
     {"compiler/sema/sema.cryo": caller_with(["this.scopes.lookup_type(name);"])},
     1, "elsewhere ScopeManager::lookup_type: tree 2, list 1"),
    ("a store's own calls are not the surface",
     {"compiler/decl_index.cryo": FILES["compiler/decl_index.cryo"].replace(
          "    entry_at(&this, i: i64) -> TypeRef { return this.entries[i]; }",
          "    entry_at(&this, i: i64) -> TypeRef { return this.lookup_type(this.names[i]); }")},
     0, "residue: OK"),
    ("a commented-out call is not counted",
     {"compiler/sema/sema.cryo": caller_with(["// this.ctx.decl_index.lookup_func_type(name);"])},
     0, "residue: OK"),
    # Rule 1c: a scan over a TABLE array is a site as its door would be; a
    # scan over DATA is not.
    ("the door's loop written at the caller over a member table is a site, and is drift",
     {"compiler/sema/sema.cryo": caller_with(["const td: TraitDeclNode* = null;",
                                              "for (mut i: i64 = 0; i < td.methods.length; i++) {",
                                              "    const f: FunctionDeclNode* = td.methods[i];",
                                              "    if (f.name.equals(name)) { return; }",
                                              "}"])},
     1, "TraitDeclNode::methods[] (name): tree 1, list 0"),
    ("the same loop over an array placed as data (a diagnostic's labels) is no site",
     {"compiler/sema/sema.cryo": caller_with(["const dg: Diagnostic* = null;",
                                              "if (dg.labels[0].name.equals(name)) { return; }"])},
     0, "residue: OK"),
]

# The J row the list mutations edit: the graph's module read.
J_ROW_KEY = "| `compiler/sema/sema.cryo:9` | `ModuleGraph::find_module_index` | `name` | J |"


def list_mutations(text):
    line = [l for l in text.splitlines() if l.startswith(J_ROW_KEY)]
    assert len(line) == 1, "the fixture list has no single J row:\n" + text
    line = line[0]
    els = "| `ScopeManager::lookup_type` | 1 |"
    assert text.count(els) == 1
    return [
        ("a row deleted from the list is drift",
         text.replace(line + "\n", ""), 1, "ModuleGraph::find_module_index (name): tree 1, list 0"),
        ("a row duplicated in the list is drift",
         text.replace(line + "\n", line + "\n" + line + "\n"), 1, "tree 1, list 2"),
        ("a row's class blanked to `?` is refused as not the classifier's",
         text.replace(line, line.replace("| J |", "| ? |")), 1,
         "the list says `?`, which is no class (JNSBCFW); the classifier says J"),
        ("a J row flipped to N by hand is refused, not summed",
         text.replace(line, line.replace("| J |", "| N |")), 1,
         "the list says N, the classifier says J"),
        ("a class letter the classifier does not define is refused",
         text.replace(line, line.replace("| J |", "| X |")), 1,
         "the list says `X`, which is no class (JNSBCFW)"),
        ("an elsewhere count edited by hand is refused",
         text.replace(els, "| `ScopeManager::lookup_type` | 0 |"), 1,
         "elsewhere ScopeManager::lookup_type: tree 1, list 0"),
        ("an elsewhere row deleted by hand is refused",
         text.replace(els + "\n", ""), 1,
         "elsewhere ScopeManager::lookup_type: tree 1, list 0"),
    ]


def main():
    work = tempfile.mkdtemp(prefix="residue-selftest-")
    failures = []
    try:
        base = os.path.join(work, "base")
        write_tree(base, FILES)
        list_path = os.path.join(work, "residue.md")
        io.open(list_path, "w", encoding="utf-8", newline="\n").write(LIST_HEAD)

        # The baseline: the classifier writes the list for the fixture, and
        # the check then reads OK against it with the counts the fixture was
        # written to produce.
        gate = residue.load_gate()
        try:
            rows, _sets, elsewhere = residue.population(gate, base)
        except SystemExit as e:
            failures.append("baseline: the population refused the fixture:\n%s" % e)
            rows, elsewhere = [], {}
        classified, unknown = rc.classify(rows)
        if unknown or len(rows) != BASE_SITES:
            failures.append("baseline: %d sites (want %d), unknown %s:\n%s"
                            % (len(rows), BASE_SITES, sorted(unknown), rows))
        counts = {}
        for r in classified:
            counts[r[4]] = counts.get(r[4], 0) + 1
        if counts != BASE_CLASSES:
            failures.append("baseline: classes %s, want %s" % (counts, BASE_CLASSES))
        if elsewhere != BASE_ELSEWHERE:
            failures.append("baseline: elsewhere %s, want %s" % (elsewhere, BASE_ELSEWHERE))
        rc.write_list(list_path, classified, elsewhere)
        code, out = run_check(base, list_path)
        if code != 0 or "residue: OK -- %d sites" % BASE_SITES not in out or "elsewhere 1" not in out:
            failures.append("baseline: the check did not read OK against the list it wrote:\n%s" % out)
        text = io.open(list_path, encoding="utf-8").read()

        for i, (name, edits, want_code, want_text) in enumerate(TREE_MUTATIONS):
            tree = os.path.join(work, "t%d" % i)
            shutil.copytree(base, tree)
            write_tree(tree, edits)
            code, out = run_check(tree, list_path)
            if code != want_code or want_text not in out:
                failures.append("tree mutation %d (%s): expected exit %d with `%s`, got exit %d:\n%s"
                                % (i, name, want_code, want_text, code, out))

        for i, (name, mutated, want_code, want_text) in enumerate(list_mutations(text)):
            p = os.path.join(work, "list%d.md" % i)
            io.open(p, "w", encoding="utf-8", newline="\n").write(mutated)
            code, out = run_check(base, p)
            if code != want_code or want_text not in out:
                failures.append("list mutation %d (%s): expected exit %d with `%s`, got exit %d:\n%s"
                                % (i, name, want_code, want_text, code, out))
    finally:
        shutil.rmtree(work, ignore_errors=True)

    if failures:
        sys.stderr.write("residue-selftest: %d FAILURE(S)\n\n" % len(failures))
        for f in failures:
            sys.stderr.write(f + "\n\n")
        return 1
    print("residue-selftest: OK -- baseline accepted, %d tree and %d list mutations behaved"
          % (len(TREE_MUTATIONS), len(list_mutations(text))))
    return 0


if __name__ == "__main__":
    sys.exit(main())
