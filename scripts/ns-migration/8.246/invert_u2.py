"""Flip unit 2's shadow conditions (control by inversion) or restore them."""
import io, sys
R = r"C:\Programming\apps\CryoLang\compiler\src\compiler"
EDITS = [
    (R + r"\sema\type_utils.cryo",
     "        if (via_sig.id != old.id) {",
     "        if (via_sig.id == old.id) { /* INVERTED */"),
    (R + r"\sema\call_resolver.cryo",
     "                            if (spec_ret.is_valid()) {\n                                fmt::eprintf(\"SHADOW\\tFAMRET-RESCUE",
     "                            if (!spec_ret.is_valid()) { /* INVERTED */\n                                fmt::eprintf(\"SHADOW\\tFAMRET-RESCUE"),
]
mode = sys.argv[1]
for path, a, b in EDITS:
    src = io.open(path, encoding="utf-8", newline="").read()
    old, new = (a, b) if mode == "flip" else (b, a)
    assert src.count(old) == 1, (path, src.count(old))
    src = src.replace(old, new)
    io.open(path, "w", encoding="utf-8", newline="").write(src)
print(mode, "ok")
