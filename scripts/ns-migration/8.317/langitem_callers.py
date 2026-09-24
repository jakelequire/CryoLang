"""Rewrite every `wellknown(<interned leaf>)` call to `wellknown(LangItem::<Leaf>)`.

Each edit is an exact (old, new) pair per file; a pair that does not match
exactly once refuses the run, so a drifted tree is reported, not half-edited.
Run from the repo root: `python scripts/ns-migration/8.317/langitem_callers.py`.
"""
import sys

ROOT = "compiler/src/compiler/"
IMPORT = "import compiler::types::generic_registry::{ LangItem };"

EDITS = {
    "passes/move_check.cryo": [
        ('wellknown(this.intern.intern("Future"))', "wellknown(LangItem::Future)"),
    ],
    "sema/async_lower.cryo": [
        ('wellknown(this.intern.intern("Poll"))', "wellknown(LangItem::Poll)"),
        ('wellknown(this.intern.intern("Option"))', "wellknown(LangItem::Option)"),
        ("wellknown(future_leaf)", "wellknown(LangItem::Future)"),
    ],
    "sema/member_resolver.cryo": [
        ('        const deref_leaf: SymbolStr   = this.intern.intern("Deref");\n', ""),
        ("wellknown(deref_leaf)", "wellknown(LangItem::Deref)"),
        ('wellknown(this.intern.intern("Index"))', "wellknown(LangItem::Index)"),
    ],
    "sema/sema.cryo": [
        ('wellknown(this.intern.intern("Future"))', "wellknown(LangItem::Future)"),
        ('wellknown(this.intern.intern("Result"))', "wellknown(LangItem::Result)"),
        ('wellknown(this.intern.intern("Option"))', "wellknown(LangItem::Option)"),
    ],
    "types/ownership.cryo": [
        ('wellknown(intern.intern("Drop"))', "wellknown(LangItem::Drop)"),
    ],
    "types/trait_checker.cryo": [
        ('wellknown(this.intern_table.intern("Copy"))', "wellknown(LangItem::Copy)"),
        ('wellknown(this.intern_table.intern("Send"))', "wellknown(LangItem::Send)"),
        ('wellknown(this.intern_table.intern("Sync"))', "wellknown(LangItem::Sync)"),
        ('wellknown(this.intern_table.intern("Drop"))', "wellknown(LangItem::Drop)"),
    ],
}


def main() -> int:
    bad = 0
    for rel, pairs in EDITS.items():
        path = ROOT + rel
        with open(path, encoding="utf-8", newline="") as fh:
            src = fh.read()
        nl = "\r\n" if "\r\n" in src else "\n"
        for old, new in pairs:
            old_n, new_n = old.replace("\n", nl), new.replace("\n", nl)
            if src.count(old_n) != 1:
                print(f"REFUSED {rel}: {old!r} matches {src.count(old_n)} times")
                bad += 1
                continue
            src = src.replace(old_n, new_n)
        if IMPORT not in src:
            lines = src.split(nl)
            # Beside the file's own single-line generic_registry import.
            at = [i for i, l in enumerate(lines)
                  if l.startswith("import compiler::types::generic_registry") and l.rstrip().endswith(";")]
            if not at:
                print(f"REFUSED {rel}: no single-line generic_registry import to sit beside")
                bad += 1
                continue
            lines.insert(at[-1] + 1, IMPORT)
            src = nl.join(lines)
        with open(path, "w", encoding="utf-8", newline="") as fh:
            fh.write(src)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
