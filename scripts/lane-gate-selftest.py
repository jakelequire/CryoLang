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

And the store rule, which replaced a hand-written list of stores: a type
that owns a map is a store or a stated exclusion, and one in neither is
refused - the audit's mutation was a new map-keyed type carried by the
context with a reader and a caller, over which the list read OK.  The
fixture carries a stub for every exclusion the gate lists (generated from
the gate's own table, so an exclusion added there is exercised here), and
a stub that loses its map is refused as stale.

The gate reads what a declaration IS from the compiler's declaration records
(`type`, `field`, `fn`, `param` in the facts), not from source, so the
fixture is a set of declarations (`Fixture`) rendered twice: as the records
the compiler would write for them, and as a source file per module, which
only has to exist.  A case changes the declarations, and both follow.

Usage:
    python3 scripts/lane-gate-selftest.py

Exit codes: 0 every case behaved; 1 otherwise, with the case named.
"""
import copy
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GATE = os.path.join(ROOT, "scripts", "lane-gate.py")


def load_gate():
    """The gate as a module, for its STORES and EXCLUDED tables: the fixture
    is built from them, so a store or an exclusion added to the gate is a
    stub added here without editing this file."""
    import importlib.util
    spec = importlib.util.spec_from_file_location("lane_gate", GATE)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


GATE_MOD = load_gate()

SYM = "compiler::resolver::symbol_str::SymbolStr"
TREF = "compiler::types::type_ref::TypeRef"
MODULE_PATH = "compiler::module_graph::ModulePath"
MAP = "std::collections::hashmap::HashMap<u32, i64, std::alloc::allocator::GlobalAlloc>"
KEY_EQ = SYM + ".equals(&this, " + SYM + ") -> boolean"
TREF_EQ = TREF + ".equals(&this, " + TREF + ") -> boolean"
KEY_TYPE_TEXT = {"SymbolStr": SYM, "string": "string", "ModulePath": MODULE_PATH}


def map_of(value):
    return "std::collections::hashmap::HashMap<u32, %s, std::alloc::allocator::GlobalAlloc>" % value


def module_of(rel):
    return "::".join(rel[:-len(".cryo")].split("/"))


def _mangled(path):
    return ".".join("%d%s" % (len(seg), seg) for seg in path.split("::"))


def key_of(ty):
    """The key column the compiler writes for a declared type: the key it
    carries under any number of references and arrays, `-` for none."""
    t = ty
    while True:
        t = t.lstrip("&")
        if not t.endswith("[]"):
            break
        t = t[:-2]
    if t == "string":
        return "string"
    if "<" in t or t.endswith("*"):
        return "-"
    leaf = t.rsplit("::", 1)[-1]
    return leaf if leaf in ("SymbolStr", "QualifiedName", "ModulePath") else "-"


def written(ty):
    """A declared type as the fixture's source file spells it."""
    import re
    return re.sub(r"(?:[A-Za-z_][A-Za-z_0-9]*::)+", "", ty)


class Fixture(object):
    """The fixture's declarations: types, each in a module file, with fields
    and methods.  `records()` is what `cryo build --emit=facts` writes for
    them; `files()` is a source file per module."""

    def __init__(self):
        self.types = []
        self.extra_files = set()

    def find(self, name, rel=None):
        for t in self.types:
            if t["name"] == name and (rel is None or t["rel"] == rel):
                return t
        return None

    def declare(self, rel, name, fields=(), methods=(), kind="struct", base="-"):
        t = {"rel": rel, "name": name, "kind": kind, "base": base,
             "fields": list(fields), "methods": list(methods)}
        self.types.append(t)
        return t

    def path(self, name):
        t = self.find(name)
        return module_of(t["rel"]) + "::" + name if t else "fixture::" + name

    def set_field(self, name, field, ty, rel=None):
        """`field` declared on `name` with type `ty`, replacing a field of
        that name already there."""
        t = self.find(name, rel)
        for i, (f, _ty) in enumerate(t["fields"]):
            if f == field:
                t["fields"][i] = (field, ty)
                return
        t["fields"].append((field, ty))

    def drop_field(self, name, field):
        t = self.find(name)
        t["fields"] = [(f, ty) for f, ty in t["fields"] if f != field]

    def drop_file(self, rel):
        self.types = [t for t in self.types if t["rel"] != rel]
        self.extra_files.discard(rel)

    def rels(self):
        return sorted(set(t["rel"] for t in self.types if not t.get("no_file")) | self.extra_files)

    def files(self):
        """A source file per module; a type marked `no_file` has records and
        no file, as facts written from another tree would."""
        out = {rel: "" for rel in self.rels()}
        for t in self.types:
            if t.get("no_file"):
                continue
            head = "type %s %s%s {\n" % (t["kind"], t["name"],
                                         "" if t["base"] == "-" else " : " + written(t["base"]))
            body = "".join("    %s: %s;\n" % (f, written(ty)) for f, ty in t["fields"])
            for name, recv, params, ret, _trait in t["methods"]:
                sig = {"read": ["&this"], "write": ["mut &this"]}.get(recv, [])
                sig += ["%s: %s" % (p, written(ty)) for p, ty in params]
                body += "    %s%s(%s) -> %s { }\n" % ("static " if recv == "static" else "", name,
                                                     ", ".join(sig), written(ret))
            out[t["rel"]] += head + body + "}\n"
        return out

    def records(self):
        recs = []
        line = {}

        def at(rel):
            line[rel] = line.get(rel, 0) + 1
            return str(line[rel])

        for t in self.types:
            rel, path = "src/" + t["rel"], module_of(t["rel"]) + "::" + t["name"]
            recs.append("\t".join(["type", rel, at(rel), "1", str(len(t["fields"])), t["base"], "public",
                                   t["kind"], "-", "-", "-", "-", path, "-", "0"]))
            for i, (f, ty) in enumerate(t["fields"]):
                recs.append("\t".join(["field", rel, at(rel), "5", str(i), ty, "public", "-", "-",
                                       key_of(ty), "-", "-", path, f, "0"]))
            for name, recv, params, ret, trait in t["methods"]:
                role = "static" if recv == "static" else "method"
                # A trait impl's method is mangled under the trait
                # (docs/cryo-mangling-spec.md): `C$tr$<trait>$f<type>-<method>`.
                owner = _mangled(path) if trait is None else "tr$%s$f%s" % (_mangled(trait), _mangled(path))
                symbol = "C$%s-%d%s$F%s$Rv" % (owner, len(name), name,
                                               {"read": "$s", "write": "$m"}.get(recv, ""))
                n = at(rel)
                recs.append("\t".join(["fn", rel, n, "5", str(len(params)), ret, "public", role, symbol,
                                       "-", "-", "-", path, name, "0"]))
                for i, (p, ty) in enumerate(params):
                    recs.append("\t".join(["param", rel, n, str(10 + i), str(i), ty, "public", role, symbol,
                                           key_of(ty), "-", "-", path, name + ":" + p, "0"]))
        return recs


def M(name, recv, params=(), ret="void", trait=None):
    """A method: `recv` is `read` (`&this`), `write` (`mut &this`) or
    `static`; `params` are (name, type) pairs; `trait` is the path of the
    trait it implements, for a method of a trait impl."""
    return (name, recv, list(params), ret, trait)


# The smallest tree the gate accepts: every store present, each owning a map
# (rule 1's candidate test) and one name-keyed method (the index with the
# four LOOKUP names, the arena with get_qualified_name - the parser's own
# controls), a context carrying them, a stub per exclusion, and one caller
# exercising one call per baseline row.
BASE_FX = Fixture()
BASE_FX.declare("compiler/decl_index.cryo", "DeclarationIndex",
                [("entries", TREF + "[]"), ("type_map", map_of(TREF))],
                [M("lookup_type", "read", [("name", SYM)], TREF),
                 M("lookup_func_type", "read", [("name", SYM)], TREF),
                 M("lookup_method_return", "read", [("type_sym", SYM), ("method_sym", SYM)], TREF),
                 M("register_type", "write", [("qualified_name", SYM), ("ty", TREF)]),
                 M("entry_at", "read", [("i", "i64")], TREF)])
BASE_FX.declare("compiler/sema/type_utils.cryo", "TypeUtils",
                [("ctx", "compiler::compilation_context::CompilationContext*")],
                [M("lookup_type_exact", "read", [("name", SYM)], TREF)])
BASE_FX.declare("compiler/types/arena.cryo", "TypeArena",
                [("names", SYM + "[]"), ("struct_cache", map_of(TREF))],
                [M("lookup_by_name", "read", [("name", SYM)], TREF),
                 M("get_qualified_name", "read", [("ty", TREF)], SYM),
                 M("create_struct", "write", [("qualified_name", SYM), ("module_name", SYM)], TREF),
                 M("lookup", "read", [("id", "u64")], "compiler::types::type_base::Type*")])
BASE_FX.declare("compiler/types/generic_registry.cryo", "GenericRegistry",
                [("arena", "compiler::types::arena::TypeArena*"), ("name_index", MAP)],
                [M("get_template", "read", [("qualified_name", SYM)],
                   "compiler::types::generic_registry::TemplateEntry*"),
                 M("register_impl_block", "write",
                   [("qualified_name", SYM), ("block", "compiler::ast::declaration::ImplBlockNode*")])])
BASE_FX.declare("compiler/module_graph.cryo", "ModuleGraph",
                [("modules", "compiler::module_graph::ModuleInfo[]"), ("name_index", map_of("u32"))],
                [M("find_module_index", "read", [("name", SYM)], "i64")])
BASE_FX.declare("compiler/const_table.cryo", "ConstantTable",
                [("entries", "compiler::const_table::ConstEntry[]"), ("by_qualified", MAP)],
                [M("register", "write", [("qualified", SYM), ("init", "compiler::ast::expression::ExpressionNode*")])])
BASE_FX.declare("compiler/resolver/resolver.cryo", "Resolver",
                [("scopes", "compiler::resolver::scope::Scope[]"), ("module_scopes", map_of("u64"))],
                [M("lookup", "read", [("name", SYM), ("start", "compiler::resolver::scope::ScopeID")],
                   "compiler::resolver::symbol_id::SymbolID"),
                 M("set_module", "write", [("name", SYM)])])
BASE_FX.declare("compiler/resolver/name_resolution.cryo", "NameResolver",
                [("ctx", "compiler::compilation_context::CompilationContext*")],
                [M("bind", "write", [("name", SYM)])])
BASE_FX.declare("compiler/compilation_context.cryo", "CompilationContext",
                [("decl_index", "compiler::decl_index::DeclarationIndex*"),
                 ("type_arena", "compiler::types::arena::TypeArena*"),
                 ("generic_registry", "compiler::types::generic_registry::GenericRegistry*"),
                 ("module_graph", "compiler::module_graph::ModuleGraph*"),
                 ("const_table", "compiler::const_table::ConstantTable*"),
                 ("resolver", "compiler::resolver::resolver::Resolver*")],
                [M("get_resolver", "read", [], "compiler::resolver::resolver::Resolver*"),
                 M("get_arena", "read", [], "compiler::types::arena::TypeArena*")])
BASE_FX.declare("compiler/sema/scope_manager.cryo", "ScopeManager",
                [("scopes", "compiler::resolver::scope::Scope[]")],
                [M("lookup_type", "read", [("name", SYM)], TREF)])
BASE_FX.declare("compiler/sema/sema.cryo", "Sema",
                [("ctx", "compiler::compilation_context::CompilationContext*"),
                 ("types", "compiler::sema::type_utils::TypeUtils"),
                 ("scopes", "compiler::sema::scope_manager::ScopeManager")],
                [M("walk", "read", [("name", SYM), ("t", TREF)])])

# One stub per exclusion the gate lists, in the file the gate names, owning a
# map: rule 1 requires every listed type to be in the tree with its map, so
# the fixture cannot accept the gate's table without carrying it.  A stub may
# share its file with a fixture store (the resolver's rib sits beside the
# resolver), never a name with a type already declared there.
for _name, _ex in GATE_MOD.EXCLUDED.items():
    assert BASE_FX.find(_name, _ex.defn) is None, "an exclusion twins a fixture type: %s in %s" % (_name, _ex.defn)
    BASE_FX.declare(_ex.defn, _name, [("table", MAP)])

# One stub per ARRAY exclusion (rule 1b), each a candidate as the gate
# defines one: no map, an array of names, a method taking a name.  Several
# share a file with each other or with a fixture store (the context, the
# graph, the registry); a fixture type of that name already declared there
# becomes the candidate itself rather than gaining a twin.
ARRAY_OWN_FILE = []
for _name, _ex in sorted(GATE_MOD.EXCLUDED_ARRAYS.items()):
    _t = BASE_FX.find(_name, _ex.defn)
    if _t is None:
        if _ex.defn not in BASE_FX.rels():
            ARRAY_OWN_FILE.append(_name)
        _t = BASE_FX.declare(_ex.defn, _name)
    _t["fields"].append(("names", SYM + "[]"))
    _t["methods"].append(M("index_of", "read", [("name", SYM)], "i64"))


def elem_prov(owner, field, elem, member="name", at="element@1:", base="param:o"):
    """Where a compared value comes from when it is read off an element of
    `owner.field` (a local array when `owner` is LOCAL): `.member` of a
    record element, the element itself when `member` is None."""
    if owner == GATE_MOD.LOCAL:
        src = at + "local:mut xs@0=expr:ArrayLiteral"
    else:
        src = "%sfield:fixture::%s*.%s<-%s" % (at, owner, field, base)
    if member is None:
        return src
    return "field:fixture::%s*.%s<-%s" % (elem, member, src)


def cmp_rec(rel, line, left, right="param:name", depth=1, ktype=SYM):
    """A `cmp` record: `left == right` at loop depth `depth`."""
    return "\t".join(["cmp", "src/" + rel, str(line), "9", "==", ktype, right, left,
                      "-", "-", "-", "-", "-", "-", str(depth)])


def eq_rec(rel, line, recv, arg="param:name", depth=1):
    """The `arg` record of `recv.equals(arg)` on a SymbolStr."""
    return "\t".join(["arg", "src/" + rel, str(line), "9", "0", SYM, arg, recv,
                      "C$fixture$SymbolStr-equals", "call", "-", "-", KEY_EQ, "-", str(depth)])


def id_rec(rel, line, recv, depth=1):
    """The `call` record of `recv.equals(t)` on a TypeRef."""
    return "\t".join(["call", "src/" + rel, str(line), "9", "1", "-", "-", recv,
                      "C$fixture$TypeRef-equals", "call", "-", "-", TREF_EQ, "-", str(depth)])


SEMA_FILE = "compiler/sema/sema.cryo"


def scan_rec(label, line):
    """The facts record of a scan of the entry `label` by its element's key,
    in the owner's own file (a local table's in sema's)."""
    sc = GATE_MOD.SCANNED_ARRAYS[label]
    owner, field = label.split(".", 1)
    rel = SEMA_FILE if owner == GATE_MOD.LOCAL else sc.defn
    if sc.kind == GATE_MOD.IDENTITY:
        return id_rec(rel, line, elem_prov(owner, field, sc.elem, "key"))
    if sc.elem in GATE_MOD.KEY_TYPES:
        return cmp_rec(rel, line, elem_prov(owner, sc.elem if owner == GATE_MOD.LOCAL else field, sc.elem, None),
                       ktype=KEY_TYPE_TEXT[sc.elem])
    return cmp_rec(rel, line, elem_prov(owner, field, sc.elem))


# One stub per SCANNED array (rule 1c): the element type with a `name`
# key and the owner with the array, declared where the gate's table says;
# the scan itself is a facts record (SCAN_FACTS), in the owner's own file so
# the residue, which shares this fixture, sees no site.  Rule 1c refuses an
# entry nothing scans, so the fixture cannot accept the gate's table without
# carrying every one.
SCAN_ELEMS_FILE = "compiler/scan_elems.cryo"
SCAN_FACTS = []
for _label, _sc in sorted(GATE_MOD.SCANNED_ARRAYS.items()):
    _owner, _field = _label.split(".", 1)
    if _sc.elem not in GATE_MOD.KEY_TYPES:
        if BASE_FX.find(_sc.elem) is None:
            BASE_FX.declare(SCAN_ELEMS_FILE, _sc.elem)
        BASE_FX.set_field(_sc.elem, "name", SYM)
    if _sc.kind == GATE_MOD.IDENTITY:
        BASE_FX.set_field(_sc.elem, "key", TREF)
    SCAN_FACTS.append(scan_rec(_label, 900 + len(SCAN_FACTS)))
    if _owner == GATE_MOD.LOCAL:
        continue
    # A map owner's key arrays hold interned ids (`u32[]`), compared with a
    # key's `.id`, as the tree's do; declared as the key type they would
    # make the owner a record carrying a name.
    if _sc.elem in GATE_MOD.KEY_TYPES:
        _elem_ty = "u32" if (_owner in GATE_MOD.EXCLUDED or _owner in GATE_MOD.STORES) \
            else KEY_TYPE_TEXT[_sc.elem]
    else:
        _elem_ty = BASE_FX.path(_sc.elem)
    if BASE_FX.find(_owner, _sc.defn) is None:
        BASE_FX.declare(_sc.defn, _owner)
    BASE_FX.set_field(_owner, _field, _elem_ty + "[]", _sc.defn)

# The sealed-type check reads the compiler's declaration records, so each
# SEALED type is declared in the base facts: its `type` record and one private
# field.  The first one is the sealed-type cases' subject.
SEALED_FILE = "src/compiler/sealed.cryo"
BASE_FX.extra_files.add(SEALED_FILE[len("src/"):])


def type_rec(path, line):
    return "\t".join(["type", SEALED_FILE, str(line), "1", "1", "-", "public", "struct",
                      "-", "-", "-", "-", path, "-", "0"])


def field_rec(path, line, vis="private"):
    return "\t".join(["field", SEALED_FILE, str(line), "5", "0", "u32", vis, "-",
                      "-", "-", "-", "-", path, "slot", "0"])


def fn_rec(path, name, params, ret, vis="public", role="static", rel=SEALED_FILE, line=90):
    return "\t".join(["fn", rel, str(line), "5", str(params), ret, vis, role,
                      "C$sym", "-", "-", "-", path, name, "0"])


SEALED_FACTS = []
for _i, _path in enumerate(sorted(GATE_MOD.SEALED_TYPES)):
    SEALED_FACTS += [type_rec(_path, 10 * _i + 1), field_rec(_path, 10 * _i + 3)]
FIRST_SEALED = sorted(GATE_MOD.SEALED_TYPES)[0]
FIRST_STORE = sorted(GATE_MOD.SEALED_STORES)[0]

# The first scanned array in the gate's table held by a declared owner whose
# element is a record, for the stale-entry, element, file and derived-class
# cases.
FIRST_SCANNED = sorted(l for l, sc in GATE_MOD.SCANNED_ARRAYS.items()
                       if sc.kind == GATE_MOD.DATA and not l.startswith(GATE_MOD.LOCAL + ".")
                       and sc.elem not in GATE_MOD.KEY_TYPES
                       and not any(l.split(".")[0] in t for t in (GATE_MOD.STORES, GATE_MOD.EXCLUDED,
                                                                    GATE_MOD.EXCLUDED_ARRAYS)))[0]
_SC_OWNER, _SC_FIELD = FIRST_SCANNED.split(".", 1)
_SC = GATE_MOD.SCANNED_ARRAYS[FIRST_SCANNED]
assert (_SC_FIELD, BASE_FX.path(_SC.elem) + "[]") in BASE_FX.find(_SC_OWNER, _SC.defn)["fields"], FIRST_SCANNED
# The first array keyed by identity, for the two identity cases: a scan of
# it by name, and the identity scan gone.
FIRST_IDENTITY = sorted(l for l, sc in GATE_MOD.SCANNED_ARRAYS.items() if sc.kind == GATE_MOD.IDENTITY)[0]
_ID_OWNER, _ID_FIELD = FIRST_IDENTITY.split(".", 1)
_ID_ELEM = GATE_MOD.SCANNED_ARRAYS[FIRST_IDENTITY].elem

# LOOKUP is 2: the caller's one, and the funnel's call INTO the index in
# type_utils.cryo, which is counted - only a store's own file is excluded
# from its own set.  ARENA_READ is 1: `lookup_by_name`; `get_qualified_name`
# is asked by an identity and is not counted.
BASELINE = {
    "LOOKUP": 2, "LOOKUP_OTHER": 0, "REGISTER": 0, "LOOKUP_ROUTED": 1,
    "LOOKUP_LOCAL": 1, "ARENA_READ": 1, "ARENA_WRITE": 0,
    "REGISTRY_READ": 0, "REGISTRY_WRITE": 0, "GRAPH_READ": 0, "GRAPH_WRITE": 0,
    "CONST_READ": 0, "CONST_WRITE": 0,
    "REENTRY": 0, "HOME_WRITE": 0,
    "DEFID_PATH": 0,
}
# The first exclusion in the gate's table, for the stale-exclusion mutation.
FIRST_EXCLUDED = sorted(GATE_MOD.EXCLUDED)[0]
# The first ARRAY exclusion declared in a file of its own, for the stale and
# gained-a-map mutations.
FIRST_ARRAY_EXCLUDED = sorted(ARRAY_OWN_FILE)[0]


# The compiler's report of the fixture's calls: one `call` record per call,
# as `cryo build --emit=facts` writes it - the declaration each call was
# bound to, as its linker symbol and its rendered text.  The counting rules
# read only these; the declarations above feed the rules that read
# declarations, and are appended to every case's facts from the case's own
# fixture.
INDEX = "compiler::decl_index::DeclarationIndex"
FUNNEL = "compiler::sema::type_utils::TypeUtils"
ARENA = "compiler::types::arena::TypeArena"
REGISTRY = "compiler::types::generic_registry::GenericRegistry"
GRAPH = "compiler::module_graph::ModuleGraph"
CONSTS = "compiler::const_table::ConstantTable"
RESOLVER = "compiler::resolver::resolver::Resolver"
CONTEXT = "compiler::compilation_context::CompilationContext"
SCOPES = "compiler::sema::scope_manager::ScopeManager"


def call(rel, line, owner, meth, recv, params, ret="void", pin="call", col=9):
    """A `call` record for a call to method `meth` of `owner` - `recv` is
    `read` (`&this`), `write` (`mut &this`) or `static` - or, with `owner`
    None, to the free function `meth` (a path)."""
    receiver = {"read": ["&this"], "write": ["mut &this"]}.get(recv, [])
    if owner is None:
        text = "%s(%s) -> %s" % (meth, ", ".join(params), ret)
        symbol = "C$%s$F%s$Rv" % (_mangled(meth), "_".join("N" for _ in params) or "v")
    else:
        sep = "::" if recv == "static" else "."
        text = "%s%s%s(%s) -> %s" % (owner, sep, meth, ", ".join(receiver + params), ret)
        marker = {"read": "$s", "write": "$m"}.get(recv, "")
        symbol = "C$%s-%d%s$F%s$Rv" % (_mangled(owner.split("<")[0]), len(meth), meth, marker)
    if pin == "none":
        symbol, text = "?", "." + meth
    return "\t".join(["call", "src/" + rel, str(line), str(col), str(len(params)), "-", "-", "-",
                      symbol, pin, "-", "-", text, "-", "0"])


BASE_FACTS = [
    call(SEMA_FILE, 7, INDEX, "lookup_type", "read", [SYM], TREF),
    call(SEMA_FILE, 8, FUNNEL, "lookup_type_exact", "read", [SYM], TREF),
    call(SEMA_FILE, 9, SCOPES, "lookup_type", "read", [SYM], TREF),
    call(SEMA_FILE, 10, ARENA, "lookup_by_name", "read", [SYM], TREF),
    # Asked by an identity, a name handed back: reaches the store, not counted.
    call(SEMA_FILE, 12, ARENA, "get_qualified_name", "read", [TREF], SYM),
    # A store method whose signature names no key: not counted.
    call(SEMA_FILE, 13, ARENA, "lookup", "read", ["u64"], "compiler::types::type_base::Type*"),
    # The funnel's call INTO the index: counted, only a store's own file is not.
    call("compiler/sema/type_utils.cryo", 5, INDEX, "lookup_type", "read", [SYM], TREF),
    # The resolver's owners asking it: not re-entries.
    call("compiler/resolver/name_resolution.cryo", 5, RESOLVER, "lookup", "read",
         [SYM, "compiler::resolver::scope::ScopeID"], "compiler::resolver::symbol_id::SymbolID"),
    call("compiler/resolver/name_resolution.cryo", 6, RESOLVER, "set_module", "write", [SYM]),
    # A store's calls from its own file: not counted, but they reach it.
    call("compiler/types/generic_registry.cryo", 6, REGISTRY, "get_template", "read", [SYM],
         "compiler::types::generic_registry::TemplateEntry*"),
    call("compiler/module_graph.cryo", 6, GRAPH, "find_module_index", "read", [SYM], "i64"),
    call("compiler/const_table.cryo", 6, CONSTS, "register", "write",
         [SYM, "compiler::ast::expression::ExpressionNode*"]),
] + SCAN_FACTS + SEALED_FACTS

# The base tree as files and as declaration records, for a suite that
# shares this fixture (the residue's).
FILES = BASE_FX.files()
DECL_FACTS = BASE_FX.records()


def facts_without(record):
    assert record in BASE_FACTS
    return [r for r in BASE_FACTS if r != record]


def facts_with(extra):
    return BASE_FACTS + extra


# (name, fixture change or None, facts records, expected exit,
# must-appear-in-output).  Each counting rule has a case that fails when the
# rule is removed: the key found in a parameter, in a return, inside a
# generic argument, and not in `String`; the member test; the receiver's kind
# choosing the row; the three doors; the owner's own file; the same-named
# method on another type; the unpinned call; the reach control; one call
# recorded twice; a record from outside the tree; a generic owner.
COUNT_CASES = [
    ("a reader keyed by `string` is inside the rule", None,
     facts_with([call(SEMA_FILE, 20, INDEX, "by_spelling", "read", ["string"], TREF)]),
     1, "LOOKUP_OTHER TOTAL 0 -> 1"),
    ("a reader asked by an identity that hands a name back is a read by identity: accepted", None,
     facts_with([call(SEMA_FILE, 20, GRAPH, "name_of", "read", ["u32"], SYM)]),
     0, "lane-gate: OK"),
    ("the same store's reader asked BY a name is counted", None,
     facts_with([call(SEMA_FILE, 20, GRAPH, "id_named", "read", [SYM], "u32")]),
     1, "GRAPH_READ TOTAL 0 -> 1"),
    ("a key inside a generic argument is a key", None,
     facts_with([call(SEMA_FILE, 20, REGISTRY, "templates_named", "read",
                      ["std::core::option::Option<%s>" % SYM], "u32")]),
     1, "REGISTRY_READ TOTAL 0 -> 1"),
    ("a function-typed parameter's own return is inside the parameter list", None,
     facts_with([call(SEMA_FILE, 20, REGISTRY, "visit_named", "read",
                      ["(u32) -> %s" % SYM], "u32")]),
     1, "REGISTRY_READ TOTAL 0 -> 1"),
    ("a `std::collections::string::String` is text, not the key type `string`: accepted", None,
     facts_with([call(SEMA_FILE, 20, INDEX, "describe", "read", ["std::collections::string::String"])]),
     0, "lane-gate: OK"),
    ("a `mut &this` store method lands in the store's WRITE row", None,
     facts_with([call(SEMA_FILE, 20, CONSTS, "register", "write",
                      [SYM, "compiler::ast::expression::ExpressionNode*"])]),
     1, "CONST_WRITE TOTAL 0 -> 1"),
    ("a static taking a name lands in the store's READ row", None,
     facts_with([call(SEMA_FILE, 20, INDEX, "parse_key", "static", ["string"], TREF)]),
     1, "LOOKUP_OTHER TOTAL 0 -> 1"),
    ("a free function whose path reads like a store's static is not a store call: accepted", None,
     facts_with([call(SEMA_FILE, 20, None, GRAPH + "::find", "static", ["string"], "i64")]),
     0, "lane-gate: OK"),
    ("a per-kind lookup on the index is the LOOKUP row", None,
     facts_with([call(SEMA_FILE, 20, INDEX, "lookup_func_type", "read", [SYM], TREF)]),
     1, "LOOKUP TOTAL 2 -> 3"),
    ("the resolver asked by name outside its owners is a re-entry", None,
     facts_with([call(SEMA_FILE, 20, RESOLVER, "lookup", "read",
                      [SYM, "compiler::resolver::scope::ScopeID"], "compiler::resolver::symbol_id::SymbolID")]),
     1, "REENTRY TOTAL 0 -> 1"),
    ("get_resolver() outside the driver is a re-entry", None,
     facts_with([call(SEMA_FILE, 20, CONTEXT, "get_resolver", "read", [], RESOLVER + "*")]),
     1, "REENTRY TOTAL 0 -> 1"),
    ("get_resolver() inside the driver is not: accepted", None,
     facts_with([call("compiler/compilation_context.cryo", 20, CONTEXT, "get_resolver", "read", [],
                      RESOLVER + "*")]),
     0, "lane-gate: OK"),
    ("DefTable::path_of is the identity turned back into a name", None,
     facts_with([call(SEMA_FILE, 20, "compiler::resolver::res::DefTable", "path_of", "read",
                      ["compiler::resolver::res::DefId"], SYM)]),
     1, "DEFID_PATH TOTAL 0 -> 1"),
    ("a module handed to a resolution context is a home write", None,
     facts_with([call(SEMA_FILE, 20, "compiler::types::resolver::ResolutionContext", "set_home_module",
                      "write", [SYM])]),
     1, "HOME_WRITE TOTAL 0 -> 1"),
    ("a store's own calls are not the surface: accepted", None,
     facts_with([call("compiler/decl_index.cryo", 20, INDEX, "lookup_type", "read", [SYM], TREF)]),
     0, "lane-gate: OK"),
    ("a same-named method on another type is LOOKUP_LOCAL", None,
     facts_with([call(SEMA_FILE, 20, "compiler::sema::sema::Sema", "get_template", "read", [SYM])]),
     1, "LOOKUP_LOCAL TOTAL 1 -> 2"),
    ("a method on another type named like no store method is not counted: accepted", None,
     facts_with([call(SEMA_FILE, 20, "compiler::sema::sema::Sema", "walk", "read", [SYM])]),
     0, "lane-gate: OK"),
    ("a decrease is refused too (the ceiling must be re-pinned deliberately)", None,
     [r for r in BASE_FACTS if "\t7\t9\t" not in r],
     1, "LOOKUP TOTAL 2 -> 1"),
    ("a call the compiler left unpinned, spelled like a counted method, is refused", None,
     facts_with([call(SEMA_FILE, 20, None, "lookup_type", "read", [SYM], pin="none")]),
     1, "left unpinned"),
    ("an unpinned call spelled like nothing counted is not a store call: accepted", None,
     facts_with([call(SEMA_FILE, 20, None, "push", "read", ["T"], pin="none")]),
     0, "lane-gate: OK"),
    ("a store no call reaches is refused: the reader cannot place calls on it", None,
     [r for r in BASE_FACTS if "const_table" not in r],
     1, "reaches ConstantTable"),
    ("one call recorded twice at one site is one call: accepted", None,
     facts_with([BASE_FACTS[0]]),
     0, "lane-gate: OK"),
    ("a call from outside the tree is not counted: accepted", None,
     facts_with([BASE_FACTS[0].replace("src/compiler/sema/sema.cryo", "<stdlib>/core/str.cryo")]),
     0, "lane-gate: OK"),
    ("a generic owner's rendered arguments do not hide the store", None,
     facts_with([call(SEMA_FILE, 20, GRAPH + "<T>", "find_module_index", "read", [SYM], "i64")]),
     1, "GRAPH_READ TOTAL 0 -> 1"),
    ("a record of another shape is refused: the reader and the format disagree", None,
     facts_with(["call\tsrc/compiler/sema/sema.cryo\t20\t9"]),
     1, "a record with 4 fields, not 15"),
]


def scan_fact_of(label):
    """The base facts' scan record of the entry `label`."""
    owner, field = label.split(".", 1)
    needle = "field:fixture::%s*.%s<-" % (owner, field)
    hits = [r for r in SCAN_FACTS if needle in r]
    assert len(hits) == 1, (label, hits)
    return hits[0]


FIELD_TABLE = "compiler/types/field_table.cryo"


def slot_tree(fx):
    """A record array in neither table, for the rule 1c cases."""
    fx.declare(FIELD_TABLE, "SlotDecl", [("name", SYM)])
    fx.declare(FIELD_TABLE, "SlotTable", [("rows", "compiler::types::field_table::SlotDecl[]")])


def owner_moved(fx):
    """The first scanned entry's owner renamed where the table says it is,
    and declared again, with the array, in another file."""
    fx.find(_SC_OWNER, _SC.defn)["name"] = _SC_OWNER + "Moved"
    fx.declare(FIELD_TABLE, _SC_OWNER, [(_SC_FIELD, BASE_FX.path(_SC.elem) + "[]")])


SLOT_ROW = "field:fixture::SlotDecl*.name<-%sfield:fixture::SlotTable*.rows<-param:st"
SLOT_REFUSED = "`SlotTable.rows` (SlotDecl[]) is scanned inline by a key (compiler/sema/sema.cryo:%d, 1 site) and is not in"

# Rule 1c: the door's loop written at the caller, read from the compiler's
# report of each comparison.  Eleven such loops stood in the compiler under
# rules 1-1b reading OK, because a scan is no method call and the array's
# owner had no name-taking method to make it a candidate.  (name, fixture
# change, facts, expected exit, must-appear-in-output); each rule has a case
# that fails when the rule is removed from the gate.
RULE_1C_CASES = [
    ("a record array scanned by its element's name at the innermost loop, in neither table, is refused",
     slot_tree, facts_with([cmp_rec(SEMA_FILE, 15, SLOT_ROW % "element@1:")]),
     1, SLOT_REFUSED % 15),
    ("the same scan written `row.name.equals(name)` through a local bound to the element, refused",
     slot_tree, facts_with([eq_rec(SEMA_FILE, 16, SLOT_ROW.replace("<-%s", "<-local:row@1=%s") % "element@1:")]),
     1, SLOT_REFUSED % 16),
    ("a local table whose key is read through two locals (`w = ws[i]; d = w.decl; d.name`) is placed by "
     "the element's type, and refused when placed nowhere",
     None, facts_with([cmp_rec(SEMA_FILE, 25, "field:fixture::SlotDecl*.name<-local:d@1=field:fixture::SlotWrap*.decl"
                                              "<-local:w@1=element@1:param:ws")]),
     1, "`local.SlotWrap` (SlotWrap[]) is scanned inline by a key (compiler/sema/sema.cryo:25, 1 site) and is not in"),
    ("an element bound outside the innermost loop is the key searched for, not the array searched: accepted",
     slot_tree, facts_with([cmp_rec(SEMA_FILE, 17, SLOT_ROW.replace("<-%s", "<-local:row@1=%s") % "element@1:",
                                    depth=2)]),
     0, "lane-gate: OK"),
    ("an element indexed by an outer loop's counter is the key searched for: accepted",
     slot_tree, facts_with([cmp_rec(SEMA_FILE, 18, SLOT_ROW % "element@1:", depth=2)]),
     0, "lane-gate: OK"),
    ("an element read outside every loop is read by position, not searched: accepted",
     slot_tree, facts_with([cmp_rec(SEMA_FILE, 19, SLOT_ROW % "element:", depth=0)]),
     0, "lane-gate: OK"),
    ("a value that passed through a call is the call's, not the element's: accepted",
     slot_tree, facts_with([cmp_rec(SEMA_FILE, 20, "call:fixture::f(string) -> string of " + SLOT_ROW % "element@1:")]),
     0, "lane-gate: OK"),
    ("a join - both operands elements the innermost loop reads - is a read of each: the unplaced side is refused",
     slot_tree, facts_with([cmp_rec(SEMA_FILE, 21, SLOT_ROW % "element@1:",
                                    right=elem_prov(_SC_OWNER, _SC_FIELD, _SC.elem))]),
     1, SLOT_REFUSED % 21),
    ("an array read through a derived class's value is the declaring base's: accepted where the base is placed",
     lambda fx: fx.declare(FIELD_TABLE, "DerivedOwner", kind="class", base=BASE_FX.path(_SC_OWNER)),
     facts_with([cmp_rec(SEMA_FILE, 22, elem_prov("DerivedOwner", _SC_FIELD, _SC.elem))]),
     0, "lane-gate: OK"),
    ("an entry whose element the compiler reports differently is refused",
     None, facts_with([cmp_rec(SEMA_FILE, 23, elem_prov(_SC_OWNER, _SC_FIELD, "OtherElem"))]),
     1, "`%s` is listed in SCANNED_ARRAYS with element `%s` but the compiler reports" % (FIRST_SCANNED, _SC.elem)),
    ("an entry whose owner is declared in another file than the table says is refused",
     owner_moved, BASE_FACTS,
     1, "`%s` is listed in SCANNED_ARRAYS at %s but `%s` is declared in "
        % (FIRST_SCANNED, _SC.defn, _SC_OWNER)),
    ("an entry whose owner no longer declares the array is refused",
     lambda fx: fx.drop_field(_SC_OWNER, _SC_FIELD), BASE_FACTS,
     1, "without a field `%s`" % _SC_FIELD),
    ("a scanned-array entry nothing scans any more is a stale entry, refused",
     None, facts_without(scan_fact_of(FIRST_SCANNED)),
     1, "`%s` is listed in SCANNED_ARRAYS but nothing in the tree scans it inline: a stale entry" % FIRST_SCANNED),
    ("an array keyed by identity, scanned by a name, is refused though it is listed",
     None, facts_with([cmp_rec(SEMA_FILE, 24, elem_prov(_ID_OWNER, _ID_FIELD, _ID_ELEM))]),
     1, "`%s` is listed in SCANNED_ARRAYS as keyed by identity (DefId / TypeRef) but is scanned by a name"
        % FIRST_IDENTITY),
    ("an array keyed by identity that nothing scans by identity any more is a stale entry, refused",
     None, facts_without(scan_fact_of(FIRST_IDENTITY)),
     1, "`%s` is listed in SCANNED_ARRAYS as keyed by identity but nothing in the tree scans it by an identity"
        % FIRST_IDENTITY),
]


def add_context_field(field, ty):
    return lambda fx: fx.set_field("CompilationContext", field, ty)


def both(*changes):
    def run(fx):
        for c in changes:
            c(fx)
    return run


def set_fields(name, fields, rel=None):
    def run(fx):
        fx.find(name, rel)["fields"] = list(fields)
    return run


# (name, fixture change, expected exit, must-appear-in-output)
MUTATIONS = [
    ("a missing store definition is refused",
     lambda fx: fx.drop_file("compiler/const_table.cryo"),
     1, "no compiler/const_table.cryo"),
    # Rule 1: the store list is derived from the tree's map owners.
    ("the audit's mutation: a new map-keyed type carried by the context, with a "
     "reader, is a map owner in neither table and is refused",
     both(lambda fx: fx.declare("compiler/impl_index.cryo", "ImplIndex", [("owners", MAP)],
                                [M("owner_of", "read", [("name", SYM)], "i64")]),
          add_context_field("impl_index", "compiler::impl_index::ImplIndex*")),
     1, "`ImplIndex` (compiler/impl_index.cryo) owns a map (owners: HashMap<u32>) and is in neither STORES nor EXCLUDED"),
    ("a map owner nothing carries is refused the same way: the map is the test, not the context",
     lambda fx: fx.declare("compiler/sema/leaf_cache.cryo", "LeafCache", [("by_leaf", map_of(TREF))]),
     1, "`LeafCache` (compiler/sema/leaf_cache.cryo) owns a map"),
    ("a store whose map is gone is a stale entry, refused",
     set_fields("ModuleGraph", [("modules", "compiler::module_graph::ModuleInfo[]")]),
     1, "`ModuleGraph` is listed in STORES but owns no map: a stale entry"),
    ("an exclusion whose map is gone is a stale exclusion, refused",
     set_fields(FIRST_EXCLUDED, [("items", "i64[]")]),
     1, "`%s` is listed in EXCLUDED but owns no map: a stale exclusion" % FIRST_EXCLUDED),
    ("a map owner named like a store but declared in another file is refused",
     lambda fx: fx.declare("compiler/sema/const_table.cryo", "ConstantTable", [("by_qualified", MAP)]),
     1, "`ConstantTable` is listed in STORES at compiler/const_table.cryo but declared with a map in "
        "compiler/const_table.cryo, compiler/sema/const_table.cryo"),
    ("the funnel given a map of its own is refused: a funnel holds nothing",
     lambda fx: fx.set_field("TypeUtils", "memo", map_of(TREF)),
     1, "`TypeUtils` is marked a funnel (no map of its own) but owns one"),
    # Rule 1b: a name-keyed table that is an ARRAY, not a map.  Audit 14's
    # m8 read OK under the map rule: an array-backed store on the context
    # with a linear lookup and a caller.
    ("audit 14's m8: an array-backed store (`Pair<SymbolStr, TypeRef>[]`, a linear "
     "lookup by name) carried by the context is refused",
     both(lambda fx: fx.declare("compiler/sema/leaf_table.cryo", "LeafTable",
                                [("entries", "std::collections::pair::Pair<%s, %s>[]" % (SYM, TREF))],
                                [M("lookup_leaf", "read", [("name", SYM)], TREF)]),
          add_context_field("leaf_table", "compiler::sema::leaf_table::LeafTable*")),
     1, "`LeafTable` (compiler/sema/leaf_table.cryo) owns an array of names or records (entries: Pair<SymbolStr,TypeRef>[]), "
        "takes a name (lookup_leaf) and is in"),
    ("an array of bare names with a linear search is refused the same way, carried by nothing",
     lambda fx: fx.declare("compiler/parser/name_tables.cryo", "NameTables",
                           [("generic_decl_names", SYM + "[]")],
                           [M("is_generic_decl_name", "read", [("name", SYM)], "boolean")]),
     1, "`NameTables` (compiler/parser/name_tables.cryo) owns an array of names or records (generic_decl_names: SymbolStr[]), "
        "takes a name (is_generic_decl_name) and is in"),
    ("an array of `string` names asked by a `string` is the same table: refused",
     lambda fx: fx.declare("compiler/parser/name_tables.cryo", "NameTables",
                           [("parts", "string[]")],
                           [M("has_part", "read", [("part", "&string")], "boolean")]),
     1, "`NameTables` (compiler/parser/name_tables.cryo) owns an array of names or records (parts: string[]), "
        "takes a name (has_part) and is in"),
    ("an array of names nothing asks by name is data, not a table: accepted",
     lambda fx: fx.declare("compiler/parser/name_tables.cryo", "NameTables",
                           [("generic_decl_names", SYM + "[]")],
                           [M("count", "read", [], "i64")]),
     0, "lane-gate: OK"),
    ("a method taking an ARRAY of names is not a name-keyed ask: accepted",
     lambda fx: fx.declare("compiler/parser/name_tables.cryo", "NameTables",
                           [("generic_decl_names", SYM + "[]")],
                           [M("replace", "write", [("names", SYM + "[]")])]),
     0, "lane-gate: OK"),
    ("a trait impl's method taking a name is the trait's signature, not the type's: accepted",
     lambda fx: fx.declare("compiler/parser/name_tables.cryo", "NameTables",
                           [("generic_decl_names", SYM + "[]")],
                           [M("find", "read", [("name", SYM)], "boolean", trait="compiler::parser::Find")]),
     0, "lane-gate: OK"),
    # Rule 1b over RECORDS: the verification's m4 - a type owning an array of
    # records that carry a name field, searched by `rows[i].name.equals(n)`,
    # is neither a map owner nor a name-array owner and read OK.
    ("the verification's m4: a table of records with a key-typed field (`FieldInfo[]`, a linear "
     "search by the record's name) with a name-taking reader is refused as an unplaced candidate",
     both(lambda fx: fx.declare(FIELD_TABLE, "FieldInfo", [("name", SYM), ("ty", TREF)]),
          lambda fx: fx.declare(FIELD_TABLE, "FieldTable", [("rows", "compiler::types::field_table::FieldInfo[]")],
                                [M("find", "read", [("name", SYM)], "compiler::types::field_table::FieldInfo*")])),
     1, "`FieldTable` (compiler/types/field_table.cryo) owns an array of names or records (rows: FieldInfo[]), "
        "takes a name (find) and is in"),
    ("an array of POINTERS to such records is the same table: refused",
     both(lambda fx: fx.declare(FIELD_TABLE, "AssocTypeDeclNode", [("name", SYM)]),
          lambda fx: fx.declare(FIELD_TABLE, "AssocTable",
                                [("rows", "compiler::types::field_table::AssocTypeDeclNode*[]")],
                                [M("lookup", "read", [("name", SYM)],
                                   "compiler::types::field_table::AssocTypeDeclNode*")])),
     1, "`AssocTable` (compiler/types/field_table.cryo) owns an array of names or records (rows: AssocTypeDeclNode[]), "
        "takes a name (lookup) and is in"),
    ("an array of records with no key-typed field is data, whatever asks by name: accepted",
     both(lambda fx: fx.declare(FIELD_TABLE, "SlotInfo", [("offset", "i64"), ("ty", TREF)]),
          lambda fx: fx.declare(FIELD_TABLE, "SlotTable", [("rows", "compiler::types::field_table::SlotInfo[]")],
                                [M("find", "read", [("name", SYM)], "compiler::types::field_table::SlotInfo*")])),
     0, "lane-gate: OK"),
    ("an array exclusion whose array is gone is a stale exclusion, refused",
     lambda fx: fx.set_field(FIRST_ARRAY_EXCLUDED, "names", "i64[]"),
     1, "`%s` is listed in EXCLUDED_ARRAYS but owns no array of names or records, or takes no name: a stale exclusion"
        % FIRST_ARRAY_EXCLUDED),
    ("an array exclusion that gains a map becomes rule 1's question first: refused as an unplaced map owner",
     lambda fx: fx.set_field(FIRST_ARRAY_EXCLUDED, "by_id", MAP),
     1, "`%s` (%s) owns a map (by_id: HashMap<u32>) and is in neither STORES nor EXCLUDED"
        % (FIRST_ARRAY_EXCLUDED, GATE_MOD.EXCLUDED_ARRAYS[FIRST_ARRAY_EXCLUDED].defn)),
    ("declaration records naming a file the tree does not hold are refused: the facts are another tree's",
     lambda fx: fx.types.append(dict(fx.find("Sema"), rel="compiler/sema/gone.cryo", name="Gone", no_file=True)),
     1, "the facts declare in src/compiler/sema/gone.cryo, which is not under"),
]


def _sealed_facts(drop=None, add=()):
    return [r for r in BASE_FACTS if r != drop] + list(add)


# The sealed-type check, over the compiler's declaration records.  Each case
# is a record set; the tree is the base.  The implement-block case is the
# shape the source-reading check could not see: it read only the type's own
# block.
SEALED_CASES = [
    ("a sealed identity type whose field is public - no private field left - is refused",
     None, _sealed_facts(field_rec(FIRST_SEALED, 3), [field_rec(FIRST_SEALED, 3, vis="public")]),
     1, "`%s` (%s:1) has no private field" % (FIRST_SEALED, SEALED_FILE)),
    ("a sealed identity type publishing a static that builds one from an argument is refused",
     None, _sealed_facts(add=[fn_rec(FIRST_SEALED, "make", 1, FIRST_SEALED)]),
     1, "has a public static `make (%s:90)` taking an argument and returning it" % SEALED_FILE),
    ("the same static declared in an `implement` block in another file is refused",
     lambda fx: fx.extra_files.add("compiler/elsewhere.cryo"),
     _sealed_facts(add=[fn_rec(FIRST_SEALED, "make", 1, FIRST_SEALED, rel="src/compiler/elsewhere.cryo")]),
     1, "has a public static `make (src/compiler/elsewhere.cryo:90)`"),
    ("a private minting static, a public one taking no argument, one returning another type and a method are accepted",
     None, _sealed_facts(add=[fn_rec(FIRST_SEALED, "make", 1, FIRST_SEALED, vis="private"),
                              fn_rec(FIRST_SEALED, "none", 0, FIRST_SEALED),
                              fn_rec(FIRST_SEALED, "slot_of", 1, "u32"),
                              fn_rec(FIRST_SEALED, "with_slot", 1, FIRST_SEALED, role="method")]),
     0, "lane-gate: OK"),
    ("a store's public constructor taking an argument is accepted",
     None, _sealed_facts(add=[fn_rec(FIRST_STORE, "new", 1, FIRST_STORE)]),
     0, "lane-gate: OK"),
    ("a sealed-type entry the compiler no longer declares is stale, refused",
     None, _sealed_facts(type_rec(FIRST_SEALED, 1)),
     1, "`%s` is listed in SEALED_TYPES but the compiler declares no such type" % FIRST_SEALED),
]


def write_tree(base, files):
    for rel, content in files.items():
        path = os.path.join(base, rel.replace("/", os.sep))
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w", encoding="utf-8", newline="\n") as fh:
            fh.write(content)


def write_facts(path, records):
    with open(path, "w", encoding="utf-8", newline="\n") as fh:
        for r in records:
            fh.write(r + "\n")


def run_gate(src, golden, facts, *extra):
    p = subprocess.run([sys.executable, GATE, "--src", src, "--golden", golden, "--facts", facts]
                       + list(extra),
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    return p.returncode, p.stdout


def main():
    work = tempfile.mkdtemp(prefix="lane-gate-selftest-")
    failures = []
    try:
        base = os.path.join(work, "base")
        golden = os.path.join(work, "golden.txt")
        facts = os.path.join(work, "base.facts")
        write_tree(base, FILES)
        write_facts(facts, BASE_FACTS + DECL_FACTS)

        # The baseline: --update pins exactly the numbers the facts were
        # written to produce, and the gate then reads OK against them.
        code, out = run_gate(base, golden, facts, "--update")
        if code != 0:
            failures.append("baseline --update exited %d:\n%s" % (code, out))
        for kind, n in BASELINE.items():
            want = "%s = %d" % (kind, n)
            if want not in out:
                failures.append("baseline: expected `%s` in --update output:\n%s" % (want, out))
        code, out = run_gate(base, golden, facts)
        if code != 0 or "lane-gate: OK" not in out:
            failures.append("baseline: gate did not read OK against its own golden:\n%s" % out)
        # --names lists each store's methods that calls reach, with the
        # receiver's kind, so what the rule swept up can be read.
        code, out = run_gate(base, golden, facts, "--names")
        for want in ("GenericRegistry (1):", "ModuleGraph (1):", "write  register", "read   lookup_by_name"):
            if want not in out:
                failures.append("--names did not list `%s`:\n%s" % (want, out))
        code, out = run_gate(base, os.path.join(work, "absent.txt"), facts)
        if code != 1 or "no golden" not in out:
            failures.append("a missing golden must be refused:\n%s" % out)
        code, out = run_gate(base, golden, os.path.join(work, "absent.facts"))
        if code != 1 or "no facts at" not in out:
            failures.append("missing facts must be refused:\n%s" % out)
        # `--row` and `--rows` read the facts, never the golden: they answer
        # with no golden at all, and a bucket the gate does not count is refused.
        code, out = run_gate(base, os.path.join(work, "absent.txt"), facts, "--row", "LOOKUP")
        if code != 0 or out.strip() != "2":
            failures.append("--row LOOKUP must print the live total 2 with no golden:\n%s" % out)
        code, out = run_gate(base, os.path.join(work, "absent.txt"), facts, "--rows")
        if code != 0 or out.strip() != str(len(BASELINE)):
            failures.append("--rows must print %d:\n%s" % (len(BASELINE), out))
        code, out = run_gate(base, golden, facts, "--row", "NO_SUCH_ROW")
        if code != 1 or "no bucket named NO_SUCH_ROW" not in out:
            failures.append("--row of an unknown bucket must be refused:\n%s" % out)

        cases = ([(n, ch, BASE_FACTS, c, t) for n, ch, c, t in MUTATIONS]
                 + COUNT_CASES + RULE_1C_CASES + SEALED_CASES)
        for i, (name, change, records, want_code, want_text) in enumerate(cases):
            fx = copy.deepcopy(BASE_FX)
            if change is not None:
                change(fx)
            tree = os.path.join(work, "m%d" % i)
            write_tree(tree, fx.files())
            case_facts = os.path.join(work, "m%d.facts" % i)
            write_facts(case_facts, records + fx.records())
            code, out = run_gate(tree, golden, case_facts)
            if code != want_code or want_text not in out:
                failures.append("case %d (%s): expected exit %d with `%s`, got exit %d:\n%s"
                                % (i, name, want_code, want_text, code, out))

        # The golden's side: a section the gate does not count (a retired
        # bucket left behind) is refused, not read past.  The tree is the
        # unmutated base, so the only thing wrong is the golden.
        # With no golden the baseline has already failed, and said why.
        stale = os.path.join(work, "stale-golden.txt")
        if os.path.exists(golden):
            with open(golden, "r", encoding="utf-8") as fh:
                text = fh.read()
            with open(stale, "w", encoding="utf-8", newline="\n") as fh:
                fh.write(text + "\n[RETIRED_ROW]\nTOTAL 0\n\n")
            code, out = run_gate(base, stale, facts)
            if code != 1 or "RETIRED_ROW: golden carries a section this gate does not count" not in out:
                failures.append("a golden section the gate does not count must be refused:\n%s" % out)
    finally:
        shutil.rmtree(work, ignore_errors=True)

    if failures:
        sys.stderr.write("lane-gate-selftest: %d FAILURE(S)\n\n" % len(failures))
        for f in failures:
            sys.stderr.write(f + "\n\n")
        return 1
    print("lane-gate-selftest: OK -- baseline accepted, %d declaration mutations, %d counting cases, %d "
          "scan cases and %d sealed-type cases behaved, a stale golden section refused"
          % (len(MUTATIONS), len(COUNT_CASES), len(RULE_1C_CASES), len(SEALED_CASES)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
