/******************************************************************************
 * @file TypeResolver.cpp
 * @brief Implementation of TypeResolver for Cryo's new type system
 ******************************************************************************/

#include "Types2/TypeResolver.hpp"
#include "Types2/PrimitiveTypes.hpp"
#include "Types2/CompoundTypes.hpp"
#include "Types2/UserDefinedTypes.hpp"

#include <sstream>

namespace Cryo
{
    // ========================================================================
    // TypeAnnotation factory methods
    // ========================================================================

    TypeAnnotation TypeAnnotation::primitive(const std::string &name, SourceLocation loc)
    {
        TypeAnnotation ann;
        ann.kind = TypeAnnotationKind::Primitive;
        ann.name = name;
        ann.location = loc;
        return ann;
    }

    TypeAnnotation TypeAnnotation::named(const std::string &name, SourceLocation loc)
    {
        TypeAnnotation ann;
        ann.kind = TypeAnnotationKind::Named;
        ann.name = name;
        ann.location = loc;
        return ann;
    }

    TypeAnnotation TypeAnnotation::qualified(std::vector<std::string> path, SourceLocation loc)
    {
        TypeAnnotation ann;
        ann.kind = TypeAnnotationKind::Qualified;
        ann.qualified_path = std::move(path);
        ann.location = loc;
        return ann;
    }

    TypeAnnotation TypeAnnotation::pointer(TypeAnnotation inner, SourceLocation loc)
    {
        TypeAnnotation ann;
        ann.kind = TypeAnnotationKind::Pointer;
        ann.inner = std::make_unique<TypeAnnotation>(std::move(inner));
        ann.location = loc;
        return ann;
    }

    TypeAnnotation TypeAnnotation::reference(TypeAnnotation inner, bool is_mut, SourceLocation loc)
    {
        TypeAnnotation ann;
        ann.kind = TypeAnnotationKind::Reference;
        ann.inner = std::make_unique<TypeAnnotation>(std::move(inner));
        ann.is_mutable = is_mut;
        ann.location = loc;
        return ann;
    }

    TypeAnnotation TypeAnnotation::array(TypeAnnotation elem, std::optional<size_t> size, SourceLocation loc)
    {
        TypeAnnotation ann;
        ann.kind = TypeAnnotationKind::Array;
        ann.inner = std::make_unique<TypeAnnotation>(std::move(elem));
        ann.array_size = size;
        ann.location = loc;
        return ann;
    }

    TypeAnnotation TypeAnnotation::function(std::vector<TypeAnnotation> params,
                                             TypeAnnotation ret, bool variadic, SourceLocation loc)
    {
        TypeAnnotation ann;
        ann.kind = TypeAnnotationKind::Function;
        ann.elements = std::move(params);
        ann.return_type = std::make_unique<TypeAnnotation>(std::move(ret));
        ann.is_variadic = variadic;
        ann.location = loc;
        return ann;
    }

    TypeAnnotation TypeAnnotation::tuple(std::vector<TypeAnnotation> elems, SourceLocation loc)
    {
        TypeAnnotation ann;
        ann.kind = TypeAnnotationKind::Tuple;
        ann.elements = std::move(elems);
        ann.location = loc;
        return ann;
    }

    TypeAnnotation TypeAnnotation::optional(TypeAnnotation inner, SourceLocation loc)
    {
        TypeAnnotation ann;
        ann.kind = TypeAnnotationKind::Optional;
        ann.inner = std::make_unique<TypeAnnotation>(std::move(inner));
        ann.location = loc;
        return ann;
    }

    TypeAnnotation TypeAnnotation::generic(TypeAnnotation base,
                                            std::vector<TypeAnnotation> args, SourceLocation loc)
    {
        TypeAnnotation ann;
        ann.kind = TypeAnnotationKind::Generic;
        ann.inner = std::make_unique<TypeAnnotation>(std::move(base));
        ann.elements = std::move(args);
        ann.location = loc;
        return ann;
    }

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
                               DiagnosticManager *diag)
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

        // Type not found
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
        // Resolve base type
        TypeRef base_type = resolve(base, ctx);
        if (base_type.is_error())
        {
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
        // TODO: Integrate with DiagnosticManager when available
        // For now, errors are captured in ErrorType
    }

    void TypeResolver::report_note(const std::string &message, const SourceLocation &location)
    {
        // TODO: Integrate with DiagnosticManager when available
    }

} // namespace Cryo
