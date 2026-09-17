"""is_candidate_public: every registration records a verdict, and the
permissive default becomes a door.

1. `register_type(key, ty)` -> `register_type(key, ty, is_public)`: the type
   registration records its visibility verdict.  28 callers rewritten.
2. The extern-block arm records each imported function's verdict under its
   key (and the C-import alias key).
3. register_decl_in_index's type arms drop their (now second) verdict writes.
4. is_candidate_public's None arm records an unregistered-def and answers
   `true` - the report is the E0900 tally, not a misleading E0353.
5. The shadow (`visibility_recorded`, three SHADOW lines) is deleted.
"""
import io, re, sys
def load(p): return io.open(p, encoding="utf-8", newline="").read()
def save(p, t): io.open(p, "w", encoding="utf-8", newline="").write(t)
def rep(t, old, new, n=1, where=""):
    c = t.count(old)
    assert c == n, (where, old[:70], c, n)
    return t.replace(old, new)

# ---- 1. the index ------------------------------------------------------
DI = "compiler/src/compiler/decl_index.cryo"
t = load(DI)
t = rep(t,
"    /// Register a named type (struct, class, enum, trait) under its\n"
"    /// qualified name (e.g. \"Module::Point\").  Callers must qualify the\n"
"    /// name before calling.\n"
"    register_type(mut &this, qualified_name: SymbolStr, ty: TypeRef) -> void {\n"
"        this.type_map.insert(qualified_name.id, ty);\n"
"\n"
"        // Reverse map: TypeRef.id -> qualified name\n"
"        this.type_reverse.insert(ty.id, qualified_name.id);\n"
"    }\n",
"    /// Register a named type (struct, class, enum, trait) under its\n"
"    /// qualified name (e.g. \"Module::Point\"), with its visibility verdict.\n"
"    /// Callers must qualify the name before calling.  The verdict is\n"
"    /// recorded here, with the key, so no registered type is ever asked\n"
"    /// about before it has one.\n"
"    register_type(mut &this, qualified_name: SymbolStr, ty: TypeRef, is_public: boolean) -> void {\n"
"        this.type_map.insert(qualified_name.id, ty);\n"
"\n"
"        // Reverse map: TypeRef.id -> qualified name\n"
"        this.type_reverse.insert(ty.id, qualified_name.id);\n"
"        this.decl_visibility.insert(qualified_name.id, is_public);\n"
"    }\n", where=DI)
t = t.replace(
"    visibility_recorded(&this, qualified: SymbolStr) -> boolean {\n"
"        return this.decl_visibility.get(&qualified.id).is_some();\n"
"    }\n\n", "")
t = rep(t,
"    /// Visibility verdict for a candidate; permissive default (see\n"
"    /// `decl_visibility`).\n"
"    is_candidate_public(&this, qualified: SymbolStr) -> boolean {\n"
"        return match (this.decl_visibility.get(&qualified.id)) {\n"
"            Option::Some(v) => { v }\n"
"            Option::None    => { true }\n"
"        };\n"
"    }\n",
"    /// Visibility verdict for a candidate.  Every registration records one\n"
"    /// (a type's with `register_type`, a function's beside its signature),\n"
"    /// so a candidate with none is a declaration that never registered -\n"
"    /// recorded as such, which fails the build; the answer given meanwhile\n"
"    /// is the top-level default rather than a refusal that would report a\n"
"    /// public item as private.\n"
"    is_candidate_public(&this, qualified: SymbolStr) -> boolean {\n"
"        return match (this.decl_visibility.get(&qualified.id)) {\n"
"            Option::Some(v) => { v }\n"
"            Option::None    => {\n"
"                compiler::resolver::res::record_unregistered_def(\n"
"                    \"decl_index: visibility of a candidate no registration recorded a verdict for\");\n"
"                true\n"
"            }\n"
"        };\n"
"    }\n", where=DI)
# the field's comment: the permissive default is gone
t = rep(t,
"    // alternatives set) is treated as public (permissive).\n",
"    // alternatives set) is a declaration that never registered, recorded as\n"
"    // such (`is_candidate_public`).\n", where=DI)
save(DI, t)

# ---- 1b. callers of register_type -------------------------------------
PR = "compiler/src/compiler/passes/pass_registry.cryo"
t = load(PR)
t = rep(t, "ctx.decl_index.register_type(qualified_sym, struct_ref);", "ctx.decl_index.register_type(qualified_sym, struct_ref, node.is_public);", where=PR)
t = rep(t, "ctx.decl_index.register_type(qualified_sym, union_ref);", "ctx.decl_index.register_type(qualified_sym, union_ref, node.is_public);", where=PR)
t = rep(t, "ctx.decl_index.register_type(qualified_sym, enum_ref);", "ctx.decl_index.register_type(qualified_sym, enum_ref, node.is_public);", where=PR)
t = rep(t, "ctx.decl_index.register_type(qualified_sym, class_ref);", "ctx.decl_index.register_type(qualified_sym, class_ref, node.is_public);", where=PR)
t = rep(t,
"                    ctx.decl_index.register_type(qualified_sym, trait_ref);",
"                    // A trait carries no visibility modifier: public.\n"
"                    ctx.decl_index.register_type(qualified_sym, trait_ref, true);", where=PR)
t = rep(t,
"                    ctx.decl_index.register_type(qualified_alias, alias_ref);",
"                    // An alias carries no visibility modifier: public.\n"
"                    ctx.decl_index.register_type(qualified_alias, alias_ref, true);", where=PR)
# the 22 primitives: `register_type(ctx.intern("x"), arena.get_x());`
prim = re.compile(r'register_type\((ctx\.intern\("[^"]+"\)),\s+(arena\.get_[a-z0-9_]+\(\))\);')
t, n = prim.subn(r'register_type(\1, \2, true);', t)
assert n == 19, n
save(PR, t)

SP = "compiler/src/compiler/passes/specialization.cryo"
t = load(SP)
t = rep(t,
"        ctx.decl_index.register_type(arena_qname, spec_type);\n",
"        ctx.decl_index.register_type(arena_qname, spec_type, SpecInjector::node_is_public(node));\n", where=SP)
t = rep(t,
"    /// Register a newly injected specialized declaration into the DeclarationIndex\n",
"    /// The visibility a specialized type declaration carries: its template's,\n"
"    /// which the cloner copied onto the node.  A kind without a modifier is\n"
"    /// public.\n"
"    static node_is_public(node: ASTNode*) -> boolean {\n"
"        if (node.kind == NodeKind::StructDeclaration) { return (node as StructDeclNode*).is_public; }\n"
"        if (node.kind == NodeKind::UnionDeclaration)  { return (node as UnionDeclNode*).is_public; }\n"
"        if (node.kind == NodeKind::ClassDeclaration)  { return (node as ClassDeclNode*).is_public; }\n"
"        if (node.kind == NodeKind::EnumDeclaration)   { return (node as EnumDeclNode*).is_public; }\n"
"        return true;\n"
"    }\n\n"
"    /// Register a newly injected specialized declaration into the DeclarationIndex\n", where=SP)
save(SP, t)

AL = "compiler/src/compiler/sema/async_lower.cryo"
t = load(AL)
t = rep(t,
"        this.ctx.decl_index.register_type(q_name, struct_ref);\n",
"        this.ctx.decl_index.register_type(q_name, struct_ref, true);\n", where=AL)
save(AL, t)

LS = "compiler/src/compiler/sema/lambda_synth.cryo"
t = load(LS)
t = rep(t,
"        ctx_ptr.decl_index.register_type(qual_name, struct_ref);\n",
"        ctx_ptr.decl_index.register_type(qual_name, struct_ref, true);\n", where=LS)
save(LS, t)

# ---- 2 + 3 + 5. type_resolution -----------------------------------------
TR = "compiler/src/compiler/passes/type_resolution.cryo"
t = load(TR)
# 3. the five verdict writes in register_decl_in_index's type arms
t = rep(t,
"                    ctx.decl_index.register_name_mapping(node.name, qualified_sym);\n"
"                    ctx.decl_index.set_decl_visibility(qualified_sym, node.is_public);\n",
"                    ctx.decl_index.register_name_mapping(node.name, qualified_sym);\n", n=4, where=TR)
t = rep(t,
"                    ctx.decl_index.register_name_mapping(node.name, qualified_sym);\n"
"                    // Traits are always public.\n"
"                    ctx.decl_index.set_decl_visibility(qualified_sym, true);\n",
"                    ctx.decl_index.register_name_mapping(node.name, qualified_sym);\n", where=TR)
# 2. the extern-block arm
t = rep(t,
"                            fn_node.set_overload_entry(ctx.decl_index.register_function_signature(\n"
"                                ext_q_name, fn_node.resolved_return_type, func_type_ref,\n"
"                                ctx.intern_table, arena, ext_link_sym));\n"
"                            ctx.decl_index.note_extern_symbol(ext_link_sym, ext_q_name, func_type_ref);\n",
"                            fn_node.set_overload_entry(ctx.decl_index.register_function_signature(\n"
"                                ext_q_name, fn_node.resolved_return_type, func_type_ref,\n"
"                                ctx.intern_table, arena, ext_link_sym));\n"
"                            ctx.decl_index.note_extern_symbol(ext_link_sym, ext_q_name, func_type_ref);\n"
"                            // An imported function is a declaration of this\n"
"                            // module and takes its written visibility, as a\n"
"                            // free function does: a `private` one is not\n"
"                            // callable by qualified path from another module.\n"
"                            ctx.decl_index.set_decl_visibility(ext_q_name, fn_node.is_public);\n", where=TR)
t = rep(t,
"                                ctx.decl_index.register_function_signature(\n"
"                                    q_sym, fn_node.resolved_return_type, func_type_ref,\n"
"                                    ctx.intern_table, arena, ext_link_sym);\n",
"                                ctx.decl_index.register_function_signature(\n"
"                                    q_sym, fn_node.resolved_return_type, func_type_ref,\n"
"                                    ctx.intern_table, arena, ext_link_sym);\n"
"                                ctx.decl_index.set_decl_visibility(q_sym, fn_node.is_public);\n", where=TR)
# 5. the shadow lines
t = re.sub(r'        if \(this\.ctx\.decl_index\.visibility_recorded\([a-z]+\)\) \{ fmt::eprintf\("SHADOW[^\n]*\n', "", t)
assert "visibility_recorded" not in t and "SHADOW" not in t
save(TR, t)

CR = "compiler/src/compiler/sema/call_resolver.cryo"
t = load(CR)
t = re.sub(r'        if \(di\.visibility_recorded\(callee\)\) \{ fmt::eprintf\("SHADOW[^\n]*\n', "", t)
assert "visibility_recorded" not in t and "SHADOW" not in t
save(CR, t)
print("ok")
