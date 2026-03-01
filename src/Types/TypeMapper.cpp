/******************************************************************************
 * @file TypeMapper.cpp
 * @brief Implementation of TypeMapper for Cryo's new type system
 ******************************************************************************/

#include "Types/TypeMapper.hpp"
#include "Types/GenericRegistry.hpp"
#include "Diagnostics/Diag.hpp"
#include "Utils/Logger.hpp"

#include <algorithm>
#include <sstream>

namespace Cryo
{
    // Forward declaration for static helper
    static std::string mangle_display_name(const std::string &display_name);

    // ========================================================================
    // Construction
    // ========================================================================

    TypeMapper::TypeMapper(TypeArena &arena,
                           llvm::LLVMContext &llvm_ctx,
                           llvm::Module *module)
        : _arena(arena),
          _llvm_ctx(llvm_ctx),
          _module(module),
          _generics(nullptr)
    {
    }

    // ========================================================================
    // Primary Interface
    // ========================================================================

    llvm::Type *TypeMapper::map(TypeRef type)
    {
        if (!type.is_valid())
        {
            report_error("Cannot map invalid type");
            return nullptr;
        }

        const Type *t = type.get();
        if (!t)
        {
            report_error("TypeRef points to null type");
            return nullptr;
        }

        TypeKind kind = t->kind();
        // Check cache first
        auto cached = lookup_cached(type);
        if (cached)
        {
            return cached;
        }

        llvm::Type *result = nullptr;

        switch (kind)
        {
        case TypeKind::Void:
            result = map_void(static_cast<const VoidType *>(t));
            break;

        case TypeKind::Unit:
            result = map_unit(static_cast<const UnitType *>(t));
            break;

        case TypeKind::Bool:
            result = map_bool(static_cast<const BoolType *>(t));
            break;

        case TypeKind::Int:
            result = map_int(static_cast<const IntType *>(t));
            break;

        case TypeKind::Float:
            result = map_float(static_cast<const FloatType *>(t));
            break;

        case TypeKind::Char:
            result = map_char(static_cast<const CharType *>(t));
            break;

        case TypeKind::String:
            result = map_string(static_cast<const StringType *>(t));
            break;

        case TypeKind::Never:
            result = map_never(static_cast<const NeverType *>(t));
            break;

        case TypeKind::Pointer:
            result = map_pointer(static_cast<const PointerType *>(t));
            break;

        case TypeKind::Reference:
            result = map_reference(static_cast<const ReferenceType *>(t));
            break;

        case TypeKind::Array:
            result = map_array(static_cast<const ArrayType *>(t));
            break;

        case TypeKind::Function:
            result = map_function_type(static_cast<const FunctionType *>(t));
            break;

        case TypeKind::Tuple:
            result = map_tuple(static_cast<const TupleType *>(t));
            break;

        case TypeKind::Optional:
            result = map_optional(static_cast<const OptionalType *>(t));
            break;

        case TypeKind::Struct:
            result = map_struct(static_cast<const StructType *>(t));
            break;

        case TypeKind::Class:
            result = map_class(static_cast<const ClassType *>(t));
            break;

        case TypeKind::Enum:
            result = map_enum(static_cast<const EnumType *>(t));
            break;

        case TypeKind::Trait:
            result = map_trait(static_cast<const TraitType *>(t));
            break;

        case TypeKind::TypeAlias:
            result = map_type_alias(static_cast<const TypeAliasType *>(t));
            break;

        case TypeKind::GenericParam:
            result = map_generic_param(static_cast<const GenericParamType *>(t));
            break;

        case TypeKind::BoundedParam:
            result = map_bounded_param(static_cast<const BoundedParamType *>(t));
            break;

        case TypeKind::InstantiatedType:
            result = map_instantiated(static_cast<const InstantiatedType *>(t));
            break;

        case TypeKind::Error:
            result = map_error(static_cast<const ErrorType *>(t));
            break;

        default:
            report_error("Unknown type kind: " + std::to_string(static_cast<int>(kind)));
            return nullptr;
        }

        // Log the result
        LOG_DEBUG(Cryo::LogComponent::TYPECHECKER,
                  "TypeMapper::map: TypeID={} '{}' -> LLVM type ID={}",
                  type.id().id, t->display_name(),
                  result ? result->getTypeID() : -1);

        // Cache the result
        if (result)
        {
            cache_type(type, result);
        }

        return result;
    }

    std::vector<llvm::Type *> TypeMapper::map_all(const std::vector<TypeRef> &types)
    {
        std::vector<llvm::Type *> result;
        result.reserve(types.size());

        for (const auto &type : types)
        {
            result.push_back(map(type));
        }

        return result;
    }

    llvm::FunctionType *TypeMapper::map_function(TypeRef return_type,
                                                 const std::vector<TypeRef> &param_types,
                                                 bool is_variadic)
    {
        llvm::Type *ret = return_type.is_valid() ? map(return_type) : void_type();
        if (!ret)
        {
            ret = void_type();
        }

        std::vector<llvm::Type *> params;
        params.reserve(param_types.size());

        for (size_t i = 0; i < param_types.size(); ++i)
        {
            const auto &param = param_types[i];
            llvm::Type *mapped = map(param);
            if (mapped)
            {
                params.push_back(mapped);
            }
            else
            {
                // Report error - parameter type mapping failed
                std::string param_name = param.is_valid() ? param->display_name() : "<invalid>";
                LOG_ERROR(LogComponent::CODEGEN,
                          "TypeMapper::map_function: Failed to map parameter {} type '{}' - "
                          "this indicates a type resolution issue that should be fixed",
                          i, param_name);

                diag_emitter().emit(
                    Diag::error(ErrorCode::E0609_TYPE_MAPPING_ERROR,
                                "failed to map function parameter " + std::to_string(i) +
                                    " type '" + param_name + "'")
                        .with_note("Using pointer type as fallback - this may cause incorrect code generation")
                        .help("Ensure the parameter type is properly resolved before codegen"));

                report_error("Failed to map parameter type: " + param_name);
                params.push_back(ptr_type());
            }
        }

        return llvm::FunctionType::get(ret, params, is_variadic);
    }

    // ========================================================================
    // Primitive Type Accessors
    // ========================================================================

    llvm::Type *TypeMapper::void_type()
    {
        return llvm::Type::getVoidTy(_llvm_ctx);
    }

    llvm::StructType *TypeMapper::unit_type()
    {
        // Unit type is an empty struct {} - zero-sized but can be used as a value
        // Note: We don't use static caching here because the struct type is tied
        // to a specific LLVM context. Creating an empty struct is trivial.
        return llvm::StructType::get(_llvm_ctx, {}, false);
    }

    llvm::IntegerType *TypeMapper::bool_type()
    {
        return llvm::Type::getInt1Ty(_llvm_ctx);
    }

    llvm::IntegerType *TypeMapper::char_type()
    {
        return llvm::Type::getInt8Ty(_llvm_ctx);
    }

    llvm::IntegerType *TypeMapper::i8_type()
    {
        return llvm::Type::getInt8Ty(_llvm_ctx);
    }

    llvm::IntegerType *TypeMapper::i16_type()
    {
        return llvm::Type::getInt16Ty(_llvm_ctx);
    }

    llvm::IntegerType *TypeMapper::i32_type()
    {
        return llvm::Type::getInt32Ty(_llvm_ctx);
    }

    llvm::IntegerType *TypeMapper::i64_type()
    {
        return llvm::Type::getInt64Ty(_llvm_ctx);
    }

    llvm::IntegerType *TypeMapper::i128_type()
    {
        return llvm::Type::getInt128Ty(_llvm_ctx);
    }

    llvm::IntegerType *TypeMapper::int_type(unsigned bits)
    {
        return llvm::Type::getIntNTy(_llvm_ctx, bits);
    }

    llvm::Type *TypeMapper::f32_type()
    {
        return llvm::Type::getFloatTy(_llvm_ctx);
    }

    llvm::Type *TypeMapper::f64_type()
    {
        return llvm::Type::getDoubleTy(_llvm_ctx);
    }

    llvm::PointerType *TypeMapper::string_type()
    {
        return llvm::PointerType::get(i8_type(), 0);
    }

    llvm::PointerType *TypeMapper::ptr_type()
    {
        return llvm::PointerType::getUnqual(_llvm_ctx);
    }

    // ========================================================================
    // Type Utilities
    // ========================================================================

    uint64_t TypeMapper::size_of(llvm::Type *type)
    {
        if (!type || !_module)
            return 0;

        if (!type->isSized())
        {
            return 0;
        }

        const llvm::DataLayout &layout = _module->getDataLayout();
        return layout.getTypeAllocSize(type);
    }

    uint64_t TypeMapper::align_of(llvm::Type *type)
    {
        if (!type || !_module)
            return 1;

        if (!type->isSized())
        {
            return 8; // Default alignment for unsized types
        }

        const llvm::DataLayout &layout = _module->getDataLayout();
        return layout.getABITypeAlign(type).value();
    }

    llvm::Constant *TypeMapper::null_value(llvm::Type *type)
    {
        if (!type)
            return nullptr;
        return llvm::Constant::getNullValue(type);
    }

    bool TypeMapper::is_signed(TypeRef type)
    {
        if (!type.is_valid())
            return false;

        const Type *t = type.get();
        if (t->kind() != TypeKind::Int)
            return false;

        auto *int_type = static_cast<const IntType *>(t);
        return int_type->is_signed();
    }

    bool TypeMapper::is_float(llvm::Type *type)
    {
        return type && (type->isFloatTy() || type->isDoubleTy() ||
                        type->isHalfTy() || type->isFP128Ty());
    }

    // ========================================================================
    // Struct Type Management
    // ========================================================================

    llvm::StructType *TypeMapper::get_or_create_struct(const std::string &name)
    {
        auto it = _struct_cache.find(name);
        if (it != _struct_cache.end())
        {
            return it->second;
        }

        // Check LLVM context for existing struct
        if (llvm::StructType *existing = llvm::StructType::getTypeByName(_llvm_ctx, name))
        {
            _struct_cache[name] = existing;
            return existing;
        }

        // Create new opaque struct
        llvm::StructType *st = llvm::StructType::create(_llvm_ctx, name);
        _struct_cache[name] = st;
        return st;
    }

    void TypeMapper::register_struct(const std::string &name, llvm::StructType *type)
    {
        if (type)
        {
            _struct_cache[name] = type;
        }
    }

    llvm::StructType *TypeMapper::lookup_struct(const std::string &name)
    {
        auto it = _struct_cache.find(name);
        return (it != _struct_cache.end()) ? it->second : nullptr;
    }

    bool TypeMapper::has_struct(const std::string &name)
    {
        return _struct_cache.find(name) != _struct_cache.end();
    }

    void TypeMapper::complete_struct(llvm::StructType *st,
                                     const std::vector<llvm::Type *> &fields,
                                     bool packed)
    {
        if (st && st->isOpaque())
        {
            st->setBody(fields, packed);
        }
    }

    // ========================================================================
    // Cache Management
    // ========================================================================

    void TypeMapper::clear_cache()
    {
        _type_cache.clear();
        _struct_cache.clear();
    }

    void TypeMapper::cache_type(TypeRef type, llvm::Type *llvm_type)
    {
        if (type.is_valid() && llvm_type)
        {
            _type_cache[type.id()] = llvm_type;
        }
    }

    llvm::Type *TypeMapper::lookup_cached(TypeRef type)
    {
        if (!type.is_valid())
            return nullptr;
        return lookup_cached(type.id());
    }

    llvm::Type *TypeMapper::lookup_cached(TypeID id)
    {
        auto it = _type_cache.find(id);
        return (it != _type_cache.end()) ? it->second : nullptr;
    }

    // ========================================================================
    // Primitive Type Mapping
    // ========================================================================

    llvm::Type *TypeMapper::map_void(const VoidType *)
    {
        return void_type();
    }

    llvm::Type *TypeMapper::map_unit(const UnitType *)
    {
        // Unit type is represented as an empty struct in LLVM
        // This is a zero-sized type that can be passed/returned unlike void
        return unit_type();
    }

    llvm::Type *TypeMapper::map_bool(const BoolType *)
    {
        return bool_type();
    }

    llvm::Type *TypeMapper::map_int(const IntType *type)
    {
        if (!type)
            return nullptr;

        switch (type->integer_kind())
        {
        case IntegerKind::I8:
        case IntegerKind::U8:
            return i8_type();

        case IntegerKind::I16:
        case IntegerKind::U16:
            return i16_type();

        case IntegerKind::I32:
        case IntegerKind::U32:
            return i32_type();

        case IntegerKind::I64:
        case IntegerKind::U64:
            return i64_type();

        case IntegerKind::I128:
        case IntegerKind::U128:
            return i128_type();

        default:
            return i32_type(); // Default
        }
    }

    llvm::Type *TypeMapper::map_float(const FloatType *type)
    {
        if (!type)
            return nullptr;

        switch (type->float_kind())
        {
        case FloatKind::F32:
            return f32_type();

        case FloatKind::F64:
        default:
            return f64_type();
        }
    }

    llvm::Type *TypeMapper::map_char(const CharType *)
    {
        return char_type();
    }

    llvm::Type *TypeMapper::map_string(const StringType *)
    {
        return string_type();
    }

    llvm::Type *TypeMapper::map_never(const NeverType *)
    {
        // Never type doesn't have a runtime representation
        // Using void as placeholder - code paths should never reach here
        return void_type();
    }

    // ========================================================================
    // Compound Type Mapping
    // ========================================================================

    llvm::Type *TypeMapper::map_pointer(const PointerType *)
    {
        // LLVM 15+ uses opaque pointers
        return ptr_type();
    }

    llvm::Type *TypeMapper::map_reference(const ReferenceType *)
    {
        // References are represented as pointers
        return ptr_type();
    }

    llvm::Type *TypeMapper::map_array(const ArrayType *type)
    {
        if (!type)
            return nullptr;

        TypeRef elem_ref = type->element();
        llvm::Type *elem_llvm = map(elem_ref);
        if (!elem_llvm)
        {
            report_error("Failed to map array element type");
            return nullptr;
        }

        // Fixed-size arrays (T[N]) are mapped to native LLVM arrays [N x T]
        // This is important for struct field layout and efficient access
        if (type->is_fixed_size())
        {
            size_t array_size = type->size().value();
            // void[N] represents an opaque byte buffer (e.g., pthread types) - map to [N x i8]
            if (elem_llvm->isVoidTy())
            {
                elem_llvm = i8_type();
            }
            return llvm::ArrayType::get(elem_llvm, array_size);
        }

        // Dynamic arrays (T[]) are represented as struct { T* elements, u64 length, u64 capacity }
        std::string array_name = "Array<" + elem_ref.get()->display_name() + ">";

        auto it = _struct_cache.find(array_name);
        if (it != _struct_cache.end())
        {
            return it->second;
        }

        std::vector<llvm::Type *> fields = {
            ptr_type(), // elements: T*
            i64_type(), // length: u64
            i64_type()  // capacity: u64
        };

        llvm::StructType *array_struct = llvm::StructType::create(
            _llvm_ctx, fields, array_name);

        _struct_cache[array_name] = array_struct;
        return array_struct;
    }

    llvm::Type *TypeMapper::map_function_type(const FunctionType *type)
    {
        if (!type)
            return nullptr;

        TypeRef ret_ref = type->return_type();
        llvm::Type *ret_llvm = ret_ref.is_valid() ? map(ret_ref) : void_type();

        std::vector<llvm::Type *> params;
        for (const auto &param : type->param_types())
        {
            llvm::Type *mapped = map(param);
            if (mapped)
            {
                params.push_back(mapped);
            }
        }

        llvm::FunctionType *fn = llvm::FunctionType::get(
            ret_llvm, params, type->is_variadic());

        // Return as pointer to function
        return llvm::PointerType::get(fn, 0);
    }

    llvm::Type *TypeMapper::map_tuple(const TupleType *type)
    {
        if (!type)
            return nullptr;

        // Zero-element tuple is the unit type () — use an anonymous empty struct
        // like unit_type() rather than creating a named "tuple()" that could
        // conflict with (or shadow) an existing opaque struct of the same name.
        if (type->elements().empty())
            return unit_type();

        std::vector<llvm::Type *> fields;
        std::ostringstream name_stream;
        name_stream << "tuple(";

        bool first = true;
        size_t elem_index = 0;
        for (const auto &elem : type->elements())
        {
            llvm::Type *mapped = map(elem);
            if (mapped)
            {
                fields.push_back(mapped);
            }
            else
            {
                // Report error - tuple element type mapping failed
                std::string elem_name = elem.is_valid() ? elem->display_name() : "<invalid>";
                LOG_ERROR(LogComponent::CODEGEN,
                          "TypeMapper::map_tuple: Failed to map element {} type '{}' - "
                          "this indicates a type resolution issue that should be fixed",
                          elem_index, elem_name);

                diag_emitter().emit(
                    Diag::error(ErrorCode::E0609_TYPE_MAPPING_ERROR,
                                "failed to map tuple element " + std::to_string(elem_index) +
                                    " type '" + elem_name + "'")
                        .with_note("Using pointer type as fallback - this may cause incorrect code generation")
                        .help("Ensure the tuple element type is properly resolved before codegen"));

                report_error("Failed to map tuple element type: " + elem_name);
                fields.push_back(ptr_type());
            }

            if (!first)
                name_stream << ",";
            first = false;
            name_stream << elem.get()->display_name();
            ++elem_index;
        }
        name_stream << ")";

        std::string tuple_name = name_stream.str();

        // Check cache
        auto it = _struct_cache.find(tuple_name);
        if (it != _struct_cache.end())
        {
            return it->second;
        }

        // Check if a struct with this name already exists in the LLVM context
        // (e.g., created as opaque by get_or_create_struct). If so, set its body
        // rather than creating a duplicate with a suffixed name.
        llvm::StructType *tuple_struct = llvm::StructType::getTypeByName(_llvm_ctx, tuple_name);
        if (tuple_struct)
        {
            if (tuple_struct->isOpaque())
                tuple_struct->setBody(fields, false);
        }
        else
        {
            tuple_struct = llvm::StructType::create(_llvm_ctx, fields, tuple_name);
        }

        _struct_cache[tuple_name] = tuple_struct;
        return tuple_struct;
    }

    llvm::Type *TypeMapper::map_optional(const OptionalType *type)
    {
        if (!type)
            return nullptr;

        TypeRef wrapped = type->wrapped();
        llvm::Type *wrapped_llvm = map(wrapped);

        std::string name = "Optional<" + wrapped.get()->display_name() + ">";

        auto it = _struct_cache.find(name);
        if (it != _struct_cache.end() && !it->second->isOpaque())
        {
            return it->second;
        }

        size_t payload_size = wrapped_llvm ? size_of(wrapped_llvm) : 8;
        return create_tagged_union(name, 1, payload_size);
    }

    // ========================================================================
    // User-Defined Type Mapping
    // ========================================================================

    llvm::Type *TypeMapper::map_struct(const StructType *type)
    {
        if (!type)
            return nullptr;

        std::string name = type->qualified_name().name;

        // Check if this struct is actually a placeholder for a type alias
        // If so, redirect to the resolved target type
        auto alias_it = _type_alias_targets.find(name);
        if (alias_it != _type_alias_targets.end() && alias_it->second.is_valid())
        {
            LOG_DEBUG(LogComponent::CODEGEN,
                      "TypeMapper::map_struct: '{}' is a type alias placeholder, redirecting to '{}'",
                      name, alias_it->second->display_name());
            return map(alias_it->second);
        }

        // First check our cache
        auto existing = lookup_struct(name);
        if (existing && !existing->isOpaque())
        {
            return existing;
        }

        // Check LLVM context directly - the struct might have been generated
        // by TypeCodegen but not yet in our cache
        if (llvm::StructType *llvm_existing = llvm::StructType::getTypeByName(_llvm_ctx, name))
        {
            if (!llvm_existing->isOpaque())
            {
                // Found a complete struct - add to our cache and return
                _struct_cache[name] = llvm_existing;
                return llvm_existing;
            }
            // Use existing opaque struct
            if (!existing)
            {
                existing = llvm_existing;
                _struct_cache[name] = llvm_existing;
            }
        }

        llvm::StructType *st = existing ? existing : get_or_create_struct(name);

        if (!st->isOpaque())
        {
            return st;
        }

        // Cycle detection: if we're already in the process of mapping this
        // struct, return the opaque struct to break the recursion.  This
        // handles self-referential types like `children: Diagnostic[]` where
        // mapping the struct's fields would recursively try to map the same
        // struct, causing infinite recursion.  The opaque struct is safe to
        // use here because LLVM struct fields that reference the same struct
        // go through an Array or Pointer indirection (both use ptr in LLVM's
        // opaque-pointer model), so the struct body doesn't need the
        // recursive type to be fully resolved.
        if (_structs_in_progress.count(name))
        {
            LOG_DEBUG(Cryo::LogComponent::TYPECHECKER,
                      "TypeMapper::map_struct: Breaking recursive cycle for '{}', returning opaque struct",
                      name);
            return st;
        }

        // Mark this struct as in-progress before mapping its fields
        _structs_in_progress.insert(name);

        // Try to complete with field information from the Cryo type
        // Note: For locally-defined structs, fields() may be empty because set_fields
        // is only called for imported modules. In that case, the struct was completed
        // by TypeCodegen using AST node fields, and we should find it in LLVM context above.
        const auto &fields = type->fields();
        if (!fields.empty())
        {
            std::vector<llvm::Type *> llvm_fields;
            llvm_fields.reserve(fields.size());
            bool has_invalid_field = false;

            for (const auto &field : fields)
            {
                llvm::Type *mapped = map(field.type);
                if (mapped)
                {
                    // Check if the mapped type is void - this indicates an unresolved/error type
                    // We should NOT set the body with void fields - let TypeCodegen handle it later
                    if (mapped->isVoidTy())
                    {
                        LOG_DEBUG(Cryo::LogComponent::TYPECHECKER,
                                  "TypeMapper::map_struct: Field '{}' of struct '{}' mapped to void, deferring to TypeCodegen",
                                  field.name, name);
                        has_invalid_field = true;
                        break;
                    }
                    llvm_fields.push_back(mapped);
                }
                else
                {
                    llvm_fields.push_back(ptr_type()); // Fallback
                }
            }

            // Only set the body if all fields have valid (non-void) types
            if (!has_invalid_field)
            {
                st->setBody(llvm_fields);
            }
        }

        // Done mapping this struct — remove from in-progress set
        _structs_in_progress.erase(name);

        return st;
    }

    llvm::Type *TypeMapper::map_class(const ClassType *type)
    {
        if (!type)
            return nullptr;

        std::string name = type->qualified_name().name;

        // First check our cache
        auto existing = lookup_struct(name);
        if (existing && !existing->isOpaque())
        {
            return existing;
        }

        // Check LLVM context directly - the class might have been generated
        // by TypeCodegen but not yet in our cache
        if (llvm::StructType *llvm_existing = llvm::StructType::getTypeByName(_llvm_ctx, name))
        {
            if (!llvm_existing->isOpaque())
            {
                // Found a complete class - add to our cache and return
                _struct_cache[name] = llvm_existing;
                return llvm_existing;
            }
            // Use existing opaque struct
            if (!existing)
            {
                existing = llvm_existing;
                _struct_cache[name] = llvm_existing;
            }
        }

        llvm::StructType *st = existing ? existing : get_or_create_struct(name);

        if (!st->isOpaque())
        {
            return st;
        }

        // Cycle detection: break infinite recursion from self-referential class types
        if (_structs_in_progress.count(name))
        {
            LOG_DEBUG(Cryo::LogComponent::TYPECHECKER,
                      "TypeMapper::map_class: Breaking recursive cycle for '{}', returning opaque struct",
                      name);
            return st;
        }

        _structs_in_progress.insert(name);

        // Try to complete with field information from the Cryo type
        const auto &fields = type->fields();
        if (!fields.empty())
        {
            std::vector<llvm::Type *> llvm_fields;
            llvm_fields.reserve(fields.size());
            bool has_invalid_field = false;

            // Add vtable pointer as first field if class has virtual methods or a base class
            if (type->needs_vtable_pointer())
            {
                llvm_fields.push_back(llvm::PointerType::get(_llvm_ctx, 0));
            }

            for (const auto &field : fields)
            {
                llvm::Type *mapped = map(field.type);
                if (mapped)
                {
                    // Check if the mapped type is void - this indicates an unresolved/error type
                    // We should NOT set the body with void fields - let TypeCodegen handle it later
                    if (mapped->isVoidTy())
                    {
                        LOG_DEBUG(Cryo::LogComponent::TYPECHECKER,
                                  "TypeMapper::map_class: Field '{}' of class '{}' mapped to void, deferring to TypeCodegen",
                                  field.name, name);
                        has_invalid_field = true;
                        break;
                    }
                    llvm_fields.push_back(mapped);
                }
                else
                {
                    llvm_fields.push_back(ptr_type()); // Fallback
                }
            }

            // Only set the body if all fields have valid (non-void) types
            if (!has_invalid_field)
            {
                st->setBody(llvm_fields);
            }
        }

        _structs_in_progress.erase(name);

        return st;
    }

    llvm::Type *TypeMapper::map_enum(const EnumType *type)
    {
        if (!type)
            return nullptr;

        std::string name = type->qualified_name().name;

        // Simple enum (no data) -> integer
        if (type->is_simple_enum())
        {
            return i32_type();
        }

        // Complex enum (tagged union)
        auto existing = lookup_struct(name);
        if (existing && !existing->isOpaque())
        {
            return existing;
        }

        // Also check LLVM context directly (GenericCodegen may have created the struct
        // there without registering it in _struct_cache)
        if (llvm::StructType *ctx_existing = llvm::StructType::getTypeByName(_llvm_ctx, name))
        {
            if (!ctx_existing->isOpaque())
            {
                _struct_cache[name] = ctx_existing;
                return ctx_existing;
            }
        }

        // GenericCodegen mangles special characters in type names (e.g., * -> p).
        // Try the mangled name to find types it created.
        {
            std::string mangled_name = name;
            std::replace(mangled_name.begin(), mangled_name.end(), '<', '_');
            std::replace(mangled_name.begin(), mangled_name.end(), '>', '_');
            std::replace(mangled_name.begin(), mangled_name.end(), ',', '_');
            std::replace(mangled_name.begin(), mangled_name.end(), ' ', '_');
            std::replace(mangled_name.begin(), mangled_name.end(), '*', 'p');
            if (mangled_name != name)
            {
                if (llvm::StructType *mangled_existing = llvm::StructType::getTypeByName(_llvm_ctx, mangled_name))
                {
                    if (!mangled_existing->isOpaque())
                    {
                        _struct_cache[name] = mangled_existing;
                        return mangled_existing;
                    }
                }
            }
        }

        // Calculate payload size from variants, with Cryo type system fallback
        size_t max_payload = 0;
        for (const auto &variant : type->variants())
        {
            size_t variant_size = 0;
            for (const auto &field : variant.payload_types)
            {
                llvm::Type *mapped = map(field);
                size_t sz = 0;
                if (mapped && mapped->isSized())
                {
                    sz = size_of(mapped);
                }
                // Fallback to Cryo type system when LLVM type is opaque
                if (sz == 0 && field.is_valid())
                {
                    sz = field->size_bytes();
                }
                variant_size += sz;
            }
            max_payload = std::max(max_payload, variant_size);
        }

        // Ensure minimum payload size
        max_payload = std::max(max_payload, static_cast<size_t>(8));

        return create_tagged_union(name, 4, max_payload);
    }

    llvm::Type *TypeMapper::map_trait(const TraitType *type)
    {
        if (!type)
            return nullptr;

        // Traits are represented as vtable pointer + data pointer (fat pointer)
        std::string name = "trait." + type->qualified_name().name;

        auto existing = lookup_struct(name);
        if (existing && !existing->isOpaque())
        {
            return existing;
        }

        std::vector<llvm::Type *> fields = {
            ptr_type(), // data pointer
            ptr_type()  // vtable pointer
        };

        llvm::StructType *st = llvm::StructType::create(_llvm_ctx, fields, name);
        _struct_cache[name] = st;
        return st;
    }

    llvm::Type *TypeMapper::map_type_alias(const TypeAliasType *type)
    {
        if (!type)
            return nullptr;

        TypeRef target = type->target();
        if (!target.is_valid())
        {
            report_error("Type alias has no target: " + type->display_name());
            return nullptr;
        }

        LOG_DEBUG(LogComponent::CODEGEN,
                  "TypeMapper::map_type_alias: '{}' -> target '{}' (kind={})",
                  type->display_name(), target->display_name(),
                  static_cast<int>(target->kind()));

        // If target is an InstantiatedType, check if it has a resolved concrete type
        if (target->kind() == TypeKind::InstantiatedType)
        {
            auto *inst = static_cast<const InstantiatedType *>(target.get());

            // First try: use resolved_type if available
            if (inst->has_resolved_type())
            {
                TypeRef resolved = inst->resolved_type();
                LOG_DEBUG(LogComponent::CODEGEN,
                          "TypeMapper::map_type_alias: '{}' -> InstantiatedType has resolved_type '{}'",
                          type->display_name(), resolved->display_name());
                return map(resolved);
            }

            // Second try: look up by mangled name directly
            // The monomorphized type might exist under a different name like "Result_voidp_AllocError"
            std::string mangled = target->mangled_name();
            if (!mangled.empty())
            {
                llvm::StructType *existing = llvm::StructType::getTypeByName(_llvm_ctx, mangled);
                if (existing && !existing->isOpaque())
                {
                    LOG_DEBUG(LogComponent::CODEGEN,
                              "TypeMapper::map_type_alias: '{}' found concrete type via mangled name '{}'",
                              type->display_name(), mangled);
                    return existing;
                }
            }

            // Third try: if we have a generic instantiator, trigger instantiation
            if (!_skip_generic_instantiation && _generic_instantiator)
            {
                TypeRef base = inst->generic_base();
                const std::vector<TypeRef> &args = inst->type_args();
                if (base.is_valid() && !args.empty())
                {
                    std::string base_name = base->display_name();
                    LOG_DEBUG(LogComponent::CODEGEN,
                              "TypeMapper::map_type_alias: '{}' triggering instantiation for base '{}'",
                              type->display_name(), base_name);
                    llvm::StructType *result = _generic_instantiator(base_name, args);
                    if (result && !result->isOpaque())
                    {
                        return result;
                    }
                }
            }

            // Fourth try: mangle the display name and look up in LLVM context
            std::string display = target->display_name();
            std::string mangled_display = mangle_display_name(display);
            if (mangled_display != display)
            {
                llvm::StructType *by_mangled = llvm::StructType::getTypeByName(_llvm_ctx, mangled_display);
                if (by_mangled && !by_mangled->isOpaque())
                {
                    LOG_DEBUG(LogComponent::CODEGEN,
                              "TypeMapper::map_type_alias: '{}' found concrete type via mangled display name '{}'",
                              type->display_name(), mangled_display);
                    return by_mangled;
                }
            }
        }

        // Default: delegate to map() which handles all type kinds
        llvm::Type *result = map(target);

        // Safety check: if we got an opaque struct with the alias name, that's wrong
        // The alias should resolve to the target's concrete type, not create a new type
        if (result && result->isStructTy())
        {
            auto *st = llvm::cast<llvm::StructType>(result);
            if (st->isOpaque() && st->hasName() && st->getName().str() == type->display_name())
            {
                LOG_WARN(LogComponent::CODEGEN,
                         "TypeMapper::map_type_alias: '{}' resolved to opaque struct with same name - "
                         "this indicates the target type was not properly instantiated",
                         type->display_name());
            }
        }

        return result;
    }

    // ========================================================================
    // Generic Type Mapping
    // ========================================================================

    llvm::Type *TypeMapper::map_generic_param(const GenericParamType *type)
    {
        if (!type)
            return nullptr;

        // Try to resolve the generic parameter using the type_param_resolver
        // This is set by GenericCodegen::begin_type_params during generic instantiation
        if (_type_param_resolver)
        {
            std::string param_name = type->display_name();
            TypeRef resolved = _type_param_resolver(param_name);
            if (resolved.is_valid())
            {
                LOG_DEBUG(LogComponent::CODEGEN,
                          "TypeMapper::map_generic_param: Resolved '{}' -> '{}'",
                          param_name, resolved->display_name());
                return map(resolved);
            }
        }

        // Generic parameters should be substituted before codegen
        // Return opaque pointer as fallback
        report_error("Unsubstituted generic parameter: " + type->display_name());
        return ptr_type();
    }

    llvm::Type *TypeMapper::map_bounded_param(const BoundedParamType *type)
    {
        if (!type)
            return nullptr;

        // Bounded parameters should be substituted before codegen
        report_error("Unsubstituted bounded parameter: " + type->display_name());
        return ptr_type();
    }

    llvm::Type *TypeMapper::map_instantiated(const InstantiatedType *type)
    {
        if (!type)
            return nullptr;

        std::string name = type->display_name();
        LOG_DEBUG(LogComponent::CODEGEN, "TypeMapper::map_instantiated: '{}' has_resolved_type={}",
                  name, type->has_resolved_type());

        // If we have a resolved concrete type, use that directly
        // BUT for enums, we still need to call the generic instantiator to generate methods
        // because Monomorphizer::create_concrete_enum only creates the type, not methods
        if (type->has_resolved_type())
        {
            TypeRef resolved = type->resolved_type();
            LOG_DEBUG(LogComponent::CODEGEN, "TypeMapper::map_instantiated: '{}' using resolved_type '{}'",
                      name, resolved.get()->display_name());

            // Trigger method generation through GenericCodegen — but only when
            // not in "skip" mode (skip is set during pre_register_functions to
            // avoid generating method bodies for cross-module generic types).
            if (!_skip_generic_instantiation && _generic_instantiator)
            {
                if (resolved->kind() == TypeKind::Enum)
                {
                    TypeRef base = type->generic_base();
                    const std::vector<TypeRef> &args = type->type_args();
                    if (base.is_valid())
                    {
                        std::string base_name = base->display_name();
                        LOG_DEBUG(LogComponent::CODEGEN,
                                  "TypeMapper::map_instantiated: Triggering method generation for enum '{}' (base: '{}')",
                                  name, base_name);
                        _generic_instantiator(base_name, args);
                    }
                }
                else if (resolved->kind() == TypeKind::Struct)
                {
                    TypeRef base = type->generic_base();
                    const std::vector<TypeRef> &args = type->type_args();
                    if (base.is_valid() && !args.empty())
                    {
                        std::string base_name = base->display_name();
                        LOG_DEBUG(LogComponent::CODEGEN,
                                  "TypeMapper::map_instantiated: Triggering method generation for struct '{}' (base: '{}')",
                                  name, base_name);
                        _generic_instantiator(base_name, args);
                    }
                }
            }

            return map(resolved);
        }

        LOG_DEBUG(LogComponent::CODEGEN, "TypeMapper::map_instantiated: '{}' has NO resolved_type, falling through",
                  name);

        // Attempt on-demand monomorphization if we have access to the monomorphizer
        // This follows the pattern from GenericExpressionResolutionPass::lookup_concrete_type()
        if (_monomorphizer && _generics)
        {
            TypeRef base = type->generic_base();
            const std::vector<TypeRef> &args = type->type_args();

            // Try by TypeID first
            if (base.is_valid() && _generics->is_template(base))
            {
                LOG_DEBUG(LogComponent::CODEGEN,
                          "TypeMapper::map_instantiated: Attempting on-demand monomorphization for '{}'", name);
                auto result = _monomorphizer->specialize(base, args);
                if (result.is_ok())
                {
                    TypeRef specialized = result.specialized_type;
                    if (specialized.is_valid() && specialized->kind() == TypeKind::InstantiatedType)
                    {
                        auto *spec_inst = static_cast<const InstantiatedType *>(specialized.get());
                        if (spec_inst->has_resolved_type())
                        {
                            LOG_DEBUG(LogComponent::CODEGEN,
                                      "TypeMapper::map_instantiated: On-demand monomorphization succeeded for '{}'", name);
                            // Update the original InstantiatedType so subsequent lookups succeed
                            auto *mut_inst = const_cast<InstantiatedType *>(type);
                            mut_inst->set_resolved_type(spec_inst->resolved_type());
                            return map(spec_inst->resolved_type());
                        }
                    }
                }
            }

            // If TypeID lookup failed, try by name (handles cross-module TypeID mismatches)
            if (base.is_valid())
            {
                std::string base_name = base->display_name();
                auto tmpl = _generics->get_template_by_name(base_name);
                if (tmpl && tmpl->generic_type.is_valid() && tmpl->generic_type.id() != base.id())
                {
                    LOG_DEBUG(LogComponent::CODEGEN,
                              "TypeMapper::map_instantiated: TypeID mismatch for '{}', trying by name", base_name);
                    auto result = _monomorphizer->specialize(tmpl->generic_type, args);
                    if (result.is_ok())
                    {
                        TypeRef specialized = result.specialized_type;
                        if (specialized.is_valid() && specialized->kind() == TypeKind::InstantiatedType)
                        {
                            auto *spec_inst = static_cast<const InstantiatedType *>(specialized.get());
                            if (spec_inst->has_resolved_type())
                            {
                                LOG_DEBUG(LogComponent::CODEGEN,
                                          "TypeMapper::map_instantiated: Name-based monomorphization succeeded for '{}'", name);
                                auto *mut_inst = const_cast<InstantiatedType *>(type);
                                mut_inst->set_resolved_type(spec_inst->resolved_type());
                                return map(spec_inst->resolved_type());
                            }
                        }
                    }
                }
            }
        }

        // Check if already exists and is complete - try display name first
        auto existing = lookup_struct(name);
        if (existing && !existing->isOpaque())
        {
            return existing;
        }

        // Also try the mangled name - GenericCodegen registers structs under mangled names
        // (e.g., "Option_voidp" instead of "Option<void*>")
        std::string mangled = mangle_display_name(name);
        if (mangled != name)
        {
            // Check our cache under mangled name
            auto mangled_existing = lookup_struct(mangled);
            if (mangled_existing && !mangled_existing->isOpaque())
            {
                // Cache under display name too for future lookups
                _struct_cache[name] = mangled_existing;
                LOG_DEBUG(LogComponent::CODEGEN,
                          "TypeMapper::map_instantiated: Found '{}' under mangled name '{}'",
                          name, mangled);
                return mangled_existing;
            }

            // Check LLVM context directly - the struct might exist there but not in our cache
            if (llvm::StructType *llvm_mangled = llvm::StructType::getTypeByName(_llvm_ctx, mangled))
            {
                if (!llvm_mangled->isOpaque())
                {
                    _struct_cache[name] = llvm_mangled;
                    _struct_cache[mangled] = llvm_mangled;
                    LOG_DEBUG(LogComponent::CODEGEN,
                              "TypeMapper::map_instantiated: Found '{}' in LLVM context as '{}'",
                              name, mangled);
                    return llvm_mangled;
                }
            }
        }

        // Try instantiation callback (takes TypeRef base)
        if (_instantiation_callback)
        {
            TypeRef base = type->generic_base();
            std::vector<TypeRef> args = type->type_args();

            llvm::StructType *result = _instantiation_callback(base, args);
            if (result && !result->isOpaque())
            {
                _struct_cache[name] = result;
                return result;
            }
        }

        // Try generic instantiator callback (takes string name)
        // This is wired to GenericCodegen::get_instantiated_type in CodegenVisitor
        TypeRef base = type->generic_base();
        std::vector<TypeRef> args = type->type_args();

        // Substitute type parameters in the args using the resolver
        // For example, if args contain T and we're in a context where T -> string,
        // we need to resolve T to string before instantiating
        if (_type_param_resolver)
        {
            std::vector<TypeRef> substituted_args;
            bool any_substituted = false;
            for (const auto &arg : args)
            {
                if (arg.is_valid() && arg->kind() == TypeKind::GenericParam)
                {
                    std::string param_name = arg->display_name();
                    TypeRef resolved = _type_param_resolver(param_name);
                    if (resolved.is_valid())
                    {
                        LOG_DEBUG(LogComponent::CODEGEN,
                                  "TypeMapper::map_instantiated: Substituted type arg '{}' -> '{}'",
                                  param_name, resolved->display_name());
                        substituted_args.push_back(resolved);
                        any_substituted = true;
                        continue;
                    }
                }
                substituted_args.push_back(arg);
            }
            if (any_substituted)
            {
                args = std::move(substituted_args);
                // Recompute the name with substituted args for cache lookup
                std::string substituted_name = base.is_valid() ? base->display_name() : "";
                if (!args.empty())
                {
                    substituted_name += "<";
                    for (size_t i = 0; i < args.size(); ++i)
                    {
                        if (i > 0)
                            substituted_name += ", ";
                        substituted_name += args[i]->display_name();
                    }
                    substituted_name += ">";
                }
                LOG_DEBUG(LogComponent::CODEGEN,
                          "TypeMapper::map_instantiated: After substitution, looking for '{}'",
                          substituted_name);
                // Check if the substituted type already exists
                auto substituted_existing = lookup_struct(substituted_name);
                if (substituted_existing && !substituted_existing->isOpaque())
                {
                    return substituted_existing;
                }
            }
        }

        if (!_skip_generic_instantiation && _generic_instantiator && base.is_valid())
        {
            std::string base_name = base->display_name();
            llvm::StructType *result = _generic_instantiator(base_name, args);
            if (result && !result->isOpaque())
            {
                _struct_cache[name] = result;
                return result;
            }
        }

        // Handle based on the generic base type

        if (base.is_valid())
        {
            const Type *base_type = base.get();

            if (base_type->kind() == TypeKind::Enum)
            {
                // Handle instantiated enum (like Result<T, E> or Option<T>)
                auto *enum_type = static_cast<const EnumType *>(base_type);
                return map_instantiated_enum(type, enum_type, args);
            }
            else if (base_type->kind() == TypeKind::Struct)
            {
                // Handle instantiated struct (like Array<T>)
                auto *struct_type = static_cast<const StructType *>(base_type);
                return map_instantiated_struct(type, struct_type, args);
            }
        }

        // Report error - instantiated type could not be properly mapped
        // This means the generic base type is neither an enum nor a struct,
        // or the base type reference is invalid
        std::string base_name = base.is_valid() ? base->display_name() : "<invalid base>";
        std::string base_kind = base.is_valid() ? std::to_string(static_cast<int>(base->kind())) : "invalid";

        LOG_ERROR(LogComponent::CODEGEN,
                  "TypeMapper::map_instantiated: Failed to map instantiated type '{}' - "
                  "base type '{}' (kind={}) is not a struct or enum. "
                  "This indicates incomplete monomorphization or type registration.",
                  name, base_name, base_kind);

        diag_emitter().emit(
            Diag::error(ErrorCode::E0609_TYPE_MAPPING_ERROR,
                        "failed to map instantiated type '" + name + "'")
                .with_note("Base type '" + base_name + "' could not be mapped to a concrete type")
                .help("Ensure the generic type is properly monomorphized before codegen"));

        report_error("Failed to map instantiated type: " + name);

        // Return opaque struct to prevent crashes, but error is tracked
        llvm::StructType *st = existing ? existing : get_or_create_struct(name);
        return st;
    }

    /// @brief Convert a display name like "Option<()>" to the mangled form "Option_()"
    /// used by GenericCodegen. This must match ICodegenComponent::mangle_generic_type_name().
    static std::string mangle_display_name(const std::string &display_name)
    {
        size_t angle_pos = display_name.find('<');
        if (angle_pos == std::string::npos)
            return display_name;

        std::string base_name = display_name.substr(0, angle_pos);
        size_t close_pos = display_name.rfind('>');
        if (close_pos == std::string::npos || close_pos <= angle_pos)
            return display_name;

        std::string args_str = display_name.substr(angle_pos + 1, close_pos - angle_pos - 1);

        // Split by commas respecting angle bracket nesting, then mangle each arg
        std::string mangled = base_name + "_";
        int depth = 0;
        std::string current_arg;
        for (char c : args_str)
        {
            if (c == '<')
            {
                depth++;
                current_arg += c;
            }
            else if (c == '>')
            {
                depth--;
                current_arg += c;
            }
            else if (c == ',' && depth == 0)
            {
                // Recursively mangle nested generics
                std::string trimmed;
                size_t s = current_arg.find_first_not_of(" \t");
                size_t e = current_arg.find_last_not_of(" \t");
                if (s != std::string::npos)
                    trimmed = current_arg.substr(s, e - s + 1);
                mangled += mangle_display_name(trimmed) + "_";
                current_arg.clear();
            }
            else if (c == '*')
            {
                current_arg += 'p'; // Pointer marker: void* -> voidp
            }
            else if (c != ' ' || depth > 0)
            {
                current_arg += c;
            }
        }
        // Add final argument
        if (!current_arg.empty())
        {
            size_t s = current_arg.find_first_not_of(" \t");
            size_t e = current_arg.find_last_not_of(" \t");
            if (s != std::string::npos)
                mangled += mangle_display_name(current_arg.substr(s, e - s + 1));
        }
        // Remove trailing underscores
        while (!mangled.empty() && mangled.back() == '_')
            mangled.pop_back();

        return mangled;
    }

    llvm::Type *TypeMapper::map_instantiated_enum(
        const InstantiatedType *inst,
        const EnumType *base_enum,
        const std::vector<TypeRef> &type_args)
    {
        std::string name = inst->display_name();
        std::string mangled = mangle_display_name(name);

        // Check if already complete under display name or mangled name
        auto existing = lookup_struct(name);
        if (existing && !existing->isOpaque())
        {
            return existing;
        }
        if (mangled != name)
        {
            auto mangled_existing = llvm::StructType::getTypeByName(_llvm_ctx, mangled);
            if (mangled_existing && !mangled_existing->isOpaque())
            {
                _struct_cache[name] = mangled_existing;
                _struct_cache[mangled] = mangled_existing;
                return mangled_existing;
            }
        }

        // Simple enum (no data) -> integer
        if (base_enum->is_simple_enum())
        {
            return i32_type();
        }

        // Complex enum - compute payload size with substituted types
        size_t max_payload = 0;

        // Helper: get size from LLVM type, falling back to Cryo type system
        // when LLVM types are opaque (e.g., during Pass 6.2 before TypeLoweringPass)
        auto get_size = [this](TypeRef cryo_type) -> size_t
        {
            llvm::Type *mapped = map(cryo_type);
            if (mapped && mapped->isSized())
            {
                size_t llvm_size = size_of(mapped);
                if (llvm_size > 0)
                    return llvm_size;
            }
            // LLVM type is opaque/unsized - use Cryo type system
            if (cryo_type.is_valid())
            {
                size_t cryo_size = cryo_type->size_bytes();
                if (cryo_size > 0)
                    return cryo_size;
            }
            return 0;
        };

        // First, check if the InstantiatedType has a resolved_type with proper variant info
        // The Monomorphizer creates a concrete EnumType with correctly substituted payload types
        if (inst->has_resolved_type())
        {
            TypeRef resolved = inst->resolved_type();
            if (resolved.is_valid() && resolved->kind() == TypeKind::Enum)
            {
                auto *resolved_enum = static_cast<const EnumType *>(resolved.get());
                for (const auto &variant : resolved_enum->variants())
                {
                    size_t variant_size = 0;
                    for (const auto &payload_type : variant.payload_types)
                    {
                        variant_size += get_size(payload_type);
                    }
                    max_payload = std::max(max_payload, variant_size);
                }

                if (max_payload > 0)
                {
                    LOG_DEBUG(LogComponent::CODEGEN,
                              "TypeMapper::map_instantiated_enum: Using resolved_type for '{}', max_payload={}",
                              name, max_payload);
                }
            }
        }

        // If we couldn't get payload size from resolved_type, try the base_enum's variants
        if (max_payload == 0 && base_enum->variant_count() > 0)
        {
            for (const auto &variant : base_enum->variants())
            {
                size_t variant_size = 0;
                for (const auto &payload_type : variant.payload_types)
                {
                    // Substitute generic params with concrete type args
                    TypeRef concrete = substitute_generic_param(payload_type, type_args);
                    variant_size += get_size(concrete);
                }
                max_payload = std::max(max_payload, variant_size);
            }
        }

        // If still no payload size (generic enum without variant info), compute from type args directly
        // This handles cases like Option<T> where the payload is simply T
        if (max_payload == 0 && !type_args.empty())
        {
            LOG_DEBUG(LogComponent::CODEGEN,
                      "TypeMapper::map_instantiated_enum: No variant info for '{}', computing from type_args",
                      name);

            for (const auto &type_arg : type_args)
            {
                size_t arg_size = get_size(type_arg);
                max_payload = std::max(max_payload, arg_size);
                LOG_DEBUG(LogComponent::CODEGEN,
                          "TypeMapper::map_instantiated_enum: Type arg '{}' has size {}",
                          type_arg->display_name(), arg_size);
            }
        }

        // Ensure minimum payload size
        max_payload = std::max(max_payload, static_cast<size_t>(8));

        LOG_DEBUG(LogComponent::CODEGEN,
                  "TypeMapper::map_instantiated_enum: Creating tagged union for '{}' with payload size {}",
                  name, max_payload);

        return create_tagged_union(name, 4, max_payload);
    }

    llvm::Type *TypeMapper::map_instantiated_struct(
        const InstantiatedType *inst,
        const StructType *base_struct,
        const std::vector<TypeRef> &type_args)
    {
        std::string name = inst->display_name();

        // Check if already complete
        auto existing = lookup_struct(name);
        if (existing && !existing->isOpaque())
        {
            return existing;
        }

        // Create struct with substituted field types
        llvm::StructType *st = existing ? existing : get_or_create_struct(name);

        std::vector<llvm::Type *> field_types;
        for (const auto &field : base_struct->fields())
        {
            TypeRef concrete = substitute_generic_param(field.type, type_args);
            llvm::Type *mapped = map(concrete);
            if (mapped)
            {
                field_types.push_back(mapped);
            }
            else
            {
                field_types.push_back(ptr_type()); // Fallback
            }
        }

        if (!field_types.empty())
        {
            st->setBody(field_types, false);
            LOG_DEBUG(LogComponent::CODEGEN, "TypeMapper::map_instantiated_struct: Set body for '{}' with {} fields",
                      name, field_types.size());
        }
        else
        {
            // Log warning - struct will remain opaque which will cause "GEP into unsized type" errors
            LOG_WARN(LogComponent::CODEGEN, "TypeMapper::map_instantiated_struct: No fields found for '{}' - base_struct has {} fields, struct will remain opaque",
                     name, base_struct->fields().size());
        }

        return st;
    }

    TypeRef TypeMapper::substitute_generic_param(TypeRef type,
                                                 const std::vector<TypeRef> &type_args)
    {
        if (!type.is_valid())
            return type;

        const Type *t = type.get();

        // If it's a generic param, substitute with the corresponding type arg
        if (t->kind() == TypeKind::GenericParam)
        {
            auto *param = static_cast<const GenericParamType *>(t);
            size_t index = param->param_index();
            if (index < type_args.size())
            {
                return type_args[index];
            }
            // Can't substitute - return as-is
            return type;
        }

        // For bounded params, also substitute
        if (t->kind() == TypeKind::BoundedParam)
        {
            auto *param = static_cast<const BoundedParamType *>(t);
            size_t index = param->param_index();
            if (index < type_args.size())
            {
                return type_args[index];
            }
            return type;
        }

        // For compound types, recursively substitute
        if (t->kind() == TypeKind::Pointer)
        {
            auto *ptr = static_cast<const PointerType *>(t);
            TypeRef inner = substitute_generic_param(ptr->pointee(), type_args);
            if (inner != ptr->pointee())
            {
                return _arena.get_pointer_to(inner);
            }
        }
        else if (t->kind() == TypeKind::Reference)
        {
            auto *ref = static_cast<const ReferenceType *>(t);
            TypeRef inner = substitute_generic_param(ref->referent(), type_args);
            if (inner != ref->referent())
            {
                RefMutability mut = ref->is_mutable() ? RefMutability::Mutable : RefMutability::Immutable;
                return _arena.get_reference_to(inner, mut);
            }
        }
        else if (t->kind() == TypeKind::Array)
        {
            auto *arr = static_cast<const ArrayType *>(t);
            TypeRef elem = substitute_generic_param(arr->element(), type_args);
            if (elem != arr->element())
            {
                // get_array_of handles both fixed and dynamic arrays via optional size
                return _arena.get_array_of(elem, arr->size());
            }
        }
        else if (t->kind() == TypeKind::Optional)
        {
            auto *opt = static_cast<const OptionalType *>(t);
            TypeRef inner = substitute_generic_param(opt->wrapped(), type_args);
            if (inner != opt->wrapped())
            {
                return _arena.get_optional_of(inner);
            }
        }

        // Non-generic or unhandled - return as-is
        return type;
    }

    llvm::Type *TypeMapper::map_error(const ErrorType *type)
    {
        if (!type)
            return void_type();

        const std::string &reason = type->reason();

        // Check if this is an "unresolved generic" error — these often have
        // concrete instantiated types available under a mangled name.
        // Example reasons: "unresolved generic: Option<Layout>", "unresolved generic: IoResult<()>"
        const std::string prefix = "unresolved generic: ";
        size_t prefix_pos = reason.find(prefix);
        if (prefix_pos != std::string::npos)
        {
            std::string generic_name = reason.substr(prefix_pos + prefix.size());

            // Step 1: If we have a type_param_resolver, substitute any remaining
            // generic params (T, U, V, E, K, etc.) with their concrete bindings.
            // This handles cases like "Option<T>" inside an Array<String> instantiation
            // where T should resolve to String.
            if (_type_param_resolver && generic_name.find('<') != std::string::npos)
            {
                size_t angle_start = generic_name.find('<');
                size_t angle_end = generic_name.rfind('>');
                if (angle_start != std::string::npos && angle_end != std::string::npos && angle_end > angle_start)
                {
                    std::string base = generic_name.substr(0, angle_start);
                    std::string args_str = generic_name.substr(angle_start + 1, angle_end - angle_start - 1);

                    // Parse args respecting nesting
                    std::vector<std::string> arg_names;
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
                            std::string arg = args_str.substr(start, i - start);
                            while (!arg.empty() && arg.front() == ' ')
                                arg.erase(arg.begin());
                            while (!arg.empty() && arg.back() == ' ')
                                arg.pop_back();
                            arg_names.push_back(arg);
                            start = i + 1;
                        }
                    }
                    std::string last_arg = args_str.substr(start);
                    while (!last_arg.empty() && last_arg.front() == ' ')
                        last_arg.erase(last_arg.begin());
                    while (!last_arg.empty() && last_arg.back() == ' ')
                        last_arg.pop_back();
                    if (!last_arg.empty())
                        arg_names.push_back(last_arg);

                    // Try to resolve each arg — substitute type params
                    bool any_substituted = false;
                    std::vector<std::string> resolved_arg_names;
                    std::vector<TypeRef> resolved_arg_types;
                    bool all_resolved = true;
                    for (const auto &arg : arg_names)
                    {
                        // First try type param resolver (handles T, U, V, E, K, etc.)
                        TypeRef resolved = _type_param_resolver(arg);
                        if (resolved.is_valid())
                        {
                            resolved_arg_names.push_back(resolved->display_name());
                            resolved_arg_types.push_back(resolved);
                            any_substituted = true;
                        }
                        else
                        {
                            // Try pointer forms like "T*"
                            if (arg.size() > 1 && arg.back() == '*')
                            {
                                std::string inner = arg.substr(0, arg.size() - 1);
                                TypeRef inner_resolved = _type_param_resolver(inner);
                                if (inner_resolved.is_valid())
                                {
                                    resolved_arg_names.push_back(inner_resolved->display_name() + "*");
                                    resolved_arg_types.push_back(_arena.get_pointer_to(inner_resolved));
                                    any_substituted = true;
                                    continue;
                                }
                            }
                            // Not a type param — keep as-is and try to resolve as concrete type
                            TypeRef concrete = resolve_type_from_string(arg);
                            if (concrete.is_valid())
                            {
                                resolved_arg_names.push_back(arg);
                                resolved_arg_types.push_back(concrete);
                            }
                            else
                            {
                                all_resolved = false;
                                break;
                            }
                        }
                    }

                    if (all_resolved && !resolved_arg_types.empty())
                    {
                        // Build substituted name and try to find/create the type
                        std::string substituted_name = base + "<";
                        for (size_t i = 0; i < resolved_arg_names.size(); ++i)
                        {
                            if (i > 0)
                                substituted_name += ", ";
                            substituted_name += resolved_arg_names[i];
                        }
                        substituted_name += ">";

                        // Try mangled name lookup in LLVM context
                        std::string mangled = mangle_display_name(substituted_name);
                        if (llvm::StructType *existing = llvm::StructType::getTypeByName(_llvm_ctx, mangled))
                        {
                            if (!existing->isOpaque())
                            {
                                LOG_DEBUG(LogComponent::CODEGEN,
                                          "TypeMapper::map_error: Resolved '{}' -> '{}' via mangled name",
                                          reason, mangled);
                                return existing;
                            }
                        }

                        // Try the generic instantiator
                        if (!_skip_generic_instantiation && _generic_instantiator)
                        {
                            // Look up the base type
                            TypeRef base_type = _arena.lookup_type_by_name(base);
                            if (!base_type.is_valid())
                                base_type = _arena.lookup_type_by_name("std::core::option::" + base);
                            if (!base_type.is_valid())
                                base_type = _arena.lookup_type_by_name("std::core::result::" + base);
                            if (!base_type.is_valid())
                                base_type = _arena.lookup_type_by_name("std::io::error::" + base);

                            if (base_type.is_valid())
                            {
                                llvm::StructType *result = _generic_instantiator(base, resolved_arg_types);
                                if (result && !result->isOpaque())
                                {
                                    LOG_DEBUG(LogComponent::CODEGEN,
                                              "TypeMapper::map_error: Resolved '{}' -> '{}' via instantiator",
                                              reason, result->getName().str());
                                    return result;
                                }
                            }
                        }

                        // Try resolve_and_map with the substituted name
                        llvm::Type *mapped = try_instantiate_generic_from_string(substituted_name);
                        if (mapped && (!llvm::isa<llvm::StructType>(mapped) ||
                                       !llvm::cast<llvm::StructType>(mapped)->isOpaque()))
                        {
                            LOG_DEBUG(LogComponent::CODEGEN,
                                      "TypeMapper::map_error: Resolved '{}' -> '{}' via string instantiation",
                                      reason, substituted_name);
                            return mapped;
                        }
                    }
                }
            }

            // Step 2: For already-concrete names (no type params to substitute),
            // try direct mangled name lookup
            {
                std::string mangled = mangle_display_name(generic_name);
                if (llvm::StructType *existing = llvm::StructType::getTypeByName(_llvm_ctx, mangled))
                {
                    if (!existing->isOpaque())
                    {
                        LOG_DEBUG(LogComponent::CODEGEN,
                                  "TypeMapper::map_error: Resolved concrete '{}' -> '{}'",
                                  reason, mangled);
                        return existing;
                    }
                }

                // Try display name too
                if (mangled != generic_name)
                {
                    if (llvm::StructType *existing = llvm::StructType::getTypeByName(_llvm_ctx, generic_name))
                    {
                        if (!existing->isOpaque())
                        {
                            LOG_DEBUG(LogComponent::CODEGEN,
                                      "TypeMapper::map_error: Resolved concrete '{}' by display name",
                                      reason);
                            return existing;
                        }
                    }
                }

                // Only try string instantiation if all type args are resolvable
                // as concrete types. If any arg fails to resolve, it's unresolved.
                bool all_args_concrete = true;
                {
                    size_t as = generic_name.find('<');
                    size_t ae = generic_name.rfind('>');
                    if (as != std::string::npos && ae != std::string::npos && ae > as)
                    {
                        std::string args_check = generic_name.substr(as + 1, ae - as - 1);
                        int depth = 0;
                        size_t seg_start = 0;
                        for (size_t ci = 0; ci <= args_check.size(); ++ci)
                        {
                            if (ci < args_check.size() && args_check[ci] == '<') depth++;
                            else if (ci < args_check.size() && args_check[ci] == '>') depth--;
                            else if ((ci == args_check.size() || (args_check[ci] == ',' && depth == 0)))
                            {
                                std::string seg = args_check.substr(seg_start, ci - seg_start);
                                while (!seg.empty() && seg.front() == ' ') seg.erase(seg.begin());
                                while (!seg.empty() && seg.back() == ' ') seg.pop_back();
                                if (!seg.empty() && !resolve_type_from_string(seg).is_valid())
                                {
                                    all_args_concrete = false;
                                    break;
                                }
                                seg_start = ci + 1;
                            }
                        }
                    }
                }

                if (all_args_concrete)
                {
                    llvm::Type *mapped = try_instantiate_generic_from_string(generic_name);
                    if (mapped && (!llvm::isa<llvm::StructType>(mapped) ||
                                   !llvm::cast<llvm::StructType>(mapped)->isOpaque()))
                    {
                        LOG_DEBUG(LogComponent::CODEGEN,
                                  "TypeMapper::map_error: Created concrete '{}' via string instantiation",
                                  reason);
                        return mapped;
                    }
                }
            }
        }

        // Fallback — genuine error type, use void as placeholder
        report_error("Mapping error type: " + reason);
        return void_type();
    }

    // ========================================================================
    // Helpers
    // ========================================================================

    llvm::StructType *TypeMapper::create_tagged_union(const std::string &name,
                                                      size_t discriminant_size,
                                                      size_t payload_size)
    {
        // Compute the mangled form of the name for GenericCodegen compatibility.
        // GenericCodegen creates opaque structs with mangled names (e.g., "Option_()"),
        // while TypeMapper uses display names (e.g., "Option<()>").
        std::string mangled = mangle_display_name(name);
        bool has_mangled = (mangled != name);

        // Check for existing non-opaque struct under either name
        auto existing = lookup_struct(name);
        if (existing && !existing->isOpaque())
        {
            return existing;
        }

        // Also check for existing struct under the mangled name
        llvm::StructType *mangled_st = nullptr;
        if (has_mangled)
        {
            mangled_st = llvm::StructType::getTypeByName(_llvm_ctx, mangled);
            if (mangled_st && !mangled_st->isOpaque())
            {
                // Already complete under mangled name - cache and return it
                _struct_cache[name] = mangled_st;
                _struct_cache[mangled] = mangled_st;
                return mangled_st;
            }
        }

        llvm::Type *discriminant_type;
        switch (discriminant_size)
        {
        case 1:
            discriminant_type = i8_type();
            break;
        case 2:
            discriminant_type = i16_type();
            break;
        case 4:
            discriminant_type = i32_type();
            break;
        default:
            discriminant_type = i64_type();
            break;
        }

        llvm::ArrayType *payload_type = llvm::ArrayType::get(i8_type(), payload_size);
        std::vector<llvm::Type *> fields = {discriminant_type, payload_type};

        // If a mangled-name opaque struct exists (from GenericCodegen), fill it and use it
        // as the canonical type. This ensures all GEP instructions from GenericCodegen
        // reference a struct with the correct body.
        if (mangled_st && mangled_st->isOpaque())
        {
            mangled_st->setBody(fields, false);
            _struct_cache[name] = mangled_st;
            _struct_cache[mangled] = mangled_st;
            return mangled_st;
        }

        // Otherwise, fill the display-name struct as before
        llvm::StructType *st = existing ? existing : get_or_create_struct(name);

        // Don't overwrite a struct that already has a body (e.g., set by GenericCodegen
        // with correct sizes from the Cryo type system). get_or_create_struct may find
        // a non-opaque struct in the LLVM context that lookup_struct missed in _struct_cache.
        if (!st->isOpaque())
        {
            _struct_cache[name] = st;
            return st;
        }

        st->setBody(fields, false);

        // Also create/fill a mangled-name alias so GenericCodegen lookups succeed
        if (has_mangled)
        {
            _struct_cache[mangled] = st;
        }

        return st;
    }

    void TypeMapper::report_error(const std::string &msg)
    {
        _has_error = true;
        _last_error = msg;
    }

    // ========================================================================
    // Backward Compatibility Methods
    // ========================================================================

    llvm::Type *TypeMapper::get_type(const std::string &name)
    {
        // First check struct cache
        auto it = _struct_cache.find(name);
        if (it != _struct_cache.end())
        {
            return it->second;
        }

        // Try to look up in module if available
        if (_module)
        {
            llvm::StructType *st = llvm::StructType::getTypeByName(_module->getContext(), name);
            if (st)
            {
                _struct_cache[name] = st;
                return st;
            }
        }

        // Try primitive type names
        if (name == "void")
            return void_type();
        if (name == "boolean")
            return bool_type();
        if (name == "i8" || name == "u8")
            return i8_type();
        if (name == "i16" || name == "u16")
            return i16_type();
        if (name == "i32" || name == "int" || name == "u32")
            return i32_type();
        if (name == "i64" || name == "u64")
            return i64_type();
        if (name == "f32" || name == "float")
            return f32_type();
        if (name == "f64" || name == "double")
            return f64_type();
        if (name == "string")
            return string_type();
        if (name == "i128")
            return i128_type();
        if (name == "u128")
            return i128_type(); // u128 uses same LLVM type as i128

        // Handle pointer types (e.g., "void*", "string*", "u8*", "WorkerData*")
        if (name.size() > 1 && name.back() == '*')
        {
            std::string base_name = name.substr(0, name.size() - 1);
            // For void*, string*, and other primitive pointer types, return opaque ptr
            if (base_name == "void" || base_name == "string" || base_name == "u8" ||
                base_name == "i8" || base_name == "char")
            {
                return ptr_type();
            }
            // For user-defined types like "WorkerData*", also return opaque ptr
            // The actual type will be handled through struct lookup
            return ptr_type();
        }

        return nullptr;
    }

    llvm::Type *TypeMapper::resolve_and_map(const std::string &name)
    {
        // Try exact name first (handles primitives and exact struct name matches)
        llvm::Type *result = get_type(name);
        if (result && (!llvm::isa<llvm::StructType>(result) ||
                       !llvm::cast<llvm::StructType>(result)->isOpaque()))
        {
            return result;
        }

        // If the name is qualified (contains ::), extract the simple type name and try that
        // e.g., "std::core::option::Option<u64>" -> "Option<u64>"
        size_t last_sep = name.rfind("::");
        if (last_sep != std::string::npos)
        {
            std::string simple_name = name.substr(last_sep + 2);
            if (!simple_name.empty())
            {
                result = get_type(simple_name);
                if (result && (!llvm::isa<llvm::StructType>(result) ||
                               !llvm::cast<llvm::StructType>(result)->isOpaque()))
                {
                    _struct_cache[name] = static_cast<llvm::StructType *>(result);
                    return result;
                }

                // For generic types like "Option<u64>", also try the mangled form "Option_u64"
                size_t angle_pos = simple_name.find('<');
                if (angle_pos != std::string::npos)
                {
                    std::string mangled = simple_name;
                    for (char &c : mangled)
                    {
                        if (c == '<' || c == '>' || c == ',' || c == ' ')
                            c = '_';
                    }
                    // Remove trailing underscore if present
                    while (!mangled.empty() && mangled.back() == '_')
                        mangled.pop_back();

                    llvm::StructType *st = llvm::StructType::getTypeByName(_llvm_ctx, mangled);
                    if (st && !st->isOpaque())
                    {
                        _struct_cache[name] = st;
                        _struct_cache[simple_name] = st;
                        return st;
                    }
                }
            }
        }

        // For generic types, try to parse and instantiate properly
        size_t angle_pos = name.find('<');
        if (angle_pos != std::string::npos)
        {
            // Try the mangled form first
            std::string mangled = name;
            for (char &c : mangled)
            {
                if (c == '<' || c == '>' || c == ',' || c == ' ')
                    c = '_';
            }
            while (!mangled.empty() && mangled.back() == '_')
                mangled.pop_back();

            llvm::StructType *st = llvm::StructType::getTypeByName(_llvm_ctx, mangled);
            if (st && !st->isOpaque())
            {
                _struct_cache[name] = st;
                return st;
            }

            // Try to parse and instantiate the generic type
            llvm::Type *instantiated = try_instantiate_generic_from_string(name);
            if (instantiated && (!llvm::isa<llvm::StructType>(instantiated) ||
                                 !llvm::cast<llvm::StructType>(instantiated)->isOpaque()))
            {
                return instantiated;
            }
        }

        // Create opaque struct as fallback
        return get_or_create_struct(name);
    }

    llvm::Type *TypeMapper::try_instantiate_generic_from_string(const std::string &name)
    {
        // Parse "Result<Duration,SystemTimeError>" or "Option<u64>"
        size_t angle_start = name.find('<');
        size_t angle_end = name.rfind('>');

        if (angle_start == std::string::npos || angle_end == std::string::npos ||
            angle_end <= angle_start)
        {
            return nullptr;
        }

        std::string base_name = name.substr(0, angle_start);
        std::string args_str = name.substr(angle_start + 1, angle_end - angle_start - 1);

        // Parse type arguments (handle nested generics)
        std::vector<std::string> arg_names;
        std::string current_arg;
        int nesting = 0;

        for (size_t i = 0; i < args_str.size(); ++i)
        {
            char c = args_str[i];
            if (c == '<')
            {
                nesting++;
                current_arg += c;
            }
            else if (c == '>')
            {
                nesting--;
                current_arg += c;
            }
            else if (c == ',' && nesting == 0)
            {
                // End of argument - trim and save
                while (!current_arg.empty() && current_arg.front() == ' ')
                    current_arg.erase(0, 1);
                while (!current_arg.empty() && current_arg.back() == ' ')
                    current_arg.pop_back();
                if (!current_arg.empty())
                    arg_names.push_back(current_arg);
                current_arg.clear();
            }
            else
            {
                current_arg += c;
            }
        }

        // Don't forget the last argument
        if (!current_arg.empty())
        {
            while (!current_arg.empty() && current_arg.front() == ' ')
                current_arg.erase(0, 1);
            while (!current_arg.empty() && current_arg.back() == ' ')
                current_arg.pop_back();
            if (!current_arg.empty())
                arg_names.push_back(current_arg);
        }

        if (arg_names.empty())
        {
            return nullptr;
        }

        // Try to look up base type in arena first
        TypeRef base_type = _arena.lookup_type_by_name(base_name);
        if (!base_type.is_valid())
        {
            // Try qualified names
            base_type = _arena.lookup_type_by_name("std::core::result::" + base_name);
            if (!base_type.is_valid())
                base_type = _arena.lookup_type_by_name("std::core::option::" + base_name);
        }

        // If we found the base type, try the full instantiation path
        if (base_type.is_valid())
        {
            std::vector<TypeRef> type_args;
            bool all_args_valid = true;
            for (const auto &arg_name : arg_names)
            {
                TypeRef arg_type = resolve_type_from_string(arg_name);
                if (!arg_type.is_valid())
                {
                    all_args_valid = false;
                    break;
                }
                type_args.push_back(arg_type);
            }

            if (all_args_valid && !type_args.empty())
            {
                TypeRef instantiated = _arena.create_instantiation(base_type, type_args);
                if (instantiated.is_valid() && !instantiated.is_error())
                {
                    // Register in name caches so lookup_type_by_name() can find it
                    _arena.register_instantiated_by_name(instantiated);
                    return map(instantiated);
                }
            }
        }

        // Fallback: For well-known generic types, directly compute the LLVM type
        // This handles cases where the base type is a template not in TypeArena
        if (base_name == "Result" || base_name == "Option")
        {
            return create_generic_enum_type_directly(name, base_name, arg_names);
        }

        return nullptr;
    }

    llvm::Type *TypeMapper::create_generic_enum_type_directly(
        const std::string &full_name,
        const std::string &base_name,
        const std::vector<std::string> &arg_names)
    {
        // Check if we already have this type
        auto existing = lookup_struct(full_name);
        if (existing && !existing->isOpaque())
        {
            return existing;
        }

        // Helper lambda to compute type size, falling back to Cryo type info for opaque LLVM types
        auto compute_type_size = [this](const std::string &type_name) -> size_t
        {
            llvm::Type *llvm_type = resolve_and_map(type_name);
            if (llvm_type && llvm_type->isSized())
            {
                return size_of(llvm_type);
            }

            // LLVM type is opaque or not available - try to compute from Cryo type info
            TypeRef cryo_type = resolve_type_from_string(type_name);
            if (!cryo_type.is_valid())
            {
                LOG_DEBUG(LogComponent::CODEGEN,
                          "TypeMapper: Cannot resolve type '{}' for size computation",
                          type_name);
                return 0;
            }

            if (cryo_type->kind() == TypeKind::Struct)
            {
                auto *struct_type = static_cast<const StructType *>(cryo_type.get());
                size_t total_size = 0;
                for (const auto &field : struct_type->fields())
                {
                    llvm::Type *field_llvm = map(field.type);
                    if (field_llvm && field_llvm->isSized())
                    {
                        total_size += _module->getDataLayout().getTypeAllocSize(field_llvm);
                    }
                    else
                    {
                        // Can't determine field size, use pointer size as default
                        total_size += 8;
                    }
                }
                LOG_DEBUG(LogComponent::CODEGEN,
                          "TypeMapper: Computed struct '{}' size from fields: {} bytes",
                          type_name, total_size);
                return total_size;
            }

            // For other types, try to map and get size
            llvm::Type *mapped = map(cryo_type);
            if (mapped && mapped->isSized())
            {
                return _module->getDataLayout().getTypeAllocSize(mapped);
            }

            LOG_DEBUG(LogComponent::CODEGEN,
                      "TypeMapper: Cannot determine size for type '{}'",
                      type_name);
            return 0;
        };

        // Compute the payload size based on type arguments
        size_t max_payload = 0;

        if (base_name == "Option")
        {
            // Option<T> has variants: Some(T), None
            // Payload is just T
            if (arg_names.size() >= 1)
            {
                max_payload = compute_type_size(arg_names[0]);
            }
        }
        else if (base_name == "Result")
        {
            // Result<T, E> has variants: Ok(T), Err(E)
            // Payload is max(sizeof(T), sizeof(E))
            if (arg_names.size() >= 1)
            {
                max_payload = std::max(max_payload, compute_type_size(arg_names[0]));
            }
            if (arg_names.size() >= 2)
            {
                max_payload = std::max(max_payload, compute_type_size(arg_names[1]));
            }
        }

        // Ensure minimum payload size
        max_payload = std::max(max_payload, static_cast<size_t>(8));

        LOG_DEBUG(LogComponent::CODEGEN,
                  "TypeMapper: Creating enum '{}' with payload size {} bytes",
                  full_name, max_payload);

        // Create the tagged union type
        return create_tagged_union(full_name, 4, max_payload);
    }

    TypeRef TypeMapper::resolve_type_from_string(const std::string &name)
    {
        // Try primitives first
        if (name == "()" || name == "unit")
            return _arena.get_unit();
        if (name == "void")
            return _arena.get_void();
        if (name == "boolean" || name == "bool")
            return _arena.get_bool();
        if (name == "i8")
            return _arena.get_i8();
        if (name == "i16")
            return _arena.get_i16();
        if (name == "i32" || name == "int")
            return _arena.get_i32();
        if (name == "i64")
            return _arena.get_i64();
        if (name == "u8")
            return _arena.get_u8();
        if (name == "u16")
            return _arena.get_u16();
        if (name == "u32")
            return _arena.get_u32();
        if (name == "u64")
            return _arena.get_u64();
        if (name == "f32" || name == "float")
            return _arena.get_f32();
        if (name == "f64" || name == "double")
            return _arena.get_f64();
        if (name == "char")
            return _arena.get_char();
        if (name == "string")
            return _arena.get_string();

        // Try to look up user-defined types
        TypeRef found = _arena.lookup_type_by_name(name);
        if (found.is_valid())
        {
            return found;
        }

        // Handle pointer types (e.g., "void*", "u8*")
        if (name.size() > 1 && name.back() == '*')
        {
            std::string inner_name = name.substr(0, name.size() - 1);
            TypeRef inner = resolve_type_from_string(inner_name);
            if (inner.is_valid())
            {
                return _arena.get_pointer_to(inner);
            }
        }

        // Check if it's a generic type itself
        if (name.find('<') != std::string::npos)
        {
            // Recursively handle nested generic
            llvm::Type *nested = try_instantiate_generic_from_string(name);
            if (nested)
            {
                // Return the type from cache or create a placeholder
                // Since we mapped it, it should be in the type cache
            }
        }

        return TypeRef{}; // Invalid
    }

    // ========================================================================
    // Utility Functions
    // ========================================================================

    std::string mangle_instantiation_name(TypeRef base,
                                          const std::vector<TypeRef> &type_args)
    {
        if (!base.is_valid())
            return "";

        std::ostringstream ss;
        ss << base.get()->display_name();

        if (!type_args.empty())
        {
            ss << "<";
            for (size_t i = 0; i < type_args.size(); ++i)
            {
                if (i > 0)
                    ss << ",";
                if (type_args[i].is_valid())
                {
                    ss << type_args[i].get()->display_name();
                }
                else
                {
                    ss << "?";
                }
            }
            ss << ">";
        }

        return ss.str();
    }

    std::string TypeIDToString(llvm::Type::TypeID id)
    {
        switch (id)
        {
        case llvm::Type::VoidTyID:
            return "VoidTyID";
        case llvm::Type::HalfTyID:
            return "HalfTyID";
        case llvm::Type::FloatTyID:
            return "FloatTyID";
        case llvm::Type::DoubleTyID:
            return "DoubleTyID";
        case llvm::Type::X86_FP80TyID:
            return "X86_FP80TyID";
        case llvm::Type::FP128TyID:
            return "FP128TyID";
        case llvm::Type::PPC_FP128TyID:
            return "PPC_FP128TyID";
        case llvm::Type::LabelTyID:
            return "LabelTyID";
        case llvm::Type::MetadataTyID:
            return "MetadataTyID";
        case llvm::Type::X86_AMXTyID:
            return "X86_AMXTyID";
        case llvm::Type::IntegerTyID:
            return "IntegerTyID";
        case llvm::Type::FunctionTyID:
            return "FunctionTyID";
        case llvm::Type::StructTyID:
            return "StructTyID";
        case llvm::Type::ArrayTyID:
            return "ArrayTyID";
        case llvm::Type::PointerTyID:
            return "PointerTyID";
        case llvm::Type::FixedVectorTyID:
            return "FixedVectorTyID";
        case llvm::Type::ScalableVectorTyID:
            return "ScalableVectorTyID";
        default:
            return "UnknownTypeID";
        }
    }

} // namespace Cryo
