"""Shadow the six lookup-then-re-register pairs in register_decl_in_index.
Each `const ty = lookup_type(qualified_sym); if (ty.is_valid()) { register_type(qualified_sym, ty); }`
gets a SHADOW line BEFORE the re-registration saying whether the reverse map
already names the key (agree) or not (DISAGREE, with both names)."""
import io, re
P = "compiler/src/compiler/passes/type_resolution.cryo"
t = io.open(P, encoding="utf-8", newline="").read()
nl = "\r\n" if "\r\n" in t[:4000] else "\n"
old = ("                const ty: TypeRef = ctx.decl_index.lookup_type(qualified_sym);" + nl +
       "                if (ty.is_valid()) {" + nl +
       "                    ctx.decl_index.register_type(qualified_sym, ty);" + nl +
       "                }" + nl)
assert t.count(old) == 6, t.count(old)
kinds = ["struct", "union", "class", "enum", "trait", "alias"]
out = []
pos = 0
for k in kinds:
    i = t.index(old, pos)
    out.append(t[pos:i])
    out.append(old.replace("                if (ty.is_valid()) {",
        '                TypeResolutionPasses::shadow_rdi(ctx, "%s", is_source_decl, qualified_sym, ty);' % k + nl +
        "                if (ty.is_valid()) {"))
    pos = i + len(old)
out.append(t[pos:])
t = "".join(out)
helper = ("    static register_decl_in_index(stmt: ASTNode*, ctx: CompilationContext*," + nl)
assert t.count(helper) == 1
shadow = (
"    static shadow_rdi(ctx: CompilationContext*, kind: string, src: boolean, key: SymbolStr, ty: TypeRef) -> void {" + nl +
"        const prev: SymbolStr = ctx.decl_index.lookup_type_name(ty);" + nl +
"        const who: string = if (src) { \"src\" } else { \"clone\" };" + nl +
"        if (ty.is_valid() && prev.id == key.id) {" + nl +
"            fmt::eprintf(\"SHADOW\\tRDI\\tagree\\t%s\\t%s\\n\", kind, who);" + nl +
"        } else {" + nl +
"            const prev_s: string = if (ty.is_valid()) { ctx.intern_table.resolve(prev) } else { \"-\" };" + nl +
"            const state: string = if (ty.is_valid()) { \"valid\" } else { \"INVALID\" };" + nl +
"            fmt::eprintf(\"SHADOW\\tRDI\\tDISAGREE\\t%s\\t%s\\t%s\\t%s\\t%s\\n\", kind, who, ctx.intern_table.resolve(key), prev_s, state);" + nl +
"        }" + nl +
"    }" + nl + nl)
t = t.replace(helper, shadow + helper)
io.open(P, "w", encoding="utf-8", newline="").write(t)
print("shadowed 6 arms; eol", repr(nl))
