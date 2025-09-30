#include "Codegen/TypeMapper.hpp"
#include "AST/ASTNode.hpp"
#include <llvm/IR/Constants.h>
#include <llvm/IR/Type.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/Support/Casting.h>
#include <iostream>
#include <set>
#include <sstream>

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

        // Initialize built-in generic types
        initialize_builtin_generic_types();
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
        {
            // Cast to PointerType to access pointee_type
            auto pointer_type = static_cast<Cryo::PointerType *>(cryo_type);
            llvm::Type *pointee_llvm_type = map_type(pointer_type->pointee_type().get());
            if (pointee_llvm_type)
            {
                llvm_type = llvm::PointerType::get(pointee_llvm_type, 0);
            }
            break;
        }
        case Cryo::TypeKind::Reference:
        {
            // Cast to ReferenceType to access referent_type
            auto reference_type = static_cast<Cryo::ReferenceType *>(cryo_type);
            llvm::Type *referent_llvm_type = map_type(reference_type->referent_type().get());
            if (referent_llvm_type)
            {
                // In LLVM, references are implemented as pointers
                llvm_type = llvm::PointerType::get(referent_llvm_type, 0);
            }
            break;
        }
        case Cryo::TypeKind::Function:
            // TODO: Extract function signature from cryo_type
            llvm_type = nullptr; // Placeholder for now
            break;
        case Cryo::TypeKind::Struct:
            // TODO: Extract struct info from cryo_type
            llvm_type = nullptr; // Placeholder for now
            break;
        case Cryo::TypeKind::Enum:
        {
            // Cast to EnumType to access enum-specific methods
            auto enum_type = static_cast<Cryo::EnumType *>(cryo_type);
            if (enum_type->is_simple_enum())
            {
                llvm_type = get_integer_type(32); // Simple enum -> i32
            }
            else
            {
                llvm_type = create_tagged_union_type(enum_type);
            }
            break;
        }
        case Cryo::TypeKind::Null:
            // Null is represented as a generic pointer type
            llvm_type = llvm::PointerType::get(get_void_type(), 0);
            break;
        case Cryo::TypeKind::Parameterized:
        {
            // Handle parameterized types (generics)
            auto parameterized_type = static_cast<Cryo::ParameterizedType *>(cryo_type);

            if (parameterized_type->base_name() == "Array")
            {
                // For Array<T>, create a struct with data pointer and size
                auto type_args = parameterized_type->type_parameters();
                if (!type_args.empty())
                {
                    llvm::Type *element_type = map_type(type_args[0].get());
                    if (element_type)
                    {
                        std::vector<llvm::Type *> fields = {
                            llvm::PointerType::get(element_type, 0), // data pointer
                            get_integer_type(64)                     // size (u64)
                        };
                        llvm_type = llvm::StructType::create(_context_manager.get_context(), fields,
                                                             parameterized_type->get_instantiated_name());
                    }
                }
            }
            else if (parameterized_type->base_name() == "Option")
            {
                // For Option<T>, create a tagged union
                auto type_args = parameterized_type->type_parameters();
                if (!type_args.empty())
                {
                    llvm::Type *value_type = map_type(type_args[0].get());
                    if (value_type)
                    {
                        std::vector<llvm::Type *> fields = {
                            get_boolean_type(), // has_value flag
                            value_type          // value
                        };
                        llvm_type = llvm::StructType::create(_context_manager.get_context(), fields,
                                                             parameterized_type->get_instantiated_name());
                    }
                }
            }
            else if (parameterized_type->base_name() == "Result")
            {
                // For Result<T,E>, create a tagged union with value/error
                auto type_args = parameterized_type->type_parameters();
                if (type_args.size() >= 2)
                {
                    llvm::Type *value_type = map_type(type_args[0].get());
                    llvm::Type *error_type = map_type(type_args[1].get());
                    if (value_type && error_type)
                    {
                        // For simplicity, create a union using a large enough byte array
                        // This is a simplified approach - in a production compiler you'd want
                        // more sophisticated union handling
                        std::vector<llvm::Type *> fields = {
                            get_boolean_type(),                       // is_ok flag
                            llvm::ArrayType::get(get_char_type(), 64) // union data (64 bytes should be enough for most types)
                        };
                        llvm_type = llvm::StructType::create(_context_manager.get_context(), fields,
                                                             parameterized_type->get_instantiated_name());
                    }
                }
            }
            else
            {
                report_error("Unsupported parameterized type: " + parameterized_type->base_name());
                return nullptr;
            }
            break;
        }
        case Cryo::TypeKind::Variadic:
            // Variadic parameters are special and shouldn't be mapped to concrete LLVM types
            // This should normally be handled by the function signature generation
            report_error("Variadic type should not be mapped to concrete LLVM type");
            return nullptr;
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
        // Check cache first
        llvm::Type *cached_type = lookup_type(type_name);
        if (cached_type)
        {
            return cached_type;
        }

        // Handle pointer types: "int*" -> pointer to int
        if (!type_name.empty() && type_name.back() == '*')
        {
            std::string base_type = type_name.substr(0, type_name.length() - 1);
            // Trim whitespace
            while (!base_type.empty() && std::isspace(base_type.back()))
            {
                base_type.pop_back();
            }

            llvm::Type *pointee_type = map_type(base_type);
            if (pointee_type)
            {
                llvm::Type *pointer_type = llvm::PointerType::get(pointee_type, 0);
                register_type(type_name, pointer_type);
                return pointer_type;
            }
            return nullptr;
        }

        // Handle reference types: "&int" -> pointer to int (references implemented as pointers in LLVM)
        if (!type_name.empty() && type_name.front() == '&')
        {
            std::string base_type = type_name.substr(1);
            // Trim whitespace
            while (!base_type.empty() && std::isspace(base_type.front()))
            {
                base_type.erase(0, 1);
            }

            llvm::Type *referent_type = map_type(base_type);
            if (referent_type)
            {
                llvm::Type *reference_type = llvm::PointerType::get(referent_type, 0);
                register_type(type_name, reference_type);
                return reference_type;
            }
            return nullptr;
        }

        // Check struct cache for registered struct types
        auto struct_it = _struct_cache.find(type_name);
        if (struct_it != _struct_cache.end())
        {
            return struct_it->second;
        }

        // Handle generic type instantiation: "GenericStruct<int>" -> instantiated struct type
        size_t angle_pos = type_name.find('<');
        if (angle_pos != std::string::npos)
        {
            return map_generic_instantiation(type_name);
        }

        // For enum types that aren't cached yet, we need a way to identify them
        // This is a temporary solution - ideally enum types would be registered
        // during enum declaration processing

        return nullptr; // Type not found
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

        // Register field metadata
        register_struct_fields(struct_decl, struct_type);

        return struct_type;
    }

    llvm::StructType *TypeMapper::map_class_type(Cryo::ClassDeclarationNode *class_decl)
    {
        if (!class_decl)
        {
            report_error("Cannot map null class declaration");
            return nullptr;
        }

        const std::string &class_name = class_decl->name();

        // Check if already in cache
        auto cache_it = _struct_cache.find(class_name);
        if (cache_it != _struct_cache.end())
        {
            return cache_it->second;
        }

        // Create opaque struct first for forward declarations
        auto &llvm_context = _context_manager.get_context();
        llvm::StructType *class_type = llvm::StructType::create(llvm_context, class_name);
        _struct_cache[class_name] = class_type;

        // Generate field types (classes are represented as structs in LLVM)
        std::vector<llvm::Type *> field_types = generate_class_fields(class_decl);
        if (has_errors())
        {
            std::cout << "[DEBUG] TypeMapper: has_errors() = true after generate_class_fields for " << class_name << std::endl;
            std::cout << "[DEBUG] TypeMapper: Last error: " << _last_error << std::endl;
            return nullptr;
        }

        // Set the class body
        class_type->setBody(field_types);

        // Register in main type cache
        register_type(class_name, class_type);

        // Register field metadata
        register_class_fields(class_decl, class_type);

        return class_type;
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

    //===================================================================
    // Enum Type Mapping
    //===================================================================

    llvm::Type *TypeMapper::map_enum_type(Cryo::EnumDeclarationNode *enum_decl)
    {
        if (!enum_decl)
        {
            report_error("Cannot map null enum declaration");
            return nullptr;
        }

        // Get the enum name
        std::string enum_name = enum_decl->name();

        // Skip cache lookup to avoid recursion issues
        // TODO: Implement proper caching without recursion

        // Analyze enum variants to determine if simple or complex
        bool is_simple = true;
        size_t max_payload_size = 0;

        for (const auto &variant : enum_decl->variants())
        {
            if (!variant->associated_types().empty())
            {
                is_simple = false;

                // Calculate payload size for this variant
                size_t variant_payload_size = 0;
                for (const auto &type_name : variant->associated_types())
                {
                    // For now, assume each type is 8 bytes (will improve with proper type size calculation)
                    variant_payload_size += 8;
                }
                max_payload_size = std::max(max_payload_size, variant_payload_size);
            }
        }

        llvm::Type *enum_type;
        if (is_simple)
        {
            // Simple enum -> just an integer
            enum_type = get_integer_type(32);
        }
        else
        {
            // Complex enum -> tagged union (discriminant + payload)
            enum_type = create_tagged_union_type(enum_name, max_payload_size);
        }

        // Register in cache
        register_type(enum_name, enum_type);

        return enum_type;
    }

    llvm::Type *TypeMapper::create_tagged_union_type(Cryo::EnumType *enum_type)
    {
        // Create tagged union for complex enum
        std::string enum_name = enum_type->name();
        size_t max_payload_size = 8; // Default for now, should calculate properly

        return create_tagged_union_type(enum_name, max_payload_size);
    }

    llvm::StructType *TypeMapper::create_tagged_union_type(const std::string &name, size_t payload_size)
    {
        auto &llvm_context = _context_manager.get_context();

        // Create struct type: { i32 discriminant, [payload_size x i8] payload }
        std::vector<llvm::Type *> fields;
        fields.push_back(get_integer_type(32));                                // discriminant
        fields.push_back(llvm::ArrayType::get(get_char_type(), payload_size)); // payload

        return llvm::StructType::create(llvm_context, fields, name + "_tagged_union");
    }

    //===================================================================
    // Utility Functions
    //===================================================================

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

    //===================================================================
    // Field Metadata Management Implementation
    //===================================================================

    void TypeMapper::register_field_metadata(const std::string &type_name, const std::string &field_name,
                                             int field_index, llvm::Type *field_type)
    {
        std::cout << "[TypeMapper] Registering field metadata: type=" << type_name << ", field=" << field_name << ", index=" << field_index << "\n";
        FieldInfo info;
        info.field_index = field_index;
        info.field_type = field_type;
        info.field_name = field_name;
        info.struct_type = lookup_type(type_name);

        _field_metadata[type_name][field_name] = info;
    }

    std::optional<TypeMapper::FieldInfo> TypeMapper::get_field_info(llvm::Type *llvm_type, const std::string &field_name)
    {
        // First, try to find the type name from the LLVM type
        auto type_name_it = _llvm_type_to_name_map.find(llvm_type);
        if (type_name_it != _llvm_type_to_name_map.end())
        {
            const std::string &type_name = type_name_it->second;
            auto type_it = _field_metadata.find(type_name);
            if (type_it != _field_metadata.end())
            {
                auto field_it = type_it->second.find(field_name);
                if (field_it != type_it->second.end())
                {
                    return field_it->second;
                }
            }
        }

        // Fallback: search through all registered types
        for (const auto &[type_name, fields] : _field_metadata)
        {
            llvm::Type *registered_type = lookup_type(type_name);
            if (registered_type == llvm_type)
            {
                auto field_it = fields.find(field_name);
                if (field_it != fields.end())
                {
                    return field_it->second;
                }
            }
        }

        return std::nullopt;
    }

    int TypeMapper::get_field_index(const std::string &type_name, const std::string &field_name)
    {
        std::cout << "[TypeMapper] Looking up field: type=" << type_name << ", field=" << field_name << "\n";
        auto type_it = _field_metadata.find(type_name);
        if (type_it != _field_metadata.end())
        {
            auto field_it = type_it->second.find(field_name);
            if (field_it != type_it->second.end())
            {
                std::cout << "[TypeMapper] Found field index: " << field_it->second.field_index << "\n";
                return field_it->second.field_index;
            }
            else
            {
                std::cout << "[TypeMapper] Field not found in type\n";
            }
        }
        else
        {
            std::cout << "[TypeMapper] Type not found in field metadata\n";
        }
        return -1;
    }

    void TypeMapper::register_struct_fields(Cryo::StructDeclarationNode *struct_decl, llvm::StructType *llvm_struct_type)
    {
        if (!struct_decl || !llvm_struct_type)
            return;

        const std::string &type_name = struct_decl->name();

        // Store the mapping from LLVM type to name for quick lookups
        _llvm_type_to_name_map[llvm_struct_type] = type_name;

        // Register each field
        int field_index = 0;
        for (const auto &field : struct_decl->fields())
        {
            if (field)
            {
                llvm::Type *field_llvm_type = map_type(field->type_annotation());
                if (field_llvm_type)
                {
                    register_field_metadata(type_name, field->name(), field_index, field_llvm_type);
                }
                field_index++;
            }
        }
    }

    void TypeMapper::register_class_fields(Cryo::ClassDeclarationNode *class_decl, llvm::StructType *llvm_class_type)
    {
        if (!class_decl || !llvm_class_type)
            return;

        const std::string &type_name = class_decl->name();

        // Store the mapping from LLVM type to name for quick lookups
        _llvm_type_to_name_map[llvm_class_type] = type_name;

        // Register each field
        int field_index = 0;
        for (const auto &field : class_decl->fields())
        {
            if (field)
            {
                llvm::Type *field_llvm_type = map_type(field->type_annotation());
                if (field_llvm_type)
                {
                    register_field_metadata(type_name, field->name(), field_index, field_llvm_type);
                }
                field_index++;
            }
        }
    }

    //===================================================================
    // Generic Type Instantiation Implementation
    //===================================================================

    llvm::Type *TypeMapper::map_generic_instantiation(const std::string &type_name)
    {
        // Parse generic type instantiation: "GenericStruct<int>"
        size_t angle_pos = type_name.find('<');
        if (angle_pos == std::string::npos)
        {
            return nullptr;
        }

        std::string base_name = type_name.substr(0, angle_pos);

        // Extract type arguments between < and >
        size_t close_angle = type_name.find('>', angle_pos);
        if (close_angle == std::string::npos)
        {
            report_error("Malformed generic type: missing closing '>' in " + type_name);
            return nullptr;
        }

        std::string args_str = type_name.substr(angle_pos + 1, close_angle - angle_pos - 1);

        // Parse type arguments (simplified - assumes single type argument for now)
        std::vector<std::string> type_args;
        std::stringstream ss(args_str);
        std::string arg;
        while (std::getline(ss, arg, ','))
        {
            // Trim whitespace
            arg.erase(0, arg.find_first_not_of(" \t"));
            arg.erase(arg.find_last_not_of(" \t") + 1);
            type_args.push_back(arg);
        }

        if (type_args.empty())
        {
            report_error("No type arguments found in generic type: " + type_name);
            return nullptr;
        }

        // Create instantiated type name for caching
        std::string instantiated_name = base_name + "<";
        for (size_t i = 0; i < type_args.size(); ++i)
        {
            if (i > 0)
                instantiated_name += ",";
            instantiated_name += type_args[i];
        }
        instantiated_name += ">";

        // Check if we've already instantiated this type
        llvm::Type *cached_type = lookup_type(instantiated_name);
        if (cached_type)
        {
            return cached_type;
        }

        // Look up the generic base type definition
        // For now, we'll handle struct types that were registered as generic
        return create_generic_struct_instantiation(base_name, type_args, instantiated_name);
    }

    llvm::Type *TypeMapper::create_generic_struct_instantiation(const std::string &base_name,
                                                                const std::vector<std::string> &type_args,
                                                                const std::string &instantiated_name)
    {
        // Use the new generic type definition system
        llvm::Type *result = create_generic_type_from_def(base_name, type_args, instantiated_name);
        if (result)
        {
            return result;
        }

        // If not found in registry, try legacy fallback for backwards compatibility
        std::cout << "[TypeMapper] Generic type '" << base_name << "' not found in registry, trying legacy fallback\n";

        // Legacy fallback for GenericStruct (can be removed once all types are registered)
        if (base_name == "GenericStruct" && type_args.size() == 1)
        {
            // Map the type argument
            llvm::Type *value_type = map_type(type_args[0]);
            if (!value_type)
            {
                report_error("Failed to map generic type argument: " + type_args[0]);
                return nullptr;
            }

            // Create LLVM struct type with the concrete type
            auto &context = _context_manager.get_context();
            std::vector<llvm::Type *> field_types = {value_type};

            llvm::StructType *instantiated_type = llvm::StructType::create(context, field_types, instantiated_name);

            // Register the instantiated type
            register_type(instantiated_name, instantiated_type);

            // Register field metadata for the instantiated type
            register_generic_field_metadata(instantiated_name, "value", 0, value_type);

            std::cout << "[TypeMapper] Created legacy GenericStruct instantiation: " << instantiated_name << "\n";
            return instantiated_type;
        }

        report_error("Unsupported generic type instantiation: " + base_name);
        return nullptr;
    }

    void TypeMapper::register_generic_field_metadata(const std::string &type_name,
                                                     const std::string &field_name,
                                                     int field_index,
                                                     llvm::Type *field_type)
    {
        register_field_metadata(type_name, field_name, field_index, field_type);
    }

    //===================================================================
    // Generic Type Definition System Implementation
    //===================================================================

    void TypeMapper::register_generic_type_def(const GenericTypeDef &def)
    {
        _generic_type_registry[def.base_name] = def;
        std::cout << "[TypeMapper] Registered generic type definition: " << def.base_name
                  << " with " << def.num_type_params << " parameters\n";
    }

    void TypeMapper::initialize_builtin_generic_types()
    {
        // Define Array<T> generic type
        GenericTypeDef array_def;
        array_def.base_name = "Array";
        array_def.num_type_params = 1;
        array_def.description = "Dynamic array with elements, length, and capacity";
        array_def.fields = {
            {"elements", "ptr<T>", true}, // T* elements
            {"length", "u64", false},     // u64 length
            {"capacity", "u64", false}    // u64 capacity
        };
        register_generic_type_def(array_def);

        // Define Pair<T,U> generic type
        GenericTypeDef pair_def;
        pair_def.base_name = "Pair";
        pair_def.num_type_params = 2;
        pair_def.description = "Pair containing two values of potentially different types";
        pair_def.fields = {
            {"first", "T", true}, // T first
            {"second", "U", true} // U second
        };
        register_generic_type_def(pair_def);

        // Define ptr<T> generic type (pointer)
        GenericTypeDef ptr_def;
        ptr_def.base_name = "ptr";
        ptr_def.num_type_params = 1;
        ptr_def.description = "Pointer to type T";
        ptr_def.fields = {}; // No fields - this is a primitive pointer type
        register_generic_type_def(ptr_def);

        // Define const_ptr<T> generic type
        GenericTypeDef const_ptr_def;
        const_ptr_def.base_name = "const_ptr";
        const_ptr_def.num_type_params = 1;
        const_ptr_def.description = "Const pointer to type T";
        const_ptr_def.fields = {}; // No fields - this is a primitive pointer type
        register_generic_type_def(const_ptr_def);

        // Define Option<T> generic type
        GenericTypeDef option_def;
        option_def.base_name = "Option";
        option_def.num_type_params = 1;
        option_def.description = "Optional value that may contain Some(T) or None";
        option_def.fields = {
            {"has_value", "bool", false}, // bool has_value
            {"value", "T", true}          // T value
        };
        register_generic_type_def(option_def);

        // Define Result<T,E> generic type
        GenericTypeDef result_def;
        result_def.base_name = "Result";
        result_def.num_type_params = 2;
        result_def.description = "Result type that contains either Ok(T) or Err(E)";
        result_def.fields = {
            {"is_ok", "bool", false},         // bool is_ok
            {"data", "union_data<T,E>", true} // Union data (simplified)
        };
        register_generic_type_def(result_def);

        std::cout << "[TypeMapper] Initialized " << _generic_type_registry.size()
                  << " built-in generic type definitions\n";
    }

    llvm::Type *TypeMapper::create_generic_type_from_def(const std::string &base_name,
                                                         const std::vector<std::string> &type_args,
                                                         const std::string &instantiated_name)
    {
        // Look up the generic type definition
        auto def_it = _generic_type_registry.find(base_name);
        if (def_it == _generic_type_registry.end())
        {
            report_error("Unknown generic type: " + base_name);
            return nullptr;
        }

        const GenericTypeDef &def = def_it->second;

        // Validate type argument count
        if (static_cast<int>(type_args.size()) != def.num_type_params)
        {
            report_error("Generic type " + base_name + " expects " +
                         std::to_string(def.num_type_params) + " type arguments, got " +
                         std::to_string(type_args.size()));
            return nullptr;
        }

        // Handle special cases for primitive pointer types
        if (base_name == "ptr" || base_name == "const_ptr")
        {
            llvm::Type *pointee_type = map_type(type_args[0]);
            if (!pointee_type)
            {
                report_error("Failed to map pointee type for " + base_name + ": " + type_args[0]);
                return nullptr;
            }

            llvm::Type *pointer_type = llvm::PointerType::get(pointee_type, 0);
            register_type(instantiated_name, pointer_type);
            return pointer_type;
        }

        // Create type parameter mapping (T -> concrete_type, U -> concrete_type2, etc.)
        std::unordered_map<std::string, std::string> type_params_map;
        std::vector<std::string> param_names = {"T", "U", "V", "W"}; // Support up to 4 params for now
        for (int i = 0; i < def.num_type_params && i < static_cast<int>(param_names.size()); ++i)
        {
            type_params_map[param_names[i]] = type_args[i];
        }

        // Resolve field types using the type parameter mapping
        auto &context = _context_manager.get_context();
        std::vector<llvm::Type *> field_types;

        for (const auto &field_def : def.fields)
        {
            llvm::Type *field_type = resolve_type_expression(field_def.type_expr, type_params_map);
            if (!field_type)
            {
                report_error("Failed to resolve field type '" + field_def.type_expr +
                             "' for field '" + field_def.name + "' in " + base_name);
                return nullptr;
            }
            field_types.push_back(field_type);
        }

        // Create the struct type
        llvm::StructType *instantiated_type = llvm::StructType::create(context, field_types, instantiated_name);

        // Register the instantiated type
        register_type(instantiated_name, instantiated_type);

        // Register field metadata
        for (size_t i = 0; i < def.fields.size(); ++i)
        {
            register_generic_field_metadata(instantiated_name, def.fields[i].name,
                                            static_cast<int>(i), field_types[i]);
        }

        std::cout << "[TypeMapper] Created generic type instantiation: " << instantiated_name
                  << " with " << field_types.size() << " fields\n";

        return instantiated_type;
    }

    llvm::Type *TypeMapper::resolve_type_expression(const std::string &type_expr,
                                                    const std::unordered_map<std::string, std::string> &type_params_map)
    {
        // Handle template parameter substitution
        auto param_it = type_params_map.find(type_expr);
        if (param_it != type_params_map.end())
        {
            // Direct template parameter: T -> int
            return map_type(param_it->second);
        }

        // Handle templated expressions like ptr<T>
        if (type_expr.find('<') != std::string::npos)
        {
            size_t angle_pos = type_expr.find('<');
            size_t close_angle = type_expr.find('>', angle_pos);

            if (close_angle == std::string::npos)
            {
                report_error("Malformed type expression: " + type_expr);
                return nullptr;
            }

            std::string base_type = type_expr.substr(0, angle_pos);
            std::string param_expr = type_expr.substr(angle_pos + 1, close_angle - angle_pos - 1);

            // Recursively resolve the parameter
            llvm::Type *param_type = resolve_type_expression(param_expr, type_params_map);
            if (!param_type)
            {
                return nullptr;
            }

            // Handle specific template expressions
            if (base_type == "ptr")
            {
                return llvm::PointerType::get(param_type, 0);
            }
            else if (base_type == "const_ptr")
            {
                return llvm::PointerType::get(param_type, 0); // LLVM doesn't distinguish const at type level
            }
            else
            {
                // For other generic types, construct the instantiated name and map it
                std::string instantiated_expr = base_type + "<" + param_expr + ">";
                return map_type(instantiated_expr);
            }
        }

        // Handle special union data type (simplified for Result<T,E>)
        if (type_expr.find("union_data<") == 0)
        {
            // For now, just create a large enough byte array to hold either type
            // In a production compiler, you'd want proper union support
            auto &context = _context_manager.get_context();
            return llvm::ArrayType::get(get_char_type(), 64); // 64 bytes should be enough for most types
        }

        // Direct type lookup for non-templated types
        return map_type(type_expr);
    }

} // namespace Cryo::Codegen