"""Inversion for attack W's zero: the registry never flags a same-leaf free
function, so two modules' clones share one bare identifier."""
import io, sys
import os
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
p = os.path.join(ROOT, "compiler", "src", "compiler", "types", "generic_registry.cryo")
t = io.open(p, encoding="utf-8").read()
old = """    finalize_disambiguation(mut &this, table: InternTable*) -> void {
        if (this.disambig_finalized) { return; }
"""
new = """    finalize_disambiguation(mut &this, table: InternTable*) -> void {
        if (this.disambig_finalized || table != null) { return; }
"""
a, b = (old, new) if sys.argv[1] == "on" else (new, old)
assert t.count(a) == 1
io.open(p, "w", encoding="utf-8", newline="\n").write(t.replace(a, b)); print("mut_w", sys.argv[1])
