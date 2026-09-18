"""Unit 4 shadow: TemplateRegistration's keys and base type off the stamp,
compared with the re-derived key and the arena's name lookup."""
import io
P = r"C:\Programming\apps\CryoLang\compiler\src\compiler\passes\specialization.cryo"
src = io.open(P, encoding="utf-8", newline="").read()

def rep(old, new, count=1):
    global src
    assert src.count(old) == count, (src.count(old), old[:80])
    src = src.replace(old, new)

rep("import compiler::resolver::symbol_str::{ SymbolStr };\n",
    "import compiler::resolver::symbol_str::{ SymbolStr };\nimport compiler::resolver::res::{ DefId };\n")

# 1. register_generic_template: a `def` parameter beside lookup_base
rep("""    static register_generic_template(name: SymbolStr, generic_params: &GenericParamNode*[],
                                       ast_node: ASTNode*, node_kind: NodeKind,
                                       lookup_base: boolean,
                                       registry: GenericRegistry*, arena: TypeArena*,
                                       ctx: CompilationContext*) -> void {
        if (generic_params.length == 0) { return; }

        const qualified_sym: SymbolStr = if (lookup_base) {
            ctx.decl_type_key(name, SymbolStr::empty(), ast_node.span.file)
        } else {
            ctx.decl_fn_key(name, ast_node.span.file)
        };
""",
"""    static register_generic_template(name: SymbolStr, generic_params: &GenericParamNode*[],
                                       ast_node: ASTNode*, node_kind: NodeKind,
                                       lookup_base: boolean, def: DefId,
                                       registry: GenericRegistry*, arena: TypeArena*,
                                       ctx: CompilationContext*) -> void {
        if (generic_params.length == 0) { return; }

        const qualified_sym: SymbolStr = if (lookup_base) {
            ctx.decl_type_key(name, SymbolStr::empty(), ast_node.span.file)
        } else {
            ctx.decl_fn_key(name, ast_node.span.file)
        };
        if (lookup_base) {
            const stamp_key: SymbolStr = def.qualified_name();
            if (!stamp_key.equals(qualified_sym)) {
                fmt::eprintf("SHADOW\\tTMPLKEY-DIFF\\t%s\\t%s\\n",
                    ctx.intern_table.resolve(qualified_sym), ctx.intern_table.resolve(stamp_key));
            }
            const stamp_base: TypeRef = ctx.decl_index.type_of_def(def);
            const name_base: TypeRef = arena.lookup_by_name(qualified_sym);
            if (stamp_base.id != name_base.id) {
                fmt::eprintf("SHADOW\\tTMPLBASE-DIFF\\t%s\\t%llu\\t%llu\\n",
                    ctx.intern_table.resolve(qualified_sym), name_base.id, stamp_base.id);
            }
        }
""")
# the six type callers pass node.def, the function caller an invalid id
for kind in ("StructDeclaration", "UnionDeclaration", "EnumDeclaration", "ClassDeclaration", "TraitDeclaration", "TypeAliasDeclaration"):
    rep("                            stmt, NodeKind::%s, true, registry, arena, ctx);" % kind,
        "                            stmt, NodeKind::%s, true, node.def, registry, arena, ctx);" % kind)
rep("                            stmt, NodeKind::FunctionDeclaration, false, registry, arena, ctx);",
    "                            stmt, NodeKind::FunctionDeclaration, false, DefId::invalid(), registry, arena, ctx);")

# 2. register_static_method_templates: an owner_key parameter
rep("""    static register_static_method_templates(owner_name: SymbolStr,
                                              methods: &MethodNode*[],
                                              registry: GenericRegistry*,
                                              arena: TypeArena*,
                                              ctx: CompilationContext*) -> void {
        if (methods.length == 0) { return; }

        const owner_qsym: SymbolStr = ctx.decl_type_key(owner_name, SymbolStr::empty(), methods[0].span.file);
""",
"""    static register_static_method_templates(owner_name: SymbolStr, owner_key: SymbolStr,
                                              methods: &MethodNode*[],
                                              registry: GenericRegistry*,
                                              arena: TypeArena*,
                                              ctx: CompilationContext*) -> void {
        if (methods.length == 0) { return; }

        const owner_qsym: SymbolStr = ctx.decl_type_key(owner_name, SymbolStr::empty(), methods[0].span.file);
        if (!owner_key.equals(owner_qsym)) {
            fmt::eprintf("SHADOW\\tSTATKEY-DIFF\\t%s\\t%s\\n",
                ctx.intern_table.resolve(owner_qsym), ctx.intern_table.resolve(owner_key));
        }
""")
rep("""                        SpecializationPasses::register_static_method_templates(node.name, node.methods,
                            registry, arena, ctx);""",
    """                        SpecializationPasses::register_static_method_templates(node.name, node.def.qualified_name(), node.methods,
                            registry, arena, ctx);""", count=3)
rep("""                SpecializationPasses::register_static_method_templates(
                    node.target_type, node.methods, registry, arena, ctx);""",
    """                SpecializationPasses::register_static_method_templates(
                    node.target_type, qualified_sym, node.methods, registry, arena, ctx);""")

# 3. register_inherent_owner
rep("""    static register_inherent_owner(
            owner_name: SymbolStr, owner_ast: ASTNode*,
            registry: GenericRegistry*, ctx: CompilationContext*) -> void {
        if (owner_ast == null) { return; }
""",
"""    static register_inherent_owner(
            owner_name: SymbolStr, owner_key: SymbolStr, owner_ast: ASTNode*,
            registry: GenericRegistry*, ctx: CompilationContext*) -> void {
        if (owner_ast == null) { return; }
        if (!owner_key.equals(ctx.decl_type_key(owner_name, SymbolStr::empty(), owner_ast.span.file))) {
            fmt::eprintf("SHADOW\\tOWNERKEY-DIFF\\t%s\\t%s\\n",
                ctx.intern_table.resolve(ctx.decl_type_key(owner_name, SymbolStr::empty(), owner_ast.span.file)),
                ctx.intern_table.resolve(owner_key));
        }
""")
rep("SpecializationPasses::register_inherent_owner(node.name, stmt, registry, ctx);",
    "SpecializationPasses::register_inherent_owner(node.name, node.def.qualified_name(), stmt, registry, ctx);", count=3)
io.open(P, "w", encoding="utf-8", newline="").write(src)
print("ok")
