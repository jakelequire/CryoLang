# Does anything on a GENERATED `implement` block tell it from a WRITTEN one?
#
# Every `ImplBlockNode` is tagged, for this measurement only, with where it
# was created - the parser (1), the AST cloner (2, which is how the
# monomorphizer makes the blocks it places for an instantiation), the async
# lowering (3), the C++ binding importer (4).  Just before code generation,
# every block in the module's top-level statement list prints the fields a
# consumer could tell the two apart by:
#   SHADOW implmark o=<origin> res=<head: 0 unstamped, 1 a definition, 2 other> spec=<spec_owner set>
#          tspan=<target_type_span set> gen=<generic params> trait=<trait_def set>
#          span=<node span set> <file>
# The origin tag is the independent truth; each field is a candidate mark,
# and it is a mark only if it splits the population exactly as the tag does.
#
# A MEASUREMENT: never committed applied.
#   python scripts/ns-migration/8.341/impl-origin-probe.py
#   rm -rf compiler/build && make cryo
#   bash scripts/objcmp/corpus2.sh impl
#   git checkout -- compiler/src   (then re-apply whatever unit is in the tree)
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

patch("AST/declaration.cryo", [
    ("    assoc_binding_annotations: TypeAnnotation*[];\n",
     "    assoc_binding_annotations: TypeAnnotation*[];\n    probe_origin: i32;\n"),
    ("        this.assoc_binding_annotations = [];\n    }\n",
     "        this.assoc_binding_annotations = [];\n        this.probe_origin = 0;\n    }\n"),
])
patch("parser/parser.cryo", [
    ("        mut node: ImplBlockNode* = new ImplBlockNode(type_name, this.span_from_token(start));\n",
     "        mut node: ImplBlockNode* = new ImplBlockNode(type_name, this.span_from_token(start));\n"
     "        node.probe_origin = 1;\n"),
])
patch("AST/cloner.cryo", [
    ("        mut c: ImplBlockNode* = new ImplBlockNode(node.target_type, node.span);\n",
     "        mut c: ImplBlockNode* = new ImplBlockNode(node.target_type, node.span);\n"
     "        c.probe_origin = 2;\n"),
])
patch("sema/async_lower.cryo", [
    ("        mut impl_node: ImplBlockNode* = new ImplBlockNode(q_name, span);\n",
     "        mut impl_node: ImplBlockNode* = new ImplBlockNode(q_name, span);\n"
     "        impl_node.probe_origin = 3;\n"),
])
patch("bindgen/importer.cryo", [
    ("        mut impl: ImplBlockNode* = new ImplBlockNode(owner_sym, SourceSpan::none());\n",
     "        mut impl: ImplBlockNode* = new ImplBlockNode(owner_sym, SourceSpan::none());\n"
     "        impl.probe_origin = 4;\n"),
])
patch("passes/pass_registry.cryo", [
    ("    ClassDeclNode, EnumDeclNode, ImportDeclNode, StructDeclNode, TraitDeclNode,\n",
     "    ClassDeclNode, EnumDeclNode, ImplBlockNode, ImportDeclNode, StructDeclNode, TraitDeclNode,\n"),
    ("            PassID::IRGeneration => {\n                CodegenPasses::run_ir_generation(ctx)\n",
     "            PassID::IRGeneration => {\n                PassRegistry::probe_impl_marks(ctx);\n"
     "                CodegenPasses::run_ir_generation(ctx)\n"),
    ("    static run_pass(id: PassID, ctx: CompilationContext*) -> PassResult {\n",
     "    static probe_impl_marks(ctx: CompilationContext*) -> void {\n"
     "        const root: ProgramNode* = ctx.artifacts.ast.root;\n"
     "        if (root == null) { return; }\n"
     "        for (mut i: i64 = 0; i < root.statements.length; i++) {\n"
     "            const st: ASTNode* = root.statements[i] as ASTNode*;\n"
     "            if (st == null || st.kind != NodeKind::ImplementationBlock) { continue; }\n"
     "            const ib: ImplBlockNode* = st as ImplBlockNode*;\n"
     # res: 0 unanswered, 1 a definition, 2 answered with something else
     # (a primitive's `PrimTy`, a refusal)
     "            mut r: i32 = 0;\n"
     "            if (!ib.res.is_pending()) { r = if (ib.target_def().is_valid()) { 1 } else { 2 }; }\n"
     "            const sp: i32 = if (ib.spec_owner.is_valid()) { 1 } else { 0 };\n"
     "            const ts: i32 = if (ib.target_type_span.file.length() > 0) { 1 } else { 0 };\n"
     "            const g: i32 = if (ib.generic_params.length > 0) { 1 } else { 0 };\n"
     "            const t: i32 = if (ib.trait_def.is_valid()) { 1 } else { 0 };\n"
     "            const s: i32 = if (ib.span.file.length() > 0) { 1 } else { 0 };\n"
     "            fmt::printf(\"SHADOW implmark o=%d res=%d spec=%d tspan=%d gen=%d trait=%d span=%d %s\\n\",\n"
     "                ib.probe_origin, r, sp, ts, g, t, s, ib.span.file);\n"
     "        }\n"
     "    }\n\n"
     "    static run_pass(id: PassID, ctx: CompilationContext*) -> PassResult {\n"),
])
print("patched")
