#include "Codegen/Declarations/GenericCodegen.hpp"
#include "Codegen/Declarations/DeclarationCodegen.hpp"
#include "Codegen/Declarations/TypeCodegen.hpp"
#include "Codegen/CodegenVisitor.hpp"
#include "Types/CompoundTypes.hpp"
#include "Types/GenericTypes.hpp"
#include "Types/UserDefinedTypes.hpp"
#include "Types/TypeAnnotation.hpp"
#include "Types/ErrorType.hpp"
#include "Utils/Logger.hpp"

#include <llvm/IR/Verifier.h>
#include <unordered_set>

namespace Cryo::Codegen
{
    //===================================================================
    // Construction
    //===================================================================

    GenericCodegen::GenericCodegen(CodegenContext &ctx)
        : ICodegenComponent(ctx)
    {
    }

    //===================================================================
    // Instantiation Context Helper
    //===================================================================

    void GenericCodegen::set_instantiation_context_from_type(
        const std::string &generic_name,
        const std::vector<TypeRef> &type_args)
    {
        // If an external caller (CallCodegen, ExpressionCodegen) already set
        // the instantiation source, don't overwrite it — the outermost call
        // site is the one the user cares about.
        if (!ctx().instantiation_file().empty())
            return;

        // Build the display name the same way InstantiatedType::display_name() does:
        //   "Array<String>", "HashMap<string, String>", etc.
        std::string display = generic_name + "<";
        for (size_t i = 0; i < type_args.size(); ++i)
        {
            if (i > 0)
                display += ", ";
            display += type_args[i].is_valid() ? type_args[i]->display_name() : "<invalid>";
        }
        display += ">";

        // Look up the dedicated call-site map.  This was populated during
        // TypeResolution (Stage 4) via GenericRegistry::instantiate() and is
        // immune to the TypeRef overwrites that happen in later stages.
        std::string site_file;
        SourceLocation site_loc;
        auto &arena = ctx().symbols().arena();

        if (arena.get_instantiation_site(display, site_file, site_loc))
        {
            ctx().set_instantiation_source(site_file, site_loc);

            // Also register under the mangled name so child types (e.g.,
            // HashMapIter<K,V> instantiated while generating HashMap<K,V>
            // methods) can inherit this call site via current_type_name().
            std::string mangled = mangle_type_name(generic_name, type_args);
            arena.store_instantiation_site(mangled, site_file, site_loc);
            return;
        }

        // Direct lookup failed — this type was created during monomorphization
        // (not TypeResolution), so there's no direct call site.  Inherit the
        // call site of the parent type whose methods triggered this instantiation.
        const std::string &parent = ctx().current_type_name();
        if (!parent.empty() && arena.get_instantiation_site(parent, site_file, site_loc))
        {
            ctx().set_instantiation_source(site_file, site_loc);
            return;
        }
    }

    //===================================================================
    // Generic Type Instantiation
    //===================================================================

    llvm::StructType *GenericCodegen::instantiate_struct(const std::string &generic_name,
                                                         const std::vector<TypeRef> &type_args)
    {
        // Check if any type arguments are uninstantiated type parameters
        // This happens when compiling generic templates where K, V, T etc. aren't yet concrete
        for (const auto &arg : type_args)
        {
            if (!arg)
                continue;

            // TypeKind::GenericParam means it's an uninstantiated type parameter
            if (arg->kind() == TypeKind::GenericParam)
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "GenericCodegen: Skipping instantiation of {} - type arg '{}' is a generic parameter",
                          generic_name, arg.get()->display_name());
                return nullptr;
            }

            // Also check if this would map to an opaque/undefined struct
            // This catches cases where type parameters are misclassified as Struct types
            if (arg->kind() == TypeKind::Struct || arg->kind() == TypeKind::Class)
            {
                llvm::StructType *existing = llvm::StructType::getTypeByName(llvm_ctx(), arg.get()->display_name());
                if (!existing || existing->isOpaque())
                {
                    // This is likely an uninstantiated type parameter masquerading as a struct
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "GenericCodegen: Skipping instantiation of {} - type arg '{}' is undefined/opaque",
                              generic_name, arg.get()->display_name());
                    return nullptr;
                }
            }
        }

        // Generate mangled name
        std::string mangled = mangle_type_name(generic_name, type_args);

        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "GenericCodegen: Instantiating struct {} -> {}",
                  generic_name, mangled);

        // Check if methods have already been generated for this instantiation
        // We track this separately from type caching because the LLVM type may exist
        // (created by Monomorphizer) but methods may not be generated yet
        if (_generated_struct_methods.count(mangled))
        {
            // Type and methods already generated, return cached type
            if (has_type_instantiation(mangled))
            {
                llvm::Type *cached = get_cached_type(mangled);
                if (cached)
                {
                    if (auto *struct_type = llvm::dyn_cast<llvm::StructType>(cached))
                    {
                        return struct_type;
                    }
                }
            }
            // Methods generated but type not in our cache - get from LLVM context
            if (llvm::StructType *existing = llvm::StructType::getTypeByName(llvm_ctx(), mangled))
            {
                _type_cache[mangled] = existing;
                return existing;
            }
        }

        // Track structs that are currently being instantiated to detect nested instantiation.
        // If we're already in the middle of instantiating this struct (detected via recursion
        // through create_substituted_fields -> ensure_dependent_types_instantiated), we should
        // return the existing (possibly opaque) struct without trying to generate methods.
        bool is_nested_instantiation = _structs_being_instantiated.count(mangled) > 0;
        if (is_nested_instantiation)
        {
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "GenericCodegen: Detected nested instantiation of '{}', returning existing/opaque type",
                      mangled);
            // Return existing type if any (might be opaque, callers should handle that)
            if (llvm::StructType *existing = llvm::StructType::getTypeByName(llvm_ctx(), mangled))
            {
                return existing;
            }
            // No type exists yet - this shouldn't happen, but return nullptr to be safe
            return nullptr;
        }

        // Add to tracking set - will be removed when we exit this function
        _structs_being_instantiated.insert(mangled);

        // RAII guard to ensure we remove from tracking set even on early returns
        struct InstantiationGuard
        {
            std::string name;
            std::unordered_set<std::string> &set;
            InstantiationGuard(const std::string &n, std::unordered_set<std::string> &s) : name(n), set(s) {}
            ~InstantiationGuard() { set.erase(name); }
        } guard(mangled, _structs_being_instantiated);

        // Check if LLVM type already exists (created by Monomorphizer)
        // If so, we still need to generate methods, but can reuse the type
        llvm::StructType *existing_struct = llvm::StructType::getTypeByName(llvm_ctx(), mangled);
        llvm::StructType *existing_opaque_struct = nullptr;
        bool type_already_exists = false;
        if (existing_struct)
        {
            if (!existing_struct->isOpaque())
            {
                type_already_exists = true;
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "GenericCodegen: Found existing complete LLVM type for struct {}, will generate methods",
                          mangled);
            }
            else
            {
                // Type exists but is opaque - we'll set its body later
                existing_opaque_struct = existing_struct;
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "GenericCodegen: Found existing opaque LLVM type for struct {}, will set body",
                          mangled);
            }
        }

        // Check if we have a pre-specialized AST from the Monomorphizer
        // This AST has types already substituted (T -> Token), so we can use it directly
        Cryo::ASTNode *specialized_ast = nullptr;
        bool using_specialized_ast = false;
        if (auto *mono = ctx().monomorphizer())
        {
            auto entry = mono->get_specialization_by_name(mangled);
            if (entry && entry->ast_node)
            {
                specialized_ast = entry->ast_node;
                using_specialized_ast = true;
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "GenericCodegen: Using pre-specialized AST for '{}'", mangled);
            }
            else
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "GenericCodegen: No specialized AST found for '{}' (entry={}, ast_node={})",
                          mangled, entry.has_value() ? "found" : "not found",
                          (entry && entry->ast_node) ? "valid" : "null");
            }
        }
        else
        {
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "GenericCodegen: No monomorphizer available, using original AST for '{}'", mangled);
        }

        // Get generic type definition - first check local registry, then TemplateRegistry
        Cryo::ASTNode *generic_def = specialized_ast ? specialized_ast : get_generic_type_def(generic_name);
        if (!generic_def)
        {
            // Try to get from TemplateRegistry (for cross-module structs)
            Cryo::TemplateRegistry *template_registry = ctx().template_registry();
            if (template_registry)
            {
                const Cryo::TemplateRegistry::TemplateInfo *tmpl_info = template_registry->find_template(generic_name);
                if (tmpl_info && tmpl_info->struct_template)
                {
                    generic_def = tmpl_info->struct_template;
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "GenericCodegen: Got struct template '{}' from TemplateRegistry",
                              generic_name);
                }
            }
        }
        if (!generic_def)
        {
            report_error(ErrorCode::E0632_TYPE_RESOLUTION_ERROR,
                         "Unknown generic type: " + generic_name);
            return nullptr;
        }

        // Get type parameters from definition
        std::vector<std::string> type_params;
        auto *struct_decl = dynamic_cast<StructDeclarationNode *>(generic_def);
        if (struct_decl)
        {
            for (const auto &param : struct_decl->generic_parameters())
            {
                type_params.push_back(param->name());
            }
        }

        // Begin type parameter scope - pass generic_name and mangled for scope resolution
        begin_type_params(type_params, type_args, generic_name, mangled);

        // Create struct type OR reuse existing one
        llvm::StructType *struct_type = nullptr;
        std::vector<std::string> field_names;
        std::vector<TypeRef> substituted_field_types; // For TemplateRegistry field type registration

        if (type_already_exists)
        {
            // Reuse existing type, just collect field names for method generation
            struct_type = existing_struct;
            if (struct_decl)
            {
                field_names.reserve(struct_decl->fields().size());
                substituted_field_types.reserve(struct_decl->fields().size());
                for (const auto &field : struct_decl->fields())
                {
                    field_names.push_back(field->name());

                    // Collect TypeRef for TemplateRegistry
                    // Always apply substitution - specialized AST may still have compound
                    // generic types like Inner<T> that need T substituted to concrete args
                    TypeRef field_type = field->get_resolved_type();
                    TypeRef final_type = substitute_type_params(field_type);
                    if (!final_type.is_valid() && field->has_type_annotation())
                    {
                        final_type = resolve_field_type_from_annotation(field->type_annotation());
                        if (final_type.is_valid())
                        {
                            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                      "GenericCodegen: Resolved field '{}' type from annotation: {}",
                                      field->name(), final_type->display_name());
                        }
                    }
                    substituted_field_types.push_back(final_type.is_valid() ? final_type : TypeRef{});
                }
            }
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "GenericCodegen: Reusing existing LLVM type for struct {} (specialized_ast={})",
                      mangled, using_specialized_ast);
        }
        else
        {
            // Use existing opaque struct type if one exists, otherwise create new
            struct_type = existing_opaque_struct ? existing_opaque_struct
                                                 : llvm::StructType::create(llvm_ctx(), mangled);

            // Substitute type parameters in fields and collect field names
            // If using specialized AST, types are already substituted
            if (struct_decl)
            {
                std::vector<llvm::Type *> field_types;
                if (using_specialized_ast)
                {
                    // Specialized AST has concrete types - create fields directly
                    field_types = create_fields_from_specialized(struct_decl->fields());
                }
                else
                {
                    // Generic AST needs type parameter substitution
                    field_types = create_substituted_fields(struct_decl->fields());
                }
                struct_type->setBody(field_types);

                // Collect field names AND TypeRefs for member access resolution
                field_names.reserve(struct_decl->fields().size());
                substituted_field_types.reserve(struct_decl->fields().size());
                for (const auto &field : struct_decl->fields())
                {
                    field_names.push_back(field->name());

                    // Collect TypeRef for TemplateRegistry
                    // Always apply substitution - specialized AST may still have compound
                    // generic types like Inner<T> that need T substituted to concrete args
                    TypeRef field_type = field->get_resolved_type();
                    TypeRef final_type = substitute_type_params(field_type);
                    if (!final_type.is_valid() && field->has_type_annotation())
                    {
                        final_type = resolve_field_type_from_annotation(field->type_annotation());
                        if (final_type.is_valid())
                        {
                            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                      "GenericCodegen: Resolved field '{}' type from annotation: {}",
                                      field->name(), final_type->display_name());
                        }
                    }
                    substituted_field_types.push_back(final_type.is_valid() ? final_type : TypeRef{});
                }

                // Safety check: verify struct body has correct number of elements
                if (struct_type->getNumElements() != field_names.size())
                {
                    LOG_ERROR(Cryo::LogComponent::CODEGEN,
                              "GenericCodegen: Struct '{}' body has {} elements but {} field names! "
                              "This will cause field access errors.",
                              mangled, struct_type->getNumElements(), field_names.size());
                }
                else
                {
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "GenericCodegen: Struct '{}' body set with {} elements",
                              mangled, struct_type->getNumElements());
                }
            }

            // Cache and register in multiple places for consistency BEFORE generating methods
            // This ensures the struct type is available for self-referential types
            _type_cache[mangled] = struct_type;
            ctx().register_type(mangled, struct_type);
            types().register_struct(mangled, struct_type);
        }

        // Register type's namespace for cross-module method resolution
        // First check TemplateRegistry (for cross-module structs), then fall back to local context
        Cryo::TemplateRegistry *template_registry = ctx().template_registry();
        std::string base_namespace;
        if (template_registry)
        {
            const Cryo::TemplateRegistry::TemplateInfo *tmpl_info = template_registry->find_template(generic_name);
            if (tmpl_info && !tmpl_info->module_namespace.empty())
            {
                base_namespace = tmpl_info->module_namespace;
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "GenericCodegen: Got namespace '{}' from template registry for struct '{}'",
                          base_namespace, generic_name);
            }
        }
        if (base_namespace.empty())
        {
            base_namespace = ctx().get_type_namespace(generic_name);
        }
        if (base_namespace.empty())
        {
            base_namespace = ctx().namespace_context();
        }
        if (!base_namespace.empty())
        {
            // Register both the mangled name and the base type name
            ctx().register_type_namespace(mangled, base_namespace);
            if (ctx().get_type_namespace(generic_name).empty())
            {
                ctx().register_type_namespace(generic_name, base_namespace);
            }
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "GenericCodegen: Registered instantiated struct '{}' namespace: {}", mangled, base_namespace);
        }

        // Register field names BEFORE generating methods so this.field accesses work
        if (!field_names.empty())
        {
            ctx().register_struct_fields(mangled, field_names);
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "GenericCodegen: Pre-registered {} field names for struct {} before method generation",
                      field_names.size(), mangled);
        }

        // Register field types in TemplateRegistry for nested member access resolution
        // This enables resolution of patterns like this.entries[i].state in generic contexts
        if (!substituted_field_types.empty())
        {
            if (auto *template_reg = ctx().template_registry())
            {
                std::string source_ns = base_namespace.empty() ? ctx().namespace_context() : base_namespace;

                template_reg->register_struct_field_types(mangled, field_names, substituted_field_types, source_ns);

                // Also register with qualified name for cross-module access
                if (!source_ns.empty())
                {
                    std::string qualified_name = source_ns + "::" + mangled;
                    template_reg->register_struct_field_types(qualified_name, field_names, substituted_field_types, source_ns);
                }

                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "GenericCodegen: Registered {} field types in TemplateRegistry for instantiated struct {}",
                          substituted_field_types.size(), mangled);
            }
        }

        // Generate methods for the instantiated struct while type params are still in scope
        // This ensures type parameters like T are substituted with concrete types (e.g., int)
        if (struct_decl && _declarations)
        {
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "GenericCodegen: Generating {} methods for instantiated struct {}",
                      struct_decl->methods().size(), mangled);

            // Save parent context before generating methods
            auto saved_fn_ctx = ctx().release_current_function();
            llvm::BasicBlock *saved_insert_block = builder().GetInsertBlock();
            std::string saved_type_name = ctx().current_type_name();
            std::string saved_namespace = ctx().namespace_context();

            // Set namespace context to the template's defining namespace
            // This ensures SRM generates correct lookup candidates for module-level identifiers
            if (!base_namespace.empty())
            {
                ctx().set_namespace_context(base_namespace);
            }

            // Ensure module-level constants from the defining namespace are available
            // in the consumer module (e.g., BUCKET_EMPTY for hashmap generic methods)
            if (!base_namespace.empty() && base_namespace != saved_namespace)
            {
                auto *template_reg = ctx().template_registry();
                if (template_reg)
                {
                    const auto *constants = template_reg->get_module_constants(base_namespace);
                    if (constants)
                    {
                        for (const auto &c : *constants)
                        {
                            if (!module()->getGlobalVariable(c.name))
                            {
                                // Map type annotation to LLVM type
                                llvm::Type *const_type = nullptr;
                                if (c.type_annotation == "u8" || c.type_annotation == "i8")
                                    const_type = llvm::Type::getInt8Ty(llvm_ctx());
                                else if (c.type_annotation == "u16" || c.type_annotation == "i16")
                                    const_type = llvm::Type::getInt16Ty(llvm_ctx());
                                else if (c.type_annotation == "u32" || c.type_annotation == "i32")
                                    const_type = llvm::Type::getInt32Ty(llvm_ctx());
                                else if (c.type_annotation == "u64" || c.type_annotation == "i64")
                                    const_type = llvm::Type::getInt64Ty(llvm_ctx());
                                else if (c.type_annotation == "bool")
                                    const_type = llvm::Type::getInt1Ty(llvm_ctx());
                                else
                                    const_type = llvm::Type::getInt64Ty(llvm_ctx()); // default

                                auto *gv = new llvm::GlobalVariable(
                                    *module(), const_type, true,
                                    llvm::GlobalValue::PrivateLinkage,
                                    llvm::ConstantInt::get(const_type, c.int_value),
                                    c.name);
                                values().set_global_value(c.name, gv);
                                // Also register with qualified name for SRM lookups
                                std::string qualified = base_namespace + "::" + c.name;
                                values().set_global_value(qualified, gv);
                                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                          "GenericCodegen: Forwarded constant '{}' (={}) from namespace '{}' for struct '{}'",
                                          c.name, c.int_value, base_namespace, mangled);
                            }
                        }
                    }
                }
            }

            // Two-pass method generation: declare all methods first so they can
            // reference each other regardless of source order, then generate bodies.

            // Pass 1: Generate all method declarations
            std::vector<std::pair<Cryo::FunctionDeclarationNode *, llvm::Function *>> method_decls;
            for (const auto &method : struct_decl->methods())
            {
                if (!method)
                    continue;

                // Track the method node so TypeMapper callbacks get the method location
                NodeTracker method_tracker(ctx(), method.get());
                llvm::Function *fn = _declarations->generate_method_declaration(method.get(), mangled);
                if (fn && method->body() && fn->empty())
                {
                    method_decls.push_back({method.get(), fn});
                }
            }

            // Pass 2: Generate all method bodies (all declarations now exist)
            for (auto &[method, fn] : method_decls)
            {
                // Track the method node for precise error locations
                NodeTracker method_tracker(ctx(), method);
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "GenericCodegen: Generating method body: {}::{}",
                          mangled, method->name());

                // Create entry block
                llvm::BasicBlock *entry = llvm::BasicBlock::Create(llvm_ctx(), "entry", fn);
                builder().SetInsertPoint(entry);

                // Set up function context
                auto fn_ctx = std::make_unique<FunctionContext>(fn, method);
                fn_ctx->entry_block = entry;
                ctx().set_current_function(std::move(fn_ctx));

                // Set current type name and instantiation context for error reporting
                ctx().set_current_type_name(mangled);
                set_instantiation_context_from_type(generic_name, type_args);

                // Enter function scope
                values().enter_scope(fn->getName().str());

                // Allocate parameters and register their types
                const auto &ast_params = method->parameters();
                unsigned param_idx = 0;
                for (auto &arg : fn->args())
                {
                    std::string param_name = arg.getName().str();
                    llvm::AllocaInst *alloca = create_entry_alloca(fn, arg.getType(), param_name);
                    create_store(&arg, alloca);
                    values().set_value(param_name, nullptr, alloca);

                    // Register parameter type in variable_types_map for method resolution
                    // Skip 'this' parameter (first param for instance methods)
                    if (param_name == "this")
                    {
                        // 'this' is a pointer to the current type
                        TypeRef this_type = symbols().arena().lookup_type_by_name(mangled);
                        if (this_type.is_valid())
                        {
                            TypeRef this_ptr = symbols().arena().get_pointer_to(this_type);
                            ctx().variable_types_map()[param_name] = this_ptr;
                            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                      "GenericCodegen: Registered 'this' type: {}",
                                      this_ptr->display_name());
                        }
                    }
                    else if (param_idx < ast_params.size() + 1)
                    {
                        // Get AST parameter - check if AST has explicit 'this' parameter
                        // If AST includes 'this' explicitly, use param_idx directly
                        // Otherwise, subtract 1 to account for implicit 'this' added by codegen
                        bool ast_has_explicit_this = !ast_params.empty() &&
                                                     ast_params[0]->name() == "this";
                        size_t ast_idx = ast_has_explicit_this ? param_idx : (param_idx > 0 ? param_idx - 1 : param_idx);
                        if (ast_idx < ast_params.size())
                        {
                            TypeRef param_type = ast_params[ast_idx]->get_resolved_type();
                            // Apply type substitution (T -> string, etc.)
                            TypeRef substituted = substitute_type_params(param_type);
                            if (substituted.is_valid())
                            {
                                // Ensure dependent types are instantiated (e.g., &HashSet<string> needs HashSet<string>)
                                ensure_dependent_types_instantiated(substituted);
                                ctx().variable_types_map()[param_name] = substituted;
                                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                          "GenericCodegen: Registered param '{}' type: {} -> {}",
                                          param_name,
                                          param_type.is_valid() ? param_type->display_name() : "null",
                                          substituted->display_name());
                            }
                        }
                    }
                    param_idx++;
                }

                // Generate method body - type params are still active so T->int works
                CodegenVisitor *visitor = ctx().visitor();
                if (visitor && method->body())
                {
                    method->body()->accept(*visitor);
                }

                // Add implicit return if needed
                llvm::BasicBlock *current_block = builder().GetInsertBlock();
                if (current_block && !current_block->getTerminator())
                {
                    if (fn->getReturnType()->isVoidTy())
                    {
                        builder().CreateRetVoid();
                    }
                    else
                    {
                        builder().CreateRet(llvm::Constant::getNullValue(fn->getReturnType()));
                    }
                }

                // Verify function - don't erase on failure as other methods may reference it
                if (llvm::verifyFunction(*fn, &llvm::errs()))
                {
                    LOG_ERROR(Cryo::LogComponent::CODEGEN,
                              "Generic struct method verification failed: {}::{}",
                              mangled, method->name());
                }

                // Clean up method scope
                values().exit_scope();
                ctx().set_result(nullptr);
            }

            // Restore parent context after generating all methods
            if (saved_fn_ctx)
            {
                ctx().set_current_function(std::move(saved_fn_ctx));
            }
            if (saved_insert_block)
            {
                builder().SetInsertPoint(saved_insert_block);
            }
            ctx().set_current_type_name(saved_type_name);

            ctx().set_namespace_context(saved_namespace);
        }

        // Generate methods from struct implementation blocks (e.g., implement Array<T> { ... })
        if (template_registry && _declarations)
        {
            Cryo::ImplementationBlockNode *impl_block = template_registry->get_struct_impl_block(generic_name);
            if (impl_block)
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "GenericCodegen: Generating {} impl block methods for struct {}",
                          impl_block->method_implementations().size(), mangled);

                // Save parent context before generating methods
                auto saved_fn_ctx = ctx().release_current_function();
                llvm::BasicBlock *saved_insert_block = builder().GetInsertBlock();
                std::string saved_type_name = ctx().current_type_name();
                std::string saved_namespace = ctx().namespace_context();

                // Set namespace context to the template's defining namespace
                if (!base_namespace.empty())
                {
                    ctx().set_namespace_context(base_namespace);
                }

                // Ensure module-level constants from the defining namespace are available
                if (!base_namespace.empty() && base_namespace != saved_namespace)
                {
                    auto *template_reg = ctx().template_registry();
                    if (template_reg)
                    {
                        const auto *constants = template_reg->get_module_constants(base_namespace);
                        if (constants)
                        {
                            for (const auto &c : *constants)
                            {
                                if (!module()->getGlobalVariable(c.name))
                                {
                                    llvm::Type *const_type = nullptr;
                                    if (c.type_annotation == "u8" || c.type_annotation == "i8")
                                        const_type = llvm::Type::getInt8Ty(llvm_ctx());
                                    else if (c.type_annotation == "u16" || c.type_annotation == "i16")
                                        const_type = llvm::Type::getInt16Ty(llvm_ctx());
                                    else if (c.type_annotation == "u32" || c.type_annotation == "i32")
                                        const_type = llvm::Type::getInt32Ty(llvm_ctx());
                                    else if (c.type_annotation == "u64" || c.type_annotation == "i64")
                                        const_type = llvm::Type::getInt64Ty(llvm_ctx());
                                    else if (c.type_annotation == "bool")
                                        const_type = llvm::Type::getInt1Ty(llvm_ctx());
                                    else
                                        const_type = llvm::Type::getInt64Ty(llvm_ctx());

                                    auto *gv = new llvm::GlobalVariable(
                                        *module(), const_type, true,
                                        llvm::GlobalValue::PrivateLinkage,
                                        llvm::ConstantInt::get(const_type, c.int_value),
                                        c.name);
                                    values().set_global_value(c.name, gv);
                                    std::string qualified = base_namespace + "::" + c.name;
                                    values().set_global_value(qualified, gv);
                                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                              "GenericCodegen: Forwarded constant '{}' (={}) from namespace '{}' for struct impl '{}'",
                                              c.name, c.int_value, base_namespace, mangled);
                                }
                            }
                        }
                    }
                }

                // Two-pass method generation: declare all methods first, then generate bodies

                // Pass 1: Declare all impl block methods
                std::vector<std::pair<Cryo::FunctionDeclarationNode *, llvm::Function *>> impl_method_decls;
                for (const auto &method : impl_block->method_implementations())
                {
                    if (!method)
                        continue;

                    NodeTracker method_tracker(ctx(), method.get());
                    llvm::Function *fn = _declarations->generate_method_declaration(method.get(), mangled);
                    if (fn && method->body() && fn->empty())
                    {
                        impl_method_decls.push_back({method.get(), fn});
                    }
                }

                // Pass 2: Generate all impl block method bodies
                for (auto &[method, fn] : impl_method_decls)
                {
                    NodeTracker method_tracker(ctx(), method);
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "GenericCodegen: Generating struct impl method body: {}::{}",
                              mangled, method->name());

                    // Create entry block
                    llvm::BasicBlock *entry = llvm::BasicBlock::Create(llvm_ctx(), "entry", fn);
                    builder().SetInsertPoint(entry);

                    // Set up function context
                    auto fn_ctx = std::make_unique<FunctionContext>(fn, method);
                    fn_ctx->entry_block = entry;
                    ctx().set_current_function(std::move(fn_ctx));

                    // Set current type name and instantiation context for error reporting
                    ctx().set_current_type_name(mangled);
                    set_instantiation_context_from_type(generic_name, type_args);

                    // Enter function scope
                    values().enter_scope(fn->getName().str());

                    // Allocate parameters and register their types
                    const auto &ast_params = method->parameters();
                    unsigned param_idx = 0;
                    for (auto &arg : fn->args())
                    {
                        std::string param_name = arg.getName().str();
                        llvm::AllocaInst *alloca = create_entry_alloca(fn, arg.getType(), param_name);
                        create_store(&arg, alloca);
                        values().set_value(param_name, nullptr, alloca);

                        if (param_name == "this")
                        {
                            // 'this' is a pointer to the current type
                            TypeRef this_type = symbols().arena().lookup_type_by_name(mangled);
                            if (this_type.is_valid())
                            {
                                TypeRef this_ptr = symbols().arena().get_pointer_to(this_type);
                                ctx().variable_types_map()[param_name] = this_ptr;
                                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                          "GenericCodegen: Registered impl 'this' type: {}",
                                          this_ptr->display_name());
                            }
                        }
                        else if (param_idx < ast_params.size() + 1)
                        {
                            bool ast_has_explicit_this = !ast_params.empty() &&
                                                         ast_params[0]->name() == "this";
                            size_t ast_idx = ast_has_explicit_this ? param_idx : (param_idx > 0 ? param_idx - 1 : param_idx);
                            if (ast_idx < ast_params.size())
                            {
                                TypeRef param_type = ast_params[ast_idx]->get_resolved_type();
                                TypeRef substituted = substitute_type_params(param_type);
                                if (substituted.is_valid())
                                {
                                    ensure_dependent_types_instantiated(substituted);
                                    ctx().variable_types_map()[param_name] = substituted;
                                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                              "GenericCodegen: Registered impl param '{}' type: {} -> {}",
                                              param_name,
                                              param_type.is_valid() ? param_type->display_name() : "null",
                                              substituted->display_name());
                                }
                            }
                        }
                        param_idx++;
                    }

                    // Generate method body - type params are still active so T->concrete works
                    CodegenVisitor *visitor = ctx().visitor();
                    if (visitor && method->body())
                    {
                        method->body()->accept(*visitor);
                    }

                    // Add implicit return if needed
                    llvm::BasicBlock *current_block = builder().GetInsertBlock();
                    if (current_block && !current_block->getTerminator())
                    {
                        if (fn->getReturnType()->isVoidTy())
                        {
                            builder().CreateRetVoid();
                        }
                        else
                        {
                            builder().CreateRet(llvm::Constant::getNullValue(fn->getReturnType()));
                        }
                    }

                    // Verify function
                    if (llvm::verifyFunction(*fn, &llvm::errs()))
                    {
                        LOG_ERROR(Cryo::LogComponent::CODEGEN,
                                  "Generic struct impl method verification failed: {}::{}",
                                  mangled, method->name());
                    }

                    // Clean up method scope
                    values().exit_scope();
                    ctx().set_result(nullptr);
                }

                // Restore parent context after generating all methods
                if (saved_fn_ctx)
                {
                    ctx().set_current_function(std::move(saved_fn_ctx));
                }
                if (saved_insert_block)
                {
                    builder().SetInsertPoint(saved_insert_block);
                }
                ctx().set_current_type_name(saved_type_name);
    
                ctx().set_namespace_context(saved_namespace);
            }
        }

        // End type parameter scope AFTER methods are generated
        end_type_params();

        // Propagate method annotations from base template to instantiated name
        // This ensures cross-module lookups work even for methods that were skipped
        // during instantiation (due to generic params, error types, etc.)
        {
            Cryo::TemplateRegistry *tr = ctx().template_registry();
            if (tr)
            {
                std::string base_namespace;
                const auto *tmpl_info = tr->find_template(generic_name);
                if (tmpl_info)
                    base_namespace = tmpl_info->module_namespace;

                if (!base_namespace.empty())
                {
                    // Get method info from template metadata (persists after AST cleanup)
                    const auto *method_info = tr->get_template_method_info(generic_name);
                    if (method_info)
                    {
                        for (const auto &method_meta : method_info->methods)
                        {
                            std::string base_key = base_namespace + "::" + generic_name + "::" + method_meta.name;
                            std::string inst_key = base_namespace + "::" + mangled + "::" + method_meta.name;

                            if (!tr->has_method_return_type_annotation(inst_key))
                            {
                                std::string annotation = tr->get_method_return_type_annotation(base_key);
                                if (!annotation.empty())
                                {
                                    tr->register_method_return_type_annotation(inst_key, annotation);
                                }
                            }

                            auto [is_static, found] = tr->get_method_is_static(inst_key);
                            if (!found)
                            {
                                auto [base_static, base_found] = tr->get_method_is_static(base_key);
                                if (base_found)
                                {
                                    tr->register_method_is_static(inst_key, base_static);
                                }
                            }
                        }
                    }

                    // Also check struct template methods directly (for inline methods)
                    if (tmpl_info->struct_template)
                    {
                        for (const auto &method : tmpl_info->struct_template->methods())
                        {
                            if (!method)
                                continue;
                            std::string base_key = base_namespace + "::" + generic_name + "::" + method->name();
                            std::string inst_key = base_namespace + "::" + mangled + "::" + method->name();

                            if (!tr->has_method_return_type_annotation(inst_key))
                            {
                                std::string annotation = tr->get_method_return_type_annotation(base_key);
                                if (!annotation.empty())
                                {
                                    tr->register_method_return_type_annotation(inst_key, annotation);
                                }
                            }

                            auto [is_static, found] = tr->get_method_is_static(inst_key);
                            if (!found)
                            {
                                tr->register_method_is_static(inst_key, method->is_static());
                            }
                        }
                    }

                    // Also check struct impl block methods
                    Cryo::ImplementationBlockNode *impl_block = tr->get_struct_impl_block(generic_name);
                    if (impl_block)
                    {
                        for (const auto &method : impl_block->method_implementations())
                        {
                            if (!method)
                                continue;
                            std::string base_key = base_namespace + "::" + generic_name + "::" + method->name();
                            std::string inst_key = base_namespace + "::" + mangled + "::" + method->name();

                            if (!tr->has_method_return_type_annotation(inst_key))
                            {
                                std::string annotation = tr->get_method_return_type_annotation(base_key);
                                if (!annotation.empty())
                                {
                                    tr->register_method_return_type_annotation(inst_key, annotation);
                                }
                            }

                            auto [is_static, found] = tr->get_method_is_static(inst_key);
                            if (!found)
                            {
                                tr->register_method_is_static(inst_key, method->is_static());
                            }
                        }
                    }
                }
            }
        }

        // Also build the unmangled name (e.g., "Array<u64>") for lookup consistency
        std::string instantiated_name = generic_name + "<";
        for (size_t i = 0; i < type_args.size(); ++i)
        {
            if (i > 0)
                instantiated_name += ", ";
            if (type_args[i])
            {
                instantiated_name += type_args[i].get()->display_name();
            }
        }
        instantiated_name += ">";
        types().register_struct(instantiated_name, struct_type);

        // Register field names for both mangled and unmangled names
        if (!field_names.empty())
        {
            ctx().register_struct_fields(mangled, field_names);
            ctx().register_struct_fields(instantiated_name, field_names);
        }

        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                  "GenericCodegen: Registered struct {} (also as {}) with {} fields",
                  mangled, instantiated_name, field_names.size());

        // Mark methods as generated to prevent duplicate generation on next call
        _generated_struct_methods.insert(mangled);

        return struct_type;
    }

    llvm::StructType *GenericCodegen::instantiate_class(const std::string &generic_name,
                                                        const std::vector<TypeRef> &type_args)
    {
        // Classes are represented as structs in LLVM
        // Similar to struct instantiation but may include vtable pointer

        // Check if any type arguments are uninstantiated type parameters
        for (const auto &arg : type_args)
        {
            if (!arg)
                continue;

            if (arg->kind() == TypeKind::GenericParam)
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "GenericCodegen: Skipping class instantiation of {} - type arg '{}' is a generic parameter",
                          generic_name, arg.get()->display_name());
                return nullptr;
            }

            if (arg->kind() == TypeKind::Struct || arg->kind() == TypeKind::Class)
            {
                llvm::StructType *existing = llvm::StructType::getTypeByName(llvm_ctx(), arg.get()->display_name());
                if (!existing || existing->isOpaque())
                {
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "GenericCodegen: Skipping class instantiation of {} - type arg '{}' is undefined/opaque",
                              generic_name, arg.get()->display_name());
                    return nullptr;
                }
            }
        }

        std::string mangled = mangle_type_name(generic_name, type_args);

        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "GenericCodegen: Instantiating class {} -> {}",
                  generic_name, mangled);

        // Check cache
        if (has_type_instantiation(mangled))
        {
            llvm::Type *cached = get_cached_type(mangled);
            // Guard against null - dyn_cast asserts on null
            if (cached)
            {
                if (auto *struct_type = llvm::dyn_cast<llvm::StructType>(cached))
                {
                    return struct_type;
                }
            }
        }

        // Check LLVM context
        llvm::StructType *existing_opaque_class = nullptr;
        if (llvm::StructType *existing = llvm::StructType::getTypeByName(llvm_ctx(), mangled))
        {
            if (!existing->isOpaque())
            {
                _type_cache[mangled] = existing;
                return existing;
            }
            else
            {
                // Type exists but is opaque - we'll set its body later
                existing_opaque_class = existing;
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "GenericCodegen: Found existing opaque LLVM type for class {}, will set body",
                          mangled);
            }
        }

        // Track classes that are currently being instantiated to detect nested instantiation.
        bool is_nested_instantiation = _classes_being_instantiated.count(mangled) > 0;
        if (is_nested_instantiation)
        {
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "GenericCodegen: Detected nested instantiation of class '{}', returning existing/opaque type",
                      mangled);
            if (llvm::StructType *existing = llvm::StructType::getTypeByName(llvm_ctx(), mangled))
            {
                return existing;
            }
            return nullptr;
        }

        // Add to tracking set
        _classes_being_instantiated.insert(mangled);

        // RAII guard to ensure we remove from tracking set even on early returns
        struct ClassInstantiationGuard
        {
            std::string name;
            std::unordered_set<std::string> &set;
            ClassInstantiationGuard(const std::string &n, std::unordered_set<std::string> &s) : name(n), set(s) {}
            ~ClassInstantiationGuard() { set.erase(name); }
        } class_guard(mangled, _classes_being_instantiated);

        // Get generic definition - first check local registry, then TemplateRegistry
        Cryo::ASTNode *generic_def = get_generic_type_def(generic_name);
        if (!generic_def)
        {
            // Try to get from TemplateRegistry (for cross-module classes)
            Cryo::TemplateRegistry *template_registry = ctx().template_registry();
            if (template_registry)
            {
                const Cryo::TemplateRegistry::TemplateInfo *tmpl_info = template_registry->find_template(generic_name);
                if (tmpl_info && tmpl_info->class_template)
                {
                    generic_def = tmpl_info->class_template;
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "GenericCodegen: Got class template '{}' from TemplateRegistry",
                              generic_name);
                }
            }
        }
        if (!generic_def)
        {
            report_error(ErrorCode::E0632_TYPE_RESOLUTION_ERROR,
                         "Unknown generic class: " + generic_name);
            return nullptr;
        }

        // Get type parameters
        std::vector<std::string> type_params;
        auto *class_decl = dynamic_cast<ClassDeclarationNode *>(generic_def);
        if (class_decl)
        {
            for (const auto &param : class_decl->generic_parameters())
            {
                type_params.push_back(param->name());
            }
        }

        // Begin substitution scope - pass generic_name and mangled for scope resolution
        begin_type_params(type_params, type_args, generic_name, mangled);

        // Use existing opaque class type if one exists, otherwise create new
        llvm::StructType *class_type = existing_opaque_class ? existing_opaque_class
                                                             : llvm::StructType::create(llvm_ctx(), mangled);

        // Substitute type parameters in fields and collect field names
        std::vector<std::string> field_names;
        std::vector<TypeRef> substituted_field_types; // For TemplateRegistry field type registration
        if (class_decl)
        {
            std::vector<llvm::Type *> field_types = create_substituted_fields(class_decl->fields());
            class_type->setBody(field_types);

            // Collect field names AND substituted TypeRefs for member access resolution
            field_names.reserve(class_decl->fields().size());
            substituted_field_types.reserve(class_decl->fields().size());
            for (const auto &field : class_decl->fields())
            {
                field_names.push_back(field->name());

                // Collect substituted TypeRef for TemplateRegistry
                TypeRef field_type = field->get_resolved_type();
                TypeRef substituted = substitute_type_params(field_type);
                if (!substituted.is_valid() && field->has_type_annotation())
                {
                    substituted = resolve_field_type_from_annotation(field->type_annotation());
                    if (substituted.is_valid())
                    {
                        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                  "GenericCodegen: Resolved field '{}' type from annotation: {}",
                                  field->name(), substituted->display_name());
                    }
                }
                substituted_field_types.push_back(substituted.is_valid() ? substituted : TypeRef{});
            }

            // Safety check: verify class body has correct number of elements
            if (class_type->getNumElements() != field_names.size())
            {
                LOG_ERROR(Cryo::LogComponent::CODEGEN,
                          "GenericCodegen: Class '{}' body has {} elements but {} field names! "
                          "This will cause field access errors.",
                          mangled, class_type->getNumElements(), field_names.size());
            }
            else
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "GenericCodegen: Class '{}' body set with {} elements",
                          mangled, class_type->getNumElements());
            }
        }

        // Cache and register in multiple places for consistency BEFORE generating methods
        _type_cache[mangled] = class_type;
        ctx().register_type(mangled, class_type);
        types().register_struct(mangled, class_type);

        // Register type's namespace for cross-module method resolution
        // First check TemplateRegistry (for cross-module classes), then fall back to local context
        Cryo::TemplateRegistry *template_registry_cls = ctx().template_registry();
        std::string base_namespace;
        if (template_registry_cls)
        {
            const Cryo::TemplateRegistry::TemplateInfo *tmpl_info = template_registry_cls->find_template(generic_name);
            if (tmpl_info && !tmpl_info->module_namespace.empty())
            {
                base_namespace = tmpl_info->module_namespace;
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "GenericCodegen: Got namespace '{}' from template registry for class '{}'",
                          base_namespace, generic_name);
            }
        }
        if (base_namespace.empty())
        {
            base_namespace = ctx().get_type_namespace(generic_name);
        }
        if (base_namespace.empty())
        {
            base_namespace = ctx().namespace_context();
        }
        if (!base_namespace.empty())
        {
            ctx().register_type_namespace(mangled, base_namespace);
            if (ctx().get_type_namespace(generic_name).empty())
            {
                ctx().register_type_namespace(generic_name, base_namespace);
            }
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "GenericCodegen: Registered instantiated class '{}' namespace: {}", mangled, base_namespace);
        }

        // Register field names BEFORE generating methods so this.field accesses work
        if (!field_names.empty())
        {
            ctx().register_struct_fields(mangled, field_names);
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "GenericCodegen: Pre-registered {} field names for class {} before method generation",
                      field_names.size(), mangled);
        }

        // Register field types in TemplateRegistry for nested member access resolution
        // This enables resolution of patterns like this.entries[i].state in generic contexts
        if (!substituted_field_types.empty())
        {
            if (auto *template_reg = ctx().template_registry())
            {
                std::string source_ns = base_namespace.empty() ? ctx().namespace_context() : base_namespace;

                template_reg->register_struct_field_types(mangled, field_names, substituted_field_types, source_ns);

                // Also register with qualified name for cross-module access
                if (!source_ns.empty())
                {
                    std::string qualified_name = source_ns + "::" + mangled;
                    template_reg->register_struct_field_types(qualified_name, field_names, substituted_field_types, source_ns);
                }

                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "GenericCodegen: Registered {} field types in TemplateRegistry for instantiated class {}",
                          substituted_field_types.size(), mangled);
            }
        }

        // Generate methods for the instantiated class while type params are still in scope
        if (class_decl && _declarations)
        {
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "GenericCodegen: Generating {} methods for instantiated class {}",
                      class_decl->methods().size(), mangled);

            // Save parent context before generating methods
            auto saved_fn_ctx = ctx().release_current_function();
            llvm::BasicBlock *saved_insert_block = builder().GetInsertBlock();
            std::string saved_type_name = ctx().current_type_name();
            std::string saved_namespace = ctx().namespace_context();

            // Set namespace context to the template's defining namespace
            // This ensures SRM generates correct lookup candidates for module-level identifiers
            if (!base_namespace.empty())
            {
                ctx().set_namespace_context(base_namespace);
            }

            // Ensure module-level constants from the defining namespace are available
            if (!base_namespace.empty() && base_namespace != saved_namespace)
            {
                auto *template_reg = ctx().template_registry();
                if (template_reg)
                {
                    const auto *constants = template_reg->get_module_constants(base_namespace);
                    if (constants)
                    {
                        for (const auto &c : *constants)
                        {
                            if (!module()->getGlobalVariable(c.name))
                            {
                                llvm::Type *const_type = nullptr;
                                if (c.type_annotation == "u8" || c.type_annotation == "i8")
                                    const_type = llvm::Type::getInt8Ty(llvm_ctx());
                                else if (c.type_annotation == "u16" || c.type_annotation == "i16")
                                    const_type = llvm::Type::getInt16Ty(llvm_ctx());
                                else if (c.type_annotation == "u32" || c.type_annotation == "i32")
                                    const_type = llvm::Type::getInt32Ty(llvm_ctx());
                                else if (c.type_annotation == "u64" || c.type_annotation == "i64")
                                    const_type = llvm::Type::getInt64Ty(llvm_ctx());
                                else if (c.type_annotation == "bool")
                                    const_type = llvm::Type::getInt1Ty(llvm_ctx());
                                else
                                    const_type = llvm::Type::getInt64Ty(llvm_ctx());

                                auto *gv = new llvm::GlobalVariable(
                                    *module(), const_type, true,
                                    llvm::GlobalValue::PrivateLinkage,
                                    llvm::ConstantInt::get(const_type, c.int_value),
                                    c.name);
                                values().set_global_value(c.name, gv);
                                std::string qualified = base_namespace + "::" + c.name;
                                values().set_global_value(qualified, gv);
                                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                          "GenericCodegen: Forwarded constant '{}' (={}) from namespace '{}' for class '{}'",
                                          c.name, c.int_value, base_namespace, mangled);
                            }
                        }
                    }
                }
            }

            for (const auto &method : class_decl->methods())
            {
                if (!method)
                    continue;

                // Track the method node for precise error locations
                NodeTracker method_tracker(ctx(), method.get());

                // Generate method with the mangled class name as parent type
                llvm::Function *fn = _declarations->generate_method_declaration(method.get(), mangled);
                if (fn && method->body() && fn->empty())
                {
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "GenericCodegen: Generating method body: {}::{}",
                              mangled, method->name());

                    // Create entry block
                    llvm::BasicBlock *entry = llvm::BasicBlock::Create(llvm_ctx(), "entry", fn);
                    builder().SetInsertPoint(entry);

                    // Set up function context
                    auto fn_ctx = std::make_unique<FunctionContext>(fn, method.get());
                    fn_ctx->entry_block = entry;
                    ctx().set_current_function(std::move(fn_ctx));

                    // Set current type name and instantiation context for error reporting
                    ctx().set_current_type_name(mangled);
                    set_instantiation_context_from_type(generic_name, type_args);

                    // Enter function scope
                    values().enter_scope(fn->getName().str());

                    // Allocate parameters and register their types
                    const auto &ast_params = method->parameters();
                    unsigned param_idx = 0;
                    for (auto &arg : fn->args())
                    {
                        std::string param_name = arg.getName().str();
                        llvm::AllocaInst *alloca = create_entry_alloca(fn, arg.getType(), param_name);
                        create_store(&arg, alloca);
                        values().set_value(param_name, nullptr, alloca);

                        // Register parameter type in variable_types_map for method resolution
                        if (param_name == "this")
                        {
                            TypeRef this_type = symbols().arena().lookup_type_by_name(mangled);
                            if (this_type.is_valid())
                            {
                                TypeRef this_ptr = symbols().arena().get_pointer_to(this_type);
                                ctx().variable_types_map()[param_name] = this_ptr;
                                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                          "GenericCodegen: Registered 'this' type: {}",
                                          this_ptr->display_name());
                            }
                        }
                        else if (param_idx < ast_params.size() + 1)
                        {
                            // Check if AST has explicit 'this' parameter
                            bool ast_has_explicit_this = !ast_params.empty() &&
                                                         ast_params[0]->name() == "this";
                            size_t ast_idx = ast_has_explicit_this ? param_idx : (param_idx > 0 ? param_idx - 1 : param_idx);
                            if (ast_idx < ast_params.size())
                            {
                                TypeRef param_type = ast_params[ast_idx]->get_resolved_type();
                                TypeRef substituted = substitute_type_params(param_type);
                                if (substituted.is_valid())
                                {
                                    // Ensure dependent types are instantiated (e.g., &HashSet<string> needs HashSet<string>)
                                    ensure_dependent_types_instantiated(substituted);
                                    ctx().variable_types_map()[param_name] = substituted;
                                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                              "GenericCodegen: Registered param '{}' type: {} -> {}",
                                              param_name,
                                              param_type.is_valid() ? param_type->display_name() : "null",
                                              substituted->display_name());
                                }
                            }
                        }
                        param_idx++;
                    }

                    // Generate method body - type params are still active
                    CodegenVisitor *visitor = ctx().visitor();
                    if (visitor && method->body())
                    {
                        method->body()->accept(*visitor);
                    }

                    // Add implicit return if needed
                    llvm::BasicBlock *current_block = builder().GetInsertBlock();
                    if (current_block && !current_block->getTerminator())
                    {
                        if (fn->getReturnType()->isVoidTy())
                        {
                            builder().CreateRetVoid();
                        }
                        else
                        {
                            builder().CreateRet(llvm::Constant::getNullValue(fn->getReturnType()));
                        }
                    }

                    // Clean up method scope
                    values().exit_scope();
                    ctx().set_result(nullptr);
                }
            }

            // Restore parent context after generating all methods
            if (saved_fn_ctx)
            {
                ctx().set_current_function(std::move(saved_fn_ctx));
            }
            if (saved_insert_block)
            {
                builder().SetInsertPoint(saved_insert_block);
            }
            ctx().set_current_type_name(saved_type_name);

            ctx().set_namespace_context(saved_namespace);
        }

        // End substitution scope AFTER methods are generated
        end_type_params();

        // Also build the unmangled name (e.g., "MyClass<u64>") for lookup consistency
        std::string instantiated_name = generic_name + "<";
        for (size_t i = 0; i < type_args.size(); ++i)
        {
            if (i > 0)
                instantiated_name += ", ";
            if (type_args[i])
            {
                instantiated_name += type_args[i].get()->display_name();
            }
        }
        instantiated_name += ">";
        types().register_struct(instantiated_name, class_type);

        // Register field names for both mangled and unmangled names
        if (!field_names.empty())
        {
            ctx().register_struct_fields(mangled, field_names);
            ctx().register_struct_fields(instantiated_name, field_names);
        }

        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                  "GenericCodegen: Registered class {} (also as {}) with {} fields",
                  mangled, instantiated_name, field_names.size());

        return class_type;
    }

    llvm::Type *GenericCodegen::instantiate_enum(const std::string &generic_name,
                                                 const std::vector<TypeRef> &type_args)
    {
        // Check if any type arguments are uninstantiated type parameters
        for (const auto &arg : type_args)
        {
            if (!arg)
                continue;

            if (arg->kind() == TypeKind::GenericParam)
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "GenericCodegen: Skipping enum instantiation of {} - type arg '{}' is a generic parameter",
                          generic_name, arg.get()->display_name());
                return nullptr;
            }

            if (arg->kind() == TypeKind::Struct || arg->kind() == TypeKind::Class)
            {
                llvm::StructType *existing = llvm::StructType::getTypeByName(llvm_ctx(), arg.get()->display_name());
                if (!existing || existing->isOpaque())
                {
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "GenericCodegen: Skipping enum instantiation of {} - type arg '{}' is undefined/opaque",
                              generic_name, arg.get()->display_name());
                    return nullptr;
                }
            }
        }

        std::string mangled = mangle_type_name(generic_name, type_args);

        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "GenericCodegen: Instantiating enum {} -> {}",
                  generic_name, mangled);

        // Check if methods have already been generated for this instantiation
        // We track this separately from type caching because the LLVM type may exist
        // (created by TypeMapper::map_enum) but methods may not be generated yet
        if (_generated_enum_methods.count(mangled))
        {
            // Type and methods already generated, return cached type
            if (has_type_instantiation(mangled))
            {
                return get_cached_type(mangled);
            }
            // Methods generated but type not in our cache - get from LLVM context
            if (llvm::StructType *existing = llvm::StructType::getTypeByName(llvm_ctx(), mangled))
            {
                _type_cache[mangled] = existing;
                return existing;
            }
        }

        // Check if LLVM type already exists (created by TypeMapper)
        // If so, we still need to generate methods, but can reuse the type
        llvm::Type *enum_type = nullptr;
        bool type_already_exists = false;
        llvm::StructType *existing_opaque = nullptr;
        if (llvm::StructType *existing = llvm::StructType::getTypeByName(llvm_ctx(), mangled))
        {
            if (!existing->isOpaque())
            {
                enum_type = existing;
                type_already_exists = true;
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "GenericCodegen: Found existing complete LLVM type for enum {}, will generate methods",
                          mangled);
            }
            else
            {
                // Type exists but is opaque - we'll set its body later
                existing_opaque = existing;
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "GenericCodegen: Found existing opaque LLVM type for enum {}, will set body",
                          mangled);
            }
        }

        // Get generic definition - first check local registry, then TemplateRegistry
        Cryo::ASTNode *generic_def = get_generic_type_def(generic_name);
        if (!generic_def)
        {
            // Try to get from TemplateRegistry (for cross-module enums)
            Cryo::TemplateRegistry *template_registry = ctx().template_registry();
            if (template_registry)
            {
                const Cryo::TemplateRegistry::TemplateInfo *tmpl_info = template_registry->find_template(generic_name);
                if (tmpl_info && tmpl_info->enum_template)
                {
                    generic_def = tmpl_info->enum_template;
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "GenericCodegen: Got enum template '{}' from TemplateRegistry",
                              generic_name);
                }
            }
        }
        if (!generic_def)
        {
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "GenericCodegen: Cannot find enum template '{}' in either GenericCodegen or TemplateRegistry",
                      generic_name);
            return nullptr;
        }

        auto *enum_decl = dynamic_cast<EnumDeclarationNode *>(generic_def);
        if (!enum_decl)
        {
            return nullptr;
        }

        // Get type parameters from definition
        std::vector<std::string> type_params;
        for (const auto &param : enum_decl->generic_parameters())
        {
            type_params.push_back(param->name());
        }

        // Begin type parameter scope for substitution
        // Pass generic_name and mangled name so scope resolutions like Option::None
        // get redirected to Option_voidp::None
        begin_type_params(type_params, type_args, generic_name, mangled);

        // Check if this is a simple enum (all variants are simple with no payloads)
        bool is_simple = enum_decl->is_simple_enum();

        // Only create the type if it doesn't already exist
        if (!type_already_exists)
        {
            if (is_simple)
            {
                // Simple enums are just integers
                enum_type = llvm::Type::getInt32Ty(llvm_ctx());
            }
            else
            {
                // Complex enums are tagged unions: { i32 discriminant, [payload_size x i8] }
                // Compute payload size using Cryo type system's size_bytes() which is
                // available at any pass (computed during ModuleLoader from field types).
                // We do NOT use LLVM types here because struct LLVM bodies aren't set
                // until Pass 7.1, but GenericCodegen runs at Pass 6.2.
                size_t max_payload_size = 0;

                for (const auto &variant : enum_decl->variants())
                {
                    if (!variant->is_simple_variant())
                    {
                        size_t variant_size = 0;
                        for (const auto &type_name : variant->associated_types())
                        {
                            TypeRef payload_type;

                            // Check if it's a type parameter → substitute with type arg
                            for (size_t i = 0; i < type_params.size(); ++i)
                            {
                                if (type_name == type_params[i] && i < type_args.size())
                                {
                                    payload_type = type_args[i];
                                    break;
                                }
                            }

                            // If not a type param, look up in the type arena
                            if (!payload_type.is_valid())
                            {
                                payload_type = symbols().arena().lookup_type_by_name(type_name);
                            }

                            if (payload_type.is_valid())
                            {
                                size_t sz = payload_type->size_bytes();
                                variant_size += (sz > 0) ? sz : 8; // pointer-size fallback
                            }
                            else
                            {
                                variant_size += 8; // pointer-size fallback for unknown types
                            }
                        }
                        max_payload_size = std::max(max_payload_size, variant_size);
                    }
                }

                // Ensure minimum payload size
                max_payload_size = std::max(max_payload_size, static_cast<size_t>(8));

                llvm::StructType *enum_struct = existing_opaque ? existing_opaque
                                                                : llvm::StructType::create(llvm_ctx(), mangled);
                llvm::Type *discriminant_type = llvm::Type::getInt32Ty(llvm_ctx());
                llvm::Type *payload_arr = llvm::ArrayType::get(
                    llvm::Type::getInt8Ty(llvm_ctx()), max_payload_size);
                enum_struct->setBody({discriminant_type, payload_arr}, false);

                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "GenericCodegen: Created tagged union for enum '{}': "
                          "payload size = {} bytes (from Cryo type system)",
                          mangled, max_payload_size);
                enum_type = enum_struct;
            }
        } // end if (!type_already_exists)

        // Cache and register type (only if we created it or need to update cache)
        if (enum_type)
        {
            _type_cache[mangled] = enum_type;
            ctx().register_type(mangled, enum_type);
        }

        // Register type's namespace - first check TemplateRegistry (for cross-module enums),
        // then fall back to local context
        Cryo::TemplateRegistry *template_registry = ctx().template_registry();
        std::string base_namespace;
        if (template_registry)
        {
            const Cryo::TemplateRegistry::TemplateInfo *tmpl_info = template_registry->find_template(generic_name);
            if (tmpl_info && !tmpl_info->module_namespace.empty())
            {
                base_namespace = tmpl_info->module_namespace;
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "GenericCodegen: Got namespace '{}' from template registry for enum '{}'",
                          base_namespace, generic_name);
            }
        }
        if (base_namespace.empty())
        {
            base_namespace = ctx().get_type_namespace(generic_name);
        }
        if (base_namespace.empty())
        {
            base_namespace = ctx().namespace_context();
        }
        if (!base_namespace.empty())
        {
            ctx().register_type_namespace(mangled, base_namespace);
        }

        // Ensure all type arguments that are themselves generic types are instantiated first.
        // This is needed for nested generic enums (e.g., Maybe<Maybe<int>>) where the inner
        // type (Maybe_i32) must exist as an LLVM type before the outer type's constructor
        // can reference it as a parameter type.
        for (const auto &arg : type_args)
        {
            if (arg.is_valid() && arg->kind() == Cryo::TypeKind::InstantiatedType)
            {
                auto *inst = static_cast<const Cryo::InstantiatedType *>(arg.get());
                TypeRef base = inst->generic_base();
                if (base.is_valid())
                {
                    std::string base_name = base->display_name();
                    std::string arg_mangled = mangle_type_name(base_name, inst->type_args());
                    if (!types().get_type(arg_mangled) &&
                        !llvm::StructType::getTypeByName(llvm_ctx(), arg_mangled))
                    {
                        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                  "GenericCodegen: Pre-instantiating nested type '{}' for enum '{}'",
                                  arg_mangled, mangled);
                        get_instantiated_type(base_name, inst->type_args());
                    }
                }
            }
        }

        // Register enum variants with instantiated names
        // This is CRITICAL for codegen to find Option_string::None, Option_string::Some, etc.
        int32_t index = 0;
        for (const auto &variant : enum_decl->variants())
        {
            std::string variant_name = mangled + "::" + variant->name();
            llvm::Constant *discriminant = llvm::ConstantInt::get(llvm::Type::getInt32Ty(llvm_ctx()), index);

            // Register variant discriminant
            ctx().register_enum_variant(variant_name, discriminant);
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "GenericCodegen: Registered instantiated enum variant: {} = {}", variant_name, index);

            // Also register with namespace prefix if applicable
            if (!base_namespace.empty())
            {
                std::string qualified_variant = base_namespace + "::" + variant_name;
                ctx().register_enum_variant(qualified_variant, discriminant);
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "GenericCodegen: Also registered enum variant as: {}", qualified_variant);
            }

            // Register variant field types for pattern matching (with substituted types)
            // Also collect substituted types for constructor generation
            std::vector<std::string> substituted_types;
            if (!variant->is_simple_variant())
            {
                for (const auto &type_name : variant->associated_types())
                {
                    // Substitute type parameters
                    std::string substituted_name = type_name;
                    for (size_t i = 0; i < type_params.size(); ++i)
                    {
                        if (type_name == type_params[i] && i < type_args.size() && type_args[i].is_valid())
                        {
                            // For instantiated generic types (e.g., Maybe<i32>), use the
                            // mangled name (e.g., Maybe_i32) that matches LLVM type registration.
                            // display_name() returns "Maybe<i32>" but the LLVM type is "Maybe_i32".
                            if (type_args[i]->kind() == Cryo::TypeKind::InstantiatedType)
                            {
                                auto *inst = static_cast<const Cryo::InstantiatedType *>(type_args[i].get());
                                if (inst->has_resolved_type())
                                {
                                    substituted_name = inst->resolved_type()->display_name();
                                }
                                else
                                {
                                    TypeRef base = inst->generic_base();
                                    if (base.is_valid())
                                        substituted_name = mangle_type_name(base->display_name(), inst->type_args());
                                    else
                                        substituted_name = type_args[i]->display_name();
                                }
                            }
                            else
                            {
                                substituted_name = type_args[i]->display_name();
                            }
                            break;
                        }
                    }
                    substituted_types.push_back(substituted_name);
                }
                ctx().register_enum_variant_fields(variant_name, substituted_types);
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "GenericCodegen: Registered {} field types for variant {}",
                          substituted_types.size(), variant_name);
            }

            // Generate constructor function for complex variants (variants with payloads)
            // This creates functions like Result_i32_string::Ok(i32) -> Result_i32_string
            if (!is_simple && !variant->is_simple_variant() && enum_type && enum_type->isStructTy())
            {
                std::string ctor_name = mangled + "::" + variant->name();

                // Check if constructor already exists
                if (!module()->getFunction(ctor_name))
                {
                    // Build parameter types from substituted associated types
                    std::vector<llvm::Type *> param_types;
                    bool all_types_resolved = true;
                    for (const auto &type_name : substituted_types)
                    {
                        llvm::Type *param_type = types().get_type(type_name);
                        // Fallback: the type may not be in the CodegenContext registry yet
                        // (e.g., nested generic enums instantiated out of order).
                        // Try looking up the LLVM struct type directly from the LLVM context.
                        if (!param_type)
                        {
                            param_type = llvm::StructType::getTypeByName(llvm_ctx(), type_name);
                        }
                        if (!param_type)
                        {
                            LOG_WARN(Cryo::LogComponent::CODEGEN,
                                     "GenericCodegen: Unknown type '{}' in enum variant {}", type_name, ctor_name);
                            all_types_resolved = false;
                            break;
                        }
                        param_types.push_back(param_type);
                    }

                    if (all_types_resolved && !param_types.empty())
                    {
                        // Create function type: (...fields) -> EnumType
                        llvm::FunctionType *ctor_type = llvm::FunctionType::get(enum_type, param_types, false);
                        llvm::Function *ctor = llvm::Function::Create(
                            ctor_type, llvm::Function::ExternalLinkage, ctor_name, module());

                        // Create entry block
                        llvm::BasicBlock *entry = llvm::BasicBlock::Create(llvm_ctx(), "entry", ctor);
                        llvm::IRBuilder<> ctor_builder(entry);

                        // Allocate the enum struct
                        llvm::AllocaInst *enum_alloca = ctor_builder.CreateAlloca(enum_type, nullptr, "enum_val");

                        // Store the discriminant
                        llvm::Value *disc_ptr = ctor_builder.CreateStructGEP(enum_type, enum_alloca, 0, "disc_ptr");
                        ctor_builder.CreateStore(
                            llvm::ConstantInt::get(llvm::Type::getInt32Ty(llvm_ctx()), index),
                            disc_ptr);

                        // Store the payload fields
                        llvm::Value *payload_ptr = ctor_builder.CreateStructGEP(enum_type, enum_alloca, 1, "payload_ptr");

                        size_t offset = 0;
                        for (auto &arg : ctor->args())
                        {
                            llvm::Type *arg_type = arg.getType();

                            // Calculate pointer to this field in the payload
                            llvm::Value *field_ptr = ctor_builder.CreateConstGEP1_32(
                                llvm::Type::getInt8Ty(llvm_ctx()), payload_ptr, offset, "field_ptr");

                            // Store the argument value
                            ctor_builder.CreateStore(&arg, field_ptr);

                            // Calculate size of argument type
                            if (arg_type->isSized())
                            {
                                offset += module()->getDataLayout().getTypeAllocSize(arg_type);
                            }
                            else
                            {
                                offset += 8; // Default to pointer size for unsized types
                            }
                        }

                        // Load and return the enum value
                        llvm::Value *result = ctor_builder.CreateLoad(enum_type, enum_alloca, "result");
                        ctor_builder.CreateRet(result);

                        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                  "GenericCodegen: Generated enum variant constructor: {} with {} parameters",
                                  ctor_name, param_types.size());
                    }
                }
            }

            index++;
        }

        // Generate methods for the instantiated enum from its implementation block
        // This allows generic enums like Option<T> to have methods like is_some() and is_none()
        // Note: template_registry was already obtained above for namespace lookup
        if (template_registry && _declarations)
        {
            // Look up the impl block for this generic enum
            Cryo::ImplementationBlockNode *impl_block = template_registry->get_enum_impl_block(generic_name);
            if (impl_block)
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "GenericCodegen: Generating {} methods for instantiated enum {} (namespace: {})",
                          impl_block->method_implementations().size(), mangled, base_namespace);

                // Save parent context before generating methods
                auto saved_fn_ctx = ctx().release_current_function();
                llvm::BasicBlock *saved_insert_block = builder().GetInsertBlock();
                std::string saved_type_name = ctx().current_type_name();
                std::string saved_namespace = ctx().namespace_context();

                // Set namespace context to the template's defining namespace
                // This ensures methods are registered with the correct fully-qualified name
                if (!base_namespace.empty())
                {
                    ctx().set_namespace_context(base_namespace);
                }

                // Two-pass method generation: declare all methods first so they can
                // reference each other regardless of source order, then generate bodies.

                // Pass 1: Generate all method declarations (with constrained-this filtering)
                std::vector<std::pair<Cryo::FunctionDeclarationNode *, llvm::Function *>> enum_method_decls;
                for (const auto &method : impl_block->method_implementations())
                {
                    if (!method)
                        continue;

                    // Skip methods whose `this` type is constrained in a way that's
                    // incompatible with the current instantiation.
                    // E.g., transpose(this: Result<Option<T>, E>) should NOT be generated
                    // for Result<Duration, SystemTimeError> because Duration is not Option<...>.
                    //
                    // The parser creates Named annotations with the full type string
                    // (e.g., "Result<Option<T>, E>"), not structured Generic annotations,
                    // so we parse the string to extract type argument constraints.
                    if (!method->is_static() && !method->parameters().empty())
                    {
                        const auto &this_param = method->parameters()[0];
                        if (this_param->name() == "this" && this_param->has_type_annotation())
                        {
                            const TypeAnnotation *ann = this_param->type_annotation();
                            if (ann && ann->kind == TypeAnnotationKind::Named)
                            {
                                const std::string &ann_name = ann->name;
                                std::string prefix = generic_name + "<";

                                // Check if annotation is "EnumName<...>" (constrained this type)
                                if (ann_name.length() > prefix.length() &&
                                    ann_name.compare(0, prefix.length(), prefix) == 0 &&
                                    ann_name.back() == '>')
                                {
                                    // Extract type argument strings from e.g. "Result<Option<T>, E>"
                                    // → ["Option<T>", "E"], respecting angle bracket nesting
                                    std::string inner = ann_name.substr(
                                        prefix.length(),
                                        ann_name.length() - prefix.length() - 1);

                                    std::vector<std::string> ann_type_args;
                                    int depth = 0;
                                    size_t start = 0;
                                    for (size_t ci = 0; ci < inner.length(); ++ci)
                                    {
                                        if (inner[ci] == '<')
                                            depth++;
                                        else if (inner[ci] == '>')
                                            depth--;
                                        else if (inner[ci] == ',' && depth == 0)
                                        {
                                            std::string arg = inner.substr(start, ci - start);
                                            size_t f = arg.find_first_not_of(" \t");
                                            size_t l = arg.find_last_not_of(" \t");
                                            if (f != std::string::npos)
                                                ann_type_args.push_back(arg.substr(f, l - f + 1));
                                            start = ci + 1;
                                        }
                                    }
                                    // Last argument
                                    std::string last_arg = inner.substr(start);
                                    size_t f = last_arg.find_first_not_of(" \t");
                                    size_t l = last_arg.find_last_not_of(" \t");
                                    if (f != std::string::npos)
                                        ann_type_args.push_back(last_arg.substr(f, l - f + 1));

                                    // Check compatibility if we got the right number of type args
                                    if (ann_type_args.size() == type_params.size())
                                    {
                                        bool compatible = true;
                                        for (size_t pi = 0;
                                             pi < type_params.size() && pi < type_args.size();
                                             ++pi)
                                        {
                                            const std::string &constraint = ann_type_args[pi];

                                            // Bare type parameter name (e.g., "T", "E") → no constraint
                                            if (constraint == type_params[pi])
                                                continue;

                                            if (!type_args[pi].is_valid())
                                            {
                                                compatible = false;
                                                break;
                                            }

                                            std::string actual = type_args[pi]->display_name();

                                            // Generic wrapper constraint (e.g., "Option<T>")
                                            size_t angle = constraint.find('<');
                                            if (angle != std::string::npos)
                                            {
                                                std::string wrapper = constraint.substr(0, angle);
                                                std::string exp_prefix = wrapper + "<";
                                                if (actual.length() < exp_prefix.length() ||
                                                    actual.compare(0, exp_prefix.length(),
                                                                   exp_prefix) != 0)
                                                {
                                                    compatible = false;
                                                    break;
                                                }
                                            }
                                            // Pointer constraint (e.g., "T*")
                                            else if (!constraint.empty() && constraint.back() == '*')
                                            {
                                                if (type_args[pi]->kind() != TypeKind::Pointer)
                                                {
                                                    compatible = false;
                                                    break;
                                                }
                                            }
                                            // Concrete type constraint
                                            else if (actual != constraint)
                                            {
                                                compatible = false;
                                                break;
                                            }
                                        }

                                        if (!compatible)
                                        {
                                            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                                      "GenericCodegen: Skipping method '{}::{}' - "
                                                      "constrained this type '{}' incompatible with "
                                                      "current instantiation",
                                                      mangled, method->name(), ann_name);
                                            continue;
                                        }
                                    }
                                }
                            }
                        }
                    }

                    // Track the method node for precise error locations
                    NodeTracker method_tracker(ctx(), method.get());

                    // Generate method declaration with the mangled enum name as parent type
                    llvm::Function *fn = _declarations->generate_method_declaration(method.get(), mangled);
                    if (fn && method->body() && fn->empty())
                    {
                        enum_method_decls.push_back({method.get(), fn});
                    }
                }

                // Pass 2: Generate all method bodies (all declarations now exist)
                for (auto &[method, fn] : enum_method_decls)
                {
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "GenericCodegen: Generating enum method body: {}::{}",
                              mangled, method->name());

                    // Create entry block
                    llvm::BasicBlock *entry = llvm::BasicBlock::Create(llvm_ctx(), "entry", fn);
                    builder().SetInsertPoint(entry);

                    // Set up function context
                    auto fn_ctx = std::make_unique<FunctionContext>(fn, method);
                    fn_ctx->entry_block = entry;
                    ctx().set_current_function(std::move(fn_ctx));

                    // Set current type name and instantiation context for error reporting
                    ctx().set_current_type_name(mangled);
                    set_instantiation_context_from_type(generic_name, type_args);

                    // Enter function scope
                    values().enter_scope(fn->getName().str());

                    // Allocate parameters and register their types
                    const auto &ast_params = method->parameters();
                    unsigned param_idx = 0;
                    for (auto &arg : fn->args())
                    {
                        std::string param_name = arg.getName().str();

                        // Debug: Log LLVM argument type
                        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                  "GenericCodegen: Enum method arg '{}' LLVM type ID={}, isStruct={}, isInt={}",
                                  param_name,
                                  arg.getType()->getTypeID(),
                                  arg.getType()->isStructTy(),
                                  arg.getType()->isIntegerTy());

                        llvm::AllocaInst *alloca = create_entry_alloca(fn, arg.getType(), param_name);
                        create_store(&arg, alloca);
                        values().set_value(param_name, nullptr, alloca);

                        // Register parameter type in variable_types_map for method resolution
                        // Skip 'this' parameter (first param for instance methods)
                        if (param_name == "this")
                        {
                            // Robust lookup chain for monomorphized enum 'this' type.
                            // The Monomorphizer may not have created the concrete enum TypeRef
                            // (e.g., due to TypeID mismatches on imported generic types),
                            // so we try multiple strategies and create as last resort.
                            TypeRef this_type = symbols().arena().lookup_type_by_name(mangled);
                            if (!this_type.is_valid())
                                this_type = symbols().lookup_enum_type(mangled);
                            if (!this_type.is_valid())
                                this_type = symbols().lookup_struct_type(mangled);
                            // Fallback: look up the base generic type (e.g., "Option")
                            if (!this_type.is_valid())
                                this_type = symbols().arena().lookup_type_by_name(generic_name);
                            // Last resort: create a bare enum TypeRef so 'this' is resolvable
                            if (!this_type.is_valid())
                            {
                                Cryo::QualifiedTypeName qname;
                                qname.name = mangled;
                                qname.module = Cryo::ModuleID::invalid();
                                this_type = symbols().arena().create_enum(qname);
                            }
                            if (this_type.is_valid())
                            {
                                TypeRef this_ptr = symbols().arena().get_pointer_to(this_type);
                                ctx().variable_types_map()[param_name] = this_ptr;
                                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                          "GenericCodegen: Registered enum 'this' type: {}",
                                          this_ptr->display_name());
                            }
                        }
                        else if (param_idx < ast_params.size() + 1)
                        {
                            // Get AST parameter - check if AST has explicit 'this' parameter
                            // If AST includes 'this' explicitly, use param_idx directly
                            // Otherwise, subtract 1 to account for implicit 'this' added by codegen
                            bool ast_has_explicit_this = !ast_params.empty() &&
                                                         ast_params[0]->name() == "this";
                            size_t ast_idx = ast_has_explicit_this ? param_idx : (param_idx > 0 ? param_idx - 1 : param_idx);
                            if (ast_idx < ast_params.size())
                            {
                                TypeRef param_type = ast_params[ast_idx]->get_resolved_type();
                                // Apply type substitution (T -> i64, etc.)
                                TypeRef substituted = substitute_type_params(param_type);
                                if (substituted.is_valid())
                                {
                                    ensure_dependent_types_instantiated(substituted);
                                    ctx().variable_types_map()[param_name] = substituted;
                                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                              "GenericCodegen: Registered enum param '{}' type: {} -> {}",
                                              param_name,
                                              param_type.is_valid() ? param_type->display_name() : "null",
                                              substituted->display_name());
                                }
                            }
                        }
                        param_idx++;
                    }

                    // Generate method body - type params are still active so T->i64 works
                    CodegenVisitor *visitor = ctx().visitor();
                    if (visitor && method->body())
                    {
                        method->body()->accept(*visitor);
                    }

                    // Add implicit return if needed
                    llvm::BasicBlock *current_block = builder().GetInsertBlock();
                    if (current_block && !current_block->getTerminator())
                    {
                        if (fn->getReturnType()->isVoidTy())
                        {
                            builder().CreateRetVoid();
                        }
                        else
                        {
                            builder().CreateRet(llvm::Constant::getNullValue(fn->getReturnType()));
                        }
                    }

                    // Verify function - don't erase on failure as other methods may reference it
                    if (llvm::verifyFunction(*fn, &llvm::errs()))
                    {
                        LOG_ERROR(Cryo::LogComponent::CODEGEN,
                                  "Generic enum method verification failed: {}::{}",
                                  mangled, method->name());
                    }

                    // Clean up method scope
                    values().exit_scope();
                    ctx().set_result(nullptr);
                }

                // Restore parent context after generating all methods
                if (saved_fn_ctx)
                {
                    ctx().set_current_function(std::move(saved_fn_ctx));
                }
                if (saved_insert_block)
                {
                    builder().SetInsertPoint(saved_insert_block);
                }
                ctx().set_current_type_name(saved_type_name);
    
                ctx().set_namespace_context(saved_namespace);
            }
            else
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "GenericCodegen: No impl block found for enum {}", generic_name);
            }
        }

        // Mark methods as generated for this instantiation
        _generated_enum_methods.insert(mangled);

        // End type parameter scope AFTER methods are generated
        end_type_params();

        // Propagate method annotations from base template to instantiated name
        // This ensures cross-module lookups work for enum methods (e.g., Option_i64::is_some)
        {
            Cryo::TemplateRegistry *tr = ctx().template_registry();
            if (tr)
            {
                std::string base_namespace;
                const auto *tmpl_info = tr->find_template(generic_name);
                if (tmpl_info)
                    base_namespace = tmpl_info->module_namespace;

                if (!base_namespace.empty())
                {
                    const auto *method_info = tr->get_template_method_info(generic_name);
                    if (method_info)
                    {
                        for (const auto &method_meta : method_info->methods)
                        {
                            std::string base_key = base_namespace + "::" + generic_name + "::" + method_meta.name;
                            std::string inst_key = base_namespace + "::" + mangled + "::" + method_meta.name;

                            if (!tr->has_method_return_type_annotation(inst_key))
                            {
                                std::string annotation = tr->get_method_return_type_annotation(base_key);
                                if (!annotation.empty())
                                {
                                    tr->register_method_return_type_annotation(inst_key, annotation);
                                }
                            }

                            auto [is_static, found] = tr->get_method_is_static(inst_key);
                            if (!found)
                            {
                                auto [base_static, base_found] = tr->get_method_is_static(base_key);
                                if (base_found)
                                {
                                    tr->register_method_is_static(inst_key, base_static);
                                }
                            }
                        }
                    }

                    // Also check enum impl block methods directly
                    ImplementationBlockNode *impl_block = tr->get_enum_impl_block(generic_name);
                    if (impl_block)
                    {
                        for (const auto &method : impl_block->method_implementations())
                        {
                            if (!method)
                                continue;
                            std::string base_key = base_namespace + "::" + generic_name + "::" + method->name();
                            std::string inst_key = base_namespace + "::" + mangled + "::" + method->name();

                            if (!tr->has_method_return_type_annotation(inst_key))
                            {
                                std::string annotation = tr->get_method_return_type_annotation(base_key);
                                if (!annotation.empty())
                                {
                                    tr->register_method_return_type_annotation(inst_key, annotation);
                                }
                            }

                            auto [is_static, found] = tr->get_method_is_static(inst_key);
                            if (!found)
                            {
                                tr->register_method_is_static(inst_key, method->is_static());
                            }
                        }
                    }
                }
            }
        }

        // Also register with unmangled name for lookup consistency
        std::string instantiated_name = generic_name + "<";
        for (size_t i = 0; i < type_args.size(); ++i)
        {
            if (i > 0)
                instantiated_name += ", ";
            if (type_args[i])
            {
                instantiated_name += type_args[i].get()->display_name();
            }
        }
        instantiated_name += ">";

        if (auto *st = llvm::dyn_cast<llvm::StructType>(enum_type))
        {
            types().register_struct(instantiated_name, st);
        }

        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                  "GenericCodegen: Registered enum {} (also as {}) with {} variants",
                  mangled, instantiated_name, enum_decl->variants().size());

        return enum_type;
    }

    llvm::Type *GenericCodegen::get_instantiated_type(const std::string &generic_name,
                                                      const std::vector<TypeRef> &type_args)
    {
        std::string mangled = mangle_type_name(generic_name, type_args);

        // Check cache first
        if (has_type_instantiation(mangled))
        {
            return get_cached_type(mangled);
        }

        // Try to instantiate - first check local registry, then TemplateRegistry
        Cryo::ASTNode *def = get_generic_type_def(generic_name);
        if (!def)
        {
            // Try to get from TemplateRegistry (for cross-module types)
            Cryo::TemplateRegistry *template_registry = ctx().template_registry();
            if (template_registry)
            {
                const Cryo::TemplateRegistry::TemplateInfo *tmpl_info = template_registry->find_template(generic_name);
                if (tmpl_info)
                {
                    if (tmpl_info->enum_template)
                        def = tmpl_info->enum_template;
                    else if (tmpl_info->struct_template)
                        def = tmpl_info->struct_template;
                    else if (tmpl_info->class_template)
                        def = tmpl_info->class_template;
                }
            }
        }
        if (!def)
        {
            return nullptr;
        }

        if (dynamic_cast<StructDeclarationNode *>(def))
        {
            return instantiate_struct(generic_name, type_args);
        }
        else if (dynamic_cast<ClassDeclarationNode *>(def))
        {
            return instantiate_class(generic_name, type_args);
        }
        else if (dynamic_cast<EnumDeclarationNode *>(def))
        {
            return instantiate_enum(generic_name, type_args);
        }

        return nullptr;
    }

    //===================================================================
    // Generic Function Instantiation
    //===================================================================

    llvm::Function *GenericCodegen::instantiate_function(const std::string &generic_name,
                                                         const std::vector<TypeRef> &type_args)
    {
        // Check if any type arguments are uninstantiated type parameters
        for (const auto &arg : type_args)
        {
            if (!arg)
                continue;

            if (arg->kind() == TypeKind::GenericParam)
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "GenericCodegen: Skipping function instantiation of {} - type arg '{}' is a generic parameter",
                          generic_name, arg.get()->display_name());
                return nullptr;
            }

            if (arg->kind() == TypeKind::Struct || arg->kind() == TypeKind::Class)
            {
                llvm::StructType *existing = llvm::StructType::getTypeByName(llvm_ctx(), arg.get()->display_name());
                if (!existing || existing->isOpaque())
                {
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "GenericCodegen: Skipping function instantiation of {} - type arg '{}' is undefined/opaque",
                              generic_name, arg.get()->display_name());
                    return nullptr;
                }
            }
        }

        std::string mangled = mangle_function_name(generic_name, type_args);

        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "GenericCodegen: Instantiating function {} -> {}",
                  generic_name, mangled);

        // Check cache
        if (has_function_instantiation(mangled))
        {
            return get_cached_function(mangled);
        }

        // Check module
        if (llvm::Function *existing = module()->getFunction(mangled))
        {
            _function_cache[mangled] = existing;
            return existing;
        }

        // Get generic definition
        Cryo::ASTNode *generic_def = get_generic_function_def(generic_name);
        if (!generic_def)
        {
            report_error(ErrorCode::E0633_FUNCTION_BODY_ERROR,
                         "Unknown generic function: " + generic_name);
            return nullptr;
        }

        auto *func_decl = dynamic_cast<FunctionDeclarationNode *>(generic_def);
        if (!func_decl)
        {
            return nullptr;
        }

        // Get type parameters
        std::vector<std::string> type_params;
        if (func_decl->generic_parameters().size() > 0)
        {
            for (const auto &param : func_decl->generic_parameters())
            {
                type_params.push_back(param->name());
            }
        }

        // Begin substitution scope
        begin_type_params(type_params, type_args);

        // Create function type with substituted types
        llvm::FunctionType *fn_type = create_substituted_function_type(func_decl);
        if (!fn_type)
        {
            end_type_params();
            return nullptr;
        }

        // Create function
        llvm::Function *fn = llvm::Function::Create(
            fn_type,
            llvm::Function::ExternalLinkage,
            mangled,
            module());

        // Set parameter names from AST
        {
            const auto &ast_params = func_decl->parameters();
            unsigned idx = 0;
            for (auto &arg : fn->args())
            {
                if (idx < ast_params.size())
                {
                    arg.setName(ast_params[idx]->name());
                }
                idx++;
            }
        }

        // Generate body if available
        if (func_decl->body() && _declarations)
        {
            // Save caller's state (we may be called during another function's codegen)
            llvm::BasicBlock *saved_block = builder().GetInsertBlock();
            auto saved_fn_ctx = ctx().release_current_function();

            // Create entry block
            llvm::BasicBlock *entry = llvm::BasicBlock::Create(llvm_ctx(), "entry", fn);
            builder().SetInsertPoint(entry);

            // Set up function context
            auto fn_ctx = std::make_unique<FunctionContext>(fn, func_decl);
            fn_ctx->entry_block = entry;
            ctx().set_current_function(std::move(fn_ctx));

            // Enter function scope
            values().enter_scope(fn->getName().str());

            // Allocate parameters
            const auto &ast_params = func_decl->parameters();
            unsigned param_idx = 0;
            for (auto &arg : fn->args())
            {
                std::string param_name = arg.getName().str();
                llvm::AllocaInst *alloca = create_entry_alloca(fn, arg.getType(), param_name);
                create_store(&arg, alloca);
                values().set_value(param_name, nullptr, alloca);

                if (param_idx < ast_params.size())
                {
                    TypeRef param_type = ast_params[param_idx]->get_resolved_type();
                    TypeRef substituted = substitute_type_params(param_type);
                    if (substituted.is_valid())
                    {
                        ctx().variable_types_map()[param_name] = substituted;
                    }
                }
                param_idx++;
            }

            // Generate function body
            CodegenVisitor *visitor = ctx().visitor();
            if (visitor && func_decl->body())
            {
                func_decl->body()->accept(*visitor);
            }

            // Add implicit return if needed
            llvm::BasicBlock *current_block = builder().GetInsertBlock();
            if (current_block && !current_block->getTerminator())
            {
                if (fn->getReturnType()->isVoidTy())
                {
                    builder().CreateRetVoid();
                }
                else
                {
                    builder().CreateRet(llvm::Constant::getNullValue(fn->getReturnType()));
                }
            }

            // Verify function
            if (llvm::verifyFunction(*fn, &llvm::errs()))
            {
                LOG_ERROR(Cryo::LogComponent::CODEGEN,
                          "Generic function verification failed: {}", mangled);
            }

            // Exit function scope
            values().exit_scope();
            ctx().clear_current_function();

            // Restore caller's state
            if (saved_fn_ctx)
                ctx().set_current_function(std::move(saved_fn_ctx));
            if (saved_block)
                builder().SetInsertPoint(saved_block);
        }

        // End substitution scope
        end_type_params();

        // Cache and register
        _function_cache[mangled] = fn;
        ctx().register_function(mangled, fn);

        return fn;
    }

    llvm::Function *GenericCodegen::instantiate_method(const std::string &type_name,
                                                       const std::string &method_name,
                                                       const std::vector<TypeRef> &type_args)
    {
        std::string qualified = type_name + "::" + method_name;
        return instantiate_function(qualified, type_args);
    }

    llvm::Function *GenericCodegen::instantiate_method_for_type(
        const std::string &mangled_type,
        const std::string &generic_base,
        const std::vector<std::string> &all_type_params,
        const std::vector<TypeRef> &all_type_args,
        Cryo::FunctionDeclarationNode *method,
        const std::string &base_namespace)
    {
        if (!method || !_declarations)
            return nullptr;

        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                  "GenericCodegen::instantiate_method_for_type: {}::{} with {} type params",
                  mangled_type, method->name(), all_type_params.size());

        // Save parent context
        auto saved_fn_ctx = ctx().release_current_function();
        llvm::BasicBlock *saved_insert_block = builder().GetInsertBlock();
        std::string saved_type_name = ctx().current_type_name();
        std::string saved_namespace = ctx().namespace_context();

        // Set namespace context
        if (!base_namespace.empty())
        {
            ctx().set_namespace_context(base_namespace);
        }

        // Begin type parameter scope with combined params
        begin_type_params(all_type_params, all_type_args, generic_base, mangled_type);

        // Register partially-substituted type names as redirects.
        // The Monomorphizer already substituted enum-level params (T→i32) but left
        // method-level params (U) as-is, producing names like "Maybe_U".
        // We need to redirect "Maybe_U" → "Maybe_i32" for variant resolution.
        // Build the partial name: generic_base + "_" + method-level param names
        // and register variants under the fully-substituted name.
        {
            // Build the partially-substituted name (e.g., "Maybe_U")
            std::string partial_name = generic_base;
            for (const auto &param : all_type_params)
            {
                // Check if this param has a concrete binding in our scope
                TypeRef binding = resolve_type_param(param);
                if (binding.is_valid())
                {
                    partial_name += "_" + binding->display_name();
                }
                else
                {
                    partial_name += "_" + param;
                }
            }

            // The Monomorphizer produces names with only enum-level params substituted
            // e.g., for Maybe<T> with T=i32, method-level U stays: "Maybe_U"
            // Compute this: substitute only enum-level params, leave method-level as param names
            std::string mono_partial = generic_base;
            // Enum-level params come first in all_type_params
            Cryo::TemplateRegistry *template_registry = ctx().template_registry();
            size_t enum_param_count = 0;
            if (template_registry)
            {
                const auto *tmpl_info = template_registry->find_template(generic_base);
                if (tmpl_info && tmpl_info->enum_template)
                {
                    enum_param_count = tmpl_info->enum_template->generic_parameters().size();
                }
            }
            // Build partial name: enum params substituted, method params as names
            for (size_t i = enum_param_count; i < all_type_params.size(); ++i)
            {
                mono_partial = generic_base + "_" + all_type_params[i];
            }

            if (mono_partial != mangled_type && mono_partial != generic_base)
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "GenericCodegen::instantiate_method_for_type: Registering partial redirect {} -> {}",
                          mono_partial, mangled_type);

                // Register enum variants under the partial name pointing to the real ones
                // Look up existing variants for mangled_type and register under mono_partial
                Cryo::EnumDeclarationNode *enum_decl = nullptr;
                if (template_registry)
                {
                    const auto *tmpl_info = template_registry->find_template(generic_base);
                    if (tmpl_info && tmpl_info->enum_template)
                        enum_decl = tmpl_info->enum_template;
                }
                auto &variants_map = ctx().enum_variants_map();
                if (enum_decl)
                {
                    for (const auto &variant : enum_decl->variants())
                    {
                        std::string real_variant = mangled_type + "::" + variant->name();
                        std::string partial_variant = mono_partial + "::" + variant->name();
                        // Look up the discriminant from the real variant
                        llvm::Value *disc = nullptr;
                        auto it = variants_map.find(real_variant);
                        if (it != variants_map.end())
                            disc = it->second;
                        if (!disc && !base_namespace.empty())
                        {
                            auto it2 = variants_map.find(base_namespace + "::" + real_variant);
                            if (it2 != variants_map.end())
                                disc = it2->second;
                        }
                        if (disc)
                        {
                            ctx().register_enum_variant(partial_variant, disc);
                            if (!base_namespace.empty())
                            {
                                ctx().register_enum_variant(base_namespace + "::" + partial_variant, disc);
                            }
                            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                      "GenericCodegen::instantiate_method_for_type: Registered variant redirect {} -> {}",
                                      partial_variant, real_variant);
                        }
                    }
                }

                // Variant constructor functions (Maybe_i32::Just) will be found via
                // substitute_type_annotation redirecting "Maybe_U" → "Maybe_i32" at call sites
            }
        }

        // Track the method node for precise error locations
        NodeTracker method_tracker(ctx(), method);

        // Generate method declaration (force_allow_generic = true to bypass generic skip)
        llvm::Function *fn = _declarations->generate_method_declaration(method, mangled_type, /*force_allow_generic=*/true);
        if (fn && method->body() && fn->empty())
        {
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "GenericCodegen::instantiate_method_for_type: Generating body for {}::{}",
                      mangled_type, method->name());

            // Create entry block
            llvm::BasicBlock *entry = llvm::BasicBlock::Create(llvm_ctx(), "entry", fn);
            builder().SetInsertPoint(entry);

            // Set up function context
            auto fn_ctx = std::make_unique<FunctionContext>(fn, method);
            fn_ctx->entry_block = entry;
            ctx().set_current_function(std::move(fn_ctx));

            // Set current type name and instantiation context for error reporting
            ctx().set_current_type_name(mangled_type);
            set_instantiation_context_from_type(generic_base, all_type_args);

            // Enter function scope
            values().enter_scope(fn->getName().str());

            // Allocate parameters and register their types
            const auto &ast_params = method->parameters();
            unsigned param_idx = 0;
            for (auto &arg : fn->args())
            {
                std::string param_name = arg.getName().str();

                llvm::AllocaInst *alloca = create_entry_alloca(fn, arg.getType(), param_name);
                create_store(&arg, alloca);
                values().set_value(param_name, nullptr, alloca);

                if (param_name == "this")
                {
                    // Robust lookup chain for monomorphized 'this' type.
                    // Same strategy as instantiate_enum: try multiple lookups,
                    // fall back to base generic type, create as last resort.
                    TypeRef this_type = symbols().arena().lookup_type_by_name(mangled_type);
                    if (!this_type.is_valid())
                        this_type = symbols().lookup_enum_type(mangled_type);
                    if (!this_type.is_valid())
                        this_type = symbols().lookup_struct_type(mangled_type);
                    if (!this_type.is_valid())
                        this_type = symbols().arena().lookup_type_by_name(generic_base);
                    if (!this_type.is_valid())
                    {
                        Cryo::QualifiedTypeName qname;
                        qname.name = mangled_type;
                        qname.module = Cryo::ModuleID::invalid();
                        this_type = symbols().arena().create_enum(qname);
                    }
                    if (this_type.is_valid())
                    {
                        TypeRef this_ptr = symbols().arena().get_pointer_to(this_type);
                        ctx().variable_types_map()[param_name] = this_ptr;
                    }
                }
                else if (param_idx < ast_params.size() + 1)
                {
                    bool ast_has_explicit_this = !ast_params.empty() &&
                                                 ast_params[0]->name() == "this";
                    size_t ast_idx = ast_has_explicit_this ? param_idx : (param_idx > 0 ? param_idx - 1 : param_idx);
                    if (ast_idx < ast_params.size())
                    {
                        TypeRef param_type = ast_params[ast_idx]->get_resolved_type();
                        TypeRef substituted = substitute_type_params(param_type);
                        if (substituted.is_valid())
                        {
                            ensure_dependent_types_instantiated(substituted);
                            ctx().variable_types_map()[param_name] = substituted;
                        }
                    }
                }
                param_idx++;
            }

            // Generate method body
            CodegenVisitor *visitor = ctx().visitor();
            if (visitor && method->body())
            {
                method->body()->accept(*visitor);
            }

            // Add implicit return if needed
            llvm::BasicBlock *current_block = builder().GetInsertBlock();
            if (current_block && !current_block->getTerminator())
            {
                if (fn->getReturnType()->isVoidTy())
                {
                    builder().CreateRetVoid();
                }
                else
                {
                    builder().CreateRet(llvm::Constant::getNullValue(fn->getReturnType()));
                }
            }

            // Verify function
            if (llvm::verifyFunction(*fn, &llvm::errs()))
            {
                LOG_ERROR(Cryo::LogComponent::CODEGEN,
                          "GenericCodegen::instantiate_method_for_type: verification failed for {}::{}",
                          mangled_type, method->name());
            }

            // Clean up method scope
            values().exit_scope();
            ctx().set_result(nullptr);
        }

        // End type parameter scope
        end_type_params();

        // Restore parent context
        if (saved_fn_ctx)
        {
            ctx().set_current_function(std::move(saved_fn_ctx));
        }
        if (saved_insert_block)
        {
            builder().SetInsertPoint(saved_insert_block);
        }
        ctx().set_current_type_name(saved_type_name);
        ctx().set_namespace_context(saved_namespace);

        return fn;
    }

    //===================================================================
    // Type Parameter Handling
    //===================================================================

    void GenericCodegen::begin_type_params(const std::vector<std::string> &params,
                                           const std::vector<TypeRef> &args,
                                           const std::string &generic_base,
                                           const std::string &instantiated_name)
    {
        TypeParamScope scope;
        scope.generic_base = generic_base;
        scope.instantiated_name = instantiated_name;

        size_t count = std::min(params.size(), args.size());
        for (size_t i = 0; i < count; ++i)
        {
            scope.bindings[params[i]] = args[i];
            LOG_DEBUG(Cryo::LogComponent::CODEGEN, "GenericCodegen: Binding {} -> {}",
                      params[i], args[i] ? args[i].get()->display_name() : "null");
        }

        if (!generic_base.empty())
        {
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "GenericCodegen: Scope resolution mapping: {} -> {}",
                      generic_base, instantiated_name);
        }

        _type_param_stack.push_back(std::move(scope));

        // Set the type parameter resolver on TypeMapper so type lookups can resolve T, E, etc.
        types().set_type_param_resolver([this](const std::string &name) -> TypeRef
                                        { return this->resolve_type_param(name); });
    }

    std::string GenericCodegen::get_instantiated_scope_name(const std::string &generic_base) const
    {
        // Search from innermost to outermost scope
        for (auto it = _type_param_stack.rbegin(); it != _type_param_stack.rend(); ++it)
        {
            if (it->generic_base == generic_base && !it->instantiated_name.empty())
            {
                return it->instantiated_name;
            }
        }
        return "";
    }

    std::string GenericCodegen::substitute_type_annotation(const std::string &type_annotation)
    {
        if (_type_param_stack.empty())
            return "";

        // Check if the type annotation contains generic angle brackets
        size_t angle_pos = type_annotation.find('<');
        if (angle_pos == std::string::npos)
        {
            // No angle brackets - check if it's a direct type parameter (like "T")
            TypeRef resolved = resolve_type_param(type_annotation);
            if (resolved.is_valid())
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "substitute_type_annotation: Direct type param '{}' -> '{}'",
                          type_annotation, resolved->display_name());
                return resolved->display_name();
            }

            // Check for underscore-mangled names (e.g., "Maybe_U" from Monomorphizer)
            // where a suffix component is a bound type parameter.
            // Try replacing type param names after underscores with their concrete types.
            size_t underscore_pos = type_annotation.find('_');
            if (underscore_pos != std::string::npos)
            {
                std::string result = type_annotation;
                bool any_substituted = false;
                const auto &scope = _type_param_stack.back();
                for (const auto &[param_name, binding] : scope.bindings)
                {
                    if (!binding.is_valid())
                        continue;
                    // Look for _ParamName as a component in the mangled name
                    std::string search = "_" + param_name;
                    size_t pos = result.find(search);
                    while (pos != std::string::npos)
                    {
                        // Make sure it's a complete component (followed by _ or end of string)
                        size_t end = pos + search.size();
                        if (end == result.size() || result[end] == '_')
                        {
                            std::string replacement = "_" + binding->display_name();
                            result.replace(pos, search.size(), replacement);
                            any_substituted = true;
                            pos = result.find(search, pos + replacement.size());
                        }
                        else
                        {
                            pos = result.find(search, end);
                        }
                    }
                }
                if (any_substituted)
                {
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "substitute_type_annotation: Mangled name '{}' -> '{}'",
                              type_annotation, result);
                    return result;
                }
            }

            // Also check for exact base name match (handled by get_instantiated_scope_name)
            return get_instantiated_scope_name(type_annotation);
        }

        // Parse the base name and type arguments
        std::string base_name = type_annotation.substr(0, angle_pos);

        // Find matching closing bracket
        size_t close_pos = type_annotation.rfind('>');
        if (close_pos == std::string::npos || close_pos <= angle_pos)
            return "";

        std::string args_str = type_annotation.substr(angle_pos + 1, close_pos - angle_pos - 1);

        // Parse comma-separated type arguments (simple parsing, doesn't handle nested generics deeply)
        std::vector<std::string> type_args;
        std::string current_arg;
        int depth = 0;
        for (char c : args_str)
        {
            if (c == '<')
                depth++;
            else if (c == '>')
                depth--;
            else if (c == ',' && depth == 0)
            {
                // Trim whitespace
                size_t start = current_arg.find_first_not_of(" \t");
                size_t end = current_arg.find_last_not_of(" \t");
                if (start != std::string::npos)
                    type_args.push_back(current_arg.substr(start, end - start + 1));
                current_arg.clear();
                continue;
            }
            current_arg += c;
        }
        // Add last argument
        size_t start = current_arg.find_first_not_of(" \t");
        size_t end = current_arg.find_last_not_of(" \t");
        if (start != std::string::npos)
            type_args.push_back(current_arg.substr(start, end - start + 1));

        // Substitute type parameters in each argument
        std::vector<TypeRef> substituted_args;
        for (const auto &arg : type_args)
        {
            TypeRef resolved = resolve_type_param(arg);
            if (resolved.is_valid())
            {
                substituted_args.push_back(resolved);
            }
            else
            {
                // Try to look up the type by name in the arena
                TypeRef type = symbols().arena().lookup_type_by_name(arg);
                if (type.is_valid())
                {
                    substituted_args.push_back(type);
                }
                else
                {
                    // Unknown type - might be a nested generic or unresolved
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "substitute_type_annotation: Could not resolve type arg '{}'", arg);
                    return "";
                }
            }
        }

        // Generate mangled name for the substituted type
        std::string mangled = mangle_type_name(base_name, substituted_args);

        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                  "substitute_type_annotation: '{}' -> '{}'",
                  type_annotation, mangled);

        // Also ensure the dependent type is instantiated
        llvm::Type *existing = get_cached_type(mangled);
        if (!existing)
        {
            // Try to instantiate the dependent type
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "substitute_type_annotation: Instantiating dependent type '{}'", mangled);
            get_instantiated_type(base_name, substituted_args);
        }

        return mangled;
    }

    void GenericCodegen::end_type_params()
    {
        if (!_type_param_stack.empty())
        {
            _type_param_stack.pop_back();
        }

        // If the stack is now empty, clear the type parameter resolver
        if (_type_param_stack.empty())
        {
            types().clear_type_param_resolver();
        }
    }

    TypeRef GenericCodegen::resolve_type_param(const std::string &param_name)
    {
        // Search from innermost to outermost scope
        for (auto it = _type_param_stack.rbegin(); it != _type_param_stack.rend(); ++it)
        {
            auto found = it->bindings.find(param_name);
            if (found != it->bindings.end())
            {
                return found->second;
            }
        }
        return TypeRef{};
    }

    TypeRef GenericCodegen::substitute_type_params(TypeRef type)
    {
        if (!type.is_valid())
            return TypeRef{};

        // If type is a direct type parameter (e.g., "T"), resolve it
        if (type->kind() == TypeKind::GenericParam)
        {
            std::string type_name = type.get()->display_name();
            if (TypeRef resolved = resolve_type_param(type_name))
            {
                return resolved;
            }
        }

        // Handle pointer types - substitute the pointee
        if (type->kind() == TypeKind::Pointer)
        {
            auto *ptr_type = static_cast<const PointerType *>(type.get());
            TypeRef pointee = ptr_type->pointee();
            TypeRef substituted_pointee = substitute_type_params(pointee);

            // If pointee was substituted, create a new pointer type
            if (substituted_pointee != pointee && substituted_pointee.is_valid())
            {
                TypeRef new_ptr = symbols().arena().get_pointer_to(substituted_pointee);
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "GenericCodegen: Substituted pointer type {} -> {}",
                          type->display_name(), new_ptr->display_name());
                return new_ptr;
            }
            return type;
        }

        // Handle reference types - substitute the referent
        if (type->kind() == TypeKind::Reference)
        {
            auto *ref_type = static_cast<const ReferenceType *>(type.get());
            TypeRef referent = ref_type->referent();
            TypeRef substituted_referent = substitute_type_params(referent);

            // If referent was substituted, create a new reference type
            if (substituted_referent != referent && substituted_referent.is_valid())
            {
                TypeRef new_ref = symbols().arena().get_reference_to(substituted_referent, ref_type->mutability());
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "GenericCodegen: Substituted reference type {} -> {}",
                          type->display_name(), new_ref->display_name());
                return new_ref;
            }
            return type;
        }

        // Handle instantiated types (e.g., HashSet<T>) - substitute type arguments
        if (type->kind() == TypeKind::InstantiatedType)
        {
            auto *inst_type = static_cast<const InstantiatedType *>(type.get());
            TypeRef generic_base = inst_type->generic_base();
            const std::vector<TypeRef> &type_args = inst_type->type_args();

            // Substitute each type argument
            std::vector<TypeRef> substituted_args;
            bool any_substituted = false;

            for (const auto &arg : type_args)
            {
                TypeRef substituted_arg = substitute_type_params(arg);
                if (substituted_arg != arg)
                {
                    any_substituted = true;
                }
                substituted_args.push_back(substituted_arg.is_valid() ? substituted_arg : arg);
            }

            // If any argument was substituted, create a new instantiated type
            if (any_substituted)
            {
                TypeRef new_inst = symbols().arena().create_instantiation(generic_base, substituted_args);
                // Register in name caches so lookup_type_by_name() can find it
                symbols().arena().register_instantiated_by_name(new_inst);
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "GenericCodegen: Substituted instantiated type {} -> {}",
                          type->display_name(), new_inst->display_name());
                return new_inst;
            }
            return type;
        }

        // Handle array types - substitute element type
        if (type->kind() == TypeKind::Array)
        {
            auto *arr_type = static_cast<const ArrayType *>(type.get());
            TypeRef element = arr_type->element();
            TypeRef substituted_element = substitute_type_params(element);

            // If element was substituted, create a new array type
            if (substituted_element != element && substituted_element.is_valid())
            {
                TypeRef new_arr = symbols().arena().get_array_of(substituted_element, arr_type->size());
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "GenericCodegen: Substituted array type {} -> {}",
                          type->display_name(), new_arr->display_name());
                return new_arr;
            }
            return type;
        }

        // Handle function types - substitute parameter and return types
        if (type->kind() == TypeKind::Function)
        {
            auto *fn_type = static_cast<const FunctionType *>(type.get());
            TypeRef return_type = fn_type->return_type();
            const std::vector<TypeRef> &param_types = fn_type->param_types();

            // Substitute return type
            TypeRef substituted_return = substitute_type_params(return_type);

            // Substitute parameter types
            std::vector<TypeRef> substituted_params;
            bool any_substituted = (substituted_return != return_type);

            for (const auto &param : param_types)
            {
                TypeRef substituted_param = substitute_type_params(param);
                if (substituted_param != param)
                {
                    any_substituted = true;
                }
                substituted_params.push_back(substituted_param.is_valid() ? substituted_param : param);
            }

            // If anything was substituted, create a new function type
            if (any_substituted)
            {
                TypeRef new_fn = symbols().arena().get_function(
                    substituted_return.is_valid() ? substituted_return : return_type,
                    substituted_params,
                    fn_type->is_variadic());
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "GenericCodegen: Substituted function type {} -> {}",
                          type->display_name(), new_fn->display_name());
                return new_fn;
            }
            return type;
        }

        // Handle error types containing unresolved generics
        // These are created during parsing when types like "Option<T>" can't be resolved
        // because T is a generic parameter. During instantiation, we can now substitute.
        if (type->kind() == TypeKind::Error)
        {
            auto *error_type = static_cast<const ErrorType *>(type.get());
            const std::string &reason = error_type->reason();

            // Check for "unresolved generic: X<Y>" pattern
            const std::string prefix = "unresolved generic: ";
            if (reason.rfind(prefix, 0) == 0)
            {
                // Extract the type string (e.g., "Option<T>" from "unresolved generic: Option<T>")
                std::string type_str = reason.substr(prefix.length());

                // Use substitute_type_annotation to resolve type parameters
                // This returns the mangled name (e.g., "Option_voidp")
                std::string substituted_name = substitute_type_annotation(type_str);
                if (!substituted_name.empty())
                {
                    // Try lookup by mangled name first (e.g., "Option_voidp")
                    TypeRef resolved = symbols().arena().lookup_type_by_name(substituted_name);

                    // If that fails, try to construct and lookup by display name format
                    // The arena may have the type registered as "Option<void*>" not "Option_voidp"
                    if (!resolved.is_valid())
                    {
                        // Parse type_str to get base name and type args, then construct display name
                        size_t angle_pos = type_str.find('<');
                        if (angle_pos != std::string::npos)
                        {
                            std::string base_name = type_str.substr(0, angle_pos);
                            size_t close_pos = type_str.rfind('>');
                            if (close_pos != std::string::npos && close_pos > angle_pos)
                            {
                                std::string args_str = type_str.substr(angle_pos + 1, close_pos - angle_pos - 1);

                                // Substitute each type parameter
                                std::string display_name = base_name + "<";
                                bool first = true;
                                std::string current_arg;
                                int depth = 0;
                                for (size_t i = 0; i <= args_str.size(); ++i)
                                {
                                    char c = (i < args_str.size()) ? args_str[i] : ',';
                                    if (c == '<')
                                        depth++;
                                    else if (c == '>')
                                        depth--;
                                    else if ((c == ',' && depth == 0) || i == args_str.size())
                                    {
                                        // Trim whitespace
                                        size_t start = current_arg.find_first_not_of(" \t");
                                        size_t end = current_arg.find_last_not_of(" \t");
                                        if (start != std::string::npos)
                                        {
                                            std::string arg = current_arg.substr(start, end - start + 1);
                                            // Resolve type parameter
                                            TypeRef resolved_param = resolve_type_param(arg);
                                            if (!first)
                                                display_name += ", ";
                                            first = false;
                                            if (resolved_param.is_valid())
                                            {
                                                display_name += resolved_param->display_name();
                                            }
                                            else
                                            {
                                                display_name += arg;
                                            }
                                        }
                                        current_arg.clear();
                                        continue;
                                    }
                                    current_arg += c;
                                }
                                display_name += ">";

                                // Try lookup by display name (e.g., "Option<void*>")
                                resolved = symbols().arena().lookup_type_by_name(display_name);
                                if (!resolved.is_valid())
                                {
                                    // Also try with qualified namespace
                                    resolved = symbols().arena().lookup_type_by_name("std::core::option::" + display_name);
                                    if (!resolved.is_valid())
                                    {
                                        resolved = symbols().arena().lookup_type_by_name("std::core::result::" + display_name);
                                    }
                                }
                            }
                        }
                    }

                    if (resolved.is_valid())
                    {
                        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                  "GenericCodegen: Substituted error type '{}' -> '{}'",
                                  type->display_name(), resolved->display_name());
                        return resolved;
                    }
                }
            }

            // For other error types or if substitution fails, return as-is
            return type;
        }

        return type;
    }

    //===================================================================
    // Name Mangling
    //===================================================================

    std::string GenericCodegen::mangle_type_name(const std::string &generic_name,
                                                 const std::vector<TypeRef> &type_args)
    {
        if (type_args.empty())
        {
            return generic_name;
        }

        std::string result = generic_name + "_";

        for (size_t i = 0; i < type_args.size(); ++i)
        {
            if (i > 0)
                result += "_";

            if (type_args[i])
            {
                std::string arg_name = type_args[i].get()->display_name();
                // Replace problematic characters
                std::replace(arg_name.begin(), arg_name.end(), '<', '_');
                std::replace(arg_name.begin(), arg_name.end(), '>', '_');
                std::replace(arg_name.begin(), arg_name.end(), ',', '_');
                std::replace(arg_name.begin(), arg_name.end(), ' ', '_');
                std::replace(arg_name.begin(), arg_name.end(), '*', 'p');
                result += arg_name;
            }
        }

        return result;
    }

    std::string GenericCodegen::mangle_function_name(const std::string &generic_name,
                                                     const std::vector<TypeRef> &type_args)
    {
        // Same mangling scheme as types
        return mangle_type_name(generic_name, type_args);
    }

    //===================================================================
    // Instantiation Cache
    //===================================================================

    bool GenericCodegen::has_type_instantiation(const std::string &mangled_name) const
    {
        return _type_cache.find(mangled_name) != _type_cache.end();
    }

    bool GenericCodegen::has_function_instantiation(const std::string &mangled_name) const
    {
        return _function_cache.find(mangled_name) != _function_cache.end();
    }

    llvm::Type *GenericCodegen::get_cached_type(const std::string &mangled_name)
    {
        auto it = _type_cache.find(mangled_name);
        return (it != _type_cache.end()) ? it->second : nullptr;
    }

    llvm::Function *GenericCodegen::get_cached_function(const std::string &mangled_name)
    {
        auto it = _function_cache.find(mangled_name);
        return (it != _function_cache.end()) ? it->second : nullptr;
    }

    //===================================================================
    // Generic Definition Lookup
    //===================================================================

    void GenericCodegen::register_generic_type(const std::string &name, Cryo::ASTNode *node)
    {
        _generic_types[name] = node;
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "GenericCodegen: Registered generic type: {}", name);
    }

    bool GenericCodegen::is_generic_template(const std::string &name)
    {
        // Check local registry first (fast path)
        if (_generic_types.find(name) != _generic_types.end())
        {
            return true;
        }

        // Check TemplateRegistry for cross-module templates
        Cryo::TemplateRegistry *template_registry = ctx().template_registry();
        if (template_registry && template_registry->has_template(name))
        {
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "GenericCodegen::is_generic_template: Found '{}' in TemplateRegistry (cross-module)",
                      name);
            return true;
        }

        return false;
    }

    void GenericCodegen::register_generic_function(const std::string &name, Cryo::ASTNode *node)
    {
        _generic_functions[name] = node;
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "GenericCodegen: Registered generic function: {}", name);
    }

    Cryo::ASTNode *GenericCodegen::get_generic_type_def(const std::string &name)
    {
        // Check local registry first
        auto it = _generic_types.find(name);
        if (it != _generic_types.end())
        {
            return it->second;
        }

        // Fallback: check TemplateRegistry for cross-module templates
        Cryo::TemplateRegistry *template_registry = ctx().template_registry();
        if (template_registry)
        {
            const Cryo::TemplateRegistry::TemplateInfo *tmpl_info =
                template_registry->find_template(name);
            if (tmpl_info)
            {
                if (tmpl_info->struct_template)
                    return tmpl_info->struct_template;
                if (tmpl_info->class_template)
                    return tmpl_info->class_template;
                if (tmpl_info->enum_template)
                    return tmpl_info->enum_template;
            }
        }

        return nullptr;
    }

    Cryo::ASTNode *GenericCodegen::get_generic_function_def(const std::string &name)
    {
        auto it = _generic_functions.find(name);
        return (it != _generic_functions.end()) ? it->second : nullptr;
    }

    //===================================================================
    // Internal Helpers
    //===================================================================

    std::vector<llvm::Type *> GenericCodegen::create_substituted_fields(
        const std::vector<std::unique_ptr<Cryo::StructFieldNode>> &fields)
    {
        std::vector<llvm::Type *> result;
        result.reserve(fields.size());

        for (const auto &field : fields)
        {
            if (!field)
                continue;

            TypeRef field_type = field->get_resolved_type();
            TypeRef substituted = substitute_type_params(field_type);

            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "create_substituted_fields: Field '{}' type '{}' -> substituted '{}'",
                      field->name(),
                      field_type ? field_type->display_name() : "<null>",
                      substituted ? substituted->display_name() : "<null>");

            // Ensure any dependent generic types are instantiated before mapping
            // This is necessary because LLVM 15+ opaque pointers don't trigger
            // pointee type instantiation when mapping pointer types
            ensure_dependent_types_instantiated(substituted);

            llvm::Type *llvm_type = get_llvm_type(substituted);
            if (llvm_type)
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "create_substituted_fields: Field '{}' mapped to LLVM type ID {}",
                          field->name(), llvm_type->getTypeID());
                result.push_back(llvm_type);
            }
            else
            {
                // CRITICAL: We must not skip fields or the struct body will have
                // fewer elements than the field registry, causing assertion failures
                // when accessing fields by index. Use a fallback type.
                LOG_ERROR(Cryo::LogComponent::CODEGEN,
                          "create_substituted_fields: Failed to get LLVM type for field '{}' "
                          "(type: '{}'), using ptr fallback",
                          field->name(),
                          substituted ? substituted->display_name() : "<null>");

                // Use opaque pointer as fallback - this ensures field indices stay aligned
                // even if the type couldn't be fully resolved
                result.push_back(llvm::PointerType::get(llvm_ctx(), 0));
            }
        }

        return result;
    }

    std::vector<llvm::Type *> GenericCodegen::create_fields_from_specialized(
        const std::vector<std::unique_ptr<Cryo::StructFieldNode>> &fields)
    {
        std::vector<llvm::Type *> result;
        result.reserve(fields.size());

        for (const auto &field : fields)
        {
            if (!field)
                continue;

            // For specialized AST, types should already be concrete (T -> Token).
            // However, compound generic types (e.g., Inner<T>) may not be fully
            // substituted by the ASTTypeSubstituter. Apply substitution as fallback.
            TypeRef field_type = field->get_resolved_type();

            // If the field type still contains generic params (e.g., Inner<T> when
            // specializing Outer<i32>), apply type param substitution to resolve them.
            TypeRef substituted = substitute_type_params(field_type);
            if (substituted.is_valid())
                field_type = substituted;

            // Fallback: resolve from annotation if type is still empty
            if (!field_type.is_valid() && field->has_type_annotation())
            {
                field_type = resolve_field_type_from_annotation(field->type_annotation());
            }

            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "create_fields_from_specialized: Field '{}' has concrete type '{}'",
                      field->name(),
                      field_type ? field_type->display_name() : "<null>");

            // Ensure any dependent generic types are instantiated
            ensure_dependent_types_instantiated(field_type);

            llvm::Type *llvm_type = get_llvm_type(field_type);
            if (llvm_type)
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "create_fields_from_specialized: Field '{}' mapped to LLVM type ID {}",
                          field->name(), llvm_type->getTypeID());
                result.push_back(llvm_type);
            }
            else
            {
                // CRITICAL: We must not skip fields or the struct body will have
                // fewer elements than the field registry, causing assertion failures
                // when accessing fields by index. Use a fallback type.
                LOG_ERROR(Cryo::LogComponent::CODEGEN,
                          "create_fields_from_specialized: Failed to get LLVM type for field '{}' "
                          "(type: '{}'), using ptr fallback",
                          field->name(),
                          field_type ? field_type->display_name() : "<null>");

                // Use opaque pointer as fallback
                result.push_back(llvm::PointerType::get(llvm_ctx(), 0));
            }
        }

        return result;
    }

    void GenericCodegen::ensure_dependent_types_instantiated(TypeRef type)
    {
        if (!type.is_valid())
            return;

        // Handle pointer types - ensure the pointee is instantiated
        if (type->kind() == TypeKind::Pointer)
        {
            auto *ptr_type = static_cast<const PointerType *>(type.get());
            ensure_dependent_types_instantiated(ptr_type->pointee());
            return;
        }

        // Handle reference types - ensure the referent is instantiated
        if (type->kind() == TypeKind::Reference)
        {
            auto *ref_type = static_cast<const ReferenceType *>(type.get());
            ensure_dependent_types_instantiated(ref_type->referent());
            return;
        }

        // Handle array types - ensure the element type is instantiated
        if (type->kind() == TypeKind::Array)
        {
            auto *arr_type = static_cast<const ArrayType *>(type.get());
            ensure_dependent_types_instantiated(arr_type->element());
            return;
        }

        // Handle instantiated types - trigger instantiation
        if (type->kind() == TypeKind::InstantiatedType)
        {
            auto *inst_type = static_cast<const InstantiatedType *>(type.get());
            TypeRef generic_base = inst_type->generic_base();
            const std::vector<TypeRef> &type_args = inst_type->type_args();

            // First, ensure type arguments are also instantiated
            for (const auto &arg : type_args)
            {
                ensure_dependent_types_instantiated(arg);
            }

            // Then trigger instantiation of this type
            if (generic_base.is_valid())
            {
                std::string base_name = generic_base->display_name();

                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "GenericCodegen: Ensuring dependent type {} is instantiated",
                          type->display_name());

                get_instantiated_type(base_name, type_args);
            }
        }
    }

    llvm::FunctionType *GenericCodegen::create_substituted_function_type(
        Cryo::FunctionDeclarationNode *node)
    {
        if (!node)
            return nullptr;

        // Substitute return type
        TypeRef ret_type = substitute_type_params(node->get_resolved_return_type());
        // Ensure dependent types in return type are instantiated
        ensure_dependent_types_instantiated(ret_type);
        llvm::Type *llvm_ret = get_llvm_type(ret_type);
        if (!llvm_ret)
        {
            llvm_ret = llvm::Type::getVoidTy(llvm_ctx());
        }

        bool is_variadic = node->is_variadic();

        // Substitute parameter types
        std::vector<llvm::Type *> param_types;

        if (node->parameters().size() > 0)
        {
            for (const auto &param : node->parameters())
            {
                TypeRef param_type = substitute_type_params(param->get_resolved_type());
                // Ensure dependent types in parameter types are instantiated
                ensure_dependent_types_instantiated(param_type);
                llvm::Type *llvm_param = get_llvm_type(param_type);
                if (llvm_param)
                {
                    param_types.push_back(llvm_param);
                }
            }
        }

        return llvm::FunctionType::get(llvm_ret, param_types, is_variadic);
    }

    TypeRef GenericCodegen::resolve_field_type_from_annotation(const TypeAnnotation *annotation)
    {
        if (!annotation)
            return TypeRef();

        TypeArena &arena = symbols().arena();

        switch (annotation->kind)
        {
        case TypeAnnotationKind::Primitive:
        {
            const std::string &name = annotation->name;

            // Check type param bindings first (T could be annotated as a "primitive" name)
            TypeRef param = resolve_type_param(name);
            if (param.is_valid())
                return param;

            if (name == "void")
                return arena.get_void();
            if (name == "boolean")
                return arena.get_bool();
            if (name == "char")
                return arena.get_char();
            if (name == "string" || name == "String")
                return arena.get_string();
            if (name == "i8")
                return arena.get_i8();
            if (name == "i16")
                return arena.get_i16();
            if (name == "i32" || name == "int")
                return arena.get_i32();
            if (name == "i64")
                return arena.get_i64();
            if (name == "i128")
                return arena.get_i128();
            if (name == "u8")
                return arena.get_u8();
            if (name == "u16")
                return arena.get_u16();
            if (name == "u32" || name == "uint")
                return arena.get_u32();
            if (name == "u64")
                return arena.get_u64();
            if (name == "u128")
                return arena.get_u128();
            if (name == "f32" || name == "float")
                return arena.get_f32();
            if (name == "f64" || name == "double")
                return arena.get_f64();

            return TypeRef();
        }

        case TypeAnnotationKind::Named:
        {
            const std::string &name = annotation->name;

            // Check type param bindings first (e.g., T -> string)
            TypeRef param = resolve_type_param(name);
            if (param.is_valid())
                return param;

            TypeRef resolved = symbols().lookup_struct_type(name);
            if (resolved.is_valid() && !resolved.is_error())
                return resolved;

            resolved = symbols().lookup_class_type(name);
            if (resolved.is_valid() && !resolved.is_error())
                return resolved;

            resolved = symbols().lookup_enum_type(name);
            if (resolved.is_valid() && !resolved.is_error())
                return resolved;

            // Try with current namespace prefix
            std::string ns = ctx().namespace_context();
            if (!ns.empty())
            {
                std::string qualified = ns + "::" + name;
                resolved = symbols().lookup_struct_type(qualified);
                if (resolved.is_valid() && !resolved.is_error())
                    return resolved;

                resolved = symbols().lookup_class_type(qualified);
                if (resolved.is_valid() && !resolved.is_error())
                    return resolved;

                resolved = symbols().lookup_enum_type(qualified);
                if (resolved.is_valid() && !resolved.is_error())
                    return resolved;
            }

            // Try arena name lookup
            resolved = arena.lookup_type_by_name(name);
            if (resolved.is_valid() && !resolved.is_error())
                return resolved;

            // Check generic templates in GenericRegistry
            GenericRegistry *generics = types().generic_registry();
            if (generics)
            {
                auto template_info = generics->get_template_by_name(name);
                if (template_info)
                    return template_info->generic_type;
            }

            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "resolve_field_type_from_annotation: Could not resolve named type '{}'", name);
            return TypeRef();
        }

        case TypeAnnotationKind::Qualified:
        {
            std::string qualified_name;
            for (size_t i = 0; i < annotation->qualified_path.size(); ++i)
            {
                if (i > 0)
                    qualified_name += "::";
                qualified_name += annotation->qualified_path[i];
            }

            TypeRef resolved = symbols().lookup_struct_type(qualified_name);
            if (resolved.is_valid() && !resolved.is_error())
                return resolved;

            resolved = symbols().lookup_class_type(qualified_name);
            if (resolved.is_valid() && !resolved.is_error())
                return resolved;

            resolved = symbols().lookup_enum_type(qualified_name);
            if (resolved.is_valid() && !resolved.is_error())
                return resolved;

            return TypeRef();
        }

        case TypeAnnotationKind::Generic:
        {
            if (!annotation->inner)
                return TypeRef();

            TypeRef base_type = resolve_field_type_from_annotation(annotation->inner.get());
            if (!base_type.is_valid() || base_type.is_error())
                return base_type;

            std::vector<TypeRef> type_args;
            type_args.reserve(annotation->elements.size());

            for (const auto &arg_annotation : annotation->elements)
            {
                TypeRef arg_type = resolve_field_type_from_annotation(&arg_annotation);
                if (!arg_type.is_valid() || arg_type.is_error())
                    return arg_type;
                type_args.push_back(arg_type);
            }

            TypeRef instantiated = arena.create_instantiation(base_type, std::move(type_args));
            arena.register_instantiated_by_name(instantiated);

            // Ensure the LLVM struct for this nested generic is created
            ensure_dependent_types_instantiated(instantiated);

            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "resolve_field_type_from_annotation: Created instantiation '{}'",
                      instantiated.is_valid() ? instantiated->display_name() : "invalid");

            return instantiated;
        }

        case TypeAnnotationKind::Pointer:
        {
            if (!annotation->inner)
                return TypeRef();

            TypeRef inner = resolve_field_type_from_annotation(annotation->inner.get());
            if (!inner.is_valid() || inner.is_error())
                return inner;

            return arena.get_pointer_to(inner);
        }

        case TypeAnnotationKind::Reference:
        {
            if (!annotation->inner)
                return TypeRef();

            TypeRef inner = resolve_field_type_from_annotation(annotation->inner.get());
            if (!inner.is_valid() || inner.is_error())
                return inner;

            return arena.get_reference_to(inner,
                                          annotation->is_mutable ? RefMutability::Mutable : RefMutability::Immutable);
        }

        case TypeAnnotationKind::Array:
        {
            if (!annotation->inner)
                return TypeRef();

            TypeRef element = resolve_field_type_from_annotation(annotation->inner.get());
            if (!element.is_valid() || element.is_error())
                return element;

            return arena.get_array_of(element, annotation->array_size);
        }

        case TypeAnnotationKind::Optional:
        {
            if (!annotation->inner)
                return TypeRef();

            TypeRef inner = resolve_field_type_from_annotation(annotation->inner.get());
            if (!inner.is_valid() || inner.is_error())
                return inner;

            return arena.get_optional_of(inner);
        }

        case TypeAnnotationKind::Tuple:
        {
            std::vector<TypeRef> elements;
            elements.reserve(annotation->elements.size());

            for (const auto &elem : annotation->elements)
            {
                TypeRef resolved = resolve_field_type_from_annotation(&elem);
                if (!resolved.is_valid() || resolved.is_error())
                    return resolved;
                elements.push_back(resolved);
            }

            return arena.get_tuple(std::move(elements));
        }

        case TypeAnnotationKind::Function:
        {
            TypeRef return_type;
            if (annotation->return_type)
            {
                return_type = resolve_field_type_from_annotation(annotation->return_type.get());
                if (!return_type.is_valid() || return_type.is_error())
                    return return_type;
            }
            else
            {
                return_type = arena.get_void();
            }

            std::vector<TypeRef> param_types;
            param_types.reserve(annotation->elements.size());

            for (const auto &param : annotation->elements)
            {
                TypeRef resolved = resolve_field_type_from_annotation(&param);
                if (!resolved.is_valid() || resolved.is_error())
                    return resolved;
                param_types.push_back(resolved);
            }

            return arena.get_function(return_type, std::move(param_types), annotation->is_variadic);
        }

        default:
            return TypeRef();
        }
    }

} // namespace Cryo::Codegen
