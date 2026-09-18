"""Unit 4 final: TemplateRegistration keyed by the stamp; the arena's name
lookup deleted.  Run on a tree holding the shadow (restored conditions)."""
import io
R = r"C:\Programming\apps\CryoLang\compiler\src\compiler"

def edit(path, pairs):
    src = io.open(path, encoding="utf-8", newline="").read()
    for old, new, *cnt in pairs:
        c = cnt[0] if cnt else 1
        assert src.count(old) == c, (path, src.count(old), old[:80])
        src = src.replace(old, new)
    io.open(path, "w", encoding="utf-8", newline="").write(src)

edit(R + r"\passes\specialization.cryo", [
# 1. register_generic_template: the stamp is the key and names the base type
("""    /// Unified template registration for any generic declaration.
    /// Extracts generic parameter metadata, qualifies the name, looks up the
    /// base type in the arena, and registers a TemplateEntry.
    ///
    /// `name` and `generic_params` are the common fields every generic
    /// declaration shares.  `ast_node` is the original declaration cast to
    /// ASTNode*.  `node_kind` identifies which declaration type it is.
    /// `lookup_base` controls whether the base type is looked up in the arena
    /// (true for types: struct/enum/class/trait) or left invalid (false for
    /// functions, which have no arena entry).
    static register_generic_template(name: SymbolStr, generic_params: &GenericParamNode*[],
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
""",
"""    /// Unified template registration for any generic declaration.
    /// Extracts generic parameter metadata and registers a TemplateEntry
    /// under the declaration's key.
    ///
    /// `name` and `generic_params` are the common fields every generic
    /// declaration shares.  `ast_node` is the original declaration cast to
    /// ASTNode*.  `node_kind` identifies which declaration type it is.
    /// `def` is the id the type-declaration stage stamped on a TYPE
    /// declaration: its canonical name is the registry's key and the index
    /// answers the base type by it.  A function template carries no stamp at
    /// this stage (its signature registers later) and is keyed by its
    /// declaration key with a placeholder base.
    static register_generic_template(name: SymbolStr, generic_params: &GenericParamNode*[],
                                       ast_node: ASTNode*, node_kind: NodeKind,
                                       def: DefId,
                                       registry: GenericRegistry*, arena: TypeArena*,
                                       ctx: CompilationContext*) -> void {
        if (generic_params.length == 0) { return; }

        const qualified_sym: SymbolStr = if (def.is_valid()) {
            def.qualified_name()
        } else {
            ctx.decl_fn_key(name, ast_node.span.file)
        };
"""),
("""        mut base_type: TypeRef = TypeRef::invalid();
        if (node_kind == NodeKind::FunctionDeclaration) {
            // Generic functions have no arena type, but they need a TypeID so
            // the GenericRegistry can track instantiations (e.g., identity<int>).
            // Use a FunctionTemplate placeholder instead of a fake StructType.
            base_type = arena.create_function_template(qualified_sym);
        } else if (lookup_base) {
            base_type = arena.lookup_by_name(qualified_sym);
        }
""",
"""        mut base_type: TypeRef = TypeRef::invalid();
        if (node_kind == NodeKind::FunctionDeclaration) {
            // Generic functions have no arena type, but they need a TypeID so
            // the GenericRegistry can track instantiations (e.g., identity<int>).
            // Use a FunctionTemplate placeholder instead of a fake StructType.
            base_type = arena.create_function_template(qualified_sym);
        } else {
            base_type = ctx.decl_index.type_of_def(def);
        }
"""),
])
for kind in ("StructDeclaration", "UnionDeclaration", "EnumDeclaration", "ClassDeclaration", "TraitDeclaration", "TypeAliasDeclaration"):
    edit(R + r"\passes\specialization.cryo", [
        ("                            stmt, NodeKind::%s, true, node.def, registry, arena, ctx);" % kind,
         "                            stmt, NodeKind::%s, node.def, registry, arena, ctx);" % kind)])
edit(R + r"\passes\specialization.cryo", [
("                            stmt, NodeKind::FunctionDeclaration, false, DefId::invalid(), registry, arena, ctx);",
 "                            stmt, NodeKind::FunctionDeclaration, DefId::invalid(), registry, arena, ctx);"),
# 2. register_static_method_templates: the owner's key is handed in
("""    /// Owner-qualified key (`<owner_qualified>::<method>`) avoids collisions
    /// when two types in the same namespace each declare a static method with
    /// the same bare name.
    static register_static_method_templates(owner_name: SymbolStr, owner_key: SymbolStr,
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
        const owner_qstr: string = ctx.intern_table.resolve(owner_qsym);
""",
"""    /// Owner-qualified key (`<owner_key>::<method>`) avoids collisions when
    /// two types in the same namespace each declare a static method with the
    /// same bare name.  `owner_key` is the owner's registered key - a
    /// declaration's stamp, an impl head's `target_key` - never the spelling
    /// qualified by the file the methods were written in, which names the
    /// owner only when that file declares it.
    static register_static_method_templates(owner_key: SymbolStr,
                                              methods: &MethodNode*[],
                                              registry: GenericRegistry*,
                                              arena: TypeArena*,
                                              ctx: CompilationContext*) -> void {
        if (methods.length == 0) { return; }

        const owner_qstr: string = ctx.intern_table.resolve(owner_key);
"""),
("""                        SpecializationPasses::register_static_method_templates(node.name, node.def.qualified_name(), node.methods,
                            registry, arena, ctx);""",
 """                        SpecializationPasses::register_static_method_templates(node.def.qualified_name(), node.methods,
                            registry, arena, ctx);""", 3),
("""                SpecializationPasses::register_static_method_templates(
                    node.target_type, qualified_sym, node.methods, registry, arena, ctx);""",
 """                SpecializationPasses::register_static_method_templates(
                    qualified_sym, node.methods, registry, arena, ctx);"""),
# 3. register_inherent_owner
("""    static register_inherent_owner(
            owner_name: SymbolStr, owner_key: SymbolStr, owner_ast: ASTNode*,
            registry: GenericRegistry*, ctx: CompilationContext*) -> void {
        if (owner_ast == null) { return; }
        if (!owner_key.equals(ctx.decl_type_key(owner_name, SymbolStr::empty(), owner_ast.span.file))) {
            fmt::eprintf("SHADOW\\tOWNERKEY-DIFF\\t%s\\t%s\\n",
                ctx.intern_table.resolve(ctx.decl_type_key(owner_name, SymbolStr::empty(), owner_ast.span.file)),
                ctx.intern_table.resolve(owner_key));
        }
        // Under the declaration's own key and nothing else.  A bare leaf as a
        // second key is last-writer-wins across every module declaring the
        // leaf, and a reader that reached it by a receiver's leaf was handed
        // some other module's declaration.
        const owner_qsym: SymbolStr = ctx.decl_type_key(owner_name, SymbolStr::empty(), owner_ast.span.file);
        registry.register_inherent_owner(owner_qsym, owner_ast);
    }
""",
"""    static register_inherent_owner(
            owner_key: SymbolStr, owner_ast: ASTNode*,
            registry: GenericRegistry*) -> void {
        if (owner_ast == null) { return; }
        // Under the declaration's own key - its stamp's name - and nothing
        // else.  A bare leaf as a second key is last-writer-wins across every
        // module declaring the leaf, and a reader that reached it by a
        // receiver's leaf was handed some other module's declaration.
        registry.register_inherent_owner(owner_key, owner_ast);
    }
"""),
("SpecializationPasses::register_inherent_owner(node.name, node.def.qualified_name(), stmt, registry, ctx);",
 "SpecializationPasses::register_inherent_owner(node.def.qualified_name(), stmt, registry);", 3),
])

# 4. the arena's name lookup: no caller left
A = R + r"\types\arena.cryo"
src = io.open(A, encoding="utf-8", newline="").read()
i = src.index("    /// Search all user-defined type caches by name.\n    lookup_by_name(&this, name: SymbolStr) -> TypeRef {")
# the method ends at the first "\n    }\n" after i
j = src.index("\n    }\n", i) + len("\n    }\n")
block = src[i:j]
assert block.count("lookup_by_name(") == 1 and "cache.get" in block, block
# drop a following blank line too
if src[j:j+1] == "\n":
    j += 1
src = src[:i] + src[j:]
assert "lookup_by_name(" not in src
io.open(A, "w", encoding="utf-8", newline="").write(src)
print("ok; deleted:\n" + block)
