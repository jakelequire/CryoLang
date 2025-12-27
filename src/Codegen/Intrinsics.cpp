#include "Codegen/Intrinsics.hpp"
#include "Codegen/LLVMContext.hpp"
#include "AST/ASTNode.hpp"
#include "GDM/GDM.hpp"
#include "Utils/Logger.hpp"

#include <llvm/IR/InlineAsm.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Type.h>
#include <llvm/IR/Intrinsics.h>
#include <iostream>
#include <set>

namespace Cryo::Codegen
{
    Intrinsics::Intrinsics(LLVMContextManager &context_manager, Cryo::DiagnosticManager *gdm)
        : _context_manager(context_manager), _gdm(gdm), _has_errors(false)
    {
    }

    llvm::Value *Intrinsics::generate_intrinsic_call(Cryo::CallExpressionNode *node,
                                                     const std::string &intrinsic_name,
                                                     const std::vector<llvm::Value *> &args)
    {
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "Generating intrinsic call: {}", intrinsic_name);

        // Memory management intrinsics
        if (intrinsic_name == "__malloc__")
            return generate_malloc(args);
        else if (intrinsic_name == "__free__")
            return generate_free(args);
        else if (intrinsic_name == "__realloc__")
            return generate_realloc(args);
        else if (intrinsic_name == "__calloc__")
            return generate_calloc(args);
        else if (intrinsic_name == "__mmap__")
            return generate_mmap(args);
        else if (intrinsic_name == "__munmap__")
            return generate_munmap(args);

        // Memory operations
        else if (intrinsic_name == "__memcpy__")
            return generate_memcpy(args);
        else if (intrinsic_name == "__memset__")
            return generate_memset(args);
        else if (intrinsic_name == "__memcmp__")
            return generate_memcmp(args);
        else if (intrinsic_name == "__memmove__")
            return generate_memmove(args);

        // Pointer arithmetic operations
        else if (intrinsic_name == "__ptr_add__")
            return generate_ptr_add(args);
        else if (intrinsic_name == "__ptr_sub__")
            return generate_ptr_sub(args);
        else if (intrinsic_name == "__ptr_diff__")
            return generate_ptr_diff(args);

        // System calls
        else if (intrinsic_name == "__syscall_write__")
            return generate_syscall_write(args);
        else if (intrinsic_name == "__syscall_read__")
            return generate_syscall_read(args);
        else if (intrinsic_name == "__syscall_exit__")
            return generate_syscall_exit(args);
        else if (intrinsic_name == "__syscall_open__")
            return generate_syscall_open(args);
        else if (intrinsic_name == "__syscall_close__")
            return generate_syscall_close(args);
        else if (intrinsic_name == "__syscall_lseek__")
            return generate_syscall_lseek(args);
        else if (intrinsic_name == "__syscall_unlink__")
            return generate_syscall_unlink(args);
        else if (intrinsic_name == "__syscall_mkdir__")
            return generate_syscall_mkdir(args);
        else if (intrinsic_name == "__syscall_rmdir__")
            return generate_syscall_rmdir(args);

        // String operations
        else if (intrinsic_name == "__strlen__")
            return generate_strlen(args);
        else if (intrinsic_name == "__strcmp__")
            return generate_strcmp(args);
        else if (intrinsic_name == "__strcpy__")
            return generate_strcpy(args);
        else if (intrinsic_name == "__strcat__")
            return generate_strcat(args);

        // I/O operations
        else if (intrinsic_name == "__printf__")
            return generate_printf(args);
        else if (intrinsic_name == "__sprintf__")
            return generate_sprintf(args);
        else if (intrinsic_name == "__fprintf__")
            return generate_fprintf(args);
        else if (intrinsic_name == "__panic__")
            return generate_panic(args);

        // String conversion operations
        else if (intrinsic_name == "__float32_to_string__")
            return generate_float32_to_string(args);
        else if (intrinsic_name == "__float64_to_string__")
            return generate_float64_to_string(args);

        // Math operations
        else if (intrinsic_name == "__sqrt__")
            return generate_sqrt(args);
        else if (intrinsic_name == "__pow__")
            return generate_pow(args);
        else if (intrinsic_name == "__sin__")
            return generate_sin(args);
        else if (intrinsic_name == "__cos__")
            return generate_cos(args);

        // Type conversion intrinsics - Safe widening conversions
        else if (intrinsic_name == "__i8_to_i16__")
            return generate_integer_extension(args, 8, 16, true);
        else if (intrinsic_name == "__i8_to_i32__")
            return generate_integer_extension(args, 8, 32, true);
        else if (intrinsic_name == "__i8_to_i64__")
            return generate_integer_extension(args, 8, 64, true);
        else if (intrinsic_name == "__i16_to_i32__")
            return generate_integer_extension(args, 16, 32, true);
        else if (intrinsic_name == "__i16_to_i64__")
            return generate_integer_extension(args, 16, 64, true);
        else if (intrinsic_name == "__i32_to_i64__")
            return generate_integer_extension(args, 32, 64, true);

        // Unsigned widening conversions
        else if (intrinsic_name == "__u8_to_u16__")
            return generate_integer_extension(args, 8, 16, false);
        else if (intrinsic_name == "__u8_to_u32__")
            return generate_integer_extension(args, 8, 32, false);
        else if (intrinsic_name == "__u8_to_u64__")
            return generate_integer_extension(args, 8, 64, false);
        else if (intrinsic_name == "__u16_to_u32__")
            return generate_integer_extension(args, 16, 32, false);
        else if (intrinsic_name == "__u16_to_u64__")
            return generate_integer_extension(args, 16, 64, false);
        else if (intrinsic_name == "__u32_to_u64__")
            return generate_integer_extension(args, 32, 64, false);

        // Narrowing conversions
        else if (intrinsic_name == "__i64_to_i32__")
            return generate_integer_truncation(args, 64, 32);
        else if (intrinsic_name == "__i64_to_i16__")
            return generate_integer_truncation(args, 64, 16);
        else if (intrinsic_name == "__i64_to_i8__")
            return generate_integer_truncation(args, 64, 8);
        else if (intrinsic_name == "__i32_to_i16__")
            return generate_integer_truncation(args, 32, 16);
        else if (intrinsic_name == "__i32_to_i8__")
            return generate_integer_truncation(args, 32, 8);
        else if (intrinsic_name == "__i16_to_i8__")
            return generate_integer_truncation(args, 16, 8);

        // Cross-signedness conversions
        else if (intrinsic_name == "__i32_to_u32__")
            return generate_sign_conversion(args, 32);
        else if (intrinsic_name == "__u32_to_i32__")
            return generate_sign_conversion(args, 32);
        else if (intrinsic_name == "__i64_to_u64__")
            return generate_sign_conversion(args, 64);
        else if (intrinsic_name == "__u64_to_i64__")
            return generate_sign_conversion(args, 64);

        // Safe conversions with overflow checks
        else if (intrinsic_name == "__try_i64_to_i32__")
            return generate_checked_conversion(args, 64, 32, true, true);
        else if (intrinsic_name == "__try_i64_to_u32__")
            return generate_checked_conversion(args, 64, 32, true, false);
        else if (intrinsic_name == "__try_u64_to_i32__")
            return generate_checked_conversion(args, 64, 32, false, true);
        else if (intrinsic_name == "__try_u32_to_i32__")
            return generate_checked_conversion(args, 32, 32, false, true);

        // Process management
        else if (intrinsic_name == "__getpid__")
            return generate_getpid(args);
        else if (intrinsic_name == "__fork__")
            return generate_fork(args);

        else if (intrinsic_name == "__maloc__")
            return generate_maloc(args);
        else if (intrinsic_name == "__mfree__")
            return generate_mfree(args);
        else if (intrinsic_name == "__msize__")
            return generate_msize(args);

        else if (intrinsic_name == "__exit__")
            return generate_exit(args);

        else
        {
            report_unimplemented_intrinsic(intrinsic_name, node);
            return nullptr;
        }
    }

    // ========================================
    // Memory Management Intrinsics
    // ========================================

    llvm::Value *Intrinsics::generate_malloc(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 1)
        {
            report_error("__malloc__ requires exactly 1 argument (size)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();
        auto *module = _context_manager.get_module();

        // Create malloc function type: void* malloc(size_t size)
        llvm::Type *size_t_type = llvm::Type::getInt64Ty(context);
        llvm::Type *void_ptr_type = llvm::PointerType::get(context, 0);
        llvm::FunctionType *malloc_type = llvm::FunctionType::get(
            void_ptr_type, {size_t_type}, false);

        // Get or create malloc function
        llvm::Function *malloc_func = get_or_create_libc_function("malloc", malloc_type);

        // Ensure size argument is size_t (i64)
        llvm::Value *size_arg = ensure_type(args[0], size_t_type, "malloc.size");

        // Call malloc
        return builder.CreateCall(malloc_func, {size_arg}, "malloc.result");
    }

    llvm::Value *Intrinsics::generate_free(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 1)
        {
            report_error("__free__ requires exactly 1 argument (ptr)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        // Create free function type: void free(void* ptr)
        llvm::Type *void_ptr_type = llvm::PointerType::get(context, 0);
        llvm::Type *void_type = llvm::Type::getVoidTy(context);
        llvm::FunctionType *free_type = llvm::FunctionType::get(
            void_type, {void_ptr_type}, false);

        // Get or create free function
        llvm::Function *free_func = get_or_create_libc_function("free", free_type);

        // Ensure ptr argument is void*
        llvm::Value *ptr_arg = args[0];
        if (!ptr_arg->getType()->isPointerTy())
        {
            report_error("__free__ argument must be a pointer");
            return nullptr;
        }

        // Call free
        builder.CreateCall(free_func, {ptr_arg});

        // Free returns void, but we return a dummy value for consistency
        return llvm::Constant::getNullValue(llvm::Type::getInt32Ty(context));
    }

    llvm::Value *Intrinsics::generate_realloc(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 2)
        {
            report_error("__realloc__ requires exactly 2 arguments (ptr, size)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        // Create realloc function type: void* realloc(void* ptr, size_t size)
        llvm::Type *void_ptr_type = llvm::PointerType::get(context, 0);
        llvm::Type *size_t_type = llvm::Type::getInt64Ty(context);
        llvm::FunctionType *realloc_type = llvm::FunctionType::get(
            void_ptr_type, {void_ptr_type, size_t_type}, false);

        // Get or create realloc function
        llvm::Function *realloc_func = get_or_create_libc_function("realloc", realloc_type);

        // Prepare arguments
        llvm::Value *ptr_arg = args[0];
        llvm::Value *size_arg = ensure_type(args[1], size_t_type, "realloc.size");

        if (!ptr_arg->getType()->isPointerTy())
        {
            report_error("__realloc__ first argument must be a pointer");
            return nullptr;
        }

        // Call realloc
        return builder.CreateCall(realloc_func, {ptr_arg, size_arg}, "realloc.result");
    }

    llvm::Value *Intrinsics::generate_calloc(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 2)
        {
            report_error("__calloc__ requires exactly 2 arguments (num, size)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        // Create calloc function type: void* calloc(size_t num, size_t size)
        llvm::Type *size_t_type = llvm::Type::getInt64Ty(context);
        llvm::Type *void_ptr_type = llvm::PointerType::get(context, 0);
        llvm::FunctionType *calloc_type = llvm::FunctionType::get(
            void_ptr_type, {size_t_type, size_t_type}, false);

        // Get or create calloc function
        llvm::Function *calloc_func = get_or_create_libc_function("calloc", calloc_type);

        // Prepare arguments
        llvm::Value *num_arg = ensure_type(args[0], size_t_type, "calloc.num");
        llvm::Value *size_arg = ensure_type(args[1], size_t_type, "calloc.size");

        // Call calloc
        return builder.CreateCall(calloc_func, {num_arg, size_arg}, "calloc.result");
    }

    llvm::Value *Intrinsics::generate_mmap(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 1)
        {
            report_error("__mmap__ requires exactly 1 argument (size)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        // For simplicity, we'll implement mmap as a malloc call
        // In a real implementation, you'd want actual mmap system call
        llvm::Type *size_t_type = llvm::Type::getInt64Ty(context);
        llvm::Type *void_ptr_type = llvm::PointerType::get(context, 0);
        llvm::FunctionType *malloc_type = llvm::FunctionType::get(
            void_ptr_type, {size_t_type}, false);

        // Get or create malloc function
        llvm::Function *malloc_func = get_or_create_libc_function("malloc", malloc_type);

        // Ensure size argument is size_t (i64)
        llvm::Value *size_arg = ensure_type(args[0], size_t_type, "mmap.size");

        // Call malloc (simplified mmap implementation)
        return builder.CreateCall(malloc_func, {size_arg}, "mmap.result");
    }

    llvm::Value *Intrinsics::generate_munmap(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 2)
        {
            report_error("__munmap__ requires exactly 2 arguments (ptr, size)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        // For simplicity, we'll implement munmap as a free call
        // In a real implementation, you'd want actual munmap system call
        llvm::Type *void_type = llvm::Type::getVoidTy(context);
        llvm::Type *void_ptr_type = llvm::PointerType::get(context, 0);
        llvm::FunctionType *free_type = llvm::FunctionType::get(
            void_type, {void_ptr_type}, false);

        // Get or create free function
        llvm::Function *free_func = get_or_create_libc_function("free", free_type);

        // Only use the pointer argument (ignore size for simplified implementation)
        llvm::Value *ptr_arg = args[0];
        if (!ptr_arg->getType()->isPointerTy())
        {
            report_error("__munmap__ first argument must be a pointer");
            return nullptr;
        }

        // Call free (simplified munmap implementation)
        builder.CreateCall(free_func, {ptr_arg});

        // munmap typically returns int (0 on success, -1 on error)
        // For simplicity, always return 0
        return llvm::ConstantInt::get(llvm::Type::getInt32Ty(context), 0);
    }

    // ========================================
    // Memory Operations Intrinsics
    // ========================================

    llvm::Value *Intrinsics::generate_memcpy(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 3)
        {
            report_error("__memcpy__ requires exactly 3 arguments (dest, src, n)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        // Use LLVM's builtin memcpy intrinsic
        llvm::Function *memcpy_intrinsic = llvm::Intrinsic::getDeclaration(
            _context_manager.get_module(),
            llvm::Intrinsic::memcpy,
            {llvm::PointerType::get(context, 0),
             llvm::PointerType::get(context, 0),
             llvm::Type::getInt64Ty(context)});

        // Prepare arguments
        llvm::Value *dest = args[0];
        llvm::Value *src = args[1];
        llvm::Value *n = ensure_type(args[2], llvm::Type::getInt64Ty(context), "memcpy.n");
        llvm::Value *is_volatile = llvm::ConstantInt::get(llvm::Type::getInt1Ty(context), 0);

        if (!dest->getType()->isPointerTy() || !src->getType()->isPointerTy())
        {
            report_error("__memcpy__ first two arguments must be pointers");
            return nullptr;
        }

        // Call memcpy intrinsic
        builder.CreateCall(memcpy_intrinsic, {dest, src, n, is_volatile});

        // Return dest pointer
        return dest;
    }

    llvm::Value *Intrinsics::generate_memset(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 3)
        {
            report_error("__memset__ requires exactly 3 arguments (ptr, value, n)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        // Use LLVM's builtin memset intrinsic
        llvm::Function *memset_intrinsic = llvm::Intrinsic::getDeclaration(
            _context_manager.get_module(),
            llvm::Intrinsic::memset,
            {llvm::PointerType::get(context, 0),
             llvm::Type::getInt64Ty(context)});

        // Prepare arguments
        llvm::Value *ptr = args[0];
        llvm::Value *value = ensure_type(args[1], llvm::Type::getInt8Ty(context), "memset.val");
        llvm::Value *n = ensure_type(args[2], llvm::Type::getInt64Ty(context), "memset.n");
        llvm::Value *is_volatile = llvm::ConstantInt::get(llvm::Type::getInt1Ty(context), 0);

        if (!ptr->getType()->isPointerTy())
        {
            report_error("__memset__ first argument must be a pointer");
            return nullptr;
        }

        // Call memset intrinsic
        builder.CreateCall(memset_intrinsic, {ptr, value, n, is_volatile});

        // Return ptr
        return ptr;
    }

    llvm::Value *Intrinsics::generate_memcmp(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 3)
        {
            report_error("__memcmp__ requires exactly 3 arguments (ptr1, ptr2, n)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        // Create memcmp function type: int memcmp(const void* s1, const void* s2, size_t n)
        llvm::Type *void_ptr_type = llvm::PointerType::get(context, 0);
        llvm::Type *size_t_type = llvm::Type::getInt64Ty(context);
        llvm::Type *int_type = llvm::Type::getInt32Ty(context);
        llvm::FunctionType *memcmp_type = llvm::FunctionType::get(
            int_type, {void_ptr_type, void_ptr_type, size_t_type}, false);

        // Get or create memcmp function
        llvm::Function *memcmp_func = get_or_create_libc_function("memcmp", memcmp_type);

        // Prepare arguments
        llvm::Value *ptr1 = args[0];
        llvm::Value *ptr2 = args[1];
        llvm::Value *n = ensure_type(args[2], size_t_type, "memcmp.n");

        if (!ptr1->getType()->isPointerTy() || !ptr2->getType()->isPointerTy())
        {
            report_error("__memcmp__ first two arguments must be pointers");
            return nullptr;
        }

        // Call memcmp
        return builder.CreateCall(memcmp_func, {ptr1, ptr2, n}, "memcmp.result");
    }

    llvm::Value *Intrinsics::generate_memmove(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 3)
        {
            report_error("__memmove__ requires exactly 3 arguments (dest, src, n)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        // Use LLVM's builtin memmove intrinsic
        llvm::Function *memmove_intrinsic = llvm::Intrinsic::getDeclaration(
            _context_manager.get_module(),
            llvm::Intrinsic::memmove,
            {llvm::PointerType::get(context, 0),
             llvm::PointerType::get(context, 0),
             llvm::Type::getInt64Ty(context)});

        // Prepare arguments
        llvm::Value *dest = args[0];
        llvm::Value *src = args[1];
        llvm::Value *n = ensure_type(args[2], llvm::Type::getInt64Ty(context), "memmove.n");
        llvm::Value *is_volatile = llvm::ConstantInt::get(llvm::Type::getInt1Ty(context), 0);

        if (!dest->getType()->isPointerTy() || !src->getType()->isPointerTy())
        {
            report_error("__memmove__ first two arguments must be pointers");
            return nullptr;
        }

        // Call memmove intrinsic
        builder.CreateCall(memmove_intrinsic, {dest, src, n, is_volatile});

        // Return dest pointer
        return dest;
    }

    // ========================================
    // Pointer Arithmetic Intrinsics
    // ========================================

    llvm::Value *Intrinsics::generate_ptr_add(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 2)
        {
            report_error("__ptr_add__ requires exactly 2 arguments (ptr, offset)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        llvm::Value *ptr = args[0];
        llvm::Value *offset = args[1];

        if (!ptr->getType()->isPointerTy())
        {
            report_error("__ptr_add__ first argument must be a pointer");
            return nullptr;
        }

        // Convert pointer to i8* for byte arithmetic
        llvm::Type *i8_ptr_type = llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
        llvm::Value *byte_ptr = builder.CreateBitCast(ptr, i8_ptr_type, "ptr_as_bytes");

        // Ensure offset is i64
        llvm::Value *offset_i64 = ensure_type(offset, llvm::Type::getInt64Ty(context), "ptr_add.offset");

        // Use getelementptr for pointer arithmetic
        llvm::Value *result_ptr = builder.CreateGEP(
            llvm::Type::getInt8Ty(context),
            byte_ptr,
            offset_i64,
            "ptr_add_result");

        // Cast back to original pointer type
        return builder.CreateBitCast(result_ptr, ptr->getType(), "ptr_add_cast");
    }

    llvm::Value *Intrinsics::generate_ptr_sub(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 2)
        {
            report_error("__ptr_sub__ requires exactly 2 arguments (ptr, offset)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        llvm::Value *ptr = args[0];
        llvm::Value *offset = args[1];

        if (!ptr->getType()->isPointerTy())
        {
            report_error("__ptr_sub__ first argument must be a pointer");
            return nullptr;
        }

        // Convert pointer to i8* for byte arithmetic
        llvm::Type *i8_ptr_type = llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
        llvm::Value *byte_ptr = builder.CreateBitCast(ptr, i8_ptr_type, "ptr_as_bytes");

        // Ensure offset is i64 and negate it
        llvm::Value *offset_i64 = ensure_type(offset, llvm::Type::getInt64Ty(context), "ptr_sub.offset");
        llvm::Value *neg_offset = builder.CreateNeg(offset_i64, "neg_offset");

        // Use getelementptr for pointer arithmetic
        llvm::Value *result_ptr = builder.CreateGEP(
            llvm::Type::getInt8Ty(context),
            byte_ptr,
            neg_offset,
            "ptr_sub_result");

        // Cast back to original pointer type
        return builder.CreateBitCast(result_ptr, ptr->getType(), "ptr_sub_cast");
    }

    llvm::Value *Intrinsics::generate_ptr_diff(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 2)
        {
            report_error("__ptr_diff__ requires exactly 2 arguments (ptr1, ptr2)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        llvm::Value *ptr1 = args[0];
        llvm::Value *ptr2 = args[1];

        if (!ptr1->getType()->isPointerTy() || !ptr2->getType()->isPointerTy())
        {
            report_error("__ptr_diff__ both arguments must be pointers");
            return nullptr;
        }

        // Convert pointers to integers for arithmetic
        llvm::Type *int_ptr_type = llvm::Type::getIntNTy(context, 64);
        llvm::Value *int_ptr1 = builder.CreatePtrToInt(ptr1, int_ptr_type, "ptr1_as_int");
        llvm::Value *int_ptr2 = builder.CreatePtrToInt(ptr2, int_ptr_type, "ptr2_as_int");

        // Calculate difference
        return builder.CreateSub(int_ptr1, int_ptr2, "ptr_diff_result");
    }

    // ========================================
    // System Call Intrinsics
    // ========================================

    llvm::Value *Intrinsics::generate_syscall_write(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 3)
        {
            report_error("__syscall_write__ requires exactly 3 arguments (fd, buf, count)");
            return nullptr;
        }

        auto &context = _context_manager.get_context();

        // On Linux x86_64, write syscall number is 1
        llvm::Value *syscall_num = llvm::ConstantInt::get(llvm::Type::getInt64Ty(context), 1);

        return create_syscall(syscall_num, args);
    }

    llvm::Value *Intrinsics::generate_syscall_read(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 3)
        {
            report_error("__syscall_read__ requires exactly 3 arguments (fd, buf, count)");
            return nullptr;
        }

        auto &context = _context_manager.get_context();

        // On Linux x86_64, read syscall number is 0
        llvm::Value *syscall_num = llvm::ConstantInt::get(llvm::Type::getInt64Ty(context), 0);

        return create_syscall(syscall_num, args);
    }

    llvm::Value *Intrinsics::generate_syscall_exit(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 1)
        {
            report_error("__syscall_exit__ requires exactly 1 argument (status)");
            return nullptr;
        }

        auto &context = _context_manager.get_context();

        // On Linux x86_64, exit syscall number is 60
        llvm::Value *syscall_num = llvm::ConstantInt::get(llvm::Type::getInt64Ty(context), 60);

        return create_syscall(syscall_num, args);
    }

    llvm::Value *Intrinsics::generate_syscall_open(const std::vector<llvm::Value *> &args)
    {
        if (args.size() < 2 || args.size() > 3)
        {
            report_error("__syscall_open__ requires 2-3 arguments (path, flags, [mode])");
            return nullptr;
        }

        auto &context = _context_manager.get_context();

        // On Linux x86_64, open syscall number is 2
        llvm::Value *syscall_num = llvm::ConstantInt::get(llvm::Type::getInt64Ty(context), 2);

        // If mode is not provided, use default (0644)
        std::vector<llvm::Value *> syscall_args = args;
        if (args.size() == 2)
        {
            syscall_args.push_back(llvm::ConstantInt::get(llvm::Type::getInt64Ty(context), 0644));
        }

        return create_syscall(syscall_num, syscall_args);
    }

    llvm::Value *Intrinsics::generate_syscall_close(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 1)
        {
            report_error("__syscall_close__ requires exactly 1 argument (fd)");
            return nullptr;
        }

        auto &context = _context_manager.get_context();

        // On Linux x86_64, close syscall number is 3
        llvm::Value *syscall_num = llvm::ConstantInt::get(llvm::Type::getInt64Ty(context), 3);

        return create_syscall(syscall_num, args);
    }

    llvm::Value *Intrinsics::generate_syscall_lseek(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 3)
        {
            report_error("__syscall_lseek__ requires exactly 3 arguments (fd, offset, whence)");
            return nullptr;
        }

        auto &context = _context_manager.get_context();

        // On Linux x86_64, lseek syscall number is 8
        llvm::Value *syscall_num = llvm::ConstantInt::get(llvm::Type::getInt64Ty(context), 8);

        return create_syscall(syscall_num, args);
    }

    llvm::Value *Intrinsics::generate_syscall_unlink(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 1)
        {
            report_error("__syscall_unlink__ requires exactly 1 argument (path)");
            return nullptr;
        }

        auto &context = _context_manager.get_context();

        // On Linux x86_64, unlink syscall number is 87
        llvm::Value *syscall_num = llvm::ConstantInt::get(llvm::Type::getInt64Ty(context), 87);

        return create_syscall(syscall_num, args);
    }

    llvm::Value *Intrinsics::generate_syscall_mkdir(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 2)
        {
            report_error("__syscall_mkdir__ requires exactly 2 arguments (path, mode)");
            return nullptr;
        }

        auto &context = _context_manager.get_context();

        // On Linux x86_64, mkdir syscall number is 83
        llvm::Value *syscall_num = llvm::ConstantInt::get(llvm::Type::getInt64Ty(context), 83);

        return create_syscall(syscall_num, args);
    }

    llvm::Value *Intrinsics::generate_syscall_rmdir(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 1)
        {
            report_error("__syscall_rmdir__ requires exactly 1 argument (path)");
            return nullptr;
        }

        auto &context = _context_manager.get_context();

        // On Linux x86_64, rmdir syscall number is 84
        llvm::Value *syscall_num = llvm::ConstantInt::get(llvm::Type::getInt64Ty(context), 84);

        return create_syscall(syscall_num, args);
    }

    // ========================================
    // String Operations Intrinsics
    // ========================================

    llvm::Value *Intrinsics::generate_strlen(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 1)
        {
            report_error("__strlen__ requires exactly 1 argument (str)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        // SAFETY: Add null pointer checks to prevent segfault
        llvm::Value *str_arg = args[0];
        if (!str_arg)
        {
            report_error("__strlen__ received null argument");
            return nullptr;
        }

        // SAFETY: Check if the argument has a valid type
        llvm::Type *arg_type = str_arg->getType();
        if (!arg_type)
        {
            report_error("__strlen__ argument has null type");
            return nullptr;
        }

        if (!arg_type->isPointerTy())
        {
            report_error("__strlen__ argument must be a pointer");
            return nullptr;
        }

        // Create strlen function type: size_t strlen(const char* str)
        llvm::Type *char_ptr_type = llvm::PointerType::get(context, 0);
        llvm::Type *size_t_type = llvm::Type::getInt64Ty(context);
        llvm::FunctionType *strlen_type = llvm::FunctionType::get(
            size_t_type, {char_ptr_type}, false);

        // Get or create strlen function
        llvm::Function *strlen_func = get_or_create_libc_function("strlen", strlen_type);

        // SAFETY: Check function creation
        if (!strlen_func)
        {
            report_error("Failed to create strlen function");
            return nullptr;
        }

        // Call strlen
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "__strlen__ about to call strlen with argument: {}", static_cast<void *>(str_arg));
        llvm::Value *result = builder.CreateCall(strlen_func, {str_arg}, "strlen.result");
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "__strlen__ call completed, result: {}", static_cast<void *>(result));
        return result;
    }

    llvm::Value *Intrinsics::generate_strcmp(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 2)
        {
            report_error("__strcmp__ requires exactly 2 arguments (str1, str2)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        // Create strcmp function type: int strcmp(const char* str1, const char* str2)
        llvm::Type *char_ptr_type = llvm::PointerType::get(context, 0);
        llvm::Type *int_type = llvm::Type::getInt32Ty(context);
        llvm::FunctionType *strcmp_type = llvm::FunctionType::get(
            int_type, {char_ptr_type, char_ptr_type}, false);

        // Get or create strcmp function
        llvm::Function *strcmp_func = get_or_create_libc_function("strcmp", strcmp_type);

        llvm::Value *str1_arg = args[0];
        llvm::Value *str2_arg = args[1];

        if (!str1_arg->getType()->isPointerTy() || !str2_arg->getType()->isPointerTy())
        {
            report_error("__strcmp__ arguments must be pointers");
            return nullptr;
        }

        // Call strcmp
        return builder.CreateCall(strcmp_func, {str1_arg, str2_arg}, "strcmp.result");
    }

    llvm::Value *Intrinsics::generate_strcpy(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 2)
        {
            report_error("__strcpy__ requires exactly 2 arguments (dest, src)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        // Create strcpy function type: char* strcpy(char* dest, const char* src)
        llvm::Type *char_ptr_type = llvm::PointerType::get(context, 0);
        llvm::FunctionType *strcpy_type = llvm::FunctionType::get(
            char_ptr_type, {char_ptr_type, char_ptr_type}, false);

        // Get or create strcpy function
        llvm::Function *strcpy_func = get_or_create_libc_function("strcpy", strcpy_type);

        llvm::Value *dest_arg = args[0];
        llvm::Value *src_arg = args[1];

        if (!dest_arg->getType()->isPointerTy() || !src_arg->getType()->isPointerTy())
        {
            report_error("__strcpy__ arguments must be pointers");
            return nullptr;
        }

        // Call strcpy
        return builder.CreateCall(strcpy_func, {dest_arg, src_arg}, "strcpy.result");
    }

    llvm::Value *Intrinsics::generate_strcat(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 2)
        {
            report_error("__strcat__ requires exactly 2 arguments (dest, src)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        // Create strcat function type: char* strcat(char* dest, const char* src)
        llvm::Type *char_ptr_type = llvm::PointerType::get(context, 0);
        llvm::FunctionType *strcat_type = llvm::FunctionType::get(
            char_ptr_type, {char_ptr_type, char_ptr_type}, false);

        // Get or create strcat function
        llvm::Function *strcat_func = get_or_create_libc_function("strcat", strcat_type);

        llvm::Value *dest_arg = args[0];
        llvm::Value *src_arg = args[1];

        if (!dest_arg->getType()->isPointerTy() || !src_arg->getType()->isPointerTy())
        {
            report_error("__strcat__ arguments must be pointers");
            return nullptr;
        }

        // Call strcat
        return builder.CreateCall(strcat_func, {dest_arg, src_arg}, "strcat.result");
    }

    // ========================================
    // I/O Operations Intrinsics
    // ========================================

    llvm::Value *Intrinsics::generate_printf(const std::vector<llvm::Value *> &args)
    {
        if (args.empty())
        {
            report_error("__printf__ requires at least 1 argument (format)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();
        auto *module = _context_manager.get_module();

        // On Windows, printf is often not directly exported, so use vfprintf with stdout
        // Create vfprintf function type: int vfprintf(FILE* stream, const char* format, va_list args)
        llvm::Type *char_ptr_type = llvm::PointerType::get(context, 0); // FILE* and const char*
        llvm::Type *int_type = llvm::Type::getInt32Ty(context);
        llvm::FunctionType *vfprintf_type = llvm::FunctionType::get(
            int_type, {char_ptr_type, char_ptr_type}, true); // FILE*, format, ...

        // Get or create vfprintf function (this is available on Windows)
        llvm::Function *vfprintf_func = get_or_create_libc_function("vfprintf", vfprintf_type);

        // Add Windows calling convention
        if (module->getTargetTriple().find("windows") != std::string::npos)
        {
            vfprintf_func->setCallingConv(llvm::CallingConv::Win64);
        }

        // Get stdout - use __acrt_iob_func or __iob_func to get stdout
        // Create __acrt_iob_func function: FILE* __acrt_iob_func(int index)
        llvm::FunctionType *iob_func_type = llvm::FunctionType::get(
            char_ptr_type, {llvm::Type::getInt32Ty(context)}, false);
        llvm::Function *iob_func = get_or_create_libc_function("__acrt_iob_func", iob_func_type);
        
        if (module->getTargetTriple().find("windows") != std::string::npos)
        {
            iob_func->setCallingConv(llvm::CallingConv::Win64);
        }

        // Get stdout (index 1 for __acrt_iob_func: stdin=0, stdout=1, stderr=2)
        llvm::Value *stdout_index = llvm::ConstantInt::get(llvm::Type::getInt32Ty(context), 1);
        llvm::Value *stdout_file = builder.CreateCall(iob_func, {stdout_index}, "stdout");

        // Ensure format argument is a pointer
        if (!args[0]->getType()->isPointerTy())
        {
            report_error("__printf__ format argument must be a pointer");
            return nullptr;
        }

        // WINDOWS x64 ABI FIX: Convert arguments to ensure proper ABI compliance
        std::vector<llvm::Value *> converted_args;
        converted_args.reserve(args.size() + 1); // +1 for stdout

        // Add stdout as first argument
        converted_args.push_back(stdout_file);

        for (size_t i = 0; i < args.size(); ++i)
        {
            llvm::Value *arg = args[i];
            llvm::Type *arg_type = arg->getType();

            // For Windows x64 ABI: convert small integers to i32, keep i64 as i64
            if (arg_type->isIntegerTy())
            {
                unsigned bit_width = arg_type->getIntegerBitWidth();
                if (bit_width < 32)
                {
                    // Promote small integers to i32 for Windows ABI
                    arg = builder.CreateZExt(arg, llvm::Type::getInt32Ty(context), "printf.arg.promote");
                }
            }
            // For floating point: ensure f32 gets promoted to f64 in variadic calls
            else if (arg_type->isFloatTy())
            {
                arg = builder.CreateFPExt(arg, llvm::Type::getDoubleTy(context), "printf.arg.fpext");
            }

            converted_args.push_back(arg);
        }

        // Call vfprintf with properly converted arguments for Windows ABI
        llvm::CallInst *call = builder.CreateCall(vfprintf_func, converted_args, "printf.result");

        // WINDOWS x64 ABI FIX: Ensure the call uses the correct calling convention
        if (module->getTargetTriple().find("windows") != std::string::npos)
        {
            call->setCallingConv(llvm::CallingConv::Win64);
        }

        return call;
    }

    llvm::Value *Intrinsics::generate_sprintf(const std::vector<llvm::Value *> &args)
    {
        if (args.size() < 2)
        {
            report_error("__sprintf__ requires at least 2 arguments (buffer, format, ...)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        // Create sprintf function type: int sprintf(char* buffer, const char* format, ...)
        llvm::Type *char_ptr_type = llvm::PointerType::get(context, 0);
        llvm::Type *int_type = llvm::Type::getInt32Ty(context);
        llvm::FunctionType *sprintf_type = llvm::FunctionType::get(
            int_type, {char_ptr_type, char_ptr_type}, true); // variadic

        // Get or create sprintf function
        llvm::Function *sprintf_func = get_or_create_libc_function("sprintf", sprintf_type);

        // Ensure first two arguments are pointers
        if (!args[0]->getType()->isPointerTy() || !args[1]->getType()->isPointerTy())
        {
            report_error("__sprintf__ buffer and format arguments must be pointers");
            return nullptr;
        }

        // WINDOWS x64 ABI FIX: Set the correct calling convention for Windows variadic functions
        auto *module = _context_manager.get_module();
        if (module->getTargetTriple().find("windows") != std::string::npos)
        {
            sprintf_func->setCallingConv(llvm::CallingConv::Win64);
        }

        // WINDOWS x64 ABI FIX: Convert arguments to ensure proper ABI compliance
        std::vector<llvm::Value *> converted_args;
        converted_args.reserve(args.size());

        for (size_t i = 0; i < args.size(); ++i)
        {
            llvm::Value *arg = args[i];
            llvm::Type *arg_type = arg->getType();

            // For Windows x64 ABI: convert small integers to i32, keep i64 as i64
            if (arg_type->isIntegerTy())
            {
                unsigned bit_width = arg_type->getIntegerBitWidth();
                if (bit_width < 32)
                {
                    // Promote small integers to i32 for Windows ABI
                    arg = builder.CreateZExt(arg, llvm::Type::getInt32Ty(context), "sprintf.arg.promote");
                }
            }
            // For floating point: ensure f32 gets promoted to f64 in variadic calls
            else if (arg_type->isFloatTy())
            {
                arg = builder.CreateFPExt(arg, llvm::Type::getDoubleTy(context), "sprintf.arg.fpext");
            }

            converted_args.push_back(arg);
        }

        // Call sprintf with properly converted arguments for Windows ABI
        llvm::CallInst *call = builder.CreateCall(sprintf_func, converted_args, "sprintf.result");

        // WINDOWS x64 ABI FIX: Ensure the call uses the correct calling convention
        if (module->getTargetTriple().find("windows") != std::string::npos)
        {
            call->setCallingConv(llvm::CallingConv::Win64);
        }

        return call;
    }

    llvm::Value *Intrinsics::generate_fprintf(const std::vector<llvm::Value *> &args)
    {
        if (args.size() < 2)
        {
            report_error("__fprintf__ requires at least 2 arguments (file, format, ...)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        // Create fprintf function type: int fprintf(FILE* stream, const char* format, ...)
        llvm::Type *file_ptr_type = llvm::PointerType::get(context, 0); // FILE*
        llvm::Type *char_ptr_type = llvm::PointerType::get(context, 0);
        llvm::Type *int_type = llvm::Type::getInt32Ty(context);
        llvm::FunctionType *fprintf_type = llvm::FunctionType::get(
            int_type, {file_ptr_type, char_ptr_type}, true); // variadic

        // Get or create fprintf function
        llvm::Function *fprintf_func = get_or_create_libc_function("fprintf", fprintf_type);

        // Ensure first two arguments are pointers
        if (!args[0]->getType()->isPointerTy() || !args[1]->getType()->isPointerTy())
        {
            report_error("__fprintf__ file and format arguments must be pointers");
            return nullptr;
        }

        // Call fprintf
        return builder.CreateCall(fprintf_func, args, "fprintf.result");
    }

    llvm::Value *Intrinsics::generate_panic(const std::vector<llvm::Value *> &args)
    {
        // __panic__ can be called with 0 args (just panic) or 1 arg (panic with message)
        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        if (args.size() > 1)
        {
            report_error("__panic__ requires 0 or 1 arguments (optional message)");
            return nullptr;
        }

        if (!args.empty())
        {
            // If message provided, print it first
            llvm::Type *char_ptr_type = llvm::PointerType::get(context, 0);
            llvm::Type *int_type = llvm::Type::getInt32Ty(context);
            llvm::FunctionType *printf_type = llvm::FunctionType::get(
                int_type, {char_ptr_type}, true); // variadic

            llvm::Function *printf_func = get_or_create_libc_function("printf", printf_type);

            if (args[0]->getType()->isPointerTy())
            {
                // Print the panic message
                builder.CreateCall(printf_func, args, "panic.print");
            }
        }

        // Exit with status code 1 (indicating error)
        llvm::Value *exit_code = llvm::ConstantInt::get(llvm::Type::getInt64Ty(context), 1);
        std::vector<llvm::Value *> exit_args = {exit_code};

        return generate_syscall_exit(exit_args);
    }

    // ========================================
    // String Conversion Intrinsics
    // ========================================

    llvm::Value *Intrinsics::generate_float32_to_string(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 1)
        {
            report_error("__float32_to_string__ requires exactly 1 argument (f32_value)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();
        llvm::Module *module = _context_manager.get_module();

        // Check that argument is a float32 type
        llvm::Value *float_arg = args[0];
        if (!float_arg->getType()->isFloatTy())
        {
            report_error("__float32_to_string__ argument must be a f32 type");
            return nullptr;
        }

        // Allocate buffer for the string (enough for most float representations)
        llvm::Type *char_ptr_type = llvm::PointerType::get(context, 0);
        llvm::Constant *buffer_size = llvm::ConstantInt::get(llvm::Type::getInt32Ty(context), 64);

        // Create malloc call for buffer
        llvm::Function *malloc_func = get_or_create_libc_function("malloc",
                                                                  llvm::FunctionType::get(char_ptr_type, {llvm::Type::getInt64Ty(context)}, false));

        llvm::Value *buffer_size_64 = builder.CreateZExt(buffer_size, llvm::Type::getInt64Ty(context));
        llvm::Value *buffer = builder.CreateCall(malloc_func, {buffer_size_64}, "float32_str_buffer");

        // Create sprintf call: sprintf(buffer, "%.6f", float_value)
        llvm::Type *int_type = llvm::Type::getInt32Ty(context);
        llvm::FunctionType *sprintf_type = llvm::FunctionType::get(
            int_type, {char_ptr_type, char_ptr_type}, true); // variadic

        llvm::Function *sprintf_func = get_or_create_libc_function("sprintf", sprintf_type);

        // Create format string "%.6f" as a global constant
        std::string format_str = "%.6f";
        llvm::Constant *format_const = llvm::ConstantDataArray::getString(context, format_str, true);
        llvm::GlobalVariable *format_global = new llvm::GlobalVariable(
            *module, format_const->getType(), true, llvm::GlobalValue::PrivateLinkage,
            format_const, "float32_format_str");

        // Get pointer to the format string
        llvm::Value *format_ptr = builder.CreatePointerCast(format_global, char_ptr_type);

        // Convert float32 to double (sprintf %f expects double)
        llvm::Value *double_arg = builder.CreateFPExt(float_arg, llvm::Type::getDoubleTy(context), "f32_to_double");

        // Call sprintf(buffer, "%.6f", double_value)
        builder.CreateCall(sprintf_func, {buffer, format_ptr, double_arg}, "sprintf.f32.result");

        // Return the buffer (which is now a null-terminated string)
        return buffer;
    }

    llvm::Value *Intrinsics::generate_float64_to_string(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 1)
        {
            report_error("__float64_to_string__ requires exactly 1 argument (f64_value)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();
        llvm::Module *module = _context_manager.get_module();

        // Check that argument is a float64 type
        llvm::Value *float_arg = args[0];
        if (!float_arg->getType()->isDoubleTy())
        {
            report_error("__float64_to_string__ argument must be a f64 type");
            return nullptr;
        }

        // Allocate buffer for the string (enough for most float representations)
        llvm::Type *char_ptr_type = llvm::PointerType::get(context, 0);
        llvm::Constant *buffer_size = llvm::ConstantInt::get(llvm::Type::getInt32Ty(context), 64);

        // Create malloc call for buffer
        llvm::Function *malloc_func = get_or_create_libc_function("malloc",
                                                                  llvm::FunctionType::get(char_ptr_type, {llvm::Type::getInt64Ty(context)}, false));

        llvm::Value *buffer_size_64 = builder.CreateZExt(buffer_size, llvm::Type::getInt64Ty(context));
        llvm::Value *buffer = builder.CreateCall(malloc_func, {buffer_size_64}, "float64_str_buffer");

        // Create sprintf call: sprintf(buffer, "%.6f", double_value)
        llvm::Type *int_type = llvm::Type::getInt32Ty(context);
        llvm::FunctionType *sprintf_type = llvm::FunctionType::get(
            int_type, {char_ptr_type, char_ptr_type}, true); // variadic

        llvm::Function *sprintf_func = get_or_create_libc_function("sprintf", sprintf_type);

        // Create format string "%.15g" for better double precision
        std::string format_str = "%.15g";
        llvm::Constant *format_const = llvm::ConstantDataArray::getString(context, format_str, true);
        llvm::GlobalVariable *format_global = new llvm::GlobalVariable(
            *module, format_const->getType(), true, llvm::GlobalValue::PrivateLinkage,
            format_const, "float64_format_str");

        // Get pointer to the format string
        llvm::Value *format_ptr = builder.CreatePointerCast(format_global, char_ptr_type);

        // double is already the right type for sprintf %g
        // Call sprintf(buffer, "%.15g", double_value)
        builder.CreateCall(sprintf_func, {buffer, format_ptr, float_arg}, "sprintf.f64.result");

        // Return the buffer (which is now a null-terminated string)
        return buffer;
    }

    // ========================================
    // Math Operations Intrinsics
    // ========================================

    llvm::Value *Intrinsics::generate_sqrt(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 1)
        {
            report_error("__sqrt__ requires exactly 1 argument");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        llvm::Value *arg = args[0];
        llvm::Type *arg_type = arg->getType();

        // Use LLVM sqrt intrinsic
        llvm::Function *sqrt_intrinsic = llvm::Intrinsic::getDeclaration(
            _context_manager.get_module(),
            llvm::Intrinsic::sqrt,
            arg_type);

        return builder.CreateCall(sqrt_intrinsic, {arg}, "sqrt.result");
    }

    llvm::Value *Intrinsics::generate_pow(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 2)
        {
            report_error("__pow__ requires exactly 2 arguments");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        llvm::Value *base = args[0];
        llvm::Value *exp = args[1];

        // Use LLVM pow intrinsic
        llvm::Function *pow_intrinsic = llvm::Intrinsic::getDeclaration(
            _context_manager.get_module(),
            llvm::Intrinsic::pow,
            base->getType());

        return builder.CreateCall(pow_intrinsic, {base, exp}, "pow.result");
    }

    llvm::Value *Intrinsics::generate_sin(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 1)
        {
            report_error("__sin__ requires exactly 1 argument");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();

        llvm::Value *arg = args[0];

        // Use LLVM sin intrinsic
        llvm::Function *sin_intrinsic = llvm::Intrinsic::getDeclaration(
            _context_manager.get_module(),
            llvm::Intrinsic::sin,
            arg->getType());

        return builder.CreateCall(sin_intrinsic, {arg}, "sin.result");
    }

    llvm::Value *Intrinsics::generate_cos(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 1)
        {
            report_error("__cos__ requires exactly 1 argument");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();

        llvm::Value *arg = args[0];

        // Use LLVM cos intrinsic
        llvm::Function *cos_intrinsic = llvm::Intrinsic::getDeclaration(
            _context_manager.get_module(),
            llvm::Intrinsic::cos,
            arg->getType());

        return builder.CreateCall(cos_intrinsic, {arg}, "cos.result");
    }

    // ========================================
    // Process Management Intrinsics
    // ========================================

    llvm::Value *Intrinsics::generate_getpid(const std::vector<llvm::Value *> &args)
    {
        if (!args.empty())
        {
            report_error("__getpid__ requires no arguments");
            return nullptr;
        }

        auto &context = _context_manager.get_context();

        // On Linux x86_64, getpid syscall number is 39
        llvm::Value *syscall_num = llvm::ConstantInt::get(llvm::Type::getInt64Ty(context), 39);

        return create_syscall(syscall_num, {});
    }

    llvm::Value *Intrinsics::generate_fork(const std::vector<llvm::Value *> &args)
    {
        if (!args.empty())
        {
            report_error("__fork__ requires no arguments");
            return nullptr;
        }

        auto &context = _context_manager.get_context();

        // On Linux x86_64, fork syscall number is 57
        llvm::Value *syscall_num = llvm::ConstantInt::get(llvm::Type::getInt64Ty(context), 57);

        return create_syscall(syscall_num, {});
    }

    // ========================================
    // Helper Methods
    // ========================================

    void Intrinsics::report_error(const std::string &message)
    {
        _has_errors = true;
        _last_error = message;

        // Report to GDM if available
        if (_gdm)
        {
            Cryo::SourceRange dummy_range; // Default constructed range
            _gdm->create_error(Cryo::ErrorCode::E0600_CODEGEN_FAILED, dummy_range, "<intrinsics>");
        }

        std::cerr << "[Intrinsics] Error: " << message << std::endl;
    }

    void Intrinsics::report_unimplemented_intrinsic(const std::string &intrinsic_name, Cryo::CallExpressionNode *node)
    {
        _has_errors = true;
        _last_error = "Unimplemented intrinsic: " + intrinsic_name;

        // Report to GDM if available
        if (_gdm)
        {
            std::string message = "Intrinsic function '" + intrinsic_name + "' is called but not implemented";

            // Try to get source location if node is available
            if (node && node->location().line() > 0)
            {
                // Convert from lexer SourceLocation to GDM SourceRange
                const auto &loc = node->location();
                Cryo::SourceRange range(loc); // Use explicit constructor

                _gdm->create_error(Cryo::ErrorCode::E0604_UNIMPLEMENTED_INTRINSIC,
                                   range, "<source_file>");
            }
            else
            {
                // Fallback: report without location
                Cryo::SourceRange dummy_range; // Default constructed range
                _gdm->create_error(Cryo::ErrorCode::E0604_UNIMPLEMENTED_INTRINSIC,
                                   dummy_range, "<unknown>");
            }
        }

        // Still output to stderr as backup
        std::cerr << "[Intrinsics] Error: " << _last_error << std::endl;
    }

    void Intrinsics::clear_errors()
    {
        _has_errors = false;
        _last_error.clear();
    }

    llvm::Value *Intrinsics::create_syscall(llvm::Value *syscall_num, const std::vector<llvm::Value *> &args)
    {
        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        // Create syscall inline assembly based on number of arguments
        std::string asm_template;
        std::string constraints;
        std::vector<llvm::Value *> asm_args;

        // Add syscall number as first argument
        asm_args.push_back(syscall_num);

        if (args.empty())
        {
            // No arguments syscall
            asm_template = "syscall";
            constraints = "={rax},0";
        }
        else if (args.size() == 1)
        {
            // One argument syscall
            asm_template = "syscall";
            constraints = "={rax},0,{rdi}";
            asm_args.push_back(ensure_type(args[0], llvm::Type::getInt64Ty(context), "syscall.arg0"));
        }
        else if (args.size() == 2)
        {
            // Two argument syscall
            asm_template = "syscall";
            constraints = "={rax},0,{rdi},{rsi}";
            asm_args.push_back(ensure_type(args[0], llvm::Type::getInt64Ty(context), "syscall.arg0"));
            asm_args.push_back(ensure_type(args[1], llvm::Type::getInt64Ty(context), "syscall.arg1"));
        }
        else if (args.size() == 3)
        {
            // Three argument syscall
            asm_template = "syscall";
            constraints = "={rax},0,{rdi},{rsi},{rdx}";
            asm_args.push_back(ensure_type(args[0], llvm::Type::getInt64Ty(context), "syscall.arg0"));
            // For pointers in syscall_write/read, keep as pointer for rsi
            if (args[1]->getType()->isPointerTy())
            {
                asm_args.push_back(args[1]); // Keep buffer as pointer
            }
            else
            {
                asm_args.push_back(ensure_type(args[1], llvm::Type::getInt64Ty(context), "syscall.arg1"));
            }
            asm_args.push_back(ensure_type(args[2], llvm::Type::getInt64Ty(context), "syscall.arg2"));
        }
        else
        {
            report_error("Syscalls with more than 3 arguments not yet supported");
            return nullptr;
        }

        // Create function type for inline assembly
        std::vector<llvm::Type *> param_types;
        for (auto *arg : asm_args)
        {
            param_types.push_back(arg->getType());
        }

        llvm::FunctionType *syscall_type = llvm::FunctionType::get(
            llvm::Type::getInt64Ty(context), param_types, false);

        // Create inline assembly
        llvm::InlineAsm *syscall_asm = llvm::InlineAsm::get(
            syscall_type,
            asm_template,
            constraints,
            true, // Has side effects
            false // No align stack
        );

        // Call the inline assembly
        return builder.CreateCall(syscall_asm, asm_args, "syscall.result");
    }

    llvm::Value *Intrinsics::ensure_type(llvm::Value *value, llvm::Type *target_type, const std::string &name)
    {
        if (!value || !target_type)
            return value;

        auto &builder = _context_manager.get_builder();
        llvm::Type *current_type = value->getType();

        if (current_type == target_type)
            return value;

        // Handle integer type conversions
        if (current_type->isIntegerTy() && target_type->isIntegerTy())
        {
            unsigned current_width = current_type->getIntegerBitWidth();
            unsigned target_width = target_type->getIntegerBitWidth();

            if (current_width < target_width)
            {
                // Sign extend
                return builder.CreateSExt(value, target_type, name + ".sext");
            }
            else if (current_width > target_width)
            {
                // Truncate
                return builder.CreateTrunc(value, target_type, name + ".trunc");
            }
        }

        // Handle pointer to integer conversion
        if (current_type->isPointerTy() && target_type->isIntegerTy())
        {
            return builder.CreatePtrToInt(value, target_type, name + ".ptrtoint");
        }

        // Handle integer to pointer conversion
        if (current_type->isIntegerTy() && target_type->isPointerTy())
        {
            return builder.CreateIntToPtr(value, target_type, name + ".inttoptr");
        }

        // If we can't convert, return original value
        return value;
    }

    llvm::Function *Intrinsics::get_or_create_libc_function(const std::string &name, llvm::FunctionType *type)
    {
        auto *module = _context_manager.get_module();

        // Check if this is a runtime function that needs qualification
        static const std::set<std::string> runtime_functions = {
            "cryo_memcpy", "cryo_alloc", "cryo_free", "cryo_realloc",
            "cryo_malloc", "cryo_strlen", "cryo_strcmp", "cryo_strcpy", "cryo_strcat"};

        std::string function_name = name;
        if (runtime_functions.find(name) != runtime_functions.end())
        {
            function_name = "std::Runtime::" + name;
        }

        llvm::Function *func = module->getFunction(function_name);

        if (!func)
        {
            // Create external function declaration
            func = llvm::Function::Create(
                type,
                llvm::Function::ExternalLinkage,
                function_name,
                module);
        }

        return func;
    }

    // ========================================
    // Type Conversion Intrinsics
    // ========================================

    llvm::Value *Intrinsics::generate_integer_extension(const std::vector<llvm::Value *> &args,
                                                        unsigned source_bits, unsigned target_bits, bool is_signed)
    {
        if (args.size() != 1)
        {
            report_error("Integer extension requires exactly 1 argument");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        llvm::Type *target_type = llvm::Type::getIntNTy(context, target_bits);

        if (is_signed)
        {
            return builder.CreateSExt(args[0], target_type, "sext");
        }
        else
        {
            return builder.CreateZExt(args[0], target_type, "zext");
        }
    }

    llvm::Value *Intrinsics::generate_integer_truncation(const std::vector<llvm::Value *> &args,
                                                         unsigned source_bits, unsigned target_bits)
    {
        if (args.size() != 1)
        {
            report_error("Integer truncation requires exactly 1 argument");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        llvm::Type *target_type = llvm::Type::getIntNTy(context, target_bits);
        return builder.CreateTrunc(args[0], target_type, "trunc");
    }

    llvm::Value *Intrinsics::generate_sign_conversion(const std::vector<llvm::Value *> &args, unsigned bit_width)
    {
        if (args.size() != 1)
        {
            report_error("Sign conversion requires exactly 1 argument");
            return nullptr;
        }

        // For same-width sign conversions, no actual LLVM instruction is needed
        // LLVM treats integers as bit patterns - the interpretation is contextual
        return args[0];
    }

    llvm::Value *Intrinsics::generate_checked_conversion(const std::vector<llvm::Value *> &args,
                                                         unsigned source_bits, unsigned target_bits,
                                                         bool source_signed, bool target_signed)
    {
        if (args.size() != 1)
        {
            report_error("Checked conversion requires exactly 1 argument");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        llvm::Value *source_val = args[0];
        llvm::Type *target_type = llvm::Type::getIntNTy(context, target_bits);

        // Create bounds for overflow checking
        llvm::Value *min_val = nullptr;
        llvm::Value *max_val = nullptr;

        if (target_signed)
        {
            // For signed target: -2^(n-1) to 2^(n-1)-1
            int64_t max_signed = (1LL << (target_bits - 1)) - 1;
            int64_t min_signed = -(1LL << (target_bits - 1));

            max_val = llvm::ConstantInt::get(source_val->getType(), max_signed, true);
            min_val = llvm::ConstantInt::get(source_val->getType(), min_signed, true);
        }
        else
        {
            // For unsigned target: 0 to 2^n-1
            uint64_t max_unsigned = (target_bits == 64) ? UINT64_MAX : (1ULL << target_bits) - 1;

            max_val = llvm::ConstantInt::get(source_val->getType(), max_unsigned, false);
            min_val = llvm::ConstantInt::get(source_val->getType(), 0, false);
        }

        // Create overflow checks
        llvm::Value *too_large = builder.CreateICmpSGT(source_val, max_val, "too_large");
        llvm::Value *too_small = builder.CreateICmpSLT(source_val, min_val, "too_small");
        llvm::Value *overflow = builder.CreateOr(too_large, too_small, "overflow");

        // Create the actual conversion
        llvm::Value *converted;
        if (target_bits < source_bits)
        {
            converted = builder.CreateTrunc(source_val, target_type, "trunc_conv");
        }
        else if (target_bits > source_bits)
        {
            if (source_signed)
                converted = builder.CreateSExt(source_val, target_type, "sext_conv");
            else
                converted = builder.CreateZExt(source_val, target_type, "zext_conv");
        }
        else
        {
            // Same size - just bitcast
            converted = source_val;
        }

        // For checked conversions, we need to return an optional type
        // This is simplified - in a full implementation you'd create an optional struct
        // For now, return null on overflow, the converted value otherwise
        llvm::Type *result_type = llvm::PointerType::get(target_type, 0);
        llvm::Value *null_ptr = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(result_type));

        // Allocate space for the result
        llvm::Value *result_ptr = builder.CreateAlloca(target_type, nullptr, "result");
        builder.CreateStore(converted, result_ptr);

        // Return null on overflow, pointer to result otherwise
        return builder.CreateSelect(overflow, null_ptr, result_ptr, "checked_result");
    }

    llvm::Value *Intrinsics::generate_maloc(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 1)
        {
            report_error("__maloc__ requires exactly 1 argument (size)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        llvm::Type *char_ptr_type = llvm::PointerType::get(context, 0);

        // Create malloc function type: void* malloc(size_t size)
        llvm::FunctionType *malloc_type = llvm::FunctionType::get(
            char_ptr_type, {llvm::Type::getInt64Ty(context)}, false);

        // Get or create malloc function
        llvm::Function *malloc_func = get_or_create_libc_function("malloc", malloc_type);

        llvm::Value *size_arg = args[0];
        llvm::Value *size_arg_64 = ensure_type(size_arg, llvm::Type::getInt64Ty(context), "maloc.size");

        // Call malloc
        return builder.CreateCall(malloc_func, {size_arg_64}, "maloc.result");
    }

    llvm::Value *Intrinsics::generate_mfree(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 1)
        {
            report_error("__mfree__ requires exactly 1 argument (pointer)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        // Create free function type: void free(void* ptr)
        llvm::Type *char_ptr_type = llvm::PointerType::get(context, 0);
        llvm::FunctionType *free_type = llvm::FunctionType::get(
            llvm::Type::getVoidTy(context), {char_ptr_type}, false);

        // Get or create free function
        llvm::Function *free_func = get_or_create_libc_function("free", free_type);

        llvm::Value *ptr_arg = args[0];

        if (!ptr_arg->getType()->isPointerTy())
        {
            report_error("__mfree__ argument must be a pointer");
            return nullptr;
        }

        // Call free
        return builder.CreateCall(free_func, {ptr_arg}, "mfree.result");
    }

    llvm::Value *Intrinsics::generate_msize(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 1)
        {
            report_error("__msize__ requires exactly 1 argument (pointer)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        // Create malloc_usable_size function type: size_t malloc_usable_size(void* ptr)
        llvm::Type *char_ptr_type = llvm::PointerType::get(context, 0);
        llvm::FunctionType *msize_type = llvm::FunctionType::get(
            llvm::Type::getInt64Ty(context), {char_ptr_type}, false);

        // Get or create malloc_usable_size function
        llvm::Function *msize_func = get_or_create_libc_function("malloc_usable_size", msize_type);

        llvm::Value *ptr_arg = args[0];

        if (!ptr_arg->getType()->isPointerTy())
        {
            report_error("__msize__ argument must be a pointer");
            return nullptr;
        }

        // Call malloc_usable_size
        return builder.CreateCall(msize_func, {ptr_arg}, "msize.result");
    }

    llvm::Value *Intrinsics::generate_exit(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 1)
        {
            report_error("__exit__ requires exactly 1 argument (exit_code)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        // Create exit function type: void exit(int status)
        llvm::Type *int_type = llvm::Type::getInt32Ty(context);
        llvm::FunctionType *exit_type = llvm::FunctionType::get(
            llvm::Type::getVoidTy(context), {int_type}, false);

        // Get or create exit function
        llvm::Function *exit_func = get_or_create_libc_function("exit", exit_type);

        llvm::Value *code_arg = args[0];
        llvm::Value *code_arg_32 = ensure_type(code_arg, int_type, "exit.code");

        // Call exit - exit() is a void function so no result name
        llvm::Value *call_result = builder.CreateCall(exit_func, {code_arg_32});

        // Since exit() never returns, we should add an unreachable instruction
        builder.CreateUnreachable();

        // Return the call instruction as the result (even though exit is void)
        return call_result;
    }

} // namespace Cryo::Codegen
