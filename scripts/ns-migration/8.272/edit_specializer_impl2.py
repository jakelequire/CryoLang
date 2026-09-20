p = 'compiler/src/compiler/mono/specializer.cryo'
s = open(p, encoding='utf-8', newline='').read()
def rep(old, new, count=1):
    global s
    assert s.count(old) == count, (old[:70], s.count(old))
    s = s.replace(old, new)
rep("""        // Independent copies of the parameter lists (both element types are
        // Copy): the substituter stores and owns its arrays, so handing it
        // the template entry's own buffers would double-free them.
        mut pn_copy: SymbolStr[] = [];
        mut ps_copy: SymbolID[] = [];
        for (mut pi: i64 = 0; pi < entry.param_names.length; pi++) {
            pn_copy.push(entry.param_names[pi]);
            ps_copy.push(entry.param_syms[pi]);
        }
        mut substituter: ASTTypeSubstituter* = new ASTTypeSubstituter(
            subst_ptr, this.arena, this.intern_table,
            entry.name, spec_sym,
            pn_copy, ps_copy, arg_syms, spec_typeref
        );
        cloned_ast.accept(substituter);
""", """        // Independent copies of the parameter and display lists (every
        // element type is Copy): the substituter stores and owns its arrays,
        // so handing it the template entry's own buffers would double-free
        // them, and `arg_syms` is read again for each impl block below.
        mut pn_copy: SymbolStr[] = [];
        mut ps_copy: SymbolID[] = [];
        for (mut pi: i64 = 0; pi < entry.param_names.length; pi++) {
            pn_copy.push(entry.param_names[pi]);
            ps_copy.push(entry.param_syms[pi]);
        }
        mut disp_copy: SymbolStr[] = [];
        for (mut ai: i64 = 0; ai < arg_syms.length; ai++) { disp_copy.push(arg_syms[ai]); }
        mut substituter: ASTTypeSubstituter* = new ASTTypeSubstituter(
            subst_ptr, this.arena, this.intern_table,
            entry.name, spec_sym,
            pn_copy, ps_copy, disp_copy, spec_typeref
        );
        cloned_ast.accept(substituter);
""")
open(p, 'w', encoding='utf-8', newline='').write(s)
print("ok")
