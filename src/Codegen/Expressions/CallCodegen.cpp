#include "Codegen/Expressions/CallCodegen.hpp"
#include "Codegen/CodegenVisitor.hpp"
#include "Codegen/Memory/MemoryCodegen.hpp"
#include "Codegen/Declarations/GenericCodegen.hpp"
#include "Codegen/Intrinsics.hpp"
#include "AST/ASTVisitor.hpp"
#include "Utils/Logger.hpp"

namespace Cryo::Codegen
{
    //===================================================================
    // Static Data
    //===================================================================

    const std::unordered_set<std::string> CallCodegen::_primitive_types = {
        "i8", "i16", "i32", "i64", "i128",
        "u8", "u16", "u32", "u64", "u128",
        "f32", "f64",
        "bool", "char", "void", "string"};

    const std::unordered_set<std::string> CallCodegen::_runtime_functions = {
        "cryo_alloc", "cryo_free", "cryo_realloc",
        "cryo_print", "cryo_println", "cryo_to_string",
        "cryo_panic", "cryo_assert"};

    const std::unordered_set<std::string> CallCodegen::_intrinsic_functions = {
        "__malloc__", "__free__", "__realloc__", "__calloc__",
        "__memcpy__", "__memset__", "__memcmp__", "__memmove__",
        "__ptr_add__", "__ptr_sub__", "__ptr_diff__",
        "__strlen__", "__strcmp__", "__strcpy__", "__strcat__",
        "__printf__", "__sprintf__", "__fprintf__",
        "__sqrt__", "__pow__", "__sin__", "__cos__",
        "__syscall_write__", "__syscall_read__", "__syscall_exit__",
        "__syscall_open__", "__syscall_close__", "__syscall_lseek__",
        "__panic__", "__float32_to_string__", "__float64_to_string__"};

    //===================================================================
    // Construction
    //===================================================================

    CallCodegen::CallCodegen(CodegenContext &ctx)
        : ICodegenComponent(ctx)
    {
    }

    //===================================================================
    // Main Entry Point
    //===================================================================

    llvm::Value *CallCodegen::generate(Cryo::CallExpressionNode *node)
    {
        if (!node)
        {
            report_error(ErrorCode::E0636_UNDEFINED_FUNCTION_CALL, "Null call expression node");
            return nullptr;
        }

        // Extract function name from callee
        std::string function_name = extract_function_name(node->callee());
        if (function_name.empty())
        {
            LOG_DEBUG(Cryo::LogComponent::CODEGEN, "CallCodegen: Could not extract function name");
        }
        else
        {
            LOG_DEBUG(Cryo::LogComponent::CODEGEN, "CallCodegen: Processing call to '{}'", function_name);
        }

        // Classify and dispatch
        CallKind kind = classify_call(node);

        switch (kind)
        {
        case CallKind::PrimitiveConstructor:
            return generate_primitive_constructor(node, function_name);

        case CallKind::Intrinsic:
            return generate_intrinsic(node, function_name);

        case CallKind::RuntimeFunction:
            return generate_runtime_call(node, function_name);

        case CallKind::StructConstructor:
            return generate_struct_constructor(node, function_name);

        case CallKind::ClassConstructor:
            return generate_class_constructor(node, function_name);

        case CallKind::EnumVariant:
        {
            // Parse enum::variant from function name
            size_t sep = function_name.rfind("::");
            if (sep != std::string::npos)
            {
                std::string enum_name = function_name.substr(0, sep);
                std::string variant_name = function_name.substr(sep + 2);
                return generate_enum_variant(node, enum_name, variant_name);
            }
            report_error(ErrorCode::E0636_UNDEFINED_FUNCTION_CALL, node,
                         "Invalid enum variant syntax: " + function_name);
            return nullptr;
        }

        case CallKind::StaticMethod:
        {
            // Parse Type::method from function name
            size_t sep = function_name.rfind("::");
            if (sep != std::string::npos)
            {
                std::string type_name = function_name.substr(0, sep);
                std::string method_name = function_name.substr(sep + 2);
                return generate_static_method(node, type_name, method_name);
            }
            report_error(ErrorCode::E0636_UNDEFINED_FUNCTION_CALL, node,
                         "Invalid static method syntax: " + function_name);
            return nullptr;
        }

        case CallKind::InstanceMethod:
        {
            // For instance methods, we need to generate the receiver
            if (auto *member = dynamic_cast<MemberAccessNode *>(node->callee()))
            {
                llvm::Value *receiver = nullptr;

                // Check if the receiver object is itself a member access (e.g., this.heap_manager)
                // In that case, we need the address (pointer) to the member, not the loaded value
                if (auto *nested_member = dynamic_cast<MemberAccessNode *>(member->object()))
                {
                    // Generate member address for nested member access
                    receiver = generate_member_receiver_address(nested_member);
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "Generated member address for nested receiver: {}",
                              nested_member->member());
                }
                else
                {
                    // For simple receivers, we need to check if it's a global variable
                    // Global struct variables need their ADDRESS passed to methods, not the loaded value
                    // Otherwise modifications won't persist back to the global
                    if (auto *identifier = dynamic_cast<IdentifierNode *>(member->object()))
                    {
                        std::string name = identifier->name();

                        // Check if it's a global variable
                        if (llvm::GlobalVariable *global = module()->getGlobalVariable(name))
                        {
                            // For global struct variables, return the global's address directly
                            // This allows methods to modify the global in-place
                            receiver = global;
                            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                      "Using global variable address for method receiver: {}", name);
                        }
                        else
                        {
                            // Try with namespace qualification
                            auto candidates = generate_lookup_candidates(name, Cryo::SymbolKind::Variable);
                            for (const auto &candidate : candidates)
                            {
                                if (llvm::GlobalVariable *g = module()->getGlobalVariable(candidate))
                                {
                                    receiver = g;
                                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                              "Using qualified global variable address for method receiver: {}", candidate);
                                    break;
                                }
                            }
                        }

                        // If not a global, fall back to generating the expression
                        if (!receiver)
                        {
                            receiver = generate_expression(member->object());
                        }
                    }
                    else
                    {
                        // For other receivers (like 'this'), generate the expression value
                        receiver = generate_expression(member->object());
                    }
                }

                if (!receiver)
                {
                    report_error(ErrorCode::E0636_UNDEFINED_FUNCTION_CALL, node,
                                 "Failed to generate receiver for method call");
                    return nullptr;
                }

                // Ensure receiver is a pointer type (methods expect ptr to self)
                if (!receiver->getType()->isPointerTy())
                {
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "Method receiver is not a pointer, creating temporary storage");
                    // Create an alloca to store the value and pass the pointer
                    llvm::AllocaInst *temp = create_entry_alloca(receiver->getType(), "method.receiver.tmp");
                    builder().CreateStore(receiver, temp);
                    receiver = temp;
                }

                return generate_instance_method(node, member, receiver, member->member());
            }
            report_error(ErrorCode::E0636_UNDEFINED_FUNCTION_CALL, node,
                         "Invalid instance method call");
            return nullptr;
        }

        case CallKind::FreeFunction:
        {
            LOG_DEBUG(Cryo::LogComponent::CODEGEN, "FreeFunction: resolving '{}'", function_name);
            llvm::Function *fn = resolve_function(function_name);
            if (!fn)
            {
                report_error(ErrorCode::E0636_UNDEFINED_FUNCTION_CALL, node,
                             "Unknown function: " + function_name);
                return nullptr;
            }
            LOG_DEBUG(Cryo::LogComponent::CODEGEN, "FreeFunction: resolved '{}' to LLVM function '{}'",
                      function_name, fn->getName().str());
            return generate_free_function(node, fn);
        }

        case CallKind::GenericInstantiation:
        {
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "GenericInstantiation: Processing '{}'", function_name);

            // Parse the generic type name: "GenericStruct<int>" -> base="GenericStruct", type_args=["int"]
            size_t open_bracket = function_name.find('<');
            size_t close_bracket = function_name.rfind('>');

            if (open_bracket == std::string::npos || close_bracket == std::string::npos)
            {
                // Fallback to function resolution
                llvm::Function *fn = resolve_function(function_name);
                if (fn)
                {
                    return generate_free_function(node, fn);
                }
                report_error(ErrorCode::E0636_UNDEFINED_FUNCTION_CALL, node,
                             "Failed to resolve generic instantiation: " + function_name);
                return nullptr;
            }

            std::string base_name = function_name.substr(0, open_bracket);
            std::string type_args_str = function_name.substr(open_bracket + 1, close_bracket - open_bracket - 1);

            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "GenericInstantiation: base='{}', type_args_str='{}'", base_name, type_args_str);

            // Get the GenericCodegen component
            CodegenVisitor *visitor = ctx().visitor();
            GenericCodegen *generics = visitor ? visitor->get_generics() : nullptr;

            if (!generics)
            {
                report_error(ErrorCode::E0636_UNDEFINED_FUNCTION_CALL, node,
                             "GenericCodegen not available for: " + function_name);
                return nullptr;
            }

            // Check if base_name is a registered generic struct template
            if (generics->is_generic_template(base_name))
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "GenericInstantiation: '{}' is a generic template, instantiating as struct constructor",
                          base_name);

                // Parse type arguments (simple single type arg for now)
                // TODO: Handle multiple type arguments with proper parsing
                std::vector<Cryo::Type *> type_args;

                // Trim whitespace from type_args_str
                size_t start = type_args_str.find_first_not_of(" \t");
                size_t end = type_args_str.find_last_not_of(" \t");
                if (start != std::string::npos && end != std::string::npos)
                {
                    type_args_str = type_args_str.substr(start, end - start + 1);
                }

                // Resolve the type argument - try struct/class types first
                Cryo::Type *arg_type = symbols().get_type_context()->lookup_struct_type(type_args_str);
                if (!arg_type)
                {
                    arg_type = symbols().get_type_context()->lookup_class_type(type_args_str);
                }
                if (!arg_type)
                {
                    // Try common primitive types using convenience methods
                    if (type_args_str == "int" || type_args_str == "i32")
                    {
                        arg_type = symbols().get_type_context()->get_i32_type();
                    }
                    else if (type_args_str == "i64")
                    {
                        arg_type = symbols().get_type_context()->get_i64_type();
                    }
                    else if (type_args_str == "f32" || type_args_str == "float")
                    {
                        arg_type = symbols().get_type_context()->get_f32_type();
                    }
                    else if (type_args_str == "f64" || type_args_str == "double")
                    {
                        arg_type = symbols().get_type_context()->get_f64_type();
                    }
                    else if (type_args_str == "string")
                    {
                        arg_type = symbols().get_type_context()->get_string_type();
                    }
                    else if (type_args_str == "bool" || type_args_str == "boolean")
                    {
                        arg_type = symbols().get_type_context()->get_boolean_type();
                    }
                }

                if (arg_type)
                {
                    type_args.push_back(arg_type);
                }

                // Instantiate the generic struct type
                llvm::StructType *instantiated_type = generics->instantiate_struct(base_name, type_args);
                if (!instantiated_type)
                {
                    report_error(ErrorCode::E0625_LITERAL_GENERATION_ERROR, node,
                                 "Failed to instantiate generic struct: " + function_name);
                    return nullptr;
                }

                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "GenericInstantiation: Instantiated struct type '{}'",
                          instantiated_type->getName().str());

                // Now generate the struct constructor call using the mangled name
                return generate_struct_constructor(node, instantiated_type->getName().str());
            }

            // Not a generic struct, try as a function
            llvm::Function *fn = resolve_function(function_name);
            if (fn)
            {
                return generate_free_function(node, fn);
            }
            report_error(ErrorCode::E0636_UNDEFINED_FUNCTION_CALL, node,
                         "Failed to resolve generic instantiation: " + function_name);
            return nullptr;
        }

        default:
            report_error(ErrorCode::E0636_UNDEFINED_FUNCTION_CALL, node,
                         "Unhandled call kind for: " + function_name);
            return nullptr;
        }
    }

    //===================================================================
    // Call Classification
    //===================================================================

    CallCodegen::CallKind CallCodegen::classify_call(Cryo::CallExpressionNode *node)
    {
        if (!node || !node->callee())
        {
            return CallKind::Unknown;
        }

        // Check for identifier callee
        if (auto *identifier = dynamic_cast<IdentifierNode *>(node->callee()))
        {
            std::string name = identifier->name();

            // Check primitive constructors first
            if (is_primitive_constructor(name))
            {
                return CallKind::PrimitiveConstructor;
            }

            // STDLIB FUNCTION INTRINSIC REDIRECTION: Handle problematic stdlib functions
            // Redirect printf directly to intrinsic to avoid variadic forwarding issues
            if (name == "printf" || name == "IO::printf" || name == "std::IO::printf")
            {
                return CallKind::Intrinsic;
            }

            // Check for intrinsics
            if (is_intrinsic(name))
            {
                return CallKind::Intrinsic;
            }

            // Check for runtime functions
            if (is_runtime_function(name))
            {
                return CallKind::RuntimeFunction;
            }

            // Check if it's a known class type (heap-allocated) - check BEFORE struct
            // Classes and structs both use LLVM struct types, but classes use heap allocation
            if (is_class_type(name))
            {
                return CallKind::ClassConstructor;
            }

            // Check if it's a known struct type (stack-allocated)
            if (is_struct_type(name))
            {
                return CallKind::StructConstructor;
            }

            // Check for generic instantiation (contains angle brackets)
            if (name.find('<') != std::string::npos)
            {
                return CallKind::GenericInstantiation;
            }

            // Default to free function
            return CallKind::FreeFunction;
        }

        // Check for member access callee
        if (auto *member = dynamic_cast<MemberAccessNode *>(node->callee()))
        {
            std::string method_name = member->member();

            // Check if method is an intrinsic
            if (is_intrinsic(method_name))
            {
                return CallKind::Intrinsic;
            }

            // Check if object is an identifier (could be namespace or variable)
            if (auto *obj_id = dynamic_cast<IdentifierNode *>(member->object()))
            {
                std::string obj_name = obj_id->name();

                // Check if it's an enum variant
                if (is_enum_type(obj_name))
                {
                    return CallKind::EnumVariant;
                }

                // Check if it's a static method call (Type::method)
                if (is_struct_type(obj_name))
                {
                    return CallKind::StaticMethod;
                }

                // Check if obj_name is a variable (instance method call)
                if (values().get_value(obj_name) || values().get_alloca(obj_name))
                {
                    return CallKind::InstanceMethod;
                }

                // Could be a namespace-qualified function
                return CallKind::FreeFunction;
            }

            // Object is not a simple identifier - likely an instance method
            return CallKind::InstanceMethod;
        }

        return CallKind::Unknown;
    }

    //===================================================================
    // Specialized Generators
    //===================================================================

    llvm::Value *CallCodegen::generate_primitive_constructor(Cryo::CallExpressionNode *node,
                                                             const std::string &type_name)
    {
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "Generating primitive constructor for type: {}", type_name);

        // Generate arguments
        auto args = generate_arguments(node->arguments());
        if (args.empty())
        {
            report_error(ErrorCode::E0636_UNDEFINED_FUNCTION_CALL, node,
                         "Primitive constructor requires at least one argument");
            return nullptr;
        }

        llvm::Value *source = args[0];
        llvm::Type *source_type = source->getType();
        llvm::Type *target_type = types().get_type(type_name);

        if (!target_type)
        {
            report_error(ErrorCode::E0636_UNDEFINED_FUNCTION_CALL, node,
                         "Unknown primitive type: " + type_name);
            return nullptr;
        }

        // Handle conversions
        if (source_type == target_type)
        {
            return source; // No conversion needed
        }

        // Integer to integer
        if (source_type->isIntegerTy() && target_type->isIntegerTy())
        {
            unsigned src_bits = source_type->getIntegerBitWidth();
            unsigned dst_bits = target_type->getIntegerBitWidth();

            if (dst_bits > src_bits)
            {
                // Check if source type is signed (heuristic: starts with 'i' not 'u')
                bool is_signed = type_name.empty() || type_name[0] == 'i';
                if (is_signed)
                {
                    return builder().CreateSExt(source, target_type, "sext");
                }
                else
                {
                    return builder().CreateZExt(source, target_type, "zext");
                }
            }
            else
            {
                return builder().CreateTrunc(source, target_type, "trunc");
            }
        }

        // Float to float
        if (source_type->isFloatingPointTy() && target_type->isFloatingPointTy())
        {
            if (target_type->isDoubleTy() && source_type->isFloatTy())
            {
                return builder().CreateFPExt(source, target_type, "fpext");
            }
            else
            {
                return builder().CreateFPTrunc(source, target_type, "fptrunc");
            }
        }

        // Integer to float
        if (source_type->isIntegerTy() && target_type->isFloatingPointTy())
        {
            bool is_signed = type_name.empty() || type_name[0] != 'u';
            if (is_signed)
            {
                return builder().CreateSIToFP(source, target_type, "sitofp");
            }
            else
            {
                return builder().CreateUIToFP(source, target_type, "uitofp");
            }
        }

        // Float to integer
        if (source_type->isFloatingPointTy() && target_type->isIntegerTy())
        {
            bool is_signed = type_name.empty() || type_name[0] != 'u';
            if (is_signed)
            {
                return builder().CreateFPToSI(source, target_type, "fptosi");
            }
            else
            {
                return builder().CreateFPToUI(source, target_type, "fptoui");
            }
        }

        // Pointer to integer
        if (source_type->isPointerTy() && target_type->isIntegerTy())
        {
            return builder().CreatePtrToInt(source, target_type, "ptrtoint");
        }

        // Integer to pointer
        if (source_type->isIntegerTy() && target_type->isPointerTy())
        {
            return builder().CreateIntToPtr(source, target_type, "inttoptr");
        }

        report_error(ErrorCode::E0636_UNDEFINED_FUNCTION_CALL, node,
                     "Cannot convert to " + type_name);
        return nullptr;
    }

    llvm::Value *CallCodegen::generate_struct_constructor(Cryo::CallExpressionNode *node,
                                                          const std::string &type_name)
    {
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "Generating struct constructor for: {}", type_name);

        // Use SRM-based type resolution to find the struct type
        // This handles: current namespace, imported namespaces, aliases, global scope
        llvm::Type *struct_type = resolve_type_by_name(type_name);

        // Determine the resolved type name for constructor lookup
        std::string resolved_type_name = type_name;
        if (struct_type)
        {
            // Try to get the actual qualified name from the candidates
            auto candidates = generate_lookup_candidates(type_name, Cryo::SymbolKind::Type);
            for (const auto &candidate : candidates)
            {
                if (ctx().get_type(candidate) == struct_type ||
                    llvm::StructType::getTypeByName(llvm_ctx(), candidate) == struct_type)
                {
                    resolved_type_name = candidate;
                    break;
                }
            }
        }

        if (!struct_type || !struct_type->isStructTy())
        {
            report_error(ErrorCode::E0636_UNDEFINED_FUNCTION_CALL, node,
                         "Unknown struct type: " + type_name);
            return nullptr;
        }

        // Allocate on stack
        llvm::AllocaInst *alloca = create_entry_alloca(struct_type, type_name + ".instance");
        if (!alloca)
        {
            report_error(ErrorCode::E0636_UNDEFINED_FUNCTION_CALL, node,
                         "Failed to allocate struct instance");
            return nullptr;
        }

        // Generate arguments
        auto args = generate_arguments(node->arguments());

        // Check for explicit constructor - use the resolved type name
        llvm::Function *ctor = resolve_constructor(resolved_type_name);
        if (ctor)
        {
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "Found constructor for {}, calling with {} args", resolved_type_name, args.size() + 1);
            // Call constructor with allocated memory and arguments
            std::vector<llvm::Value *> ctor_args;
            ctor_args.push_back(alloca);
            ctor_args.insert(ctor_args.end(), args.begin(), args.end());
            builder().CreateCall(ctor, ctor_args);
        }
        else
        {
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "No explicit constructor for {}, initializing fields directly", resolved_type_name);
            // Initialize fields directly from arguments
            auto *st = llvm::cast<llvm::StructType>(struct_type);
            for (size_t i = 0; i < args.size() && i < st->getNumElements(); ++i)
            {
                llvm::Value *field_ptr = create_struct_gep(struct_type, alloca, i, "field." + std::to_string(i));
                llvm::Value *arg = cast_if_needed(args[i], st->getElementType(i));
                create_store(arg, field_ptr);
            }
        }

        return alloca;
    }

    llvm::Value *CallCodegen::generate_class_constructor(Cryo::CallExpressionNode *node,
                                                         const std::string &type_name)
    {
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "Generating class constructor for: {}", type_name);

        // Use SRM-based type resolution to find the class type
        // This handles: current namespace, imported namespaces, aliases, global scope
        llvm::Type *class_type = resolve_type_by_name(type_name);

        // Determine the resolved type name for constructor lookup
        std::string resolved_type_name = type_name;
        if (class_type)
        {
            // Try to get the actual qualified name from the candidates
            auto candidates = generate_lookup_candidates(type_name, Cryo::SymbolKind::Type);
            for (const auto &candidate : candidates)
            {
                if (ctx().get_type(candidate) == class_type ||
                    llvm::StructType::getTypeByName(llvm_ctx(), candidate) == class_type)
                {
                    resolved_type_name = candidate;
                    break;
                }
            }
        }

        if (!class_type || !class_type->isStructTy())
        {
            report_error(ErrorCode::E0635_TYPE_CONSTRUCTOR_UNDEFINED, node,
                         "Unknown class type: " + type_name);
            return nullptr;
        }

        // Classes and structs have same allocation semantics: value by default
        // 'new' keyword determines heap allocation, absence means value/stack allocation
        // ClassName() creates value instance (stack allocated)
        // new ClassName() creates pointer instance (heap allocated via cryo_alloc)

        // Allocate on stack (value semantics)
        llvm::AllocaInst *alloca = create_entry_alloca(class_type, type_name + ".instance");
        if (!alloca)
        {
            report_error(ErrorCode::E0635_TYPE_CONSTRUCTOR_UNDEFINED, node,
                         "Failed to allocate class instance");
            return nullptr;
        }

        // Generate arguments
        auto args = generate_arguments(node->arguments());

        // Look for constructor - use the resolved type name
        llvm::Function *ctor = resolve_constructor(resolved_type_name);
        if (ctor)
        {
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "Found constructor for {}, calling with {} args", resolved_type_name, args.size() + 1);
            // Call constructor with allocated memory and arguments
            std::vector<llvm::Value *> ctor_args;
            ctor_args.push_back(alloca);
            ctor_args.insert(ctor_args.end(), args.begin(), args.end());
            builder().CreateCall(ctor, ctor_args);
        }
        else
        {
            // Log the failure for debugging
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                     "No explicit constructor for {}, initializing fields directly", resolved_type_name);

            // Initialize fields as fallback
            auto *st = llvm::cast<llvm::StructType>(class_type);
            for (size_t i = 0; i < args.size() && i < st->getNumElements(); ++i)
            {
                llvm::Value *field_ptr = create_struct_gep(class_type, alloca, i, "field." + std::to_string(i));
                llvm::Value *arg = cast_if_needed(args[i], st->getElementType(i));
                create_store(arg, field_ptr);
            }
        }

        return alloca;
    }

    llvm::Value *CallCodegen::generate_enum_variant(Cryo::CallExpressionNode *node,
                                                    const std::string &enum_name,
                                                    const std::string &variant_name)
    {
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "Generating enum variant: {}::{}", enum_name, variant_name);

        std::string qualified_variant = enum_name + "::" + variant_name;

        // Generate arguments
        auto args = generate_arguments(node->arguments());

        // Look for variant constructor function (for complex variants with payloads)
        llvm::Function *ctor = module()->getFunction(qualified_variant);
        if (ctor)
        {
            return builder().CreateCall(ctor, args, variant_name);
        }

        // For simple enum variants (no payload), look up the registered constant value
        auto &enum_variants = ctx().enum_variants_map();
        auto it = enum_variants.find(qualified_variant);
        if (it != enum_variants.end())
        {
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "Found simple enum variant: {} -> constant", qualified_variant);
            return it->second;
        }

        // Try with namespace prefix
        std::string current_ns = ctx().namespace_context();
        if (!current_ns.empty())
        {
            std::string ns_qualified = current_ns + "::" + qualified_variant;
            it = enum_variants.find(ns_qualified);
            if (it != enum_variants.end())
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "Found simple enum variant with namespace: {} -> constant", ns_qualified);
                return it->second;
            }
        }

        // Look up the enum type to check if it exists
        llvm::Type *enum_type = ctx().get_type(enum_name);
        if (!enum_type)
        {
            report_error(ErrorCode::E0633_FUNCTION_BODY_ERROR, node,
                         "Unknown enum type: " + enum_name);
            return nullptr;
        }

        report_error(ErrorCode::E0633_FUNCTION_BODY_ERROR, node,
                     "Enum variant not found: " + qualified_variant);
        return nullptr;
    }

    llvm::Value *CallCodegen::generate_intrinsic(Cryo::CallExpressionNode *node,
                                                 const std::string &intrinsic_name)
    {
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "Generating intrinsic call: {}", intrinsic_name);

        // Generate arguments
        auto args = generate_arguments(node->arguments());

        // Delegate to the Intrinsics class via context if available
        // For now, handle common intrinsics inline

        if (intrinsic_name == "__malloc__")
        {
            if (args.empty())
            {
                report_error(ErrorCode::E0633_FUNCTION_BODY_ERROR, node, "__malloc__ requires size argument");
                return nullptr;
            }
            llvm::Function *malloc_fn = module()->getFunction("malloc");
            if (!malloc_fn)
            {
                llvm::Type *ptr_type = llvm::PointerType::get(llvm_ctx(), 0);
                llvm::Type *i64_type = llvm::Type::getInt64Ty(llvm_ctx());
                llvm::FunctionType *fn_type = llvm::FunctionType::get(ptr_type, {i64_type}, false);
                malloc_fn = llvm::Function::Create(fn_type, llvm::Function::ExternalLinkage, "malloc", module());
            }
            llvm::Value *size = cast_if_needed(args[0], llvm::Type::getInt64Ty(llvm_ctx()));
            return builder().CreateCall(malloc_fn, {size}, "malloc_result");
        }

        if (intrinsic_name == "__free__")
        {
            if (args.empty())
            {
                report_error(ErrorCode::E0636_UNDEFINED_FUNCTION_CALL, node, "__free__ requires pointer argument");
                return nullptr;
            }
            llvm::Function *free_fn = module()->getFunction("free");
            if (!free_fn)
            {
                llvm::Type *ptr_type = llvm::PointerType::get(llvm_ctx(), 0);
                llvm::Type *void_type = llvm::Type::getVoidTy(llvm_ctx());
                llvm::FunctionType *fn_type = llvm::FunctionType::get(void_type, {ptr_type}, false);
                free_fn = llvm::Function::Create(fn_type, llvm::Function::ExternalLinkage, "free", module());
            }
            return builder().CreateCall(free_fn, {args[0]});
        }

        if (intrinsic_name == "__printf__" || intrinsic_name == "printf" || 
            intrinsic_name == "IO::printf" || intrinsic_name == "std::IO::printf")
        {
            // Use the full intrinsic system with Windows ABI fixes
            LOG_DEBUG(Cryo::LogComponent::CODEGEN, "[STDLIB REDIRECT] Redirecting {} to __printf__ intrinsic", intrinsic_name);
            
            // Generate arguments
            auto args = generate_arguments(node->arguments());
            
            // Delegate to the proper intrinsic system that handles Windows ABI
            Intrinsics intrinsic_gen(ctx().llvm_manager(), ctx().diagnostics());
            return intrinsic_gen.generate_intrinsic_call(node, "__printf__", args);
        }

        // Other intrinsics would be handled similarly
        report_error(ErrorCode::E0636_UNDEFINED_FUNCTION_CALL, node,
                     "Unhandled intrinsic: " + intrinsic_name);
        return nullptr;
    }

    llvm::Value *CallCodegen::generate_runtime_call(Cryo::CallExpressionNode *node,
                                                    const std::string &function_name)
    {
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "Generating runtime call: {}", function_name);

        // Generate arguments
        auto args = generate_arguments(node->arguments());

        // Get qualified runtime function name
        std::string qualified_name = "std::Runtime::" + function_name;

        // Look up or create function declaration
        llvm::Function *fn = module()->getFunction(qualified_name);
        if (!fn)
        {
            // Try unqualified name
            fn = module()->getFunction(function_name);
        }

        if (!fn)
        {
            // Runtime function not found - create a declaration for it
            // This allows linking to resolve the actual implementation later
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "Runtime function '{}' not found, creating declaration as '{}'",
                      function_name, qualified_name);

            fn = declare_runtime_function(function_name, qualified_name);
            if (!fn)
            {
                report_error(ErrorCode::E0636_UNDEFINED_FUNCTION_CALL, node,
                             "Runtime function not found and could not create declaration: " + function_name);
                return nullptr;
            }
        }

        // Prepare arguments with type coercion
        std::vector<llvm::Value *> coerced_args;
        llvm::FunctionType *fn_type = fn->getFunctionType();

        for (size_t i = 0; i < args.size(); ++i)
        {
            if (i < fn_type->getNumParams())
            {
                coerced_args.push_back(cast_if_needed(args[i], fn_type->getParamType(i)));
            }
            else if (fn_type->isVarArg())
            {
                coerced_args.push_back(args[i]);
            }
        }

        std::string result_name = fn->getReturnType()->isVoidTy() ? "" : function_name + ".result";
        return builder().CreateCall(fn, coerced_args, result_name);
    }

    llvm::Value *CallCodegen::generate_static_method(Cryo::CallExpressionNode *node,
                                                     const std::string &type_name,
                                                     const std::string &method_name)
    {
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "Generating static method: {}::{}", type_name, method_name);

        // Generate arguments
        auto args = generate_arguments(node->arguments());

        // Resolve the method
        llvm::Function *method = resolve_method(type_name, method_name);
        if (!method)
        {
            report_error(ErrorCode::E0636_UNDEFINED_FUNCTION_CALL, node,
                         "Static method not found: " + type_name + "::" + method_name);
            return nullptr;
        }

        // Call with arguments (no implicit 'this')
        std::vector<llvm::Value *> coerced_args;
        llvm::FunctionType *fn_type = method->getFunctionType();

        for (size_t i = 0; i < args.size() && i < fn_type->getNumParams(); ++i)
        {
            coerced_args.push_back(cast_if_needed(args[i], fn_type->getParamType(i)));
        }

        std::string result_name = method->getReturnType()->isVoidTy() ? "" : method_name + ".result";
        return builder().CreateCall(method, coerced_args, result_name);
    }

    llvm::Value *CallCodegen::generate_instance_method(Cryo::CallExpressionNode *node,
                                                       Cryo::MemberAccessNode *callee,
                                                       llvm::Value *receiver,
                                                       const std::string &method_name)
    {
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "Generating instance method: {}", method_name);

        if (!receiver)
        {
            report_error(ErrorCode::E0636_UNDEFINED_FUNCTION_CALL, node,
                         "Null receiver for method call");
            return nullptr;
        }

        // Generate arguments
        auto args = generate_arguments(node->arguments());

        // Determine receiver type name from the callee's object expression
        std::string type_name;
        if (callee && callee->object())
        {
            Cryo::Type *obj_type = callee->object()->get_resolved_type();
            if (obj_type)
            {
                type_name = obj_type->to_string();
                // Strip pointer suffix if present
                if (!type_name.empty() && type_name.back() == '*')
                {
                    type_name.pop_back();
                }
                // Handle pointer types explicitly
                if (obj_type->kind() == Cryo::TypeKind::Pointer)
                {
                    auto *ptr_type = dynamic_cast<Cryo::PointerType *>(obj_type);
                    if (ptr_type && ptr_type->pointee_type())
                    {
                        type_name = ptr_type->pointee_type()->to_string();
                    }
                }
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "Instance method receiver type: {}", type_name);
            }

            // Fallback: try to get type from variable_types_map
            if (type_name.empty())
            {
                if (auto *id = dynamic_cast<IdentifierNode *>(callee->object()))
                {
                    auto &var_types = ctx().variable_types_map();
                    auto it = var_types.find(id->name());
                    if (it != var_types.end() && it->second)
                    {
                        type_name = it->second->to_string();
                        // Strip pointer suffix if present
                        if (!type_name.empty() && type_name.back() == '*')
                        {
                            type_name.pop_back();
                        }
                        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                  "Instance method receiver type from variable map: {}", type_name);
                    }
                }
                // Additional fallback for nested member access (e.g., this.stack_trace.capture())
                else if (auto *member_access = dynamic_cast<MemberAccessNode *>(callee->object()))
                {
                    // For member access, try to infer type from the member name and context
                    std::string member_name = member_access->member();
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "Attempting type resolution for nested member access: {}", member_name);

                    // Special case for common field types in runtime
                    if (member_name == "stack_trace")
                    {
                        type_name = "StackTrace";
                        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                  "Resolved nested member access type: {} -> {}", member_name, type_name);
                    }
                }
            }
        }

        // Resolve the method
        llvm::Function *method = resolve_method(type_name, method_name);
        if (!method)
        {
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "Method not found via normal resolution: type='{}', method='{}' - trying fallbacks",
                      type_name, method_name);

            // Additional fallback: try fully-qualified names for known problematic cases
            if (type_name == "StackTrace" && method_name == "capture")
            {
                // Try the known fully-qualified function name
                method = module()->getFunction("std::Runtime::StackTrace::capture");
                if (method)
                {
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "Found method via direct fallback: std::Runtime::StackTrace::capture");
                }
                else
                {
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "Direct fallback failed - function std::Runtime::StackTrace::capture not found in module");
                }
            }
        }

        if (!method)
        {
            // Report error if method not found
            report_error(ErrorCode::E0636_UNDEFINED_FUNCTION_CALL, node,
                         "Instance method not found: " + method_name);
            return nullptr;
        }

        // Prepare arguments: receiver (this) + method args
        std::vector<llvm::Value *> call_args;
        call_args.push_back(receiver);

        llvm::FunctionType *fn_type = method->getFunctionType();
        for (size_t i = 0; i < args.size(); ++i)
        {
            size_t param_idx = i + 1; // Skip 'this' parameter
            if (param_idx < fn_type->getNumParams())
            {
                call_args.push_back(cast_if_needed(args[i], fn_type->getParamType(param_idx)));
            }
            else if (fn_type->isVarArg())
            {
                call_args.push_back(args[i]);
            }
        }

        std::string result_name = method->getReturnType()->isVoidTy() ? "" : method_name + ".result";
        return builder().CreateCall(method, call_args, result_name);
    }

    llvm::Value *CallCodegen::generate_free_function(Cryo::CallExpressionNode *node,
                                                     llvm::Function *function)
    {
        if (!function)
        {
            report_error(ErrorCode::E0636_UNDEFINED_FUNCTION_CALL, node, "Null function");
            return nullptr;
        }

        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "Generating free function call: {}",
                  function->getName().str());

        // Generate arguments
        auto args = generate_arguments(node->arguments());

        // Coerce argument types
        std::vector<llvm::Value *> coerced_args;
        llvm::FunctionType *fn_type = function->getFunctionType();

        for (size_t i = 0; i < args.size(); ++i)
        {
            if (i < fn_type->getNumParams())
            {
                coerced_args.push_back(cast_if_needed(args[i], fn_type->getParamType(i)));
            }
            else if (fn_type->isVarArg())
            {
                coerced_args.push_back(args[i]);
            }
        }

        std::string result_name = function->getReturnType()->isVoidTy() ? "" : "call.result";
        return builder().CreateCall(function, coerced_args, result_name);
    }

    //===================================================================
    // Argument Handling
    //===================================================================

    std::vector<llvm::Value *> CallCodegen::generate_arguments(
        const std::vector<std::unique_ptr<Cryo::ExpressionNode>> &args)
    {
        std::vector<llvm::Value *> result;
        result.reserve(args.size());

        for (const auto &arg : args)
        {
            if (arg)
            {
                llvm::Value *value = generate_expression(arg.get());
                if (value)
                {
                    result.push_back(value);
                }
                else
                {
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN, "Failed to generate argument");
                }
            }
        }

        return result;
    }

    llvm::Value *CallCodegen::prepare_argument(llvm::Value *arg, llvm::Type *param_type)
    {
        return cast_if_needed(arg, param_type);
    }

    bool CallCodegen::check_argument_compatibility(const std::vector<llvm::Value *> &args,
                                                   llvm::FunctionType *fn_type)
    {
        if (!fn_type)
            return false;

        size_t num_params = fn_type->getNumParams();
        bool is_vararg = fn_type->isVarArg();

        if (!is_vararg && args.size() != num_params)
        {
            return false;
        }

        if (is_vararg && args.size() < num_params)
        {
            return false;
        }

        // Check that fixed parameters are compatible
        for (size_t i = 0; i < num_params; ++i)
        {
            llvm::Type *arg_type = args[i]->getType();
            llvm::Type *param_type = fn_type->getParamType(i);

            if (arg_type != param_type)
            {
                // Check if cast is possible (simplified check)
                bool can_cast = (arg_type->isIntegerTy() && param_type->isIntegerTy()) ||
                                (arg_type->isFloatingPointTy() && param_type->isFloatingPointTy()) ||
                                (arg_type->isPointerTy() && param_type->isPointerTy());
                if (!can_cast)
                {
                    return false;
                }
            }
        }

        return true;
    }

    //===================================================================
    // Function Resolution
    //===================================================================

    llvm::Function *CallCodegen::resolve_function(const std::string &name)
    {
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "resolve_function: Looking up '{}'", name);

        // Use SRM-based function resolution
        // This handles: current namespace, imported namespaces, aliases, global scope
        llvm::Function *fn = resolve_function_by_name(name);
        if (fn)
        {
            LOG_DEBUG(Cryo::LogComponent::CODEGEN, "resolve_function: Found '{}' -> LLVM name '{}'",
                      name, fn->getName().str());
            return fn;
        }

        // Check for common C library functions and create declarations if needed
        fn = get_or_create_c_function(name);
        if (fn)
            return fn;

        // Try to create forward declaration from symbol table lookup
        // This handles imported stdlib functions that aren't in the LLVM module yet
        fn = create_forward_declaration_from_symbol(name);
        if (fn)
            return fn;

        return nullptr;
    }

    llvm::Function *CallCodegen::create_forward_declaration_from_symbol(const std::string &name)
    {
        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                  "create_forward_declaration_from_symbol: Trying to find '{}' in symbol table", name);

        // Generate lookup candidates using SRM (includes imported namespaces)
        auto candidates = generate_lookup_candidates(name, Cryo::SymbolKind::Function);

        for (const auto &candidate : candidates)
        {
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "create_forward_declaration_from_symbol: Trying candidate '{}'", candidate);

            // Try to find the function in the symbol table
            Cryo::Symbol *symbol = symbols().lookup_symbol(candidate);
            if (!symbol)
            {
                // Try parsing the candidate into namespace and function name parts
                size_t last_sep = candidate.rfind("::");
                if (last_sep != std::string::npos)
                {
                    std::string ns = candidate.substr(0, last_sep);
                    std::string fn_name = candidate.substr(last_sep + 2);
                    symbol = symbols().lookup_namespaced_symbol(ns, fn_name);
                }
            }

            if (symbol && symbol->kind == Cryo::SymbolKind::Function && symbol->data_type)
            {
                Cryo::FunctionType *func_type = dynamic_cast<Cryo::FunctionType *>(symbol->data_type);
                if (func_type)
                {
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "create_forward_declaration_from_symbol: Found function '{}' in symbol table", candidate);

                    // Build LLVM function type from Cryo function type
                    llvm::Type *return_type = types().map(func_type->return_type().get());
                    if (!return_type)
                    {
                        LOG_ERROR(Cryo::LogComponent::CODEGEN,
                                  "Failed to map return type for function '{}'", candidate);
                        continue;
                    }

                    std::vector<llvm::Type *> param_types;
                    for (const auto &param : func_type->parameter_types())
                    {
                        llvm::Type *pt = types().map(param.get());
                        if (!pt)
                        {
                            LOG_ERROR(Cryo::LogComponent::CODEGEN,
                                      "Failed to map parameter type for function '{}'", candidate);
                            break;
                        }
                        param_types.push_back(pt);
                    }

                    if (param_types.size() != func_type->parameter_types().size())
                    {
                        continue; // Failed to map all parameter types
                    }

                    // Create the function type
                    llvm::FunctionType *llvm_func_type = llvm::FunctionType::get(
                        return_type, param_types, func_type->is_variadic());

                    // Create the function declaration with the qualified name
                    llvm::Function *fn = llvm::Function::Create(
                        llvm_func_type,
                        llvm::Function::ExternalLinkage,
                        candidate,
                        module());

                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "create_forward_declaration_from_symbol: Created forward declaration for '{}'", candidate);

                    // Register in context for future lookups
                    ctx().register_function(candidate, fn);

                    return fn;
                }
            }
        }

        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                  "create_forward_declaration_from_symbol: Could not find '{}' in symbol table", name);
        return nullptr;
    }

    llvm::Function *CallCodegen::get_or_create_c_function(const std::string &name)
    {
        // Map of common C functions to their signatures
        // For variadic functions: {return_type, fixed_param_types, is_variadic}

        llvm::Type *i32_type = llvm::Type::getInt32Ty(llvm_ctx());
        llvm::Type *i64_type = llvm::Type::getInt64Ty(llvm_ctx());
        llvm::Type *ptr_type = llvm::PointerType::get(llvm_ctx(), 0);
        llvm::Type *void_type = llvm::Type::getVoidTy(llvm_ctx());

        if (name == "printf" || name == "IO::printf" || name == "std::IO::printf")
        {
            llvm::Function *fn = module()->getFunction("printf");
            if (!fn)
            {
                llvm::FunctionType *fn_type = llvm::FunctionType::get(i32_type, {ptr_type}, true);
                fn = llvm::Function::Create(fn_type, llvm::Function::ExternalLinkage, "printf", module());
            }
            return fn;
        }

        if (name == "sprintf")
        {
            llvm::Function *fn = module()->getFunction("sprintf");
            if (!fn)
            {
                llvm::FunctionType *fn_type = llvm::FunctionType::get(i32_type, {ptr_type, ptr_type}, true);
                fn = llvm::Function::Create(fn_type, llvm::Function::ExternalLinkage, "sprintf", module());
            }
            return fn;
        }

        if (name == "snprintf")
        {
            llvm::Function *fn = module()->getFunction("snprintf");
            if (!fn)
            {
                llvm::FunctionType *fn_type = llvm::FunctionType::get(i32_type, {ptr_type, i64_type, ptr_type}, true);
                fn = llvm::Function::Create(fn_type, llvm::Function::ExternalLinkage, "snprintf", module());
            }
            return fn;
        }

        if (name == "fprintf")
        {
            llvm::Function *fn = module()->getFunction("fprintf");
            if (!fn)
            {
                llvm::FunctionType *fn_type = llvm::FunctionType::get(i32_type, {ptr_type, ptr_type}, true);
                fn = llvm::Function::Create(fn_type, llvm::Function::ExternalLinkage, "fprintf", module());
            }
            return fn;
        }

        if (name == "puts")
        {
            llvm::Function *fn = module()->getFunction("puts");
            if (!fn)
            {
                llvm::FunctionType *fn_type = llvm::FunctionType::get(i32_type, {ptr_type}, false);
                fn = llvm::Function::Create(fn_type, llvm::Function::ExternalLinkage, "puts", module());
            }
            return fn;
        }

        if (name == "putchar")
        {
            llvm::Function *fn = module()->getFunction("putchar");
            if (!fn)
            {
                llvm::FunctionType *fn_type = llvm::FunctionType::get(i32_type, {i32_type}, false);
                fn = llvm::Function::Create(fn_type, llvm::Function::ExternalLinkage, "putchar", module());
            }
            return fn;
        }

        if (name == "exit")
        {
            llvm::Function *fn = module()->getFunction("exit");
            if (!fn)
            {
                llvm::FunctionType *fn_type = llvm::FunctionType::get(void_type, {i32_type}, false);
                fn = llvm::Function::Create(fn_type, llvm::Function::ExternalLinkage, "exit", module());
            }
            return fn;
        }

        if (name == "abort")
        {
            llvm::Function *fn = module()->getFunction("abort");
            if (!fn)
            {
                llvm::FunctionType *fn_type = llvm::FunctionType::get(void_type, {}, false);
                fn = llvm::Function::Create(fn_type, llvm::Function::ExternalLinkage, "abort", module());
            }
            return fn;
        }

        return nullptr;
    }

    llvm::Function *CallCodegen::resolve_method(const std::string &type_name,
                                                const std::string &method_name)
    {
        if (type_name.empty())
        {
            LOG_DEBUG(Cryo::LogComponent::CODEGEN, "resolve_method: Empty type name for method {}", method_name);
            return nullptr;
        }

        // Use SRM-based method resolution
        // This handles: namespace qualification, imports, and aliases
        return resolve_method_by_name(type_name, method_name);
    }

    llvm::Function *CallCodegen::resolve_constructor(const std::string &type_name,
                                                     const std::vector<llvm::Type *> &arg_types)
    {
        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                  "resolve_constructor: Looking for constructor of type '{}'", type_name);

        // Generate type candidates using SRM
        auto type_candidates = generate_lookup_candidates(type_name, Cryo::SymbolKind::Type);

        // Extract simple name for constructor patterns
        auto get_simple_name = [](const std::string &name) -> std::string
        {
            size_t last_sep = name.rfind("::");
            return (last_sep != std::string::npos) ? name.substr(last_sep + 2) : name;
        };

        // For each type candidate, try constructor patterns
        for (const auto &type_candidate : type_candidates)
        {
            std::string simple_name = get_simple_name(type_candidate);

            // Try multiple constructor patterns including exact matches
            std::vector<std::string> ctor_names = {
                // Standard patterns
                type_candidate + "::init",
                type_candidate + "::new", 
                type_candidate + "::" + simple_name,
                // Exact match for CryoRuntime
                "std::Runtime::CryoRuntime::CryoRuntime",
                // Also try the simple name directly
                simple_name + "::" + simple_name
            };

            for (const auto &ctor_name : ctor_names)
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "resolve_constructor: Trying '{}'", ctor_name);

                // Check LLVM module first
                llvm::Function *fn = module()->getFunction(ctor_name);
                if (fn)
                {
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "resolve_constructor: Found '{}' in module", ctor_name);
                    return fn;
                }

                // Check context's function registry
                fn = ctx().get_function(ctor_name);
                if (fn)
                {
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "resolve_constructor: Found '{}' in function registry", ctor_name);
                    return fn;
                }
            }
        }

        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                  "resolve_constructor: No constructor found for '{}'", type_name);
        return nullptr;
    }

    //===================================================================
    // Type Checks
    //===================================================================

    bool CallCodegen::is_primitive_constructor(const std::string &name) const
    {
        return _primitive_types.find(name) != _primitive_types.end();
    }

    bool CallCodegen::is_runtime_function(const std::string &name) const
    {
        return _runtime_functions.find(name) != _runtime_functions.end();
    }

    bool CallCodegen::is_intrinsic(const std::string &name) const
    {
        // Check static set
        if (_intrinsic_functions.find(name) != _intrinsic_functions.end())
        {
            return true;
        }

        // Also check pattern: starts and ends with __
        if (name.length() > 4 && name.substr(0, 2) == "__" && name.substr(name.length() - 2) == "__")
        {
            return true;
        }

        return false;
    }

    bool CallCodegen::is_struct_type(const std::string &name) const
    {
        // Use SRM to generate type candidates and check if any is a struct type
        auto &non_const_ctx = const_cast<CodegenContext &>(ctx());
        auto candidates = non_const_ctx.srm().generate_lookup_candidates(name, Cryo::SymbolKind::Type);

        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                  "is_struct_type: Checking '{}' with {} candidates", name, candidates.size());

        for (const auto &candidate : candidates)
        {
            // Check in type registry
            llvm::Type *type = non_const_ctx.get_type(candidate);
            if (type && type->isStructTy())
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "is_struct_type: Found '{}' as '{}'", name, candidate);
                return true;
            }

            // Check LLVM context directly
            llvm::StructType *struct_type = llvm::StructType::getTypeByName(
                non_const_ctx.llvm_context(), candidate);
            if (struct_type)
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "is_struct_type: Found '{}' in LLVM context as '{}'", name, candidate);
                return true;
            }
        }

        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "is_struct_type: '{}' not found as struct type", name);
        return false;
    }

    bool CallCodegen::is_enum_type(const std::string &name) const
    {
        // Use SRM to generate type candidates and check if any is an enum type
        auto &non_const_ctx = const_cast<CodegenContext &>(ctx());
        auto candidates = non_const_ctx.srm().generate_lookup_candidates(name, Cryo::SymbolKind::Type);

        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                  "is_enum_type: Checking '{}' with {} candidates", name, candidates.size());

        for (const auto &candidate : candidates)
        {
            // Check symbol table for enum type
            Symbol *sym = const_cast<CallCodegen *>(this)->symbols().lookup_symbol(candidate);
            if (sym && sym->kind == SymbolKind::Type && sym->data_type)
            {
                if (sym->data_type->kind() == TypeKind::Enum)
                {
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "is_enum_type: Found '{}' as enum type via '{}'", name, candidate);
                    return true;
                }
            }
        }

        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "is_enum_type: '{}' not found as enum type", name);
        return false;
    }

    bool CallCodegen::is_class_type(const std::string &name) const
    {
        // Use SRM to generate type candidates and check if any is a class type
        auto &non_const_ctx = const_cast<CodegenContext &>(ctx());
        auto candidates = non_const_ctx.srm().generate_lookup_candidates(name, Cryo::SymbolKind::Type);

        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                  "is_class_type: Checking '{}' with {} candidates", name, candidates.size());

        for (const auto &candidate : candidates)
        {
            // Check symbol table for class type
            Symbol *sym = const_cast<CallCodegen *>(this)->symbols().lookup_symbol(candidate);
            if (sym && sym->kind == SymbolKind::Type && sym->data_type)
            {
                if (sym->data_type->kind() == TypeKind::Class)
                {
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "is_class_type: Found '{}' as class type via '{}'", name, candidate);
                    return true;
                }
            }
        }

        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "is_class_type: '{}' not found as class type", name);
        return false;
    }

    //===================================================================
    // Internal Helpers
    //===================================================================

    std::string CallCodegen::extract_function_name(Cryo::ExpressionNode *callee)
    {
        if (!callee)
            return "";

        if (auto *id = dynamic_cast<IdentifierNode *>(callee))
        {
            return id->name();
        }

        if (auto *member = dynamic_cast<MemberAccessNode *>(callee))
        {
            // For member access, build qualified name
            std::string obj_name = extract_function_name(member->object());
            if (!obj_name.empty())
            {
                return obj_name + "::" + member->member();
            }
            return member->member();
        }

        return "";
    }

    bool CallCodegen::extract_member_call_info(Cryo::MemberAccessNode *member,
                                               std::string &out_type,
                                               std::string &out_method)
    {
        if (!member)
            return false;

        out_method = member->member();

        if (auto *obj_id = dynamic_cast<IdentifierNode *>(member->object()))
        {
            out_type = obj_id->name();
            return true;
        }

        return false;
    }

    llvm::Value *CallCodegen::generate_expression(Cryo::ExpressionNode *expr)
    {
        if (!expr)
            return nullptr;

        // Use visitor pattern through context
        // The expression needs to be visited to generate its value
        // This delegates back to the main visitor
        expr->accept(*ctx().visitor());
        return get_result();
    }

    llvm::Value *CallCodegen::generate_member_receiver_address(Cryo::MemberAccessNode *node)
    {
        if (!node)
            return nullptr;

        std::string member_name = node->member();

        // Generate the object expression to get the base pointer
        llvm::Value *object = generate_expression(node->object());
        if (!object)
        {
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "generate_member_receiver_address: Failed to generate object");
            return nullptr;
        }

        // Ensure we have a pointer to the struct
        if (!object->getType()->isPointerTy())
        {
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "generate_member_receiver_address: Object is not a pointer type");
            return nullptr;
        }

        // Resolve struct type and field index
        llvm::StructType *struct_type = nullptr;
        std::string type_name;

        // Get the resolved type of the object expression
        Cryo::Type *obj_type = node->object()->get_resolved_type();
        if (obj_type)
        {
            type_name = obj_type->to_string();
            // Handle pointer types - get the pointee type name
            if (obj_type->kind() == Cryo::TypeKind::Pointer)
            {
                auto *ptr_type = dynamic_cast<Cryo::PointerType *>(obj_type);
                if (ptr_type && ptr_type->pointee_type())
                {
                    type_name = ptr_type->pointee_type()->to_string();
                }
            }
            else if (!type_name.empty() && type_name.back() == '*')
            {
                type_name.pop_back();
            }
        }
        else
        {
            // Fallback: Check if this is the 'this' identifier
            if (auto *identifier = dynamic_cast<Cryo::IdentifierNode *>(node->object()))
            {
                if (identifier->name() == "this")
                {
                    // Try to get 'this' type from variable_types_map
                    auto &var_types = ctx().variable_types_map();
                    auto it = var_types.find("this");
                    if (it != var_types.end() && it->second)
                    {
                        type_name = it->second->to_string();
                        if (!type_name.empty() && type_name.back() == '*')
                        {
                            type_name.pop_back();
                        }
                    }
                    else
                    {
                        // Fallback to current_type_name if available
                        type_name = ctx().current_type_name();
                    }
                }
            }
        }

        if (type_name.empty())
        {
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "generate_member_receiver_address: Could not determine type for member access");
            return nullptr;
        }

        // Look up struct type in LLVM context
        struct_type = llvm::StructType::getTypeByName(llvm_ctx(), type_name);
        if (!struct_type)
        {
            // Try context registry
            if (llvm::Type *type = ctx().get_type(type_name))
            {
                struct_type = llvm::dyn_cast<llvm::StructType>(type);
            }
        }

        if (!struct_type)
        {
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "generate_member_receiver_address: Struct type '{}' not found", type_name);
            return nullptr;
        }

        // Look up field index
        int field_idx = ctx().get_struct_field_index(type_name, member_name);
        if (field_idx < 0)
        {
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "generate_member_receiver_address: Field '{}' not found in struct '{}'",
                      member_name, type_name);
            return nullptr;
        }

        // Create GEP for field access - returns pointer to the member field
        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                  "generate_member_receiver_address: Creating GEP for {}.{} at index {}",
                  type_name, member_name, field_idx);
        return create_struct_gep(struct_type, object, static_cast<unsigned>(field_idx),
                                 member_name + ".ptr");
    }

    llvm::Function *CallCodegen::get_or_create_function(const std::string &name,
                                                        llvm::Type *return_type,
                                                        const std::vector<llvm::Type *> &param_types,
                                                        bool is_variadic)
    {
        llvm::Function *fn = module()->getFunction(name);
        if (fn)
            return fn;

        llvm::FunctionType *fn_type = llvm::FunctionType::get(return_type, param_types, is_variadic);
        return llvm::Function::Create(fn_type, llvm::Function::ExternalLinkage, name, module());
    }

    std::string CallCodegen::qualify_runtime_function(const std::string &name)
    {
        // If already qualified, return as-is
        if (name.find("::") != std::string::npos)
        {
            return name;
        }

        // Try to find the function using SRM-based resolution first
        // This will check imported namespaces and aliases
        llvm::Function *fn = resolve_function_by_name(name);
        if (fn)
        {
            return fn->getName().str();
        }

        // Fallback: use SRM to qualify with current namespace context
        // Runtime functions are registered with their full namespace during codegen
        return qualify_symbol_name(name, Cryo::SymbolKind::Function);
    }

    llvm::Function *CallCodegen::declare_runtime_function(const std::string &unqualified_name,
                                                           const std::string &qualified_name)
    {
        // Map of runtime function signatures
        // Format: function_name -> (return_type, {param_types}, is_variadic)
        llvm::Type *void_ty = llvm::Type::getVoidTy(llvm_ctx());
        llvm::Type *ptr_ty = llvm::PointerType::get(llvm_ctx(), 0);
        llvm::Type *i64_ty = llvm::Type::getInt64Ty(llvm_ctx());
        llvm::Type *i32_ty = llvm::Type::getInt32Ty(llvm_ctx());

        llvm::FunctionType *fn_type = nullptr;

        // Memory allocation functions
        if (unqualified_name == "cryo_alloc" || unqualified_name == "cryo_malloc")
        {
            // void* cryo_alloc(i64 size)
            fn_type = llvm::FunctionType::get(ptr_ty, {i64_ty}, false);
        }
        else if (unqualified_name == "cryo_free")
        {
            // void cryo_free(void* ptr)
            fn_type = llvm::FunctionType::get(void_ty, {ptr_ty}, false);
        }
        else if (unqualified_name == "cryo_realloc")
        {
            // void* cryo_realloc(void* ptr, i64 size)
            fn_type = llvm::FunctionType::get(ptr_ty, {ptr_ty, i64_ty}, false);
        }
        else if (unqualified_name == "cryo_calloc")
        {
            // void* cryo_calloc(i64 count, i64 size)
            fn_type = llvm::FunctionType::get(ptr_ty, {i64_ty, i64_ty}, false);
        }
        // String functions
        else if (unqualified_name == "cryo_strlen")
        {
            // i64 cryo_strlen(void* str)
            fn_type = llvm::FunctionType::get(i64_ty, {ptr_ty}, false);
        }
        else if (unqualified_name == "cryo_strcmp")
        {
            // i32 cryo_strcmp(void* s1, void* s2)
            fn_type = llvm::FunctionType::get(i32_ty, {ptr_ty, ptr_ty}, false);
        }
        else if (unqualified_name == "cryo_strcpy" || unqualified_name == "cryo_strcat")
        {
            // void* cryo_strcpy/strcat(void* dest, void* src)
            fn_type = llvm::FunctionType::get(ptr_ty, {ptr_ty, ptr_ty}, false);
        }
        else if (unqualified_name == "cryo_strdup")
        {
            // void* cryo_strdup(void* str)
            fn_type = llvm::FunctionType::get(ptr_ty, {ptr_ty}, false);
        }
        // Memory functions
        else if (unqualified_name == "cryo_memcpy" || unqualified_name == "cryo_memmove")
        {
            // void* cryo_memcpy/memmove(void* dest, void* src, i64 size)
            fn_type = llvm::FunctionType::get(ptr_ty, {ptr_ty, ptr_ty, i64_ty}, false);
        }
        else if (unqualified_name == "cryo_memset")
        {
            // void* cryo_memset(void* ptr, i32 value, i64 size)
            fn_type = llvm::FunctionType::get(ptr_ty, {ptr_ty, i32_ty, i64_ty}, false);
        }
        else if (unqualified_name == "cryo_memcmp")
        {
            // i32 cryo_memcmp(void* s1, void* s2, i64 size)
            fn_type = llvm::FunctionType::get(i32_ty, {ptr_ty, ptr_ty, i64_ty}, false);
        }
        // Runtime functions
        else if (unqualified_name == "cryo_runtime_allocate")
        {
            // void* cryo_runtime_allocate(i64 size)
            fn_type = llvm::FunctionType::get(ptr_ty, {i64_ty}, false);
        }
        else if (unqualified_name == "cryo_runtime_deallocate")
        {
            // void cryo_runtime_deallocate(void* ptr)
            fn_type = llvm::FunctionType::get(void_ty, {ptr_ty}, false);
        }
        else if (unqualified_name == "cryo_runtime_initialize")
        {
            // void cryo_runtime_initialize()
            fn_type = llvm::FunctionType::get(void_ty, {}, false);
        }
        // Print/output functions
        else if (unqualified_name == "cryo_print" || unqualified_name == "cryo_println")
        {
            // void cryo_print/println(void* str)
            fn_type = llvm::FunctionType::get(void_ty, {ptr_ty}, false);
        }
        else if (unqualified_name == "cryo_to_string")
        {
            // void* cryo_to_string(i64 value)
            fn_type = llvm::FunctionType::get(ptr_ty, {i64_ty}, false);
        }
        // Error handling
        else if (unqualified_name == "cryo_panic")
        {
            // void cryo_panic(void* msg)
            fn_type = llvm::FunctionType::get(void_ty, {ptr_ty}, false);
        }
        else if (unqualified_name == "cryo_assert")
        {
            // void cryo_assert(i32 condition, void* msg)
            fn_type = llvm::FunctionType::get(void_ty, {i32_ty, ptr_ty}, false);
        }
        else if (unqualified_name == "cryo_throw_exception")
        {
            // void cryo_throw_exception(void* msg, i32 code)
            fn_type = llvm::FunctionType::get(void_ty, {ptr_ty, i32_ty}, false);
        }
        // Profiling
        else if (unqualified_name == "cryo_profile_start")
        {
            // i64 cryo_profile_start(void* name)
            fn_type = llvm::FunctionType::get(i64_ty, {ptr_ty}, false);
        }
        else if (unqualified_name == "cryo_profile_end")
        {
            // void cryo_profile_end(void* name, i64 start_time)
            fn_type = llvm::FunctionType::get(void_ty, {ptr_ty, i64_ty}, false);
        }
        else
        {
            LOG_ERROR(Cryo::LogComponent::CODEGEN,
                      "Unknown runtime function '{}' - cannot create declaration", unqualified_name);
            return nullptr;
        }

        // Create the function declaration
        llvm::Function *fn = llvm::Function::Create(
            fn_type,
            llvm::Function::ExternalLinkage,
            qualified_name,
            module());

        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                  "Created runtime function declaration: {}", qualified_name);

        return fn;
    }

} // namespace Cryo::Codegen
