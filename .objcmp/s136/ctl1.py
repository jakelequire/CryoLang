import runpy
runpy.run_path("C:/Programming/apps/CryoLang/.objcmp/s136/probe1.py")
p = "C:/Programming/apps/CryoLang/compiler/src/compiler/types/generic_registry.cryo"
s = open(p, encoding="utf-8", newline="").read()
old = "const pnew: boolean = d.equals(this.decls.def_of(arg, this.arena));"
assert s.count(old) == 1
s = s.replace(old, "const pnew: boolean = true;")
open(p, "w", encoding="utf-8", newline="").write(s)
q = "C:/Programming/apps/CryoLang/compiler/src/compiler/sema/identity_text.cryo"
t = open(q, encoding="utf-8", newline="").read()
o2 = "        IdentityTextCheck::answer_overridden(ctx, state, func);\n"
assert t.count(o2) == 1
t = t.replace(o2, "")
open(q, "w", encoding="utf-8", newline="").write(t)
print("ctl1 in")
