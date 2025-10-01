#include "Codegen/TypeMapper.hpp"
#include "AST/Type.hpp"
#include "AST/TypeChecker.hpp"
#include <iostream>
#include <sstream>

namespace Cryo::Codegen
{
    //===================================================================
    // Constructor and Initialization
    //===================================================================

    TypeMapper::TypeMapper(LLVMContextManager &context_manager, Cryo::TypeContext *type_context)
        : _context_manager(context_manager), _type_context(type_context), _has_errors(false)
    {
        if (!_type_context)
        {
            report_error("TypeContext is required for TypeMapper");
        }
    }

    //===================================================================
    // Core Type Mapping Interface - The Heart of the New Architecture
    //===================================================================

    llvm::Type *TypeMapper::map_type(Cryo::Type *cryo_type)
    {
        if (!cryo_type)
        {
            report_error("Cannot map null type");
            return nullptr;
        }

        // Check cache first using type pointer as key
        auto cache_it = _cryo_type_cache.find(cryo_type);
        if (cache_it != _cryo_type_cache.end())
        {
            return cache_it->second;
        }

        llvm::Type *llvm_type = nullptr;

        // Dispatch to type-specific mapping methods based on TypeKind
        switch (cryo_type->kind())
        {
        case Cryo::TypeKind::Void:
            llvm_type = get_void_type();
            break;

        case Cryo::TypeKind::Boolean:
            llvm_type = get_boolean_type();
            break;

        case Cryo::TypeKind::Integer:
            llvm_type = map_integer_type(static_cast<Cryo::IntegerType *>(cryo_type));
            break;

        case Cryo::TypeKind::Float:
            llvm_type = map_float_type(static_cast<Cryo::FloatType *>(cryo_type));
            break;

        case Cryo::TypeKind::Char:
            llvm_type = get_char_type();
            break;

        case Cryo::TypeKind::String:
            llvm_type = get_string_type();
            break;

        case Cryo::TypeKind::Array:
            llvm_type = map_array_type(static_cast<Cryo::ArrayType *>(cryo_type));
            break;

        case Cryo::TypeKind::Pointer:
            llvm_type = map_pointer_type(static_cast<Cryo::PointerType *>(cryo_type));
            break;

        case Cryo::TypeKind::Reference:
            llvm_type = map_reference_type(static_cast<Cryo::ReferenceType *>(cryo_type));
            break;

        case Cryo::TypeKind::Function:
            llvm_type = map_function_type(static_cast<Cryo::FunctionType *>(cryo_type));
            break;

        case Cryo::TypeKind::Struct:
            llvm_type = map_struct_type(static_cast<Cryo::StructType *>(cryo_type));
            break;

        case Cryo::TypeKind::Class:
            llvm_type = map_class_type(static_cast<Cryo::ClassType *>(cryo_type));
            break;

        case Cryo::TypeKind::Enum:
            llvm_type = map_enum_type(static_cast<Cryo::EnumType *>(cryo_type));
            break;

        case Cryo::TypeKind::Parameterized:
            llvm_type = map_parameterized_type(static_cast<Cryo::ParameterizedType *>(cryo_type));
            break;

        case Cryo::TypeKind::Optional:
            llvm_type = map_optional_type(static_cast<Cryo::OptionalType *>(cryo_type));
            break;

        case Cryo::TypeKind::Null:
            // Null is represented as a generic pointer type
            llvm_type = llvm::PointerType::get(get_void_type(), 0);
            break;

        case Cryo::TypeKind::Auto:
        case Cryo::TypeKind::Unknown:
            // These should be resolved by type inference before reaching codegen
            report_error("Auto/Unknown types should be resolved before code generation");
            return nullptr;

        case Cryo::TypeKind::Generic:
            // Generic type parameters should be instantiated before reaching codegen
            report_error("Generic type parameters should be instantiated before code generation");
            return nullptr;

        case Cryo::TypeKind::Variadic:
            // Variadic parameters are special and shouldn't be mapped to concrete LLVM types
            report_error("Variadic type should not be mapped to concrete LLVM type");
            return nullptr;

        default:
            report_error("Unsupported type kind: " + std::to_string(static_cast<int>(cryo_type->kind())));
            return nullptr;
        }

        // Cache the result
        if (llvm_type)
        {
            _cryo_type_cache[cryo_type] = llvm_type;
        }

        return llvm_type;
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
        case 16:
            return llvm::Type::getHalfTy(llvm_context);
        case 32:
            return llvm::Type::getFloatTy(llvm_context);
        case 64:
            return llvm::Type::getDoubleTy(llvm_context);
        case 128:
            return llvm::Type::getFP128Ty(llvm_context);
        default:
            report_error("Unsupported float precision: " + std::to_string(precision));
            return llvm::Type::getFloatTy(llvm_context); // Default fallback
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
        // String is represented as i8* (null-terminated C string)
        return llvm::PointerType::get(get_char_type(), 0);
    }

    llvm::Type *TypeMapper::get_void_type()
    {
        auto &llvm_context = _context_manager.get_context();
        return llvm::Type::getVoidTy(llvm_context);
    }

    //===================================================================
    // Type-Specific Mapping Methods - Core of the Architecture
    //===================================================================

    llvm::Type *TypeMapper::map_integer_type(Cryo::IntegerType *int_type)
    {
        if (!int_type)
        {
            report_error("Cannot map null integer type");
            return nullptr;
        }

        // Convert size_bytes to bit width
        size_t byte_size = int_type->size_bytes();
        int bit_width = static_cast<int>(byte_size * 8);

        return get_integer_type(bit_width, int_type->is_signed());
    }

    llvm::Type *TypeMapper::map_float_type(Cryo::FloatType *float_type)
    {
        if (!float_type)
        {
            report_error("Cannot map null float type");
            return nullptr;
        }

        // Convert size_bytes to bit width
        size_t byte_size = float_type->size_bytes();
        int bit_width = static_cast<int>(byte_size * 8);

        return get_float_type(bit_width);
    }

    llvm::Type *TypeMapper::map_array_type(Cryo::ArrayType *array_type)
    {
        if (!array_type)
        {
            report_error("Cannot map null array type");
            return nullptr;
        }

        // Get element type
        llvm::Type *element_type = map_type(array_type->element_type().get());
        if (!element_type)
        {
            return nullptr;
        }

        if (!array_type->is_dynamic())
        {
            // Fixed-size array: [T; N]
            size_t size = array_type->array_size().value();
            return llvm::ArrayType::get(element_type, size);
        }
        else
        {
            // Dynamic array: [T] - represented as a struct with pointer + size + capacity
            std::vector<llvm::Type *> fields = {
                llvm::PointerType::get(element_type, 0), // data pointer
                get_integer_type(64),                    // size (u64)
                get_integer_type(64)                     // capacity (u64)
            };
            return llvm::StructType::create(_context_manager.get_context(), fields,
                                            "dynamic_array_" + array_type->element_type()->name());
        }
    }

    llvm::Type *TypeMapper::map_pointer_type(Cryo::PointerType *pointer_type)
    {
        if (!pointer_type)
        {
            report_error("Cannot map null pointer type");
            return nullptr;
        }

        // Get the pointee type and create pointer to it
        llvm::Type *pointee_type = map_type(pointer_type->pointee_type().get());
        if (!pointee_type)
        {
            return nullptr;
        }

        return llvm::PointerType::get(pointee_type, 0);
    }

    llvm::Type *TypeMapper::map_reference_type(Cryo::ReferenceType *reference_type)
    {
        if (!reference_type)
        {
            report_error("Cannot map null reference type");
            return nullptr;
        }

        // Get the referent type - in LLVM, references are implemented as pointers
        llvm::Type *referent_type = map_type(reference_type->referent_type().get());
        if (!referent_type)
        {
            return nullptr;
        }

        return llvm::PointerType::get(referent_type, 0);
    }

    llvm::Type *TypeMapper::map_function_type(Cryo::FunctionType *function_type)
    {
        if (!function_type)
        {
            report_error("Cannot map null function type");
            return nullptr;
        }

        // Map return type
        llvm::Type *return_type = map_type(function_type->return_type().get());
        if (!return_type)
        {
            return nullptr;
        }

        // Map parameter types
        std::vector<llvm::Type *> param_types;
        for (const auto &param : function_type->parameter_types())
        {
            llvm::Type *param_type = map_type(param.get());
            if (!param_type)
            {
                return nullptr;
            }
            param_types.push_back(param_type);
        }

        // Create function type
        bool is_variadic = function_type->is_variadic();
        return llvm::FunctionType::get(return_type, param_types, is_variadic);
    }

    llvm::Type *TypeMapper::map_struct_type(Cryo::StructType *struct_type)
    {
        if (!struct_type)
        {
            report_error("Cannot map null struct type");
            return nullptr;
        }

        // Check if we already have this struct type in cache
        std::string struct_name = struct_type->name();
        auto it = _struct_cache.find(struct_name);
        if (it != _struct_cache.end())
        {
            return it->second;
        }

        // Create named struct type
        llvm::StructType *llvm_struct = llvm::StructType::create(_context_manager.get_context(), struct_name);

        // Add to cache early to handle recursive structs
        _struct_cache[struct_name] = llvm_struct;

        // Generate field types from AST node if available
        std::vector<llvm::Type *> field_types;

        auto ast_it = _struct_ast_nodes.find(struct_name);
        if (ast_it != _struct_ast_nodes.end())
        {
            auto struct_node = ast_it->second;
            std::cout << "[TypeMapper] Generating field types for struct: " << struct_name << std::endl;

            for (const auto &field : struct_node->fields())
            {
                if (field)
                {
                    std::cout << "[TypeMapper] Processing field: " << field->name() << " : " << field->type_annotation() << std::endl;

                    // Map field type using TypeContext
                    if (_type_context)
                    {
                        auto field_cryo_type = _type_context->parse_type_from_string(field->type_annotation());
                        if (field_cryo_type)
                        {
                            llvm::Type *field_llvm_type = map_type(field_cryo_type);
                            if (field_llvm_type)
                            {
                                field_types.push_back(field_llvm_type);
                                std::cout << "[TypeMapper] Mapped field '" << field->name() << "' to LLVM type" << std::endl;
                            }
                            else
                            {
                                std::cerr << "[TypeMapper] Failed to map field type: " << field->type_annotation() << std::endl;
                                // Use a placeholder pointer type for failed field mappings
                                field_types.push_back(llvm::PointerType::getUnqual(_context_manager.get_context()));
                            }
                        }
                        else
                        {
                            std::cerr << "[TypeMapper] Failed to parse field type: " << field->type_annotation() << std::endl;
                            // Use a placeholder pointer type for failed field parsing
                            field_types.push_back(llvm::PointerType::getUnqual(_context_manager.get_context()));
                        }
                    }
                    else
                    {
                        std::cerr << "[TypeMapper] No TypeContext available for field type mapping" << std::endl;
                        // Use a placeholder pointer type when no TypeContext
                        field_types.push_back(llvm::PointerType::getUnqual(_context_manager.get_context()));
                    }
                }
            }
        }
        else
        {
            // Check if this is a parameterized type that should map to a monomorphized version
            if (struct_name.find('<') != std::string::npos && struct_name.find('>') != std::string::npos)
            {
                // Extract base name and type arguments to create monomorphized name
                size_t angle_pos = struct_name.find('<');
                std::string base_name = struct_name.substr(0, angle_pos);

                size_t close_angle = struct_name.find('>', angle_pos);
                if (close_angle != std::string::npos)
                {
                    std::string type_args_str = struct_name.substr(angle_pos + 1, close_angle - angle_pos - 1);

                    // Create monomorphized name by replacing <T> with _T
                    std::string monomorphized_name = base_name;
                    std::stringstream ss(type_args_str);
                    std::string arg;
                    while (std::getline(ss, arg, ','))
                    {
                        // Trim whitespace
                        arg.erase(0, arg.find_first_not_of(" \t"));
                        arg.erase(arg.find_last_not_of(" \t") + 1);
                        if (!arg.empty())
                        {
                            monomorphized_name += "_" + arg;
                        }
                    }

                    std::cout << "[TypeMapper] Trying monomorphized name: " << monomorphized_name << " for parameterized type: " << struct_name << std::endl;

                    // Check if the monomorphized version exists
                    auto mono_it = _struct_ast_nodes.find(monomorphized_name);
                    if (mono_it != _struct_ast_nodes.end())
                    {
                        std::cout << "[TypeMapper] Found monomorphized struct: " << monomorphized_name << std::endl;
                        auto struct_node = mono_it->second;

                        for (const auto &field : struct_node->fields())
                        {
                            if (field)
                            {
                                std::cout << "[TypeMapper] Processing field: " << field->name() << " : " << field->type_annotation() << std::endl;

                                // Map field type using TypeContext
                                if (_type_context)
                                {
                                    auto field_cryo_type = _type_context->parse_type_from_string(field->type_annotation());
                                    if (field_cryo_type)
                                    {
                                        llvm::Type *field_llvm_type = map_type(field_cryo_type);
                                        if (field_llvm_type)
                                        {
                                            field_types.push_back(field_llvm_type);
                                            std::cout << "[TypeMapper] Mapped field '" << field->name() << "' to LLVM type" << std::endl;
                                        }
                                        else
                                        {
                                            std::cerr << "[TypeMapper] Failed to map field type: " << field->type_annotation() << std::endl;
                                            field_types.push_back(llvm::PointerType::getUnqual(_context_manager.get_context()));
                                        }
                                    }
                                    else
                                    {
                                        std::cerr << "[TypeMapper] Failed to parse field type: " << field->type_annotation() << std::endl;
                                        field_types.push_back(llvm::PointerType::getUnqual(_context_manager.get_context()));
                                    }
                                }
                                else
                                {
                                    std::cerr << "[TypeMapper] No TypeContext available for field type mapping" << std::endl;
                                    field_types.push_back(llvm::PointerType::getUnqual(_context_manager.get_context()));
                                }
                            }
                        }
                    }
                    else
                    {
                        std::cout << "[TypeMapper] Monomorphized struct not found: " << monomorphized_name << ", trying base template" << std::endl;

                        // If monomorphized version not found, try to use the base template to create it
                        auto base_it = _struct_ast_nodes.find(base_name);
                        if (base_it != _struct_ast_nodes.end())
                        {
                            std::cout << "[TypeMapper] Found base template: " << base_name << ", creating fields with substituted types" << std::endl;
                            auto base_struct = base_it->second;

                            // Parse the type arguments for substitution
                            std::vector<std::string> type_args;
                            std::stringstream ss(type_args_str);
                            std::string arg;
                            while (std::getline(ss, arg, ','))
                            {
                                // Trim whitespace
                                arg.erase(0, arg.find_first_not_of(" \t"));
                                arg.erase(arg.find_last_not_of(" \t") + 1);
                                if (!arg.empty())
                                {
                                    type_args.push_back(arg);
                                }
                            }

                            // Create fields by substituting generic type parameters
                            for (const auto &field : base_struct->fields())
                            {
                                if (field)
                                {
                                    std::string field_type = field->type_annotation();

                                    // Simple substitution - replace T with the first type argument
                                    if (field_type == "T" && !type_args.empty())
                                    {
                                        field_type = type_args[0];
                                    }
                                    // Add more substitution rules as needed for U, V, etc.

                                    std::cout << "[TypeMapper] Processing substituted field: " << field->name() << " : " << field_type << std::endl;

                                    // Map field type using TypeContext
                                    if (_type_context)
                                    {
                                        auto field_cryo_type = _type_context->parse_type_from_string(field_type);
                                        if (field_cryo_type)
                                        {
                                            llvm::Type *field_llvm_type = map_type(field_cryo_type);
                                            if (field_llvm_type)
                                            {
                                                field_types.push_back(field_llvm_type);
                                                std::cout << "[TypeMapper] Mapped substituted field '" << field->name() << "' to LLVM type" << std::endl;
                                            }
                                            else
                                            {
                                                std::cerr << "[TypeMapper] Failed to map substituted field type: " << field_type << std::endl;
                                                field_types.push_back(llvm::PointerType::getUnqual(_context_manager.get_context()));
                                            }
                                        }
                                        else
                                        {
                                            std::cerr << "[TypeMapper] Failed to parse substituted field type: " << field_type << std::endl;
                                            field_types.push_back(llvm::PointerType::getUnqual(_context_manager.get_context()));
                                        }
                                    }
                                    else
                                    {
                                        std::cerr << "[TypeMapper] No TypeContext available for substituted field type mapping" << std::endl;
                                        field_types.push_back(llvm::PointerType::getUnqual(_context_manager.get_context()));
                                    }
                                }
                            }
                        }
                        else
                        {
                            std::cout << "[TypeMapper] Base template not found: " << base_name << ", falling back to enhanced enum system" << std::endl;
                        }
                    }
                }
            }

            // If monomorphized version not found, check if this is a parameterized enum that should use enhanced type system
            if (field_types.empty() && _type_context && struct_name.find('<') != std::string::npos && struct_name.find('>') != std::string::npos)
            {
                // Extract base name and type arguments for parameterized types
                size_t angle_pos = struct_name.find('<');
                std::string base_name = struct_name.substr(0, angle_pos);

                size_t close_angle = struct_name.find('>', angle_pos);
                if (close_angle != std::string::npos)
                {
                    std::string type_args_str = struct_name.substr(angle_pos + 1, close_angle - angle_pos - 1);

                    // Parse type arguments
                    std::vector<Cryo::Type *> type_args;
                    std::stringstream ss(type_args_str);
                    std::string arg;
                    while (std::getline(ss, arg, ','))
                    {
                        // Trim whitespace
                        arg.erase(0, arg.find_first_not_of(" \t"));
                        arg.erase(arg.find_last_not_of(" \t") + 1);
                        if (!arg.empty())
                        {
                            Cryo::Type *arg_type = _type_context->parse_type_from_string(arg);
                            if (arg_type)
                            {
                                type_args.push_back(arg_type);
                            }
                        }
                    }

                    // Query enhanced enum type system for field layout
                    std::vector<std::shared_ptr<Cryo::Type>> enum_field_types =
                        _type_context->get_enum_field_types(base_name, type_args);

                    if (!enum_field_types.empty())
                    {
                        std::cout << "[TypeMapper] Using enhanced enum system for parameterized type: " << struct_name << std::endl;

                        // Map enum field types to LLVM types
                        for (const auto &field_type : enum_field_types)
                        {
                            llvm::Type *llvm_field_type = map_type(field_type.get());
                            if (llvm_field_type)
                            {
                                field_types.push_back(llvm_field_type);
                            }
                        }
                    }
                    else
                    {
                        std::cout << "[TypeMapper] No enhanced enum layout found for: " << struct_name << ", creating empty struct" << std::endl;
                    }
                }
                else
                {
                    std::cout << "[TypeMapper] Malformed parameterized type: " << struct_name << ", creating empty struct" << std::endl;
                }
            }
            else
            {
                std::cout << "[TypeMapper] No AST node found for struct: " << struct_name << ", creating empty struct" << std::endl;
            }
        }

        // Set the body of the struct
        llvm_struct->setBody(field_types);

        std::cout << "[TypeMapper] Created struct type '" << struct_name << "' with " << field_types.size() << " fields" << std::endl;

        // Cache the created type for future lookups
        register_type(struct_name, llvm_struct);

        return llvm_struct;
    }

    llvm::Type *TypeMapper::map_class_type(Cryo::ClassType *class_type)
    {
        if (!class_type)
        {
            report_error("Cannot map null class type");
            return nullptr;
        }

        // Classes are similar to structs but may include vtable pointers
        std::string class_name = class_type->name();

        // Check cache
        auto it = _struct_cache.find(class_name);
        if (it != _struct_cache.end())
        {
            return it->second;
        }

        // Create named struct type for the class
        llvm::StructType *llvm_class = llvm::StructType::create(_context_manager.get_context(), class_name);

        // Add to cache early to handle recursive classes
        _struct_cache[class_name] = llvm_class;

        // Generate field types from AST node if available
        std::vector<llvm::Type *> field_types;

        auto ast_it = _class_ast_nodes.find(class_name);
        if (ast_it != _class_ast_nodes.end())
        {
            auto class_node = ast_it->second;
            std::cout << "[TypeMapper] Generating field types for class: " << class_name << std::endl;

            for (const auto &field : class_node->fields())
            {
                if (field)
                {
                    std::cout << "[TypeMapper] Processing field: " << field->name() << " : " << field->type_annotation() << std::endl;

                    // Map field type using TypeContext
                    if (_type_context)
                    {
                        auto field_cryo_type = _type_context->parse_type_from_string(field->type_annotation());
                        if (field_cryo_type)
                        {
                            llvm::Type *field_llvm_type = map_type(field_cryo_type);
                            if (field_llvm_type)
                            {
                                field_types.push_back(field_llvm_type);
                                std::cout << "[TypeMapper] Mapped field '" << field->name() << "' to LLVM type" << std::endl;
                            }
                            else
                            {
                                std::cerr << "[TypeMapper] Failed to map field type: " << field->type_annotation() << std::endl;
                                // Use a placeholder pointer type for failed field mappings
                                field_types.push_back(llvm::PointerType::getUnqual(_context_manager.get_context()));
                            }
                        }
                        else
                        {
                            std::cerr << "[TypeMapper] Failed to parse field type: " << field->type_annotation() << std::endl;
                            // Use a placeholder pointer type for failed field parsing
                            field_types.push_back(llvm::PointerType::getUnqual(_context_manager.get_context()));
                        }
                    }
                    else
                    {
                        std::cerr << "[TypeMapper] No TypeContext available for field type mapping" << std::endl;
                        // Use a placeholder pointer type when no TypeContext
                        field_types.push_back(llvm::PointerType::getUnqual(_context_manager.get_context()));
                    }
                }
            }
        }
        else
        {
            std::cout << "[TypeMapper] No AST node found for class: " << class_name << ", creating empty struct" << std::endl;
        }

        // TODO: Add vtable pointer if the class has virtual methods
        // if (class_type->has_virtual_methods()) {
        //     field_types.insert(field_types.begin(),
        //                        llvm::PointerType::get(get_void_type(), 0)); // vtable pointer
        // }

        // Set the body of the class struct
        llvm_class->setBody(field_types);

        std::cout << "[TypeMapper] Created class type '" << class_name << "' with " << field_types.size() << " fields" << std::endl;

        // Cache the created type for future lookups
        register_type(class_name, llvm_class);

        return llvm_class;
    }

    llvm::Type *TypeMapper::map_enum_type(Cryo::EnumType *enum_type)
    {
        if (!enum_type)
        {
            report_error("Cannot map null enum type");
            return nullptr;
        }

        // Check if it's a simple enum (no associated data) or complex enum (with data)
        if (enum_type->is_simple_enum())
        {
            // Simple enum without associated data -> i32
            return get_integer_type(32);
        }
        else
        {
            // Complex enum with associated data -> tagged union
            return create_tagged_union_type(enum_type);
        }
    }

    llvm::Type *TypeMapper::map_parameterized_type(Cryo::ParameterizedType *param_type)
    {
        if (!param_type)
        {
            report_error("Cannot map null parameterized type");
            return nullptr;
        }

        std::string base_name = param_type->base_name();
        auto type_args = param_type->type_parameters();
        std::string instantiated_name = param_type->get_instantiated_name();

        // Check if we already have this parameterized type in cache
        auto it = _struct_cache.find(instantiated_name);
        if (it != _struct_cache.end())
        {
            std::cout << "[TypeMapper] Found cached parameterized type: " << instantiated_name << std::endl;
            return it->second;
        }

        // Use enhanced type system to get field layout for parameterized types
        if (_type_context && !type_args.empty())
        {
            // Convert shared_ptr type args to raw pointers for query
            std::vector<Cryo::Type *> type_args_raw;
            for (const auto &type_arg : type_args)
            {
                type_args_raw.push_back(type_arg.get());
            }

            // Query enhanced type system for enum field layout
            std::vector<std::shared_ptr<Cryo::Type>> field_types =
                _type_context->get_enum_field_types(base_name, type_args_raw);

            if (!field_types.empty())
            {
                std::cout << "[TypeMapper] Using enhanced enum system for parameterized type: " << instantiated_name << std::endl;

                // Map enum field types to LLVM types
                std::vector<llvm::Type *> llvm_field_types;
                for (const auto &field_type : field_types)
                {
                    llvm::Type *llvm_field_type = map_type(field_type.get());
                    if (llvm_field_type)
                    {
                        llvm_field_types.push_back(llvm_field_type);
                    }
                }

                if (!llvm_field_types.empty())
                {
                    // Create and cache the parameterized type
                    llvm::StructType *created_type = llvm::StructType::create(_context_manager.get_context(),
                                                                              llvm_field_types, instantiated_name);
                    _struct_cache[instantiated_name] = created_type;
                    std::cout << "[TypeMapper] Created parameterized type '" << instantiated_name
                              << "' with " << llvm_field_types.size() << " fields" << std::endl;
                    return created_type;
                }
            }
        }

        report_error("Failed to create parameterized type using enhanced type system: " + base_name);
        return nullptr;
    }

    llvm::Type *TypeMapper::map_optional_type(Cryo::OptionalType *optional_type)
    {
        if (!optional_type)
        {
            report_error("Cannot map null optional type");
            return nullptr;
        }

        // Optional<T> is similar to Option<T>
        llvm::Type *value_type = map_type(optional_type->wrapped_type().get());
        if (!value_type)
        {
            return nullptr;
        }

        std::vector<llvm::Type *> fields = {
            get_boolean_type(), // has_value flag
            value_type          // value
        };

        return llvm::StructType::create(_context_manager.get_context(), fields, "optional");
    }

    //===================================================================
    // Complex Type Helpers
    //===================================================================

    llvm::Type *TypeMapper::create_tagged_union_type(Cryo::EnumType *enum_type)
    {
        // Create tagged union for complex enum
        std::string enum_name = enum_type->name();

        // TODO: Calculate actual payload size based on enum variants
        // For now, use a conservative estimate
        size_t max_payload_size = 64; // 64 bytes should handle most cases

        return create_tagged_union_type(enum_name, max_payload_size);
    }

    llvm::StructType *TypeMapper::create_tagged_union_type(const std::string &name, size_t payload_size)
    {
        auto &llvm_context = _context_manager.get_context();

        // Create struct type: { i32 discriminant, [payload_size x i8] payload }
        std::vector<llvm::Type *> fields;
        fields.push_back(get_integer_type(32));                                // discriminant (tag)
        fields.push_back(llvm::ArrayType::get(get_char_type(), payload_size)); // payload data

        return llvm::StructType::create(llvm_context, fields, name + "_tagged_union");
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
            return llvm_type->getIntegerBitWidth() / 8;
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
            return 8; // Assume 64-bit pointers
        }
        else if (llvm_type->isArrayTy())
        {
            auto *array_type = llvm::cast<llvm::ArrayType>(llvm_type);
            return array_type->getNumElements() * get_type_size(array_type->getElementType());
        }
        else if (llvm_type->isStructTy())
        {
            auto *struct_type = llvm::cast<llvm::StructType>(llvm_type);
            size_t total_size = 0;
            for (unsigned i = 0; i < struct_type->getNumElements(); ++i)
            {
                total_size += get_type_size(struct_type->getElementType(i));
            }
            return total_size;
        }

        return 8; // Default fallback
    }

    size_t TypeMapper::get_type_alignment(llvm::Type *llvm_type)
    {
        if (!llvm_type)
        {
            return 1;
        }

        // Use target machine data layout if available
        if (auto *target_machine = _context_manager.get_target_machine())
        {
            auto data_layout = target_machine->createDataLayout();
            return data_layout.getABITypeAlign(llvm_type).value();
        }

        // Fallback to basic alignment estimation
        if (llvm_type->isIntegerTy())
        {
            size_t byte_size = llvm_type->getIntegerBitWidth() / 8;
            return std::min(byte_size, size_t(8)); // Max 8-byte alignment
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
            return 8; // Assume 64-bit pointers
        }

        return 1; // Default fallback
    }

    bool TypeMapper::is_signed_integer(llvm::Type *type)
    {
        // LLVM doesn't distinguish signed/unsigned at the type level
        // This information would need to be tracked separately
        return type && type->isIntegerTy();
    }

    bool TypeMapper::is_floating_point(llvm::Type *type)
    {
        return type && type->isFloatingPointTy();
    }

    bool TypeMapper::requires_heap_allocation(llvm::Type *type)
    {
        if (!type)
        {
            return false;
        }

        // Large types or dynamic arrays typically require heap allocation
        size_t size = get_type_size(type);
        return size > 1024; // Arbitrary threshold - types larger than 1KB go on heap
    }

    llvm::Constant *TypeMapper::get_default_value(llvm::Type *type)
    {
        if (!type)
        {
            return nullptr;
        }

        return llvm::Constant::getNullValue(type);
    }

    //===================================================================
    // Type Cache Management
    //===================================================================

    void TypeMapper::register_type(const std::string &name, llvm::Type *type)
    {
        if (name.empty() || !type)
        {
            return;
        }
        _type_cache[name] = type;
    }

    llvm::Type *TypeMapper::lookup_type(const std::string &name)
    {
        auto it = _type_cache.find(name);
        return (it != _type_cache.end()) ? it->second : nullptr;
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
    }

    //===================================================================
    // Error Handling
    //===================================================================

    void TypeMapper::report_error(const std::string &message)
    {
        _has_errors = true;
        _last_error = message;
        std::cerr << "[TypeMapper Error] " << message << std::endl;
    }

    //===================================================================
    // Utility Functions
    //===================================================================

    llvm::StructType *TypeMapper::create_opaque_struct(const std::string &name)
    {
        auto &llvm_context = _context_manager.get_context();
        return llvm::StructType::create(llvm_context, name);
    }

    // Helper function to convert generic type names to instantiated names
    // e.g., "Pair<int,string>" -> "Pair_int_string"
    std::string TypeMapper::convert_generic_to_instantiated_name(const std::string &type_name)
    {
        // Check if this is a generic type (contains < and >)
        size_t start = type_name.find('<');
        if (start == std::string::npos)
        {
            return type_name; // Not a generic type
        }

        size_t end = type_name.rfind('>');
        if (end == std::string::npos || end <= start)
        {
            return type_name; // Malformed generic type
        }

        // Extract base name and type parameters
        std::string base_name = type_name.substr(0, start);
        std::string type_params = type_name.substr(start + 1, end - start - 1);

        // Parse type parameters (simple comma-separated parsing)
        std::vector<std::string> concrete_types;
        std::istringstream iss(type_params);
        std::string param;
        while (std::getline(iss, param, ','))
        {
            // Trim whitespace
            param.erase(0, param.find_first_not_of(" \t"));
            param.erase(param.find_last_not_of(" \t") + 1);
            concrete_types.push_back(param);
        }

        // Generate mangled name using the same convention as MonomorphizationPass
        std::ostringstream mangled;
        mangled << base_name;
        for (const auto &type : concrete_types)
        {
            mangled << "_" << type;
        }

        return mangled.str();
    }

    int TypeMapper::get_field_index(const std::string &type_name, const std::string &field_name)
    {
        // Convert generic type name to instantiated name if needed
        std::string lookup_name = convert_generic_to_instantiated_name(type_name);

        std::cout << "[TypeMapper] Looking up field '" << field_name << "' in type '" << type_name << "'" << std::endl;
        if (lookup_name != type_name)
        {
            std::cout << "[TypeMapper] Converted generic type '" << type_name << "' to instantiated name '" << lookup_name << "'" << std::endl;
        }

        // Check if we have AST information for this type
        auto class_it = _class_ast_nodes.find(lookup_name);
        if (class_it != _class_ast_nodes.end())
        {
            auto class_node = class_it->second;
            const auto &fields = class_node->fields();

            for (size_t i = 0; i < fields.size(); ++i)
            {
                if (fields[i] && fields[i]->name() == field_name)
                {
                    std::cout << "[TypeMapper] Found field '" << field_name << "' at index " << i << " in class '" << lookup_name << "'" << std::endl;
                    return static_cast<int>(i);
                }
            }
        }

        auto struct_it = _struct_ast_nodes.find(lookup_name);
        if (struct_it != _struct_ast_nodes.end())
        {
            auto struct_node = struct_it->second;
            const auto &fields = struct_node->fields();

            for (size_t i = 0; i < fields.size(); ++i)
            {
                if (fields[i] && fields[i]->name() == field_name)
                {
                    std::cout << "[TypeMapper] Found field '" << field_name << "' at index " << i << " in struct '" << lookup_name << "'" << std::endl;
                    return static_cast<int>(i);
                }
            }
        }

        std::cerr << "[TypeMapper] Field '" << field_name << "' not found in type '" << type_name << "' (lookup name: '" << lookup_name << "')" << std::endl;
        return -1; // Field not found
    }

    //===================================================================
    // AST Node Integration
    //===================================================================

    void TypeMapper::register_class_ast_node(Cryo::ClassDeclarationNode *class_node)
    {
        if (!class_node)
        {
            std::cerr << "[TypeMapper] Attempted to register null class node" << std::endl;
            return;
        }

        std::string class_name = class_node->name();
        _class_ast_nodes[class_name] = class_node;

        std::cout << "[TypeMapper] Registered AST node for class: " << class_name << std::endl;
        std::cout << "[TypeMapper] Class has " << class_node->fields().size() << " fields" << std::endl;

        // Debug: Print field names
        for (const auto &field : class_node->fields())
        {
            if (field)
            {
                std::cout << "[TypeMapper]   Field: " << field->name() << " : " << field->type_annotation() << std::endl;
            }
        }
    }

    void TypeMapper::register_struct_ast_node(Cryo::StructDeclarationNode *struct_node)
    {
        if (!struct_node)
        {
            std::cerr << "[TypeMapper] Attempted to register null struct node" << std::endl;
            return;
        }

        std::string struct_name = struct_node->name();
        _struct_ast_nodes[struct_name] = struct_node;

        std::cout << "[TypeMapper] Registered AST node for struct: " << struct_name << std::endl;
        std::cout << "[TypeMapper] Struct has " << struct_node->fields().size() << " fields" << std::endl;

        // Debug: Print field names
        for (const auto &field : struct_node->fields())
        {
            if (field)
            {
                std::cout << "[TypeMapper]   Field: " << field->name() << " : " << field->type_annotation() << std::endl;
            }
        }
    }

} // namespace Cryo::Codegen