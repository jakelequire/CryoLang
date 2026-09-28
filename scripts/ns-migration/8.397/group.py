"""Group the string-only name-taking functions by mechanism.

Usage: python group.py <sigs.tsv> <out.tsv>
Every row of kind `string` must be claimed by exactly one rule; an unclaimed
row is printed as UNCLAIMED and fails the run.
"""
import sys, collections

# (group, file substring, function names or "*")
RULES = [
    ("edge", "resolver/intern_table.cryo", ["intern", "contains", "lookup"]),
    ("edge", "compiler/compilation_context.cryo", ["intern"]),
    ("edge", "codegen/", ["intern_str"]),
    ("edge", "resolver/qualified_name.cryo", ["single", "parse", "child", "push"]),
    ("edge", "sema/async_lower.cryo", ["mint_bare", "mint_temp", "method_call0", "method_call1"]),
    ("edge", "mono/", ["qualify_spec_name"]),
    ("edge", "resolver/name_resolution.cryo", ["alias_keyword_primitive"]),
    ("edge", "passes/pass_registry.cryo", ["primitive_def"]),
    ("edge", "parser/expr_parser.cryo", ["fstr_call0", "fstr_call2", "lang_variant_pattern"]),
    ("literal-text", "parser/expr_parser.cryo", ["fstr_call_fmt"]),
    ("edge", "utils/keyword.cryo", ["new"]),
    ("edge", "utils/text.cryo", ["new"]),
    ("edge", "resolver/mangled_name.cryo", ["new"]),
    ("edge", "compiler/instance.cryo", ["mark_implicit_closure"]),
    ("edge", "bindgen/type_map.cryo", ["prim"]),
    ("edge", "bindgen/importer.cryo", ["is_bound_function"]),
    ("operator", "types/checker.cryo", ["check_binary_op", "check_unary_op"]),
    ("operator", "sema/sema.cryo", ["for_binary", "for_unary", "emit_operator_missing_impl", "mk", "mk_ord"]),
    ("operator", "compiler/const_table.cryo", ["apply_binary"]),
    ("fixed-table", "resolver/res.cryo", ["is_primitive_spelling", "primitive_of_alias", "of_spelling"]),
    ("fixed-table", "compiler/intrinsic_kind.cryo", ["from_name"]),
    ("fixed-table", "lex/lexer.cryo", ["is_float_suffix"]),
    ("fixed-table", "passes/config_gating.cryo", ["*"]),
    ("fixed-table", "passes/directive_processing.cryo", ["is_known_builtin", "is_cfg_os_atom", "has"]),
    ("fixed-table", "AST/declaration.cryo", ["find_directive", "has_directive"]),
    ("fixed-table", "compiler/const_table.cryo", ["wrap_to_primitive"]),
    ("fixed-table", "bindgen/generator.cryo", ["signed_backing", "backing_bits"]),
    ("fixed-table", "bindgen/importer.cryo", ["is_operator_spelling", "strip_float_suffix", "has_float_marker", "is_safe_header_name"]),
    ("fixed-table", "resolver/mangled_name.cryo", ["operator_kind_from_name"]),
    ("fixed-table", "resolver/demangler.cryo", ["op_code_to_symbol"]),
    ("fixed-table", "compiler/project_config.cryo", ["profile_base_opt_level", "profile_base_debug_info"]),
    ("cli", "CLI/", ["*"]),
    ("cli", "src/main.cryo", ["main"]),
    ("vestigial-owner", "sema/call_resolver.cryo", ["pin_method_callee_from_qname", "select_method", "pick_own_method", "bind_on_impl_block"]),
    ("type-display", "mono/", ["refuse_unsatisfied_type_bounds", "specialize", "method_has_modified_self_type",
                               "annotation_is_modified_outer", "annotation_matches_param_or_spec", "ok"]),
    ("module-name", "compiler/module_loader.cryo", ["resolve_import_path", "is_vendor_path", "note_vendor_import",
                                                     "suggest_module", "extract_module_name", "to_snake_case", "replace_separator"]),
    ("module-name", "compiler/compilation_context.cryo", ["modules_written_as"]),
    ("module-name", "resolver/name_resolution.cryo", ["walk_module_rooted_type", "offered_leaves",
                                                       "refuse_unoffered_import", "refuse_unknown_module_import", "path_precedes"]),
    ("module-name", "resolver/resolver.cryo", ["ns_written_as", "contains_separator"]),
    ("module-name", "AST/node.cryo", ["set_namespace_name"]),
    ("module-name", "AST/node_locator.cryo", ["find_program_by_namespace"]),
    ("module-name", "compiler/build_manifest.cryo", ["lookup", "write_module_keys", "module_source_hash", "path_module"]),
    ("module-name", "codegen/context.cryo", ["new"]),
    ("module-name", "codegen/passes.cryo", ["audit_layout", "ctx_audit_type", "ctx_audit_module"]),
    ("module-name", "compiler/module_graph.cryo", ["origin_subdir"]),
    ("qname-text", "resolver/qualified_name.cryo", ["head_of", "leaf_of", "segment_count", "parent_of", "shorten_paths"]),
    ("qname-text", "bindgen/generator.cryo", ["last_segment", "ident"]),
    ("qname-text", "parser/expr_parser.cryo", ["append_path_segments"]),
    ("qname-text", "compiler/instance.cryo", ["dup_survivor"]),
    ("mangle-text", "resolver/demangler.cryo", ["new", "finalize", "demangle"]),
    ("mangle-text", "resolver/mangled_name.cryo", ["mangle_with_path", "encode_type_ref", "encode_ident"]),
    ("file-path", "compiler/module_loader.cryo", ["*"]),
    ("file-path", "compiler/module_graph.cryo", ["module_of_file", "find_module_by_path", "paths_equal_ignore_case"]),
    ("file-path", "compiler/compilation_context.cryo", ["module_of_file", "new", "debug", "reset_for_next_module"]),
    ("file-path", "AST/node_locator.cryo", ["*"]),
    ("file-path", "AST/declaration.cryo", ["set_source_file", "add_include_path", "add_system_include_path"]),
    ("file-path", "types/resolver.cryo", ["new", "project_where_bound_params_into"]),
    ("file-path", "passes/directive_processing.cryo", ["file_is_under_tests_dir", "validate_test_module_placement"]),
    ("file-path", "compiler/build_manifest.cryo", ["is_up_to_date", "write_manifest"]),
    ("file-path", "compiler/instance.cryo", ["resolve_stdlib_root"]),
    ("file-path", "codegen/passes.cryo", ["run_tool", "write_rsp_file", "run_multiprocess_emit", "bundle_object_list"]),
    ("file-path", "bindgen/generator.cryo", ["provenance_key", "write_depfile", "dedup_in_place", "resolve_header", "generate", "contains"]),
    ("file-path", "bindgen/importer.cryo", ["collect_from_tu"]),
    ("deps-vendor", "compiler/deps/", ["*"]),
    ("deps-vendor", "compiler/vendor/", ["*"]),
    ("deps-vendor", "compiler/project_config.cryo", ["empty"]),
    ("display", "passes/directive_processing.cryo", ["*"]),
    ("display", "passes/ast_validation.cryo", ["*"]),
    ("display", "passes/type_resolution.cryo", ["emit_undefined_type"]),
    ("display", "codegen/type_map.cryo", ["report_unmappable_fields"]),
    ("display", "codegen/state/diag_sink.cryo", ["report_invalid_function_body"]),
    ("display", "sema/diagnostics.cryo", ["attach_type_coercion_suggestion"]),
    ("display", "diag/", ["*"]),
    ("display", "types/user_defined.cryo", ["set_display_name"]),
    ("display", "resolver/resolver.cryo", ["create_scope", "enter_scope"]),
    ("display", "resolver/scope.cryo", ["new"]),
    ("display", "bindgen/generator.cryo", ["render", "skip_const", "render_struct_const", "render_function_named"]),
    ("display", "bindgen/importer.cryo", ["emit_cxx_method"]),
    ("display", "bindgen/type_map.cryo", ["type_spelling_contains"]),
    ("display", "sema/call_facts.cryo", ["sort_key", "local_provenance"]),
    ("literal-text", "lex/_module.cryo", ["*"]),
    ("literal-text", "codegen/visit/pattern_emitter.cryo", ["lower_scalar_literal"]),
    ("literal-text", "codegen/state/string_cache.cryo", ["get_or_create"]),
    ("literal-text", "AST/pattern.cryo", ["ident"]),
    ("literal-text", "AST/statement.cryo", ["raw", "add_clobber"]),
]

rows = [l.rstrip("\n").split("\t") for l in open(sys.argv[1], encoding="utf-8")]
out = open(sys.argv[2], "w", encoding="utf-8", newline="\n")
c = collections.Counter(); bad = 0
for k, loc, name, sig in rows:
    if k != "string": continue
    g = None
    for grp, sub, names in RULES:
        if sub in loc and ("*" in names or name in names): g = grp; break
    if g is None: print("UNCLAIMED", loc, name); bad += 1; continue
    c[g] += 1
    out.write("%s\t%s\t%s\t%s\n" % (g, loc, name, sig))
for g, v in c.most_common(): print("%5d %s" % (v, g))
print("%5d total" % sum(c.values()))
sys.exit(1 if bad else 0)
