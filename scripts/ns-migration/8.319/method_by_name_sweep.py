"""Replace hand-written "the method in this list spelled N" loops with the
existing `NodeLocator::method_by_name`, at the sites where the loop is exactly
that question: first match, null element or null `func` skipped, compared on
`m.func.name`, no other filter.

Left out on purpose (each its own unit): loops that also filter by arity,
`is_static`, `is_async` or generic-ness; loops that count matches and refuse a
tie; loops whose answer is the first impl BLOCK in registration order
(`method_binding.cryo`'s projection-bound scan, `trait_checker.cryo`'s bound
check), even where their inner loop is a plain first match; and the body of a
registry lookup itself (`GenericRegistry::inherent_impl_has_method`).

Every edit is an exact (old, new) pair that must match once; a drifted tree is
refused, not half-edited.  Run from the repo root:
`python scripts/ns-migration/8.319/method_by_name_sweep.py`.
"""
import sys

ROOT = "compiler/src/compiler/"
IMPORT = "import compiler::ast::node_locator::{ NodeLocator };"

TR_EXIST = """            mut {flag}: boolean = false;
            for (mut mi: i64 = 0; mi < impl_node.methods.length; mi++) {{
                const m: MethodNode* = impl_node.methods[mi];
                if (m != null && m.func != null && m.func.name.equals(tfunc.name)) {{
                    {flag} = true;
                    break;
                }}
            }}
"""
TR_NEW = """            const {flag}: boolean =
                NodeLocator::method_by_name(&impl_node.methods, tfunc.name) != null;
"""

TEMPLATE_OLD = """            const d: {T}* = ast_node as {T}*;
            for (mut i: i64 = 0; i < d.methods.length; i++) {{
                const m: MethodNode* = d.methods[i];
                if (m != null && m.func != null && m.func.name.equals(name)) {{ return m.func; }}
            }}
"""
TEMPLATE_NEW = """            const d: {T}* = ast_node as {T}*;
            const m: MethodNode* = NodeLocator::method_by_name(&d.methods, name);
            if (m != null) {{ return m.func; }}
"""

DROP_OLD = """            const decl: {T}* = node as {T}*;
            for (mut i: i64 = 0; i < decl.methods.length; i++) {{
                const m: MethodNode* = decl.methods[i];
                if (m == null || m.func == null) {{ continue; }}
                if (m.func.name.equals(drop_sym)) {{ return true; }}
            }}
            return false;
"""
DROP_NEW = """            const decl: {T}* = node as {T}*;
            return NodeLocator::method_by_name(&decl.methods, drop_sym) != null;
"""

DECLS = ("StructDeclNode", "UnionDeclNode", "ClassDeclNode")

EDITS = {
    "passes/type_resolution.cryo": [
        (TR_EXIST.format(flag="overridden"), TR_NEW.format(flag="overridden")),
        (TR_EXIST.format(flag="already_present"), TR_NEW.format(flag="already_present")),
    ],
    "sema/call_resolver.cryo": [
        (TEMPLATE_OLD.format(T=t), TEMPLATE_NEW.format(T=t)) for t in DECLS
    ],
    "types/ownership.cryo": [
        (DROP_OLD.format(T=t), DROP_NEW.format(T=t)) for t in DECLS
    ] + [
        ("""            for (mut j: i64 = 0; j < ib.methods.length; j++) {
                const m: MethodNode* = ib.methods[j];
                if (m == null || m.func == null) { continue; }
                if (m.func.name.equals(drop_sym)) { return true; }
            }
""",
         """            if (NodeLocator::method_by_name(&ib.methods, drop_sym) != null) { return true; }
"""),
    ],
    "sema/method_binding.cryo": [
        ("""            for (mut k: i64 = 0; k < block.methods.length; k++) {
                const m: MethodNode* = block.methods[k];
                if (m != null && m.func != null && m.func.name.equals(method_name)) {
                    declared_by_head = true;
                    break;
                }
            }
            if (declared_by_head) { break; }
""",
         """            if (this.methods_declare(&block.methods, method_name)) {
                declared_by_head = true;
                break;
            }
"""),
        ("""    methods_declare(&this, methods: &MethodNode*[], name: SymbolStr) -> boolean {
        for (mut i: i64 = 0; i < methods.length; i++) {
            const m: MethodNode* = methods[i];
            if (m != null && m.func != null && m.func.name.equals(name)) { return true; }
        }
        return false;
    }
""",
         """    methods_declare(&this, methods: &MethodNode*[], name: SymbolStr) -> boolean {
        return NodeLocator::method_by_name(methods, name) != null;
    }
"""),
    ],
}


def main() -> int:
    bad = 0
    out = {}
    for rel, pairs in EDITS.items():
        path = ROOT + rel
        with open(path, encoding="utf-8", newline="") as fh:
            src = fh.read()
        nl = "\r\n" if "\r\n" in src else "\n"
        for old, new in pairs:
            old_n, new_n = old.replace("\n", nl), new.replace("\n", nl)
            if src.count(old_n) != 1:
                print(f"REFUSED {rel}: {src.count(old_n)} matches for {old.splitlines()[0]!r}...")
                bad += 1
                continue
            src = src.replace(old_n, new_n)
        if IMPORT not in src:
            lines = src.split(nl)
            at = [i for i, l in enumerate(lines)
                  if l.startswith("import compiler::ast") and l.rstrip().endswith(";")]
            if not at:
                print(f"REFUSED {rel}: no single-line compiler::ast import to sit beside")
                bad += 1
                continue
            lines.insert(at[-1] + 1, IMPORT)
            src = nl.join(lines)
        out[path] = src
    if bad:
        return 1
    for path, src in out.items():
        with open(path, "w", encoding="utf-8", newline="") as fh:
            fh.write(src)
    return 0


if __name__ == "__main__":
    sys.exit(main())
