"""Part 2: every reader that asked the index for a type declaration's OWN
type by a key re-derived from the node asks `type_of_def(node.def)`.

With --shadow, each converted site also keeps the old key-derived answer and
prints `SHADOW\tTDEF\t<site>\t<verdict>` through a temporary
`DeclarationIndex::shadow_tdef`; without it the sites are converted clean.
Nothing is saved until every replacement has matched exactly once.
"""
import os as _os
_REPO = _os.path.abspath(_os.path.join(_os.path.dirname(__file__), "..", "..", ".."))
import io, os, sys
ROOT = _REPO
SHADOW = "--shadow" in sys.argv
edits = {}

def rw(rel, pairs):
    p = os.path.join(ROOT, rel)
    t = io.open(p, encoding="utf-8", newline="").read()
    for old, new in pairs:
        n = t.count(old)
        assert n == 1, (rel, old[:90], n)
        t = t.replace(old, new)
    edits[p] = t

def conv(indent, lhs_decl, recv, node, ns_expr, name_field, site, old_lookup_lines):
    """Build (old, new) for one site.

    old_lookup_lines: the exact old text of the lookup statement.
    new: `<lhs_decl> = <recv>.type_of_def(<node>.def);` plus, under --shadow,
    the comparison line carrying the old key derivation.
    """
    new = "%s%s = %s.type_of_def(%s.def);\n" % (indent, lhs_decl, recv, node)
    if SHADOW:
        new += "%s%s.shadow_tdef(\"%s\", %s.def, %s.decl_type_key(%s.%s, %s, %s.span.file));\n" % (
            indent, recv, site, node, CTX[recv], node, name_field, ns_expr, node)
    return (old_lookup_lines, new)

# receiver -> the CompilationContext expression that owns decl_type_key
CTX = {
    "this.ctx.decl_index": "this.ctx",
    "ctx.decl_index": "ctx",
    "di": "c",
}

DE = "compiler/src/compiler/codegen/ops/declaration_emitter.cryo"
de_pairs = []
for node_ty, ns in (("struct", "node.binding_namespace"), ("union", "node.binding_namespace"),
                    ("class", "SymbolStr::empty()"), ("enum", "node.binding_namespace")):
    old = ("        const type_ref: TypeRef = this.ctx.decl_index.lookup_type(\n"
           "            this.ctx.decl_type_key(node.name, %s, node.span.file));\n" % ns)
    de_pairs.append(conv("        ", "const type_ref: TypeRef", "this.ctx.decl_index", "node", ns,
                         "name", "cg_declare_%s" % node_ty, old))
p = os.path.join(ROOT, DE)
t = io.open(p, encoding="utf-8", newline="").read()
# struct, union and enum share one text (all `node.binding_namespace`); they
# sit in file order, so each first-occurrence replace takes the next arm.
assert t.count(de_pairs[0][0]) == 3, t.count(de_pairs[0][0])
assert t.count(de_pairs[2][0]) == 1, t.count(de_pairs[2][0])
t = t.replace(de_pairs[0][0], de_pairs[0][1], 1)   # struct
t = t.replace(de_pairs[1][0], de_pairs[1][1], 1)   # union
t = t.replace(de_pairs[2][0], de_pairs[2][1], 1)   # class
t = t.replace(de_pairs[3][0], de_pairs[3][1], 1)   # enum
if not SHADOW:
    assert t.count("this.ctx.decl_type_key(node.name, node.binding_namespace, node.span.file));\n") == 0
old_c = ("        // The declaration's own key: the type is registered under it, and a\n"
         "        // written type's leaf is not a key any store holds.\n"
         "        const type_ref: TypeRef = this.ctx.decl_index.type_of_def(node.def);\n")
assert t.count(old_c) == 1, t.count(old_c)
t = t.replace(old_c,
    "        // The declaration's own registration, by the id it stamped.\n"
    "        const type_ref: TypeRef = this.ctx.decl_index.type_of_def(node.def);\n")
old_m, new_m = conv("        ", "const di_ref: TypeRef", "di", "node", "SymbolStr::empty()",
                    "name", "cg_class_methods",
                    "        const di_ref: TypeRef = di.lookup_type(qualified);\n")
assert t.count(old_m) == 1
t = t.replace(old_m, new_m)
edits[p] = t

DP = "compiler/src/compiler/passes/directive_processing.cryo"
dp_pairs = []
for node_ty, ns in (("struct", "node.binding_namespace"), ("union", "node.binding_namespace"),
                    ("class", "SymbolStr::empty()"), ("enum", "node.binding_namespace")):
    # the four arms are textually identical but for the namespace expression,
    # so the struct/union pair and the enum are told apart by their tails
    pass
old_su = ("                    const qsym: SymbolStr = ctx.decl_type_key(node.name, node.binding_namespace, node.span.file);\n"
          "                    const tref: TypeRef = ctx.decl_index.lookup_type(qsym);\n"
          "                    if (!tref.is_valid()) { continue; }\n"
          "                    const ty: Type* = arena.lookup(tref.id);\n"
          "                    if (ty == null || ty.kind != TypeKind::Struct) { continue; }\n")
new_su = ("                    const tref: TypeRef = ctx.decl_index.type_of_def(node.def);\n"
          + ("                    ctx.decl_index.shadow_tdef(\"dp_%s\", node.def, ctx.decl_type_key(node.name, node.binding_namespace, node.span.file));\n" if SHADOW else "")
          + "                    if (!tref.is_valid()) { continue; }\n"
          "                    const ty: Type* = arena.lookup(tref.id);\n"
          "                    if (ty == null || ty.kind != TypeKind::Struct) { continue; }\n")
p = os.path.join(ROOT, DP)
t = io.open(p, encoding="utf-8", newline="").read()
assert t.count(old_su) == 2, t.count(old_su)
t = t.replace(old_su, new_su % "struct" if SHADOW else new_su, 1)
t = t.replace(old_su, new_su % "union" if SHADOW else new_su, 1)
old_c = ("                    const qsym: SymbolStr = ctx.decl_type_key(node.name, SymbolStr::empty(), node.span.file);\n"
         "                    const tref: TypeRef = ctx.decl_index.lookup_type(qsym);\n")
new_c = ("                    const tref: TypeRef = ctx.decl_index.type_of_def(node.def);\n"
         + ("                    ctx.decl_index.shadow_tdef(\"dp_class\", node.def, ctx.decl_type_key(node.name, SymbolStr::empty(), node.span.file));\n" if SHADOW else ""))
assert t.count(old_c) == 1
t = t.replace(old_c, new_c)
old_e = ("                    const qsym: SymbolStr = ctx.decl_type_key(node.name, node.binding_namespace, node.span.file);\n"
         "                    const tref: TypeRef = ctx.decl_index.lookup_type(qsym);\n")
new_e = ("                    const tref: TypeRef = ctx.decl_index.type_of_def(node.def);\n"
         + ("                    ctx.decl_index.shadow_tdef(\"dp_enum\", node.def, ctx.decl_type_key(node.name, node.binding_namespace, node.span.file));\n" if SHADOW else ""))
assert t.count(old_e) == 1, t.count(old_e)
t = t.replace(old_e, new_e)
old_doc = ("    /// Each declaration is found in the index under its own key\n"
           "    /// (`decl_type_key`: a C import's alias-qualified name, else the module\n"
           "    /// of the file that wrote it), the key its registration used.\n")
new_doc = ("    /// Each declaration is found in the index by the id its registration\n"
           "    /// stamped on it.\n")
assert t.count(old_doc) == 1
t = t.replace(old_doc, new_doc)
edits[p] = t

TR = "compiler/src/compiler/passes/type_resolution.cryo"
tr_pairs = []
for owner, ns, site in (("struct_owner", "node.binding_namespace", "tr_sig_struct"),
                        ("union_owner", "node.binding_namespace", "tr_sig_union"),
                        ("class_owner", "SymbolStr::empty()", "tr_sig_class")):
    old = ("                    const %s: TypeRef = ctx.decl_index.lookup_type(\n"
           "                        ctx.decl_type_key(node.name, %s, node.span.file));\n" % (owner, ns))
    tr_pairs.append(conv("                    ", "const %s: TypeRef" % owner, "ctx.decl_index", "node",
                         ns, "name", site, old))
tr_pairs.append((
    "                    const tr_qname: SymbolStr = ctx.decl_type_key(node.name, SymbolStr::empty(), node.span.file);\n"
    "                    const tr_ref: TypeRef = ctx.decl_index.lookup_type(tr_qname);\n",
    "                    const tr_ref: TypeRef = ctx.decl_index.type_of_def(node.def);\n"
    + ("                    ctx.decl_index.shadow_tdef(\"tr_trait\", node.def, ctx.decl_type_key(node.name, SymbolStr::empty(), node.span.file));\n" if SHADOW else "")))
tr_pairs.append((
    "                        const alias_ref: TypeRef = ctx.decl_index.lookup_type(\n"
    "                            ctx.decl_type_key(node.alias_name, node.binding_namespace, node.span.file));\n",
    "                        const alias_ref: TypeRef = ctx.decl_index.type_of_def(node.def);\n"
    + ("                        ctx.decl_index.shadow_tdef(\"tr_alias\", node.def, ctx.decl_type_key(node.alias_name, node.binding_namespace, node.span.file));\n" if SHADOW else "")))
# the four field-population arms
for ns, site in (("node.binding_namespace", "tr_fields_struct"), ("node.binding_namespace", "tr_fields_union"),
                 ("SymbolStr::empty()", "tr_fields_class"), ("node.binding_namespace", "tr_fields_enum")):
    pass
p = os.path.join(ROOT, TR)
t = io.open(p, encoding="utf-8", newline="").read()
for old, new in tr_pairs:
    assert t.count(old) == 1, (old[:90], t.count(old))
    t = t.replace(old, new)
old_f = ("                    const qualified_sym: SymbolStr = ctx.decl_type_key(node.name, node.binding_namespace, node.span.file);\n"
         "                    const type_ref: TypeRef = ctx.decl_index.lookup_type(qualified_sym);\n"
         "                    if (!type_ref.is_valid()) { continue; }\n")
assert t.count(old_f) == 3, t.count(old_f)
for site in ("tr_fields_struct", "tr_fields_union", "tr_fields_enum"):
    new_f = ("                    const type_ref: TypeRef = ctx.decl_index.type_of_def(node.def);\n"
             + ("                    ctx.decl_index.shadow_tdef(\"%s\", node.def, ctx.decl_type_key(node.name, node.binding_namespace, node.span.file));\n" % site if SHADOW else "")
             + "                    if (!type_ref.is_valid()) { continue; }\n")
    t = t.replace(old_f, new_f, 1)
old_fc = ("                    const qualified_sym: SymbolStr = ctx.decl_type_key(node.name, SymbolStr::empty(), node.span.file);\n"
          "                    const type_ref: TypeRef = ctx.decl_index.lookup_type(qualified_sym);\n"
          "                    if (!type_ref.is_valid()) { continue; }\n")
assert t.count(old_fc) == 1, t.count(old_fc)
t = t.replace(old_fc, "                    const type_ref: TypeRef = ctx.decl_index.type_of_def(node.def);\n"
              + ("                    ctx.decl_index.shadow_tdef(\"tr_fields_class\", node.def, ctx.decl_type_key(node.name, SymbolStr::empty(), node.span.file));\n" if SHADOW else "")
              + "                    if (!type_ref.is_valid()) { continue; }\n")
edits[p] = t

SE = "compiler/src/compiler/sema/sema.cryo"
se_pairs = [
    # struct: generic arm + concrete arm
    ("        if (node.is_generic()) {\n"
     "            const sym: SymbolStr = this.ctx.decl_type_key(node.name, node.binding_namespace, node.span.file);\n"
     "            const ty: TypeRef = this.types.lookup_type_exact(sym);\n"
     "            this.symbolic_check_owner_methods(node.methods, ty, node.generic_params);\n"
     "            return;\n"
     "        }\n"
     "        // The declaration's own key: its registered name when it was minted\n"
     "        // qualified (a closure struct), else the module of the file that\n"
     "        // wrote it - for an injected specialization the TEMPLATE's file, so\n"
     "        // the key is the arena's `template module :: spec` and not the\n"
     "        // module the clone was placed in.\n"
     "        const qualified_sym: SymbolStr = this.ctx.decl_type_key(node.name, node.binding_namespace, node.span.file);\n"
     "        const st_type: TypeRef = this.types.lookup_type_exact(qualified_sym);\n"
     "        this.visit_methods(node.methods, st_type);\n",
     "        // The declaration's own registration, by the id it stamped - for an\n"
     "        // injected specialization the one its injector wrote, whichever\n"
     "        // module the clone was placed in.\n"
     "        const st_type: TypeRef = this.ctx.decl_index.type_of_def(node.def);\n"
     + ("        this.ctx.decl_index.shadow_tdef(\"se_struct\", node.def, this.ctx.decl_type_key(node.name, node.binding_namespace, node.span.file));\n" if SHADOW else "")
     + "        if (node.is_generic()) {\n"
     "            this.symbolic_check_owner_methods(node.methods, st_type, node.generic_params);\n"
     "            return;\n"
     "        }\n"
     "        this.visit_methods(node.methods, st_type);\n"),
    # union
    ("        if (node.is_generic()) {\n"
     "            const sym: SymbolStr = this.ctx.decl_type_key(node.name, node.binding_namespace, node.span.file);\n"
     "            const ty: TypeRef = this.types.lookup_type_exact(sym);\n"
     "            this.symbolic_check_owner_methods(node.methods, ty, node.generic_params);\n"
     "            return;\n"
     "        }\n"
     "        const qualified_sym: SymbolStr = this.ctx.decl_type_key(node.name, node.binding_namespace, node.span.file);\n"
     "        const un_type: TypeRef = this.types.lookup_type_exact(qualified_sym);\n"
     "        this.visit_methods(node.methods, un_type);\n",
     "        const un_type: TypeRef = this.ctx.decl_index.type_of_def(node.def);\n"
     + ("        this.ctx.decl_index.shadow_tdef(\"se_union\", node.def, this.ctx.decl_type_key(node.name, node.binding_namespace, node.span.file));\n" if SHADOW else "")
     + "        if (node.is_generic()) {\n"
     "            this.symbolic_check_owner_methods(node.methods, un_type, node.generic_params);\n"
     "            return;\n"
     "        }\n"
     "        this.visit_methods(node.methods, un_type);\n"),
    # class
    ("        // A class is never C-imported, so its key has no alias arm.\n"
     "        const qualified_sym: SymbolStr = this.ctx.decl_type_key(node.name, SymbolStr::empty(), node.span.file);\n"
     "        if (node.is_generic()) {\n"
     "            const ty: TypeRef = this.types.lookup_type_exact(qualified_sym);\n"
     "            this.symbolic_check_owner_methods(node.methods, ty, node.generic_params);\n"
     "            return;\n"
     "        }\n"
     "        const cl_type: TypeRef = this.types.lookup_type_exact(qualified_sym);\n"
     "        this.visit_methods(node.methods, cl_type);\n",
     "        const cl_type: TypeRef = this.ctx.decl_index.type_of_def(node.def);\n"
     + ("        this.ctx.decl_index.shadow_tdef(\"se_class\", node.def, this.ctx.decl_type_key(node.name, SymbolStr::empty(), node.span.file));\n" if SHADOW else "")
     + "        if (node.is_generic()) {\n"
     "            this.symbolic_check_owner_methods(node.methods, cl_type, node.generic_params);\n"
     "            return;\n"
     "        }\n"
     "        this.visit_methods(node.methods, cl_type);\n"),
]
rw(SE, se_pairs)

if SHADOW:
    DI = "compiler/src/compiler/decl_index.cryo"
    rw(DI, [(
        "    type_of_def(&this, d: DefId) -> TypeRef {\n",
        "    shadow_tdef(&this, site: string, d: DefId, key: SymbolStr) -> void {\n"
        "        const by_def: TypeRef = this.type_of_def(d);\n"
        "        const by_key: TypeRef = this.lookup_type(key);\n"
        "        if (by_def.is_valid() && by_key.is_valid() && by_def.id == by_key.id) {\n"
        "            fmt::eprintf(\"SHADOW\\tTDEF\\t%s\\tAGREE\\n\", site);\n"
        "        } else if (!d.is_valid() && by_key.is_valid()) {\n"
        "            fmt::eprintf(\"SHADOW\\tTDEF\\t%s\\tNOSTAMP\\t%u\\n\", site, key.id);\n"
        "        } else if (!by_def.is_valid() && !by_key.is_valid()) {\n"
        "            fmt::eprintf(\"SHADOW\\tTDEF\\t%s\\tBOTHINV\\t%u\\t%u\\n\", site, key.id, d.qualified_name().id);\n"
        "        } else if (by_def.is_valid() && !by_key.is_valid()) {\n"
        "            fmt::eprintf(\"SHADOW\\tTDEF\\t%s\\tKEYINV\\t%u\\t%u\\n\", site, key.id, d.qualified_name().id);\n"
        "        } else {\n"
        "            fmt::eprintf(\"SHADOW\\tTDEF\\t%s\\tDISAGREE\\t%u\\t%u\\n\", site, key.id, d.qualified_name().id);\n"
        "        }\n"
        "    }\n\n"
        "    type_of_def(&this, d: DefId) -> TypeRef {\n")])

for p, t in edits.items():
    io.open(p, "w", encoding="utf-8", newline="").write(t)
print("readers converted: %d files, shadow=%s" % (len(edits), SHADOW))
