"""Unit 2: a path's member is looked up in the namespace the path is written
in, both what the module offers and what it declares privately, so a call
`M::f()` is stamped with the function `f` and never with a type `f`."""

def edit(path, pairs):
    src = open(path, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in src else '\n'
    for a, b, n in pairs:
        a = a.replace('\n', nl); b = b.replace('\n', nl)
        c = src.count(a)
        assert c == n, (path, c, n, a[:90])
        src = src.replace(a, b)
    open(path, 'w', encoding='utf-8', newline='').write(src)

RV = 'compiler/src/compiler/resolver/resolver.cryo'
edit(RV, [
("""    lookup_in_module(&this, module_name: SymbolStr, name: SymbolStr) -> SymbolID {
        const mod_key: u64 = module_name.id as u64;
        for (mut i: i64 = 0; i < this.exports.length; i++) {
            if (this.exports[i].second == mod_key) {
                const sym: Symbol = this.get_symbol(this.exports[i].first);
                if (sym.name.equals(name) && sym.visibility.is_public()) {
                    return this.exports[i].first;
                }
            }
        }
        return SymbolID::invalid();
    }
""", """    lookup_in_module(&this, module_name: SymbolStr, name: SymbolStr) -> SymbolID {
        const all: SymbolID[] = this.exports_named(module_name, name);
        if (all.length == 0) { return SymbolID::invalid(); }
        return all[0];
    }

    /// Every public declaration `module_name` exports under `name`, in
    /// declaration order: a type and a function may share a leaf, and a
    /// reader that wants one of them chooses by kind.
    exports_named(&this, module_name: SymbolStr, name: SymbolStr) -> SymbolID[] {
        mut out: SymbolID[] = [];
        const mod_key: u64 = module_name.id as u64;
        for (mut i: i64 = 0; i < this.exports.length; i++) {
            if (this.exports[i].second == mod_key) {
                const sym: Symbol = this.get_symbol(this.exports[i].first);
                if (sym.name.equals(name) && sym.visibility.is_public()) {
                    out.push(this.exports[i].first);
                }
            }
        }
        return out;
    }
""", 1),
("""    /// The top-level declaration `module` holds under `name` and marks
    /// `private`, or an invalid id - the one case a path or an import can
    /// name a declaration and still be handed nothing, which the writer must
    /// be told about rather than left to read as "no such name".
    ///
    /// Read off the module's own scope: the export list holds only what is
    /// public, so a private declaration is exactly what it does not carry.
    private_declaration(&this, module_name: SymbolStr, name: SymbolStr) -> SymbolID {
        const sid: u64 = this.find_module_scope(ModulePath::of(module_name));
        if (sid == 0) { return SymbolID::invalid(); }
        const scope: Scope* = this.get_scope(ScopeID { id: sid });
        const found: SymbolID = scope.find(name);
        if (!found.is_valid()) { return SymbolID::invalid(); }
        const sym: Symbol = this.get_symbol(found);
        if (sym.kind == SymbolKind::Import) { return SymbolID::invalid(); }
        if (sym.visibility.is_public()) { return SymbolID::invalid(); }
        return found;
    }
""", """    /// The top-level declarations `module` holds under `name` and marks
    /// `private` - the one case a path or an import can name a declaration
    /// and still be handed nothing, which the writer must be told about
    /// rather than left to read as "no such name".  Every one, in
    /// declaration order: a type and a function may share a leaf (the second
    /// sits in the scope's overload set), and a reader that wants one of
    /// them chooses by kind.
    ///
    /// Read off the module's own scope: the export list holds only what is
    /// public, so a private declaration is exactly what it does not carry.
    private_declarations(&this, module_name: SymbolStr, name: SymbolStr) -> SymbolID[] {
        mut out: SymbolID[] = [];
        const sid: u64 = this.find_module_scope(ModulePath::of(module_name));
        if (sid == 0) { return out; }
        const scope: Scope* = this.get_scope(ScopeID { id: sid });
        const all: SymbolID[] = scope.get_overloads(name);
        for (mut i: i64 = 0; i < all.length; i++) {
            const sym: Symbol = this.get_symbol(all[i]);
            if (sym.kind == SymbolKind::Import) { continue; }
            if (sym.visibility.is_public()) { continue; }
            out.push(all[i]);
        }
        return out;
    }
""", 1),
])

NR = 'compiler/src/compiler/resolver/name_resolution.cryo'
edit(NR, [
# the module-rooted TYPE path
("        const offered: SymbolID[] = this.module_offerings(head.found, leaf_sym);",
 "        const offered: SymbolID[] = this.module_offerings(head.found, leaf_sym, Namespace::Type);", 1),
("        const hidden: SymbolID = this.resolver.private_declaration(head.found, leaf_sym);",
 "        const hidden: SymbolID = this.private_in(head.found, leaf_sym, Namespace::Type);", 1),
# the scope segment's member: a value
("            const hit: SymbolID = this.module_offering(found, node.member_name, &offered);",
 "            const hit: SymbolID = this.module_offering(found, node.member_name, Namespace::Value, &offered);", 1),
("                const hidden: SymbolID = this.resolver.private_declaration(found, node.member_name);",
 "                const hidden: SymbolID = this.private_in(found, node.member_name, Namespace::Value);", 1),
# imports ask whether any private declaration carries the name
("""                } else if (!node.is_export
                           && this.resolver.private_declaration(module_sym, name).is_valid()) {""",
 """                } else if (!node.is_export
                           && this.resolver.private_declarations(module_sym, name).length > 0) {""", 1),
("""                } else if (node.is_export
                           && this.resolver.private_declaration(module_sym, name).is_valid()) {""",
 """                } else if (node.is_export
                           && this.resolver.private_declarations(module_sym, name).length > 0) {""", 1),
("""    module_offering(&this, m: SymbolStr, name: SymbolStr, hits: i64*) -> SymbolID {
        const all: SymbolID[] = this.module_offerings(m, name);""",
 """    module_offering(&this, m: SymbolStr, name: SymbolStr, ns: Namespace, hits: i64*) -> SymbolID {
        const all: SymbolID[] = this.module_offerings(m, name, ns);""", 1),
("""    module_offerings(&this, m: SymbolStr, name: SymbolStr) -> SymbolID[] {
        mut out: SymbolID[] = [];
        // A module that DECLARES the name offers that one.  A re-export cannot
        // displace a declaration, so this is not a first-of-several choice.
        const own: SymbolID = this.resolver.lookup_in_module(m, name);
        if (own.is_valid()) {
            out.push(own);
            return out;
        }""",
 """    module_offerings(&this, m: SymbolStr, name: SymbolStr, ns: Namespace) -> SymbolID[] {
        mut out: SymbolID[] = [];
        // A module that DECLARES the name offers that one.  A re-export cannot
        // displace a declaration, so this is not a first-of-several choice.
        const own: SymbolID = this.export_in(m, name, ns);
        if (own.is_valid()) {
            out.push(own);
            return out;
        }""", 1),
("""            const cand: SymbolID = this.resolver.lookup_in_module(via[i], name);
            if (!cand.is_valid()) { continue; }""",
 """            const cand: SymbolID = this.export_in(via[i], name, ns);
            if (!cand.is_valid()) { continue; }""", 1),
])

# the two namespace readers, beside `module_offerings`
src = open(NR, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in src else '\n'
anchor = '    /// Binds what an `import` - or an `export` - names into the current scope.'
assert src.count(anchor) == 1
helpers = """    /// The public declaration module `m` exports under `name` in the
    /// namespace `ns`, or an invalid id: `M::f` in an expression names the
    /// function `f`, never a type `M` also declares as `f`.
    export_in(&this, m: SymbolStr, name: SymbolStr, ns: Namespace) -> SymbolID {
        const all: SymbolID[] = this.resolver.exports_named(m, name);
        for (mut i: i64 = 0; i < all.length; i++) {
            if (ns.accepts(this.resolver.get_symbol(all[i]).kind)) { return all[i]; }
        }
        return SymbolID::invalid();
    }

    /// The private declaration module `m` holds under `name` in the
    /// namespace `ns`, or an invalid id.
    private_in(&this, m: SymbolStr, name: SymbolStr, ns: Namespace) -> SymbolID {
        const all: SymbolID[] = this.resolver.private_declarations(m, name);
        for (mut i: i64 = 0; i < all.length; i++) {
            if (ns.accepts(this.resolver.get_symbol(all[i]).kind)) { return all[i]; }
        }
        return SymbolID::invalid();
    }

""".replace('\n', nl)
src = src.replace(anchor, helpers + anchor, 1)
open(NR, 'w', encoding='utf-8', newline='').write(src)

IN = 'compiler/src/compiler/instance.cryo'
edit(IN, [
("                    if (!ctx.resolver.private_declaration(module_sym, item).is_valid()) {",
 "                    if (ctx.resolver.private_declarations(module_sym, item).length == 0) {", 1),
])
print('ok')
