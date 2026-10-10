# Cross-check: the Python spelling-flow list against the compiler's own checks

Measured on the tree of the commit carrying this file.  `scripts/spelling-flow.outstanding.tsv` as it stands (ruling 219: the Python is not changed): 362 entries, 503 counting each entry's weight.

## Method

* `xcheck/srcindex.py` indexes every function of `compiler/src` (qualified
  name as the list writes it, file, line span, its allow and door marker).
  Control: every allow and door marker in the source lands on an indexed
  function or on a field (`xcheck/idxctl.py`, `idxctl2.py`); all 128 lookup
  functions the list names are indexed (`xcheck/mapcheck.py`).
* `xcheck/strip.py` copies the compiler and deletes all 97 allows of
  `lookup_by_spelling` that sit on a function (a field's stays: the
  store-key lint runs before sema and would stop the build).  The compiler
  under test builds the copy: 59 E0157 refusals in 55 functions, every one
  mapped to an indexed function.
* `xcheck/xcheck.py`: an entry is SEEN when its lookup function is allowed
  and the stripped build refuses a site in it.  Every other entry is
  classified below by `xcheck/classify.py`, which also writes this file;
  each class carries its reason.

## Result

| class | entries | weighted |
|---|---|---|
| SEEN: The compiler refuses it | 120 | 214 |
| PY-MODULEPATH: Not a lookup: module identities compared (a Python-side limitation) | 16 | 20 |
| OUT-FLAG: Not a lookup of a program's name: command-line flags (ruling 5) | 111 | 125 |
| OUT-CONFIG: Not a lookup of a program's name: configuration, manifest, lock-file and vendor keys | 39 | 58 |
| OUT-TEXT: Not a lookup of a program's name: file paths, messages and literal contents | 23 | 26 |
| OUT-FIXED: Not a lookup of a program's name: the language's own fixed spellings | 23 | 23 |
| GAP-STRING: A lookup the compiler does not see: a written name handed on as a `string` | 3 | 3 |
| GAP-LOADER: Not seen by the compiler: module discovery's text, before any module exists | 14 | 18 |
| GAP-ORDER: A lookup the compiler does not see: a spelling kept for a later loop iteration | 2 | 2 |
| GAP-IDTEXT: A lookup the compiler does not see: identity text that is not a `DeclName` | 9 | 12 |
| ALLOWED: Not refused, and allowed with a reason | 2 | 2 |

**Is the Python list redundant?  Not yet.**  120 entries (214 weighted) are refused by the compiler; 212 (252) are not lookups of a program's name, or compare module identities the Python reads as text; 2 (2) are allowed with reasons the compiler does not need.  The rest - 28 entries (35 weighted), in the GAP classes - are lookups, or may be, that only the Python sees today.  Each GAP class names what the compiler would have to follow to see it.

## SEEN: The compiler refuses it (120 entries, 214 weighted)

Its lookup function carries an allow of `lookup_by_spelling`; with every such allow removed, the compiler under test refuses a site in that function (E0157).  The allow's written reason is why the lookup stands.

| n | rule | kind | key | lookup function | origin | origin function | origin text |
|---|---|---|---|---|---|---|---|
| 1 | 3 | scan | name | ast::declaration::TraitDeclNode.lookup_method | chain-lost | ast::declaration::TraitDeclNode.lookup_method | no recorded caller passes n |
| 1 | 3 | scan | name | ast::dumper::ASTDumper.visit | field | ast::dumper::ASTDumper.visit | ast::declaration::DestructureBinding.local_name |
| 1 | 3 | scan | name | ast::node_locator::NodeLocator::method_by_name | chain-lost | ast::node_locator::NodeLocator::method_by_name | no recorded caller passes name |
| 1 | 3 | scan | string | bindgen::generator::BindingSerializer::render | expr | bindgen::generator::BindingSerializer::render | IfExpression |
| 1 | 3 | scan | string | bindgen::generator::BindingSerializer::render | field | bindgen::generator::BindingSerializer::render | ast::declaration::FunctionDeclNode*.name |
| 1 | 3 | scan | name | bindgen::importer::Importer.arith_operand | call | bindgen::importer::Importer.arith_operand | bindgen::clang::CXToken::spelling |
| 2 | 3 | scan | name | bindgen::importer::Importer.arith_operand_int | call | bindgen::importer::Importer.arith_operand_int | bindgen::clang::CXToken::spelling |
| 1 | 3 | scan | string | bindgen::importer::Importer.emit_forwarding_wrapper | literal | bindgen::importer::Importer.emit_forwarding_wrapper | "" |
| 1 | 3 | scan | name | bindgen::importer::Importer.find_function | call | bindgen::importer::Importer.emit_macro_complex | bindgen::clang::CXToken::spelling |
| 1 | 3 | scan | name | bindgen::importer::Importer.find_struct | call | bindgen::importer::Importer.emit_record | bindgen::clang::CXCursor::intern_spelling |
| 1 | 3 | scan | name | bindgen::importer::Importer.find_struct | call | bindgen::importer::Importer.try_emit_compound_struct | bindgen::clang::CXToken::spelling |
| 1 | 3 | scan | name | bindgen::importer::Importer.is_bound_function | call | bindgen::importer::Importer.emit_macro_complex | bindgen::clang::CXToken::spelling |
| 1 | 3 | scan | name | bindgen::importer::Importer.name_seen | call | bindgen::importer::Importer.add_anon_const_from | bindgen::clang::CXCursor::intern_spelling |
| 1 | 3 | scan | name | bindgen::importer::Importer.name_seen | call | bindgen::importer::Importer.emit_enum | bindgen::clang::CXCursor::intern_spelling |
| 1 | 3 | scan | name | bindgen::importer::Importer.name_seen | call | bindgen::importer::Importer.emit_global | bindgen::clang::CXCursor::intern_spelling |
| 1 | 3 | scan | name | bindgen::importer::Importer.name_seen | call | bindgen::importer::Importer.emit_macro | bindgen::clang::CXCursor::intern_spelling |
| 1 | 3 | scan | name | bindgen::importer::Importer.name_seen | call | bindgen::importer::Importer.emit_macro_complex | bindgen::clang::CXCursor::intern_spelling |
| 2 | 3 | scan | name | bindgen::importer::Importer.name_seen | call | bindgen::importer::Importer.emit_record | bindgen::clang::CXCursor::intern_spelling |
| 1 | 3 | scan | name | bindgen::importer::Importer.name_seen | call | bindgen::importer::Importer.emit_typedef | bindgen::clang::CXCursor::intern_spelling |
| 1 | 3 | scan | name | bindgen::importer::Importer.name_seen | element | bindgen::importer::Importer::visit_probe | bindgen::importer::Importer*.probe_names<-local:imp@0=param:client |
| 1 | 3 | scan | name | bindgen::importer::Importer.name_seen | element | bindgen::importer::Importer.probe_macro_constants | bindgen::importer::Importer.probe_names<-param:this |
| 1 | 3 | scan | name | bindgen::importer::Importer.pin_record_refs | field | bindgen::importer::Importer.pin_record_refs | bindgen::importer::Importer.alias |
| 1 | 3 | scan | name | bindgen::importer::Importer.pin_record_refs | field | bindgen::importer::Importer.pin_record_refs | compilation_context::CRecordRef.tag |
| 3 | 3 | scan | name | bindgen::importer::Importer.try_emit_identifier_alias | call | bindgen::importer::Importer.emit_macro_complex | bindgen::clang::CXToken::spelling |
| 1 | 3 | scan | name | codegen::ops::declaration_emitter::DeclarationEmitter.collect_vtable_slots | field | codegen::ops::declaration_emitter::DeclarationEmitter.collect_vtable_slots | &types::user_defined::MethodInfo.name |
| 1 | 3 | scan | name | codegen::ops::declaration_emitter::DeclarationEmitter.find_vtable_slot_impl | field | codegen::ops::declaration_emitter::DeclarationEmitter.codegen_vtable_for_class | codegen::util::VTableSlot.name |
| 1 | 3 | scan | name | codegen::ops::declaration_emitter::DeclarationEmitter.vtable_slot_index | field | codegen::visit::call_emitter::CallEmitter.emit | ast::declaration::FunctionDeclNode*.name |
| 1 | 3 | map | name | codegen::state::function_registry::FunctionRegistry.get | call | codegen::ops::declaration_emitter::DeclarationEmitter.codegen_function_prologue | codegen::ops::declaration_emitter::DeclarationEmitter.free_function_sy |
| 1 | 3 | map | name | codegen::state::function_registry::FunctionRegistry.get | call | codegen::ops::declaration_emitter::DeclarationEmitter.codegen_naked_prologue | codegen::ops::declaration_emitter::DeclarationEmitter.free_function_sy |
| 1 | 3 | map | name | codegen::state::function_registry::FunctionRegistry.get | call | codegen::ops::declaration_emitter::DeclarationEmitter.codegen_method_prologue | codegen::ops::declaration_emitter::DeclarationEmitter.method_symbol |
| 1 | 3 | map | name | codegen::state::function_registry::FunctionRegistry.get | call | codegen::ops::symbol_resolver::SymbolResolver.resolve_family | codegen::ops::symbol_resolver::SymbolResolver.family_answer |
| 1 | 3 | map | name | codegen::state::function_registry::FunctionRegistry.get | call | codegen::ops::symbol_resolver::SymbolResolver.resolve_decl | decl_index::DeclarationIndex.entry_symbol |
| 1 | 3 | map | name | decl_index::DeclarationIndex.extern_symbol_conflict | expr | passes::type_resolution::TypeResolutionPasses::run_function_signature | IfExpression |
| 1 | 4 | door:intern | string | decl_index::DeclarationIndex.mangle_function_symbol | identity | decl_index::DeclarationIndex.mangle_function_symbol | MangledName.as_string |
| 1 | 3 | map | name | decl_index::DeclarationIndex.note_extern_symbol | expr | passes::type_resolution::TypeResolutionPasses::run_function_signature | IfExpression |
| 1 | 3 | write | name | decl_index::DeclarationIndex.note_extern_symbol | expr | passes::type_resolution::TypeResolutionPasses::run_function_signature | IfExpression |
| 1 | 3 | scan | name | decl_index::DeclarationIndex.register_signature | expr | decl_index::DeclarationIndex.register_function_signature | IfExpression |
| 1 | 3 | scan | name | decl_index::DeclarationIndex.register_signature | expr | decl_index::DeclarationIndex.register_methods_through | IfExpression |
| 1 | 3 | scan | name | module_graph::ModuleGraph.source_file_of | field | module_graph::ModuleGraph.source_file_of | module_graph::ModulePath.sym |
| 1 | 3 | scan | name | parser::parser::Parser::declares_generic_param | field | parser::parser::Parser::declares_generic_param | ast::declaration::GenericParamNode*.name |
| 1 | 3 | scan | string | resolver::name_resolution::NameResolver.type_name_candidates | field | resolver::name_resolution::NameResolver.type_name_candidates | resolver::name_resolution::NameResolver.defining_type |
| 1 | 3 | scan | name | resolver::name_resolution::NameResolver::repeats_binding | field | resolver::name_resolution::NameResolver::repeats_binding | ast::declaration::DestructureBinding.local_name |
| 1 | 3 | scan | name | resolver::name_resolution::NameResolver::repeats_parameter | field | resolver::name_resolution::NameResolver::repeats_parameter | ast::declaration::VarDeclNode*.name |
| 1 | 3 | scan | name | resolver::resolver::Scope.clear_ambiguity | call | resolver::resolver::Resolver.declare_module_symbol | resolver::decl_name::DeclName.spelling |
| 1 | 3 | scan | name | resolver::resolver::Scope.clear_ambiguity | field | resolver::resolver::Resolver.declare | resolver::symbol::Symbol.name |
| 1 | 4 | scan | name | resolver::resolver::Scope.clear_ambiguity | identity | resolver::resolver::Resolver.declare_module_symbol | DeclName.spelling |
| 1 | 4 | scan | name | resolver::resolver::Scope.clear_ambiguity | identity | resolver::resolver::Resolver.declare | Symbol.name |
| 4 | 3 | map | name | resolver::resolver::Scope.find | element | resolver::name_resolution::NameResolver.stamp_trait_ref_identity | ast::TraitRef*.path<-param:tref |
| 4 | 3 | map | name | resolver::resolver::Scope.find | expr | resolver::name_resolution::NameResolver.stamp_trait_ref_identity | BinaryExpression |
| 3 | 3 | map | name | resolver::resolver::Scope.find | field | resolver::name_resolution::NameResolver.stamp_impl_target_args | ast::NamedAnnotation*.name |
| 4 | 3 | map | name | resolver::resolver::Scope.find | field | resolver::name_resolution::NameResolver.stamp_named_annotation | ast::NamedAnnotation*.name |
| 1 | 3 | map | name | resolver::resolver::Scope.find | field | resolver::name_resolution::NameResolver.stamp_trait_bounds | ast::TraitBound*.type_parameter |
| 1 | 3 | map | name | resolver::resolver::Scope.find | field | resolver::name_resolution::NameResolver.declare_generics | ast::declaration::GenericParamNode*.name |
| 4 | 3 | map | name | resolver::resolver::Scope.find | field | resolver::name_resolution::NameResolver.visit | ast::declaration::ImplBlockNode*.target_type |
| 1 | 3 | map | name | resolver::resolver::Scope.find | field | resolver::name_resolution::NameResolver.visit | ast::declaration::VarDeclNode*.name |
| 1 | 3 | map | name | resolver::resolver::Scope.find | field | resolver::name_resolution::NameResolver.refuse_unbound_name | ast::expression::IdentifierNode*.name |
| 1 | 3 | map | name | resolver::resolver::Scope.find | field | resolver::name_resolution::NameResolver.visit | ast::expression::IdentifierNode*.name |
| 10 | 3 | map | name | resolver::resolver::Scope.find | field | resolver::name_resolution::NameResolver.visit | ast::expression::NewExprNode*.type_name |
| 2 | 3 | map | name | resolver::resolver::Scope.find | field | resolver::name_resolution::NameResolver.stamp_module_scope | ast::expression::ScopeResolutionNode*.scope_name |
| 1 | 3 | map | name | resolver::resolver::Scope.find | field | resolver::name_resolution::NameResolver.visit | ast::expression::ScopeResolutionNode*.scope_name |
| 5 | 3 | map | name | resolver::resolver::Scope.find | field | resolver::name_resolution::NameResolver.visit | ast::expression::StructLiteralNode*.struct_type |
| 4 | 3 | map | name | resolver::resolver::Scope.find | field | resolver::name_resolution::NameResolver.visit | ast::pattern::EnumPatternNode*.enum_name |
| 1 | 3 | map | name | resolver::resolver::Scope.find | field | resolver::name_resolution::NameResolver.visit | ast::pattern::PatternNode*.binding_name |
| 1 | 3 | map | name | resolver::resolver::Scope.find | field | resolver::resolver::Resolver.declare_import | resolver::symbol::Symbol.name |
| 1 | 4 | map | name | resolver::resolver::Scope.find | identity | resolver::resolver::Resolver.declare_import | Symbol.name |
| 1 | 3 | map | name | resolver::resolver::Scope.find | literal | resolver::name_resolution::NameDeclarationPass::record_entry_fn | "main" |
| 2 | 3 | scan | name | resolver::resolver::Scope.get_ambiguous_modules | element | resolver::name_resolution::NameResolver.stamp_trait_ref_identity | ast::TraitRef*.path<-param:tref |
| 2 | 3 | scan | name | resolver::resolver::Scope.get_ambiguous_modules | expr | resolver::name_resolution::NameResolver.stamp_trait_ref_identity | BinaryExpression |
| 2 | 3 | scan | name | resolver::resolver::Scope.get_ambiguous_modules | field | resolver::name_resolution::NameResolver.stamp_impl_target_args | ast::NamedAnnotation*.name |
| 2 | 3 | scan | name | resolver::resolver::Scope.get_ambiguous_modules | field | resolver::name_resolution::NameResolver.stamp_named_annotation | ast::NamedAnnotation*.name |
| 2 | 3 | scan | name | resolver::resolver::Scope.get_ambiguous_modules | field | resolver::name_resolution::NameResolver.visit | ast::declaration::ImplBlockNode*.target_type |
| 1 | 3 | scan | name | resolver::resolver::Scope.get_ambiguous_modules | field | resolver::name_resolution::NameResolver.visit | ast::expression::IdentifierNode*.name |
| 6 | 3 | scan | name | resolver::resolver::Scope.get_ambiguous_modules | field | resolver::name_resolution::NameResolver.visit | ast::expression::NewExprNode*.type_name |
| 2 | 3 | scan | name | resolver::resolver::Scope.get_ambiguous_modules | field | resolver::name_resolution::NameResolver.visit | ast::expression::StructLiteralNode*.struct_type |
| 2 | 3 | scan | name | resolver::resolver::Scope.get_ambiguous_modules | field | resolver::name_resolution::NameResolver.visit | ast::pattern::EnumPatternNode*.enum_name |
| 3 | 3 | map | name | resolver::resolver::Scope.get_overloads | element | resolver::name_resolution::NameResolver.stamp_trait_ref_identity | ast::TraitRef*.path<-param:tref |
| 3 | 3 | scan | name | resolver::resolver::Scope.get_overloads | element | resolver::name_resolution::NameResolver.stamp_trait_ref_identity | ast::TraitRef*.path<-param:tref |
| 2 | 3 | map | name | resolver::resolver::Scope.get_overloads | element | resolver::name_resolution::NameResolver.process_import | ast::declaration::ImportDeclNode*.specific_imports<-param:node |
| 2 | 3 | scan | name | resolver::resolver::Scope.get_overloads | element | resolver::name_resolution::NameResolver.process_import | ast::declaration::ImportDeclNode*.specific_imports<-param:node |
| 3 | 3 | map | name | resolver::resolver::Scope.get_overloads | expr | resolver::name_resolution::NameResolver.stamp_trait_ref_identity | BinaryExpression |
| 3 | 3 | scan | name | resolver::resolver::Scope.get_overloads | expr | resolver::name_resolution::NameResolver.stamp_trait_ref_identity | BinaryExpression |
| 1 | 3 | map | name | resolver::resolver::Scope.get_overloads | field | resolver::resolver::Resolver.import_target | &resolver::symbol::Symbol.name |
| 1 | 3 | scan | name | resolver::resolver::Scope.get_overloads | field | resolver::resolver::Resolver.import_target | &resolver::symbol::Symbol.name |
| 3 | 3 | map | name | resolver::resolver::Scope.get_overloads | field | resolver::name_resolution::NameResolver.stamp_impl_target_args | ast::NamedAnnotation*.name |
| 3 | 3 | map | name | resolver::resolver::Scope.get_overloads | field | resolver::name_resolution::NameResolver.stamp_named_annotation | ast::NamedAnnotation*.name |
| 3 | 3 | scan | name | resolver::resolver::Scope.get_overloads | field | resolver::name_resolution::NameResolver.stamp_impl_target_args | ast::NamedAnnotation*.name |
| 3 | 3 | scan | name | resolver::resolver::Scope.get_overloads | field | resolver::name_resolution::NameResolver.stamp_named_annotation | ast::NamedAnnotation*.name |
| 3 | 3 | map | name | resolver::resolver::Scope.get_overloads | field | resolver::name_resolution::NameResolver.visit | ast::declaration::ImplBlockNode*.target_type |
| 3 | 3 | scan | name | resolver::resolver::Scope.get_overloads | field | resolver::name_resolution::NameResolver.visit | ast::declaration::ImplBlockNode*.target_type |
| 9 | 3 | map | name | resolver::resolver::Scope.get_overloads | field | resolver::name_resolution::NameResolver.visit | ast::expression::NewExprNode*.type_name |
| 9 | 3 | scan | name | resolver::resolver::Scope.get_overloads | field | resolver::name_resolution::NameResolver.visit | ast::expression::NewExprNode*.type_name |
| 1 | 3 | map | name | resolver::resolver::Scope.get_overloads | field | resolver::name_resolution::NameResolver.stamp_module_scope | ast::expression::ScopeResolutionNode*.member_name |
| 1 | 3 | scan | name | resolver::resolver::Scope.get_overloads | field | resolver::name_resolution::NameResolver.stamp_module_scope | ast::expression::ScopeResolutionNode*.member_name |
| 1 | 3 | map | name | resolver::resolver::Scope.get_overloads | field | resolver::name_resolution::NameResolver.stamp_module_scope | ast::expression::ScopeResolutionNode*.scope_name |
| 1 | 3 | scan | name | resolver::resolver::Scope.get_overloads | field | resolver::name_resolution::NameResolver.stamp_module_scope | ast::expression::ScopeResolutionNode*.scope_name |
| 3 | 3 | map | name | resolver::resolver::Scope.get_overloads | field | resolver::name_resolution::NameResolver.visit | ast::expression::StructLiteralNode*.struct_type |
| 3 | 3 | scan | name | resolver::resolver::Scope.get_overloads | field | resolver::name_resolution::NameResolver.visit | ast::expression::StructLiteralNode*.struct_type |
| 3 | 3 | map | name | resolver::resolver::Scope.get_overloads | field | resolver::name_resolution::NameResolver.visit | ast::pattern::EnumPatternNode*.enum_name |
| 3 | 3 | scan | name | resolver::resolver::Scope.get_overloads | field | resolver::name_resolution::NameResolver.visit | ast::pattern::EnumPatternNode*.enum_name |
| 1 | 3 | map | name | resolver::resolver::Scope.get_overloads | field | resolver::resolver::Resolver.declare_import | resolver::symbol::Symbol.name |
| 1 | 3 | scan | name | resolver::resolver::Scope.get_overloads | field | resolver::resolver::Resolver.declare_import | resolver::symbol::Symbol.name |
| 1 | 4 | map | name | resolver::resolver::Scope.get_overloads | identity | resolver::resolver::Resolver.declare_import | Symbol.name |
| 1 | 4 | scan | name | resolver::resolver::Scope.get_overloads | identity | resolver::resolver::Resolver.declare_import | Symbol.name |
| 1 | 3 | map | name | resolver::resolver::Scope.insert | call | resolver::resolver::Resolver.declare_module_symbol | resolver::decl_name::DeclName.spelling |
| 1 | 3 | map | name | resolver::resolver::Scope.insert | field | resolver::resolver::Resolver.declare | resolver::symbol::Symbol.name |
| 1 | 4 | map | name | resolver::resolver::Scope.insert | identity | resolver::resolver::Resolver.declare_module_symbol | DeclName.spelling |
| 1 | 4 | map | name | resolver::resolver::Scope.insert | identity | resolver::resolver::Resolver.declare | Symbol.name |
| 1 | 3 | map | name | resolver::resolver::Scope.insert_import | field | resolver::resolver::Resolver.declare_import | resolver::symbol::Symbol.name |
| 1 | 4 | map | name | resolver::resolver::Scope.insert_import | identity | resolver::resolver::Resolver.declare_import | Symbol.name |
| 1 | 3 | scan | name | resolver::resolver::Scope.is_ambiguous | field | resolver::name_resolution::NameResolver.visit | ast::expression::IdentifierNode*.name |
| 1 | 3 | write | name | resolver::resolver::Scope.push_entry | call | resolver::resolver::Resolver.declare_module_symbol | resolver::decl_name::DeclName.spelling |
| 1 | 3 | write | name | resolver::resolver::Scope.push_entry | field | resolver::resolver::Resolver.declare | resolver::symbol::Symbol.name |
| 1 | 3 | write | name | resolver::resolver::Scope.push_entry | field | resolver::resolver::Resolver.declare_import | resolver::symbol::Symbol.name |
| 1 | 4 | write | name | resolver::resolver::Scope.push_entry | identity | resolver::resolver::Resolver.declare_module_symbol | DeclName.spelling |
| 1 | 4 | write | name | resolver::resolver::Scope.push_entry | identity | resolver::resolver::Resolver.declare | Symbol.name |
| 1 | 4 | write | name | resolver::resolver::Scope.push_entry | identity | resolver::resolver::Resolver.declare_import | Symbol.name |
| 1 | 3 | scan | name | sema::sema::SemaVisitor.check_destructure_shape | element | sema::sema::SemaVisitor.check_destructure_shape | names |
| 2 | 3 | scan | name | sema::sema::SemaVisitor.check_destructure_shape | field | sema::sema::SemaVisitor.check_destructure_shape | ast::declaration::DestructureBinding.source_field |
| 1 | 3 | scan | name | types::generic_registry::GenericRegistry.finalize_disambiguation | field | types::generic_registry::GenericRegistry.finalize_disambiguation | types::generic_registry::TemplateEntry.declared_in |
| 1 | 3 | scan | name | types::generic_registry::GenericRegistry.finalize_disambiguation | field | types::generic_registry::GenericRegistry.finalize_disambiguation | types::generic_registry::TemplateEntry.name |

## PY-MODULEPATH: Not a lookup: module identities compared (a Python-side limitation) (16 entries, 20 weighted)

The function compares `ModulePath`s with `equals` - module identities.  `spelling-flow.py` reads a `ModulePath` comparison as a text comparison; ruling 219 leaves it so.

| n | rule | kind | key | lookup function | origin | origin function | origin text |
|---|---|---|---|---|---|---|---|
| 1 | 3 | scan | name | decl_index::DeclarationIndex.is_prelude_ns | element | resolver::name_resolution::NameResolver.refuse_unbound_type | mut owners@0=expr:ArrayLiteral |
| 1 | 3 | scan | name | decl_index::DeclarationIndex.register_prelude_namespaces | element | decl_index::DeclarationIndex.register_prelude_namespaces | prelude |
| 2 | 3 | scan | name | instance::CompilerInstance.run_project | call | instance::CompilerInstance.run_project | module_graph::ModuleInfo.path |
| 1 | 3 | scan | name | instance::CompilerInstance.run_project | element | instance::CompilerInstance.run_project | module_graph::ModuleInfo*.reexports<-local:src@3=element:field:compile |
| 1 | 3 | scan | name | module_graph::ModuleGraph.imports | element | resolver::name_resolution::NameResolver.refuse_unbound_type | mut owners@0=expr:ArrayLiteral |
| 3 | 3 | scan | name | module_graph::ModuleGraph.reexport_closure | call | resolver::name_resolution::NameResolver.process_import | module_graph::ModuleGraph.module_of_written_path |
| 1 | 3 | scan | name | module_graph::ModuleGraph.reexport_closure | call | resolver::name_resolution::NameResolver.reaching_import_for | module_graph::ModuleInfo.path |
| 1 | 3 | scan | name | module_graph::ModuleGraph.reexport_closure | element | module_graph::ModuleGraph.reexport_closure | module_graph::ModuleInfo*.reexports<-local:vinfo@1=call:modu |
| 1 | 3 | scan | name | module_graph::ModuleGraph.reexport_closure | field | resolver::name_resolution::NameResolver.stamp_module_scope | module_graph::ModuleSpellingMatch.found |
| 1 | 3 | scan | name | module_graph::ModuleGraph.reexport_closure | field | resolver::name_resolution::NameResolver.walk_module_rooted_type | module_graph::ModuleSpellingMatch.found |
| 1 | 3 | scan | name | resolver::name_resolution::NameResolver.reaching_import_for | element | resolver::name_resolution::NameResolver.refuse_unbound_type | mut owners@0=expr:ArrayLiteral |
| 2 | 3 | scan | name | resolver::resolver::Resolver.get_exports | call | resolver::name_resolution::NameResolver.process_import | module_graph::ModuleGraph.module_of_written_path |
| 1 | 3 | scan | name | resolver::resolver::Resolver.get_exports | call | resolver::name_resolution::NameResolver.process_import | module_graph::ModuleInfo.path |
| 1 | 3 | scan | name | resolver::resolver::Resolver.get_exports | element | resolver::name_resolution::NameResolver.visible_type_names | resolver::resolver::Resolver*.prelude_modules<-field:resolve |
| 1 | 3 | scan | name | resolver::resolver::Resolver.get_exports | element | resolver::name_resolution::NameResolver.offered_leaves | rx_all@0=call:module_graph::ModuleGraph.reexport_closure(&this, compil |
| 1 | 3 | scan | name | resolver::resolver::Resolver.get_exports | element | resolver::name_resolution::NameResolver.process_import | rx_all@0=call:module_graph::ModuleGraph.reexport_closure(&this, compil |

## OUT-FLAG: Not a lookup of a program's name: command-line flags (ruling 5) (111 entries, 125 weighted)

A flag or subcommand string is an option string, out of scope for the rules by ruling 5.

| n | rule | kind | key | lookup function | origin | origin function | origin text |
|---|---|---|---|---|---|---|---|
| 2 | 3 | match | string | cli::ArgumentParser::is_known_flag | field | cli::ParsedArgs.validate | std::collections::pair::Pair<string, cli::Argument>.first |
| 1 | 3 | scan | string | cli::ParsedArgs.get_value | literal | cli::commands::Executor::cmd_build | "build-dir" |
| 1 | 3 | scan | string | cli::ParsedArgs.get_value | literal | cli::commands::Executor::cmd_run | "build-dir" |
| 1 | 3 | scan | string | cli::ParsedArgs.get_value | literal | cli::commands::Executor::cmd_test | "build-dir" |
| 1 | 3 | scan | string | cli::ParsedArgs.get_value | literal | cli::commands::Executor::cmd_build | "codegen-threads" |
| 1 | 3 | scan | string | cli::ParsedArgs.get_value | literal | cli::commands::Executor::compile_file | "codegen-threads" |
| 1 | 3 | scan | string | cli::ParsedArgs.get_value | literal | cli::commands::Executor::cmd_test | "color" |
| 1 | 3 | scan | string | cli::ParsedArgs.get_value | literal | cli::commands::Executor::apply_profile_flags | "emit" |
| 1 | 3 | scan | string | cli::ParsedArgs.get_value | literal | cli::commands::Executor::cmd_test | "format" |
| 2 | 3 | scan | string | cli::ParsedArgs.get_value | literal | cli::commands::Executor::cmd_test | "jobs" |
| 1 | 3 | scan | string | cli::ParsedArgs.get_value | literal | cli::commands::Executor::compile_file | "o" |
| 1 | 3 | scan | string | cli::ParsedArgs.get_value | literal | cli::commands::Executor::cmd_build | "opt-level" |
| 1 | 3 | scan | string | cli::ParsedArgs.get_value | literal | cli::commands::Executor::compile_file | "opt-level" |
| 1 | 3 | scan | string | cli::ParsedArgs.get_value | literal | cli::commands::Executor::compile_file | "output" |
| 1 | 3 | scan | string | cli::ParsedArgs.get_value | literal | cli::commands::Executor::cmd_build | "panic" |
| 1 | 3 | scan | string | cli::ParsedArgs.get_value | literal | cli::commands::Executor::compile_file | "panic" |
| 1 | 3 | scan | string | cli::ParsedArgs.get_value | literal | cli::commands::Executor::apply_profile_flags | "profile" |
| 1 | 3 | scan | string | cli::ParsedArgs.get_value | literal | cli::commands::Executor::cmd_build | "stdlib" |
| 1 | 3 | scan | string | cli::ParsedArgs.get_value | literal | cli::commands::Executor::cmd_check | "stdlib" |
| 1 | 3 | scan | string | cli::ParsedArgs.get_value | literal | cli::commands::Executor::cmd_run | "stdlib" |
| 1 | 3 | scan | string | cli::ParsedArgs.get_value | literal | cli::commands::Executor::cmd_test | "stdlib" |
| 1 | 3 | scan | string | cli::ParsedArgs.get_value | literal | cli::commands::Executor::compile_file | "stdlib" |
| 1 | 3 | scan | string | cli::ParsedArgs.get_value | literal | cli::commands::Executor::cmd_build | "target" |
| 1 | 3 | scan | string | cli::ParsedArgs.get_value | literal | cli::commands::Executor::cmd_check | "target" |
| 1 | 3 | scan | string | cli::ParsedArgs.get_value | literal | cli::commands::Executor::cmd_run | "target" |
| 1 | 3 | scan | string | cli::ParsedArgs.get_value | literal | cli::commands::Executor::cmd_test | "target" |
| 1 | 3 | scan | string | cli::ParsedArgs.get_value | literal | cli::commands::Executor::compile_file | "target" |
| 1 | 3 | scan | string | cli::ParsedArgs.get_value | literal | cli::commands::Executor::vendor_register | "target" |
| 1 | 3 | scan | string | cli::ParsedArgs.get_value | literal | cli::commands::Executor::cmd_test | "timeout" |
| 1 | 3 | scan | string | cli::ParsedArgs.has_arg | literal | cli::commands::Executor::cmd_build | "build-dir" |
| 1 | 3 | scan | string | cli::ParsedArgs.has_arg | literal | cli::commands::Executor::cmd_run | "build-dir" |
| 1 | 3 | scan | string | cli::ParsedArgs.has_arg | literal | cli::commands::Executor::cmd_test | "build-dir" |
| 1 | 3 | scan | string | cli::ParsedArgs.has_arg | literal | cli::commands::Executor::cmd_build | "codegen-threads" |
| 1 | 3 | scan | string | cli::ParsedArgs.has_arg | literal | cli::commands::Executor::compile_file | "codegen-threads" |
| 1 | 3 | scan | string | cli::ParsedArgs.has_arg | literal | cli::commands::Executor::cmd_test | "color" |
| 1 | 3 | scan | string | cli::ParsedArgs.has_arg | literal | cli::commands::Executor::apply_profile_flags | "emit" |
| 1 | 3 | scan | string | cli::ParsedArgs.has_arg | literal | cli::commands::Executor::cmd_test | "format" |
| 2 | 3 | scan | string | cli::ParsedArgs.has_arg | literal | cli::commands::Executor::cmd_test | "jobs" |
| 1 | 3 | scan | string | cli::ParsedArgs.has_arg | literal | cli::commands::Executor::compile_file | "o" |
| 1 | 3 | scan | string | cli::ParsedArgs.has_arg | literal | cli::commands::Executor::cmd_build | "opt-level" |
| 1 | 3 | scan | string | cli::ParsedArgs.has_arg | literal | cli::commands::Executor::compile_file | "opt-level" |
| 1 | 3 | scan | string | cli::ParsedArgs.has_arg | literal | cli::commands::Executor::compile_file | "output" |
| 1 | 3 | scan | string | cli::ParsedArgs.has_arg | literal | cli::commands::Executor::cmd_build | "panic" |
| 1 | 3 | scan | string | cli::ParsedArgs.has_arg | literal | cli::commands::Executor::compile_file | "panic" |
| 1 | 3 | scan | string | cli::ParsedArgs.has_arg | literal | cli::commands::Executor::apply_profile_flags | "profile" |
| 1 | 3 | scan | string | cli::ParsedArgs.has_arg | literal | cli::commands::Executor::cmd_build | "stdlib" |
| 1 | 3 | scan | string | cli::ParsedArgs.has_arg | literal | cli::commands::Executor::cmd_check | "stdlib" |
| 1 | 3 | scan | string | cli::ParsedArgs.has_arg | literal | cli::commands::Executor::cmd_run | "stdlib" |
| 1 | 3 | scan | string | cli::ParsedArgs.has_arg | literal | cli::commands::Executor::cmd_test | "stdlib" |
| 1 | 3 | scan | string | cli::ParsedArgs.has_arg | literal | cli::commands::Executor::compile_file | "stdlib" |
| 1 | 3 | scan | string | cli::ParsedArgs.has_arg | literal | cli::commands::Executor::cmd_build | "target" |
| 1 | 3 | scan | string | cli::ParsedArgs.has_arg | literal | cli::commands::Executor::cmd_check | "target" |
| 1 | 3 | scan | string | cli::ParsedArgs.has_arg | literal | cli::commands::Executor::cmd_run | "target" |
| 1 | 3 | scan | string | cli::ParsedArgs.has_arg | literal | cli::commands::Executor::cmd_test | "target" |
| 1 | 3 | scan | string | cli::ParsedArgs.has_arg | literal | cli::commands::Executor::compile_file | "target" |
| 1 | 3 | scan | string | cli::ParsedArgs.has_arg | literal | cli::commands::Executor::vendor_register | "target" |
| 1 | 3 | scan | string | cli::ParsedArgs.has_arg | literal | cli::commands::Executor::cmd_test | "timeout" |
| 1 | 3 | scan | string | cli::ParsedArgs.has_flag | literal | cli::commands::Executor::cmd_build | "ast" |
| 1 | 3 | scan | string | cli::ParsedArgs.has_flag | literal | cli::commands::Executor::cmd_check | "ast" |
| 1 | 3 | scan | string | cli::ParsedArgs.has_flag | literal | cli::commands::Executor::cmd_raw | "ast" |
| 1 | 3 | scan | string | cli::ParsedArgs.has_flag | literal | cli::commands::Executor::cmd_run | "ast" |
| 1 | 3 | scan | string | cli::ParsedArgs.has_flag | literal | cli::commands::Executor::compile_file | "ast" |
| 2 | 3 | scan | string | cli::ParsedArgs.has_flag | literal | cli::commands::Executor::cmd_build | "debug" |
| 2 | 3 | scan | string | cli::ParsedArgs.has_flag | literal | cli::commands::Executor::cmd_check | "debug" |
| 2 | 3 | scan | string | cli::ParsedArgs.has_flag | literal | cli::commands::Executor::cmd_raw | "debug" |
| 2 | 3 | scan | string | cli::ParsedArgs.has_flag | literal | cli::commands::Executor::cmd_run | "debug" |
| 2 | 3 | scan | string | cli::ParsedArgs.has_flag | literal | cli::commands::Executor::cmd_test | "debug" |
| 1 | 3 | scan | string | cli::ParsedArgs.has_flag | literal | cli::commands::Executor::cmd_vendor | "debug" |
| 2 | 3 | scan | string | cli::ParsedArgs.has_flag | literal | cli::commands::Executor::compile_file | "debug" |
| 1 | 3 | scan | string | cli::ParsedArgs.has_flag | literal | cli::commands::Executor::run_dep_resolve | "debug" |
| 1 | 3 | scan | string | cli::ParsedArgs.has_flag | literal | cli::commands::Executor::cmd_build | "debug-info" |
| 1 | 3 | scan | string | cli::ParsedArgs.has_flag | literal | cli::commands::Executor::compile_file | "debug-info" |
| 1 | 3 | scan | string | cli::ParsedArgs.has_flag | literal | cli::commands::Executor::apply_profile_flags | "dev" |
| 1 | 3 | scan | string | cli::ParsedArgs.has_flag | literal | cli::commands::Executor::cmd_build | "emit-llvm" |
| 1 | 3 | scan | string | cli::ParsedArgs.has_flag | literal | cli::commands::Executor::cmd_run | "emit-llvm" |
| 1 | 3 | scan | string | cli::ParsedArgs.has_flag | literal | cli::commands::Executor::compile_file | "emit-llvm" |
| 1 | 3 | scan | string | cli::ParsedArgs.has_flag | literal | cli::commands::Executor::cmd_test | "exact" |
| 1 | 3 | scan | string | cli::ParsedArgs.has_flag | literal | cli::commands::Executor::cmd_build | "g" |
| 1 | 3 | scan | string | cli::ParsedArgs.has_flag | literal | cli::commands::Executor::compile_file | "g" |
| 1 | 3 | scan | string | cli::ParsedArgs.has_flag | literal | cli::commands::Executor::cmd_test | "ignored" |
| 3 | 3 | scan | string | cli::ParsedArgs.has_flag | literal | cli::commands::Executor::cmd_test | "list" |
| 1 | 3 | scan | string | cli::ParsedArgs.has_flag | literal | cli::commands::Executor::apply_profile_flags | "locked" |
| 1 | 3 | scan | string | cli::ParsedArgs.has_flag | literal | cli::commands::Executor::apply_profile_flags | "no-incremental" |
| 1 | 3 | scan | string | cli::ParsedArgs.has_flag | literal | cli::commands::Executor::cmd_build | "no-runtime" |
| 1 | 3 | scan | string | cli::ParsedArgs.has_flag | literal | cli::commands::Executor::compile_file | "no-runtime" |
| 1 | 3 | scan | string | cli::ParsedArgs.has_flag | literal | cli::commands::Executor::cmd_build | "no-std" |
| 1 | 3 | scan | string | cli::ParsedArgs.has_flag | literal | cli::commands::Executor::compile_file | "no-std" |
| 1 | 3 | scan | string | cli::ParsedArgs.has_flag | literal | cli::commands::Executor::cmd_test | "nocapture" |
| 1 | 3 | scan | string | cli::ParsedArgs.has_flag | literal | cli::commands::Executor::cmd_test | "q" |
| 1 | 3 | scan | string | cli::ParsedArgs.has_flag | literal | cli::commands::Executor::cmd_test | "quiet" |
| 1 | 3 | scan | string | cli::ParsedArgs.has_flag | literal | cli::commands::Executor::apply_profile_flags | "release" |
| 1 | 3 | scan | string | cli::ParsedArgs.has_flag | literal | cli::commands::Executor::apply_profile_flags | "release-static" |
| 1 | 3 | scan | string | cli::ParsedArgs.has_flag | literal | cli::commands::Executor::cmd_test | "show-output" |
| 1 | 3 | scan | string | cli::ParsedArgs.has_flag | literal | cli::commands::Executor::execute | "triple" |
| 2 | 3 | scan | string | cli::ParsedArgs.set_arg | call | cli::ArgumentParser::parse | cli::ArgumentParser::normalize_flag_name |
| 1 | 3 | scan | string | cli::ParsedArgs.set_define | expr | cli::ArgumentParser::parse | BinaryExpression |
| 1 | 3 | scan | string | cli::ParsedArgs.set_flag | call | cli::ArgumentParser::parse | cli::ArgumentParser::normalize_flag_name |
| 1 | 3 | scan | string | cli::ParsedArgs.validate | literal | cli::ParsedArgs.validate | "" |
| 1 | 3 | scan | string | cli::ParsedArgs.validate | literal | cli::ParsedArgs.validate | "emit" |
| 1 | 3 | scan | string | cli::ParsedArgs.validate | literal | cli::ParsedArgs.validate | "facts" |
| 1 | 3 | scan | string | cli::ParsedArgs.validate | literal | cli::ParsedArgs.validate | "o" |
| 1 | 3 | scan | string | cli::ParsedArgs.validate | literal | cli::ParsedArgs.validate | "output" |
| 1 | 3 | scan | string | cli::ParsedArgs.validate | literal | cli::ParsedArgs.validate | "target" |
| 1 | 3 | write | string | cli::Runner.add_command | field | cli::Runner.add_command | cli::Command.name |
| 1 | 3 | map | string | cli::Runner.print_command_help | element | cli::Runner.run | argv |
| 1 | 3 | map | string | cli::Runner.print_help | element | cli::Runner.print_help | cli::Runner.command_order<-param:this |
| 1 | 3 | scan | string | cli::Runner.run | literal | cli::Runner.run | "--debug" |
| 1 | 3 | scan | string | cli::Runner.run | literal | cli::Runner.run | "--help" |
| 1 | 3 | scan | string | cli::Runner.run | literal | cli::Runner.run | "-h" |
| 3 | 3 | match | string | cli::commands::Executor::resolve | element | cli::Runner.run | argv |
| 1 | 3 | match | string | cli::commands::FlagKind::resolve | call | cli::commands::FlagKind::resolve | cli::ArgumentParser::normalize_flag_name |

## OUT-CONFIG: Not a lookup of a program's name: configuration, manifest, lock-file and vendor keys (39 entries, 58 weighted)

Keys of the build's own files - `cryoconfig` sections and values, dependency and vendor names, cache and object paths, a test's requirement words - name no declaration of a program.

| n | rule | kind | key | lookup function | origin | origin function | origin text |
|---|---|---|---|---|---|---|---|
| 1 | 3 | scan | string | cli::commands::Executor::proj_first_unknown_requirement | literal | cli::commands::Executor::proj_first_unknown_requirement | "cxx" |
| 1 | 3 | scan | string | cli::commands::Executor::proj_first_unknown_requirement | literal | cli::commands::Executor::proj_first_unknown_requirement | "display" |
| 1 | 3 | scan | string | cli::commands::Executor::vendor_rebuild | call | cli::commands::Executor::cmd_vendor | cli::ParsedArgs.positional_at |
| 1 | 3 | scan | string | cli::commands::Executor::vendor_register | call | cli::commands::Executor::vendor_register | vendor::registry::VendorRegistry::normalize_triple |
| 1 | 3 | scan | string | build_manifest::ModuleKeyTable.lookup | element | instance::CompilerInstance.run_project | mut pm_key_names@0=expr:ArrayLiteral |
| 1 | 3 | map | string | deps::dep_resolver::DepResolver::resolve_git_dep | field | deps::dep_resolver::DepResolver::resolve_git_dep | deps::dep_resolver::DepKey.val |
| 1 | 3 | map | string | deps::dep_resolver::DepResolver::resolve_path_dep | field | deps::dep_resolver::DepResolver::resolve_path_dep | deps::dep_resolver::DepKey.val |
| 2 | 3 | scan | string | deps::lockfile::Lockfile.find | field | deps::dep_resolver::DepResolver::resolve_git_dep | project_config::Dependency.name |
| 1 | 3 | scan | string | instance::CompilerInstance.run_project | element | instance::CompilerInstance.run_project | mut cg_obj@0=expr:ArrayLiteral |
| 1 | 3 | scan | string | instance::CompilerInstance.run_project | element | instance::CompilerInstance.run_project | mut pm_key_hex@0=expr:ArrayLiteral |
| 2 | 3 | scan | string | instance::CompilerInstance.run_project | literal | instance::CompilerInstance.run_project | "" |
| 4 | 3 | scan | string | instance::CompilerInstance.run_project | literal | instance::CompilerInstance.run_project | null |
| 1 | 3 | scan | string | instance::CompilerInstance::sync_vendor_lock | field | instance::CompilerInstance::sync_vendor_lock | deps::lockfile::LockedVendor.binding_sha |
| 1 | 3 | scan | string | instance::CompilerInstance::sync_vendor_lock | field | instance::CompilerInstance::sync_vendor_lock | deps::lockfile::LockedVendor.name |
| 1 | 3 | scan | string | instance::CompilerInstance::sync_vendor_lock | field | instance::CompilerInstance::sync_vendor_lock | deps::lockfile::LockedVendor.triple |
| 1 | 3 | scan | string | module_loader::ModuleLoader.note_vendor_import | field | module_loader::ModuleLoader.note_vendor_import | vendor::registry::VendorEntry.key |
| 1 | 3 | scan | string | module_loader::ModuleLoader.triple_for_lib | expr | module_loader::ModuleLoader.resolve_import_path | BinaryExpression |
| 1 | 3 | scan | string | module_loader::ModuleLoader.triple_for_lib | field | module_loader::ModuleLoader.collect_vendor_locks | vendor::registry::VendorEntry.name |
| 1 | 3 | scan | string | module_loader::ModuleLoader.triple_for_lib | literal | module_loader::ModuleLoader.triple_for_lib | "" |
| 13 | 3 | match | string | project_config::ProjectConfig::parse | call | project_config::ProjectConfig::parse | string.trim |
| 1 | 3 | match | string | project_config::ProjectConfig::parse | literal | project_config::ProjectConfig::parse | "" |
| 1 | 3 | match | string | project_config::ProjectConfig::parse | literal | project_config::ProjectConfig::parse | 1 |
| 1 | 3 | match | string | project_config::ProjectConfig::parse | literal | project_config::ProjectConfig::parse | 2 |
| 1 | 3 | match | string | project_config::ProjectConfig::parse_bool | call | project_config::ProjectConfig::parse_bool | utils::text::Text.as_string |
| 1 | 3 | match | string | project_config::ProjectConfig::parse_opt_level | call | project_config::ProjectConfig::parse_opt_level | utils::text::Text.as_string |
| 1 | 3 | scan | string | vendor::registry::VendorEntry.cache_file_for | call | vendor::registry::VendorEntry.cache_file_for | utils::text::Text.as_string |
| 1 | 3 | scan | string | vendor::registry::VendorEntry.cache_key_for | call | vendor::auto_vendor::AutoVendor::ensure_one | vendor::registry::VendorRegistry::normalize_triple |
| 3 | 3 | scan | string | vendor::registry::VendorEntry.set_cache | call | cli::commands::Executor::vendor_register | vendor::registry::VendorRegistry::normalize_triple |
| 1 | 3 | scan | string | vendor::registry::VendorEntry.set_cache | call | vendor::auto_vendor::AutoVendor::ensure_one | vendor::registry::VendorRegistry::normalize_triple |
| 1 | 3 | scan | string | vendor::registry::VendorRegistry.find_index | call | cli::commands::Executor::cmd_vendor | cli::ParsedArgs.positional_at |
| 1 | 3 | scan | string | vendor::registry::VendorRegistry.find_index | call | cli::commands::Executor::vendor_register | cli::commands::Executor::to_lower |
| 1 | 3 | scan | string | vendor::registry::VendorRegistry.find_index | element | module_loader::ModuleLoader.collect_vendor_locks | module_loader::ModuleLoader.used_vendor_keys<-param:this |
| 1 | 3 | scan | string | vendor::registry::VendorRegistry.find_index | element | module_loader::ModuleLoader.merge_vendor_links | module_loader::ModuleLoader.used_vendor_keys<-param:this |
| 1 | 3 | scan | string | vendor::registry::VendorRegistry.find_index | expr | cli::commands::Executor::proj_requirement_met | BinaryExpression |
| 1 | 3 | scan | string | vendor::registry::VendorRegistry.find_index | expr | module_loader::ModuleLoader.note_vendor_import | BinaryExpression |
| 1 | 3 | scan | string | vendor::registry::VendorRegistry.find_index | expr | module_loader::ModuleLoader.resolve_import_path | BinaryExpression |
| 1 | 3 | scan | string | vendor::registry::VendorRegistry.find_index | field | instance::CompilerInstance.run_project | module_graph::ModuleInfo*.origin_name |
| 1 | 3 | scan | string | vendor::registry::VendorRegistry.find_index | field | vendor::auto_vendor::AutoVendor::ensure_pins | project_config::VendorPin.name |
| 1 | 3 | scan | string | vendor::registry::VendorRegistry.upsert | field | vendor::registry::VendorRegistry.upsert | vendor::registry::VendorEntry.key |

## OUT-TEXT: Not a lookup of a program's name: file paths, messages and literal contents (23 entries, 26 weighted)

Source file paths, diagnostic text and its dedupe, a string literal's contents, a C header's emitted names, the empty string: text that names no declaration.

| n | rule | kind | key | lookup function | origin | origin function | origin text |
|---|---|---|---|---|---|---|---|
| 2 | 3 | scan | string | cli::commands::Executor::check_annotations | field | cli::commands::Executor::check_annotations | cli::commands::NegExpect*.code |
| 2 | 3 | scan | string | cli::commands::Executor::check_annotations | field | cli::commands::Executor::check_annotations | cli::commands::NegExpect*.severity |
| 1 | 3 | scan | string | bindgen::generator::BindingSerializer::contains | call | bindgen::generator::BindingSerializer::render | bindgen::generator::BindingSerializer::decl_name |
| 2 | 3 | scan | string | bindgen::generator::BindingSerializer::contains | call | bindgen::generator::BindingSerializer::render | std::fmt::format |
| 1 | 3 | scan | string | bindgen::generator::VendorGenerator::dedup_in_place | element | bindgen::generator::VendorGenerator::dedup_in_place | mut out@0=expr:ArrayLiteral |
| 1 | 3 | scan | string | codegen::state::diag_sink::DiagSink.is_stripped | call | codegen::state::diag_sink::DiagSink.is_stripped | resolver::mangled_name::MangledName.as_string |
| 1 | 3 | scan | string | codegen::state::string_cache::StringCache.get_or_create | call | codegen::visit::pattern_emitter::PatternEmitter.emit_match_arm_comparison | codegen::context::CodegenContext.resolve |
| 1 | 3 | scan | string | codegen::state::string_cache::StringCache.get_or_create | field | codegen::ops::declaration_emitter::DeclarationEmitter.codegen_global_var | ast::expression::LiteralNode*.value |
| 1 | 3 | scan | string | codegen::state::string_cache::StringCache.get_or_create | field | codegen::visit::ir_generator::IRGeneratorVisitor.codegen_literal | ast::expression::LiteralNode*.value |
| 1 | 3 | scan | string | diag::renderer::DiagRenderer.collect_same_file_labels | field | diag::renderer::DiagRenderer.collect_same_file_labels | diag::source_span::SourceSpan.file |
| 1 | 3 | map | string | diag::renderer::DiagRenderer.get_content | call | diag::renderer::DiagRenderer.get_content | utils::text::Text.as_string |
| 1 | 3 | scan | string | diag::renderer::DiagRenderer.has_same_file_secondary | field | diag::renderer::DiagRenderer.has_same_file_secondary | diag::source_span::SourceSpan.file |
| 1 | 3 | map | string | diag::renderer::DiagRenderer.has_source | call | diag::renderer::DiagRenderer.has_source | utils::text::Text.as_string |
| 1 | 3 | write | string | diag::renderer::DiagRenderer.load_source | call | diag::renderer::DiagRenderer.load_source | utils::text::Text.as_string |
| 1 | 3 | scan | string | diag::renderer::DiagRenderer.primary_label_caption | literal | diag::renderer::DiagRenderer.primary_label_caption | "" |
| 1 | 3 | scan | string | diag::renderer::DiagRenderer.render_body | field | diag::renderer::DiagRenderer.render_body | diag::source_span::SourceSpan.file |
| 1 | 3 | map | string | diag::sink::DiagnosticSink.emit | call | diag::sink::DiagnosticSink.emit | std::fmt::format |
| 1 | 3 | write | string | diag::sink::DiagnosticSink.emit | call | diag::sink::DiagnosticSink.emit | std::fmt::format |
| 1 | 3 | scan | string | diag::sink::DiagnosticSink.is_vendor_file | call | diag::sink::DiagnosticSink.is_vendor_file | utils::text::Text.as_string |
| 1 | 3 | scan | string | diag::sink::DiagnosticSink.mark_vendor_file | call | diag::sink::DiagnosticSink.mark_vendor_file | utils::text::Text.as_string |
| 1 | 3 | scan | string | passes::type_lowering::TypeLoweringPasses::run | literal | passes::type_lowering::TypeLoweringPasses::run | "" |
| 1 | 3 | write | string | resolver::intern_table::InternTable::new | literal | resolver::intern_table::InternTable::new | "" |
| 1 | 3 | scan | string | sema::diagnostics::Diagnostics.attach_shadow_import_suggestions | literal | sema::diagnostics::Diagnostics.attach_shadow_import_suggestions | "" |

## OUT-FIXED: Not a lookup of a program's name: the language's own fixed spellings (23 entries, 23 weighted)

Keywords, numeric suffixes, directive and builtin names, the lint's own name, OS atoms, asm dialects, demangler operator codes, the fixed language-module table: spellings the language defines, which no program declares.

| n | rule | kind | key | lookup function | origin | origin function | origin text |
|---|---|---|---|---|---|---|---|
| 1 | 3 | match | string | lex::TokenType::from_keyword | call | lex::TokenType::from_keyword | utils::keyword::Keyword.as_string |
| 1 | 3 | scan | string | lex::lexer::Lexer::is_valid_numeric_suffix | call | lex::lexer::Lexer::is_valid_numeric_suffix | utils::keyword::Keyword.as_string |
| 1 | 3 | scan | string | parser::parser::Parser.parse_asm_block | field | parser::parser::Parser.parse_asm_block | lex::Token.lexeme |
| 1 | 3 | scan | string | parser::parser::Parser.parse_asm_block | literal | parser::parser::Parser.parse_asm_block | "" |
| 1 | 3 | scan | string | parser::parser::Parser.parse_asm_block | literal | parser::parser::Parser.parse_asm_block | "intel" |
| 1 | 3 | match | string | passes::config_gating::HostOS::is_os_atom | call | passes::config_gating::HostOS::is_os_atom | utils::keyword::Keyword.as_string |
| 1 | 3 | scan | string | passes::directive_processing::DirectiveProcessingPasses::allows_spelling | global | passes::directive_processing::DirectiveProcessingPasses::allows_spelling | passes::directive_processing::LOOKUP_BY_SPELLING |
| 1 | 3 | match | string | passes::directive_processing::DirectiveProcessingPasses::apply_layout_effects_class | call | passes::directive_processing::DirectiveProcessingPasses::apply_layout_effects_class | utils::keyword::Keyword.as_string |
| 1 | 3 | match | string | passes::directive_processing::DirectiveProcessingPasses::apply_layout_effects_class | field | passes::directive_processing::DirectiveProcessingPasses::apply_layout_effects_class | ast::pattern::DirectiveArg.name |
| 1 | 3 | match | string | passes::directive_processing::DirectiveProcessingPasses::apply_layout_effects_enum | call | passes::directive_processing::DirectiveProcessingPasses::apply_layout_effects_enum | utils::keyword::Keyword.as_string |
| 1 | 3 | match | string | passes::directive_processing::DirectiveProcessingPasses::apply_layout_effects_struct | call | passes::directive_processing::DirectiveProcessingPasses::apply_layout_effects_struct | utils::keyword::Keyword.as_string |
| 1 | 3 | match | string | passes::directive_processing::DirectiveProcessingPasses::apply_layout_effects_struct | field | passes::directive_processing::DirectiveProcessingPasses::apply_layout_effects_struct | ast::pattern::DirectiveArg.name |
| 1 | 3 | match | string | passes::directive_processing::DirectiveProcessingPasses::apply_layout_effects_union | call | passes::directive_processing::DirectiveProcessingPasses::apply_layout_effects_union | utils::keyword::Keyword.as_string |
| 1 | 3 | match | string | passes::directive_processing::DirectiveProcessingPasses::apply_layout_effects_union | field | passes::directive_processing::DirectiveProcessingPasses::apply_layout_effects_union | ast::pattern::DirectiveArg.name |
| 1 | 3 | match | string | passes::directive_processing::DirectiveProcessingPasses::is_cfg_os_atom | call | passes::directive_processing::DirectiveProcessingPasses::is_cfg_os_atom | utils::keyword::Keyword.as_string |
| 1 | 3 | match | string | passes::directive_processing::DirectiveProcessingPasses::is_known_builtin | call | passes::directive_processing::DirectiveProcessingPasses::is_known_builtin | utils::keyword::Keyword.as_string |
| 1 | 3 | scan | string | passes::directive_processing::DirectiveProcessingPasses::run_directive_processing | literal | passes::directive_processing::DirectiveProcessingPasses::run_directive_processing | "testing" |
| 1 | 3 | match | string | passes::directive_processing::DirectiveProcessingPasses::validate_builtin | call | passes::directive_processing::DirectiveProcessingPasses::validate_builtin | utils::keyword::Keyword.as_string |
| 1 | 3 | match | string | passes::directive_processing::DirectiveProcessingPasses::validate_builtin | field | passes::directive_processing::DirectiveProcessingPasses::validate_builtin | ast::pattern::DirectiveArg.name |
| 1 | 3 | scan | string | passes::directive_processing::DirectiveProcessingPasses::validate_builtin | global | passes::directive_processing::DirectiveProcessingPasses::validate_builtin | passes::directive_processing::LOOKUP_BY_SPELLING |
| 1 | 3 | scan | string | passes::directive_processing::DirectiveProcessingPasses::validate_builtin | literal | passes::directive_processing::DirectiveProcessingPasses::validate_builtin | "reason" |
| 1 | 3 | scan | string | passes::pass_registry::PassRegistry::run_auto_import_pass | call | passes::pass_registry::PassRegistry::run_auto_import_pass | resolver::lang_item::LangModule.path |
| 1 | 3 | match | string | resolver::demangler::Cursor::op_code_to_symbol | field | resolver::demangler::Demangler::demangle | resolver::demangler::ParsedPath.member |

## GAP-STRING: A lookup the compiler does not see: a written name handed on as a `string` (3 entries, 3 weighted)

A declaration's or identifier's written name is matched against a fixed table in a function that takes it as `string`.  The compiler's rule-three check seeds only `SymbolStr`/`QualifiedName` parameters and fields, so a name handed across a call as `string` is not followed.  The tables are the kind the `primitive`/`lang-item` doors are for; the functions are not marked doors.

| n | rule | kind | key | lookup function | origin | origin function | origin text |
|---|---|---|---|---|---|---|---|
| 1 | 3 | match | string | intrinsic_kind::IntrinsicKind::from_name | field | passes::type_resolution::TypeResolutionPasses::register_decl_in_index | ast::declaration::FunctionDeclNode*.name |
| 1 | 3 | match | string | intrinsic_kind::IntrinsicKind::from_name | field | passes::type_resolution::TypeResolutionPasses::run_function_signature | ast::declaration::IntrinsicDeclNode*.name |
| 1 | 3 | match | string | resolver::res::SourceLoc::of_spelling | field | resolver::name_resolution::NameResolver.visit | ast::expression::IdentifierNode*.name |

## GAP-LOADER: Not seen by the compiler: module discovery's text, before any module exists (14 entries, 18 weighted)

Module discovery matches written import paths and lower-cased file paths as `string`s before the graph holds a module to be their identity (the loader's own stores carry written reasons for this).  The compiler's check does not follow `string`; whether discovery's path matching needs a door of its own is open.

| n | rule | kind | key | lookup function | origin | origin function | origin text |
|---|---|---|---|---|---|---|---|
| 5 | 3 | map | string | module_loader::ModuleLoader.discover_module | call | module_loader::ModuleLoader.discover_module | string.to_ascii_lower |
| 1 | 3 | write | string | module_loader::ModuleLoader.discover_module | call | module_loader::ModuleLoader.discover_module | string.to_ascii_lower |
| 1 | 3 | write | string | module_loader::ModuleLoader.discover_module | expr | module_loader::ModuleLoader.discover_module | IfExpression |
| 1 | 3 | scan | string | module_loader::ModuleLoader.is_loaded | call | module_loader::ModuleLoader.discover_module | string.to_ascii_lower |
| 1 | 3 | scan | string | module_loader::ModuleLoader.is_loading | call | module_loader::ModuleLoader.discover_module | string.to_ascii_lower |
| 1 | 3 | scan | string | module_loader::ModuleLoader.pop_loading | call | module_loader::ModuleLoader.discover_module | string.to_ascii_lower |
| 1 | 3 | map | string | module_loader::ModuleLoader.resolve_import_path | element | module_loader::ModuleLoader.discover_module | module_loader::ModuleScanResult.export_items<-local:scan@0=call:compil |
| 1 | 3 | map | string | module_loader::ModuleLoader.resolve_import_path | element | module_loader::ModuleLoader.discover_module | module_loader::ModuleScanResult.export_paths<-local:scan@0=call:compil |
| 1 | 3 | map | string | module_loader::ModuleLoader.resolve_import_path | element | module_loader::ModuleLoader.discover_module | module_loader::ModuleScanResult.import_items<-local:scan@0=call:compil |
| 1 | 3 | map | string | module_loader::ModuleLoader.resolve_import_path | element | module_loader::ModuleLoader.discover_module | module_loader::ModuleScanResult.import_paths<-local:scan@0=call:compil |
| 1 | 3 | map | string | module_loader::ModuleLoader.resolve_import_path | element | module_loader::ModuleLoader.discover_module | module_loader::ModuleScanResult.submodule_paths<-local:scan@0=call:com |
| 1 | 3 | map | string | module_loader::ModuleLoader.resolve_import_path | field | passes::pass_registry::PassRegistry::run_import_resolution_pass | ast::declaration::ImportDeclNode*.module_path |
| 1 | 3 | write | string | module_loader::ModuleLoader.scan_file_namespace | call | module_loader::ModuleLoader.scan_all_project_files | std::fmt::format |
| 1 | 3 | write | string | module_loader::ModuleLoader.scan_file_namespace | field | module_loader::ModuleLoader.scan_file_namespace | module_loader::ModuleScanResult.namespace_name |

## GAP-ORDER: A lookup the compiler does not see: a spelling kept for a later loop iteration (2 entries, 2 weighted)

A duplicate-declaration check pushes each spelling's number into a local array after the comparison that reads the array.  The check follows a function once, in source order, so the push is recorded after the comparison is judged.  A push before the comparison is refused (`spelling_lint_follows_text_and_numbers`).

| n | rule | kind | key | lookup function | origin | origin function | origin text |
|---|---|---|---|---|---|---|---|
| 1 | 3 | scan | name | passes::ast_validation::ASTValidator.check_duplicate_fields | field | passes::ast_validation::ASTValidator.check_duplicate_fields | ast::declaration::FieldDeclNode*.name |
| 1 | 3 | scan | name | passes::type_resolution::TypeResolutionPasses::run_function_signature | field | passes::type_resolution::TypeResolutionPasses::run_function_signature | ast::declaration::FunctionDeclNode*.name |

## GAP-IDTEXT: A lookup the compiler does not see: identity text that is not a `DeclName` (9 entries, 12 weighted)

Text computed from an identity - the type arena's display names, a mangled link symbol - reaches the interner or a comparison.  The rule-four check follows `DeclName` text only; these are plain `string`s or `SymbolStr`s.

| n | rule | kind | key | lookup function | origin | origin function | origin text |
|---|---|---|---|---|---|---|---|
| 1 | 4 | door:intern | string | ast::substituter::ASTTypeSubstituter.arg_label | identity | ast::substituter::ASTTypeSubstituter.arg_label | TypeArena.resolve_display_name |
| 2 | 4 | door:intern | string | ast::substituter::ASTTypeSubstituter.substitute_named_annotation | identity | ast::substituter::ASTTypeSubstituter.substitute_named_annotation | TypeArena.resolve_display_name |
| 1 | 3 | scan | name | codegen::ops::symbol_resolver::SymbolResolver.family_answer | call | codegen::ops::symbol_resolver::SymbolResolver.family_answer | decl_index::DeclarationIndex.entry_symbol |
| 1 | 3 | scan | name | codegen::ops::symbol_resolver::SymbolResolver.family_answer | call | codegen::ops::symbol_resolver::SymbolResolver.family_answer | resolver::symbol_str::SymbolStr::empty |
| 1 | 4 | scan | string | codegen::state::diag_sink::DiagSink.is_stripped | identity | codegen::state::diag_sink::DiagSink.is_stripped | MangledName.as_string |
| 1 | 4 | door:intern | string | sema::async_lower::AsyncLower.make_type_ann | identity | sema::async_lower::AsyncLower.make_type_ann | TypeArena.resolve_display_name |
| 2 | 4 | door:intern | string | sema::sema::SemaVisitor.resolve_struct_literal | identity | sema::sema::SemaVisitor.resolve_struct_literal | TypeArena.format_display |
| 2 | 4 | door:intern | string | sema::sema::SemaVisitor.visit | identity | sema::sema::SemaVisitor.visit | TypeArena.format_display |
| 1 | 4 | door:intern | string | sema::type_utils::TypeUtils.type_display_name | identity | sema::type_utils::TypeUtils.type_display_name | TypeArena.format_display |

## ALLOWED: Not refused, and allowed with a reason (2 entries, 2 weighted)

The function carries an allow whose reason names what it does; removing the allow draws no refusal, because the text it is handed is not a `DeclName` or arrives as a `string`.

| n | rule | kind | key | lookup function | origin | origin function | origin text |
|---|---|---|---|---|---|---|---|
| 1 | 4 | door:intern | string | decl_index::DeclarationIndex.register_methods_through | identity | decl_index::DeclarationIndex.register_methods_through | MangledName.as_string |
| 1 | 4 | door:modules_in_view | string | module_graph::ModuleGraph.modules_written_as | identity | resolver::name_resolution::NameResolver.refuse_module_type_collisions | Symbol.name |

## The other direction: functions the compiler refuses that the list does not name (19)

With its allow removed, each of these is refused by the compiler; no entry of the list has it as its lookup function.  The list is therefore no superset of the compiler's check either.

* `bindgen::generator::BindingSerializer.render_extern_global`
* `bindgen::importer::Importer.is_void_annotation`
* `codegen::ops::symbol_resolver::SymbolResolver.owned_by_current_module`
* `codegen::state::function_registry::FunctionRegistry.register`
* `decl_index::DeclarationIndex.check_method_def`
* `module_graph::ModuleGraph.modules_abbreviated_by`
* `module_graph::ModulePath.equals`
* `module_graph::ModulePath.is_within`
* `passes::dead_code::DeadCodeChecker.check_local_decl`
* `passes::specialization::SpecializationPasses.route_specializations_to_owners`
* `resolver::mangled_name::MangledName.specialized_identifier`
* `resolver::qualified_name::QualifiedName.single`
* `resolver::symbol_str::SymbolStr.equals`
* `sema::call_facts::CallFacts.key_of`
* `sema::call_facts::CallFacts.spelling_of`
* `sema::call_resolver::CallResolver.leaves_print_alike`
* `sema::member_resolver::MemberResolver.template_field`
* `types::generic_registry::GenericRegistry.annotation_unifies`
* `types::generic_registry::GenericRegistry.head_unify`

