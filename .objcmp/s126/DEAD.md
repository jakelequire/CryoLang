# Compiler dead-code sweep

Every function under `compiler/src` that nothing live reaches, found from
the compiler's own `--emit=facts` call records (`dead.py`), deleted by
`delete.py`; `iter.sh` runs one round (regenerate, delete, type-check).
Live = reachable from `main`, from every function outside `compiler/src`
(the stdlib and the LSP, whose calls into the compiler are in `lsp.facts`),
from destructors, from a method in an `implement trait` block (trait
dispatch), and from a class method an ancestor class also declares
(virtual dispatch).  Constructors are never candidates: a `new` leaves no
call record.  The roots the records cannot see are in `keep.txt`, each
found by a type-check, `make cross-check` or a read that refused its
deletion.

Round 2 is the same sweep over the tree round 1 left: a method that
overrode a base method round 1 deleted loses its virtual-dispatch root.

Deleted: **149 functions**, 1436 lines of declaration and body (the diff also
drops a blank line beside some).  Held (not deleted): 1.

## Held

| function | file | why |
|---|---|---|
| `CodegenPasses::shell_quote` | `compiler/src/compiler/codegen/passes.cryo:2157` | Called only from code gated to the other OS: this host's facts never see the call, and `make cross-check` refused its deletion. |

## Roots the call records cannot see (`keep.txt`)

| function | why |
|---|---|
| `CodegenPasses::shell_quote` | Called only from code gated to the other OS: this host's facts never see the call, and `make cross-check` refused its deletion. |
| `cryo_ast_arena_alloc` | A language item: emitted code calls it at every `new` of a syntax-tree node. |
| `QualifiedName::equals` | No callers, but held: the brief parks these comparison primitives for Jake. |
| `QualifiedName::starts_with` | No callers, but held: the brief parks these comparison primitives for Jake. |
| `QualifiedName::ends_with` | No callers, but held: the brief parks these comparison primitives for Jake. |
| `Importer::visit_anon_enum_const` | Callbacks handed to libclang or a thread spawn, and calls made inside a constructor body (no call record is written there). |
| `Importer::visit_collect_acc_member` | Callbacks handed to libclang or a thread spawn, and calls made inside a constructor body (no call record is written there). |
| `Importer::visit_collect_cxx_method` | Callbacks handed to libclang or a thread spawn, and calls made inside a constructor body (no call record is written there). |
| `Importer::visit_collect_member` | Callbacks handed to libclang or a thread spawn, and calls made inside a constructor body (no call record is written there). |
| `Importer::visit_enum_const` | Callbacks handed to libclang or a thread spawn, and calls made inside a constructor body (no call record is written there). |
| `ArrayLitEmitter::null` | Callbacks handed to libclang or a thread spawn, and calls made inside a constructor body (no call record is written there). |
| `AsyncLower::null` | Callbacks handed to libclang or a thread spawn, and calls made inside a constructor body (no call record is written there). |
| `BinaryUnaryEmitter::null` | Callbacks handed to libclang or a thread spawn, and calls made inside a constructor body (no call record is written there). |
| `CallEmitter::null` | Callbacks handed to libclang or a thread spawn, and calls made inside a constructor body (no call record is written there). |
| `CallResolver::null` | Callbacks handed to libclang or a thread spawn, and calls made inside a constructor body (no call record is written there). |
| `DeclVisitEmitter::null` | Callbacks handed to libclang or a thread spawn, and calls made inside a constructor body (no call record is written there). |
| `Diagnostics::null` | Callbacks handed to libclang or a thread spawn, and calls made inside a constructor body (no call record is written there). |
| `EnumVariantEmitter::null` | Callbacks handed to libclang or a thread spawn, and calls made inside a constructor body (no call record is written there). |
| `ExprDispatch::null` | Callbacks handed to libclang or a thread spawn, and calls made inside a constructor body (no call record is written there). |
| `Importer::visit_decl` | Callbacks handed to libclang or a thread spawn, and calls made inside a constructor body (no call record is written there). |
| `Importer::visit_inclusion` | Callbacks handed to libclang or a thread spawn, and calls made inside a constructor body (no call record is written there). |
| `Importer::visit_probe` | Callbacks handed to libclang or a thread spawn, and calls made inside a constructor body (no call record is written there). |
| `LambdaEmitter::null` | Callbacks handed to libclang or a thread spawn, and calls made inside a constructor body (no call record is written there). |
| `LambdaSynth::null` | Callbacks handed to libclang or a thread spawn, and calls made inside a constructor body (no call record is written there). |
| `LiteralResolver::null` | Callbacks handed to libclang or a thread spawn, and calls made inside a constructor body (no call record is written there). |
| `MemberResolver::null` | Callbacks handed to libclang or a thread spawn, and calls made inside a constructor body (no call record is written there). |
| `MethodBinding::null` | Callbacks handed to libclang or a thread spawn, and calls made inside a constructor body (no call record is written there). |
| `NewDeleteEmitter::null` | Callbacks handed to libclang or a thread spawn, and calls made inside a constructor body (no call record is written there). |
| `PatternEmitter::null` | Callbacks handed to libclang or a thread spawn, and calls made inside a constructor body (no call record is written there). |
| `PatternResolver::null` | Callbacks handed to libclang or a thread spawn, and calls made inside a constructor body (no call record is written there). |
| `PlaceEmitter::null` | Callbacks handed to libclang or a thread spawn, and calls made inside a constructor body (no call record is written there). |
| `ScopeManager::null` | Callbacks handed to libclang or a thread spawn, and calls made inside a constructor body (no call record is written there). |
| `SemaDispatch::null` | Callbacks handed to libclang or a thread spawn, and calls made inside a constructor body (no call record is written there). |
| `SemaState::new` | Callbacks handed to libclang or a thread spawn, and calls made inside a constructor body (no call record is written there). |
| `SymbolicChecker::null` | Callbacks handed to libclang or a thread spawn, and calls made inside a constructor body (no call record is written there). |
| `TypeChecker::new` | Callbacks handed to libclang or a thread spawn, and calls made inside a constructor body (no call record is written there). |
| `TypeUtils::null` | Callbacks handed to libclang or a thread spawn, and calls made inside a constructor body (no call record is written there). |
| `VisitorState::new` | Callbacks handed to libclang or a thread spawn, and calls made inside a constructor body (no call record is written there). |
| `emit_worker_entry` | Callbacks handed to libclang or a thread spawn, and calls made inside a constructor body (no call record is written there). |
| `shell_jobs_worker` | Callbacks handed to libclang or a thread spawn, and calls made inside a constructor body (no call record is written there). |

## Deleted

| round | function | role | file | lines |
|---|---|---|---|---|
| 1 | `Command::add_argument` | method | `compiler/src/CLI/_module.cryo:210` | 4 |
| 1 | `LiteralKind::to_string` | method | `compiler/src/compiler/AST/_module.cryo:252` | 10 |
| 1 | `TraitRef::written_leaf` | method | `compiler/src/compiler/AST/_module.cryo:886` | 10 |
| 1 | `DeclarationNode::has_documentation` | method | `compiler/src/compiler/AST/declaration.cryo:70` | 1 |
| 1 | `DeclarationNode::has_directives` | method | `compiler/src/compiler/AST/declaration.cryo:72` | 1 |
| 1 | `VarDeclNode::has_kw_span` | method | `compiler/src/compiler/AST/declaration.cryo:288` | 1 |
| 1 | `DestructureDeclNode::set_resolved_type` | method | `compiler/src/compiler/AST/declaration.cryo:369` | 1 |
| 1 | `FunctionDeclNode::set_inline` | method | `compiler/src/compiler/AST/declaration.cryo:590` | 1 |
| 1 | `FunctionDeclNode::set_noinline` | method | `compiler/src/compiler/AST/declaration.cryo:591` | 1 |
| 1 | `FunctionDeclNode::set_implicit` | method | `compiler/src/compiler/AST/declaration.cryo:593` | 1 |
| 1 | `StructDeclNode::set_name` | method | `compiler/src/compiler/AST/declaration.cryo:987` | 1 |
| 1 | `UnionDeclNode::set_name` | method | `compiler/src/compiler/AST/declaration.cryo:1059` | 1 |
| 1 | `ClassDeclNode::set_name` | method | `compiler/src/compiler/AST/declaration.cryo:1136` | 1 |
| 1 | `TraitDeclNode::has_assoc_types` | method | `compiler/src/compiler/AST/declaration.cryo:1222` | 1 |
| 1 | `TypeAliasDeclNode::set_resolved_target` | method | `compiler/src/compiler/AST/declaration.cryo:1313` | 1 |
| 1 | `EnumDeclNode::set_name` | method | `compiler/src/compiler/AST/declaration.cryo:1368` | 1 |
| 1 | `FieldDeclNode::has_resolved_type` | method | `compiler/src/compiler/AST/declaration.cryo:1878` | 1 |
| 1 | `IdentifierNode::set_global_reference` | method | `compiler/src/compiler/AST/expression.cryo:231` | 3 |
| 1 | `CallExprNode::set_argument` | method | `compiler/src/compiler/AST/expression.cryo:590` | 6 |
| 1 | `ArrayLiteralNode::set_repeat_count` | method | `compiler/src/compiler/AST/expression.cryo:872` | 3 |
| 1 | `TryExprNode::has_desugar` | method | `compiler/src/compiler/AST/expression.cryo:1417` | 1 |
| 1 | `ASTNode::mark_error` | method | `compiler/src/compiler/AST/node.cryo:28` | 1 |
| 1 | `ASTNode::clear_error` | method | `compiler/src/compiler/AST/node.cryo:29` | 1 |
| 1 | `MatchArmNode::has_guard` | method | `compiler/src/compiler/AST/pattern.cryo:249` | 3 |
| 1 | `StaticMatchArmNode::is_wildcard` | method | `compiler/src/compiler/AST/pattern.cryo:278` | 1 |
| 1 | `TokenStream::token_count` | method | `compiler/src/compiler/artifacts.cryo:45` | 3 |
| 1 | `LinkedOutput::is_valid` | method | `compiler/src/compiler/artifacts.cryo:102` | 3 |
| 1 | `AbiClassifier::null` | static | `compiler/src/compiler/codegen/abi.cryo:217` | 16 |
| 1 | `AbiClassifier::classify_param_extern_c` | method | `compiler/src/compiler/codegen/abi.cryo:523` | 7 |
| 1 | `AbiClassifier::classify_return_extern_c` | method | `compiler/src/compiler/codegen/abi.cryo:815` | 7 |
| 1 | `CodegenContext::intern_str` | method | `compiler/src/compiler/codegen/context.cryo:249` | 6 |
| 1 | `LModule::verify` | method | `compiler/src/compiler/codegen/llvm_types.cryo:200` | 10 |
| 1 | `LModule::dump` | method | `compiler/src/compiler/codegen/llvm_types.cryo:211` | 3 |
| 1 | `LModule::context` | method | `compiler/src/compiler/codegen/llvm_types.cryo:300` | 3 |
| 1 | `LContext::global` | static | `compiler/src/compiler/codegen/llvm_types.cryo:352` | 3 |
| 1 | `LContext::null` | static | `compiler/src/compiler/codegen/llvm_types.cryo:370` | 3 |
| 1 | `LContext::is_valid` | method | `compiler/src/compiler/codegen/llvm_types.cryo:374` | 3 |
| 1 | `LBuilder::null` | static | `compiler/src/compiler/codegen/llvm_types.cryo:445` | 3 |
| 1 | `LBuilder::is_valid` | method | `compiler/src/compiler/codegen/llvm_types.cryo:449` | 3 |
| 1 | `LValue::dump` | method | `compiler/src/compiler/codegen/llvm_types.cryo:955` | 3 |
| 1 | `DeclarationEmitter::int_type_is_signed` | method | `compiler/src/compiler/codegen/ops/declaration_emitter.cryo:212` | 7 |
| 1 | `DeclarationEmitter::auto_deref_to` | method | `compiler/src/compiler/codegen/ops/declaration_emitter.cryo:2336` | 9 |
| 1 | `DiagSink::stripped_names` | method | `compiler/src/compiler/codegen/state/diag_sink.cryo:53` | 11 |
| 1 | `TypeMapper::forward_declare_struct` | method | `compiler/src/compiler/codegen/type_map.cryo:414` | 8 |
| 1 | `ExprDispatch::codegen_expr` | method | `compiler/src/compiler/codegen/visit/expr_dispatch.cryo:128` | 6 |
| 1 | `OutputKind::is_override` | method | `compiler/src/compiler/compilation_context.cryo:78` | 6 |
| 1 | `CompilationContext::has_errors` | method | `compiler/src/compiler/compilation_context.cryo:913` | 4 |
| 1 | `LockedVendor::empty` | static | `compiler/src/compiler/deps/lockfile.cryo:80` | 3 |
| 1 | `ErrorCode::to_string` | method | `compiler/src/compiler/diag/_module.cryo:345` | 267 |
| 1 | `DiagConfig::suppress` | method | `compiler/src/compiler/diag/config.cryo:47` | 6 |
| 1 | `Diagnostic::note` | static | `compiler/src/compiler/diag/diagnostic.cryo:56` | 12 |
| 1 | `RenderConfig::colored` | static | `compiler/src/compiler/diag/renderer.cryo:135` | 12 |
| 1 | `DiagnosticSink::begin_suppress` | method | `compiler/src/compiler/diag/sink.cryo:224` | 6 |
| 1 | `DiagnosticSink::end_suppress` | method | `compiler/src/compiler/diag/sink.cryo:229` | 6 |
| 1 | `DiagnosticSink::count` | method | `compiler/src/compiler/diag/sink.cryo:265` | 4 |
| 1 | `Suggestion::multipart` | static | `compiler/src/compiler/diag/suggestion.cryo:140` | 20 |
| 1 | `CompilerInstance::compile_for_lsp` | method | `compiler/src/compiler/instance.cryo:646` | 13 |
| 1 | `Token::is_keyword` | method | `compiler/src/compiler/lex/_module.cryo:392` | 4 |
| 1 | `Token::is_punctuator` | method | `compiler/src/compiler/lex/_module.cryo:397` | 4 |
| 1 | `Token::is_literal` | method | `compiler/src/compiler/lex/_module.cryo:402` | 4 |
| 1 | `Token::to_string` | method | `compiler/src/compiler/lex/_module.cryo:407` | 3 |
| 1 | `TokenType::is_punctuator` | method | `compiler/src/compiler/lex/_module.cryo:551` | 76 |
| 1 | `TokenType::is_literal` | method | `compiler/src/compiler/lex/_module.cryo:628` | 11 |
| 1 | `SourceLocation::to_string` | method | `compiler/src/compiler/lex/_module.cryo:1101` | 3 |
| 1 | `ModuleInfo::new` | static | `compiler/src/compiler/module_graph.cryo:167` | 21 |
| 1 | `MonoCallSpecializer::impl_provides_trait` | method | `compiler/src/compiler/mono/call_specializer.cryo:2581` | 8 |
| 1 | `MonoCallSpecializer::qualify_spec_name` | method | `compiler/src/compiler/mono/call_specializer.cryo:2587` | 12 |
| 1 | `MonoTraitSpecializer::template_key_of` | method | `compiler/src/compiler/mono/trait_specializer.cryo:466` | 5 |
| 1 | `ExprParser::is_type_start` | method | `compiler/src/compiler/parser/expr_parser.cryo:3247` | 11 |
| 1 | `ParserBase::check_any` | method | `compiler/src/compiler/parser/parser_base.cryo:248` | 10 |
| 1 | `ParserBase::match_any` | method | `compiler/src/compiler/parser/parser_base.cryo:268` | 8 |
| 1 | `ParserBase::report_error_at` | method | `compiler/src/compiler/parser/parser_base.cryo:605` | 6 |
| 1 | `ParserBase::error_eof` | method | `compiler/src/compiler/parser/parser_base.cryo:636` | 6 |
| 1 | `DirectiveRegistry::has` | method | `compiler/src/compiler/passes/directive_processing.cryo:164` | 9 |
| 1 | `DirectiveRegistry::clear` | method | `compiler/src/compiler/passes/directive_processing.cryo:176` | 5 |
| 1 | `PassID::name` | method | `compiler/src/compiler/passes/pass_id.cryo:98` | 33 |
| 1 | `PassStage::to_int` | method | `compiler/src/compiler/passes/pass_id.cryo:626` | 14 |
| 1 | `PassRegistry::run_until` | method | `compiler/src/compiler/passes/pass_registry.cryo:293` | 49 |
| 1 | `PassRegistry::build_standard_pipeline` | static | `compiler/src/compiler/passes/pass_registry.cryo:358` | 36 |
| 1 | `PanicStrategy::label` | method | `compiler/src/compiler/project_config.cryo:79` | 7 |
| 1 | `DeclName::is_empty` | method | `compiler/src/compiler/resolver/decl_name.cryo:42` | 3 |
| 1 | `InternTable::contains` | method | `compiler/src/compiler/resolver/intern_table.cryo:105` | 5 |
| 1 | `InternTable::lookup` | method | `compiler/src/compiler/resolver/intern_table.cryo:111` | 9 |
| 1 | `InternTable::count` | method | `compiler/src/compiler/resolver/intern_table.cryo:120` | 4 |
| 1 | `OperatorKind::code` | method | `compiler/src/compiler/resolver/mangled_name.cryo:129` | 27 |
| 1 | `mangled_name::operator_kind_from_name` | free | `compiler/src/compiler/resolver/mangled_name.cryo:160` | 40 |
| 1 | `MangledName::empty` | static | `compiler/src/compiler/resolver/mangled_name.cryo:231` | 4 |
| 1 | `MangledName::is_empty` | method | `compiler/src/compiler/resolver/mangled_name.cryo:409` | 1 |
| 1 | `MangledName::is_valid` | method | `compiler/src/compiler/resolver/mangled_name.cryo:410` | 1 |
| 1 | `MangledName::equals` | method | `compiler/src/compiler/resolver/mangled_name.cryo:411` | 1 |
| 1 | `MangleContext::owner_qname_leaf_or_intern` | static | `compiler/src/compiler/resolver/mangled_name.cryo:911` | 16 |
| 1 | `Namespace::to_string` | method | `compiler/src/compiler/resolver/namespace_kind.cryo:88` | 6 |
| 1 | `QualifiedName::from_parts` | static | `compiler/src/compiler/resolver/qualified_name.cryo:50` | 4 |
| 1 | `QualifiedName::parent` | method | `compiler/src/compiler/resolver/qualified_name.cryo:115` | 13 |
| 1 | `QualifiedName::child` | method | `compiler/src/compiler/resolver/qualified_name.cryo:138` | 10 |
| 1 | `QualifiedName::join` | method | `compiler/src/compiler/resolver/qualified_name.cryo:156` | 12 |
| 1 | `QualifiedName::display_parent` | method | `compiler/src/compiler/resolver/qualified_name.cryo:234` | 14 |
| 1 | `DefTable::origin_of` | method | `compiler/src/compiler/resolver/res.cryo:287` | 4 |
| 1 | `ResBase::to_string` | method | `compiler/src/compiler/resolver/res.cryo:540` | 7 |
| 1 | `SourceLoc::to_string` | method | `compiler/src/compiler/resolver/res.cryo:575` | 6 |
| 1 | `Res::to_string` | method | `compiler/src/compiler/resolver/res.cryo:668` | 13 |
| 1 | `Resolver::debug_dump` | method | `compiler/src/compiler/resolver/resolver.cryo:1164` | 16 |
| 1 | `ScopeID::equals` | method | `compiler/src/compiler/resolver/scope.cryo:44` | 4 |
| 1 | `SymbolKind::to_string` | method | `compiler/src/compiler/resolver/symbol.cryo:61` | 19 |
| 1 | `SymbolVisibility::to_string` | method | `compiler/src/compiler/resolver/symbol.cryo:108` | 7 |
| 1 | `CallResolver::current_module_name` | method | `compiler/src/compiler/sema/call_resolver.cryo:241` | 5 |
| 1 | `SemaDispatch::dispatch_top_level` | method | `compiler/src/compiler/sema/dispatch.cryo:68` | 5 |
| 1 | `MemberResolver::current_module_name` | method | `compiler/src/compiler/sema/member_resolver.cryo:943` | 5 |
| 1 | `TypeUtils::is_reference_type` | method | `compiler/src/compiler/sema/type_utils.cryo:617` | 6 |
| 1 | `TypeUtils::is_ref_or_ptr_type` | method | `compiler/src/compiler/sema/type_utils.cryo:632` | 8 |
| 1 | `TypeKind::is_compound` | method | `compiler/src/compiler/types/_module.cryo:101` | 11 |
| 1 | `TypeKind::is_generic` | method | `compiler/src/compiler/types/_module.cryo:114` | 7 |
| 1 | `TypeArena::find_instantiation` | method | `compiler/src/compiler/types/arena.cryo:729` | 11 |
| 1 | `TypeArena::array_element_of` | method | `compiler/src/compiler/types/arena.cryo:756` | 13 |
| 1 | `TypeCompatibility::to_string` | method | `compiler/src/compiler/types/checker.cryo:47` | 9 |
| 1 | `ConversionInfo::new` | static | `compiler/src/compiler/types/checker.cryo:101` | 8 |
| 1 | `TypeChecker::check_function_call` | method | `compiler/src/compiler/types/checker.cryo:991` | 30 |
| 1 | `ArrayType::fixed_pending_size` | static | `compiler/src/compiler/types/compound.cryo:119` | 1 |
| 1 | `FunctionType::param_count` | method | `compiler/src/compiler/types/compound.cryo:162` | 3 |
| 2 | `GenericParamType::is_resolved` | method | `compiler/src/compiler/types/generic.cryo:57` | 2 |
| 1 | `InstantiatedType::arg_count` | method | `compiler/src/compiler/types/generic.cryo:144` | 3 |
| 2 | `AssocProjectionType::has_resolved_type` | method | `compiler/src/compiler/types/generic.cryo:203` | 1 |
| 2 | `AssocProjectionType::is_resolved` | method | `compiler/src/compiler/types/generic.cryo:216` | 2 |
| 1 | `ErrorType::add_note` | method | `compiler/src/compiler/types/generic.cryo:241` | 3 |
| 1 | `InferCtx::adopt` | static | `compiler/src/compiler/types/inference.cryo:56` | 20 |
| 1 | `TypeSubstitution::is_empty` | method | `compiler/src/compiler/types/substitution.cryo:87` | 4 |
| 1 | `TypeSubstitution::size` | method | `compiler/src/compiler/types/substitution.cryo:92` | 4 |
| 1 | `Type::is_resolved` | method | `compiler/src/compiler/types/type_base.cryo:37` | 4 |
| 1 | `Type::is_primitive` | method | `compiler/src/compiler/types/type_base.cryo:47` | 4 |
| 1 | `IntType::is_unsigned` | method | `compiler/src/compiler/types/type_base.cryo:109` | 1 |
| 1 | `StructType::add_field` | method | `compiler/src/compiler/types/user_defined.cryo:233` | 3 |
| 1 | `StructType::field_count` | method | `compiler/src/compiler/types/user_defined.cryo:251` | 1 |
| 1 | `ClassType::add_field` | method | `compiler/src/compiler/types/user_defined.cryo:355` | 3 |
| 1 | `LogLevel::label` | method | `compiler/src/utils/logger.cryo:33` | 11 |
| 1 | `LogLevel::color` | method | `compiler/src/utils/logger.cryo:45` | 11 |
| 1 | `LogLevel::reset` | method | `compiler/src/utils/logger.cryo:57` | 3 |
| 1 | `LogLevel::to_int` | method | `compiler/src/utils/logger.cryo:61` | 11 |
| 1 | `LogLevel::meets_threshold` | method | `compiler/src/utils/logger.cryo:74` | 5 |
| 1 | `LogComponent::label` | method | `compiler/src/utils/logger.cryo:99` | 18 |
| 1 | `LoggerConfig::debug_config` | static | `compiler/src/utils/logger.cryo:134` | 8 |
| 1 | `Logger::new` | static | `compiler/src/utils/logger.cryo:149` | 3 |
| 1 | `Logger::trace` | method | `compiler/src/utils/logger.cryo:178` | 3 |
| 1 | `Logger::debug` | method | `compiler/src/utils/logger.cryo:182` | 3 |
| 1 | `Logger::info` | method | `compiler/src/utils/logger.cryo:186` | 3 |
| 1 | `Logger::warn` | method | `compiler/src/utils/logger.cryo:190` | 3 |
| 1 | `Logger::error` | method | `compiler/src/utils/logger.cryo:194` | 3 |
| 1 | `Logger::fatal` | method | `compiler/src/utils/logger.cryo:198` | 6 |
| 1 | `Logger::write_log` | method | `compiler/src/utils/logger.cryo:208` | 41 |
| 1 | `logger::compiler_debug_enabled` | free | `compiler/src/utils/logger.cryo:291` | 3 |

## Types and fields (second batch)

From `deadtypes.py` over the tree the function sweep left: a type whose
name appears nowhere outside its own declaration and `implement` blocks,
and a field never accessed as `.name` anywhere in the compiler or the LSP
(set only in struct literals).  Fields are removed with their literal
initializers by `delete_fields.py`; three whose initializers share a line
with other fields were edited by hand (`AsyncDecl::owner_impl`,
`BindingSerializer::ns`, the three `LoggerConfig` flags).  The libclang
mirror structs' fields are kept: their layout is C's.  `empty_impls.py`
removed the `implement` blocks and access labels the sweep left empty.

| kind | owner | name | file |
|---|---|---|---|
| type (enum) | | `compiler::codegen::llvm_types::LVerifierAction` | `compiler/src/compiler/codegen/llvm_types.cryo:131` |
| type (enum) | | `compiler::passes::move_check::MoveState` | `compiler/src/compiler/passes/move_check.cryo:89` |
| type (enum) | | `compiler::resolver::mangled_name::OperatorKind` | `compiler/src/compiler/resolver/mangled_name.cryo:119` |
| type (struct) | | `compiler::types::checker::ConversionInfo` | `compiler/src/compiler/types/checker.cryo:86` |
| field | `Argument` | `aliases` | `compiler/src/CLI/_module.cryo:25` |
| field | `Argument` | `required` | `compiler/src/CLI/_module.cryo:26` |
| field | `Argument` | `is_flag` | `compiler/src/CLI/_module.cryo:27` |
| field | `BindingSerializer` | `ns` | `compiler/src/compiler/bindgen/generator.cryo:130` |
| field | `RenderConfig` | `preserve_markup` | `compiler/src/compiler/diag/renderer.cryo:102` |
| field | `Lexer` | `spot_content` | `compiler/src/compiler/lex/Lexer.cryo:32` |
| field | `Lexer` | `current_token` | `compiler/src/compiler/lex/Lexer.cryo:34` |
| field | `Lexer` | `token_count` | `compiler/src/compiler/lex/Lexer.cryo:36` |
| field | `ModuleInfo` | `processed` | `compiler/src/compiler/module_graph.cryo:149` |
| field | `ModuleInfo` | `specializations` | `compiler/src/compiler/module_graph.cryo:159` |
| field | `SpecializationEntry` | `specialized_name` | `compiler/src/compiler/mono/State.cryo:112` |
| field | `PassMetadata` | `order` | `compiler/src/compiler/passes/pass_id.cryo:674` |
| field | `AsyncDecl` | `owner_impl` | `compiler/src/compiler/sema/async_lower.cryo:344` |
| field | `LoggerConfig` | `enable_colors` | `compiler/src/utils/logger.cryo:59` |
| field | `LoggerConfig` | `enable_timestamps` | `compiler/src/utils/logger.cryo:60` |
| field | `LoggerConfig` | `enable_component_tags` | `compiler/src/utils/logger.cryo:61` |
