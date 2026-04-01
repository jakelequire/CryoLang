#include "Codegen/Expressions/CallCodegen.hpp"
#include "Codegen/CodegenVisitor.hpp"
#include "Codegen/Expressions/ExpressionCodegen.hpp"
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
        "strchr", "strrchr", "strstr", "strdup", "substr",
        // I/O operations
        "printf", "println", "print", "eprintln", "eprint",
        "snprintf", "sprintf", "fprintf", "sscanf", "getchar", "putchar", "puts",
        // File I/O
        "fopen", "fclose", "fread", "fwrite", "fseek", "ftell", "fflush", "feof", "ferror",
        "fgets", "fputs", "fgetc", "fputc", "fileno", "fdopen",
        // Low-level file descriptor I/O
        "read", "write", "open", "close", "lseek", "dup", "dup2", "pipe", "fcntl",
        // Filesystem operations
        "stat", "fstat", "lstat", "access", "mkdir", "rmdir", "unlink", "rename",
        "symlink", "readlink", "truncate", "ftruncate", "chmod", "chown",
        "getcwd", "chdir", "opendir", "readdir", "closedir", "dirent_name", "dirent_type",
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
        // Environment
        "getenv", "setenv", "unsetenv", "clearenv", "environ",
        // Integer type conversions (signed)
        "i8_to_i16", "i8_to_i32", "i8_to_i64", "i16_to_i32", "i16_to_i64", "i32_to_i64",
        "i64_to_i32", "i64_to_i16", "i64_to_i8", "i32_to_i16", "i32_to_i8", "i16_to_i8",
        // Integer type conversions (unsigned)
        "u8_to_u16", "u8_to_u32", "u8_to_u64", "u16_to_u32", "u16_to_u64", "u32_to_u64",
        "u64_to_u32", "u64_to_u16", "u64_to_u8", "u32_to_u16", "u32_to_u8", "u16_to_u8",
        // Sign conversions
        "i32_to_u32", "u32_to_i32", "i64_to_u64", "u64_to_i64", "u8_to_i8", "i8_to_u8",
        // Float conversions
        "f32_to_f64", "f64_to_f32",
        // Int to float conversions
        "i32_to_f32", "i32_to_f64", "i64_to_f64", "u32_to_f32", "u32_to_f64", "u64_to_f64",
        // Float to int conversions
        "f32_to_i32", "f64_to_i32", "f64_to_i64", "f32_to_u32", "f64_to_u32", "f64_to_u64",
        // Additional float math
        "truncf", "log10f", "log2f",
        // Float classification
        "isinf", "isfinite", "isnan", "isnormal", "signbit",
        // Special math
        "tgamma", "lgamma",
        // Bit manipulation
        "clz", "clz32", "clz64", "ctz", "ctz32", "ctz64",
        "popcount32", "popcount64",
        "rotl32", "rotl64", "rotr32", "rotr64",
        "bswap16", "bswap32", "bswap64",
        // Variadic argument intrinsics
        "va_arg_i32", "va_arg_i64", "va_arg_u64", "va_arg_f64", "va_arg_ptr",
        // Additional filesystem
        "pread", "pwrite", "fsync", "fdatasync", "link", "realpath",
        // Memory mapping
        "mmap", "munmap",
        // Memory protection
        "mprotect", "mlock", "munlock", "madvise",
        // Dynamic loading
        "dlopen", "dlsym", "dlclose", "dlerror",
        // Misc
        "panic", "todo", "float32_to_string", "float64_to_string", "sizeof"};

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

                    // CRITICAL FIX: generate_member_receiver_address returns a GEP (address of the
                    // field within the struct). If the field itself stores a pointer value (e.g., ASTNode*,
                    // string, etc.), we need to LOAD the pointer from the field before using it as a
                    // method receiver.
                    //
                    // Check the LLVM struct field type to determine if a load is needed.
                    // The GEP gives us ptr-to-field; if the field is ptr type, we need to load it.
                    if (receiver)
                    {
                        bool needs_load = false;

                        // Look up the parent object's LLVM struct type and check the field type
                        std::string parent_type_name;
                        TypeRef parent_cryo_type;

                        // Get the parent object's type name to find the LLVM struct type.
                        // Try AST resolved types first, then fall back to codegen variable_types_map.
                        if (auto *inner_id = dynamic_cast<Cryo::IdentifierNode *>(nested_member->object()))
                        {
                            parent_cryo_type = inner_id->get_resolved_type();
                            // Fall back to variable_types_map (parameters/locals often have types here)
                            if (!parent_cryo_type.is_valid())
                            {
                                auto &var_types = ctx().variable_types_map();
                                auto it = var_types.find(inner_id->name());
                                if (it != var_types.end())
                                    parent_cryo_type = it->second;
                            }
                        }
                        else if (auto *inner_mem = dynamic_cast<Cryo::MemberAccessNode *>(nested_member->object()))
                        {
                            parent_cryo_type = inner_mem->get_resolved_type();
                        }

                        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                  "Nested receiver load check: parent_cryo_type valid={}, kind={}",
                                  parent_cryo_type.is_valid(),
                                  parent_cryo_type.is_valid() ? static_cast<int>(parent_cryo_type->kind()) : -1);

                        // Unwrap pointer to get the base type name
                        if (parent_cryo_type.is_valid() && parent_cryo_type->kind() == Cryo::TypeKind::Pointer)
                        {
                            auto *ptr_t = static_cast<const Cryo::PointerType *>(parent_cryo_type.get());
                            if (ptr_t->pointee().is_valid())
                                parent_type_name = ptr_t->pointee()->display_name();
                        }
                        else if (parent_cryo_type.is_valid())
                        {
                            parent_type_name = parent_cryo_type->display_name();
                        }

                        // Fallback for 'this': use current_type_name from codegen context.
                        // In class methods, 'this' often has no resolved type in the AST
                        // and isn't in the variable_types_map.
                        if (parent_type_name.empty())
                        {
                            if (auto *inner_id = dynamic_cast<Cryo::IdentifierNode *>(nested_member->object()))
                            {
                                if (inner_id->name() == "this" && !ctx().current_type_name().empty())
                                {
                                    parent_type_name = ctx().current_type_name();
                                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                              "Using current_type_name '{}' for 'this' receiver in chained call",
                                              parent_type_name);
                                }
                            }
                        }

                        // Try LLVM struct type lookup to check field type
                        if (!parent_type_name.empty())
                        {
                            llvm::StructType *parent_llvm_type = llvm::StructType::getTypeByName(llvm_ctx(), parent_type_name);
                            if (parent_llvm_type)
                            {
                                // Find the field index by name using the class/struct metadata
                                TypeRef base_type = ctx().symbols().lookup_class_type(parent_type_name);
                                if (!base_type.is_valid())
                                    base_type = ctx().symbols().lookup_struct_type(parent_type_name);

                                if (base_type.is_valid())
                                {
                                    // Use get_struct_field_index which correctly accounts for
                                    // vtable pointers AND inherited fields in the LLVM struct layout.
                                    // cls->field_index() only returns own-field indices, which is
                                    // WRONG for classes with inheritance (the LLVM struct is flat
                                    // with base class fields prepended).
                                    int full_llvm_idx = ctx().get_struct_field_index(parent_type_name, nested_member->member());
                                    if (full_llvm_idx >= 0)
                                    {
                                        size_t llvm_idx = static_cast<size_t>(full_llvm_idx);
                                        if (llvm_idx < parent_llvm_type->getNumElements())
                                        {
                                            llvm::Type *field_llvm_type = parent_llvm_type->getElementType(llvm_idx);
                                            if (field_llvm_type->isPointerTy())
                                            {
                                                needs_load = true;
                                                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                                          "Field '{}' in '{}' at LLVM index {} is pointer type, will load for chained access",
                                                          nested_member->member(), parent_type_name, llvm_idx);
                                            }
                                        }
                                    }
                                    else
                                    {
                                        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                                  "Field '{}' not found in '{}' via get_struct_field_index",
                                                  nested_member->member(), parent_type_name);
                                    }
                                }
                            }
                        }

                        // Fallback: check the AST resolved type
                        if (!needs_load)
                        {
                            TypeRef nested_type = nested_member->get_resolved_type();
                            if (nested_type.is_valid() &&
                                nested_type->kind() == Cryo::TypeKind::Pointer)
                            {
                                // Pointer field (e.g., ctx: CompilationContext*) — load to get the
                                // pointed-to object's address, which is the correct 'this' for methods.
                                needs_load = true;
                            }
                            // NOTE: String fields are NOT loaded here. String is a primitive value type
                            // represented as ptr in LLVM. Methods with &this expect a pointer TO the
                            // string value (char**). The GEP address of the field provides that pointer.
                            // Loading would give the raw char* value, causing methods to crash.
                        }

                        // Counter-check: if LLVM-level check set needs_load but the field is
                        // actually a String type (not a pointer-to-object), don't load.
                        if (needs_load)
                        {
                            TypeRef nested_type = nested_member->get_resolved_type();
                            if (nested_type.is_valid() && nested_type->kind() == Cryo::TypeKind::String)
                            {
                                needs_load = false;
                                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                          "Field '{}' is String type — keeping GEP address as receiver (not loading)",
                                          nested_member->member());
                            }
                        }

                        // Final fallback: if type resolution couldn't determine needs_load,
                        // check the GEP instruction's source struct type directly.
                        // This handles cases where AST types aren't resolved (imported/cross-module).
                        if (!needs_load && receiver)
                        {
                            if (auto *gep = llvm::dyn_cast<llvm::GetElementPtrInst>(receiver))
                            {
                                llvm::Type *src_ty = gep->getSourceElementType();
                                if (auto *src_struct = llvm::dyn_cast<llvm::StructType>(src_ty))
                                {
                                    // The last index in the GEP is the field index
                                    unsigned num_indices = gep->getNumIndices();
                                    if (num_indices >= 2)
                                    {
                                        if (auto *ci = llvm::dyn_cast<llvm::ConstantInt>(gep->getOperand(num_indices)))
                                        {
                                            unsigned field_idx = ci->getZExtValue();
                                            if (field_idx < src_struct->getNumElements())
                                            {
                                                llvm::Type *field_ty = src_struct->getElementType(field_idx);
                                                if (field_ty->isPointerTy())
                                                {
                                                    // Check that the Cryo type isn't String (char*) —
                                                    // string fields should NOT be loaded for &this methods
                                                    TypeRef nested_type = nested_member->get_resolved_type();
                                                    bool is_string = nested_type.is_valid() &&
                                                                     nested_type->kind() == Cryo::TypeKind::String;
                                                    if (!is_string)
                                                    {
                                                        needs_load = true;
                                                        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                                                  "GEP fallback: field '{}' at index {} in struct '{}' is pointer — will load",
                                                                  nested_member->member(), field_idx,
                                                                  src_struct->hasName() ? src_struct->getName().str() : "<anon>");
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        if (needs_load)
                        {
                            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                      "Loading pointer from member field for chained method call: {}",
                                      nested_member->member());
                            receiver = builder().CreateLoad(
                                llvm::PointerType::get(llvm_ctx(), 0), receiver,
                                nested_member->member() + ".ptr.load");
                        }
                    }
                    else
                    {
                        // Fallback: generate_member_receiver_address returns nullptr for
                        // non-struct types (e.g., enum fields accessed via nested member chains
                        // like this.state.phase). Use generate_expression which handles all types.
                        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                  "Nested member receiver address returned nullptr, falling back to generate_expression for: {}",
                                  nested_member->member());
                        receiver = generate_expression(member->object());
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

                // Track whether we need to write back a modified struct value
                // to the original array element after the method call.
                llvm::Value *array_elem_writeback_ptr = nullptr;
                llvm::Type *array_elem_writeback_type = nullptr;
                llvm::AllocaInst *writeback_temp = nullptr;

                // Ensure receiver is a pointer type (methods expect ptr to self)
                if (!receiver->getType()->isPointerTy())
                {
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "Method receiver is not a pointer, creating temporary storage");

                    llvm::Type *receiver_type = receiver->getType();
                    if (receiver_type->isVoidTy())
                    {
                        LOG_ERROR(Cryo::LogComponent::CODEGEN,
                                  "Method receiver has void type (likely from unresolved generic). "
                                  "Using opaque pointer as fallback.");
                        llvm::Type *ptr_type = llvm::PointerType::get(llvm_ctx(), 0);
                        llvm::AllocaInst *temp = create_entry_alloca(ptr_type, "method.receiver.tmp");
                        builder().CreateStore(llvm::ConstantPointerNull::get(llvm::PointerType::get(llvm_ctx(), 0)), temp);
                        receiver = temp;
                    }
                    else
                    {
                        llvm::AllocaInst *temp = create_entry_alloca(receiver_type, "method.receiver.tmp");
                        builder().CreateStore(receiver, temp);

                        // If the receiver was an array element (struct value, not pointer),
                        // set up write-back: after the method call, copy the modified
                        // struct from the temp back to the original array element.
                        if (auto *array_access = dynamic_cast<ArrayAccessNode *>(member->object()))
                        {
                            // The last GEP before the load is the element pointer.
                            // We can recover it by re-generating the index address.
                            // Only do this if the receiver is a struct (non-pointer) type,
                            // meaning the method might modify it in-place.
                            CodegenVisitor *vis = ctx().visitor();
                            if (vis && vis->expressions())
                            {
                                llvm::Value *elem_ptr = vis->expressions()->generate_index_address(array_access);
                                if (elem_ptr)
                                {
                                    array_elem_writeback_ptr = elem_ptr;
                                    array_elem_writeback_type = receiver_type;
                                    writeback_temp = temp;
                                }
                            }
                        }

                        receiver = temp;
                    }
                }

                llvm::Value *result = generate_instance_method(node, member, receiver, member->member());

                // Write back: if the receiver was a struct-typed array element stored
                // in a temp, copy the (potentially modified) value back to the original
                // array element so mutations persist.
                if (array_elem_writeback_ptr && writeback_temp && array_elem_writeback_type)
                {
                    llvm::Value *modified = create_load(writeback_temp, array_elem_writeback_type, "writeback.load");
                    builder().CreateStore(modified, array_elem_writeback_ptr);
                }

                return result;
            }
            report_error(ErrorCode::E0636_UNDEFINED_FUNCTION_CALL, node,
                         "Invalid instance method call");
            return nullptr;
        }

        case CallKind::FreeFunction:
        {
            LOG_DEBUG(Cryo::LogComponent::CODEGEN, "FreeFunction: resolving '{}'", function_name);

            // First, check if this is a call to a generic function template.
            // Generic functions need instantiation with concrete type arguments before they
            // can be called. This must happen BEFORE resolve_function, which might create
            // an incorrect forward declaration for the uninstantiated generic.
            {
                CodegenVisitor *visitor = ctx().visitor();
                GenericCodegen *generics = visitor ? visitor->get_generics() : nullptr;

                if (generics)
                {
                    // Strip namespace prefix to get the base function name
                    std::string base_name = function_name;
                    size_t sep = function_name.rfind("::");
                    if (sep != std::string::npos)
                        base_name = function_name.substr(sep + 2);

                    Cryo::ASTNode *generic_def = generics->get_generic_function_def(base_name);
                    if (generic_def)
                    {
                        auto *func_decl = dynamic_cast<Cryo::FunctionDeclarationNode *>(generic_def);
                        if (func_decl && !func_decl->generic_parameters().empty())
                        {
                            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                      "FreeFunction: '{}' is a generic function template, attempting type inference",
                                      function_name);

                            // Collect generic parameter names in order
                            std::vector<std::string> param_names;
                            for (const auto &gp : func_decl->generic_parameters())
                                param_names.push_back(gp->name());

                            std::unordered_map<std::string, TypeRef> inferred;

                            // Phase 1: Infer from return type context
                            // If the current function's return type is an InstantiatedType (e.g., Result<boolean, string>)
                            // and the generic function's return type annotation uses generic params,
                            // we can match position-wise to infer type arguments.
                            auto *fn_ctx = ctx().current_function();
                            if (fn_ctx && fn_ctx->ast_node)
                            {
                                TypeRef enclosing_return = fn_ctx->ast_node->get_resolved_return_type();
                                auto *ret_ann = func_decl->return_type_annotation();

                                if (enclosing_return.is_valid() &&
                                    enclosing_return->kind() == Cryo::TypeKind::InstantiatedType && ret_ann)
                                {
                                    auto *inst = static_cast<const Cryo::InstantiatedType *>(enclosing_return.get());
                                    const auto &type_args = inst->type_args();

                                    // Extract generic param names from the return type annotation.
                                    // Handle two cases:
                                    // 1. TypeAnnotationKind::Generic - elements contain the type params
                                    // 2. TypeAnnotationKind::Named with embedded generics - parse from name string
                                    std::vector<std::string> ret_type_params;

                                    if (ret_ann->kind == Cryo::TypeAnnotationKind::Generic)
                                    {
                                        for (const auto &elem : ret_ann->elements)
                                            ret_type_params.push_back(elem.name);
                                    }
                                    else if (ret_ann->kind == Cryo::TypeAnnotationKind::Named)
                                    {
                                        // Parse "Result<T, E>" from the annotation name string
                                        const std::string &ann_name = ret_ann->name;
                                        size_t open = ann_name.find('<');
                                        size_t close = ann_name.rfind('>');
                                        if (open != std::string::npos && close != std::string::npos && close > open)
                                        {
                                            std::string args_str = ann_name.substr(open + 1, close - open - 1);
                                            // Split by comma, handling spaces
                                            size_t start = 0;
                                            while (start < args_str.size())
                                            {
                                                size_t comma = args_str.find(',', start);
                                                std::string arg = (comma != std::string::npos)
                                                    ? args_str.substr(start, comma - start)
                                                    : args_str.substr(start);
                                                // Trim whitespace
                                                size_t first = arg.find_first_not_of(" \t");
                                                size_t last = arg.find_last_not_of(" \t");
                                                if (first != std::string::npos)
                                                    ret_type_params.push_back(arg.substr(first, last - first + 1));
                                                start = (comma != std::string::npos) ? comma + 1 : args_str.size();
                                            }
                                        }
                                    }

                                    // Match parsed return type params to InstantiatedType type_args by position
                                    for (size_t i = 0; i < ret_type_params.size() && i < type_args.size(); i++)
                                    {
                                        const std::string &rtp = ret_type_params[i];
                                        for (const auto &pn : param_names)
                                        {
                                            if (rtp == pn && type_args[i].is_valid())
                                            {
                                                inferred[pn] = type_args[i];
                                                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                                          "FreeFunction: Inferred generic param '{}' = '{}' from return type",
                                                          pn, type_args[i]->display_name());
                                            }
                                        }
                                    }
                                }
                                // Fallback: if the enclosing return type is an error (unresolved generic from imported modules),
                                // parse the concrete type args from the enclosing function's return type annotation string.
                                // e.g., annotation "Result<boolean, string>" + generic func returning "Result<T, E>"
                                // → infer T=boolean, E=string
                                else if (enclosing_return.is_valid() && enclosing_return.is_error() && ret_ann)
                                {
                                    auto *enclosing_ret_ann = fn_ctx->ast_node->return_type_annotation();
                                    if (enclosing_ret_ann)
                                    {
                                        // Parse concrete type args from the enclosing function's annotation
                                        std::string enc_ann_str = enclosing_ret_ann->to_string();
                                        size_t enc_open = enc_ann_str.find('<');
                                        size_t enc_close = enc_ann_str.rfind('>');

                                        // Parse the generic function's return annotation params
                                        std::string gen_ret_str = ret_ann->to_string();
                                        size_t gen_open = gen_ret_str.find('<');
                                        size_t gen_close = gen_ret_str.rfind('>');

                                        if (enc_open != std::string::npos && enc_close != std::string::npos &&
                                            gen_open != std::string::npos && gen_close != std::string::npos)
                                        {
                                            // Parse concrete args from enclosing: "boolean, string"
                                            std::string enc_args = enc_ann_str.substr(enc_open + 1, enc_close - enc_open - 1);
                                            std::vector<std::string> concrete_args;
                                            {
                                                size_t s = 0;
                                                int depth = 0;
                                                for (size_t ci = 0; ci <= enc_args.size(); ci++)
                                                {
                                                    if (ci == enc_args.size() || (enc_args[ci] == ',' && depth == 0))
                                                    {
                                                        std::string a = enc_args.substr(s, ci - s);
                                                        size_t f = a.find_first_not_of(" \t");
                                                        size_t l = a.find_last_not_of(" \t");
                                                        if (f != std::string::npos)
                                                            concrete_args.push_back(a.substr(f, l - f + 1));
                                                        s = ci + 1;
                                                    }
                                                    else if (enc_args[ci] == '<') depth++;
                                                    else if (enc_args[ci] == '>') depth--;
                                                }
                                            }

                                            // Parse generic params from the callee: "T, E"
                                            std::string gen_args = gen_ret_str.substr(gen_open + 1, gen_close - gen_open - 1);
                                            std::vector<std::string> generic_ret_params;
                                            {
                                                size_t s = 0;
                                                for (size_t ci = 0; ci <= gen_args.size(); ci++)
                                                {
                                                    if (ci == gen_args.size() || gen_args[ci] == ',')
                                                    {
                                                        std::string a = gen_args.substr(s, ci - s);
                                                        size_t f = a.find_first_not_of(" \t");
                                                        size_t l = a.find_last_not_of(" \t");
                                                        if (f != std::string::npos)
                                                            generic_ret_params.push_back(a.substr(f, l - f + 1));
                                                        s = ci + 1;
                                                    }
                                                }
                                            }

                                            // Match: if generic param name matches a param_name, infer it
                                            for (size_t i = 0; i < generic_ret_params.size() && i < concrete_args.size(); i++)
                                            {
                                                for (const auto &pn : param_names)
                                                {
                                                    if (generic_ret_params[i] == pn && inferred.find(pn) == inferred.end())
                                                    {
                                                        // Resolve the concrete type name to a TypeRef
                                                        TypeRef resolved = symbols().arena().lookup_type_by_name(concrete_args[i]);
                                                        if (!resolved.is_valid())
                                                        {
                                                            // Try common type aliases
                                                            if (concrete_args[i] == "boolean" || concrete_args[i] == "bool")
                                                                resolved = symbols().arena().get_bool();
                                                            else if (concrete_args[i] == "i32" || concrete_args[i] == "int")
                                                                resolved = symbols().arena().get_i32();
                                                            else if (concrete_args[i] == "i64")
                                                                resolved = symbols().arena().get_i64();
                                                            else if (concrete_args[i] == "string")
                                                                resolved = symbols().arena().get_string();
                                                            else if (concrete_args[i] == "u32")
                                                                resolved = symbols().arena().get_u32();
                                                            else if (concrete_args[i] == "u64")
                                                                resolved = symbols().arena().get_u64();
                                                        }
                                                        if (resolved.is_valid())
                                                        {
                                                            inferred[pn] = resolved;
                                                            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                                                      "FreeFunction: Inferred generic param '{}' = '{}' from enclosing annotation",
                                                                      pn, resolved->display_name());
                                                        }
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }

                            // Phase 2: Infer from argument types (for any still-unresolved params)
                            const auto &func_params = func_decl->parameters();
                            const auto &call_args = node->arguments();
                            for (size_t i = 0; i < func_params.size() && i < call_args.size(); i++)
                            {
                                auto *param_ann = func_params[i]->type_annotation();
                                if (!param_ann || param_ann->kind != Cryo::TypeAnnotationKind::Named)
                                    continue;

                                const std::string &ann_name = param_ann->name;
                                if (inferred.find(ann_name) != inferred.end())
                                    continue;

                                bool is_generic_param = false;
                                for (const auto &pn : param_names)
                                {
                                    if (ann_name == pn)
                                    {
                                        is_generic_param = true;
                                        break;
                                    }
                                }
                                if (!is_generic_param)
                                    continue;

                                TypeRef arg_type{};
                                // Try multiple sources for the argument's type
                                if (call_args[i]->has_resolved_type())
                                {
                                    arg_type = call_args[i]->get_resolved_type();
                                }
                                else if (auto *lit = dynamic_cast<Cryo::LiteralNode *>(call_args[i].get()))
                                {
                                    // Infer from literal kind
                                    auto lk = lit->literal_kind();
                                    if (lk == Cryo::TokenKind::TK_KW_TRUE || lk == Cryo::TokenKind::TK_KW_FALSE ||
                                        lk == Cryo::TokenKind::TK_BOOLEAN_LITERAL)
                                        arg_type = symbols().arena().get_bool();
                                    else if (lk == Cryo::TokenKind::TK_NUMERIC_CONSTANT)
                                        arg_type = symbols().arena().get_i32();
                                    else if (lk == Cryo::TokenKind::TK_STRING_LITERAL)
                                        arg_type = symbols().arena().get_string();
                                }
                                else if (auto *id = dynamic_cast<Cryo::IdentifierNode *>(call_args[i].get()))
                                {
                                    // Look up variable type from variable_types_map or symbol table
                                    auto &var_types = ctx().variable_types_map();
                                    auto vt_it = var_types.find(id->name());
                                    if (vt_it != var_types.end() && vt_it->second.is_valid())
                                    {
                                        arg_type = vt_it->second;
                                    }
                                    else
                                    {
                                        Cryo::Symbol *sym = symbols().lookup_symbol(id->name());
                                        if (sym && sym->type.is_valid())
                                            arg_type = sym->type;
                                    }
                                }

                                if (arg_type.is_valid())
                                {
                                    inferred[ann_name] = arg_type;
                                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                              "FreeFunction: Inferred generic param '{}' = '{}' from argument",
                                              ann_name, arg_type->display_name());
                                }
                            }

                            // Build type_args vector in generic parameter order
                            std::vector<TypeRef> type_args;
                            bool all_resolved = true;
                            for (const auto &pn : param_names)
                            {
                                auto it = inferred.find(pn);
                                if (it != inferred.end())
                                {
                                    type_args.push_back(it->second);
                                }
                                else
                                {
                                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                              "FreeFunction: Could not infer generic param '{}'", pn);
                                    all_resolved = false;
                                    break;
                                }
                            }

                            if (all_resolved && !type_args.empty())
                            {
                                llvm::Function *instantiated = generics->instantiate_function(base_name, type_args);
                                if (instantiated)
                                {
                                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                              "FreeFunction: Successfully instantiated generic function '{}' -> '{}'",
                                              function_name, instantiated->getName().str());
                                    return generate_free_function(node, instantiated);
                                }
                            }
                        }
                    }
                }
            }

            // Normal resolution path for non-generic free functions
            llvm::Function *fn = resolve_function(function_name);
            if (!fn)
            {
                // Fallback: if the function name looks like a namespace-qualified static method
                // (e.g., "Foo::Bar::method"), try resolving as a static method call.
                size_t last_sep = function_name.rfind("::");
                if (last_sep != std::string::npos && function_name.find("::") != last_sep)
                {
                    // Has at least two :: separators → likely Namespace::Type::method
                    std::string type_name = function_name.substr(0, last_sep);
                    std::string method_name = function_name.substr(last_sep + 2);
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "FreeFunction: Trying as static method '{}::{}'", type_name, method_name);
                    llvm::Function *method = resolve_method_by_name(type_name, method_name);
                    if (method)
                    {
                        auto args = generate_arguments(node->arguments());
                        std::vector<llvm::Value *> coerced_args;
                        llvm::FunctionType *fn_type = method->getFunctionType();
                        for (size_t i = 0; i < args.size() && i < fn_type->getNumParams(); ++i)
                        {
                            llvm::Value *arg = args[i];
                            llvm::Type *param_type = fn_type->getParamType(i);
                            if (arg && param_type->isPointerTy() && arg->getType()->isStructTy())
                            {
                                llvm::AllocaInst *temp = create_entry_alloca(arg->getType(), "struct.arg.tmp");
                                builder().CreateStore(arg, temp);
                                arg = temp;
                            }
                            // Handle raw [N x T] array → Array<T> struct conversion
                            if (arg && param_type->isStructTy() && arg->getType()->isPointerTy())
                            {
                                llvm::StructType *param_st = llvm::cast<llvm::StructType>(param_type);
                                std::string st_name = param_st->hasName() ? param_st->getName().str() : "";
                                if (st_name.find("Array<") != std::string::npos)
                                {
                                    llvm::AllocaInst *alloca_arg = llvm::dyn_cast<llvm::AllocaInst>(arg);
                                    if (alloca_arg && alloca_arg->getAllocatedType()->isArrayTy())
                                    {
                                        llvm::ArrayType *arr_ty = llvm::cast<llvm::ArrayType>(alloca_arg->getAllocatedType());
                                        uint64_t num_elems = arr_ty->getNumElements();
                                        auto &DL = module()->getDataLayout();
                                        uint64_t elem_size = DL.getTypeAllocSize(arr_ty->getElementType());
                                        uint64_t total_bytes = num_elems * elem_size;

                                        llvm::Function *malloc_fn = module()->getFunction("malloc");
                                        if (!malloc_fn)
                                        {
                                            llvm::FunctionType *malloc_type = llvm::FunctionType::get(
                                                llvm::PointerType::get(llvm_ctx(), 0),
                                                {llvm::Type::getInt64Ty(llvm_ctx())}, false);
                                            malloc_fn = llvm::Function::Create(malloc_type, llvm::Function::ExternalLinkage,
                                                                               "malloc", module());
                                        }
                                        llvm::Value *size_val = llvm::ConstantInt::get(llvm::Type::getInt64Ty(llvm_ctx()), total_bytes);
                                        llvm::Value *heap_ptr = builder().CreateCall(malloc_fn, {size_val}, "array.heap");
                                        builder().CreateMemCpy(heap_ptr, llvm::MaybeAlign(1),
                                                               alloca_arg, llvm::MaybeAlign(1), size_val);

                                        llvm::AllocaInst *wrapper = create_entry_alloca(param_st, "array.wrap");
                                        llvm::Value *f0 = builder().CreateStructGEP(param_st, wrapper, 0, "array.wrap.data");
                                        builder().CreateStore(heap_ptr, f0);
                                        llvm::Value *len_val = llvm::ConstantInt::get(llvm::Type::getInt64Ty(llvm_ctx()), num_elems);
                                        llvm::Value *f1 = builder().CreateStructGEP(param_st, wrapper, 1, "array.wrap.len");
                                        builder().CreateStore(len_val, f1);
                                        llvm::Value *f2 = builder().CreateStructGEP(param_st, wrapper, 2, "array.wrap.cap");
                                        builder().CreateStore(len_val, f2);
                                        arg = create_load(wrapper, param_st, "array.wrapped");
                                    }
                                }
                            }
                            coerced_args.push_back(cast_if_needed(arg, param_type));
                        }
                        std::string result_name = method->getReturnType()->isVoidTy() ? "" : method_name + ".result";
                        return builder().CreateCall(method, coerced_args, result_name);
                    }
                }
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

            // Check for generic function FIRST (before struct),
            // since is_generic_template matches both function and struct templates
            if (generics->get_generic_function_def(base_name))
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "GenericInstantiation: '{}' is a generic function, instantiating",
                          base_name);

                // Parse type arguments — handle pointer types (e.g., "void*")
                std::vector<TypeRef> type_args;
                std::string trimmed = type_args_str;
                size_t fn_start = trimmed.find_first_not_of(" \t");
                size_t fn_end = trimmed.find_last_not_of(" \t");
                if (fn_start != std::string::npos && fn_end != std::string::npos)
                {
                    trimmed = trimmed.substr(fn_start, fn_end - fn_start + 1);
                }

                std::string base_type_name = trimmed;
                int ptr_depth = 0;
                while (!base_type_name.empty() && base_type_name.back() == '*')
                {
                    base_type_name.pop_back();
                    ptr_depth++;
                }
                while (!base_type_name.empty() && std::isspace(static_cast<unsigned char>(base_type_name.back())))
                    base_type_name.pop_back();

                TypeRef arg_type{};
                Symbol *type_sym = symbols().lookup_symbol(base_type_name);
                if (type_sym && type_sym->kind == SymbolKind::Type && type_sym->type.is_valid())
                {
                    arg_type = type_sym->type;
                }
                if (!arg_type.is_valid())
                {
                    if (base_type_name == "int" || base_type_name == "i32")
                        arg_type = symbols().arena().get_i32();
                    else if (base_type_name == "i64")
                        arg_type = symbols().arena().get_i64();
                    else if (base_type_name == "f32" || base_type_name == "float")
                        arg_type = symbols().arena().get_f32();
                    else if (base_type_name == "f64" || base_type_name == "double")
                        arg_type = symbols().arena().get_f64();
                    else if (base_type_name == "string")
                        arg_type = symbols().arena().get_string();
                    else if (base_type_name == "bool" || base_type_name == "boolean")
                        arg_type = symbols().arena().get_bool();
                    else if (base_type_name == "void")
                        arg_type = symbols().arena().get_void();
                }
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

                llvm::Function *instantiated_fn = generics->instantiate_function(base_name, type_args);
                if (instantiated_fn)
                {
                    return generate_free_function(node, instantiated_fn);
                }

                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "GenericInstantiation: Failed to instantiate generic function '{}', trying struct path",
                          base_name);
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

                // Instantiate the generic struct type — record call site for error reporting
                bool had_inst_source = !ctx().instantiation_file().empty();
                if (!had_inst_source && node)
                    ctx().set_instantiation_source(node->source_file(), node->location());
                llvm::StructType *instantiated_type = generics->instantiate_struct(base_name, type_args);
                if (!had_inst_source)
                    ctx().clear_instantiation_source();
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

            // Check for intrinsics — but prefer stdlib versions when available.
            // When the stdlib is linked, functions like println/print/eprintln exist
            // as cross-module declarations in the LLVM module. We prefer those over
            // the compiler-intrinsic versions so that user code goes through the
            // stdlib's richer implementations (e.g., IoResult return types).
            // The stdlib itself still uses intrinsics via the `intrinsics::` namespace.
            if (is_intrinsic(name))
            {
                // These intrinsics have proper stdlib replacements in io::stdio.
                // When stdlib is linked, prefer the stdlib version.
                static const std::unordered_set<std::string> stdlib_overrides = {
                    "println", "print", "eprintln", "eprint",
                };
                if (stdlib_overrides.count(name))
                {
                    llvm::Function *stdlib_fn = resolve_function_by_name(name);
                    if (stdlib_fn)
                    {
                        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                  "classify_call: '{}' is intrinsic but stdlib version exists, preferring FreeFunction",
                                  name);
                        return CallKind::FreeFunction;
                    }
                }
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

                // If the object is the 'intrinsics' namespace, always treat as intrinsic call.
                // This avoids needing to keep _intrinsic_functions perfectly in sync with
                // Intrinsics.cpp. Unimplemented intrinsics get a clear error from
                // report_unimplemented_intrinsic instead of confusing "Unknown function".
                if (obj_name == "intrinsics")
                {
                    return CallKind::Intrinsic;
                }

                // Check if it's an enum variant vs. a static method on an enum
                if (is_enum_type(obj_name))
                {
                    if (is_enum_variant(obj_name, method_name))
                    {
                        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                  "classify_call: '{}::{}' is an enum variant", obj_name, method_name);
                        return CallKind::EnumVariant;
                    }
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "classify_call: '{}::{}' is a static method on enum type", obj_name, method_name);
                    return CallKind::StaticMethod;
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

            // Object is not a simple identifier - check if it's a qualified enum type name
            // e.g., Shapes::Shape in Shapes::Shape::Circle(5)
            {
                std::string obj_qualified = extract_function_name(member->object());
                if (!obj_qualified.empty())
                {
                    if (is_enum_type(obj_qualified))
                    {
                        if (is_enum_variant(obj_qualified, method_name))
                        {
                            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                      "classify_call: Qualified '{}::{}' is an enum variant", obj_qualified, method_name);
                            return CallKind::EnumVariant;
                        }
                        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                  "classify_call: Qualified '{}::{}' is a static method on enum type", obj_qualified, method_name);
                        return CallKind::StaticMethod;
                    }

                    // Also try the last segment (e.g., "Shape" from "Shapes::Shape")
                    size_t last_sep = obj_qualified.rfind("::");
                    if (last_sep != std::string::npos)
                    {
                        std::string simple_name = obj_qualified.substr(last_sep + 2);
                        if (is_enum_type(simple_name))
                        {
                            if (is_enum_variant(simple_name, method_name))
                            {
                                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                          "classify_call: Qualified '{}' -> simple '{}::{}' is an enum variant",
                                          obj_qualified, simple_name, method_name);
                                return CallKind::EnumVariant;
                            }
                            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                      "classify_call: Qualified '{}' -> simple '{}::{}' is a static method on enum type",
                                      obj_qualified, simple_name, method_name);
                            return CallKind::StaticMethod;
                        }
                    }
                }
            }

            // Fallback: likely an instance method
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

            // If the scope is the 'intrinsics' namespace, always treat as intrinsic call.
            if (scope_name == "intrinsics")
            {
                return CallKind::Intrinsic;
            }

            // Check if it's an enum variant vs. a static method on an enum
            // (implement blocks add static methods to enum types)
            if (is_enum_type(scope_name))
            {
                if (is_enum_variant(scope_name, member_name))
                    return CallKind::EnumVariant;
                return CallKind::StaticMethod;
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

            // Check if the scope name is a known type in the symbol table or type arena.
            // This handles imported types whose LLVM struct may not exist yet at classify time
            // (e.g., File::exists where File is imported from another module).
            {
                Symbol *sym = symbols().lookup_symbol(scope_name);
                if (sym && sym->kind == SymbolKind::Type)
                {
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "classify_call: '{}::{}' - scope is a known type (kind={}), treating as StaticMethod",
                              scope_name, member_name,
                              sym->type.is_valid() ? static_cast<int>(sym->type->kind()) : -1);
                    return CallKind::StaticMethod;
                }

                TypeRef type_ref = ctx().symbols().arena().lookup_type_by_name(scope_name);
                if (type_ref.is_valid())
                {
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "classify_call: '{}' found in type arena, treating as StaticMethod", scope_name);
                    return CallKind::StaticMethod;
                }

                if (ctx().is_type_alias(scope_name))
                {
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "classify_call: '{}' is a type alias, treating as StaticMethod", scope_name);
                    return CallKind::StaticMethod;
                }
            }

            // Fallback: scope_name may be namespace-qualified (e.g., "Foo::Bar")
            // Extract the last component and check if it's a type
            size_t last_sep = scope_name.rfind("::");
            if (last_sep != std::string::npos)
            {
                std::string simple_type_name = scope_name.substr(last_sep + 2);
                if (is_enum_type(simple_type_name))
                {
                    if (is_enum_variant(simple_type_name, member_name))
                        return CallKind::EnumVariant;
                    return CallKind::StaticMethod;
                }
                if (is_struct_type(simple_type_name) || is_class_type(simple_type_name))
                    return CallKind::StaticMethod;
                // Also check the symbol table directly (more reliable for imported types
                // whose LLVM struct types may not exist yet)
                Symbol *sym = symbols().lookup_symbol(simple_type_name);
                if (sym && sym->kind == SymbolKind::Type)
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

        // Initialize vtable pointer if this class has virtual methods
        {
            TypeRef cryo_type = ctx().symbols().lookup_class_type(effective_type_name);
            if (cryo_type.is_valid())
            {
                auto *cryo_class = dynamic_cast<const Cryo::ClassType *>(cryo_type.get());
                if (cryo_class && cryo_class->needs_vtable_pointer())
                {
                    std::string vtable_name = "vtable." + effective_type_name;
                    llvm::GlobalVariable *vtable_global = module()->getGlobalVariable(vtable_name, /*AllowInternal=*/true);
                    if (vtable_global)
                    {
                        llvm::Value *vptr_gep = builder().CreateStructGEP(class_type, alloca, 0, "vptr");
                        builder().CreateStore(vtable_global, vptr_gep);
                        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                  "Initialized vtable pointer for class '{}' (stack alloc)", effective_type_name);
                    }
                }
            }
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

            // When inside a type param scope, the Monomorphizer may have partially
            // substituted the type name (e.g., "Maybe_U" where T was substituted but
            // method-level U was not). Apply type annotation substitution to resolve
            // the remaining params (e.g., "Maybe_U" → "Maybe_i32" when U=i32).
            CodegenVisitor *visitor = ctx().visitor();
            GenericCodegen *generics = visitor ? visitor->get_generics() : nullptr;
            if (generics && generics->in_type_param_scope())
            {
                std::string substituted = generics->substitute_type_annotation(instantiated_enum_name);
                if (!substituted.empty() && substituted != instantiated_enum_name)
                {
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "generate_enum_variant: Substituted partially-resolved enum name '{}' -> '{}'",
                              instantiated_enum_name, substituted);
                    instantiated_enum_name = substituted;
                }
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

                                        // If no resolved type on the AST node (e.g., generic function body
                                        // skipped by resolution pass), try variable_types_map which has
                                        // concrete substituted types from instantiate_function
                                        if (!arg_type.is_valid())
                                        {
                                            if (auto *ident = dynamic_cast<Cryo::IdentifierNode *>(arg_nodes[i].get()))
                                            {
                                                auto it = ctx().variable_types_map().find(ident->name());
                                                if (it != ctx().variable_types_map().end())
                                                {
                                                    arg_type = it->second;
                                                }
                                            }
                                        }

                                        // If arg is a struct literal with generic args, try to resolve
                                        // its type from the struct name + substituted generic args
                                        if (!arg_type.is_valid())
                                        {
                                            if (auto *struct_lit = dynamic_cast<Cryo::StructLiteralNode *>(arg_nodes[i].get()))
                                            {
                                                if (!struct_lit->generic_args().empty() && generics)
                                                {
                                                    // Build type args from the struct literal's generic args
                                                    std::vector<TypeRef> struct_type_args;
                                                    auto &arena2 = ctx().symbols().arena();
                                                    bool all_resolved = true;
                                                    for (const auto &ga : struct_lit->generic_args())
                                                    {
                                                        std::string resolved_ga = ga;
                                                        if (generics->in_type_param_scope())
                                                        {
                                                            std::string sub = generics->substitute_type_annotation(ga);
                                                            if (!sub.empty() && sub != ga)
                                                                resolved_ga = sub;
                                                        }
                                                        TypeRef ta = arena2.lookup_type_by_name(resolved_ga);
                                                        if (!ta.is_valid() && resolved_ga.size() > 2 &&
                                                            resolved_ga.substr(resolved_ga.size() - 2) == "[]")
                                                        {
                                                            TypeRef base = arena2.lookup_type_by_name(
                                                                resolved_ga.substr(0, resolved_ga.size() - 2));
                                                            if (base.is_valid())
                                                                ta = arena2.get_array_of(base);
                                                        }
                                                        if (!ta.is_valid() && resolved_ga.size() > 1 &&
                                                            resolved_ga.back() == '*')
                                                        {
                                                            TypeRef base = arena2.lookup_type_by_name(
                                                                resolved_ga.substr(0, resolved_ga.size() - 1));
                                                            if (base.is_valid())
                                                                ta = arena2.get_pointer_to(base);
                                                        }
                                                        if (ta.is_valid())
                                                            struct_type_args.push_back(ta);
                                                        else
                                                            all_resolved = false;
                                                    }
                                                    if (all_resolved && !struct_type_args.empty())
                                                    {
                                                        // Create the instantiated struct type
                                                        std::string mangled_struct = generics->mangle_type_name(
                                                            struct_lit->struct_type(), struct_type_args);
                                                        arg_type = arena2.lookup_type_by_name(mangled_struct);
                                                        if (!arg_type.is_valid())
                                                        {
                                                            // Try on-demand instantiation
                                                            llvm::Type *inst = generics->get_instantiated_type(
                                                                struct_lit->struct_type(), struct_type_args);
                                                            if (inst)
                                                                arg_type = arena2.lookup_type_by_name(mangled_struct);
                                                        }
                                                        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                                                  "generate_enum_variant: Inferred struct literal type '{}' -> {}",
                                                                  mangled_struct, arg_type.is_valid() ? arg_type->display_name() : "<invalid>");
                                                    }
                                                }
                                            }
                                        }

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
                                            // If it's a bare struct/class that's a generic template,
                                            // substitute type params to get the concrete instantiation.
                                            // Sema may resolve "HashMapPair<K,V>" to bare StructType("HashMapPair").
                                            else if (arg_type->kind() == TypeKind::Struct ||
                                                     arg_type->kind() == TypeKind::Class)
                                            {
                                                TypeRef substituted = generics->substitute_type_params(arg_type);
                                                if (substituted.is_valid() && substituted != arg_type)
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

                        // Last resort: if still not inferred, check the current function's
                        // return type. If it's the same enum type (e.g., return type is
                        // Option_u32 and we're constructing Option::Some), use it directly.
                        if (!all_inferred)
                        {
                            llvm::Function *current_fn = builder().GetInsertBlock()
                                                             ? builder().GetInsertBlock()->getParent()
                                                             : nullptr;
                            if (current_fn)
                            {
                                llvm::Type *ret_ty = current_fn->getReturnType();
                                if (ret_ty && ret_ty->isStructTy())
                                {
                                    auto *ret_st = llvm::cast<llvm::StructType>(ret_ty);
                                    if (ret_st->hasName())
                                    {
                                        std::string ret_name = ret_st->getName().str();
                                        // Check if the return type starts with the enum name
                                        // (e.g., "Option_u32" starts with "Option")
                                        // Also handle short enum name (strip namespace)
                                        std::string short_enum = resolved_enum_name;
                                        size_t ns_pos = short_enum.rfind("::");
                                        if (ns_pos != std::string::npos)
                                            short_enum = short_enum.substr(ns_pos + 2);

                                        if ((ret_name.size() > resolved_enum_name.size() &&
                                             ret_name.substr(0, resolved_enum_name.size()) == resolved_enum_name &&
                                             ret_name[resolved_enum_name.size()] == '_') ||
                                            (ret_name.size() > short_enum.size() &&
                                             ret_name.substr(0, short_enum.size()) == short_enum &&
                                             ret_name[short_enum.size()] == '_'))
                                        {
                                            instantiated_enum_name = ret_name;
                                            all_inferred = true;
                                            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                                      "generate_enum_variant: Inferred instantiation '{}' from function return type",
                                                      instantiated_enum_name);
                                        }
                                    }
                                }
                            }
                        }

                        if (all_inferred)
                        {
                            if (instantiated_enum_name == resolved_enum_name)
                                instantiated_enum_name = generics->mangle_type_name(resolved_enum_name, inferred_type_args);
                            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                      "generate_enum_variant: Inferred instantiation '{}' for {}::{}",
                                      instantiated_enum_name, resolved_enum_name, variant_name);

                            // Ensure the enum type is instantiated
                            if (!generics->has_type_instantiation(instantiated_enum_name))
                            {
                                // Only try to instantiate if we have valid type args
                                bool has_valid_args = true;
                                for (const auto &ta : inferred_type_args)
                                {
                                    if (!ta.is_valid()) { has_valid_args = false; break; }
                                }
                                if (has_valid_args)
                                {
                                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                              "generate_enum_variant: Instantiating enum '{}'",
                                              instantiated_enum_name);
                                    bool had_inst_source = !ctx().instantiation_file().empty();
                                    if (!had_inst_source && node)
                                        ctx().set_instantiation_source(node->source_file(), node->location());
                                    generics->instantiate_enum(resolved_enum_name, inferred_type_args);
                                    if (!had_inst_source)
                                        ctx().clear_instantiation_source();
                                }
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

        // Sanitize instantiated_enum_name to match the mangling used by mangle_type_name.
        // display_name() may contain '*' (e.g., "Outcome_void*_i32") but the defined
        // constructor functions use sanitized names (e.g., "Outcome_voidp_i32::Ok").
        std::replace(instantiated_enum_name.begin(), instantiated_enum_name.end(), '*', 'p');

        // Use instantiated name for qualified variant lookup
        std::string qualified_variant = instantiated_enum_name + "::" + variant_name;
        // Also try with base name for non-generic enums
        std::string base_qualified_variant = resolved_enum_name + "::" + variant_name;

        // Generate arguments
        auto args = generate_arguments(node->arguments());

        // Look for variant constructor function (for complex variants with payloads)
        // Only accept functions with a DEFINITION (not mere declarations), because
        // cross-module stubs or previous invocations may have created a `declare`
        // that will never be defined in the final binary.
        llvm::Function *ctor = module()->getFunction(qualified_variant);
        if (ctor && ctor->isDeclaration()) ctor = nullptr;
        if (!ctor && instantiated_enum_name != resolved_enum_name)
        {
            // Fallback to base name (e.g., Option::Some)
            ctor = module()->getFunction(base_qualified_variant);
            if (ctor && ctor->isDeclaration()) ctor = nullptr;
        }
        if (!ctor)
        {
            // Fallback: strip namespace prefix (e.g., "Shapes::Shape::Circle" -> "Shape::Circle")
            size_t last_sep = resolved_enum_name.rfind("::");
            if (last_sep != std::string::npos)
            {
                std::string short_name = resolved_enum_name.substr(last_sep + 2) + "::" + variant_name;
                ctor = module()->getFunction(short_name);
                if (ctor && ctor->isDeclaration()) ctor = nullptr;
            }
        }
        if (!ctor)
        {
            // Fallback: the constructor may exist with a fully-qualified namespace prefix
            // (e.g., "std::core::result::Result_i8_ConversionError::Ok" when we searched
            // for "Result_i8_ConversionError::Ok"). Search all functions for a matching suffix.
            std::string suffix = "::" + qualified_variant;
            for (auto &fn : module()->functions())
            {
                if (fn.isDeclaration()) continue;
                std::string fn_name = fn.getName().str();
                if (fn_name.size() > suffix.size() &&
                    fn_name.compare(fn_name.size() - suffix.size(), suffix.size(), suffix) == 0)
                {
                    ctor = &fn;
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "generate_enum_variant: Found constructor via suffix match: {}", fn_name);
                    break;
                }
            }
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

        // Look up the registered variant constant (discriminant value)
        auto &enum_variants = ctx().enum_variants_map();
        auto it = enum_variants.find(qualified_variant);
        if (it == enum_variants.end() && instantiated_enum_name != resolved_enum_name)
        {
            // Try base name
            it = enum_variants.find(base_qualified_variant);
        }

        // Try with namespace prefix if direct lookup failed
        if (it == enum_variants.end())
        {
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
            }
        }

        // If variant has payload arguments, inline-construct the enum struct
        // (the constructor function wasn't found, but we have the discriminant and args)
        if (!args.empty() && it != enum_variants.end())
        {
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "generate_enum_variant: Variant '{}' has {} payload args, inline constructing",
                      it->first, args.size());

            llvm::Value *disc_const = it->second;

            // Find the enum struct type
            llvm::Type *inline_enum_type = nullptr;
            if (resolved_type_ref.is_valid())
                inline_enum_type = types().map(resolved_type_ref);
            if (!inline_enum_type)
                inline_enum_type = types().get_type(instantiated_enum_name);
            if (!inline_enum_type && instantiated_enum_name != resolved_enum_name)
                inline_enum_type = types().get_type(resolved_enum_name);
            // Fallback: use the current function's return type (common for return statements)
            if (!inline_enum_type)
            {
                llvm::Function *fn = builder().GetInsertBlock()
                    ? builder().GetInsertBlock()->getParent() : nullptr;
                if (fn && fn->getReturnType()->isStructTy())
                    inline_enum_type = fn->getReturnType();
            }

            if (inline_enum_type && inline_enum_type->isStructTy())
            {
                auto *struct_ty = llvm::cast<llvm::StructType>(inline_enum_type);

                // Allocate the enum struct
                llvm::Value *enum_alloca = create_entry_alloca(struct_ty, variant_name + ".ctor");

                // Store discriminant
                llvm::Value *disc_ptr = builder().CreateStructGEP(struct_ty, enum_alloca, 0, "disc_ptr");
                llvm::Value *disc_val = disc_const;
                if (disc_val->getType() != struct_ty->getElementType(0))
                    disc_val = builder().CreateIntCast(disc_val, struct_ty->getElementType(0), false, "disc.cast");
                builder().CreateStore(disc_val, disc_ptr);

                // Store payload fields
                if (struct_ty->getNumElements() > 1)
                {
                    llvm::Value *payload_ptr = builder().CreateStructGEP(struct_ty, enum_alloca, 1, "payload_ptr");
                    size_t byte_offset = 0;
                    for (size_t i = 0; i < args.size(); i++)
                    {
                        if (!args[i])
                        {
                            LOG_WARN(Cryo::LogComponent::CODEGEN,
                                     "generate_enum_variant: Skipping null payload arg {} for '{}'",
                                     i, qualified_variant);
                            continue;
                        }
                        llvm::Value *field_ptr = builder().CreateConstGEP1_32(
                            llvm::Type::getInt8Ty(llvm_ctx()), payload_ptr, byte_offset, "field_ptr");
                        builder().CreateStore(args[i], field_ptr);

                        llvm::Type *arg_type = args[i]->getType();
                        if (arg_type->isSized())
                            byte_offset += module()->getDataLayout().getTypeAllocSize(arg_type);
                        else
                            byte_offset += 8;
                    }
                }

                return builder().CreateLoad(struct_ty, enum_alloca, variant_name + ".val");
            }
            else
            {
                LOG_WARN(Cryo::LogComponent::CODEGEN,
                         "generate_enum_variant: Could not find struct type for inline construction of '{}'",
                         qualified_variant);
            }
        }

        // For simple enum variants (no payload), return the registered constant
        if (it != enum_variants.end() && args.empty())
        {
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "Found simple enum variant: {} -> constant", it->first);
            return it->second;
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

            // If name-based lookup also failed, try inferring from the current function's
            // return type. This handles cases where type inference produced a mangled name
            // that doesn't match the actual registered enum type name.
            if (!enum_type)
            {
                llvm::Function *current_fn = builder().GetInsertBlock()
                                                 ? builder().GetInsertBlock()->getParent()
                                                 : nullptr;
                if (current_fn)
                {
                    llvm::Type *ret_ty = current_fn->getReturnType();
                    if (ret_ty && ret_ty->isStructTy())
                    {
                        auto *ret_st = llvm::cast<llvm::StructType>(ret_ty);
                        if (ret_st->hasName())
                        {
                            std::string ret_name = ret_st->getName().str();
                            std::string short_enum = resolved_enum_name;
                            size_t ns_pos = short_enum.rfind("::");
                            if (ns_pos != std::string::npos)
                                short_enum = short_enum.substr(ns_pos + 2);

                            if ((ret_name.size() > resolved_enum_name.size() &&
                                 ret_name.substr(0, resolved_enum_name.size()) == resolved_enum_name &&
                                 ret_name[resolved_enum_name.size()] == '_') ||
                                (ret_name.size() > short_enum.size() &&
                                 ret_name.substr(0, short_enum.size()) == short_enum &&
                                 ret_name[short_enum.size()] == '_'))
                            {
                                enum_type = ret_st;
                                instantiated_enum_name = ret_name;
                                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                          "generate_enum_variant: Inferred enum type '{}' from function return type",
                                          ret_name);
                            }
                        }
                    }
                }
            }
        }

        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                  "generate_enum_variant: Enum type lookup for '{}': {}",
                  instantiated_enum_name, enum_type ? "found" : "not found");

        // For enum variant calls with payloads where the constructor function and
        // discriminant weren't found, try inline-constructing with a computed index.
        if (!args.empty())
        {
            // Determine the discriminant by looking up the variant name in the
            // generic enum template.  For Result<T,E>: Ok=0, Err=1.
            int32_t computed_disc = -1;
            if (auto *template_reg = ctx().template_registry())
            {
                // Try the resolved (base) enum name first
                auto *tmpl_info = template_reg->find_template(resolved_enum_name);
                if (!tmpl_info)
                {
                    // Try stripping namespace prefix
                    size_t sep = resolved_enum_name.rfind("::");
                    if (sep != std::string::npos)
                        tmpl_info = template_reg->find_template(resolved_enum_name.substr(sep + 2));
                }
                if (tmpl_info && tmpl_info->enum_template)
                {
                    int32_t idx = 0;
                    for (const auto &v : tmpl_info->enum_template->variants())
                    {
                        if (v->name() == variant_name)
                        {
                            computed_disc = idx;
                            break;
                        }
                        idx++;
                    }
                }
            }

            // If we have the enum type and discriminant, inline-construct
            if (computed_disc >= 0 && enum_type && enum_type->isStructTy())
            {
                auto *struct_ty = llvm::cast<llvm::StructType>(enum_type);
                llvm::Value *enum_alloca = create_entry_alloca(struct_ty, variant_name + ".ctor");

                // Store discriminant
                llvm::Value *disc_ptr = builder().CreateStructGEP(struct_ty, enum_alloca, 0, "disc_ptr");
                llvm::Value *disc_val = llvm::ConstantInt::get(struct_ty->getElementType(0), computed_disc);
                builder().CreateStore(disc_val, disc_ptr);

                // Store payload fields
                if (struct_ty->getNumElements() > 1)
                {
                    llvm::Value *payload_ptr = builder().CreateStructGEP(struct_ty, enum_alloca, 1, "payload_ptr");
                    size_t byte_offset = 0;
                    for (size_t i = 0; i < args.size(); ++i)
                    {
                        if (!args[i]) continue;
                        llvm::Value *field_ptr = builder().CreateConstGEP1_32(
                            llvm::Type::getInt8Ty(llvm_ctx()), payload_ptr, byte_offset, "field_ptr");
                        builder().CreateStore(args[i], field_ptr);
                        llvm::Type *arg_type = args[i]->getType();
                        if (arg_type->isSized())
                            byte_offset += module()->getDataLayout().getTypeAllocSize(arg_type);
                        else
                            byte_offset += 8;
                    }
                }

                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "generate_enum_variant: Inline-constructed '{}' via template lookup (disc={})",
                          qualified_variant, computed_disc);

                return builder().CreateLoad(struct_ty, enum_alloca, variant_name + ".val");
            }

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

            // Determine the variant's discriminant index.  We try to look it
            // up from the generic (non-instantiated) enum variant registry first,
            // then from the generic enum declaration's variant ordering.
            int32_t disc_index = -1;
            {
                // Try looking up the discriminant from registered variants using
                // various name patterns
                auto &ev = ctx().enum_variants_map();
                std::vector<std::string> disc_keys = {
                    qualified_variant,
                    base_qualified_variant,
                    resolved_enum_name + "::" + variant_name,
                };
                for (const auto &key : disc_keys)
                {
                    auto disc_it = ev.find(key);
                    if (disc_it != ev.end())
                    {
                        if (auto *ci = llvm::dyn_cast<llvm::ConstantInt>(disc_it->second))
                            disc_index = static_cast<int32_t>(ci->getZExtValue());
                        break;
                    }
                }
                // Fallback: search for any key ending with "::<variant_name>"
                // in the same base enum
                if (disc_index < 0)
                {
                    std::string suffix = "::" + variant_name;
                    for (auto &[k, v] : ev)
                    {
                        if (k.size() > suffix.size() && k.ends_with(suffix))
                        {
                            if (auto *ci = llvm::dyn_cast<llvm::ConstantInt>(v))
                                disc_index = static_cast<int32_t>(ci->getZExtValue());
                            break;
                        }
                    }
                }
            }

            if (disc_index >= 0 && enum_type->isStructTy())
            {
                // Inline-construct the enum value directly at the call site.
                // This avoids needing a separate constructor function that may
                // not be generated in the stdlib.
                auto *struct_ty = llvm::cast<llvm::StructType>(enum_type);
                llvm::Value *enum_alloca = create_entry_alloca(struct_ty, variant_name + ".ctor");

                // Store discriminant
                llvm::Value *disc_ptr = builder().CreateStructGEP(struct_ty, enum_alloca, 0, "disc_ptr");
                llvm::Value *disc_val = llvm::ConstantInt::get(struct_ty->getElementType(0), disc_index);
                builder().CreateStore(disc_val, disc_ptr);

                // Store payload fields
                if (struct_ty->getNumElements() > 1)
                {
                    llvm::Value *payload_ptr = builder().CreateStructGEP(struct_ty, enum_alloca, 1, "payload_ptr");
                    size_t byte_offset = 0;
                    for (size_t i = 0; i < args.size(); ++i)
                    {
                        if (!args[i]) continue;
                        llvm::Value *field_ptr = builder().CreateConstGEP1_32(
                            llvm::Type::getInt8Ty(llvm_ctx()), payload_ptr, byte_offset, "field_ptr");
                        builder().CreateStore(args[i], field_ptr);
                        llvm::Type *arg_type = args[i]->getType();
                        if (arg_type->isSized())
                            byte_offset += module()->getDataLayout().getTypeAllocSize(arg_type);
                        else
                            byte_offset += 8;
                    }
                }

                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "generate_enum_variant: Inline-constructed '{}' (disc={})",
                          qualified_variant, disc_index);

                return builder().CreateLoad(struct_ty, enum_alloca, variant_name + ".val");
            }

            // Last resort: create extern declaration and hope it's defined elsewhere
            llvm::FunctionType *ctor_type = llvm::FunctionType::get(enum_type, param_types, false);
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

        // Check if any argument is a va_list parameter (variadic forwarding)
        int va_list_arg_index = -1;
        for (size_t i = 0; i < node->arguments().size(); ++i)
        {
            if (node->arguments()[i]->kind() == Cryo::NodeKind::Identifier)
            {
                auto *id_node = static_cast<Cryo::IdentifierNode *>(node->arguments()[i].get());
                if (ctx().is_va_list_param(id_node->name()))
                {
                    va_list_arg_index = static_cast<int>(i);
                    break;
                }
            }
        }

        if (va_list_arg_index >= 0)
        {
            return generate_va_forwarding_intrinsic(node, intrinsic_name, args, va_list_arg_index);
        }

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

            // args already generated at the top of generate_intrinsic — do NOT
            // call generate_arguments again, as it would double-evaluate
            // side-effectful expressions (e.g., it.next()).

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

        if (intrinsic_name == "todo")
        {
            // User writes: todo("message")
            // Compiler injects file and line: todo(msg, file, line)
            LOG_DEBUG(Cryo::LogComponent::CODEGEN, "Generating todo intrinsic with injected file/line");
            std::string file = node->source_file();
            size_t line = node->location().line();
            llvm::Value *file_val = builder().CreateGlobalStringPtr(file, "todo.file", 0, module());
            llvm::Value *line_val = llvm::ConstantInt::get(llvm::Type::getInt32Ty(llvm_ctx()), line);
            args.push_back(file_val);
            args.push_back(line_val);
            Intrinsics intrinsic_gen(ctx().llvm_manager(), ctx().diagnostics());
            return intrinsic_gen.generate_intrinsic_call(node, "todo", args);
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

                // Handle raw [N x T] array → Array<T> struct conversion
                if (arg && param_type->isStructTy() && arg->getType()->isPointerTy())
                {
                    llvm::StructType *param_st = llvm::cast<llvm::StructType>(param_type);
                    std::string st_name = param_st->hasName() ? param_st->getName().str() : "";
                    if (st_name.find("Array<") != std::string::npos)
                    {
                        llvm::AllocaInst *alloca_arg = llvm::dyn_cast<llvm::AllocaInst>(arg);
                        if (alloca_arg && alloca_arg->getAllocatedType()->isArrayTy())
                        {
                            llvm::ArrayType *arr_ty = llvm::cast<llvm::ArrayType>(alloca_arg->getAllocatedType());
                            uint64_t num_elems = arr_ty->getNumElements();
                            auto &DL = module()->getDataLayout();
                            uint64_t elem_size = DL.getTypeAllocSize(arr_ty->getElementType());
                            uint64_t total_bytes = num_elems * elem_size;

                            llvm::Function *malloc_fn = module()->getFunction("malloc");
                            if (!malloc_fn)
                            {
                                llvm::FunctionType *malloc_type = llvm::FunctionType::get(
                                    llvm::PointerType::get(llvm_ctx(), 0),
                                    {llvm::Type::getInt64Ty(llvm_ctx())}, false);
                                malloc_fn = llvm::Function::Create(malloc_type, llvm::Function::ExternalLinkage,
                                                                   "malloc", module());
                            }
                            llvm::Value *size_val = llvm::ConstantInt::get(llvm::Type::getInt64Ty(llvm_ctx()), total_bytes);
                            llvm::Value *heap_ptr = builder().CreateCall(malloc_fn, {size_val}, "array.heap");
                            builder().CreateMemCpy(heap_ptr, llvm::MaybeAlign(1),
                                                   alloca_arg, llvm::MaybeAlign(1), size_val);

                            llvm::AllocaInst *wrapper = create_entry_alloca(param_st, "array.wrap");
                            llvm::Value *f0 = builder().CreateStructGEP(param_st, wrapper, 0, "array.wrap.data");
                            builder().CreateStore(heap_ptr, f0);
                            llvm::Value *len_val = llvm::ConstantInt::get(llvm::Type::getInt64Ty(llvm_ctx()), num_elems);
                            llvm::Value *f1 = builder().CreateStructGEP(param_st, wrapper, 1, "array.wrap.len");
                            builder().CreateStore(len_val, f1);
                            llvm::Value *f2 = builder().CreateStructGEP(param_st, wrapper, 2, "array.wrap.cap");
                            builder().CreateStore(len_val, f2);
                            arg = create_load(wrapper, param_st, "array.wrapped");
                        }
                    }
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

                // Only use the resolved type to redirect type_name when the generic base
                // matches the call's receiver type. The resolved type is the *return type*,
                // which may be a completely different generic (e.g., File::open returns
                // IoResult<File> — we must not redirect "File" to "IoResult_File").
                std::string base_name;
                if (inst_type->generic_base().is_valid())
                    base_name = inst_type->generic_base()->display_name();

                // Extract base from type_name (e.g., "Stack" from "Stack<int>")
                std::string receiver_base = type_name;
                size_t angle = receiver_base.find('<');
                if (angle != std::string::npos)
                    receiver_base = receiver_base.substr(0, angle);

                bool base_matches = !base_name.empty() &&
                    (base_name == type_name || base_name == receiver_base);

                if (base_matches && inst_type->has_resolved_type())
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
                else if (base_matches && generics && generics->in_type_param_scope())
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
                // Only override if the resolved struct type name relates to the receiver.
                // The resolved type is the return type, which may differ from the receiver
                // (e.g., File::open returns IoResult<File>, not File).
                std::string concrete_name = call_resolved_type->display_name();
                // Extract base name from type_name (e.g., "Stack" from "Stack<int>")
                std::string base = type_name;
                size_t angle = base.find('<');
                if (angle != std::string::npos)
                    base = base.substr(0, angle);
                if (concrete_name == type_name || concrete_name == base ||
                    concrete_name.find(base + "_") == 0)
                {
                    resolved_type_name = concrete_name;
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "generate_static_method: Using concrete resolved type: {}",
                              resolved_type_name);
                }
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

        // Fallback: If the type_name contains generic angle brackets and we have
        // active generic bindings, substitute type params to get the concrete mangled name.
        // This handles static calls like Inner<T>::new(v) inside Outer<i32>::new
        // where the type name hasn't been fully resolved through other paths.
        if (generics && generics->in_type_param_scope() && type_name.find('<') != std::string::npos)
        {
            std::string substituted = generics->substitute_type_annotation(type_name);
            if (!substituted.empty())
            {
                resolved_type_name = substituted;
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "generate_static_method: Substituted nested generic type '{}' -> '{}'",
                          type_name, resolved_type_name);
            }
        }

        // Generate arguments
        auto args = generate_arguments(node->arguments());

        // 1. First try: resolve the static method directly
        llvm::Function *method = resolve_method(resolved_type_name, method_name);

        // Check for overloaded methods: if the resolved method's parameter count doesn't
        // match the argument count, try finding an overloaded variant with a mangled name
        // that includes parameter types (e.g., "Buffer::new(i32)" instead of "Buffer::new")
        if (method && method->getFunctionType()->getNumParams() != args.size())
        {
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "generate_static_method: Parameter count mismatch (method has {}, call has {}), "
                      "trying overload resolution for {}::{}",
                      method->getFunctionType()->getNumParams(), args.size(),
                      resolved_type_name, method_name);

            // Build overloaded name with argument types.
            // LLVM doesn't distinguish signed/unsigned integers, so we build
            // multiple suffix variants: one with signed names (i8/i16/i32/i64)
            // and one with unsigned names (u8/u16/u32/u64), since Cryo's
            // overloaded method names use the Cryo type system names.
            struct ArgTypeNames { std::string signed_name; std::string unsigned_name; };
            std::vector<ArgTypeNames> arg_type_names;
            for (size_t i = 0; i < args.size(); ++i)
            {
                ArgTypeNames names;
                if (args[i])
                {
                    llvm::Type *arg_type = args[i]->getType();
                    if (arg_type->isIntegerTy(1))
                    {
                        names.signed_name = "bool";
                        names.unsigned_name = "boolean";
                    }
                    else if (arg_type->isIntegerTy(8))
                    {
                        names.signed_name = "i8";
                        names.unsigned_name = "u8";
                    }
                    else if (arg_type->isIntegerTy(16))
                    {
                        names.signed_name = "i16";
                        names.unsigned_name = "u16";
                    }
                    else if (arg_type->isIntegerTy(32))
                    {
                        names.signed_name = "i32";
                        names.unsigned_name = "u32";
                    }
                    else if (arg_type->isIntegerTy(64))
                    {
                        names.signed_name = "i64";
                        names.unsigned_name = "u64";
                    }
                    else if (arg_type->isFloatTy())
                    {
                        names.signed_name = "f32";
                        names.unsigned_name = "f32";
                    }
                    else if (arg_type->isDoubleTy())
                    {
                        names.signed_name = "f64";
                        names.unsigned_name = "f64";
                    }
                    else if (arg_type->isPointerTy())
                    {
                        names.signed_name = "ptr";
                        names.unsigned_name = "ptr";
                    }
                    else
                    {
                        std::string type_str;
                        llvm::raw_string_ostream rso(type_str);
                        arg_type->print(rso);
                        names.signed_name = type_str;
                        names.unsigned_name = type_str;
                    }
                }
                arg_type_names.push_back(names);
            }

            // Generate all suffix permutations (signed/unsigned for each arg).
            // For a single arg, try both "(i64)" and "(u64)".
            // For multiple args, generate all 2^N combinations.
            std::vector<std::string> overload_suffixes;
            overload_suffixes.push_back(""); // seed with empty string
            for (size_t i = 0; i < arg_type_names.size(); ++i)
            {
                std::vector<std::string> new_suffixes;
                for (const auto &prev : overload_suffixes)
                {
                    std::string sep = prev.empty() ? "" : ",";
                    new_suffixes.push_back(prev + sep + arg_type_names[i].signed_name);
                    if (arg_type_names[i].unsigned_name != arg_type_names[i].signed_name)
                        new_suffixes.push_back(prev + sep + arg_type_names[i].unsigned_name);
                }
                overload_suffixes = std::move(new_suffixes);
            }

            // Try finding the overloaded function with various qualified names and suffix variants
            auto type_candidates = generate_lookup_candidates(resolved_type_name, Cryo::SymbolKind::Type);
            for (const auto &suffix : overload_suffixes)
            {
                std::string overload_suffix = "(" + suffix + ")";
                for (const auto &type_candidate : type_candidates)
                {
                    std::string overloaded_name = type_candidate + "::" + method_name + overload_suffix;
                    if (llvm::Function *overloaded_fn = module()->getFunction(overloaded_name))
                    {
                        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                  "generate_static_method: Found overloaded method: {}", overloaded_name);
                        method = overloaded_fn;
                        break;
                    }
                }
                if (method->getFunctionType()->getNumParams() == args.size())
                    break;
            }

            // Broader fallback: scan module for functions matching Type::method(...)
            // with the correct parameter count (handles type name mismatches like int vs i32)
            if (method->getFunctionType()->getNumParams() != args.size())
            {
                for (const auto &type_candidate : type_candidates)
                {
                    std::string prefix = type_candidate + "::" + method_name + "(";
                    for (auto &fn : module()->functions())
                    {
                        std::string fn_name = fn.getName().str();
                        if (fn_name.starts_with(prefix) && fn_name.back() == ')' &&
                            fn.getFunctionType()->getNumParams() == args.size())
                        {
                            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                      "generate_static_method: Found overloaded method via scan: {}", fn_name);
                            method = &fn;
                            break;
                        }
                    }
                    if (method->getFunctionType()->getNumParams() == args.size())
                        break;
                }
            }

            // Fallback for no-arg overloads: try the exact base name (no overload suffix)
            // across all type candidates. No-arg methods are registered without a "()" suffix,
            // so the suffix-based scan above won't find them.
            if (method->getFunctionType()->getNumParams() != args.size())
            {
                for (const auto &type_candidate : type_candidates)
                {
                    // Skip "Global::" candidates — these are synthetic scope names
                    // from inject_parent_module_import, not real namespaces.
                    if (type_candidate.substr(0, 8) == "Global::")
                        continue;
                    std::string base_qualified = type_candidate + "::" + method_name;
                    if (llvm::Function *fn = module()->getFunction(base_qualified))
                    {
                        if (fn->getFunctionType()->getNumParams() == args.size())
                        {
                            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                      "generate_static_method: Found correct overload via exact base name: {}",
                                      base_qualified);
                            method = fn;
                            break;
                        }
                    }
                }
            }

            // Ultimate fallback: scan all module functions for Type::method (any namespace prefix)
            // matching the correct parameter count. This handles cases where the method was
            // instantiated under a different namespace than expected.
            if (method->getFunctionType()->getNumParams() != args.size())
            {
                for (const auto &type_candidate : type_candidates)
                {
                    // Extract simple type name (e.g., "Array_u8" from "std::collections::array::Array_u8")
                    std::string simple_type = type_candidate;
                    size_t last_sep = type_candidate.rfind("::");
                    if (last_sep != std::string::npos)
                        simple_type = type_candidate.substr(last_sep + 2);

                    std::string target_suffix = simple_type + "::" + method_name;
                    for (auto &fn : module()->functions())
                    {
                        std::string fn_name = fn.getName().str();
                        // Skip "Global::" prefixed functions — synthetic scope, not real namespace
                        if (fn_name.substr(0, 8) == "Global::")
                            continue;
                        // Match functions containing "Type::method" either exactly or
                        // with an overload suffix like "(u64)".
                        // Strip any overload suffix from fn_name for comparison.
                        std::string fn_base = fn_name;
                        size_t paren_pos = fn_name.find('(');
                        if (paren_pos != std::string::npos)
                            fn_base = fn_name.substr(0, paren_pos);
                        if ((fn_base == target_suffix || fn_base.ends_with("::" + target_suffix)) &&
                            fn.getFunctionType()->getNumParams() == args.size())
                        {
                            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                      "generate_static_method: Found correct overload via module scan: {}", fn_name);
                            method = &fn;
                            break;
                        }
                    }
                    if (method->getFunctionType()->getNumParams() == args.size())
                        break;
                }
            }
        }

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

                // Handle raw [N x T] array → Array<T> struct conversion
                if (arg && param_type->isStructTy() && arg->getType()->isPointerTy())
                {
                    llvm::StructType *param_st = llvm::cast<llvm::StructType>(param_type);
                    std::string st_name = param_st->hasName() ? param_st->getName().str() : "";
                    if (st_name.find("Array<") != std::string::npos)
                    {
                        llvm::AllocaInst *alloca_arg = llvm::dyn_cast<llvm::AllocaInst>(arg);
                        if (alloca_arg && alloca_arg->getAllocatedType()->isArrayTy())
                        {
                            llvm::ArrayType *arr_ty = llvm::cast<llvm::ArrayType>(alloca_arg->getAllocatedType());
                            uint64_t num_elems = arr_ty->getNumElements();
                            auto &DL = module()->getDataLayout();
                            uint64_t elem_size = DL.getTypeAllocSize(arr_ty->getElementType());
                            uint64_t total_bytes = num_elems * elem_size;

                            llvm::Function *malloc_fn = module()->getFunction("malloc");
                            if (!malloc_fn)
                            {
                                llvm::FunctionType *malloc_type = llvm::FunctionType::get(
                                    llvm::PointerType::get(llvm_ctx(), 0),
                                    {llvm::Type::getInt64Ty(llvm_ctx())}, false);
                                malloc_fn = llvm::Function::Create(malloc_type, llvm::Function::ExternalLinkage,
                                                                   "malloc", module());
                            }
                            llvm::Value *size_val = llvm::ConstantInt::get(llvm::Type::getInt64Ty(llvm_ctx()), total_bytes);
                            llvm::Value *heap_ptr = builder().CreateCall(malloc_fn, {size_val}, "array.heap");
                            builder().CreateMemCpy(heap_ptr, llvm::MaybeAlign(1),
                                                   alloca_arg, llvm::MaybeAlign(1), size_val);

                            llvm::AllocaInst *wrapper = create_entry_alloca(param_st, "array.wrap");
                            llvm::Value *f0 = builder().CreateStructGEP(param_st, wrapper, 0, "array.wrap.data");
                            builder().CreateStore(heap_ptr, f0);
                            llvm::Value *len_val = llvm::ConstantInt::get(llvm::Type::getInt64Ty(llvm_ctx()), num_elems);
                            llvm::Value *f1 = builder().CreateStructGEP(param_st, wrapper, 1, "array.wrap.len");
                            builder().CreateStore(len_val, f1);
                            llvm::Value *f2 = builder().CreateStructGEP(param_st, wrapper, 2, "array.wrap.cap");
                            builder().CreateStore(len_val, f2);
                            arg = create_load(wrapper, param_st, "array.wrapped");
                        }
                    }
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
                        // Trigger on-demand instantiation — record call site for error reporting
                        bool had_inst_source = !ctx().instantiation_file().empty();
                        if (!had_inst_source && node)
                            ctx().set_instantiation_source(node->source_file(), node->location());
                        llvm::StructType *instantiated = gen_codegen->instantiate_struct(base_name, type_args);
                        if (!had_inst_source)
                            ctx().clear_instantiation_source();

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

                                    // Handle raw [N x T] array → Array<T> struct conversion
                                    if (arg && param_type->isStructTy() && arg->getType()->isPointerTy())
                                    {
                                        llvm::StructType *param_st = llvm::cast<llvm::StructType>(param_type);
                                        std::string st_name = param_st->hasName() ? param_st->getName().str() : "";
                                        if (st_name.find("Array<") != std::string::npos)
                                        {
                                            llvm::AllocaInst *alloca_arg = llvm::dyn_cast<llvm::AllocaInst>(arg);
                                            if (alloca_arg && alloca_arg->getAllocatedType()->isArrayTy())
                                            {
                                                llvm::ArrayType *arr_ty = llvm::cast<llvm::ArrayType>(alloca_arg->getAllocatedType());
                                                uint64_t num_elems = arr_ty->getNumElements();
                                                auto &DL = module()->getDataLayout();
                                                uint64_t elem_size = DL.getTypeAllocSize(arr_ty->getElementType());
                                                uint64_t total_bytes = num_elems * elem_size;

                                                llvm::Function *malloc_fn = module()->getFunction("malloc");
                                                if (!malloc_fn)
                                                {
                                                    llvm::FunctionType *malloc_type = llvm::FunctionType::get(
                                                        llvm::PointerType::get(llvm_ctx(), 0),
                                                        {llvm::Type::getInt64Ty(llvm_ctx())}, false);
                                                    malloc_fn = llvm::Function::Create(malloc_type, llvm::Function::ExternalLinkage,
                                                                                       "malloc", module());
                                                }
                                                llvm::Value *size_val = llvm::ConstantInt::get(llvm::Type::getInt64Ty(llvm_ctx()), total_bytes);
                                                llvm::Value *heap_ptr = builder().CreateCall(malloc_fn, {size_val}, "array.heap");
                                                builder().CreateMemCpy(heap_ptr, llvm::MaybeAlign(1),
                                                                       alloca_arg, llvm::MaybeAlign(1), size_val);

                                                llvm::AllocaInst *wrapper = create_entry_alloca(param_st, "array.wrap");
                                                llvm::Value *f0 = builder().CreateStructGEP(param_st, wrapper, 0, "array.wrap.data");
                                                builder().CreateStore(heap_ptr, f0);
                                                llvm::Value *len_val = llvm::ConstantInt::get(llvm::Type::getInt64Ty(llvm_ctx()), num_elems);
                                                llvm::Value *f1 = builder().CreateStructGEP(param_st, wrapper, 1, "array.wrap.len");
                                                builder().CreateStore(len_val, f1);
                                                llvm::Value *f2 = builder().CreateStructGEP(param_st, wrapper, 2, "array.wrap.cap");
                                                builder().CreateStore(len_val, f2);
                                                arg = create_load(wrapper, param_st, "array.wrapped");
                                            }
                                        }
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
        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                  "generate_default_value: Generating default for '{}'", type_name);

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

        // Strategy 1: Look up TypeRef through arena (by resolved name, then original name)
        TypeRef type_ref = ctx().symbols().arena().lookup_type_by_name(resolved_name);
        if (!type_ref.is_valid() && resolved_name != type_name)
        {
            type_ref = ctx().symbols().arena().lookup_type_by_name(type_name);
        }

        // Strategy 2: Try symbol table lookup (handles cross-module imported types)
        if (!type_ref.is_valid())
        {
            Symbol *sym = symbols().lookup_symbol(type_name);
            if (sym && sym->kind == SymbolKind::Type && sym->type.is_valid())
            {
                type_ref = sym->type;
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "generate_default_value: Found '{}' via symbol table (kind={})",
                          type_name, static_cast<int>(type_ref->kind()));
            }
        }

        // Unwrap nested TypeAlias to get actual target
        while (type_ref.is_valid() && type_ref->kind() == Cryo::TypeKind::TypeAlias)
        {
            auto *alias = static_cast<const Cryo::TypeAliasType *>(type_ref.get());
            type_ref = alias->target();
        }

        if (type_ref.is_valid())
        {
            llvm::Type *llvm_type = types().map(type_ref);
            if (llvm_type && llvm_type->isSized() && !llvm_type->isVoidTy())
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "generate_default_value: Mapped '{}' to LLVM type (typeID={})",
                          type_name, llvm_type->getTypeID());
                return generate_zero_value(llvm_type, type_name);
            }
        }

        // Strategy 3: Try LLVM type lookup by name (may find non-opaque struct)
        llvm::Type *llvm_type = resolve_type_by_name(type_name);
        if (llvm_type && llvm_type->isSized() && !llvm_type->isVoidTy())
        {
            return generate_zero_value(llvm_type, type_name);
        }

        // Strategy 4: Parse the resolved alias string directly
        // Handles type aliases to fixed-size arrays like "void[48]" or "u8[64]"
        if (resolved_name != type_name)
        {
            llvm::Type *parsed_type = parse_type_annotation_to_llvm(resolved_name);
            if (parsed_type && parsed_type->isSized() && !parsed_type->isVoidTy())
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "generate_default_value: Parsed alias target '{}' to LLVM type (typeID={})",
                          resolved_name, parsed_type->getTypeID());
                return generate_zero_value(parsed_type, type_name);
            }
        }

        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                  "generate_default_value: Could not resolve type '{}'", type_name);
        return nullptr;
    }

    llvm::Type *CallCodegen::parse_type_annotation_to_llvm(const std::string &annotation)
    {
        // Handle fixed-size array annotations: "T[N]" -> [N x LLVM_T]
        // e.g., "void[48]" -> [48 x i8], "u8[64]" -> [64 x i8]
        size_t bracket_pos = annotation.find('[');
        size_t close_pos = annotation.find(']');
        if (bracket_pos != std::string::npos && close_pos != std::string::npos && close_pos > bracket_pos)
        {
            std::string elem_name = annotation.substr(0, bracket_pos);
            std::string size_str = annotation.substr(bracket_pos + 1, close_pos - bracket_pos - 1);

            size_t array_size = 0;
            try { array_size = std::stoull(size_str); }
            catch (...) { return nullptr; }

            if (array_size == 0) return nullptr;

            // Map element type - void means opaque byte buffer
            llvm::Type *elem_type = nullptr;
            if (elem_name == "void" || elem_name == "u8" || elem_name == "i8")
                elem_type = types().i8_type();
            else if (elem_name == "i16" || elem_name == "u16")
                elem_type = types().i16_type();
            else if (elem_name == "i32" || elem_name == "u32")
                elem_type = types().i32_type();
            else if (elem_name == "i64" || elem_name == "u64")
                elem_type = types().i64_type();
            else
            {
                LOG_WARN(Cryo::LogComponent::CODEGEN,
                         "parse_type_annotation_to_llvm: Unknown element type '{}' in fixed-size array",
                         elem_name);
                elem_type = types().i8_type(); // Keep fallback - annotation parsing is best-effort
            }

            return llvm::ArrayType::get(elem_type, array_size);
        }

        // Handle pointer types: "T*"
        if (!annotation.empty() && annotation.back() == '*')
        {
            return types().ptr_type();
        }

        // Try as a primitive or known type via TypeMapper
        return types().get_type(annotation);
    }

    llvm::Value *CallCodegen::generate_zero_value(llvm::Type *llvm_type, const std::string &name)
    {
        // Safety: reject unsized types (e.g., opaque structs) - they can't be allocated or zero-initialized
        if (!llvm_type->isSized())
        {
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "generate_zero_value: Type '{}' is not sized, cannot zero-initialize", name);
            return nullptr;
        }

        // For aggregate types (structs), allocate and zero-initialize
        if (llvm_type->isStructTy())
        {
            llvm::AllocaInst *alloca = create_entry_alloca(llvm_type, name + ".default");
            if (!alloca) return nullptr;
            builder().CreateStore(llvm::Constant::getNullValue(llvm_type), alloca);
            return alloca;
        }

        // For arrays, allocate and zero-initialize
        if (llvm_type->isArrayTy())
        {
            llvm::AllocaInst *alloca = create_entry_alloca(llvm_type, name + ".default");
            if (!alloca) return nullptr;
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

                // Handle InstantiatedType - use monomorphized name
                if (obj_type->kind() == Cryo::TypeKind::InstantiatedType)
                {
                    auto *inst_type = static_cast<const Cryo::InstantiatedType *>(obj_type.get());
                    if (inst_type->has_resolved_type())
                    {
                        type_name = inst_type->resolved_type()->display_name();
                    }
                    else if (inst_type->generic_base().is_valid())
                    {
                        std::string base = inst_type->generic_base()->display_name();
                        std::string mangled = base;
                        const auto &args = inst_type->type_args();
                        if (!args.empty())
                        {
                            mangled += "_";
                            for (size_t i = 0; i < args.size(); ++i)
                            {
                                if (i > 0) mangled += "_";
                                if (args[i].is_valid())
                                {
                                    std::string arg_name = args[i]->display_name();
                                    std::replace(arg_name.begin(), arg_name.end(), '<', '_');
                                    std::replace(arg_name.begin(), arg_name.end(), '>', '_');
                                    std::replace(arg_name.begin(), arg_name.end(), ',', '_');
                                    std::replace(arg_name.begin(), arg_name.end(), ' ', '_');
                                    std::replace(arg_name.begin(), arg_name.end(), '*', 'p');
                                    mangled += arg_name;
                                }
                            }
                        }
                        type_name = mangled;
                    }
                }

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
                // If we're inside a monomorphized method body, the AST types
                // may still contain unresolved generic parameters (e.g.,
                // "Array<T>" instead of "Array<u8>").  The codegen context's
                // current_type_name is set correctly by
                // GenericCodegen::instantiate_method_for_type to the mangled
                // concrete name (e.g., "Array_u8").  Prefer it when available
                // and the AST-derived name still looks generic.
                {
                    std::string ctx_type = ctx().current_type_name();
                    if (!ctx_type.empty() && ctx_type != type_name)
                    {
                        // Check if type_name contains single-letter type params
                        // that suggest it hasn't been fully monomorphized
                        bool looks_generic = false;
                        // Generic params: <T>, <T, E>, <K, V> etc.
                        if (type_name.find('<') != std::string::npos)
                        {
                            // Has angle brackets - check if the args are single uppercase letters
                            size_t open = type_name.find('<');
                            size_t close = type_name.find('>');
                            if (open != std::string::npos && close != std::string::npos)
                            {
                                std::string params = type_name.substr(open + 1, close - open - 1);
                                // If params section contains single uppercase letters (T, E, K, V)
                                // surrounded by delimiters, it's still generic
                                for (size_t i = 0; i < params.size(); ++i)
                                {
                                    char c = params[i];
                                    if (std::isupper(c) && (i == 0 || params[i-1] == ' ' || params[i-1] == ',') &&
                                        (i+1 >= params.size() || params[i+1] == ',' || params[i+1] == '>'))
                                    {
                                        looks_generic = true;
                                        break;
                                    }
                                }
                            }
                        }
                        if (looks_generic)
                        {
                            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                      "Instance method: type '{}' has unsubstituted params, "
                                      "using context type '{}' instead",
                                      type_name, ctx_type);
                            type_name = ctx_type;
                        }
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
                        // If the type name still has unsubstituted generic params (e.g.,
                        // "Array<T>"), use GenericCodegen to substitute them into the
                        // concrete mangled name (e.g., "Array_String").
                        if (type_name.find('<') != std::string::npos)
                        {
                            CodegenVisitor *visitor = ctx().visitor();
                            GenericCodegen *generics = visitor ? visitor->get_generics() : nullptr;
                            if (generics && generics->in_type_param_scope())
                            {
                                std::string substituted = generics->substitute_type_annotation(type_name);
                                if (!substituted.empty())
                                {
                                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                              "Instance method: substituted variable map type '{}' -> '{}'",
                                              type_name, substituted);
                                    type_name = substituted;
                                }
                            }
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

                                // Handle InstantiatedType - use monomorphized name
                                if (it->second->kind() == Cryo::TypeKind::InstantiatedType)
                                {
                                    auto *inst = static_cast<const Cryo::InstantiatedType *>(it->second.get());
                                    if (inst->has_resolved_type())
                                    {
                                        parent_type_name = inst->resolved_type()->display_name();
                                    }
                                    else if (inst->generic_base().is_valid())
                                    {
                                        std::string base = inst->generic_base()->display_name();
                                        std::string mangled = base;
                                        const auto &args = inst->type_args();
                                        if (!args.empty())
                                        {
                                            mangled += "_";
                                            for (size_t i = 0; i < args.size(); ++i)
                                            {
                                                if (i > 0) mangled += "_";
                                                if (args[i].is_valid())
                                                {
                                                    std::string arg_name = args[i]->display_name();
                                                    std::replace(arg_name.begin(), arg_name.end(), '<', '_');
                                                    std::replace(arg_name.begin(), arg_name.end(), '>', '_');
                                                    std::replace(arg_name.begin(), arg_name.end(), ',', '_');
                                                    std::replace(arg_name.begin(), arg_name.end(), ' ', '_');
                                                    std::replace(arg_name.begin(), arg_name.end(), '*', 'p');
                                                    mangled += arg_name;
                                                }
                                            }
                                        }
                                        parent_type_name = mangled;
                                    }
                                }

                                if (!parent_type_name.empty() && parent_type_name.back() == '*')
                                {
                                    parent_type_name.pop_back();
                                }
                                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                          "Nested member access: parent '{}' -> {}", parent_id->name(), parent_type_name);
                            }
                        }
                    }
                    // Handle doubly-nested member access (e.g., this.state.phase in this.state.phase.label())
                    // Walk the chain to resolve the intermediate type through TemplateRegistry field lookups
                    else if (auto *parent_member = dynamic_cast<MemberAccessNode *>(member_access->object()))
                    {
                        // Collect the member chain from innermost to outermost
                        std::vector<std::string> chain;
                        chain.push_back(parent_member->member()); // e.g., "state"
                        ASTNode *current = parent_member->object();

                        // Walk up through any further nested MemberAccessNodes
                        while (auto *next = dynamic_cast<MemberAccessNode *>(current))
                        {
                            chain.push_back(next->member());
                            current = next->object();
                        }

                        // Resolve the root type
                        std::string root_type;
                        if (auto *root_id = dynamic_cast<IdentifierNode *>(current))
                        {
                            if (root_id->name() == "this")
                            {
                                root_type = ctx().current_type_name();
                            }
                            else
                            {
                                auto &var_types = ctx().variable_types_map();
                                auto it = var_types.find(root_id->name());
                                if (it != var_types.end() && it->second.is_valid())
                                {
                                    root_type = it->second->display_name();
                                    if (!root_type.empty() && (root_type.back() == '*' || root_type.back() == '&'))
                                        root_type.pop_back();
                                }
                            }
                        }

                        // Walk the chain in reverse (outermost to innermost), resolving field types
                        if (!root_type.empty())
                        {
                            auto *template_reg = ctx().template_registry();
                            std::string current_type = root_type;

                            for (int ci = static_cast<int>(chain.size()) - 1; ci >= 0; --ci)
                            {
                                const std::string &field = chain[ci];
                                bool found = false;

                                if (template_reg)
                                {
                                    auto candidates = generate_lookup_candidates(current_type, Cryo::SymbolKind::Type);
                                    for (const auto &cand : candidates)
                                    {
                                        const TemplateRegistry::StructFieldInfo *fi = template_reg->get_struct_field_types(cand);
                                        if (fi)
                                        {
                                            for (size_t i = 0; i < fi->field_names.size(); ++i)
                                            {
                                                if (fi->field_names[i] == field && i < fi->field_types.size())
                                                {
                                                    TypeRef ft = fi->field_types[i];
                                                    if (ft.is_valid())
                                                    {
                                                        current_type = ft->display_name();
                                                        if (!current_type.empty() && (current_type.back() == '*' || current_type.back() == '&'))
                                                            current_type.pop_back();
                                                        found = true;
                                                    }
                                                    break;
                                                }
                                            }
                                        }
                                        if (found) break;
                                    }
                                }

                                if (!found)
                                {
                                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                              "Doubly-nested member access: could not resolve field '{}' in type '{}'",
                                              field, current_type);
                                    current_type.clear();
                                    break;
                                }
                            }

                            parent_type_name = current_type;
                            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                      "Doubly-nested member access: resolved chain to parent type '{}'", parent_type_name);
                        }
                    }

                    // Handle call-expression parent (e.g., this.current().kind in this.current().kind.is_unary_prefix_op())
                    // The parent of the member access is a method call — resolve its return type
                    else if (auto *parent_call = dynamic_cast<CallExpressionNode *>(member_access->object()))
                    {
                        // First try: resolved type on the call expression node itself
                        if (parent_call->has_resolved_type())
                        {
                            TypeRef call_ret = parent_call->get_resolved_type();
                            if (call_ret.is_valid())
                            {
                                parent_type_name = call_ret->display_name();
                                if (!parent_type_name.empty() && (parent_type_name.back() == '*' || parent_type_name.back() == '&'))
                                    parent_type_name.pop_back();
                            }
                        }

                        // Fallback: infer return type from the inner method's LLVM function or TemplateRegistry
                        if (parent_type_name.empty())
                        {
                            if (auto *inner_callee = dynamic_cast<MemberAccessNode *>(parent_call->callee()))
                            {
                                std::string inner_method_name = inner_callee->member();
                                std::string inner_receiver_type;

                                if (auto *inner_id = dynamic_cast<IdentifierNode *>(inner_callee->object()))
                                {
                                    if (inner_id->name() == "this")
                                    {
                                        inner_receiver_type = ctx().current_type_name();
                                    }
                                    else
                                    {
                                        auto &var_types = ctx().variable_types_map();
                                        auto it = var_types.find(inner_id->name());
                                        if (it != var_types.end() && it->second.is_valid())
                                        {
                                            inner_receiver_type = it->second->display_name();
                                            if (!inner_receiver_type.empty() && (inner_receiver_type.back() == '*' || inner_receiver_type.back() == '&'))
                                                inner_receiver_type.pop_back();
                                        }
                                    }
                                }
                                else if (auto *inner_member = dynamic_cast<MemberAccessNode *>(inner_callee->object()))
                                {
                                    // Handle nested member access receivers (e.g., this.parser.current())
                                    std::string nested_field = inner_member->member();
                                    std::string nested_root;
                                    if (auto *root_id = dynamic_cast<IdentifierNode *>(inner_member->object()))
                                    {
                                        if (root_id->name() == "this")
                                            nested_root = ctx().current_type_name();
                                        else
                                        {
                                            auto &var_types = ctx().variable_types_map();
                                            auto it = var_types.find(root_id->name());
                                            if (it != var_types.end() && it->second.is_valid())
                                            {
                                                nested_root = it->second->display_name();
                                                if (!nested_root.empty() && (nested_root.back() == '*' || nested_root.back() == '&'))
                                                    nested_root.pop_back();
                                            }
                                        }
                                    }
                                    if (!nested_root.empty())
                                    {
                                        auto cands = generate_lookup_candidates(nested_root, Cryo::SymbolKind::Type);
                                        if (auto *template_reg = ctx().template_registry())
                                        {
                                            for (const auto &cand : cands)
                                            {
                                                const TemplateRegistry::StructFieldInfo *fi = template_reg->get_struct_field_types(cand);
                                                if (fi)
                                                {
                                                    for (size_t i = 0; i < fi->field_names.size(); ++i)
                                                    {
                                                        if (fi->field_names[i] == nested_field && i < fi->field_types.size())
                                                        {
                                                            TypeRef ft = fi->field_types[i];
                                                            if (ft.is_valid())
                                                            {
                                                                inner_receiver_type = ft->display_name();
                                                                if (!inner_receiver_type.empty() && (inner_receiver_type.back() == '*' || inner_receiver_type.back() == '&'))
                                                                    inner_receiver_type.pop_back();
                                                            }
                                                            break;
                                                        }
                                                    }
                                                    if (!inner_receiver_type.empty()) break;
                                                }
                                            }
                                        }
                                    }
                                }

                                if (!inner_receiver_type.empty())
                                {
                                    // Build a list of types to try: the receiver type itself + any base classes
                                    std::vector<std::string> types_to_try;
                                    types_to_try.push_back(inner_receiver_type);

                                    // Walk the class hierarchy to find base classes
                                    {
                                        std::string walk_type = inner_receiver_type;
                                        for (int depth = 0; depth < 10 && !walk_type.empty(); ++depth)
                                        {
                                            auto walk_candidates = generate_lookup_candidates(walk_type, Cryo::SymbolKind::Type);
                                            bool found_base = false;
                                            for (const auto &wc : walk_candidates)
                                            {
                                                TypeRef class_type = ctx().symbols().lookup_class_type(wc);
                                                if (class_type.is_valid() && class_type->kind() == Cryo::TypeKind::Class)
                                                {
                                                    auto *cls = dynamic_cast<const Cryo::ClassType *>(class_type.get());
                                                    if (cls && cls->has_base_class())
                                                    {
                                                        std::string base = cls->base_class()->display_name();
                                                        if (!base.empty() && (base.back() == '*' || base.back() == '&'))
                                                            base.pop_back();
                                                        if (!base.empty())
                                                        {
                                                            types_to_try.push_back(base);
                                                            walk_type = base;
                                                            found_base = true;
                                                        }
                                                    }
                                                }
                                                if (found_base) break;
                                            }
                                            if (!found_base) break;
                                        }
                                    }

                                    // Try LLVM function return type for each type in hierarchy
                                    for (const auto &try_type : types_to_try)
                                    {
                                        if (!parent_type_name.empty()) break;
                                        llvm::Function *inner_fn = resolve_method_by_name(try_type, inner_method_name);
                                        if (inner_fn)
                                        {
                                            llvm::Type *ret_type = inner_fn->getReturnType();
                                            if (ret_type && !ret_type->isVoidTy())
                                            {
                                                if (ret_type->isStructTy())
                                                {
                                                    auto *st = llvm::cast<llvm::StructType>(ret_type);
                                                    if (st->hasName())
                                                    {
                                                        parent_type_name = st->getName().str();
                                                    }
                                                }
                                                else if (ret_type->isPointerTy())
                                                {
                                                    parent_type_name = "ptr";
                                                }
                                            }
                                        }
                                    }

                                    // Fallback: TemplateRegistry method return type annotation (also with hierarchy)
                                    if (parent_type_name.empty())
                                    {
                                        if (auto *template_reg = ctx().template_registry())
                                        {
                                            for (const auto &try_type : types_to_try)
                                            {
                                                if (!parent_type_name.empty()) break;
                                                auto type_candidates = generate_lookup_candidates(try_type, Cryo::SymbolKind::Type);
                                                for (const auto &cand : type_candidates)
                                                {
                                                    std::string qualified = cand + "::" + inner_method_name;
                                                    std::string ret_annotation = template_reg->get_method_return_type_annotation(qualified);
                                                    if (!ret_annotation.empty())
                                                    {
                                                        parent_type_name = ret_annotation;
                                                        if (!parent_type_name.empty() && (parent_type_name.back() == '*' || parent_type_name.back() == '&'))
                                                            parent_type_name.pop_back();
                                                        break;
                                                    }

                                                    const TemplateRegistry::MethodMetadata *meta =
                                                        template_reg->find_template_method(cand, inner_method_name);
                                                    if (meta && !meta->return_type_annotation.empty())
                                                    {
                                                        parent_type_name = meta->return_type_annotation;
                                                        if (!parent_type_name.empty() && (parent_type_name.back() == '*' || parent_type_name.back() == '&'))
                                                            parent_type_name.pop_back();
                                                        break;
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                  "Call-expression parent: resolved return type to '{}'", parent_type_name);
                    }

                    // Handle array-indexed parent (e.g., entries[k].visibility in entries[k].visibility.is_public())
                    else if (auto *parent_arr = dynamic_cast<ArrayAccessNode *>(member_access->object()))
                    {
                        ExpressionNode *arr_expr = parent_arr->array();
                        if (auto *arr_id = dynamic_cast<IdentifierNode *>(arr_expr))
                        {
                            auto &var_types = ctx().variable_types_map();
                            auto it = var_types.find(arr_id->name());
                            if (it != var_types.end() && it->second.is_valid())
                            {
                                TypeRef var_type = it->second;
                                if (var_type->kind() == Cryo::TypeKind::Array)
                                {
                                    auto *arr_type = dynamic_cast<const Cryo::ArrayType *>(var_type.get());
                                    if (arr_type && arr_type->element().is_valid())
                                    {
                                        parent_type_name = arr_type->element()->display_name();
                                        if (!parent_type_name.empty() && parent_type_name.back() == '*')
                                            parent_type_name.pop_back();
                                    }
                                }
                            }
                        }
                        else if (auto *arr_member = dynamic_cast<MemberAccessNode *>(arr_expr))
                        {
                            // e.g., this.module_entries[j] — resolve field type's element type
                            std::string arr_field_name = arr_member->member();
                            std::string arr_parent_type;

                            if (auto *arr_parent_id = dynamic_cast<IdentifierNode *>(arr_member->object()))
                            {
                                if (arr_parent_id->name() == "this")
                                    arr_parent_type = ctx().current_type_name();
                                else
                                {
                                    auto &var_types = ctx().variable_types_map();
                                    auto vit = var_types.find(arr_parent_id->name());
                                    if (vit != var_types.end() && vit->second.is_valid())
                                    {
                                        arr_parent_type = vit->second->display_name();
                                        if (!arr_parent_type.empty() && (arr_parent_type.back() == '*' || arr_parent_type.back() == '&'))
                                            arr_parent_type.pop_back();
                                    }
                                }
                            }

                            if (!arr_parent_type.empty())
                            {
                                auto cands = generate_lookup_candidates(arr_parent_type, Cryo::SymbolKind::Type);
                                if (auto *template_reg = ctx().template_registry())
                                {
                                    for (const auto &cand : cands)
                                    {
                                        const TemplateRegistry::StructFieldInfo *fi = template_reg->get_struct_field_types(cand);
                                        if (fi)
                                        {
                                            for (size_t i = 0; i < fi->field_names.size(); ++i)
                                            {
                                                if (fi->field_names[i] == arr_field_name && i < fi->field_types.size())
                                                {
                                                    TypeRef ft = fi->field_types[i];
                                                    if (ft.is_valid() && ft->kind() == Cryo::TypeKind::Array)
                                                    {
                                                        auto *arr_t = dynamic_cast<const Cryo::ArrayType *>(ft.get());
                                                        if (arr_t && arr_t->element().is_valid())
                                                        {
                                                            parent_type_name = arr_t->element()->display_name();
                                                            if (!parent_type_name.empty() && parent_type_name.back() == '*')
                                                                parent_type_name.pop_back();
                                                        }
                                                    }
                                                    break;
                                                }
                                            }
                                            if (!parent_type_name.empty()) break;
                                        }
                                    }
                                }
                            }
                        }

                        if (!parent_type_name.empty())
                        {
                            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                      "Array-indexed parent: resolved element type to '{}'", parent_type_name);
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

                                                // Handle InstantiatedType - use monomorphized name
                                                if (field_type->kind() == Cryo::TypeKind::InstantiatedType)
                                                {
                                                    auto *inst = static_cast<const Cryo::InstantiatedType *>(field_type.get());
                                                    if (inst->has_resolved_type())
                                                    {
                                                        type_name = inst->resolved_type()->display_name();
                                                    }
                                                    else if (inst->generic_base().is_valid())
                                                    {
                                                        std::string base = inst->generic_base()->display_name();
                                                        std::string mangled = base;
                                                        const auto &args = inst->type_args();
                                                        if (!args.empty())
                                                        {
                                                            mangled += "_";
                                                            for (size_t i = 0; i < args.size(); ++i)
                                                            {
                                                                if (i > 0) mangled += "_";
                                                                if (args[i].is_valid())
                                                                {
                                                                    std::string arg_name = args[i]->display_name();
                                                                    std::replace(arg_name.begin(), arg_name.end(), '<', '_');
                                                                    std::replace(arg_name.begin(), arg_name.end(), '>', '_');
                                                                    std::replace(arg_name.begin(), arg_name.end(), ',', '_');
                                                                    std::replace(arg_name.begin(), arg_name.end(), ' ', '_');
                                                                    std::replace(arg_name.begin(), arg_name.end(), '*', 'p');
                                                                    mangled += arg_name;
                                                                }
                                                            }
                                                        }
                                                        type_name = mangled;
                                                    }
                                                }

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
                                            else
                                            {
                                                // Field found by name but TypeRef is invalid (cross-module generic)
                                                // Use stored annotation string to trigger on-demand instantiation
                                                std::string annotation;
                                                if (i < field_info->field_type_annotations.size())
                                                    annotation = field_info->field_type_annotations[i];

                                                if (!annotation.empty())
                                                {
                                                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                                              "Field '{}' type unresolved, using annotation fallback: '{}'",
                                                              member_name, annotation);

                                                    size_t angle_pos = annotation.find('<');
                                                    if (angle_pos != std::string::npos)
                                                    {
                                                        // Generic type - parse and on-demand instantiate
                                                        size_t close_pos = annotation.rfind('>');
                                                        std::string base_name = annotation.substr(0, angle_pos);
                                                        std::string type_args_str = annotation.substr(angle_pos + 1, close_pos - angle_pos - 1);

                                                        CodegenVisitor *visitor = ctx().visitor();
                                                        GenericCodegen *gen_codegen = visitor ? visitor->get_generics() : nullptr;

                                                        if (gen_codegen && gen_codegen->is_generic_template(base_name))
                                                        {
                                                            std::vector<TypeRef> type_args = parse_type_args_from_string(type_args_str, symbols());
                                                            if (!type_args.empty())
                                                            {
                                                                bool had_inst_source = !ctx().instantiation_file().empty();
                                                                if (!had_inst_source && node)
                                                                    ctx().set_instantiation_source(node->source_file(), node->location());
                                                                llvm::StructType *instantiated = gen_codegen->instantiate_struct(base_name, type_args);
                                                                if (!had_inst_source)
                                                                    ctx().clear_instantiation_source();

                                                                if (instantiated)
                                                                {
                                                                    type_name = gen_codegen->mangle_type_name(base_name, type_args);
                                                                    // Qualify with source namespace from template
                                                                    if (auto *template_reg2 = ctx().template_registry())
                                                                    {
                                                                        const auto *tmpl = template_reg2->find_template(base_name);
                                                                        if (tmpl && !tmpl->module_namespace.empty())
                                                                            type_name = tmpl->module_namespace + "::" + type_name;
                                                                    }
                                                                }
                                                            }
                                                        }
                                                    }
                                                    else
                                                    {
                                                        type_name = annotation;
                                                        // Strip pointer/reference suffix for method resolution
                                                        // (e.g., "DiagRenderer*" -> "DiagRenderer")
                                                        if (!type_name.empty() && (type_name.back() == '*' || type_name.back() == '&'))
                                                            type_name.pop_back();
                                                    }
                                                }

                                                if (!type_name.empty())
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
                                          "No field info in TemplateRegistry for type: {} (tried {} candidates), trying base classes",
                                          parent_type_name, type_candidates.size());

                                // Walk the class inheritance chain to find inherited fields
                                TypeRef cls_ref = ctx().symbols().lookup_class_type(parent_type_name);
                                if (cls_ref.is_valid())
                                {
                                    auto *cls = dynamic_cast<const Cryo::ClassType *>(cls_ref.get());
                                    while (cls && cls->has_base_class() && type_name.empty())
                                    {
                                        auto *base = dynamic_cast<const Cryo::ClassType *>(cls->base_class().get());
                                        if (!base)
                                            break;

                                        std::string base_name = base->name();
                                        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                                  "Trying base class '{}' for field '{}'",
                                                  base_name, member_name);

                                        auto base_candidates = generate_lookup_candidates(base_name, Cryo::SymbolKind::Type);
                                        for (const auto &bc : base_candidates)
                                        {
                                            const TemplateRegistry::StructFieldInfo *base_fi = template_reg->get_struct_field_types(bc);
                                            if (base_fi)
                                            {
                                                for (size_t i = 0; i < base_fi->field_names.size(); ++i)
                                                {
                                                    if (base_fi->field_names[i] == member_name && i < base_fi->field_types.size())
                                                    {
                                                        TypeRef ft = base_fi->field_types[i];
                                                        if (ft.is_valid())
                                                        {
                                                            type_name = ft->display_name();
                                                            if (ft->kind() == Cryo::TypeKind::Pointer)
                                                            {
                                                                auto *ptr = dynamic_cast<const Cryo::PointerType *>(ft.get());
                                                                if (ptr && ptr->pointee().is_valid())
                                                                    type_name = ptr->pointee()->display_name();
                                                            }
                                                            else if (!type_name.empty() && type_name.back() == '*')
                                                            {
                                                                type_name.pop_back();
                                                            }

                                                            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                                                      "Resolved inherited field {}.{} -> {} (via base class '{}', candidate '{}')",
                                                                      parent_type_name, member_name, type_name, base_name, bc);
                                                        }
                                                        else if (i < base_fi->field_type_annotations.size() && !base_fi->field_type_annotations[i].empty())
                                                        {
                                                            // Use annotation fallback
                                                            type_name = base_fi->field_type_annotations[i];
                                                            if (!type_name.empty() && (type_name.back() == '*' || type_name.back() == '&'))
                                                                type_name.pop_back();
                                                            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                                                      "Resolved inherited field {}.{} -> {} (via base class '{}' annotation)",
                                                                      parent_type_name, member_name, type_name, base_name);
                                                        }
                                                        break;
                                                    }
                                                }
                                            }
                                            if (!type_name.empty()) break;
                                        }
                                        cls = base;
                                    }
                                }
                            }
                        }

                        // Fallback: try StructType/ClassType::field_type from symbols
                        if (type_name.empty())
                        {
                            for (const auto &candidate : type_candidates)
                            {
                                // Try struct first, then class
                                std::optional<TypeRef> field_type_opt;
                                TypeRef type_ref = ctx().symbols().lookup_struct_type(candidate);
                                if (type_ref.is_valid() && type_ref->kind() == Cryo::TypeKind::Struct)
                                {
                                    auto *struct_ty = static_cast<const Cryo::StructType *>(type_ref.get());
                                    field_type_opt = struct_ty->field_type(member_name);
                                }
                                if (!field_type_opt.has_value() || !field_type_opt->is_valid())
                                {
                                    type_ref = ctx().symbols().lookup_class_type(candidate);
                                    if (type_ref.is_valid() && type_ref->kind() == Cryo::TypeKind::Class)
                                    {
                                        auto *class_ty = static_cast<const Cryo::ClassType *>(type_ref.get());
                                        field_type_opt = class_ty->field_type(member_name);
                                    }
                                }

                                // If TypeRef is invalid but we found the owning type, get
                                // the annotation string from the TemplateRegistry entry
                                if ((!field_type_opt.has_value() || !field_type_opt->is_valid()) && type_ref.is_valid())
                                {
                                    if (auto *template_reg = ctx().template_registry())
                                    {
                                        const TemplateRegistry::StructFieldInfo *fi = template_reg->get_struct_field_types(candidate);
                                        if (fi)
                                        {
                                            for (size_t i = 0; i < fi->field_names.size(); ++i)
                                            {
                                                if (fi->field_names[i] == member_name && i < fi->field_type_annotations.size())
                                                {
                                                    std::string ann = fi->field_type_annotations[i];
                                                    if (!ann.empty())
                                                    {
                                                        type_name = ann;
                                                        // Strip pointer/reference suffix for method resolution
                                                        if (!type_name.empty() && (type_name.back() == '*' || type_name.back() == '&'))
                                                            type_name.pop_back();
                                                        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                                                  "Resolved field type from annotation: {}.{} -> {}",
                                                                  candidate, member_name, type_name);
                                                    }
                                                    break;
                                                }
                                            }
                                        }
                                    }
                                    if (!type_name.empty()) break;
                                }

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
                                                  "Resolved nested member access: {}.{} -> {} (via type lookup, found as '{}')",
                                                  parent_type_name, member_name, type_name, candidate);
                                        break;
                                    }
                            }
                        }
                    }
                }
                // Fallback for array-indexed receiver (e.g., this.scopes[idx].find(name))
                // When the object is an ArrayAccessNode, resolve the array's element type
                else if (auto *array_access = dynamic_cast<ArrayAccessNode *>(callee->object()))
                {
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "Attempting type resolution for array-indexed receiver");

                    // The array sub-expression can be:
                    //   - MemberAccessNode (e.g., this.scopes)
                    //   - IdentifierNode (e.g., entries)
                    ExpressionNode *array_expr = array_access->array();
                    std::string array_type_name;

                    if (auto *arr_member = dynamic_cast<MemberAccessNode *>(array_expr))
                    {
                        // e.g., this.scopes[idx] -> resolve "scopes" field type on parent
                        std::string arr_field_name = arr_member->member();
                        std::string arr_parent_type;

                        if (auto *arr_parent_id = dynamic_cast<IdentifierNode *>(arr_member->object()))
                        {
                            if (arr_parent_id->name() == "this")
                            {
                                arr_parent_type = ctx().current_type_name();
                            }
                            else
                            {
                                auto &var_types = ctx().variable_types_map();
                                auto it = var_types.find(arr_parent_id->name());
                                if (it != var_types.end() && it->second.is_valid())
                                {
                                    arr_parent_type = it->second->display_name();
                                    if (!arr_parent_type.empty() && (arr_parent_type.back() == '*' || arr_parent_type.back() == '&'))
                                        arr_parent_type.pop_back();
                                }
                            }
                        }

                        if (!arr_parent_type.empty())
                        {
                            auto type_candidates = generate_lookup_candidates(arr_parent_type, Cryo::SymbolKind::Type);
                            if (auto *template_reg = ctx().template_registry())
                            {
                                for (const auto &candidate : type_candidates)
                                {
                                    const TemplateRegistry::StructFieldInfo *field_info = template_reg->get_struct_field_types(candidate);
                                    if (field_info)
                                    {
                                        for (size_t i = 0; i < field_info->field_names.size(); ++i)
                                        {
                                            if (field_info->field_names[i] == arr_field_name && i < field_info->field_types.size())
                                            {
                                                TypeRef field_type = field_info->field_types[i];
                                                if (field_type.is_valid())
                                                {
                                                    // Field is an array type — get element type
                                                    if (field_type->kind() == Cryo::TypeKind::Array)
                                                    {
                                                        auto *arr_type = dynamic_cast<const Cryo::ArrayType *>(field_type.get());
                                                        if (arr_type && arr_type->element().is_valid())
                                                        {
                                                            TypeRef elem_type = arr_type->element();
                                                            array_type_name = elem_type->display_name();
                                                            // Strip pointer suffix
                                                            if (!array_type_name.empty() && array_type_name.back() == '*')
                                                                array_type_name.pop_back();
                                                            // If element is itself an array (e.g., Symbol[][]),
                                                            // the receiver is the inner array — use T[] naming
                                                            // so the built-in array method detector matches
                                                            if (elem_type->kind() == Cryo::TypeKind::Array)
                                                            {
                                                                auto *inner_arr = dynamic_cast<const Cryo::ArrayType *>(elem_type.get());
                                                                if (inner_arr && inner_arr->element().is_valid())
                                                                {
                                                                    array_type_name = inner_arr->element()->display_name() + "[]";
                                                                }
                                                            }
                                                        }
                                                    }
                                                    else
                                                    {
                                                        // Not an array — use field type directly (shouldn't normally reach here)
                                                        array_type_name = field_type->display_name();
                                                        if (!array_type_name.empty() && array_type_name.back() == '*')
                                                            array_type_name.pop_back();
                                                    }
                                                }
                                                else if (i < field_info->field_type_annotations.size())
                                                {
                                                    // TypeRef invalid — use annotation string
                                                    std::string annotation = field_info->field_type_annotations[i];
                                                    // Strip [] suffix to get element type name
                                                    if (annotation.size() > 2 && annotation.compare(annotation.size() - 2, 2, "[]") == 0)
                                                    {
                                                        array_type_name = annotation.substr(0, annotation.size() - 2);
                                                        // Strip pointer suffix if present
                                                        if (!array_type_name.empty() && array_type_name.back() == '*')
                                                            array_type_name.pop_back();
                                                    }
                                                }
                                                break;
                                            }
                                        }
                                        if (!array_type_name.empty()) break;
                                    }
                                }
                            }
                        }
                    }
                    else if (auto *arr_id = dynamic_cast<IdentifierNode *>(array_expr))
                    {
                        // e.g., entries[k] -> resolve "entries" type from variable_types_map
                        auto &var_types = ctx().variable_types_map();
                        auto it = var_types.find(arr_id->name());
                        if (it != var_types.end() && it->second.is_valid())
                        {
                            TypeRef var_type = it->second;
                            if (var_type->kind() == Cryo::TypeKind::Array)
                            {
                                auto *arr_type = dynamic_cast<const Cryo::ArrayType *>(var_type.get());
                                if (arr_type && arr_type->element().is_valid())
                                {
                                    array_type_name = arr_type->element()->display_name();
                                    if (!array_type_name.empty() && array_type_name.back() == '*')
                                        array_type_name.pop_back();
                                }
                            }
                        }
                    }

                    if (!array_type_name.empty())
                    {
                        type_name = array_type_name;

                        // Qualify with source namespace if unqualified
                        if (type_name.find("::") == std::string::npos)
                        {
                            if (auto *template_reg = ctx().template_registry())
                            {
                                auto candidates = generate_lookup_candidates(type_name, Cryo::SymbolKind::Type);
                                for (const auto &cand : candidates)
                                {
                                    const TemplateRegistry::StructFieldInfo *fi = template_reg->get_struct_field_types(cand);
                                    if (fi && !fi->source_namespace.empty())
                                    {
                                        type_name = fi->source_namespace + "::" + type_name;
                                        break;
                                    }
                                }
                            }
                        }

                        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                  "Resolved array-indexed receiver type: {}", type_name);
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
                                    if (inner_id->name() == "this")
                                    {
                                        inner_receiver_type = ctx().current_type_name();
                                    }
                                    else
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

                                    // If LLVM type didn't give us a name (e.g., enums are i32),
                                    // try TemplateRegistry method return type annotation
                                    if (type_name.empty())
                                    {
                                        if (auto *template_reg = ctx().template_registry())
                                        {
                                            auto type_candidates = generate_lookup_candidates(inner_receiver_type, Cryo::SymbolKind::Type);
                                            for (const auto &cand : type_candidates)
                                            {
                                                std::string qualified = cand + "::" + inner_method_name;
                                                std::string ret_annotation = template_reg->get_method_return_type_annotation(qualified);
                                                if (!ret_annotation.empty())
                                                {
                                                    type_name = ret_annotation;
                                                    if (!type_name.empty() && type_name.back() == '*')
                                                        type_name.pop_back();
                                                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                                              "Inferred chained call return type from annotation: {}", type_name);
                                                    break;
                                                }

                                                // Also try find_template_method
                                                const TemplateRegistry::MethodMetadata *meta =
                                                    template_reg->find_template_method(cand, inner_method_name);
                                                if (meta && !meta->return_type_annotation.empty())
                                                {
                                                    type_name = meta->return_type_annotation;
                                                    if (!type_name.empty() && type_name.back() == '*')
                                                        type_name.pop_back();
                                                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                                              "Inferred chained call return type from template method: {}", type_name);
                                                    break;
                                                }
                                            }
                                        }
                                    }
                                }
                                else
                                {
                                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                              "Chained call fallback: Could not find inner method '{}.{}'",
                                              inner_receiver_type, inner_method_name);

                                    // Even without LLVM function, try TemplateRegistry
                                    if (auto *template_reg = ctx().template_registry())
                                    {
                                        auto type_candidates = generate_lookup_candidates(inner_receiver_type, Cryo::SymbolKind::Type);
                                        for (const auto &cand : type_candidates)
                                        {
                                            std::string qualified = cand + "::" + inner_method_name;
                                            std::string ret_annotation = template_reg->get_method_return_type_annotation(qualified);
                                            if (!ret_annotation.empty())
                                            {
                                                type_name = ret_annotation;
                                                if (!type_name.empty() && type_name.back() == '*')
                                                    type_name.pop_back();
                                                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                                          "Inferred chained call return type from annotation (no LLVM fn): {}", type_name);
                                                break;
                                            }

                                            const TemplateRegistry::MethodMetadata *meta =
                                                template_reg->find_template_method(cand, inner_method_name);
                                            if (meta && !meta->return_type_annotation.empty())
                                            {
                                                type_name = meta->return_type_annotation;
                                                if (!type_name.empty() && type_name.back() == '*')
                                                    type_name.pop_back();
                                                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                                          "Inferred chained call return type from template method (no LLVM fn): {}", type_name);
                                                break;
                                            }
                                        }
                                    }
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

        // Resolve the method — try overloaded name first (with param type suffix),
        // then fall back to generic resolution.
        llvm::Function *method = nullptr;

        // Build overload suffix from call argument types to find the exact overload.
        // This handles methods like visit(ProgramNode*) vs visit(ExpressionNode*)
        // which would otherwise be indistinguishable (all ptrs in opaque pointer mode).
        if (!node->arguments().empty())
        {
            std::string overload_suffix = "(";
            bool all_types_resolved = true;
            for (size_t i = 0; i < node->arguments().size(); ++i)
            {
                if (i > 0)
                    overload_suffix += ",";

                std::string type_str;
                TypeRef arg_type = node->arguments()[i]->get_resolved_type();

                // For 'this' identifiers without resolved type, use the enclosing
                // class/struct type name.  This is critical for overloaded virtual
                // methods like visitor.visit(this) where 'this' type determines
                // which visit() overload to call.
                if (!arg_type.is_valid())
                {
                    auto *id_node = dynamic_cast<Cryo::IdentifierNode *>(node->arguments()[i].get());
                    if (id_node && id_node->name() == "this")
                    {
                        const std::string &current_type = ctx().current_type_name();
                        if (!current_type.empty())
                        {
                            type_str = current_type + "*";
                        }
                    }
                }

                // For cast expressions (e.g., `node as ProgramNode*`), use the cast's
                // target type for overload disambiguation instead of the original type.
                if (!arg_type.is_valid() && type_str.empty())
                {
                    auto *cast_expr = dynamic_cast<Cryo::CastExpressionNode *>(node->arguments()[i].get());
                    if (cast_expr)
                    {
                        arg_type = cast_expr->get_resolved_target_type();
                        if (!arg_type.is_valid() && cast_expr->has_target_type_annotation())
                        {
                            type_str = cast_expr->target_type_annotation()->to_string();
                        }
                    }
                }

                if (!type_str.empty())
                {
                    overload_suffix += type_str;
                }
                else if (arg_type.is_valid())
                {
                    overload_suffix += arg_type->display_name();
                }
                else
                {
                    all_types_resolved = false;
                    break;
                }
            }
            overload_suffix += ")";

            if (all_types_resolved)
            {
                auto type_candidates = generate_lookup_candidates(type_name, Cryo::SymbolKind::Type);
                type_candidates.insert(type_candidates.begin(), type_name);
                for (const auto &tc : type_candidates)
                {
                    std::string overloaded_name = tc + "::" + method_name + overload_suffix;
                    if (llvm::Function *fn = module()->getFunction(overloaded_name))
                    {
                        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                  "generate_instance_method: Found overloaded method '{}'",
                                  overloaded_name);
                        method = fn;
                        break;
                    }
                }
            }
        }

        // Fall back to generic resolution (handles non-overloaded methods)
        if (!method)
            method = resolve_method(type_name, method_name);
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

            // Walk the class inheritance chain to find methods defined on a base class
            if (!method)
            {
                TypeRef class_type_ref = ctx().symbols().lookup_class_type(type_name);
                if (class_type_ref.is_valid())
                {
                    auto *cls = dynamic_cast<const Cryo::ClassType *>(class_type_ref.get());
                    if (cls && cls->has_base_class())
                    {
                        auto *walk = dynamic_cast<const Cryo::ClassType *>(cls->base_class().get());
                        while (walk && !method)
                        {
                            std::string base_name = walk->name();
                            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                      "generate_instance_method: trying base class '{}' for method '{}'",
                                      base_name, method_name);
                            method = resolve_method(base_name, method_name);
                            walk = walk->has_base_class()
                                       ? dynamic_cast<const Cryo::ClassType *>(walk->base_class().get())
                                       : nullptr;
                        }
                    }
                }
            }
        }

        if (!method || method->empty())
        {
            // Try on-demand instantiation of generic methods
            // (e.g., map<U> on Maybe_i32 called as b.map(double))
            // This also handles cases where resolve_method created an extern declaration
            // with no body for a doubly-generic method like map<U> on Maybe<T>
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "generate_instance_method: method '{}' on type '{}' is {} - trying on-demand instantiation",
                      method_name, type_name,
                      method ? "declared but empty (no body)" : "null");
            llvm::Function *instantiated = try_instantiate_generic_method(node, type_name, method_name);
            if (instantiated)
                method = instantiated;

        }

        if (!method)
        {
            // Before reporting an error, check if the member is a function-pointer field
            // on the struct (e.g., this.write_fn(...) where write_fn is a field of type (void*, u8*, u64) -> int).
            // If so, generate an indirect call through the loaded field value.
            int field_idx = ctx().get_struct_field_index(type_name, method_name);
            if (field_idx >= 0)
            {
                // Look up the Cryo TypeRef for this field to check if it's a FunctionType
                TypeRef field_cryo_type;
                if (auto *template_reg = ctx().template_registry())
                {
                    auto candidates = generate_lookup_candidates(type_name, Cryo::SymbolKind::Type);
                    candidates.insert(candidates.begin(), type_name);
                    for (const auto &candidate : candidates)
                    {
                        const TemplateRegistry::StructFieldInfo *field_info = template_reg->get_struct_field_types(candidate);
                        if (field_info)
                        {
                            // Use name-based lookup to avoid vtable pointer offset mismatch
                            field_cryo_type = field_info->get_field_type(method_name);
                            if (field_cryo_type.is_valid())
                                break;
                        }
                    }
                }

                if (field_cryo_type.is_valid() && field_cryo_type->kind() == Cryo::TypeKind::Function)
                {
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "generate_instance_method: '{}' is a function-pointer field on '{}', generating indirect call",
                              method_name, type_name);

                    auto *func_type = static_cast<const Cryo::FunctionType *>(field_cryo_type.get());

                    // Build the LLVM function type from the Cryo FunctionType
                    llvm::Type *ret_llvm_type = llvm::Type::getVoidTy(llvm_ctx());
                    TypeRef ret_type_ref = func_type->return_type();
                    if (ret_type_ref.is_valid())
                    {
                        llvm::Type *mapped = types().map(ret_type_ref);
                        if (mapped)
                            ret_llvm_type = mapped;
                    }

                    std::vector<llvm::Type *> param_llvm_types;
                    for (const auto &param : func_type->param_types())
                    {
                        llvm::Type *mapped = param.is_valid() ? types().map(param) : llvm::PointerType::get(llvm_ctx(), 0);
                        param_llvm_types.push_back(mapped ? mapped : llvm::PointerType::get(llvm_ctx(), 0));
                    }

                    llvm::FunctionType *fn_type = llvm::FunctionType::get(ret_llvm_type, param_llvm_types, false);

                    // GEP to the field and load the function pointer
                    llvm::StructType *struct_type = nullptr;
                    if (llvm::Type *t = ctx().get_type(type_name))
                        struct_type = llvm::dyn_cast<llvm::StructType>(t);
                    if (!struct_type && receiver->getType()->isPointerTy())
                    {
                        // Try looking up via LLVM module types
                        struct_type = llvm::StructType::getTypeByName(llvm_ctx(), type_name);
                    }

                    if (struct_type)
                    {
                        llvm::Value *field_ptr = builder().CreateStructGEP(struct_type, receiver, field_idx, method_name + ".ptr");
                        llvm::Value *fn_ptr = builder().CreateLoad(llvm::PointerType::get(llvm_ctx(), 0), field_ptr, method_name + ".load");

                        // Build arguments (no 'this' prepended - these are direct args to the function pointer)
                        std::vector<llvm::Value *> call_args;
                        for (size_t i = 0; i < args.size(); ++i)
                        {
                            llvm::Value *arg = args[i];
                            if (i < param_llvm_types.size() && arg->getType() != param_llvm_types[i])
                            {
                                if (arg->getType()->isPointerTy() && param_llvm_types[i]->isPointerTy())
                                    ; // ptr-to-ptr is fine
                                else if (arg->getType()->isIntegerTy() && param_llvm_types[i]->isIntegerTy())
                                    arg = builder().CreateIntCast(arg, param_llvm_types[i], true, "arg.cast");
                            }
                            call_args.push_back(arg);
                        }

                        return builder().CreateCall(fn_type, fn_ptr, call_args,
                                                    ret_llvm_type->isVoidTy() ? "" : method_name + ".result");
                    }
                }
            }

            // Check if this is a built-in array method (push, pop)
            // Detect array types by type_name ending with "[]" (e.g., "i32[]", "string[]")
            // This is more robust than checking TypeRef, which may be invalid after AST cloning
            if (type_name.size() > 2 && type_name.compare(type_name.size() - 2, 2, "[]") == 0)
            {
                // First try TypeRef (most reliable when available)
                TypeRef obj_type_ref;
                if (callee && callee->object())
                    obj_type_ref = callee->object()->get_resolved_type();

                if (obj_type_ref.is_valid() && obj_type_ref->kind() == Cryo::TypeKind::Array)
                {
                    llvm::Value *result = generate_builtin_array_method(node, receiver, method_name, args, obj_type_ref);
                    if (result)
                        return result;
                }
                else
                {
                    // TypeRef not available (cross-module generic instantiation) -
                    // resolve element type from the type_name string
                    std::string elem_name = type_name.substr(0, type_name.size() - 2);
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "Built-in array method: TypeRef unavailable, resolving element type from string '{}'",
                              elem_name);

                    // Map element type name to LLVM type
                    llvm::Type *elem_type = nullptr;
                    if (elem_name == "i8" || elem_name == "u8") elem_type = types().i8_type();
                    else if (elem_name == "i16" || elem_name == "u16") elem_type = types().i16_type();
                    else if (elem_name == "i32" || elem_name == "u32" || elem_name == "int" || elem_name == "uint") elem_type = types().i32_type();
                    else if (elem_name == "i64" || elem_name == "u64") elem_type = types().i64_type();
                    else if (elem_name == "i128" || elem_name == "u128") elem_type = llvm::Type::getInt128Ty(llvm_ctx());
                    else if (elem_name == "f32" || elem_name == "float") elem_type = llvm::Type::getFloatTy(llvm_ctx());
                    else if (elem_name == "f64" || elem_name == "double") elem_type = llvm::Type::getDoubleTy(llvm_ctx());
                    else if (elem_name == "boolean" || elem_name == "bool") elem_type = types().bool_type();
                    else if (elem_name == "string") elem_type = types().ptr_type();
                    else if (elem_name == "char") elem_type = types().i8_type();
                    else if (elem_name.size() > 1 && elem_name.back() == '*')
                    {
                        // Pointer element type (e.g., "GenericParamNode*") — all pointers are opaque ptr
                        elem_type = types().ptr_type();
                    }
                    else
                    {
                        // Try looking up as a struct type in the LLVM module
                        elem_type = llvm::StructType::getTypeByName(llvm_ctx(), elem_name);
                        if (!elem_type)
                        {
                            // Try with common namespace prefixes
                            auto candidates = generate_lookup_candidates(elem_name, Cryo::SymbolKind::Type);
                            for (const auto &c : candidates)
                            {
                                elem_type = llvm::StructType::getTypeByName(llvm_ctx(), c);
                                if (elem_type) break;
                            }
                        }
                        // If the found type is an opaque (unsized) struct, it can't be used
                        // for array element sizing.  Fall back to pointer type.
                        if (elem_type)
                        {
                            if (auto *st = llvm::dyn_cast<llvm::StructType>(elem_type))
                            {
                                if (st->isOpaque())
                                {
                                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                              "Built-in array method: found opaque struct '{}' for element type '{}', using ptr fallback",
                                              st->hasName() ? st->getName().str() : "<anon>", elem_name);
                                    elem_type = types().ptr_type();
                                }
                            }
                        }
                        if (!elem_type)
                            elem_type = types().ptr_type(); // fallback: treat as pointer-sized
                    }

                    // Look up the Array<T> struct type
                    llvm::StructType *array_struct = llvm::dyn_cast_or_null<llvm::StructType>(
                        llvm::StructType::getTypeByName(llvm_ctx(), "Array<" + elem_name + ">"));
                    if (!array_struct)
                    {
                        // Try alternate naming: normalize int->i32 etc.
                        std::string normalized = elem_name;
                        if (normalized == "int") normalized = "i32";
                        else if (normalized == "uint") normalized = "u32";
                        else if (normalized == "float") normalized = "f32";
                        else if (normalized == "double") normalized = "f64";
                        else if (normalized == "boolean") normalized = "bool";
                        array_struct = llvm::dyn_cast_or_null<llvm::StructType>(
                            llvm::StructType::getTypeByName(llvm_ctx(), "Array<" + normalized + ">"));
                    }
                    // For pointer element types or missing array structs, create on demand.
                    // All dynamic arrays have the same layout: { ptr, i64, i64 }
                    if (!array_struct && elem_type)
                    {
                        std::string array_name = "Array<" + elem_name + ">";
                        std::vector<llvm::Type *> fields = {
                            llvm::PointerType::get(llvm_ctx(), 0),  // elements: T*
                            llvm::Type::getInt64Ty(llvm_ctx()),      // length
                            llvm::Type::getInt64Ty(llvm_ctx())       // capacity
                        };
                        array_struct = llvm::StructType::create(llvm_ctx(), fields, array_name);
                    }

                    if (elem_type && array_struct)
                    {
                        llvm::Value *result = generate_builtin_array_method_raw(
                            node, receiver, method_name, args, elem_type, array_struct);
                        if (result)
                            return result;
                    }
                }
            }

            // Report error if method not found and not a function-pointer field
            {
                std::string msg = type_name.empty()
                    ? "no method '" + method_name + "' found"
                    : "no method '" + method_name + "' found for type '" + type_name + "'";
                auto diag = Diag::error(ErrorCode::E0636_UNDEFINED_FUNCTION_CALL, msg);
                diag.at(node);
                add_type_derivation_context(diag, callee->object());
                diag.with_note("looked up as instance method on '" + type_name + "'");
                emit_diagnostic(std::move(diag));
            }
            return nullptr;
        }

        // Check for virtual dispatch: if the method is virtual/override on a class type,
        // call indirectly through the vtable instead of directly
        {
            TypeRef cryo_type = ctx().symbols().lookup_class_type(type_name);
            // Try unqualified name if qualified lookup fails (e.g., "Namespace::Class" -> "Class")
            if (!cryo_type.is_valid())
            {
                auto last_sep = type_name.rfind("::");
                if (last_sep != std::string::npos)
                    cryo_type = ctx().symbols().lookup_class_type(type_name.substr(last_sep + 2));
            }
            if (cryo_type.is_valid())
            {
                auto *cryo_class = dynamic_cast<const Cryo::ClassType *>(cryo_type.get());
                if (cryo_class && cryo_class->needs_vtable_pointer())
                {
                    // Extract overload suffix from the resolved method's LLVM name.
                    // E.g., "ASTVisitor::visit(ProgramNode*)" → "(ProgramNode*)"
                    //
                    // IMPORTANT: Use find('(') on the full name, NOT rfind("::")
                    // followed by find('(').  When parameter types contain '::'
                    // (e.g., "Ns::Class::visit(Ns::ProgramNode*)"), rfind("::")
                    // finds the '::' INSIDE the parameter type name, leaving
                    // after_sep = "ProgramNode*)" with no '(' — so the suffix
                    // comes out empty, vtable_index() returns -1, and virtual
                    // dispatch is silently skipped (falling through to a direct
                    // call to the base class stub).
                    //
                    // The first '(' in the function name is always the start of
                    // the parameter list, since the format is:
                    //   [Namespace::]ClassName::methodName(ParamType1,ParamType2,...)
                    std::string vtable_overload_suffix;
                    if (method)
                    {
                        std::string fn_name = method->getName().str();
                        size_t paren = fn_name.find('(');
                        if (paren != std::string::npos)
                        {
                            vtable_overload_suffix = fn_name.substr(paren);
                        }
                    }

                    int vtable_idx = cryo_class->vtable_index(method_name, vtable_overload_suffix);
                    if (vtable_idx >= 0)
                    {
                        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                  "Virtual dispatch: method '{}' on class '{}' at vtable index {}",
                                  method_name, type_name, vtable_idx);

                        // Get the LLVM class type
                        llvm::StructType *class_llvm_type =
                            llvm::StructType::getTypeByName(llvm_ctx(), type_name);

                        // Also try base class types for proper vtable lookup
                        if (!class_llvm_type)
                        {
                            // Walk up the hierarchy
                            auto *walk = cryo_class;
                            while (walk && !class_llvm_type)
                            {
                                class_llvm_type = llvm::StructType::getTypeByName(llvm_ctx(), walk->name());
                                if (walk->has_base_class())
                                {
                                    walk = dynamic_cast<const Cryo::ClassType *>(walk->base_class().get());
                                }
                                else
                                {
                                    break;
                                }
                            }
                        }

                        if (class_llvm_type)
                        {
                            llvm::Type *ptr_ty = llvm::PointerType::get(llvm_ctx(), 0);

                            // Load vtable pointer (field 0 of the object)
                            llvm::Value *vptr_gep = builder().CreateStructGEP(
                                class_llvm_type, receiver, 0, "vptr.gep");
                            llvm::Value *vtable = builder().CreateLoad(ptr_ty, vptr_gep, "vtable.load");

                            // Get vtable type
                            std::string vtable_type_name = "VTable." + type_name;
                            llvm::StructType *vtable_type =
                                llvm::StructType::getTypeByName(llvm_ctx(), vtable_type_name);

                            // If vtable type not found with qualified name, try simple class name
                            if (!vtable_type)
                            {
                                vtable_type = llvm::StructType::getTypeByName(
                                    llvm_ctx(), "VTable." + cryo_class->name());
                            }
                            // If still not found, walk up hierarchy
                            if (!vtable_type && cryo_class->has_base_class())
                            {
                                auto *walk = cryo_class;
                                while (walk && !vtable_type)
                                {
                                    vtable_type = llvm::StructType::getTypeByName(
                                        llvm_ctx(), "VTable." + walk->name());
                                    if (walk->has_base_class())
                                    {
                                        walk = dynamic_cast<const Cryo::ClassType *>(walk->base_class().get());
                                    }
                                    else
                                    {
                                        break;
                                    }
                                }
                            }

                            if (vtable_type)
                            {
                                // GEP into vtable for the method at vtable_idx
                                llvm::Value *method_ptr = builder().CreateStructGEP(
                                    vtable_type, vtable, vtable_idx, "vmethod.gep");
                                llvm::Value *method_fn = builder().CreateLoad(
                                    ptr_ty, method_ptr, "vmethod.load");

                                // Build call arguments: this + args
                                llvm::FunctionType *fn_type = method->getFunctionType();
                                std::vector<llvm::Value *> call_args;
                                call_args.push_back(receiver);
                                for (size_t i = 0; i < args.size(); ++i)
                                {
                                    size_t param_idx = i + 1;
                                    if (param_idx < fn_type->getNumParams())
                                    {
                                        call_args.push_back(
                                            cast_if_needed(args[i], fn_type->getParamType(param_idx)));
                                    }
                                    else
                                    {
                                        call_args.push_back(args[i]);
                                    }
                                }

                                std::string result_name = fn_type->getReturnType()->isVoidTy()
                                    ? "" : method_name + ".vresult";
                                return builder().CreateCall(fn_type, method_fn, call_args, result_name);
                            }
                        }
                    }
                }
            }
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
            // Handle string method calls where receiver is a raw string value (char*).
            // Methods with &this on string expect a pointer TO the string (char**),
            // but the receiver might be the string value itself (e.g., extracted from
            // a struct field or register-passed parameter like tok.lexeme.length()).
            // Both are ptr in opaque pointer mode.
            //
            // Detection: check the method function name for "::string::" which indicates
            // a method on the primitive string type. AST resolved types may not be set
            // on intermediate MemberAccess nodes, so we use the function name instead.
            else if (this_param_type->isPointerTy() && receiver->getType()->isPointerTy())
            {
                bool is_string_method = false;

                // Check if this is a method on the PRIMITIVE string type (char*),
                // not the String STRUCT type. Extract the type name from the
                // qualified method name:
                //   "std::core::primitives::string::length" -> type = "string" (primitive)
                //   "std::collections::string::String::length" -> type = "String" (struct)
                std::string fn_name = method->getName().str();
                {
                    size_t last_sep = fn_name.rfind("::");
                    if (last_sep != std::string::npos)
                    {
                        std::string before = fn_name.substr(0, last_sep);
                        size_t type_sep = before.rfind("::");
                        std::string type_name = (type_sep != std::string::npos)
                            ? before.substr(type_sep + 2) : before;
                        if (type_name == "string")
                            is_string_method = true;
                    }
                }

                // Also check AST type as a fallback
                if (!is_string_method && callee && callee->object())
                {
                    TypeRef obj_type = callee->object()->get_resolved_type();
                    if (obj_type.is_valid() && obj_type->kind() == Cryo::TypeKind::String)
                        is_string_method = true;
                }

                if (is_string_method)
                {
                    // Receiver is a string value but method expects &string (pointer to string).
                    // Check if receiver is already an address (alloca/GEP) — if so, it's correct.
                    bool is_address = llvm::isa<llvm::AllocaInst>(receiver) ||
                                      llvm::isa<llvm::GetElementPtrInst>(receiver) ||
                                      llvm::isa<llvm::GlobalVariable>(receiver);
                    if (!is_address)
                    {
                        // If the receiver was loaded from a local variable's alloca, pass the
                        // alloca directly. This is critical for mut &this methods like append():
                        // the method stores the new string pointer back through the &this param,
                        // so it must point to the original variable's storage, not a temporary copy.
                        if (auto *load_inst = llvm::dyn_cast<llvm::LoadInst>(receiver))
                        {
                            if (auto *source_alloca = llvm::dyn_cast<llvm::AllocaInst>(load_inst->getPointerOperand()))
                            {
                                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                          "generate_instance_method: Using source alloca for string &this method '{}'"
                                          " (preserves mutation semantics)",
                                          fn_name);
                                this_arg = source_alloca;
                            }
                            else
                            {
                                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                          "generate_instance_method: Materializing string receiver for &this method '{}'"
                                          " (receiver loaded from non-alloca)",
                                          fn_name);
                                llvm::AllocaInst *tmp = create_entry_alloca(receiver->getType(), "string.this.tmp");
                                builder().CreateStore(receiver, tmp);
                                this_arg = tmp;
                            }
                        }
                        else
                        {
                            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                      "generate_instance_method: Materializing string receiver for &this method '{}'"
                                      " (receiver is non-addressable ptr)",
                                      fn_name);
                            llvm::AllocaInst *tmp = create_entry_alloca(receiver->getType(), "string.this.tmp");
                            builder().CreateStore(receiver, tmp);
                            this_arg = tmp;
                        }
                    }
                }
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

                // Handle raw [N x T] array → Array<T> struct conversion
                if (arg && param_type->isStructTy() && arg->getType()->isPointerTy())
                {
                    llvm::StructType *param_st = llvm::cast<llvm::StructType>(param_type);
                    std::string st_name = param_st->hasName() ? param_st->getName().str() : "";
                    if (st_name.find("Array<") != std::string::npos)
                    {
                        llvm::AllocaInst *alloca_arg = llvm::dyn_cast<llvm::AllocaInst>(arg);
                        if (alloca_arg && alloca_arg->getAllocatedType()->isArrayTy())
                        {
                            llvm::ArrayType *arr_ty = llvm::cast<llvm::ArrayType>(alloca_arg->getAllocatedType());
                            uint64_t num_elems = arr_ty->getNumElements();
                            auto &DL = module()->getDataLayout();
                            uint64_t elem_size = DL.getTypeAllocSize(arr_ty->getElementType());
                            uint64_t total_bytes = num_elems * elem_size;

                            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                      "generate_instance_method: Wrapping raw [{}x T] array into {} struct (heap copy, {} bytes)",
                                      num_elems, st_name, total_bytes);

                            // Heap-allocate so data survives beyond the current stack frame
                            llvm::Function *malloc_fn = module()->getFunction("malloc");
                            if (!malloc_fn)
                            {
                                llvm::FunctionType *malloc_type = llvm::FunctionType::get(
                                    llvm::PointerType::get(llvm_ctx(), 0),
                                    {llvm::Type::getInt64Ty(llvm_ctx())}, false);
                                malloc_fn = llvm::Function::Create(malloc_type, llvm::Function::ExternalLinkage,
                                                                    "malloc", module());
                            }
                            llvm::Value *size_val = llvm::ConstantInt::get(llvm::Type::getInt64Ty(llvm_ctx()), total_bytes);
                            llvm::Value *heap_ptr = builder().CreateCall(malloc_fn, {size_val}, "array.heap");
                            builder().CreateMemCpy(heap_ptr, llvm::MaybeAlign(1),
                                                    alloca_arg, llvm::MaybeAlign(1), size_val);

                            llvm::AllocaInst *wrapper = create_entry_alloca(param_st, "array.wrap");
                            llvm::Value *f0 = builder().CreateStructGEP(param_st, wrapper, 0, "array.wrap.data");
                            builder().CreateStore(heap_ptr, f0);
                            llvm::Value *len_val = llvm::ConstantInt::get(llvm::Type::getInt64Ty(llvm_ctx()), num_elems);
                            llvm::Value *f1 = builder().CreateStructGEP(param_st, wrapper, 1, "array.wrap.len");
                            builder().CreateStore(len_val, f1);
                            llvm::Value *f2 = builder().CreateStructGEP(param_st, wrapper, 2, "array.wrap.cap");
                            builder().CreateStore(len_val, f2);
                            arg = create_load(wrapper, param_st, "array.wrapped");
                        }
                    }
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

                // Handle raw [N x T] array → Array<T> struct conversion
                // When an array literal produces a raw [N x T] alloca but the parameter
                // expects an Array<T> struct { ptr, i64, i64 }, wrap it.
                if (arg && param_type->isStructTy() && arg->getType()->isPointerTy())
                {
                    llvm::StructType *param_st = llvm::cast<llvm::StructType>(param_type);
                    std::string st_name = param_st->hasName() ? param_st->getName().str() : "";
                    if (st_name.find("Array<") != std::string::npos)
                    {
                        // Check if the argument is a pointer to a raw [N x T] array
                        llvm::AllocaInst *alloca_arg = llvm::dyn_cast<llvm::AllocaInst>(arg);
                        if (alloca_arg)
                        {
                            llvm::Type *alloc_type = alloca_arg->getAllocatedType();
                            if (alloc_type->isArrayTy())
                            {
                                llvm::ArrayType *arr_ty = llvm::cast<llvm::ArrayType>(alloc_type);
                                uint64_t num_elems = arr_ty->getNumElements();
                                auto &DL = module()->getDataLayout();
                                uint64_t elem_size = DL.getTypeAllocSize(arr_ty->getElementType());
                                uint64_t total_bytes = num_elems * elem_size;

                                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                          "generate_free_function: Wrapping raw [{}x T] array into {} struct (heap copy, {} bytes)",
                                          num_elems, st_name, total_bytes);

                                // Heap-allocate the data so it survives beyond the current stack frame
                                llvm::Function *malloc_fn = module()->getFunction("malloc");
                                if (!malloc_fn)
                                {
                                    llvm::FunctionType *malloc_type = llvm::FunctionType::get(
                                        llvm::PointerType::get(llvm_ctx(), 0),
                                        {llvm::Type::getInt64Ty(llvm_ctx())}, false);
                                    malloc_fn = llvm::Function::Create(malloc_type, llvm::Function::ExternalLinkage,
                                                                       "malloc", module());
                                }
                                llvm::Value *size_val = llvm::ConstantInt::get(llvm::Type::getInt64Ty(llvm_ctx()), total_bytes);
                                llvm::Value *heap_ptr = builder().CreateCall(malloc_fn, {size_val}, "array.heap");

                                // Copy stack data to heap
                                builder().CreateMemCpy(heap_ptr, llvm::MaybeAlign(1),
                                                       alloca_arg, llvm::MaybeAlign(1), size_val);

                                // Build the Array<T> struct: { ptr, i64 len, i64 cap }
                                llvm::AllocaInst *wrapper = create_entry_alloca(param_st, "array.wrap");
                                llvm::Value *f0 = builder().CreateStructGEP(param_st, wrapper, 0, "array.wrap.data");
                                builder().CreateStore(heap_ptr, f0);
                                llvm::Value *len_val = llvm::ConstantInt::get(llvm::Type::getInt64Ty(llvm_ctx()), num_elems);
                                llvm::Value *f1 = builder().CreateStructGEP(param_st, wrapper, 1, "array.wrap.len");
                                builder().CreateStore(len_val, f1);
                                llvm::Value *f2 = builder().CreateStructGEP(param_st, wrapper, 2, "array.wrap.cap");
                                builder().CreateStore(len_val, f2);

                                // Load the struct value to pass by value
                                arg = create_load(wrapper, param_st, "array.wrapped");
                            }
                        }
                    }
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
                    // Construct the qualified name to match the actual function definition.
                    // The symbol's scope field contains the original module namespace where
                    // the function was defined (e.g., "Utils", "Baz::Qix", "App::Core::Math").
                    // We must use that scope, NOT the current file's namespace context,
                    // to produce LLVM names that match the function definitions.
                    std::string symbol_scope = found_symbol->scope;
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
                    else if (!symbol_scope.empty() && symbol_scope != "Global" && symbol_scope != ns_context)
                    {
                        // Cross-module function: use the symbol's original scope (the defining module's namespace)
                        llvm_name = symbol_scope + "::" + simple_name;
                    }
                    else if (!ns_context.empty())
                    {
                        // Same-module function: use current namespace context
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

        if (name == "sscanf")
        {
            llvm::Function *fn = module()->getFunction("sscanf");
            if (!fn)
            {
                llvm::FunctionType *fn_type = llvm::FunctionType::get(i32_type, {ptr_type, ptr_type}, true);
                fn = llvm::Function::Create(fn_type, llvm::Function::ExternalLinkage, "sscanf", module());
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

        if (name == "fgets")
        {
            llvm::Function *fn = module()->getFunction("fgets");
            if (!fn)
            {
                llvm::FunctionType *fn_type = llvm::FunctionType::get(ptr_type, {ptr_type, i32_type, ptr_type}, false);
                fn = llvm::Function::Create(fn_type, llvm::Function::ExternalLinkage, "fgets", module());
            }
            return fn;
        }

        if (name == "fputs")
        {
            llvm::Function *fn = module()->getFunction("fputs");
            if (!fn)
            {
                llvm::FunctionType *fn_type = llvm::FunctionType::get(i32_type, {ptr_type, ptr_type}, false);
                fn = llvm::Function::Create(fn_type, llvm::Function::ExternalLinkage, "fputs", module());
            }
            return fn;
        }

        if (name == "fgetc")
        {
            llvm::Function *fn = module()->getFunction("fgetc");
            if (!fn)
            {
                llvm::FunctionType *fn_type = llvm::FunctionType::get(i32_type, {ptr_type}, false);
                fn = llvm::Function::Create(fn_type, llvm::Function::ExternalLinkage, "fgetc", module());
            }
            return fn;
        }

        if (name == "fputc")
        {
            llvm::Function *fn = module()->getFunction("fputc");
            if (!fn)
            {
                llvm::FunctionType *fn_type = llvm::FunctionType::get(i32_type, {i32_type, ptr_type}, false);
                fn = llvm::Function::Create(fn_type, llvm::Function::ExternalLinkage, "fputc", module());
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
                  "resolve_constructor: Looking for constructor of type '{}' (arg_types.size={})",
                  type_name, arg_types.size());

        // Expected param count: arg_types (caller-provided args) + 1 for 'this' pointer
        unsigned expected_params = arg_types.size() + 1;

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
                    // If arg_types is non-empty, verify parameter count matches
                    if (!arg_types.empty() && fn->getFunctionType()->getNumParams() != expected_params)
                    {
                        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                  "resolve_constructor: '{}' has {} params but expected {} — searching for overloaded variant",
                                  ctor_name, fn->getFunctionType()->getNumParams(), expected_params);

                        // Search for overloaded variants: "CtorName(Type1,Type2,...)"
                        for (auto &mod_fn : module()->functions())
                        {
                            llvm::StringRef fn_name = mod_fn.getName();
                            if (fn_name.starts_with(ctor_name) &&
                                fn_name.size() > ctor_name.size() &&
                                fn_name[ctor_name.size()] == '(' &&
                                mod_fn.getFunctionType()->getNumParams() == expected_params)
                            {
                                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                          "resolve_constructor: Found overloaded '{}' with correct param count", fn_name.str());
                                return &mod_fn;
                            }
                        }
                        // Continue to next candidate pattern
                        continue;
                    }

                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "resolve_constructor: Found '{}' in module", ctor_name);
                    return fn;
                }

                // Check context's function registry — but validate the pointer
                // belongs to our module.  Registry entries can become stale during
                // multi-module compilation when functions are replaced.
                fn = ctx().get_function(ctor_name);
                if (fn)
                {
                    if (fn->getParent() != module())
                    {
                        // Stale registry entry — try to re-resolve by the function's
                        // actual name in our module
                        llvm::Function *local_fn = module()->getFunction(fn->getName());
                        if (local_fn)
                            fn = local_fn;
                        else
                        {
                            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                      "resolve_constructor: Registry entry '{}' has stale pointer (wrong module), skipping",
                                      ctor_name);
                            continue;
                        }
                    }

                    if (!arg_types.empty() && fn->getFunctionType()->getNumParams() != expected_params)
                    {
                        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                  "resolve_constructor: '{}' (from registry) has {} params but expected {} — skipping",
                                  ctor_name, fn->getFunctionType()->getNumParams(), expected_params);
                        continue;
                    }

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

        // Check TemplateRegistry for cross-module generic enum templates.
        // This handles the case where a generic function body references an enum
        // by unqualified name (e.g., "Outcome::Ok(value)") but the enum is from
        // another module and not in the local symbol table.
        Cryo::TemplateRegistry *template_registry = non_const_ctx.template_registry();
        if (template_registry)
        {
            const Cryo::TemplateRegistry::TemplateInfo *tmpl_info =
                template_registry->find_template(name);
            if (tmpl_info && tmpl_info->enum_template)
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "is_enum_type: Found '{}' as generic enum template in TemplateRegistry", name);
                return true;
            }
        }

        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "is_enum_type: '{}' not found as enum type", name);
        return false;
    }

    bool CallCodegen::is_enum_variant(const std::string &enum_name, const std::string &member_name) const
    {
        // Try direct symbol lookup for the enum type
        Symbol *sym = const_cast<CallCodegen *>(this)->symbols().lookup_symbol(enum_name);
        if (sym && sym->kind == SymbolKind::Type && sym->type.is_valid() &&
            sym->type->kind() == TypeKind::Enum)
        {
            auto *enum_type = static_cast<const EnumType *>(sym->type.get());
            if (enum_type->variant_index(member_name).has_value())
                return true;
            // If the enum has registered variants but the member isn't one of them,
            // it's a static method — return false definitively.
            // Only fall through to TemplateRegistry when variant_count is 0
            // (generic template placeholder that hasn't had variants populated).
            if (enum_type->variant_count() > 0)
                return false;
        }

        // Also try SRM lookup candidates (handles cross-module / qualified names)
        auto &non_const_ctx = const_cast<CodegenContext &>(ctx());
        auto candidates = non_const_ctx.srm().generate_lookup_candidates(enum_name, Cryo::SymbolKind::Type);
        for (const auto &candidate : candidates)
        {
            Symbol *csym = const_cast<CallCodegen *>(this)->symbols().lookup_symbol(candidate);
            if (csym && csym->kind == SymbolKind::Type && csym->type.is_valid() &&
                csym->type->kind() == TypeKind::Enum)
            {
                auto *enum_type = static_cast<const EnumType *>(csym->type.get());
                if (enum_type->variant_index(member_name).has_value())
                    return true;
                if (enum_type->variant_count() > 0)
                    return false;
            }
        }

        // Check TemplateRegistry for generic enum templates
        Cryo::TemplateRegistry *template_registry = non_const_ctx.template_registry();
        if (template_registry)
        {
            const Cryo::TemplateRegistry::TemplateInfo *tmpl_info =
                template_registry->find_template(enum_name);
            if (tmpl_info && tmpl_info->enum_template)
            {
                // Check the AST node's variants
                auto *enum_decl = tmpl_info->enum_template;
                for (const auto &v : enum_decl->variants())
                {
                    if (v->name() == member_name)
                        return true;
                }
                return false;
            }
        }

        // When we can't find the enum type definition to check variants,
        // conservatively return true to preserve existing behavior
        return true;
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

        // Try to get the alloca (pointer) for identifiers instead of loading the value.
        // For struct variables, generate_expression loads the struct value, but we need
        // a pointer to GEP into the struct fields.
        llvm::Value *object = nullptr;
        if (auto *identifier = dynamic_cast<Cryo::IdentifierNode *>(node->object()))
        {
            std::string var_name = identifier->name();
            if (llvm::AllocaInst *alloca = values().get_alloca(var_name))
            {
                llvm::Type *alloca_type = alloca->getAllocatedType();
                if (alloca_type->isPointerTy())
                {
                    // The alloca stores a pointer (like 'this') - load to get the pointer value
                    object = create_load(alloca, alloca_type, var_name + ".load");
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "generate_member_receiver_address: Loaded pointer from alloca for '{}'", var_name);
                }
                else
                {
                    // The alloca stores a by-value struct - use alloca address directly
                    object = alloca;
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                              "generate_member_receiver_address: Using alloca for identifier '{}'", var_name);
                }
            }
            else if (llvm::GlobalVariable *global = module()->getGlobalVariable(var_name))
            {
                object = global;
            }
        }

        // Fallback to generating the expression
        if (!object)
        {
            object = generate_expression(node->object());
        }

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
                      "generate_member_receiver_address: Object is not a pointer type (type: {})",
                      object->getType()->getTypeID());
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

            // Handle InstantiatedType - use the monomorphized name
            // This ensures we get "Outer_i32" instead of "Outer<i32>"
            if (obj_type->kind() == Cryo::TypeKind::InstantiatedType)
            {
                auto *inst_type = static_cast<const Cryo::InstantiatedType *>(obj_type.get());
                if (inst_type->has_resolved_type())
                {
                    type_name = inst_type->resolved_type()->display_name();
                }
                else if (inst_type->generic_base().is_valid())
                {
                    std::string base = inst_type->generic_base()->display_name();
                    std::string mangled = base;
                    const auto &args = inst_type->type_args();
                    if (!args.empty())
                    {
                        mangled += "_";
                        for (size_t i = 0; i < args.size(); ++i)
                        {
                            if (i > 0) mangled += "_";
                            if (args[i].is_valid())
                            {
                                std::string arg_name = args[i]->display_name();
                                std::replace(arg_name.begin(), arg_name.end(), '<', '_');
                                std::replace(arg_name.begin(), arg_name.end(), '>', '_');
                                std::replace(arg_name.begin(), arg_name.end(), ',', '_');
                                std::replace(arg_name.begin(), arg_name.end(), ' ', '_');
                                std::replace(arg_name.begin(), arg_name.end(), '*', 'p');
                                mangled += arg_name;
                            }
                        }
                    }
                    type_name = mangled;
                }
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "generate_member_receiver_address: Using InstantiatedType name: {}",
                          type_name);
            }
            // Handle pointer types - get the pointee type name
            else if (obj_type->kind() == Cryo::TypeKind::Pointer)
            {
                auto *ptr_type = dynamic_cast<const Cryo::PointerType *>(obj_type.get());
                if (ptr_type && ptr_type->pointee().is_valid())
                {
                    type_name = ptr_type->pointee()->display_name();
                }
            }
            // Handle reference types - get the referent type name
            else if (obj_type->kind() == Cryo::TypeKind::Reference)
            {
                auto *ref_type = dynamic_cast<const Cryo::ReferenceType *>(obj_type.get());
                if (ref_type && ref_type->referent().is_valid())
                {
                    type_name = ref_type->referent()->display_name();
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
                    Cryo::TypeRef resolved_var_type = it->second;
                    // Unwrap reference types to get the referent
                    if (resolved_var_type->kind() == Cryo::TypeKind::Reference)
                    {
                        auto *ref_type = dynamic_cast<const Cryo::ReferenceType *>(resolved_var_type.get());
                        if (ref_type && ref_type->referent().is_valid())
                        {
                            resolved_var_type = ref_type->referent();
                        }
                    }
                    // Unwrap pointer types to get the pointee
                    else if (resolved_var_type->kind() == Cryo::TypeKind::Pointer)
                    {
                        auto *ptr_type = dynamic_cast<const Cryo::PointerType *>(resolved_var_type.get());
                        if (ptr_type && ptr_type->pointee().is_valid())
                        {
                            resolved_var_type = ptr_type->pointee();
                        }
                    }
                    type_name = resolved_var_type->display_name();
                    // Handle InstantiatedType
                    if (resolved_var_type->kind() == Cryo::TypeKind::InstantiatedType)
                    {
                        auto *inst = static_cast<const Cryo::InstantiatedType *>(resolved_var_type.get());
                        if (inst->has_resolved_type())
                        {
                            type_name = inst->resolved_type()->display_name();
                        }
                        else if (inst->generic_base().is_valid())
                        {
                            std::string base = inst->generic_base()->display_name();
                            std::string mangled = base;
                            const auto &args = inst->type_args();
                            if (!args.empty())
                            {
                                mangled += "_";
                                for (size_t i = 0; i < args.size(); ++i)
                                {
                                    if (i > 0) mangled += "_";
                                    if (args[i].is_valid())
                                    {
                                        std::string arg_name = args[i]->display_name();
                                        std::replace(arg_name.begin(), arg_name.end(), '<', '_');
                                        std::replace(arg_name.begin(), arg_name.end(), '>', '_');
                                        std::replace(arg_name.begin(), arg_name.end(), ',', '_');
                                        std::replace(arg_name.begin(), arg_name.end(), ' ', '_');
                                        std::replace(arg_name.begin(), arg_name.end(), '*', 'p');
                                        mangled += arg_name;
                                    }
                                }
                            }
                            type_name = mangled;
                        }
                    }
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
                                        else if (field_type->kind() == Cryo::TypeKind::Reference)
                                        {
                                            auto *ref_type = dynamic_cast<const Cryo::ReferenceType *>(field_type.get());
                                            if (ref_type && ref_type->referent().is_valid())
                                            {
                                                type_name = ref_type->referent()->display_name();
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
                                else if (field_type->kind() == Cryo::TypeKind::Reference)
                                {
                                    auto *ref_type = dynamic_cast<const Cryo::ReferenceType *>(field_type.get());
                                    if (ref_type && ref_type->referent().is_valid())
                                    {
                                        type_name = ref_type->referent()->display_name();
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

            // Count and strip trailing array modifiers (e.g., "SymbolID[]" -> "SymbolID", array_depth=1)
            std::string base_name = arg_name;
            int array_depth = 0;
            while (base_name.size() >= 2 && base_name.substr(base_name.size() - 2) == "[]")
            {
                base_name.erase(base_name.size() - 2);
                array_depth++;
            }

            // Count and strip trailing pointer modifiers (e.g., "Expr*" -> "Expr", pointer_depth=1)
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

            // Wrap in array type(s) if trailing [] were present
            if (arg_type.is_valid() && array_depth > 0)
            {
                for (int a = 0; a < array_depth; ++a)
                {
                    arg_type = symbols.arena().get_array_of(arg_type, std::nullopt);
                }
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "parse_type_args_from_string: Wrapped '{}' in {} array level(s) -> '{}'",
                          base_name, array_depth, arg_type->display_name());
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

    //===================================================================
    // On-Demand Generic Method Instantiation
    //===================================================================

    llvm::Function *CallCodegen::try_instantiate_generic_method(
        Cryo::CallExpressionNode *node,
        const std::string &type_name,
        const std::string &method_name)
    {
        GenericCodegen *generics = ctx().visitor() ? ctx().visitor()->get_generics() : nullptr;
        Cryo::TemplateRegistry *template_registry = ctx().template_registry();
        if (!generics || !template_registry)
            return nullptr;

        // Get the receiver's TypeRef to find the generic base and type args
        // We need the callee to be a MemberAccessNode
        if (!node->callee() || node->callee()->kind() != NodeKind::MemberAccess)
            return nullptr;

        auto *member = static_cast<Cryo::MemberAccessNode *>(node->callee());
        if (!member->object())
            return nullptr;

        TypeRef obj_type = member->object()->get_resolved_type();

        // Fallback: if the AST node doesn't have a resolved type, check variable_types_map
        if ((!obj_type.is_valid() || obj_type->kind() != Cryo::TypeKind::InstantiatedType) &&
            member->object()->kind() == NodeKind::Identifier)
        {
            auto *id = static_cast<Cryo::IdentifierNode *>(member->object());
            auto &var_types = ctx().variable_types_map();
            auto it = var_types.find(id->name());
            if (it != var_types.end() && it->second.is_valid())
            {
                obj_type = it->second;
            }
        }

        if (!obj_type.is_valid() || obj_type->kind() != Cryo::TypeKind::InstantiatedType)
            return nullptr;

        auto *inst = static_cast<const Cryo::InstantiatedType *>(obj_type.get());
        if (!inst->generic_base().is_valid())
            return nullptr;

        std::string generic_base = inst->generic_base()->display_name();
        const auto &enum_type_args = inst->type_args();

        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                  "try_instantiate_generic_method: Trying on-demand instantiation of {}::{} "
                  "(generic_base={}, enum_type_args={})",
                  type_name, method_name, generic_base, enum_type_args.size());

        // Look up the impl block for the generic base
        Cryo::ImplementationBlockNode *impl_block = template_registry->get_enum_impl_block(generic_base);
        if (!impl_block)
        {
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "try_instantiate_generic_method: No impl block for '{}'", generic_base);
            return nullptr;
        }

        // Find the method by name that has its own generic parameters
        Cryo::FunctionDeclarationNode *target_method = nullptr;
        for (const auto &method_impl : impl_block->method_implementations())
        {
            if (method_impl->name() == method_name && !method_impl->generic_parameters().empty())
            {
                target_method = method_impl.get();
                break;
            }
        }
        if (!target_method)
        {
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "try_instantiate_generic_method: No generic method '{}' in impl block for '{}'",
                      method_name, generic_base);
            return nullptr;
        }

        // Get enum-level type params from the template
        const Cryo::TemplateRegistry::TemplateInfo *tmpl_info = template_registry->find_template(generic_base);
        std::vector<std::string> enum_type_params;
        if (tmpl_info && tmpl_info->enum_template)
        {
            for (const auto &param : tmpl_info->enum_template->generic_parameters())
            {
                enum_type_params.push_back(param->name());
            }
        }

        // Get method-level type params
        std::vector<std::string> method_type_params;
        for (const auto &param : target_method->generic_parameters())
        {
            method_type_params.push_back(param->name());
        }

        // Infer method-level type arguments from call arguments
        std::vector<TypeRef> method_type_args;
        const auto &method_params = target_method->parameters();
        const auto &call_args = node->arguments();

        for (const auto &method_param_name : method_type_params)
        {
            TypeRef inferred;

            // Strategy 1: Check method parameters for FunctionType whose return type
            // is a GenericParam matching the method type param we need to infer.
            // Then look at the corresponding call argument to get the concrete type.
            bool has_this = !method_params.empty() && method_params[0]->name() == "this";

            for (size_t pi = 0; pi < method_params.size(); ++pi)
            {
                TypeRef param_type = method_params[pi]->get_resolved_type();
                if (!param_type.is_valid())
                    continue;

                // Check if this param is a FunctionType with return type matching the generic param
                if (param_type->kind() == Cryo::TypeKind::Function)
                {
                    auto *fn_type = static_cast<const Cryo::FunctionType *>(param_type.get());
                    TypeRef ret = fn_type->return_type();
                    if (ret.is_valid() && ret->kind() == Cryo::TypeKind::GenericParam &&
                        ret->display_name() == method_param_name)
                    {
                        // Found it! Now infer from the corresponding call argument
                        size_t call_arg_idx = has_this ? (pi - 1) : pi;
                        if (call_arg_idx < call_args.size())
                        {
                            // Primary: check resolved type of the call argument
                            TypeRef arg_type = call_args[call_arg_idx]->get_resolved_type();
                            if (arg_type.is_valid() && arg_type->kind() == Cryo::TypeKind::Function)
                            {
                                auto *arg_fn = static_cast<const Cryo::FunctionType *>(arg_type.get());
                                inferred = arg_fn->return_type();
                                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                          "try_instantiate_generic_method: Inferred {} = {} from call arg resolved type",
                                          method_param_name, inferred.is_valid() ? inferred->display_name() : "null");
                            }

                            // Fallback: look up LLVM function by identifier name
                            if (!inferred.is_valid() && call_args[call_arg_idx]->kind() == NodeKind::Identifier)
                            {
                                auto *id_node = static_cast<Cryo::IdentifierNode *>(call_args[call_arg_idx].get());
                                std::string fn_name = id_node->name();

                                std::string ns = ctx().namespace_context();
                                llvm::Function *llvm_fn = nullptr;
                                if (!ns.empty())
                                    llvm_fn = module()->getFunction(ns + "::" + fn_name);
                                if (!llvm_fn)
                                    llvm_fn = module()->getFunction(fn_name);

                                if (llvm_fn)
                                {
                                    llvm::Type *ret_ty = llvm_fn->getReturnType();
                                    if (ret_ty->isIntegerTy(32))
                                        inferred = symbols().arena().lookup_type_by_name("i32");
                                    else if (ret_ty->isIntegerTy(64))
                                        inferred = symbols().arena().lookup_type_by_name("i64");
                                    else if (ret_ty->isIntegerTy(8))
                                        inferred = symbols().arena().lookup_type_by_name("i8");
                                    else if (ret_ty->isIntegerTy(16))
                                        inferred = symbols().arena().lookup_type_by_name("i16");
                                    else if (ret_ty->isIntegerTy(1))
                                        inferred = symbols().arena().lookup_type_by_name("boolean");
                                    else if (ret_ty->isFloatTy())
                                        inferred = symbols().arena().lookup_type_by_name("f32");
                                    else if (ret_ty->isDoubleTy())
                                        inferred = symbols().arena().lookup_type_by_name("f64");
                                    else if (ret_ty->isVoidTy())
                                        inferred = symbols().arena().lookup_type_by_name("void");

                                    if (inferred.is_valid())
                                    {
                                        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                                                  "try_instantiate_generic_method: Inferred {} = {} from LLVM function '{}'",
                                                  method_param_name, inferred->display_name(), llvm_fn->getName().str());
                                    }
                                }
                            }
                        }
                        break;
                    }
                }

                // Also check TypeAnnotation as a fallback (some params retain the original annotation)
                const auto *ann = method_params[pi]->type_annotation();
                if (ann && ann->kind == TypeAnnotationKind::Function &&
                    ann->return_type && ann->return_type->name == method_param_name)
                {
                    size_t call_arg_idx = has_this ? (pi - 1) : pi;
                    if (call_arg_idx < call_args.size())
                    {
                        TypeRef arg_type = call_args[call_arg_idx]->get_resolved_type();
                        if (arg_type.is_valid() && arg_type->kind() == Cryo::TypeKind::Function)
                        {
                            auto *arg_fn = static_cast<const Cryo::FunctionType *>(arg_type.get());
                            inferred = arg_fn->return_type();
                        }
                    }
                    break;
                }
            }

            if (!inferred.is_valid())
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "try_instantiate_generic_method: Could not infer type for param '{}'",
                          method_param_name);
                return nullptr;
            }

            method_type_args.push_back(inferred);
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "try_instantiate_generic_method: Inferred {} = {}",
                      method_param_name, inferred->display_name());
        }

        // Combine enum type params + method type params
        std::vector<std::string> all_type_params;
        std::vector<TypeRef> all_type_args;
        for (size_t i = 0; i < enum_type_params.size(); ++i)
        {
            all_type_params.push_back(enum_type_params[i]);
            all_type_args.push_back(i < enum_type_args.size() ? enum_type_args[i] : TypeRef{});
        }
        for (size_t i = 0; i < method_type_params.size(); ++i)
        {
            all_type_params.push_back(method_type_params[i]);
            all_type_args.push_back(i < method_type_args.size() ? method_type_args[i] : TypeRef{});
        }

        // Get namespace
        std::string base_namespace;
        if (tmpl_info && !tmpl_info->module_namespace.empty())
        {
            base_namespace = tmpl_info->module_namespace;
        }
        if (base_namespace.empty())
        {
            base_namespace = ctx().get_type_namespace(generic_base);
        }
        if (base_namespace.empty())
        {
            base_namespace = ctx().namespace_context();
        }

        // Call GenericCodegen to instantiate
        llvm::Function *fn = generics->instantiate_method_for_type(
            type_name, generic_base, all_type_params, all_type_args,
            target_method, base_namespace);

        if (fn)
        {
            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "try_instantiate_generic_method: Successfully generated {}::{}",
                      type_name, method_name);
        }

        return fn;
    }

    //===================================================================
    // Built-in Array Methods (push, pop)
    //===================================================================

    llvm::Value *CallCodegen::generate_builtin_array_method(
        Cryo::CallExpressionNode *node,
        llvm::Value *receiver,
        const std::string &method_name,
        const std::vector<llvm::Value *> &args,
        TypeRef obj_type)
    {
        auto *arr_type = static_cast<const Cryo::ArrayType *>(obj_type.get());
        if (!arr_type || arr_type->is_fixed_size())
            return nullptr;

        TypeRef elem_type_ref = arr_type->element();
        llvm::Type *elem_type = types().map(elem_type_ref);
        if (!elem_type)
            return nullptr;

        llvm::Type *mapped = types().map(obj_type);
        llvm::StructType *array_struct = llvm::dyn_cast_or_null<llvm::StructType>(mapped);
        if (!array_struct)
            return nullptr;

        return generate_builtin_array_method_raw(node, receiver, method_name, args, elem_type, array_struct);
    }

    llvm::Value *CallCodegen::generate_builtin_array_method_raw(
        Cryo::CallExpressionNode *node,
        llvm::Value *receiver,
        const std::string &method_name,
        const std::vector<llvm::Value *> &args,
        llvm::Type *elem_type,
        llvm::StructType *array_struct)
    {
        auto *i64_ty = llvm::Type::getInt64Ty(llvm_ctx());

        if (method_name == "push")
        {
            if (args.empty())
            {
                report_error(ErrorCode::E0636_UNDEFINED_FUNCTION_CALL, node,
                             "Array::push requires one argument");
                return nullptr;
            }

            LOG_DEBUG(Cryo::LogComponent::CODEGEN, "Generating built-in Array::push");

            llvm::Value *value = args[0] ? cast_if_needed(args[0], elem_type) : nullptr;
            if (!value)
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "Generating built-in Array::push: argument value is null, cannot proceed");
                return nullptr;
            }

            // Load current length and capacity
            llvm::Value *len_ptr = builder().CreateStructGEP(array_struct, receiver, 1, "arr.len.ptr");
            llvm::Value *len = builder().CreateLoad(i64_ty, len_ptr, "arr.len");

            llvm::Value *cap_ptr = builder().CreateStructGEP(array_struct, receiver, 2, "arr.cap.ptr");
            llvm::Value *cap = builder().CreateLoad(i64_ty, cap_ptr, "arr.cap");

            // Check if we need to grow: len >= cap
            llvm::Value *needs_grow = builder().CreateICmpUGE(len, cap, "needs.grow");

            llvm::Function *func = builder().GetInsertBlock()->getParent();
            llvm::BasicBlock *grow_bb = llvm::BasicBlock::Create(llvm_ctx(), "arr.grow", func);
            llvm::BasicBlock *store_bb = llvm::BasicBlock::Create(llvm_ctx(), "arr.store", func);

            builder().CreateCondBr(needs_grow, grow_bb, store_bb);

            // === Grow block ===
            builder().SetInsertPoint(grow_bb);

            // new_cap = (cap == 0) ? 8 : cap * 2
            llvm::Value *is_zero = builder().CreateICmpEQ(cap, llvm::ConstantInt::get(i64_ty, 0), "cap.is.zero");
            llvm::Value *doubled = builder().CreateMul(cap, llvm::ConstantInt::get(i64_ty, 2), "cap.doubled");
            llvm::Value *new_cap = builder().CreateSelect(is_zero, llvm::ConstantInt::get(i64_ty, 8), doubled, "new.cap");

            // Calculate byte size: new_cap * sizeof(element)
            const llvm::DataLayout &dl = module()->getDataLayout();
            uint64_t elem_size = dl.getTypeAllocSize(elem_type);
            llvm::Value *byte_size = builder().CreateMul(new_cap, llvm::ConstantInt::get(i64_ty, elem_size), "byte.size");

            // Load current elements pointer
            llvm::Value *elems_ptr_gep = builder().CreateStructGEP(array_struct, receiver, 0, "arr.elems.ptr");
            llvm::Value *old_ptr = builder().CreateLoad(llvm::PointerType::get(llvm_ctx(), 0), elems_ptr_gep, "old.ptr");

            // If cap was 0, the data pointer may be a stack address (from empty array literal).
            // Pass null to realloc in that case, which is equivalent to malloc.
            llvm::Value *safe_ptr = builder().CreateSelect(is_zero,
                llvm::ConstantPointerNull::get(llvm::PointerType::get(llvm_ctx(), 0)),
                old_ptr, "safe.ptr");

            // realloc(safe_ptr, byte_size) - handles both malloc (null) and realloc (heap) cases
            llvm::Function *realloc_fn = get_or_create_function("realloc",
                llvm::PointerType::get(llvm_ctx(), 0),
                {llvm::PointerType::get(llvm_ctx(), 0), i64_ty});
            llvm::Value *new_ptr = builder().CreateCall(realloc_fn, {safe_ptr, byte_size}, "new.ptr");

            // Store new pointer and capacity
            builder().CreateStore(new_ptr, elems_ptr_gep);
            builder().CreateStore(new_cap, cap_ptr);
            builder().CreateBr(store_bb);

            // === Store block ===
            builder().SetInsertPoint(store_bb);

            // Reload elements pointer (may have been updated in grow block)
            llvm::Value *elems_ptr2 = builder().CreateStructGEP(array_struct, receiver, 0, "arr.elems.ptr2");
            llvm::Value *elems = builder().CreateLoad(llvm::PointerType::get(llvm_ctx(), 0), elems_ptr2, "arr.elems");

            // Store value at elements[length]
            llvm::Value *elem_ptr = builder().CreateInBoundsGEP(elem_type, elems, len, "arr.elem.ptr");
            builder().CreateStore(value, elem_ptr);

            // Increment length
            llvm::Value *new_len = builder().CreateAdd(len, llvm::ConstantInt::get(i64_ty, 1), "arr.new.len");
            builder().CreateStore(new_len, len_ptr);

            // push returns void - return non-null sentinel
            return receiver;
        }
        else if (method_name == "pop")
        {
            LOG_DEBUG(Cryo::LogComponent::CODEGEN, "Generating built-in Array::pop");

            // Load length
            llvm::Value *len_ptr = builder().CreateStructGEP(array_struct, receiver, 1, "arr.len.ptr");
            llvm::Value *len = builder().CreateLoad(i64_ty, len_ptr, "arr.len");

            // Decrement length
            llvm::Value *new_len = builder().CreateSub(len, llvm::ConstantInt::get(i64_ty, 1), "arr.new.len");
            builder().CreateStore(new_len, len_ptr);

            // Load elements pointer
            llvm::Value *elems_ptr = builder().CreateStructGEP(array_struct, receiver, 0, "arr.elems.ptr");
            llvm::Value *elems = builder().CreateLoad(llvm::PointerType::get(llvm_ctx(), 0), elems_ptr, "arr.elems");

            // Load value at elements[new_length]
            llvm::Value *elem_ptr = builder().CreateInBoundsGEP(elem_type, elems, new_len, "arr.elem.ptr");
            return builder().CreateLoad(elem_type, elem_ptr, "pop.val");
        }

        // Not a built-in array method
        return nullptr;
    }

    //===================================================================
    // Variadic Forwarding (va_list → v* variant)
    //===================================================================

    llvm::Value *CallCodegen::generate_va_forwarding_intrinsic(
        Cryo::CallExpressionNode *node,
        const std::string &intrinsic_name,
        std::vector<llvm::Value *> &args,
        int va_list_index)
    {
        static const std::unordered_map<std::string, std::string> va_variants = {
            {"printf", "vprintf"},
            {"fprintf", "vfprintf"},
            {"sprintf", "vsprintf"},
            {"snprintf", "vsnprintf"},
        };

        auto it = va_variants.find(intrinsic_name);
        if (it == va_variants.end())
        {
            report_error(ErrorCode::E0636_UNDEFINED_FUNCTION_CALL, node,
                         "Cannot forward va_list to non-printf-family intrinsic: " + intrinsic_name);
            return nullptr;
        }

        const std::string &v_name = it->second;
        llvm::Type *ptr_type = llvm::PointerType::get(llvm_ctx(), 0);
        llvm::Type *int_type = llvm::Type::getInt32Ty(llvm_ctx());

        LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                  "CallCodegen: Variadic forwarding {} -> {} (va_list at arg index {})",
                  intrinsic_name, v_name, va_list_index);

        // Get the va_list alloca, then handle platform-specific ABI:
        // - Win64: va_list = char* (8 bytes), v* functions expect the char* by value,
        //   so we must LOAD the pointer from the alloca.
        // - System V x86-64: va_list = __va_list_tag[1] (24 bytes), v* functions expect
        //   a pointer to the tag array, so we pass the alloca directly.
        llvm::Value *va_list_ptr = nullptr;
        if (va_list_index < static_cast<int>(node->arguments().size()))
        {
            auto *id_node = dynamic_cast<Cryo::IdentifierNode *>(node->arguments()[va_list_index].get());
            if (id_node)
            {
                // Get the alloca (pointer) instead of the loaded value
                va_list_ptr = values().get_alloca(id_node->name());
                if (!va_list_ptr)
                    va_list_ptr = values().get_value(id_node->name());
            }
        }
        if (!va_list_ptr)
            va_list_ptr = args[va_list_index]; // fallback

        // On Win64, va_list is char* — load the pointer value from the alloca
        // so that v* functions receive the char* by value, not a pointer-to-pointer.
#ifdef _WIN32
        if (va_list_ptr && llvm::isa<llvm::AllocaInst>(va_list_ptr))
        {
            va_list_ptr = builder().CreateLoad(
                llvm::PointerType::get(llvm_ctx(), 0), va_list_ptr, "va_list.loaded");
        }
#endif

        // Build args: everything before va_list stays, va_list pointer replaces rest
        std::vector<llvm::Value *> v_args;

        if (v_name == "vsprintf" && va_list_index == 1)
        {
            // sprintf(format, args) → vsprintf(buffer, format, va_list)
            // User called sprintf(msg, args) with only format + va_list,
            // so we need to allocate a temp buffer.
            // Use malloc instead of alloca: alloca buffers accumulate in the
            // caller's stack frame and are never freed until the function returns.
            // In recursive functions with many sprintf calls (e.g., module discovery
            // with 30+ modules), the cumulative alloca can exhaust the stack and
            // overwrite heap metadata, causing heap corruption.
            llvm::Function *malloc_fn = module()->getFunction("malloc");
            if (!malloc_fn)
            {
                llvm::FunctionType *malloc_type = llvm::FunctionType::get(
                    llvm::PointerType::get(llvm_ctx(), 0),
                    {llvm::Type::getInt64Ty(llvm_ctx())},
                    false);
                malloc_fn = llvm::Function::Create(
                    malloc_type, llvm::Function::ExternalLinkage, "malloc", module());
            }
            llvm::Value *buffer = builder().CreateCall(
                malloc_fn,
                {llvm::ConstantInt::get(llvm::Type::getInt64Ty(llvm_ctx()), 4096)},
                "sprintf.buf");
            v_args = {buffer, args[0], va_list_ptr};
        }
        else
        {
            // General case: args before va_list + va_list pointer
            for (int i = 0; i < va_list_index; ++i)
                v_args.push_back(args[i]);
            v_args.push_back(va_list_ptr);
        }

        // Create v* function type (all ptr params + va_list ptr, returns i32)
        std::vector<llvm::Type *> param_types;
        for (auto *a : v_args)
            param_types.push_back(a->getType());
        llvm::FunctionType *fn_type = llvm::FunctionType::get(int_type, param_types, false);

        llvm::Function *v_func = module()->getFunction(v_name);
        if (!v_func)
        {
            v_func = llvm::Function::Create(
                fn_type, llvm::Function::ExternalLinkage, v_name, module());
        }

        llvm::Value *result = builder().CreateCall(v_func, v_args, v_name + ".result");

        // For vsprintf, return the buffer pointer (string), not the int result
        if (v_name == "vsprintf")
            return v_args[0]; // buffer pointer

        return result;
    }

} // namespace Cryo::Codegen
