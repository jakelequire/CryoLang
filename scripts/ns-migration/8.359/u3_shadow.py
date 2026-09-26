"""Unit 3 shadow: the refused-signature flag by identity beside the old
composed-name map; the old answer drives, both are printed when either is
true or they differ."""

def edit(path, pairs):
    src = open(path, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in src else '\n'
    for a, b, n in pairs:
        a = a.replace('\n', nl); b = b.replace('\n', nl)
        c = src.count(a)
        assert c == n, (path, c, n, a[:90])
        src = src.replace(a, b)
    open(path, 'w', encoding='utf-8', newline='').write(src)

edit('compiler/src/compiler/resolver/res.cryo', [
("""    visibility: DeclVisibility[];

public:""", """    visibility: DeclVisibility[];
    refused:    boolean[];

public:""", 1),
("""            visibility: [],
        };""", """            visibility: [],
            refused:    [],
        };""", 1),
("""        this.visibility.push(DeclVisibility::Unrecorded);
        return DefId { index: at };""", """        this.visibility.push(DeclVisibility::Unrecorded);
        this.refused.push(false);
        return DefId { index: at };""", 1),
("""    /// `d`'s visibility as its registration recorded it.""",
 """    /// Record that `d`'s written signature carries a name the name layer
    /// refused, so its registration registers no signature for it.
    record_signature_refused(mut &this, d: DefId) -> void {
        if (!d.is_valid()) { return; }
        this.refused[d.index as i64] = true;
    }

    /// Whether `d`'s signature was refused where it was written.
    signature_refused(&this, d: DefId) -> boolean {
        if (!d.is_valid()) { return false; }
        return this.refused[d.index as i64];
    }

    /// `d`'s visibility as its registration recorded it.""", 1),
])

DI = 'compiler/src/compiler/decl_index.cryo'
edit(DI, [
("""    refused_signatures:  HashMap<u32, boolean>;""",
 """    refused_signatures:  HashMap<u32, boolean>;
    refused_methods:     HashMap<u64, boolean>;""", 1),
("""            refused_signatures: HashMap::<u32, boolean>::new(),""",
 """            refused_signatures: HashMap::<u32, boolean>::new(),
            refused_methods:    HashMap::<u64, boolean>::new(),""", 1),
("""            if (DeclarationIndex::signature_is_refused(func)) {
                this.mark_signature_refused(combined_sym);
                continue;
            }""", """            if (DeclarationIndex::signature_is_refused(func)) {
                this.mark_signature_refused(combined_sym);
                this.mark_method_signature_refused(owner, func.name);
                continue;
            }""", 1),
("""    /// `signature_refused` for a stamp: the definition's canonical name is
    /// the key its registration would have used.
    signature_refused_of_def(&this, d: DefId) -> boolean {
        if (!d.is_valid()) { return false; }
        return this.signature_refused(this.defs.path_of(d));
    }""", """    /// `signature_refused` for a stamp: the definition's canonical name is
    /// the key its registration would have used.
    signature_refused_of_def(&this, d: DefId) -> boolean {
        if (!d.is_valid()) { return false; }
        const old_r: boolean = this.signature_refused(this.defs.path_of(d));
        DeclarationIndex::shadow_refused("def", old_r, this.defs.signature_refused(d));
        return old_r;
    }

    /// Record that the method `method` of `owner` has a refused signature.
    mark_method_signature_refused(mut &this, owner: TypeRef, method: SymbolStr) -> void {
        if (!owner.is_valid()) { return; }
        this.refused_methods.insert(DeclarationIndex::pack_method_key(owner, method), true);
    }

    /// Whether the method `method` of `owner` had its signature refused.
    method_signature_refused(&this, owner: TypeRef, method: SymbolStr) -> boolean {
        if (!owner.is_valid()) { return false; }
        return this.refused_methods.contains_key(&DeclarationIndex::pack_method_key(owner, method));
    }

    /// PROBE-S53: the two answers, printed when either is true or they differ.
    static shadow_refused(site: string, old_r: boolean, new_r: boolean) -> void {
        if (!old_r && !new_r) { return; }
        mut o: string = "0";
        if (old_r) { o = "1"; }
        mut n: string = "0";
        if (new_r) { n = "1"; }
        fmt::eprintln("SHADOW\\tREFUSED\\t" + site + "\\t" + o + "\\t" + n);
    }""", 1),
])

TR = 'compiler/src/compiler/passes/type_resolution.cryo'
edit(TR, [
("""                    ctx.decl_index.mark_signature_refused(
                        ctx.decl_fn_key(func.name, func.span.file));""",
 """                    ctx.decl_index.mark_signature_refused(
                        ctx.decl_fn_key(func.name, func.span.file));
                    ctx.defs.record_signature_refused(func.def);""", 1),
])

CR = 'compiler/src/compiler/sema/call_resolver.cryo'
edit(CR, [
("""        if (type_sym.is_valid() && this.ctx.decl_index.signature_refused(this.intern.intern(
                fmt::format("%s::%s", this.intern.resolve(type_sym), method_name)))) {
            return TypeRef::invalid();
        }""", """        const old_mr: boolean = type_sym.is_valid() && this.ctx.decl_index.signature_refused(this.intern.intern(
                fmt::format("%s::%s", this.intern.resolve(type_sym), method_name)));
        DeclarationIndex::shadow_refused("method", old_mr,
            this.ctx.decl_index.method_signature_refused(this.types.method_owner_ref(lookup_ref), member.member));
        if (old_mr) {
            return TypeRef::invalid();
        }""", 1),
("""        if (names_module) {
            return this.ctx.decl_index.signature_refused_in_module(scope.member_name,
                scope.require_scope_res("scope-call refused signature").def_id(), this.intern);
        }
        const owner_key: SymbolStr = this.scope_owner_key(scope);
        if (!owner_key.is_valid()) { return false; }
        const combined: SymbolStr = this.intern.intern(fmt::format("%s::%s",
            this.intern.resolve(owner_key), this.intern.resolve(scope.member_name)));
        return this.ctx.decl_index.signature_refused(combined);""",
 """        if (names_module) {
            const old_m: boolean = this.ctx.decl_index.signature_refused_in_module(scope.member_name,
                scope.require_scope_res("scope-call refused signature").def_id(), this.intern);
            DeclarationIndex::shadow_refused("module", old_m,
                this.ctx.defs.signature_refused(scope.member_def));
            return old_m;
        }
        mut owner_t: TypeRef = scope.spec_owner;
        if (!owner_t.is_valid()) { owner_t = this.types.scope_qualifier_type(scope); }
        const new_s: boolean = this.ctx.decl_index.method_signature_refused(owner_t, scope.member_name);
        const owner_key: SymbolStr = this.scope_owner_key(scope);
        if (!owner_key.is_valid()) {
            DeclarationIndex::shadow_refused("static-nokey", false, new_s);
            return false;
        }
        const combined: SymbolStr = this.intern.intern(fmt::format("%s::%s",
            this.intern.resolve(owner_key), this.intern.resolve(scope.member_name)));
        const old_s: boolean = this.ctx.decl_index.signature_refused(combined);
        DeclarationIndex::shadow_refused("static", old_s, new_s);
        return old_s;""", 1),
])
print('ok')
