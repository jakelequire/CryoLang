#include "Codegen/ICodegenComponent.hpp"
#include "Utils/Logger.hpp"
#include "Utils/SymbolResolutionManager.hpp"

#include <unordered_set>

namespace Cryo::Codegen
{
    //===================================================================
    // Symbol Resolution (SRM)
    //===================================================================

    std::vector<std::string> ICodegenComponent::generate_lookup_candidates(
        const std::string &name, Cryo::SymbolKind kind)
    {
        // Use SRM to generate all possible name variations based on:
        // - Current namespace context
        // - Imported namespaces
        // - Namespace aliases
        // - Parent namespaces
        // - Global scope
        return srm().generate_lookup_candidates(name, kind);
    }

    llvm::Function *ICodegenComponent::resolve_function_by_name(const std::string &name)
    {
        // Generate all possible candidates using SRM
        auto candidates = generate_lookup_candidates(name, Cryo::SymbolKind::Function);

        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                  "resolve_function_by_name: Looking for '{}', {} candidates generated",
                  name, candidates.size());

        // Log all candidates for debugging
        for (size_t i = 0; i < candidates.size(); ++i)
        {
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "resolve_function_by_name: Candidate {}: '{}'", i, candidates[i]);
        }

        for (const auto &candidate : candidates)
        {
            // Try LLVM module first
            if (llvm::Function *fn = module()->getFunction(candidate))
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "resolve_function_by_name: Found '{}' as '{}'", name, candidate);
                return fn;
            }

            // Try context's function registry
            if (llvm::Function *fn = ctx().get_function(candidate))
            {
                // Validate that the function is properly formed
                if (fn->getName().empty())
                {
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "resolve_function_by_name: Found '{}' in registry as '{}' but function has empty name, skipping",
                              name, candidate);
                    continue;
                }
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "resolve_function_by_name: Found '{}' in registry as '{}'", name, candidate);
                return fn;
            }
        }

        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                  "resolve_function_by_name: '{}' not found after trying {} candidates",
                  name, candidates.size());
        return nullptr;
    }

    llvm::Type *ICodegenComponent::resolve_type_by_name(const std::string &name)
    {
        // Generate all possible candidates using SRM
        auto candidates = generate_lookup_candidates(name, Cryo::SymbolKind::Type);

        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                  "resolve_type_by_name: Looking for '{}', {} candidates generated",
                  name, candidates.size());

        for (const auto &candidate : candidates)
        {
            // Try context's type registry
            if (llvm::Type *type = ctx().get_type(candidate))
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "resolve_type_by_name: Found '{}' as '{}'", name, candidate);
                return type;
            }

            // Try LLVM context directly for struct types
            if (auto *st = llvm::StructType::getTypeByName(llvm_ctx(), candidate))
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "resolve_type_by_name: Found '{}' in LLVM context as '{}'", name, candidate);
                return st;
            }
        }

        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                  "resolve_type_by_name: '{}' not found after trying {} candidates",
                  name, candidates.size());
        return nullptr;
    }

    llvm::Function *ICodegenComponent::resolve_method_by_name(
        const std::string &type_name, const std::string &method_name)
    {
        // First, generate candidates for the type name
        auto type_candidates = generate_lookup_candidates(type_name, Cryo::SymbolKind::Type);

        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                  "resolve_method_by_name: Looking for '{}.{}', {} type candidates",
                  type_name, method_name, type_candidates.size());

        // Log all candidates for debugging
        for (size_t i = 0; i < type_candidates.size(); ++i)
        {
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "resolve_method_by_name: Candidate[{}]: '{}'", i, type_candidates[i]);
        }

        // For each type candidate, try to find the method
        for (const auto &type_candidate : type_candidates)
        {
            // Build qualified method name: Type::method
            std::string qualified_method = type_candidate + "::" + method_name;

            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "resolve_method_by_name: Trying '{}'", qualified_method);

            // Try LLVM module
            if (llvm::Function *fn = module()->getFunction(qualified_method))
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "resolve_method_by_name: Found '{}.{}' as '{}'",
                          type_name, method_name, qualified_method);
                return fn;
            }

            // Try context's function registry
            if (llvm::Function *fn = ctx().get_function(qualified_method))
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "resolve_method_by_name: Found '{}.{}' in registry as '{}'",
                          type_name, method_name, qualified_method);
                return fn;
            }
        }

        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                  "resolve_method_by_name: '{}.{}' not found after SRM lookup", type_name, method_name);

        // Try using registered type namespace from CodegenContext
        // This handles cross-module types like Option, Result, Array, etc.
        {
            // Extract base type name for parameterized types (e.g., "Option" from "Option<i64>")
            std::string base_type = type_name;
            size_t angle_pos = type_name.find('<');
            if (angle_pos != std::string::npos)
            {
                base_type = type_name.substr(0, angle_pos);
            }

            std::string type_namespace = ctx().get_type_namespace(base_type);
            if (!type_namespace.empty())
            {
                // Try fully qualified method name: namespace::Type::method
                std::string qualified_method = type_namespace + "::" + base_type + "::" + method_name;
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "resolve_method_by_name: Trying type namespace lookup '{}'", qualified_method);

                if (llvm::Function *fn = module()->getFunction(qualified_method))
                {
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "resolve_method_by_name: Found '{}.{}' via type namespace as '{}'",
                              type_name, method_name, qualified_method);
                    return fn;
                }

                if (llvm::Function *fn = ctx().get_function(qualified_method))
                {
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "resolve_method_by_name: Found '{}.{}' in registry via type namespace as '{}'",
                              type_name, method_name, qualified_method);
                    return fn;
                }
            }
        }

        // Fallback for primitive type methods (string, i32, u64, etc.)
        // These are defined in std::core::Types with fully qualified names
        static const std::unordered_set<std::string> primitive_types = {
            "string", "i8", "i16", "i32", "i64", "u8", "u16", "u32", "u64",
            "f32", "f64", "bool", "char", "boolean"};

        if (primitive_types.find(type_name) != primitive_types.end())
        {
            // Try simple type::method name
            std::string simple_method = type_name + "::" + method_name;
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "resolve_method_by_name: Trying primitive fallback '{}'", simple_method);

            if (llvm::Function *fn = module()->getFunction(simple_method))
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "resolve_method_by_name: Found primitive method via module: {}", simple_method);
                return fn;
            }

            if (llvm::Function *fn = ctx().get_function(simple_method))
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "resolve_method_by_name: Found primitive method via registry: {}", simple_method);
                return fn;
            }

            // Try with std::core::Types:: prefix (where primitive impl blocks live)
            std::string stdlib_method = "std::core::Types::" + simple_method;
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "resolve_method_by_name: Trying stdlib fallback '{}'", stdlib_method);

            if (llvm::Function *fn = module()->getFunction(stdlib_method))
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "resolve_method_by_name: Found primitive method via stdlib: {}", stdlib_method);
                return fn;
            }

            if (llvm::Function *fn = ctx().get_function(stdlib_method))
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "resolve_method_by_name: Found primitive method in registry: {}", stdlib_method);
                return fn;
            }

            // Method not found in current module - create an extern declaration
            // This is needed for cross-module calls (e.g., brainfuck.cryo calling types.cryo methods)
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "resolve_method_by_name: Creating extern declaration for primitive method: {}", stdlib_method);

            // Get the 'this' pointer type for the primitive
            llvm::Type *this_type = llvm::PointerType::get(llvm_ctx(), 0); // Opaque pointer

            // Determine return type based on known primitive methods
            llvm::Type *return_type = nullptr;
            if (method_name == "length" || method_name == "is_even" || method_name == "is_odd" ||
                method_name == "max_size")
            {
                // These return u32 or the primitive type
                if (type_name == "string")
                    return_type = llvm::Type::getInt32Ty(llvm_ctx()); // u32
                else
                    return_type = llvm::Type::getInt1Ty(llvm_ctx()); // boolean for is_even/is_odd
            }
            else if (method_name == "to_upper" || method_name == "to_lower" || method_name == "to_string")
            {
                return_type = llvm::PointerType::get(llvm_ctx(), 0); // Returns string (ptr)
            }
            else if (method_name == "test_method")
            {
                return_type = llvm::Type::getVoidTy(llvm_ctx());
            }
            else
            {
                // Default to i32 for unknown methods
                return_type = llvm::Type::getInt32Ty(llvm_ctx());
            }

            // Create function type: return_type(ptr this)
            std::vector<llvm::Type *> param_types = {this_type};
            llvm::FunctionType *fn_type = llvm::FunctionType::get(return_type, param_types, false);

            // Create extern declaration
            llvm::Function *fn = llvm::Function::Create(
                fn_type,
                llvm::Function::ExternalLinkage,
                stdlib_method,
                module());

            // Register in context
            ctx().register_function(stdlib_method, fn);

            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "resolve_method_by_name: Created extern declaration for: {}", stdlib_method);
            return fn;
        }

        // Additional fallback for known problematic cases
        if (type_name == "StackTrace" && method_name == "capture")
        {
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "Attempting direct fallback for StackTrace.capture");

            // Try the known fully-qualified function name
            if (llvm::Function *fn = module()->getFunction("std::Runtime::StackTrace::capture"))
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "Found method via direct fallback: std::Runtime::StackTrace::capture");
                return fn;
            }
            else
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "Direct fallback failed - function std::Runtime::StackTrace::capture not found in module");

                // Try to get from the function registry
                if (llvm::Function *fn = ctx().get_function("std::Runtime::StackTrace::capture"))
                {
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "Found method in function registry: std::Runtime::StackTrace::capture");
                    return fn;
                }
                else
                {
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "Function not found in registry either - creating declaration");

                    // Create a function declaration as a last resort
                    // StackTrace::capture() -> void (taking a StackTrace* as receiver)
                    llvm::LLVMContext &llvm_ctx = module()->getContext();
                    llvm::Type *void_type = llvm::Type::getVoidTy(llvm_ctx);
                    llvm::Type *stacktrace_ptr = llvm::PointerType::get(llvm::Type::getInt8Ty(llvm_ctx), 0);

                    // Create function type: void(StackTrace*)
                    std::vector<llvm::Type *> param_types = {stacktrace_ptr};
                    llvm::FunctionType *func_type = llvm::FunctionType::get(void_type, param_types, false);

                    // Create the function declaration
                    llvm::Function *new_fn = llvm::Function::Create(
                        func_type,
                        llvm::Function::ExternalLinkage,
                        "std::Runtime::StackTrace::capture",
                        module());

                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "Created function declaration for std::Runtime::StackTrace::capture");

                    return new_fn;
                }
            }
        }

        // Fallback for parameterized types (e.g., Option<i64>, Array<string>, Result<T, E>)
        // Extract the base type name and try to resolve the method using that
        size_t angle_pos = type_name.find('<');
        if (angle_pos != std::string::npos)
        {
            std::string base_type_name = type_name.substr(0, angle_pos);
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "resolve_method_by_name: Trying parameterized type fallback with base type '{}'", base_type_name);

            // Recursively try to resolve using the base type name
            llvm::Function *fn = resolve_method_by_name(base_type_name, method_name);
            if (fn)
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "resolve_method_by_name: Found '{}.{}' via base type '{}'",
                          type_name, method_name, base_type_name);
                return fn;
            }
        }

        // Pattern-based method lookup: scan module for functions matching *::BaseType::method
        // This handles cross-module cases where we don't have the namespace info
        {
            std::string base_type = type_name;
            size_t angle_pos = type_name.find('<');
            if (angle_pos != std::string::npos)
            {
                base_type = type_name.substr(0, angle_pos);
            }

            // Build pattern suffix: "::BaseType::method"
            std::string pattern_suffix = "::" + base_type + "::" + method_name;

            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "resolve_method_by_name: Scanning module for pattern '*{}'", pattern_suffix);

            for (auto &fn : module()->functions())
            {
                std::string fn_name = fn.getName().str();
                // Check if function name ends with our pattern
                if (fn_name.length() >= pattern_suffix.length() &&
                    fn_name.compare(fn_name.length() - pattern_suffix.length(),
                                    pattern_suffix.length(), pattern_suffix) == 0)
                {
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "resolve_method_by_name: Found '{}.{}' via pattern match as '{}'",
                              type_name, method_name, fn_name);
                    return &fn;
                }
            }

            // Also try in function registry
            for (const auto &[name, fn] : ctx().functions_map())
            {
                if (name.length() >= pattern_suffix.length() &&
                    name.compare(name.length() - pattern_suffix.length(),
                                 pattern_suffix.length(), pattern_suffix) == 0)
                {
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "resolve_method_by_name: Found '{}.{}' in registry via pattern match as '{}'",
                              type_name, method_name, name);
                    return fn;
                }
            }
        }

        // Create extern declarations for cross-module method calls
        // Uses TemplateRegistry to dynamically look up the type's defining namespace
        {
            std::string base_type = type_name;
            size_t angle_pos = type_name.find('<');
            if (angle_pos != std::string::npos)
            {
                base_type = type_name.substr(0, angle_pos);
            }

            // Try to get namespace from TemplateRegistry (dynamic lookup)
            std::string type_namespace;
            Cryo::TemplateRegistry *template_registry = ctx().template_registry();
            if (template_registry)
            {
                const Cryo::TemplateRegistry::TemplateInfo *template_info = template_registry->find_template(base_type);
                if (template_info)
                {
                    type_namespace = template_info->module_namespace;
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "resolve_method_by_name: Found template '{}' in namespace '{}'",
                              base_type, type_namespace);
                }
            }

            // Also check the type namespace map (for locally defined types)
            if (type_namespace.empty())
            {
                type_namespace = ctx().get_type_namespace(base_type);
            }

            if (!type_namespace.empty())
            {
                std::string full_method_name = type_namespace + "::" + base_type + "::" + method_name;
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "resolve_method_by_name: Trying dynamically resolved method '{}'", full_method_name);

                // Check if it already exists
                if (llvm::Function *existing = module()->getFunction(full_method_name))
                {
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "resolve_method_by_name: Found existing declaration '{}'", full_method_name);
                    return existing;
                }

                // Check function registry
                if (llvm::Function *existing = ctx().get_function(full_method_name))
                {
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "resolve_method_by_name: Found in registry '{}'", full_method_name);
                    return existing;
                }

                // Method exists in template but not yet compiled - create extern declaration
                // Get method signature from the template if possible
                llvm::Type *return_type = nullptr;
                std::vector<llvm::Type *> param_types;

                // Add 'this' parameter (pointer to the type)
                param_types.push_back(llvm::PointerType::get(llvm_ctx(), 0));

                // Try to get return type from template methods
                if (template_registry)
                {
                    const Cryo::TemplateRegistry::TemplateInfo *template_info = template_registry->find_template(base_type);
                    if (template_info && template_info->struct_template)
                    {
                        for (const auto &method : template_info->struct_template->methods())
                        {
                            if (method->name() == method_name)
                            {
                                Cryo::Type *method_return = method->get_resolved_return_type();
                                if (method_return)
                                {
                                    return_type = types().get_type(method_return);
                                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                              "resolve_method_by_name: Got return type from struct template method: {}",
                                              method_return->to_string());
                                }
                                break;
                            }
                        }
                    }
                    else if (template_info && template_info->class_template)
                    {
                        for (const auto &method : template_info->class_template->methods())
                        {
                            if (method->name() == method_name)
                            {
                                Cryo::Type *method_return = method->get_resolved_return_type();
                                if (method_return)
                                {
                                    return_type = types().get_type(method_return);
                                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                              "resolve_method_by_name: Got return type from class template method: {}",
                                              method_return->to_string());
                                }
                                break;
                            }
                        }
                    }
                    // Note: EnumDeclarationNode doesn't have methods() - enum methods are in impl blocks
                    // which are stored separately. Check the method registry for return type.
                }

                // Try to get return type from method registry (populated from impl blocks)
                // First check local CodegenContext, then shared TemplateRegistry
                if (!return_type)
                {
                    Cryo::Type *cryo_return_type = ctx().get_method_return_type(full_method_name);
                    if (!cryo_return_type && template_registry)
                    {
                        // Try TemplateRegistry for cross-module method signatures
                        cryo_return_type = template_registry->get_method_return_type(full_method_name);
                    }

                    if (cryo_return_type)
                    {
                        return_type = types().get_type(cryo_return_type);
                        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                  "resolve_method_by_name: Got return type from method registry for '{}': {}",
                                  full_method_name, cryo_return_type->to_string());
                    }
                }

                // Default return type if not found
                if (!return_type)
                {
                    return_type = llvm::Type::getVoidTy(llvm_ctx());
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "resolve_method_by_name: Using default void return type for '{}'", full_method_name);
                }

                // Create function type and declaration
                llvm::FunctionType *fn_type = llvm::FunctionType::get(return_type, param_types, false);
                llvm::Function *fn = llvm::Function::Create(
                    fn_type,
                    llvm::Function::ExternalLinkage,
                    full_method_name,
                    module());

                // Register in context
                ctx().register_function(full_method_name, fn);

                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "resolve_method_by_name: Created extern declaration for '{}'", full_method_name);

                return fn;
            }
        }

        // Final diagnostic: list all functions in module containing the method name
        // This helps debug what the actual registered name is
        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                  "resolve_method_by_name: FAILED to find '{}.{}' - listing functions containing '{}':",
                  type_name, method_name, method_name);
        int match_count = 0;
        for (const auto &fn : module()->functions())
        {
            std::string fn_name = fn.getName().str();
            if (fn_name.find(method_name) != std::string::npos)
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "  Module function: '{}'", fn_name);
                match_count++;
                if (match_count >= 10)
                {
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN, "  ... (truncated, more matches exist)");
                    break;
                }
            }
        }
        if (match_count == 0)
        {
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "  No functions in module contain '{}'", method_name);
        }

        return nullptr;
    }

    std::string ICodegenComponent::qualify_symbol_name(const std::string &name, Cryo::SymbolKind kind)
    {
        // If already qualified, return as-is
        if (name.find("::") != std::string::npos)
        {
            return name;
        }

        // Use SRM context to create a qualified identifier
        auto identifier = srm_context().create_qualified_identifier(name, kind);
        if (identifier)
        {
            return identifier->to_string();
        }

        // Fallback: return original name
        return name;
    }

    std::string ICodegenComponent::build_method_name(
        const std::string &type_name, const std::string &method_name)
    {
        // Parse the type name to get namespace parts and base name
        auto [ns_parts, base_type] = Cryo::SRM::Utils::parse_qualified_name(type_name);

        // Build the method name: namespace::Type::method
        std::vector<std::string> method_parts = ns_parts;
        method_parts.push_back(base_type);

        return Cryo::SRM::Utils::build_qualified_name(method_parts, method_name);
    }

    std::string ICodegenComponent::build_constructor_name(const std::string &type_name)
    {
        // Parse the type name to get namespace parts and base name
        auto [ns_parts, base_type] = Cryo::SRM::Utils::parse_qualified_name(type_name);

        // Constructor name is Type::Type
        std::vector<std::string> ctor_parts = ns_parts;
        ctor_parts.push_back(base_type);

        return Cryo::SRM::Utils::build_qualified_name(ctor_parts, base_type);
    }

    //===================================================================
    // Common Memory Operations
    //===================================================================

    llvm::AllocaInst *ICodegenComponent::create_entry_alloca(llvm::Function *fn, llvm::Type *type, const std::string &name)
    {
        if (!fn || !type)
            return nullptr;

        // Save current insertion point
        llvm::IRBuilder<> &b = builder();
        llvm::BasicBlock *current_block = b.GetInsertBlock();
        llvm::BasicBlock::iterator current_point = b.GetInsertPoint();

        // Get the entry block
        llvm::BasicBlock &entry = fn->getEntryBlock();

        // Insert at the beginning of entry block (after any existing allocas)
        if (entry.empty())
        {
            b.SetInsertPoint(&entry);
        }
        else
        {
            // Find the first non-alloca instruction
            llvm::BasicBlock::iterator insert_point = entry.begin();
            while (insert_point != entry.end() && llvm::isa<llvm::AllocaInst>(&*insert_point))
            {
                ++insert_point;
            }
            b.SetInsertPoint(&entry, insert_point);
        }

        // Create the alloca
        llvm::AllocaInst *alloca = b.CreateAlloca(type, nullptr, name);

        // Restore insertion point
        if (current_block)
        {
            b.SetInsertPoint(current_block, current_point);
        }

        return alloca;
    }

    llvm::AllocaInst *ICodegenComponent::create_entry_alloca(llvm::Type *type, const std::string &name)
    {
        FunctionContext *fn_ctx = ctx().current_function();
        if (!fn_ctx || !fn_ctx->function)
            return nullptr;

        return create_entry_alloca(fn_ctx->function, type, name);
    }

    llvm::Value *ICodegenComponent::create_load(llvm::Value *ptr, llvm::Type *type, const std::string &name)
    {
        if (!ptr)
            return nullptr;

        if (!ptr->getType()->isPointerTy())
        {
            LOG_WARN(Cryo::LogComponent::CODEGEN, "create_load called on non-pointer type");
            return ptr;
        }

        return builder().CreateLoad(type, ptr, name.empty() ? "load" : name);
    }

    void ICodegenComponent::create_store(llvm::Value *value, llvm::Value *ptr)
    {
        if (!value || !ptr)
            return;

        if (!ptr->getType()->isPointerTy())
        {
            LOG_ERROR(Cryo::LogComponent::CODEGEN, "create_store called with non-pointer destination");
            return;
        }

        builder().CreateStore(value, ptr);
    }

    llvm::Value *ICodegenComponent::create_struct_gep(llvm::Type *struct_type, llvm::Value *ptr,
                                                      unsigned field_idx, const std::string &name)
    {
        if (!struct_type || !ptr)
            return nullptr;

        return builder().CreateStructGEP(struct_type, ptr, field_idx,
                                         name.empty() ? "field.ptr" : name);
    }

    llvm::Value *ICodegenComponent::create_array_gep(llvm::Type *element_type, llvm::Value *ptr,
                                                     llvm::Value *index, const std::string &name)
    {
        if (!element_type || !ptr || !index)
            return nullptr;

        return builder().CreateGEP(element_type, ptr, index,
                                   name.empty() ? "elem.ptr" : name);
    }

    //===================================================================
    // Common Control Flow Operations
    //===================================================================

    llvm::BasicBlock *ICodegenComponent::create_block(const std::string &name)
    {
        FunctionContext *fn_ctx = ctx().current_function();
        if (!fn_ctx || !fn_ctx->function)
            return nullptr;

        return create_block(name, fn_ctx->function);
    }

    llvm::BasicBlock *ICodegenComponent::create_block(const std::string &name, llvm::Function *fn)
    {
        if (!fn)
            return nullptr;

        return llvm::BasicBlock::Create(llvm_ctx(), name, fn);
    }

    void ICodegenComponent::ensure_valid_insertion_point()
    {
        llvm::IRBuilder<> &b = builder();
        llvm::BasicBlock *current = b.GetInsertBlock();

        // Check if we have a valid insertion point
        if (!current)
        {
            // No current block - try to create one in current function
            FunctionContext *fn_ctx = ctx().current_function();
            if (fn_ctx && fn_ctx->function)
            {
                llvm::BasicBlock *fallback = create_block("fallback", fn_ctx->function);
                b.SetInsertPoint(fallback);
            }
            return;
        }

        // Check if current block is already terminated
        if (current->getTerminator())
        {
            // Block is terminated - create a new unreachable block
            FunctionContext *fn_ctx = ctx().current_function();
            if (fn_ctx && fn_ctx->function)
            {
                llvm::BasicBlock *unreachable = create_block("unreachable", fn_ctx->function);
                b.SetInsertPoint(unreachable);
            }
        }
    }

    //===================================================================
    // Common Type Operations
    //===================================================================

    llvm::Type *ICodegenComponent::get_llvm_type(Cryo::Type *cryo_type)
    {
        if (!cryo_type)
            return nullptr;

        return types().map_type(cryo_type);
    }

    llvm::Value *ICodegenComponent::cast_if_needed(llvm::Value *value, llvm::Type *target_type)
    {
        if (!value || !target_type)
        {
            LOG_WARN(Cryo::LogComponent::CODEGEN, "cast_if_needed called with null value or target_type");
            return value;
        }

        llvm::Type *source_type = value->getType();
        if (!source_type)
        {
            LOG_WARN(Cryo::LogComponent::CODEGEN, "cast_if_needed: value has null type");
            return llvm::Constant::getNullValue(target_type);
        }

        if (source_type == target_type)
            return value;

        llvm::IRBuilder<> &b = builder();

        // Target is boolean (i1)
        if (target_type->isIntegerTy(1))
        {
            // Pointer to bool: check if not null
            if (source_type->isPointerTy())
            {
                llvm::Value *null_ptr = llvm::ConstantPointerNull::get(
                    llvm::cast<llvm::PointerType>(source_type));
                return b.CreateICmpNE(value, null_ptr, "tobool");
            }
            // Integer to bool: check if not zero
            if (source_type->isIntegerTy())
            {
                return b.CreateICmpNE(value,
                                      llvm::ConstantInt::get(source_type, 0), "tobool");
            }
            // Float to bool: check if not zero
            if (source_type->isFloatingPointTy())
            {
                return b.CreateFCmpUNE(value,
                                       llvm::ConstantFP::get(source_type, 0.0), "tobool");
            }
        }

        // Integer to integer
        if (source_type->isIntegerTy() && target_type->isIntegerTy())
        {
            unsigned source_bits = source_type->getIntegerBitWidth();
            unsigned target_bits = target_type->getIntegerBitWidth();

            if (source_bits < target_bits)
            {
                // Extension - assume signed for safety
                return b.CreateSExt(value, target_type, "sext");
            }
            else if (source_bits > target_bits)
            {
                // Truncation
                return b.CreateTrunc(value, target_type, "trunc");
            }
        }

        // Float to float
        if (source_type->isFloatingPointTy() && target_type->isFloatingPointTy())
        {
            if (source_type->isFloatTy() && target_type->isDoubleTy())
            {
                return b.CreateFPExt(value, target_type, "fpext");
            }
            else if (source_type->isDoubleTy() && target_type->isFloatTy())
            {
                return b.CreateFPTrunc(value, target_type, "fptrunc");
            }
        }

        // Integer to float
        if (source_type->isIntegerTy() && target_type->isFloatingPointTy())
        {
            return b.CreateSIToFP(value, target_type, "sitofp");
        }

        // Float to integer
        if (source_type->isFloatingPointTy() && target_type->isIntegerTy())
        {
            return b.CreateFPToSI(value, target_type, "fptosi");
        }

        // Pointer casts (with opaque pointers, usually not needed)
        if (source_type->isPointerTy() && target_type->isPointerTy())
        {
            // With opaque pointers, this is usually a no-op
            return value;
        }

        // Boolean to integer (extend i1 to larger integer)
        if (source_type->isIntegerTy(1) && target_type->isIntegerTy())
        {
            // Zero-extend boolean to target integer type
            return b.CreateZExt(value, target_type, "zext.bool");
        }

        // Boolean/integer to pointer (for null comparisons or casts)
        if (source_type->isIntegerTy() && target_type->isPointerTy())
        {
            return b.CreateIntToPtr(value, target_type, "int2ptr");
        }

        // Pointer to integer
        if (source_type->isPointerTy() && target_type->isIntegerTy())
        {
            return b.CreatePtrToInt(value, target_type, "ptr2int");
        }

        // Can't cast - return null value of target type to avoid IR verification errors
        LOG_WARN(Cryo::LogComponent::CODEGEN, "Unable to cast between incompatible types, using null value");
        return llvm::Constant::getNullValue(target_type);
    }

    bool ICodegenComponent::is_alloca(llvm::Value *value) const
    {
        return value && llvm::isa<llvm::AllocaInst>(value);
    }

    //===================================================================
    // Error Reporting
    //===================================================================

    void ICodegenComponent::report_error(ErrorCode code, Cryo::ASTNode *node, const std::string &msg)
    {
        _ctx.report_error(code, node, msg);
    }

    void ICodegenComponent::report_error(ErrorCode code, const std::string &msg)
    {
        _ctx.report_error(code, msg);
    }

    //===================================================================
    // Value Registration
    //===================================================================

    void ICodegenComponent::register_value(Cryo::ASTNode *node, llvm::Value *value)
    {
        _ctx.register_value(node, value);
    }

    void ICodegenComponent::set_result(llvm::Value *value)
    {
        _ctx.set_result(value);
    }

    llvm::Value *ICodegenComponent::get_result()
    {
        return _ctx.get_result();
    }

} // namespace Cryo::Codegen
