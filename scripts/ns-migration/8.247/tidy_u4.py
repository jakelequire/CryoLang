"""One unwrap per declaration arm; the self-test's arena mutation expects 2 -> 3."""
import io, re
R = r"C:\Programming\apps\CryoLang"
P = R + r"\compiler\src\compiler\passes\specialization.cryo"
src = io.open(P, encoding="utf-8", newline="").read()
old_a = """                        SpecializationPasses::register_static_method_templates(node.def.qualified_name(), node.methods,
                            registry, arena, ctx);"""
new_a = """                        const owner_key: SymbolStr = node.def.qualified_name();
                        SpecializationPasses::register_static_method_templates(owner_key, node.methods,
                            registry, arena, ctx);"""
assert src.count(old_a) == 3
src = src.replace(old_a, new_a)
old_b = "SpecializationPasses::register_inherent_owner(node.def.qualified_name(), stmt, registry);"
new_b = "SpecializationPasses::register_inherent_owner(owner_key, stmt, registry);"
assert src.count(old_b) == 3
src = src.replace(old_b, new_b)
io.open(P, "w", encoding="utf-8", newline="").write(src)

S = R + r"\scripts\lane-gate-selftest.py"
s = io.open(S, encoding="utf-8", newline="").read()
old = '     1, "ARENA_READ TOTAL 1 -> 2"),'
assert s.count(old) == 1
s = s.replace(old, '     1, "ARENA_READ TOTAL 2 -> 3"),')
io.open(S, "w", encoding="utf-8", newline="").write(s)
print("ok")
