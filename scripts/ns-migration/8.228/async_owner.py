"""Sema's async declare pass takes the owner TYPE and the registration KEY
import os as _os
_REPO = _os.path.abspath(_os.path.join(_os.path.dirname(__file__), "..", "..", ".."))
from what the caller holds (the stamp), not a re-derived key.  --shadow
prints, at the three declaration callers, the guessed key beside the stamp's
and the guessed type beside the stamp's."""
import io, sys
SHADOW = "--shadow" in sys.argv
P = _REPO + "/compiler/src/compiler/sema/sema.cryo"
t = io.open(P, encoding="utf-8", newline="").read()
def rep(old, new, n=1):
    global t
    c = t.count(old); assert c == n, (old[:80], c, n); t = t.replace(old, new)

def sh(site, node, ns):
    if not SHADOW: return ""
    return ("                    this.shadow_async_owner(\"%s\", this.ctx.decl_type_key(%s.name, %s, %s.span.file), %s.def);\n"
            % (site, node, ns, node, node))

rep("                // The owner's key is the declaration's own: a C import's\n"
    "                // alias-qualified name, else its registered name when it was\n"
    "                // minted qualified, else the module of the file that WROTE\n"
    "                // it - for an injected specialization the template's file,\n"
    "                // so the key is the arena's and not the module the clone was\n"
    "                // placed in.\n"
    "                NodeKind::StructDeclaration => {\n"
    "                    const s: StructDeclNode* = stmt as StructDeclNode*;\n"
    "                    this.declare_async_methods(s.methods, s.generic_params,\n"
    "                        this.ctx.decl_type_key(s.name, s.binding_namespace, s.span.file), null);\n"
    "                }\n"
    "                NodeKind::UnionDeclaration => {\n"
    "                    const u: UnionDeclNode* = stmt as UnionDeclNode*;\n"
    "                    this.declare_async_methods(u.methods, u.generic_params,\n"
    "                        this.ctx.decl_type_key(u.name, u.binding_namespace, u.span.file), null);\n"
    "                }\n"
    "                NodeKind::ClassDeclaration => {\n"
    "                    const c: ClassDeclNode* = stmt as ClassDeclNode*;\n"
    "                    this.declare_async_methods(c.methods, c.generic_params,\n"
    "                        this.ctx.decl_type_key(c.name, SymbolStr::empty(), c.span.file), null);\n"
    "                }\n",
    "                // A declaration's owner is its own registered type and the\n"
    "                // key it registered under, both off the stamp.\n"
    "                NodeKind::StructDeclaration => {\n"
    "                    const s: StructDeclNode* = stmt as StructDeclNode*;\n"
    + sh("as_struct", "s", "s.binding_namespace") +
    "                    this.declare_async_methods(s.methods, s.generic_params,\n"
    "                        this.ctx.decl_index.type_of_def(s.def), s.def.qualified_name(), null);\n"
    "                }\n"
    "                NodeKind::UnionDeclaration => {\n"
    "                    const u: UnionDeclNode* = stmt as UnionDeclNode*;\n"
    + sh("as_union", "u", "u.binding_namespace") +
    "                    this.declare_async_methods(u.methods, u.generic_params,\n"
    "                        this.ctx.decl_index.type_of_def(u.def), u.def.qualified_name(), null);\n"
    "                }\n"
    "                NodeKind::ClassDeclaration => {\n"
    "                    const c: ClassDeclNode* = stmt as ClassDeclNode*;\n"
    + sh("as_class", "c", "SymbolStr::empty()") +
    "                    this.declare_async_methods(c.methods, c.generic_params,\n"
    "                        this.ctx.decl_index.type_of_def(c.def), c.def.qualified_name(), null);\n"
    "                }\n")
rep("                    // The head's key is the one owner the methods were\n"
    "                    // registered under.\n"
    "                    const canonical: SymbolStr = ib.target_key(this.arena);\n"
    "                    mut owner_params: GenericParamNode*[] = this.impl_owner_params(ib);\n"
    "                    this.declare_async_methods(ib.methods, owner_params,\n"
    "                                               canonical, ib);\n",
    "                    // The head's owner, and its key - the one owner the\n"
    "                    // methods were registered under.\n"
    "                    const canonical: SymbolStr = ib.target_key(this.arena);\n"
    "                    mut owner_params: GenericParamNode*[] = this.impl_owner_params(ib);\n"
    "                    this.declare_async_methods(ib.methods, owner_params,\n"
    "                        this.ctx.decl_index.impl_owner(ib, \"sema/async declare\"), canonical, ib);\n")
rep("    /// `owner_params` are the OWNER's generic parameters, which the future must\n"
    "    /// carry as well as the method's own; `qname` is the key the declaration\n"
    "    /// index registered the methods' signatures under.\n",
    "    /// `owner_params` are the OWNER's generic parameters, which the future must\n"
    "    /// carry as well as the method's own; `owner_ty` is the owner's type and\n"
    "    /// `qname` the key the declaration index registered the methods'\n"
    "    /// signatures under - both handed over by a caller that holds them,\n"
    "    /// never re-derived here.\n")
rep("                          qname: SymbolStr, impl_node: ImplBlockNode*) -> void {\n"
    "        if (!qname.is_valid()) { this.reject_undeclarable_async(methods, qname); return; }\n"
    "        const owner_ty: TypeRef = this.types.lookup_type_exact(qname);\n"
    "        if (!owner_ty.is_valid()) { this.reject_undeclarable_async(methods, qname); return; }\n",
    "                          owner_ty: TypeRef, qname: SymbolStr,\n"
    "                          impl_node: ImplBlockNode*) -> void {\n"
    "        if (!owner_ty.is_valid() || !qname.is_valid()) { this.reject_undeclarable_async(methods, qname); return; }\n")
if SHADOW:
    rep("    reject_undeclarable_async(mut &this, methods: &MethodNode*[], qname: SymbolStr) -> void {\n",
        "    shadow_async_owner(mut &this, site: string, guess: SymbolStr, d: DefId) -> void {\n"
        "        const by_guess: TypeRef = this.types.lookup_type_exact(guess);\n"
        "        const by_def: TypeRef = this.ctx.decl_index.type_of_def(d);\n"
        "        const stamp: SymbolStr = d.qualified_name();\n"
        "        mut v: string = \"DISAGREE\";\n"
        "        if (guess.id == stamp.id && by_guess.is_valid() && by_guess.id == by_def.id) { v = \"AGREE\"; }\n"
        "        else if (guess.id != stamp.id && !by_guess.is_valid() && by_def.is_valid()) { v = \"KEYINV\"; }\n"
        "        else if (!d.is_valid()) { v = \"NOSTAMP\"; }\n"
        "        fmt::eprintf(\"SHADOW\\tASYNC\\t%s\\t%s\\t%s\\t%s\\n\", site, v, this.intern.resolve(guess), this.intern.resolve(stamp));\n"
        "    }\n\n"
        "    reject_undeclarable_async(mut &this, methods: &MethodNode*[], qname: SymbolStr) -> void {\n")
io.open(P, "w", encoding="utf-8", newline="").write(t)
print("async owner: sema.cryo edited, shadow=%s" % SHADOW)
