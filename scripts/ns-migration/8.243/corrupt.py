"""Write four copies of a ledger, each carrying one of the check-cell
corruptions §8.243 found - the two the audit named (D5's spliced cell and the
trait-registry row's pasted fragment, as at fd3756af) and the two the
structural rules found beyond them (an expected value in backticks, which the
extractor never ran; an unescaped pipe in a check, an extra table column).

    python3 scripts/ns-migration/8.243/corrupt.py docs/name-resolution.md <outdir>

Each copy is the ledger given with ONE substitution, asserted to match once.
"""
import io
import os
import sys

src, outdir = sys.argv[1], sys.argv[2]
text = io.open(src, encoding="utf-8").read()
os.makedirs(outdir, exist_ok=True)

CASES = {
    "d5.md": (
        "`grep -rho 'check_module_type_collision' compiler/src \\| wc -l` → **0**;",
        "`grep -rho 'check_module_type_collision' compiler/src \\|`grep -rho -e trait_impls_typed"
        " -e precise_trait_leaf compiler/src --include=*.cryo | wc -l` → **0**;"),
    "row109.md": (
        "compiler/src/compiler/types/generic_registry.cryo` → **5**; `grep -c -e 'select_impl(&this'",
        "compiler/src/compiler/types/generic_registry.cryo` → **5**|heads_for(&this\\|trait_`grep -c"
        " -e 'select_impl(&this' -e 'impls_of(&this' -e 'impl_applies(&this'"
        " compiler/src/compiler/types/trait_checker.cryo`"
        " → **3**|template_head_for(&this\\|template_trait_impls_of(&this'"
        " compiler/src/compiler/types/generic_registry.cryo` → **5**; `grep -c -e 'select_impl(&this'"),
    "d6.md": (
        "--names @stdlib/core/intrinsics.cryo --check` → **0**;",
        "--names @stdlib/core/intrinsics.cryo` → `0 rewritten`;"),
    "pipe.md": (
        "new expr: calls`; `grep -rho 'is_alias_keyword' compiler/src --include=*.cryo \\| wc -l`",
        "new expr: calls`; `grep -rho 'is_alias_keyword' compiler/src --include=*.cryo | wc -l`"),
}

for name, (old, new) in CASES.items():
    assert text.count(old) == 1, (name, text.count(old))
    io.open(os.path.join(outdir, name), "w", encoding="utf-8", newline="").write(text.replace(old, new))
    print("wrote", name)
