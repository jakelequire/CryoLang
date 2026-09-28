"""Write the link-symbol slice: every `string` parameter that names a symbol
in the backend's symbol table, as `file:line <TAB> function <TAB> parameter
<TAB> string` - the input `retype.py --as MangledName --params` reads.

The declarations are found by (file, function, parameter) in the listing
`done.py --name-taking` prints, so the line numbers are the tree's own.

usage: python mk_slice.py <name-taking listing> <repo-root> > slice.tsv
"""
import os, re, sys

SLICE = [
    # the backend's symbol table, read and written by symbol
    ("compiler/src/compiler/codegen/llvm_types.cryo", "get_named_function", "name"),
    ("compiler/src/compiler/codegen/llvm_types.cryo", "get_named_global", "name"),
    ("compiler/src/compiler/codegen/llvm_types.cryo", "add_function", "name"),
    ("compiler/src/compiler/codegen/llvm_types.cryo", "add_global", "name"),
    ("compiler/src/compiler/codegen/llvm_types.cryo", "named_struct", "name"),
    ("compiler/src/compiler/codegen/llvm_types.cryo", "struct_by_name", "name"),
    ("compiler/src/compiler/codegen/type_map.cryo", "forward_declare_struct", "name"),
    ("compiler/src/compiler/codegen/test_main_codegen.cryo", "add_extern_test_function", "mangled"),
    # a symbol's linkage and COMDAT
    ("compiler/src/compiler/codegen/ops/declaration_emitter.cryo", "apply_spec_linkage", "fn_name"),
    ("compiler/src/compiler/codegen/ops/declaration_emitter.cryo", "apply_directive_effects", "fn_name"),
    # the set of symbols stripped from a module
    ("compiler/src/compiler/codegen/passes.cryo", "is_stripped", "name"),
    ("compiler/src/compiler/codegen/state/diag_sink.cryo", "is_stripped", "name"),
    # LLVM intrinsics and runtime check handlers, declared by symbol
    ("compiler/src/compiler/codegen/ops/intrinsic_emitter.cryo", "get_or_decl_unary_intrinsic", "name"),
    ("compiler/src/compiler/codegen/ops/intrinsic_emitter.cryo", "get_or_decl_ctlz_cttz", "name"),
    ("compiler/src/compiler/codegen/ops/intrinsic_emitter.cryo", "get_or_decl_binary_intrinsic", "name"),
    ("compiler/src/compiler/codegen/ops/intrinsic_emitter.cryo", "get_or_decl_ternary_intrinsic", "name"),
    ("compiler/src/compiler/codegen/ops/intrinsic_emitter.cryo", "get_or_decl_void_noarg", "name"),
    ("compiler/src/compiler/codegen/ops/intrinsic_emitter.cryo", "emit_addr_intrinsic", "name"),
    ("compiler/src/compiler/codegen/ops/intrinsic_emitter.cryo", "get_or_decl_fpclass", "name"),
    ("compiler/src/compiler/codegen/ops/intrinsic_emitter.cryo", "emit_unary_llvm", "iname"),
    ("compiler/src/compiler/codegen/ops/intrinsic_emitter.cryo", "emit_clz_or_ctz", "iname"),
    ("compiler/src/compiler/codegen/ops/intrinsic_emitter.cryo", "emit_rotate", "iname"),
    ("compiler/src/compiler/codegen/ops/intrinsic_emitter.cryo", "emit_binary_llvm", "iname"),
    ("compiler/src/compiler/codegen/ops/intrinsic_emitter.cryo", "emit_ternary_llvm", "iname"),
    ("compiler/src/compiler/codegen/ops/intrinsic_emitter.cryo", "get_or_decl_check_handler", "name"),
    ("compiler/src/compiler/codegen/ops/intrinsic_emitter.cryo", "emit_check_handler_call", "name"),
    ("compiler/src/compiler/codegen/ops/intrinsic_emitter.cryo", "emit_check_sink", "name"),
]

listing, root = sys.argv[1], sys.argv[2]
rows = {}
for l in open(listing, encoding="utf-8"):
    c = l.rstrip("\n").split("\t")
    if len(c) >= 2:
        rows.setdefault((c[0].rsplit(":", 1)[0], c[1]), []).append(c[0])
missing = 0
for f, fn, pn in SLICE:
    locs = rows.get((f, fn), [])
    if len(locs) != 1:
        sys.stderr.write("%s %s: %d rows in the listing\n" % (f, fn, len(locs)))
        missing += 1
        continue
    ln = int(locs[0].rsplit(":", 1)[1])
    src = open(os.path.join(root, f), encoding="utf-8").read().splitlines()
    head = " ".join(src[ln - 1:ln + 3])
    if not re.search(r"\b%s\s*:\s*string\b" % re.escape(pn), head):
        sys.stderr.write("%s %s: no `%s: string` at its head\n" % (f, fn, pn))
        missing += 1
        continue
    print("%s\t%s\t%s\tstring" % (locs[0], fn, pn))
sys.exit(1 if missing else 0)
