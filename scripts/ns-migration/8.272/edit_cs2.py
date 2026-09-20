p = 'compiler/src/compiler/mono/call_specializer.cryo'
s = open(p, encoding='utf-8', newline='').read()
def rep(old, new, count=1):
    global s
    assert s.count(old) == count, (old[:70], s.count(old))
    s = s.replace(old, new)
rep("""        mut empty_names: SymbolStr[] = [];
        mut empty_args: TypeRef[] = [];
        const spec_method: MethodNode* = this.specialize_method(
            orig, &empty_names, &empty_args, recv_type, impl_node);
""", """        mut empty_params: GenericParamNode*[] = [];
        mut empty_args: TypeRef[] = [];
        const spec_method: MethodNode* = this.specialize_method(
            orig, &empty_params, &empty_args, recv_type, impl_node);
""")
rep("""            this.ast_resolver.resolve_func_and_body(spec_method.func, &body_ctx, &full_subst, recv_type);
""", """            this.ast_resolver.resolve_func_and_body(spec_method.func, &body_ctx, owner_entry, &full_subst, recv_type);
""")
open(p, 'w', encoding='utf-8', newline='').write(s)
print("ok")
