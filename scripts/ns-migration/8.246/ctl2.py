"""Control for the family-pin arm: print on entry (flip) / remove (restore)."""
import io, sys
p = r"C:\Programming\apps\CryoLang\compiler\src\compiler\sema\call_resolver.cryo"
src = io.open(p, encoding="utf-8", newline="").read()
a = ("                    CalleePin::Family(fam) => {\n"
     "                        pinned_ft = this.ctx.decl_index.lookup_func_type(fam);\n")
b = ("                    CalleePin::Family(fam) => {\n"
     "                        fmt::eprintf(\"SHADOW\\tFAMPIN-ARM\\n\"); /* CONTROL */\n"
     "                        pinned_ft = this.ctx.decl_index.lookup_func_type(fam);\n")
old, new = (a, b) if sys.argv[1] == "flip" else (b, a)
assert src.count(old) == 1, src.count(old)
io.open(p, "w", encoding="utf-8", newline="").write(src.replace(old, new))
print(sys.argv[1], "ok")
