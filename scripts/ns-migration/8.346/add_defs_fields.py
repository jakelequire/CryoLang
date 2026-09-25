"""Give each store that reads a definition's path a pointer to the
compilation's DefTable, set by the context that owns both.  Run once."""
import re

SPECS = [
    # (file, struct head line, constructor literal line, extra fields)
    ("compiler/src/compiler/module_graph.cryo", "type struct ModuleGraph {", "        return ModuleGraph {", []),
    ("compiler/src/compiler/decl_index.cryo", "type struct DeclarationIndex {", "        return DeclarationIndex {", []),
    ("compiler/src/compiler/const_table.cryo", "type struct ConstantTable {", "        return ConstantTable {", []),
    ("compiler/src/compiler/types/arena.cryo", "type struct TypeArena {", "        mut arena: TypeArena = TypeArena {", []),
    ("compiler/src/compiler/resolver/resolver.cryo", "type struct Resolver {", "        return Resolver {",
     [("graph", "ModuleGraph*",
       "The module graph, for the definition a declaring symbol's module is:\n"
       "    /// a declaration's id is registered under its module's.")]),
]

IMPORT = "import compiler::resolver::res::{ DefTable };"

for path, head, ctor, extra in SPECS:
    s = open(path, encoding="utf-8", newline="").read()
    assert s.count(head) == 1 and s.count(ctor) == 1, path
    fields = [("defs", "DefTable*",
               "The compilation's definitions, for a definition's path.  Borrowed;\n"
               "    /// set by `CompilationContext`, which owns it.")] + extra
    decl = "".join("    /// %s\n    %s: %s;\n" % (doc, n, t) for n, t, doc in fields)
    s = s.replace(head + "\n", head + "\n" + decl, 1)
    init = "".join("            %s: null,\n" % n for n, _, _ in fields)
    s = s.replace(ctor + "\n", ctor + "\n" + init, 1)
    if IMPORT not in s:
        m = re.search(r"^import compiler::resolver::res::\{ ([^}]*) \};", s, re.M)
        if m is not None:
            s = s[:m.start()] + "import compiler::resolver::res::{ %s, DefTable };" % m.group(1) + s[m.end():]
        else:
            m = re.search(r"^import .*;\n", s, re.M)
            s = s[:m.start()] + "import compiler::resolver::res;\n" + IMPORT + "\n" + s[m.start():]
    if extra and "ModuleGraph }" not in s and "{ ModuleGraph" not in s:
        m = re.search(r"^import .*;\n", s, re.M)
        s = s[:m.start()] + "import compiler::module_graph;\nimport compiler::module_graph::{ ModuleGraph };\n" + s[m.start():]
    open(path, "w", encoding="utf-8", newline="").write(s)
    print("ok", path)
