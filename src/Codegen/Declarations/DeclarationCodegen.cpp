#include "Codegen/Declarations/DeclarationCodegen.hpp"
#include "Codegen/Declarations/GenericCodegen.hpp"
#include "Codegen/Memory/MemoryCodegen.hpp"
#include "Codegen/CodegenVisitor.hpp"
#include "Utils/Logger.hpp"

#include <llvm/IR/Verifier.h>

namespace Cryo::Codegen
{
    //===================================================================
    // Construction
    //===================================================================

    DeclarationCodegen::DeclarationCodegen(CodegenContext &ctx)
        : ICodegenComponent(ctx)
    {
    }

    //===================================================================
    // Function Declarations
    //===================================================================

    llvm::Function *DeclarationCodegen::generate_function_declaration(Cryo::FunctionDeclarationNode *node)
    {
        if (!node)
        {
            report_error(ErrorCode::E0633_FUNCTION_BODY_ERROR, "Null function declaration node");
            return nullptr;
        }

        // Generate properly qualified function name using namespace context
        std::string name;
        std::string ns_context = ctx().namespace_context();
        
        // Special case: main and _user_main_ functions should never be namespace qualified
        if (node->name() == "main" || node->name() == "_user_main_")
        {
            name = node->name();
        }
        else if (!ns_context.empty())
        {
            // Use fully-qualified name: namespace::function
            name = ns_context + "::" + node->name();
        }
        else
        {
            name = node->name();
        }

        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "DeclarationCodegen: Generating function declaration: {} (namespace: '{}')", name, ns_context);

        // Check if already declared
        if (llvm::Function *existing = module()->getFunction(name))
        {
            return existing;
        }

        // Get function type
        llvm::FunctionType *fn_type = get_function_type(node);
        if (!fn_type)
        {
            report_error(ErrorCode::E0633_FUNCTION_BODY_ERROR, node,
                         "Failed to get function type for: " + name);
            return nullptr;
        }

        // Create function
        llvm::GlobalValue::LinkageTypes linkage = get_linkage(node);
        llvm::Function *fn = llvm::Function::Create(fn_type, linkage, name, module());

        // Apply attributes
        apply_function_attributes(fn, node);

        // Name parameters
        generate_parameters(fn, node);

        // Register in context with the qualified name
        ctx().register_function(name, fn);
        
        // Also register with the unqualified name for backward compatibility
        std::string unqualified_name = node->name();
        if (name != unqualified_name)
        {
            ctx().register_function(unqualified_name, fn);
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "DeclarationCodegen: Also registered function as: {}", unqualified_name);
        }

        return fn;
    }

    llvm::Function *DeclarationCodegen::generate_function_definition(Cryo::FunctionDeclarationNode *node)
    {
        if (!node)
        {
            report_error(ErrorCode::E0633_FUNCTION_BODY_ERROR, "Null function declaration node");
            return nullptr;
        }

        // Generate properly qualified function name using namespace context
        std::string name;
        std::string ns_context = ctx().namespace_context();
        
        // Special case: main and _user_main_ functions should never be namespace qualified
        if (node->name() == "main" || node->name() == "_user_main_")
        {
            name = node->name();
        }
        else if (!ns_context.empty())
        {
            // Use fully-qualified name: namespace::function
            name = ns_context + "::" + node->name();
        }
        else
        {
            name = node->name();
        }

        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "DeclarationCodegen: Generating function definition: {} (namespace: '{}')", name, ns_context);

        // Get or create declaration
        llvm::Function *fn = module()->getFunction(name);
        if (!fn)
        {
            fn = generate_function_declaration(node);
            if (!fn)
                return nullptr;
        }

        // Always ensure parameters are named - the function might have been
        // pre-declared by CallCodegen::get_or_create_function without parameter names
        generate_parameters(fn, node);

        // Skip if already has a body
        if (!fn->empty())
        {
            LOG_DEBUG(Cryo::LogComponent::CODEGEN, "Function already has body: {}", name);
            return fn;
        }

        // Create entry block - generate_function_body will handle context/scope setup
        llvm::BasicBlock *entry_block = llvm::BasicBlock::Create(llvm_ctx(), "entry", fn);
        builder().SetInsertPoint(entry_block);

        // Generate function body (handles context/scope setup and teardown)
        generate_function_body(fn, node);

        // Clear any stale result from previous expressions
        ctx().set_result(nullptr);

        // Verify function
        if (llvm::verifyFunction(*fn, &llvm::errs()))
        {
            LOG_ERROR(Cryo::LogComponent::CODEGEN, "Function verification failed: {}", name);
            fn->eraseFromParent();
            return nullptr;
        }

        return fn;
    }

    llvm::Function *DeclarationCodegen::generate_method_declaration(Cryo::FunctionDeclarationNode *node,
                                                                    const std::string &parent_type)
    {
        if (!node)
            return nullptr;

        // Skip methods of generic templates - they should only be instantiated with concrete types
        if (_generics && !parent_type.empty() && _generics->is_generic_template(parent_type))
        {
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "DeclarationCodegen: Skipping method declaration '{}' of generic template '{}'",
                      node->name(), parent_type);
            return nullptr;
        }

        // Additional check: detect uninstantiated type parameters in signature
        // This catches cases where the generic template check above didn't work
        for (const auto &param : node->parameters())
        {
            if (param && param->get_resolved_type())
            {
                Cryo::Type *ptype = param->get_resolved_type();
                // Check for Generic type kind or undefined struct/class types
                if (ptype->kind() == TypeKind::Generic)
                {
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "DeclarationCodegen: Skipping method '{}' - param '{}' has generic type '{}'",
                              node->name(), param->name(), ptype->to_string());
                    return nullptr;
                }
                if (ptype->kind() == TypeKind::Struct || ptype->kind() == TypeKind::Class)
                {
                    // Check if this type exists in LLVM context - if not, it's likely a type parameter
                    llvm::StructType *existing = llvm::StructType::getTypeByName(llvm_ctx(), ptype->to_string());
                    if (!existing)
                    {
                        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                  "DeclarationCodegen: Skipping method '{}' - param '{}' has undefined type '{}'",
                                  node->name(), param->name(), ptype->to_string());
                        return nullptr;
                    }
                }
            }
        }

        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "DeclarationCodegen: Generating method: {}::{}",
                  parent_type, node->name());

        // Generate base method name (Type::method)
        std::string base_method_name = generate_method_name(parent_type, node->name());

        // Build fully-qualified name if we have a namespace context
        std::string ns_context = ctx().namespace_context();
        std::string llvm_fn_name = base_method_name;
        if (!ns_context.empty())
        {
            // Use fully-qualified name: namespace::Type::method
            llvm_fn_name = ns_context + "::" + base_method_name;
        }

        // Check if already declared (check both names to handle existing declarations)
        if (llvm::Function *existing = module()->getFunction(llvm_fn_name))
        {
            return existing;
        }
        if (llvm::Function *existing = module()->getFunction(base_method_name))
        {
            return existing;
        }

        // Get function type with implicit 'this' parameter
        llvm::FunctionType *fn_type = get_function_type(node, true);
        if (!fn_type)
        {
            report_error(ErrorCode::E0633_FUNCTION_BODY_ERROR, node,
                         "Failed to get method type for: " + llvm_fn_name);
            return nullptr;
        }

        // Create function with fully-qualified name
        llvm::Function *fn = llvm::Function::Create(fn_type, llvm::Function::ExternalLinkage,
                                                    llvm_fn_name, module());

        // Name parameters (first is 'this')
        auto arg_it = fn->arg_begin();
        arg_it->setName("this");
        ++arg_it;

        // Name remaining parameters
        if (node->parameters().size() > 0)
        {
            for (const auto &param : node->parameters())
            {
                if (arg_it != fn->arg_end())
                {
                    arg_it->setName(param->name());
                    ++arg_it;
                }
            }
        }

        // Register with both names for lookups
        ctx().register_function(llvm_fn_name, fn);
        if (llvm_fn_name != base_method_name)
        {
            ctx().register_function(base_method_name, fn);
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "DeclarationCodegen: Registered method as: {} (also as {})", llvm_fn_name, base_method_name);
        }
        else
        {
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "DeclarationCodegen: Registered method as: {}", llvm_fn_name);
        }

        return fn;
    }

    llvm::Function *DeclarationCodegen::generate_extern_function(Cryo::FunctionDeclarationNode *node)
    {
        if (!node)
            return nullptr;

        std::string name = node->name();
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "DeclarationCodegen: Generating extern function: {}", name);

        // Check if already declared
        if (llvm::Function *existing = module()->getFunction(name))
        {
            return existing;
        }

        // Get return type
        llvm::Type *return_type = types().get_type(node->get_resolved_return_type());
        if (!return_type)
        {
            return_type = llvm::Type::getVoidTy(llvm_ctx());
        }

        // Get parameter types
        std::vector<llvm::Type *> param_types;
        bool is_variadic = node->is_variadic();

        if (node->parameters().size() > 0)
        {
            for (const auto &param : node->parameters())
            {
                llvm::Type *param_type = types().get_type(param->get_resolved_type());
                if (param_type)
                {
                    param_types.push_back(param_type);
                }
            }
        }

        // Create function type and declaration
        llvm::FunctionType *fn_type = llvm::FunctionType::get(return_type, param_types, is_variadic);
        llvm::Function *fn = llvm::Function::Create(fn_type, llvm::Function::ExternalLinkage,
                                                    name, module());

        ctx().register_function(name, fn);
        return fn;
    }

    //===================================================================
    // Variable Declarations
    //===================================================================

    llvm::AllocaInst *DeclarationCodegen::generate_local_variable(Cryo::VariableDeclarationNode *node)
    {
        if (!node)
            return nullptr;

        std::string name = node->name();
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "DeclarationCodegen: Generating local variable: {}", name);

        // Get variable type
        Cryo::Type *cryo_var_type = node->get_resolved_type();
        llvm::Type *var_type = get_llvm_type(cryo_var_type);
        if (!var_type)
        {
            report_error(ErrorCode::E0634_VARIABLE_INITIALIZATION_ERROR, node,
                         "Unknown type for variable: " + name);
            return nullptr;
        }

        // Debug: Log the LLVM type being used
        {
            std::string llvm_type_str;
            llvm::raw_string_ostream rso(llvm_type_str);
            var_type->print(rso);
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "DeclarationCodegen: Variable '{}' Cryo type: {}, LLVM type: {}, isStructTy: {}",
                      name,
                      cryo_var_type ? cryo_var_type->to_string() : "null",
                      llvm_type_str,
                      var_type->isStructTy() ? "true" : "false");
        }

        // Create alloca in entry block
        llvm::AllocaInst *alloca = create_entry_alloca(var_type, name);
        if (!alloca)
        {
            report_error(ErrorCode::E0634_VARIABLE_INITIALIZATION_ERROR, node,
                         "Failed to allocate variable: " + name);
            return nullptr;
        }

        // Generate initializer if present
        if (node->initializer())
        {
            CodegenVisitor *visitor = ctx().visitor();
            node->initializer()->accept(*visitor);
            llvm::Value *init_val = get_result();
            if (init_val)
            {
                llvm::Value *cast_val = cast_if_needed(init_val, var_type);
                create_store(cast_val, alloca);
            }
        }

        // Register in value context
        values().set_value(name, nullptr, alloca);

        // Register the variable type in variable_types_map for Array<T> detection
        Cryo::Type *resolved_type = node->get_resolved_type();
        if (resolved_type)
        {
            ctx().variable_types_map()[name] = resolved_type;
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "DeclarationCodegen: Registered local variable type: {} -> {} (kind: {})",
                      name, resolved_type->to_string(),
                      static_cast<int>(resolved_type->kind()));

            // Special logging for potential Array<T> types
            if (resolved_type->kind() == TypeKind::Array || resolved_type->to_string().find("[]") != std::string::npos)
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "DeclarationCodegen: *** Registered Array<T> variable: {} with type: {}",
                          name, resolved_type->to_string());
            }
        }
        else
        {
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "DeclarationCodegen: No resolved type available for local variable: {}", name);
        }

        return alloca;
    }

    llvm::GlobalVariable *DeclarationCodegen::generate_global_variable(Cryo::VariableDeclarationNode *node)
    {
        if (!node)
            return nullptr;

        std::string name = node->name();
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "DeclarationCodegen: Generating global variable: {}", name);

        // Check if already exists
        if (llvm::GlobalVariable *existing = module()->getGlobalVariable(name))
        {
            // If it exists but is just an external declaration (no initializer),
            // we should update it with the actual initializer
            if (!existing->hasInitializer() && node->initializer())
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "Global '{}' exists as external declaration, adding initializer", name);

                // Generate initializer
                llvm::Constant *initializer = generate_global_initializer(node, existing->getValueType());
                if (initializer)
                {
                    existing->setInitializer(initializer);
                    existing->setConstant(!node->is_mutable());
                }
            }
            else
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN, "Global variable already exists: {}", name);
            }
            return existing;
        }

        // Get variable type from the AST node's resolved type
        Cryo::Type *cryo_type = node->get_resolved_type();
        if (!cryo_type)
        {
            report_error(ErrorCode::E0634_VARIABLE_INITIALIZATION_ERROR, node,
                         "No resolved type for global variable: " + name);
            return nullptr;
        }

        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "DeclarationCodegen: Global '{}' has Cryo type: {} (kind={})",
                  name, cryo_type->to_string(), Cryo::TypeKindToString(cryo_type->kind()));

        llvm::Type *var_type = get_llvm_type(cryo_type);
        if (!var_type)
        {
            report_error(ErrorCode::E0634_VARIABLE_INITIALIZATION_ERROR, node,
                         "Unknown type for global variable: " + name);
            return nullptr;
        }

        // IMPORTANT: For class types or uninstantiated generic types that resolve to pointers,
        // we need to use the actual struct type, not a pointer type.
        // This handles forward references where a variable is declared before its class definition.
        // Classes are value types when declared as variables (the variable holds the struct inline),
        // not reference types (which would be a pointer to heap-allocated memory).
        bool is_class_or_generic = (cryo_type->kind() == Cryo::TypeKind::Class ||
                                    cryo_type->kind() == Cryo::TypeKind::Generic);
        if (is_class_or_generic && var_type->isPointerTy())
        {
            LOG_WARN(Cryo::LogComponent::CODEGEN,
                     "DeclarationCodegen: Global '{}' has {} type but got pointer LLVM type. "
                     "Looking up struct type directly.",
                     name, Cryo::TypeKindToString(cryo_type->kind()));

            // Get the type's name and look up the struct directly
            std::string type_name = cryo_type->name();
            llvm::StructType *struct_type = llvm::StructType::getTypeByName(llvm_ctx(), type_name);
            if (!struct_type)
            {
                // Try with namespace context
                std::string ns_context = ctx().namespace_context();
                if (!ns_context.empty())
                {
                    std::string qualified_name = ns_context + "::" + type_name;
                    struct_type = llvm::StructType::getTypeByName(llvm_ctx(), qualified_name);
                    if (struct_type)
                    {
                        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                  "DeclarationCodegen: Found struct type with qualified name '{}'", qualified_name);
                    }
                }
            }
            if (!struct_type)
            {
                // Try common namespace patterns for runtime types
                std::vector<std::string> candidates = {
                    type_name,
                    "std::Runtime::" + type_name,
                    "std::runtime::" + type_name,
                    "Runtime::" + type_name
                };
                for (const auto &candidate : candidates)
                {
                    struct_type = llvm::StructType::getTypeByName(llvm_ctx(), candidate);
                    if (struct_type)
                    {
                        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                  "DeclarationCodegen: Found struct type with candidate name '{}'", candidate);
                        break;
                    }
                }
            }
            if (!struct_type)
            {
                // Create an opaque struct as fallback - it should be completed when the class is processed
                struct_type = llvm::StructType::create(llvm_ctx(), type_name);
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "DeclarationCodegen: Created opaque struct for type '{}'", type_name);
            }
            if (struct_type)
            {
                var_type = struct_type;
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "DeclarationCodegen: Using struct type '{}' for global '{}'",
                          type_name, name);
            }
        }

        // Log the LLVM type for debugging
        std::string type_str;
        llvm::raw_string_ostream rso(type_str);
        var_type->print(rso);
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "DeclarationCodegen: Global '{}' LLVM type: {}",
                  name, type_str);

        // Generate initializer
        llvm::Constant *initializer = generate_global_initializer(node, var_type);

        // Track if this global has an explicit initializer in source code
        // Only globals with explicit initializers like `= Type()` should have constructors called
        // Globals declared without initializer like `mut x: Type;` should NOT be auto-constructed
        if (node->initializer() && var_type->isStructTy())
        {
            _globals_with_explicit_initializers.insert(name);
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "DeclarationCodegen: Global '{}' has explicit initializer, will call constructor",
                      name);
        }

        // Log the initializer type for debugging
        if (initializer)
        {
            std::string init_type_str;
            llvm::raw_string_ostream init_rso(init_type_str);
            initializer->getType()->print(init_rso);
            LOG_DEBUG(Cryo::LogComponent::CODEGEN, "DeclarationCodegen: Global '{}' initializer type: {}",
                      name, init_type_str);

            // Verify type match before creating global
            if (initializer->getType() != var_type)
            {
                LOG_ERROR(Cryo::LogComponent::CODEGEN,
                          "DeclarationCodegen: TYPE MISMATCH for global '{}': var_type vs init_type",
                          name);
            }
        }

        // Create global variable
        bool is_constant = !node->is_mutable();
        llvm::GlobalValue::LinkageTypes linkage = get_linkage(node);

        llvm::GlobalVariable *global = new llvm::GlobalVariable(
            *module(),
            var_type,
            is_constant,
            linkage,
            initializer,
            name);

        // Register in value context for later lookup
        // Register with simple name
        values().set_global_value(name, global);

        // Also register with fully-qualified name (namespace::name) for SRM-based lookups
        std::string ns_context = ctx().namespace_context();
        if (!ns_context.empty())
        {
            std::string qualified_name = ns_context + "::" + name;
            values().set_global_value(qualified_name, global);
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "DeclarationCodegen: Also registered global as: {}", qualified_name);
        }

        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "DeclarationCodegen: Registered global variable: {}", name);

        // Register the variable type in variable_types_map for Array<T> detection
        if (cryo_type)
        {
            ctx().variable_types_map()[name] = cryo_type;
            // Also register qualified name if exists
            if (!ns_context.empty())
            {
                std::string qualified_name = ns_context + "::" + name;
                ctx().variable_types_map()[qualified_name] = cryo_type;
            }
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "DeclarationCodegen: Registered global variable type: {} -> {}",
                      name, cryo_type->to_string());
        }

        return global;
    }

    llvm::Constant *DeclarationCodegen::generate_constant(Cryo::VariableDeclarationNode *node)
    {
        if (!node)
            return nullptr;

        std::string name = node->name();
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "DeclarationCodegen: Generating constant: {}", name);

        // Generate constant value
        if (node->initializer())
        {
            CodegenVisitor *visitor = ctx().visitor();
            node->initializer()->accept(*visitor);
            llvm::Value *val = get_result();
            if (auto *constant = llvm::dyn_cast<llvm::Constant>(val))
            {
                return constant;
            }
        }

        report_error(ErrorCode::E0634_VARIABLE_INITIALIZATION_ERROR, node,
                     "Constant initializer is not a compile-time constant: " + name);
        return nullptr;
    }

    //===================================================================
    // Type Declarations
    //===================================================================

    llvm::StructType *DeclarationCodegen::generate_struct_declaration(Cryo::StructDeclarationNode *node)
    {
        if (!node)
            return nullptr;

        std::string name = node->name();
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "DeclarationCodegen: Generating struct: {}", name);

        // Check if already declared
        if (llvm::StructType *existing = llvm::StructType::getTypeByName(llvm_ctx(), name))
        {
            return existing;
        }

        // Create opaque struct first (for recursive types)
        llvm::StructType *struct_type = llvm::StructType::create(llvm_ctx(), name);

        // Collect field types
        std::vector<llvm::Type *> field_types;
        for (const auto &field : node->fields())
        {
            llvm::Type *field_type = get_llvm_type(field->get_resolved_type());
            if (field_type)
            {
                field_types.push_back(field_type);
            }
        }

        // Set struct body
        struct_type->setBody(field_types);

        // Register type
        ctx().register_type(name, struct_type);

        return struct_type;
    }

    llvm::StructType *DeclarationCodegen::generate_class_declaration(Cryo::ClassDeclarationNode *node)
    {
        if (!node)
            return nullptr;

        std::string name = node->name();
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "DeclarationCodegen: Generating class: {}", name);

        // Check if already declared
        if (llvm::StructType *existing = llvm::StructType::getTypeByName(llvm_ctx(), name))
        {
            return existing;
        }

        // Create opaque struct
        llvm::StructType *class_type = llvm::StructType::create(llvm_ctx(), name);

        // Collect field types (including inherited fields if any)
        std::vector<llvm::Type *> field_types;

        // Add vtable pointer if class has virtual methods
        // (simplified - full implementation would check for virtual methods)
        // field_types.push_back(llvm::PointerType::get(llvm_ctx(), 0));

        for (const auto &field : node->fields())
        {
            llvm::Type *field_type = get_llvm_type(field->get_resolved_type());
            if (field_type)
            {
                field_types.push_back(field_type);
            }
        }

        // Set class body
        class_type->setBody(field_types);

        // Register type
        ctx().register_type(name, class_type);

        return class_type;
    }

    llvm::Type *DeclarationCodegen::generate_enum_declaration(Cryo::EnumDeclarationNode *node)
    {
        if (!node)
            return nullptr;

        std::string name = node->name();
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "DeclarationCodegen: Generating enum: {}", name);

        // Simple enums are just integers
        // Tagged unions would need struct representation
        llvm::Type *enum_type = llvm::Type::getInt32Ty(llvm_ctx());

        // Register type
        ctx().register_type(name, enum_type);

        // Generate variant constants and register them
        int32_t index = 0;
        for (const auto &variant : node->variants())
        {
            std::string variant_name = name + "::" + variant->name();
            llvm::Constant *value = llvm::ConstantInt::get(llvm::Type::getInt32Ty(llvm_ctx()), index++);
            // Register enum variant in the context for scope resolution lookup
            ctx().register_enum_variant(variant_name, value);
            LOG_DEBUG(Cryo::LogComponent::CODEGEN, "DeclarationCodegen: Registered enum variant: {} = {}",
                      variant_name, index - 1);
        }

        return enum_type;
    }

    //===================================================================
    // Helpers
    //===================================================================

    llvm::FunctionType *DeclarationCodegen::get_function_type(Cryo::FunctionDeclarationNode *node,
                                                              bool has_this_param)
    {
        if (!node)
            return nullptr;

        // Get return type
        llvm::Type *return_type = get_llvm_type(node->get_resolved_return_type());
        if (!return_type)
        {
            return_type = llvm::Type::getVoidTy(llvm_ctx());
        }

        // Collect parameter types
        std::vector<llvm::Type *> param_types;

        // Add 'this' pointer for methods
        if (has_this_param)
        {
            param_types.push_back(llvm::PointerType::get(llvm_ctx(), 0));
        }

        // Add regular parameters
        bool is_variadic = node->is_variadic();
        if (node->parameters().size() > 0)
        {
            for (const auto &param : node->parameters())
            {
                llvm::Type *param_type = get_llvm_type(param->get_resolved_type());
                if (param_type)
                {
                    param_types.push_back(param_type);
                }
            }
        }

        return llvm::FunctionType::get(return_type, param_types, is_variadic);
    }

    std::string DeclarationCodegen::mangle_function_name(const std::string &name,
                                                         const std::vector<std::string> &namespace_parts,
                                                         const std::vector<Cryo::Type *> &param_types)
    {
        std::string result;

        // Add namespace prefix
        for (const auto &ns : namespace_parts)
        {
            result += ns + "::";
        }

        result += name;

        // Add parameter type suffix for overloading (simplified)
        if (!param_types.empty())
        {
            result += "(";
            for (size_t i = 0; i < param_types.size(); ++i)
            {
                if (i > 0)
                    result += ",";
                if (param_types[i])
                {
                    result += param_types[i]->to_string();
                }
            }
            result += ")";
        }

        return result;
    }

    std::string DeclarationCodegen::generate_method_name(const std::string &type_name,
                                                         const std::string &method_name,
                                                         const std::vector<Cryo::Type *> &param_types)
    {
        std::string result = type_name + "::" + method_name;

        // Add parameter types for overload resolution
        if (!param_types.empty())
        {
            result += "(";
            for (size_t i = 0; i < param_types.size(); ++i)
            {
                if (i > 0)
                    result += ",";
                if (param_types[i])
                {
                    result += param_types[i]->to_string();
                }
            }
            result += ")";
        }

        return result;
    }

    llvm::Function *DeclarationCodegen::get_or_create_function(const std::string &name,
                                                               llvm::FunctionType *fn_type,
                                                               llvm::GlobalValue::LinkageTypes linkage)
    {
        llvm::Function *fn = module()->getFunction(name);
        if (fn)
            return fn;

        fn = llvm::Function::Create(fn_type, linkage, name, module());
        ctx().register_function(name, fn);
        return fn;
    }

    bool DeclarationCodegen::is_function_declared(const std::string &name) const
    {
        return module()->getFunction(name) != nullptr;
    }

    //===================================================================
    // Internal Helpers
    //===================================================================

    void DeclarationCodegen::generate_parameters(llvm::Function *fn,
                                                 Cryo::FunctionDeclarationNode *node,
                                                 unsigned start_idx)
    {
        if (!fn || !node)
            return;

        auto arg_it = fn->arg_begin();
        std::advance(arg_it, start_idx);

        for (const auto &param : node->parameters())
        {
            if (arg_it == fn->arg_end())
                break;

            arg_it->setName(param->name());
            ++arg_it;
        }
    }

    void DeclarationCodegen::generate_function_body(llvm::Function *fn,
                                                    Cryo::FunctionDeclarationNode *node)
    {
        if (!fn || !node || !node->body())
            return;

        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "Generating body for function: {}", fn->getName().str());

        // Set current function context for variable allocation
        auto fn_ctx = std::make_unique<FunctionContext>(fn, node);
        fn_ctx->entry_block = &fn->getEntryBlock();
        ctx().set_current_function(std::move(fn_ctx));

        // Enter function scope for local variables
        values().enter_scope(fn->getName().str());

        // Allocate space for parameters
        for (auto &arg : fn->args())
        {
            llvm::AllocaInst *alloca = create_entry_alloca(fn, arg.getType(), arg.getName().str());
            create_store(&arg, alloca);
            values().set_value(arg.getName().str(), nullptr, alloca);
        }

        // Register parameter types in variable_types_map for Array<T> detection
        // This is critical for array access to correctly determine element types
        const auto &ast_params = node->parameters();
        size_t param_idx = 0;
        for (auto &arg : fn->args())
        {
            if (param_idx < ast_params.size())
            {
                Cryo::Type *param_type = ast_params[param_idx]->get_resolved_type();
                if (param_type)
                {
                    std::string param_name = arg.getName().str();
                    ctx().variable_types_map()[param_name] = param_type;
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "DeclarationCodegen: Registered function parameter type: {} -> {} (kind: {})",
                              param_name, param_type->to_string(),
                              static_cast<int>(param_type->kind()));

                    // Special logging for Array<T> parameters
                    if (param_type->kind() == TypeKind::Array ||
                        param_type->to_string().find("[]") != std::string::npos)
                    {
                        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                  "DeclarationCodegen: *** Registered Array<T> parameter: {} with type: {}",
                                  param_name, param_type->to_string());
                    }
                }
            }
            param_idx++;
        }

        // Generate body statements
        CodegenVisitor *visitor = ctx().visitor();
        node->body()->accept(*visitor);

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
                // Return default value
                llvm::Value *default_val = llvm::Constant::getNullValue(fn->getReturnType());
                builder().CreateRet(default_val);
            }
        }

        // Exit function scope
        values().exit_scope();

        // Clear function context
        ctx().clear_current_function();
    }

    llvm::Function *DeclarationCodegen::generate_default_constructor(const std::string &type_name,
                                                                     llvm::StructType *struct_type)
    {
        std::string ctor_name = type_name + "::init";

        // Check if already exists
        if (llvm::Function *existing = module()->getFunction(ctor_name))
        {
            return existing;
        }

        // Constructor takes 'this' pointer and returns void
        llvm::Type *this_type = llvm::PointerType::get(llvm_ctx(), 0);
        llvm::FunctionType *ctor_type = llvm::FunctionType::get(
            llvm::Type::getVoidTy(llvm_ctx()), {this_type}, false);

        llvm::Function *ctor = llvm::Function::Create(ctor_type, llvm::Function::ExternalLinkage,
                                                      ctor_name, module());

        // Create body
        llvm::BasicBlock *entry = llvm::BasicBlock::Create(llvm_ctx(), "entry", ctor);
        builder().SetInsertPoint(entry);

        // Zero-initialize all fields
        llvm::Value *this_ptr = ctor->arg_begin();
        for (unsigned i = 0; i < struct_type->getNumElements(); ++i)
        {
            llvm::Value *field_ptr = create_struct_gep(struct_type, this_ptr, i, "field." + std::to_string(i));
            llvm::Type *field_type = struct_type->getElementType(i);
            llvm::Value *zero = llvm::Constant::getNullValue(field_type);
            create_store(zero, field_ptr);
        }

        builder().CreateRetVoid();

        ctx().register_function(ctor_name, ctor);
        return ctor;
    }

    llvm::Function *DeclarationCodegen::generate_destructor(const std::string &type_name,
                                                            llvm::StructType *class_type)
    {
        std::string dtor_name = type_name + "::destroy";

        // Check if already exists
        if (llvm::Function *existing = module()->getFunction(dtor_name))
        {
            return existing;
        }

        // Destructor takes 'this' pointer and returns void
        llvm::Type *this_type = llvm::PointerType::get(llvm_ctx(), 0);
        llvm::FunctionType *dtor_type = llvm::FunctionType::get(
            llvm::Type::getVoidTy(llvm_ctx()), {this_type}, false);

        llvm::Function *dtor = llvm::Function::Create(dtor_type, llvm::Function::ExternalLinkage,
                                                      dtor_name, module());

        // Create empty body (subclasses can override)
        llvm::BasicBlock *entry = llvm::BasicBlock::Create(llvm_ctx(), "entry", dtor);
        builder().SetInsertPoint(entry);
        builder().CreateRetVoid();

        ctx().register_function(dtor_name, dtor);
        return dtor;
    }

    llvm::GlobalValue::LinkageTypes DeclarationCodegen::get_linkage(Cryo::ASTNode *node) const
    {
        // Check for public/private visibility
        // For now, default to external linkage
        return llvm::GlobalValue::ExternalLinkage;
    }

    void DeclarationCodegen::apply_function_attributes(llvm::Function *fn,
                                                       Cryo::FunctionDeclarationNode *node)
    {
        if (!fn || !node)
            return;

        // Add common attributes
        fn->addFnAttr(llvm::Attribute::NoUnwind);

        // Check for inline hint
        // fn->addFnAttr(llvm::Attribute::InlineHint);
    }

    llvm::Constant *DeclarationCodegen::generate_global_initializer(Cryo::VariableDeclarationNode *node,
                                                                    llvm::Type *type)
    {
        if (!node || !type)
            return llvm::Constant::getNullValue(type);

        // If there's an initializer, try to evaluate it
        if (node->initializer())
        {
            // Visit the initializer expression to get its value
            CodegenVisitor *visitor = ctx().visitor();
            if (visitor)
            {
                node->initializer()->accept(*visitor);
                llvm::Value *val = get_result();
                // Guard against null - dyn_cast asserts on null
                if (!val)
                {
                    LOG_WARN(Cryo::LogComponent::CODEGEN,
                             "generate_global_initializer: No result for initializer of '{}'",
                             node->name());
                    return llvm::Constant::getNullValue(type);
                }
                if (auto *constant = llvm::dyn_cast<llvm::Constant>(val))
                {
                    // Cast to target type if needed
                    if (constant->getType() != type)
                    {
                        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                  "generate_global_initializer: Casting for '{}' from source to target type",
                                  node->name());

                        // Handle integer to integer casts (with proper bit width handling)
                        if (constant->getType()->isIntegerTy() && type->isIntegerTy())
                        {
                            if (auto constInt = llvm::dyn_cast<llvm::ConstantInt>(constant))
                            {
                                llvm::IntegerType *dstIntTy = llvm::cast<llvm::IntegerType>(type);
                                llvm::APInt value = constInt->getValue();

                                // Extend or truncate the APInt to match destination bit width
                                unsigned srcBits = value.getBitWidth();
                                unsigned dstBits = dstIntTy->getBitWidth();

                                if (dstBits > srcBits)
                                {
                                    // Zero-extend for unsigned types
                                    value = value.zext(dstBits);
                                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                              "generate_global_initializer: Zero-extended {} bits -> {} bits for '{}'",
                                              srcBits, dstBits, node->name());
                                }
                                else if (dstBits < srcBits)
                                {
                                    // Truncate
                                    value = value.trunc(dstBits);
                                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                              "generate_global_initializer: Truncated {} bits -> {} bits for '{}'",
                                              srcBits, dstBits, node->name());
                                }

                                return llvm::ConstantInt::get(dstIntTy, value);
                            }
                            // Fallback: use bitcast or return as-is
                            return constant;
                        }

                        // Handle float to float casts
                        if (constant->getType()->isFloatingPointTy() && type->isFloatingPointTy())
                        {
                            if (auto constFP = llvm::dyn_cast<llvm::ConstantFP>(constant))
                            {
                                if (type->isFloatTy())
                                {
                                    return llvm::ConstantFP::get(type, constFP->getValueAPF().convertToFloat());
                                }
                                else if (type->isDoubleTy())
                                {
                                    return llvm::ConstantFP::get(type, constFP->getValueAPF().convertToDouble());
                                }
                            }
                            // Fallback: use bitcast or return as-is
                            return constant;
                        }

                        // Handle float to integer casts (for hex literals that got parsed as float)
                        if (constant->getType()->isFloatingPointTy() && type->isIntegerTy())
                        {
                            if (auto constFP = llvm::dyn_cast<llvm::ConstantFP>(constant))
                            {
                                llvm::IntegerType *dstIntTy = llvm::cast<llvm::IntegerType>(type);
                                // Convert double to integer by reinterpreting the bit pattern
                                // This handles cases like 0xDEADBEEF being parsed as float
                                double fpVal = constFP->getValueAPF().convertToDouble();
                                uint64_t intVal = static_cast<uint64_t>(fpVal);
                                LOG_WARN(Cryo::LogComponent::CODEGEN,
                                         "generate_global_initializer: Converting float {} to int {} for '{}' (possible literal parsing issue)",
                                         fpVal, intVal, node->name());
                                return llvm::ConstantInt::get(dstIntTy, intVal);
                            }
                        }

                        // For other cases, just use the constant as-is and let LLVM handle it
                        LOG_WARN(Cryo::LogComponent::CODEGEN,
                                 "Global initializer type mismatch for '{}', may cause issues",
                                 node->name());
                    }
                    return constant;
                }
                // If not a constant, log a warning
                LOG_WARN(Cryo::LogComponent::CODEGEN,
                         "Global initializer for '{}' is not a compile-time constant, using zero",
                         node->name());
            }
        }

        // Default to zero initializer
        return llvm::Constant::getNullValue(type);
    }

    //===================================================================
    // High-Level Entry Points (called by CodegenVisitor)
    //===================================================================

    llvm::Function *DeclarationCodegen::generate_function(Cryo::FunctionDeclarationNode *node)
    {
        if (!node)
            return nullptr;

        // If function has a body, generate full definition; otherwise just declaration
        if (node->body())
        {
            return generate_function_definition(node);
        }
        else
        {
            return generate_function_declaration(node);
        }
    }

    void DeclarationCodegen::generate_variable(Cryo::VariableDeclarationNode *node)
    {
        if (!node)
            return;

        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "DeclarationCodegen::generate_variable called for: {}", node->name());

        // Check if we're at global scope or function scope using the proper function context
        // DO NOT use builder().GetInsertBlock() - it can be stale from previous processing
        FunctionContext *fn_ctx = ctx().current_function();

        if (fn_ctx && fn_ctx->function)
        {
            // Local variable - we're inside a function body
            LOG_DEBUG(Cryo::LogComponent::CODEGEN, "DeclarationCodegen: {} is local variable (in function {})",
                      node->name(), fn_ctx->function->getName().str());
            generate_local_variable(node);
        }
        else
        {
            // Global variable - no active function context
            LOG_DEBUG(Cryo::LogComponent::CODEGEN, "DeclarationCodegen: {} is global variable (no active function context)",
                      node->name());
            generate_global_variable(node);
        }
    }

    void DeclarationCodegen::generate_method(Cryo::StructMethodNode *node)
    {
        if (!node)
            return;

        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "DeclarationCodegen: Generating struct method: {}", node->name());

        // Get the parent type name from context
        std::string parent_type = ctx().current_type_name();

        // Skip methods of generic templates - they should only be instantiated with concrete types
        if (_generics && !parent_type.empty() && _generics->is_generic_template(parent_type))
        {
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "DeclarationCodegen: Skipping method '{}' of generic template '{}'",
                      node->name(), parent_type);
            return;
        }

        // Additional check: detect uninstantiated type parameters in signature
        for (const auto &param : node->parameters())
        {
            if (param && param->get_resolved_type())
            {
                Cryo::Type *ptype = param->get_resolved_type();
                if (ptype->kind() == TypeKind::Generic)
                {
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "DeclarationCodegen: Skipping method '{}' - param has generic type '{}'",
                              node->name(), ptype->to_string());
                    return;
                }
                if (ptype->kind() == TypeKind::Struct || ptype->kind() == TypeKind::Class)
                {
                    llvm::StructType *existing = llvm::StructType::getTypeByName(llvm_ctx(), ptype->to_string());
                    if (!existing)
                    {
                        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                  "DeclarationCodegen: Skipping method '{}' - param has undefined type '{}'",
                                  node->name(), ptype->to_string());
                        return;
                    }
                }
            }
        }

        std::string base_method_name = generate_method_name(parent_type, node->name());

        // Build fully-qualified name for logging
        std::string ns_context = ctx().namespace_context();
        std::string method_name = base_method_name;
        if (!ns_context.empty())
        {
            method_name = ns_context + "::" + base_method_name;
        }

        // Generate method declaration (will return existing if already created)
        llvm::Function *fn = generate_method_declaration(node, parent_type);

        // If the method has a body and the function is empty, generate the body
        if (fn && node->body() && fn->empty())
        {
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "DeclarationCodegen: Generating body for method: {}", fn->getName().str());
                
                // Special debug for main function
                if (fn->getName().contains("main")) 
                {
                    LOG_ERROR(Cryo::LogComponent::CODEGEN,
                             "=== MAIN FUNCTION DEBUG: Starting body generation for '{}'", fn->getName().str());
                }
            llvm::BasicBlock *entry = llvm::BasicBlock::Create(llvm_ctx(), "entry", fn);
            builder().SetInsertPoint(entry);

            // Set up function context for proper scope isolation
            auto fn_ctx = std::make_unique<FunctionContext>(fn, node);
            fn_ctx->entry_block = entry;
            ctx().set_current_function(std::move(fn_ctx));

            // Enter function scope
            values().enter_scope(method_name);

            // Clear stale result
            ctx().set_result(nullptr);

            // Allocate parameters
            for (auto &arg : fn->args())
            {
                llvm::AllocaInst *alloca = create_entry_alloca(fn, arg.getType(), arg.getName().str());
                create_store(&arg, alloca);
                values().set_value(arg.getName().str(), nullptr, alloca);

                // Register 'this' type in variable_types_map for member access resolution
                if (arg.getName() == "this")
                {
                    // Get the Cryo type for the current struct/class
                    Cryo::Type *this_type = ctx().symbols().get_type_context()->get_struct_type(parent_type);
                    if (!this_type)
                    {
                        this_type = ctx().symbols().get_type_context()->get_class_type(parent_type);
                    }
                    if (this_type)
                    {
                        ctx().variable_types_map()["this"] = this_type;
                        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                  "Registered 'this' type for struct method: {} -> {}",
                                  parent_type, this_type->to_string());
                    }
                }
            }

            // Register all parameter types in variable_types_map for Array<T> detection
            // StructMethodNode inherits from FunctionDeclarationNode so parameters() is available
            const auto &ast_params = node->parameters();
            size_t param_idx = 0;
            for (auto &arg : fn->args())
            {
                // Skip 'this' as it's handled above
                if (arg.getName() == "this")
                {
                    param_idx++;
                    continue;
                }

                // Account for 'this' parameter offset - AST params don't include 'this'
                size_t ast_idx = param_idx > 0 ? param_idx - 1 : param_idx;
                if (ast_idx < ast_params.size())
                {
                    Cryo::Type *param_type = ast_params[ast_idx]->get_resolved_type();
                    if (param_type)
                    {
                        std::string param_name = arg.getName().str();
                        ctx().variable_types_map()[param_name] = param_type;
                        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                  "DeclarationCodegen: Registered method parameter type: {} -> {} (kind: {})",
                                  param_name, param_type->to_string(),
                                  static_cast<int>(param_type->kind()));

                        // Special logging for Array<T> parameters
                        if (param_type->kind() == TypeKind::Array ||
                            param_type->to_string().find("[]") != std::string::npos)
                        {
                            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                      "DeclarationCodegen: *** Registered Array<T> method parameter: {} with type: {}",
                                      param_name, param_type->to_string());
                        }
                    }
                }
                param_idx++;
            }

            // Generate body
            CodegenVisitor *visitor = ctx().visitor();
            node->body()->accept(*visitor);

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

            // Exit function scope and clean up
            values().exit_scope();
            ctx().clear_current_function();
            ctx().set_result(nullptr);
        }
    }

    void DeclarationCodegen::generate_impl_block(Cryo::ImplementationBlockNode *node)
    {
        if (!node)
            return;

        std::string type_name = node->target_type();
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "DeclarationCodegen: Generating impl block for {}", type_name);

        // Skip generic type implementations - they should only be instantiated with concrete types
        // Generic types have unresolved type parameters like <T>, <T, E>, etc.
        if (type_name.find('<') != std::string::npos)
        {
            // Check if it contains unresolved generic parameters (single capital letters)
            size_t start = type_name.find('<');
            size_t end = type_name.find('>');
            if (start != std::string::npos && end != std::string::npos && end > start)
            {
                std::string params = type_name.substr(start + 1, end - start - 1);
                // Check for simple generic parameters like T, E, K, V (single uppercase letters)
                // or common patterns like T, E separated by commas
                bool has_generic_param = false;
                size_t pos = 0;
                while (pos < params.length())
                {
                    // Skip whitespace
                    while (pos < params.length() && (params[pos] == ' ' || params[pos] == ','))
                        pos++;
                    if (pos >= params.length())
                        break;

                    // Check if this is a single uppercase letter (generic parameter)
                    if (std::isupper(params[pos]))
                    {
                        size_t param_end = pos + 1;
                        while (param_end < params.length() && std::isalnum(params[param_end]))
                            param_end++;

                        // If it's a single letter or looks like a type parameter, it's generic
                        if (param_end == pos + 1 || (param_end - pos <= 2 && std::isupper(params[pos])))
                        {
                            has_generic_param = true;
                            break;
                        }
                        pos = param_end;
                    }
                    else
                    {
                        // Skip to next separator
                        while (pos < params.length() && params[pos] != ',' && params[pos] != ' ')
                            pos++;
                    }
                }

                if (has_generic_param)
                {
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "Skipping generic impl block for {} - will be instantiated with concrete types",
                              type_name);
                    return;
                }
            }
        }

        // Set current type context
        std::string previous_type = ctx().current_type_name();
        ctx().set_current_type_name(type_name);

        // Two-pass approach: First generate all method declarations (forward references)
        // This ensures that methods can call other methods defined later in the impl block
        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                  "Pass 1: Generating {} method declarations for {}",
                  node->method_implementations().size(), type_name);

        for (const auto &method : node->method_implementations())
        {
            Cryo::StructMethodNode *fn_node = method.get();
            if (fn_node)
            {
                generate_method_declaration(fn_node, type_name);
            }
        }

        // Second pass: Generate method bodies
        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                  "Pass 2: Generating method bodies for {}", type_name);

        for (const auto &method : node->method_implementations())
        {
            // StructMethodNode IS a FunctionDeclarationNode
            Cryo::StructMethodNode *fn_node = method.get();
            if (!fn_node)
                continue;

            // Generate method body
            if (fn_node->body())
            {
                // Full definition - function should already be declared
                std::string method_name = generate_method_name(type_name, fn_node->name());
                llvm::Function *fn = module()->getFunction(method_name);
                if (fn && fn->empty())
                {
                    // Create entry block and generate body
                    llvm::BasicBlock *entry = llvm::BasicBlock::Create(llvm_ctx(), "entry", fn);
                    builder().SetInsertPoint(entry);

                    // Set up function context for proper scope isolation
                    auto fn_ctx = std::make_unique<FunctionContext>(fn, fn_node);
                    fn_ctx->entry_block = entry;
                    ctx().set_current_function(std::move(fn_ctx));

                    // Enter function scope
                    values().enter_scope(method_name);

                    // Clear stale result
                    ctx().set_result(nullptr);

                    // Allocate parameters
                    for (auto &arg : fn->args())
                    {
                        llvm::AllocaInst *alloca = create_entry_alloca(fn, arg.getType(), arg.getName().str());
                        create_store(&arg, alloca);
                        values().set_value(arg.getName().str(), nullptr, alloca);

                        // Register 'this' type in variable_types_map for member access resolution
                        if (arg.getName() == "this")
                        {
                            // Get the Cryo type for the current struct/class
                            Cryo::Type *this_type = ctx().symbols().get_type_context()->get_struct_type(type_name);
                            if (!this_type)
                            {
                                this_type = ctx().symbols().get_type_context()->get_class_type(type_name);
                            }
                            if (this_type)
                            {
                                ctx().variable_types_map()["this"] = this_type;
                                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                          "Registered 'this' type for method: {} -> {}",
                                          type_name, this_type->to_string());
                            }
                        }
                    }

                    // Generate body
                    CodegenVisitor *visitor = ctx().visitor();
                    fn_node->body()->accept(*visitor);

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

                    // Exit function scope and clean up
                    values().exit_scope();
                    ctx().clear_current_function();
                    ctx().set_result(nullptr);
                }
            }
            // Note: methods without bodies were already declared in pass 1
        }

        // Restore previous type context
        ctx().set_current_type_name(previous_type);
    }

    void DeclarationCodegen::generate_extern_block(Cryo::ExternBlockNode *node)
    {
        if (!node)
            return;

        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "DeclarationCodegen: Generating extern block with linkage: {}",
                  node->linkage_type());

        // Generate each function declaration in the extern block
        for (const auto &fn_decl : node->function_declarations())
        {
            if (fn_decl)
            {
                generate_extern_function(fn_decl.get());
            }
        }
    }

    void DeclarationCodegen::pre_register_functions()
    {
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "DeclarationCodegen: Pre-registering functions from symbol table");

        // This would iterate through the symbol table and pre-declare all functions
        // For now, this is a no-op - functions are declared on first use
    }

    void DeclarationCodegen::import_specialized_methods(const Cryo::TypeChecker &type_checker)
    {
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "DeclarationCodegen: Importing specialized methods");

        // This would import specialized generic methods from the type checker
        // For now, this is a no-op
    }

    void DeclarationCodegen::process_global_variables(Cryo::ASTNode *node)
    {
        if (!node)
            return;

        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "DeclarationCodegen: Processing global variables");

        // Walk the AST and generate global variable declarations
        if (auto *program = dynamic_cast<Cryo::ProgramNode *>(node))
        {
            for (const auto &stmt : program->statements())
            {
                if (auto *var_node = dynamic_cast<Cryo::VariableDeclarationNode *>(stmt.get()))
                {
                    generate_global_variable(var_node);
                }
            }
        }
    }

    void DeclarationCodegen::generate_global_constructors()
    {
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "DeclarationCodegen: Generating global constructors");

        llvm::Module *mod = module();
        if (!mod)
        {
            LOG_ERROR(Cryo::LogComponent::CODEGEN, "No module available for global constructor generation");
            return;
        }

        llvm::LLVMContext &ctx = llvm_ctx();

        // Check if we need global constructors (look for global variables with non-trivial types)
        std::vector<llvm::GlobalVariable*> globals_needing_ctors;

        for (auto &global : mod->globals())
        {
            // Skip LLVM internal globals
            if (global.getName().starts_with("llvm.") || global.getName().starts_with(".str"))
            {
                continue;
            }

            // Skip LLVM internal globals and some runtime internals
            // but allow proper user-defined globals to be processed normally
            std::string gname = global.getName().str();
            if (gname.starts_with("llvm.") || gname.starts_with(".str"))
            {
                continue;
            }

            // Only process globals that have explicit initializers in source code
            // (e.g., `mut x: Type = Type();`)
            // Globals declared without initializers (e.g., `mut x: Type;`) should NOT
            // be auto-constructed - the user is responsible for initializing them
            std::string global_name = global.getName().str();
            if (_globals_with_explicit_initializers.find(global_name) != _globals_with_explicit_initializers.end())
            {
                llvm::Type *global_type = global.getValueType();
                if (global_type->isStructTy())
                {
                    globals_needing_ctors.push_back(&global);
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                             "Global '{}' has explicit initializer, will call constructor",
                             global_name);
                }
            }
            else if (global.getValueType()->isStructTy())
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                         "Global '{}' declared without initializer, skipping auto-construction",
                         global_name);
            }
        }

        if (globals_needing_ctors.empty())
        {
            LOG_DEBUG(Cryo::LogComponent::CODEGEN, "No global variables need constructors");
            return;
        }

        // Create global constructor function
        llvm::FunctionType *ctor_type = llvm::FunctionType::get(llvm::Type::getVoidTy(ctx), false);
        llvm::Function *global_ctor = llvm::Function::Create(
            ctor_type,
            llvm::Function::InternalLinkage,
            "__cryo_global_constructors",
            mod);

        global_ctor->addFnAttr(llvm::Attribute::NoInline);
        global_ctor->addFnAttr(llvm::Attribute::OptimizeNone);

        // Create function body with a LOCAL builder to avoid corrupting shared builder state
        llvm::BasicBlock *entry_block = llvm::BasicBlock::Create(ctx, "entry", global_ctor);
        llvm::IRBuilder<> local_builder(entry_block);

        // Generate constructor calls for globals that need them
        for (llvm::GlobalVariable *global : globals_needing_ctors)
        {
            llvm::Type *global_type = global->getValueType();
            if (llvm::StructType *struct_type = llvm::dyn_cast<llvm::StructType>(global_type))
            {
                std::string type_name = struct_type->hasName() ? struct_type->getName().str() : "unnamed_struct";
                std::string global_name = global->getName().str();

                // Generic constructor lookup
                std::string ctor_name = type_name + "::init";

                // Look for constructor function
                if (llvm::Function *ctor_func = mod->getFunction(ctor_name))
                {
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                             "Calling constructor '{}' for global '{}'",
                             ctor_name, global_name);

                    // Call constructor with global variable as 'this' pointer
                    std::vector<llvm::Value*> ctor_args = { global };
                    local_builder.CreateCall(ctor_func, ctor_args);
                }
                else
                {
                    // Try alternative naming patterns
                    std::vector<std::string> alt_names = {
                        type_name + "()",
                        type_name + "::" + type_name,
                        "std::Runtime::" + type_name + "::" + type_name
                    };

                    bool found = false;
                    for (const std::string &alt_name : alt_names)
                    {
                        if (llvm::Function *alt_ctor = mod->getFunction(alt_name))
                        {
                            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                     "Calling constructor '{}' for global '{}'",
                                     alt_name, global_name);

                            std::vector<llvm::Value*> ctor_args = { global };
                            local_builder.CreateCall(alt_ctor, ctor_args);
                            found = true;
                            break;
                        }
                    }

                    if (!found)
                    {
                        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                 "No constructor found for type '{}' (tried multiple patterns)",
                                 type_name);
                    }
                }
            }
        }

        // Return from global constructor
        local_builder.CreateRetVoid();

        // Register with LLVM's global constructor mechanism
        llvm::Type *i32_type = llvm::Type::getInt32Ty(ctx);
        // In opaque pointer mode (LLVM 15+), all pointers are just 'ptr'
        // global_ctor->getType() returns FunctionType, NOT a pointer type
        // We need PointerType for the struct entry
        llvm::PointerType *ptr_type = llvm::PointerType::get(ctx, 0);

        // Create array type for global_ctors entry: { i32, ptr, ptr }
        // The standard @llvm.global_ctors format uses opaque pointers
        std::vector<llvm::Type*> ctor_entry_types = {
            i32_type,  // priority
            ptr_type,  // constructor function pointer (opaque ptr)
            ptr_type   // associated data (null)
        };
        llvm::StructType *ctor_entry_type = llvm::StructType::get(ctx, ctor_entry_types);

        // Create the global_ctors entry
        // Note: global_ctor (llvm::Function*) is already a Constant and pointer-compatible
        std::vector<llvm::Constant*> ctor_entry_values = {
            llvm::ConstantInt::get(i32_type, 65535),     // priority (default)
            global_ctor,                                  // constructor function (Function* is a Constant*)
            llvm::ConstantPointerNull::get(ptr_type)     // no associated data
        };
        llvm::Constant *ctor_entry = llvm::ConstantStruct::get(ctor_entry_type, ctor_entry_values);

        // Create global_ctors array
        llvm::ArrayType *ctor_array_type = llvm::ArrayType::get(ctor_entry_type, 1);
        llvm::Constant *ctor_array = llvm::ConstantArray::get(ctor_array_type, {ctor_entry});

        // Create the @llvm.global_ctors global variable
        new llvm::GlobalVariable(*mod, ctor_array_type, false,
                                llvm::GlobalValue::AppendingLinkage, 
                                ctor_array, "llvm.global_ctors");

        LOG_DEBUG(Cryo::LogComponent::CODEGEN, 
                 "Global constructor function created and registered with @llvm.global_ctors");
    }

} // namespace Cryo::Codegen
