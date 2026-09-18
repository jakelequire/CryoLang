"""Unit 1 final edits: the shadows removed, the old paths deleted.
Every anchor must match exactly once."""
import io
R = r"C:\Programming\apps\CryoLang\compiler\src\compiler"


def edit(path, pairs):
    src = io.open(path, encoding="utf-8", newline="").read()
    for old, new in pairs:
        assert src.count(old) == 1, (path, src.count(old), old[:80])
        src = src.replace(old, new)
    io.open(path, "w", encoding="utf-8", newline="").write(src)


# 1. type_utils: the Def arm asks by id; the miss is not a recorded defect
#    while an `extern module` alias qualifier is answered as a definition.
edit(R + r"\sema\type_utils.cryo", [
("""        mut q: SymbolStr = SymbolStr::empty();
        match (base) {
            ResBase::Def(d)    => {
                const found: TypeRef = this.ctx.decl_index.type_of_def(d);
                if (!found.is_valid()) {
                    fmt::eprintf("SHADOW\\tSCOPEQUAL-MISS\\t%s\\n", this.intern.resolve(d.qualified_name()));
                }
                return found;
            }
            ResBase::PrimTy(n) => { q = n; }
""",
"""        mut q: SymbolStr = SymbolStr::empty();
        match (base) {
            // By the id, as an impl head's owner is read.  Not through
            // `def_type`'s recording door: the name layer answers an `extern
            // module` alias qualifier (`probe::add3`) as a type-relative
            // definition, and the index holds no type under an alias - its
            // members are the C-import door's.  Invalid is the answer there,
            // not a defect, until the alias has an answer of its own.
            ResBase::Def(d)    => { return this.ctx.decl_index.type_of_def(d); }
            ResBase::PrimTy(n) => { q = n; }
"""),
])

# 2. types/resolver: the index answers the stamp by id; the arena is not a
#    second place to ask (0 answers over six halves; the control fired 7,522).
edit(R + r"\types\resolver.cryo", [
("""            if (stamp.is_valid()) {
                // What remains is only WHERE the name's `TypeRef` is stored.
                // The DeclarationIndex indexes the qualified NAME without
                // necessarily holding the type, and the arena is the other
                // place, so a miss in one is not an answer.
                if (this.decl_index != null) {
                    stamp_hit = this.decl_index.type_of_def(stamp);
                }
                if (!stamp_hit.is_valid()) {
                    stamp_hit = this.arena.lookup_by_name(stamp.qualified_name());
                    fmt::eprintf("SHADOW\\t%s\\t%s\\n",
                        if (stamp_hit.is_valid()) { "STAMP-ARENA" } else { "STAMP-MISS" },
                        this.intern_table.resolve(stamp.qualified_name()));
                }
""",
"""            if (stamp.is_valid() && this.decl_index != null) {
                // The index holds every registered type under the id the
                // stamp carries; a definition it does not hold is one that
                // never registered, and the arena's written-name map is not
                // a second place to ask for it.
                stamp_hit = this.decl_index.type_of_def(stamp);
"""),
])

# 3. call_resolver: the return type off the stamp's signature, one map.
edit(R + r"\sema\call_resolver.cryo", [
("""        mut stamped_q: SymbolStr = SymbolStr::empty();
        mut stamped_r: Res = Res::Err;
        match (ident.res) {
""",
"""        mut stamped_r: Res = Res::Err;
        match (ident.res) {
"""),
("""                    Res::Err => { return TypeRef::invalid(); }
                    _        => { stamped_q = r.def_id().qualified_name(); stamped_r = r; }
                }
            }
            _ => {}
        }
        mut func_ret: TypeRef = TypeRef::invalid();
        if (stamped_q.is_valid()) {
            func_ret = this.types.lookup_func_return(stamped_q);
            const via_sig: TypeRef = this.overload_return_type(
                this.ctx.decl_index.func_type_of_res(stamped_r));
            if (via_sig.id != func_ret.id) {
                fmt::eprintf("SHADOW\\tFNRET-DIFF\\t%s\\t%llu\\t%llu\\n",
                    this.intern.resolve(stamped_q), func_ret.id, via_sig.id);
            }
        }
""",
"""                    Res::Err => { return TypeRef::invalid(); }
                    _        => { stamped_r = r; }
                }
            }
            _ => {}
        }
        // The return type is the stamped signature's own, asked by the id.
        const func_ret: TypeRef = this.overload_return_type(
            this.ctx.decl_index.func_type_of_res(stamped_r));
"""),
])

# 4. pattern_emitter: one splitter, the index's.
edit(R + r"\codegen\visit\pattern_emitter.cryo", [
("""                Res::Def(d)   => {
                    const q_str: string = this.cg.resolve(d.qualified_name());
                    const ns_str: string = QualifiedName::parent_of(q_str);
                    const ns_sym: SymbolStr = if (ns_str.length() > 0) { this.cg.intern_str(ns_str) } else { SymbolStr::empty() };
                    slot = this.cg.resolver.resolve_global_in_namespace(pat.value, ns_sym);
                    const leaf_str: string = QualifiedName::leaf_of(q_str);
                    if (leaf_str != this.cg.resolve(pat.value)) {
                        fmt::eprintf("SHADOW\\tPATLEAF-DIFF\\t%s\\t%s\\n", q_str, this.cg.resolve(pat.value));
                    }
                }
""",
"""                Res::Def(d)   => {
                    slot = this.cg.resolver.resolve_global_by_qualified(d.qualified_name());
                }
"""),
])

# 5. type_resolution: the stamp-keyed renderers replace the name-keyed ones.
p = R + r"\passes\type_resolution.cryo"
src = io.open(p, encoding="utf-8", newline="").read()

# 5a. delete the old type_name_key + ann_canon_key (from the doc comment of
#     type_name_key up to the SHADOW doc line of type_name_key_res).
a = src.index("    /// Canonical key fragment for a type-position NAME.  Resolution order:")
b = src.index("    /// SHADOW: the stamp-keyed form of `type_name_key`.")
old_block = src[a:b]
assert old_block.count("static type_name_key(") == 1 and old_block.count("static ann_canon_key(") == 1
src = src[:a] + src[b:]

# 5b. rename the _res forms and rewrite their doc comments
src = src.replace("""    /// SHADOW: the stamp-keyed form of `type_name_key`.  A definition is
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
assert src.count("static type_name_key(") == 1
src = src.replace("""    /// SHADOW: `ann_canon_key` over `type_name_key_res`.
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
assert src.count("static ann_canon_key(") == 1
src = src.replace("TypeResolutionPasses::type_name_key_res(", "TypeResolutionPasses::type_name_key(")
src = src.replace("TypeResolutionPasses::ann_canon_key_res(", "TypeResolutionPasses::ann_canon_key(")
assert "_res(" not in src.replace("coherence_key_for_res(", "")

# 5c. delete the OLD coherence_key_for (doc comment through its closing brace)
a = src.index("    /// Build the canonical impl-head signature for a trait impl: the")
b = src.index("    /// SHADOW: `coherence_key_for` over the stamp-keyed renderers.")
old_coh = src[a:b]
assert old_coh.count("static coherence_key_for(") == 1, old_coh.count("static coherence_key_for(")
src = src[:a] + src[b:]
src = src.replace("    /// SHADOW: `coherence_key_for` over the stamp-keyed renderers.\n", "")
src = src.replace("static coherence_key_for_res(", "static coherence_key_for(")
assert src.count("static coherence_key_for(") == 1

# 5d. the comparison at the caller
cmp = """                    {
                        const coh_key2: string =
                            TypeResolutionPasses::coherence_key_for_res(
                                node, canonical_target, ctx);
                        if (coh_key2 != coh_key) {
                            fmt::eprintf("SHADOW\\tCOHKEY-DIFF\\t%s\\t%s\\n", coh_key, coh_key2);
                        }
                    }
"""
assert src.count(cmp) == 1
src = src.replace(cmp, "")
assert "coherence_key_for_res" not in src and "SHADOW" not in src
io.open(p, "w", encoding="utf-8", newline="").write(src)
print("ok")
