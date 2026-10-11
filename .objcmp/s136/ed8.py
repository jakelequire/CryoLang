R = "C:/Programming/apps/CryoLang/compiler/src/compiler/"

def sub(path, pairs):
    p = R + path
    s = open(p, encoding="utf-8", newline="").read()
    for old, new in pairs:
        c = s.count(old)
        assert c == 1, (path, old[:70], c)
        s = s.replace(old, new)
    open(p, "w", encoding="utf-8", newline="").write(s)

sub("AST/declaration.cryo", [
    ("""    trait_item_methods: i64[];
""",
     """    trait_item_methods: i64[];
    /// The trait's arguments as this head writes them, resolved in the
    /// block's own parameters (`[i32]` for `implement trait Conv<i32> for
    /// S`); a specialization's clone holds them substituted.  They are part
    /// of each method's link symbol, so one type implementing a trait at
    /// two arguments links two methods.  Empty for a trait that takes none,
    /// and for an inherent block.
    trait_arg_types: TypeRef[];
"""),
    ("""        this.trait_item_methods    = [];
        this.trait_annotation      = null;
""",
     """        this.trait_item_methods    = [];
        this.trait_arg_types       = [];
        this.trait_annotation      = null;
"""),
])

sub("AST/cloner.cryo", [
    ("""        c.set_target_args(this.clone_type_annotations(node.target_args));
        // where_bounds: shallow array copy.""",
     """        c.set_target_args(this.clone_type_annotations(node.target_args));
        mut tats: TypeRef[] = [];
        for (mut i: i64 = 0; i < node.trait_arg_types.length; i++) { tats.push(node.trait_arg_types[i]); }
        c.trait_arg_types = tats;
        // where_bounds: shallow array copy."""),
])

sub("AST/substituter.cryo", [
    ("""    override visit(node: ImplBlockNode*) -> void {
        if (node.trait_annotation != null) {
            this.substitute_annotation(node.trait_annotation);
        }
""",
     """    override visit(node: ImplBlockNode*) -> void {
        if (node.trait_annotation != null) {
            this.substitute_annotation(node.trait_annotation);
        }
        if (node.trait_arg_types.length > 0 && this.subst != null) {
            mut tats: TypeRef[] = [];
            for (mut i: i64 = 0; i < node.trait_arg_types.length; i++) {
                tats.push(this.reduced(this.subst.apply(node.trait_arg_types[i], this.arena)));
            }
            node.trait_arg_types = tats;
        }
"""),
])

sub("passes/type_resolution.cryo", [
    ("""            if (ngp != nargs) { return; }
            for (mut i: i64 = 0; i < nargs; i++) {
                const gp: GenericParamNode* = trait_decl.generic_params[i];
                if (gp == null) { continue; }
                const arg_ty: TypeRef = this.resolver.resolve(gen.args[i], res_ctx);
                if (!arg_ty.is_valid()) { continue; }
                res_ctx.add_binding(gp.sym_id, gp.name, arg_ty);
            }
            return;
""",
     """            if (ngp != nargs) { return; }
            mut written: TypeRef[] = [];
            for (mut i: i64 = 0; i < nargs; i++) {
                const gp: GenericParamNode* = trait_decl.generic_params[i];
                if (gp == null) { continue; }
                const arg_ty: TypeRef = this.resolver.resolve(gen.args[i], res_ctx);
                if (!arg_ty.is_valid()) { continue; }
                res_ctx.add_binding(gp.sym_id, gp.name, arg_ty);
                written.push(arg_ty);
            }
            if (written.length == nargs) { impl_node.trait_arg_types = written; }
            return;
"""),
    ("""                ctx.decl_index.register_methods_through(
                    ctx.decl_index.impl_owner(node, Text::new("type_resolution/register impl methods")),
                    node.methods, &head, ctx.intern_table, arena, Text::new("type_resolution/register impl methods"));
""",
     """                ctx.decl_index.register_methods_through(
                    ctx.decl_index.impl_owner(node, Text::new("type_resolution/register impl methods")),
                    node.methods, &head, &node.trait_arg_types, ctx.intern_table, arena,
                    Text::new("type_resolution/register impl methods"));
"""),
])

sub("sema/async_lower.cryo", [
    ("""        this.ctx.decl_index.register_methods_through(
            owner.decl, arr, &head, this.intern, this.arena, Text::new("async_lower/repoint method"));
""",
     """        const trait_args: TypeRef[] = if (owner.impl_node == null) { [] } else { owner.impl_node.trait_arg_types };
        this.ctx.decl_index.register_methods_through(
            owner.decl, arr, &head, &trait_args, this.intern, this.arena, Text::new("async_lower/repoint method"));
"""),
])

sub("decl_index.cryo", [
    ("""        this.register_methods_through(owner, methods, null, intern, arena, site);
""",
     """        const no_trait_args: TypeRef[] = [];
        this.register_methods_through(owner, methods, null, &no_trait_args, intern, arena, site);
"""),
    ("""    /// with `head` applied - the block's parameters bound to the owner's
    /// through its head (`TraitChecker::add_head_param_bindings`).  Null or
    /// empty `head` stores the return as written.
""",
     """    /// with `head` applied - the block's parameters bound to the owner's
    /// through its head (`TraitChecker::add_head_param_bindings`).  Null or
    /// empty `head` stores the return as written.  `trait_args` are the
    /// block's trait arguments, which a trait method's link symbol carries.
"""),
    ("""                             head: TypeSubstitution*, intern: InternTable*,
                             arena: TypeArena*, site: Text) -> void {
""",
     """                             head: TypeSubstitution*, trait_args: &TypeRef[],
                             intern: InternTable*, arena: TypeArena*, site: Text) -> void {
"""),
    ("""                intern, arena, type_qname, this.defs.path_of(func.origin_trait).spelling(), method_sym, receiver_kind,
                method_param_refs, func.resolved_return_type, func.is_variadic);
""",
     """                intern, arena, type_qname, this.defs.path_of(func.origin_trait).spelling(), trait_args,
                method_sym, receiver_kind, method_param_refs, func.resolved_return_type, func.is_variadic);
"""),
])

sub("resolver/mangled_name.cryo", [
    ("""    /// Mangle a trait-impl method: `C$tr$<trait>$f<owner>-<method>$F...`.
    static for_trait_impl(table: InternTable*, arena: TypeArena*,
                          trait_qname: QualifiedName,
                          owner_qname: QualifiedName, method: SymbolStr,
                          receiver: ReceiverKind,
                          params: &TypeRef[], ret: TypeRef,
                          is_variadic: boolean) -> MangledName {
        // C$tr$<trait_path>$f<owner_path>-<method>$F<params>$R<ret>
        mut ctx: MangleContext = MangleContext::new(table, arena);
        mut buf: string = "C$tr$";
        buf += MangleContext::encode_path(table, trait_qname, SymbolStr::empty(), false);
        buf += "$f";
""",
     """    /// Mangle a trait-impl method: `C$tr$<trait>$f<owner>-<method>$F...`.
    /// A trait implemented with arguments carries them on its segment
    /// (`C$tr$4Conv$Li$G$f1S-4conv...`), so one type implementing it at two
    /// arguments has two symbols.
    static for_trait_impl(table: InternTable*, arena: TypeArena*,
                          trait_qname: QualifiedName, trait_args: &TypeRef[],
                          owner_qname: QualifiedName, method: SymbolStr,
                          receiver: ReceiverKind,
                          params: &TypeRef[], ret: TypeRef,
                          is_variadic: boolean) -> MangledName {
        // C$tr$<trait_path>[$L<args>$G]$f<owner_path>-<method>$F<params>$R<ret>
        mut ctx: MangleContext = MangleContext::new(table, arena);
        mut buf: string = "C$tr$";
        buf += MangleContext::encode_path(table, trait_qname, SymbolStr::empty(), false);
        if (trait_args.length > 0) {
            buf += "$L";
            for (mut i: i64 = 0; i < trait_args.length; i++) {
                if (i > 0) { buf += "_"; }
                buf += ctx.encode_type_ref(trait_args[i], "for_trait_impl.trait_arg");
            }
            buf += "$G";
        }
        buf += "$f";
"""),
])
print("ok")
