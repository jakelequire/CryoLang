/******************************************************************************
 * @file TypeResolver.cpp
 * @brief Implementation of TypeResolver for Cryo's new type system
 ******************************************************************************/

#include "Types/TypeResolver.hpp"
#include "Types/PrimitiveTypes.hpp"
#include "Types/CompoundTypes.hpp"
#include "Types/UserDefinedTypes.hpp"
#include "Utils/Logger.hpp"

#include <sstream>
#include <cctype>

namespace Cryo
{
    // ========================================================================
    // Primitive type map
    // ========================================================================

    const std::unordered_map<std::string, std::function<TypeRef(TypeArena &)>>
        TypeResolver::_primitive_map = {
            // Void
            {"void", [](TypeArena &a) { return a.get_void(); }},

            // Boolean
            {"boolean", [](TypeArena &a) { return a.get_bool(); }},

            // Signed integers
            {"i8", [](TypeArena &a) { return a.get_i8(); }},
            {"i16", [](TypeArena &a) { return a.get_i16(); }},
            {"i32", [](TypeArena &a) { return a.get_i32(); }},
            {"i64", [](TypeArena &a) { return a.get_i64(); }},
            {"i128", [](TypeArena &a) { return a.get_i128(); }},
            {"int", [](TypeArena &a) { return a.get_i32(); }},

            // Unsigned integers
            {"u8", [](TypeArena &a) { return a.get_u8(); }},
            {"u16", [](TypeArena &a) { return a.get_u16(); }},
            {"u32", [](TypeArena &a) { return a.get_u32(); }},
            {"u64", [](TypeArena &a) { return a.get_u64(); }},
            {"u128", [](TypeArena &a) { return a.get_u128(); }},
            {"uint", [](TypeArena &a) { return a.get_u32(); }},

            // Floating point
            {"f32", [](TypeArena &a) { return a.get_f32(); }},
            {"f64", [](TypeArena &a) { return a.get_f64(); }},
            {"float", [](TypeArena &a) { return a.get_f32(); }},
            {"double", [](TypeArena &a) { return a.get_f64(); }},

            // Other primitives
            {"char", [](TypeArena &a) { return a.get_char(); }},
            {"string", [](TypeArena &a) { return a.get_string(); }},
            {"never", [](TypeArena &a) { return a.get_never(); }},
        };

    // ========================================================================
    // TypeResolver implementation
    // ========================================================================

    TypeResolver::TypeResolver(TypeArena &arena,
                               ModuleTypeRegistry &modules,
                               GenericRegistry &generics,
                               DiagEmitter *diag)
        : _arena(arena),
          _module_registry(modules),
          _generic_registry(generics),
          _diagnostics(diag)
    {
    }

    TypeRef TypeResolver::resolve(const TypeAnnotation &annotation, ResolutionContext &ctx)
    {
        ctx.current_location = annotation.location;

        switch (annotation.kind)
        {
        case TypeAnnotationKind::Primitive:
            return resolve_primitive(annotation.name);

        case TypeAnnotationKind::Named:
            return resolve_named(annotation.name, ctx);

        case TypeAnnotationKind::Qualified:
            return resolve_qualified(annotation.qualified_path, ctx);

        case TypeAnnotationKind::Pointer:
            return resolve_pointer(*annotation.inner, ctx);

        case TypeAnnotationKind::Reference:
            return resolve_reference(*annotation.inner, annotation.is_mutable, ctx);

        case TypeAnnotationKind::Array:
            return resolve_array(*annotation.inner, annotation.array_size, ctx);

        case TypeAnnotationKind::Function:
            return resolve_function(annotation.elements, *annotation.return_type,
                                    annotation.is_variadic, ctx);

        case TypeAnnotationKind::Tuple:
            return resolve_tuple(annotation.elements, ctx);

        case TypeAnnotationKind::Optional:
            return resolve_optional(*annotation.inner, ctx);

        case TypeAnnotationKind::Generic:
            return resolve_generic(*annotation.inner, annotation.elements, ctx);

        default:
            return make_error("unknown type annotation kind", annotation.location);
        }
    }

    std::vector<TypeRef> TypeResolver::resolve_all(const std::vector<TypeAnnotation> &annotations,
                                                    ResolutionContext &ctx)
    {
        std::vector<TypeRef> results;
        results.reserve(annotations.size());

        for (const auto &ann : annotations)
        {
            results.push_back(resolve(ann, ctx));
        }

        return results;
    }

    TypeRef TypeResolver::resolve_string(const std::string &type_str, ResolutionContext &ctx)
    {
        // Simple string-based resolution for backwards compatibility
        // First try as primitive
        TypeRef prim = resolve_primitive(type_str);
        if (!prim.is_error())
        {
            return prim;
        }

        // Then try as named type
        return resolve_named(type_str, ctx);
    }

    TypeRef TypeResolver::resolve_primitive(const std::string &name)
    {
        auto it = _primitive_map.find(name);
        if (it != _primitive_map.end())
        {
            return it->second(_arena);
        }

        // Not a primitive - return error
        return make_error("unknown primitive type '" + name + "'", SourceLocation{});
    }

    TypeRef TypeResolver::resolve_named(const std::string &name, ResolutionContext &ctx)
    {
        // First check if it's a primitive
        if (is_primitive_name(name))
        {
            return resolve_primitive(name);
        }

        // Check if the name is a function type (e.g., "(void*,u8*,u64)->IoResult<u64>")
        // Function types start with '(' and contain '->'
        if (!name.empty() && name[0] == '(' && name.find("->") != std::string::npos)
        {
            LOG_DEBUG(LogComponent::GENERAL, "TypeResolver: Detected function type syntax in named type '{}'", name);
            return resolve_function_type_string(name, ctx);
        }

        // Check if the name is a pointer type (ends with '*')
        if (!name.empty() && name.back() == '*')
        {
            LOG_DEBUG(LogComponent::GENERAL, "TypeResolver: Detected pointer type syntax in named type '{}'", name);
            return resolve_pointer_type_string(name, ctx);
        }

        // Check if the name is a reference type (starts with '&')
        if (!name.empty() && name[0] == '&')
        {
            LOG_DEBUG(LogComponent::GENERAL, "TypeResolver: Detected reference type syntax in named type '{}'", name);
            return resolve_reference_type_string(name, ctx);
        }

        // Check if the name contains generic syntax (e.g., "Option<Layout>")
        // This handles cases where the Parser stored the full type string as a Named annotation
        size_t angle_pos = name.find('<');
        if (angle_pos != std::string::npos && name.back() == '>')
        {
            LOG_DEBUG(LogComponent::GENERAL, "TypeResolver: Detected generic syntax in named type '{}'", name);

            // Parse the base type name
            std::string base_name = name.substr(0, angle_pos);

            // Parse the type arguments - handle nested generics
            std::string args_str = name.substr(angle_pos + 1, name.size() - angle_pos - 2);

            // Split arguments by comma, respecting nesting
            std::vector<std::string> arg_strings;
            int depth = 0;
            size_t start = 0;
            for (size_t i = 0; i < args_str.size(); ++i)
            {
                if (args_str[i] == '<')
                    depth++;
                else if (args_str[i] == '>')
                    depth--;
                else if (args_str[i] == ',' && depth == 0)
                {
                    arg_strings.push_back(args_str.substr(start, i - start));
                    start = i + 1;
                }
            }
            // Don't forget the last argument
            if (start < args_str.size())
            {
                arg_strings.push_back(args_str.substr(start));
            }

            LOG_DEBUG(LogComponent::GENERAL, "TypeResolver: Parsed generic '{}' with {} args",
                      base_name, arg_strings.size());

            // Create TypeAnnotations for the components and call resolve_generic
            TypeAnnotation base_ann = TypeAnnotation::named(base_name, ctx.current_location);
            std::vector<TypeAnnotation> arg_anns;
            for (const auto &arg_str : arg_strings)
            {
                // Trim whitespace from argument
                std::string trimmed = arg_str;
                while (!trimmed.empty() && std::isspace(trimmed.front()))
                    trimmed.erase(0, 1);
                while (!trimmed.empty() && std::isspace(trimmed.back()))
                    trimmed.pop_back();

                arg_anns.push_back(TypeAnnotation::named(trimmed, ctx.current_location));
            }

            return resolve_generic(base_ann, arg_anns, ctx);
        }

        // Check if it's a generic parameter in scope
        auto generic_binding = ctx.lookup_generic(name);
        if (generic_binding)
        {
            return *generic_binding;
        }

        // Check for cycles
        if (ctx.is_in_progress(name))
        {
            return make_error("cyclic type reference to '" + name + "'", ctx.current_location);
        }

        // Try to resolve via module registry with imports
        auto module_info = _module_registry.get_module_info(ctx.current_module);
        std::vector<ImportDecl> imports;
        if (module_info)
        {
            imports = module_info->imports;
        }
        // Add any additional imports from the context
        imports.insert(imports.end(), ctx.imports.begin(), ctx.imports.end());

        auto result = _module_registry.resolve_with_imports(name, ctx.current_module, imports);
        if (result)
        {
            return *result;
        }

        // Check if it's a generic template by name
        auto template_info = _generic_registry.get_template_by_name(name);
        if (template_info)
        {
            LOG_DEBUG(LogComponent::GENERAL, "TypeResolver: Found template '{}' in GenericRegistry", name);
            return template_info->generic_type;
        }

        // Type not found
        LOG_DEBUG(LogComponent::GENERAL, "TypeResolver: Could not resolve named type '{}'", name);
        return make_error("undefined type '" + name + "'", ctx.current_location);
    }

    TypeRef TypeResolver::resolve_qualified(const std::vector<std::string> &path,
                                             ResolutionContext &ctx)
    {
        if (path.empty())
        {
            return make_error("empty qualified type path", ctx.current_location);
        }

        auto result = _module_registry.resolve_qualified(path, ctx.current_module);
        if (result)
        {
            return *result;
        }

        // Build path string for error message
        std::ostringstream oss;
        for (size_t i = 0; i < path.size(); ++i)
        {
            if (i > 0)
                oss << "::";
            oss << path[i];
        }

        return make_error("undefined type '" + oss.str() + "'", ctx.current_location);
    }

    TypeRef TypeResolver::resolve_pointer(const TypeAnnotation &inner, ResolutionContext &ctx)
    {
        TypeRef pointee = resolve(inner, ctx);

        // Error propagation is handled by get_pointer_to
        return _arena.get_pointer_to(pointee);
    }

    TypeRef TypeResolver::resolve_reference(const TypeAnnotation &inner,
                                             bool is_mutable, ResolutionContext &ctx)
    {
        TypeRef referent = resolve(inner, ctx);

        RefMutability mut = is_mutable ? RefMutability::Mutable : RefMutability::Immutable;
        return _arena.get_reference_to(referent, mut);
    }

    TypeRef TypeResolver::resolve_array(const TypeAnnotation &element,
                                         std::optional<size_t> size, ResolutionContext &ctx)
    {
        TypeRef elem_type = resolve(element, ctx);

        return _arena.get_array_of(elem_type, size);
    }

    TypeRef TypeResolver::resolve_function(const std::vector<TypeAnnotation> &params,
                                            const TypeAnnotation &return_type,
                                            bool is_variadic, ResolutionContext &ctx)
    {
        // Resolve return type
        TypeRef ret = resolve(return_type, ctx);

        // Resolve parameter types
        std::vector<TypeRef> param_types;
        param_types.reserve(params.size());

        for (const auto &param : params)
        {
            param_types.push_back(resolve(param, ctx));
        }

        return _arena.get_function(ret, std::move(param_types), is_variadic);
    }

    TypeRef TypeResolver::resolve_tuple(const std::vector<TypeAnnotation> &elements,
                                         ResolutionContext &ctx)
    {
        std::vector<TypeRef> elem_types;
        elem_types.reserve(elements.size());

        for (const auto &elem : elements)
        {
            elem_types.push_back(resolve(elem, ctx));
        }

        return _arena.get_tuple(std::move(elem_types));
    }

    TypeRef TypeResolver::resolve_optional(const TypeAnnotation &inner, ResolutionContext &ctx)
    {
        TypeRef wrapped = resolve(inner, ctx);

        return _arena.get_optional_of(wrapped);
    }

    TypeRef TypeResolver::resolve_generic(const TypeAnnotation &base,
                                           const std::vector<TypeAnnotation> &args,
                                           ResolutionContext &ctx)
    {
        LOG_DEBUG(LogComponent::GENERAL, "TypeResolver: resolve_generic called with base '{}' and {} args",
            base.to_string(), args.size());

        // Resolve base type
        TypeRef base_type = resolve(base, ctx);
        if (base_type.is_error())
        {
            LOG_DEBUG(LogComponent::GENERAL, "TypeResolver: resolve_generic failed to resolve base type '{}'",
                base.to_string());
            return base_type;
        }

        // Resolve type arguments
        std::vector<TypeRef> type_args;
        type_args.reserve(args.size());

        for (const auto &arg : args)
        {
            TypeRef resolved_arg = resolve(arg, ctx);
            if (resolved_arg.is_error())
            {
                return resolved_arg;
            }
            type_args.push_back(resolved_arg);
        }

        // Check if base is a registered generic template
        if (!_generic_registry.is_template(base_type))
        {
            return make_error("'" + base_type->display_name() + "' is not a generic type",
                              ctx.current_location);
        }

        // Instantiate the generic type
        return _generic_registry.instantiate(base_type, std::move(type_args), _arena);
    }

    bool TypeResolver::is_primitive_name(const std::string &name) const
    {
        return _primitive_map.find(name) != _primitive_map.end();
    }

    bool TypeResolver::is_generic_param(const std::string &name, const ResolutionContext &ctx) const
    {
        return ctx.generic_bindings.find(name) != ctx.generic_bindings.end();
    }

    TypeRef TypeResolver::make_error(const std::string &reason,
                                      const SourceLocation &location,
                                      const std::vector<std::string> &notes)
    {
        // Report diagnostic if available
        if (_diagnostics)
        {
            report_error(reason, location);
            for (const auto &note : notes)
            {
                report_note(note, location);
            }
        }

        // Create error type
        if (notes.empty())
        {
            return _arena.create_error(reason, location);
        }
        return _arena.create_error(reason, location, notes);
    }

    void TypeResolver::report_error(const std::string &message, const SourceLocation &location)
    {
        // TODO: Integrate with DiagEmitter when available
        // For now, errors are captured in ErrorType
    }

    void TypeResolver::report_note(const std::string &message, const SourceLocation &location)
    {
        // TODO: Integrate with DiagEmitter when available
    }

    // ========================================================================
    // String-based type resolution helpers
    // ========================================================================

    TypeRef TypeResolver::resolve_function_type_string(const std::string &type_str, ResolutionContext &ctx)
    {
        // Parse function type string like "(void*,u8*,u64)->IoResult<u64>"
        LOG_DEBUG(LogComponent::GENERAL, "TypeResolver: Parsing function type string '{}'", type_str);

        // Find the closing paren that matches the opening one
        int depth = 0;
        size_t params_end = 0;
        for (size_t i = 0; i < type_str.size(); ++i)
        {
            if (type_str[i] == '(')
                depth++;
            else if (type_str[i] == ')')
            {
                depth--;
                if (depth == 0)
                {
                    params_end = i;
                    break;
                }
            }
        }

        if (params_end == 0)
        {
            return make_error("invalid function type (unbalanced parens): " + type_str, ctx.current_location);
        }

        // Extract parameter list and return type
        std::string params_str = type_str.substr(1, params_end - 1); // Content inside ()
        std::string rest = type_str.substr(params_end + 1);

        // Find -> and extract return type
        size_t arrow_pos = rest.find("->");
        if (arrow_pos == std::string::npos)
        {
            return make_error("invalid function type (missing arrow): " + type_str, ctx.current_location);
        }

        std::string return_type_str = rest.substr(arrow_pos + 2);
        // Trim whitespace from return type
        while (!return_type_str.empty() && std::isspace(static_cast<unsigned char>(return_type_str.front())))
            return_type_str.erase(0, 1);
        while (!return_type_str.empty() && std::isspace(static_cast<unsigned char>(return_type_str.back())))
            return_type_str.pop_back();

        LOG_DEBUG(LogComponent::GENERAL, "TypeResolver: Function type params='{}', return='{}'",
                  params_str, return_type_str);

        // Parse parameter types
        std::vector<TypeRef> param_types;
        if (!params_str.empty())
        {
            // Split by comma, respecting nested types
            int nested_depth = 0;
            size_t start = 0;
            for (size_t i = 0; i <= params_str.size(); ++i)
            {
                if (i == params_str.size() || (params_str[i] == ',' && nested_depth == 0))
                {
                    std::string param_type_str = params_str.substr(start, i - start);
                    // Trim whitespace
                    while (!param_type_str.empty() && std::isspace(static_cast<unsigned char>(param_type_str.front())))
                        param_type_str.erase(0, 1);
                    while (!param_type_str.empty() && std::isspace(static_cast<unsigned char>(param_type_str.back())))
                        param_type_str.pop_back();

                    if (!param_type_str.empty())
                    {
                        // Resolve each parameter type recursively through resolve_named
                        TypeRef param_type = resolve_named(param_type_str, ctx);
                        if (param_type.is_error())
                        {
                            LOG_DEBUG(LogComponent::GENERAL, "TypeResolver: Function param '{}' failed to resolve",
                                      param_type_str);
                            return param_type; // Propagate error
                        }
                        param_types.push_back(param_type);
                    }
                    start = i + 1;
                }
                else if (params_str[i] == '<' || params_str[i] == '(')
                {
                    nested_depth++;
                }
                else if (params_str[i] == '>' || params_str[i] == ')')
                {
                    nested_depth--;
                }
            }
        }

        // Resolve return type
        TypeRef return_type = resolve_named(return_type_str, ctx);
        if (return_type.is_error())
        {
            LOG_DEBUG(LogComponent::GENERAL, "TypeResolver: Function return type '{}' failed to resolve",
                      return_type_str);
            return return_type; // Propagate error
        }

        LOG_DEBUG(LogComponent::GENERAL, "TypeResolver: Successfully resolved function type: {} params, return='{}'",
                  param_types.size(), return_type->display_name());

        return _arena.get_function(return_type, std::move(param_types), false);
    }

    TypeRef TypeResolver::resolve_pointer_type_string(const std::string &type_str, ResolutionContext &ctx)
    {
        // Parse pointer type string like "void*", "u8**", etc.
        LOG_DEBUG(LogComponent::GENERAL, "TypeResolver: Parsing pointer type string '{}'", type_str);

        // Count trailing asterisks
        size_t ptr_count = 0;
        size_t pos = type_str.size();
        while (pos > 0 && type_str[pos - 1] == '*')
        {
            ptr_count++;
            pos--;
        }

        // Get the base type string (without the asterisks)
        std::string base_type_str = type_str.substr(0, pos);

        LOG_DEBUG(LogComponent::GENERAL, "TypeResolver: Pointer type base='{}', ptr_levels={}",
                  base_type_str, ptr_count);

        // Resolve the base type
        TypeRef base_type = resolve_named(base_type_str, ctx);
        if (base_type.is_error())
        {
            LOG_DEBUG(LogComponent::GENERAL, "TypeResolver: Pointer base type '{}' failed to resolve",
                      base_type_str);
            return base_type; // Propagate error
        }

        // Apply pointer levels
        TypeRef result = base_type;
        for (size_t i = 0; i < ptr_count; ++i)
        {
            result = _arena.get_pointer_to(result);
        }

        LOG_DEBUG(LogComponent::GENERAL, "TypeResolver: Successfully resolved pointer type '{}'", type_str);
        return result;
    }

    TypeRef TypeResolver::resolve_reference_type_string(const std::string &type_str, ResolutionContext &ctx)
    {
        // Parse reference type string like "&T", "&mut T", etc.
        LOG_DEBUG(LogComponent::GENERAL, "TypeResolver: Parsing reference type string '{}'", type_str);

        bool is_mutable = false;
        std::string base_type_str;

        // Check for &mut T pattern
        if (type_str.length() > 4 && type_str.substr(0, 4) == "&mut")
        {
            is_mutable = true;
            // Skip "&mut" and any whitespace
            size_t start = 4;
            while (start < type_str.length() &&
                   (type_str[start] == ' ' || type_str[start] == '\t'))
            {
                start++;
            }
            base_type_str = type_str.substr(start);
        }
        else
        {
            // Just & (immutable reference)
            // Skip "&" and any whitespace
            size_t start = 1;
            while (start < type_str.length() &&
                   (type_str[start] == ' ' || type_str[start] == '\t'))
            {
                start++;
            }
            base_type_str = type_str.substr(start);
        }

        LOG_DEBUG(LogComponent::GENERAL, "TypeResolver: Reference type base='{}', mutable={}",
                  base_type_str, is_mutable);

        // Resolve the base type
        TypeRef base_type = resolve_named(base_type_str, ctx);
        if (base_type.is_error())
        {
            LOG_DEBUG(LogComponent::GENERAL, "TypeResolver: Reference base type '{}' failed to resolve",
                      base_type_str);
            return base_type; // Propagate error
        }

        // Create the reference type
        RefMutability mut = is_mutable ? RefMutability::Mutable : RefMutability::Immutable;
        TypeRef result = _arena.get_reference_to(base_type, mut);

        LOG_DEBUG(LogComponent::GENERAL, "TypeResolver: Successfully resolved reference type '{}'", type_str);
        return result;
    }

} // namespace Cryo
