"""Seal ModulePath: the type moves into module_graph.cryo (the graph, which
registers every module, is its only minter), `of` and the payload private
there; three graph doors; every outside site rewritten by kind."""
import io
import os
import re

ROOT = r"C:\Programming\apps\CryoLang"
SRC = os.path.join(ROOT, "compiler", "src", "compiler")


def load(p):
    s = io.open(p, encoding="utf-8", newline="").read()
    return s, ("\r\n" if "\r\n" in s else "\n")


def save(p, s):
    io.open(p, "w", encoding="utf-8", newline="").write(s)


def edit(p, pairs):
    s, nl = load(p)
    for a, b, n in pairs:
        a = a.replace("\n", nl)
        b = b.replace("\n", nl)
        assert s.count(a) == n, (p, s.count(a), n, a[:90])
        s = s.replace(a, b)
    save(p, s)


# --- 1. the type, in the graph's module ------------------------------------
TYPE = '''/// A module's canonical namespace path - `std::fs`, `compiler::resolver` -
/// which is the module's identity: a module and a type can never carry the
/// same name, and the graph registers every module under exactly one path.
///
/// It is a type of its own so that a door asking for a MODULE cannot be
/// handed a spelling that names something else.  It is minted only in this
/// module, by the graph that registers modules: a registered module's own
/// path (`ModuleInfo::path`), the module a source file declares
/// (`ModuleGraph::module_of_file`), and the one door that turns written
/// text into a module, `ModuleGraph::module_named`, which answers only a
/// module the graph holds.  Nothing else can build one from a spelling, so
/// a path always names a module that exists.
type struct ModulePath {
private:
    sym: SymbolStr;

    static of(sym: SymbolStr) -> ModulePath {
        return ModulePath { sym: sym };
    }

public:
    /// No module.
    static none() -> ModulePath {
        return ModulePath { sym: SymbolStr::empty() };
    }

    is_valid(&this) -> boolean {
        return this.sym.is_valid();
    }

    equals(&this, other: ModulePath) -> boolean {
        return this.sym.equals(other.sym);
    }

    /// Whether this module is `ancestor` or nested anywhere under it.  A
    /// module's children are the modules whose path continues its own past a
    /// `::` - `app::net::tcp` is under `app::net` and `app`, while
    /// `app::network` is not under `app::net`.
    is_within(&this, ancestor: ModulePath, intern: InternTable*) -> boolean {
        if (!this.is_valid() || !ancestor.is_valid()) { return false; }
        if (this.equals(ancestor)) { return true; }
        return intern.resolve(this.sym).starts_with(intern.resolve(ancestor.sym) + "::");
    }

    /// The path as interned text, for a store keyed by it and for display.
    as_sym(&this) -> SymbolStr {
        return this.sym;
    }
}

'''

p = os.path.join(SRC, "module_graph.cryo")
s, nl = load(p)
for imp in ("import compiler::resolver::module_path;" + nl,
            "import compiler::resolver::module_path::{ ModulePath };" + nl):
    assert s.count(imp) == 1, imp
    s = s.replace(imp, "")
anchor = "type struct ModuleInfo {"
i = s.index(anchor)
j = s.rfind(nl + nl, 0, i) + len(nl + nl)
s = s[:j] + TYPE.replace("\n", nl) + s[j:]
save(p, s)

edit(p, [
    # ModuleInfo: its own path
    ("""type struct ModuleInfo {
    name:           SymbolStr;""",
     """type struct ModuleInfo {
    name:           SymbolStr;""", 1),
    ("""    /// Find a module's index by its path.  Returns -1 if not found.""",
     """    /// The path of the module the namespace `ns` names, when the graph holds
    /// one under it - discovered, or bound by a C import - and none
    /// otherwise.  The one door that turns written text into a module: an
    /// import's path, an export's item, a qualified path's module part, a
    /// namespace some other store recorded.  It cannot answer a module that
    /// does not exist.
    module_named(&this, ns: SymbolStr) -> ModulePath {
        if (!ns.is_valid()) { return ModulePath::none(); }
        if (this.name_index.contains_key(&ns.id) || this.module_defs.contains_key(&ns.id)) {
            return ModulePath::of(ns);
        }
        return ModulePath::none();
    }

    /// The path of the module the source file `file` declares; none for a
    /// file the graph did not load, or one declaring no namespace.
    module_of_file(&this, file: string) -> ModulePath {
        const idx: i64 = this.find_module_by_path(file);
        if (idx < 0) { return ModulePath::none(); }
        return ModulePath::of(this.modules[idx].namespace_name);
    }

    /// Find a module's index by its path.  Returns -1 if not found.""", 1),
    ("""    /// Record the module a C import declares: `def` is the binding the name
    /// layer made for the import's alias, and `m` the namespace its
    /// declarations are declared in.
    bind_c_import_module(mut &this, m: ModulePath, def: DefId) -> void {
        this.module_defs.insert(m.as_sym().id, def);
    }""",
     """    /// Record the module a C import declares: `def` is the binding the name
    /// layer made for the import's alias, and `ns` the namespace its
    /// declarations are declared in - a module registered here, as a
    /// discovered one is in `add_module`.
    bind_c_import_module(mut &this, ns: SymbolStr, def: DefId) -> void {
        this.module_defs.insert(ns.id, def);
    }""", 1),
])

# ModuleInfo::path - find the ModuleInfo impl or add one after the struct
s, nl = load(p)
if "implement struct ModuleInfo" in s:
    k = s.index("implement struct ModuleInfo")
    k = s.index("{", k) + 1
    s = s[:k] + nl + """    /// This registered module's own path.
    path(&this) -> ModulePath {
        return ModulePath::of(this.name);
    }
""".replace("\n", nl) + s[k:]
else:
    raise SystemExit("no ModuleInfo impl block; add one")
save(p, s)

# delete the old file
os.remove(os.path.join(SRC, "resolver", "module_path.cryo"))

# --- 2. importers ----------------------------------------------------------
roots = [os.path.join(ROOT, "compiler", "src"), os.path.join(ROOT, "tools", "CryoLSP", "src")]
n = 0
for r in roots:
    for dirpath, _, files in os.walk(r):
        for f in files:
            if not f.endswith(".cryo"):
                continue
            q = os.path.join(dirpath, f)
            s, nl = load(q)
            if "compiler::resolver::module_path" not in s:
                continue
            s = s.replace("import compiler::resolver::module_path;" + nl, "")
            has_mod = ("import compiler::module_graph;" + nl) in s
            s = s.replace("import compiler::resolver::module_path::{ ModulePath };",
                          ("" if has_mod else "import compiler::module_graph;" + nl)
                          + "import compiler::module_graph::{ ModulePath };")
            assert "resolver::module_path" not in s, q
            save(q, s)
            n += 1
print("importers rewritten:", n)
