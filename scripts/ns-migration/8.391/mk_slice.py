"""Write a slice as `file:line <TAB> function <TAB> parameter <TAB> string`,
the input `retype.py --as <Type> --params` reads:

  mangled - every `string` parameter that names a symbol in the backend's
            symbol table (`--as MangledName`)
  keyword - every `string` parameter matched against a table the language
            or its configuration format fixes (`--as Keyword`)

The declarations are found by (file, function, parameter) in the listing
`done.py --name-taking` prints, so the line numbers are the tree's own.

usage: python mk_slice.py <mangled|keyword> <name-taking listing> <repo-root> > slice.tsv
"""
import os, re, sys

KEYWORD = [
    # the lexer's keyword table and the numeric-literal suffix table
    ("compiler/src/compiler/lex/_module.cryo", "from_keyword", "s"),
    ("compiler/src/compiler/lex/lexer.cryo", "is_valid_numeric_suffix", "s"),
    ("compiler/src/compiler/sema/helpers.cryo", "sema_numeric_suffix_type", "suffix"),
    # the primitive numeric types' spellings
    ("compiler/src/compiler/sema/helpers.cryo", "is_numeric_type_name", "name"),
    # the syntax highlighter's keyword list
    ("compiler/src/utils/syntax_highlighter.cryo", "lex_eq", "kw"),
    # a project setting's fixed values
    ("compiler/src/compiler/project_config.cryo", "from_string", "s"),
]

MANGLED = [
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

which, listing, root = sys.argv[1], sys.argv[2], sys.argv[3]
SLICE = {"mangled": MANGLED, "keyword": KEYWORD}[which]
rows = {}
for l in open(listing, encoding="utf-8"):
    c = l.rstrip("\n").split("\t")
    if len(c) >= 2:
        rows.setdefault((c[0].rsplit(":", 1)[0], c[1]), []).append(c[0])
missing = 0
for f, fn, pn in SLICE:
    # a name declared more than once in a file (both config enums'
    # `from_string`) contributes every declaration
    locs = rows.get((f, fn), [])
    if not locs:
        sys.stderr.write("%s %s: not in the listing\n" % (f, fn))
        missing += 1
        continue
    src = open(os.path.join(root, f), encoding="utf-8").read().splitlines()
    for loc in locs:
        ln = int(loc.rsplit(":", 1)[1])
        head = " ".join(src[ln - 1:ln + 3])
        if not re.search(r"\b%s\s*:\s*string\b" % re.escape(pn), head):
            sys.stderr.write("%s %s: no `%s: string` at its head\n" % (loc, fn, pn))
            missing += 1
            continue
        print("%s\t%s\t%s\tstring" % (loc, fn, pn))
sys.exit(1 if missing else 0)
