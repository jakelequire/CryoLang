def edit(p, reps):
    s = open(p, encoding='utf-8', newline='').read()
    for old, new, count in reps:
        assert s.count(old) == count, (p, old[:70], s.count(old))
        s = s.replace(old, new)
    open(p, 'w', encoding='utf-8', newline='').write(s)
    print("ok", p)

imp = """import compiler::resolver::symbol_id;
import compiler::resolver::symbol_id::{ SymbolID };
"""
edit('compiler/src/compiler/mono/call_specializer.cryo', [
("""import compiler::resolver::symbol_str::{ SymbolStr };
""", """import compiler::resolver::symbol_str::{ SymbolStr };
""" + imp, 1)])
edit('compiler/src/compiler/mono/specializer.cryo', [
("""import compiler::resolver::symbol_str::{ SymbolStr };
""", """import compiler::resolver::symbol_str::{ SymbolStr };
""" + imp, 1)])
edit('compiler/src/compiler/mono/trait_specializer.cryo', [
("""import compiler::resolver::symbol_str::{ SymbolStr };
""", """import compiler::resolver::symbol_str::{ SymbolStr };
""" + imp, 1)])
