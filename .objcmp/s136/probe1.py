p = "C:/Programming/apps/CryoLang/compiler/src/compiler/types/generic_registry.cryo"
s = open(p, encoding="utf-8", newline="").read()
old = "                    Res::Def(d)          => { return d.equals(this.decls.def_of(arg, this.arena)); }\n"
new = (
    "                    Res::Def(d)          => {\n"
    "                        const pk: SymbolStr = this.arena.template_key_of(arg, intern);\n"
    "                        const pold: boolean = pk.is_valid() && pk.equals(this.arena.defs.path_of(d).spelling());\n"
    "                        const pnew: boolean = d.equals(this.decls.def_of(arg, this.arena));\n"
    "                        if (pold == pnew) { fmt::eprintf(\"NRPROBE au agree\\n\"); }\n"
    "                        if (pold != pnew) { fmt::eprintf(\"NRPROBE au DISAGREE\\n\"); }\n"
    "                        return pnew;\n"
    "                    }\n"
)
assert s.count(old) == 1
s = s.replace(old, new)
s = s.replace("import utils::text::{ Text };\n", "import utils::text::{ Text };\nimport std::fmt;\n", 1)
open(p, "w", encoding="utf-8", newline="").write(s)
print("probe in")
