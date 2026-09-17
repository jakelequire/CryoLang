"""The six type-declaration nodes carry the DefId their registration minted.

Part 1 of the change: the field, the registrar returning the id, every
writer stamping it.  Nothing is saved until every replacement has matched
exactly once (a script that asserts mid-way leaves half the files edited).
"""
import os as _os
_REPO = _os.path.abspath(_os.path.join(_os.path.dirname(__file__), "..", "..", ".."))
import io, sys, os
ROOT = _REPO
edits = {}

def rw(rel, pairs):
    p = os.path.join(ROOT, rel)
    t = io.open(p, encoding="utf-8", newline="").read()
    for old, new in pairs:
        n = t.count(old)
        assert n == 1, (rel, old[:80], n)
        t = t.replace(old, new)
    edits[p] = t

DOC = ("    /// The id this declaration registered under, stamped by the type-\n"
       "    /// declaration stage (a specialization's by its injector, a synthesized\n"
       "    /// declaration's by its synthesizer).  Invalid until then.  A reader\n"
       "    /// that holds the node asks the index by this id; it never re-derives\n"
       "    /// the key from the name and the file.\n"
       "    def:               DefId;\n")

DECL = "compiler/src/compiler/AST/declaration.cryo"
rw(DECL, [
    ("import compiler::resolver::res::{ OverloadId, Res, ResBase, ResSlot };\n",
     "import compiler::resolver::res::{ DefId, OverloadId, Res, ResBase, ResSlot };\n"),
    # StructDeclNode
    ("    where_bounds:      TraitBound[];\n\n    StructDeclNode(name: SymbolStr, span: SourceSpan)\n",
     "    where_bounds:      TraitBound[];\n" + DOC + "\n    StructDeclNode(name: SymbolStr, span: SourceSpan)\n"),
    ("        this.binding_namespace = SymbolStr::empty();\n        this.where_bounds      = [];\n    }\n\n    add_accessor(",
     "        this.binding_namespace = SymbolStr::empty();\n        this.where_bounds      = [];\n        this.def               = DefId::invalid();\n    }\n\n    add_accessor("),
    # UnionDeclNode
    ("    where_bounds:      TraitBound[];\n\n    UnionDeclNode(name: SymbolStr, span: SourceSpan)\n",
     "    where_bounds:      TraitBound[];\n" + DOC + "\n    UnionDeclNode(name: SymbolStr, span: SourceSpan)\n"),
    ("        : DeclarationNode(NodeKind::UnionDeclaration, span) {\n        this.name              = name;\n        this.generic_params    = [];\n        this.fields            = [];\n        this.methods           = [];\n        this.repr_c            = false;\n        this.repr_packed       = false;\n        this.repr_transparent  = false;\n        this.min_alignment     = 0;\n        this.is_public         = true;\n        this.binding_namespace = SymbolStr::empty();\n        this.where_bounds      = [];\n    }\n",
     "        : DeclarationNode(NodeKind::UnionDeclaration, span) {\n        this.name              = name;\n        this.generic_params    = [];\n        this.fields            = [];\n        this.methods           = [];\n        this.repr_c            = false;\n        this.repr_packed       = false;\n        this.repr_transparent  = false;\n        this.min_alignment     = 0;\n        this.is_public         = true;\n        this.binding_namespace = SymbolStr::empty();\n        this.where_bounds      = [];\n        this.def               = DefId::invalid();\n    }\n"),
    # ClassDeclNode
    ("    where_bounds:     TraitBound[];\n\n    ClassDeclNode(name: SymbolStr, span: SourceSpan)\n",
     "    where_bounds:     TraitBound[];\n" + DOC + "\n    ClassDeclNode(name: SymbolStr, span: SourceSpan)\n"),
    ("        this.is_public        = true;\n        this.where_bounds     = [];\n    }\n",
     "        this.is_public        = true;\n        this.where_bounds     = [];\n        this.def              = DefId::invalid();\n    }\n"),
    # TraitDeclNode
    ("    assoc_types:    AssocTypeDeclNode*[];\n\n    TraitDeclNode(name: SymbolStr, span: SourceSpan)\n",
     "    assoc_types:    AssocTypeDeclNode*[];\n" + DOC + "\n    TraitDeclNode(name: SymbolStr, span: SourceSpan)\n"),
    ("        this.base_traits    = [];\n        this.assoc_types    = [];\n    }\n",
     "        this.base_traits    = [];\n        this.assoc_types    = [];\n        this.def            = DefId::invalid();\n    }\n"),
    # TypeAliasDeclNode
    ("    binding_namespace: SymbolStr;\n\n    TypeAliasDeclNode(alias_name: SymbolStr, span: SourceSpan)\n",
     "    binding_namespace: SymbolStr;\n" + DOC + "\n    TypeAliasDeclNode(alias_name: SymbolStr, span: SourceSpan)\n"),
    ("        this.resolved_target   = TypeRef::invalid();\n        this.binding_namespace = SymbolStr::empty();\n    }\n",
     "        this.resolved_target   = TypeRef::invalid();\n        this.binding_namespace = SymbolStr::empty();\n        this.def               = DefId::invalid();\n    }\n"),
    # EnumDeclNode
    ("    where_bounds:      TraitBound[];\n\n    EnumDeclNode(name: SymbolStr, span: SourceSpan)\n",
     "    where_bounds:      TraitBound[];\n" + DOC + "\n    EnumDeclNode(name: SymbolStr, span: SourceSpan)\n"),
    ("        this.is_public               = true;\n        this.binding_namespace       = SymbolStr::empty();\n        this.where_bounds            = [];\n    }\n",
     "        this.is_public               = true;\n        this.binding_namespace       = SymbolStr::empty();\n        this.where_bounds            = [];\n        this.def                     = DefId::invalid();\n    }\n"),
])

DI = "compiler/src/compiler/decl_index.cryo"
rw(DI, [
    ("    register_type(mut &this, qualified_name: SymbolStr, ty: TypeRef, is_public: boolean) -> void {\n"
     "        this.type_map.insert(qualified_name.id, ty);\n"
     "\n"
     "        // Reverse map: TypeRef.id -> qualified name\n"
     "        this.type_reverse.insert(ty.id, qualified_name.id);\n"
     "        this.decl_visibility.insert(qualified_name.id, is_public);\n"
     "    }\n",
     "    ///\n"
     "    /// Returns the definition's id, which the caller stamps on the\n"
     "    /// declaration it registered: the registration is the one place that\n"
     "    /// knows the referent by construction, so the id is minted here and a\n"
     "    /// reader holding the node asks by it (`type_of_def`) rather than\n"
     "    /// re-deriving the key.\n"
     "    register_type(mut &this, qualified_name: SymbolStr, ty: TypeRef, is_public: boolean) -> DefId {\n"
     "        this.type_map.insert(qualified_name.id, ty);\n"
     "\n"
     "        // Reverse map: TypeRef.id -> qualified name\n"
     "        this.type_reverse.insert(ty.id, qualified_name.id);\n"
     "        this.decl_visibility.insert(qualified_name.id, is_public);\n"
     "        return DefId::of_definition(qualified_name);\n"
     "    }\n"),
])

PR = "compiler/src/compiler/passes/pass_registry.cryo"
rw(PR, [
    ("                    ctx.decl_index.register_type(qualified_sym, struct_ref, node.is_public);\n",
     "                    node.def = ctx.decl_index.register_type(qualified_sym, struct_ref, node.is_public);\n"),
    ("                    ctx.decl_index.register_type(qualified_sym, union_ref, node.is_public);\n",
     "                    node.def = ctx.decl_index.register_type(qualified_sym, union_ref, node.is_public);\n"),
    ("                    ctx.decl_index.register_type(qualified_sym, enum_ref, node.is_public);\n",
     "                    node.def = ctx.decl_index.register_type(qualified_sym, enum_ref, node.is_public);\n"),
    ("                    ctx.decl_index.register_type(qualified_sym, class_ref, node.is_public);\n",
     "                    node.def = ctx.decl_index.register_type(qualified_sym, class_ref, node.is_public);\n"),
    ("                    ctx.decl_index.register_type(qualified_sym, trait_ref, true);\n",
     "                    node.def = ctx.decl_index.register_type(qualified_sym, trait_ref, true);\n"),
    ("                    ctx.decl_index.register_type(qualified_alias, alias_ref, true);\n",
     "                    node.def = ctx.decl_index.register_type(qualified_alias, alias_ref, true);\n"),
])

SP = "compiler/src/compiler/passes/specialization.cryo"
rw(SP, [
    ("import compiler::resolver::{ intern_table, symbol_str };\n",
     "import compiler::resolver::{ intern_table, res, symbol_str };\n"
     "import compiler::resolver::res::{ DefId };\n"),
    ("        ctx.decl_index.register_type(arena_qname, spec_type, SpecInjector::node_is_public(node));\n",
     "        SpecInjector::stamp_def(node,\n"
     "            ctx.decl_index.register_type(arena_qname, spec_type, SpecInjector::node_is_public(node)));\n"),
    ("    /// The visibility a specialized type declaration carries: its template's,\n",
     "    /// Stamp the id a specialized type declaration registered under.  The\n"
     "    /// cloner leaves the field invalid (a clone has its own registration,\n"
     "    /// never its template's), so this is the clone's one writer.\n"
     "    static stamp_def(node: ASTNode*, d: DefId) -> void {\n"
     "        if (node.kind == NodeKind::StructDeclaration) { (node as StructDeclNode*).def = d; return; }\n"
     "        if (node.kind == NodeKind::UnionDeclaration)  { (node as UnionDeclNode*).def = d; return; }\n"
     "        if (node.kind == NodeKind::ClassDeclaration)  { (node as ClassDeclNode*).def = d; return; }\n"
     "        if (node.kind == NodeKind::EnumDeclaration)   { (node as EnumDeclNode*).def = d; return; }\n"
     "        if (node.kind == NodeKind::TraitDeclaration)  { (node as TraitDeclNode*).def = d; return; }\n"
     "        if (node.kind == NodeKind::TypeAliasDeclaration) { (node as TypeAliasDeclNode*).def = d; return; }\n"
     "    }\n"
     "\n"
     "    /// The visibility a specialized type declaration carries: its template's,\n"),
])

AL = "compiler/src/compiler/sema/async_lower.cryo"
rw(AL, [
    ("        this.ctx.decl_index.register_type(q_name, struct_ref, true);\n",
     "        struct_decl.def = this.ctx.decl_index.register_type(q_name, struct_ref, true);\n"),
])

LS = "compiler/src/compiler/sema/lambda_synth.cryo"
rw(LS, [
    ("        ctx_ptr.decl_index.register_type(qual_name, struct_ref, true);\n",
     "        struct_decl.def = ctx_ptr.decl_index.register_type(qual_name, struct_ref, true);\n"),
])

for p, t in edits.items():
    io.open(p, "w", encoding="utf-8", newline="").write(t)
print("stamped: %d files" % len(edits))
