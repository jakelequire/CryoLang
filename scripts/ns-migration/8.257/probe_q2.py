"""Q2's split: of the sites that stand in for an `extern module` alias's
missing stamp, which resolve the ALIAS (the scope segment's answer is not a
module, so a reader falls to the written `alias::member` spelling) and which
resolve a MEMBER of the C-import key space (a declaration keyed by the whole
`alias::member` spelling, found without asking what the alias is)?

    SHADOW<TAB>Q2-<site><TAB><scope or spelling><TAB><member>

sites, ALIAS group (the scope segment's stamp is the missing input):
  CG-VALUE   codegen's scope-resolution value: the stamp names no module, so
             `resolve_global("<scope>::<member>")` is asked by spelling
  CALL-DOOR  sema's `try_resolve_cimport_function` answered a call by the
             spelling `<scope>::<member>` after the type-owner doors declined
  CALL-HINT  sema's callee hint answered by `lookup_func_type_exact(bare_sym)`
  QUAL-TYPE  sema's `scope_qualifier_type` read a `TypeRelative(Def(d))`
             qualifier whose definition the index holds no type for (d)
sites, MEMBER group (a whole-spelling key answered, no alias asked):
  TYPE-LEAF  the name layer answered a qualified type spelling from the
             writer's own module scope as a whole-spelling symbol
             (`leaf_in_module_scope(q_sid, "cit::Vec2")`), the C-import
             type's declaration

Applied over the tree as it stands; `--revert` removes it.
"""
import io, sys
R = r"C:\Programming\apps\CryoLang\compiler\src\compiler"
revert = "--revert" in sys.argv

EDITS = {
    R + r"\codegen\visit\ir_generator.cryo": [
        ("        const global: LValue = if (stamped_ns.is_valid()) {\n"
         "            this.cg.resolver.resolve_global_in_namespace(node.member_name, stamped_ns)\n"
         "        } else {\n"
         "            this.cg.resolver.resolve_global(this.cg.intern_str(fmt::format(\"%s::%s\",\n"
         "                this.cg.resolve(node.scope_name), this.cg.resolve(node.member_name))))\n"
         "        };\n",
         "        if (!stamped_ns.is_valid()) { fmt::eprintf(\"SHADOW\\tQ2-CG-VALUE\\t%s\\t%s\\n\", this.cg.resolve(node.scope_name), this.cg.resolve(node.member_name)); }\n"
         "        const global: LValue = if (stamped_ns.is_valid()) {\n"
         "            this.cg.resolver.resolve_global_in_namespace(node.member_name, stamped_ns)\n"
         "        } else {\n"
         "            this.cg.resolver.resolve_global(this.cg.intern_str(fmt::format(\"%s::%s\",\n"
         "                this.cg.resolve(node.scope_name), this.cg.resolve(node.member_name))))\n"
         "        };\n"),
    ],
    R + r"\sema\call_resolver.cryo": [
        ("        const ret: TypeRef = this.family_return_type(q_sym);\n"
         "        if (ret.is_valid()) {\n"
         "            scope.set_resolved_type(ret);\n",
         "        const ret: TypeRef = this.family_return_type(q_sym);\n"
         "        if (ret.is_valid()) {\n"
         "            fmt::eprintf(\"SHADOW\\tQ2-CALL-DOOR\\t%s\\t%s\\n\", scope_str, member_str);\n"
         "            scope.set_resolved_type(ret);\n"),
        ("            return this.types.lookup_func_type_exact(bare_sym);\n",
         "            const bare_ft: TypeRef = this.types.lookup_func_type_exact(bare_sym);\n"
         "            if (bare_ft.is_valid()) { fmt::eprintf(\"SHADOW\\tQ2-CALL-HINT\\t%s\\t%s\\n\", this.intern.resolve(sr.scope_name), this.intern.resolve(sr.member_name)); }\n"
         "            return bare_ft;\n"),
    ],
    R + r"\sema\type_utils.cryo": [
        ("            ResBase::Def(d)    => { return this.ctx.decl_index.type_of_def(d); }\n",
         "            ResBase::Def(d)    => {\n"
         "                const dt: TypeRef = this.ctx.decl_index.type_of_def(d);\n"
         "                if (!dt.is_valid()) { fmt::eprintf(\"SHADOW\\tQ2-QUAL-TYPE\\t%s\\t%s\\n\", this.ctx.intern_table.resolve(d.qualified_name()), this.ctx.intern_table.resolve(scope.member_name)); }\n"
         "                return dt;\n"
         "            }\n"),
    ],
    R + r"\resolver\name_resolution.cryo": [
        ("                        const alias_slot: ResSlot = this.leaf_in_module_scope(q_sid, name, span);\n"
         "                        if (!alias_slot.is_pending()) { return alias_slot; }\n",
         "                        const alias_slot: ResSlot = this.leaf_in_module_scope(q_sid, name, span);\n"
         "                        if (!alias_slot.is_pending()) { fmt::eprintf(\"SHADOW\\tQ2-TYPE-LEAF\\t%s\\t%s\\n\", q_prefix, QualifiedName::leaf_of(written)); return alias_slot; }\n"),
    ],
}


def main():
    for path, edits in EDITS.items():
        src = io.open(path, encoding="utf-8", newline="").read()
        nl = "\r\n" if "\r\n" in src else "\n"
        edits = [(a.replace("\n", nl), b.replace("\n", nl)) for a, b in edits]
        if revert:
            for a, b in edits:
                if src.count(b) != 1:
                    raise SystemExit("no probe to revert at: %r" % a[:60])
                src = src.replace(b, a)
        else:
            if "SHADOW\\tQ2-" in src:
                raise SystemExit("already probed: " + path)
            for a, b in edits:
                if src.count(a) != 1:
                    raise SystemExit("anchor not exactly once (%d) in %s: %r" % (src.count(a), path, a[:70]))
                src = src.replace(a, b)
        io.open(path, "w", encoding="utf-8", newline="").write(src)
    print("reverted" if revert else "probed")


if __name__ == "__main__":
    main()
