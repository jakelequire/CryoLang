p = 'compiler/src/compiler/mono/dispatch_annotator.cryo'
s = open(p, encoding='utf-8', newline='').read()
def rep(old, new, count=1):
    global s
    assert s.count(old) == count, (old[:70], s.count(old))
    s = s.replace(old, new)
rep("""import compiler::resolver::symbol_str::{ SymbolStr };
""", """import compiler::resolver::symbol_str::{ SymbolStr };
import compiler::resolver::symbol_id;
import compiler::resolver::symbol_id::{ SymbolID };
""")
rep("""                const head: SymbolStr = this.param_head_of_ann(param.type_annotation);
                if (head.is_valid()) {
                    const tr: SymbolStr = this.single_bound_trait_for(head, bounds);
""", """                const head: SymbolID = this.param_head_of_ann(param.type_annotation);
                if (head.is_valid()) {
                    const tr: SymbolStr = this.single_bound_trait_for(head, bounds);
""")
rep("""        const tr: SymbolStr = this.single_bound_trait_for(sr.scope_name, bounds);
        if (tr.is_valid()) { sr.set_resolved_trait(tr); }
    }

    /// Return the identity of the single trait bounding `param_name`, or
    /// empty when the param has zero or more-than-one bound trait (ambiguous -
    /// leave the dispatch to its default).  The bound's stamped identity, not
    /// its written leaf: two traits may share a leaf, and the stamp on the
    /// call is compared against a method's `origin_trait`.
    single_bound_trait_for(&this, param_name: SymbolStr,
                           bounds: &TraitBound[]) -> SymbolStr {
        mut found: SymbolStr = SymbolStr::empty();
        mut count: i64 = 0;
        for (mut i: i64 = 0; i < bounds.length; i++) {
            const b: TraitBound* = &bounds[i];
            if (!b.type_parameter.equals(param_name)) { continue; }
""", """        const tr: SymbolStr = this.single_bound_trait_for(
            sr.scope_res.relative_param_sym(), bounds);
        if (tr.is_valid()) { sr.set_resolved_trait(tr); }
    }

    /// Return the identity of the single trait bounding the parameter `param`,
    /// or empty when the param has zero or more-than-one bound trait
    /// (ambiguous - leave the dispatch to its default), or when `param` names
    /// no parameter.  The bound's stamped identity, not its written leaf: two
    /// traits may share a leaf, and the stamp on the call is compared against
    /// a method's `origin_trait`.
    single_bound_trait_for(&this, param: SymbolID,
                           bounds: &TraitBound[]) -> SymbolStr {
        mut found: SymbolStr = SymbolStr::empty();
        mut count: i64 = 0;
        if (!param.is_valid()) { return found; }
        for (mut i: i64 = 0; i < bounds.length; i++) {
            const b: TraitBound* = &bounds[i];
            if (!b.subject_sym.equals(param)) { continue; }
""")
rep("""    param_head_of_ann(&this, ann: TypeAnnotation*) -> SymbolStr {
        if (ann == null) { return SymbolStr::empty(); }
        return match (*ann) {
            TypeAnnotation::Named(na)     => { na.name }
            TypeAnnotation::Generic(ga)   => { this.param_head_of_ann(ga.base) }
            TypeAnnotation::Reference(ra) => { this.param_head_of_ann(ra.inner) }
            TypeAnnotation::Pointer(pa)   => { this.param_head_of_ann(pa.inner) }
            _                             => { SymbolStr::empty() }
        };
    }
""", """    param_head_of_ann(&this, ann: TypeAnnotation*) -> SymbolID {
        if (ann == null) { return SymbolID::invalid(); }
        return match (*ann) {
            TypeAnnotation::Named(na)     => { na.param_sym() }
            TypeAnnotation::Generic(ga)   => { this.param_head_of_ann(ga.base) }
            TypeAnnotation::Reference(ra) => { this.param_head_of_ann(ra.inner) }
            TypeAnnotation::Pointer(pa)   => { this.param_head_of_ann(pa.inner) }
            _                             => { SymbolID::invalid() }
        };
    }
""")
open(p, 'w', encoding='utf-8', newline='').write(s)
print("ok")
