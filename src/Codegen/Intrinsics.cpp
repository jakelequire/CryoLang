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

        // Memory allocation intrinsics
        if (intrinsic_name == "malloc")
            return generate_malloc(args);
        else if (intrinsic_name == "calloc")
            return generate_calloc(args);
        else if (intrinsic_name == "realloc")
            return generate_realloc(args);
        else if (intrinsic_name == "free")
            return generate_free(args);
        else if (intrinsic_name == "aligned_alloc")
            return generate_aligned_alloc(args);
        
        // Memory operations intrinsics
        else if (intrinsic_name == "memcpy")
            return generate_memcpy(args);
        else if (intrinsic_name == "memmove")
            return generate_memmove(args);
        else if (intrinsic_name == "memset")
            return generate_memset(args);
        else if (intrinsic_name == "memcmp")
            return generate_memcmp(args);
        else if (intrinsic_name == "memchr")
            return generate_memchr(args);
        
        // String intrinsics
        else if (intrinsic_name == "strlen")
            return generate_strlen(args);
        else if (intrinsic_name == "strcmp")
            return generate_strcmp(args);
        else if (intrinsic_name == "strncmp")
            return generate_strncmp(args);
        else if (intrinsic_name == "strcpy")
            return generate_strcpy(args);
        else if (intrinsic_name == "strncpy")
            return generate_strncpy(args);
        else if (intrinsic_name == "strcat")
            return generate_strcat(args);
        else if (intrinsic_name == "strchr")
            return generate_strchr(args);
        else if (intrinsic_name == "strrchr")
            return generate_strrchr(args);
        else if (intrinsic_name == "strstr")
            return generate_strstr(args);
        else if (intrinsic_name == "strdup")
            return generate_strdup(args);
        
        // I/O intrinsics
        else if (intrinsic_name == "printf")
            return generate_printf(args);
        else if (intrinsic_name == "snprintf")
            return generate_snprintf(args);
        else if (intrinsic_name == "getchar")
            return generate_getchar(args);
        else if (intrinsic_name == "putchar")
            return generate_putchar(args);
        else if (intrinsic_name == "puts")
            return generate_puts(args);
        
        // File I/O intrinsics
        else if (intrinsic_name == "fopen")
            return generate_fopen(args);
        else if (intrinsic_name == "fclose")
            return generate_fclose(args);
        else if (intrinsic_name == "fread")
            return generate_fread(args);
        else if (intrinsic_name == "fwrite")
            return generate_fwrite(args);
        else if (intrinsic_name == "fseek")
            return generate_fseek(args);
        else if (intrinsic_name == "ftell")
            return generate_ftell(args);
        else if (intrinsic_name == "fflush")
            return generate_fflush(args);
        else if (intrinsic_name == "feof")
            return generate_feof(args);
        else if (intrinsic_name == "ferror")
            return generate_ferror(args);
        
        // Low-level file descriptor I/O
        else if (intrinsic_name == "read")
            return generate_read(args);
        else if (intrinsic_name == "write")
            return generate_write(args);
        else if (intrinsic_name == "open")
            return generate_open(args);
        else if (intrinsic_name == "close")
            return generate_close(args);
        else if (intrinsic_name == "lseek")
            return generate_lseek(args);
        else if (intrinsic_name == "dup")
            return generate_dup(args);
        else if (intrinsic_name == "dup2")
            return generate_dup2(args);
        else if (intrinsic_name == "pipe")
            return generate_pipe(args);
        else if (intrinsic_name == "fcntl")
            return generate_fcntl(args);
        
        // Filesystem intrinsics
        else if (intrinsic_name == "stat")
            return generate_stat(args);
        else if (intrinsic_name == "fstat")
            return generate_fstat(args);
        else if (intrinsic_name == "lstat")
            return generate_lstat(args);
        else if (intrinsic_name == "access")
            return generate_access(args);
        else if (intrinsic_name == "mkdir")
            return generate_mkdir(args);
        else if (intrinsic_name == "rmdir")
            return generate_rmdir(args);
        else if (intrinsic_name == "unlink")
            return generate_unlink(args);
        else if (intrinsic_name == "rename")
            return generate_rename(args);
        else if (intrinsic_name == "symlink")
            return generate_symlink(args);
        else if (intrinsic_name == "readlink")
            return generate_readlink(args);
        else if (intrinsic_name == "truncate")
            return generate_truncate(args);
        else if (intrinsic_name == "ftruncate")
            return generate_ftruncate(args);
        else if (intrinsic_name == "chmod")
            return generate_chmod(args);
        else if (intrinsic_name == "chown")
            return generate_chown(args);
        else if (intrinsic_name == "getcwd")
            return generate_getcwd(args);
        else if (intrinsic_name == "chdir")
            return generate_chdir(args);
        else if (intrinsic_name == "opendir")
            return generate_opendir(args);
        else if (intrinsic_name == "readdir")
            return generate_readdir(args);
        else if (intrinsic_name == "closedir")
            return generate_closedir(args);
        
        // Process intrinsics
        else if (intrinsic_name == "exit")
            return generate_exit(args);
        else if (intrinsic_name == "abort")
            return generate_abort(args);
        else if (intrinsic_name == "fork")
            return generate_fork(args);
        else if (intrinsic_name == "execvp")
            return generate_execvp(args);
        else if (intrinsic_name == "wait")
            return generate_wait(args);
        else if (intrinsic_name == "waitpid")
            return generate_waitpid(args);
        else if (intrinsic_name == "getpid")
            return generate_getpid(args);
        else if (intrinsic_name == "getppid")
            return generate_getppid(args);
        else if (intrinsic_name == "getuid")
            return generate_getuid(args);
        else if (intrinsic_name == "getgid")
            return generate_getgid(args);
        else if (intrinsic_name == "geteuid")
            return generate_geteuid(args);
        else if (intrinsic_name == "getegid")
            return generate_getegid(args);
        else if (intrinsic_name == "kill")
            return generate_kill(args);
        else if (intrinsic_name == "raise")
            return generate_raise(args);
        else if (intrinsic_name == "signal")
            return generate_signal(args);
        
        // Math intrinsics
        else if (intrinsic_name == "sqrt")
            return generate_sqrt(args);
        else if (intrinsic_name == "sqrtf")
            return generate_sqrtf(args);
        else if (intrinsic_name == "pow")
            return generate_pow(args);
        else if (intrinsic_name == "powf")
            return generate_powf(args);
        else if (intrinsic_name == "exp")
            return generate_exp(args);
        else if (intrinsic_name == "expf")
            return generate_expf(args);
        else if (intrinsic_name == "exp2")
            return generate_exp2(args);
        else if (intrinsic_name == "expm1")
            return generate_expm1(args);
        else if (intrinsic_name == "log")
            return generate_log(args);
        else if (intrinsic_name == "logf")
            return generate_logf(args);
        else if (intrinsic_name == "log10")
            return generate_log10(args);
        else if (intrinsic_name == "log2")
            return generate_log2(args);
        else if (intrinsic_name == "log1p")
            return generate_log1p(args);
        else if (intrinsic_name == "sin")
            return generate_sin(args);
        else if (intrinsic_name == "sinf")
            return generate_sinf(args);
        else if (intrinsic_name == "cos")
            return generate_cos(args);
        else if (intrinsic_name == "cosf")
            return generate_cosf(args);
        else if (intrinsic_name == "tan")
            return generate_tan(args);
        else if (intrinsic_name == "tanf")
            return generate_tanf(args);
        else if (intrinsic_name == "asin")
            return generate_asin(args);
        else if (intrinsic_name == "acos")
            return generate_acos(args);
        else if (intrinsic_name == "atan")
            return generate_atan(args);
        else if (intrinsic_name == "atan2")
            return generate_atan2(args);
        else if (intrinsic_name == "sinh")
            return generate_sinh(args);
        else if (intrinsic_name == "cosh")
            return generate_cosh(args);
        else if (intrinsic_name == "tanh")
            return generate_tanh(args);
        else if (intrinsic_name == "asinh")
            return generate_asinh(args);
        else if (intrinsic_name == "acosh")
            return generate_acosh(args);
        else if (intrinsic_name == "atanh")
            return generate_atanh(args);
        else if (intrinsic_name == "cbrt")
            return generate_cbrt(args);
        else if (intrinsic_name == "hypot")
            return generate_hypot(args);
        else if (intrinsic_name == "fabs")
            return generate_fabs(args);
        else if (intrinsic_name == "fabsf")
            return generate_fabsf(args);
        else if (intrinsic_name == "floor")
            return generate_floor(args);
        else if (intrinsic_name == "floorf")
            return generate_floorf(args);
        else if (intrinsic_name == "ceil")
            return generate_ceil(args);
        else if (intrinsic_name == "ceilf")
            return generate_ceilf(args);
        else if (intrinsic_name == "round")
            return generate_round(args);
        else if (intrinsic_name == "roundf")
            return generate_roundf(args);
        else if (intrinsic_name == "trunc")
            return generate_trunc(args);
        else if (intrinsic_name == "fmod")
            return generate_fmod(args);
        else if (intrinsic_name == "remainder")
            return generate_remainder(args);
        else if (intrinsic_name == "fmin")
            return generate_fmin(args);
        else if (intrinsic_name == "fmax")
            return generate_fmax(args);
        else if (intrinsic_name == "fma")
            return generate_fma(args);
        else if (intrinsic_name == "copysign")
            return generate_copysign(args);
        else if (intrinsic_name == "nextafter")
            return generate_nextafter(args);
        else if (intrinsic_name == "frexp")
            return generate_frexp(args);
        else if (intrinsic_name == "ldexp")
            return generate_ldexp(args);
        else if (intrinsic_name == "modf")
            return generate_modf(args);
        else if (intrinsic_name == "erf")
            return generate_erf(args);
        else if (intrinsic_name == "erfc")
            return generate_erfc(args);
        
        // Network intrinsics
        else if (intrinsic_name == "socket")
            return generate_socket(args);
        else if (intrinsic_name == "bind")
            return generate_bind(args);
        else if (intrinsic_name == "listen")
            return generate_listen(args);
        else if (intrinsic_name == "accept")
            return generate_accept(args);
        else if (intrinsic_name == "connect")
            return generate_connect(args);
        else if (intrinsic_name == "send")
            return generate_send(args);
        else if (intrinsic_name == "recv")
            return generate_recv(args);
        else if (intrinsic_name == "sendto")
            return generate_sendto(args);
        else if (intrinsic_name == "recvfrom")
            return generate_recvfrom(args);
        else if (intrinsic_name == "shutdown")
            return generate_shutdown(args);
        else if (intrinsic_name == "setsockopt")
            return generate_setsockopt(args);
        else if (intrinsic_name == "getsockopt")
            return generate_getsockopt(args);
        else if (intrinsic_name == "getsockname")
            return generate_getsockname(args);
        else if (intrinsic_name == "getpeername")
            return generate_getpeername(args);
        else if (intrinsic_name == "poll")
            return generate_poll(args);
        else if (intrinsic_name == "htons")
            return generate_htons(args);
        else if (intrinsic_name == "ntohs")
            return generate_ntohs(args);
        else if (intrinsic_name == "htonl")
            return generate_htonl(args);
        else if (intrinsic_name == "ntohl")
            return generate_ntohl(args);
        
        // Time intrinsics
        else if (intrinsic_name == "time")
            return generate_time(args);
        else if (intrinsic_name == "gettimeofday")
            return generate_gettimeofday(args);
        else if (intrinsic_name == "clock_gettime")
            return generate_clock_gettime(args);
        else if (intrinsic_name == "nanosleep")
            return generate_nanosleep(args);
        else if (intrinsic_name == "sleep")
            return generate_sleep(args);
        else if (intrinsic_name == "usleep")
            return generate_usleep(args);
        
        // Note: Threading, atomic, and other complex intrinsics would require
        // much more extensive implementation and platform-specific considerations
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

        // Create printf function type: int printf(const char* format, ...)
        llvm::Type *char_ptr_type = llvm::PointerType::get(context, 0);
        llvm::Type *int_type = llvm::Type::getInt32Ty(context);
        llvm::FunctionType *printf_type = llvm::FunctionType::get(
            int_type, {char_ptr_type}, true); // format, ... (variadic)

        // Get or create printf function
        llvm::Function *printf_func = get_or_create_libc_function("printf", printf_type);

        // Note: Don't explicitly set calling convention - let LLVM use the default
        // based on the target triple. The old codegen didn't set any calling convention
        // and worked correctly.

        // Ensure format argument is a pointer
        if (!args[0]->getType()->isPointerTy())
        {
            report_error("__printf__ format argument must be a pointer");
            return nullptr;
        }

        // Convert arguments for variadic call ABI compliance
        std::vector<llvm::Value *> converted_args;
        converted_args.reserve(args.size());

        for (size_t i = 0; i < args.size(); ++i)
        {
            llvm::Value *arg = args[i];
            llvm::Type *arg_type = arg->getType();

            // Promote small integers to i32 for variadic calls
            if (arg_type->isIntegerTy())
            {
                unsigned bit_width = arg_type->getIntegerBitWidth();
                if (bit_width < 32)
                {
                    arg = builder.CreateZExt(arg, llvm::Type::getInt32Ty(context), "printf.arg.promote");
                }
            }
            // Promote f32 to f64 for variadic calls (C standard)
            else if (arg_type->isFloatTy())
            {
                arg = builder.CreateFPExt(arg, llvm::Type::getDoubleTy(context), "printf.arg.fpext");
            }

            converted_args.push_back(arg);
        }

        // Call printf
        llvm::CallInst *call = builder.CreateCall(printf_func, converted_args, "printf.result");

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

        // Convert arguments for variadic call ABI compliance
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

        // Call sprintf
        llvm::CallInst *call = builder.CreateCall(sprintf_func, converted_args, "sprintf.result");

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
        
        // System functions use their original names (printf, malloc, etc.)
        // The namespace qualification in DeclarationCodegen ensures stdlib functions
        // like std::IO::printf don't conflict with these system function names

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

    // ========================================
    // Additional Memory Allocation Intrinsics
    // ========================================

    llvm::Value *Intrinsics::generate_aligned_alloc(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 2)
        {
            report_error("aligned_alloc requires exactly 2 arguments (alignment, size)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        // Create aligned_alloc function type: void* aligned_alloc(size_t alignment, size_t size)
        llvm::Type *size_t_type = llvm::Type::getInt64Ty(context);
        llvm::Type *void_ptr_type = llvm::PointerType::get(context, 0);
        llvm::FunctionType *aligned_alloc_type = llvm::FunctionType::get(
            void_ptr_type, {size_t_type, size_t_type}, false);

        // Get or create aligned_alloc function
        llvm::Function *aligned_alloc_func = get_or_create_libc_function("aligned_alloc", aligned_alloc_type);

        // Prepare arguments
        llvm::Value *alignment_arg = ensure_type(args[0], size_t_type, "aligned_alloc.alignment");
        llvm::Value *size_arg = ensure_type(args[1], size_t_type, "aligned_alloc.size");

        // Call aligned_alloc
        return builder.CreateCall(aligned_alloc_func, {alignment_arg, size_arg}, "aligned_alloc.result");
    }

    llvm::Value *Intrinsics::generate_memchr(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 3)
        {
            report_error("memchr requires exactly 3 arguments (ptr, value, count)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        // Create memchr function type: void* memchr(const void* ptr, int value, size_t count)
        llvm::Type *void_ptr_type = llvm::PointerType::get(context, 0);
        llvm::Type *int_type = llvm::Type::getInt32Ty(context);
        llvm::Type *size_t_type = llvm::Type::getInt64Ty(context);
        llvm::FunctionType *memchr_type = llvm::FunctionType::get(
            void_ptr_type, {void_ptr_type, int_type, size_t_type}, false);

        // Get or create memchr function
        llvm::Function *memchr_func = get_or_create_libc_function("memchr", memchr_type);

        // Prepare arguments
        llvm::Value *ptr = args[0];
        llvm::Value *value = ensure_type(args[1], int_type, "memchr.value");
        llvm::Value *count = ensure_type(args[2], size_t_type, "memchr.count");

        if (!ptr->getType()->isPointerTy())
        {
            report_error("memchr: first argument must be a pointer");
            return nullptr;
        }

        // Call memchr
        return builder.CreateCall(memchr_func, {ptr, value, count}, "memchr.result");
    }

    // ========================================
    // Additional String Intrinsics
    // ========================================

    llvm::Value *Intrinsics::generate_strncmp(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 3)
        {
            report_error("strncmp requires exactly 3 arguments (s1, s2, count)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        // Create strncmp function type: int strncmp(const char* s1, const char* s2, size_t count)
        llvm::Type *char_ptr_type = llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
        llvm::Type *size_t_type = llvm::Type::getInt64Ty(context);
        llvm::Type *int_type = llvm::Type::getInt32Ty(context);
        llvm::FunctionType *strncmp_type = llvm::FunctionType::get(
            int_type, {char_ptr_type, char_ptr_type, size_t_type}, false);

        // Get or create strncmp function
        llvm::Function *strncmp_func = get_or_create_libc_function("strncmp", strncmp_type);

        // Prepare arguments
        llvm::Value *s1 = args[0];
        llvm::Value *s2 = args[1];
        llvm::Value *count = ensure_type(args[2], size_t_type, "strncmp.count");

        if (!s1->getType()->isPointerTy() || !s2->getType()->isPointerTy())
        {
            report_error("strncmp: first two arguments must be string pointers");
            return nullptr;
        }

        // Call strncmp
        return builder.CreateCall(strncmp_func, {s1, s2, count}, "strncmp.result");
    }

    llvm::Value *Intrinsics::generate_strncpy(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 3)
        {
            report_error("strncpy requires exactly 3 arguments (dest, src, count)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        // Create strncpy function type: char* strncpy(char* dest, const char* src, size_t count)
        llvm::Type *char_ptr_type = llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
        llvm::Type *size_t_type = llvm::Type::getInt64Ty(context);
        llvm::FunctionType *strncpy_type = llvm::FunctionType::get(
            char_ptr_type, {char_ptr_type, char_ptr_type, size_t_type}, false);

        // Get or create strncpy function
        llvm::Function *strncpy_func = get_or_create_libc_function("strncpy", strncpy_type);

        // Prepare arguments
        llvm::Value *dest = args[0];
        llvm::Value *src = args[1];
        llvm::Value *count = ensure_type(args[2], size_t_type, "strncpy.count");

        if (!dest->getType()->isPointerTy() || !src->getType()->isPointerTy())
        {
            report_error("strncpy: first two arguments must be string pointers");
            return nullptr;
        }

        // Call strncpy
        return builder.CreateCall(strncpy_func, {dest, src, count}, "strncpy.result");
    }

    llvm::Value *Intrinsics::generate_strchr(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 2)
        {
            report_error("strchr requires exactly 2 arguments (str, c)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        // Create strchr function type: char* strchr(const char* str, int c)
        llvm::Type *char_ptr_type = llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
        llvm::Type *int_type = llvm::Type::getInt32Ty(context);
        llvm::FunctionType *strchr_type = llvm::FunctionType::get(
            char_ptr_type, {char_ptr_type, int_type}, false);

        // Get or create strchr function
        llvm::Function *strchr_func = get_or_create_libc_function("strchr", strchr_type);

        // Prepare arguments
        llvm::Value *str = args[0];
        llvm::Value *c = ensure_type(args[1], int_type, "strchr.c");

        if (!str->getType()->isPointerTy())
        {
            report_error("strchr: first argument must be a string pointer");
            return nullptr;
        }

        // Call strchr
        return builder.CreateCall(strchr_func, {str, c}, "strchr.result");
    }

    llvm::Value *Intrinsics::generate_strrchr(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 2)
        {
            report_error("strrchr requires exactly 2 arguments (str, c)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        // Create strrchr function type: char* strrchr(const char* str, int c)
        llvm::Type *char_ptr_type = llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
        llvm::Type *int_type = llvm::Type::getInt32Ty(context);
        llvm::FunctionType *strrchr_type = llvm::FunctionType::get(
            char_ptr_type, {char_ptr_type, int_type}, false);

        // Get or create strrchr function
        llvm::Function *strrchr_func = get_or_create_libc_function("strrchr", strrchr_type);

        // Prepare arguments
        llvm::Value *str = args[0];
        llvm::Value *c = ensure_type(args[1], int_type, "strrchr.c");

        if (!str->getType()->isPointerTy())
        {
            report_error("strrchr: first argument must be a string pointer");
            return nullptr;
        }

        // Call strrchr
        return builder.CreateCall(strrchr_func, {str, c}, "strrchr.result");
    }

    llvm::Value *Intrinsics::generate_strstr(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 2)
        {
            report_error("strstr requires exactly 2 arguments (haystack, needle)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        // Create strstr function type: char* strstr(const char* haystack, const char* needle)
        llvm::Type *char_ptr_type = llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
        llvm::FunctionType *strstr_type = llvm::FunctionType::get(
            char_ptr_type, {char_ptr_type, char_ptr_type}, false);

        // Get or create strstr function
        llvm::Function *strstr_func = get_or_create_libc_function("strstr", strstr_type);

        // Prepare arguments
        llvm::Value *haystack = args[0];
        llvm::Value *needle = args[1];

        if (!haystack->getType()->isPointerTy() || !needle->getType()->isPointerTy())
        {
            report_error("strstr: both arguments must be string pointers");
            return nullptr;
        }

        // Call strstr
        return builder.CreateCall(strstr_func, {haystack, needle}, "strstr.result");
    }

    llvm::Value *Intrinsics::generate_strdup(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 1)
        {
            report_error("strdup requires exactly 1 argument (str)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        // Create strdup function type: char* strdup(const char* str)
        llvm::Type *char_ptr_type = llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
        llvm::FunctionType *strdup_type = llvm::FunctionType::get(
            char_ptr_type, {char_ptr_type}, false);

        // Get or create strdup function
        llvm::Function *strdup_func = get_or_create_libc_function("strdup", strdup_type);

        // Prepare arguments
        llvm::Value *str = args[0];

        if (!str->getType()->isPointerTy())
        {
            report_error("strdup: argument must be a string pointer");
            return nullptr;
        }

        // Call strdup
        return builder.CreateCall(strdup_func, {str}, "strdup.result");
    }

    // ========================================
    // Additional I/O Intrinsics
    // ========================================

    llvm::Value *Intrinsics::generate_snprintf(const std::vector<llvm::Value *> &args)
    {
        if (args.size() < 3)
        {
            report_error("snprintf requires at least 3 arguments (buffer, size, format, ...)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        // Create variadic snprintf function type
        llvm::Type *char_ptr_type = llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
        llvm::Type *size_t_type = llvm::Type::getInt64Ty(context);
        llvm::Type *int_type = llvm::Type::getInt32Ty(context);
        llvm::FunctionType *snprintf_type = llvm::FunctionType::get(
            int_type, {char_ptr_type, size_t_type, char_ptr_type}, true);

        // Get or create snprintf function
        llvm::Function *snprintf_func = get_or_create_libc_function("snprintf", snprintf_type);

        // Prepare arguments
        std::vector<llvm::Value *> call_args;
        call_args.push_back(args[0]); // buffer
        call_args.push_back(ensure_type(args[1], size_t_type, "snprintf.size")); // size
        call_args.push_back(args[2]); // format

        // Add remaining variadic arguments
        for (size_t i = 3; i < args.size(); ++i)
        {
            call_args.push_back(args[i]);
        }

        // Call snprintf
        return builder.CreateCall(snprintf_func, call_args, "snprintf.result");
    }

    llvm::Value *Intrinsics::generate_getchar(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 0)
        {
            report_error("getchar requires exactly 0 arguments");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        // Create getchar function type: int getchar(void)
        llvm::Type *int_type = llvm::Type::getInt32Ty(context);
        llvm::FunctionType *getchar_type = llvm::FunctionType::get(int_type, {}, false);

        // Get or create getchar function
        llvm::Function *getchar_func = get_or_create_libc_function("getchar", getchar_type);

        // Call getchar
        return builder.CreateCall(getchar_func, {}, "getchar.result");
    }

    llvm::Value *Intrinsics::generate_putchar(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 1)
        {
            report_error("putchar requires exactly 1 argument (c)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        // Create putchar function type: int putchar(int c)
        llvm::Type *int_type = llvm::Type::getInt32Ty(context);
        llvm::FunctionType *putchar_type = llvm::FunctionType::get(int_type, {int_type}, false);

        // Get or create putchar function
        llvm::Function *putchar_func = get_or_create_libc_function("putchar", putchar_type);

        // Prepare arguments
        llvm::Value *c = ensure_type(args[0], int_type, "putchar.c");

        // Call putchar
        return builder.CreateCall(putchar_func, {c}, "putchar.result");
    }

    llvm::Value *Intrinsics::generate_puts(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 1)
        {
            report_error("puts requires exactly 1 argument (str)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        // Create puts function type: int puts(const char* str)
        llvm::Type *char_ptr_type = llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
        llvm::Type *int_type = llvm::Type::getInt32Ty(context);
        llvm::FunctionType *puts_type = llvm::FunctionType::get(int_type, {char_ptr_type}, false);

        // Get or create puts function
        llvm::Function *puts_func = get_or_create_libc_function("puts", puts_type);

        // Prepare arguments
        llvm::Value *str = args[0];

        if (!str->getType()->isPointerTy())
        {
            report_error("puts: argument must be a string pointer");
            return nullptr;
        }

        // Call puts
        return builder.CreateCall(puts_func, {str}, "puts.result");
    }

    // ========================================
    // File I/O Intrinsics  
    // ========================================

    llvm::Value *Intrinsics::generate_fopen(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 2)
        {
            report_error("fopen requires exactly 2 arguments (path, mode)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        // Create fopen function type: FILE* fopen(const char* path, const char* mode)
        llvm::Type *char_ptr_type = llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
        llvm::Type *void_ptr_type = llvm::PointerType::get(context, 0); // FILE*
        llvm::FunctionType *fopen_type = llvm::FunctionType::get(
            void_ptr_type, {char_ptr_type, char_ptr_type}, false);

        llvm::Function *fopen_func = get_or_create_libc_function("fopen", fopen_type);
        return builder.CreateCall(fopen_func, {args[0], args[1]}, "fopen.result");
    }

    llvm::Value *Intrinsics::generate_fclose(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 1)
        {
            report_error("fclose requires exactly 1 argument (file)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        // Create fclose function type: int fclose(FILE* file)
        llvm::Type *void_ptr_type = llvm::PointerType::get(context, 0);
        llvm::Type *int_type = llvm::Type::getInt32Ty(context);
        llvm::FunctionType *fclose_type = llvm::FunctionType::get(int_type, {void_ptr_type}, false);

        llvm::Function *fclose_func = get_or_create_libc_function("fclose", fclose_type);
        return builder.CreateCall(fclose_func, {args[0]}, "fclose.result");
    }

    llvm::Value *Intrinsics::generate_fread(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 4)
        {
            report_error("fread requires exactly 4 arguments (buffer, size, count, file)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        // Create fread function type: size_t fread(void* buffer, size_t size, size_t count, FILE* file)
        llvm::Type *void_ptr_type = llvm::PointerType::get(context, 0);
        llvm::Type *size_t_type = llvm::Type::getInt64Ty(context);
        llvm::FunctionType *fread_type = llvm::FunctionType::get(
            size_t_type, {void_ptr_type, size_t_type, size_t_type, void_ptr_type}, false);

        llvm::Function *fread_func = get_or_create_libc_function("fread", fread_type);
        
        std::vector<llvm::Value *> call_args = {
            args[0], // buffer
            ensure_type(args[1], size_t_type, "fread.size"),
            ensure_type(args[2], size_t_type, "fread.count"),
            args[3] // file
        };
        
        return builder.CreateCall(fread_func, call_args, "fread.result");
    }

    llvm::Value *Intrinsics::generate_fwrite(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 4)
        {
            report_error("fwrite requires exactly 4 arguments (buffer, size, count, file)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        // Create fwrite function type: size_t fwrite(const void* buffer, size_t size, size_t count, FILE* file)
        llvm::Type *void_ptr_type = llvm::PointerType::get(context, 0);
        llvm::Type *size_t_type = llvm::Type::getInt64Ty(context);
        llvm::FunctionType *fwrite_type = llvm::FunctionType::get(
            size_t_type, {void_ptr_type, size_t_type, size_t_type, void_ptr_type}, false);

        llvm::Function *fwrite_func = get_or_create_libc_function("fwrite", fwrite_type);
        
        std::vector<llvm::Value *> call_args = {
            args[0], // buffer
            ensure_type(args[1], size_t_type, "fwrite.size"),
            ensure_type(args[2], size_t_type, "fwrite.count"),
            args[3] // file
        };
        
        return builder.CreateCall(fwrite_func, call_args, "fwrite.result");
    }

    llvm::Value *Intrinsics::generate_fseek(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 3)
        {
            report_error("fseek requires exactly 3 arguments (file, offset, whence)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        // Create fseek function type: int fseek(FILE* file, long offset, int whence)
        llvm::Type *void_ptr_type = llvm::PointerType::get(context, 0);
        llvm::Type *long_type = llvm::Type::getInt64Ty(context);
        llvm::Type *int_type = llvm::Type::getInt32Ty(context);
        llvm::FunctionType *fseek_type = llvm::FunctionType::get(
            int_type, {void_ptr_type, long_type, int_type}, false);

        llvm::Function *fseek_func = get_or_create_libc_function("fseek", fseek_type);
        
        std::vector<llvm::Value *> call_args = {
            args[0], // file
            ensure_type(args[1], long_type, "fseek.offset"),
            ensure_type(args[2], int_type, "fseek.whence")
        };
        
        return builder.CreateCall(fseek_func, call_args, "fseek.result");
    }

    llvm::Value *Intrinsics::generate_ftell(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 1)
        {
            report_error("ftell requires exactly 1 argument (file)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        // Create ftell function type: long ftell(FILE* file)
        llvm::Type *void_ptr_type = llvm::PointerType::get(context, 0);
        llvm::Type *long_type = llvm::Type::getInt64Ty(context);
        llvm::FunctionType *ftell_type = llvm::FunctionType::get(long_type, {void_ptr_type}, false);

        llvm::Function *ftell_func = get_or_create_libc_function("ftell", ftell_type);
        return builder.CreateCall(ftell_func, {args[0]}, "ftell.result");
    }

    llvm::Value *Intrinsics::generate_fflush(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 1)
        {
            report_error("fflush requires exactly 1 argument (file)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        // Create fflush function type: int fflush(FILE* file)
        llvm::Type *void_ptr_type = llvm::PointerType::get(context, 0);
        llvm::Type *int_type = llvm::Type::getInt32Ty(context);
        llvm::FunctionType *fflush_type = llvm::FunctionType::get(int_type, {void_ptr_type}, false);

        llvm::Function *fflush_func = get_or_create_libc_function("fflush", fflush_type);
        return builder.CreateCall(fflush_func, {args[0]}, "fflush.result");
    }

    llvm::Value *Intrinsics::generate_feof(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 1)
        {
            report_error("feof requires exactly 1 argument (file)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        // Create feof function type: int feof(FILE* file)
        llvm::Type *void_ptr_type = llvm::PointerType::get(context, 0);
        llvm::Type *int_type = llvm::Type::getInt32Ty(context);
        llvm::FunctionType *feof_type = llvm::FunctionType::get(int_type, {void_ptr_type}, false);

        llvm::Function *feof_func = get_or_create_libc_function("feof", feof_type);
        return builder.CreateCall(feof_func, {args[0]}, "feof.result");
    }

    llvm::Value *Intrinsics::generate_ferror(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 1)
        {
            report_error("ferror requires exactly 1 argument (file)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        // Create ferror function type: int ferror(FILE* file)
        llvm::Type *void_ptr_type = llvm::PointerType::get(context, 0);
        llvm::Type *int_type = llvm::Type::getInt32Ty(context);
        llvm::FunctionType *ferror_type = llvm::FunctionType::get(int_type, {void_ptr_type}, false);

        llvm::Function *ferror_func = get_or_create_libc_function("ferror", ferror_type);
        return builder.CreateCall(ferror_func, {args[0]}, "ferror.result");
    }

    // ========================================
    // Low-Level File Descriptor I/O
    // ========================================

    llvm::Value *Intrinsics::generate_read(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 3)
        {
            report_error("read requires exactly 3 arguments (fd, buffer, count)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        // Create read function type: ssize_t read(int fd, void* buffer, size_t count)
        llvm::Type *int_type = llvm::Type::getInt32Ty(context);
        llvm::Type *void_ptr_type = llvm::PointerType::get(context, 0);
        llvm::Type *size_t_type = llvm::Type::getInt64Ty(context);
        llvm::Type *ssize_t_type = llvm::Type::getInt64Ty(context);
        llvm::FunctionType *read_type = llvm::FunctionType::get(
            ssize_t_type, {int_type, void_ptr_type, size_t_type}, false);

        llvm::Function *read_func = get_or_create_libc_function("read", read_type);
        
        std::vector<llvm::Value *> call_args = {
            ensure_type(args[0], int_type, "read.fd"),
            args[1], // buffer
            ensure_type(args[2], size_t_type, "read.count")
        };
        
        return builder.CreateCall(read_func, call_args, "read.result");
    }

    llvm::Value *Intrinsics::generate_write(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 3)
        {
            report_error("write requires exactly 3 arguments (fd, buffer, count)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        // Create write function type: ssize_t write(int fd, const void* buffer, size_t count)
        llvm::Type *int_type = llvm::Type::getInt32Ty(context);
        llvm::Type *void_ptr_type = llvm::PointerType::get(context, 0);
        llvm::Type *size_t_type = llvm::Type::getInt64Ty(context);
        llvm::Type *ssize_t_type = llvm::Type::getInt64Ty(context);
        llvm::FunctionType *write_type = llvm::FunctionType::get(
            ssize_t_type, {int_type, void_ptr_type, size_t_type}, false);

        llvm::Function *write_func = get_or_create_libc_function("write", write_type);
        
        std::vector<llvm::Value *> call_args = {
            ensure_type(args[0], int_type, "write.fd"),
            args[1], // buffer
            ensure_type(args[2], size_t_type, "write.count")
        };
        
        return builder.CreateCall(write_func, call_args, "write.result");
    }

    llvm::Value *Intrinsics::generate_open(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 3)
        {
            report_error("open requires exactly 3 arguments (path, flags, mode)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        // Create open function type: int open(const char* path, int flags, mode_t mode)
        llvm::Type *char_ptr_type = llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
        llvm::Type *int_type = llvm::Type::getInt32Ty(context);
        llvm::FunctionType *open_type = llvm::FunctionType::get(
            int_type, {char_ptr_type, int_type, int_type}, false);

        llvm::Function *open_func = get_or_create_libc_function("open", open_type);
        
        std::vector<llvm::Value *> call_args = {
            args[0], // path
            ensure_type(args[1], int_type, "open.flags"),
            ensure_type(args[2], int_type, "open.mode")
        };
        
        return builder.CreateCall(open_func, call_args, "open.result");
    }

    llvm::Value *Intrinsics::generate_close(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 1)
        {
            report_error("close requires exactly 1 argument (fd)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        // Create close function type: int close(int fd)
        llvm::Type *int_type = llvm::Type::getInt32Ty(context);
        llvm::FunctionType *close_type = llvm::FunctionType::get(int_type, {int_type}, false);

        llvm::Function *close_func = get_or_create_libc_function("close", close_type);
        return builder.CreateCall(close_func, {ensure_type(args[0], int_type, "close.fd")}, "close.result");
    }

    llvm::Value *Intrinsics::generate_lseek(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 3)
        {
            report_error("lseek requires exactly 3 arguments (fd, offset, whence)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        // Create lseek function type: off_t lseek(int fd, off_t offset, int whence)
        llvm::Type *int_type = llvm::Type::getInt32Ty(context);
        llvm::Type *off_t_type = llvm::Type::getInt64Ty(context);
        llvm::FunctionType *lseek_type = llvm::FunctionType::get(
            off_t_type, {int_type, off_t_type, int_type}, false);

        llvm::Function *lseek_func = get_or_create_libc_function("lseek", lseek_type);
        
        std::vector<llvm::Value *> call_args = {
            ensure_type(args[0], int_type, "lseek.fd"),
            ensure_type(args[1], off_t_type, "lseek.offset"),
            ensure_type(args[2], int_type, "lseek.whence")
        };
        
        return builder.CreateCall(lseek_func, call_args, "lseek.result");
    }

    llvm::Value *Intrinsics::generate_dup(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 1)
        {
            report_error("dup requires exactly 1 argument (fd)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        // Create dup function type: int dup(int fd)
        llvm::Type *int_type = llvm::Type::getInt32Ty(context);
        llvm::FunctionType *dup_type = llvm::FunctionType::get(int_type, {int_type}, false);

        llvm::Function *dup_func = get_or_create_libc_function("dup", dup_type);
        return builder.CreateCall(dup_func, {ensure_type(args[0], int_type, "dup.fd")}, "dup.result");
    }

    llvm::Value *Intrinsics::generate_dup2(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 2)
        {
            report_error("dup2 requires exactly 2 arguments (oldfd, newfd)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        // Create dup2 function type: int dup2(int oldfd, int newfd)
        llvm::Type *int_type = llvm::Type::getInt32Ty(context);
        llvm::FunctionType *dup2_type = llvm::FunctionType::get(int_type, {int_type, int_type}, false);

        llvm::Function *dup2_func = get_or_create_libc_function("dup2", dup2_type);
        
        std::vector<llvm::Value *> call_args = {
            ensure_type(args[0], int_type, "dup2.oldfd"),
            ensure_type(args[1], int_type, "dup2.newfd")
        };
        
        return builder.CreateCall(dup2_func, call_args, "dup2.result");
    }

    llvm::Value *Intrinsics::generate_pipe(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 1)
        {
            report_error("pipe requires exactly 1 argument (pipefd)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        // Create pipe function type: int pipe(int pipefd[2])
        llvm::Type *int_ptr_type = llvm::PointerType::get(llvm::Type::getInt32Ty(context), 0);
        llvm::Type *int_type = llvm::Type::getInt32Ty(context);
        llvm::FunctionType *pipe_type = llvm::FunctionType::get(int_type, {int_ptr_type}, false);

        llvm::Function *pipe_func = get_or_create_libc_function("pipe", pipe_type);
        return builder.CreateCall(pipe_func, {args[0]}, "pipe.result");
    }

    llvm::Value *Intrinsics::generate_fcntl(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 3)
        {
            report_error("fcntl requires exactly 3 arguments (fd, cmd, arg)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        // Create fcntl function type: int fcntl(int fd, int cmd, int arg)
        llvm::Type *int_type = llvm::Type::getInt32Ty(context);
        llvm::FunctionType *fcntl_type = llvm::FunctionType::get(
            int_type, {int_type, int_type, int_type}, false);

        llvm::Function *fcntl_func = get_or_create_libc_function("fcntl", fcntl_type);
        
        std::vector<llvm::Value *> call_args = {
            ensure_type(args[0], int_type, "fcntl.fd"),
            ensure_type(args[1], int_type, "fcntl.cmd"),
            ensure_type(args[2], int_type, "fcntl.arg")
        };
        
        return builder.CreateCall(fcntl_func, call_args, "fcntl.result");
    }

    // ========================================
    // Filesystem Intrinsics
    // ========================================

    llvm::Value *Intrinsics::generate_stat(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 2)
        {
            report_error("stat requires exactly 2 arguments (path, buf)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        // Create stat function type: int stat(const char* path, struct stat* buf)
        llvm::Type *char_ptr_type = llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
        llvm::Type *void_ptr_type = llvm::PointerType::get(context, 0);
        llvm::Type *int_type = llvm::Type::getInt32Ty(context);
        llvm::FunctionType *stat_type = llvm::FunctionType::get(int_type, {char_ptr_type, void_ptr_type}, false);

        llvm::Function *stat_func = get_or_create_libc_function("stat", stat_type);
        return builder.CreateCall(stat_func, {args[0], args[1]}, "stat.result");
    }

    llvm::Value *Intrinsics::generate_fstat(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 2)
        {
            report_error("fstat requires exactly 2 arguments (fd, buf)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        llvm::Type *int_type = llvm::Type::getInt32Ty(context);
        llvm::Type *void_ptr_type = llvm::PointerType::get(context, 0);
        llvm::FunctionType *fstat_type = llvm::FunctionType::get(int_type, {int_type, void_ptr_type}, false);

        llvm::Function *fstat_func = get_or_create_libc_function("fstat", fstat_type);
        return builder.CreateCall(fstat_func, {ensure_type(args[0], int_type, "fstat.fd"), args[1]}, "fstat.result");
    }

    llvm::Value *Intrinsics::generate_lstat(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 2)
        {
            report_error("lstat requires exactly 2 arguments (path, buf)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        llvm::Type *char_ptr_type = llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
        llvm::Type *void_ptr_type = llvm::PointerType::get(context, 0);
        llvm::Type *int_type = llvm::Type::getInt32Ty(context);
        llvm::FunctionType *lstat_type = llvm::FunctionType::get(int_type, {char_ptr_type, void_ptr_type}, false);

        llvm::Function *lstat_func = get_or_create_libc_function("lstat", lstat_type);
        return builder.CreateCall(lstat_func, {args[0], args[1]}, "lstat.result");
    }

    llvm::Value *Intrinsics::generate_access(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 2)
        {
            report_error("access requires exactly 2 arguments (path, mode)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        llvm::Type *char_ptr_type = llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
        llvm::Type *int_type = llvm::Type::getInt32Ty(context);
        llvm::FunctionType *access_type = llvm::FunctionType::get(int_type, {char_ptr_type, int_type}, false);

        llvm::Function *access_func = get_or_create_libc_function("access", access_type);
        return builder.CreateCall(access_func, {args[0], ensure_type(args[1], int_type, "access.mode")}, "access.result");
    }

    llvm::Value *Intrinsics::generate_mkdir(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 2)
        {
            report_error("mkdir requires exactly 2 arguments (path, mode)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        llvm::Type *char_ptr_type = llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
        llvm::Type *int_type = llvm::Type::getInt32Ty(context);
        llvm::FunctionType *mkdir_type = llvm::FunctionType::get(int_type, {char_ptr_type, int_type}, false);

        llvm::Function *mkdir_func = get_or_create_libc_function("mkdir", mkdir_type);
        return builder.CreateCall(mkdir_func, {args[0], ensure_type(args[1], int_type, "mkdir.mode")}, "mkdir.result");
    }

    llvm::Value *Intrinsics::generate_rmdir(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 1)
        {
            report_error("rmdir requires exactly 1 argument (path)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        llvm::Type *char_ptr_type = llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
        llvm::Type *int_type = llvm::Type::getInt32Ty(context);
        llvm::FunctionType *rmdir_type = llvm::FunctionType::get(int_type, {char_ptr_type}, false);

        llvm::Function *rmdir_func = get_or_create_libc_function("rmdir", rmdir_type);
        return builder.CreateCall(rmdir_func, {args[0]}, "rmdir.result");
    }

    llvm::Value *Intrinsics::generate_unlink(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 1)
        {
            report_error("unlink requires exactly 1 argument (path)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        llvm::Type *char_ptr_type = llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
        llvm::Type *int_type = llvm::Type::getInt32Ty(context);
        llvm::FunctionType *unlink_type = llvm::FunctionType::get(int_type, {char_ptr_type}, false);

        llvm::Function *unlink_func = get_or_create_libc_function("unlink", unlink_type);
        return builder.CreateCall(unlink_func, {args[0]}, "unlink.result");
    }

    llvm::Value *Intrinsics::generate_rename(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 2)
        {
            report_error("rename requires exactly 2 arguments (oldpath, newpath)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        llvm::Type *char_ptr_type = llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
        llvm::Type *int_type = llvm::Type::getInt32Ty(context);
        llvm::FunctionType *rename_type = llvm::FunctionType::get(int_type, {char_ptr_type, char_ptr_type}, false);

        llvm::Function *rename_func = get_or_create_libc_function("rename", rename_type);
        return builder.CreateCall(rename_func, {args[0], args[1]}, "rename.result");
    }

    llvm::Value *Intrinsics::generate_symlink(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 2)
        {
            report_error("symlink requires exactly 2 arguments (target, linkpath)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        llvm::Type *char_ptr_type = llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
        llvm::Type *int_type = llvm::Type::getInt32Ty(context);
        llvm::FunctionType *symlink_type = llvm::FunctionType::get(int_type, {char_ptr_type, char_ptr_type}, false);

        llvm::Function *symlink_func = get_or_create_libc_function("symlink", symlink_type);
        return builder.CreateCall(symlink_func, {args[0], args[1]}, "symlink.result");
    }

    llvm::Value *Intrinsics::generate_readlink(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 3)
        {
            report_error("readlink requires exactly 3 arguments (path, buf, size)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        llvm::Type *char_ptr_type = llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
        llvm::Type *size_t_type = llvm::Type::getInt64Ty(context);
        llvm::Type *ssize_t_type = llvm::Type::getInt64Ty(context);
        llvm::FunctionType *readlink_type = llvm::FunctionType::get(ssize_t_type, {char_ptr_type, char_ptr_type, size_t_type}, false);

        llvm::Function *readlink_func = get_or_create_libc_function("readlink", readlink_type);
        return builder.CreateCall(readlink_func, {args[0], args[1], ensure_type(args[2], size_t_type, "readlink.size")}, "readlink.result");
    }

    llvm::Value *Intrinsics::generate_truncate(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 2)
        {
            report_error("truncate requires exactly 2 arguments (path, length)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        llvm::Type *char_ptr_type = llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
        llvm::Type *off_t_type = llvm::Type::getInt64Ty(context);
        llvm::Type *int_type = llvm::Type::getInt32Ty(context);
        llvm::FunctionType *truncate_type = llvm::FunctionType::get(int_type, {char_ptr_type, off_t_type}, false);

        llvm::Function *truncate_func = get_or_create_libc_function("truncate", truncate_type);
        return builder.CreateCall(truncate_func, {args[0], ensure_type(args[1], off_t_type, "truncate.length")}, "truncate.result");
    }

    llvm::Value *Intrinsics::generate_ftruncate(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 2)
        {
            report_error("ftruncate requires exactly 2 arguments (fd, length)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        llvm::Type *int_type = llvm::Type::getInt32Ty(context);
        llvm::Type *off_t_type = llvm::Type::getInt64Ty(context);
        llvm::FunctionType *ftruncate_type = llvm::FunctionType::get(int_type, {int_type, off_t_type}, false);

        llvm::Function *ftruncate_func = get_or_create_libc_function("ftruncate", ftruncate_type);
        return builder.CreateCall(ftruncate_func, {ensure_type(args[0], int_type, "ftruncate.fd"), ensure_type(args[1], off_t_type, "ftruncate.length")}, "ftruncate.result");
    }

    llvm::Value *Intrinsics::generate_chmod(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 2)
        {
            report_error("chmod requires exactly 2 arguments (path, mode)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        llvm::Type *char_ptr_type = llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
        llvm::Type *int_type = llvm::Type::getInt32Ty(context);
        llvm::FunctionType *chmod_type = llvm::FunctionType::get(int_type, {char_ptr_type, int_type}, false);

        llvm::Function *chmod_func = get_or_create_libc_function("chmod", chmod_type);
        return builder.CreateCall(chmod_func, {args[0], ensure_type(args[1], int_type, "chmod.mode")}, "chmod.result");
    }

    llvm::Value *Intrinsics::generate_chown(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 3)
        {
            report_error("chown requires exactly 3 arguments (path, uid, gid)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        llvm::Type *char_ptr_type = llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
        llvm::Type *int_type = llvm::Type::getInt32Ty(context);
        llvm::FunctionType *chown_type = llvm::FunctionType::get(int_type, {char_ptr_type, int_type, int_type}, false);

        llvm::Function *chown_func = get_or_create_libc_function("chown", chown_type);
        return builder.CreateCall(chown_func, {args[0], ensure_type(args[1], int_type, "chown.uid"), ensure_type(args[2], int_type, "chown.gid")}, "chown.result");
    }

    llvm::Value *Intrinsics::generate_getcwd(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 2)
        {
            report_error("getcwd requires exactly 2 arguments (buf, size)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        llvm::Type *char_ptr_type = llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
        llvm::Type *size_t_type = llvm::Type::getInt64Ty(context);
        llvm::FunctionType *getcwd_type = llvm::FunctionType::get(char_ptr_type, {char_ptr_type, size_t_type}, false);

        llvm::Function *getcwd_func = get_or_create_libc_function("getcwd", getcwd_type);
        return builder.CreateCall(getcwd_func, {args[0], ensure_type(args[1], size_t_type, "getcwd.size")}, "getcwd.result");
    }

    llvm::Value *Intrinsics::generate_chdir(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 1)
        {
            report_error("chdir requires exactly 1 argument (path)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        llvm::Type *char_ptr_type = llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
        llvm::Type *int_type = llvm::Type::getInt32Ty(context);
        llvm::FunctionType *chdir_type = llvm::FunctionType::get(int_type, {char_ptr_type}, false);

        llvm::Function *chdir_func = get_or_create_libc_function("chdir", chdir_type);
        return builder.CreateCall(chdir_func, {args[0]}, "chdir.result");
    }

    llvm::Value *Intrinsics::generate_opendir(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 1)
        {
            report_error("opendir requires exactly 1 argument (path)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        llvm::Type *char_ptr_type = llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
        llvm::Type *void_ptr_type = llvm::PointerType::get(context, 0); // DIR*
        llvm::FunctionType *opendir_type = llvm::FunctionType::get(void_ptr_type, {char_ptr_type}, false);

        llvm::Function *opendir_func = get_or_create_libc_function("opendir", opendir_type);
        return builder.CreateCall(opendir_func, {args[0]}, "opendir.result");
    }

    llvm::Value *Intrinsics::generate_readdir(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 1)
        {
            report_error("readdir requires exactly 1 argument (dirp)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        llvm::Type *void_ptr_type = llvm::PointerType::get(context, 0);
        llvm::FunctionType *readdir_type = llvm::FunctionType::get(void_ptr_type, {void_ptr_type}, false);

        llvm::Function *readdir_func = get_or_create_libc_function("readdir", readdir_type);
        return builder.CreateCall(readdir_func, {args[0]}, "readdir.result");
    }

    llvm::Value *Intrinsics::generate_closedir(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 1)
        {
            report_error("closedir requires exactly 1 argument (dirp)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        llvm::Type *void_ptr_type = llvm::PointerType::get(context, 0);
        llvm::Type *int_type = llvm::Type::getInt32Ty(context);
        llvm::FunctionType *closedir_type = llvm::FunctionType::get(int_type, {void_ptr_type}, false);

        llvm::Function *closedir_func = get_or_create_libc_function("closedir", closedir_type);
        return builder.CreateCall(closedir_func, {args[0]}, "closedir.result");
    }

    // ========================================
    // Process Intrinsics
    // ========================================

    llvm::Value *Intrinsics::generate_abort(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 0)
        {
            report_error("abort requires exactly 0 arguments");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        llvm::Type *void_type = llvm::Type::getVoidTy(context);
        llvm::FunctionType *abort_type = llvm::FunctionType::get(void_type, {}, false);

        llvm::Function *abort_func = get_or_create_libc_function("abort", abort_type);
        llvm::Value *call_result = builder.CreateCall(abort_func, {}, "abort");

        // abort() never returns, add unreachable
        builder.CreateUnreachable();
        return call_result;
    }

    llvm::Value *Intrinsics::generate_execvp(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 2)
        {
            report_error("execvp requires exactly 2 arguments (file, argv)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        llvm::Type *char_ptr_type = llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
        llvm::Type *void_ptr_type = llvm::PointerType::get(context, 0);
        llvm::Type *int_type = llvm::Type::getInt32Ty(context);
        llvm::FunctionType *execvp_type = llvm::FunctionType::get(int_type, {char_ptr_type, void_ptr_type}, false);

        llvm::Function *execvp_func = get_or_create_libc_function("execvp", execvp_type);
        return builder.CreateCall(execvp_func, {args[0], args[1]}, "execvp.result");
    }

    llvm::Value *Intrinsics::generate_wait(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 1)
        {
            report_error("wait requires exactly 1 argument (status)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        llvm::Type *int_ptr_type = llvm::PointerType::get(llvm::Type::getInt32Ty(context), 0);
        llvm::Type *pid_t_type = llvm::Type::getInt32Ty(context); // Typically int on Linux
        llvm::FunctionType *wait_type = llvm::FunctionType::get(pid_t_type, {int_ptr_type}, false);

        llvm::Function *wait_func = get_or_create_libc_function("wait", wait_type);
        return builder.CreateCall(wait_func, {args[0]}, "wait.result");
    }

    llvm::Value *Intrinsics::generate_waitpid(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 3)
        {
            report_error("waitpid requires exactly 3 arguments (pid, status, options)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        llvm::Type *int_type = llvm::Type::getInt32Ty(context);
        llvm::Type *int_ptr_type = llvm::PointerType::get(int_type, 0);
        llvm::FunctionType *waitpid_type = llvm::FunctionType::get(int_type, {int_type, int_ptr_type, int_type}, false);

        llvm::Function *waitpid_func = get_or_create_libc_function("waitpid", waitpid_type);
        return builder.CreateCall(waitpid_func, {
            ensure_type(args[0], int_type, "waitpid.pid"),
            args[1],
            ensure_type(args[2], int_type, "waitpid.options")
        }, "waitpid.result");
    }

    llvm::Value *Intrinsics::generate_getppid(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 0)
        {
            report_error("getppid requires exactly 0 arguments");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        llvm::Type *pid_t_type = llvm::Type::getInt32Ty(context);
        llvm::FunctionType *getppid_type = llvm::FunctionType::get(pid_t_type, {}, false);

        llvm::Function *getppid_func = get_or_create_libc_function("getppid", getppid_type);
        return builder.CreateCall(getppid_func, {}, "getppid.result");
    }

    llvm::Value *Intrinsics::generate_getuid(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 0)
        {
            report_error("getuid requires exactly 0 arguments");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        llvm::Type *uid_t_type = llvm::Type::getInt32Ty(context);
        llvm::FunctionType *getuid_type = llvm::FunctionType::get(uid_t_type, {}, false);

        llvm::Function *getuid_func = get_or_create_libc_function("getuid", getuid_type);
        return builder.CreateCall(getuid_func, {}, "getuid.result");
    }

    llvm::Value *Intrinsics::generate_getgid(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 0)
        {
            report_error("getgid requires exactly 0 arguments");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        llvm::Type *gid_t_type = llvm::Type::getInt32Ty(context);
        llvm::FunctionType *getgid_type = llvm::FunctionType::get(gid_t_type, {}, false);

        llvm::Function *getgid_func = get_or_create_libc_function("getgid", getgid_type);
        return builder.CreateCall(getgid_func, {}, "getgid.result");
    }

    llvm::Value *Intrinsics::generate_geteuid(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 0)
        {
            report_error("geteuid requires exactly 0 arguments");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        llvm::Type *uid_t_type = llvm::Type::getInt32Ty(context);
        llvm::FunctionType *geteuid_type = llvm::FunctionType::get(uid_t_type, {}, false);

        llvm::Function *geteuid_func = get_or_create_libc_function("geteuid", geteuid_type);
        return builder.CreateCall(geteuid_func, {}, "geteuid.result");
    }

    llvm::Value *Intrinsics::generate_getegid(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 0)
        {
            report_error("getegid requires exactly 0 arguments");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        llvm::Type *gid_t_type = llvm::Type::getInt32Ty(context);
        llvm::FunctionType *getegid_type = llvm::FunctionType::get(gid_t_type, {}, false);

        llvm::Function *getegid_func = get_or_create_libc_function("getegid", getegid_type);
        return builder.CreateCall(getegid_func, {}, "getegid.result");
    }

    llvm::Value *Intrinsics::generate_kill(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 2)
        {
            report_error("kill requires exactly 2 arguments (pid, sig)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        llvm::Type *int_type = llvm::Type::getInt32Ty(context);
        llvm::FunctionType *kill_type = llvm::FunctionType::get(int_type, {int_type, int_type}, false);

        llvm::Function *kill_func = get_or_create_libc_function("kill", kill_type);
        return builder.CreateCall(kill_func, {
            ensure_type(args[0], int_type, "kill.pid"),
            ensure_type(args[1], int_type, "kill.sig")
        }, "kill.result");
    }

    llvm::Value *Intrinsics::generate_raise(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 1)
        {
            report_error("raise requires exactly 1 argument (sig)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        llvm::Type *int_type = llvm::Type::getInt32Ty(context);
        llvm::FunctionType *raise_type = llvm::FunctionType::get(int_type, {int_type}, false);

        llvm::Function *raise_func = get_or_create_libc_function("raise", raise_type);
        return builder.CreateCall(raise_func, {ensure_type(args[0], int_type, "raise.sig")}, "raise.result");
    }

    llvm::Value *Intrinsics::generate_signal(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 2)
        {
            report_error("signal requires exactly 2 arguments (signum, handler)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        llvm::Type *int_type = llvm::Type::getInt32Ty(context);
        llvm::Type *void_ptr_type = llvm::PointerType::get(context, 0);
        llvm::FunctionType *signal_type = llvm::FunctionType::get(void_ptr_type, {int_type, void_ptr_type}, false);

        llvm::Function *signal_func = get_or_create_libc_function("signal", signal_type);
        return builder.CreateCall(signal_func, {
            ensure_type(args[0], int_type, "signal.signum"),
            args[1]
        }, "signal.result");
    }

    // ========================================
    // Math Intrinsics
    // ========================================

    llvm::Value *Intrinsics::generate_sqrtf(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 1)
        {
            report_error("sqrtf requires exactly 1 argument");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        // Use LLVM's builtin sqrt intrinsic
        llvm::Function *sqrt_intrinsic = llvm::Intrinsic::getDeclaration(
            _context_manager.get_module(),
            llvm::Intrinsic::sqrt,
            {llvm::Type::getFloatTy(context)});

        llvm::Value *x = ensure_type(args[0], llvm::Type::getFloatTy(context), "sqrtf.x");
        return builder.CreateCall(sqrt_intrinsic, {x}, "sqrtf.result");
    }

    llvm::Value *Intrinsics::generate_powf(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 2)
        {
            report_error("powf requires exactly 2 arguments");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        // Use LLVM's builtin pow intrinsic
        llvm::Function *pow_intrinsic = llvm::Intrinsic::getDeclaration(
            _context_manager.get_module(),
            llvm::Intrinsic::pow,
            {llvm::Type::getFloatTy(context)});

        llvm::Value *base = ensure_type(args[0], llvm::Type::getFloatTy(context), "powf.base");
        llvm::Value *exp = ensure_type(args[1], llvm::Type::getFloatTy(context), "powf.exp");
        return builder.CreateCall(pow_intrinsic, {base, exp}, "powf.result");
    }

    llvm::Value *Intrinsics::generate_exp(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 1)
        {
            report_error("exp requires exactly 1 argument");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        // Use LLVM's builtin exp intrinsic
        llvm::Function *exp_intrinsic = llvm::Intrinsic::getDeclaration(
            _context_manager.get_module(),
            llvm::Intrinsic::exp,
            {llvm::Type::getDoubleTy(context)});

        llvm::Value *x = ensure_type(args[0], llvm::Type::getDoubleTy(context), "exp.x");
        return builder.CreateCall(exp_intrinsic, {x}, "exp.result");
    }

    llvm::Value *Intrinsics::generate_expf(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 1)
        {
            report_error("expf requires exactly 1 argument");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        // Use LLVM's builtin exp intrinsic
        llvm::Function *exp_intrinsic = llvm::Intrinsic::getDeclaration(
            _context_manager.get_module(),
            llvm::Intrinsic::exp,
            {llvm::Type::getFloatTy(context)});

        llvm::Value *x = ensure_type(args[0], llvm::Type::getFloatTy(context), "expf.x");
        return builder.CreateCall(exp_intrinsic, {x}, "expf.result");
    }

    llvm::Value *Intrinsics::generate_exp2(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 1)
        {
            report_error("exp2 requires exactly 1 argument");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        // Use LLVM's builtin exp2 intrinsic
        llvm::Function *exp2_intrinsic = llvm::Intrinsic::getDeclaration(
            _context_manager.get_module(),
            llvm::Intrinsic::exp2,
            {llvm::Type::getDoubleTy(context)});

        llvm::Value *x = ensure_type(args[0], llvm::Type::getDoubleTy(context), "exp2.x");
        return builder.CreateCall(exp2_intrinsic, {x}, "exp2.result");
    }

    llvm::Value *Intrinsics::generate_expm1(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 1)
        {
            report_error("expm1 requires exactly 1 argument");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        // expm1(x) = exp(x) - 1, use libc function
        llvm::Type *double_type = llvm::Type::getDoubleTy(context);
        llvm::FunctionType *expm1_type = llvm::FunctionType::get(double_type, {double_type}, false);

        llvm::Function *expm1_func = get_or_create_libc_function("expm1", expm1_type);
        llvm::Value *x = ensure_type(args[0], double_type, "expm1.x");
        return builder.CreateCall(expm1_func, {x}, "expm1.result");
    }

    llvm::Value *Intrinsics::generate_log(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 1)
        {
            report_error("log requires exactly 1 argument");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        // Use LLVM's builtin log intrinsic
        llvm::Function *log_intrinsic = llvm::Intrinsic::getDeclaration(
            _context_manager.get_module(),
            llvm::Intrinsic::log,
            {llvm::Type::getDoubleTy(context)});

        llvm::Value *x = ensure_type(args[0], llvm::Type::getDoubleTy(context), "log.x");
        return builder.CreateCall(log_intrinsic, {x}, "log.result");
    }

    llvm::Value *Intrinsics::generate_logf(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 1)
        {
            report_error("logf requires exactly 1 argument");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        // Use LLVM's builtin log intrinsic
        llvm::Function *log_intrinsic = llvm::Intrinsic::getDeclaration(
            _context_manager.get_module(),
            llvm::Intrinsic::log,
            {llvm::Type::getFloatTy(context)});

        llvm::Value *x = ensure_type(args[0], llvm::Type::getFloatTy(context), "logf.x");
        return builder.CreateCall(log_intrinsic, {x}, "logf.result");
    }

    llvm::Value *Intrinsics::generate_log10(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 1)
        {
            report_error("log10 requires exactly 1 argument");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        // Use LLVM's builtin log10 intrinsic
        llvm::Function *log10_intrinsic = llvm::Intrinsic::getDeclaration(
            _context_manager.get_module(),
            llvm::Intrinsic::log10,
            {llvm::Type::getDoubleTy(context)});

        llvm::Value *x = ensure_type(args[0], llvm::Type::getDoubleTy(context), "log10.x");
        return builder.CreateCall(log10_intrinsic, {x}, "log10.result");
    }

    llvm::Value *Intrinsics::generate_log2(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 1)
        {
            report_error("log2 requires exactly 1 argument");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        // Use LLVM's builtin log2 intrinsic
        llvm::Function *log2_intrinsic = llvm::Intrinsic::getDeclaration(
            _context_manager.get_module(),
            llvm::Intrinsic::log2,
            {llvm::Type::getDoubleTy(context)});

        llvm::Value *x = ensure_type(args[0], llvm::Type::getDoubleTy(context), "log2.x");
        return builder.CreateCall(log2_intrinsic, {x}, "log2.result");
    }

    llvm::Value *Intrinsics::generate_log1p(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 1)
        {
            report_error("log1p requires exactly 1 argument");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        // log1p(x) = log(1 + x), use libc function
        llvm::Type *double_type = llvm::Type::getDoubleTy(context);
        llvm::FunctionType *log1p_type = llvm::FunctionType::get(double_type, {double_type}, false);

        llvm::Function *log1p_func = get_or_create_libc_function("log1p", log1p_type);
        llvm::Value *x = ensure_type(args[0], double_type, "log1p.x");
        return builder.CreateCall(log1p_func, {x}, "log1p.result");
    }

    llvm::Value *Intrinsics::generate_sinf(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 1)
        {
            report_error("sinf requires exactly 1 argument");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        // Use LLVM's builtin sin intrinsic
        llvm::Function *sin_intrinsic = llvm::Intrinsic::getDeclaration(
            _context_manager.get_module(),
            llvm::Intrinsic::sin,
            {llvm::Type::getFloatTy(context)});

        llvm::Value *x = ensure_type(args[0], llvm::Type::getFloatTy(context), "sinf.x");
        return builder.CreateCall(sin_intrinsic, {x}, "sinf.result");
    }

    llvm::Value *Intrinsics::generate_cosf(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 1)
        {
            report_error("cosf requires exactly 1 argument");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        // Use LLVM's builtin cos intrinsic
        llvm::Function *cos_intrinsic = llvm::Intrinsic::getDeclaration(
            _context_manager.get_module(),
            llvm::Intrinsic::cos,
            {llvm::Type::getFloatTy(context)});

        llvm::Value *x = ensure_type(args[0], llvm::Type::getFloatTy(context), "cosf.x");
        return builder.CreateCall(cos_intrinsic, {x}, "cosf.result");
    }

    llvm::Value *Intrinsics::generate_tan(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 1)
        {
            report_error("tan requires exactly 1 argument");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        llvm::Type *double_type = llvm::Type::getDoubleTy(context);
        llvm::FunctionType *tan_type = llvm::FunctionType::get(double_type, {double_type}, false);

        llvm::Function *tan_func = get_or_create_libc_function("tan", tan_type);
        llvm::Value *x = ensure_type(args[0], double_type, "tan.x");
        return builder.CreateCall(tan_func, {x}, "tan.result");
    }

    llvm::Value *Intrinsics::generate_tanf(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 1)
        {
            report_error("tanf requires exactly 1 argument");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        llvm::Type *float_type = llvm::Type::getFloatTy(context);
        llvm::FunctionType *tanf_type = llvm::FunctionType::get(float_type, {float_type}, false);

        llvm::Function *tanf_func = get_or_create_libc_function("tanf", tanf_type);
        llvm::Value *x = ensure_type(args[0], float_type, "tanf.x");
        return builder.CreateCall(tanf_func, {x}, "tanf.result");
    }

    // ========================================
    // Additional Math Functions (using libc)
    // ========================================

    #define DEFINE_MATH_FUNC_DOUBLE(name) \
    llvm::Value *Intrinsics::generate_##name(const std::vector<llvm::Value *> &args) \
    { \
        if (args.size() != 1) \
        { \
            report_error(#name " requires exactly 1 argument"); \
            return nullptr; \
        } \
        \
        auto &builder = _context_manager.get_builder(); \
        auto &context = _context_manager.get_context(); \
        \
        llvm::Type *double_type = llvm::Type::getDoubleTy(context); \
        llvm::FunctionType *func_type = llvm::FunctionType::get(double_type, {double_type}, false); \
        \
        llvm::Function *func = get_or_create_libc_function(#name, func_type); \
        llvm::Value *x = ensure_type(args[0], double_type, #name ".x"); \
        return builder.CreateCall(func, {x}, #name ".result"); \
    }

    #define DEFINE_MATH_FUNC_DOUBLE_2ARG(name) \
    llvm::Value *Intrinsics::generate_##name(const std::vector<llvm::Value *> &args) \
    { \
        if (args.size() != 2) \
        { \
            report_error(#name " requires exactly 2 arguments"); \
            return nullptr; \
        } \
        \
        auto &builder = _context_manager.get_builder(); \
        auto &context = _context_manager.get_context(); \
        \
        llvm::Type *double_type = llvm::Type::getDoubleTy(context); \
        llvm::FunctionType *func_type = llvm::FunctionType::get(double_type, {double_type, double_type}, false); \
        \
        llvm::Function *func = get_or_create_libc_function(#name, func_type); \
        llvm::Value *x = ensure_type(args[0], double_type, #name ".x"); \
        llvm::Value *y = ensure_type(args[1], double_type, #name ".y"); \
        return builder.CreateCall(func, {x, y}, #name ".result"); \
    }

    DEFINE_MATH_FUNC_DOUBLE(asin)
    DEFINE_MATH_FUNC_DOUBLE(acos)
    DEFINE_MATH_FUNC_DOUBLE(atan)
    DEFINE_MATH_FUNC_DOUBLE_2ARG(atan2)
    DEFINE_MATH_FUNC_DOUBLE(sinh)
    DEFINE_MATH_FUNC_DOUBLE(cosh)
    DEFINE_MATH_FUNC_DOUBLE(tanh)
    DEFINE_MATH_FUNC_DOUBLE(asinh)
    DEFINE_MATH_FUNC_DOUBLE(acosh)
    DEFINE_MATH_FUNC_DOUBLE(atanh)
    DEFINE_MATH_FUNC_DOUBLE(cbrt)
    DEFINE_MATH_FUNC_DOUBLE_2ARG(hypot)
    DEFINE_MATH_FUNC_DOUBLE(fabs)
    DEFINE_MATH_FUNC_DOUBLE(floor)
    DEFINE_MATH_FUNC_DOUBLE(ceil)
    DEFINE_MATH_FUNC_DOUBLE(round)
    DEFINE_MATH_FUNC_DOUBLE(trunc)
    DEFINE_MATH_FUNC_DOUBLE_2ARG(fmod)
    DEFINE_MATH_FUNC_DOUBLE_2ARG(remainder)
    DEFINE_MATH_FUNC_DOUBLE_2ARG(fmin)
    DEFINE_MATH_FUNC_DOUBLE_2ARG(fmax)
    DEFINE_MATH_FUNC_DOUBLE_2ARG(copysign)
    DEFINE_MATH_FUNC_DOUBLE_2ARG(nextafter)
    DEFINE_MATH_FUNC_DOUBLE(erf)
    DEFINE_MATH_FUNC_DOUBLE(erfc)

    #define DEFINE_MATH_FUNC_FLOAT(name, fname) \
    llvm::Value *Intrinsics::generate_##name(const std::vector<llvm::Value *> &args) \
    { \
        if (args.size() != 1) \
        { \
            report_error(#name " requires exactly 1 argument"); \
            return nullptr; \
        } \
        \
        auto &builder = _context_manager.get_builder(); \
        auto &context = _context_manager.get_context(); \
        \
        llvm::Type *float_type = llvm::Type::getFloatTy(context); \
        llvm::FunctionType *func_type = llvm::FunctionType::get(float_type, {float_type}, false); \
        \
        llvm::Function *func = get_or_create_libc_function(fname, func_type); \
        llvm::Value *x = ensure_type(args[0], float_type, #name ".x"); \
        return builder.CreateCall(func, {x}, #name ".result"); \
    }

    DEFINE_MATH_FUNC_FLOAT(fabsf, "fabsf")
    DEFINE_MATH_FUNC_FLOAT(floorf, "floorf")
    DEFINE_MATH_FUNC_FLOAT(ceilf, "ceilf")
    DEFINE_MATH_FUNC_FLOAT(roundf, "roundf")

    // Special functions that need custom implementations
    llvm::Value *Intrinsics::generate_fma(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 3)
        {
            report_error("fma requires exactly 3 arguments");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        // Use LLVM's builtin fma intrinsic
        llvm::Function *fma_intrinsic = llvm::Intrinsic::getDeclaration(
            _context_manager.get_module(),
            llvm::Intrinsic::fma,
            {llvm::Type::getDoubleTy(context)});

        llvm::Type *double_type = llvm::Type::getDoubleTy(context);
        llvm::Value *x = ensure_type(args[0], double_type, "fma.x");
        llvm::Value *y = ensure_type(args[1], double_type, "fma.y");
        llvm::Value *z = ensure_type(args[2], double_type, "fma.z");
        
        return builder.CreateCall(fma_intrinsic, {x, y, z}, "fma.result");
    }

    llvm::Value *Intrinsics::generate_frexp(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 2)
        {
            report_error("frexp requires exactly 2 arguments (x, exp)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        llvm::Type *double_type = llvm::Type::getDoubleTy(context);
        llvm::Type *int_ptr_type = llvm::PointerType::get(llvm::Type::getInt32Ty(context), 0);
        llvm::FunctionType *frexp_type = llvm::FunctionType::get(double_type, {double_type, int_ptr_type}, false);

        llvm::Function *frexp_func = get_or_create_libc_function("frexp", frexp_type);
        llvm::Value *x = ensure_type(args[0], double_type, "frexp.x");
        return builder.CreateCall(frexp_func, {x, args[1]}, "frexp.result");
    }

    llvm::Value *Intrinsics::generate_ldexp(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 2)
        {
            report_error("ldexp requires exactly 2 arguments (x, n)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        llvm::Type *double_type = llvm::Type::getDoubleTy(context);
        llvm::Type *int_type = llvm::Type::getInt32Ty(context);
        llvm::FunctionType *ldexp_type = llvm::FunctionType::get(double_type, {double_type, int_type}, false);

        llvm::Function *ldexp_func = get_or_create_libc_function("ldexp", ldexp_type);
        llvm::Value *x = ensure_type(args[0], double_type, "ldexp.x");
        llvm::Value *n = ensure_type(args[1], int_type, "ldexp.n");
        return builder.CreateCall(ldexp_func, {x, n}, "ldexp.result");
    }

    llvm::Value *Intrinsics::generate_modf(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 2)
        {
            report_error("modf requires exactly 2 arguments (x, iptr)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        llvm::Type *double_type = llvm::Type::getDoubleTy(context);
        llvm::Type *double_ptr_type = llvm::PointerType::get(double_type, 0);
        llvm::FunctionType *modf_type = llvm::FunctionType::get(double_type, {double_type, double_ptr_type}, false);

        llvm::Function *modf_func = get_or_create_libc_function("modf", modf_type);
        llvm::Value *x = ensure_type(args[0], double_type, "modf.x");
        return builder.CreateCall(modf_func, {x, args[1]}, "modf.result");
    }

    // ========================================
    // Network Intrinsics (Stub implementations - require platform-specific headers)
    // ========================================
    
    llvm::Value *Intrinsics::generate_socket(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 3)
        {
            report_error("socket requires exactly 3 arguments (domain, type, protocol)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        llvm::Type *int_type = llvm::Type::getInt32Ty(context);
        llvm::FunctionType *socket_type = llvm::FunctionType::get(int_type, {int_type, int_type, int_type}, false);

        llvm::Function *socket_func = get_or_create_libc_function("socket", socket_type);
        return builder.CreateCall(socket_func, {
            ensure_type(args[0], int_type, "socket.domain"),
            ensure_type(args[1], int_type, "socket.type"),
            ensure_type(args[2], int_type, "socket.protocol")
        }, "socket.result");
    }

    llvm::Value *Intrinsics::generate_bind(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 3)
        {
            report_error("bind requires exactly 3 arguments (sockfd, addr, addrlen)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        llvm::Type *int_type = llvm::Type::getInt32Ty(context);
        llvm::Type *void_ptr_type = llvm::PointerType::get(context, 0);
        llvm::Type *uint32_type = llvm::Type::getInt32Ty(context);
        llvm::FunctionType *bind_type = llvm::FunctionType::get(int_type, {int_type, void_ptr_type, uint32_type}, false);

        llvm::Function *bind_func = get_or_create_libc_function("bind", bind_type);
        return builder.CreateCall(bind_func, {
            ensure_type(args[0], int_type, "bind.sockfd"),
            args[1], // addr
            ensure_type(args[2], uint32_type, "bind.addrlen")
        }, "bind.result");
    }

    // Additional network functions would follow similar pattern...
    // For brevity, I'll add stub implementations that return -1 (not implemented)
    
    #define DEFINE_NETWORK_STUB(name, arg_count) \
    llvm::Value *Intrinsics::generate_##name(const std::vector<llvm::Value *> &args) \
    { \
        report_error(#name " intrinsic not yet fully implemented"); \
        auto &builder = _context_manager.get_builder(); \
        auto &context = _context_manager.get_context(); \
        return llvm::ConstantInt::get(llvm::Type::getInt32Ty(context), -1); \
    }

    DEFINE_NETWORK_STUB(listen, 2)
    DEFINE_NETWORK_STUB(accept, 3)
    DEFINE_NETWORK_STUB(connect, 3)
    DEFINE_NETWORK_STUB(send, 4)
    DEFINE_NETWORK_STUB(recv, 4)
    DEFINE_NETWORK_STUB(sendto, 6)
    DEFINE_NETWORK_STUB(recvfrom, 6)
    DEFINE_NETWORK_STUB(shutdown, 2)
    DEFINE_NETWORK_STUB(setsockopt, 5)
    DEFINE_NETWORK_STUB(getsockopt, 5)
    DEFINE_NETWORK_STUB(getsockname, 3)
    DEFINE_NETWORK_STUB(getpeername, 3)
    DEFINE_NETWORK_STUB(poll, 3)

    // Network byte order functions
    llvm::Value *Intrinsics::generate_htons(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 1)
        {
            report_error("htons requires exactly 1 argument");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        llvm::Type *uint16_type = llvm::Type::getInt16Ty(context);
        llvm::FunctionType *htons_type = llvm::FunctionType::get(uint16_type, {uint16_type}, false);

        llvm::Function *htons_func = get_or_create_libc_function("htons", htons_type);
        return builder.CreateCall(htons_func, {ensure_type(args[0], uint16_type, "htons.hostshort")}, "htons.result");
    }

    llvm::Value *Intrinsics::generate_ntohs(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 1)
        {
            report_error("ntohs requires exactly 1 argument");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        llvm::Type *uint16_type = llvm::Type::getInt16Ty(context);
        llvm::FunctionType *ntohs_type = llvm::FunctionType::get(uint16_type, {uint16_type}, false);

        llvm::Function *ntohs_func = get_or_create_libc_function("ntohs", ntohs_type);
        return builder.CreateCall(ntohs_func, {ensure_type(args[0], uint16_type, "ntohs.netshort")}, "ntohs.result");
    }

    llvm::Value *Intrinsics::generate_htonl(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 1)
        {
            report_error("htonl requires exactly 1 argument");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        llvm::Type *uint32_type = llvm::Type::getInt32Ty(context);
        llvm::FunctionType *htonl_type = llvm::FunctionType::get(uint32_type, {uint32_type}, false);

        llvm::Function *htonl_func = get_or_create_libc_function("htonl", htonl_type);
        return builder.CreateCall(htonl_func, {ensure_type(args[0], uint32_type, "htonl.hostlong")}, "htonl.result");
    }

    llvm::Value *Intrinsics::generate_ntohl(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 1)
        {
            report_error("ntohl requires exactly 1 argument");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        llvm::Type *uint32_type = llvm::Type::getInt32Ty(context);
        llvm::FunctionType *ntohl_type = llvm::FunctionType::get(uint32_type, {uint32_type}, false);

        llvm::Function *ntohl_func = get_or_create_libc_function("ntohl", ntohl_type);
        return builder.CreateCall(ntohl_func, {ensure_type(args[0], uint32_type, "ntohl.netlong")}, "ntohl.result");
    }

    // ========================================
    // Time Intrinsics
    // ========================================

    llvm::Value *Intrinsics::generate_time(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 1)
        {
            report_error("time requires exactly 1 argument (t)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        llvm::Type *time_t_type = llvm::Type::getInt64Ty(context);
        llvm::Type *time_t_ptr_type = llvm::PointerType::get(time_t_type, 0);
        llvm::FunctionType *time_type = llvm::FunctionType::get(time_t_type, {time_t_ptr_type}, false);

        llvm::Function *time_func = get_or_create_libc_function("time", time_type);
        return builder.CreateCall(time_func, {args[0]}, "time.result");
    }

    llvm::Value *Intrinsics::generate_gettimeofday(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 2)
        {
            report_error("gettimeofday requires exactly 2 arguments (tv, tz)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        llvm::Type *void_ptr_type = llvm::PointerType::get(context, 0);
        llvm::Type *int_type = llvm::Type::getInt32Ty(context);
        llvm::FunctionType *gettimeofday_type = llvm::FunctionType::get(int_type, {void_ptr_type, void_ptr_type}, false);

        llvm::Function *gettimeofday_func = get_or_create_libc_function("gettimeofday", gettimeofday_type);
        return builder.CreateCall(gettimeofday_func, {args[0], args[1]}, "gettimeofday.result");
    }

    llvm::Value *Intrinsics::generate_clock_gettime(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 2)
        {
            report_error("clock_gettime requires exactly 2 arguments (clk_id, tp)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        llvm::Type *int_type = llvm::Type::getInt32Ty(context);
        llvm::Type *void_ptr_type = llvm::PointerType::get(context, 0);
        llvm::FunctionType *clock_gettime_type = llvm::FunctionType::get(int_type, {int_type, void_ptr_type}, false);

        llvm::Function *clock_gettime_func = get_or_create_libc_function("clock_gettime", clock_gettime_type);
        return builder.CreateCall(clock_gettime_func, {
            ensure_type(args[0], int_type, "clock_gettime.clk_id"),
            args[1]
        }, "clock_gettime.result");
    }

    llvm::Value *Intrinsics::generate_nanosleep(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 2)
        {
            report_error("nanosleep requires exactly 2 arguments (req, rem)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        llvm::Type *void_ptr_type = llvm::PointerType::get(context, 0);
        llvm::Type *int_type = llvm::Type::getInt32Ty(context);
        llvm::FunctionType *nanosleep_type = llvm::FunctionType::get(int_type, {void_ptr_type, void_ptr_type}, false);

        llvm::Function *nanosleep_func = get_or_create_libc_function("nanosleep", nanosleep_type);
        return builder.CreateCall(nanosleep_func, {args[0], args[1]}, "nanosleep.result");
    }

    llvm::Value *Intrinsics::generate_sleep(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 1)
        {
            report_error("sleep requires exactly 1 argument (seconds)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        llvm::Type *uint32_type = llvm::Type::getInt32Ty(context);
        llvm::FunctionType *sleep_type = llvm::FunctionType::get(uint32_type, {uint32_type}, false);

        llvm::Function *sleep_func = get_or_create_libc_function("sleep", sleep_type);
        return builder.CreateCall(sleep_func, {ensure_type(args[0], uint32_type, "sleep.seconds")}, "sleep.result");
    }

    llvm::Value *Intrinsics::generate_usleep(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 1)
        {
            report_error("usleep requires exactly 1 argument (usec)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        llvm::Type *uint32_type = llvm::Type::getInt32Ty(context);
        llvm::Type *int_type = llvm::Type::getInt32Ty(context);
        llvm::FunctionType *usleep_type = llvm::FunctionType::get(int_type, {uint32_type}, false);

        llvm::Function *usleep_func = get_or_create_libc_function("usleep", usleep_type);
        return builder.CreateCall(usleep_func, {ensure_type(args[0], uint32_type, "usleep.usec")}, "usleep.result");
    }

} // namespace Cryo::Codegen
