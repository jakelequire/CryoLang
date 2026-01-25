#include "Codegen/Declarations/DeclarationCodegen.hpp"
#include "Codegen/Declarations/GenericCodegen.hpp"
#include "Codegen/Memory/MemoryCodegen.hpp"
#include "Codegen/CodegenVisitor.hpp"
#include "Types/ErrorType.hpp"
#include "Types/GenericRegistry.hpp"
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

        // Skip generic functions - they should only be instantiated with concrete types
        // Check 1: Function has generic type parameters defined (e.g., fn foo<T>(...))
        if (!node->generic_parameters().empty())
        {
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "DeclarationCodegen: Skipping generic function declaration '{}' - has {} generic type parameters",
                      node->name(), node->generic_parameters().size());
            return nullptr;
        }

        // Check 2: Detect uninstantiated type parameters or error types in function signature
        for (const auto &param : node->parameters())
        {
            if (param && param->get_resolved_type())
            {
                TypeRef ptype = param->get_resolved_type();
                // Check for error types (unresolved generics, undefined types, etc.)
                if (ptype.is_error())
                {
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "DeclarationCodegen: Skipping function declaration '{}' - param '{}' has error type '{}'",
                              node->name(), param->name(), ptype->display_name());
                    // Emit actual error so it shows up in diagnostics
                    report_error(ErrorCode::E0642_PARAM_TYPE_ERROR, node,
                                "Function '" + node->name() + "' parameter '" + param->name() +
                                "' has unresolved type: " + ptype->display_name());
                    return nullptr;
                }
                if (ptype->kind() == TypeKind::GenericParam)
                {
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "DeclarationCodegen: Skipping function declaration '{}' - param '{}' has generic type '{}'",
                              node->name(), param->name(), ptype.get()->display_name());
                    return nullptr;
                }
                if (ptype->kind() == TypeKind::Struct || ptype->kind() == TypeKind::Class)
                {
                    llvm::StructType *existing = llvm::StructType::getTypeByName(llvm_ctx(), ptype.get()->display_name());
                    if (!existing)
                    {
                        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                  "DeclarationCodegen: Skipping function declaration '{}' - param '{}' has undefined type '{}'",
                                  node->name(), param->name(), ptype.get()->display_name());
                        return nullptr;
                    }
                }
            }
        }

        // Check 3: Detect uninstantiated return type or error types
        if (TypeRef ret_type = node->get_resolved_return_type())
        {
            // Check for error types (unresolved generics, undefined types, etc.)
            if (ret_type.is_error())
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "DeclarationCodegen: Skipping function declaration '{}' - return type is error: '{}'",
                          node->name(), ret_type->display_name());
                // Emit actual error so it shows up in diagnostics
                report_error(ErrorCode::E0643_RETURN_TYPE_ERROR, node,
                            "Function '" + node->name() + "' has unresolved return type: " + ret_type->display_name());
                return nullptr;
            }
            // Try to substitute type parameters if we're in a generic instantiation scope
            // This handles both direct type params (T) and types containing params (Option<T>)
            if (_generics && _generics->in_type_param_scope())
            {
                TypeRef substituted = _generics->substitute_type_params(ret_type);
                if (substituted.is_valid() && substituted != ret_type)
                {
                    ret_type = substituted;
                }
            }
            if (ret_type->kind() == TypeKind::GenericParam)
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "DeclarationCodegen: Skipping function declaration '{}' - has generic return type '{}'",
                          node->name(), ret_type.get()->display_name());
                return nullptr;
            }
            if (ret_type->kind() == TypeKind::Struct || ret_type->kind() == TypeKind::Class)
            {
                llvm::StructType *existing = llvm::StructType::getTypeByName(llvm_ctx(), ret_type.get()->display_name());
                if (!existing)
                {
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "DeclarationCodegen: Skipping function declaration '{}' - has undefined return type '{}'",
                              node->name(), ret_type.get()->display_name());
                    return nullptr;
                }
            }
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

        // Skip generic functions - they should only be instantiated with concrete types
        // Check 1: Function has generic type parameters defined (e.g., fn foo<T>(...))
        if (!node->generic_parameters().empty())
        {
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "DeclarationCodegen: Skipping generic function '{}' - has {} generic type parameters",
                      node->name(), node->generic_parameters().size());
            return nullptr;
        }

        // Check 2: Detect uninstantiated type parameters or error types in function signature
        for (const auto &param : node->parameters())
        {
            if (param && param->get_resolved_type())
            {
                TypeRef ptype = param->get_resolved_type();
                // Check for error types (unresolved generics, undefined types, etc.)
                if (ptype.is_error())
                {
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "DeclarationCodegen: Skipping function '{}' - param '{}' has error type '{}'",
                              node->name(), param->name(), ptype->display_name());
                    // Emit actual error so it shows up in diagnostics
                    report_error(ErrorCode::E0642_PARAM_TYPE_ERROR, node,
                                "Function '" + node->name() + "' parameter '" + param->name() +
                                "' has unresolved type: " + ptype->display_name());
                    return nullptr;
                }
                // Check for Generic type kind
                if (ptype->kind() == TypeKind::GenericParam)
                {
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "DeclarationCodegen: Skipping function '{}' - param '{}' has generic type '{}'",
                              node->name(), param->name(), ptype.get()->display_name());
                    return nullptr;
                }
                // Check if struct/class parameter type exists in LLVM context
                if (ptype->kind() == TypeKind::Struct || ptype->kind() == TypeKind::Class)
                {
                    llvm::StructType *existing = llvm::StructType::getTypeByName(llvm_ctx(), ptype.get()->display_name());
                    if (!existing)
                    {
                        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                  "DeclarationCodegen: Skipping function '{}' - param '{}' has undefined type '{}'",
                                  node->name(), param->name(), ptype.get()->display_name());
                        return nullptr;
                    }
                }
            }
        }

        // Check 3: Detect uninstantiated return type or error types
        if (TypeRef ret_type = node->get_resolved_return_type())
        {
            // Check for error types (unresolved generics, undefined types, etc.)
            if (ret_type.is_error())
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "DeclarationCodegen: Skipping function '{}' - return type is error: '{}'",
                          node->name(), ret_type->display_name());
                // Emit actual error so it shows up in diagnostics
                report_error(ErrorCode::E0643_RETURN_TYPE_ERROR, node,
                            "Function '" + node->name() + "' has unresolved return type: " + ret_type->display_name());
                return nullptr;
            }
            // Try to substitute type parameters if we're in a generic instantiation scope
            // This handles both direct type params (T) and types containing params (Option<T>)
            if (_generics && _generics->in_type_param_scope())
            {
                TypeRef substituted = _generics->substitute_type_params(ret_type);
                if (substituted.is_valid() && substituted != ret_type)
                {
                    ret_type = substituted;
                }
            }
            if (ret_type->kind() == TypeKind::GenericParam)
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "DeclarationCodegen: Skipping function '{}' - has generic return type '{}'",
                          node->name(), ret_type.get()->display_name());
                return nullptr;
            }
            if (ret_type->kind() == TypeKind::Struct || ret_type->kind() == TypeKind::Class)
            {
                llvm::StructType *existing = llvm::StructType::getTypeByName(llvm_ctx(), ret_type.get()->display_name());
                if (!existing)
                {
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "DeclarationCodegen: Skipping function '{}' - has undefined return type '{}'",
                              node->name(), ret_type.get()->display_name());
                    return nullptr;
                }
            }
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
            report_error(ErrorCode::E0633_FUNCTION_BODY_ERROR, node,
                        "Function '" + name + "' failed LLVM verification");
            // Clear the builder's insert point before erasing the function
            // to prevent dangling pointers to the deleted basic blocks
            builder().ClearInsertionPoint();
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

        // Early trace to track which methods are being processed
        LOG_ERROR(Cryo::LogComponent::CODEGEN,
                  "DeclarationCodegen: TRACE ENTRY generate_method_declaration for '{}::{}' (return_type_annotation='{}')",
                  parent_type, node->name(),
                  node->return_type_annotation() ? node->return_type_annotation()->to_string() : "<none>");

        // Skip methods of generic templates - they should only be instantiated with concrete types
        if (_generics && !parent_type.empty() && _generics->is_generic_template(parent_type))
        {
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "DeclarationCodegen: Skipping method declaration '{}' of generic template '{}'",
                      node->name(), parent_type);
            return nullptr;
        }

        // Skip methods that have their own generic parameters (e.g., alloc_one<T>)
        // These should only be generated when instantiated with concrete types
        if (!node->generic_parameters().empty())
        {
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "DeclarationCodegen: Skipping generic method declaration '{}' - has {} generic type parameters",
                      node->name(), node->generic_parameters().size());
            return nullptr;
        }

        // Additional check: detect uninstantiated type parameters in signature
        // This catches cases where the generic template check above didn't work
        for (const auto &param : node->parameters())
        {
            if (param && param->get_resolved_type())
            {
                TypeRef ptype = param->get_resolved_type();
                // Check for error types (unresolved function types, etc.)
                // Error types map to void in LLVM, which is invalid as a parameter type
                if (ptype.is_error())
                {
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "DeclarationCodegen: Skipping method '{}' - param '{}' has error type '{}'",
                              node->name(), param->name(), ptype.get()->display_name());
                    // Emit actual error so it shows up in diagnostics
                    report_error(ErrorCode::E0642_PARAM_TYPE_ERROR, node,
                                "Method '" + node->name() + "' parameter '" + param->name() +
                                "' has unresolved type: " + ptype.get()->display_name());
                    return nullptr;
                }
                // Check for Generic type kind or undefined struct/class types
                // First, try to substitute type parameters if we're in a generic instantiation scope
                // This handles both direct type params (T) and types containing params (Option<T>)
                if (_generics && _generics->in_type_param_scope())
                {
                    TypeRef substituted = _generics->substitute_type_params(ptype);
                    if (substituted.is_valid() && substituted != ptype)
                    {
                        // Successfully substituted, use the concrete type
                        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                  "DeclarationCodegen: Substituted param '{}' type '{}' -> '{}'",
                                  param->name(), ptype->display_name(),
                                  substituted->display_name());
                        ptype = substituted;
                    }
                }
                if (ptype->kind() == TypeKind::GenericParam)
                {
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "DeclarationCodegen: Skipping method '{}' - param '{}' has generic type '{}'",
                              node->name(), param->name(), ptype.get()->display_name());
                    return nullptr;
                }
                if (ptype->kind() == TypeKind::Struct || ptype->kind() == TypeKind::Class)
                {
                    // Check if this type exists in LLVM context - if not, it's likely a type parameter
                    llvm::StructType *existing = llvm::StructType::getTypeByName(llvm_ctx(), ptype.get()->display_name());
                    if (!existing)
                    {
                        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                  "DeclarationCodegen: Skipping method '{}' - param '{}' has undefined type '{}'",
                                  node->name(), param->name(), ptype.get()->display_name());
                        return nullptr;
                    }
                }
            }
        }

        // Also check if return type has generic parameters
        TypeRef resolved_ret = node->get_resolved_return_type();
        LOG_ERROR(Cryo::LogComponent::CODEGEN,
                  "DeclarationCodegen: TRACE Method '{}::{}' resolved_return_type valid={}, is_error={}, kind={}",
                  parent_type, node->name(),
                  resolved_ret.is_valid(),
                  resolved_ret.is_valid() ? resolved_ret.is_error() : false,
                  resolved_ret.is_valid() ? static_cast<int>(resolved_ret->kind()) : -1);
        if (resolved_ret.is_valid())
        {
            TypeRef ret_type = resolved_ret;
            // Try to substitute type parameters if we're in a generic instantiation scope
            // This handles direct type params (T), types containing params (Option<T>), and pointers
            if (_generics && _generics->in_type_param_scope())
            {
                TypeRef substituted = _generics->substitute_type_params(ret_type);
                if (substituted.is_valid() && substituted != ret_type)
                {
                    ret_type = substituted;
                }
            }
            if (ret_type->kind() == TypeKind::GenericParam)
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "DeclarationCodegen: Skipping method declaration '{}' - return type is generic '{}'",
                          node->name(), ret_type.get()->display_name());
                return nullptr;
            }
            // Also check for pointer to generic type (after substitution)
            if (ret_type->kind() == TypeKind::Pointer)
            {
                auto *ptr_type = static_cast<const PointerType *>(ret_type.get());
                TypeRef pointee = ptr_type->pointee();
                if (pointee.is_valid() && pointee->kind() == TypeKind::GenericParam)
                {
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "DeclarationCodegen: Skipping method declaration '{}' - return type is pointer to generic",
                              node->name());
                    return nullptr;
                }
            }
            // Check if return type is an error type (unresolved generics, undefined types, etc.)
            if (ret_type.is_error())
            {
                // Check if we have a valid annotation string we can use instead
                if (node->return_type_annotation())
                {
                    // We have an annotation - proceed with method generation
                    // The annotation will be registered and used to resolve the actual return type at call-time
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "DeclarationCodegen: Method '{}::{}' has error return type '{}' but has annotation '{}' - proceeding",
                              parent_type, node->name(), ret_type->display_name(),
                              node->return_type_annotation()->to_string());
                }
                else
                {
                    // No annotation - we can't resolve the type, skip
                    LOG_ERROR(Cryo::LogComponent::CODEGEN,
                              "DeclarationCodegen: Skipping method declaration '{}::{}' - return type is error '{}' with no annotation",
                              parent_type, node->name(), ret_type->display_name());
                    // Emit actual error so it shows up in diagnostics
                    report_error(ErrorCode::E0643_RETURN_TYPE_ERROR, node,
                                "Method '" + parent_type + "::" + node->name() +
                                "' has unresolved return type: " + ret_type->display_name());
                    return nullptr;
                }
            }
            // Check for Struct return types that don't exist in LLVM yet
            if (ret_type->kind() == TypeKind::Struct || ret_type->kind() == TypeKind::Class)
            {
                std::string ret_type_name = ret_type->display_name();
                // Check if this is a generic instantiation (e.g., Option<u64>)
                size_t angle_pos = ret_type_name.find('<');
                if (angle_pos != std::string::npos)
                {
                    // This is a monomorphized generic type - it should be valid
                    // Don't skip, let it proceed to code generation
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "DeclarationCodegen: Method '{}::{}' returns monomorphized type '{}', proceeding",
                              parent_type, node->name(), ret_type_name);
                }
                else
                {
                    // Plain struct type - check if it exists
                    llvm::StructType *existing = llvm::StructType::getTypeByName(llvm_ctx(), ret_type_name);
                    if (!existing)
                    {
                        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                  "DeclarationCodegen: Method '{}::{}' returns undefined struct '{}', checking alternatives",
                                  parent_type, node->name(), ret_type_name);
                        // Don't skip yet - the type might be defined in another namespace
                    }
                }
            }
        }

        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "DeclarationCodegen: Generating method: {}::{}",
                  parent_type, node->name());

        // Check if this is a static method (no 'this' parameter)
        bool is_static = false;
        if (auto *struct_method = dynamic_cast<Cryo::StructMethodNode *>(node))
        {
            is_static = struct_method->is_static();
        }
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "DeclarationCodegen: Method '{}::{}' is_static={}",
                  parent_type, node->name(), is_static);

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

        // IMPORTANT: Register method return type annotation BEFORE early return check
        // This ensures annotations are registered even when the function already exists
        // (e.g., from Pass 2.5 forward declarations)
        LOG_ERROR(Cryo::LogComponent::CODEGEN,
                  "DeclarationCodegen: TRACE registering annotation for '{}' (passed skip checks)",
                  llvm_fn_name);
        Cryo::TemplateRegistry *template_registry = ctx().template_registry();
        if (template_registry)
        {
            // Check if annotation is already registered
            std::string existing_annotation = template_registry->get_method_return_type_annotation(llvm_fn_name);
            if (existing_annotation.empty())
            {
                // Get the return type annotation string
                TypeRef resolved_ret = node->get_resolved_return_type();
                std::string return_type_str;

                // First try the AST annotation
                if (node->return_type_annotation())
                {
                    return_type_str = node->return_type_annotation()->to_string();
                }
                // For StructMethodNode, return_type_annotation() may be null but resolved type is valid
                else if (resolved_ret.is_valid() && !resolved_ret.is_error())
                {
                    return_type_str = resolved_ret.get()->display_name();
                }

                if (!return_type_str.empty() && return_type_str != "void")
                {
                    template_registry->register_method_return_type_annotation(llvm_fn_name, return_type_str);
                    LOG_ERROR(Cryo::LogComponent::CODEGEN,
                              "DeclarationCodegen: TRACE registered annotation for '{}': '{}'",
                              llvm_fn_name, return_type_str);
                }
                else
                {
                    LOG_ERROR(Cryo::LogComponent::CODEGEN,
                              "DeclarationCodegen: TRACE NOT registering annotation for '{}': return_type_str='{}' (empty or void)",
                              llvm_fn_name, return_type_str);
                }
            }
            else
            {
                LOG_ERROR(Cryo::LogComponent::CODEGEN,
                          "DeclarationCodegen: TRACE annotation already exists for '{}': '{}'",
                          llvm_fn_name, existing_annotation);
            }

            // Register is_static flag for cross-module extern declarations
            template_registry->register_method_is_static(llvm_fn_name, is_static);
        }

        // Check if already declared (check both names to handle existing declarations)
        if (llvm::Function *existing = module()->getFunction(llvm_fn_name))
        {
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "DeclarationCodegen: Method '{}' already exists (llvm_fn_name match), returning early",
                      llvm_fn_name);
            return existing;
        }
        if (llvm::Function *existing = module()->getFunction(base_method_name))
        {
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "DeclarationCodegen: Method '{}' already exists (base_method_name='{}' match), returning early",
                      llvm_fn_name, base_method_name);
            return existing;
        }

        // Get function type - only add 'this' parameter for non-static methods
        llvm::FunctionType *fn_type = get_function_type(node, !is_static, parent_type);
        if (!fn_type)
        {
            report_error(ErrorCode::E0633_FUNCTION_BODY_ERROR, node,
                         "Failed to get method type for: " + llvm_fn_name);
            return nullptr;
        }

        // Create function with fully-qualified name
        llvm::Function *fn = llvm::Function::Create(fn_type, llvm::Function::ExternalLinkage,
                                                    llvm_fn_name, module());

        // Name parameters
        auto arg_it = fn->arg_begin();

        // For non-static methods, first parameter is 'this'
        if (!is_static)
        {
            arg_it->setName("this");
            ++arg_it;
        }

        // Name remaining parameters (skip 'this' since we already handled it above)
        if (node->parameters().size() > 0)
        {
            for (const auto &param : node->parameters())
            {
                // Skip 'this' parameter - already named above for non-static methods
                if (param->name() == "this")
                {
                    continue;
                }
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

        // Register method return type (TypeRef) for cross-module extern declarations
        // This allows other modules to create correct extern declarations for this method
        // Note: String annotation is registered earlier (before early return check)
        TypeRef return_type = node->get_resolved_return_type();
        if (return_type)
        {
            // Register in local CodegenContext
            ctx().register_method_return_type(llvm_fn_name, return_type);

            // Also register in shared TemplateRegistry for cross-module access
            if (template_registry)
            {
                template_registry->register_method_return_type(llvm_fn_name, return_type);
            }

            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "DeclarationCodegen: Registered method return type for '{}': {}",
                      llvm_fn_name, return_type.get()->display_name());
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
        TypeRef cryo_var_type = node->get_resolved_type();

        // Check if variable type is an error type (unresolved generics, undefined types, etc.)
        // This can happen with forward-referenced types that weren't resolved during parsing
        if (cryo_var_type.is_error())
        {
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "DeclarationCodegen: Variable '{}' has error type: '{}', attempting late resolution",
                      name, cryo_var_type->display_name());

            const TypeAnnotation *annotation = node->type_annotation();
            TypeRef resolved;

            // First try: Use the type annotation directly for proper generic resolution
            // This handles generic types like Array<String> properly
            if (annotation)
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "DeclarationCodegen: Attempting late resolution via type annotation (kind={})",
                          static_cast<int>(annotation->kind));

                resolved = resolve_type_annotation(annotation);

                if (resolved.is_valid() && !resolved.is_error())
                {
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "DeclarationCodegen: Late resolution via annotation succeeded -> '{}'",
                              resolved->display_name());
                    cryo_var_type = resolved;
                    node->set_resolved_type(resolved);
                }
            }

            // Second try: Fallback for simple named types without proper annotations
            if (!resolved.is_valid() || resolved.is_error())
            {
                std::string type_name;

                if (annotation && !annotation->name.empty() &&
                    annotation->kind != TypeAnnotationKind::Generic)
                {
                    // Use annotation name for non-generic types
                    type_name = annotation->name;
                }
                else
                {
                    // Fallback: Extract type name from the error reason
                    // Error format is "unresolved type: TypeName" or "unresolved generic: TypeName<...>"
                    const ErrorType *error_type = static_cast<const ErrorType *>(cryo_var_type.get());
                    if (error_type)
                    {
                        std::string reason = error_type->reason();
                        const std::string prefix = "unresolved type: ";
                        const std::string generic_prefix = "unresolved generic: ";

                        if (reason.rfind(prefix, 0) == 0)
                        {
                            // Simple type: "unresolved type: timespec"
                            type_name = reason.substr(prefix.length());
                        }
                        else if (reason.rfind(generic_prefix, 0) == 0)
                        {
                            // For unresolved generics, the annotation-based resolution should
                            // have worked. If we're here, it means we don't have the annotation.
                            // Log a warning and skip - we can't resolve generics without annotations.
                            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                      "DeclarationCodegen: Cannot resolve generic type without proper annotation");
                            return nullptr;
                        }
                    }
                }

                if (type_name.empty())
                {
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "DeclarationCodegen: Skipping local variable '{}' - cannot determine type name for late resolution",
                              name);
                    return nullptr;
                }

                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "DeclarationCodegen: Fallback resolution for simple type '{}'", type_name);

                // Try to look up as struct type first
                resolved = ctx().symbols().lookup_struct_type(type_name);
                if (!resolved.is_valid())
                {
                    // Try with current namespace prefix
                    std::string ns = ctx().namespace_context();
                    if (!ns.empty())
                    {
                        std::string qualified = ns + "::" + type_name;
                        resolved = ctx().symbols().lookup_struct_type(qualified);
                    }
                }
                if (!resolved.is_valid())
                {
                    // Try as class type
                    resolved = ctx().symbols().lookup_class_type(type_name);
                }

                if (resolved.is_valid() && !resolved.is_error())
                {
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "DeclarationCodegen: Late resolution succeeded for type '{}' -> '{}'",
                              type_name, resolved->display_name());
                    cryo_var_type = resolved;
                    node->set_resolved_type(resolved);
                }
                else
                {
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "DeclarationCodegen: Late resolution failed for type '{}'", type_name);
                    return nullptr;
                }
            }
        }

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
                      cryo_var_type ? cryo_var_type.get()->display_name() : "null",
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
                // Special handling for struct/class value types: if the initializer returns a pointer
                // to a struct (e.g., from a struct literal), we need to memcpy instead of store.
                // IMPORTANT: Only do this for actual Struct/Class types, not Arrays which may also
                // be represented as LLVM struct types.
                bool is_struct_value_type = false;
                if (cryo_var_type)
                {
                    TypeKind kind = cryo_var_type->kind();
                    is_struct_value_type = (kind == TypeKind::Struct || kind == TypeKind::Class);
                }

                // Fallback: if Cryo type didn't indicate struct but LLVM type is a named struct,
                // treat it as a struct value type. Named structs (like %SingleStruct) are user-defined
                // structs, while anonymous structs are typically arrays or tuples.
                // IMPORTANT: Exclude Array<T> types which are also named structs but have special
                // initialization semantics.
                if (!is_struct_value_type && var_type->isStructTy())
                {
                    auto *struct_type = llvm::cast<llvm::StructType>(var_type);
                    if (struct_type->hasName())
                    {
                        llvm::StringRef struct_name = struct_type->getName();
                        // Exclude Array<T> types - they use store, not memcpy
                        if (!struct_name.starts_with("Array<"))
                        {
                            is_struct_value_type = true;
                        }
                    }
                }

                if (is_struct_value_type && var_type->isStructTy() && init_val->getType()->isPointerTy())
                {
                    // The init_val is a pointer to the struct data, memcpy to our alloca
                    // Check if the struct type is sized before computing size
                    if (!var_type->isSized())
                    {
                        LOG_WARN(Cryo::LogComponent::CODEGEN,
                                 "DeclarationCodegen: Struct type for '{}' is opaque, using store instead of memcpy",
                                 name);
                        // Fall back to store for unsized types
                        llvm::Value *cast_val = cast_if_needed(init_val, var_type);
                        create_store(cast_val, alloca);
                    }
                    else
                    {
                        auto &data_layout = module()->getDataLayout();
                        uint64_t size = data_layout.getTypeAllocSize(var_type);

                        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                  "DeclarationCodegen: Struct initialization via memcpy for '{}', size {} bytes",
                                  name, size);

                        builder().CreateMemCpy(
                            alloca,                                                    // dest
                            llvm::MaybeAlign(data_layout.getABITypeAlign(var_type)),   // dest align
                            init_val,                                                  // src
                            llvm::MaybeAlign(data_layout.getABITypeAlign(var_type)),   // src align
                            size                                                       // size
                        );
                    }
                }
                else
                {
                    llvm::Value *cast_val = cast_if_needed(init_val, var_type);
                    create_store(cast_val, alloca);
                }
            }
        }

        // Register in value context
        values().set_value(name, nullptr, alloca);

        // Register the variable type in variable_types_map for Array<T> detection
        TypeRef resolved_type = node->get_resolved_type();
        // Apply type substitution if we're in a generic instantiation scope
        if (resolved_type.is_valid() && _generics)
        {
            TypeRef substituted = _generics->substitute_type_params(resolved_type);
            if (substituted.is_valid())
            {
                resolved_type = substituted;
            }
        }
        if (resolved_type)
        {
            ctx().variable_types_map()[name] = resolved_type;
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "DeclarationCodegen: Registered local variable type: {} -> {} (kind: {})",
                      name, resolved_type.get()->display_name(),
                      static_cast<int>(resolved_type->kind()));

            // Special logging for potential Array<T> types
            if (resolved_type->kind() == TypeKind::Array || resolved_type.get()->display_name().find("[]") != std::string::npos)
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "DeclarationCodegen: *** Registered Array<T> variable: {} with type: {}",
                          name, resolved_type.get()->display_name());
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
        TypeRef cryo_type = node->get_resolved_type();
        if (!cryo_type)
        {
            report_error(ErrorCode::E0634_VARIABLE_INITIALIZATION_ERROR, node,
                         "No resolved type for global variable: " + name);
            return nullptr;
        }

        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "DeclarationCodegen: Global '{}' has Cryo type: {} (kind={})",
                  name, cryo_type.get()->display_name(), Cryo::type_kind_to_string(cryo_type->kind()));

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
                                    cryo_type->kind() == Cryo::TypeKind::GenericParam);
        if (is_class_or_generic && var_type->isPointerTy())
        {
            LOG_WARN(Cryo::LogComponent::CODEGEN,
                     "DeclarationCodegen: Global '{}' has {} type but got pointer LLVM type. "
                     "Looking up struct type directly.",
                     name, Cryo::type_kind_to_string(cryo_type->kind()));

            // Get the type's name and look up the struct directly
            std::string type_name = cryo_type->display_name();
            llvm::StructType* struct_type = llvm::StructType::getTypeByName(llvm_ctx(), type_name);
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
                      name, cryo_type.get()->display_name());
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
        llvm::StructType *struct_type = llvm::StructType::getTypeByName(llvm_ctx(), name);
        if (struct_type && !struct_type->isOpaque())
        {
            // Already fully defined
            return struct_type;
        }

        // Create opaque struct first if it doesn't exist (for recursive types)
        if (!struct_type)
        {
            struct_type = llvm::StructType::create(llvm_ctx(), name);
        }

        // Collect field types
        std::vector<llvm::Type *> field_types;
        for (size_t i = 0; i < node->fields().size(); ++i)
        {
            const auto &field = node->fields()[i];
            Cryo::TypeRef cryo_field_type = field->get_resolved_type();

            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "DeclarationCodegen: Struct '{}' field[{}] '{}': Cryo TypeID={}, type={} (kind={})",
                      name, i, field->name(),
                      cryo_field_type ? cryo_field_type.id().id : 0,
                      cryo_field_type ? cryo_field_type->display_name() : "<null>",
                      cryo_field_type ? static_cast<int>(cryo_field_type->kind()) : -1);

            llvm::Type *field_type = get_llvm_type(cryo_field_type);

            if (field_type)
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "DeclarationCodegen: Struct '{}' field[{}] '{}': LLVM type ID = {}",
                          name, i, field->name(), field_type->getTypeID());
                field_types.push_back(field_type);
            }
            else
            {
                LOG_ERROR(Cryo::LogComponent::CODEGEN,
                          "DeclarationCodegen: Struct '{}' field[{}] '{}': FAILED to get LLVM type!",
                          name, i, field->name());
            }
        }

        // Set struct body (this works even if struct was previously opaque)
        struct_type->setBody(field_types);
        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                  "DeclarationCodegen: Set struct '{}' body with {} fields", name, field_types.size());

        // Register type
        ctx().register_type(name, struct_type);

        // Register type's namespace for cross-module method resolution
        std::string ns_context = ctx().namespace_context();
        if (!ns_context.empty())
        {
            ctx().register_type_namespace(name, ns_context);
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "DeclarationCodegen: Registered struct '{}' namespace: {}", name, ns_context);
        }

        return struct_type;
    }

    llvm::StructType *DeclarationCodegen::generate_class_declaration(Cryo::ClassDeclarationNode *node)
    {
        if (!node)
            return nullptr;

        std::string name = node->name();
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "DeclarationCodegen: Generating class: {}", name);

        // Check if already declared
        llvm::StructType *class_type = llvm::StructType::getTypeByName(llvm_ctx(), name);
        if (class_type && !class_type->isOpaque())
        {
            // Already fully defined
            return class_type;
        }

        // Create opaque struct if it doesn't exist (for recursive types)
        if (!class_type)
        {
            class_type = llvm::StructType::create(llvm_ctx(), name);
        }

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

        // Set class body (this works even if class was previously opaque)
        class_type->setBody(field_types);
        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                  "DeclarationCodegen: Set class '{}' body with {} fields", name, field_types.size());

        // Register type
        ctx().register_type(name, class_type);

        // Register type's namespace for cross-module method resolution
        std::string ns_context = ctx().namespace_context();
        if (!ns_context.empty())
        {
            ctx().register_type_namespace(name, ns_context);
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "DeclarationCodegen: Registered class '{}' namespace: {}", name, ns_context);
        }

        return class_type;
    }

    llvm::Type *DeclarationCodegen::generate_enum_declaration(Cryo::EnumDeclarationNode *node)
    {
        if (!node)
            return nullptr;

        std::string name = node->name();
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "DeclarationCodegen: Generating enum: {}", name);

        // Skip generic enum declarations - they should only be generated when instantiated
        // with concrete type parameters. Generic enums like Option<T> will be codegen'd
        // when monomorphized (e.g., Option<int>, Option<string>).
        if (!node->generic_parameters().empty())
        {
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "Skipping generic enum '{}' - will be generated on instantiation", name);
            return nullptr;
        }

        // Check if this is a simple enum or complex enum with payloads
        bool is_simple = true;
        size_t max_payload_size = 0;
        for (const auto &variant : node->variants())
        {
            if (!variant->is_simple_variant())
            {
                is_simple = false;
                // Calculate payload size for this variant
                size_t variant_payload = 0;
                for (const auto &type_name : variant->associated_types())
                {
                    llvm::Type *field_type = types().get_type(type_name);
                    if (field_type && field_type->isSized())
                    {
                        variant_payload += module()->getDataLayout().getTypeAllocSize(field_type);
                    }
                    else
                    {
                        // Default to 8 bytes for unknown or unsized types
                        variant_payload += 8;
                    }
                }
                max_payload_size = std::max(max_payload_size, variant_payload);
            }
        }

        llvm::Type *enum_type = nullptr;

        if (is_simple)
        {
            // Simple enums are just integers
            enum_type = llvm::Type::getInt32Ty(llvm_ctx());
        }
        else
        {
            // Complex enums are tagged unions: { i32 discriminant, [payload_size x i8] }
            llvm::Type *discriminant_type = llvm::Type::getInt32Ty(llvm_ctx());
            llvm::Type *payload_type = llvm::ArrayType::get(llvm::Type::getInt8Ty(llvm_ctx()), max_payload_size);

            // Check if there's an existing struct type with this name
            llvm::StructType *enum_struct = llvm::StructType::getTypeByName(llvm_ctx(), name);

            if (enum_struct && !enum_struct->isOpaque())
            {
                // Type already exists and is complete - use it
                enum_type = enum_struct;
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "Reusing existing tagged union for enum '{}'", name);
            }
            else
            {
                // Create or complete the struct type
                if (!enum_struct)
                {
                    enum_struct = llvm::StructType::create(llvm_ctx(), name);
                }
                // Set the body (completes the type if it was opaque)
                enum_struct->setBody({discriminant_type, payload_type}, false);
                enum_type = enum_struct;

                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "Created tagged union for enum '{}': payload size = {} bytes", name, max_payload_size);
            }
        }

        // Register type
        ctx().register_type(name, enum_type);

        // Register type's namespace for cross-module method resolution
        std::string ns_context = ctx().namespace_context();
        if (!ns_context.empty())
        {
            ctx().register_type_namespace(name, ns_context);
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "DeclarationCodegen: Registered enum '{}' namespace: {}", name, ns_context);
        }

        // Generate variant constants and constructor functions
        int32_t index = 0;
        for (const auto &variant : node->variants())
        {
            std::string variant_name = name + "::" + variant->name();
            llvm::Constant *discriminant = llvm::ConstantInt::get(llvm::Type::getInt32Ty(llvm_ctx()), index);

            // Register discriminant value with simple name
            ctx().register_enum_variant(variant_name, discriminant);
            LOG_DEBUG(Cryo::LogComponent::CODEGEN, "DeclarationCodegen: Registered enum variant: {} = {}",
                      variant_name, index);

            // Also register with fully-qualified name (namespace::EnumName::Variant)
            if (!ns_context.empty())
            {
                std::string qualified_variant = ns_context + "::" + variant_name;
                ctx().register_enum_variant(qualified_variant, discriminant);
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "DeclarationCodegen: Also registered enum variant as: {}", qualified_variant);
            }

            // Register variant field types for pattern matching
            if (!variant->is_simple_variant())
            {
                ctx().register_enum_variant_fields(variant_name, variant->associated_types());
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "Registered {} field types for variant {}", variant->associated_types().size(), variant_name);
            }

            // For complex variants, generate constructor function
            if (!is_simple && !variant->is_simple_variant())
            {
                generate_enum_variant_constructor(node, variant.get(), index, enum_type);
            }

            index++;
        }

        return enum_type;
    }

    void DeclarationCodegen::generate_enum_variant_constructor(
        Cryo::EnumDeclarationNode *enum_node,
        Cryo::EnumVariantNode *variant,
        int32_t discriminant_value,
        llvm::Type *enum_type)
    {
        std::string enum_name = enum_node->name();
        std::string variant_name = variant->name();
        std::string ctor_name = enum_name + "::" + variant_name;

        // Build parameter types from associated types
        std::vector<llvm::Type *> param_types;
        for (const auto &type_name : variant->associated_types())
        {
            llvm::Type *param_type = types().get_type(type_name);
            if (!param_type)
            {
                LOG_WARN(Cryo::LogComponent::CODEGEN,
                         "Unknown type '{}' in enum variant {}", type_name, ctor_name);
                return;
            }
            param_types.push_back(param_type);
        }

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
            llvm::ConstantInt::get(llvm::Type::getInt32Ty(llvm_ctx()), discriminant_value),
            disc_ptr);

        // Store the payload fields
        llvm::Value *payload_ptr = ctor_builder.CreateStructGEP(enum_type, enum_alloca, 1, "payload_ptr");

        size_t offset = 0;
        size_t arg_idx = 0;
        for (auto &arg : ctor->args())
        {
            llvm::Type *arg_type = arg.getType();

            // Calculate pointer to this field in the payload
            llvm::Value *field_ptr = ctor_builder.CreateConstGEP1_32(
                llvm::Type::getInt8Ty(llvm_ctx()), payload_ptr, offset, "field_ptr");

            // Bitcast to correct type pointer and store
            field_ptr = ctor_builder.CreateBitCast(field_ptr, llvm::PointerType::get(arg_type, 0));
            ctor_builder.CreateStore(&arg, field_ptr);

            // Calculate size of argument type, with safety check for unsized types
            if (arg_type->isSized())
            {
                offset += module()->getDataLayout().getTypeAllocSize(arg_type);
            }
            else
            {
                offset += 8; // Default to pointer size for unsized types
            }
            arg_idx++;
        }

        // Load and return the enum value
        llvm::Value *result = ctor_builder.CreateLoad(enum_type, enum_alloca, "result");
        ctor_builder.CreateRet(result);

        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                  "Generated enum variant constructor: {} with {} parameters", ctor_name, param_types.size());
    }

    //===================================================================
    // Helpers
    //===================================================================

    llvm::FunctionType *DeclarationCodegen::get_function_type(Cryo::FunctionDeclarationNode *node,
                                                              bool has_this_param,
                                                              const std::string &parent_type_name)
    {
        if (!node)
            return nullptr;

        // Get return type
        TypeRef resolved_type = node->get_resolved_return_type();

        // Try to substitute type parameters if we're in a generic instantiation scope
        // This handles both direct type params (T) and types containing params (Option<T>)
        if (resolved_type.is_valid() && _generics && _generics->in_type_param_scope())
        {
            TypeRef substituted = _generics->substitute_type_params(resolved_type);
            if (substituted.is_valid() && substituted != resolved_type)
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN, "get_function_type: Substituted return type '{}' -> '{}'",
                          resolved_type->display_name(), substituted->display_name());
                resolved_type = substituted;
            }
        }

        // Check for corrupted or invalid type pointer
        if (resolved_type.is_valid())
        {
            try {
                // Attempt to access the type safely
                TypeKind kind = resolved_type->kind();
                std::string type_name = resolved_type->display_name();
                LOG_DEBUG(Cryo::LogComponent::CODEGEN, "get_function_type: Function '{}' resolved return type: '{}'",
                          node->name(), resolved_type->display_name());
            }
            catch (...)
            {
                LOG_ERROR(Cryo::LogComponent::CODEGEN, "get_function_type: CORRUPTED TYPE POINTER for function '{}' - this indicates a memory management bug in Monomorphizer", node->name());
                throw std::runtime_error("Corrupted type pointer detected for function '" + node->name() + 
                                       "' - this is a compiler bug in generic type substitution that must be fixed");
            }
        }
        else
        {
            LOG_DEBUG(Cryo::LogComponent::CODEGEN, "get_function_type: Function '{}' has NULL return type", node->name());
        }
        
        // Special handling for constructors - if return type is void, fix it to pointer type
        // Only apply this fix for static methods named "new" (conventional constructor name)
        // Regular void-returning methods like reset(), drop() should stay void
        bool is_likely_constructor = false;
        if (auto *struct_method = dynamic_cast<Cryo::StructMethodNode *>(node))
        {
            // Constructors are static methods named "new"
            is_likely_constructor = struct_method->is_static() && node->name() == "new";
        }

        if (resolved_type.is_valid() && resolved_type->kind() == TypeKind::Void &&
            !parent_type_name.empty() && is_likely_constructor)
        {
            // This is a constructor with wrong return type - fix it to pointer to struct type
            TypeRef struct_type = ctx().symbols().lookup_struct_type(parent_type_name);
            if (struct_type.is_valid())
            {
                resolved_type = ctx().symbols().arena().get_pointer_to(struct_type);
                LOG_DEBUG(Cryo::LogComponent::CODEGEN, "get_function_type: Constructor '{}' return type fixed from {} to {}",
                          node->name(),
                          node->get_resolved_return_type().is_valid() ? node->get_resolved_return_type()->display_name() : "NULL",
                          resolved_type.is_valid() ? resolved_type->display_name() : "NULL");
            }
        }

        // If resolved_type is an error type but we have an annotation, try to resolve from annotation
        if (resolved_type.is_valid() && resolved_type.is_error() && node->return_type_annotation())
        {
            std::string annotation_str = node->return_type_annotation()->to_string();
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "get_function_type: Function '{}' has error return type, trying annotation '{}'",
                      node->name(), annotation_str);

            // Try to get the LLVM type from the annotation string (handles generic instantiations)
            llvm::Type *annotation_type = types().resolve_and_map(annotation_str);
            if (annotation_type)
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "get_function_type: Successfully resolved return type from annotation '{}' for '{}'",
                          annotation_str, node->name());
                llvm::Type *return_type = annotation_type;

                // Collect parameter types
                std::vector<llvm::Type *> param_types;

                // Add 'this' parameter for methods (pointer to parent type)
                if (has_this_param && !parent_type_name.empty())
                {
                    param_types.push_back(llvm::PointerType::get(llvm_ctx(), 0));
                }

                // Add regular parameters
                for (const auto &param : node->parameters())
                {
                    if (!param)
                        continue;
                    // Skip 'this' parameter - it's already added above when has_this_param is true
                    if (param->name() == "this")
                    {
                        continue;
                    }
                    TypeRef param_cryo_type = param->get_resolved_type();

                    // Try to substitute type parameters if we're in a generic instantiation scope
                    // This handles both direct type params (T) and types containing params (Option<T>)
                    if (param_cryo_type.is_valid() && _generics && _generics->in_type_param_scope())
                    {
                        TypeRef substituted = _generics->substitute_type_params(param_cryo_type);
                        if (substituted.is_valid() && substituted != param_cryo_type)
                        {
                            param_cryo_type = substituted;
                        }
                    }

                    llvm::Type *llvm_param = get_llvm_type(param_cryo_type);
                    if (llvm_param)
                    {
                        // Skip void parameters - void is not a valid parameter type in LLVM
                        // This happens when T=void in generic instantiation like Result<void>
                        if (llvm_param->isVoidTy())
                        {
                            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                     "get_function_type: Skipping void parameter '{}' in '{}'",
                                     param->name(), node->name());
                            continue;
                        }
                        param_types.push_back(llvm_param);
                    }
                    else
                    {
                        // Fallback to pointer type to avoid silently dropping parameters
                        LOG_WARN(Cryo::LogComponent::CODEGEN,
                                 "get_function_type: Parameter '{}' type could not be resolved, using ptr fallback",
                                 param->name());
                        param_types.push_back(llvm::PointerType::get(llvm_ctx(), 0));
                    }
                }

                return llvm::FunctionType::get(return_type, param_types, node->is_variadic());
            }
            else
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "get_function_type: Could not resolve annotation '{}' - using opaque pointer for '{}'",
                          annotation_str, node->name());
                // Use opaque pointer for now - the actual type will be resolved at call-time
                resolved_type = TypeRef(); // Clear to trigger fallback
            }
        }

        llvm::Type *return_type = get_llvm_type(resolved_type);
        if (!return_type)
        {
            // For generic methods, check if this is a template context where the return type
            // should be resolved during monomorphization rather than defaulting to void
            bool is_generic_context = !parent_type_name.empty() &&
                                     (parent_type_name.find('<') != std::string::npos ||
                                      parent_type_name.find('_') != std::string::npos); // e.g., Option_T

            // Also check if we have an annotation for a generic return type
            bool has_generic_annotation = node->return_type_annotation() &&
                                         node->return_type_annotation()->to_string().find('<') != std::string::npos;

            if ((is_generic_context && resolved_type && resolved_type->kind() == Cryo::TypeKind::GenericParam) ||
                has_generic_annotation)
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN, "get_function_type: Method '{}' using opaque pointer return type for generic", node->name());
                // Use opaque pointer for generic return types that will be resolved later
                return_type = llvm::PointerType::get(llvm_ctx(), 0);
            }
            else
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN, "get_function_type: Using void return type for '{}'", node->name());
                return_type = llvm::Type::getVoidTy(llvm_ctx());
            }
        }

        // Collect parameter types
        std::vector<llvm::Type *> param_types;

        // Add 'this' parameter for methods
        if (has_this_param)
        {
            // Check if this is a simple enum - if so, pass 'this' by value as i32
            bool is_simple_enum = false;
            if (!parent_type_name.empty())
            {
                TypeRef parent_cryo_type = symbols().lookup_enum_type(parent_type_name);
                if (parent_cryo_type.is_valid() && parent_cryo_type->kind() == Cryo::TypeKind::Enum)
                {
                    auto *enum_type = static_cast<const Cryo::EnumType *>(parent_cryo_type.get());
                    is_simple_enum = enum_type->is_simple_enum();
                }
            }

            if (is_simple_enum)
            {
                // Simple enum 'this' is passed by value as i32
                param_types.push_back(llvm::Type::getInt32Ty(llvm_ctx()));
            }
            else
            {
                // Struct/class 'this' is passed as pointer
                param_types.push_back(llvm::PointerType::get(llvm_ctx(), 0));
            }
        }

        // Add regular parameters
        bool is_variadic = node->is_variadic();
        if (node->parameters().size() > 0)
        {
            for (const auto &param : node->parameters())
            {
                // Skip 'this' parameter - it's already added above when has_this_param is true
                if (param->name() == "this")
                {
                    continue;
                }

                TypeRef param_cryo_type = param->get_resolved_type();

                // Try to substitute type parameters if we're in a generic instantiation scope
                // This handles both direct type params (T) and types containing params (Option<T>)
                if (param_cryo_type.is_valid() && _generics && _generics->in_type_param_scope())
                {
                    TypeRef substituted = _generics->substitute_type_params(param_cryo_type);
                    if (substituted.is_valid() && substituted != param_cryo_type)
                    {
                        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "get_function_type: Substituted param '{}' type '{}' -> '{}'",
                                  param->name(), param_cryo_type->display_name(), substituted->display_name());
                        param_cryo_type = substituted;
                    }
                }

                llvm::Type *param_type = get_llvm_type(param_cryo_type);
                if (param_type)
                {
                    // Skip void parameters - void is not a valid parameter type in LLVM
                    // This happens when T=void in generic instantiation like Result<void>
                    if (param_type->isVoidTy())
                    {
                        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                 "get_function_type: Skipping void parameter '{}' in function",
                                 param->name());
                        continue;
                    }
                    param_types.push_back(param_type);
                }
                else
                {
                    // Fallback to pointer type if parameter type can't be resolved
                    // This prevents silently dropping parameters which causes argument count mismatches
                    LOG_WARN(Cryo::LogComponent::CODEGEN,
                             "get_function_type: Parameter '{}' type could not be resolved, using ptr fallback",
                             param->name());
                    param_types.push_back(llvm::PointerType::get(llvm_ctx(), 0));
                }
            }
        }

        return llvm::FunctionType::get(return_type, param_types, is_variadic);
    }

    std::string DeclarationCodegen::mangle_function_name(const std::string &name,
                                                         const std::vector<std::string> &namespace_parts,
                                                         const std::vector<TypeRef> &param_types)
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
                    result += param_types[i].get()->display_name();
                }
            }
            result += ")";
        }

        return result;
    }

    std::string DeclarationCodegen::generate_method_name(const std::string &type_name,
                                                         const std::string &method_name,
                                                         const std::vector<TypeRef> &param_types)
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
                    result += param_types[i].get()->display_name();
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
                TypeRef param_type = ast_params[param_idx]->get_resolved_type();
                // Apply type substitution if we're in a generic instantiation scope
                if (param_type.is_valid() && _generics)
                {
                    TypeRef substituted = _generics->substitute_type_params(param_type);
                    if (substituted.is_valid())
                    {
                        param_type = substituted;
                    }
                }
                if (param_type)
                {
                    std::string param_name = arg.getName().str();
                    ctx().variable_types_map()[param_name] = param_type;
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "DeclarationCodegen: Registered function parameter type: {} -> {} (kind: {})",
                              param_name, param_type.get()->display_name(),
                              static_cast<int>(param_type->kind()));

                    // Special logging for Array<T> parameters
                    if (param_type->kind() == TypeKind::Array ||
                        param_type.get()->display_name().find("[]") != std::string::npos)
                    {
                        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                  "DeclarationCodegen: *** Registered Array<T> parameter: {} with type: {}",
                                  param_name, param_type.get()->display_name());
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
                                                                     llvm::StructType* struct_type)
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
            // Check if the initializer is a constructor call for a class/struct type
            // Constructor calls generate runtime instructions (malloc, calls) that can't be
            // used as constant initializers. These must be handled by the global constructor
            // mechanism instead. Return zeroinitializer and let generate_global_constructors
            // call the constructor at runtime.
            if (type->isStructTy())
            {
                // Check if it's a call expression (constructor call)
                if (dynamic_cast<Cryo::CallExpressionNode *>(node->initializer()))
                {
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "generate_global_initializer: '{}' has struct type with constructor call, "
                              "using zeroinitializer (constructor will be called via global_ctors)",
                              node->name());
                    return llvm::Constant::getNullValue(type);
                }
            }

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

        // Skip methods that have their own generic parameters (e.g., alloc_one<T>)
        // These should only be generated when instantiated with concrete types
        if (!node->generic_parameters().empty())
        {
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "DeclarationCodegen: Skipping generic method '{}' - has {} generic type parameters",
                      node->name(), node->generic_parameters().size());
            return;
        }

        // Additional check: detect uninstantiated type parameters in signature
        // This handles both direct type params (T) and types containing params (Option<T>)
        for (const auto &param : node->parameters())
        {
            if (param && param->get_resolved_type())
            {
                TypeRef ptype = param->get_resolved_type();
                // Try to substitute type parameters if we're in a generic instantiation scope
                if (_generics && _generics->in_type_param_scope())
                {
                    TypeRef substituted = _generics->substitute_type_params(ptype);
                    if (substituted.is_valid() && substituted != ptype)
                    {
                        // Successfully substituted, use the concrete type
                        ptype = substituted;
                    }
                }
                if (ptype->kind() == TypeKind::GenericParam)
                {
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "DeclarationCodegen: Skipping method '{}' - param has generic type '{}'",
                              node->name(), ptype.get()->display_name());
                    return;
                }
                if (ptype->kind() == TypeKind::Struct || ptype->kind() == TypeKind::Class)
                {
                    llvm::StructType *existing = llvm::StructType::getTypeByName(llvm_ctx(), ptype.get()->display_name());
                    if (!existing)
                    {
                        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                  "DeclarationCodegen: Skipping method '{}' - param has undefined type '{}'",
                                  node->name(), ptype.get()->display_name());
                        return;
                    }
                }
            }
        }

        // Also check if return type has generic parameters
        if (node->get_resolved_return_type())
        {
            TypeRef ret_type = node->get_resolved_return_type();
            // Try to substitute type parameters if we're in a generic instantiation scope
            if (_generics && _generics->in_type_param_scope())
            {
                TypeRef substituted = _generics->substitute_type_params(ret_type);
                if (substituted.is_valid() && substituted != ret_type)
                {
                    ret_type = substituted;
                }
            }
            if (ret_type->kind() == TypeKind::GenericParam)
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "DeclarationCodegen: Skipping method '{}' - return type is generic '{}'",
                          node->name(), ret_type.get()->display_name());
                return;
            }
            // Also check for pointer to generic type (after substitution)
            if (ret_type->kind() == TypeKind::Pointer)
            {
                auto *ptr_type = static_cast<const PointerType *>(ret_type.get());
                TypeRef pointee = ptr_type->pointee();
                if (pointee.is_valid() && pointee->kind() == TypeKind::GenericParam)
                {
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "DeclarationCodegen: Skipping method '{}' - return type is pointer to generic",
                              node->name());
                    return;
                }
            }
            // Check if return type is an error type (unresolved generics, undefined types, etc.)
            if (ret_type.is_error())
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "DeclarationCodegen: Skipping method '{}' - return type is error: '{}'",
                          node->name(), ret_type->display_name());
                // Emit actual error so it shows up in diagnostics
                report_error(ErrorCode::E0643_RETURN_TYPE_ERROR, node,
                            "Method '" + node->name() + "' has unresolved return type: " + ret_type->display_name());
                return;
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
                    TypeRef this_type = ctx().symbols().lookup_struct_type(parent_type);
                    if (!this_type.is_valid())
                    {
                        this_type = ctx().symbols().lookup_class_type(parent_type);
                    }
                    // Fallback: check for primitive types (implement f32/f64/i32/etc. blocks)
                    if (!this_type.is_valid())
                    {
                        TypeArena &arena = ctx().symbols().arena();
                        if (parent_type == "f32")
                            this_type = arena.get_f32();
                        else if (parent_type == "f64")
                            this_type = arena.get_f64();
                        else if (parent_type == "i8")
                            this_type = arena.get_i8();
                        else if (parent_type == "i16")
                            this_type = arena.get_i16();
                        else if (parent_type == "i32")
                            this_type = arena.get_i32();
                        else if (parent_type == "i64")
                            this_type = arena.get_i64();
                        else if (parent_type == "u8")
                            this_type = arena.get_u8();
                        else if (parent_type == "u16")
                            this_type = arena.get_u16();
                        else if (parent_type == "u32")
                            this_type = arena.get_u32();
                        else if (parent_type == "u64")
                            this_type = arena.get_u64();
                        else if (parent_type == "boolean" || parent_type == "bool")
                            this_type = arena.get_bool();
                        else if (parent_type == "char")
                            this_type = arena.get_char();
                    }
                    if (this_type.is_valid())
                    {
                        // For primitive implement blocks, 'this' is passed by reference (&this),
                        // so we need to store the reference type, not the primitive directly.
                        // This allows the auto-dereference logic in ExpressionCodegen to work correctly.
                        if (this_type->is_primitive())
                        {
                            TypeArena &type_arena = ctx().symbols().arena();
                            TypeRef ref_type = type_arena.get_reference_to(this_type);
                            ctx().variable_types_map()["this"] = ref_type;
                            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                      "Registered 'this' reference type for primitive implement block: {} -> {}",
                                      parent_type, ref_type->display_name());
                        }
                        else
                        {
                            ctx().variable_types_map()["this"] = this_type;
                            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                      "Registered 'this' type for struct method: {} -> {}",
                                      parent_type, this_type->display_name());
                        }
                    }
                }
            }

            // Check if this is a static method to handle parameter indexing correctly
            bool is_static_method = false;
            if (auto *struct_method = dynamic_cast<Cryo::StructMethodNode *>(node))
            {
                is_static_method = struct_method->is_static();
            }

            // Register all parameter types in variable_types_map for Array<T> detection
            // StructMethodNode inherits from FunctionDeclarationNode so parameters() is available
            // Note: AST params DO include 'this' for non-static methods, so LLVM args and AST params
            // are aligned 1:1. We still skip 'this' since it's handled separately above.
            const auto &ast_params = node->parameters();
            size_t param_idx = 0;
            for (auto &arg : fn->args())
            {
                // Skip 'this' as it's handled above (only for non-static methods)
                if (arg.getName() == "this")
                {
                    param_idx++;
                    continue;
                }

                // AST params and LLVM args are aligned 1:1 (both include 'this')
                // So we use param_idx directly
                if (param_idx < ast_params.size())
                {
                    TypeRef param_type = ast_params[param_idx]->get_resolved_type();
                    // Apply type substitution if we're in a generic instantiation scope
                    if (param_type.is_valid() && _generics)
                    {
                        TypeRef substituted = _generics->substitute_type_params(param_type);
                        if (substituted.is_valid())
                        {
                            param_type = substituted;
                        }
                    }
                    if (param_type.is_valid())
                    {
                        std::string param_name = arg.getName().str();
                        ctx().variable_types_map()[param_name] = param_type;
                        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                  "DeclarationCodegen: Registered method parameter type: {} -> {} (kind: {})",
                                  param_name, param_type->display_name(),
                                  static_cast<int>(param_type->kind()));

                        // Special logging for Array<T> parameters
                        if (param_type->kind() == TypeKind::Array ||
                            param_type->display_name().find("[]") != std::string::npos)
                        {
                            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                      "DeclarationCodegen: *** Registered Array<T> method parameter: {} with type: {}",
                                      param_name, param_type->display_name());
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

                    // Even though we skip codegen for generic impl blocks, we still need to
                    // register method return types in the TemplateRegistry for cross-module lookups.
                    // This allows other modules to create correct extern declarations.
                    Cryo::TemplateRegistry *template_registry = ctx().template_registry();
                    if (template_registry)
                    {
                        // Extract base type name (e.g., "Option" from "Option<T>")
                        std::string base_type_name = type_name.substr(0, type_name.find('<'));
                        std::string ns_context = ctx().namespace_context();
                        std::string qualified_type = ns_context.empty() ? base_type_name : ns_context + "::" + base_type_name;

                        for (const auto &method : node->method_implementations())
                        {
                            Cryo::StructMethodNode *fn_node = method.get();
                            if (fn_node)
                            {
                                TypeRef return_type = fn_node->get_resolved_return_type();
                                if (return_type)
                                {
                                    std::string qualified_method_name = qualified_type + "::" + fn_node->name();
                                    template_registry->register_method_return_type(qualified_method_name, return_type);
                                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                              "Registered generic method return type: {} -> {}",
                                              qualified_method_name, return_type.get()->display_name());
                                }
                            }
                        }
                    }

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

                // Also try with namespace prefix if not found
                if (!fn)
                {
                    std::string ns_context = ctx().namespace_context();
                    if (!ns_context.empty())
                    {
                        std::string namespaced_name = ns_context + "::" + method_name;
                        fn = module()->getFunction(namespaced_name);
                        if (fn)
                        {
                            method_name = namespaced_name; // Update for scope entry below
                        }
                    }
                }

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
                            // Get the Cryo type for the current struct/class/enum
                            TypeRef this_type = ctx().symbols().lookup_struct_type(type_name);
                            if (!this_type.is_valid())
                            {
                                this_type = ctx().symbols().lookup_class_type(type_name);
                            }
                            if (!this_type.is_valid())
                            {
                                this_type = ctx().symbols().lookup_enum_type(type_name);
                            }
                            // Fallback: check for primitive types (implement f32/f64/i32/etc. blocks)
                            if (!this_type.is_valid())
                            {
                                TypeArena &arena = ctx().symbols().arena();
                                if (type_name == "f32")
                                    this_type = arena.get_f32();
                                else if (type_name == "f64")
                                    this_type = arena.get_f64();
                                else if (type_name == "i8")
                                    this_type = arena.get_i8();
                                else if (type_name == "i16")
                                    this_type = arena.get_i16();
                                else if (type_name == "i32")
                                    this_type = arena.get_i32();
                                else if (type_name == "i64")
                                    this_type = arena.get_i64();
                                else if (type_name == "u8")
                                    this_type = arena.get_u8();
                                else if (type_name == "u16")
                                    this_type = arena.get_u16();
                                else if (type_name == "u32")
                                    this_type = arena.get_u32();
                                else if (type_name == "u64")
                                    this_type = arena.get_u64();
                                else if (type_name == "boolean" || type_name == "bool")
                                    this_type = arena.get_bool();
                                else if (type_name == "char")
                                    this_type = arena.get_char();
                            }
                            if (this_type.is_valid())
                            {
                                // For primitive implement blocks, 'this' is passed by reference (&this),
                                // so we need to store the reference type, not the primitive directly.
                                // This allows the auto-dereference logic in ExpressionCodegen to work correctly.
                                if (this_type->is_primitive())
                                {
                                    TypeArena &arena = ctx().symbols().arena();
                                    TypeRef ref_type = arena.get_reference_to(this_type);
                                    ctx().variable_types_map()["this"] = ref_type;
                                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                              "Registered 'this' reference type for primitive implement block: {} -> {}",
                                              type_name, ref_type->display_name());
                                }
                                else
                                {
                                    ctx().variable_types_map()["this"] = this_type;
                                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                              "Registered 'this' type for method: {} -> {}",
                                              type_name, this_type->display_name());
                                }
                            }
                        }
                    }

                    // Register all parameter types in variable_types_map
                    // AST params and LLVM args are aligned 1:1 (both include 'this')
                    const auto &ast_params = fn_node->parameters();
                    size_t param_idx = 0;
                    for (auto &arg : fn->args())
                    {
                        // Skip 'this' as it's handled above
                        if (arg.getName() == "this")
                        {
                            param_idx++;
                            continue;
                        }

                        if (param_idx < ast_params.size())
                        {
                            TypeRef param_type = ast_params[param_idx]->get_resolved_type();
                            // Apply type substitution if we're in a generic instantiation scope
                            if (param_type.is_valid() && _generics)
                            {
                                TypeRef substituted = _generics->substitute_type_params(param_type);
                                if (substituted.is_valid())
                                {
                                    param_type = substituted;
                                }
                            }
                            if (param_type.is_valid())
                            {
                                std::string param_name = arg.getName().str();
                                ctx().variable_types_map()[param_name] = param_type;
                                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                          "Registered impl method parameter type: {} -> {}",
                                          param_name, param_type->display_name());
                            }
                        }
                        param_idx++;
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

        // Check if @llvm.global_ctors already exists - if so, we've already processed this
        if (mod->getNamedGlobal("llvm.global_ctors"))
        {
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                     "Global constructors already registered, skipping duplicate generation");
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

        // Check if the constructor function already exists
        if (mod->getFunction("__cryo_global_constructors"))
        {
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                     "Global constructor function already exists, skipping duplicate creation");
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
            if (llvm::StructType* struct_type = llvm::dyn_cast<llvm::StructType>(global_type))
            {
                std::string type_name = struct_type->hasName() ? struct_type->getName().str() : "unnamed_struct";
                std::string global_name = global->getName().str();

                // Extract base type name (without namespace) for constructor lookup
                // e.g., "std::Runtime::CryoRuntime" -> "CryoRuntime"
                std::string base_type_name = type_name;
                size_t last_sep = type_name.rfind("::");
                if (last_sep != std::string::npos)
                {
                    base_type_name = type_name.substr(last_sep + 2);
                }

                // Generic constructor lookup - constructor is TypeName::TypeName
                // For namespaced types: std::Runtime::CryoRuntime::CryoRuntime
                std::string ctor_name = type_name + "::" + base_type_name;

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
                        type_name + "::init",
                        type_name + "()",
                        base_type_name + "::" + base_type_name,  // For non-namespaced lookup
                        "std::Runtime::" + base_type_name + "::" + base_type_name
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

    //===================================================================
    // Type Resolution Helpers
    //===================================================================

    TypeRef DeclarationCodegen::resolve_type_annotation(const TypeAnnotation *annotation)
    {
        if (!annotation)
            return TypeRef();

        TypeArena &arena = ctx().types().arena();

        switch (annotation->kind)
        {
        case TypeAnnotationKind::Primitive:
        {
            // Lookup primitive type in arena
            const std::string &name = annotation->name;
            if (name == "void") return arena.get_void();
            if (name == "bool") return arena.get_bool();
            if (name == "char") return arena.get_char();
            if (name == "string" || name == "String") return arena.get_string();
            if (name == "i8") return arena.get_i8();
            if (name == "i16") return arena.get_i16();
            if (name == "i32" || name == "int") return arena.get_i32();
            if (name == "i64") return arena.get_i64();
            if (name == "i128") return arena.get_i128();
            if (name == "u8") return arena.get_u8();
            if (name == "u16") return arena.get_u16();
            if (name == "u32" || name == "uint") return arena.get_u32();
            if (name == "u64") return arena.get_u64();
            if (name == "u128") return arena.get_u128();
            if (name == "f32" || name == "float") return arena.get_f32();
            if (name == "f64" || name == "double") return arena.get_f64();

            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "resolve_type_annotation: Unknown primitive type '{}'", name);
            return TypeRef();
        }

        case TypeAnnotationKind::Named:
        {
            // Try to lookup as struct, class, or enum type
            const std::string &name = annotation->name;

            TypeRef resolved = ctx().symbols().lookup_struct_type(name);
            if (resolved.is_valid() && !resolved.is_error())
                return resolved;

            resolved = ctx().symbols().lookup_class_type(name);
            if (resolved.is_valid() && !resolved.is_error())
                return resolved;

            resolved = ctx().symbols().lookup_enum_type(name);
            if (resolved.is_valid() && !resolved.is_error())
                return resolved;

            // Try with current namespace prefix
            std::string ns = ctx().namespace_context();
            if (!ns.empty())
            {
                std::string qualified = ns + "::" + name;
                resolved = ctx().symbols().lookup_struct_type(qualified);
                if (resolved.is_valid() && !resolved.is_error())
                    return resolved;

                resolved = ctx().symbols().lookup_class_type(qualified);
                if (resolved.is_valid() && !resolved.is_error())
                    return resolved;

                resolved = ctx().symbols().lookup_enum_type(qualified);
                if (resolved.is_valid() && !resolved.is_error())
                    return resolved;
            }

            // Check if this is a generic template type (e.g., Array, Option, Result)
            // Generic templates aren't stored in the symbol table as regular types,
            // they're registered in the GenericRegistry
            GenericRegistry *generics = types().generic_registry();
            if (generics)
            {
                auto template_info = generics->get_template_by_name(name);
                if (template_info)
                {
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "resolve_type_annotation: Found generic template '{}' in GenericRegistry", name);
                    return template_info->generic_type;
                }
            }

            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "resolve_type_annotation: Could not resolve named type '{}'", name);
            return TypeRef();
        }

        case TypeAnnotationKind::Qualified:
        {
            // Join qualified path and lookup
            std::string qualified_name;
            for (size_t i = 0; i < annotation->qualified_path.size(); ++i)
            {
                if (i > 0) qualified_name += "::";
                qualified_name += annotation->qualified_path[i];
            }

            TypeRef resolved = ctx().symbols().lookup_struct_type(qualified_name);
            if (resolved.is_valid() && !resolved.is_error())
                return resolved;

            resolved = ctx().symbols().lookup_class_type(qualified_name);
            if (resolved.is_valid() && !resolved.is_error())
                return resolved;

            resolved = ctx().symbols().lookup_enum_type(qualified_name);
            if (resolved.is_valid() && !resolved.is_error())
                return resolved;

            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "resolve_type_annotation: Could not resolve qualified type '{}'", qualified_name);
            return TypeRef();
        }

        case TypeAnnotationKind::Generic:
        {
            // Resolve base type from inner annotation
            if (!annotation->inner)
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "resolve_type_annotation: Generic type has no inner (base) annotation");
                return TypeRef();
            }

            TypeRef base_type = resolve_type_annotation(annotation->inner.get());
            if (!base_type.is_valid() || base_type.is_error())
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "resolve_type_annotation: Failed to resolve generic base type");
                return base_type;
            }

            // Resolve type arguments from elements
            std::vector<TypeRef> type_args;
            type_args.reserve(annotation->elements.size());

            for (const auto &arg_annotation : annotation->elements)
            {
                TypeRef arg_type = resolve_type_annotation(&arg_annotation);
                if (!arg_type.is_valid() || arg_type.is_error())
                {
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "resolve_type_annotation: Failed to resolve type argument");
                    return arg_type;
                }
                type_args.push_back(arg_type);
            }

            // Create instantiated type
            TypeRef instantiated = arena.create_instantiation(base_type, std::move(type_args));

            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "resolve_type_annotation: Created instantiation '{}' with {} type args",
                      instantiated.is_valid() ? instantiated->display_name() : "invalid",
                      annotation->elements.size());

            return instantiated;
        }

        case TypeAnnotationKind::Pointer:
        {
            if (!annotation->inner)
                return TypeRef();

            TypeRef inner = resolve_type_annotation(annotation->inner.get());
            if (!inner.is_valid() || inner.is_error())
                return inner;

            return arena.get_pointer_to(inner);
        }

        case TypeAnnotationKind::Reference:
        {
            if (!annotation->inner)
                return TypeRef();

            TypeRef inner = resolve_type_annotation(annotation->inner.get());
            if (!inner.is_valid() || inner.is_error())
                return inner;

            return arena.get_reference_to(inner,
                annotation->is_mutable ? RefMutability::Mutable : RefMutability::Immutable);
        }

        case TypeAnnotationKind::Array:
        {
            if (!annotation->inner)
                return TypeRef();

            TypeRef element = resolve_type_annotation(annotation->inner.get());
            if (!element.is_valid() || element.is_error())
                return element;

            return arena.get_array_of(element, annotation->array_size);
        }

        case TypeAnnotationKind::Optional:
        {
            if (!annotation->inner)
                return TypeRef();

            TypeRef inner = resolve_type_annotation(annotation->inner.get());
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
                TypeRef resolved = resolve_type_annotation(&elem);
                if (!resolved.is_valid() || resolved.is_error())
                    return resolved;
                elements.push_back(resolved);
            }

            return arena.get_tuple(std::move(elements));
        }

        case TypeAnnotationKind::Function:
        {
            // Resolve return type
            TypeRef return_type;
            if (annotation->return_type)
            {
                return_type = resolve_type_annotation(annotation->return_type.get());
                if (!return_type.is_valid() || return_type.is_error())
                    return return_type;
            }
            else
            {
                return_type = arena.get_void();
            }

            // Resolve parameter types
            std::vector<TypeRef> param_types;
            param_types.reserve(annotation->elements.size());

            for (const auto &param : annotation->elements)
            {
                TypeRef resolved = resolve_type_annotation(&param);
                if (!resolved.is_valid() || resolved.is_error())
                    return resolved;
                param_types.push_back(resolved);
            }

            return arena.get_function(return_type, std::move(param_types), annotation->is_variadic);
        }

        default:
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "resolve_type_annotation: Unhandled annotation kind {}",
                      static_cast<int>(annotation->kind));
            return TypeRef();
        }
    }

} // namespace Cryo::Codegen
