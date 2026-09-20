def edit(p, reps):
    s = open(p, encoding='utf-8', newline='').read()
    for old, new, count in reps:
        assert s.count(old) == count, (p, old[:70], s.count(old))
        s = s.replace(old, new)
    open(p, 'w', encoding='utf-8', newline='').write(s)
    print("ok", p)

edit('compiler/src/compiler/sema/async_lower.cryo', [
("""import compiler::resolver::res;
import compiler::resolver::res::{ DefId, Res, ResBase, ResSlot };
import compiler::ast;
""", """import compiler::resolver::res;
import compiler::resolver::res::{ DefId, Res, ResBase, ResSlot };
import compiler::resolver::symbol_id;
import compiler::resolver::symbol_id::{ SymbolID };
import compiler::ast;
""", 1)])

edit('compiler/src/compiler/passes/specialization.cryo', [
("""import compiler::resolver::symbol_str::{ SymbolStr };
import compiler::resolver::res::{ DefId };
""", """import compiler::resolver::symbol_str::{ SymbolStr };
import compiler::resolver::res::{ DefId };
import compiler::resolver::symbol_id;
import compiler::resolver::symbol_id::{ SymbolID };
""", 1)])
