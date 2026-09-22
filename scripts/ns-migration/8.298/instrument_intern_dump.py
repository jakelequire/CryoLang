"""Attack W's second half: every NEW interned symbol printed once as
SHADOW<TAB>I<TAB><id><TAB><text>, so the analysis can spell the keys."""
import io, sys
import os
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
p = os.path.join(ROOT, "compiler", "src", "compiler", "resolver", "intern_table.cryo")
t = io.open(p, encoding="utf-8").read()
old = """        mut id: u32 = this.strings.length as u32;
        this.strings.push(copy);
        this.lookup.insert(copy, id);
        return SymbolStr::from_id(id);
"""
new = """        mut id: u32 = this.strings.length as u32;
        this.strings.push(copy);
        this.lookup.insert(copy, id);
        fmt::printf("SHADOW\\tI\\t%u\\t%s\\n", id, copy);
        return SymbolStr::from_id(id);
"""
imp_old = "import std::alloc::allocator;"
imp_new = "import std::alloc::allocator;\nimport std::fmt;"
on = sys.argv[1] == "on"
a, b = (old, new) if on else (new, old)
assert t.count(a) == 1
t = t.replace(a, b)
if "import std::fmt;" not in t and on:
    assert t.count(imp_old) == 1
    t = t.replace(imp_old, imp_new)
elif not on:
    t = t.replace(imp_new, imp_old)
io.open(p, "w", encoding="utf-8", newline="\n").write(t)
print("intern dump", "on" if on else "off")
