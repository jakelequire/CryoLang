"""Give shadow_tdef the intern table so non-AGREE lines carry names."""
import os as _os
_REPO = _os.path.abspath(_os.path.join(_os.path.dirname(__file__), "..", "..", ".."))
import io, os, re
ROOT = _REPO + "/compiler/src/compiler/"
files = ["codegen/ops/declaration_emitter.cryo", "passes/directive_processing.cryo",
         "passes/type_resolution.cryo", "sema/sema.cryo"]
pat = re.compile(r"^(\s*)(\S+)\.shadow_tdef\((.*), (\S+)\.decl_type_key\((.*)\)\);$")
n = 0
for f in files:
    p = ROOT + f
    lines = io.open(p, encoding="utf-8", newline="").read().split("\n")
    out = []
    for ln in lines:
        m = pat.match(ln)
        if m:
            ln = "%s%s.shadow_tdef(%s, %s.decl_type_key(%s), %s.intern_table);" % (
                m.group(1), m.group(2), m.group(3), m.group(4), m.group(5), m.group(4))
            n += 1
        out.append(ln)
    io.open(p, "w", encoding="utf-8", newline="").write("\n".join(out))
assert n == 21, n
DI = ROOT + "decl_index.cryo"
t = io.open(DI, encoding="utf-8", newline="").read()
old = "    shadow_tdef(&this, site: string, d: DefId, key: SymbolStr) -> void {\n"
assert t.count(old) == 1
t = t.replace(old, "    shadow_tdef(&this, site: string, d: DefId, key: SymbolStr, intern: InternTable*) -> void {\n")
for v in ("NOSTAMP", "BOTHINV", "KEYINV", "DISAGREE"):
    pass
t = t.replace("\\tNOSTAMP\\t%u\\n\", site, key.id);", "\\tNOSTAMP\\t%s\\n\", site, intern.resolve(key));")
for v in ("BOTHINV", "KEYINV", "DISAGREE"):
    old = "\\t%s\\t%%u\\t%%u\\n\", site, key.id, d.qualified_name().id);" % v
    assert t.count(old) == 1, (v, t.count(old))
    t = t.replace(old, "\\t%s\\t%%s\\t%%s\\n\", site, intern.resolve(key), intern.resolve(d.qualified_name()));" % v)
assert t.count("intern.resolve(key)") == 4
io.open(DI, "w", encoding="utf-8", newline="").write(t)
print("shadow lines re-armed with names: %d" % n)
