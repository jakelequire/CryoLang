"""Temporary instrument: at every binding of a call to a TRAIT method, say
whether the trait is in scope of the module that WROTE the call.
SHADOW<TAB>TSC<TAB>kind<TAB>verdict<TAB>bound?<TAB>file:line:col<TAB>trait<TAB>site
kind: method|path|value ; verdict: same|prelude|import|other|OUT ; bound: bnd|-"""
import io
P = "compiler/src/compiler/sema/call_resolver.cryo"
t = io.open(P, encoding="utf-8", newline="").read()
def rep(old, new, n=1):
    global t
    assert t.count(old) == n, (old[:60], t.count(old))
    t = t.replace(old, new)

helper = '''
    /// SHADOW (temporary): is `trait_q` in scope of the module that wrote `span`?
    shadow_trait_scope(mut &this, kind: string, trait_q: SymbolStr, span: SourceSpan, bound: boolean) -> void {
        if (!trait_q.is_valid()) { return; }
        const site: SymbolStr = this.call_use_site_ns(span);
        const trait_ns: SymbolStr = this.ctx.decl_index.namespace_of(trait_q, this.intern);
        const trait_str: string = this.intern.resolve(trait_q);
        const leaf: SymbolStr = this.intern.intern(QualifiedName::leaf_of(trait_str));
        mut verdict: string = "OUT";
        if (site.is_valid() && trait_ns.is_valid() && site.id == trait_ns.id) {
            verdict = "same";
        } else {
            const resolver: Resolver* = this.ctx.get_resolver();
            const sid: u64 = if (site.is_valid()) { resolver.find_module_scope(site) } else { 0 };
            mut found: SymbolID = SymbolID::invalid();
            if (sid != 0) {
                const scope: Scope* = resolver.get_scope(ScopeID::new(sid));
                found = scope.find(leaf);
            }
            if (found.is_valid()) {
                const sym: Symbol = resolver.get_symbol(found);
                const tns_str: string = this.intern.resolve(trait_ns);
                verdict = if (sym.source_module == tns_str || sym.source_module == trait_str) { "import" } else { "other" };
            } else {
                const pre: SymbolID = resolver.lookup_prelude(leaf);
                if (pre.is_valid()) {
                    const psym: Symbol = resolver.get_symbol(pre);
                    const tns_str2: string = this.intern.resolve(trait_ns);
                    verdict = if (psym.source_module == tns_str2 || psym.source_module == trait_str) { "prelude" } else { "other" };
                }
            }
        }
        fmt::eprintf("SHADOW\\tTSC\\t%s\\t%s\\t%s\\t%s:%u:%u\\t%s\\t%s\\n", kind, verdict,
            if (bound) { "bnd" } else { "-" }, span.file, span.start_line, span.start_col,
            trait_str, this.intern.resolve(site));
    }

    /// The overload FAMILY a bare callee names: the canonical name under which
'''
rep("\n    /// The overload FAMILY a bare callee names: the canonical name under which\n", helper)

# method binds
rep('''            call.set_resolved_method(own.ast_node as ASTNode*);
            this.pin_method_callee_from_qname(call, own_owner, own.ast_node as MethodNode*, own.is_static);
            return true;''',
'''            call.set_resolved_method(own.ast_node as ASTNode*);
            this.pin_method_callee_from_qname(call, own_owner, own.ast_node as MethodNode*, own.is_static);
            { const omn: MethodNode* = own.ast_node as MethodNode*; if (omn != null && omn.func != null) { this.shadow_trait_scope("method", omn.func.origin_trait, member.span, member.resolved_trait.is_valid() || only_trait.is_valid()); } }
            return true;''')
rep('''                call.set_resolved_method(mn as ASTNode*);
                this.pin_method_callee_from_qname(call, owner_str, mn, mn.is_static);
                return true;''',
'''                call.set_resolved_method(mn as ASTNode*);
                this.pin_method_callee_from_qname(call, owner_str, mn, mn.is_static);
                this.shadow_trait_scope("method", if (fn_node.origin_trait.is_valid()) { fn_node.origin_trait } else { block.qualified_trait_name }, member.span, member.resolved_trait.is_valid());
                return true;''')
# static path: at the end of pin_scope_callee_combined
rep('''        if (this.ctx.decl_index != null) {
            const entries: OverloadId[] = this.ctx.decl_index.lookup_family_entries(sym);
            if (entries.length == 1) {
                call.set_resolved_callee(CalleePin::Decl(entries[0]));
            }
        }
    }''',
'''        if (this.ctx.decl_index != null) {
            const entries: OverloadId[] = this.ctx.decl_index.lookup_family_entries(sym);
            if (entries.length == 1) {
                call.set_resolved_callee(CalleePin::Decl(entries[0]));
            }
        }
        match (&call.resolved_callee) {
            CalleePin::Decl(pe) => { this.shadow_trait_scope("path", this.ctx.decl_index.entry_trait(pe), scope.span, false); }
            _ => { }
        }
    }''')
rep('''        if (this.try_pin_static_method_overload(call, scope, sym)) { return; }''',
'''        if (this.try_pin_static_method_overload(call, scope, sym)) {
            match (&call.resolved_callee) {
                CalleePin::Decl(pe2) => { this.shadow_trait_scope("path", this.ctx.decl_index.entry_trait(pe2), scope.span, false); }
                _ => { }
            }
            return;
        }''')
# value form
rep('''        const entry: OverloadId = this.entry_of_signature(sym, method_ft, scope);
        scope.set_resolved_callee(if (entry.is_valid()) { CalleePin::Decl(entry) } else { CalleePin::Family(sym) });''',
'''        const entry: OverloadId = this.entry_of_signature(sym, method_ft, scope);
        scope.set_resolved_callee(if (entry.is_valid()) { CalleePin::Decl(entry) } else { CalleePin::Family(sym) });
        if (entry.is_valid()) { this.shadow_trait_scope("value", this.ctx.decl_index.entry_trait(entry), scope.span, false); }''')
io.open(P, "w", encoding="utf-8", newline="").write(t)
print("ok")
