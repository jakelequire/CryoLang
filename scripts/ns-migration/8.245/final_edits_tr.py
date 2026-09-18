"""Unit 1 final edits, type_resolution.cryo only (steps 1-4 already applied)."""
import io
p = r"C:\Programming\apps\CryoLang\compiler\src\compiler\passes\type_resolution.cryo"
src = io.open(p, encoding="utf-8", newline="").read()

def cut(start_marker, end_marker, must_contain):
    global src
    a = src.index(start_marker)
    b = src.index(end_marker)
    assert a < b
    block = src[a:b]
    assert block.count(must_contain) == 1, (must_contain, block.count(must_contain))
    src = src[:a] + src[b:]

# old type_name_key (its doc comment through the line before the _res doc)
cut("    /// Canonical key fragment for a type-position NAME.  Resolution order:",
    "    /// SHADOW: the stamp-keyed form of `type_name_key`.",
    "static type_name_key(")
# old ann_canon_key
cut("    /// Render a type annotation to a stable, canonical string for coherence",
    "    /// Build the canonical impl-head signature for a trait impl: the",
    "static ann_canon_key(")
# old coherence_key_for
cut("    /// Build the canonical impl-head signature for a trait impl: the",
    "    /// SHADOW: `coherence_key_for` over the stamp-keyed renderers.",
    "static coherence_key_for(")

def rep(old, new, count=1):
    global src
    assert src.count(old) == count, (src.count(old), old[:80])
    src = src.replace(old, new)

rep("""    /// SHADOW: the stamp-keyed form of `type_name_key`.  A definition is
    /// asked of the index by its id, a primitive by its spelling; every other
    /// answer names no declaration and renders as the spelling.
    static type_name_key_res(name: SymbolStr, res: Res, node: ImplBlockNode*,
                             ctx: CompilationContext*) -> string {""",
"""    /// Canonical key fragment for a type-position NAME.  Resolution order:
    ///   1. an impl-head generic param -> positional `#n` marker;
    ///   2. a name whose stamp names a declaration -> that type's canonical
    ///      id (`#T<id>`), asked of the index by the id, which folds every
    ///      spelling of one type to one key and keeps two same-leaf types of
    ///      different modules apart - a leaf folded by its spelling could do
    ///      neither once two modules declared it; a primitive by its
    ///      spelling, which is its registration key;
    ///   3. otherwise the spelling itself: a projection (`I::Item`), a
    ///      parameter, or a name nothing stamped.  The spelling is never
    ///      looked up: a name the writer's module did not resolve is not one
    ///      the module being compiled may resolve for it.
    static type_name_key(name: SymbolStr, res: Res, node: ImplBlockNode*,
                         ctx: CompilationContext*) -> string {""")
rep("""    /// SHADOW: `ann_canon_key` over `type_name_key_res`.
    static ann_canon_key_res(ann: TypeAnnotation*, node: ImplBlockNode*,
                             ctx: CompilationContext*) -> string {""",
"""    /// Render a type annotation to a stable, canonical string for coherence
    /// keying.  Type-position names fold to their resolved type identity
    /// (see `type_name_key`); the impl's own generic params normalize to
    /// positional `#n` markers; everything else renders structurally.
    /// Purely a string identity - never type-checks - so it is safe to call
    /// this early in TypeResolution.
    static ann_canon_key(ann: TypeAnnotation*, node: ImplBlockNode*,
                         ctx: CompilationContext*) -> string {""")
rep("    /// SHADOW: `coherence_key_for` over the stamp-keyed renderers.\n", "")
rep("static coherence_key_for_res(", "static coherence_key_for(")
n = src.count("TypeResolutionPasses::type_name_key_res(")
src = src.replace("TypeResolutionPasses::type_name_key_res(", "TypeResolutionPasses::type_name_key(")
m = src.count("TypeResolutionPasses::ann_canon_key_res(")
src = src.replace("TypeResolutionPasses::ann_canon_key_res(", "TypeResolutionPasses::ann_canon_key(")
print("renamed calls", n, m)

cmp = """                    {
                        const coh_key2: string =
                            TypeResolutionPasses::coherence_key_for_res(
                                node, canonical_target, ctx);
                        if (coh_key2 != coh_key) {
                            fmt::eprintf("SHADOW\\tCOHKEY-DIFF\\t%s\\t%s\\n", coh_key, coh_key2);
                        }
                    }
"""
rep(cmp, "")
assert "key_res(" not in src and "for_res(" not in src and "SHADOW" not in src
assert src.count("static type_name_key(") == 1
assert src.count("static ann_canon_key(") == 1
assert src.count("static coherence_key_for(") == 1
io.open(p, "w", encoding="utf-8", newline="").write(src)
print("ok")
