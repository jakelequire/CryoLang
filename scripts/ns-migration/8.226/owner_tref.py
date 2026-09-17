"""Codegen's method owner is a TypeRef, from one `DeclarationIndex::impl_owner`.

--shadow keeps, at each of the five owner sources, the old name-derived
TypeRef (with the `()` hack) beside the new one and prints a verdict.
Nothing is saved until every replacement has matched.
"""
import os as _os
_REPO = _os.path.abspath(_os.path.join(_os.path.dirname(__file__), "..", "..", ".."))
import io, os, sys
ROOT = _REPO + "/compiler/src/compiler/"
SHADOW = "--shadow" in sys.argv
edits = {}

def load(rel):
    return io.open(ROOT + rel, encoding="utf-8", newline="").read()

def rep(t, old, new, n=1, rel=""):
    c = t.count(old)
    assert c == n, (rel, old[:100], c, n)
    return t.replace(old, new)

# ---------------------------------------------------------------- decl_index
DI = "decl_index.cryo"
t = load(DI)
t = rep(t, "import compiler::ast::declaration::{ FunctionDeclNode, MethodNode, VarDeclNode };\n",
           "import compiler::ast::declaration::{ FunctionDeclNode, ImplBlockNode, MethodNode, VarDeclNode };\n", rel=DI)
t = rep(t, "import compiler::resolver::res::{ DefId, OverloadId };\n",
           "import compiler::resolver::res::{ DefId, OverloadId, Res, ResSlot };\n", rel=DI)
IMPL_OWNER = (
"    /// The type an impl head is for, off the head's stamp - the one reader\n"
"    /// of that stamp for every pass that walks an impl block.\n"
"    ///\n"
"    /// One answering path per kind of answer the slot can carry, each an\n"
"    /// exact lookup under the key that kind supplies, never a retry of one\n"
"    /// key under another.  A `Def` is canonical, so the index holds it or the\n"
"    /// declaration never registered - recorded against `site`.  A written\n"
"    /// primitive is `PrimTy` and names no declaration, because a primitive\n"
"    /// owns members without being declared anywhere; its spelling IS its\n"
"    /// registration key.  An unanswered slot belongs to the cloner, which\n"
"    /// leaves a specialization alone because a `Res` names a definition and\n"
"    /// a specialization is not one - there the monomorphizer's `spec_owner`,\n"
"    /// the instantiation's own arena id, is the carrier.  Every head a\n"
"    /// synthesizer builds is stamped by that synthesizer.\n"
"    ///\n"
"    /// Nothing else can name a type here: a type parameter, a local and an\n"
"    /// already-failed resolution each name something that is not a\n"
"    /// declaration, so there is no key to look one up under.\n"
"    impl_owner(&this, node: ImplBlockNode*, site: string) -> TypeRef {\n"
"        return match (node.res) {\n"
"            ResSlot::Answered(r) => {\n"
"                match (r) {\n"
"                    Res::Def(q) => {\n"
"                        const found: TypeRef = this.type_of_def(q);\n"
"                        if (!found.is_valid()) { compiler::resolver::res::record_unregistered_def(site); }\n"
"                        found\n"
"                    }\n"
"                    Res::PrimTy(n) => { this.lookup_type(n) }\n"
"                    _              => { TypeRef::invalid() }\n"
"                }\n"
"            }\n"
"            _ => { node.spec_owner }\n"
"        };\n"
"    }\n\n")
SHADOW_OWNER = (
"    shadow_owner(&this, site: string, name: SymbolStr, owner: TypeRef, arena: TypeArena*) -> void {\n"
"        mut by_name: TypeRef = this.lookup_type(name);\n"
"        if (!by_name.is_valid()) {\n"
"            const s: string = arena.intern_table.resolve(name);\n"
"            const l: u64 = s.length();\n"
"            if (l >= 2 && s[l - 2] == 0x28 && s[l - 1] == 0x29) { by_name = arena.get_unit(); }\n"
"        }\n"
"        const v: string = if (by_name.is_valid() && owner.is_valid() && by_name.id == owner.id) { \"AGREE\" }\n"
"            else if (!by_name.is_valid() && !owner.is_valid()) { \"BOTHINV\" }\n"
"            else if (!by_name.is_valid()) { \"NAMEINV\" }\n"
"            else if (!owner.is_valid()) { \"OWNERINV\" }\n"
"            else { \"DISAGREE\" };\n"
"        fmt::eprintf(\"SHADOW\\tOWNER\\t%s\\t%s\\t%u\\t%u\\t%s\\n\", site, v, by_name.id, owner.id, arena.intern_table.resolve(name));\n"
"    }\n\n")
t = rep(t, "    /// The type of the definition `d`, or invalid when `d` names none or the\n",
           IMPL_OWNER + (SHADOW_OWNER if SHADOW else "") +
           "    /// The type of the definition `d`, or invalid when `d` names none or the\n", rel=DI)
edits[DI] = t

# ---------------------------------------------------------- type_resolution
TR = "passes/type_resolution.cryo"
t = load(TR)
old = t[t.index("    /// The type an impl head is for, read off the stamp as sema's\n"):]
end = old.index("    }\n\n") + len("    }\n\n")
block = old[:end]
assert "impl_owner(&this, node: ImplBlockNode*) -> TypeRef {" in block and block.count("\n") < 25, block
t = t.replace(block, "", 1)
t = rep(t, "runner.impl_owner(node), node.target_key(runner.arena));",
           "ctx.decl_index.impl_owner(node, \"type_resolution/impl methods\"), node.target_key(runner.arena));", n=2, rel=TR)
t = rep(t, "                    const sync_runner: TypeResolutionRunner = TypeResolutionRunner::new(ctx);\n"
           "                    const tgt: TypeRef = sync_runner.impl_owner(node);\n",
           "                    const tgt: TypeRef = ctx.decl_index.impl_owner(node, \"type_resolution/struct field sync\");\n", rel=TR)
edits[TR] = t

# --------------------------------------------------------------------- sema
SE = "sema/sema.cryo"
t = load(SE)
start = t.index("    /// The type an impl head is for.\n")
endm = t.index("    override visit(mut &this, node: ImplBlockNode*) -> void {\n")
block = t[start:endm]
assert "impl_target_type(mut &this, node: ImplBlockNode*) -> TypeRef {" in block and block.count("\n") < 45, block.count("\n")
t = t[:start] + t[endm:]
t = rep(t, "this.impl_target_type(node)", "this.ctx.decl_index.impl_owner(node, \"sema/visit:ImplBlockNode\")", n=3, rel=SE)
edits[SE] = t

# ------------------------------------------------------------ visitor_state
VS = "codegen/state/visitor_state.cryo"
t = load(VS)
t = rep(t, "import compiler::resolver::symbol_str::{ SymbolStr };\n",
           "import compiler::resolver::symbol_str::{ SymbolStr };\nimport compiler::types::type_ref;\nimport compiler::types::type_ref::{ TypeRef };\n", rel=VS)
t = rep(t, "    /// Current impl block target type, set during impl-block traversal\n"
           "    /// so method bodies can resolve `Self` and emit correct mangles.\n"
           "    current_impl_target: SymbolStr;\n",
           "    /// The type whose methods are being emitted - an impl block's owner or\n"
           "    /// a declaration's own type - while its bodies are walked; invalid\n"
           "    /// outside one.  A type, not a name: `implement i32` and `for ()` have\n"
           "    /// an owner and no declaration.\n"
           "    current_impl_target: TypeRef;\n", rel=VS)
t = rep(t, "            current_impl_target: SymbolStr::empty(),\n",
           "            current_impl_target: TypeRef::invalid(),\n", rel=VS)
edits[VS] = t

# -------------------------------------------------------- decl_visit_emitter
DV = "codegen/visit/decl_visit_emitter.cryo"
t = load(DV)
def walk(ns, site):
    old = "        this.state.current_impl_target = this.cg.ctx.decl_type_key(node.name, %s, node.span.file);\n" % ns
    new = "        this.state.current_impl_target = this.cg.ctx.decl_index.type_of_def(node.def);\n"
    if SHADOW:
        new += "        this.cg.ctx.decl_index.shadow_owner(\"%s\", this.cg.ctx.decl_type_key(node.name, %s, node.span.file), this.state.current_impl_target, this.cg.ctx.type_arena);\n" % (site, ns)
    return old, new
o, n = walk("node.binding_namespace", "dv_struct")
assert t.count(o) == 2
t = t.replace(o, n, 1)
o2, n2 = walk("node.binding_namespace", "dv_union")
t = t.replace(o2, n2, 1)
o, n = walk("SymbolStr::empty()", "dv_class")
t = rep(t, o, n, rel=DV)
t = rep(t, "        this.state.current_impl_target = SymbolStr::empty();\n",
           "        this.state.current_impl_target = TypeRef::invalid();\n", n=4, rel=DV)
t = rep(t, "        this.state.current_impl_target = qualified_impl_target;\n",
           "        this.state.current_impl_target = this.cg.ctx.decl_index.impl_owner(node, \"codegen/impl bodies\");\n"
           + ("        this.cg.ctx.decl_index.shadow_owner(\"dv_impl\", qualified_impl_target, this.state.current_impl_target, this.cg.ctx.type_arena);\n" if SHADOW else ""), rel=DV)
t = rep(t, "            const target_str: string = this.cg.resolve(this.state.current_impl_target);\n",
           "            const target_str: string = if (this.state.current_impl_target.is_valid()) {\n"
           "                this.cg.ctx.type_arena.resolve_display_name(this.state.current_impl_target.id)\n"
           "            } else { \"\" };\n", rel=DV)
edits[DV] = t

# -------------------------------------------------------- declaration_emitter
DE = "codegen/ops/declaration_emitter.cryo"
t = load(DE)
# declare_impl_block: owner beside the template check's key
t = rep(t, "        for (mut i: i64 = 0; i < node.methods.length; i++) {\n"
           "            this.declare_method(node.methods[i], qualified_target);\n"
           "        }\n",
           "        const owner: TypeRef = this.ctx.decl_index.impl_owner(node, \"codegen/declare impl\");\n"
           + ("        this.ctx.decl_index.shadow_owner(\"de_impl\", qualified_target, owner, this.get_arena());\n" if SHADOW else "")
           + "        for (mut i: i64 = 0; i < node.methods.length; i++) {\n"
           "            this.declare_method(node.methods[i], owner);\n"
           "        }\n", rel=DE)
# struct / union: the key derivation goes, the owner is the stamp's
old_su = ("        const c: CompilationContext* = this.ctx;\n"
          "        const qualified: SymbolStr = c.decl_type_key(node.name, node.binding_namespace, node.span.file);\n"
          "        for (mut i: i64 = 0; i < node.methods.length; i++) {\n"
          "            this.declare_method(node.methods[i], qualified);\n"
          "        }\n")
def new_su(site):
    return ("        const owner: TypeRef = this.ctx.decl_index.type_of_def(node.def);\n"
            + ("        this.ctx.decl_index.shadow_owner(\"%s\", this.ctx.decl_type_key(node.name, node.binding_namespace, node.span.file), owner, this.get_arena());\n" % site if SHADOW else "")
            + "        for (mut i: i64 = 0; i < node.methods.length; i++) {\n"
            "            this.declare_method(node.methods[i], owner);\n"
            "        }\n")
assert t.count(old_su) == 2, t.count(old_su)
t = t.replace(old_su, new_su("de_struct"), 1)
t = t.replace(old_su, new_su("de_union"), 1)
# class: di_ref already IS the owner
t = rep(t, "        const c: CompilationContext* = this.ctx;\n"
           "        const qualified: SymbolStr = c.decl_type_key(node.name, SymbolStr::empty(), node.span.file);\n"
           "        const di: DeclarationIndex* = this.get_decl_index();\n"
           "        const di_ref: TypeRef = di.type_of_def(node.def);\n"
           "        for (mut i: i64 = 0; i < node.methods.length; i++) {\n"
           "            this.declare_method(node.methods[i], qualified);\n"
           "        }\n",
           "        const di_ref: TypeRef = this.ctx.decl_index.type_of_def(node.def);\n"
           + ("        this.ctx.decl_index.shadow_owner(\"de_class\", this.ctx.decl_type_key(node.name, SymbolStr::empty(), node.span.file), di_ref, this.get_arena());\n" if SHADOW else "")
           + "        for (mut i: i64 = 0; i < node.methods.length; i++) {\n"
           "            this.declare_method(node.methods[i], di_ref);\n"
           "        }\n", rel=DE)
# declare_method itself
t = rep(t, "    declare_method(mut &this, node: MethodNode*, target_type: SymbolStr) -> void {\n",
           "    /// `owner` is the type the method is on: an impl head's owner or a\n"
           "    /// declaration's own type, never re-derived from a name here.\n"
           "    declare_method(mut &this, node: MethodNode*, owner: TypeRef) -> void {\n", rel=DE)
t = rep(t, "        if (is_instance) {\n"
           "            mut target_ref: TypeRef = di.lookup_type(target_type);\n"
           "            if (!target_ref.is_valid()) {\n"
           "                // The unit type `()` isn't in the DeclarationIndex (it's a\n"
           "                // primitive owned by the arena, never registered through the\n"
           "                // decl-collection pass).  Fall back to the arena's canonical\n"
           "                // unit TypeRef so `implement trait Drop for ()` and similar\n"
           "                // get a real LLVM declaration.  Detected by the literal `()`\n"
           "                // suffix on the qualified target name.\n"
           "                const target_str: string = intern.resolve(target_type);\n"
           "                const tlen: u64 = target_str.length();\n"
           "                if (tlen >= 2 && target_str[tlen - 2] == 0x28 /* '(' */\n"
           "                              && target_str[tlen - 1] == 0x29 /* ')' */) {\n"
           "                    target_ref = arena.get_unit();\n"
           "                }\n"
           "            }\n"
           "            if (target_ref.is_valid()) {\n"
           "                target_mapped = this.type_mapper.map_type(target_ref);\n",
           "        if (is_instance) {\n"
           "            if (owner.is_valid()) {\n"
           "                target_mapped = this.type_mapper.map_type(owner);\n", rel=DE)
t = rep(t, "                mut recv_tref_v: TypeRef = di.lookup_type(target_type);\n"
           "                if (!recv_tref_v.is_valid()) { recv_tref_v = param.resolved_type; }\n",
           "                mut recv_tref_v: TypeRef = owner;\n"
           "                if (!recv_tref_v.is_valid()) { recv_tref_v = param.resolved_type; }\n", rel=DE)
# the prologue
t = rep(t, "    codegen_method_prologue(mut &this, node: MethodNode*, target_type: SymbolStr) -> LValue {\n"
           "        const func: FunctionDeclNode* = node.func;\n"
           "        if (func == null) { return LValue::null(); }\n"
           "\n"
           "        const intern: InternTable* = this.get_intern();\n"
           "        const type_str: string = intern.resolve(target_type);\n",
           "    /// `owner` as in `declare_method`; its display name serves the diagnostics.\n"
           "    codegen_method_prologue(mut &this, node: MethodNode*, owner: TypeRef) -> LValue {\n"
           "        const func: FunctionDeclNode* = node.func;\n"
           "        if (func == null) { return LValue::null(); }\n"
           "\n"
           "        const intern: InternTable* = this.get_intern();\n"
           "        const type_str: string = if (owner.is_valid()) {\n"
           "            this.get_arena().resolve_display_name(owner.id)\n"
           "        } else { \"\" };\n", rel=DE)
t = rep(t, "            // A method node's `target_type` is already the canonical owner\n"
           "            // name, fully qualified by the pass that stamped it.  Prepending a\n"
           "            // namespace to it can only name a type no module declares, so a\n"
           "            // qualified lane in front of this lookup cannot answer and only the\n"
           "            // name as stamped ever binds.\n"
           "            mut target_ref: TypeRef = this.ctx.decl_index.lookup_type(target_type);\n"
           "            if (target_ref.is_invalid()) {\n"
           "                // Unit-target impls (`implement trait Drop for ()`):\n"
           "                // unit type lives in the arena, not the DeclarationIndex.\n"
           "                // Detected by the literal `()` suffix on the qualified target\n"
           "                // name; mirrors the fallback in declare_method.\n"
           "                const target_str: string = this.resolve(target_type);\n"
           "                const tlen: u64 = target_str.length();\n"
           "                if (tlen >= 2 && target_str[tlen - 2] == 0x28 /* '(' */\n"
           "                              && target_str[tlen - 1] == 0x29 /* ')' */) {\n"
           "                    target_ref = this.get_arena().get_unit();\n"
           "                }\n"
           "            }\n"
           "            if (target_ref.is_invalid()) {\n"
           "                const ts: string = this.resolve(target_type);\n"
           "                const ms: string = this.resolve(func.name);\n"
           "                this.diag.emit_error_at_span(ErrorCode::E0900_INTERNAL_COMPILER_ERROR,\n"
           "                    fmt::format(\"codegen: method '%s::%s': target type '%s' not found in declaration index\", ts, ms, ts),\n",
           "            const target_ref: TypeRef = owner;\n"
           "            if (target_ref.is_invalid()) {\n"
           "                const ms: string = this.resolve(func.name);\n"
           "                this.diag.emit_error_at_span(ErrorCode::E0900_INTERNAL_COMPILER_ERROR,\n"
           "                    fmt::format(\"codegen: method '%s::%s': the owner has no type\", type_str, ms),\n", rel=DE)
edits[DE] = t

for rel, txt in edits.items():
    io.open(ROOT + rel, "w", encoding="utf-8", newline="").write(txt)
print("owner-as-TypeRef: %d files, shadow=%s" % (len(edits), SHADOW))
