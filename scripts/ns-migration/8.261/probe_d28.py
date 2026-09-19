"""D28's population: every written trait-impl head, printed where the
overlap refusal would be raised (type resolution's impl arm, beside the
E0308 coherence check), against every written head registered before it
under the same (trait, target) key:

    SHADOW<TAB>D28-ARM<TAB><file>:<line><TAB><trait> for <target>
        the arm visited a written trait impl (a control: one line per
        head, so a head visited twice shows as a duplicate)
    SHADOW<TAB>D28-SIBLING<TAB><file>:<line><TAB><prior file>:<line>
        an earlier written head under the same key (the population the
        unifier must tell apart)
    SHADOW<TAB>D28-OVERLAP<TAB><file>:<line><TAB><prior file>:<line><TAB><trait> for <target><TAB>bounds=<prior>/<this>
        the two heads unify (GenericRegistry::heads_overlap) - what the
        refusal would report at the later head

The unifier itself (`heads_overlap.cryo`, Rust's overlap rule: the two
heads' trait and target arguments unify under one substitution, each
head's parameters as variables, where-bounds not consulted) and its
binding record (`head_binding.cryo`) are spliced into
`types/generic_registry.cryo` by this script, so the tree holds no
uncalled door while the refusal waits on a ruling; the refusal, once
ruled, lands them for real with an E0119 emit at the later head.

Applied over the tree as it stands; `--revert` removes it.
"""
import io, os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = r"C:\Programming\apps\CryoLang"
GR = os.path.join(ROOT, r"compiler\src\compiler\types\generic_registry.cryo")
TR = os.path.join(ROOT, r"compiler\src\compiler\passes\type_resolution.cryo")
revert = "--revert" in sys.argv

def read(p):
    return io.open(p, encoding="utf-8", newline="").read()

binding = read(os.path.join(HERE, "head_binding.cryo"))
unifier = read(os.path.join(HERE, "heads_overlap.cryo"))

GR_EDITS = [
    ("import compiler::ast::{ TypeAnnotation };\n",
     "import compiler::ast::{ TypeAnnotation, GenericAnnotation };\n"),
    ("type struct GenericRegistry {\n",
     binding + "type struct GenericRegistry {\n"),
    ("    /// Register a trait declaration's AST under its canonical identity, so\n",
     unifier + "\n    /// Register a trait declaration's AST under its canonical identity, so\n"),
]

TR_ANCHOR = (
    "                if (is_source_decl && node.is_trait_impl()\n"
    "                        && canonical_target.is_valid()\n"
    "                        && ctx.generic_registry != null) {\n"
    "                    const coh_key: string =\n"
)
TR_PROBE = (
    "                if (is_source_decl && node.is_trait_impl()\n"
    "                        && canonical_target.is_valid()\n"
    "                        && ctx.generic_registry != null) {\n"
    "                    fmt::eprintf(\"SHADOW\\tD28-ARM\\t%s:%u\\t%s for %s\\n\", node.span.file, node.span.start_line, ctx.intern_table.resolve(node.qualified_trait_name), ctx.intern_table.resolve(canonical_target));\n"
    "                    {\n"
    "                        const d28_heads: ImplBlockNode*[] = ctx.generic_registry.heads_under(node.qualified_trait_name, canonical_target);\n"
    "                        for (mut d28i: i64 = 0; d28i < d28_heads.length; d28i++) {\n"
    "                            if (d28_heads[d28i] == node) { break; }\n"
    "                            if (d28_heads[d28i].spec_owner.is_valid()) { continue; }\n"
    "                            fmt::eprintf(\"SHADOW\\tD28-SIBLING\\t%s:%u\\t%s:%u\\n\", node.span.file, node.span.start_line, d28_heads[d28i].span.file, d28_heads[d28i].span.start_line);\n"
    "                            if (GenericRegistry::heads_overlap(d28_heads[d28i], node)) {\n"
    "                                fmt::eprintf(\"SHADOW\\tD28-OVERLAP\\t%s:%u\\t%s:%u\\t%s for %s\\tbounds=%lld/%lld\\n\", node.span.file, node.span.start_line, d28_heads[d28i].span.file, d28_heads[d28i].span.start_line, ctx.intern_table.resolve(node.qualified_trait_name), ctx.intern_table.resolve(canonical_target), d28_heads[d28i].where_bounds.length, node.where_bounds.length);\n"
    "                            }\n"
    "                        }\n"
    "                    }\n"
    "                    const coh_key: string =\n"
)

def apply(path, edits):
    text = read(path)
    for old, new in edits:
        if revert:
            assert text.count(new) == 1, ("not applied", path, old[:50])
            text = text.replace(new, old)
        else:
            assert text.count(old) == 1, ("anchor", path, old[:50], text.count(old))
            text = text.replace(old, new)
    io.open(path, "w", encoding="utf-8", newline="").write(text)

apply(GR, GR_EDITS)
apply(TR, [(TR_ANCHOR, TR_PROBE)])
print("reverted" if revert else "applied")
