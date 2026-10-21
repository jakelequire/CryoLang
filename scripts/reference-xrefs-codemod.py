"""Renumber textual references to docs/cryo.md sections 10-25 after section 9
was deleted.  Each (file, old, new) must match exactly once.  A one-off
codemod, run after scripts/reference-renumber-codemod.py, and deleted in the
commit after the one that ran it."""
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__))).replace("\\", "/") + "/"
EDITS = [
    ("CHANGELOG.md", "`docs/cryo.md` section 11.5]", "`docs/cryo.md` section 10.5]"),
    ("CHANGELOG.md", "`docs/cryo.md` section 19](./docs/cryo.md#18", "`docs/cryo.md` section 18](./docs/cryo.md#18"),
    ("CHANGELOG.md", "`docs/cryo.md` section 22](./docs/cryo.md#21", "`docs/cryo.md` section 21](./docs/cryo.md#21"),
    ("README.md", "`docs/cryo.md` section 16.4]", "`docs/cryo.md` section 15.4]"),
    ("README.md", "`docs/cryo.md` section 19]", "`docs/cryo.md` section 18]"),
    ("README.md", "`docs/cryo.md` section 22]", "`docs/cryo.md` section 21]"),
    ("docs/async-internals.md", "see [section 19 of the language", "see [section 18 of the language"),
    ("docs/grammar.md", "see cryo.md section 15.2 *)", "see cryo.md section 14.2 *)"),
    ("docs/grammar.md", "`async` body; see cryo.md section 19 *)", "`async` body; see cryo.md section 18 *)"),
    ("docs/grammar.md", "semantics yet (cryo.md section 22) *)", "semantics yet (cryo.md section 21) *)"),
    ("docs/grammar.md", "(cryo.md section 11.7) *)", "(cryo.md section 10.7) *)"),
    ("docs/grammar.md", "are implemented (cryo.md section 19).", "are implemented (cryo.md section 18)."),
    ("docs/resolution-rules.md", "as `docs/cryo.md` section 14.5 says", "as `docs/cryo.md` section 13.5 says"),
    ("stdlib/sys/_module.cryo", "(docs/cryo.md section 14.2)", "(docs/cryo.md section 13.2)"),
    ("compiler/src/compiler/codegen/visit/new_delete_emitter.cryo", "/// section 15.2 (use", "/// section 14.2 (use"),
    ("tests/tests/lang/autoref_arg_drops.cryo", "(cryo.md section 16.4:", "(cryo.md section 15.3:"),
    ("tests/tests/lang/async_carry_across_await.cryo", "`docs/cryo.md` section 16.2:", "`docs/cryo.md` section 15.2:"),
    ("tests/tests/lang/memory_layout.cryo", "See docs/cryo.md section 17.3.", "See docs/cryo.md section 16.3."),
    ("tests/tests/lang/new_array.cryo", "See docs/cryo.md section 15.2.", "See docs/cryo.md section 14.2."),
    ("tests/tests/projects/namespace_gate/src/orphan.cryo", "(docs/cryo.md section 14.7)", "(docs/cryo.md section 13.7)"),
    ("tests/tests/projects/plural_leaf_gate/cryoconfig", "# docs/cryo.md section 14.7:", "# docs/cryo.md section 13.7:"),
    ("tests/tests/projects/reexport_plural_function/cryoconfig", "docs/cryo.md section 14.5)", "docs/cryo.md section 13.5)"),
    ("tests/tests/projects/resolution_leaf_index/cryoconfig", "docs/cryo.md section 14.7 require", "docs/cryo.md section 13.7 require"),
    ("tests/tests/projects/sizeof_undeclared_type/cryoconfig", "(docs/cryo.md 12.2:", "(docs/cryo.md 11.2:"),
]

failed = False
for path, old, new in EDITS:
    with open(ROOT + path, encoding="utf-8", newline="") as f:
        body = f.read()
    n = body.count(old)
    if n != 1:
        print(f"SKIP {path}: {n} matches for {old!r}")
        failed = True
        continue
    with open(ROOT + path, "w", encoding="utf-8", newline="") as f:
        f.write(body.replace(old, new))
    print(f"ok   {path}: {new}")
sys.exit(1 if failed else 0)
