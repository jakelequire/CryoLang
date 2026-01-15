/******************************************************************************
 * @file TypeMapper2.cpp
 * @brief Implementation of TypeMapper2 for Cryo's new type system
 ******************************************************************************/

#include "Types2/TypeMapper2.hpp"
#include "Types2/GenericRegistry.hpp"

#include <sstream>

namespace Cryo
{
    // ========================================================================
    // Construction
    // ========================================================================

    TypeMapper2::TypeMapper2(TypeArena &arena,
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

    llvm::Type *TypeMapper2::map(TypeRef type)
    {
        if (!type.is_valid())
        {
            report_error("Cannot map invalid type");
            return nullptr;
        }

        // Check cache first
        auto cached = lookup_cached(type);
        if (cached)
        {
            return cached;
        }

        const Type *t = type.get();
        if (!t)
        {
            report_error("TypeRef points to null type");
            return nullptr;
        }

        llvm::Type *result = nullptr;
        TypeKind kind = t->kind();

        switch (kind)
        {
        case TypeKind::Void:
            result = map_void(static_cast<const VoidType *>(t));
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

        // Cache the result
        if (result)
        {
            cache_type(type, result);
        }

        return result;
    }

    std::vector<llvm::Type *> TypeMapper2::map_all(const std::vector<TypeRef> &types)
    {
        std::vector<llvm::Type *> result;
        result.reserve(types.size());

        for (const auto &type : types)
        {
            result.push_back(map(type));
        }

        return result;
    }

    llvm::FunctionType *TypeMapper2::map_function(TypeRef return_type,
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

        for (const auto &param : param_types)
        {
            llvm::Type *mapped = map(param);
            if (mapped)
            {
                params.push_back(mapped);
            }
            else
            {
                // Fallback to pointer for failed mappings
                params.push_back(ptr_type());
            }
        }

        return llvm::FunctionType::get(ret, params, is_variadic);
    }

    // ========================================================================
    // Primitive Type Accessors
    // ========================================================================

    llvm::Type *TypeMapper2::void_type()
    {
        return llvm::Type::getVoidTy(_llvm_ctx);
    }

    llvm::IntegerType *TypeMapper2::bool_type()
    {
        return llvm::Type::getInt1Ty(_llvm_ctx);
    }

    llvm::IntegerType *TypeMapper2::char_type()
    {
        return llvm::Type::getInt8Ty(_llvm_ctx);
    }

    llvm::IntegerType *TypeMapper2::i8_type()
    {
        return llvm::Type::getInt8Ty(_llvm_ctx);
    }

    llvm::IntegerType *TypeMapper2::i16_type()
    {
        return llvm::Type::getInt16Ty(_llvm_ctx);
    }

    llvm::IntegerType *TypeMapper2::i32_type()
    {
        return llvm::Type::getInt32Ty(_llvm_ctx);
    }

    llvm::IntegerType *TypeMapper2::i64_type()
    {
        return llvm::Type::getInt64Ty(_llvm_ctx);
    }

    llvm::IntegerType *TypeMapper2::i128_type()
    {
        return llvm::Type::getInt128Ty(_llvm_ctx);
    }

    llvm::IntegerType *TypeMapper2::int_type(unsigned bits)
    {
        return llvm::Type::getIntNTy(_llvm_ctx, bits);
    }

    llvm::Type *TypeMapper2::f32_type()
    {
        return llvm::Type::getFloatTy(_llvm_ctx);
    }

    llvm::Type *TypeMapper2::f64_type()
    {
        return llvm::Type::getDoubleTy(_llvm_ctx);
    }

    llvm::PointerType *TypeMapper2::string_type()
    {
        return llvm::PointerType::get(i8_type(), 0);
    }

    llvm::PointerType *TypeMapper2::ptr_type()
    {
        return llvm::PointerType::getUnqual(_llvm_ctx);
    }

    // ========================================================================
    // Type Utilities
    // ========================================================================

    uint64_t TypeMapper2::size_of(llvm::Type *type)
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

    uint64_t TypeMapper2::align_of(llvm::Type *type)
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

    llvm::Constant *TypeMapper2::null_value(llvm::Type *type)
    {
        if (!type)
            return nullptr;
        return llvm::Constant::getNullValue(type);
    }

    bool TypeMapper2::is_signed(TypeRef type)
    {
        if (!type.is_valid())
            return false;

        const Type *t = type.get();
        if (t->kind() != TypeKind::Int)
            return false;

        auto *int_type = static_cast<const IntType *>(t);
        return int_type->is_signed();
    }

    bool TypeMapper2::is_float(llvm::Type *type)
    {
        return type && (type->isFloatTy() || type->isDoubleTy() ||
                        type->isHalfTy() || type->isFP128Ty());
    }

    // ========================================================================
    // Struct Type Management
    // ========================================================================

    llvm::StructType *TypeMapper2::get_or_create_struct(const std::string &name)
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

    void TypeMapper2::register_struct(const std::string &name, llvm::StructType *type)
    {
        if (type)
        {
            _struct_cache[name] = type;
        }
    }

    llvm::StructType *TypeMapper2::lookup_struct(const std::string &name)
    {
        auto it = _struct_cache.find(name);
        return (it != _struct_cache.end()) ? it->second : nullptr;
    }

    bool TypeMapper2::has_struct(const std::string &name)
    {
        return _struct_cache.find(name) != _struct_cache.end();
    }

    void TypeMapper2::complete_struct(llvm::StructType *st,
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

    void TypeMapper2::clear_cache()
    {
        _type_cache.clear();
        _struct_cache.clear();
    }

    void TypeMapper2::cache_type(TypeRef type, llvm::Type *llvm_type)
    {
        if (type.is_valid() && llvm_type)
        {
            _type_cache[type.id()] = llvm_type;
        }
    }

    llvm::Type *TypeMapper2::lookup_cached(TypeRef type)
    {
        if (!type.is_valid())
            return nullptr;
        return lookup_cached(type.id());
    }

    llvm::Type *TypeMapper2::lookup_cached(TypeID id)
    {
        auto it = _type_cache.find(id);
        return (it != _type_cache.end()) ? it->second : nullptr;
    }

    // ========================================================================
    // Primitive Type Mapping
    // ========================================================================

    llvm::Type *TypeMapper2::map_void(const VoidType *)
    {
        return void_type();
    }

    llvm::Type *TypeMapper2::map_bool(const BoolType *)
    {
        return bool_type();
    }

    llvm::Type *TypeMapper2::map_int(const IntType *type)
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

    llvm::Type *TypeMapper2::map_float(const FloatType *type)
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

    llvm::Type *TypeMapper2::map_char(const CharType *)
    {
        return char_type();
    }

    llvm::Type *TypeMapper2::map_string(const StringType *)
    {
        return string_type();
    }

    llvm::Type *TypeMapper2::map_never(const NeverType *)
    {
        // Never type doesn't have a runtime representation
        // Using void as placeholder - code paths should never reach here
        return void_type();
    }

    // ========================================================================
    // Compound Type Mapping
    // ========================================================================

    llvm::Type *TypeMapper2::map_pointer(const PointerType *)
    {
        // LLVM 15+ uses opaque pointers
        return ptr_type();
    }

    llvm::Type *TypeMapper2::map_reference(const ReferenceType *)
    {
        // References are represented as pointers
        return ptr_type();
    }

    llvm::Type *TypeMapper2::map_array(const ArrayType *type)
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

        // Arrays in Cryo are represented as struct { T* elements, u64 length, u64 capacity }
        std::string array_name = "Array<" + elem_ref.get()->display_name() + ">";

        auto it = _struct_cache.find(array_name);
        if (it != _struct_cache.end())
        {
            return it->second;
        }

        std::vector<llvm::Type *> fields = {
            ptr_type(),  // elements: T*
            i64_type(),  // length: u64
            i64_type()   // capacity: u64
        };

        llvm::StructType *array_struct = llvm::StructType::create(
            _llvm_ctx, fields, array_name);

        _struct_cache[array_name] = array_struct;
        return array_struct;
    }

    llvm::Type *TypeMapper2::map_function_type(const FunctionType *type)
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

    llvm::Type *TypeMapper2::map_tuple(const TupleType *type)
    {
        if (!type)
            return nullptr;

        std::vector<llvm::Type *> fields;
        std::ostringstream name_stream;
        name_stream << "tuple(";

        bool first = true;
        for (const auto &elem : type->elements())
        {
            llvm::Type *mapped = map(elem);
            if (mapped)
            {
                fields.push_back(mapped);
            }
            else
            {
                fields.push_back(ptr_type()); // Fallback
            }

            if (!first)
                name_stream << ",";
            first = false;
            name_stream << elem.get()->display_name();
        }
        name_stream << ")";

        std::string tuple_name = name_stream.str();

        // Check cache
        auto it = _struct_cache.find(tuple_name);
        if (it != _struct_cache.end())
        {
            return it->second;
        }

        llvm::StructType *tuple_struct = llvm::StructType::create(
            _llvm_ctx, fields, tuple_name);

        _struct_cache[tuple_name] = tuple_struct;
        return tuple_struct;
    }

    llvm::Type *TypeMapper2::map_optional(const OptionalType *type)
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

    llvm::Type *TypeMapper2::map_struct(const StructType *type)
    {
        if (!type)
            return nullptr;

        std::string name = type->qualified_name().full_name();

        auto existing = lookup_struct(name);
        if (existing && !existing->isOpaque())
        {
            return existing;
        }

        llvm::StructType *st = existing ? existing : get_or_create_struct(name);

        if (!st->isOpaque())
        {
            return st;
        }

        // Try to complete with field information
        const auto &fields = type->fields();
        if (!fields.empty())
        {
            std::vector<llvm::Type *> llvm_fields;
            llvm_fields.reserve(fields.size());

            for (const auto &field : fields)
            {
                llvm::Type *mapped = map(field.type);
                if (mapped)
                {
                    llvm_fields.push_back(mapped);
                }
                else
                {
                    llvm_fields.push_back(ptr_type()); // Fallback
                }
            }

            st->setBody(llvm_fields);
        }

        return st;
    }

    llvm::Type *TypeMapper2::map_class(const ClassType *type)
    {
        if (!type)
            return nullptr;

        std::string name = type->qualified_name().full_name();

        auto existing = lookup_struct(name);
        if (existing && !existing->isOpaque())
        {
            return existing;
        }

        llvm::StructType *st = existing ? existing : get_or_create_struct(name);

        if (!st->isOpaque())
        {
            return st;
        }

        // Try to complete with field information
        const auto &fields = type->fields();
        if (!fields.empty())
        {
            std::vector<llvm::Type *> llvm_fields;
            llvm_fields.reserve(fields.size());

            // TODO: Add vtable pointer if class has virtual methods

            for (const auto &field : fields)
            {
                llvm::Type *mapped = map(field.type);
                if (mapped)
                {
                    llvm_fields.push_back(mapped);
                }
                else
                {
                    llvm_fields.push_back(ptr_type()); // Fallback
                }
            }

            st->setBody(llvm_fields);
        }

        return st;
    }

    llvm::Type *TypeMapper2::map_enum(const EnumType *type)
    {
        if (!type)
            return nullptr;

        std::string name = type->qualified_name().full_name();

        // Simple enum (no data) -> integer
        if (type->is_simple())
        {
            return i32_type();
        }

        // Complex enum (tagged union)
        auto existing = lookup_struct(name);
        if (existing && !existing->isOpaque())
        {
            return existing;
        }

        // Calculate payload size from variants
        size_t max_payload = 0;
        for (const auto &variant : type->variants())
        {
            size_t variant_size = 0;
            for (const auto &field : variant.fields)
            {
                llvm::Type *mapped = map(field.type);
                if (mapped)
                {
                    variant_size += size_of(mapped);
                }
            }
            max_payload = std::max(max_payload, variant_size);
        }

        // Ensure minimum payload size
        max_payload = std::max(max_payload, static_cast<size_t>(8));

        return create_tagged_union(name, 4, max_payload);
    }

    llvm::Type *TypeMapper2::map_trait(const TraitType *type)
    {
        if (!type)
            return nullptr;

        // Traits are represented as vtable pointer + data pointer (fat pointer)
        std::string name = "trait." + type->qualified_name().full_name();

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

    llvm::Type *TypeMapper2::map_type_alias(const TypeAliasType *type)
    {
        if (!type)
            return nullptr;

        TypeRef target = type->target();
        if (!target.is_valid())
        {
            report_error("Type alias has no target: " + type->display_name());
            return nullptr;
        }

        return map(target);
    }

    // ========================================================================
    // Generic Type Mapping
    // ========================================================================

    llvm::Type *TypeMapper2::map_generic_param(const GenericParamType *type)
    {
        if (!type)
            return nullptr;

        // Generic parameters should be substituted before codegen
        // Return opaque pointer as fallback
        report_error("Unsubstituted generic parameter: " + type->display_name());
        return ptr_type();
    }

    llvm::Type *TypeMapper2::map_bounded_param(const BoundedParamType *type)
    {
        if (!type)
            return nullptr;

        // Bounded parameters should be substituted before codegen
        report_error("Unsubstituted bounded parameter: " + type->display_name());
        return ptr_type();
    }

    llvm::Type *TypeMapper2::map_instantiated(const InstantiatedType *type)
    {
        if (!type)
            return nullptr;

        std::string name = type->display_name();

        // Check if already exists
        auto existing = lookup_struct(name);
        if (existing && !existing->isOpaque())
        {
            return existing;
        }

        // Try instantiation callback
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

        // Fallback: create opaque struct
        llvm::StructType *st = existing ? existing : get_or_create_struct(name);
        return st;
    }

    llvm::Type *TypeMapper2::map_error(const ErrorType *type)
    {
        // Error types indicate compilation failure - use void as placeholder
        if (type)
        {
            report_error("Mapping error type: " + type->reason());
        }
        return void_type();
    }

    // ========================================================================
    // Helpers
    // ========================================================================

    llvm::StructType *TypeMapper2::create_tagged_union(const std::string &name,
                                                        size_t discriminant_size,
                                                        size_t payload_size)
    {
        auto existing = lookup_struct(name);
        if (existing && !existing->isOpaque())
        {
            return existing;
        }

        llvm::StructType *st = existing ? existing : get_or_create_struct(name);

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
        st->setBody(fields, false);

        return st;
    }

    void TypeMapper2::report_error(const std::string &msg)
    {
        _has_error = true;
        _last_error = msg;
    }

    // ========================================================================
    // Backward Compatibility Methods
    // ========================================================================

    llvm::Type *TypeMapper2::get_type(const std::string &name)
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
            llvm::StructType *st = _module->getTypeByName(name);
            if (st)
            {
                _struct_cache[name] = st;
                return st;
            }
        }

        // Try primitive type names
        if (name == "void")
            return void_type();
        if (name == "bool")
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

        return nullptr;
    }

    llvm::Type *TypeMapper2::resolve_and_map(const std::string &name)
    {
        // Try get_type first
        llvm::Type *result = get_type(name);
        if (result)
            return result;

        // Create opaque struct as fallback
        return get_or_create_struct(name);
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

} // namespace Cryo
