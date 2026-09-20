p = 'compiler/src/compiler/mono/specializer.cryo'
s = open(p, encoding='utf-8', newline='').read()
def rep(old, new, count=1):
    global s
    assert s.count(old) == count, (old[:70], s.count(old))
    s = s.replace(old, new)

rep("""                mut impl_substituter: ASTTypeSubstituter* =
                    this.impl_substituter(impl_node, entry, spec_sym, type_args, &arg_syms, spec_typeref);
""", """                mut impl_substituter: ASTTypeSubstituter* =
                    this.impl_substituter(impl_node, entry, spec_sym, type_args, &arg_syms, spec_typeref);
                // The parameter the head writes at each position of the
                // owner, or none where it writes a type: what a receiver
                // annotation in this block spells the owner's identity in.
                mut head_syms: SymbolID[] = [];
                for (mut hi: i64 = 0; hi < impl_node.target_args.length; hi++) {
                    head_syms.push(GenericRegistry::head_param_of(impl_node.target_args[hi]));
                }
""")
rep("""                    const is_modified: boolean = this.method_has_modified_self_type(
                        mthd, entry, type_arg_displays);
""", """                    const is_modified: boolean = this.method_has_modified_self_type(
                        mthd, entry, &head_syms, type_arg_displays);
""")
rep("""    /// Detect methods whose `this` parameter is a Generic(base, args) where
    /// base matches the outer template name AND the args are NOT the identity
    /// mapping of the template's params, and ALSO not equal to the spec's
    /// concrete args. Example on `implement Result<T, E>`:
""", """    /// Detect methods whose `this` parameter is a Generic(base, args) where
    /// base matches the outer template name AND the args are NOT the identity
    /// mapping of the head's params (`head_syms[i]`, the parameter the impl
    /// head writes at position i), and ALSO not equal to the spec's concrete
    /// args. Example on `implement Result<T, E>`:
""")
rep("""    method_has_modified_self_type(&this, method: MethodNode*, entry: TemplateEntry*,
                                   type_arg_displays: &string[]) -> boolean {
""", """    method_has_modified_self_type(&this, method: MethodNode*, entry: TemplateEntry*,
                                   head_syms: &SymbolID[],
                                   type_arg_displays: &string[]) -> boolean {
""")
rep("""            if (this.annotation_is_modified_outer(
                param.type_annotation, entry, type_arg_displays)) {
""", """            if (this.annotation_is_modified_outer(
                param.type_annotation, entry, head_syms, type_arg_displays)) {
""")
rep("""    /// Recursively unwrap Reference/Pointer and return true if the inner
    /// annotation is Generic(Named(entry.name), args) where any arg is
    /// NEITHER the bare outer param in its position NOR equal to the
    /// corresponding spec type-arg display (or its leaf).
    annotation_is_modified_outer(&this, ann: TypeAnnotation*, entry: TemplateEntry*,
                                  type_arg_displays: &string[]) -> boolean {
        if (ann == null) { return false; }
        match (*ann) {
            TypeAnnotation::Reference(ref) => {
                return this.annotation_is_modified_outer(ref.inner, entry, type_arg_displays);
            }
            TypeAnnotation::Pointer(ptr) => {
                return this.annotation_is_modified_outer(ptr.inner, entry, type_arg_displays);
            }
""", """    /// Recursively unwrap Reference/Pointer and return true if the inner
    /// annotation is Generic(Named(entry.name), args) where any arg is
    /// NEITHER the head's param in its position NOR equal to the
    /// corresponding spec type-arg display (or its leaf).
    annotation_is_modified_outer(&this, ann: TypeAnnotation*, entry: TemplateEntry*,
                                  head_syms: &SymbolID[],
                                  type_arg_displays: &string[]) -> boolean {
        if (ann == null) { return false; }
        match (*ann) {
            TypeAnnotation::Reference(ref) => {
                return this.annotation_is_modified_outer(ref.inner, entry, head_syms, type_arg_displays);
            }
            TypeAnnotation::Pointer(ptr) => {
                return this.annotation_is_modified_outer(ptr.inner, entry, head_syms, type_arg_displays);
            }
""")
rep("""                        if (gen.args.length != entry.param_count) { return false; }
                        for (mut i: i64 = 0; i < gen.args.length; i++) {
                            mut spec_disp: string = "";
                            if (i < type_arg_displays.length) {
                                spec_disp = type_arg_displays[i];
                            }
                            if (!this.annotation_matches_param_or_spec(
                                gen.args[i], entry.param_syms[i], spec_disp)) {
                                return true;
                            }
                        }
""", """                        if (gen.args.length != entry.param_count) { return false; }
                        for (mut i: i64 = 0; i < gen.args.length; i++) {
                            mut spec_disp: string = "";
                            if (i < type_arg_displays.length) {
                                spec_disp = type_arg_displays[i];
                            }
                            const head_sym: SymbolID = if (i < head_syms.length) {
                                head_syms[i]
                            } else {
                                SymbolID::invalid()
                            };
                            if (!this.annotation_matches_param_or_spec(
                                gen.args[i], head_sym, spec_disp)) {
                                return true;
                            }
                        }
""")
rep("""    /// True if `ann` is `Named(N)` where N is the outer parameter `param_sym`
    /// (template identity, read off the stamp) OR spells the spec's type-arg
""", """    /// True if `ann` is `Named(N)` where N is the head's parameter `param_sym`
    /// (template identity, read off the stamp) OR spells the spec's type-arg
""")
if 'generic_registry::{ TemplateEntry }' in s and 'GenericRegistry' not in s.split('generic_registry::{')[1].split('}')[0]:
    rep("""import compiler::types::generic_registry::{ TemplateEntry };
""", """import compiler::types::generic_registry::{ GenericRegistry, TemplateEntry };
""")
open(p, 'w', encoding='utf-8', newline='').write(s)
print("ok")
