"""Unit 3: the shadow's new answers drive; the composed-name refusal map,
its three functions and the shadow go.  Run on the tree `u3_shadow.py` made."""

def edit(path, pairs):
    src = open(path, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in src else '\n'
    for a, b, n in pairs:
        a = a.replace('\n', nl); b = b.replace('\n', nl)
        c = src.count(a)
        assert c == n, (path, c, n, a[:90])
        src = src.replace(a, b)
    open(path, 'w', encoding='utf-8', newline='').write(src)

DI = 'compiler/src/compiler/decl_index.cryo'
edit(DI, [
("""    refused_signatures:  HashMap<u32, boolean>;
    refused_methods:     HashMap<u64, boolean>;""",
 """    // A method whose written signature the name layer refused, by its
    // owner and leaf as `method_returns` is keyed: it registers no signature,
    // and a call to it reports nothing more.  A free function's refusal is
    // recorded on its definition (`DefTable::signature_refused`).
    refused_methods:     HashMap<u64, boolean>;""", 1),
("""            refused_signatures: HashMap::<u32, boolean>::new(),
""", "", 1),
("""                this.mark_signature_refused(combined_sym);
                this.mark_method_signature_refused(owner, func.name);""",
 """                this.mark_method_signature_refused(owner, func.name);""", 1),
])

src = open(DI, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in src else '\n'
a = src.index('    /// Record that the declaration `key` would register under has a')
b = src.index('    /// Record that the method `method` of `owner` has a refused signature.', a)
src = src[:a] + src[b:]
a = src.index('    /// PROBE-S53: the two answers, printed when either is true or they differ.')
b = src.index('        fmt::eprintln("SHADOW', a)
b = src.index('    }', b) + len('    }')
while src[b] in '\r\n':
    b += 1
src = src[:a] + src[b:]
# the shadow sat between `method_signature_refused` and `signature_is_refused`;
# keep one blank line between the two
src = src.replace('    }' + nl + '    /// Whether `func`\'s written signature carries a name the name layer',
                  '    }' + nl + nl + '    /// Whether `func`\'s written signature carries a name the name layer', 1)
open(DI, 'w', encoding='utf-8', newline='').write(src)

edit('compiler/src/compiler/passes/type_resolution.cryo', [
("""                    ctx.decl_index.mark_signature_refused(
                        ctx.decl_fn_key(func.name, func.span.file));
                    ctx.defs.record_signature_refused(func.def);""",
 """                    ctx.defs.record_signature_refused(func.def);""", 1),
("""                // The refusal is recorded under the declaration's key so a
                // call to it finds a declaration and reports nothing.""",
 """                // The refusal is recorded on the definition so a call to it
                // finds a declaration and reports nothing.""", 1),
])

CR = 'compiler/src/compiler/sema/call_resolver.cryo'
edit(CR, [
("""        const old_mr: boolean = type_sym.is_valid() && this.ctx.decl_index.signature_refused(this.intern.intern(
                fmt::format("%s::%s", this.intern.resolve(type_sym), method_name)));
        DeclarationIndex::shadow_refused("method", old_mr,
            this.ctx.decl_index.method_signature_refused(this.types.method_owner_ref(lookup_ref), member.member));
        if (old_mr) {
            return TypeRef::invalid();
        }""",
 """        if (this.ctx.decl_index.method_signature_refused(
                this.types.method_owner_ref(lookup_ref), member.member)) {
            return TypeRef::invalid();
        }""", 1),
("""        if (names_module) {
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
        return old_s;""",
 """        if (names_module) {
            return this.ctx.defs.signature_refused(scope.member_def);
        }
        mut owner_t: TypeRef = scope.spec_owner;
        if (!owner_t.is_valid()) { owner_t = this.types.scope_qualifier_type(scope); }
        return this.ctx.decl_index.method_signature_refused(owner_t, scope.member_name);""", 1),
("""    /// Whether the declaration a `Scope::member` call names has a signature
    /// the name layer refused: a module's function under its canonical name
    /// (`names_module`), else the owner's method under `Owner::member`, the
    /// owner by the key its methods registered under.""",
 """    /// Whether the declaration a `Scope::member` call names has a signature
    /// the name layer refused: a module's function, the definition the name
    /// layer bound for the member (`names_module`), else the owner's method,
    /// by the owner type its methods registered on and the written leaf.""", 1),
("this.ctx.decl_index.signature_refused_of_def(stamped_r.def_id())",
 "this.ctx.defs.signature_refused(stamped_r.def_id())", 1),
])

edit('compiler/src/compiler/sema/sema.cryo', [
("this.ctx.decl_index.signature_refused_of_def(def_id)",
 "this.ctx.defs.signature_refused(def_id)", 1),
])
print('ok')
