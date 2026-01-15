#include "Codegen/Declarations/GenericCodegen.hpp"
#include "Codegen/Declarations/DeclarationCodegen.hpp"
#include "Codegen/Declarations/TypeCodegen.hpp"
#include "Codegen/CodegenVisitor.hpp"
#include "Utils/Logger.hpp"

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

            // TypeKind::Generic means it's an uninstantiated type parameter
            if (arg->kind() == TypeKind::Generic)
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "GenericCodegen: Skipping instantiation of {} - type arg '{}' is a generic parameter",
                          generic_name, arg->to_string());
                return nullptr;
            }

            // Also check if this would map to an opaque/undefined struct
            // This catches cases where type parameters are misclassified as Struct types
            if (arg->kind() == TypeKind::Struct || arg->kind() == TypeKind::Class)
            {
                llvm::StructType *existing = llvm::StructType::getTypeByName(llvm_ctx(), arg->to_string());
                if (!existing || existing->isOpaque())
                {
                    // This is likely an uninstantiated type parameter masquerading as a struct
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "GenericCodegen: Skipping instantiation of {} - type arg '{}' is undefined/opaque",
                              generic_name, arg->to_string());
                    return nullptr;
                }
            }
        }

        // Generate mangled name
        std::string mangled = mangle_type_name(generic_name, type_args);

        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "GenericCodegen: Instantiating struct {} -> {}",
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

        // Check if already exists in LLVM context
        if (llvm::StructType *existing = llvm::StructType::getTypeByName(llvm_ctx(), mangled))
        {
            _type_cache[mangled] = existing;
            return existing;
        }

        // Get generic type definition
        Cryo::ASTNode *generic_def = get_generic_type_def(generic_name);
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

        // Begin type parameter scope
        begin_type_params(type_params, type_args);

        // Create struct type
        llvm::StructType *struct_type = llvm::StructType::create(llvm_ctx(), mangled);

        // Substitute type parameters in fields and collect field names
        std::vector<std::string> field_names;
        if (struct_decl)
        {
            std::vector<llvm::Type *> field_types = create_substituted_fields(struct_decl->fields());
            struct_type->setBody(field_types);

            // Collect field names for member access resolution
            field_names.reserve(struct_decl->fields().size());
            for (const auto &field : struct_decl->fields())
            {
                field_names.push_back(field->name());
            }
        }

        // Cache and register in multiple places for consistency BEFORE generating methods
        // This ensures the struct type is available for self-referential types
        _type_cache[mangled] = struct_type;
        ctx().register_type(mangled, struct_type);
        types().register_struct(mangled, struct_type);

        // Register type's namespace for cross-module method resolution
        // Use the base generic type's namespace if registered, otherwise use current context
        std::string base_namespace = ctx().get_type_namespace(generic_name);
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

            for (const auto &method : struct_decl->methods())
            {
                if (!method)
                    continue;

                // Generate method with the mangled struct name as parent type
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

                    // Set current type name for this.field resolution
                    ctx().set_current_type_name(mangled);

                    // Enter function scope
                    values().enter_scope(fn->getName().str());

                    // Allocate parameters
                    for (auto &arg : fn->args())
                    {
                        llvm::AllocaInst *alloca = create_entry_alloca(fn, arg.getType(), arg.getName().str());
                        create_store(&arg, alloca);
                        values().set_value(arg.getName().str(), nullptr, alloca);
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
        }

        // End type parameter scope AFTER methods are generated
        end_type_params();

        // Also build the unmangled name (e.g., "Array<u64>") for lookup consistency
        std::string instantiated_name = generic_name + "<";
        for (size_t i = 0; i < type_args.size(); ++i)
        {
            if (i > 0) instantiated_name += ", ";
            if (type_args[i])
            {
                instantiated_name += type_args[i]->to_string();
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

            if (arg->kind() == TypeKind::Generic)
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "GenericCodegen: Skipping class instantiation of {} - type arg '{}' is a generic parameter",
                          generic_name, arg->to_string());
                return nullptr;
            }

            if (arg->kind() == TypeKind::Struct || arg->kind() == TypeKind::Class)
            {
                llvm::StructType *existing = llvm::StructType::getTypeByName(llvm_ctx(), arg->to_string());
                if (!existing || existing->isOpaque())
                {
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "GenericCodegen: Skipping class instantiation of {} - type arg '{}' is undefined/opaque",
                              generic_name, arg->to_string());
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
        if (llvm::StructType *existing = llvm::StructType::getTypeByName(llvm_ctx(), mangled))
        {
            _type_cache[mangled] = existing;
            return existing;
        }

        // Get generic definition
        Cryo::ASTNode *generic_def = get_generic_type_def(generic_name);
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

        // Begin substitution scope
        begin_type_params(type_params, type_args);

        // Create class struct
        llvm::StructType *class_type = llvm::StructType::create(llvm_ctx(), mangled);

        // Substitute type parameters in fields and collect field names
        std::vector<std::string> field_names;
        if (class_decl)
        {
            std::vector<llvm::Type *> field_types = create_substituted_fields(class_decl->fields());
            class_type->setBody(field_types);

            // Collect field names for member access resolution
            field_names.reserve(class_decl->fields().size());
            for (const auto &field : class_decl->fields())
            {
                field_names.push_back(field->name());
            }
        }

        // Cache and register in multiple places for consistency BEFORE generating methods
        _type_cache[mangled] = class_type;
        ctx().register_type(mangled, class_type);
        types().register_struct(mangled, class_type);

        // Register type's namespace for cross-module method resolution
        // Use the base generic type's namespace if registered, otherwise use current context
        std::string base_namespace = ctx().get_type_namespace(generic_name);
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

            for (const auto &method : class_decl->methods())
            {
                if (!method)
                    continue;

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

                    // Set current type name for this.field resolution
                    ctx().set_current_type_name(mangled);

                    // Enter function scope
                    values().enter_scope(fn->getName().str());

                    // Allocate parameters
                    for (auto &arg : fn->args())
                    {
                        llvm::AllocaInst *alloca = create_entry_alloca(fn, arg.getType(), arg.getName().str());
                        create_store(&arg, alloca);
                        values().set_value(arg.getName().str(), nullptr, alloca);
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
        }

        // End substitution scope AFTER methods are generated
        end_type_params();

        // Also build the unmangled name (e.g., "MyClass<u64>") for lookup consistency
        std::string instantiated_name = generic_name + "<";
        for (size_t i = 0; i < type_args.size(); ++i)
        {
            if (i > 0) instantiated_name += ", ";
            if (type_args[i])
            {
                instantiated_name += type_args[i]->to_string();
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

    llvm::Type *GenericCodegen::get_instantiated_type(const std::string &generic_name,
                                                        const std::vector<TypeRef> &type_args)
    {
        std::string mangled = mangle_type_name(generic_name, type_args);

        // Check cache first
        if (has_type_instantiation(mangled))
        {
            return get_cached_type(mangled);
        }

        // Try to instantiate
        Cryo::ASTNode *def = get_generic_type_def(generic_name);
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

            if (arg->kind() == TypeKind::Generic)
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "GenericCodegen: Skipping function instantiation of {} - type arg '{}' is a generic parameter",
                          generic_name, arg->to_string());
                return nullptr;
            }

            if (arg->kind() == TypeKind::Struct || arg->kind() == TypeKind::Class)
            {
                llvm::StructType *existing = llvm::StructType::getTypeByName(llvm_ctx(), arg->to_string());
                if (!existing || existing->isOpaque())
                {
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "GenericCodegen: Skipping function instantiation of {} - type arg '{}' is undefined/opaque",
                              generic_name, arg->to_string());
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

        // Generate body if available
        if (func_decl->body() && _declarations)
        {
            // The body generation would need to use substituted types
            // For now, we just create the declaration
            // Full body generation would be handled by DeclarationCodegen
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

    //===================================================================
    // Type Parameter Handling
    //===================================================================

    void GenericCodegen::begin_type_params(const std::vector<std::string> &params,
                                             const std::vector<TypeRef> &args)
    {
        TypeParamScope scope;

        size_t count = std::min(params.size(), args.size());
        for (size_t i = 0; i < count; ++i)
        {
            scope.bindings[params[i]] = args[i];
            LOG_DEBUG(Cryo::LogComponent::CODEGEN, "GenericCodegen: Binding {} -> {}",
                      params[i], args[i] ? args[i]->to_string() : "null");
        }

        _type_param_stack.push_back(std::move(scope));

        // Set the type parameter resolver on TypeMapper so type lookups can resolve T, E, etc.
        types().set_type_param_resolver([this](const std::string &name) -> TypeRef {
            return this->resolve_type_param(name);
        });
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
        return nullptr;
    }

    TypeRef GenericCodegen::substitute_type_params(TypeRef type)
    {
        if (!type)
            return nullptr;

        // If type is a type parameter, resolve it
        std::string type_name = type->to_string();
        if (TypeRef resolved = resolve_type_param(type_name))
        {
            return resolved;
        }

        // TODO: Handle generic types with nested type parameters
        // e.g., Array<T> where T is a type parameter

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
                std::string arg_name = type_args[i]->to_string();
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

    bool GenericCodegen::is_generic_template(const std::string &name) const
    {
        return _generic_types.find(name) != _generic_types.end();
    }

    void GenericCodegen::register_generic_function(const std::string &name, Cryo::ASTNode *node)
    {
        _generic_functions[name] = node;
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "GenericCodegen: Registered generic function: {}", name);
    }

    Cryo::ASTNode *GenericCodegen::get_generic_type_def(const std::string &name)
    {
        auto it = _generic_types.find(name);
        return (it != _generic_types.end()) ? it->second : nullptr;
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

            llvm::Type *llvm_type = get_llvm_type(substituted);
            if (llvm_type)
            {
                result.push_back(llvm_type);
            }
        }

        return result;
    }

    llvm::FunctionType *GenericCodegen::create_substituted_function_type(
        Cryo::FunctionDeclarationNode *node)
    {
        if (!node)
            return nullptr;

        // Substitute return type
        TypeRef ret_type = substitute_type_params(node->get_resolved_return_type());
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
                llvm::Type *llvm_param = get_llvm_type(param_type);
                if (llvm_param)
                {
                    param_types.push_back(llvm_param);
                }
            }
        }

        return llvm::FunctionType::get(llvm_ret, param_types, is_variadic);
    }

} // namespace Cryo::Codegen
