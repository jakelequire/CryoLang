p = 'compiler/src/compiler/mono/call_specializer.cryo'
s = open(p, encoding='utf-8', newline='').read()
old = """            this.ast_resolver.prune_static_match_in_block(spec_method.func.body, &prune_ctx);

            mut full_subst: TypeSubstitution = TypeSubstitution::empty();
            this.add_owner_param_bindings(&full_subst, recv_type);
            for (mut i: i64 = 0; i < method_param_names.length; i++) {
                const pref: TypeRef = this.arena.create_generic_param(method_param_names[i], i as u64);
                full_subst.add(pref.id, method_binds[i]);
            }
            mut body_ctx: ResolutionContext = ResolutionContext::new("");
"""
new = """            this.ast_resolver.prune_static_match_in_block(spec_method.func.body, &prune_ctx);

            mut body_ctx: ResolutionContext = ResolutionContext::new("");
"""
assert s.count(old) == 1
s = s.replace(old, new)
open(p, 'w', encoding='utf-8', newline='').write(s)
print("ok")
