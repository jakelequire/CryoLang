"""The sites the uncapped check refused that are not a call's argument, once
`MangledName` carries its bytes: a factory's result was read back as an
interned `.value` or resolved to a `string` straight away.  Each is held as
the `MangledName` it is; the two stores that key by an interned symbol (the
declaration index, the specializer's pin) intern its bytes where they store.

usage: python fix_mangled_sites.py <repo-root>
Every replacement must match exactly once, or nothing is written.
"""
import os, sys

C = "compiler/src/compiler/"
EDITS = [
    (C + "decl_index.cryo",
     "                mangled.value\n",
     "                intern.intern(mangled.as_string())\n"),
    (C + "decl_index.cryo",
     "SymbolStr::empty(), mangled.value, DefId::invalid(), span);",
     "SymbolStr::empty(), intern.intern(mangled.as_string()), DefId::invalid(), span);"),
    (C + "mono/call_specializer.cryo",
     "        return disambig.value;",
     "        return this.intern_table.intern(disambig.as_string());"),
    (C + "codegen/type_map.cryo",
     "const mangled: string = MangledName::for_struct_type(this.intern, QualifiedName::from_symbol(t.qualified_name)).resolve(this.intern);",
     "const mangled: MangledName = MangledName::for_struct_type(this.intern, QualifiedName::from_symbol(t.qualified_name));"),
    (C + "codegen/ops/symbol_resolver.cryo",
     "        const mangled: string = if (entry.extern_symbol.is_valid()) {\n"
     "            intern.resolve(entry.extern_symbol)\n"
     "        } else {\n"
     "            MangledName::for_global(intern, entry.declared_in.as_sym(), entry.leaf).resolve(intern)\n",
     "        const mangled: MangledName = if (entry.extern_symbol.is_valid()) {\n"
     "            MangledName::new(intern.resolve(entry.extern_symbol))\n"
     "        } else {\n"
     "            MangledName::for_global(intern, entry.declared_in.as_sym(), entry.leaf)\n"),
    (C + "codegen/ops/declaration_emitter.cryo",
     "        const mangled: string = MangledName::for_vtable(intern, qname).resolve(intern);",
     "        const mangled: MangledName = MangledName::for_vtable(intern, qname);"),
    (C + "codegen/ops/declaration_emitter.cryo",
     "        const mangled: string = if (node.is_extern_import) {\n"
     "            if (node.link_name.length() > 0) { node.link_name }\n"
     "            else { this.get_intern().resolve(node.name) }\n"
     "        } else {\n"
     "            MangledName::for_global(\n"
     "                this.get_intern(), def_ns, node.name).resolve(this.get_intern())\n",
     "        const mangled: MangledName = if (node.is_extern_import) {\n"
     "            if (node.link_name.length() > 0) { MangledName::new(node.link_name) }\n"
     "            else { MangledName::new(this.get_intern().resolve(node.name)) }\n"
     "        } else {\n"
     "            MangledName::for_global(this.get_intern(), def_ns, node.name)\n"),
    (C + "codegen/visit/call_emitter.cryo",
     "const vtbl_name: string = MangledName::for_vtable(\n"
     "                                        intern, qname).resolve(intern);",
     "const vtbl_name: MangledName = MangledName::for_vtable(\n"
     "                                        intern, qname);"),
    (C + "codegen/visit/new_delete_emitter.cryo",
     "const vtbl_name: string = MangledName::for_vtable(intern, qname).resolve(intern);",
     "const vtbl_name: MangledName = MangledName::for_vtable(intern, qname);"),
    (C + "codegen/visit/ir_generator.cryo",
     "const vtbl_name: string = MangledName::for_vtable(intern, qname).resolve(intern);",
     "const vtbl_name: MangledName = MangledName::for_vtable(intern, qname);"),
    (C + "codegen/state/diag_sink.cryo",
     "if (this.stripped_func_names[i] == name) {",
     "if (this.stripped_func_names[i] == name.as_string()) {"),
    (C + "codegen/ops/intrinsic_emitter.cryo",
     'if (name == "__cryo_panic_overflow" && i == 1) {',
     'if (name.as_string() == "__cryo_panic_overflow" && i == 1) {'),
]

root = sys.argv[1]
texts = {}
for f, old, new in EDITS:
    p = os.path.join(root, f)
    if p not in texts:
        raw = open(p, encoding="utf-8", newline="").read()
        texts[p] = (raw, "\r\n" if "\r\n" in raw else "\n")
    raw, nl = texts[p]
    o, n = old.replace("\n", nl), new.replace("\n", nl)
    # the struct-type line appears twice in type_map: both are the same edit
    want = 2 if "for_struct_type(this.intern" in old else 1
    if raw.count(o) != want:
        sys.exit("%s: expected %d match(es), found %d: %s" % (f, want, raw.count(o), old[:60]))
    texts[p] = (raw.replace(o, n), nl)
for p, (raw, _) in texts.items():
    open(p, "w", encoding="utf-8", newline="").write(raw)
print("applied %d edit(s) in %d file(s)" % (len(EDITS), len(texts)))
