# Every place that handles an INSTANCE method with no receiver parameter as a
# case of its own (section 8.314), marked with a line when it is entered:
#   SHADOW norecv <site> <file>:<line>
# Once every method carries a receiver parameter - written, or synthesized by
# the parser, the async lowering or the closure synthesis - none of these
# should be entered; this is the measurement that says so before they go.
#
# A MEASUREMENT: applied to the tree carrying the synthesis, never committed
# applied.  The name layer's own block is marked by the unit's shadow build.
#   python scripts/ns-migration/8.314/norecv-shadow.py
#   rm -rf compiler/build && make cryo
#   bash scripts/objcmp/corpus2.sh nr
#   git checkout <the four files>   (then re-apply the unit)
import os
R = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
C = os.path.join(R, "compiler", "src", "compiler")

def patch(rel, pairs):
    p = os.path.join(C, rel)
    s = open(p, newline="").read()
    for old, new in pairs:
        assert s.count(old) == 1, (rel, old[:70], s.count(old))
        s = s.replace(old, new)
    open(p, "w", newline="").write(s)

patch("codegen/ops/declaration_emitter.cryo", [
    ("        const prepend_implicit_this: boolean = is_instance && explicit_self_name.length() == 0;\n",
     "        const prepend_implicit_this: boolean = is_instance && explicit_self_name.length() == 0;\n"
     "        if (prepend_implicit_this) { fmt::printf(\"SHADOW norecv codegen-declare %s:%d\\n\", func.span.file, func.span.start_line as i32); }\n"),
    ("                // `&this` (explicit or implicit): LLVM param 0 is already a ptr.\n",
     "                // `&this` (explicit or implicit): LLVM param 0 is already a ptr.\n"
     "                if (explicit_ref_idx < 0) { fmt::printf(\"SHADOW norecv codegen-prologue %s:%d\\n\", func.span.file, func.span.start_line as i32); }\n"),
])
patch("decl_index.cryo", [
    ("            if (!method.is_static && !has_explicit_self) {\n",
     "            if (!method.is_static && !has_explicit_self) {\n"
     "                fmt::printf(\"SHADOW norecv decl-index %s:%d\\n\", func.span.file, func.span.start_line as i32);\n"),
    ("        // Non-static method with no explicit receiver: implicit `&this`.\n",
     "        // Non-static method with no explicit receiver: implicit `&this`.\n"
     "        fmt::printf(\"SHADOW norecv receiver-kind %s:%d\\n\", func.span.file, func.span.start_line as i32);\n"),
])
patch("sema/async_lower.cryo", [
    ("            if (recv == null) {\n                recv = new VarDeclNode(this.this_amp_sym, span);\n",
     "            if (recv == null) {\n                fmt::printf(\"SHADOW norecv async-lower %s:%d\\n\", node.span.file, node.span.start_line as i32);\n"
     "                recv = new VarDeclNode(this.this_amp_sym, span);\n"),
])
print("patched")
