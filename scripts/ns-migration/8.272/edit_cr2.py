p = 'compiler/src/compiler/sema/call_resolver.cryo'
s = open(p, encoding='utf-8', newline='').read()
def rep(old, new, count=1):
    global s
    assert s.count(old) == count, (old[:70], s.count(old))
    s = s.replace(old, new)
rep("""        mut method_names_d: SymbolStr[] = [];
        for (mut i: i64 = 0; i < n; i++) { method_names_d.push(func.generic_params[i].name); }
        this.ctx.type_resolver.project_where_bound_params_into(
            &func.trait_bounds, &method_names_d, &ictx, this.ctx.source_file,
            this.state.in_symbolic_check);
""", """        mut method_syms_d: SymbolID[] = [];
        for (mut i: i64 = 0; i < n; i++) { method_syms_d.push(func.generic_params[i].sym_id); }
        this.ctx.type_resolver.project_where_bound_params_into(
            &func.trait_bounds, &method_syms_d, &ictx, this.ctx.source_file,
            this.state.in_symbolic_check);
""")
rep("""import compiler::resolver::symbol_str::{ SymbolStr };
""", """import compiler::resolver::symbol_str::{ SymbolStr };
import compiler::resolver::symbol_id;
import compiler::resolver::symbol_id::{ SymbolID };
""")
open(p, 'w', encoding='utf-8', newline='').write(s)
print("ok")
