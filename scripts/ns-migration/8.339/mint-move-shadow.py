# Every identity the tree used to mint at a USE, now read from the
# registration that made it (section 8.339), asked both ways at the use:
#   SHADOW mint <site> agree|agree-invalid|differ new=<id> old=<id>
#   SHADOW mintkind <site> <SymbolKind>        (name-layer sites: what reached it)
#   SHADOW mintdetail <site> ...                (names, on a disagreement)
# "new" is the stamp the site reads now; "old" is the mint the site made before
# the move, recomputed beside it.  A site that prints nothing was never entered
# (absent); a site whose lines are all agree-invalid was entered and answered
# nothing (starved).
#
# The import sites ask a DIFFERENT question, the one an index would need: the
# import's own stamp (built from the import symbol, as today) against the stamp
# of the declaration it binds (what copying the id at import would give).
#   SHADOW mint imp-<form> ...
#
# A MEASUREMENT: applied to the tree carrying the unit, never committed applied.
#   git diff > .objcmp/u1/unit1.patch
#   python scripts/ns-migration/8.339/mint-move-shadow.py
#   rm -rf compiler/build && make cryo
#   bash scripts/objcmp/corpus2.sh mint
#   git checkout -- compiler/src && git apply .objcmp/u1/unit1.patch
import os
R = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
C = os.path.join(R, "compiler", "src", "compiler")

def patch(rel, pairs):
    p = os.path.join(C, rel)
    s = open(p, newline="").read()
    nl = "\r\n" if "\r\n" in s else "\n"
    for old, new in pairs:
        old = old.replace("\n", nl)
        new = new.replace("\n", nl)
        assert s.count(old) == 1, (rel, old[:90], s.count(old))
        s = s.replace(old, new)
    open(p, "w", newline="").write(s)

patch("resolver/res.cryo", [
    ("    /// Whether this names a definition at all.\n",
     "    static shadow(site: string, n: DefId, o: DefId) -> DefId {\n"
     "        if (n.equals(o)) {\n"
     "            if (n.is_valid()) { fmt::printf(\"SHADOW mint %s agree\\n\", site); }\n"
     "            else { fmt::printf(\"SHADOW mint %s agree-invalid\\n\", site); }\n"
     "        } else {\n"
     "            fmt::printf(\"SHADOW mint %s differ new=%d old=%d\\n\", site, n.qn.id as i32, o.qn.id as i32);\n"
     "        }\n"
     "        return n;\n"
     "    }\n\n"
     "    /// Whether this names a definition at all.\n"),
])

patch("resolver/resolver.cryo", [
    ("    /// Insert a symbol into the arena and current scope.\n",
     "    probe_def(&this, site: string, sym: &Symbol) -> DefId {\n"
     "        fmt::printf(\"SHADOW mintkind %s %s\\n\", site, sym.kind.to_string());\n"
     "        const o: DefId = DefId::of_definition(this.qualified_name_of(sym));\n"
     "        if (!sym.def.equals(o)) {\n"
     "            fmt::printf(\"SHADOW mintdetail %s new=%s old=%s\\n\", site,\n"
     "                this.intern_table.resolve(sym.def.qualified_name()), this.intern_table.resolve(o.qualified_name()));\n"
     "        }\n"
     "        return DefId::shadow(site, sym.def, o);\n"
     "    }\n\n"
     "    probe_def_id(&this, site: string, id: SymbolID) -> DefId {\n"
     "        const s: Symbol = this.get_symbol(id);\n"
     "        return this.probe_def(site, &s);\n"
     "    }\n\n"
     "    /// Insert a symbol into the arena and current scope.\n"),
    ("            return Res::TypeRelative(ResBase::Def(sym.def), trailing);\n",
     "            return Res::TypeRelative(ResBase::Def(this.probe_def(\"res-head-trailing\", sym)), trailing);\n"),
    ("        return Res::Def(sym.def);\n",
     "        return Res::Def(this.probe_def(\"res-head\", sym));\n"),
    ("import compiler::resolver::symbol_str::{ SymbolStr };\n",
     "import compiler::resolver::symbol_str::{ SymbolStr };\nimport std::fmt;\n"),
])

patch("resolver/name_resolution.cryo", [
    ("        fn_node.def = this.resolver.get_symbol(sym_id).def;\n",
     "        fn_node.def = this.resolver.probe_def_id(\"nr-stamp-fn\", sym_id);\n"),
    ("\n                    return ResSlot::Answered(Res::Def(s.def));\n",
     "\n                    return ResSlot::Answered(Res::Def(this.resolver.probe_def(\"nr-bare-global\", &s)));\n"),
    ("\n        return ResSlot::Answered(Res::Def(s.def));\n",
     "\n        return ResSlot::Answered(Res::Def(this.resolver.probe_def(\"nr-bare\", &s)));\n"),
    ("                    return ResSlot::Answered(Res::Def(this.resolver.get_symbol(decl).def));\n",
     "                    return ResSlot::Answered(Res::Def(this.resolver.probe_def_id(\"nr-alias-member\", decl)));\n"),
    ("            return ResSlot::Answered(Res::Def(this.resolver.get_symbol(offered[0]).def));\n",
     "            return ResSlot::Answered(Res::Def(this.resolver.probe_def_id(\"nr-module-member\", offered[0])));\n"),
    ("                base = ResBase::Def(s.def);\n",
     "                base = ResBase::Def(this.resolver.probe_def(\"nr-scope-base\", &s));\n"),
    ("                node.set_scope_res(Res::Def(this.resolver.get_symbol(alias).def));\n",
     "                node.set_scope_res(Res::Def(DefId::shadow(\"nr-alias-module\",\n"
     "                    this.resolver.get_symbol(alias).def, DefId::of_definition(this.alias_module_of(&bound)))));\n"),
    ("            node.set_scope_res(Res::Def(owner));\n",
     "            mut oo_: SymbolStr = found;\n"
     "            if (offered == 1) {\n"
     "                const hs_: Symbol = this.resolver.get_symbol(hit);\n"
     "                const di_: string = QualifiedName::parent_of(\n"
     "                    this.resolver.intern_table.resolve(this.resolver.qualified_name_of(&hs_)));\n"
     "                if (di_.length() > 0) { oo_ = this.resolver.intern_table.intern(di_); }\n"
     "            }\n"
     "            if (!owner.equals(DefId::of_definition(oo_))) {\n"
     "                fmt::printf(\"SHADOW mintdetail nr-module-owner old=%s found=%s\\n\",\n"
     "                    this.resolver.intern_table.resolve(oo_), this.resolver.intern_table.resolve(found));\n"
     "            }\n"
     "            node.set_scope_res(Res::Def(DefId::shadow(\"nr-module-owner\", owner, DefId::of_definition(oo_))));\n"),
    ("                        hb = ResBase::Def(hs.def);\n",
     "                        hb = ResBase::Def(this.resolver.probe_def(\"nr-path-type-head\", &hs));\n"),
    # Imports: the import's own stamp against the declaration it binds.
    ("                    const import_sym: Symbol = Symbol::import_sym(\n"
     "                        id, export_sym.name, this.resolver.current_scope,\n"
     "                        node.span, resolved_module);\n"
     "                    this.resolver.declare_import(import_sym);\n",
     "                    const import_sym: Symbol = Symbol::import_sym(\n"
     "                        id, export_sym.name, this.resolver.current_scope,\n"
     "                        node.span, resolved_module);\n"
     "                    const iid_: SymbolID = this.resolver.declare_import(import_sym);\n"
     "                    DefId::shadow(\"imp-wildcard\", this.resolver.get_symbol(iid_).def, export_sym.def);\n"),
    ("                    this.resolver.declare_import(rimp);\n",
     "                    const rid_: SymbolID = this.resolver.declare_import(rimp);\n"
     "                    DefId::shadow(\"imp-reexport\", this.resolver.get_symbol(rid_).def, rx_sym.def);\n"),
    ("                        node.span, offered_src);\n"
     "                    this.resolver.declare_import(import_sym);\n",
     "                        node.span, offered_src);\n"
     "                    const sid_: SymbolID = this.resolver.declare_import(import_sym);\n"
     "                    mut oid_: SymbolID = found;\n"
     "                    if (!oid_.is_valid()) {\n"
     "                        oid_ = this.resolver.lookup_in_module(this.reexport_offering(module_sym, name), name);\n"
     "                    }\n"
     "                    DefId::shadow(\"imp-specific\", this.resolver.get_symbol(sid_).def, this.resolver.get_symbol(oid_).def);\n"),
    ("                                node.span, sub_resolved);\n"
     "                            this.resolver.declare_import(import_sym);\n",
     "                                node.span, sub_resolved);\n"
     "                            const bid_: SymbolID = this.resolver.declare_import(import_sym);\n"
     "                            DefId::shadow(\"imp-submodule\", this.resolver.get_symbol(bid_).def, sub_export_sym.def);\n"),
])

patch("passes/type_resolution.cryo", [
    ("                        ctx.generic_registry.claim_wellknown(func.def, ctx.intern_table);\n",
     "                        if (!func.def.equals(DefId::of_definition(q_name))) {\n"
     "                            fmt::printf(\"SHADOW mintdetail tr-claim-fn new=%s old=%s\\n\",\n"
     "                                ctx.intern_table.resolve(func.def.qualified_name()), ctx.intern_table.resolve(q_name));\n"
     "                        }\n"
     "                        ctx.generic_registry.claim_wellknown(\n"
     "                            DefId::shadow(\"tr-claim-fn\", func.def, DefId::of_definition(q_name)), ctx.intern_table);\n"),
])

patch("sema/async_lower.cryo", [
    ("            base: this.named_ann_def(d.q_name, d.struct_decl.def, span),\n",
     "            base: this.named_ann_def(d.q_name,\n"
     "                DefId::shadow(\"al-future-ann\", d.struct_decl.def, DefId::of_definition(d.q_name)), span),\n"),
    ("                    this.ctx.decl_index.def_of(inst.generic_base, this.ctx.type_arena), span),\n",
     "                    DefId::shadow(\"al-generic-base\",\n"
     "                        this.ctx.decl_index.def_of(inst.generic_base, this.ctx.type_arena),\n"
     "                        DefId::of_definition(base_q)), span),\n"),
    ("                this.ctx.generic_registry.wellknown(LangItem::Poll), span),\n",
     "                DefId::shadow(\"al-poll\", this.ctx.generic_registry.wellknown(LangItem::Poll),\n"
     "                    DefId::of_definition(poll_q)), span),\n"),
    ("        impl_node.res.answer(Res::Def(struct_decl.def));\n",
     "        impl_node.res.answer(Res::Def(DefId::shadow(\"al-impl-head\", struct_decl.def, DefId::of_definition(q_name))));\n"),
    ("        ctor_lit.set_synthesized_res(struct_decl.def);\n",
     "        ctor_lit.set_synthesized_res(DefId::shadow(\"al-ctor\", struct_decl.def, DefId::of_definition(q_name)));\n"),
])
print("patched")
