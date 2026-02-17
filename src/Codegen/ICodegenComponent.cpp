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

        // Additional fallback: iterate all functions in module looking for matching suffix
        // This helps when main (unqualified) calls functions in the same namespace
        if (name.find("::") == std::string::npos)
        {
            std::string suffix = "::" + name;
            for (auto &fn : module()->functions())
            {
                std::string fn_name = fn.getName().str();
                // Check if function name ends with "::name"
                if (fn_name.size() > suffix.size() &&
                    fn_name.compare(fn_name.size() - suffix.size(), suffix.size(), suffix) == 0)
                {
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "resolve_function_by_name: Found '{}' via suffix match as '{}'",
                              name, fn_name);
                    return &fn;
                }
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
        // Prefer definitions (functions with bodies) over declarations to avoid
        // picking up forward declarations with the wrong qualified name (e.g., Main::Buz::new)
        // when the actual definition has a different prefix (e.g., Baz::Qix::Buz::new).
        llvm::Function *declaration_fallback = nullptr;
        for (const auto &type_candidate : type_candidates)
        {
            // Build qualified method name: Type::method
            std::string qualified_method = type_candidate + "::" + method_name;

            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "resolve_method_by_name: Trying '{}'", qualified_method);

            // Try LLVM module
            if (llvm::Function *fn = module()->getFunction(qualified_method))
            {
                if (!fn->isDeclaration())
                {
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "resolve_method_by_name: Found definition '{}.{}' as '{}'",
                              type_name, method_name, qualified_method);
                    return fn;
                }
                // Remember declaration as fallback but keep looking for a definition
                if (!declaration_fallback)
                    declaration_fallback = fn;
            }

            // Try context's function registry
            if (llvm::Function *fn = ctx().get_function(qualified_method))
            {
                if (!fn->isDeclaration())
                {
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "resolve_method_by_name: Found definition '{}.{}' in registry as '{}'",
                              type_name, method_name, qualified_method);
                    return fn;
                }
                if (!declaration_fallback)
                    declaration_fallback = fn;
            }
        }

        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                  "resolve_method_by_name: '{}.{}' not found after SRM lookup", type_name, method_name);

        // Try using registered type namespace from CodegenContext
        // This handles cross-module types like Option, Result, Array, etc.
        {
            // Extract base type name for namespace lookup (e.g., "Option" from "Option<i64>")
            std::string base_type = type_name;
            size_t angle_pos = type_name.find('<');
            if (angle_pos != std::string::npos)
            {
                base_type = type_name.substr(0, angle_pos);
            }

            // Convert display name to mangled name for method lookup
            // e.g., "Array<u64>" -> "Array_u64"
            std::string mangled_type = mangle_generic_type_name(type_name);

            // Strip namespace prefix from mangled_type if it's already qualified
            // This prevents double-namespacing like "ns::ns::Type::method"
            {
                size_t last_sep = mangled_type.rfind("::");
                if (last_sep != std::string::npos)
                {
                    mangled_type = mangled_type.substr(last_sep + 2);
                }
            }

            // Try namespace lookup with base type first
            std::string type_namespace = ctx().get_type_namespace(base_type);

            // Also try with mangled name if base type lookup fails
            if (type_namespace.empty())
            {
                type_namespace = ctx().get_type_namespace(mangled_type);
            }

            if (!type_namespace.empty())
            {
                // Try fully qualified method name with MANGLED type name: namespace::MangledType::method
                // e.g., "std::collections::array::Array_u64::push"
                std::string qualified_method = type_namespace + "::" + mangled_type + "::" + method_name;
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

        // Try primitive type methods (string, i32, u64, etc.)
        // These are defined in std::core::primitives with fully qualified names
        static const std::unordered_set<std::string> primitive_types = {
            "string", "i8", "i16", "i32", "i64", "u8", "u16", "u32", "u64",
            "f32", "f64", "bool", "char", "boolean"};

        if (primitive_types.find(type_name) != primitive_types.end())
        {
            // Try simple type::method name
            std::string simple_method = type_name + "::" + method_name;
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "resolve_method_by_name: Trying primitive method '{}'", simple_method);

            if (llvm::Function *fn = module()->getFunction(simple_method))
            {
                return fn;
            }

            if (llvm::Function *fn = ctx().get_function(simple_method))
            {
                return fn;
            }

            // Try with std::core::primitives:: prefix
            std::string stdlib_method = "std::core::primitives::" + simple_method;
            if (llvm::Function *fn = module()->getFunction(stdlib_method))
            {
                return fn;
            }

            if (llvm::Function *fn = ctx().get_function(stdlib_method))
            {
                return fn;
            }

            // Method not found - return nullptr, let caller report error
            // Do NOT create extern declarations with guessed return types
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "resolve_method_by_name: Primitive method '{}' not found", simple_method);
        }

        // Pattern-based method lookup: scan module for functions matching *::MangledType::method
        // This handles cross-module cases where we don't have the namespace info
        {
            // Convert display name to mangled name for pattern matching
            // e.g., "Array<u64>" -> "Array_u64"
            std::string mangled_type = mangle_generic_type_name(type_name);

            // Strip namespace prefix from mangled_type if it's already qualified
            // This prevents double-namespacing like "ns::ns::Type::method"
            {
                size_t last_sep = mangled_type.rfind("::");
                if (last_sep != std::string::npos)
                {
                    mangled_type = mangled_type.substr(last_sep + 2);
                }
            }

            // Build pattern suffix using mangled type name: "::MangledType::method"
            // e.g., "::Array_u64::push" instead of "::Array::push"
            std::string pattern_suffix = "::" + mangled_type + "::" + method_name;

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

            // Direct lookup with simple type name: "SimpleType::method"
            // Handles namespace-qualified calls (e.g., "Foo::Bar::print" → try "Bar::print")
            std::string simple_method = mangled_type + "::" + method_name;
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "resolve_method_by_name: Trying simple name lookup '{}'", simple_method);
            if (llvm::Function *fn = module()->getFunction(simple_method))
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "resolve_method_by_name: Found '{}.{}' via simple name as '{}'",
                          type_name, method_name, simple_method);
                return fn;
            }
            if (llvm::Function *fn = ctx().get_function(simple_method))
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "resolve_method_by_name: Found '{}.{}' in registry via simple name as '{}'",
                          type_name, method_name, simple_method);
                return fn;
            }
        }

        // Create extern declarations for cross-module method calls
        // Uses TemplateRegistry to dynamically look up the type's defining namespace
        {
            std::string base_type = type_name;
            std::string type_args_str; // Store type arguments for generic substitution
            size_t angle_pos = type_name.find('<');
            if (angle_pos != std::string::npos)
            {
                base_type = type_name.substr(0, angle_pos);
                // Extract type arguments string (e.g., "String" from "Array<String>")
                if (type_name.back() == '>')
                {
                    type_args_str = type_name.substr(angle_pos + 1, type_name.length() - angle_pos - 2);
                }
            }

            // Convert display name to mangled name for method lookup
            // e.g., "Array<u64>" -> "Array_u64"
            std::string mangled_type = mangle_generic_type_name(type_name);

            // Strip namespace prefix from mangled_type if it's already qualified
            // This prevents double-namespacing like "ns::ns::Type::method"
            {
                size_t last_sep = mangled_type.rfind("::");
                if (last_sep != std::string::npos)
                {
                    mangled_type = mangled_type.substr(last_sep + 2);
                }
            }

            // Try to get namespace from TemplateRegistry (dynamic lookup)
            std::string type_namespace;
            std::string simple_type_name = base_type; // The type name without namespace (for template lookup)
            Cryo::TemplateRegistry *template_registry = ctx().template_registry();

            // Handle mangled type names like "Option_i64" -> "Option"
            // Try to find a template that matches a prefix of the mangled name
            if (angle_pos == std::string::npos && template_registry)
            {
                size_t underscore_pos = type_name.find('_');
                while (underscore_pos != std::string::npos)
                {
                    std::string prefix = type_name.substr(0, underscore_pos);
                    const Cryo::TemplateRegistry::TemplateInfo *template_info = template_registry->find_template(prefix);
                    if (template_info)
                    {
                        base_type = prefix;
                        simple_type_name = prefix;
                        // Extract type args from mangled name (everything after the base type)
                        type_args_str = type_name.substr(underscore_pos + 1);
                        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                  "resolve_method_by_name: Extracted base type '{}' from mangled name '{}' (type_args: '{}')",
                                  base_type, type_name, type_args_str);
                        break;
                    }
                    underscore_pos = type_name.find('_', underscore_pos + 1);
                }
            }

            // First check if the type name is already fully qualified (contains ::)
            // If so, extract namespace and simple name directly
            size_t last_sep = base_type.rfind("::");
            if (last_sep != std::string::npos)
            {
                type_namespace = base_type.substr(0, last_sep);
                simple_type_name = base_type.substr(last_sep + 2);
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "resolve_method_by_name: Extracted namespace '{}' and simple name '{}' from qualified type '{}'",
                          type_namespace, simple_type_name, base_type);
            }

            // If not already qualified, try to get namespace from TemplateRegistry
            if (type_namespace.empty() && template_registry)
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

            // Also check struct field types for source_namespace (for non-generic imported structs)
            if (type_namespace.empty() && template_registry)
            {
                const Cryo::TemplateRegistry::StructFieldInfo *field_info = template_registry->get_struct_field_types(base_type);
                if (field_info && !field_info->source_namespace.empty())
                {
                    type_namespace = field_info->source_namespace;
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "resolve_method_by_name: Found struct '{}' source_namespace '{}'",
                              base_type, type_namespace);
                }
            }

            // Also check the type namespace map (for locally defined types)
            if (type_namespace.empty())
            {
                type_namespace = ctx().get_type_namespace(base_type);
            }

            // Also try with mangled type name for namespace lookup
            if (type_namespace.empty())
            {
                type_namespace = ctx().get_type_namespace(mangled_type);
            }

            // Try to extract namespace from method annotations registry
            // For non-generic types (PathBuf, Reader, etc.), the ModuleLoader registers
            // annotations with fully-qualified names like "std::fs::path::PathBuf::method"
            if (type_namespace.empty() && template_registry)
            {
                type_namespace = template_registry->find_type_namespace_from_methods(base_type, method_name);
                if (!type_namespace.empty())
                {
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "resolve_method_by_name: Found namespace '{}' for type '{}' via method annotations",
                              type_namespace, base_type);
                }
            }

            // Fallback for primitive types: their methods are in std::core::primitives
            if (type_namespace.empty() && primitive_types.find(base_type) != primitive_types.end())
            {
                type_namespace = "std::core::primitives";
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "resolve_method_by_name: Using primitive namespace for type '{}'", base_type);
            }

            if (!type_namespace.empty())
            {
                // Use MANGLED type name (not simple_type_name) when building the full method name
                // e.g., "std::collections::array::Array_u64::push" instead of "std::collections::array::Array::push"
                std::string full_method_name = type_namespace + "::" + mangled_type + "::" + method_name;
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
                bool is_static_method = false;
                bool found_is_static = false;

                // First, try to look up is_static from the shared registry (works for all struct methods)
                if (template_registry)
                {
                    auto [is_static, found] = template_registry->get_method_is_static(full_method_name);
                    if (found)
                    {
                        is_static_method = is_static;
                        found_is_static = true;
                        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                  "resolve_method_by_name: Got is_static={} from registry for '{}'",
                                  is_static_method, full_method_name);
                    }

                    // Fallback: try with template name if instantiated name not found
                    // e.g., try "Option::is_some" if "Option_i64::is_some" not found
                    if (!found && simple_type_name != mangled_type)
                    {
                        std::string template_method_name = type_namespace + "::" + simple_type_name + "::" + method_name;
                        auto [is_static_tmpl, found_tmpl] = template_registry->get_method_is_static(template_method_name);
                        if (found_tmpl)
                        {
                            is_static_method = is_static_tmpl;
                            found_is_static = true;
                            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                      "resolve_method_by_name: Got is_static={} from template registry for '{}'",
                                      is_static_method, template_method_name);
                        }
                    }
                }

                // Then, look up the method in the template to get its signature (for generic types)
                // Use simple_type_name for template lookup (templates are stored by simple name)
                if (template_registry)
                {
                    const Cryo::TemplateRegistry::TemplateInfo *template_info = template_registry->find_template(simple_type_name);
                    if (template_info && template_info->struct_template)
                    {
                        for (const auto &method : template_info->struct_template->methods())
                        {
                            if (method->name() == method_name)
                            {
                                // Check if method is static (only if not already found from registry)
                                if (!found_is_static)
                                {
                                    is_static_method = method->is_static();
                                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                              "resolve_method_by_name: Found struct method '{}::{}' is_static={} (from template)",
                                              simple_type_name, method_name, is_static_method);
                                }

                                TypeRef method_return = method->get_resolved_return_type();
                                if (method_return)
                                {
                                    // Check if return type contains generic parameters that need substitution
                                    // This happens when the template method returns something like Option<T>
                                    if (!type_args_str.empty() && method_return->display_name().find("<error: unresolved generic") != std::string::npos)
                                    {
                                        // Parse type arguments and substitute
                                        std::vector<TypeRef> type_args;
                                        std::vector<std::string> arg_names;

                                        // Parse comma-separated type arguments, handling nested generics
                                        size_t start = 0;
                                        int depth = 0;
                                        for (size_t i = 0; i <= type_args_str.length(); ++i)
                                        {
                                            if (i == type_args_str.length() || (type_args_str[i] == ',' && depth == 0))
                                            {
                                                std::string arg = type_args_str.substr(start, i - start);
                                                // Trim whitespace
                                                while (!arg.empty() && std::isspace(static_cast<unsigned char>(arg.front())))
                                                    arg.erase(0, 1);
                                                while (!arg.empty() && std::isspace(static_cast<unsigned char>(arg.back())))
                                                    arg.pop_back();
                                                if (!arg.empty())
                                                    arg_names.push_back(arg);
                                                start = i + 1;
                                            }
                                            else if (type_args_str[i] == '<')
                                                depth++;
                                            else if (type_args_str[i] == '>')
                                                depth--;
                                        }

                                        // Resolve each type argument to a TypeRef
                                        bool all_resolved = true;
                                        for (const auto &arg_name : arg_names)
                                        {
                                            TypeRef arg_type = types().arena().lookup_type_by_name(arg_name);
                                            if (!arg_type.is_valid())
                                            {
                                                // Try with std:: prefix or other common namespaces
                                                arg_type = types().arena().lookup_type_by_name("std::collections::string::" + arg_name);
                                            }
                                            if (!arg_type.is_valid())
                                            {
                                                // Try primitives and common types (case-insensitive for String)
                                                if (arg_name == "i32" || arg_name == "int") arg_type = types().arena().get_i32();
                                                else if (arg_name == "i64") arg_type = types().arena().get_i64();
                                                else if (arg_name == "u32") arg_type = types().arena().get_u32();
                                                else if (arg_name == "u64") arg_type = types().arena().get_u64();
                                                else if (arg_name == "string" || arg_name == "String") arg_type = types().arena().get_string();
                                                else if (arg_name == "bool" || arg_name == "boolean") arg_type = types().arena().get_bool();
                                            }
                                            if (arg_type.is_valid())
                                            {
                                                type_args.push_back(arg_type);
                                                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                                          "resolve_method_by_name: Resolved type argument '{}' successfully", arg_name);
                                            }
                                            else
                                            {
                                                // Even if we can't resolve the type, we know we have a generic that needs substitution
                                                // Don't fail resolution - we'll use pointer type as fallback
                                                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                                          "resolve_method_by_name: Could not resolve type argument '{}', will use ptr fallback", arg_name);
                                            }
                                        }

                                        // Use annotation-based substitution when we have type arguments
                                        if (!arg_names.empty())
                                        {
                                            // Get the method's return type annotation string (e.g., "Option<T>")
                                            std::string return_annotation;
                                            if (method->return_type_annotation())
                                            {
                                                return_annotation = method->return_type_annotation()->to_string();
                                            }

                                            // Get the template's generic parameter names
                                            std::vector<std::string> generic_param_names;
                                            if (template_info && !template_info->metadata.generic_parameter_names.empty())
                                            {
                                                generic_param_names = template_info->metadata.generic_parameter_names;
                                            }

                                            // Perform substitution if we have both param names and annotation
                                            if (!return_annotation.empty() && !generic_param_names.empty() &&
                                                generic_param_names.size() == arg_names.size())
                                            {
                                                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                                          "resolve_method_by_name: Substituting generic params in annotation '{}'",
                                                          return_annotation);

                                                for (size_t i = 0; i < generic_param_names.size(); ++i)
                                                {
                                                    const std::string &param = generic_param_names[i];
                                                    const std::string &value = arg_names[i];

                                                    // Replace standalone occurrences of the param with the value
                                                    std::string result;
                                                    size_t pos = 0;
                                                    while (pos < return_annotation.length())
                                                    {
                                                        size_t found = return_annotation.find(param, pos);
                                                        if (found == std::string::npos)
                                                        {
                                                            result += return_annotation.substr(pos);
                                                            break;
                                                        }

                                                        bool is_start = (found == 0 ||
                                                                         (!std::isalnum(static_cast<unsigned char>(return_annotation[found - 1])) &&
                                                                          return_annotation[found - 1] != '_'));
                                                        bool is_end = (found + param.length() >= return_annotation.length() ||
                                                                       (!std::isalnum(static_cast<unsigned char>(return_annotation[found + param.length()])) &&
                                                                        return_annotation[found + param.length()] != '_'));

                                                        if (is_start && is_end)
                                                        {
                                                            result += return_annotation.substr(pos, found - pos);
                                                            result += value;
                                                            pos = found + param.length();
                                                        }
                                                        else
                                                        {
                                                            result += return_annotation.substr(pos, found - pos + param.length());
                                                            pos = found + param.length();
                                                        }
                                                    }
                                                    return_annotation = result;
                                                }

                                                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                                          "resolve_method_by_name: Substituted annotation: '{}'", return_annotation);

                                                // Now resolve the substituted annotation to LLVM type
                                                return_type = types().resolve_and_map(return_annotation);
                                                if (return_type)
                                                {
                                                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                                              "resolve_method_by_name: Resolved substituted annotation to LLVM type");
                                                }
                                            }

                                            // Fallback to pointer if substitution didn't work
                                            if (!return_type)
                                            {
                                                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                                          "resolve_method_by_name: Substitution failed, using ptr fallback");
                                                return_type = llvm::PointerType::get(llvm_ctx(), 0);
                                            }
                                        }
                                        else
                                        {
                                            return_type = types().get_type(method_return);
                                        }
                                    }
                                    else
                                    {
                                        return_type = types().get_type(method_return);
                                    }
                                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                              "resolve_method_by_name: Got return type from struct template method: {}",
                                              method_return->display_name());
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
                                // Check if method is static (only if not already found from registry)
                                if (!found_is_static)
                                {
                                    is_static_method = method->is_static();
                                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                              "resolve_method_by_name: Found class method '{}::{}' is_static={} (from template)",
                                              simple_type_name, method_name, is_static_method);
                                }

                                TypeRef method_return = method->get_resolved_return_type();
                                if (method_return)
                                {
                                    // Check if return type contains generic parameters that need substitution
                                    if (!type_args_str.empty() && method_return->display_name().find("<error: unresolved generic") != std::string::npos)
                                    {
                                        // Parse type arguments and substitute (same logic as struct template)
                                        std::vector<TypeRef> type_args;
                                        std::vector<std::string> arg_names;

                                        size_t start = 0;
                                        int depth = 0;
                                        for (size_t i = 0; i <= type_args_str.length(); ++i)
                                        {
                                            if (i == type_args_str.length() || (type_args_str[i] == ',' && depth == 0))
                                            {
                                                std::string arg = type_args_str.substr(start, i - start);
                                                while (!arg.empty() && std::isspace(static_cast<unsigned char>(arg.front())))
                                                    arg.erase(0, 1);
                                                while (!arg.empty() && std::isspace(static_cast<unsigned char>(arg.back())))
                                                    arg.pop_back();
                                                if (!arg.empty())
                                                    arg_names.push_back(arg);
                                                start = i + 1;
                                            }
                                            else if (type_args_str[i] == '<')
                                                depth++;
                                            else if (type_args_str[i] == '>')
                                                depth--;
                                        }

                                        bool all_resolved = true;
                                        for (const auto &arg_name : arg_names)
                                        {
                                            TypeRef arg_type = types().arena().lookup_type_by_name(arg_name);
                                            if (!arg_type.is_valid())
                                                arg_type = types().arena().lookup_type_by_name("std::collections::string::" + arg_name);
                                            if (!arg_type.is_valid())
                                            {
                                                // Try primitives and common types (case-insensitive for String)
                                                if (arg_name == "i32" || arg_name == "int") arg_type = types().arena().get_i32();
                                                else if (arg_name == "i64") arg_type = types().arena().get_i64();
                                                else if (arg_name == "u32") arg_type = types().arena().get_u32();
                                                else if (arg_name == "u64") arg_type = types().arena().get_u64();
                                                else if (arg_name == "string" || arg_name == "String") arg_type = types().arena().get_string();
                                                else if (arg_name == "bool" || arg_name == "boolean") arg_type = types().arena().get_bool();
                                            }
                                            if (arg_type.is_valid())
                                            {
                                                type_args.push_back(arg_type);
                                                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                                          "resolve_method_by_name: Resolved type argument '{}' successfully", arg_name);
                                            }
                                            else
                                            {
                                                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                                          "resolve_method_by_name: Could not resolve type argument '{}', will use ptr fallback", arg_name);
                                            }
                                        }

                                        // Use annotation-based substitution when we have type arguments
                                        if (!arg_names.empty())
                                        {
                                            // Get the method's return type annotation string (e.g., "Option<T>")
                                            std::string return_annotation;
                                            if (method->return_type_annotation())
                                            {
                                                return_annotation = method->return_type_annotation()->to_string();
                                            }

                                            // Get the template's generic parameter names
                                            std::vector<std::string> generic_param_names;
                                            if (template_info && !template_info->metadata.generic_parameter_names.empty())
                                            {
                                                generic_param_names = template_info->metadata.generic_parameter_names;
                                            }

                                            // Perform substitution if we have both param names and annotation
                                            if (!return_annotation.empty() && !generic_param_names.empty() &&
                                                generic_param_names.size() == arg_names.size())
                                            {
                                                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                                          "resolve_method_by_name: Substituting generic params in class method annotation '{}'",
                                                          return_annotation);

                                                for (size_t i = 0; i < generic_param_names.size(); ++i)
                                                {
                                                    const std::string &param = generic_param_names[i];
                                                    const std::string &value = arg_names[i];

                                                    std::string result;
                                                    size_t pos = 0;
                                                    while (pos < return_annotation.length())
                                                    {
                                                        size_t found = return_annotation.find(param, pos);
                                                        if (found == std::string::npos)
                                                        {
                                                            result += return_annotation.substr(pos);
                                                            break;
                                                        }

                                                        bool is_start = (found == 0 ||
                                                                         (!std::isalnum(static_cast<unsigned char>(return_annotation[found - 1])) &&
                                                                          return_annotation[found - 1] != '_'));
                                                        bool is_end = (found + param.length() >= return_annotation.length() ||
                                                                       (!std::isalnum(static_cast<unsigned char>(return_annotation[found + param.length()])) &&
                                                                        return_annotation[found + param.length()] != '_'));

                                                        if (is_start && is_end)
                                                        {
                                                            result += return_annotation.substr(pos, found - pos);
                                                            result += value;
                                                            pos = found + param.length();
                                                        }
                                                        else
                                                        {
                                                            result += return_annotation.substr(pos, found - pos + param.length());
                                                            pos = found + param.length();
                                                        }
                                                    }
                                                    return_annotation = result;
                                                }

                                                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                                          "resolve_method_by_name: Substituted class method annotation: '{}'", return_annotation);

                                                return_type = types().resolve_and_map(return_annotation);
                                                if (return_type)
                                                {
                                                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                                              "resolve_method_by_name: Resolved substituted class method annotation to LLVM type");
                                                }
                                            }

                                            if (!return_type)
                                            {
                                                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                                          "resolve_method_by_name: Class method substitution failed, using ptr fallback");
                                                return_type = llvm::PointerType::get(llvm_ctx(), 0);
                                            }
                                        }
                                        else
                                        {
                                            return_type = types().get_type(method_return);
                                        }
                                    }
                                    else
                                    {
                                        return_type = types().get_type(method_return);
                                    }
                                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                              "resolve_method_by_name: Got return type from class template method: {}",
                                              method_return->display_name());
                                }
                                break;
                            }
                        }
                    }
                    // Note: EnumDeclarationNode doesn't have methods() - enum methods are in impl blocks
                    // which are stored separately. Try TemplateMethodInfo for enum methods
                    // (populated by register_enum_impl_block).
                    if (!return_type && template_registry)
                    {
                        const auto *method_meta = template_registry->find_template_method(simple_type_name, method_name);
                        if (method_meta)
                        {
                            // Get is_static from metadata
                            if (!found_is_static)
                            {
                                is_static_method = method_meta->is_static;
                                found_is_static = true;
                                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                          "resolve_method_by_name: Got is_static={} from enum TemplateMethodInfo for '{}::{}'",
                                          is_static_method, simple_type_name, method_name);
                            }

                            // Get return type from annotation with generic substitution
                            std::string annotation = method_meta->return_type_annotation;
                            if (!annotation.empty() && annotation != "void")
                            {
                                // Substitute generic params if we have type args
                                if (!type_args_str.empty())
                                {
                                    const Cryo::TemplateRegistry::TemplateInfo *tmpl_info = template_registry->find_template(simple_type_name);
                                    std::vector<std::string> generic_param_names;
                                    if (tmpl_info && !tmpl_info->metadata.generic_parameter_names.empty())
                                    {
                                        generic_param_names = tmpl_info->metadata.generic_parameter_names;
                                    }

                                    // Parse type arguments from type_args_str
                                    std::vector<std::string> type_arg_values;
                                    {
                                        size_t start = 0;
                                        int depth = 0;
                                        for (size_t i = 0; i <= type_args_str.length(); ++i)
                                        {
                                            if (i == type_args_str.length() || (type_args_str[i] == ',' && depth == 0))
                                            {
                                                std::string arg = type_args_str.substr(start, i - start);
                                                while (!arg.empty() && std::isspace(static_cast<unsigned char>(arg.front())))
                                                    arg.erase(0, 1);
                                                while (!arg.empty() && std::isspace(static_cast<unsigned char>(arg.back())))
                                                    arg.pop_back();
                                                if (!arg.empty())
                                                    type_arg_values.push_back(arg);
                                                start = i + 1;
                                            }
                                            else if (type_args_str[i] == '<')
                                                depth++;
                                            else if (type_args_str[i] == '>')
                                                depth--;
                                        }
                                    }

                                    // Perform substitution: replace generic params with concrete types
                                    if (!generic_param_names.empty() && generic_param_names.size() == type_arg_values.size())
                                    {
                                        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                                  "resolve_method_by_name: Substituting {} generic params in enum method annotation '{}'",
                                                  generic_param_names.size(), annotation);

                                        for (size_t i = 0; i < generic_param_names.size(); ++i)
                                        {
                                            const std::string &param = generic_param_names[i];
                                            const std::string &value = type_arg_values[i];

                                            std::string result;
                                            size_t pos = 0;
                                            while (pos < annotation.length())
                                            {
                                                size_t found = annotation.find(param, pos);
                                                if (found == std::string::npos)
                                                {
                                                    result += annotation.substr(pos);
                                                    break;
                                                }

                                                bool is_start = (found == 0 ||
                                                                 (!std::isalnum(static_cast<unsigned char>(annotation[found - 1])) &&
                                                                  annotation[found - 1] != '_'));
                                                bool is_end = (found + param.length() >= annotation.length() ||
                                                               (!std::isalnum(static_cast<unsigned char>(annotation[found + param.length()])) &&
                                                                annotation[found + param.length()] != '_'));

                                                if (is_start && is_end)
                                                {
                                                    result += annotation.substr(pos, found - pos);
                                                    result += value;
                                                    pos = found + param.length();
                                                }
                                                else
                                                {
                                                    result += annotation.substr(pos, found - pos + param.length());
                                                    pos = found + param.length();
                                                }
                                            }
                                            annotation = result;
                                        }

                                        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                                  "resolve_method_by_name: Substituted enum method annotation: '{}'", annotation);
                                    }
                                }

                                return_type = types().resolve_and_map(annotation);
                                if (return_type)
                                {
                                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                              "resolve_method_by_name: Got return type from enum TemplateMethodInfo for '{}::{}': {}",
                                              simple_type_name, method_name, annotation);
                                }
                            }
                            else if (annotation == "void")
                            {
                                return_type = llvm::Type::getVoidTy(llvm_ctx());
                                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                          "resolve_method_by_name: Enum method '{}::{}' returns void",
                                          simple_type_name, method_name);
                            }
                        }
                    }
                }

                // Only add 'this' parameter for non-static methods
                if (!is_static_method)
                {
                    param_types.push_back(llvm::PointerType::get(llvm_ctx(), 0));
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "resolve_method_by_name: Added 'this' parameter for instance method '{}'", full_method_name);
                }
                else
                {
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "resolve_method_by_name: Skipping 'this' parameter for static method '{}'", full_method_name);
                }

                // Try to get return type from method registry (populated from impl blocks)
                // First check local CodegenContext, then shared TemplateRegistry
                if (!return_type)
                {
                    TypeRef cryo_return_type = ctx().get_method_return_type(full_method_name);
                    if (!cryo_return_type && template_registry)
                    {
                        // Try TemplateRegistry for cross-module method signatures
                        cryo_return_type = template_registry->get_method_return_type(full_method_name);
                    }

                    // Fallback: try with template name if instantiated name not found
                    // e.g., try "Option::is_some" if "Option_i64::is_some" not found
                    if (!cryo_return_type && template_registry && simple_type_name != mangled_type)
                    {
                        std::string template_method_name = type_namespace + "::" + simple_type_name + "::" + method_name;
                        cryo_return_type = template_registry->get_method_return_type(template_method_name);
                        if (cryo_return_type)
                        {
                            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                      "resolve_method_by_name: Got return type from template method registry for '{}': {}",
                                      template_method_name, cryo_return_type->display_name());
                        }
                    }

                    if (cryo_return_type)
                    {
                        return_type = types().get_type(cryo_return_type);
                        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                  "resolve_method_by_name: Got return type from method registry for '{}': {}",
                                  full_method_name, cryo_return_type->display_name());
                    }
                }

                // Try string annotation fallback for complex types (Option<T>, String, etc.)
                if (!return_type && template_registry)
                {
                    std::string return_type_annotation = template_registry->get_method_return_type_annotation(full_method_name);
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "resolve_method_by_name: Annotation lookup for '{}': '{}'",
                              full_method_name, return_type_annotation.empty() ? "<not found>" : return_type_annotation);

                    // If not found with instantiated name (e.g., Option_i64::is_some),
                    // try with template name (e.g., Option::is_some) since annotations
                    // are registered with the base template name
                    if (return_type_annotation.empty() && simple_type_name != mangled_type)
                    {
                        std::string template_method_name = type_namespace + "::" + simple_type_name + "::" + method_name;
                        return_type_annotation = template_registry->get_method_return_type_annotation(template_method_name);
                        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                  "resolve_method_by_name: Fallback annotation lookup for '{}': '{}'",
                                  template_method_name, return_type_annotation.empty() ? "<not found>" : return_type_annotation);
                    }
                    if (!return_type_annotation.empty() && return_type_annotation != "void")
                    {
                        // Substitute generic parameters in the annotation if we have type arguments
                        // e.g., "Option<T>" with type_args_str="String" becomes "Option<String>"
                        std::string resolved_annotation = return_type_annotation;
                        if (!type_args_str.empty())
                        {
                            // Get the template's generic parameter names
                            const Cryo::TemplateRegistry::TemplateInfo *tmpl_info = template_registry->find_template(simple_type_name);
                            std::vector<std::string> generic_param_names;
                            if (tmpl_info && !tmpl_info->metadata.generic_parameter_names.empty())
                            {
                                generic_param_names = tmpl_info->metadata.generic_parameter_names;
                            }

                            // Parse type arguments from type_args_str (handles nested generics)
                            std::vector<std::string> type_arg_values;
                            {
                                size_t start = 0;
                                int depth = 0;
                                for (size_t i = 0; i <= type_args_str.length(); ++i)
                                {
                                    if (i == type_args_str.length() || (type_args_str[i] == ',' && depth == 0))
                                    {
                                        std::string arg = type_args_str.substr(start, i - start);
                                        // Trim whitespace
                                        while (!arg.empty() && std::isspace(static_cast<unsigned char>(arg.front())))
                                            arg.erase(0, 1);
                                        while (!arg.empty() && std::isspace(static_cast<unsigned char>(arg.back())))
                                            arg.pop_back();
                                        if (!arg.empty())
                                            type_arg_values.push_back(arg);
                                        start = i + 1;
                                    }
                                    else if (type_args_str[i] == '<')
                                        depth++;
                                    else if (type_args_str[i] == '>')
                                        depth--;
                                }
                            }

                            // Perform substitution: replace generic params with concrete types
                            if (!generic_param_names.empty() && generic_param_names.size() == type_arg_values.size())
                            {
                                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                          "resolve_method_by_name: Substituting {} generic params in annotation '{}'",
                                          generic_param_names.size(), return_type_annotation);

                                for (size_t i = 0; i < generic_param_names.size(); ++i)
                                {
                                    const std::string &param = generic_param_names[i];
                                    const std::string &value = type_arg_values[i];

                                    // Replace all occurrences of the generic param with the concrete type
                                    // Be careful to only replace standalone type params, not substrings
                                    // e.g., replace "T" but not "T" in "String"
                                    std::string result;
                                    size_t pos = 0;
                                    while (pos < resolved_annotation.length())
                                    {
                                        size_t found = resolved_annotation.find(param, pos);
                                        if (found == std::string::npos)
                                        {
                                            result += resolved_annotation.substr(pos);
                                            break;
                                        }

                                        // Check if this is a standalone type parameter
                                        // (not part of a larger identifier)
                                        bool is_start = (found == 0 ||
                                                         !std::isalnum(static_cast<unsigned char>(resolved_annotation[found - 1])) &&
                                                             resolved_annotation[found - 1] != '_');
                                        bool is_end = (found + param.length() >= resolved_annotation.length() ||
                                                       !std::isalnum(static_cast<unsigned char>(resolved_annotation[found + param.length()])) &&
                                                           resolved_annotation[found + param.length()] != '_');

                                        if (is_start && is_end)
                                        {
                                            result += resolved_annotation.substr(pos, found - pos);
                                            result += value;
                                            pos = found + param.length();
                                        }
                                        else
                                        {
                                            result += resolved_annotation.substr(pos, found - pos + param.length());
                                            pos = found + param.length();
                                        }
                                    }
                                    resolved_annotation = result;
                                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                              "resolve_method_by_name: After substituting {}={}: '{}'",
                                              param, value, resolved_annotation);
                                }
                            }
                        }

                        // Use TypeMapper to resolve the type annotation to LLVM type
                        return_type = types().resolve_and_map(resolved_annotation);
                        if (return_type)
                        {
                            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                      "resolve_method_by_name: Got return type from annotation for '{}': {} -> LLVM type",
                                      full_method_name, resolved_annotation);
                        }
                        else
                        {
                            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                      "resolve_method_by_name: Failed to resolve annotation '{}' for '{}'",
                                      resolved_annotation, full_method_name);
                        }
                    }
                }

                // If return type couldn't be determined, do NOT create an extern declaration
                // with a guessed void type. Instead, return nullptr so that the caller
                // can report a proper "method not found" error.
                if (!return_type)
                {
                    LOG_ERROR(Cryo::LogComponent::CODEGEN,
                              "resolve_method_by_name: Cannot determine return type for '{}'. "
                              "Method signature not found in template registry. "
                              "This likely means the method does not exist on this type.",
                              full_method_name);
                    // Return nullptr - do NOT create a declaration with guessed void return type
                    // This allows proper error reporting upstream
                    return nullptr;
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

        // If we found a declaration (but no definition) during SRM lookup, return it as fallback
        if (declaration_fallback)
        {
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "resolve_method_by_name: Returning declaration fallback for '{}.{}'",
                      type_name, method_name);
            return declaration_fallback;
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
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "create_entry_alloca: fn={}, type={}, name='{}'",
                  fn ? "non-null" : "null", type ? "non-null" : "null", name);

        if (!fn || !type)
            return nullptr;

        // Debug type info and validate struct types
        if (auto *st = llvm::dyn_cast<llvm::StructType>(type))
        {
            if (st->isOpaque())
            {
                LOG_ERROR(Cryo::LogComponent::CODEGEN,
                         "create_entry_alloca: REFUSING to alloca opaque struct type '{}' - this would crash LLVM",
                         st->hasName() ? st->getName().str() : "<unnamed>");
                return nullptr;
            }

            unsigned numElements = st->getNumElements();
            LOG_DEBUG(Cryo::LogComponent::CODEGEN, "create_entry_alloca: Type is struct '{}', elements={}",
                      st->hasName() ? st->getName().str() : "<unnamed>", numElements);

            // Validate each element type to catch corruption early
            for (unsigned i = 0; i < numElements; ++i)
            {
                llvm::Type *elemType = st->getElementType(i);
                if (!elemType)
                {
                    LOG_ERROR(Cryo::LogComponent::CODEGEN,
                             "create_entry_alloca: Struct '{}' has null element at index {}",
                             st->hasName() ? st->getName().str() : "<unnamed>", i);
                    return nullptr;
                }
                // Check for nested opaque structs
                if (auto *nestedSt = llvm::dyn_cast<llvm::StructType>(elemType))
                {
                    if (nestedSt->isOpaque())
                    {
                        LOG_ERROR(Cryo::LogComponent::CODEGEN,
                                 "create_entry_alloca: Struct '{}' has opaque nested struct at index {}",
                                 st->hasName() ? st->getName().str() : "<unnamed>", i);
                        return nullptr;
                    }
                }
                LOG_DEBUG(Cryo::LogComponent::CODEGEN, "create_entry_alloca: Element {} type ID: {}",
                          i, elemType->getTypeID());
            }
        }
        else
        {
            LOG_DEBUG(Cryo::LogComponent::CODEGEN, "create_entry_alloca: Type is not a struct, type ID: {}",
                      type->getTypeID());
        }

        // Save current insertion point
        llvm::IRBuilder<> &b = builder();
        llvm::BasicBlock *current_block = b.GetInsertBlock();
        llvm::BasicBlock::iterator current_point = b.GetInsertPoint();

        // Get the entry block
        llvm::BasicBlock &entry = fn->getEntryBlock();
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "create_entry_alloca: Got entry block, empty={}",
                  entry.empty() ? "yes" : "no");

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

        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "create_entry_alloca: About to call CreateAlloca");
        // Create the alloca
        llvm::AllocaInst *alloca = b.CreateAlloca(type, nullptr, name);
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "create_entry_alloca: CreateAlloca returned successfully");

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
            report_error(ErrorCode::E0900_INTERNAL_COMPILER_ERROR,
                        "Internal error: create_store called with non-pointer destination");
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

    llvm::Type *ICodegenComponent::get_llvm_type(TypeRef cryo_type)
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
                // Extension - use sign-extend as the safe default because Cryo's default
                // integer type is signed (int/i32/i64). This correctly preserves negative
                // values (e.g., return -1 from i32 to i64). For positive values, sext and
                // zext produce identical results so this is safe for unsigned types too.
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

        // Pointer to struct: load the struct from the pointer
        // This handles cases where we have a pointer to a struct but need the struct by value
        if (source_type->isPointerTy() && target_type->isStructTy())
        {
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                     "cast_if_needed: Loading struct from pointer");
            return b.CreateLoad(target_type, value, "struct.load");
        }

        // Pointer to floating-point: load the value through the pointer.
        // Handles &this in primitive implement blocks (e.g., fabs(&this) where fabs expects f64).
        // If the pointer is an alloca that stores another pointer (double indirection),
        // load through both levels.
        if (source_type->isPointerTy() && target_type->isFloatingPointTy())
        {
            if (auto *alloca_inst = llvm::dyn_cast<llvm::AllocaInst>(value))
            {
                if (alloca_inst->getAllocatedType()->isPointerTy())
                {
                    // Double indirection: alloca stores a pointer → load ptr, then load value
                    llvm::Value *loaded_ptr = b.CreateLoad(
                        alloca_inst->getAllocatedType(), value, "ptr.deref");
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                             "cast_if_needed: Double deref ptr→ptr→float");
                    return b.CreateLoad(target_type, loaded_ptr, "float.deref");
                }
            }
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                     "cast_if_needed: Loading float from pointer");
            return b.CreateLoad(target_type, value, "float.load");
        }

        // Pointer (double-indirection) to integer: for &this in primitive implement blocks
        // on integer types. Only triggers when the alloca stores a pointer (double indirection).
        // Single-indirection ptr→int is handled below by PtrToInt.
        if (source_type->isPointerTy() && target_type->isIntegerTy())
        {
            if (auto *alloca_inst = llvm::dyn_cast<llvm::AllocaInst>(value))
            {
                if (alloca_inst->getAllocatedType()->isPointerTy())
                {
                    llvm::Value *loaded_ptr = b.CreateLoad(
                        alloca_inst->getAllocatedType(), value, "ptr.deref");
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                             "cast_if_needed: Double deref ptr→ptr→int");
                    return b.CreateLoad(target_type, loaded_ptr, "int.deref");
                }
            }
            // Fall through to PtrToInt for single-indirection cases
        }

        // Integer to struct (tagged union): wrap discriminant in tagged union struct
        // This handles cases where an enum variant is stored as a raw i32 discriminant
        // but the function return type expects the full tagged union struct { i32, [N x i8] }
        if (source_type->isIntegerTy() && target_type->isStructTy())
        {
            auto *struct_type = llvm::cast<llvm::StructType>(target_type);
            if (struct_type->getNumElements() > 0 &&
                struct_type->getElementType(0)->isIntegerTy())
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                         "cast_if_needed: Wrapping integer discriminant in tagged union struct");
                llvm::Value *alloca = b.CreateAlloca(struct_type, nullptr, "enum.cast.tmp");
                llvm::Value *disc_gep = b.CreateStructGEP(struct_type, alloca, 0, "disc.ptr");
                llvm::Value *cast_disc = value;
                llvm::Type *disc_field_type = struct_type->getElementType(0);
                if (source_type != disc_field_type)
                    cast_disc = b.CreateIntCast(value, disc_field_type, true, "disc.cast");
                b.CreateStore(cast_disc, disc_gep);
                if (struct_type->getNumElements() > 1)
                {
                    llvm::Value *payload_gep = b.CreateStructGEP(struct_type, alloca, 1, "payload.ptr");
                    b.CreateStore(llvm::Constant::getNullValue(struct_type->getElementType(1)), payload_gep);
                }
                return b.CreateLoad(struct_type, alloca, "enum.cast.val");
            }
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

    //===================================================================
    // Generic Type Name Mangling
    //===================================================================

    std::string ICodegenComponent::mangle_generic_type_name(const std::string &display_name)
    {
        // Check if this is a generic type with angle brackets (e.g., "Array<u64>")
        size_t angle_pos = display_name.find('<');
        if (angle_pos == std::string::npos)
        {
            // Not a generic type in display format, return as-is
            return display_name;
        }

        // Extract base name and type arguments
        std::string base_name = display_name.substr(0, angle_pos);

        // Find matching closing bracket
        size_t close_pos = display_name.rfind('>');
        if (close_pos == std::string::npos || close_pos <= angle_pos)
        {
            return display_name;
        }

        // Extract type arguments string (e.g., "u64" from "Array<u64>")
        std::string args_str = display_name.substr(angle_pos + 1, close_pos - angle_pos - 1);

        // Build mangled name: base_arg1_arg2_...
        std::string mangled = base_name + "_";

        // Parse type arguments (handle nested generics and multiple args)
        std::string current_arg;
        int depth = 0;
        for (size_t i = 0; i < args_str.size(); ++i)
        {
            char c = args_str[i];
            if (c == '<')
            {
                depth++;
                current_arg += '_';  // Replace < with _
            }
            else if (c == '>')
            {
                depth--;
                current_arg += '_';  // Replace > with _
            }
            else if (c == ',' && depth == 0)
            {
                // Argument separator at top level
                // Trim whitespace from current_arg
                size_t start = current_arg.find_first_not_of(" \t");
                size_t end = current_arg.find_last_not_of(" \t");
                if (start != std::string::npos)
                {
                    mangled += current_arg.substr(start, end - start + 1);
                }
                mangled += "_";
                current_arg.clear();
            }
            else if (c == ' ')
            {
                // Skip spaces, they become _ separators if at boundaries
                if (!current_arg.empty() && current_arg.back() != '_')
                {
                    current_arg += '_';
                }
            }
            else if (c == '*')
            {
                current_arg += 'p';  // Pointer marker
            }
            else
            {
                current_arg += c;
            }
        }

        // Add final argument
        if (!current_arg.empty())
        {
            size_t start = current_arg.find_first_not_of(" \t_");
            size_t end = current_arg.find_last_not_of(" \t_");
            if (start != std::string::npos)
            {
                mangled += current_arg.substr(start, end - start + 1);
            }
        }

        // Remove trailing underscores
        while (!mangled.empty() && mangled.back() == '_')
        {
            mangled.pop_back();
        }

        // Normalize user-facing type aliases to canonical names to match
        // what the Monomorphizer generates (e.g., Entry_int -> Entry_i32).
        // The Monomorphizer uses TypeRef::display_name() which returns canonical names.
        auto normalize_suffix = [](std::string &s, const std::string &from, const std::string &to)
        {
            size_t pos = 0;
            while ((pos = s.find(from, pos)) != std::string::npos)
            {
                // Only replace if it's a full segment (preceded by _ or start, followed by _ or end)
                bool at_start = (pos == 0 || s[pos - 1] == '_');
                bool at_end = (pos + from.size() == s.size() || s[pos + from.size()] == '_');
                if (at_start && at_end)
                {
                    s.replace(pos, from.size(), to);
                    pos += to.size();
                }
                else
                {
                    pos += from.size();
                }
            }
        };
        normalize_suffix(mangled, "int", "i32");
        normalize_suffix(mangled, "uint", "u32");
        normalize_suffix(mangled, "float", "f32");
        normalize_suffix(mangled, "double", "f64");
        normalize_suffix(mangled, "boolean", "bool");

        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                  "mangle_generic_type_name: '{}' -> '{}'", display_name, mangled);

        return mangled;
    }

} // namespace Cryo::Codegen
