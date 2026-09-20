p = 'compiler/src/compiler/sema/async_lower.cryo'
s = open(p, encoding='utf-8', newline='').read()
def rep(old, new, count=1):
    global s
    assert s.count(old) == count, (old[:70], s.count(old))
    s = s.replace(old, new)

rep("""    /// A `Named` annotation spelling a GENERIC PARAMETER by name, resolved by
    /// lookup rather than short-circuited.
    ///
    /// The counterpart to `make_type_ann`, and mandatory for anything a generic
    /// future mentions: monomorphization specializes a template by rewriting the
    /// generic parameters that appear in its ANNOTATIONS, and `pre_resolved`
    /// bypasses annotation resolution entirely — a pre-resolved `T` would reach
    /// codegen still being `T`.  A parameter names no declaration, so the slot
    /// stays unanswered and the binding in the resolution context answers it.
    /// A DECLARATION spelled by the lowering goes through `named_ann_def`: the
    /// stamping walk visits what the parser produced, so a synthesized name
    /// carries its referent or nothing resolves it.
    named_ann(&this, name: SymbolStr, span: SourceSpan) -> TypeAnnotation* {
        mut n: NamedAnnotation* = new NamedAnnotation {
            name: name, span: span, pre_resolved: TypeRef::invalid(),
            res: ResSlot::Pending,
        };
        return new TypeAnnotation::Named(n);
    }
""", """    /// A `Named` annotation spelling the GENERIC PARAMETER `gp`, stamped as
    /// the name layer stamps a written one and resolved by lookup rather than
    /// short-circuited.
    ///
    /// The counterpart to `make_type_ann`, and mandatory for anything a generic
    /// future mentions: monomorphization specializes a template by rewriting the
    /// generic parameters that appear in its ANNOTATIONS, and `pre_resolved`
    /// bypasses annotation resolution entirely — a pre-resolved `T` would reach
    /// codegen still being `T`.  The stamp carries the parameter's symbol,
    /// because every substitution table is keyed by it and a spelling matches
    /// nothing; a synthesized node is stamped by nothing but its synthesizer.
    /// A DECLARATION spelled by the lowering goes through `named_ann_def` for
    /// the same reason.
    named_ann(&this, gp: GenericParamNode*, span: SourceSpan) -> TypeAnnotation* {
        mut n: NamedAnnotation* = new NamedAnnotation {
            name: gp.name, span: span, pre_resolved: TypeRef::invalid(),
            res: ResSlot::Pending,
        };
        n.res.answer(Res::GenericParam(gp.sym_id));
        return new TypeAnnotation::Named(n);
    }

    /// The parameter among `params` whose arena type spells `name`, or null.
    ///
    /// The lowering rebuilds a frame field's type from the ARENA, whose
    /// parameter types carry a spelling and no symbol; the declaration
    /// being lowered is the scope that spelling was resolved in, and its
    /// parameters (the owner's, then the function's own) are the only ones
    /// a type the body computed can mention.  One spelling names one of
    /// them: a nested declaration cannot redeclare an enclosing one.
    param_spelled(&this, params: &GenericParamNode*[], name: SymbolStr) -> GenericParamNode* {
        for (mut i: i64 = 0; i < params.length; i++) {
            const gp: GenericParamNode* = params[i];
            if (gp != null && gp.name.equals(name)) { return gp; }
        }
        return null;
    }
""")

rep("""            const gp: GenericParamNode* = params[i];
            if (gp == null) { continue; }
            const ann: TypeAnnotation* = this.named_ann(gp.name, span);
            // A parameter of the declaration, as the name layer stamps a
            // written one: a consumer matching the head against a subject
            // reads the stamp, and a synthesized node is stamped by nothing
            // but its synthesizer.
            match (*ann) {
                TypeAnnotation::Named(n) => { n.res.answer(Res::GenericParam(gp.sym_id)); }
                _ => { }
            }
            args.push(ann);
""", """            const gp: GenericParamNode* = params[i];
            if (gp == null) { continue; }
            args.push(this.named_ann(gp, span));
""")

rep("""    /// An annotation for the `Future`'s `Output` — the declared return type, or
    /// unit for `-> void`.
    output_type_ann(mut &this, d: AsyncDecl*, span: SourceSpan) -> TypeAnnotation* {
        return this.type_ann_for(d.output_ref, span);
    }
""", """    /// An annotation for the `Future`'s `Output` — the declared return type, or
    /// unit for `-> void`.
    output_type_ann(mut &this, d: AsyncDecl*, span: SourceSpan) -> TypeAnnotation* {
        return this.type_ann_for(d.output_ref, &d.fut_params, span);
    }
""")

rep("""    /// Null when the type mentions a parameter through a shape with no annotation
    /// spelling; the caller reports that rather than emitting a field whose type
    /// would silently stay generic.
    type_ann_for(mut &this, ty: TypeRef, span: SourceSpan) -> TypeAnnotation* {
        if (!ty.is_valid()) { return null; }
        if (!this.arena.contains_generic_param(ty)) { return this.make_type_ann(ty, span); }

        const t: Type* = this.arena.lookup(ty.id);
        if (t == null) { return null; }
        if (t.kind == TypeKind::GenericParam) {
            return this.named_ann((t as GenericParamType*).param_name, span);
        }
        if (t.kind == TypeKind::BoundedParam) {
            return this.named_ann((t as BoundedParamType*).param_name, span);
        }
        if (t.kind == TypeKind::Pointer) {
            const p: PointerType* = t as PointerType*;
            mut inner: TypeAnnotation* = this.type_ann_for(p.pointee, span);
""", """    /// Null when the type mentions a parameter through a shape with no annotation
    /// spelling, or a parameter that is none of `params` (the future's own);
    /// the caller reports that rather than emitting a field whose type would
    /// silently stay generic.
    type_ann_for(mut &this, ty: TypeRef, params: &GenericParamNode*[],
                 span: SourceSpan) -> TypeAnnotation* {
        if (!ty.is_valid()) { return null; }
        if (!this.arena.contains_generic_param(ty)) { return this.make_type_ann(ty, span); }

        const t: Type* = this.arena.lookup(ty.id);
        if (t == null) { return null; }
        if (t.kind == TypeKind::GenericParam || t.kind == TypeKind::BoundedParam) {
            const spelled: SymbolStr = if (t.kind == TypeKind::GenericParam) {
                (t as GenericParamType*).param_name
            } else {
                (t as BoundedParamType*).param_name
            };
            const gp: GenericParamNode* = this.param_spelled(params, spelled);
            if (gp == null) { return null; }
            return this.named_ann(gp, span);
        }
        if (t.kind == TypeKind::Pointer) {
            const p: PointerType* = t as PointerType*;
            mut inner: TypeAnnotation* = this.type_ann_for(p.pointee, params, span);
""")
rep("""            const r: ReferenceType* = t as ReferenceType*;
            mut inner: TypeAnnotation* = this.type_ann_for(r.referent, span);
""", """            const r: ReferenceType* = t as ReferenceType*;
            mut inner: TypeAnnotation* = this.type_ann_for(r.referent, params, span);
""")
rep("""            const a: ArrayType* = t as ArrayType*;
            mut elem: TypeAnnotation* = this.type_ann_for(a.element, span);
""", """            const a: ArrayType* = t as ArrayType*;
            mut elem: TypeAnnotation* = this.type_ann_for(a.element, params, span);
""")
rep("""            const proj: AssocProjectionType* = t as AssocProjectionType*;
            mut base: TypeAnnotation* = this.type_ann_for(proj.base, span);
""", """            const proj: AssocProjectionType* = t as AssocProjectionType*;
            mut base: TypeAnnotation* = this.type_ann_for(proj.base, params, span);
""")
rep("""                mut arg: TypeAnnotation* = this.type_ann_for(inst.type_args[i], span);
""", """                mut arg: TypeAnnotation* = this.type_ann_for(inst.type_args[i], params, span);
""")

rep("""    add_future_field(mut &this, struct_decl: StructDeclNode*, name: SymbolStr,
                     ty: TypeRef, generic: boolean, span: SourceSpan) -> boolean {
        mut f: FieldDeclNode* = new FieldDeclNode(name, span);
        f.set_visibility(Visibility::Public);
        f.set_resolved_type(ty);
        if (generic) {
            mut ann: TypeAnnotation* = this.type_ann_for(ty, span);
""", """    add_future_field(mut &this, struct_decl: StructDeclNode*, name: SymbolStr,
                     ty: TypeRef, d: AsyncDecl*, span: SourceSpan) -> boolean {
        mut f: FieldDeclNode* = new FieldDeclNode(name, span);
        f.set_visibility(Visibility::Public);
        f.set_resolved_type(ty);
        if (d.generic) {
            mut ann: TypeAnnotation* = this.type_ann_for(ty, &d.fut_params, span);
""")
rep("""        mut fields_ok: boolean = this.add_future_field(struct_decl, state_sym, u32_ty, generic, span);
""", """        mut fields_ok: boolean = this.add_future_field(struct_decl, state_sym, u32_ty, &this.pending[pi], span);
""")
rep("""            if (!this.add_future_field(struct_decl, this.param_field_name(p),
                    this.param_field_type(&sm, this.param_slot_type(&this.pending[pi], p)),
                    generic, span)) {
""", """            if (!this.add_future_field(struct_decl, this.param_field_name(p),
                    this.param_field_type(&sm, this.param_slot_type(&this.pending[pi], p)),
                    &this.pending[pi], span)) {
""")
rep("""            if (!this.add_future_field(struct_decl, sm.fut_syms[i], sm.fut_opt[i], generic, span)) {
""", """            if (!this.add_future_field(struct_decl, sm.fut_syms[i], sm.fut_opt[i], &this.pending[pi], span)) {
""")
rep("""            if (!this.add_future_field(struct_decl, sm.prom_field[i], sm.prom_tys[i], generic, span)) {
""", """            if (!this.add_future_field(struct_decl, sm.prom_field[i], sm.prom_tys[i], &this.pending[pi], span)) {
""")
open(p, 'w', encoding='utf-8', newline='').write(s)
print("ok")
