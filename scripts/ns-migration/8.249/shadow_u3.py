"""Bucket E shadow, applied OVER the final code: codegen's
`resolve_global_of_def` also resolves the old way - the canonical name
split by the reader and the (leaf, ns) door - and prints GLOBAL-DIFF when
the two LLVM values differ, GLOBAL-CTL on every agreement with `--invert`.
`--revert` removes the edit."""
import io, sys
SR = r"C:\Programming\apps\CryoLang\compiler\src\compiler\codegen\ops\symbol_resolver.cryo"
invert = "--invert" in sys.argv
revert = "--revert" in sys.argv

ANCHOR = """    resolve_global_of_def(mut &this, d: DefId) -> LValue {
        return this.resolve_global_entry(
            this.ctx.decl_index.global_entry_of_def(d, this.ctx.intern_table));
    }
"""
SHADOW = """    resolve_global_of_def(mut &this, d: DefId) -> LValue {
        const by_id: LValue = this.resolve_global_entry(
            this.ctx.decl_index.global_entry_of_def(d, this.ctx.intern_table));
        mut shadow_leaf: SymbolStr = SymbolStr::empty();
        mut shadow_ns: SymbolStr = SymbolStr::empty();
        DeclarationIndex::split_global_key(d.qualified_name(), this.ctx.intern_table, &shadow_leaf, &shadow_ns);
        const by_name: LValue = this.resolve_global_in_namespace(shadow_leaf, shadow_ns);
        if (by_id.raw %s by_name.raw) {
            fmt::eprintf("SHADOW\\tGLOBAL-%s\\t%%s\\n", this.ctx.intern_table.resolve(d.qualified_name()));
        }
        return by_id;
    }
"""

def fill(t):
    return t % (("==", "CTL") if invert else ("!=", "DIFF"))

src = io.open(SR, encoding="utf-8", newline="").read()
nl = "\r\n" if "\r\n" in src else "\n"
a = ANCHOR.replace("\n", nl)
if revert:
    for variant in (SHADOW % ("!=", "DIFF"), SHADOW % ("==", "CTL")):
        v = variant.replace("\n", nl)
        if src.count(v) == 1:
            src = src.replace(v, a)
            break
    else:
        raise SystemExit("no shadow to revert")
else:
    assert src.count(a) == 1, src.count(a)
    src = src.replace(a, fill(SHADOW).replace("\n", nl))
io.open(SR, "w", encoding="utf-8", newline="").write(src)
print("reverted" if revert else ("inverted" if invert else "shadowed"))
