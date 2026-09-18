"""Unit 2 shadow: func_returns vs the signature's return, at the one delegate
and at the family pin's rescue arm.  Every anchor must match exactly once."""
import io
R = r"C:\Programming\apps\CryoLang\compiler\src\compiler"

def edit(path, pairs):
    src = io.open(path, encoding="utf-8", newline="").read()
    for old, new in pairs:
        assert src.count(old) == 1, (path, src.count(old), old[:80])
        src = src.replace(old, new)
    io.open(path, "w", encoding="utf-8", newline="").write(src)

edit(R + r"\sema\type_utils.cryo", [(
"""    lookup_func_return(&this, name: SymbolStr) -> TypeRef {
        return this.ctx.decl_index.lookup_func_return(name);
    }
""",
"""    lookup_func_return(&this, name: SymbolStr) -> TypeRef {
        const old: TypeRef = this.ctx.decl_index.lookup_func_return(name);
        mut via_sig: TypeRef = TypeRef::invalid();
        const ft: Type* = this.arena.lookup(this.ctx.decl_index.lookup_func_type(name).id);
        if (ft != null && ft.kind == TypeKind::Function) { via_sig = (ft as FunctionType*).return_type; }
        if (via_sig.id != old.id) {
            fmt::eprintf("SHADOW\\tFNRET2-DIFF\\t%s\\t%llu\\t%llu\\n", this.intern.resolve(name), old.id, via_sig.id);
        }
        return old;
    }
""")])

edit(R + r"\sema\call_resolver.cryo", [(
"""                        pinned_ft = this.ctx.decl_index.lookup_func_type(fam);
                        if (!pinned_ft.is_valid()) {
                            const spec_ret: TypeRef = this.types.lookup_func_return(fam);
                            if (spec_ret.is_valid()) { return spec_ret; }
                        }
""",
"""                        pinned_ft = this.ctx.decl_index.lookup_func_type(fam);
                        if (!pinned_ft.is_valid()) {
                            const spec_ret: TypeRef = this.types.lookup_func_return(fam);
                            if (spec_ret.is_valid()) {
                                fmt::eprintf("SHADOW\\tFAMRET-RESCUE\\t%s\\n", this.intern.resolve(fam));
                                return spec_ret;
                            }
                        }
""")])
print("ok")
