#!/usr/bin/env python3
"""Drive every rule of scripts/lane-gate.py through a throwaway tree, in both
directions: the tree it must accept, and for each rule one mutation it must
refuse, named by row.

A gate is not fixed until a mutation the old gate accepted is refused by the
new one, and a gate that runs on every commit needs a committed test that
proves it can refuse - the failure mode is silence.  The three holes this
tree pins were each found by an audit building the mutation by hand:

  * a reader declared in a cross-file `implement struct <Store>` block, which
    a parser reading only the `type struct` block never saw;
  * a reader keyed by `string`, which a key rule matching the one token
    `SymbolStr` never saw;
  * a reader on any store but the index, which a gate watching one store
    never saw.

And the placement rule, which replaced a receiver-spelling list: a store
reached through a local of a new spelling, through a zero-argument accessor,
or through an indexed field is placed by its declared type, and a receiver
whose type cannot be read is refused rather than dropped.

Usage:
    python3 scripts/lane-gate-selftest.py

Exit codes: 0 every case behaved; 1 otherwise, with the case named.
"""
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GATE = os.path.join(ROOT, "scripts", "lane-gate.py")

# The smallest tree the gate accepts: every store present, each with one
# name-keyed method (the index with the four LOOKUP names, the arena with
# get_qualified_name - the parser's own controls), a context carrying them, and
# one caller exercising one call per baseline row.
FILES = {
    "compiler/decl_index.cryo": """\
type struct DeclarationIndex {
    entries: TypeRef[];

    lookup_type(&this, name: SymbolStr) -> TypeRef { return this.entries[0]; }
    lookup_func_type(&this, name: SymbolStr) -> TypeRef { return this.entries[0]; }
    lookup_global(&this, name: SymbolStr) -> TypeRef { return this.entries[0]; }
    lookup_method_return(&this, type_sym: SymbolStr, method_sym: SymbolStr) -> TypeRef {
        return this.entries[0];
    }
    register_type(mut &this, qualified_name: SymbolStr, ty: TypeRef) -> void {
        this.entries.push(ty);
    }
    entry_at(&this, i: i64) -> TypeRef { return this.entries[i]; }
}
""",
    "compiler/sema/type_utils.cryo": """\
type struct TypeUtils {
    ctx: CompilationContext*;

    lookup_type_exact(&this, name: SymbolStr) -> TypeRef {
        return this.ctx.decl_index.lookup_type(name);
    }
}
""",
    "compiler/types/arena.cryo": """\
type struct TypeArena {
    names: SymbolStr[];

    lookup_by_name(&this, name: SymbolStr) -> TypeRef { return TypeRef::invalid(); }
    get_qualified_name(&this, ty: TypeRef) -> SymbolStr { return this.names[0]; }
    create_struct(mut &this, qualified_name: SymbolStr, module_name: SymbolStr) -> TypeRef {
        this.names.push(qualified_name);
        return TypeRef::invalid();
    }
    lookup(&this, id: u64) -> Type* { return null; }
}
""",
    "compiler/types/generic_registry.cryo": """\
type struct GenericRegistry {
    arena: TypeArena*;

    get_template(&this, qualified_name: SymbolStr) -> TemplateEntry* { return null; }
    register_impl_block(mut &this, qualified_name: SymbolStr, block: ImplBlockNode*) -> void {}
}
""",
    "compiler/module_graph.cryo": """\
type struct ModuleGraph {
    modules: ModuleInfo[];

    find_module_index(&this, name: SymbolStr) -> i64 { return -1; }
}
""",
    "compiler/const_table.cryo": """\
type struct ConstantTable {
    entries: ConstEntry[];

    register(mut &this, qualified: SymbolStr, init: ExpressionNode*) -> void {}
}
""",
    "compiler/resolver/resolver.cryo": """\
type struct Resolver {
    scopes: Scope[];

    lookup(&this, name: SymbolStr, start: ScopeID) -> SymbolID { return SymbolID::invalid(); }
    set_module(mut &this, name: SymbolStr) -> void {}
}
""",
    "compiler/resolver/name_resolution.cryo": """\
type struct NameResolver {
    ctx: CompilationContext*;

    bind(mut &this, name: SymbolStr) -> void {
        this.ctx.resolver.lookup(name, ScopeID::root());
        this.ctx.resolver.set_module(name);
    }
}
""",
    "compiler/compilation_context.cryo": """\
type struct CompilationContext {
    decl_index:       DeclarationIndex*;
    type_arena:       TypeArena*;
    generic_registry: GenericRegistry*;
    module_graph:     ModuleGraph*;
    const_table:      ConstantTable*;
    resolver:         Resolver*;

    get_resolver(&this) -> Resolver* { return this.resolver; }
    get_arena(&this) -> TypeArena* { return this.type_arena; }
}
""",
    "compiler/sema/scope_manager.cryo": """\
type struct ScopeManager {
    scopes: Scope[];

    lookup_type(&this, name: SymbolStr) -> TypeRef { return TypeRef::invalid(); }
}
""",
    "compiler/sema/sema.cryo": """\
type struct Sema {
    ctx:    CompilationContext*;
    types:  TypeUtils;
    scopes: ScopeManager;

    walk(&this, name: SymbolStr, t: TypeRef) -> void {
        this.ctx.decl_index.lookup_type(name);
        this.types.lookup_type_exact(name);
        this.scopes.lookup_type(name);
        this.ctx.type_arena.lookup_by_name(name);
        const arena: TypeArena* = this.ctx.type_arena;
        arena.get_qualified_name(t);
        // this.ctx.decl_index.lookup_global(name);
    }
}
""",
}

# LOOKUP is 2: the caller's one, and the funnel's call INTO the index in
# type_utils.cryo, which is counted - only a store's own file is excluded
# from its own set.
BASELINE = {
    "LOOKUP": 2, "LOOKUP_OTHER": 0, "REGISTER": 0, "LOOKUP_ROUTED": 1,
    "LOOKUP_LOCAL": 1, "ARENA_READ": 2, "ARENA_WRITE": 0,
    "REGISTRY_READ": 0, "REGISTRY_WRITE": 0, "GRAPH_READ": 0, "GRAPH_WRITE": 0,
    "CONST_READ": 0, "CONST_WRITE": 0, "REENTRY": 0, "HOME_WRITE": 0,
    "DEFID_MINT": 0, "DEFID_UNWRAP": 0,
}


def sema_with(extra_lines):
    """The caller with `extra_lines` appended inside `walk`."""
    src = FILES["compiler/sema/sema.cryo"]
    marker = "        arena.get_qualified_name(t);\n"
    assert marker in src
    return src.replace(marker, marker + "".join("        %s\n" % l for l in extra_lines))


# (name, {relpath: content or None to delete}, expected exit, must-appear-in-output)
MUTATIONS = [
    ("cross-file implement block declares a reader; its call is counted",
     {"compiler/types/registry_ext.cryo":
          "implement struct GenericRegistry {\n"
          "    probe(&this, name: SymbolStr) -> i64 { return 0; }\n"
          "}\n",
      "compiler/sema/sema.cryo": sema_with(["this.ctx.generic_registry.probe(name);"])},
     1, "REGISTRY_READ TOTAL 0 -> 1"),
    ("a reader keyed by `string` is inside the rule",
     {"compiler/decl_index.cryo":
          FILES["compiler/decl_index.cryo"].replace(
              "    entry_at(",
              "    by_spelling(&this, name: string) -> TypeRef { return this.entries[0]; }\n"
              "    entry_at("),
      "compiler/sema/sema.cryo": sema_with(['this.ctx.decl_index.by_spelling("x");'])},
     1, "LOOKUP_OTHER TOTAL 0 -> 1"),
    ("a reader on another store (the module graph) has a row",
     {"compiler/sema/sema.cryo": sema_with(["this.ctx.module_graph.find_module_index(name);"])},
     1, "GRAPH_READ TOTAL 0 -> 1"),
    ("a name-keyed write on another store (the constant table) has a row",
     {"compiler/sema/sema.cryo": sema_with(["this.ctx.const_table.register(name, null);"])},
     1, "CONST_WRITE TOTAL 0 -> 1"),
    ("a write declared in a cross-file implement block lands in the WRITE row",
     {"compiler/graph_ext.cryo":
          "implement ModuleGraph {\n"
          "    note(mut &this, name: SymbolStr) -> void {}\n"
          "}\n",
      "compiler/sema/sema.cryo": sema_with(["this.ctx.module_graph.note(name);"])},
     1, "GRAPH_WRITE TOTAL 0 -> 1"),
    ("the resolver asked by name outside its owners is a re-entry",
     {"compiler/sema/sema.cryo": sema_with(["this.ctx.resolver.lookup(name, ScopeID::root());"])},
     1, "REENTRY TOTAL 0 -> 1"),
    ("get_resolver() and an ask through the accessor's return type are two re-entries",
     {"compiler/sema/sema.cryo": sema_with(["this.ctx.get_resolver().lookup(name, ScopeID::root());"])},
     1, "REENTRY TOTAL 0 -> 2"),
    ("a store reached through a local under a new spelling is placed by its annotation",
     {"compiler/sema/sema.cryo": sema_with(["const idx: DeclarationIndex* = this.ctx.decl_index;",
                                            "idx.lookup_global(name);"])},
     1, "LOOKUP TOTAL 2 -> 3"),
    ("a store reached through a zero-argument accessor is placed by its return type",
     {"compiler/sema/sema.cryo": sema_with(["this.ctx.get_arena().get_qualified_name(t);"])},
     1, "ARENA_READ TOTAL 2 -> 3"),
    ("a store reached through an indexed field is placed by the element type",
     {"compiler/sema/sema.cryo": sema_with(["this.ctx.module_graph.modules[0].graph.find_module_index(name);"]),
      "compiler/module_info.cryo":
          "type struct ModuleInfo {\n"
          "    graph: ModuleGraph*;\n"
          "}\n"},
     1, "GRAPH_READ TOTAL 0 -> 1"),
    ("a receiver whose type cannot be read is refused, not dropped",
     {"compiler/sema/sema.cryo": sema_with(["const g = mystery();", "g.get_template(name);"])},
     1, "could not be placed by receiver"),
    ("a call on something other than a dotted receiver is refused",
     {"compiler/sema/sema.cryo": sema_with(["(this.ctx.decl_index).lookup_global(name);"])},
     1, "could not be placed by receiver"),
    ("a decrease is refused too (the ceiling must be re-pinned deliberately)",
     {"compiler/sema/sema.cryo": FILES["compiler/sema/sema.cryo"].replace(
          "        this.ctx.decl_index.lookup_type(name);\n", "")},
     1, "LOOKUP TOTAL 2 -> 1"),
    ("a commented-out call is not counted (uncommenting it is)",
     {"compiler/sema/sema.cryo": FILES["compiler/sema/sema.cryo"].replace(
          "        // this.ctx.decl_index.lookup_global(name);\n",
          "        this.ctx.decl_index.lookup_global(name);\n")},
     1, "LOOKUP TOTAL 2 -> 3"),
    ("a store's own calls are not the surface",
     {"compiler/decl_index.cryo": FILES["compiler/decl_index.cryo"].replace(
          "    entry_at(&this, i: i64) -> TypeRef { return this.entries[i]; }",
          "    entry_at(&this, i: i64) -> TypeRef { return this.lookup_type(this.names[i]); }")},
     0, "lane-gate: OK"),
    ("a store with no name-keyed method is an unmeasured tree, refused",
     {"compiler/module_graph.cryo":
          "type struct ModuleGraph {\n"
          "    modules: ModuleInfo[];\n"
          "    count(&this) -> i64 { return 0; }\n"
          "}\n"},
     1, "declares no method whose signature mentions"),
    ("a missing store definition is refused",
     {"compiler/const_table.cryo": None},
     1, "no compiler/const_table.cryo"),
]


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


def run_gate(src, golden, *extra):
    p = subprocess.run([sys.executable, GATE, "--src", src, "--golden", golden] + list(extra),
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    return p.returncode, p.stdout


def main():
    work = tempfile.mkdtemp(prefix="lane-gate-selftest-")
    failures = []
    try:
        base = os.path.join(work, "base")
        golden = os.path.join(work, "golden.txt")
        write_tree(base, FILES)

        # The baseline: --update pins exactly the numbers the tree was
        # written to produce, and the gate then reads OK against them.
        code, out = run_gate(base, golden, "--update")
        if code != 0:
            failures.append("baseline --update exited %d:\n%s" % (code, out))
        for kind, n in BASELINE.items():
            want = "%s = %d" % (kind, n)
            if want not in out:
                failures.append("baseline: expected `%s` in --update output:\n%s" % (want, out))
        code, out = run_gate(base, golden)
        if code != 0 or "lane-gate: OK" not in out:
            failures.append("baseline: gate did not read OK against its own golden:\n%s" % out)
        code, out = run_gate(base, golden, "--names")
        if "GenericRegistry (2):" not in out or "ModuleGraph (1):" not in out:
            failures.append("--names did not list the stores' sets:\n%s" % out)
        code, out = run_gate(base, os.path.join(work, "absent.txt"))
        if code != 1 or "no golden" not in out:
            failures.append("a missing golden must be refused:\n%s" % out)

        for i, (name, edits, want_code, want_text) in enumerate(MUTATIONS):
            tree = os.path.join(work, "m%d" % i)
            shutil.copytree(base, tree)
            write_tree(tree, edits)
            code, out = run_gate(tree, golden)
            if code != want_code or want_text not in out:
                failures.append("mutation %d (%s): expected exit %d with `%s`, got exit %d:\n%s"
                                % (i, name, want_code, want_text, code, out))
            elif i == 0:
                # The cross-file reader must also be visible in --names, so
                # what the rule swept up can be read rather than inferred.
                _c, names = run_gate(tree, golden, "--names")
                if "read   probe" not in names:
                    failures.append("mutation 0: --names does not list the implement-block reader:\n%s" % names)
    finally:
        shutil.rmtree(work, ignore_errors=True)

    if failures:
        sys.stderr.write("lane-gate-selftest: %d FAILURE(S)\n\n" % len(failures))
        for f in failures:
            sys.stderr.write(f + "\n\n")
        return 1
    print("lane-gate-selftest: OK -- baseline accepted, %d mutations behaved" % len(MUTATIONS))
    return 0


if __name__ == "__main__":
    sys.exit(main())
