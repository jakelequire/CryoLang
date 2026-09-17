"""Delete the shadow and the six lookup-then-re-register pairs in
register_decl_in_index (measured: 37,976 re-registrations over six halves,
every one re-inserting the key the map already held with the reverse map
already naming it)."""
import io, re
P = "compiler/src/compiler/passes/type_resolution.cryo"
t = io.open(P, encoding="utf-8", newline="").read()
nl = "\n"
# 1. the shadow helper
start = t.index("    static shadow_rdi(")
end = t.index("    static register_decl_in_index(")
assert 0 < end - start < 1200
t = t[:start] + t[end:]
# 2. the six blocks
n = 0
for k in ["struct", "union", "class", "enum", "trait", "alias"]:
    blk = ("                const ty: TypeRef = ctx.decl_index.lookup_type(qualified_sym);" + nl +
           '                TypeResolutionPasses::shadow_rdi(ctx, "%s", is_source_decl, qualified_sym, ty);' % k + nl +
           "                if (ty.is_valid()) {" + nl +
           "                    ctx.decl_index.register_type(qualified_sym, ty);" + nl +
           "                }" + nl)
    assert t.count(blk) == 1, (k, t.count(blk))
    t = t.replace(blk, "")
    n += 1
assert "shadow_rdi" not in t
io.open(P, "w", encoding="utf-8", newline="").write(t)
print("deleted helper and", n, "blocks")
