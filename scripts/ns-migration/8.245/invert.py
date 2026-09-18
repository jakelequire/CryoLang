"""Flip every shadow condition (control by inversion) or restore it.
Usage: python invert.py flip|restore"""
import io, sys
R = r"C:\Programming\apps\CryoLang\compiler\src\compiler"
EDITS = [
    (R + r"\types\resolver.cryo",
     "                if (!stamp_hit.is_valid()) {\n                    stamp_hit = this.arena.lookup_by_name(stamp.qualified_name());",
     "                if (stamp_hit.is_valid()) { /* INVERTED */\n                    stamp_hit = this.arena.lookup_by_name(stamp.qualified_name());"),
    (R + r"\sema\call_resolver.cryo",
     "            if (via_sig.id != func_ret.id) {",
     "            if (via_sig.id == func_ret.id) { /* INVERTED */"),
    (R + r"\passes\type_resolution.cryo",
     "                        if (coh_key2 != coh_key) {",
     "                        if (coh_key2 == coh_key) { /* INVERTED */"),
    (R + r"\codegen\visit\pattern_emitter.cryo",
     "                    if (leaf_str != this.cg.resolve(pat.value)) {",
     "                    if (leaf_str == this.cg.resolve(pat.value)) { /* INVERTED */"),
]
mode = sys.argv[1]
for path, a, b in EDITS:
    src = io.open(path, encoding="utf-8", newline="").read()
    old, new = (a, b) if mode == "flip" else (b, a)
    assert src.count(old) == 1, (path, src.count(old))
    src = src.replace(old, new)
    io.open(path, "w", encoding="utf-8", newline="").write(src)
print(mode, "ok")
