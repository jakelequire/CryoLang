#include "Codegen/TypeMapper.hpp"
#include "AST/ASTNode.hpp"
#include <llvm/IR/Constants.h>
#include <llvm/IR/Type.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/Support/Casting.h>
#include <iostream>

namespace Cryo::Codegen
{

    //===================================================================
    // Construction
    //===================================================================

    TypeMapper::TypeMapper(LLVMContextManager &context_manager)
        : _context_manager(context_manager), _has_errors(false)
    {
        // Initialize common types in cache
        register_type("void", get_void_type());
        register_type("bool", get_boolean_type());
        register_type("boolean", get_boolean_type()); // Support CryoLang keyword
        register_type("char", get_char_type());
        register_type("string", get_string_type());

        // Integer types
        register_type("i8", get_integer_type(8));
        register_type("i16", get_integer_type(16));
        register_type("i32", get_integer_type(32));
        register_type("i64", get_integer_type(64));
        register_type("int", get_integer_type(32)); // Default int

        register_type("u8", get_integer_type(8, false));
        register_type("u16", get_integer_type(16, false));
        register_type("u32", get_integer_type(32, false));
        register_type("u64", get_integer_type(64, false));
        register_type("uint", get_integer_type(32, false)); // Default uint

        // Float types
        register_type("f32", get_float_type(32));
        register_type("f64", get_float_type(64));
        register_type("float", get_float_type(32)); // Default float
    }

    //===================================================================
    // Core Type Mapping Interface
    //===================================================================

    llvm::Type *TypeMapper::map_type(Cryo::Type *cryo_type)
    {
        if (!cryo_type)
        {
            report_error("Cannot map null type");
            return nullptr;
        }

        // Check cache first
        auto cache_it = _cryo_type_cache.find(cryo_type);
        if (cache_it != _cryo_type_cache.end())
        {
            return cache_it->second;
        }

        llvm::Type *llvm_type = nullptr;

        switch (cryo_type->kind())
        {
        case Cryo::TypeKind::Void:
            llvm_type = get_void_type();
            break;
        case Cryo::TypeKind::Boolean:
            llvm_type = get_boolean_type();
            break;
        case Cryo::TypeKind::Integer:
            llvm_type = get_integer_type(32); // Default int for now
            break;
        case Cryo::TypeKind::Float:
            llvm_type = get_float_type(32); // Default float is now 32-bit
            break;
        case Cryo::TypeKind::Char:
            llvm_type = get_char_type();
            break;
        case Cryo::TypeKind::String:
            llvm_type = get_string_type();
            break;
        case Cryo::TypeKind::Array:
            // TODO: Extract array element type and size from cryo_type
            llvm_type = nullptr; // Placeholder for now
            break;
        case Cryo::TypeKind::Pointer:
            // TODO: Extract pointee type from cryo_type
            llvm_type = nullptr; // Placeholder for now
            break;
        case Cryo::TypeKind::Reference:
            // TODO: Extract referenced type from cryo_type
            llvm_type = nullptr; // Placeholder for now
            break;
        case Cryo::TypeKind::Function:
            // TODO: Extract function signature from cryo_type
            llvm_type = nullptr; // Placeholder for now
            break;
        case Cryo::TypeKind::Struct:
            // TODO: Extract struct info from cryo_type
            llvm_type = nullptr; // Placeholder for now
            break;
        default:
            report_error("Unsupported type kind: " + std::to_string(static_cast<int>(cryo_type->kind())));
            return nullptr;
        }

        if (llvm_type)
        {
            _cryo_type_cache[cryo_type] = llvm_type;
        }

        return llvm_type;
    }

    llvm::Type *TypeMapper::map_type(const std::string &type_name)
    {
        return lookup_type(type_name);
    }

    llvm::FunctionType *TypeMapper::map_function_type(
        Cryo::Type *return_type,
        const std::vector<Cryo::Type *> &param_types,
        bool is_variadic)
    {
        llvm::Type *ret_type = map_type(return_type);
        if (!ret_type)
        {
            return nullptr;
        }

        std::vector<llvm::Type *> llvm_param_types;
        for (auto param_type : param_types)
        {
            llvm::Type *llvm_param = map_type(param_type);
            if (!llvm_param)
            {
                return nullptr;
            }
            llvm_param_types.push_back(llvm_param);
        }

        return llvm::FunctionType::get(ret_type, llvm_param_types, is_variadic);
    }

    //===================================================================
    // Primitive Type Mapping
    //===================================================================

    llvm::IntegerType *TypeMapper::get_integer_type(int bit_width, bool is_signed)
    {
        auto &llvm_context = _context_manager.get_context();
        return llvm::Type::getIntNTy(llvm_context, bit_width);
    }

    llvm::Type *TypeMapper::get_float_type(int precision)
    {
        auto &llvm_context = _context_manager.get_context();

        switch (precision)
        {
        case 32:
            return llvm::Type::getFloatTy(llvm_context);
        case 64:
            return llvm::Type::getDoubleTy(llvm_context);
        default:
            report_error("Unsupported float precision: " + std::to_string(precision));
            return nullptr;
        }
    }

    llvm::IntegerType *TypeMapper::get_boolean_type()
    {
        auto &llvm_context = _context_manager.get_context();
        return llvm::Type::getInt1Ty(llvm_context);
    }

    llvm::IntegerType *TypeMapper::get_char_type()
    {
        auto &llvm_context = _context_manager.get_context();
        return llvm::Type::getInt8Ty(llvm_context);
    }

    llvm::PointerType *TypeMapper::get_string_type()
    {
        auto &llvm_context = _context_manager.get_context();
        return llvm::PointerType::get(llvm::Type::getInt8Ty(llvm_context), 0);
    }

    llvm::Type *TypeMapper::get_void_type()
    {
        auto &llvm_context = _context_manager.get_context();
        return llvm::Type::getVoidTy(llvm_context);
    }

    //===================================================================
    // Complex Type Mapping
    //===================================================================

    llvm::Type *TypeMapper::map_array_type(Cryo::Type *element_type, size_t size, bool is_dynamic)
    {
        llvm::Type *llvm_element_type = map_type(element_type);
        if (!llvm_element_type)
        {
            return nullptr;
        }

        if (is_dynamic || size == 0)
        {
            // Dynamic arrays are represented as pointers
            return llvm::PointerType::get(llvm_element_type, 0);
        }
        else
        {
            // Fixed-size arrays
            return llvm::ArrayType::get(llvm_element_type, size);
        }
    }

    llvm::StructType *TypeMapper::map_struct_type(Cryo::StructDeclarationNode *struct_decl)
    {
        if (!struct_decl)
        {
            report_error("Cannot map null struct declaration");
            return nullptr;
        }

        const std::string &struct_name = struct_decl->name();

        // Check if already in cache
        auto cache_it = _struct_cache.find(struct_name);
        if (cache_it != _struct_cache.end())
        {
            return cache_it->second;
        }

        // Create opaque struct first for forward declarations
        auto &llvm_context = _context_manager.get_context();
        llvm::StructType *struct_type = llvm::StructType::create(llvm_context, struct_name);
        _struct_cache[struct_name] = struct_type;

        // Generate field types
        std::vector<llvm::Type *> field_types = generate_struct_fields(struct_decl);
        if (has_errors())
        {
            return nullptr;
        }

        // Set the struct body
        struct_type->setBody(field_types);

        // Register in main type cache
        register_type(struct_name, struct_type);

        return struct_type;
    }

    llvm::PointerType *TypeMapper::map_pointer_type(Cryo::Type *pointee_type)
    {
        llvm::Type *llvm_pointee = map_type(pointee_type);
        if (!llvm_pointee)
        {
            return nullptr;
        }

        return llvm::PointerType::get(llvm_pointee, 0);
    }

    llvm::PointerType *TypeMapper::map_reference_type(Cryo::Type *referenced_type)
    {
        // References are implemented as pointers in LLVM
        return map_pointer_type(referenced_type);
    }

    //===================================================================
    // Type Information and Utilities
    //===================================================================

    size_t TypeMapper::get_type_size(llvm::Type *llvm_type)
    {
        if (!llvm_type)
        {
            return 0;
        }

        // Use target machine data layout if available
        if (auto *target_machine = _context_manager.get_target_machine())
        {
            auto data_layout = target_machine->createDataLayout();
            return data_layout.getTypeStoreSize(llvm_type).getFixedValue();
        }

        // Fallback to basic size estimation
        if (llvm_type->isIntegerTy())
        {
            return (llvm_type->getIntegerBitWidth() + 7) / 8; // Round up to bytes
        }
        else if (llvm_type->isFloatTy())
        {
            return 4;
        }
        else if (llvm_type->isDoubleTy())
        {
            return 8;
        }
        else if (llvm_type->isPointerTy())
        {
            return sizeof(void *);
        }

        return 1; // Default
    }

    size_t TypeMapper::get_type_alignment(llvm::Type *llvm_type)
    {
        if (!llvm_type)
        {
            return 1;
        }

        return _context_manager.get_type_alignment(llvm_type);
    }

    bool TypeMapper::is_signed_integer(llvm::Type *type)
    {
        // LLVM doesn't distinguish signed/unsigned at the type level
        // This information needs to be tracked separately
        return type->isIntegerTy() && type->getIntegerBitWidth() > 1;
    }

    bool TypeMapper::is_floating_point(llvm::Type *type)
    {
        return type->isFloatingPointTy();
    }

    bool TypeMapper::requires_heap_allocation(llvm::Type *type)
    {
        if (!type)
            return false;

        // Large structs and arrays might need heap allocation
        size_t type_size = get_type_size(type);
        const size_t STACK_THRESHOLD = 1024; // 1KB threshold

        return type_size > STACK_THRESHOLD;
    }

    llvm::Constant *TypeMapper::get_default_value(llvm::Type *type)
    {
        if (!type)
        {
            return nullptr;
        }

        if (type->isVoidTy())
        {
            return nullptr;
        }
        else if (type->isIntegerTy())
        {
            return llvm::ConstantInt::get(type, 0);
        }
        else if (type->isFloatingPointTy())
        {
            return llvm::ConstantFP::get(type, 0.0);
        }
        else if (type->isPointerTy())
        {
            return llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(type));
        }
        else if (type->isStructTy())
        {
            return llvm::ConstantStruct::getNullValue(llvm::cast<llvm::StructType>(type));
        }
        else if (type->isArrayTy())
        {
            return llvm::ConstantArray::getNullValue(llvm::cast<llvm::ArrayType>(type));
        }

        return llvm::UndefValue::get(type);
    }

    //===================================================================
    // Type Cache Management
    //===================================================================

    void TypeMapper::register_type(const std::string &name, llvm::Type *llvm_type)
    {
        if (llvm_type)
        {
            _type_cache[name] = llvm_type;
        }
    }

    llvm::Type *TypeMapper::lookup_type(const std::string &name)
    {
        auto it = _type_cache.find(name);
        if (it != _type_cache.end())
        {
            return it->second;
        }

        // Try to parse array types like "int[]", "string[][]"
        if (name.back() == ']')
        {
            return parse_array_type_from_string(name);
        }

        return nullptr;
    }

    bool TypeMapper::has_type(const std::string &name)
    {
        return _type_cache.find(name) != _type_cache.end();
    }

    void TypeMapper::clear_cache()
    {
        _type_cache.clear();
        _cryo_type_cache.clear();
        _struct_cache.clear();
        _has_errors = false;
        _last_error.clear();
    }

    //===================================================================
    // Private Implementation
    //===================================================================

    llvm::Type *TypeMapper::map_primitive_type(Cryo::TypeKind kind)
    {
        switch (kind)
        {
        case Cryo::TypeKind::Void:
            return get_void_type();
        case Cryo::TypeKind::Boolean:
            return get_boolean_type();
        case Cryo::TypeKind::Integer:
            return get_integer_type(32); // Default int
        case Cryo::TypeKind::Float:
            return get_float_type(32); // Default float is 32-bit
        case Cryo::TypeKind::Char:
            return get_char_type();
        case Cryo::TypeKind::String:
            return get_string_type();
        default:
            return nullptr;
        }
    }

    std::vector<llvm::Type *> TypeMapper::generate_struct_fields(Cryo::StructDeclarationNode *struct_decl)
    {
        std::vector<llvm::Type *> field_types;

        for (const auto &field : struct_decl->fields())
        {
            // Map field type based on type annotation
            llvm::Type *field_type = map_type(field->type_annotation());
            if (!field_type)
            {
                report_error("Failed to map struct field type: " + field->type_annotation());
                return {};
            }
            field_types.push_back(field_type);
        }

        return field_types;
    }

    std::vector<llvm::Type *> TypeMapper::generate_class_fields(Cryo::ClassDeclarationNode *class_decl)
    {
        std::vector<llvm::Type *> field_types;

        // TODO: Add vtable pointer if class has virtual methods
        // field_types.push_back(get_vtable_type());

        for (const auto &field : class_decl->fields())
        {
            llvm::Type *field_type = map_type(field->type_annotation());
            if (!field_type)
            {
                report_error("Failed to map class field type: " + field->type_annotation());
                return {};
            }
            field_types.push_back(field_type);
        }

        return field_types;
    }

    llvm::StructType *TypeMapper::create_opaque_struct(const std::string &name)
    {
        auto &llvm_context = _context_manager.get_context();
        return llvm::StructType::create(llvm_context, name);
    }

    void TypeMapper::report_error(const std::string &message)
    {
        _has_errors = true;
        _last_error = message;
        std::cerr << "[TypeMapper Error] " << message << std::endl;
    }

    //===================================================================
    // Helper Methods - TODO: Implement when Cryo::Type structure is complete
    //===================================================================

    llvm::Type *TypeMapper::parse_array_type_from_string(const std::string &name)
    {
        // Parse array notation like "int[]", "string[][]"
        size_t bracket_pos = name.find('[');
        if (bracket_pos == std::string::npos)
        {
            return nullptr;
        }

        std::string element_type_name = name.substr(0, bracket_pos);
        llvm::Type *element_type = lookup_type(element_type_name);
        if (!element_type)
        {
            return nullptr;
        }

        // Count bracket pairs to determine array dimensions
        size_t dimensions = 0;
        for (size_t i = bracket_pos; i < name.length(); i += 2)
        {
            if (i + 1 < name.length() && name[i] == '[' && name[i + 1] == ']')
            {
                dimensions++;
            }
            else
            {
                break;
            }
        }

        // Create multi-dimensional array type (represented as pointers)
        llvm::Type *result = element_type;
        for (size_t i = 0; i < dimensions; i++)
        {
            result = llvm::PointerType::get(result, 0);
        }

        return result;
    }

    // These methods would extract specific type information from Cryo::Type objects
    // For now they are placeholders until the full Type system integration

    //=======================================================================
    // Utility Functions
    //=======================================================================

    llvm::CallingConv::ID map_calling_convention(const std::string &cryo_cc)
    {
        if (cryo_cc == "cdecl" || cryo_cc.empty())
        {
            return llvm::CallingConv::C;
        }
        else if (cryo_cc == "stdcall")
        {
            return llvm::CallingConv::X86_StdCall;
        }
        else if (cryo_cc == "fastcall")
        {
            return llvm::CallingConv::X86_FastCall;
        }

        return llvm::CallingConv::C; // Default
    }

    std::string get_generic_mangled_name(const std::string &base_name,
                                         const std::vector<Cryo::Type *> &type_args)
    {
        std::string mangled = base_name + "_";

        for (size_t i = 0; i < type_args.size(); i++)
        {
            if (i > 0)
                mangled += "_";

            // This is a simplified mangling scheme
            // Real implementation would need more sophisticated type name generation
            mangled += "T" + std::to_string(i);
        }

        return mangled;
    }

} // namespace Cryo::Codegen