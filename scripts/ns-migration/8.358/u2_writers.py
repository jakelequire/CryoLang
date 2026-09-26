"""Unit 2: the visibility writers pass the definition; the intrinsic node
carries its definition as a function node does; the gate reads identities."""

def edit(path, pairs):
    src = open(path, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in src else '\n'
    for a, b, n in pairs:
        a = a.replace('\n', nl); b = b.replace('\n', nl)
        c = src.count(a)
        assert c == n, (path, c, n, a[:90])
        src = src.replace(a, b)
    open(path, 'w', encoding='utf-8', newline='').write(src)

edit('compiler/src/compiler/decl_index.cryo', [
("import compiler::resolver::res::{ DefId, OverloadId, Res, ResSlot, DefTable };",
 "import compiler::resolver::res::{ DeclVisibility, DefId, OverloadId, Res, ResSlot, DefTable };", 1),
])

edit('compiler/src/compiler/passes/type_resolution.cryo', [
("                    ctx.decl_index.set_decl_visibility(q_name, func.is_public);",
 "                    ctx.decl_index.set_decl_visibility(func.def, func.is_public);", 1),
("""                            ctx.decl_index.set_decl_visibility(ext_q_name,
                                node.is_c_import() || fn_node.is_public);""",
 """                            ctx.decl_index.set_decl_visibility(fn_node.def,
                                node.is_c_import() || fn_node.is_public);""", 1),
("                        ctx.decl_index.set_decl_visibility(q_name, true);",
 "                        ctx.decl_index.set_decl_visibility(node.def, true);", 1),
])

edit('compiler/src/compiler/AST/declaration.cryo', [
("""type class IntrinsicDeclNode : DeclarationNode {
public:
    name:                    SymbolStr;
""", """type class IntrinsicDeclNode : DeclarationNode {
public:
    name:                    SymbolStr;
    /// The definition this declaration is, stamped by the name layer when
    /// it declares the intrinsic, as a function's is.
    def:                     DefId;
""", 1),
("""        : DeclarationNode(NodeKind::IntrinsicDeclaration, span) {
        this.name                    = name;
""", """        : DeclarationNode(NodeKind::IntrinsicDeclaration, span) {
        this.name                    = name;
        this.def                     = DefId::invalid();
""", 1),
])

edit('compiler/src/compiler/resolver/name_resolution.cryo', [
("""                const intr_declared: SymbolID = this.resolver.declare(sym);
                this.resolver.export_symbol(intr_declared);""",
 """                const intr_declared: SymbolID = this.resolver.declare(sym);
                this.resolver.export_symbol(intr_declared);
                intr_node.def = this.resolver.get_symbol(intr_declared).def;""", 1),
])

CR = 'compiler/src/compiler/sema/call_resolver.cryo'
edit(CR, [
("""        if (!this.ctx.decl_index.hidden_from(callee, use_ns, this.intern)) { return; }""",
 """        const use_module: DefId = this.ctx.module_graph.module_def(ModulePath::of(use_ns));
        if (!this.ctx.decl_index.hidden_from(callee, use_module)) { return; }""", 1),
])
print('ok')
