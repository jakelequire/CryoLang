R = "C:/Programming/apps/CryoLang/compiler/src/compiler/"

def sub(path, pairs):
    p = R + path
    s = open(p, encoding="utf-8", newline="").read()
    for old, new in pairs:
        c = s.count(old)
        assert c == 1, (path, old[:70], c)
        s = s.replace(old, new)
    open(p, "w", encoding="utf-8", newline="").write(s)

sub("AST/expression.cryo", [
    ("""    bound_params: TypeRef[];
""",
     """    bound_params: TypeRef[];

    /// On a member call a bound answered (`t.conv()` under
    /// `where T: Conv<X>`): the bound's trait arguments (`[X]`), in the
    /// body's own parameters.  A type implementing the trait at several
    /// arguments (`Conv<i32>` and `Conv<i64>`) is answered by the
    /// implementation whose head writes these.  A clone carries them under
    /// the copy's substitution.  Empty on every other call.
    bound_trait_args: TypeRef[];
"""),
    ("""        this.bound_params       = [];
""",
     """        this.bound_params       = [];
        this.bound_trait_args   = [];
"""),
])

sub("AST/cloner.cryo", [
    ("""        c.bound_params = bps;
""",
     """        c.bound_params = bps;
        mut btas: TypeRef[] = [];
        for (mut i: i64 = 0; i < node.bound_trait_args.length; i++) {
            btas.push(node.bound_trait_args[i]);
        }
        c.bound_trait_args = btas;
"""),
])

sub("AST/substituter.cryo", [
    ("""                bps.push(this.reduced(this.subst.apply(node.bound_params[i], this.arena)));
            }
            node.bound_params = bps;
        }
""",
     """                bps.push(this.reduced(this.subst.apply(node.bound_params[i], this.arena)));
            }
            node.bound_params = bps;
        }
        if (node.bound_trait_args.length > 0 && this.subst != null) {
            mut btas: TypeRef[] = [];
            for (mut i: i64 = 0; i < node.bound_trait_args.length; i++) {
                btas.push(this.reduced(this.subst.apply(node.bound_trait_args[i], this.arena)));
            }
            node.bound_trait_args = btas;
        }
"""),
])

sub("sema/method_binding.cryo", [
    ("""type struct BoundDirection {
    trait_def: DefId;
    tied:      boolean;

    static none() -> BoundDirection {
        return BoundDirection { trait_def: DefId::invalid(), tied: false };
    }

    note(mut &this, d: DefId) -> void {
        if (this.tied || !d.is_valid()) { return; }
        if (!this.trait_def.is_valid()) { this.trait_def = d; return; }
        if (!this.trait_def.equals(d)) {
            this.tied = true;
            this.trait_def = DefId::invalid();
        }
    }
""",
     """type struct BoundDirection {
    trait_def: DefId;
    tied:      boolean;
    /// The trait arguments the one bound naming `trait_def` writes; empty
    /// when it writes none, or when the trait is noted more than once and
    /// the arguments no longer say which of its implementations is meant.
    args:      TypeRef[];

    static none() -> BoundDirection {
        return BoundDirection { trait_def: DefId::invalid(), tied: false, args: [] };
    }

    note(mut &this, d: DefId) -> void {
        this.note_with_args(d, []);
    }

    /// `note` for the trait a bound names itself, with the bound's trait
    /// arguments.
    note_with_args(mut &this, d: DefId, args: TypeRef[]) -> void {
        if (this.tied || !d.is_valid()) { return; }
        if (!this.trait_def.is_valid()) {
            this.trait_def = d;
            this.args = args;
            return;
        }
        this.args = [];
        if (!this.trait_def.equals(d)) {
            this.tied = true;
            this.trait_def = DefId::invalid();
        }
    }

    /// The trait arguments of the one bound that named the trait.
    bound_args(&this) -> TypeRef[] {
        mut out: TypeRef[] = [];
        for (mut i: i64 = 0; i < this.args.length; i++) { out.push(this.args[i]); }
        return out;
    }
"""),
    ("""                    dir.note(if (k == 0) { bound_def } else { chain[k] });
""",
     """                    if (k == 0 && tr != null) {
                        dir.note_with_args(bound_def, this.bound_trait_arg_types(tr));
                    } else {
                        dir.note(if (k == 0) { bound_def } else { chain[k] });
                    }
"""),
    ("""    subst_bound_trait_args(mut &this, ret: TypeRef, trait_leaf: SymbolStr,
""",
     """    bound_trait_arg_types(mut &this, tr: TraitRef*) -> TypeRef[] {
        mut out: TypeRef[] = [];
        const tdecl: TraitDeclNode* = this.ctx.generic_registry.get_trait_decl(tr.definition());
        if (tdecl == null || tdecl.generic_params.length == 0) { return out; }
        if (tr.type_args.length < tdecl.generic_params.length) { return out; }
        mut rc: ResolutionContext = ResolutionContext::new("");
        this.symbolic.symbolic_bind_params(&rc);
        for (mut g: i64 = 0; g < tdecl.generic_params.length; g++) {
            const arg: TypeRef = this.ctx.type_resolver.resolve(tr.type_args[g], &rc);
            if (!arg.is_valid()) { return []; }
            out.push(arg);
        }
        return out;
    }

    subst_bound_trait_args(mut &this, ret: TypeRef, trait_leaf: SymbolStr,
"""),
])

sub("sema/call_resolver.cryo", [
    ("""                if (dir.named().is_valid() && !member.resolved_trait.is_valid()) {
                    this.stamp_trait_item(member, dir.named());
                }
""",
     """                if (dir.named().is_valid() && !member.resolved_trait.is_valid()) {
                    this.stamp_trait_item(member, dir.named());
                    call.bound_trait_args = dir.bound_args();
                }
"""),
])

sub("mono/call_specializer.cryo", [
    ("""            const item_m: MethodNode* = this.trait_item_on(ma, recv_type, &item_impl);
""",
     """            const item_m: MethodNode* = this.trait_item_on(call, ma, recv_type, &item_impl);
"""),
    ("""                this.pin_plain(call, this.trait_item_on(call.callee as MemberAccessNode*, owner, &item_impl));
""",
     """                this.pin_plain(call, this.trait_item_on(call, call.callee as MemberAccessNode*, owner, &item_impl));
"""),
    ("""    /// Null when the receiver has no implementation of the trait, or
    /// several - a trait implemented at several trait arguments, which only
    /// the call's arguments tell apart.  A receiver that is a generic type's
    /// specialization holds its implementations in the blocks placed under
    /// it, where the instances its calls ask for are added and emitted;
    /// any other receiver's are the registry's heads selected for it.
    trait_item_on(&this, ma: MemberAccessNode*, recv_type: TypeRef, out_impl: ImplBlockNode**) -> MethodNode* {
        if (this.generic_registry == null) { return null; }
        const heads: ImplBlockNode*[] = this.trait_heads_on(ma.resolved_trait, recv_type);
        if (heads.length != 1) { return null; }
        const m: MethodNode* = this.generic_registry.trait_item_method(heads[0], ma.resolved_trait_slot);
        if (m != null) { *out_impl = heads[0]; }
        return m;
    }
""",
     """    /// A trait implemented at several trait arguments (`Conv<i32>` and
    /// `Conv<i64>`) is answered by the one implementation whose head writes
    /// the arguments the bound gave the call (`bound_trait_args`).  Null
    /// when the receiver has no implementation of the trait, or several the
    /// bound's arguments do not tell apart.  A receiver that is a generic
    /// type's specialization holds its implementations in the blocks placed
    /// under it, where the instances its calls ask for are added and
    /// emitted; any other receiver's are the registry's heads selected for
    /// it.
    trait_item_on(&this, call: CallExprNode*, ma: MemberAccessNode*, recv_type: TypeRef,
                  out_impl: ImplBlockNode**) -> MethodNode* {
        if (this.generic_registry == null) { return null; }
        const heads: ImplBlockNode*[] = this.trait_heads_on(ma.resolved_trait, recv_type);
        mut head: ImplBlockNode* = null;
        if (heads.length == 1) {
            head = heads[0];
        } else if (call.bound_trait_args.length > 0) {
            for (mut h: i64 = 0; h < heads.length; h++) {
                if (!this.generic_registry.trait_args_unify(heads[h], &call.bound_trait_args, this.intern_table)) {
                    continue;
                }
                if (head != null) { return null; }
                head = heads[h];
            }
        }
        if (head == null) { return null; }
        const m: MethodNode* = this.generic_registry.trait_item_method(head, ma.resolved_trait_slot);
        if (m != null) { *out_impl = head; }
        return m;
    }
"""),
])

sub("types/generic_registry.cryo", [
    ("""    /// One written argument of a head against one argument of the subject.
""",
     """    /// Whether the trait arguments `block`'s head writes (`Conv<i32>` in
    /// `implement trait Conv<i32> for S`) unify, position by position, with
    /// `args`.  A head writing a different number is not for them.
    trait_args_unify(&this, block: ImplBlockNode*, args: &TypeRef[], intern: InternTable*) -> boolean {
        if (block == null || block.trait_annotation == null) { return false; }
        match (*block.trait_annotation) {
            TypeAnnotation::Generic(g) => {
                if (g.args.length != args.length) { return false; }
                for (mut i: i64 = 0; i < args.length; i++) {
                    if (!this.annotation_unifies(g.args[i], args[i], intern)) { return false; }
                }
                return true;
            }
            _ => { return args.length == 0; }
        }
    }

    /// One written argument of a head against one argument of the subject.
"""),
])
print("ok")
