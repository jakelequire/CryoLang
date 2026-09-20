p = 'compiler/src/compiler/mono/ast_resolver.cryo'
s = open(p, encoding='utf-8', newline='').read()
def rep(old, new, count=1):
    global s
    assert s.count(old) == count, (old[:70], s.count(old))
    s = s.replace(old, new)

start = s.index("        // Fallback resolution with substitution-derived bindings.  The\n")
end = s.index("        // Reduce concrete-base associated-type projections in the spec signature\n")
s = s[:start] + s[end:]

rep("""                this.resolve_methods(decl.methods, &res_ctx, entry, subst, this_type);
""", """                this.resolve_methods(decl.methods, &res_ctx, this_type);
""", 4)
rep("""                this.resolve_func_and_body(decl, &res_ctx, entry, subst, TypeRef::invalid());
""", """                this.resolve_func_and_body(decl, &res_ctx, TypeRef::invalid());
""")
rep("""    resolve_methods(mut &this, methods: &MethodNode*[], res_ctx: ResolutionContext*,
                    entry: TemplateEntry*, subst: TypeSubstitution*, this_type: TypeRef) -> void {
""", """    resolve_methods(mut &this, methods: &MethodNode*[], res_ctx: ResolutionContext*,
                    this_type: TypeRef) -> void {
""")
rep("""                this.resolve_func_and_body(method.func, &method_ctx, entry, subst, m_this);
            } else {
                this.resolve_func_and_body(method.func, res_ctx, entry, subst, m_this);
            }
""", """                this.resolve_func_and_body(method.func, &method_ctx, m_this);
            } else {
                this.resolve_func_and_body(method.func, res_ctx, m_this);
            }
""")
rep("""    /// Resolve a function's signature (via TypeResolver) and then resolve local
    /// variable types in its body (cloned AST bodies have cleared resolved_type).
    ///
    /// Sema typed the body and stashed every generic call; MonoCallSpecializer
    /// consumes the stash.  Only local-decl resolution + return-type nested
    /// discovery remain here.
    resolve_func_and_body(mut &this, func: FunctionDeclNode*, res_ctx: ResolutionContext*,
                          entry: TemplateEntry*, subst: TypeSubstitution*,
                          this_type: TypeRef) -> void {
""", """    /// Resolve a function's signature (via TypeResolver) and then resolve local
    /// variable types in its body (cloned AST bodies have cleared resolved_type).
    ///
    /// Sema typed the body and stashed every generic call; MonoCallSpecializer
    /// consumes the stash.  Only local-decl resolution + return-type nested
    /// discovery remain here.  The signature's annotations were rewritten by
    /// the substituter before this runs, so a parameter left in one names a
    /// declaration the clone cannot see, and there is no second context to
    /// try it under.
    resolve_func_and_body(mut &this, func: FunctionDeclNode*, res_ctx: ResolutionContext*,
                          this_type: TypeRef) -> void {
""")
open(p, 'w', encoding='utf-8', newline='').write(s)
print("ok")

p2 = 'compiler/src/compiler/mono/call_specializer.cryo'
s2 = open(p2, encoding='utf-8', newline='').read()
old = """            this.ast_resolver.resolve_func_and_body(spec_method.func, &body_ctx, owner_entry, &full_subst, recv_type);
"""
new = """            this.ast_resolver.resolve_func_and_body(spec_method.func, &body_ctx, recv_type);
"""
assert s2.count(old) == 1
s2 = s2.replace(old, new)
open(p2, 'w', encoding='utf-8', newline='').write(s2)
print("ok2")
