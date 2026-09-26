"""Rewrite every `ModulePath::of` call outside the module graph onto the
graph's doors: a registered module's own path (`ModuleInfo::path`), the
module a file declares (`module_of_file`), a definition's module
(`DefTable::module_of`), or the one text-to-module door `module_named`."""
import io
import os

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


NR = os.path.join(SRC, "resolver", "name_resolution.cryo")
G = "this.ctx.module_graph.module_named"
edit(NR, [
    ("bind_c_import_module(\n                        ModulePath::of(canon), ",
     "bind_c_import_module(\n                        canon, ", 1),
    ("ModulePath::of(canonical)", G + "(canonical)", 1),
    ("find_module_scope(ModulePath::of(info.name))", "find_module_scope(info.path())", 2),
    ("owners.push(ModulePath::of(info.name))", "owners.push(info.path())", 1),
    ("reexport_closure(ModulePath::of(cand.name))", "reexport_closure(cand.path())", 1),
    ("ns_imports(ModulePath::of(use_ns), owner_path)",
     "ns_imports(this.ctx.module_graph.module_of_file(span.file), owner_path)", 1),
    ("""                const hs: Symbol = this.resolver.get_symbol(hit);
                const declared_in: string = QualifiedName::parent_of(
                    this.resolver.intern_table.resolve(
                        this.resolver.qualified_name_of(&hs)));
                if (declared_in.length() > 0) {
                    owner = this.ctx.module_graph.module_def(ModulePath::of(
                        this.resolver.intern_table.intern(declared_in)));
                }
""",
     """                const hs: Symbol = this.resolver.get_symbol(hit);
                const declaring: DefId = this.resolver.defs.module_of(hs.def);
                if (declaring.is_valid()) { owner = declaring; }
""", 1),
    ("find_module_scope(ModulePath::of(q_ns))", "find_module_scope(" + G + "(q_ns))", 1),
    ("find_module_scope(ModulePath::of(use_ns))",
     "find_module_scope(this.ctx.module_graph.module_of_file(span.file))", 1),
    ("ModulePath::of(m)", G + "(m)", 2),
    ("ModulePath::of(module_sym)", G + "(module_sym)", 3),
    ("ModulePath::of(sub_sym)", G + "(sub_sym)", 1),
])

RS = os.path.join(SRC, "resolver", "resolver.cryo")
edit(RS, [
    ("this.graph.module_def(ModulePath::of(this.intern_table.intern(sym.source_module)))",
     "this.graph.module_def(this.graph.module_named(this.intern_table.intern(sym.source_module)))", 1),
    ("this.find_module_scope(ModulePath::of(module_name))",
     "this.find_module_scope(this.graph.module_named(module_name))", 1),
    ("""    find_module_scope(&this, module_name: ModulePath) -> u64 {
        const oid""",
     """    find_module_scope(&this, module_name: ModulePath) -> u64 {
        if (!module_name.is_valid()) { return 0; }
        const oid""", 1),
])

edit(os.path.join(SRC, "sema", "sema.cryo"), [
    ("resolver.find_module_scope(ModulePath::of(template_mod))",
     "resolver.find_module_scope(this.ctx.module_graph.module_named(template_mod))", 1),
])

edit(os.path.join(SRC, "mono", "monomorphizer.cryo"), [
    ("find_module_scope(ModulePath::of(entry.module_name))",
     "find_module_scope(\n                    this.type_resolver.name_resolver.graph.module_named(entry.module_name))", 1),
])

edit(os.path.join(SRC, "sema", "member_resolver.cryo"), [
    ("ModulePath::of(this.ctx.module_ns_sym_of_file(span.file))",
     "this.ctx.module_of_file(span.file)", 1),
    ("ModulePath::of(this.ctx.module_ns_sym_of_file(node.span.file))",
     "this.ctx.module_of_file(node.span.file)", 1),
    ("""        if (t == null) { return ModulePath::none(); }
        if (t.kind == TypeKind::Struct) { return ModulePath::of((t as StructType*).module_name); }
        if (t.kind == TypeKind::Class) { return ModulePath::of((t as ClassType*).module_name); }""",
     """        if (t == null || this.ctx == null || this.ctx.module_graph == null) { return ModulePath::none(); }
        if (t.kind == TypeKind::Struct) {
            return this.ctx.module_graph.module_named((t as StructType*).module_name);
        }
        if (t.kind == TypeKind::Class) {
            return this.ctx.module_graph.module_named((t as ClassType*).module_name);
        }""", 1),
])

edit(os.path.join(SRC, "sema", "call_resolver.cryo"), [
    ("module_def(ModulePath::of(use_ns))",
     "module_def(this.ctx.module_graph.module_named(use_ns))", 1),
])

CC = os.path.join(SRC, "compilation_context.cryo")
edit(CC, [
    ("""        if (this.module_graph == null || this.decl_index == null) { return r; }
        const count: i64 = this.module_graph.module_count() as i64;
        for (mut i: i64 = 0; i < count; i++) {
            const cand: ModuleInfo* = this.module_graph.get_module(i);""",
     """        if (this.module_graph == null || this.decl_index == null) { return r; }
        const use_module: ModulePath = this.module_graph.module_named(use_ns);
        const count: i64 = this.module_graph.module_count() as i64;
        for (mut i: i64 = 0; i < count; i++) {
            const cand: ModuleInfo* = this.module_graph.get_module(i);""", 1),
    ("this.decl_index.is_prelude_ns(ModulePath::of(cand_ns))",
     "this.decl_index.is_prelude_ns(cand.path())", 1),
    ("this.decl_index.ns_imports(ModulePath::of(use_ns), ModulePath::of(cand_ns))",
     "this.decl_index.ns_imports(use_module, cand.path())", 1),
    ("""        return this.module_graph.module_def(ModulePath::of(ns));
    }
""",
     """        return this.module_graph.module_def(this.module_graph.module_named(ns));
    }

    /// The module the source file `file` declares; none for a file the
    /// graph did not load.
    module_of_file(&this, file: string) -> ModulePath {
        if (this.module_graph == null) { return ModulePath::none(); }
        return this.module_graph.module_of_file(file);
    }
""", 1),
    ("""        *out_leaf = name;
        const home: SymbolStr = this.module_ns_sym_of_file(span_file);
        if (home.is_valid()) { return this.module_family(home); }
        return this.module_family(this.namespace_str);""",
     """        *out_leaf = name;
        const home: ModulePath = this.module_of_file(span_file);
        if (home.is_valid()) { return FamilyOwner::Def(this.module_graph.module_def(home)); }
        return this.module_family(this.namespace_str);""", 1),
])

ML = os.path.join(SRC, "module_loader.cryo")
edit(ML, [
    ("graph.find_module_index(ModulePath::of(already_name))",
     "graph.find_module_index(graph.module_named(already_name))", 1),
    ("graph.find_module_index(ModulePath::of(item_sym))",
     "graph.find_module_index(graph.module_named(item_sym))", 1),
    ("graph.find_module_index(ModulePath::of(exp_sym))",
     "graph.find_module_index(graph.module_named(exp_sym))", 1),
    ("graph.find_module_index(ModulePath::of(eitem_sym))",
     "graph.find_module_index(graph.module_named(eitem_sym))", 1),
])

edit(os.path.join(SRC, "passes", "pass_registry.cryo"), [
    ("graph.find_module_index(ModulePath::of(mod_sym))",
     "graph.find_module_index(graph.module_named(mod_sym))", 1),
])

edit(os.path.join(SRC, "instance.cryo"), [
    ("ModulePath::of(ctx.intern_table.intern(ns))",
     "ctx.module_graph.module_named(ctx.intern_table.intern(ns))", 1),
    ("find_module_index(ModulePath::of(qual))",
     "find_module_index(ctx.module_graph.module_named(qual))", 1),
])

LSP = os.path.join(ROOT, "tools", "CryoLSP", "src", "handlers", "completion.cryo")
edit(LSP, [
    ("eng.resolve_scope_path(ident_cstr, ast.namespace_name)",
     "eng.resolve_scope_path(ident_cstr, ctx.module_graph.get_module(mod_idx).path())", 1),
    ("    /// module `use_site_ns` - that module's",
     "    /// module `use_site` - that module's", 1),
    ("resolve_scope_path(&this, path: string, use_site_ns: string) -> symbol_str::SymbolStr {",
     "resolve_scope_path(&this, path: string, use_site: ModulePath) -> symbol_str::SymbolStr {", 1),
    ("this.res.find_module_scope(ModulePath::of(this.intern.intern(use_site_ns)))",
     "this.res.find_module_scope(use_site)", 1),
])
print("ok")
