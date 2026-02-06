#include "Codegen/Expressions/CallCodegen.hpp"
#include "Codegen/CodegenVisitor.hpp"
#include "Codegen/Memory/MemoryCodegen.hpp"
#include "Codegen/Declarations/GenericCodegen.hpp"
#include "Codegen/Intrinsics.hpp"
#include "AST/ASTVisitor.hpp"
#include "AST/TemplateRegistry.hpp"
#include "Types/UserDefinedTypes.hpp"
#include "Types/TypeKind.hpp"
#include "Types/CompoundTypes.hpp"
#include "Types/GenericTypes.hpp"
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
        "boolean", "char", "void", "string"};

    const std::unordered_set<std::string> CallCodegen::_runtime_functions = {
        "cryo_alloc", "cryo_free", "cryo_realloc",
        "cryo_print", "cryo_println", "cryo_to_string",
        "cryo_panic", "cryo_assert"};

    const std::unordered_set<std::string> CallCodegen::_intrinsic_functions = {
        // Memory allocation
        "malloc", "calloc", "realloc", "free", "aligned_alloc",
        // Memory operations
        "memcpy", "memmove", "memset", "memcmp", "memchr",
        // Pointer arithmetic
        "ptr_add", "ptr_sub", "ptr_diff",
        // String operations
        "strlen", "strcmp", "strncmp", "strcpy", "strncpy", "strcat",
        "strchr", "strrchr", "strstr", "strdup",
        // I/O operations
        "printf", "snprintf", "sprintf", "fprintf", "getchar", "putchar", "puts",
        // File I/O
        "fopen", "fclose", "fread", "fwrite", "fseek", "ftell", "fflush", "feof", "ferror",
        // Low-level file descriptor I/O
        "read", "write", "open", "close", "lseek", "dup", "dup2", "pipe", "fcntl",
        // Filesystem operations
        "stat", "fstat", "lstat", "access", "mkdir", "rmdir", "unlink", "rename",
        "symlink", "readlink", "truncate", "ftruncate", "chmod", "chown",
        "getcwd", "chdir", "opendir", "readdir", "closedir",
        // Process management
        "exit", "abort", "fork", "execvp", "wait", "waitpid",
        "getpid", "getppid", "getuid", "getgid", "geteuid", "getegid",
        "kill", "raise", "signal",
        // Error handling
        "errno",
        // Math functions
        "sqrt", "sqrtf", "pow", "powf", "exp", "expf", "exp2", "expm1",
        "log", "logf", "log10", "log2", "log1p",
        "sin", "sinf", "cos", "cosf", "tan", "tanf",
        "asin", "acos", "atan", "atan2",
        "sinh", "cosh", "tanh", "asinh", "acosh", "atanh",
        "cbrt", "hypot", "fabs", "fabsf", "floor", "floorf", "ceil", "ceilf",
        "round", "roundf", "trunc", "fmod", "remainder", "fmin", "fmax", "fma",
        "copysign", "nextafter", "frexp", "ldexp", "modf", "erf", "erfc",
        // Network operations
        "socket", "bind", "listen", "accept", "connect", "send", "recv",
        "sendto", "recvfrom", "shutdown", "setsockopt", "getsockopt",
        "getsockname", "getpeername", "poll",
        "htons", "ntohs", "htonl", "ntohl",
        // Windows socket initialization
        "WSAStartup", "WSACleanup", "WSAGetLastError",
        // Time operations
        "time", "gettimeofday", "clock_gettime", "nanosleep", "sleep", "usleep",
        // Threading (pthread)
        "pthread_create", "pthread_join", "pthread_exit", "pthread_detach",
        "pthread_self", "pthread_equal", "sched_yield",
        // Mutex operations
        "pthread_mutex_init", "pthread_mutex_destroy", "pthread_mutex_lock",
        "pthread_mutex_trylock", "pthread_mutex_unlock",
        // Condition variables
        "pthread_cond_init", "pthread_cond_destroy", "pthread_cond_wait",
        "pthread_cond_timedwait", "pthread_cond_signal", "pthread_cond_broadcast",
        // Read-write locks
        "pthread_rwlock_init", "pthread_rwlock_destroy", "pthread_rwlock_rdlock",
        "pthread_rwlock_tryrdlock", "pthread_rwlock_wrlock", "pthread_rwlock_trywrlock",
        "pthread_rwlock_unlock",
        // Thread-local storage
        "pthread_key_create", "pthread_key_delete", "pthread_getspecific", "pthread_setspecific",
        // Atomic operations
        "atomic_load_8", "atomic_load_16", "atomic_load_32", "atomic_load_64",
        "atomic_store_8", "atomic_store_16", "atomic_store_32", "atomic_store_64",
        "atomic_exchange_32", "atomic_exchange_64", "atomic_swap_64",
        "atomic_compare_exchange_32", "atomic_compare_exchange_64",
        "atomic_fetch_add_32", "atomic_fetch_add_64",
        "atomic_fetch_sub_32", "atomic_fetch_sub_64",
        "atomic_fetch_and_32", "atomic_fetch_and_64",
        "atomic_fetch_or_32", "atomic_fetch_or_64",
        "atomic_fetch_xor_32", "atomic_fetch_xor_64",
        "atomic_fence",
        "atomic_swap_8", "atomic_exchange_8", "atomic_compare_exchange_8",
        "atomic_fetch_and_8", "atomic_fetch_or_8", "atomic_fetch_xor_8", "atomic_fetch_nand_8",
        "atomic_load_u8", "atomic_store_u8", "atomic_swap_u8", "atomic_cmpxchg_u8",
        "atomic_fetch_and_u8", "atomic_fetch_or_u8", "atomic_fetch_xor_u8", "atomic_fetch_nand_u8",
        "atomic_load_i32", "atomic_store_i32", "atomic_swap_i32", "atomic_cmpxchg_i32",
        "atomic_fetch_add_i32", "atomic_fetch_sub_i32", "atomic_fetch_and_i32",
        "atomic_fetch_or_i32", "atomic_fetch_xor_i32", "atomic_fetch_max_i32", "atomic_fetch_min_i32",
        "atomic_load_u32", "atomic_store_u32", "atomic_swap_u32", "atomic_cmpxchg_u32",
        "atomic_fetch_add_u32", "atomic_fetch_sub_u32", "atomic_fetch_and_u32",
        "atomic_fetch_or_u32", "atomic_fetch_xor_u32",
        "atomic_load_i64", "atomic_store_i64", "atomic_swap_i64", "atomic_cmpxchg_i64",
        "atomic_fetch_add_i64", "atomic_fetch_sub_i64",
        "atomic_load_u64", "atomic_store_u64", "atomic_swap_u64", "atomic_cmpxchg_u64",
        "atomic_fetch_add_u64", "atomic_fetch_sub_u64",
        // Syscalls
        "syscall_write", "syscall_read", "syscall_exit",
        "syscall_open", "syscall_close", "syscall_lseek",
        // Misc
        "panic", "float32_to_string", "float64_to_string", "sizeof"};

    //===================================================================
    // Forward Declarations
    //===================================================================

    // Helper function to parse type arguments from a string into TypeRefs
    static std::vector<TypeRef> parse_type_args_from_string(
        const std::string &type_args_str,
        Cryo::SymbolTable &symbols);

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

                    // CRITICAL FIX: For primitive type members (string, i32, etc.), the field contains
                    // a pointer value that we need to LOAD before passing to the method.
                    // For example: this.input_buffer.length() where input_buffer is a string (ptr)
                    //   - GEP gives us the address of the input_buffer field
                    //   - But string::length expects the actual string pointer, not the field address
                    //   - So we need to load the pointer value from the field
                    if (receiver)
                    {
                        TypeRef nested_type = nested_member->get_resolved_type();
                        if (nested_type.is_valid())
                        {
                            std::string type_name = nested_type->display_name();
                            // Check if it's a primitive type that stores a pointer value
                            static const std::unordered_set<std::string> primitive_ptr_types = {
                                "string", "i8*", "u8*", "char*"};
                            bool is_string_type = (nested_type->kind() == Cryo::TypeKind::String ||
                                                   primitive_ptr_types.find(type_name) != primitive_ptr_types.end());

                            if (is_string_type)
                            {
                                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                          "Loading string pointer from member field for primitive method call: {}",
                                          nested_member->member());
                                receiver = builder().CreateLoad(
                                    llvm::PointerType::get(llvm_ctx(), 0), receiver,
                                    nested_member->member() + ".str.load");
                            }
                        }
                    }
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

                        // If not a global, check for local variable
                        if (!receiver)
                        {
                            // CRITICAL FIX: For local struct variables, we need to pass the alloca
                            // address directly to methods, NOT load the value and create a temporary.
                            // Otherwise mutations in the method won't affect the original variable.
                            //
                            // Example: tokens.push(token) where tokens is Array<Token>
                            //   - tokens is stored as: %tokens = alloca %Array_Token
                            //   - We should pass %tokens directly to push()
                            //   - Previously: loaded value, stored to temp, passed temp (mutations lost!)
                            //
                            // For pointer-type variables (like MemoryPool* or &this parameters), we DO
                            // need to load the pointer value, because the variable holds a pointer to
                            // the object, not the object itself.

                            // Try to get the alloca for local variables
                            if (llvm::AllocaInst *alloca = values().get_alloca(name))
                            {
                                // Use LLVM type to determine if this alloca stores a pointer vs a struct
                                // - "alloca ptr" → stores a pointer, need to load
                                // - "alloca %StructType" → IS the struct storage, use directly
                                llvm::Type *allocated_type = alloca->getAllocatedType();
                                bool stores_pointer = allocated_type->isPointerTy();

                                if (stores_pointer)
                                {
                                    // The alloca stores a pointer (e.g., 'this' parameter, pointer variables)
                                    // We need to load the pointer value to get the actual object pointer
                                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                              "Loading pointer value from alloca for receiver: {} (alloca stores ptr)", name);
                                    receiver = builder().CreateLoad(
                                        llvm::PointerType::get(llvm_ctx(), 0), alloca, name + ".ptr.load");
                                }
                                else
                                {
                                    // The alloca IS the struct storage (local struct variable)
                                    // Pass the alloca address directly so mutations affect the original
                                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                              "Using local variable alloca address for method receiver: {} (alloca is struct)", name);
                                    receiver = alloca;
                                }
                            }
                            else
                            {
                                // Fallback: generate expression (for cases not in alloca map)
                                receiver = generate_expression(member->object());

                                // If we got an alloca back, check if we need to load
                                if (receiver && llvm::isa<llvm::AllocaInst>(receiver))
                                {
                                    llvm::AllocaInst *alloca_inst = llvm::cast<llvm::AllocaInst>(receiver);
                                    if (alloca_inst->getAllocatedType()->isPointerTy())
                                    {
                                        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                                  "Loading pointer value from generated alloca for receiver: {}", name);
                                        receiver = builder().CreateLoad(
                                            llvm::PointerType::get(llvm_ctx(), 0), receiver, name + ".ptr.load");
                                    }
                                }
                            }
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
                                 "Failed to generate receiver for method call for: " + function_name);
                    return nullptr;
                }

                // Ensure receiver is a pointer type (methods expect ptr to self)
                if (!receiver->getType()->isPointerTy())
                {
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "Method receiver is not a pointer, creating temporary storage");

                    // Safety check: if receiver type is void (from unresolved generic),
                    // we can't create an alloca with void type - use opaque pointer instead
                    llvm::Type *receiver_type = receiver->getType();
                    if (receiver_type->isVoidTy())
                    {
                        LOG_ERROR(Cryo::LogComponent::CODEGEN,
                                  "Method receiver has void type (likely from unresolved generic). "
                                  "Using opaque pointer as fallback.");
                        // This is a workaround - the real fix is proper generic substitution
                        // For now, create a pointer-sized alloca and proceed
                        llvm::Type *ptr_type = llvm::PointerType::get(llvm_ctx(), 0);
                        llvm::AllocaInst *temp = create_entry_alloca(ptr_type, "method.receiver.tmp");
                        // Store null pointer as placeholder - the actual value handling needs work
                        builder().CreateStore(llvm::ConstantPointerNull::get(llvm::PointerType::get(llvm_ctx(), 0)), temp);
                        receiver = temp;
                    }
                    else
                    {
                        // Create an alloca to store the value and pass the pointer
                        llvm::AllocaInst *temp = create_entry_alloca(receiver_type, "method.receiver.tmp");
                        builder().CreateStore(receiver, temp);
                        receiver = temp;
                    }
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
            // Handle two cases:
            // 1. function_name already contains <> (from identifier name embedding)
            // 2. function_name is base name only, with generic_args stored separately in node
            size_t open_bracket = function_name.find('<');
            size_t close_bracket = function_name.rfind('>');

            std::string base_name;
            std::string type_args_str;

            if (open_bracket != std::string::npos && close_bracket != std::string::npos)
            {
                // Case 1: function_name contains the full generic signature
                base_name = function_name.substr(0, open_bracket);
                type_args_str = function_name.substr(open_bracket + 1, close_bracket - open_bracket - 1);
            }
            else if (node->has_generic_args())
            {
                // Case 2: generic_args are stored separately in the node
                base_name = function_name;
                const auto &generic_args = node->generic_args();
                for (size_t i = 0; i < generic_args.size(); ++i)
                {
                    if (i > 0)
                        type_args_str += ",";
                    type_args_str += generic_args[i];
                }
                // Rebuild the full function_name for later use
                function_name = base_name + "<" + type_args_str + ">";
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "GenericInstantiation: Built function_name '{}' from node->generic_args()",
                          function_name);
            }
            else
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
                std::vector<TypeRef> type_args;

                // Trim whitespace from type_args_str
                size_t start = type_args_str.find_first_not_of(" \t");
                size_t end = type_args_str.find_last_not_of(" \t");
                if (start != std::string::npos && end != std::string::npos)
                {
                    type_args_str = type_args_str.substr(start, end - start + 1);
                }

                // Resolve the type argument - handle pointer types (e.g., "Expr*")
                TypeRef arg_type{};
                std::string base_type_name = type_args_str;
                int ptr_depth = 0;
                while (!base_type_name.empty() && base_type_name.back() == '*')
                {
                    base_type_name.pop_back();
                    ptr_depth++;
                }
                // Trim whitespace after stripping pointers
                while (!base_type_name.empty() && std::isspace(static_cast<unsigned char>(base_type_name.back())))
                    base_type_name.pop_back();

                Symbol *type_sym = symbols().lookup_symbol(base_type_name);
                if (type_sym && type_sym->kind == SymbolKind::Type && type_sym->type.is_valid())
                {
                    arg_type = type_sym->type;
                }
                if (!arg_type.is_valid())
                {
                    // Try common primitive types using convenience methods
                    if (base_type_name == "int" || base_type_name == "i32")
                    {
                        arg_type = symbols().arena().get_i32();
                    }
                    else if (base_type_name == "i64")
                    {
                        arg_type = symbols().arena().get_i64();
                    }
                    else if (base_type_name == "f32" || base_type_name == "float")
                    {
                        arg_type = symbols().arena().get_f32();
                    }
                    else if (base_type_name == "f64" || base_type_name == "double")
                    {
                        arg_type = symbols().arena().get_f64();
                    }
                    else if (base_type_name == "string")
                    {
                        arg_type = symbols().arena().get_string();
                    }
                    else if (base_type_name == "bool" || base_type_name == "boolean")
                    {
                        arg_type = symbols().arena().get_bool();
                    }
                }

                // Wrap in pointer type(s) if trailing * were present
                if (arg_type.is_valid() && ptr_depth > 0)
                {
                    for (int p = 0; p < ptr_depth; ++p)
                    {
                        arg_type = symbols().arena().get_pointer_to(arg_type);
                    }
                }

                if (arg_type.is_valid())
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

            // Not a generic struct, try as a generic function
            // First check if there's a generic function template registered
            if (generics->get_generic_function_def(base_name))
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "GenericInstantiation: '{}' is a generic function, instantiating",
                          base_name);

                // Parse type arguments similar to struct handling
                std::vector<TypeRef> type_args;

                // Trim whitespace from type_args_str
                std::string trimmed = type_args_str;
                size_t start = trimmed.find_first_not_of(" \t");
                size_t end = trimmed.find_last_not_of(" \t");
                if (start != std::string::npos && end != std::string::npos)
                {
                    trimmed = trimmed.substr(start, end - start + 1);
                }

                // Resolve the type argument
                TypeRef arg_type{};
                Symbol *type_sym = symbols().lookup_symbol(trimmed);
                if (type_sym && type_sym->kind == SymbolKind::Type && type_sym->type.is_valid())
                {
                    arg_type = type_sym->type;
                }
                if (!arg_type.is_valid())
                {
                    // Try common primitive types
                    if (trimmed == "int" || trimmed == "i32")
                        arg_type = symbols().arena().get_i32();
                    else if (trimmed == "i64")
                        arg_type = symbols().arena().get_i64();
                    else if (trimmed == "f32" || trimmed == "float")
                        arg_type = symbols().arena().get_f32();
                    else if (trimmed == "f64" || trimmed == "double")
                        arg_type = symbols().arena().get_f64();
                    else if (trimmed == "string")
                        arg_type = symbols().arena().get_string();
                    else if (trimmed == "bool" || trimmed == "boolean")
                        arg_type = symbols().arena().get_bool();
                }

                if (arg_type.is_valid())
                {
                    type_args.push_back(arg_type);
                }

                // Instantiate the generic function
                llvm::Function *instantiated_fn = generics->instantiate_function(base_name, type_args);
                if (instantiated_fn)
                {
                    return generate_free_function(node, instantiated_fn);
                }

                // Fall through to try regular resolution
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "GenericInstantiation: Failed to instantiate generic function '{}', trying regular resolution",
                          base_name);
            }

            // Try as a regular (non-generic) function
            llvm::Function *fn = resolve_function(function_name);
            if (fn)
            {
                return generate_free_function(node, fn);
            }
            report_error(ErrorCode::E0636_UNDEFINED_FUNCTION_CALL, node,
                         "Failed to resolve generic instantiation: " + function_name);
            return nullptr;
        }

        case CallKind::IndirectCall:
        {
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "IndirectCall: Generating indirect call through function pointer '{}'", function_name);

            // Get the function pointer from the variable
            llvm::AllocaInst *fn_alloca = values().get_alloca(function_name);
            if (!fn_alloca)
            {
                report_error(ErrorCode::E0636_UNDEFINED_FUNCTION_CALL, node,
                             "Function pointer variable not found: " + function_name);
                return nullptr;
            }

            // Load the function pointer
            llvm::Value *fn_ptr = builder().CreateLoad(
                llvm::PointerType::get(llvm_ctx(), 0), fn_alloca, function_name + ".ptr");

            // Generate arguments
            auto args = generate_arguments(node->arguments());

            // Try to get the function type from the variable_types_map (Cryo TypeRef)
            llvm::Type *return_type = llvm::Type::getVoidTy(llvm_ctx());
            std::vector<llvm::Type *> param_types;

            auto &var_types = ctx().variable_types_map();
            auto type_it = var_types.find(function_name);
            if (type_it != var_types.end() && type_it->second.is_valid())
            {
                TypeRef var_type = type_it->second;
                if (var_type->kind() == TypeKind::Function)
                {
                    // We have the actual Cryo FunctionType - use it
                    const auto *func_type = static_cast<const Cryo::FunctionType *>(var_type.get());

                    // Map return type
                    TypeRef ret_type_ref = func_type->return_type();
                    if (ret_type_ref.is_valid())
                    {
                        return_type = types().map(ret_type_ref);
                        if (!return_type)
                        {
                            return_type = llvm::Type::getVoidTy(llvm_ctx());
                        }
                    }

                    // Map parameter types
                    for (const auto &param_type_ref : func_type->param_types())
                    {
                        if (param_type_ref.is_valid())
                        {
                            llvm::Type *param_llvm = types().map(param_type_ref);
                            if (param_llvm)
                            {
                                param_types.push_back(param_llvm);
                            }
                        }
                    }

                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "IndirectCall: Using FunctionType for '{}' with {} params, return: {}",
                              function_name, param_types.size(),
                              return_type->isVoidTy() ? "void" : "non-void");
                }
            }

            // If we couldn't get param types from FunctionType, infer from arguments
            if (param_types.empty())
            {
                for (auto *arg : args)
                {
                    param_types.push_back(arg->getType());
                }
            }

            llvm::FunctionType *fn_type = llvm::FunctionType::get(return_type, param_types, false);

            // Create the indirect call
            // Note: void calls cannot have a name in LLVM IR, so only provide name for non-void returns
            llvm::Value *result;
            if (return_type->isVoidTy())
            {
                result = builder().CreateCall(fn_type, fn_ptr, args);
            }
            else
            {
                result = builder().CreateCall(fn_type, fn_ptr, args, "indirect.call");
            }

            return result;
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

            // Check for generic instantiation (contains angle brackets OR has generic_args)
            if (name.find('<') != std::string::npos || node->has_generic_args())
            {
                return CallKind::GenericInstantiation;
            }

            // Check if this is a scope-qualified call like "Array::new" or "HashMap::with_capacity"
            // where the left part is a generic template
            size_t scope_sep = name.rfind("::");
            if (scope_sep != std::string::npos && scope_sep > 0)
            {
                std::string type_part = name.substr(0, scope_sep);
                std::string method_part = name.substr(scope_sep + 2);

                // Check if the type part is a generic template
                CodegenVisitor *visitor = ctx().visitor();
                GenericCodegen *generics = visitor ? visitor->get_generics() : nullptr;
                if (generics && generics->is_generic_template(type_part))
                {
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "classify_call: '{}' is a static method on generic template '{}', treating as StaticMethod",
                              name, type_part);
                    return CallKind::StaticMethod;
                }
            }

            // Check if it's a function pointer variable (e.g., parameter with function type)
            // This handles cases like: fn for_each(f: (T) -> void) where f(x) is called
            // Check the variable_types_map for TypeRef information
            auto &var_types = ctx().variable_types_map();
            auto type_it = var_types.find(name);
            if (type_it != var_types.end())
            {
                TypeRef var_type = type_it->second;
                if (var_type.is_valid() && var_type->kind() == TypeKind::Function)
                {
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "classify_call: '{}' has FunctionType, treating as IndirectCall", name);
                    return CallKind::IndirectCall;
                }
            }

            // Fallback: If there's an alloca but no LLVM function, likely an indirect call
            llvm::AllocaInst *alloca = values().get_alloca(name);
            if (alloca)
            {
                llvm::Function *fn = resolve_function(name);
                if (!fn)
                {
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "classify_call: '{}' is a variable with no function, treating as IndirectCall", name);
                    return CallKind::IndirectCall;
                }
            }

            // Default to free function
            return CallKind::FreeFunction;
        }

        // Check for member access callee
        if (auto *member = dynamic_cast<MemberAccessNode *>(node->callee()))
        {
            std::string method_name = member->member();

            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "classify_call: MemberAccessNode with member '{}'", method_name);

            // Check if object is an identifier (could be namespace or variable)
            if (auto *obj_id = dynamic_cast<IdentifierNode *>(member->object()))
            {
                std::string obj_name = obj_id->name();

                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "classify_call: Object identifier is '{}'", obj_name);

                // ONLY classify as intrinsic if the object is the 'intrinsics' namespace
                // This prevents method calls like this.read(buf, len) from being
                // misclassified as intrinsic calls like intrinsics::read(fd, buf, len)
                if (obj_name == "intrinsics" && is_intrinsic(method_name))
                {
                    return CallKind::Intrinsic;
                }

                // Check if it's an enum variant
                if (is_enum_type(obj_name))
                {
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "classify_call: '{}' is an enum type, returning EnumVariant", obj_name);
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

                // Check if it's a generic template - treat as static method call
                // This handles calls like HashSet.new() when inside HashSet<T> methods
                {
                    CodegenVisitor *visitor = ctx().visitor();
                    GenericCodegen *generics = visitor ? visitor->get_generics() : nullptr;
                    if (generics && generics->is_generic_template(obj_name))
                    {
                        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                  "classify_call: '{}' is a generic template, treating as StaticMethod", obj_name);
                        return CallKind::StaticMethod;
                    }
                }

                // Check if it's a registered type (including type aliases like PthreadMutex = void[40])
                // If a type exists with this name, treat Type::method() as a static method call
                if (ctx().get_type(obj_name) != nullptr)
                {
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "classify_call: '{}' is a registered type, treating as StaticMethod", obj_name);
                    return CallKind::StaticMethod;
                }

                // Also check the type arena for type aliases that might not be in the LLVM type map yet
                {
                    TypeRef type_ref = ctx().symbols().arena().lookup_type_by_name(obj_name);
                    if (type_ref.is_valid())
                    {
                        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                  "classify_call: '{}' found in type arena, treating as StaticMethod", obj_name);
                        return CallKind::StaticMethod;
                    }
                }

                // Check if it's a type alias (including cross-module aliases)
                if (ctx().is_type_alias(obj_name))
                {
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "classify_call: '{}' is a type alias, treating as StaticMethod", obj_name);
                    return CallKind::StaticMethod;
                }

                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "classify_call: '{}::{}' falling through to FreeFunction", obj_name, method_name);

                // Could be a namespace-qualified function
                return CallKind::FreeFunction;
            }

            // Object is not a simple identifier - likely an instance method
            return CallKind::InstanceMethod;
        }

        // Handle scope resolution (e.g., Shape::Circle for enum variants, Array<String>::new())
        if (auto *scope = dynamic_cast<ScopeResolutionNode *>(node->callee()))
        {
            std::string scope_name = scope->scope_name();
            std::string member_name = scope->member_name();

            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "classify_call: ScopeResolution '{}::{}' has_generic_args={}",
                      scope_name, member_name, scope->has_generic_args());

            // Check if it's an enum variant
            if (is_enum_type(scope_name))
            {
                return CallKind::EnumVariant;
            }

            // Check if it's a static method call
            if (is_struct_type(scope_name) || is_class_type(scope_name))
            {
                return CallKind::StaticMethod;
            }

            // Check if it's a generic static method call with explicit type args
            // This handles calls like Array<String>::new() where has_generic_args() is true
            if (scope->has_generic_args())
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "classify_call: '{}::{}' has generic args, treating as StaticMethod",
                          scope_name, member_name);
                return CallKind::StaticMethod;
            }

            // Check if it's a generic template - treat as static method call
            // This handles calls like HashSet::new() when inside HashSet<T> methods
            CodegenVisitor *visitor = ctx().visitor();
            GenericCodegen *generics = visitor ? visitor->get_generics() : nullptr;
            if (generics && generics->is_generic_template(scope_name))
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "classify_call: '{}' is a generic template, treating as StaticMethod", scope_name);
                return CallKind::StaticMethod;
            }

            // Could be a namespace-qualified function
            return CallKind::FreeFunction;
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

        std::string effective_type_name = type_name;

        // First, check if the node has a resolved type we can use directly
        // This is especially important for constructor calls inside generic methods where
        // the constructor type (e.g., "CheckedResult") is different from the containing generic type
        TypeRef resolved_type = node->get_resolved_type();
        if (resolved_type.is_valid())
        {
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "generate_struct_constructor: Node has resolved type: {} (kind={})",
                      resolved_type->display_name(), static_cast<int>(resolved_type->kind()));

            // If the resolved type is an InstantiatedType, try to get the mangled name
            if (resolved_type->kind() == Cryo::TypeKind::InstantiatedType)
            {
                auto *inst_type = static_cast<const Cryo::InstantiatedType *>(resolved_type.get());

                // First check if the InstantiatedType has an already-resolved concrete type
                // This is set during monomorphization (e.g., CheckedResult<i32> -> CheckedResult_i32)
                if (inst_type->has_resolved_type())
                {
                    TypeRef concrete_type = inst_type->resolved_type();
                    if (concrete_type.is_valid())
                    {
                        effective_type_name = concrete_type->display_name();
                        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                  "generate_struct_constructor: Using InstantiatedType's resolved concrete type: {}",
                                  effective_type_name);
                    }
                }
                else
                {
                    // No concrete resolved type yet - try to substitute type parameters if in generic scope
                    auto *visitor = ctx().visitor();
                    if (visitor)
                    {
                        auto *generics = visitor->get_generics();
                        if (generics && generics->in_type_param_scope())
                        {
                            std::string resolved_display = resolved_type->display_name();
                            std::string substituted = generics->substitute_type_annotation(resolved_display);
                            if (!substituted.empty())
                            {
                                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                          "generate_struct_constructor: Using resolved type substitution {} -> {}",
                                          resolved_display, substituted);
                                effective_type_name = substituted;
                            }
                        }
                    }
                }
            }
            else if (resolved_type->kind() == Cryo::TypeKind::Struct)
            {
                // Resolved to a concrete struct type, use its name directly
                effective_type_name = resolved_type->display_name();
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "generate_struct_constructor: Using concrete resolved type name: {}",
                          effective_type_name);
            }
        }

        // Check if we're inside a generic instantiation context and need to redirect
        // the type name. For example, if we're generating code for HashSet<string> methods
        // and the AST says "HashSet { ... }", we need to look for "HashSet_string".
        // Also handle types with type parameters like "HashSetEntry<T>" -> "HashSetEntry_string".
        auto *visitor = ctx().visitor();
        if (visitor)
        {
            auto *generics = visitor->get_generics();
            if (generics && generics->in_type_param_scope())
            {
                // First try exact base name match
                std::string redirected = generics->get_instantiated_scope_name(effective_type_name);
                if (!redirected.empty())
                {
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "generate_struct_constructor: Redirecting type {} -> {} in generic context",
                              effective_type_name, redirected);
                    effective_type_name = redirected;
                }
                else if (effective_type_name.find('_') == std::string::npos)
                {
                    // Only try substitution if we haven't already mangled the name
                    // Try substituting type parameters in the annotation (e.g., "HashSetEntry<T>" -> "HashSetEntry_string")
                    redirected = generics->substitute_type_annotation(effective_type_name);
                    if (!redirected.empty())
                    {
                        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                  "generate_struct_constructor: Substituted type annotation {} -> {} in generic context",
                                  effective_type_name, redirected);
                        effective_type_name = redirected;
                    }
                }
            }
        }

        // Use SRM-based type resolution to find the struct type
        // This handles: current namespace, imported namespaces, aliases, global scope
        llvm::Type *struct_type = resolve_type_by_name(effective_type_name);

        // Determine the resolved type name for constructor lookup
        std::string resolved_type_name = effective_type_name;
        if (struct_type)
        {
            // Try to get the actual qualified name from the candidates
            auto candidates = generate_lookup_candidates(effective_type_name, Cryo::SymbolKind::Type);
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
                         "Unknown struct type: " + effective_type_name);
            return nullptr;
        }

        // Allocate on stack
        llvm::AllocaInst *alloca = create_entry_alloca(struct_type, effective_type_name + ".instance");
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

        std::string effective_type_name = type_name;

        // First, check if the node has a resolved type we can use directly
        TypeRef resolved_type = node->get_resolved_type();
        if (resolved_type.is_valid())
        {
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "generate_class_constructor: Node has resolved type: {} (kind={})",
                      resolved_type->display_name(), static_cast<int>(resolved_type->kind()));

            if (resolved_type->kind() == Cryo::TypeKind::InstantiatedType)
            {
                auto *inst_type = static_cast<const Cryo::InstantiatedType *>(resolved_type.get());

                // First check if the InstantiatedType has an already-resolved concrete type
                if (inst_type->has_resolved_type())
                {
                    TypeRef concrete_type = inst_type->resolved_type();
                    if (concrete_type.is_valid())
                    {
                        effective_type_name = concrete_type->display_name();
                        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                  "generate_class_constructor: Using InstantiatedType's resolved concrete type: {}",
                                  effective_type_name);
                    }
                }
                else
                {
                    // No concrete resolved type yet - try to substitute type parameters if in generic scope
                    auto *visitor = ctx().visitor();
                    if (visitor)
                    {
                        auto *generics = visitor->get_generics();
                        if (generics && generics->in_type_param_scope())
                        {
                            std::string resolved_display = resolved_type->display_name();
                            std::string substituted = generics->substitute_type_annotation(resolved_display);
                            if (!substituted.empty())
                            {
                                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                          "generate_class_constructor: Using resolved type substitution {} -> {}",
                                          resolved_display, substituted);
                                effective_type_name = substituted;
                            }
                        }
                    }
                }
            }
            else if (resolved_type->kind() == Cryo::TypeKind::Class)
            {
                effective_type_name = resolved_type->display_name();
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "generate_class_constructor: Using concrete resolved type name: {}",
                          effective_type_name);
            }
        }

        // Check if we're inside a generic instantiation context and need to redirect
        // the type name. For example, if we're generating code for MyClass<string> methods
        // and the AST says "MyClass { ... }", we need to look for "MyClass_string".
        // Also handle types with type parameters like "MyHelper<T>" -> "MyHelper_string".
        auto *visitor = ctx().visitor();
        if (visitor)
        {
            auto *generics = visitor->get_generics();
            if (generics && generics->in_type_param_scope())
            {
                // First try exact base name match
                std::string redirected = generics->get_instantiated_scope_name(effective_type_name);
                if (!redirected.empty())
                {
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "generate_class_constructor: Redirecting type {} -> {} in generic context",
                              effective_type_name, redirected);
                    effective_type_name = redirected;
                }
                else if (effective_type_name.find('_') == std::string::npos)
                {
                    // Try substituting type parameters in the annotation (e.g., "MyHelper<T>" -> "MyHelper_string")
                    redirected = generics->substitute_type_annotation(effective_type_name);
                    if (!redirected.empty())
                    {
                        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                  "generate_class_constructor: Substituted type annotation {} -> {} in generic context",
                                  effective_type_name, redirected);
                        effective_type_name = redirected;
                    }
                }
            }
        }

        // Use SRM-based type resolution to find the class type
        // This handles: current namespace, imported namespaces, aliases, global scope
        llvm::Type *class_type = resolve_type_by_name(effective_type_name);

        // Determine the resolved type name for constructor lookup
        std::string resolved_type_name = effective_type_name;
        if (class_type)
        {
            // Try to get the actual qualified name from the candidates
            auto candidates = generate_lookup_candidates(effective_type_name, Cryo::SymbolKind::Type);
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
                         "Unknown class type: " + effective_type_name);
            return nullptr;
        }

        // Classes and structs have same allocation semantics: value by default
        // 'new' keyword determines heap allocation, absence means value/stack allocation
        // ClassName() creates value instance (stack allocated)
        // new ClassName() creates pointer instance (heap allocated via cryo_alloc)

        // Allocate on stack (value semantics)
        llvm::AllocaInst *alloca = create_entry_alloca(class_type, effective_type_name + ".instance");
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

        // Resolve type alias to base enum name (e.g., IoResult -> Result)
        std::string resolved_enum_name = ctx().resolve_type_alias(enum_name);
        if (resolved_enum_name != enum_name)
        {
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "generate_enum_variant: Resolved type alias '{}' -> '{}'",
                      enum_name, resolved_enum_name);
        }

        // Check if the call node has a resolved type (from TypeResolver)
        // This is crucial for generic enums like Option<T> or Result<T, E>
        TypeRef resolved_type_ref = node->get_resolved_type();
        std::string instantiated_enum_name = resolved_enum_name;

        if (resolved_type_ref.is_valid())
        {
            const Type *resolved = resolved_type_ref.get();
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "generate_enum_variant: Node has resolved type: {} (kind={})",
                      resolved->display_name(), static_cast<int>(resolved->kind()));

            // If it's an InstantiatedType, get the concrete resolved type
            if (resolved->kind() == TypeKind::InstantiatedType)
            {
                const auto *inst = static_cast<const InstantiatedType *>(resolved);
                if (inst->has_resolved_type())
                {
                    resolved = inst->resolved_type().get();
                    // Use the resolved type's name (mangled name like Option_SystemTime)
                    // This is the name used to register the type in the context
                    instantiated_enum_name = resolved->display_name();
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "generate_enum_variant: Using resolved type from InstantiatedType: {}",
                              instantiated_enum_name);
                }
                else
                {
                    // Fallback to InstantiatedType's display_name if no resolved type
                    instantiated_enum_name = inst->display_name();
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "generate_enum_variant: Using InstantiatedType display name (no resolved type): {}",
                              instantiated_enum_name);
                }
            }
            else if (resolved->kind() == TypeKind::Enum)
            {
                // Direct enum type, use its name
                instantiated_enum_name = resolved->display_name();
            }
        }
        else
        {
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "generate_enum_variant: Node has no resolved type, using base name: {}",
                      resolved_enum_name);

            // When inside a generic method being monomorphized (type param scope),
            // we need to infer the instantiated enum type from context.
            // For example, Option::Some(value) inside Result<T,E>::ok() -> Option<T>
            // should become Option_i32::Some(value) when T=i32
            CodegenVisitor *visitor = ctx().visitor();
            GenericCodegen *generics = visitor ? visitor->get_generics() : nullptr;

            if (generics && generics->in_type_param_scope())
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "generate_enum_variant: In type param scope, attempting to infer instantiated enum type");

                // First check if the enum name directly maps to an instantiated scope name
                std::string redirected = generics->get_instantiated_scope_name(resolved_enum_name);
                if (!redirected.empty())
                {
                    instantiated_enum_name = redirected;
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "generate_enum_variant: Redirected '{}' to '{}' via scope name",
                              resolved_enum_name, instantiated_enum_name);
                }
                else
                {
                    // Check if the enum is a generic template - first local registry, then TemplateRegistry
                    Cryo::ASTNode *enum_def = nullptr;
                    bool is_generic = generics->is_generic_template(resolved_enum_name);

                    if (is_generic)
                    {
                        enum_def = generics->get_generic_type_def(resolved_enum_name);
                    }

                    // Try TemplateRegistry for cross-module generic enums
                    if (!enum_def)
                    {
                        Cryo::TemplateRegistry *template_registry = ctx().template_registry();
                        if (template_registry)
                        {
                            const Cryo::TemplateRegistry::TemplateInfo *tmpl_info =
                                template_registry->find_template(resolved_enum_name);
                            if (tmpl_info && tmpl_info->enum_template)
                            {
                                enum_def = tmpl_info->enum_template;
                                is_generic = true;
                                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                          "generate_enum_variant: Found enum template '{}' in TemplateRegistry",
                                          resolved_enum_name);
                            }
                        }
                    }

                    auto *enum_decl = enum_def ? dynamic_cast<Cryo::EnumDeclarationNode *>(enum_def) : nullptr;

                    if (enum_decl && !enum_decl->generic_parameters().empty())
                    {
                        // Get the enum's type parameters (e.g., ["T"] for Option, ["T", "E"] for Result)
                        std::vector<std::string> type_params;
                        for (const auto &param : enum_decl->generic_parameters())
                        {
                            type_params.push_back(param->name());
                        }

                        // Find the variant being called
                        const Cryo::EnumVariantNode *target_variant = nullptr;
                        for (const auto &variant : enum_decl->variants())
                        {
                            if (variant->name() == variant_name)
                            {
                                target_variant = variant.get();
                                break;
                            }
                        }

                        // Build type arguments by mapping variant payload types to type parameters
                        std::vector<TypeRef> inferred_type_args(type_params.size());
                        bool all_inferred = false;

                        if (target_variant && !target_variant->associated_types().empty())
                        {
                            // Variant has payload - infer type args from call arguments
                            const auto &arg_nodes = node->arguments();
                            const auto &assoc_types = target_variant->associated_types();

                            for (size_t i = 0; i < assoc_types.size() && i < arg_nodes.size(); ++i)
                            {
                                const std::string &assoc_type = assoc_types[i];

                                // Find which type parameter this payload corresponds to
                                for (size_t j = 0; j < type_params.size(); ++j)
                                {
                                    if (assoc_type == type_params[j])
                                    {
                                        // Get the argument's resolved type
                                        TypeRef arg_type = arg_nodes[i] ? arg_nodes[i]->get_resolved_type() : TypeRef{};

                                        if (arg_type.is_valid())
                                        {
                                            // If it's a GenericParam, substitute it
                                            if (arg_type->kind() == TypeKind::GenericParam)
                                            {
                                                TypeRef substituted = generics->resolve_type_param(arg_type->display_name());
                                                if (substituted.is_valid())
                                                {
                                                    arg_type = substituted;
                                                }
                                            }
                                            inferred_type_args[j] = arg_type;
                                            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                                      "generate_enum_variant: Inferred type param {} = {} from argument",
                                                      type_params[j], arg_type->display_name());
                                        }
                                        break;
                                    }
                                }
                            }
                        }

                        // For any type parameters not inferred from arguments, try the current scope bindings
                        for (size_t i = 0; i < type_params.size(); ++i)
                        {
                            if (!inferred_type_args[i].is_valid())
                            {
                                TypeRef binding = generics->resolve_type_param(type_params[i]);
                                if (binding.is_valid())
                                {
                                    inferred_type_args[i] = binding;
                                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                              "generate_enum_variant: Inferred type param {} = {} from scope binding",
                                              type_params[i], binding->display_name());
                                }
                            }
                        }

                        // Check if all type arguments were inferred
                        all_inferred = true;
                        for (const auto &arg : inferred_type_args)
                        {
                            if (!arg.is_valid())
                            {
                                all_inferred = false;
                                break;
                            }
                        }

                        if (all_inferred)
                        {
                            instantiated_enum_name = generics->mangle_type_name(resolved_enum_name, inferred_type_args);
                            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                      "generate_enum_variant: Inferred instantiation '{}' for {}::{}",
                                      instantiated_enum_name, resolved_enum_name, variant_name);

                            // Ensure the enum type is instantiated
                            if (!generics->has_type_instantiation(instantiated_enum_name))
                            {
                                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                          "generate_enum_variant: Instantiating enum '{}'",
                                          instantiated_enum_name);
                                generics->instantiate_enum(resolved_enum_name, inferred_type_args);
                            }
                        }
                        else
                        {
                            LOG_WARN(Cryo::LogComponent::CODEGEN,
                                     "generate_enum_variant: Could not infer all type arguments for {}::{}",
                                     resolved_enum_name, variant_name);
                        }
                    }
                }
            }
        }

        // Use instantiated name for qualified variant lookup
        std::string qualified_variant = instantiated_enum_name + "::" + variant_name;
        // Also try with base name for non-generic enums
        std::string base_qualified_variant = resolved_enum_name + "::" + variant_name;

        // Generate arguments
        auto args = generate_arguments(node->arguments());

        // Look for variant constructor function (for complex variants with payloads)
        // Try instantiated name first (e.g., Option_string::Some)
        llvm::Function *ctor = module()->getFunction(qualified_variant);
        if (!ctor && instantiated_enum_name != resolved_enum_name)
        {
            // Fallback to base name (e.g., Option::Some)
            ctor = module()->getFunction(base_qualified_variant);
        }
        if (ctor)
        {
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "generate_enum_variant: Found constructor function: {}",
                      ctor->getName().str());

            // Coerce arguments to match constructor parameter types
            std::vector<llvm::Value *> coerced_args;
            llvm::FunctionType *fn_type = ctor->getFunctionType();

            for (size_t i = 0; i < args.size() && i < fn_type->getNumParams(); ++i)
            {
                llvm::Value *arg = args[i];
                llvm::Type *param_type = fn_type->getParamType(i);

                // Handle struct-to-pointer conversion
                if (arg && param_type->isPointerTy() && arg->getType()->isStructTy())
                {
                    llvm::AllocaInst *temp = create_entry_alloca(arg->getType(), "struct.arg.tmp");
                    builder().CreateStore(arg, temp);
                    arg = temp;
                }

                coerced_args.push_back(cast_if_needed(arg, param_type));
            }

            return builder().CreateCall(ctor, coerced_args, variant_name);
        }

        // For simple enum variants (no payload), look up the registered constant value
        auto &enum_variants = ctx().enum_variants_map();
        auto it = enum_variants.find(qualified_variant);
        if (it == enum_variants.end() && instantiated_enum_name != resolved_enum_name)
        {
            // Try base name
            it = enum_variants.find(base_qualified_variant);
        }
        if (it != enum_variants.end())
        {
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "Found simple enum variant: {} -> constant", it->first);
            return it->second;
        }

        // Try with namespace prefix
        std::string current_ns = ctx().namespace_context();
        if (!current_ns.empty())
        {
            std::string ns_qualified = current_ns + "::" + qualified_variant;
            it = enum_variants.find(ns_qualified);
            if (it == enum_variants.end() && instantiated_enum_name != resolved_enum_name)
            {
                ns_qualified = current_ns + "::" + base_qualified_variant;
                it = enum_variants.find(ns_qualified);
            }
            if (it != enum_variants.end())
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "Found simple enum variant with namespace: {} -> constant", ns_qualified);
                return it->second;
            }
        }

        // Look up the enum type - use TypeMapper for proper caching
        // First try using the resolved TypeRef if available (most reliable)
        llvm::Type *enum_type = nullptr;
        if (resolved_type_ref.is_valid())
        {
            enum_type = types().map(resolved_type_ref);
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "generate_enum_variant: Mapped enum type from TypeRef: {}",
                      enum_type ? "success" : "failed");
        }

        // If TypeRef mapping failed, this indicates a type resolution issue
        if (!enum_type && resolved_type_ref.is_valid())
        {
            LOG_ERROR(Cryo::LogComponent::CODEGEN,
                      "generate_enum_variant: TypeRef mapping failed for '{}' (TypeID={}) - "
                      "this indicates the enum type was not properly registered in TypeMapper",
                      resolved_type_ref->display_name(),
                      resolved_type_ref.id().id);
        }

        // Name-based lookup as last resort - this is brittle and may produce incorrect results
        if (!enum_type)
        {
            LOG_WARN(Cryo::LogComponent::CODEGEN,
                     "generate_enum_variant: Using name-based lookup for '{}' - "
                     "TypeRef mapping should have succeeded if types were properly registered",
                     instantiated_enum_name);

            enum_type = types().get_type(instantiated_enum_name);
            if (!enum_type && instantiated_enum_name != resolved_enum_name)
            {
                enum_type = types().get_type(resolved_enum_name);
            }
        }

        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                  "generate_enum_variant: Enum type lookup for '{}': {}",
                  instantiated_enum_name, enum_type ? "found" : "not found");

        // For cross-module enum variant calls with payloads, create an extern declaration
        // This handles cases where the enum is defined in another module
        if (!args.empty())
        {
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "generate_enum_variant: Creating extern declaration for '{}' with {} args",
                      qualified_variant, args.size());

            // Build parameter types from the arguments
            std::vector<llvm::Type *> param_types;
            for (auto *arg : args)
            {
                param_types.push_back(arg->getType());
            }

            // If we don't have the enum type, fail - no placeholder types
            // Generic enum types must be properly monomorphized before use
            if (!enum_type)
            {
                report_error(ErrorCode::E0633_FUNCTION_BODY_ERROR, node,
                             "Unknown enum type for variant constructor: " + resolved_enum_name +
                                 " (generic enums must be instantiated before use)");
                return nullptr;
            }

            // Create function type: (...payload_types) -> enum_type
            llvm::FunctionType *ctor_type = llvm::FunctionType::get(enum_type, param_types, false);

            // Create extern declaration
            llvm::Function *extern_ctor = llvm::Function::Create(
                ctor_type, llvm::Function::ExternalLinkage, qualified_variant, module());

            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "generate_enum_variant: Created extern declaration for '{}'", qualified_variant);

            // Coerce arguments to match constructor parameter types
            std::vector<llvm::Value *> coerced_args;
            for (size_t i = 0; i < args.size() && i < ctor_type->getNumParams(); ++i)
            {
                llvm::Value *arg = args[i];
                llvm::Type *param_type = ctor_type->getParamType(i);

                // Handle struct-to-pointer conversion
                if (arg && param_type->isPointerTy() && arg->getType()->isStructTy())
                {
                    llvm::AllocaInst *temp = create_entry_alloca(arg->getType(), "struct.arg.tmp");
                    builder().CreateStore(arg, temp);
                    arg = temp;
                }

                coerced_args.push_back(cast_if_needed(arg, param_type));
            }

            return builder().CreateCall(extern_ctor, coerced_args, variant_name);
        }

        // For simple enum variants (no args), we need the type to exist
        if (!enum_type)
        {
            report_error(ErrorCode::E0633_FUNCTION_BODY_ERROR, node,
                         "Unknown enum type: " + resolved_enum_name);
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

        if (intrinsic_name == "malloc")
        {
            if (args.empty())
            {
                report_error(ErrorCode::E0633_FUNCTION_BODY_ERROR, node, "malloc requires size argument");
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

        if (intrinsic_name == "free")
        {
            if (args.empty())
            {
                report_error(ErrorCode::E0636_UNDEFINED_FUNCTION_CALL, node, "free requires pointer argument");
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
            LOG_DEBUG(Cryo::LogComponent::CODEGEN, "[STDLIB REDIRECT] Redirecting {} to printf intrinsic", intrinsic_name);

            // Generate arguments
            auto args = generate_arguments(node->arguments());

            // Delegate to the proper intrinsic system that handles Windows ABI
            Intrinsics intrinsic_gen(ctx().llvm_manager(), ctx().diagnostics());
            return intrinsic_gen.generate_intrinsic_call(node, "printf", args);
        }

        if (intrinsic_name == "float32_to_string")
        {
            LOG_DEBUG(Cryo::LogComponent::CODEGEN, "Generating __float32_to_string__ intrinsic");
            Intrinsics intrinsic_gen(ctx().llvm_manager(), ctx().diagnostics());
            return intrinsic_gen.generate_intrinsic_call(node, "float32_to_string", args);
        }

        if (intrinsic_name == "float64_to_string")
        {
            LOG_DEBUG(Cryo::LogComponent::CODEGEN, "Generating __float64_to_string__ intrinsic");
            Intrinsics intrinsic_gen(ctx().llvm_manager(), ctx().diagnostics());
            return intrinsic_gen.generate_intrinsic_call(node, "float64_to_string", args);
        }

        if (intrinsic_name == "memcpy")
        {
            LOG_DEBUG(Cryo::LogComponent::CODEGEN, "Generating memcpy intrinsic");
            Intrinsics intrinsic_gen(ctx().llvm_manager(), ctx().diagnostics());
            return intrinsic_gen.generate_intrinsic_call(node, "memcpy", args);
        }

        // Delegate to the comprehensive Intrinsics class
        // Strip namespace prefixes (e.g., "intrinsics::malloc" → "malloc")
        std::string bare_name = intrinsic_name;
        size_t last_sep = bare_name.rfind("::");
        if (last_sep != std::string::npos)
        {
            bare_name = bare_name.substr(last_sep + 2);
        }

        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "Delegating intrinsic '{}' to Intrinsics class (bare name: '{}')", intrinsic_name, bare_name);
        Intrinsics intrinsic_gen(ctx().llvm_manager(), ctx().diagnostics());
        return intrinsic_gen.generate_intrinsic_call(node, bare_name, args);
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
                llvm::Value *arg = args[i];
                llvm::Type *param_type = fn_type->getParamType(i);

                // Handle struct-to-pointer conversion
                if (arg && param_type->isPointerTy() && arg->getType()->isStructTy())
                {
                    llvm::AllocaInst *temp = create_entry_alloca(arg->getType(), "struct.arg.tmp");
                    builder().CreateStore(arg, temp);
                    arg = temp;
                }

                coerced_args.push_back(cast_if_needed(arg, param_type));
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

        std::string resolved_type_name = type_name;
        CodegenVisitor *visitor = ctx().visitor();
        GenericCodegen *generics = visitor ? visitor->get_generics() : nullptr;

        // First, check if the call expression has a resolved type we can use
        // This is crucial for generic static methods like Array::new() where the return type
        // tells us which instantiation to use (e.g., Array<u8> -> Array_u8)
        TypeRef call_resolved_type = node->get_resolved_type();
        if (call_resolved_type.is_valid())
        {
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "generate_static_method: Call has resolved type: {} (kind={})",
                      call_resolved_type->display_name(), static_cast<int>(call_resolved_type->kind()));

            if (call_resolved_type->kind() == Cryo::TypeKind::InstantiatedType)
            {
                auto *inst_type = static_cast<const Cryo::InstantiatedType *>(call_resolved_type.get());

                // Check if the InstantiatedType has a resolved concrete type
                if (inst_type->has_resolved_type())
                {
                    TypeRef concrete_type = inst_type->resolved_type();
                    if (concrete_type.is_valid())
                    {
                        resolved_type_name = concrete_type->display_name();
                        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                  "generate_static_method: Using InstantiatedType's resolved concrete type: {}",
                                  resolved_type_name);
                    }
                }
                else if (generics && generics->in_type_param_scope())
                {
                    // Try to substitute type parameters
                    std::string resolved_display = call_resolved_type->display_name();
                    std::string substituted = generics->substitute_type_annotation(resolved_display);
                    if (!substituted.empty())
                    {
                        resolved_type_name = substituted;
                        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                  "generate_static_method: Substituted type annotation {} -> {}",
                                  resolved_display, resolved_type_name);
                    }
                }
            }
            else if (call_resolved_type->kind() == Cryo::TypeKind::Struct)
            {
                // Already a concrete struct type
                resolved_type_name = call_resolved_type->display_name();
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "generate_static_method: Using concrete resolved type: {}",
                          resolved_type_name);
            }
        }

        // Fallback: Resolve generic template names to the current instantiated type
        // This handles calls like HashSet::new() inside HashSet<T> methods
        if (resolved_type_name == type_name && generics && generics->is_generic_template(type_name))
        {
            std::string current_type = ctx().current_type_name();
            // Check if we're inside an instantiated version of this generic type
            // The current type name would be like "HashSet_string" for HashSet<T>
            if (!current_type.empty() && current_type.find(type_name + "_") == 0)
            {
                resolved_type_name = current_type;
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "generate_static_method: Resolved generic template '{}' to current type '{}'",
                          type_name, resolved_type_name);
            }
        }

        // Generate arguments
        auto args = generate_arguments(node->arguments());

        // 1. First try: resolve the static method directly
        llvm::Function *method = resolve_method(resolved_type_name, method_name);
        if (method)
        {
            // Call with arguments (no implicit 'this')
            std::vector<llvm::Value *> coerced_args;
            llvm::FunctionType *fn_type = method->getFunctionType();

            for (size_t i = 0; i < args.size() && i < fn_type->getNumParams(); ++i)
            {
                llvm::Value *arg = args[i];
                llvm::Type *param_type = fn_type->getParamType(i);

                // Handle struct-to-pointer conversion
                if (arg && param_type->isPointerTy() && arg->getType()->isStructTy())
                {
                    llvm::AllocaInst *temp = create_entry_alloca(arg->getType(), "struct.arg.tmp");
                    builder().CreateStore(arg, temp);
                    arg = temp;
                }

                coerced_args.push_back(cast_if_needed(arg, param_type));
            }

            std::string result_name = method->getReturnType()->isVoidTy() ? "" : method_name + ".result";
            return builder().CreateCall(method, coerced_args, result_name);
        }

        // 1b. On-demand instantiation fallback for generic types
        // If the type name contains '<' (e.g., "Array<u8>"), try to instantiate it first
        if (!method && resolved_type_name.find('<') != std::string::npos)
        {
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "generate_static_method: Method not found, trying on-demand instantiation for generic type '{}'",
                      resolved_type_name);

            // Parse: "Array<u8>" -> base_name="Array", type_args_str="u8"
            size_t angle_pos = resolved_type_name.find('<');
            size_t close_pos = resolved_type_name.rfind('>');

            if (angle_pos != std::string::npos && close_pos != std::string::npos && close_pos > angle_pos)
            {
                std::string base_name = resolved_type_name.substr(0, angle_pos);
                std::string type_args_str = resolved_type_name.substr(angle_pos + 1, close_pos - angle_pos - 1);

                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "generate_static_method: Parsed generic - base='{}', type_args='{}'",
                          base_name, type_args_str);

                // Get the GenericCodegen component
                CodegenVisitor *visitor = ctx().visitor();
                GenericCodegen *gen_codegen = visitor ? visitor->get_generics() : nullptr;

                // Check if base is a registered generic template
                if (gen_codegen && gen_codegen->is_generic_template(base_name))
                {
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "generate_static_method: '{}' is a registered generic template, triggering instantiation",
                              base_name);

                    // Parse type arguments from string
                    std::vector<TypeRef> type_args = parse_type_args_from_string(type_args_str, symbols());

                    if (!type_args.empty())
                    {
                        // Trigger on-demand instantiation
                        llvm::StructType *instantiated = gen_codegen->instantiate_struct(base_name, type_args);

                        if (instantiated)
                        {
                            // Get the mangled name and retry method lookup
                            std::string mangled_name = gen_codegen->mangle_type_name(base_name, type_args);
                            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                      "generate_static_method: Instantiated struct '{}', retrying method lookup with mangled name '{}'",
                                      instantiated->getName().str(), mangled_name);

                            // Retry method resolution with the mangled type name
                            method = resolve_method(mangled_name, method_name);

                            if (method)
                            {
                                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                          "generate_static_method: Found method after instantiation: {}::{}",
                                          mangled_name, method_name);

                                // Call with arguments (no implicit 'this')
                                std::vector<llvm::Value *> coerced_args;
                                llvm::FunctionType *fn_type = method->getFunctionType();

                                for (size_t i = 0; i < args.size() && i < fn_type->getNumParams(); ++i)
                                {
                                    llvm::Value *arg = args[i];
                                    llvm::Type *param_type = fn_type->getParamType(i);

                                    // Handle struct-to-pointer conversion
                                    if (arg && param_type->isPointerTy() && arg->getType()->isStructTy())
                                    {
                                        llvm::AllocaInst *temp = create_entry_alloca(arg->getType(), "struct.arg.tmp");
                                        builder().CreateStore(arg, temp);
                                        arg = temp;
                                    }

                                    coerced_args.push_back(cast_if_needed(arg, param_type));
                                }

                                std::string result_name = method->getReturnType()->isVoidTy() ? "" : method_name + ".result";
                                return builder().CreateCall(method, coerced_args, result_name);
                            }
                            else
                            {
                                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                          "generate_static_method: Method still not found after instantiation: {}::{}",
                                          mangled_name, method_name);
                            }
                        }
                        else
                        {
                            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                      "generate_static_method: Failed to instantiate generic struct '{}'", base_name);
                        }
                    }
                    else
                    {
                        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                  "generate_static_method: Failed to parse type arguments from '{}'", type_args_str);
                    }
                }
                else
                {
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "generate_static_method: '{}' is not a registered generic template", base_name);
                }
            }
        }

        // 2. Fallback for ::new() - try constructor, then zero-init (Option C)
        if (method_name == "new")
        {
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "Static new() not found for {}, trying constructor fallback", resolved_type_name);

            // Get the type (could be struct, array, or other)
            llvm::Type *target_type = resolve_type_by_name(resolved_type_name);
            if (!target_type)
            {
                report_error(ErrorCode::E0635_TYPE_CONSTRUCTOR_UNDEFINED, node,
                             "Unknown type for ::new(): " + resolved_type_name);
                return nullptr;
            }

            // For struct types, try to find a constructor first
            if (target_type->isStructTy())
            {
                // 2a. Try to find a matching constructor
                std::vector<llvm::Type *> arg_types;
                for (auto *arg : args)
                {
                    arg_types.push_back(arg->getType());
                }
                llvm::Function *ctor = resolve_constructor(resolved_type_name, arg_types);

                if (ctor)
                {
                    // Found a constructor - allocate and call it
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "Found constructor for {}, calling with {} args", resolved_type_name, args.size());
                    llvm::AllocaInst *alloca = create_entry_alloca(target_type, resolved_type_name + ".instance");
                    std::vector<llvm::Value *> ctor_args;
                    ctor_args.push_back(alloca);
                    ctor_args.insert(ctor_args.end(), args.begin(), args.end());
                    builder().CreateCall(ctor, ctor_args);
                    return alloca;
                }
            }

            // 2b. No constructor found - if no args, zero-initialize
            // This works for structs without constructors, arrays, and other types
            if (args.empty())
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "No constructor for {}, generating zero-initialized instance (type ID: {})",
                          resolved_type_name, target_type->getTypeID());
                llvm::AllocaInst *alloca = create_entry_alloca(target_type, resolved_type_name + ".instance");
                // Zero-initialize the type
                builder().CreateStore(llvm::Constant::getNullValue(target_type), alloca);
                return alloca;
            }

            // 2c. Has args but no matching constructor - error
            report_error(ErrorCode::E0636_UNDEFINED_FUNCTION_CALL, node,
                         "No constructor found for " + resolved_type_name + " that accepts " +
                             std::to_string(args.size()) + " argument(s)");
            return nullptr;
        }

        // 3. Fallback for ::default() - handle type aliases and generate default values
        if (method_name == "default" && args.empty())
        {
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "Static default() not found for {}, trying default value fallback", resolved_type_name);

            llvm::Value *default_val = generate_default_value(node, resolved_type_name);
            if (default_val)
            {
                return default_val;
            }
        }

        // Not ::new() or ::default(), regular static method not found - error
        report_error(ErrorCode::E0636_UNDEFINED_FUNCTION_CALL, node,
                     "Static method not found: " + resolved_type_name + "::" + method_name);
        return nullptr;
    }

    llvm::Value *CallCodegen::generate_default_value(Cryo::CallExpressionNode *node,
                                                      const std::string &type_name)
    {
        // Resolve type alias chain to get base type name
        std::string resolved_name = type_name;
        if (ctx().is_type_alias(type_name))
        {
            resolved_name = ctx().resolve_type_alias(type_name);
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "generate_default_value: '{}' is alias for '{}'", type_name, resolved_name);

            // Try target type's ::default() method first
            llvm::Function *target_default = resolve_method(resolved_name, "default");
            if (target_default)
            {
                return builder().CreateCall(target_default, {}, "default.result");
            }
        }

        // Look up TypeRef to determine type kind
        TypeRef type_ref = ctx().symbols().arena().lookup_type_by_name(resolved_name);

        // Unwrap nested TypeAlias to get actual target
        while (type_ref.is_valid() && type_ref->kind() == Cryo::TypeKind::TypeAlias)
        {
            auto *alias = static_cast<const Cryo::TypeAliasType *>(type_ref.get());
            type_ref = alias->target();
        }

        if (!type_ref.is_valid())
        {
            // Fallback: get LLVM type and generate zero value
            llvm::Type *llvm_type = resolve_type_by_name(resolved_name);
            if (llvm_type)
            {
                return generate_zero_value(llvm_type, type_name);
            }
            return nullptr;
        }

        // Generate default based on type kind
        llvm::Type *llvm_type = types().map(type_ref);
        if (!llvm_type)
        {
            return nullptr;
        }

        return generate_zero_value(llvm_type, type_name);
    }

    llvm::Value *CallCodegen::generate_zero_value(llvm::Type *llvm_type, const std::string &name)
    {
        // For aggregate types (structs), allocate and zero-initialize
        if (llvm_type->isStructTy())
        {
            llvm::AllocaInst *alloca = create_entry_alloca(llvm_type, name + ".default");
            builder().CreateStore(llvm::Constant::getNullValue(llvm_type), alloca);
            return alloca;
        }

        // For arrays, allocate and zero-initialize
        if (llvm_type->isArrayTy())
        {
            llvm::AllocaInst *alloca = create_entry_alloca(llvm_type, name + ".default");
            builder().CreateStore(llvm::Constant::getNullValue(llvm_type), alloca);
            return alloca;
        }

        // For scalar types, return zero constant directly
        return llvm::Constant::getNullValue(llvm_type);
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
            TypeRef obj_type = callee->object()->get_resolved_type();
            if (obj_type.is_valid())
            {
                type_name = obj_type->display_name();
                // Strip pointer/reference suffix if present
                if (!type_name.empty() && (type_name.back() == '*' || type_name.back() == '&'))
                {
                    type_name.pop_back();
                }
                // Handle pointer types explicitly
                if (obj_type->kind() == Cryo::TypeKind::Pointer)
                {
                    auto *ptr_type = dynamic_cast<const Cryo::PointerType *>(obj_type.get());
                    if (ptr_type && ptr_type->pointee().is_valid())
                    {
                        type_name = ptr_type->pointee()->display_name();
                    }
                }
                // Handle reference types explicitly
                else if (obj_type->kind() == Cryo::TypeKind::Reference)
                {
                    auto *ref_type = dynamic_cast<const Cryo::ReferenceType *>(obj_type.get());
                    if (ref_type && ref_type->referent().is_valid())
                    {
                        type_name = ref_type->referent()->display_name();
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
                    if (it != var_types.end() && it->second.is_valid())
                    {
                        TypeRef var_type = it->second;
                        // Handle pointer types
                        if (var_type->kind() == Cryo::TypeKind::Pointer)
                        {
                            auto *ptr_type = dynamic_cast<const Cryo::PointerType *>(var_type.get());
                            if (ptr_type && ptr_type->pointee().is_valid())
                            {
                                type_name = ptr_type->pointee()->display_name();
                            }
                            else
                            {
                                type_name = var_type->display_name();
                                if (!type_name.empty() && type_name.back() == '*')
                                    type_name.pop_back();
                            }
                        }
                        // Handle reference types
                        else if (var_type->kind() == Cryo::TypeKind::Reference)
                        {
                            auto *ref_type = dynamic_cast<const Cryo::ReferenceType *>(var_type.get());
                            if (ref_type && ref_type->referent().is_valid())
                            {
                                type_name = ref_type->referent()->display_name();
                            }
                            else
                            {
                                type_name = var_type->display_name();
                                if (!type_name.empty() && type_name.back() == '&')
                                    type_name.pop_back();
                            }
                        }
                        else
                        {
                            type_name = var_type->display_name();
                        }
                        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                  "Instance method receiver type from variable map: {}", type_name);
                    }
                }
                // For nested member access (e.g., this.ip.eq()), resolve the field type properly
                else if (auto *member_access = dynamic_cast<MemberAccessNode *>(callee->object()))
                {
                    std::string member_name = member_access->member();
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "Attempting type resolution for nested member access: {}", member_name);

                    // Get the parent object's type to look up the field type
                    std::string parent_type_name;

                    // Check if the parent object is 'this'
                    if (auto *parent_id = dynamic_cast<IdentifierNode *>(member_access->object()))
                    {
                        if (parent_id->name() == "this")
                        {
                            // Use current_type_name for 'this'
                            parent_type_name = ctx().current_type_name();
                            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                      "Nested member access: parent is 'this' -> {}", parent_type_name);
                        }
                        else
                        {
                            // Look up the identifier's type from variable_types_map
                            auto &var_types = ctx().variable_types_map();
                            auto it = var_types.find(parent_id->name());
                            if (it != var_types.end() && it->second.is_valid())
                            {
                                parent_type_name = it->second->display_name();
                                if (!parent_type_name.empty() && parent_type_name.back() == '*')
                                {
                                    parent_type_name.pop_back();
                                }
                                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                          "Nested member access: parent '{}' -> {}", parent_id->name(), parent_type_name);
                            }
                        }
                    }

                    // Now look up the field type using TemplateRegistry
                    if (!parent_type_name.empty())
                    {
                        // Generate type name candidates (simple name, qualified name, etc.)
                        std::vector<std::string> type_candidates = generate_lookup_candidates(parent_type_name, Cryo::SymbolKind::Type);

                        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                  "Nested member access: Looking up field '{}' in type '{}', {} candidates generated",
                                  member_name, parent_type_name, type_candidates.size());
                        for (size_t i = 0; i < type_candidates.size(); ++i)
                        {
                            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                      "  Candidate[{}]: '{}'", i, type_candidates[i]);
                        }

                        if (auto *template_reg = ctx().template_registry())
                        {
                            // Try each candidate name
                            for (const auto &candidate : type_candidates)
                            {
                                const TemplateRegistry::StructFieldInfo *field_info = template_reg->get_struct_field_types(candidate);
                                if (field_info)
                                {
                                    for (size_t i = 0; i < field_info->field_names.size(); ++i)
                                    {
                                        if (field_info->field_names[i] == member_name && i < field_info->field_types.size())
                                        {
                                            TypeRef field_type = field_info->field_types[i];
                                            if (field_type.is_valid())
                                            {
                                                type_name = field_type->display_name();
                                                if (field_type->kind() == Cryo::TypeKind::Pointer)
                                                {
                                                    auto *ptr_type = dynamic_cast<const Cryo::PointerType *>(field_type.get());
                                                    if (ptr_type && ptr_type->pointee().is_valid())
                                                    {
                                                        type_name = ptr_type->pointee()->display_name();
                                                    }
                                                }
                                                else if (!type_name.empty() && type_name.back() == '*')
                                                {
                                                    type_name.pop_back();
                                                }

                                                // If display_name contains an error marker, try to resolve
                                                // from the struct declaration's field type annotation instead
                                                if (type_name.empty() || type_name.find("<error:") != std::string::npos)
                                                {
                                                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                                              "Field type display_name is invalid ('{}'), trying annotation fallback for field '{}'",
                                                              type_name, member_name);

                                                    // Try to get the field's type annotation from the struct template
                                                    const auto *tmpl_info = template_reg->find_template(candidate);
                                                    if (tmpl_info && tmpl_info->struct_template)
                                                    {
                                                        for (const auto &field : tmpl_info->struct_template->fields())
                                                        {
                                                            if (field && field->name() == member_name && field->type_annotation())
                                                            {
                                                                type_name = field->type_annotation()->to_string();
                                                                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                                                          "Resolved field type from struct template annotation: {} -> {}",
                                                                          member_name, type_name);
                                                                break;
                                                            }
                                                        }
                                                    }
                                                }

                                                // Now look up the source_namespace for this type so method resolution
                                                // can find methods defined in the type's original module
                                                if (!type_name.empty() && type_name.find("::") == std::string::npos)
                                                {
                                                    // Type name is unqualified, look up its source namespace
                                                    std::vector<std::string> field_type_candidates = generate_lookup_candidates(type_name, Cryo::SymbolKind::Type);
                                                    for (const auto &type_candidate : field_type_candidates)
                                                    {
                                                        const TemplateRegistry::StructFieldInfo *type_field_info = template_reg->get_struct_field_types(type_candidate);
                                                        if (type_field_info && !type_field_info->source_namespace.empty())
                                                        {
                                                            // Found the type's source namespace - qualify the type name
                                                            std::string qualified_type = type_field_info->source_namespace + "::" + type_name;
                                                            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                                                      "Qualified field type '{}' to '{}' using source_namespace from '{}'",
                                                                      type_name, qualified_type, type_candidate);
                                                            type_name = qualified_type;
                                                            break;
                                                        }
                                                    }
                                                }

                                                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                                          "Resolved nested member access: {}.{} -> {} (via TemplateRegistry, found as '{}')",
                                                          parent_type_name, member_name, type_name, candidate);
                                                break;
                                            }
                                        }
                                    }
                                    if (!type_name.empty())
                                        break;
                                }
                            }

                            if (type_name.empty())
                            {
                                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                          "No field info in TemplateRegistry for type: {} (tried {} candidates)",
                                          parent_type_name, type_candidates.size());
                            }
                        }

                        // Fallback: try StructType::field_type from symbols
                        if (type_name.empty())
                        {
                            for (const auto &candidate : type_candidates)
                            {
                                TypeRef struct_type_ref = ctx().symbols().lookup_struct_type(candidate);
                                if (struct_type_ref.is_valid() && struct_type_ref->kind() == Cryo::TypeKind::Struct)
                                {
                                    auto *struct_ty = static_cast<const Cryo::StructType *>(struct_type_ref.get());
                                    auto field_type_opt = struct_ty->field_type(member_name);
                                    if (field_type_opt.has_value() && field_type_opt->is_valid())
                                    {
                                        type_name = field_type_opt->get()->display_name();
                                        if (!type_name.empty() && type_name.back() == '*')
                                        {
                                            type_name.pop_back();
                                        }

                                        // Now look up the source_namespace for this type so method resolution
                                        // can find methods defined in the type's original module
                                        if (!type_name.empty() && type_name.find("::") == std::string::npos)
                                        {
                                            if (auto *template_reg = ctx().template_registry())
                                            {
                                                std::vector<std::string> field_type_candidates = generate_lookup_candidates(type_name, Cryo::SymbolKind::Type);
                                                for (const auto &type_candidate : field_type_candidates)
                                                {
                                                    const TemplateRegistry::StructFieldInfo *type_field_info = template_reg->get_struct_field_types(type_candidate);
                                                    if (type_field_info && !type_field_info->source_namespace.empty())
                                                    {
                                                        std::string qualified_type = type_field_info->source_namespace + "::" + type_name;
                                                        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                                                  "Qualified field type '{}' to '{}' using source_namespace from '{}'",
                                                                  type_name, qualified_type, type_candidate);
                                                        type_name = qualified_type;
                                                        break;
                                                    }
                                                }
                                            }
                                        }

                                        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                                  "Resolved nested member access: {}.{} -> {} (via StructType, found as '{}')",
                                                  parent_type_name, member_name, type_name, candidate);
                                        break;
                                    }
                                }
                            }
                        }
                    }
                }
                // Additional fallback for chained method calls (e.g., this.next().is_some())
                // When the object is a CallExpressionNode, try to get the return type from the call
                else if (auto *call_expr = dynamic_cast<CallExpressionNode *>(callee->object()))
                {
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "Attempting type resolution for chained call expression");

                    // Try to get the resolved type from the call expression
                    if (call_expr->has_resolved_type())
                    {
                        TypeRef call_return_type = call_expr->get_resolved_type();
                        if (call_return_type.is_valid())
                        {
                            type_name = call_return_type->display_name();
                            // Strip pointer suffix if present
                            if (!type_name.empty() && type_name.back() == '*')
                            {
                                type_name.pop_back();
                            }
                            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                      "Resolved chained call return type: {}", type_name);
                        }
                    }
                    else
                    {
                        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                  "Chained call expression has no resolved type");

                        // Fallback: Try to infer the return type from the inner method call
                        // If the inner call's callee is a MemberAccessNode, we can find the method
                        // and get its return type from the LLVM function
                        if (auto *inner_callee = dynamic_cast<MemberAccessNode *>(call_expr->callee()))
                        {
                            std::string inner_method_name = inner_callee->member();
                            std::string inner_receiver_type;

                            // Get the receiver type for the inner call
                            if (inner_callee->object())
                            {
                                if (auto *inner_id = dynamic_cast<IdentifierNode *>(inner_callee->object()))
                                {
                                    auto &var_types = ctx().variable_types_map();
                                    auto it = var_types.find(inner_id->name());
                                    if (it != var_types.end() && it->second.is_valid())
                                    {
                                        inner_receiver_type = it->second->display_name();
                                        if (!inner_receiver_type.empty() && inner_receiver_type.back() == '*')
                                        {
                                            inner_receiver_type.pop_back();
                                        }
                                    }
                                }
                                // Handle nested member access (e.g., this.args in this.args.get())
                                else if (auto *nested_member = dynamic_cast<MemberAccessNode *>(inner_callee->object()))
                                {
                                    std::string nested_field_name = nested_member->member();
                                    std::string nested_parent_type;

                                    // Get the parent object's type (e.g., 'this' -> Command)
                                    if (auto *nested_parent_id = dynamic_cast<IdentifierNode *>(nested_member->object()))
                                    {
                                        if (nested_parent_id->name() == "this")
                                        {
                                            nested_parent_type = ctx().current_type_name();
                                        }
                                        else
                                        {
                                            auto &var_types = ctx().variable_types_map();
                                            auto it = var_types.find(nested_parent_id->name());
                                            if (it != var_types.end() && it->second.is_valid())
                                            {
                                                nested_parent_type = it->second->display_name();
                                                if (!nested_parent_type.empty() && nested_parent_type.back() == '*')
                                                {
                                                    nested_parent_type.pop_back();
                                                }
                                            }
                                        }
                                    }

                                    // Look up the field type using TemplateRegistry
                                    if (!nested_parent_type.empty())
                                    {
                                        auto type_candidates = generate_lookup_candidates(nested_parent_type, Cryo::SymbolKind::Type);
                                        if (auto *template_reg = ctx().template_registry())
                                        {
                                            for (const auto &candidate : type_candidates)
                                            {
                                                const TemplateRegistry::StructFieldInfo *field_info = template_reg->get_struct_field_types(candidate);
                                                if (field_info)
                                                {
                                                    for (size_t i = 0; i < field_info->field_names.size(); ++i)
                                                    {
                                                        if (field_info->field_names[i] == nested_field_name && i < field_info->field_types.size())
                                                        {
                                                            TypeRef field_type = field_info->field_types[i];
                                                            if (field_type.is_valid())
                                                            {
                                                                inner_receiver_type = field_type->display_name();
                                                                if (field_type->kind() == Cryo::TypeKind::Pointer)
                                                                {
                                                                    auto *ptr_type = dynamic_cast<const Cryo::PointerType *>(field_type.get());
                                                                    if (ptr_type && ptr_type->pointee().is_valid())
                                                                    {
                                                                        inner_receiver_type = ptr_type->pointee()->display_name();
                                                                    }
                                                                }
                                                                else if (!inner_receiver_type.empty() && inner_receiver_type.back() == '*')
                                                                {
                                                                    inner_receiver_type.pop_back();
                                                                }
                                                                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                                                          "Chained call: resolved nested member '{}.{}' to type '{}'",
                                                                          nested_parent_type, nested_field_name, inner_receiver_type);
                                                            }
                                                            break;
                                                        }
                                                    }
                                                    if (!inner_receiver_type.empty())
                                                        break;
                                                }
                                            }
                                        }
                                    }
                                }
                                else if (inner_callee->object()->has_resolved_type())
                                {
                                    TypeRef inner_obj_type = inner_callee->object()->get_resolved_type();
                                    if (inner_obj_type.is_valid())
                                    {
                                        inner_receiver_type = inner_obj_type->display_name();
                                        if (!inner_receiver_type.empty() && inner_receiver_type.back() == '*')
                                        {
                                            inner_receiver_type.pop_back();
                                        }
                                        // Handle pointer types
                                        if (inner_obj_type->kind() == Cryo::TypeKind::Pointer)
                                        {
                                            auto *ptr_type = dynamic_cast<const Cryo::PointerType *>(inner_obj_type.get());
                                            if (ptr_type && ptr_type->pointee().is_valid())
                                            {
                                                inner_receiver_type = ptr_type->pointee()->display_name();
                                            }
                                        }
                                    }
                                }
                            }

                            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                      "Chained call fallback: inner method='{}', inner receiver type='{}'",
                                      inner_method_name, inner_receiver_type);

                            if (!inner_receiver_type.empty())
                            {
                                // Find the inner method's LLVM function
                                llvm::Function *inner_method = resolve_method_by_name(inner_receiver_type, inner_method_name);
                                if (inner_method)
                                {
                                    // Get the return type from the LLVM function
                                    llvm::Type *ret_type = inner_method->getReturnType();
                                    if (ret_type && !ret_type->isVoidTy())
                                    {
                                        // Try to get the Cryo type name from the LLVM struct type
                                        if (ret_type->isStructTy())
                                        {
                                            llvm::StructType *struct_type = llvm::cast<llvm::StructType>(ret_type);
                                            if (struct_type->hasName())
                                            {
                                                type_name = struct_type->getName().str();
                                                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                                          "Inferred chained call return type from LLVM: {}", type_name);
                                            }
                                        }
                                        else if (ret_type->isPointerTy())
                                        {
                                            // For pointer returns, try to get the element type
                                            type_name = "ptr";
                                            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                                      "Chained call returns pointer type");
                                        }
                                    }
                                }
                                else
                                {
                                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                              "Chained call fallback: Could not find inner method '{}.{}'",
                                              inner_receiver_type, inner_method_name);
                                }
                            }
                        }
                        // Handle static method calls (e.g., Instant::now() in Instant::now().ge())
                        else if (auto *scope_callee = dynamic_cast<ScopeResolutionNode *>(call_expr->callee()))
                        {
                            std::string static_type_name = scope_callee->scope_name();
                            std::string static_method_name = scope_callee->member_name();

                            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                      "Chained call: inner call is static method {}::{}",
                                      static_type_name, static_method_name);

                            // Try to find the static method's LLVM function
                            llvm::Function *static_method = resolve_method_by_name(static_type_name, static_method_name);
                            if (static_method)
                            {
                                llvm::Type *ret_type = static_method->getReturnType();
                                if (ret_type && !ret_type->isVoidTy())
                                {
                                    if (ret_type->isStructTy())
                                    {
                                        llvm::StructType *struct_type = llvm::cast<llvm::StructType>(ret_type);
                                        if (struct_type->hasName())
                                        {
                                            type_name = struct_type->getName().str();
                                            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                                      "Resolved static method return type from LLVM: {}::{}() -> {}",
                                                      static_type_name, static_method_name, type_name);
                                        }
                                    }
                                    else if (ret_type->isPointerTy())
                                    {
                                        // For pointer returns, the type name is the static type itself
                                        // (static methods returning Self* or similar)
                                        type_name = static_type_name;
                                        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                                  "Chained call: static method returns pointer, using type '{}'",
                                                  type_name);
                                    }
                                }
                            }
                            else
                            {
                                // Try template registry as fallback
                                auto *template_reg = ctx().template_registry();
                                if (template_reg)
                                {
                                    // Generate candidates for the static type
                                    auto type_candidates = generate_lookup_candidates(static_type_name, Cryo::SymbolKind::Type);
                                    for (const auto &candidate : type_candidates)
                                    {
                                        const TemplateRegistry::TemplateMethodInfo *method_info =
                                            template_reg->get_template_method_info(static_type_name);
                                        if (method_info)
                                        {
                                            const TemplateRegistry::MethodMetadata *meta =
                                                template_reg->find_template_method(static_type_name, static_method_name);
                                            if (meta && !meta->return_type_annotation.empty())
                                            {
                                                // The return type annotation tells us what type is returned
                                                type_name = meta->return_type_annotation;
                                                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                                          "Resolved static method return from template: {}::{}() -> {}",
                                                          static_type_name, static_method_name, type_name);
                                                break;
                                            }
                                        }
                                    }
                                }

                                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                          "Chained call: Could not find static method {}::{}",
                                          static_type_name, static_method_name);
                            }
                        }
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

        llvm::FunctionType *fn_type = method->getFunctionType();

        // Check if the method expects 'this' by value (e.g., simple enum i32) instead of by pointer
        // If so, load the value from the receiver pointer
        llvm::Value *this_arg = receiver;
        if (fn_type->getNumParams() > 0)
        {
            llvm::Type *this_param_type = fn_type->getParamType(0);
            if (!this_param_type->isPointerTy() && receiver->getType()->isPointerTy())
            {
                // Function expects value but we have pointer - load the value
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "generate_instance_method: Loading value from pointer for 'this' parameter (simple enum)");
                this_arg = builder().CreateLoad(this_param_type, receiver, "this.load");
            }
        }
        call_args.push_back(this_arg);
        for (size_t i = 0; i < args.size(); ++i)
        {
            size_t param_idx = i + 1; // Skip 'this' parameter
            if (param_idx < fn_type->getNumParams())
            {
                llvm::Value *arg = args[i];
                llvm::Type *param_type = fn_type->getParamType(param_idx);

                // Handle struct-to-pointer conversion: if arg is a struct value but param expects pointer,
                // create a temporary alloca and pass its address
                if (arg && param_type->isPointerTy() && arg->getType()->isStructTy())
                {
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "generate_instance_method: Converting struct arg to pointer for param {}",
                              param_idx);
                    llvm::AllocaInst *temp = create_entry_alloca(arg->getType(), "struct.arg.tmp");
                    builder().CreateStore(arg, temp);
                    arg = temp;
                }

                call_args.push_back(cast_if_needed(arg, param_type));
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
                llvm::Value *arg = args[i];
                llvm::Type *param_type = fn_type->getParamType(i);

                // Handle struct-to-pointer conversion
                if (arg && param_type->isPointerTy() && arg->getType()->isStructTy())
                {
                    llvm::AllocaInst *temp = create_entry_alloca(arg->getType(), "struct.arg.tmp");
                    builder().CreateStore(arg, temp);
                    arg = temp;
                }

                coerced_args.push_back(cast_if_needed(arg, param_type));
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

        for (size_t i = 0; i < args.size(); ++i)
        {
            const auto &arg = args[i];
            if (arg)
            {
                llvm::Value *value = generate_expression(arg.get());
                if (value)
                {
                    result.push_back(value);
                }
                else
                {
                    // Push nullptr as placeholder to preserve argument count
                    // This helps intrinsic handlers provide accurate error messages
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "Failed to generate argument {} (expression kind: {})",
                              i, static_cast<int>(arg->kind()));
                    result.push_back(nullptr);
                }
            }
            else
            {
                // Null argument expression - push nullptr placeholder
                LOG_DEBUG(Cryo::LogComponent::CODEGEN, "Null argument expression at index {}", i);
                result.push_back(nullptr);
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

        // First pass: prefer qualified candidates (those with ::)
        // This ensures we create declarations with namespace-qualified names
        // which will match the actual function definitions
        Cryo::Symbol *found_symbol = nullptr;
        std::string found_candidate;

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

            // Check for both regular functions and intrinsics (which are also functions)
            bool is_function_or_intrinsic = symbol &&
                                            (symbol->kind == Cryo::SymbolKind::Function ||
                                             symbol->kind == Cryo::SymbolKind::Intrinsic) &&
                                            symbol->type.is_valid();

            if (is_function_or_intrinsic)
            {
                // Prefer qualified candidates over unqualified ones
                bool candidate_is_qualified = (candidate.find("::") != std::string::npos);
                bool found_is_qualified = (found_candidate.find("::") != std::string::npos);

                if (!found_symbol || (candidate_is_qualified && !found_is_qualified))
                {
                    found_symbol = symbol;
                    found_candidate = candidate;

                    // If we found a qualified candidate, use it immediately
                    if (candidate_is_qualified)
                        break;
                }
            }
        }

        if (found_symbol)
        {
            const Cryo::FunctionType *func_type = dynamic_cast<const Cryo::FunctionType *>(found_symbol->type.get());
            if (func_type)
            {
                bool is_intrinsic = (found_symbol->kind == Cryo::SymbolKind::Intrinsic);
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "create_forward_declaration_from_symbol: Found {} '{}' in symbol table",
                          is_intrinsic ? "intrinsic" : "function", found_candidate);

                // Build LLVM function type from Cryo function type
                llvm::Type *return_type = types().map(func_type->return_type());
                if (!return_type)
                {
                    LOG_ERROR(Cryo::LogComponent::CODEGEN,
                              "Failed to map return type for function '{}'", found_candidate);
                    return nullptr;
                }

                std::vector<llvm::Type *> param_types;
                for (const auto &param : func_type->param_types())
                {
                    llvm::Type *pt = types().map(param);
                    if (!pt)
                    {
                        LOG_ERROR(Cryo::LogComponent::CODEGEN,
                                  "Failed to map parameter type for function '{}'", found_candidate);
                        return nullptr;
                    }
                    param_types.push_back(pt);
                }

                // Create the function type
                llvm::FunctionType *llvm_func_type = llvm::FunctionType::get(
                    return_type, param_types, func_type->is_variadic());

                // For intrinsics (like malloc, free, etc.), use the simple name as LLVM function name
                // since they map to actual C library functions. For regular functions, use qualified name.
                std::string llvm_name;
                if (is_intrinsic)
                {
                    llvm_name = name;
                }
                else
                {
                    // Construct the qualified name the same way function definitions do:
                    // Use the current namespace context from codegen (not from symbol table)
                    // This ensures forward declarations match the actual function definitions.
                    std::string ns_context = ctx().namespace_context();
                    std::string simple_name = found_symbol->name;

                    // If the symbol name contains ::, extract the simple name
                    size_t last_sep = simple_name.rfind("::");
                    if (last_sep != std::string::npos)
                    {
                        simple_name = simple_name.substr(last_sep + 2);
                    }

                    // Special case: main should never be namespace qualified
                    if (simple_name == "main" || simple_name == "_user_main_")
                    {
                        llvm_name = simple_name;
                    }
                    else if (!ns_context.empty())
                    {
                        // Use the namespace context to qualify the name
                        llvm_name = ns_context + "::" + simple_name;
                    }
                    else if (found_candidate.find("::") != std::string::npos)
                    {
                        // The candidate already has a namespace (e.g., from imported namespace)
                        llvm_name = found_candidate;
                    }
                    else
                    {
                        llvm_name = simple_name;
                    }
                }

                // IMPORTANT: Before creating a new declaration, check if a function with this name
                // (or a qualified version of it) already exists in the module.
                // This handles the case where the definition uses a qualified name (e.g., "HttpServer::handle_client")
                // but we're looking up with an unqualified name.
                if (llvm::Function *existing = module()->getFunction(llvm_name))
                {
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "create_forward_declaration_from_symbol: Found existing function '{}', reusing",
                              llvm_name);
                    ctx().register_function(found_candidate, existing);
                    if (found_candidate != name)
                        ctx().register_function(name, existing);
                    return existing;
                }

                // If llvm_name is unqualified, check if a qualified version exists in the module
                if (!is_intrinsic && llvm_name.find("::") == std::string::npos)
                {
                    std::string suffix = "::" + llvm_name;
                    for (auto &fn_iter : module()->functions())
                    {
                        std::string fn_name = fn_iter.getName().str();
                        if (fn_name.size() > suffix.size() &&
                            fn_name.compare(fn_name.size() - suffix.size(), suffix.size(), suffix) == 0)
                        {
                            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                      "create_forward_declaration_from_symbol: Found qualified function '{}' for '{}', reusing",
                                      fn_name, llvm_name);
                            ctx().register_function(found_candidate, &fn_iter);
                            if (found_candidate != name)
                                ctx().register_function(name, &fn_iter);
                            return &fn_iter;
                        }
                    }
                }

                // Create the function declaration
                llvm::Function *fn = llvm::Function::Create(
                    llvm_func_type,
                    llvm::Function::ExternalLinkage,
                    llvm_name,
                    module());

                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "create_forward_declaration_from_symbol: Created forward declaration for '{}' (LLVM name: '{}')",
                          found_candidate, llvm_name);

                // Register in context for future lookups (using both qualified and simple names for intrinsics)
                ctx().register_function(found_candidate, fn);
                if (is_intrinsic && found_candidate != name)
                {
                    ctx().register_function(name, fn);
                }

                return fn;
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
                simple_name + "::" + simple_name};

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
        return _intrinsic_functions.find(name) != _intrinsic_functions.end();
    }

    bool CallCodegen::is_struct_type(const std::string &name) const
    {
        // First check if this is a type alias and resolve to base type
        std::string resolved_name = ctx().resolve_type_alias(name);
        if (resolved_name != name)
        {
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "is_struct_type: '{}' is alias for '{}', checking base type", name, resolved_name);
            // Recursively check if the base type is a struct
            return is_struct_type(resolved_name);
        }

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
        // First check if this is a type alias and resolve to base type
        std::string resolved_name = ctx().resolve_type_alias(name);
        if (resolved_name != name)
        {
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "is_enum_type: '{}' is alias for '{}', checking base type", name, resolved_name);
            // Recursively check if the base type is an enum
            return is_enum_type(resolved_name);
        }

        // First try direct lookup in symbol table with the unqualified name
        Symbol *direct_sym = const_cast<CallCodegen *>(this)->symbols().lookup_symbol(name);
        if (direct_sym && direct_sym->kind == SymbolKind::Type && direct_sym->type.is_valid())
        {
            if (direct_sym->type->kind() == TypeKind::Enum)
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "is_enum_type: Found '{}' as enum type via direct lookup", name);
                return true;
            }
        }

        // Use SRM to generate type candidates and check if any is an enum type
        auto &non_const_ctx = const_cast<CodegenContext &>(ctx());
        auto candidates = non_const_ctx.srm().generate_lookup_candidates(name, Cryo::SymbolKind::Type);

        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                  "is_enum_type: Checking '{}' with {} candidates", name, candidates.size());

        for (const auto &candidate : candidates)
        {
            // Check symbol table for enum type
            Symbol *sym = const_cast<CallCodegen *>(this)->symbols().lookup_symbol(candidate);
            if (sym && sym->kind == SymbolKind::Type && sym->type.is_valid())
            {
                if (sym->type->kind() == TypeKind::Enum)
                {
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "is_enum_type: Found '{}' as enum type via symbol table '{}'", name, candidate);
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
            if (sym && sym->kind == SymbolKind::Type && sym->type.is_valid())
            {
                if (sym->type->kind() == TypeKind::Class)
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

        // Handle scope resolution (e.g., Shape::Circle for enum variants)
        if (auto *scope = dynamic_cast<ScopeResolutionNode *>(callee))
        {
            std::string name = scope->scope_name();
            if (scope->has_generic_args())
            {
                name += "<";
                const auto &gargs = scope->generic_args();
                for (size_t i = 0; i < gargs.size(); ++i)
                {
                    if (i > 0)
                        name += ", ";
                    name += gargs[i];
                }
                name += ">";
            }
            return name + "::" + scope->member_name();
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
        TypeRef obj_type = node->object()->get_resolved_type();
        if (obj_type.is_valid())
        {
            type_name = obj_type->display_name();
            // Handle pointer types - get the pointee type name
            if (obj_type->kind() == Cryo::TypeKind::Pointer)
            {
                auto *ptr_type = dynamic_cast<const Cryo::PointerType *>(obj_type.get());
                if (ptr_type && ptr_type->pointee().is_valid())
                {
                    type_name = ptr_type->pointee()->display_name();
                }
            }
            else if (!type_name.empty() && type_name.back() == '*')
            {
                type_name.pop_back();
            }
        }

        // If type_name is still empty, try fallback methods
        if (type_name.empty())
        {
            // Fallback: Check if this is an identifier and look up its type
            if (auto *identifier = dynamic_cast<Cryo::IdentifierNode *>(node->object()))
            {
                std::string var_name = identifier->name();
                auto &var_types = ctx().variable_types_map();
                auto it = var_types.find(var_name);
                if (it != var_types.end() && it->second.is_valid())
                {
                    type_name = it->second->display_name();
                    if (!type_name.empty() && type_name.back() == '*')
                    {
                        type_name.pop_back();
                    }
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "generate_member_receiver_address: Resolved variable '{}' to type '{}'",
                              var_name, type_name);
                }
                else if (var_name == "this")
                {
                    // Fallback to current_type_name if available
                    type_name = ctx().current_type_name();
                }
            }
            // Handle nested member access (e.g., this.current_chunk.next)
            else if (auto *nested_member = dynamic_cast<Cryo::MemberAccessNode *>(node->object()))
            {
                // Recursively resolve the type of the nested member access
                std::string base_type_name;
                if (auto *base_ident = dynamic_cast<Cryo::IdentifierNode *>(nested_member->object()))
                {
                    if (base_ident->name() == "this")
                    {
                        auto &var_types = ctx().variable_types_map();
                        auto it = var_types.find("this");
                        if (it != var_types.end() && it->second.is_valid())
                        {
                            base_type_name = it->second->display_name();
                            if (!base_type_name.empty() && base_type_name.back() == '*')
                            {
                                base_type_name.pop_back();
                            }
                        }
                        else
                        {
                            base_type_name = ctx().current_type_name();
                        }
                    }
                }

                // Look up the nested member's type
                if (!base_type_name.empty())
                {
                    std::string nested_field = nested_member->member();
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "generate_member_receiver_address: Looking up field '{}' in struct '{}'",
                              nested_field, base_type_name);

                    // Use TemplateRegistry to get field type (it stores struct field types)
                    bool found_field = false;
                    if (auto *template_reg = ctx().template_registry())
                    {
                        const TemplateRegistry::StructFieldInfo *field_info = template_reg->get_struct_field_types(base_type_name);
                        if (field_info)
                        {
                            // Find the field in field_names and get its type from field_types
                            for (size_t i = 0; i < field_info->field_names.size(); ++i)
                            {
                                if (field_info->field_names[i] == nested_field && i < field_info->field_types.size())
                                {
                                    TypeRef field_type = field_info->field_types[i];
                                    if (field_type.is_valid())
                                    {
                                        type_name = field_type->display_name();
                                        if (field_type->kind() == Cryo::TypeKind::Pointer)
                                        {
                                            auto *ptr_type = dynamic_cast<const Cryo::PointerType *>(field_type.get());
                                            if (ptr_type && ptr_type->pointee().is_valid())
                                            {
                                                type_name = ptr_type->pointee()->display_name();
                                            }
                                        }
                                        else if (!type_name.empty() && type_name.back() == '*')
                                        {
                                            type_name.pop_back();
                                        }
                                        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                                  "generate_member_receiver_address: Resolved nested member {} in {} to type {} (via TemplateRegistry)",
                                                  nested_field, base_type_name, type_name);
                                        found_field = true;
                                        break;
                                    }
                                }
                            }
                        }
                    }

                    if (!found_field)
                    {
                        // Fallback: try StructType::field_type (in case fields were populated there)
                        TypeRef struct_type_ref = ctx().symbols().lookup_struct_type(base_type_name);
                        if (struct_type_ref.is_valid() && struct_type_ref->kind() == Cryo::TypeKind::Struct)
                        {
                            auto *struct_ty = static_cast<const Cryo::StructType *>(struct_type_ref.get());
                            auto field_type_opt = struct_ty->field_type(nested_field);
                            if (field_type_opt.has_value())
                            {
                                TypeRef field_type = field_type_opt.value();
                                type_name = field_type->display_name();
                                if (field_type->kind() == Cryo::TypeKind::Pointer)
                                {
                                    auto *ptr_type = dynamic_cast<const Cryo::PointerType *>(field_type.get());
                                    if (ptr_type && ptr_type->pointee().is_valid())
                                    {
                                        type_name = ptr_type->pointee()->display_name();
                                    }
                                }
                                else if (!type_name.empty() && type_name.back() == '*')
                                {
                                    type_name.pop_back();
                                }
                                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                          "generate_member_receiver_address: Resolved nested member {} in {} to type {} (via StructType)",
                                          nested_field, base_type_name, type_name);
                                found_field = true;
                            }
                        }
                    }

                    if (!found_field)
                    {
                        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                  "generate_member_receiver_address: field_type not found for '{}' in struct '{}' (checked TemplateRegistry and StructType)",
                                  nested_field, base_type_name);
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
            // Fallback: try TemplateRegistry for cross-module struct field lookup
            if (auto *template_reg = ctx().template_registry())
            {
                std::vector<std::string> candidates = {type_name};

                // Add LLVM struct type name as candidate
                if (struct_type && struct_type->hasName())
                {
                    std::string llvm_name = struct_type->getName().str();
                    if (llvm_name != type_name)
                        candidates.push_back(llvm_name);
                }

                // Add type_namespace_map lookup
                std::string type_ns = ctx().get_type_namespace(type_name);
                if (!type_ns.empty())
                    candidates.push_back(type_ns + "::" + type_name);

                // Try base template name (e.g., "Array" from "Array_Worker")
                size_t underscore_pos = type_name.find('_');
                if (underscore_pos != std::string::npos)
                    candidates.push_back(type_name.substr(0, underscore_pos));

                for (const auto &candidate : candidates)
                {
                    const TemplateRegistry::StructFieldInfo *field_info =
                        template_reg->get_struct_field_types(candidate);
                    if (field_info && !field_info->field_names.empty())
                    {
                        for (size_t i = 0; i < field_info->field_names.size(); ++i)
                        {
                            if (field_info->field_names[i] == member_name)
                            {
                                field_idx = static_cast<int>(i);
                                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                          "generate_member_receiver_address: Found field '{}' at index {} via TemplateRegistry (candidate '{}')",
                                          member_name, field_idx, candidate);
                                break;
                            }
                        }
                        if (field_idx >= 0)
                            break;
                    }
                }
            }

            if (field_idx < 0)
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "generate_member_receiver_address: Field '{}' not found in struct '{}'",
                          member_name, type_name);
                return nullptr;
            }
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
            report_error(ErrorCode::E0636_UNDEFINED_FUNCTION_CALL,
                         "Unknown runtime function '" + unqualified_name + "' - cannot create declaration");
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

    //===================================================================
    // Helper: Parse Type Arguments from String
    //===================================================================

    /**
     * @brief Parse type arguments from a string into TypeRefs
     *
     * Handles nested generics like "HashMap<string, Array<u8>>" by tracking
     * bracket depth. Returns a vector of resolved TypeRefs.
     *
     * @param type_args_str The type arguments string (e.g., "u8" or "string, Array<u8>")
     * @return Vector of resolved TypeRefs (may be empty if resolution fails)
     */
    static std::vector<TypeRef> parse_type_args_from_string(
        const std::string &type_args_str,
        Cryo::SymbolTable &symbols)
    {
        std::vector<TypeRef> type_args;

        if (type_args_str.empty())
        {
            return type_args;
        }

        // Parse comma-separated type arguments, handling nested generics
        std::vector<std::string> arg_names;
        size_t start = 0;
        int depth = 0;

        for (size_t i = 0; i <= type_args_str.length(); ++i)
        {
            if (i == type_args_str.length() || (type_args_str[i] == ',' && depth == 0))
            {
                std::string arg = type_args_str.substr(start, i - start);
                // Trim whitespace
                while (!arg.empty() && std::isspace(static_cast<unsigned char>(arg.front())))
                    arg.erase(0, 1);
                while (!arg.empty() && std::isspace(static_cast<unsigned char>(arg.back())))
                    arg.pop_back();
                if (!arg.empty())
                    arg_names.push_back(arg);
                start = i + 1;
            }
            else if (type_args_str[i] == '<')
                depth++;
            else if (type_args_str[i] == '>')
                depth--;
        }

        // Resolve each type argument to a TypeRef
        for (const auto &arg_name : arg_names)
        {
            TypeRef arg_type{};

            // Count and strip trailing pointer modifiers (e.g., "Expr*" -> "Expr", pointer_depth=1)
            std::string base_name = arg_name;
            int pointer_depth = 0;
            while (!base_name.empty() && base_name.back() == '*')
            {
                base_name.pop_back();
                pointer_depth++;
            }
            // Trim whitespace from base name after stripping pointers
            while (!base_name.empty() && std::isspace(static_cast<unsigned char>(base_name.back())))
                base_name.pop_back();

            // Try looking up in symbol table first
            Symbol *type_sym = symbols.lookup_symbol(base_name);
            if (type_sym && type_sym->kind == SymbolKind::Type && type_sym->type.is_valid())
            {
                arg_type = type_sym->type;
            }

            // Try common primitive types
            if (!arg_type.is_valid())
            {
                if (base_name == "int" || base_name == "i32")
                    arg_type = symbols.arena().get_i32();
                else if (base_name == "i8")
                    arg_type = symbols.arena().get_i8();
                else if (base_name == "i16")
                    arg_type = symbols.arena().get_i16();
                else if (base_name == "i64")
                    arg_type = symbols.arena().get_i64();
                else if (base_name == "u8")
                    arg_type = symbols.arena().get_u8();
                else if (base_name == "u16")
                    arg_type = symbols.arena().get_u16();
                else if (base_name == "u32")
                    arg_type = symbols.arena().get_u32();
                else if (base_name == "u64")
                    arg_type = symbols.arena().get_u64();
                else if (base_name == "f32" || base_name == "float")
                    arg_type = symbols.arena().get_f32();
                else if (base_name == "f64" || base_name == "double")
                    arg_type = symbols.arena().get_f64();
                else if (base_name == "string" || base_name == "String")
                    arg_type = symbols.arena().get_string();
                else if (base_name == "bool" || base_name == "boolean")
                    arg_type = symbols.arena().get_bool();
            }

            // Try looking up by name in TypeArena
            if (!arg_type.is_valid())
            {
                arg_type = symbols.arena().lookup_type_by_name(base_name);
            }

            // Wrap in pointer type(s) if trailing * were present
            if (arg_type.is_valid() && pointer_depth > 0)
            {
                for (int p = 0; p < pointer_depth; ++p)
                {
                    arg_type = symbols.arena().get_pointer_to(arg_type);
                }
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "parse_type_args_from_string: Wrapped '{}' in {} pointer level(s) -> '{}'",
                          base_name, pointer_depth, arg_type->display_name());
            }

            if (arg_type.is_valid())
            {
                type_args.push_back(arg_type);
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "parse_type_args_from_string: Resolved type argument '{}' successfully", arg_name);
            }
            else
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "parse_type_args_from_string: Could not resolve type argument '{}'", arg_name);
            }
        }

        return type_args;
    }

} // namespace Cryo::Codegen
