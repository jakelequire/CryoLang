"""Control by inversion for the type_of_decl door: the type-declaration
stage's STRUCT arm registers the type but throws the DefId away, so every
struct declaration reaches its readers unstamped.  Applied on top of a
tree, built, and run over a probe (a struct with a method) once under the
readers that skip silently and once under the doors.  Restore with
`git checkout compiler/src/compiler/passes/pass_registry.cryo`."""
import io
import os as _os
_REPO = _os.path.abspath(_os.path.join(_os.path.dirname(__file__), "..", "..", ".."))

P = _REPO + "/compiler/src/compiler/passes/pass_registry.cryo"
t = io.open(P, encoding="utf-8", newline="").read()
old = "node.def = ctx.decl_index.register_type(qualified_sym, struct_ref, node.is_public);"
new = "ctx.decl_index.register_type(qualified_sym, struct_ref, node.is_public);"
assert t.count(old) == 1, t.count(old)
io.open(P, "w", encoding="utf-8", newline="").write(t.replace(old, new))
print("mutated: the struct arm's registration no longer stamps node.def")
