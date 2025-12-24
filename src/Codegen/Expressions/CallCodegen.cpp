#include "Codegen/Expressions/CallCodegen.hpp"
#include "Codegen/Memory/MemoryCodegen.hpp"
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
                llvm::Value *receiver = generate_expression(member->object());
                if (!receiver)
                {
                    report_error(ErrorCode::E0636_UNDEFINED_FUNCTION_CALL, node,
                                 "Failed to generate receiver for method call");
                    return nullptr;
                }
                return generate_instance_method(node, receiver, member->member());
            }
            report_error(ErrorCode::E0636_UNDEFINED_FUNCTION_CALL, node,
                         "Invalid instance method call");
            return nullptr;
        }

        case CallKind::FreeFunction:
        {
            llvm::Function *fn = resolve_function(function_name);
            if (!fn)
            {
                report_error(ErrorCode::E0636_UNDEFINED_FUNCTION_CALL, node,
                             "Unknown function: " + function_name);
                return nullptr;
            }
            return generate_free_function(node, fn);
        }

        case CallKind::GenericInstantiation:
        {
            // Handle generic instantiation - resolve the specialized function
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

            // Check if it's a known struct type
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

        // Look up the struct type
        llvm::Type *struct_type = ctx().get_type(type_name);
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

        // Check for explicit constructor
        llvm::Function *ctor = resolve_constructor(type_name);
        if (ctor)
        {
            // Call constructor with allocated memory and arguments
            std::vector<llvm::Value *> ctor_args;
            ctor_args.push_back(alloca);
            ctor_args.insert(ctor_args.end(), args.begin(), args.end());
            builder().CreateCall(ctor, ctor_args);
        }
        else
        {
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

        // Look up the class type
        llvm::Type *class_type = ctx().get_type(type_name);
        if (!class_type || !class_type->isStructTy())
        {
            report_error(ErrorCode::E0635_TYPE_CONSTRUCTOR_UNDEFINED, node,
                         "Unknown class type: " + type_name);
            return nullptr;
        }

        // Calculate size
        auto &dl = module()->getDataLayout();
        uint64_t size = dl.getTypeAllocSize(class_type);

        // Allocate on heap using memory codegen if available
        llvm::Value *heap_ptr = nullptr;
        if (_memory)
        {
            llvm::Type *i8_type = llvm::Type::getInt8Ty(llvm_ctx());
            llvm::Value *size_val = llvm::ConstantInt::get(llvm::Type::getInt64Ty(llvm_ctx()), size);
            heap_ptr = _memory->create_heap_alloc(i8_type, "class." + type_name);
        }
        else
        {
            // Fallback: call malloc directly
            llvm::Function *malloc_fn = module()->getFunction("malloc");
            if (!malloc_fn)
            {
                llvm::Type *i64_type = llvm::Type::getInt64Ty(llvm_ctx());
                llvm::Type *ptr_type = llvm::PointerType::get(llvm_ctx(), 0);
                llvm::FunctionType *malloc_type = llvm::FunctionType::get(ptr_type, {i64_type}, false);
                malloc_fn = llvm::Function::Create(malloc_type, llvm::Function::ExternalLinkage, "malloc", module());
            }
            llvm::Value *size_val = llvm::ConstantInt::get(llvm::Type::getInt64Ty(llvm_ctx()), size);
            heap_ptr = builder().CreateCall(malloc_fn, {size_val}, "heap_alloc");
        }

        if (!heap_ptr)
        {
            report_error(ErrorCode::E0633_FUNCTION_BODY_ERROR, node,
                         "Failed to allocate heap memory for class");
            return nullptr;
        }

        // Generate arguments
        auto args = generate_arguments(node->arguments());

        // Look for constructor
        llvm::Function *ctor = resolve_constructor(type_name);
        if (ctor)
        {
            std::vector<llvm::Value *> ctor_args;
            ctor_args.push_back(heap_ptr);
            ctor_args.insert(ctor_args.end(), args.begin(), args.end());
            builder().CreateCall(ctor, ctor_args);
        }
        else
        {
            // Initialize fields
            auto *st = llvm::cast<llvm::StructType>(class_type);
            for (size_t i = 0; i < args.size() && i < st->getNumElements(); ++i)
            {
                llvm::Value *field_ptr = create_struct_gep(class_type, heap_ptr, i, "field." + std::to_string(i));
                llvm::Value *arg = cast_if_needed(args[i], st->getElementType(i));
                create_store(arg, field_ptr);
            }
        }

        return heap_ptr;
    }

    llvm::Value *CallCodegen::generate_enum_variant(Cryo::CallExpressionNode *node,
                                                    const std::string &enum_name,
                                                    const std::string &variant_name)
    {
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "Generating enum variant: {}::{}", enum_name, variant_name);

        // Look up the enum type
        llvm::Type *enum_type = ctx().get_type(enum_name);
        if (!enum_type)
        {
            report_error(ErrorCode::E0633_FUNCTION_BODY_ERROR, node,
                         "Unknown enum type: " + enum_name);
            return nullptr;
        }

        // Generate arguments
        auto args = generate_arguments(node->arguments());

        // Look for variant constructor
        std::string ctor_name = enum_name + "::" + variant_name;
        llvm::Function *ctor = module()->getFunction(ctor_name);
        if (ctor)
        {
            return builder().CreateCall(ctor, args, variant_name);
        }

        // If no constructor, treat as simple enum value
        // Look up variant index from symbol table
        // For now, return a placeholder
        report_error(ErrorCode::E0633_FUNCTION_BODY_ERROR, node,
                     "Enum variant constructor not implemented: " + ctor_name);
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

        if (intrinsic_name == "__printf__")
        {
            llvm::Function *printf_fn = module()->getFunction("printf");
            if (!printf_fn)
            {
                llvm::Type *ptr_type = llvm::PointerType::get(llvm_ctx(), 0);
                llvm::Type *i32_type = llvm::Type::getInt32Ty(llvm_ctx());
                llvm::FunctionType *fn_type = llvm::FunctionType::get(i32_type, {ptr_type}, true);
                printf_fn = llvm::Function::Create(fn_type, llvm::Function::ExternalLinkage, "printf", module());
            }
            return builder().CreateCall(printf_fn, args, "printf_result");
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
        std::string qualified_name = qualify_runtime_function(function_name);

        // Look up or create function declaration
        llvm::Function *fn = module()->getFunction(qualified_name);
        if (!fn)
        {
            // Try unqualified name
            fn = module()->getFunction(function_name);
        }

        if (!fn)
        {
            report_error(ErrorCode::E0636_UNDEFINED_FUNCTION_CALL, node,
                         "Runtime function not found: " + function_name);
            return nullptr;
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

        // Determine receiver type name
        std::string type_name;
        // Try to look up from variable types or infer from LLVM type
        // This is a simplified version - full implementation would need type tracking

        // Resolve the method
        llvm::Function *method = resolve_method(type_name, method_name);
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
        // Try direct lookup in LLVM module first (primary source of truth)
        llvm::Function *fn = module()->getFunction(name);
        if (fn)
            return fn;

        // Try in context's function registry (for forward declarations, templates, etc.)
        fn = ctx().get_function(name);
        if (fn)
            return fn;

        // Try with current namespace prefix using SRM
        std::string qualified = ctx().namespace_context().empty()
                                    ? name
                                    : ctx().namespace_context() + "::" + name;

        if (qualified != name)
        {
            fn = module()->getFunction(qualified);
            if (fn)
                return fn;

            fn = ctx().get_function(qualified);
            if (fn)
                return fn;
        }

        return nullptr;
    }

    llvm::Function *CallCodegen::resolve_method(const std::string &type_name,
                                                const std::string &method_name)
    {
        // Try fully qualified name in LLVM module
        std::string qualified = type_name + "::" + method_name;
        llvm::Function *fn = module()->getFunction(qualified);
        if (fn)
            return fn;

        // Try in context's function registry
        fn = ctx().get_function(qualified);
        if (fn)
            return fn;

        return nullptr;
    }

    llvm::Function *CallCodegen::resolve_constructor(const std::string &type_name,
                                                     const std::vector<llvm::Type *> &arg_types)
    {
        // Try "Type::init" or "Type::new" patterns
        std::vector<std::string> ctor_names = {
            type_name + "::init",
            type_name + "::new",
            type_name + "::" + type_name};

        for (const auto &ctor_name : ctor_names)
        {
            // Check LLVM module first
            llvm::Function *fn = module()->getFunction(ctor_name);
            if (fn)
                return fn;

            // Check context's function registry
            fn = ctx().get_function(ctor_name);
            if (fn)
                return fn;
        }

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
        // Check in type registry
        llvm::Type *type = const_cast<CodegenContext &>(ctx()).get_type(name);
        return type && type->isStructTy();
    }

    bool CallCodegen::is_enum_type(const std::string &name) const
    {
        // Check symbol table for enum
        Symbol *sym = const_cast<CallCodegen *>(this)->symbols().lookup_symbol(name);
        if (sym && sym->kind == SymbolKind::Type && sym->data_type)
        {
            return sym->data_type->kind() == TypeKind::Enum;
        }
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
        // Runtime functions are typically in std::Runtime namespace
        if (name.find("::") == std::string::npos)
        {
            return "std::Runtime::" + name;
        }
        return name;
    }

} // namespace Cryo::Codegen
