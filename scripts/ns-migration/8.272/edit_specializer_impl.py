p = 'compiler/src/compiler/mono/specializer.cryo'
s = open(p, encoding='utf-8', newline='').read()
def rep(old, new, count=1):
    global s
    assert s.count(old) == count, (old[:70], s.count(old))
    s = s.replace(old, new)

rep("""        // 6. Clone + substitute each impl block
        mut impl_asts: ASTNode*[] = [];
        for (mut i: i64 = 0; i < entry.impl_blocks.length; i++) {
            const cloned_impl: ASTNode* = cloner.clone_node(entry.impl_blocks[i]);
            if (cloned_impl != null) {
                const impl_node: ImplBlockNode* = cloned_impl as ImplBlockNode*;
""", """        // 6. Clone + substitute each impl block, under the IMPL's own
        //    parameters.  An `implement` block declares its own parameters
        //    and writes the owner's arguments in them - `implement<X> trait
        //    Show for struct Holder<X>` - so its body is stamped with the
        //    impl's declarations, not the owner's, and only a substituter
        //    keyed by those rewrites it.  Each parameter the head writes at
        //    a position is bound to the specialization's argument at that
        //    position; an argument the head writes as a type (`Box<T,
        //    GlobalAlloc>`) binds nothing here, because the head was already
        //    selected against the arguments, and a parameter the head does
        //    not write (one the where-clause derives) is bound from the
        //    bounds once the clone is placed.
        mut impl_asts: ASTNode*[] = [];
        for (mut i: i64 = 0; i < entry.impl_blocks.length; i++) {
            const cloned_impl: ASTNode* = cloner.clone_node(entry.impl_blocks[i]);
            if (cloned_impl != null) {
                const impl_node: ImplBlockNode* = cloned_impl as ImplBlockNode*;
                mut impl_substituter: ASTTypeSubstituter* =
                    this.impl_substituter(impl_node, entry, spec_sym, type_args, &arg_syms, spec_typeref);
""")
rep("""                swap(&impl_node.methods, &kept);

                cloned_impl.accept(substituter);
                // Rename impl block target to specialized name and
                // clear generic params; the specialization is concrete.
                impl_node.set_target_type(spec_sym);
                impl_node.generic_params = [];
                impl_asts.push(cloned_impl);
            }
        }

        // 7. Return
        return SpecializationResult::ok(cloned_ast, &impl_asts, spec_name);
    }
""", """                swap(&impl_node.methods, &kept);

                cloned_impl.accept(impl_substituter);
                // Rename impl block target to specialized name and
                // clear generic params; the specialization is concrete.
                impl_node.set_target_type(spec_sym);
                impl_node.generic_params = [];
                impl_asts.push(cloned_impl);
            }
        }

        // 7. Return
        return SpecializationResult::ok(cloned_ast, &impl_asts, spec_name);
    }

    /// The substituter for one impl block of a specialization: the impl's
    /// parameters, each bound to the specialization's argument at the
    /// position the head writes it, in the order the head writes them.  A
    /// head that writes one parameter twice (`Pair<T, T>`) binds it once, at
    /// its first position; the head was selected against the arguments, so
    /// the second position holds the same type.  The arena's parameter type
    /// for each is minted at its head position, which is the identity every
    /// other consumer of the impl's parameters mints it under.
    impl_substituter(&this, impl_node: ImplBlockNode*, entry: TemplateEntry*,
                     spec_sym: SymbolStr, type_args: &TypeRef[],
                     arg_syms: &SymbolStr[], spec_typeref: TypeRef) -> ASTTypeSubstituter* {
        mut names: SymbolStr[] = [];
        mut syms: SymbolID[] = [];
        mut displays: SymbolStr[] = [];
        mut arena_ids: u64[] = [];
        mut replacements: TypeRef[] = [];
        const n: i64 = if (impl_node.target_args.length < type_args.length) {
            impl_node.target_args.length
        } else {
            type_args.length
        };
        for (mut ti: i64 = 0; ti < n; ti++) {
            const ta: TypeAnnotation* = impl_node.target_args[ti];
            if (ta == null) { continue; }
            match (*ta) {
                TypeAnnotation::Named(named) => {
                    const sym: SymbolID = named.param_sym();
                    if (!sym.is_valid()) { continue; }
                    mut seen: boolean = false;
                    for (mut k: i64 = 0; k < syms.length; k++) {
                        if (syms[k].equals(sym)) { seen = true; break; }
                    }
                    if (seen) { continue; }
                    names.push(named.name);
                    syms.push(sym);
                    displays.push(arg_syms[ti]);
                    arena_ids.push(this.arena.create_generic_param(named.name, ti as u64).id);
                    replacements.push(type_args[ti]);
                }
                _ => {}
            }
        }
        const subst_ptr: TypeSubstitution* =
            allocator::alloc(sizeof(TypeSubstitution), 16) as TypeSubstitution*;
        *subst_ptr = TypeSubstitution::from_params(&arena_ids, &replacements);
        return new ASTTypeSubstituter(
            subst_ptr, this.arena, this.intern_table,
            entry.name, spec_sym,
            names, syms, displays, spec_typeref);
    }
""")
open(p, 'w', encoding='utf-8', newline='').write(s)
print("ok")
