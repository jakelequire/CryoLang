"""TEMPORARY probes (never banked): exit 97 where a rewritten ModulePath site
could answer differently from `ModulePath::of(text)`."""
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


edit(os.path.join(SRC, "module_graph.cryo"), [
    ("""    /// The path of the module the source file `file` declares; none for a""",
     """    probe_miss(&this, tag: string, ns: SymbolStr, text: string) -> void {
        if (!ns.is_valid()) { return; }
        if (this.module_named(ns).is_valid()) { return; }
        libc::printf("SEALPROBE %s [%s]\\n", tag, text);
        libc::exit(97);
    }

    /// The path of the module the source file `file` declares; none for a""", 1),
])

RS = os.path.join(SRC, "resolver", "resolver.cryo")
edit(RS, [
    ("""    find_module_scope(&this, module_name: ModulePath) -> u64 {""",
     """    probe_scope(&this, tag: string, x: SymbolStr) -> void {
        if (!x.is_valid() || this.graph == null) { return; }
        if (!this.module_scopes.contains_key(&x.id)) { return; }
        this.graph.probe_miss(tag, x, this.intern_table.resolve(x));
    }

    find_module_scope(&this, module_name: ModulePath) -> u64 {""", 1),
    ("""        const sid: u64 = this.find_module_scope(this.graph.module_named(module_name));""",
     """        this.probe_scope("resolver-private-decls", module_name);
        const sid: u64 = this.find_module_scope(this.graph.module_named(module_name));""", 1),
])

edit(os.path.join(SRC, "sema", "sema.cryo"), [
    ("""                saved_scope = resolver.get_current_scope_id();
                const template_scope""",
     """                saved_scope = resolver.get_current_scope_id();
                resolver.probe_scope("sema-template", template_mod);
                const template_scope""", 1),
])

edit(os.path.join(SRC, "mono", "monomorphizer.cryo"), [
    ("""            saved_scope = this.type_resolver.name_resolver.get_current_scope_id();""",
     """            saved_scope = this.type_resolver.name_resolver.get_current_scope_id();
            this.type_resolver.name_resolver.probe_scope("mono-template", entry.module_name);""", 1),
])

NR = os.path.join(SRC, "resolver", "name_resolution.cryo")
edit(NR, [
    ("""                    const q_sid: u64 = this.resolver.find_module_scope(""",
     """                    this.resolver.probe_scope("nr-alias-q", q_ns);
                    const q_sid: u64 = this.resolver.find_module_scope(""", 1),
    ("""                const declaring: DefId = this.resolver.defs.module_of(hs.def);
                if (declaring.is_valid()) { owner = declaring; }
""",
     """                const owner0: DefId = owner;
                const declaring: DefId = this.resolver.defs.module_of(hs.def);
                if (declaring.is_valid()) { owner = declaring; }
                {
                    const dn: string = QualifiedName::parent_of(
                        this.resolver.intern_table.resolve(this.resolver.qualified_name_of(&hs)));
                    mut old: DefId = owner0;
                    if (dn.length() > 0) {
                        old = this.ctx.module_graph.module_def(this.ctx.module_graph.module_named(
                            this.resolver.intern_table.intern(dn)));
                    }
                    if (!old.equals(owner)) {
                        libc::printf("SEALPROBE nr-owner [%s]\\n", dn);
                        libc::exit(97);
                    }
                }
""", 1),
])

edit(os.path.join(SRC, "compilation_context.cryo"), [
    ("""        const use_module: ModulePath = this.module_graph.module_named(use_ns);""",
     """        this.module_graph.probe_miss("cc-use-ns", use_ns, this.intern_table.resolve(use_ns));
        const use_module: ModulePath = this.module_graph.module_named(use_ns);""", 1),
])

edit(os.path.join(SRC, "sema", "member_resolver.cryo"), [
    ("""        if (t.kind == TypeKind::Struct) {
            return""",
     """        if (t.kind == TypeKind::Struct) {
            this.ctx.module_graph.probe_miss("mr-struct", (t as StructType*).module_name,
                this.intern.resolve((t as StructType*).module_name));
            return""", 1),
    ("""        if (t.kind == TypeKind::Class) {
            return""",
     """        if (t.kind == TypeKind::Class) {
            this.ctx.module_graph.probe_miss("mr-class", (t as ClassType*).module_name, "");
            return""", 1),
])
print("ok")
