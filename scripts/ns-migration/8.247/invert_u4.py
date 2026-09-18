"""Flip unit 4's four shadow conditions (control by inversion) or restore them."""
import io, sys
P = r"C:\Programming\apps\CryoLang\compiler\src\compiler\passes\specialization.cryo"
EDITS = [
    ("            if (!stamp_key.equals(qualified_sym)) {", "            if (stamp_key.equals(qualified_sym)) { /* INVERTED */"),
    ("            if (stamp_base.id != name_base.id) {", "            if (stamp_base.id == name_base.id) { /* INVERTED */"),
    ("        if (!owner_key.equals(owner_qsym)) {", "        if (owner_key.equals(owner_qsym)) { /* INVERTED */"),
    ("        if (!owner_key.equals(ctx.decl_type_key(owner_name, SymbolStr::empty(), owner_ast.span.file))) {",
     "        if (owner_key.equals(ctx.decl_type_key(owner_name, SymbolStr::empty(), owner_ast.span.file))) { /* INVERTED */"),
]
mode = sys.argv[1]
src = io.open(P, encoding="utf-8", newline="").read()
for a, b in EDITS:
    old, new = (a, b) if mode == "flip" else (b, a)
    assert src.count(old) == 1, (src.count(old), old)
    src = src.replace(old, new)
io.open(P, "w", encoding="utf-8", newline="").write(src)
print(mode, "ok")
