/******************************************************************************
 * Copyright 2025 Jacob LeQuire
 * SPDX-License-Identifier: Apache-2.0
 *
 * TypeMapper Implementation - Maps CryoLang types to LLVM IR types
 *****************************************************************************/

#include "Codegen/TypeMapper.hpp"
#include "Utils/Logger.hpp"

#include <llvm/IR/Constants.h>
#include <sstream>

namespace Cryo::Codegen
{
    //===================================================================
    // Construction
    //===================================================================

    TypeMapper::TypeMapper(LLVMContextManager &llvm, Cryo::TypeContext *types)
        : _llvm(llvm), _types(types)
    {
        if (!_types)
        {
            report_error("TypeContext is required for TypeMapper");
        }
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "TypeMapper initialized");
    }

    //===================================================================
    // Primary Interface
    //===================================================================

    llvm::Type *TypeMapper::map(Cryo::Type *cryo_type)
    {
        if (!cryo_type)
        {
            report_error("Cannot map null type");
            return nullptr;
        }

        // Check cache first
        auto cached = lookup_cached(cryo_type);
        if (cached)
        {
            return cached;
        }

        TypeKind kind = cryo_type->kind();
        llvm::Type *result = nullptr;

        LOG_TRACE(Cryo::LogComponent::CODEGEN, "TypeMapper::map - mapping type kind: {}",
                  TypeKindToString(kind));

        switch (kind)
        {
        case TypeKind::Void:
            result = void_type();
            break;

        case TypeKind::Boolean:
            result = bool_type();
            break;

        case TypeKind::Integer:
            result = map_integer(static_cast<IntegerType *>(cryo_type));
            break;

        case TypeKind::Float:
            result = map_float(static_cast<FloatType *>(cryo_type));
            break;

        case TypeKind::Char:
            result = char_type();
            break;

        case TypeKind::String:
            result = string_type();
            break;

        case TypeKind::Array:
            result = map_array(static_cast<ArrayType *>(cryo_type));
            break;

        case TypeKind::Pointer:
            result = map_pointer(static_cast<PointerType *>(cryo_type));
            break;

        case TypeKind::Reference:
            result = map_reference(static_cast<ReferenceType *>(cryo_type));
            break;

        case TypeKind::Function:
            result = map_function_type(static_cast<FunctionType *>(cryo_type));
            break;

        case TypeKind::Struct:
            result = map_struct(static_cast<StructType *>(cryo_type));
            break;

        case TypeKind::Class:
            result = map_class(static_cast<ClassType *>(cryo_type));
            break;

        case TypeKind::Enum:
            result = map_enum(static_cast<EnumType *>(cryo_type));
            break;

        case TypeKind::Parameterized:
            result = map_parameterized(static_cast<ParameterizedType *>(cryo_type));
            break;

        case TypeKind::Optional:
            result = map_optional(static_cast<OptionalType *>(cryo_type));
            break;

        case TypeKind::Null:
            // Null is represented as opaque pointer
            result = ptr_type();
            break;

        case TypeKind::TypeAlias:
        {
            auto alias = static_cast<TypeAlias *>(cryo_type);
            if (alias && alias->target_type())
            {
                result = map(alias->target_type());
            }
            else
            {
                report_error("Invalid type alias: " + cryo_type->name());
            }
            break;
        }

        case TypeKind::Generic:
            // Generic type parameters should be instantiated before codegen
            // Use opaque pointer as fallback
            LOG_WARN(Cryo::LogComponent::CODEGEN, "Generic type '{}' not instantiated, using ptr",
                     cryo_type->name());
            result = ptr_type();
            break;

        case TypeKind::Auto:
        case TypeKind::Unknown:
            report_error("Type inference should resolve Auto/Unknown before codegen");
            return nullptr;

        case TypeKind::Variadic:
            report_error("Variadic type cannot be mapped to concrete LLVM type");
            return nullptr;

        default:
            report_error("Unsupported type kind: " + std::to_string(static_cast<int>(kind)));
            return nullptr;
        }

        // Cache the result
        if (result)
        {
            cache_type(cryo_type, result);
        }

        return result;
    }

    llvm::Type *TypeMapper::get_type(const std::string &name)
    {
        // Handle pointer types (names ending with '*')
        if (!name.empty() && name.back() == '*')
        {
            // All pointer types in LLVM 15+ are opaque pointers
            return ptr_type();
        }

        // Handle primitive types by name
        if (name == "void")
            return void_type();
        if (name == "bool" || name == "boolean")
            return bool_type();
        if (name == "i8" || name == "char")
            return i8_type();
        if (name == "i16")
            return i16_type();
        if (name == "i32" || name == "int")
            return i32_type();
        if (name == "i64")
            return i64_type();
        if (name == "i128")
            return i128_type();
        if (name == "u8")
            return i8_type();
        if (name == "u16")
            return i16_type();
        if (name == "u32")
            return i32_type();
        if (name == "u64")
            return i64_type();
        if (name == "u128")
            return i128_type();
        if (name == "f32" || name == "float")
            return f32_type();
        if (name == "f64" || name == "double")
            return f64_type();
        if (name == "string")
            return string_type();

        // Check struct cache first
        auto *st = lookup_struct(name);
        if (st)
        {
            return st;
        }

        // Try to resolve from TypeContext
        if (_types)
        {
            // Try struct type
            Type *cryo_type = _types->lookup_struct_type(name);
            if (cryo_type)
            {
                return map(cryo_type);
            }

            // Try class type
            cryo_type = _types->get_class_type(name);
            if (cryo_type && cryo_type->kind() != TypeKind::Unknown)
            {
                return map(cryo_type);
            }

            // Try enum type
            cryo_type = _types->lookup_enum_type(name);
            if (cryo_type)
            {
                return map(cryo_type);
            }
        }

        return nullptr;
    }

    llvm::FunctionType *TypeMapper::map_function(
        Cryo::Type *return_type,
        const std::vector<Cryo::Type *> &param_types,
        bool is_variadic)
    {
        // Map return type
        llvm::Type *ret_type = return_type ? map(return_type) : void_type();
        if (!ret_type)
        {
            ret_type = void_type();
        }

        // Map parameter types
        std::vector<llvm::Type *> params;
        params.reserve(param_types.size());

        for (auto *param : param_types)
        {
            llvm::Type *mapped = map(param);
            if (mapped)
            {
                params.push_back(mapped);
            }
            else
            {
                LOG_WARN(Cryo::LogComponent::CODEGEN, "Failed to map parameter type, using ptr");
                params.push_back(ptr_type());
            }
        }

        return llvm::FunctionType::get(ret_type, params, is_variadic);
    }

    //===================================================================
    // Primitive Type Accessors
    //===================================================================

    llvm::Type *TypeMapper::void_type()
    {
        return llvm::Type::getVoidTy(llvm_ctx());
    }

    llvm::IntegerType *TypeMapper::bool_type()
    {
        return llvm::Type::getInt1Ty(llvm_ctx());
    }

    llvm::IntegerType *TypeMapper::char_type()
    {
        return llvm::Type::getInt8Ty(llvm_ctx());
    }

    llvm::IntegerType *TypeMapper::i8_type()
    {
        return llvm::Type::getInt8Ty(llvm_ctx());
    }

    llvm::IntegerType *TypeMapper::i16_type()
    {
        return llvm::Type::getInt16Ty(llvm_ctx());
    }

    llvm::IntegerType *TypeMapper::i32_type()
    {
        return llvm::Type::getInt32Ty(llvm_ctx());
    }

    llvm::IntegerType *TypeMapper::i64_type()
    {
        return llvm::Type::getInt64Ty(llvm_ctx());
    }

    llvm::IntegerType *TypeMapper::i128_type()
    {
        return llvm::Type::getInt128Ty(llvm_ctx());
    }

    llvm::IntegerType *TypeMapper::int_type(unsigned bits)
    {
        return llvm::Type::getIntNTy(llvm_ctx(), bits);
    }

    llvm::Type *TypeMapper::f32_type()
    {
        return llvm::Type::getFloatTy(llvm_ctx());
    }

    llvm::Type *TypeMapper::f64_type()
    {
        return llvm::Type::getDoubleTy(llvm_ctx());
    }

    llvm::PointerType *TypeMapper::string_type()
    {
        return llvm::PointerType::get(i8_type(), 0);
    }

    llvm::PointerType *TypeMapper::ptr_type()
    {
        return llvm::PointerType::getUnqual(llvm_ctx());
    }

    //===================================================================
    // Type Utilities
    //===================================================================

    uint64_t TypeMapper::size_of(llvm::Type *type)
    {
        if (!type || !module())
            return 0;

        const llvm::DataLayout &layout = module()->getDataLayout();
        return layout.getTypeAllocSize(type);
    }

    uint64_t TypeMapper::align_of(llvm::Type *type)
    {
        if (!type || !module())
            return 1;

        const llvm::DataLayout &layout = module()->getDataLayout();
        return layout.getABITypeAlign(type).value();
    }

    llvm::Constant *TypeMapper::null_value(llvm::Type *type)
    {
        if (!type)
            return nullptr;
        return llvm::Constant::getNullValue(type);
    }

    bool TypeMapper::is_signed(Cryo::Type *cryo_type)
    {
        if (!cryo_type || cryo_type->kind() != TypeKind::Integer)
            return false;

        auto *int_type = static_cast<IntegerType *>(cryo_type);
        return int_type->is_signed();
    }

    bool TypeMapper::is_float(llvm::Type *type)
    {
        return type && (type->isFloatTy() || type->isDoubleTy() ||
                        type->isHalfTy() || type->isFP128Ty());
    }

    //===================================================================
    // Struct Type Management
    //===================================================================

    llvm::StructType *TypeMapper::get_or_create_struct(const std::string &name)
    {
        auto it = _struct_cache.find(name);
        if (it != _struct_cache.end())
        {
            return it->second;
        }

        // Create opaque struct type
        llvm::StructType *st = llvm::StructType::create(llvm_ctx(), name);
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

    //===================================================================
    // Cache Management
    //===================================================================

    void TypeMapper::clear_cache()
    {
        _cache.clear();
        _struct_cache.clear();
    }

    void TypeMapper::cache_type(Cryo::Type *cryo_type, llvm::Type *llvm_type)
    {
        if (cryo_type && llvm_type)
        {
            _cache[cryo_type] = llvm_type;
        }
    }

    llvm::Type *TypeMapper::lookup_cached(Cryo::Type *cryo_type)
    {
        if (!cryo_type)
            return nullptr;

        auto it = _cache.find(cryo_type);
        return (it != _cache.end()) ? it->second : nullptr;
    }

    //===================================================================
    // Type-Specific Mapping Methods
    //===================================================================

    llvm::Type *TypeMapper::map_integer(Cryo::IntegerType *type)
    {
        if (!type)
            return nullptr;

        IntegerKind kind = type->integer_kind();

        switch (kind)
        {
        case IntegerKind::I8:
        case IntegerKind::U8:
            return i8_type();

        case IntegerKind::I16:
        case IntegerKind::U16:
            return i16_type();

        case IntegerKind::I32:
        case IntegerKind::U32:
        case IntegerKind::Int:
        case IntegerKind::UInt:
            return i32_type();

        case IntegerKind::I64:
        case IntegerKind::U64:
            return i64_type();

        case IntegerKind::I128:
        case IntegerKind::U128:
            return i128_type();

        default:
            // Default to 32-bit
            return i32_type();
        }
    }

    llvm::Type *TypeMapper::map_float(Cryo::FloatType *type)
    {
        if (!type)
            return nullptr;

        FloatKind kind = type->float_kind();

        switch (kind)
        {
        case FloatKind::F32:
            return f32_type();

        case FloatKind::F64:
        case FloatKind::Float:
        default:
            return f64_type();
        }
    }

    llvm::Type *TypeMapper::map_array(Cryo::ArrayType *type)
    {
        if (!type)
            return nullptr;

        // element_type() returns shared_ptr, use .get() for raw pointer
        Type *elem_type = type->element_type().get();
        llvm::Type *llvm_elem = map(elem_type);
        if (!llvm_elem)
        {
            report_error("Failed to map array element type");
            return nullptr;
        }

        // In Cryo, T[] syntax is syntactic sugar for Array<T> which is a struct type:
        // type class Array<T> { elements: T*; length: u64; capacity: u64; }
        // So we always return the Array<T> struct type, NOT native LLVM arrays.

        // Generate a unique name for this Array<T> instantiation
        std::string array_struct_name = "Array<" + (elem_type ? elem_type->to_string() : "unknown") + ">";

        // Check if we already have this struct type cached
        auto it = _struct_cache.find(array_struct_name);
        if (it != _struct_cache.end())
        {
            return it->second;
        }

        // Create the Array<T> struct type: { T* elements, u64 length, u64 capacity }
        std::vector<llvm::Type *> fields = {
            llvm::PointerType::get(llvm_ctx(), 0), // elements: T* (opaque pointer)
            llvm::Type::getInt64Ty(llvm_ctx()),    // length: u64
            llvm::Type::getInt64Ty(llvm_ctx())     // capacity: u64
        };

        llvm::StructType *array_struct = llvm::StructType::create(
            llvm_ctx(), fields, array_struct_name);

        // Cache it for future use
        _struct_cache[array_struct_name] = array_struct;

        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                  "TypeMapper: Created Array<{}> struct type for array syntax",
                  elem_type ? elem_type->to_string() : "unknown");

        return array_struct;
    }

    llvm::Type *TypeMapper::map_pointer(Cryo::PointerType *type)
    {
        if (!type)
            return nullptr;

        // LLVM 15+ uses opaque pointers
        return ptr_type();
    }

    llvm::Type *TypeMapper::map_reference(Cryo::ReferenceType *type)
    {
        if (!type)
            return nullptr;

        // References are represented as pointers
        return ptr_type();
    }

    llvm::Type *TypeMapper::map_function_type(Cryo::FunctionType *type)
    {
        if (!type)
            return nullptr;

        // return_type() returns shared_ptr, use .get() for raw pointer
        Type *ret_type = type->return_type().get();
        const auto &params = type->parameter_types(); // vector of shared_ptr

        llvm::Type *llvm_ret = ret_type ? map(ret_type) : void_type();

        std::vector<llvm::Type *> llvm_params;
        llvm_params.reserve(params.size());

        // params is vector of shared_ptr<Type>
        for (const auto &param : params)
        {
            llvm::Type *mapped = map(param.get());
            if (mapped)
            {
                llvm_params.push_back(mapped);
            }
        }

        llvm::FunctionType *fn_type = llvm::FunctionType::get(
            llvm_ret, llvm_params, type->is_variadic());

        // Return as pointer to function
        return llvm::PointerType::get(fn_type, 0);
    }

    llvm::Type *TypeMapper::map_struct(Cryo::StructType *type)
    {
        if (!type)
            return nullptr;

        std::string name = type->name();
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "TypeMapper::map_struct - {}", name);

        // Check struct cache
        auto existing = lookup_struct(name);
        if (existing && !existing->isOpaque())
        {
            return existing;
        }

        // Get or create opaque struct
        llvm::StructType *st = existing ? existing : get_or_create_struct(name);

        // If already defined, return it
        if (!st->isOpaque())
        {
            return st;
        }

        // Try to get field information from TypeContext
        // The struct body should be set by TypeCodegen which has AST access
        // For now, leave opaque - TypeCodegen will complete it
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "TypeMapper::map_struct - created opaque struct '{}'", name);

        return st;
    }

    llvm::Type *TypeMapper::map_class(Cryo::ClassType *type)
    {
        if (!type)
            return nullptr;

        std::string name = type->name();
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "TypeMapper::map_class - {}", name);

        // Check struct cache (classes use struct types in LLVM)
        auto existing = lookup_struct(name);
        if (existing && !existing->isOpaque())
        {
            return existing;
        }

        // Get or create opaque struct
        llvm::StructType *st = existing ? existing : get_or_create_struct(name);

        // If already defined, return it
        if (!st->isOpaque())
        {
            return st;
        }

        // Leave opaque - TypeCodegen will complete it
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "TypeMapper::map_class - created opaque class '{}'", name);

        return st;
    }

    llvm::Type *TypeMapper::map_enum(Cryo::EnumType *type)
    {
        if (!type)
            return nullptr;

        std::string name = type->name();
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "TypeMapper::map_enum - {}", name);

        // Simple enum (no associated data) -> integer
        if (type->is_simple_enum())
        {
            // Use i32 for discriminant
            return i32_type();
        }

        // Complex enum (tagged union) -> struct
        auto existing = lookup_struct(name);
        if (existing && !existing->isOpaque())
        {
            return existing;
        }

        // Calculate payload size
        size_t payload_size = calculate_enum_payload_size(type);
        size_t discriminant_size = type->get_discriminant_size();

        return create_tagged_union(name, discriminant_size, payload_size);
    }

    llvm::Type *TypeMapper::map_parameterized(Cryo::ParameterizedType *type)
    {
        if (!type)
            return nullptr;

        // Template types should be instantiated before codegen
        if (type->is_template())
        {
            LOG_WARN(Cryo::LogComponent::CODEGEN, "Parameterized template '{}' not instantiated",
                     type->name());
            return ptr_type();
        }

        std::string name = type->get_instantiated_name();
        std::string base_name = type->base_name();
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "TypeMapper::map_parameterized - {} (base: {})", name, base_name);

        // Handle built-in parameterized types specially
        if (base_name == "Array" && !type->type_parameters().empty())
        {
            // Array<T> is represented as a pointer to T (dynamic array)
            // For fixed-size arrays, use the ArrayType directly
            Type *elem_type = type->type_parameters()[0].get();
            llvm::Type *llvm_elem = map(elem_type);
            if (llvm_elem)
            {
                // Dynamic arrays are represented as pointers
                return llvm::PointerType::get(llvm_elem, 0);
            }
        }

        // Check if it's a parameterized enum (Option, Result, etc.)
        auto *enum_type = dynamic_cast<ParameterizedEnumType *>(type);
        if (enum_type)
        {
            // Handle as tagged union
            auto existing = lookup_struct(name);
            if (existing && !existing->isOpaque())
            {
                return existing;
            }

            size_t payload_size = 8; // Default
            size_t discriminant_size = enum_type->get_discriminant_size();

            // Calculate actual payload size from type parameters
            // type_parameters() returns vector of shared_ptr
            for (const auto &param : type->type_parameters())
            {
                llvm::Type *mapped = map(param.get());
                if (mapped)
                {
                    payload_size = std::max(payload_size, size_of(mapped));
                }
            }

            return create_tagged_union(name, discriminant_size, payload_size);
        }

        // Generic struct/class instantiation
        // First check if it already exists and is complete
        auto existing = lookup_struct(name);
        if (existing && !existing->isOpaque())
        {
            return existing;
        }

        // Also check LLVM context directly
        if (llvm::StructType *llvm_existing = llvm::StructType::getTypeByName(llvm_ctx(), name))
        {
            _struct_cache[name] = llvm_existing;
            if (!llvm_existing->isOpaque())
            {
                return llvm_existing;
            }
            existing = llvm_existing;
        }

        // If we have a generic instantiator and type arguments, try to instantiate
        if (_generic_instantiator && !type->type_parameters().empty())
        {
            std::string base_name = type->base_name();

            // Convert shared_ptr types to raw pointers for the callback
            std::vector<Cryo::Type*> type_args;
            type_args.reserve(type->type_parameters().size());
            for (const auto& param : type->type_parameters())
            {
                type_args.push_back(param.get());
            }

            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "TypeMapper::map_parameterized - invoking generic instantiator for {} with {} type args",
                      base_name, type_args.size());

            llvm::StructType* instantiated = _generic_instantiator(base_name, type_args);
            if (instantiated && !instantiated->isOpaque())
            {
                _struct_cache[name] = instantiated;
                return instantiated;
            }
        }

        // Fallback: create opaque struct (will be completed later if possible)
        if (!existing)
        {
            existing = get_or_create_struct(name);
        }

        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                  "TypeMapper::map_parameterized - returning opaque struct for {}", name);
        return existing;
    }

    llvm::Type *TypeMapper::map_optional(Cryo::OptionalType *type)
    {
        if (!type)
            return nullptr;

        // Optional<T> is a tagged union: { i8 discriminant, T value }
        // wrapped_type() returns shared_ptr, use .get() for raw pointer
        Type *inner = type->wrapped_type().get();
        llvm::Type *inner_llvm = map(inner);

        std::string name = "Optional_" + (inner ? inner->name() : "unknown");

        auto existing = lookup_struct(name);
        if (existing && !existing->isOpaque())
        {
            return existing;
        }

        size_t payload_size = inner_llvm ? size_of(inner_llvm) : 8;
        return create_tagged_union(name, 1, payload_size);
    }

    //===================================================================
    // Enum Helpers
    //===================================================================

    llvm::StructType *TypeMapper::create_tagged_union(
        const std::string &name,
        size_t discriminant_size,
        size_t payload_size)
    {
        auto existing = lookup_struct(name);
        if (existing && !existing->isOpaque())
        {
            return existing;
        }

        llvm::StructType *st = existing ? existing : get_or_create_struct(name);

        // Tagged union layout: { discriminant, payload_bytes }
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

        // Payload as byte array
        llvm::ArrayType *payload_type = llvm::ArrayType::get(i8_type(), payload_size);

        std::vector<llvm::Type *> fields = {discriminant_type, payload_type};
        st->setBody(fields, false);

        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                  "Created tagged union '{}': discriminant={} bytes, payload={} bytes",
                  name, discriminant_size, payload_size);

        return st;
    }

    size_t TypeMapper::calculate_enum_payload_size(Cryo::EnumType *enum_type)
    {
        if (!enum_type)
            return 0;

        size_t max_size = 0;

        for (const auto &variant : enum_type->get_variant_metadata())
        {
            if (!variant.has_data)
                continue;

            size_t variant_size = 0;
            for (const auto &field_type : variant.field_types)
            {
                if (field_type)
                {
                    llvm::Type *mapped = map(field_type.get());
                    if (mapped)
                    {
                        variant_size += size_of(mapped);
                    }
                }
            }

            max_size = std::max(max_size, variant_size);
        }

        // Ensure minimum size for pointer-sized payloads
        return std::max(max_size, static_cast<size_t>(8));
    }

    std::vector<llvm::Type *> TypeMapper::get_struct_fields(Cryo::StructDeclarationNode *decl)
    {
        std::vector<llvm::Type *> fields;

        if (!decl)
            return fields;

        for (const auto &field_ptr : decl->fields())
        {
            if (!field_ptr)
                continue;

            auto *resolved = field_ptr->get_resolved_type();
            if (resolved)
            {
                llvm::Type *mapped = map(resolved);
                if (mapped)
                {
                    fields.push_back(mapped);
                }
                else
                {
                    fields.push_back(ptr_type()); // Fallback
                }
            }
            else
            {
                fields.push_back(ptr_type()); // Fallback
            }
        }

        return fields;
    }

    std::vector<llvm::Type *> TypeMapper::get_class_fields(Cryo::ClassDeclarationNode *decl)
    {
        std::vector<llvm::Type *> fields;

        if (!decl)
            return fields;

        // TODO: Add vtable pointer if class has virtual methods

        for (const auto &field_ptr : decl->fields())
        {
            if (!field_ptr)
                continue;

            auto *resolved = field_ptr->get_resolved_type();
            if (resolved)
            {
                llvm::Type *mapped = map(resolved);
                if (mapped)
                {
                    fields.push_back(mapped);
                }
                else
                {
                    fields.push_back(ptr_type()); // Fallback
                }
            }
            else
            {
                fields.push_back(ptr_type()); // Fallback
            }
        }

        return fields;
    }

    //===================================================================
    // Error Reporting
    //===================================================================

    void TypeMapper::report_error(const std::string &msg)
    {
        _has_error = true;
        _last_error = msg;
        LOG_ERROR(Cryo::LogComponent::CODEGEN, "TypeMapper error: {}", msg);
    }

    //===================================================================
    // Utility Functions
    //===================================================================

    std::string mangle_generic_name(
        const std::string &base_name,
        const std::vector<Cryo::Type *> &type_args)
    {
        if (type_args.empty())
            return base_name;

        std::stringstream ss;
        ss << base_name;

        for (auto *arg : type_args)
        {
            ss << "_";
            if (arg)
            {
                ss << arg->name();
            }
            else
            {
                ss << "unknown";
            }
        }

        return ss.str();
    }

} // namespace Cryo::Codegen
