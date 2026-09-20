p = 'compiler/src/compiler/AST/substituter.cryo'
s = open(p, encoding='utf-8', newline='').read()
def rep(old, new, count=1):
    global s
    assert s.count(old) == count, (old[:70], s.count(old))
    s = s.replace(old, new)

rep("""import compiler::resolver::intern_table::{ InternTable };
import compiler::resolver::symbol_str::{ SymbolStr };
import compiler::types::arena::{ TypeArena };
""", """import compiler::resolver::intern_table::{ InternTable };
import compiler::resolver::symbol_str::{ SymbolStr };
import compiler::resolver::symbol_id;
import compiler::resolver::symbol_id::{ SymbolID };
import compiler::types::arena::{ TypeArena };
""")

rep("""    base_name:         SymbolStr;         // "Array"
    spec_name:         SymbolStr;         // "Array_i32"
    param_names:       SymbolStr[];       // ["T"]
    type_arg_displays: SymbolStr[];       // ["i32"]
""", """    base_name:         SymbolStr;         // "Array"
    spec_name:         SymbolStr;         // "Array_i32"
    /// The parameters' spellings, index-aligned with `param_syms` and the
    /// substitution's replacements; a display, never a key.
    param_names:       SymbolStr[];       // ["T"]
    /// The symbols the parameters were declared as.  A written `T` in the
    /// cloned body carries its declaration's symbol in its stamp, and it is
    /// rewritten only when that symbol is one of these: a trait default's
    /// own `<U>` copied under an `implement<U>` keeps its `U` while the
    /// impl's is rewritten, which no comparison of spellings can do.
    param_syms:        SymbolID[];
    type_arg_displays: SymbolStr[];       // ["i32"]
""")

rep("""    ASTTypeSubstituter(subst: TypeSubstitution*, arena: TypeArena*,
                       intern_table: InternTable*,
                       base_name: SymbolStr, spec_name: SymbolStr,
                       param_names: SymbolStr[], type_arg_displays: SymbolStr[],
                       spec_typeref: TypeRef)
        : BaseASTVisitor() {
        this.subst             = subst;
        this.arena             = arena;
        this.intern_table      = intern_table;
        this.base_name         = base_name;
        this.spec_name         = spec_name;
        this.param_names       = param_names;
        this.type_arg_displays = type_arg_displays;
        this.spec_typeref      = spec_typeref;
    }
""", """    ASTTypeSubstituter(subst: TypeSubstitution*, arena: TypeArena*,
                       intern_table: InternTable*,
                       base_name: SymbolStr, spec_name: SymbolStr,
                       param_names: SymbolStr[], param_syms: SymbolID[],
                       type_arg_displays: SymbolStr[],
                       spec_typeref: TypeRef)
        : BaseASTVisitor() {
        this.subst             = subst;
        this.arena             = arena;
        this.intern_table      = intern_table;
        this.base_name         = base_name;
        this.spec_name         = spec_name;
        this.param_names       = param_names;
        this.param_syms        = param_syms;
        this.type_arg_displays = type_arg_displays;
        this.spec_typeref      = spec_typeref;
    }

    /// The slot of the parameter `sym`, or -1 when it is none of this
    /// substitution's - including an invalid symbol, which names nothing.
    param_slot(&this, sym: SymbolID) -> i64 {
        if (!sym.is_valid()) { return -1; }
        for (mut i: i64 = 0; i < this.param_syms.length; i++) {
            if (this.param_syms[i].equals(sym)) { return i; }
        }
        return -1;
    }
""")

# flat projection prefix: the stamp says which parameter the path is rooted at
rep("""    substitute_named_annotation(ann: TypeAnnotation*, named: NamedAnnotation*) -> TypeAnnotation* {
        // Associated-type projection on a generic param written as a FLAT
        // qualified name (`I::Item` - only `This::Item` is a Projection node).
        // If the segment before the first `::` is one of our substituted params,
        // rewrite the whole thing to a `Projection` over the param's concrete
        // replacement, so a field/signature type `(I::Item)->O` becomes a real
        // projection over `IntSeq` that the resolver reduces to the member type.
        const nm_str: string = this.intern_table.resolve(named.name);
        const nm_len: i64 = nm_str.length() as i64;
        if (nm_len > 2) {
            mut sep: i64 = -1;
            mut k: i64 = 0;
            while (k + 1 < nm_len) {
                if (nm_str[k] == ':' && nm_str[k + 1] == ':') { sep = k; break; }
                k = k + 1;
            }
            if (sep > 0) {
                const pfx: string = nm_str.substring(0, sep as u32);
                const pfx_sym: SymbolStr = this.intern_table.intern(pfx);
                for (mut pi: i64 = 0; pi < this.param_names.length; pi++) {
                    if (pfx_sym.equals(this.param_names[pi])) {
                        const member_str: string = nm_str.substring((sep + 2) as u32, (nm_len - sep - 2) as u32);
                        const member_sym: SymbolStr = this.intern_table.intern(member_str);
                        const base_pre: TypeRef = this.resolved_arg_typeref(pi);
                        const base_named: NamedAnnotation* = new NamedAnnotation {
                            name:         this.type_arg_displays[pi],
                            span:         named.span,
                            pre_resolved: base_pre,
                            res: ResSlot::Pending,
                        };
                        const base_ann: TypeAnnotation* =
                            new TypeAnnotation::Named(base_named);
                        const proj: ProjectionAnnotation* = new ProjectionAnnotation {
                            base:         base_ann,
                            member:       member_sym,
                            owning_trait: SymbolStr::empty(),
                            resolved:     TypeRef::invalid(),
                            span:         named.span,
                        };
                        *ann = TypeAnnotation::Projection(proj);
                        return ann;
                    }
                }
            }
        }

        // Check if this named annotation is one of our generic params.
        for (mut i: i64 = 0; i < this.param_names.length; i++) {
            if (named.name.equals(this.param_names[i])) {
""", """    substitute_named_annotation(ann: TypeAnnotation*, named: NamedAnnotation*) -> TypeAnnotation* {
        // Associated-type projection on a generic param written as a FLAT
        // qualified name (`I::Item` - only `This::Item` is a Projection node).
        // The stamp says which parameter the path is rooted at; when it is one
        // of our substituted params, rewrite the whole thing to a `Projection`
        // over the param's concrete replacement, so a field/signature type
        // `(I::Item)->O` becomes a real projection over `IntSeq` that the
        // resolver reduces to the member type.
        const pi: i64 = this.param_slot(named.relative_param_sym());
        if (pi >= 0) {
            const nm_str: string = this.intern_table.resolve(named.name);
            const nm_len: i64 = nm_str.length() as i64;
            mut sep: i64 = -1;
            mut k: i64 = 0;
            while (k + 1 < nm_len) {
                if (nm_str[k] == ':' && nm_str[k + 1] == ':') { sep = k; break; }
                k = k + 1;
            }
            if (sep > 0) {
                const member_str: string = nm_str.substring((sep + 2) as u32, (nm_len - sep - 2) as u32);
                const member_sym: SymbolStr = this.intern_table.intern(member_str);
                const base_pre: TypeRef = this.resolved_arg_typeref(pi);
                const base_named: NamedAnnotation* = new NamedAnnotation {
                    name:         this.type_arg_displays[pi],
                    span:         named.span,
                    pre_resolved: base_pre,
                    res: ResSlot::Pending,
                };
                const base_ann: TypeAnnotation* =
                    new TypeAnnotation::Named(base_named);
                const proj: ProjectionAnnotation* = new ProjectionAnnotation {
                    base:         base_ann,
                    member:       member_sym,
                    owning_trait: SymbolStr::empty(),
                    resolved:     TypeRef::invalid(),
                    span:         named.span,
                };
                *ann = TypeAnnotation::Projection(proj);
                return ann;
            }
        }

        // The parameter this annotation names, when it is one of ours.
        const i: i64 = this.param_slot(named.param_sym());
        if (i >= 0) {
            {
""")

# args_are_outer_param_refs
rep("""    args_are_outer_param_refs(&this, args: &TypeAnnotation*[]) -> boolean {
        if (args.length > this.param_names.length) { return false; }
        for (mut i: i64 = 0; i < args.length; i++) {
            const a: TypeAnnotation* = args[i];
            if (a == null) { return false; }
            mut matched: boolean = false;
            match (*a) {
                TypeAnnotation::Named(named) => {
                    matched = named.name.equals(this.param_names[i]);
                }
""", """    args_are_outer_param_refs(&this, args: &TypeAnnotation*[]) -> boolean {
        if (args.length > this.param_syms.length) { return false; }
        for (mut i: i64 = 0; i < args.length; i++) {
            const a: TypeAnnotation* = args[i];
            if (a == null) { return false; }
            mut matched: boolean = false;
            match (*a) {
                TypeAnnotation::Named(named) => {
                    matched = named.param_sym().is_valid()
                        && named.param_sym().equals(this.param_syms[i]);
                }
""")

# scope segment: the scope's stamp is a path rooted at the parameter
rep("""            // Substitute generic-param scope names too; `T::default()`
            // inside `fn f<T>() where T: Default` becomes `i32::default()`
            // when monomorphized with T=i32.  The stamp beside the name
            // still says `GenericParam` - a `Res` names a definition, and
            // the clone's syntax is the template's - so the arena id is
            // recorded as well: it is the only carrier of which type the
            // segment now means that is not a spelling to be looked up.
            for (mut i: i64 = 0; i < this.param_names.length; i++) {
                if (node.scope_name.equals(this.param_names[i])) {
                    node.scope_name = this.type_arg_displays[i];
                    const arg_ref: TypeRef = this.resolved_arg_typeref(i);
                    if (arg_ref.is_valid()) { node.spec_owner = arg_ref; }
                    break;
                }
            }
""", """            // Substitute generic-param scope names too; `T::default()`
            // inside `fn f<T>() where T: Default` becomes `i32::default()`
            // when monomorphized with T=i32.  The stamp beside the name
            // still says `GenericParam` - a `Res` names a definition, and
            // the clone's syntax is the template's - so the arena id is
            // recorded as well: it is the only carrier of which type the
            // segment now means that is not a spelling to be looked up.
            const si: i64 = this.param_slot(node.scope_res.relative_param_sym());
            if (si >= 0) {
                node.scope_name = this.type_arg_displays[si];
                const arg_ref: TypeRef = this.resolved_arg_typeref(si);
                if (arg_ref.is_valid()) { node.spec_owner = arg_ref; }
            }
""")

# ArrayLiteralNode.element_type: no writer but this, no reader but the dumper
rep("""    override visit(node: ArrayLiteralNode*) -> void {
        for (mut i: i64 = 0; i < node.elements.length; i++) {
            node.elements[i].accept(this);
        }
        // Substitute element_type string (element_type stays string, resolve SymbolStr for comparison)
        for (mut i: i64 = 0; i < this.param_names.length; i++) {
            if (node.element_type == this.intern_table.resolve(this.param_names[i])) {
                node.element_type = this.intern_table.resolve(this.type_arg_displays[i]);
                break;
            }
        }
        if (node.repeat_count_expr != null) {
""", """    override visit(node: ArrayLiteralNode*) -> void {
        for (mut i: i64 = 0; i < node.elements.length; i++) {
            node.elements[i].accept(this);
        }
        if (node.repeat_count_expr != null) {
""")

open(p, 'w', encoding='utf-8', newline='').write(s)
print("ok")
