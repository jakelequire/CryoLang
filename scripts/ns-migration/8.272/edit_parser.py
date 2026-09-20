p = 'compiler/src/compiler/parser/parser.cryo'
s = open(p, encoding='utf-8', newline='').read()
old = """import compiler::resolver::res;
import compiler::resolver::res::{ ResSlot };
"""
new = """import compiler::resolver::res;
import compiler::resolver::res::{ ResSlot };
import compiler::resolver::symbol_id;
import compiler::resolver::symbol_id::{ SymbolID };
"""
assert s.count(old) == 1
s = s.replace(old, new)
old2 = """            const bound: TraitBound = TraitBound {
                type_parameter: type_param,
                subject_type:   subject,
"""
new2 = """            const bound: TraitBound = TraitBound {
                type_parameter: type_param,
                subject_sym:    SymbolID::invalid(),
                subject_type:   subject,
"""
assert s.count(old2) == 1
s = s.replace(old2, new2)
open(p, 'w', encoding='utf-8', newline='').write(s)
print("ok")
