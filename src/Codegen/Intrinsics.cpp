#include "Codegen/Intrinsics.hpp"
#include "Codegen/LLVMContext.hpp"
#include "AST/ASTNode.hpp"
#include "Diagnostics/Diag.hpp"
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
    Intrinsics::Intrinsics(LLVMContextManager &context_manager, Cryo::DiagEmitter *diagnostics)
        : _context_manager(context_manager), _diagnostics(diagnostics), _has_errors(false)
    {
    }

    llvm::Value *Intrinsics::generate_intrinsic_call(Cryo::CallExpressionNode *node,
                                                     const std::string &intrinsic_name,
                                                     const std::vector<llvm::Value *> &args)
    {
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "Generating intrinsic call: {}", intrinsic_name);

        // Store current node for error context
        _current_node = node;

        // Check for null arguments — upstream codegen failures can produce nulls
        for (size_t i = 0; i < args.size(); ++i)
        {
            if (!args[i])
            {
                report_error("Intrinsic '" + intrinsic_name + "' has null argument at index " + std::to_string(i));
                return nullptr;
            }
        }

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
        else if (intrinsic_name == "sprintf")
            return generate_sprintf(args);
        else if (intrinsic_name == "snprintf")
            return generate_snprintf(args);
        else if (intrinsic_name == "getchar")
            return generate_getchar(args);
        else if (intrinsic_name == "putchar")
            return generate_putchar(args);
        else if (intrinsic_name == "puts")
            return generate_puts(args);
        else if (intrinsic_name == "fprintf")
            return generate_fprintf(args);

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
        else if (intrinsic_name == "fileno")
            return generate_fileno(args);
        else if (intrinsic_name == "fgets")
            return generate_fgets(args);
        else if (intrinsic_name == "fputs")
            return generate_fputs(args);
        else if (intrinsic_name == "fgetc")
            return generate_fgetc(args);
        else if (intrinsic_name == "fputc")
            return generate_fputc(args);
        else if (intrinsic_name == "sscanf")
            return generate_sscanf(args);

        // Low-level file descriptor I/O
        else if (intrinsic_name == "read")
            return generate_read(args);
        else if (intrinsic_name == "write")
            return generate_write(args);
        else if (intrinsic_name == "pread")
            return generate_pread(args);
        else if (intrinsic_name == "pwrite")
            return generate_pwrite(args);
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
        else if (intrinsic_name == "fsync")
            return generate_fsync(args);
        else if (intrinsic_name == "fdatasync")
            return generate_fdatasync(args);

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
        else if (intrinsic_name == "link")
            return generate_link(args);
        else if (intrinsic_name == "readlink")
            return generate_readlink(args);
        else if (intrinsic_name == "realpath")
            return generate_realpath(args);
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

        // Error handling intrinsics
        else if (intrinsic_name == "errno")
            return generate_errno(args);

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

        // Windows socket initialization intrinsics
        else if (intrinsic_name == "WSAStartup")
            return generate_WSAStartup(args);
        else if (intrinsic_name == "WSACleanup")
            return generate_WSACleanup(args);
        else if (intrinsic_name == "WSAGetLastError")
            return generate_WSAGetLastError(args);

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

        // Threading intrinsics (pthread)
        else if (intrinsic_name == "pthread_create")
            return generate_pthread_create(args);
        else if (intrinsic_name == "pthread_join")
            return generate_pthread_join(args);
        else if (intrinsic_name == "pthread_exit")
            return generate_pthread_exit(args);
        else if (intrinsic_name == "pthread_detach")
            return generate_pthread_detach(args);
        else if (intrinsic_name == "pthread_self")
            return generate_pthread_self(args);
        else if (intrinsic_name == "pthread_equal")
            return generate_pthread_equal(args);
        else if (intrinsic_name == "sched_yield")
            return generate_sched_yield(args);

        // Mutex intrinsics
        else if (intrinsic_name == "pthread_mutex_init")
            return generate_pthread_mutex_init(args);
        else if (intrinsic_name == "pthread_mutex_destroy")
            return generate_pthread_mutex_destroy(args);
        else if (intrinsic_name == "pthread_mutex_lock")
            return generate_pthread_mutex_lock(args);
        else if (intrinsic_name == "pthread_mutex_trylock")
            return generate_pthread_mutex_trylock(args);
        else if (intrinsic_name == "pthread_mutex_unlock")
            return generate_pthread_mutex_unlock(args);

        // Condition variable intrinsics
        else if (intrinsic_name == "pthread_cond_init")
            return generate_pthread_cond_init(args);
        else if (intrinsic_name == "pthread_cond_destroy")
            return generate_pthread_cond_destroy(args);
        else if (intrinsic_name == "pthread_cond_wait")
            return generate_pthread_cond_wait(args);
        else if (intrinsic_name == "pthread_cond_timedwait")
            return generate_pthread_cond_timedwait(args);
        else if (intrinsic_name == "pthread_cond_signal")
            return generate_pthread_cond_signal(args);
        else if (intrinsic_name == "pthread_cond_broadcast")
            return generate_pthread_cond_broadcast(args);

        // Read-write lock intrinsics
        else if (intrinsic_name == "pthread_rwlock_init")
            return generate_pthread_rwlock_init(args);
        else if (intrinsic_name == "pthread_rwlock_destroy")
            return generate_pthread_rwlock_destroy(args);
        else if (intrinsic_name == "pthread_rwlock_rdlock")
            return generate_pthread_rwlock_rdlock(args);
        else if (intrinsic_name == "pthread_rwlock_tryrdlock")
            return generate_pthread_rwlock_tryrdlock(args);
        else if (intrinsic_name == "pthread_rwlock_wrlock")
            return generate_pthread_rwlock_wrlock(args);
        else if (intrinsic_name == "pthread_rwlock_trywrlock")
            return generate_pthread_rwlock_trywrlock(args);
        else if (intrinsic_name == "pthread_rwlock_unlock")
            return generate_pthread_rwlock_unlock(args);

        // Thread-local storage intrinsics
        else if (intrinsic_name == "pthread_key_create")
            return generate_pthread_key_create(args);
        else if (intrinsic_name == "pthread_key_delete")
            return generate_pthread_key_delete(args);
        else if (intrinsic_name == "pthread_getspecific")
            return generate_pthread_getspecific(args);
        else if (intrinsic_name == "pthread_setspecific")
            return generate_pthread_setspecific(args);

        // Atomic intrinsics
        else if (intrinsic_name == "atomic_load_8")
            return generate_atomic_load_8(args);
        else if (intrinsic_name == "atomic_load_16")
            return generate_atomic_load_16(args);
        else if (intrinsic_name == "atomic_load_32")
            return generate_atomic_load_32(args);
        else if (intrinsic_name == "atomic_load_64")
            return generate_atomic_load_64(args);
        else if (intrinsic_name == "atomic_store_8")
            return generate_atomic_store_8(args);
        else if (intrinsic_name == "atomic_store_16")
            return generate_atomic_store_16(args);
        else if (intrinsic_name == "atomic_store_32")
            return generate_atomic_store_32(args);
        else if (intrinsic_name == "atomic_store_64")
            return generate_atomic_store_64(args);
        else if (intrinsic_name == "atomic_exchange_32")
            return generate_atomic_exchange_32(args);
        else if (intrinsic_name == "atomic_exchange_64")
            return generate_atomic_exchange_64(args);
        else if (intrinsic_name == "atomic_swap_64")
            return generate_atomic_swap_64(args);
        else if (intrinsic_name == "atomic_compare_exchange_32")
            return generate_atomic_compare_exchange_32(args);
        else if (intrinsic_name == "atomic_compare_exchange_64")
            return generate_atomic_compare_exchange_64(args);
        else if (intrinsic_name == "atomic_fetch_add_32")
            return generate_atomic_fetch_add_32(args);
        else if (intrinsic_name == "atomic_fetch_add_64")
            return generate_atomic_fetch_add_64(args);
        else if (intrinsic_name == "atomic_fetch_sub_32")
            return generate_atomic_fetch_sub_32(args);
        else if (intrinsic_name == "atomic_fetch_sub_64")
            return generate_atomic_fetch_sub_64(args);
        else if (intrinsic_name == "atomic_fetch_and_32")
            return generate_atomic_fetch_and_32(args);
        else if (intrinsic_name == "atomic_fetch_and_64")
            return generate_atomic_fetch_and_64(args);
        else if (intrinsic_name == "atomic_fetch_or_32")
            return generate_atomic_fetch_or_32(args);
        else if (intrinsic_name == "atomic_fetch_or_64")
            return generate_atomic_fetch_or_64(args);
        else if (intrinsic_name == "atomic_fetch_xor_32")
            return generate_atomic_fetch_xor_32(args);
        else if (intrinsic_name == "atomic_fetch_xor_64")
            return generate_atomic_fetch_xor_64(args);
        else if (intrinsic_name == "atomic_fence")
            return generate_atomic_fence(args);

        // Enhanced network intrinsics
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

        // Atomic intrinsics
        else if (intrinsic_name == "atomic_load_8")
            return generate_atomic_load_8(args);
        else if (intrinsic_name == "atomic_load_16")
            return generate_atomic_load_16(args);
        else if (intrinsic_name == "atomic_load_32")
            return generate_atomic_load_32(args);
        else if (intrinsic_name == "atomic_load_64")
            return generate_atomic_load_64(args);
        else if (intrinsic_name == "atomic_store_8")
            return generate_atomic_store_8(args);
        else if (intrinsic_name == "atomic_store_16")
            return generate_atomic_store_16(args);
        else if (intrinsic_name == "atomic_store_32")
            return generate_atomic_store_32(args);
        else if (intrinsic_name == "atomic_store_64")
            return generate_atomic_store_64(args);
        else if (intrinsic_name == "atomic_exchange_32")
            return generate_atomic_exchange_32(args);
        else if (intrinsic_name == "atomic_exchange_64")
            return generate_atomic_exchange_64(args);
        else if (intrinsic_name == "atomic_swap_64")
            return generate_atomic_swap_64(args);
        else if (intrinsic_name == "atomic_compare_exchange_32")
            return generate_atomic_compare_exchange_32(args);
        else if (intrinsic_name == "atomic_compare_exchange_64")
            return generate_atomic_compare_exchange_64(args);
        else if (intrinsic_name == "atomic_fetch_add_32")
            return generate_atomic_fetch_add_32(args);
        else if (intrinsic_name == "atomic_fetch_add_64")
            return generate_atomic_fetch_add_64(args);
        else if (intrinsic_name == "atomic_fetch_sub_32")
            return generate_atomic_fetch_sub_32(args);
        else if (intrinsic_name == "atomic_fetch_sub_64")
            return generate_atomic_fetch_sub_64(args);
        else if (intrinsic_name == "atomic_fetch_and_32")
            return generate_atomic_fetch_and_32(args);
        else if (intrinsic_name == "atomic_fetch_and_64")
            return generate_atomic_fetch_and_64(args);
        else if (intrinsic_name == "atomic_fetch_or_32")
            return generate_atomic_fetch_or_32(args);
        else if (intrinsic_name == "atomic_fetch_or_64")
            return generate_atomic_fetch_or_64(args);
        else if (intrinsic_name == "atomic_fetch_xor_32")
            return generate_atomic_fetch_xor_32(args);
        else if (intrinsic_name == "atomic_fetch_xor_64")
            return generate_atomic_fetch_xor_64(args);
        else if (intrinsic_name == "atomic_fence")
            return generate_atomic_fence(args);

        // Additional 8-bit atomic operations
        else if (intrinsic_name == "atomic_swap_8")
            return generate_atomic_swap_8(args);
        else if (intrinsic_name == "atomic_exchange_8")
            return generate_atomic_exchange_8(args);
        else if (intrinsic_name == "atomic_compare_exchange_8")
            return generate_atomic_compare_exchange_8(args);
        else if (intrinsic_name == "atomic_fetch_and_8")
            return generate_atomic_fetch_and_8(args);
        else if (intrinsic_name == "atomic_fetch_or_8")
            return generate_atomic_fetch_or_8(args);
        else if (intrinsic_name == "atomic_fetch_xor_8")
            return generate_atomic_fetch_xor_8(args);
        else if (intrinsic_name == "atomic_fetch_nand_8")
            return generate_atomic_fetch_nand_8(args);

        // Typed atomic load/store functions
        else if (intrinsic_name == "atomic_load_u8")
            return generate_atomic_load_u8(args);
        else if (intrinsic_name == "atomic_store_u8")
            return generate_atomic_store_u8(args);
        else if (intrinsic_name == "atomic_swap_u8")
            return generate_atomic_swap_u8(args);
        else if (intrinsic_name == "atomic_cmpxchg_u8")
            return generate_atomic_cmpxchg_u8(args);
        else if (intrinsic_name == "atomic_fetch_and_u8")
            return generate_atomic_fetch_and_u8(args);
        else if (intrinsic_name == "atomic_fetch_or_u8")
            return generate_atomic_fetch_or_u8(args);
        else if (intrinsic_name == "atomic_fetch_xor_u8")
            return generate_atomic_fetch_xor_u8(args);
        else if (intrinsic_name == "atomic_fetch_nand_u8")
            return generate_atomic_fetch_nand_u8(args);

        // Signed 32-bit atomic operations
        else if (intrinsic_name == "atomic_load_i32")
            return generate_atomic_load_i32(args);
        else if (intrinsic_name == "atomic_store_i32")
            return generate_atomic_store_i32(args);
        else if (intrinsic_name == "atomic_swap_i32")
            return generate_atomic_swap_i32(args);
        else if (intrinsic_name == "atomic_cmpxchg_i32")
            return generate_atomic_cmpxchg_i32(args);
        else if (intrinsic_name == "atomic_fetch_add_i32")
            return generate_atomic_fetch_add_i32(args);
        else if (intrinsic_name == "atomic_fetch_sub_i32")
            return generate_atomic_fetch_sub_i32(args);
        else if (intrinsic_name == "atomic_fetch_and_i32")
            return generate_atomic_fetch_and_i32(args);
        else if (intrinsic_name == "atomic_fetch_or_i32")
            return generate_atomic_fetch_or_i32(args);
        else if (intrinsic_name == "atomic_fetch_xor_i32")
            return generate_atomic_fetch_xor_i32(args);
        else if (intrinsic_name == "atomic_fetch_max_i32")
            return generate_atomic_fetch_max_i32(args);
        else if (intrinsic_name == "atomic_fetch_min_i32")
            return generate_atomic_fetch_min_i32(args);

        // Unsigned 32-bit atomic operations
        else if (intrinsic_name == "atomic_load_u32")
            return generate_atomic_load_u32(args);
        else if (intrinsic_name == "atomic_store_u32")
            return generate_atomic_store_u32(args);
        else if (intrinsic_name == "atomic_swap_u32")
            return generate_atomic_swap_u32(args);
        else if (intrinsic_name == "atomic_cmpxchg_u32")
            return generate_atomic_cmpxchg_u32(args);
        else if (intrinsic_name == "atomic_fetch_add_u32")
            return generate_atomic_fetch_add_u32(args);
        else if (intrinsic_name == "atomic_fetch_sub_u32")
            return generate_atomic_fetch_sub_u32(args);
        else if (intrinsic_name == "atomic_fetch_and_u32")
            return generate_atomic_fetch_and_u32(args);
        else if (intrinsic_name == "atomic_fetch_or_u32")
            return generate_atomic_fetch_or_u32(args);
        else if (intrinsic_name == "atomic_fetch_xor_u32")
            return generate_atomic_fetch_xor_u32(args);

        // Signed 64-bit atomic operations
        else if (intrinsic_name == "atomic_load_i64")
            return generate_atomic_load_i64(args);
        else if (intrinsic_name == "atomic_store_i64")
            return generate_atomic_store_i64(args);
        else if (intrinsic_name == "atomic_swap_i64")
            return generate_atomic_swap_i64(args);
        else if (intrinsic_name == "atomic_cmpxchg_i64")
            return generate_atomic_cmpxchg_i64(args);
        else if (intrinsic_name == "atomic_fetch_add_i64")
            return generate_atomic_fetch_add_i64(args);
        else if (intrinsic_name == "atomic_fetch_sub_i64")
            return generate_atomic_fetch_sub_i64(args);

        // Unsigned 64-bit atomic operations
        else if (intrinsic_name == "atomic_load_u64")
            return generate_atomic_load_u64(args);
        else if (intrinsic_name == "atomic_store_u64")
            return generate_atomic_store_u64(args);
        else if (intrinsic_name == "atomic_swap_u64")
            return generate_atomic_swap_u64(args);
        else if (intrinsic_name == "atomic_cmpxchg_u64")
            return generate_atomic_cmpxchg_u64(args);
        else if (intrinsic_name == "atomic_fetch_add_u64")
            return generate_atomic_fetch_add_u64(args);
        else if (intrinsic_name == "atomic_fetch_sub_u64")
            return generate_atomic_fetch_sub_u64(args);

        // Integer type conversion intrinsics
        else if (intrinsic_name == "i8_to_i16")
            return generate_int_conversion(args, 8, 16, true, true);
        else if (intrinsic_name == "i8_to_i32")
            return generate_int_conversion(args, 8, 32, true, true);
        else if (intrinsic_name == "i8_to_i64")
            return generate_int_conversion(args, 8, 64, true, true);
        else if (intrinsic_name == "i16_to_i32")
            return generate_int_conversion(args, 16, 32, true, true);
        else if (intrinsic_name == "i16_to_i64")
            return generate_int_conversion(args, 16, 64, true, true);
        else if (intrinsic_name == "i32_to_i64")
            return generate_int_conversion(args, 32, 64, true, true);
        else if (intrinsic_name == "i64_to_i32")
            return generate_int_conversion(args, 64, 32, true, true);
        else if (intrinsic_name == "i64_to_i16")
            return generate_int_conversion(args, 64, 16, true, true);
        else if (intrinsic_name == "i64_to_i8")
            return generate_int_conversion(args, 64, 8, true, true);
        else if (intrinsic_name == "i32_to_i16")
            return generate_int_conversion(args, 32, 16, true, true);
        else if (intrinsic_name == "i32_to_i8")
            return generate_int_conversion(args, 32, 8, true, true);
        else if (intrinsic_name == "i16_to_i8")
            return generate_int_conversion(args, 16, 8, true, true);
        else if (intrinsic_name == "u8_to_u16")
            return generate_int_conversion(args, 8, 16, false, false);
        else if (intrinsic_name == "u8_to_u32")
            return generate_int_conversion(args, 8, 32, false, false);
        else if (intrinsic_name == "u8_to_u64")
            return generate_int_conversion(args, 8, 64, false, false);
        else if (intrinsic_name == "u16_to_u32")
            return generate_int_conversion(args, 16, 32, false, false);
        else if (intrinsic_name == "u16_to_u64")
            return generate_int_conversion(args, 16, 64, false, false);
        else if (intrinsic_name == "u32_to_u64")
            return generate_int_conversion(args, 32, 64, false, false);
        else if (intrinsic_name == "u64_to_u32")
            return generate_int_conversion(args, 64, 32, false, false);
        else if (intrinsic_name == "u64_to_u16")
            return generate_int_conversion(args, 64, 16, false, false);
        else if (intrinsic_name == "u64_to_u8")
            return generate_int_conversion(args, 64, 8, false, false);
        else if (intrinsic_name == "u32_to_u16")
            return generate_int_conversion(args, 32, 16, false, false);
        else if (intrinsic_name == "u32_to_u8")
            return generate_int_conversion(args, 32, 8, false, false);
        else if (intrinsic_name == "u16_to_u8")
            return generate_int_conversion(args, 16, 8, false, false);
        // Sign conversions
        else if (intrinsic_name == "i32_to_u32")
            return generate_int_conversion(args, 32, 32, true, false);
        else if (intrinsic_name == "u32_to_i32")
            return generate_int_conversion(args, 32, 32, false, true);
        else if (intrinsic_name == "i64_to_u64")
            return generate_int_conversion(args, 64, 64, true, false);
        else if (intrinsic_name == "u64_to_i64")
            return generate_int_conversion(args, 64, 64, false, true);
        else if (intrinsic_name == "u8_to_i8")
            return generate_int_conversion(args, 8, 8, false, true);
        else if (intrinsic_name == "i8_to_u8")
            return generate_int_conversion(args, 8, 8, true, false);

        // Pointer arithmetic intrinsics
        else if (intrinsic_name == "ptr_add")
            return generate_ptr_add(args);
        else if (intrinsic_name == "ptr_sub")
            return generate_ptr_sub(args);
        else if (intrinsic_name == "ptr_diff")
            return generate_ptr_diff(args);

        // Additional float math intrinsics
        else if (intrinsic_name == "truncf")
            return generate_truncf(args);
        else if (intrinsic_name == "log10f")
            return generate_log10f(args);
        else if (intrinsic_name == "log2f")
            return generate_log2f(args);

        // Float to float conversions
        else if (intrinsic_name == "f32_to_f64")
            return generate_float_conversion(args, true);
        else if (intrinsic_name == "f64_to_f32")
            return generate_float_conversion(args, false);

        // Int to float conversions
        else if (intrinsic_name == "i32_to_f32")
            return generate_int_to_float(args, 32, true, true);
        else if (intrinsic_name == "i32_to_f64")
            return generate_int_to_float(args, 32, true, false);
        else if (intrinsic_name == "i64_to_f64")
            return generate_int_to_float(args, 64, true, false);
        else if (intrinsic_name == "u32_to_f32")
            return generate_int_to_float(args, 32, false, true);
        else if (intrinsic_name == "u32_to_f64")
            return generate_int_to_float(args, 32, false, false);
        else if (intrinsic_name == "u64_to_f64")
            return generate_int_to_float(args, 64, false, false);

        // Float to int conversions
        else if (intrinsic_name == "f32_to_i32")
            return generate_float_to_int(args, true, 32, true);
        else if (intrinsic_name == "f64_to_i32")
            return generate_float_to_int(args, false, 32, true);
        else if (intrinsic_name == "f64_to_i64")
            return generate_float_to_int(args, false, 64, true);
        else if (intrinsic_name == "f32_to_u32")
            return generate_float_to_int(args, true, 32, false);
        else if (intrinsic_name == "f64_to_u32")
            return generate_float_to_int(args, false, 32, false);
        else if (intrinsic_name == "f64_to_u64")
            return generate_float_to_int(args, false, 64, false);

        else if (intrinsic_name == "panic")
            return generate_panic(args);

        // Variadic argument intrinsics
        else if (intrinsic_name == "va_arg_i32")
            return generate_va_arg_i32(args);
        else if (intrinsic_name == "va_arg_i64")
            return generate_va_arg_i64(args);
        else if (intrinsic_name == "va_arg_u64")
            return generate_va_arg_u64(args);
        else if (intrinsic_name == "va_arg_f64")
            return generate_va_arg_f64(args);
        else if (intrinsic_name == "va_arg_ptr")
            return generate_va_arg_ptr(args);

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
            report_error("malloc requires exactly 1 argument (size)");
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
            report_error("free requires exactly 1 argument (ptr)");
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
            report_error("free argument must be a pointer");
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
            report_error("memcpy requires exactly 3 arguments (dest, src, n), but got " +
                         std::to_string(args.size()));
            return nullptr;
        }

        if (!validate_args("memcpy", args))
            return nullptr;

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
            report_error("memcpy first two arguments must be pointers");
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
            report_error("memset requires exactly 3 arguments (ptr, value, n), but got " +
                         std::to_string(args.size()));
            return nullptr;
        }

        if (!validate_args("memset", args))
            return nullptr;

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
            report_error("memset first argument must be a pointer");
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
            report_error("memmove requires exactly 3 arguments (dest, src, n), but got " +
                         std::to_string(args.size()));
            return nullptr;
        }

        if (!validate_args("memmove", args))
            return nullptr;

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
            report_error("memmove first two arguments must be pointers");
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
    // Float Math Intrinsics (f32 variants)
    // ========================================

    llvm::Value *Intrinsics::generate_truncf(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 1)
        {
            report_error("truncf requires exactly 1 argument");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();
        auto *module = _context_manager.get_module();

        llvm::Type *f32_type = llvm::Type::getFloatTy(context);
        llvm::FunctionType *fn_type = llvm::FunctionType::get(f32_type, {f32_type}, false);
        llvm::Function *fn = get_or_create_libc_function("truncf", fn_type);

        llvm::Value *arg = ensure_type(args[0], f32_type, "truncf.arg");
        return builder.CreateCall(fn, {arg}, "truncf.result");
    }

    llvm::Value *Intrinsics::generate_log10f(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 1)
        {
            report_error("log10f requires exactly 1 argument");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        llvm::Type *f32_type = llvm::Type::getFloatTy(context);
        llvm::FunctionType *fn_type = llvm::FunctionType::get(f32_type, {f32_type}, false);
        llvm::Function *fn = get_or_create_libc_function("log10f", fn_type);

        llvm::Value *arg = ensure_type(args[0], f32_type, "log10f.arg");
        return builder.CreateCall(fn, {arg}, "log10f.result");
    }

    llvm::Value *Intrinsics::generate_log2f(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 1)
        {
            report_error("log2f requires exactly 1 argument");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        llvm::Type *f32_type = llvm::Type::getFloatTy(context);
        llvm::FunctionType *fn_type = llvm::FunctionType::get(f32_type, {f32_type}, false);
        llvm::Function *fn = get_or_create_libc_function("log2f", fn_type);

        llvm::Value *arg = ensure_type(args[0], f32_type, "log2f.arg");
        return builder.CreateCall(fn, {arg}, "log2f.result");
    }

    // ========================================
    // Float Conversion Intrinsics
    // ========================================

    llvm::Value *Intrinsics::generate_float_conversion(const std::vector<llvm::Value *> &args, bool f32_to_f64)
    {
        if (args.size() != 1)
        {
            report_error("Float conversion requires exactly 1 argument");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        llvm::Type *f32_type = llvm::Type::getFloatTy(context);
        llvm::Type *f64_type = llvm::Type::getDoubleTy(context);

        if (f32_to_f64)
        {
            // f32 -> f64: extend
            llvm::Value *arg = ensure_type(args[0], f32_type, "f32_to_f64.arg");
            return builder.CreateFPExt(arg, f64_type, "f32_to_f64.result");
        }
        else
        {
            // f64 -> f32: truncate
            llvm::Value *arg = ensure_type(args[0], f64_type, "f64_to_f32.arg");
            return builder.CreateFPTrunc(arg, f32_type, "f64_to_f32.result");
        }
    }

    llvm::Value *Intrinsics::generate_int_to_float(const std::vector<llvm::Value *> &args,
                                                   int from_bits, bool is_signed, bool to_f32)
    {
        if (args.size() != 1)
        {
            report_error("Int to float conversion requires exactly 1 argument");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        llvm::Type *int_type = llvm::Type::getIntNTy(context, from_bits);
        llvm::Type *float_type = to_f32 ? llvm::Type::getFloatTy(context) : llvm::Type::getDoubleTy(context);

        llvm::Value *arg = ensure_type(args[0], int_type, "int_to_float.arg");

        if (is_signed)
        {
            return builder.CreateSIToFP(arg, float_type, "int_to_float.result");
        }
        else
        {
            return builder.CreateUIToFP(arg, float_type, "int_to_float.result");
        }
    }

    llvm::Value *Intrinsics::generate_float_to_int(const std::vector<llvm::Value *> &args,
                                                   bool from_f32, int to_bits, bool is_signed)
    {
        if (args.size() != 1)
        {
            report_error("Float to int conversion requires exactly 1 argument");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        llvm::Type *float_type = from_f32 ? llvm::Type::getFloatTy(context) : llvm::Type::getDoubleTy(context);
        llvm::Type *int_type = llvm::Type::getIntNTy(context, to_bits);

        llvm::Value *arg = ensure_type(args[0], float_type, "float_to_int.arg");

        if (is_signed)
        {
            return builder.CreateFPToSI(arg, int_type, "float_to_int.result");
        }
        else
        {
            return builder.CreateFPToUI(arg, int_type, "float_to_int.result");
        }
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
            report_error("strcmp requires exactly 2 arguments (str1, str2)");
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

        // Handle array-to-pointer decay for both arguments
        // If the argument is an array type, we need to get a pointer to the first element
        if (str1_arg->getType()->isArrayTy())
        {
            // Create alloca for the array and get pointer to first element
            llvm::Type *array_type = str1_arg->getType();
            llvm::AllocaInst *alloca = builder.CreateAlloca(array_type, nullptr, "strcmp.arr1");
            builder.CreateStore(str1_arg, alloca);
            str1_arg = builder.CreateInBoundsGEP(array_type, alloca,
                {llvm::ConstantInt::get(llvm::Type::getInt32Ty(context), 0),
                 llvm::ConstantInt::get(llvm::Type::getInt32Ty(context), 0)}, "strcmp.ptr1");
        }

        if (str2_arg->getType()->isArrayTy())
        {
            llvm::Type *array_type = str2_arg->getType();
            llvm::AllocaInst *alloca = builder.CreateAlloca(array_type, nullptr, "strcmp.arr2");
            builder.CreateStore(str2_arg, alloca);
            str2_arg = builder.CreateInBoundsGEP(array_type, alloca,
                {llvm::ConstantInt::get(llvm::Type::getInt32Ty(context), 0),
                 llvm::ConstantInt::get(llvm::Type::getInt32Ty(context), 0)}, "strcmp.ptr2");
        }

        if (!str1_arg->getType()->isPointerTy() || !str2_arg->getType()->isPointerTy())
        {
            report_error("strcmp arguments must be pointers or arrays");
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
            report_error("printf requires at least 1 argument (format)");
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
            report_error("printf format argument must be a pointer");
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
        // __panic__ can be called with:
        //   0 args: just panic
        //   1 arg:  panic with message
        //   3 args: panic with message, file, line (stdlib convention)
        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        if (args.size() != 0 && args.size() != 1 && args.size() != 3)
        {
            report_error("__panic__ requires 0, 1, or 3 arguments (message, file, line)");
            return nullptr;
        }

        llvm::Type *char_ptr_type = llvm::PointerType::get(context, 0);
        llvm::Type *int_type = llvm::Type::getInt32Ty(context);

        if (args.size() == 3)
        {
            // 3-arg form: panic(message, file, line)
            // Print: "panic: <message> at <file>:<line>\n"
            llvm::FunctionType *printf_type = llvm::FunctionType::get(
                int_type, {char_ptr_type}, true); // variadic
            llvm::Function *printf_func = get_or_create_libc_function("printf", printf_type);

            llvm::Module *module = _context_manager.get_module();
            llvm::Constant *fmt = builder.CreateGlobalStringPtr("panic: %s at %s:%u\n", "panic.fmt", 0, module);

            builder.CreateCall(printf_func, {fmt, args[0], args[1], args[2]}, "panic.print");
        }
        else if (args.size() == 1)
        {
            // 1-arg form: panic(message)
            llvm::FunctionType *printf_type = llvm::FunctionType::get(
                int_type, {char_ptr_type}, true); // variadic
            llvm::Function *printf_func = get_or_create_libc_function("printf", printf_type);

            if (args[0]->getType()->isPointerTy())
            {
                llvm::Module *module = _context_manager.get_module();
                llvm::Constant *fmt = builder.CreateGlobalStringPtr("panic: %s\n", "panic.fmt1", 0, module);
                builder.CreateCall(printf_func, {fmt, args[0]}, "panic.print");
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

        llvm::Type *double_type = llvm::Type::getDoubleTy(context);
        llvm::Value *arg = ensure_type(args[0], double_type, "sqrt.x");

        // Use LLVM sqrt intrinsic
        llvm::Function *sqrt_intrinsic = llvm::Intrinsic::getDeclaration(
            _context_manager.get_module(),
            llvm::Intrinsic::sqrt,
            {double_type});

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

        llvm::Type *double_type = llvm::Type::getDoubleTy(context);
        llvm::Value *base = ensure_type(args[0], double_type, "pow.base");
        llvm::Value *exp = ensure_type(args[1], double_type, "pow.exp");

        // Use LLVM pow intrinsic
        llvm::Function *pow_intrinsic = llvm::Intrinsic::getDeclaration(
            _context_manager.get_module(),
            llvm::Intrinsic::pow,
            {double_type});

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
        auto &context = _context_manager.get_context();

        llvm::Type *double_type = llvm::Type::getDoubleTy(context);
        llvm::Value *arg = ensure_type(args[0], double_type, "sin.x");

        // Use LLVM sin intrinsic
        llvm::Function *sin_intrinsic = llvm::Intrinsic::getDeclaration(
            _context_manager.get_module(),
            llvm::Intrinsic::sin,
            {double_type});

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
        auto &context = _context_manager.get_context();

        llvm::Type *double_type = llvm::Type::getDoubleTy(context);
        llvm::Value *arg = ensure_type(args[0], double_type, "cos.x");

        // Use LLVM cos intrinsic
        llvm::Function *cos_intrinsic = llvm::Intrinsic::getDeclaration(
            _context_manager.get_module(),
            llvm::Intrinsic::cos,
            {double_type});

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

        // Report to DiagEmitter if available, with location context from current node
        if (_diagnostics)
        {
            auto diag = Diag::error(Cryo::ErrorCode::E0600_CODEGEN_FAILED, message);
            if (_current_node)
            {
                diag.at(_current_node);
            }
            _diagnostics->emit(std::move(diag));
        }

        std::cerr << "[Intrinsics] Error: " << message << std::endl;
    }

    bool Intrinsics::validate_args(const std::string &intrinsic_name,
                                   const std::vector<llvm::Value *> &args)
    {
        for (size_t i = 0; i < args.size(); ++i)
        {
            if (!args[i])
            {
                report_error(intrinsic_name + ": argument " + std::to_string(i + 1) + " failed to generate");
                return false;
            }
        }
        return true;
    }

    void Intrinsics::report_unimplemented_intrinsic(const std::string &intrinsic_name, Cryo::CallExpressionNode *node)
    {
        _has_errors = true;
        _last_error = "Unimplemented intrinsic: " + intrinsic_name;

        // Report to DiagEmitter if available
        if (_diagnostics)
        {
            std::string message = "Intrinsic function '" + intrinsic_name + "' is called but not implemented";

            auto diag = Diag::error(Cryo::ErrorCode::E0604_UNIMPLEMENTED_INTRINSIC, message);
            if (node)
            {
                diag.at(node);
            }
            _diagnostics->emit(std::move(diag));
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

        // Handle pointer to floating-point conversion.
        // This handles &this in primitive implement blocks where the alloca stores
        // a pointer to the primitive value (double indirection: alloca ptr → ptr → f64).
        if (current_type->isPointerTy() && target_type->isFloatingPointTy())
        {
            if (auto *alloca_inst = llvm::dyn_cast<llvm::AllocaInst>(value))
            {
                if (alloca_inst->getAllocatedType()->isPointerTy())
                {
                    // Double indirection: alloca stores ptr → load ptr, then load float
                    llvm::Value *loaded_ptr = builder.CreateLoad(
                        alloca_inst->getAllocatedType(), value, name + ".ptr.deref");
                    return builder.CreateLoad(target_type, loaded_ptr, name + ".val");
                }
            }
            // Single indirection: pointer directly to float value
            return builder.CreateLoad(target_type, value, name + ".load");
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

        // Handle buffer argument - may be an array that needs pointer decay
        llvm::Value *buffer_arg = args[0];
        if (buffer_arg->getType()->isArrayTy())
        {
            llvm::Type *array_type = buffer_arg->getType();
            llvm::AllocaInst *alloca = builder.CreateAlloca(array_type, nullptr, "snprintf.buf.arr");
            builder.CreateStore(buffer_arg, alloca);
            buffer_arg = builder.CreateInBoundsGEP(array_type, alloca,
                {llvm::ConstantInt::get(int_type, 0),
                 llvm::ConstantInt::get(int_type, 0)}, "snprintf.buf.ptr");
        }

        // Prepare arguments
        std::vector<llvm::Value *> call_args;
        call_args.push_back(buffer_arg);                                         // buffer (pointer)
        call_args.push_back(ensure_type(args[1], size_t_type, "snprintf.size")); // size
        call_args.push_back(args[2]);                                            // format

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
            report_error("fclose requires exactly 1 argument (file/fd)");
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
            report_error("fseek requires exactly 3 arguments (file/fd, offset, whence)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        // Create fseek function type: int fseek(FILE* file, long offset, int whence)
        llvm::Type *void_ptr_type = llvm::PointerType::get(context, 0);
        llvm::Type *i64_type = llvm::Type::getInt64Ty(context);
        llvm::Type *int_type = llvm::Type::getInt32Ty(context);
        llvm::FunctionType *fseek_type = llvm::FunctionType::get(
            int_type, {void_ptr_type, i64_type, int_type}, false);

        llvm::Function *fseek_func = get_or_create_libc_function("fseek", fseek_type);

        std::vector<llvm::Value *> call_args = {
            args[0],
            ensure_type(args[1], i64_type, "fseek.offset"),
            ensure_type(args[2], int_type, "fseek.whence")};

        return builder.CreateCall(fseek_func, call_args, "fseek.result");
    }

    llvm::Value *Intrinsics::generate_ftell(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 1)
        {
            report_error("ftell requires exactly 1 argument (file/fd)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        // Create ftell function type: long ftell(FILE* file)
        llvm::Type *void_ptr_type = llvm::PointerType::get(context, 0);
        llvm::Type *i64_type = llvm::Type::getInt64Ty(context);
        llvm::FunctionType *ftell_type = llvm::FunctionType::get(i64_type, {void_ptr_type}, false);

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

    llvm::Value *Intrinsics::generate_fileno(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 1)
        {
            report_error("fileno requires exactly 1 argument (file)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        // Create fileno function type: int fileno(FILE* file)
        llvm::Type *void_ptr_type = llvm::PointerType::get(context, 0);
        llvm::Type *int_type = llvm::Type::getInt32Ty(context);
        llvm::FunctionType *fileno_type = llvm::FunctionType::get(int_type, {void_ptr_type}, false);

        llvm::Function *fileno_func = get_or_create_libc_function("fileno", fileno_type);
        return builder.CreateCall(fileno_func, {args[0]}, "fileno.result");
    }

    llvm::Value *Intrinsics::generate_fgets(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 3)
        {
            report_error("fgets requires exactly 3 arguments (buffer, size, file)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        // Create fgets function type: char* fgets(char* str, int n, FILE* stream)
        llvm::Type *void_ptr_type = llvm::PointerType::get(context, 0);
        llvm::Type *int_type = llvm::Type::getInt32Ty(context);
        llvm::FunctionType *fgets_type = llvm::FunctionType::get(
            void_ptr_type, {void_ptr_type, int_type, void_ptr_type}, false);

        llvm::Function *fgets_func = get_or_create_libc_function("fgets", fgets_type);

        std::vector<llvm::Value *> call_args = {
            args[0],
            ensure_type(args[1], int_type, "fgets.size"),
            args[2]};

        return builder.CreateCall(fgets_func, call_args, "fgets.result");
    }

    llvm::Value *Intrinsics::generate_fputs(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 2)
        {
            report_error("fputs requires exactly 2 arguments (str, file)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        // Create fputs function type: int fputs(const char* str, FILE* stream)
        llvm::Type *void_ptr_type = llvm::PointerType::get(context, 0);
        llvm::Type *int_type = llvm::Type::getInt32Ty(context);
        llvm::FunctionType *fputs_type = llvm::FunctionType::get(
            int_type, {void_ptr_type, void_ptr_type}, false);

        llvm::Function *fputs_func = get_or_create_libc_function("fputs", fputs_type);
        return builder.CreateCall(fputs_func, {args[0], args[1]}, "fputs.result");
    }

    llvm::Value *Intrinsics::generate_fgetc(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 1)
        {
            report_error("fgetc requires exactly 1 argument (file)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        // Create fgetc function type: int fgetc(FILE* stream)
        llvm::Type *void_ptr_type = llvm::PointerType::get(context, 0);
        llvm::Type *int_type = llvm::Type::getInt32Ty(context);
        llvm::FunctionType *fgetc_type = llvm::FunctionType::get(int_type, {void_ptr_type}, false);

        llvm::Function *fgetc_func = get_or_create_libc_function("fgetc", fgetc_type);
        return builder.CreateCall(fgetc_func, {args[0]}, "fgetc.result");
    }

    llvm::Value *Intrinsics::generate_fputc(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 2)
        {
            report_error("fputc requires exactly 2 arguments (char, file)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        // Create fputc function type: int fputc(int c, FILE* stream)
        llvm::Type *void_ptr_type = llvm::PointerType::get(context, 0);
        llvm::Type *int_type = llvm::Type::getInt32Ty(context);
        llvm::FunctionType *fputc_type = llvm::FunctionType::get(
            int_type, {int_type, void_ptr_type}, false);

        llvm::Function *fputc_func = get_or_create_libc_function("fputc", fputc_type);

        std::vector<llvm::Value *> call_args = {
            ensure_type(args[0], int_type, "fputc.char"),
            args[1]};

        return builder.CreateCall(fputc_func, call_args, "fputc.result");
    }

    llvm::Value *Intrinsics::generate_sscanf(const std::vector<llvm::Value *> &args)
    {
        if (args.size() < 2)
        {
            report_error("sscanf requires at least 2 arguments (str, format, ...)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        // Create sscanf function type: int sscanf(const char* str, const char* format, ...)
        llvm::Type *char_ptr_type = llvm::PointerType::get(context, 0);
        llvm::Type *int_type = llvm::Type::getInt32Ty(context);
        llvm::FunctionType *sscanf_type = llvm::FunctionType::get(
            int_type, {char_ptr_type, char_ptr_type}, true);

        llvm::Function *sscanf_func = get_or_create_libc_function("sscanf", sscanf_type);

        if (!args[0]->getType()->isPointerTy() || !args[1]->getType()->isPointerTy())
        {
            report_error("sscanf str and format arguments must be pointers");
            return nullptr;
        }

        return builder.CreateCall(sscanf_func, args, "sscanf.result");
    }

    // ========================================
    // Low-Level File Descriptor I/O
    // ========================================

    llvm::Value *Intrinsics::generate_read(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 3)
        {
            report_error("intrinsics::read requires exactly 3 arguments (fd, buffer, count), but got " +
                         std::to_string(args.size()) + ". Did you mean to call a method instead?");
            return nullptr;
        }

        if (!validate_args("intrinsics::read", args))
            return nullptr;

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        // Create read function type: int read(int fd, void* buffer, int count)
        // Using i32 for count to match common user declarations
        llvm::Type *int_type = llvm::Type::getInt32Ty(context);
        llvm::Type *void_ptr_type = llvm::PointerType::get(context, 0);
        llvm::FunctionType *read_type = llvm::FunctionType::get(
            int_type, {int_type, void_ptr_type, int_type}, false);

        llvm::Function *read_func = get_or_create_libc_function("read", read_type);

        std::vector<llvm::Value *> call_args = {
            ensure_type(args[0], int_type, "read.fd"),
            args[1], // buffer
            ensure_type(args[2], int_type, "read.count")};

        return builder.CreateCall(read_func, call_args, "read.result");
    }

    llvm::Value *Intrinsics::generate_write(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 3)
        {
            report_error("intrinsics::write requires exactly 3 arguments (fd, buffer, count), but got " +
                         std::to_string(args.size()) + ". Did you mean to call a method instead?");
            return nullptr;
        }

        if (!validate_args("intrinsics::write", args))
            return nullptr;

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
            ensure_type(args[2], size_t_type, "write.count")};

        return builder.CreateCall(write_func, call_args, "write.result");
    }

    llvm::Value *Intrinsics::generate_pread(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 4)
        {
            report_error("pread requires exactly 4 arguments (fd, buffer, count, offset)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        // Create pread function type: ssize_t pread(int fd, void* buffer, size_t count, off_t offset)
        llvm::Type *int_type = llvm::Type::getInt32Ty(context);
        llvm::Type *void_ptr_type = llvm::PointerType::get(context, 0);
        llvm::Type *size_t_type = llvm::Type::getInt64Ty(context);
        llvm::Type *ssize_t_type = llvm::Type::getInt64Ty(context);
        llvm::Type *off_t_type = llvm::Type::getInt64Ty(context);
        llvm::FunctionType *pread_type = llvm::FunctionType::get(
            ssize_t_type, {int_type, void_ptr_type, size_t_type, off_t_type}, false);

        llvm::Function *pread_func = get_or_create_libc_function("pread", pread_type);

        std::vector<llvm::Value *> call_args = {
            ensure_type(args[0], int_type, "pread.fd"),
            args[1], // buffer
            ensure_type(args[2], size_t_type, "pread.count"),
            ensure_type(args[3], off_t_type, "pread.offset")};

        return builder.CreateCall(pread_func, call_args, "pread.result");
    }

    llvm::Value *Intrinsics::generate_pwrite(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 4)
        {
            report_error("pwrite requires exactly 4 arguments (fd, buffer, count, offset)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        // Create pwrite function type: ssize_t pwrite(int fd, const void* buffer, size_t count, off_t offset)
        llvm::Type *int_type = llvm::Type::getInt32Ty(context);
        llvm::Type *void_ptr_type = llvm::PointerType::get(context, 0);
        llvm::Type *size_t_type = llvm::Type::getInt64Ty(context);
        llvm::Type *ssize_t_type = llvm::Type::getInt64Ty(context);
        llvm::Type *off_t_type = llvm::Type::getInt64Ty(context);
        llvm::FunctionType *pwrite_type = llvm::FunctionType::get(
            ssize_t_type, {int_type, void_ptr_type, size_t_type, off_t_type}, false);

        llvm::Function *pwrite_func = get_or_create_libc_function("pwrite", pwrite_type);

        std::vector<llvm::Value *> call_args = {
            ensure_type(args[0], int_type, "pwrite.fd"),
            args[1], // buffer
            ensure_type(args[2], size_t_type, "pwrite.count"),
            ensure_type(args[3], off_t_type, "pwrite.offset")};

        return builder.CreateCall(pwrite_func, call_args, "pwrite.result");
    }

    llvm::Value *Intrinsics::generate_open(const std::vector<llvm::Value *> &args)
    {
        if (args.size() < 2 || args.size() > 3)
        {
            report_error("open requires 2 or 3 arguments (path, flags[, mode])");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        // Create open function type: int open(const char* path, int flags[, mode_t mode])
        llvm::Type *char_ptr_type = llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
        llvm::Type *int_type = llvm::Type::getInt32Ty(context);

        // Handle path argument - may be an array that needs pointer decay
        llvm::Value *path_arg = args[0];
        if (path_arg->getType()->isArrayTy())
        {
            llvm::Type *array_type = path_arg->getType();
            llvm::AllocaInst *alloca = builder.CreateAlloca(array_type, nullptr, "open.path.arr");
            builder.CreateStore(path_arg, alloca);
            path_arg = builder.CreateInBoundsGEP(array_type, alloca,
                {llvm::ConstantInt::get(int_type, 0),
                 llvm::ConstantInt::get(int_type, 0)}, "open.path.ptr");
        }

        std::vector<llvm::Value *> call_args;
        call_args.push_back(path_arg);
        call_args.push_back(ensure_type(args[1], int_type, "open.flags"));

        llvm::FunctionType *open_type;
        if (args.size() == 3)
        {
            open_type = llvm::FunctionType::get(int_type, {char_ptr_type, int_type, int_type}, false);
            call_args.push_back(ensure_type(args[2], int_type, "open.mode"));
        }
        else
        {
            // 2-argument version - mode defaults to 0
            open_type = llvm::FunctionType::get(int_type, {char_ptr_type, int_type}, false);
        }

        llvm::Function *open_func = get_or_create_libc_function("open", open_type);
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
            ensure_type(args[2], int_type, "lseek.whence")};

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
            ensure_type(args[1], int_type, "dup2.newfd")};

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
            ensure_type(args[2], int_type, "fcntl.arg")};

        return builder.CreateCall(fcntl_func, call_args, "fcntl.result");
    }

    llvm::Value *Intrinsics::generate_fsync(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 1)
        {
            report_error("fsync requires exactly 1 argument (fd)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        // Create fsync function type: int fsync(int fd)
        llvm::Type *int_type = llvm::Type::getInt32Ty(context);
        llvm::FunctionType *fsync_type = llvm::FunctionType::get(
            int_type, {int_type}, false);

        llvm::Function *fsync_func = get_or_create_libc_function("fsync", fsync_type);

        std::vector<llvm::Value *> call_args = {
            ensure_type(args[0], int_type, "fsync.fd")};

        return builder.CreateCall(fsync_func, call_args, "fsync.result");
    }

    llvm::Value *Intrinsics::generate_fdatasync(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 1)
        {
            report_error("fdatasync requires exactly 1 argument (fd)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        // Create fdatasync function type: int fdatasync(int fd)
        llvm::Type *int_type = llvm::Type::getInt32Ty(context);
        llvm::FunctionType *fdatasync_type = llvm::FunctionType::get(
            int_type, {int_type}, false);

        llvm::Function *fdatasync_func = get_or_create_libc_function("fdatasync", fdatasync_type);

        std::vector<llvm::Value *> call_args = {
            ensure_type(args[0], int_type, "fdatasync.fd")};

        return builder.CreateCall(fdatasync_func, call_args, "fdatasync.result");
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

    llvm::Value *Intrinsics::generate_link(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 2)
        {
            report_error("link requires exactly 2 arguments (oldpath, newpath)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        llvm::Type *char_ptr_type = llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
        llvm::Type *int_type = llvm::Type::getInt32Ty(context);
        llvm::FunctionType *link_type = llvm::FunctionType::get(int_type, {char_ptr_type, char_ptr_type}, false);

        llvm::Function *link_func = get_or_create_libc_function("link", link_type);
        return builder.CreateCall(link_func, {args[0], args[1]}, "link.result");
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

    llvm::Value *Intrinsics::generate_realpath(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 2)
        {
            report_error("realpath requires exactly 2 arguments (path, resolved)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        llvm::Type *char_ptr_type = llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
        llvm::FunctionType *realpath_type = llvm::FunctionType::get(char_ptr_type, {char_ptr_type, char_ptr_type}, false);

        llvm::Function *realpath_func = get_or_create_libc_function("realpath", realpath_type);
        return builder.CreateCall(realpath_func, {args[0], args[1]}, "realpath.result");
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
        return builder.CreateCall(waitpid_func, {ensure_type(args[0], int_type, "waitpid.pid"), args[1], ensure_type(args[2], int_type, "waitpid.options")}, "waitpid.result");
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
        return builder.CreateCall(kill_func, {ensure_type(args[0], int_type, "kill.pid"), ensure_type(args[1], int_type, "kill.sig")}, "kill.result");
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
        return builder.CreateCall(signal_func, {ensure_type(args[0], int_type, "signal.signum"), args[1]}, "signal.result");
    }

    // ========================================
    // Error Handling Intrinsics
    // ========================================

    llvm::Value *Intrinsics::generate_errno(const std::vector<llvm::Value *> &args)
    {
        if (!args.empty())
        {
            report_error("errno requires no arguments");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();
        auto *module = _context_manager.get_module();

        llvm::Type *int_type = llvm::Type::getInt32Ty(context);
        llvm::Type *int_ptr_type = llvm::PointerType::get(int_type, 0);

        // errno is accessed differently on Windows vs POSIX:
        // - Windows: _errno() returns int*
        // - POSIX (Linux/macOS): __errno_location() returns int*
        // We need to call the appropriate function and load the value.

#if defined(_WIN32) || defined(_WIN64)
        const char *errno_func_name = "_errno";
#else
        const char *errno_func_name = "__errno_location";
#endif

        // Function type: int* errno_func(void)
        llvm::FunctionType *errno_func_type = llvm::FunctionType::get(int_ptr_type, false);

        // Get or create the errno function
        llvm::Function *errno_func = module->getFunction(errno_func_name);
        if (!errno_func)
        {
            errno_func = llvm::Function::Create(
                errno_func_type,
                llvm::Function::ExternalLinkage,
                errno_func_name,
                module);
        }

        // Call the errno function to get the pointer to errno
        llvm::Value *errno_ptr = builder.CreateCall(errno_func, {}, "errno.ptr");

        // Load the errno value
        return builder.CreateLoad(int_type, errno_ptr, "errno.value");
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

#define DEFINE_MATH_FUNC_DOUBLE(name)                                                               \
    llvm::Value *Intrinsics::generate_##name(const std::vector<llvm::Value *> &args)                \
    {                                                                                               \
        if (args.size() != 1)                                                                       \
        {                                                                                           \
            report_error(#name " requires exactly 1 argument");                                     \
            return nullptr;                                                                         \
        }                                                                                           \
                                                                                                    \
        auto &builder = _context_manager.get_builder();                                             \
        auto &context = _context_manager.get_context();                                             \
                                                                                                    \
        llvm::Type *double_type = llvm::Type::getDoubleTy(context);                                 \
        llvm::FunctionType *func_type = llvm::FunctionType::get(double_type, {double_type}, false); \
                                                                                                    \
        llvm::Function *func = get_or_create_libc_function(#name, func_type);                       \
        llvm::Value *x = ensure_type(args[0], double_type, #name ".x");                             \
        return builder.CreateCall(func, {x}, #name ".result");                                      \
    }

#define DEFINE_MATH_FUNC_DOUBLE_2ARG(name)                                                                       \
    llvm::Value *Intrinsics::generate_##name(const std::vector<llvm::Value *> &args)                             \
    {                                                                                                            \
        if (args.size() != 2)                                                                                    \
        {                                                                                                        \
            report_error(#name " requires exactly 2 arguments");                                                 \
            return nullptr;                                                                                      \
        }                                                                                                        \
                                                                                                                 \
        auto &builder = _context_manager.get_builder();                                                          \
        auto &context = _context_manager.get_context();                                                          \
                                                                                                                 \
        llvm::Type *double_type = llvm::Type::getDoubleTy(context);                                              \
        llvm::FunctionType *func_type = llvm::FunctionType::get(double_type, {double_type, double_type}, false); \
                                                                                                                 \
        llvm::Function *func = get_or_create_libc_function(#name, func_type);                                    \
        llvm::Value *x = ensure_type(args[0], double_type, #name ".x");                                          \
        llvm::Value *y = ensure_type(args[1], double_type, #name ".y");                                          \
        return builder.CreateCall(func, {x, y}, #name ".result");                                                \
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

#define DEFINE_MATH_FUNC_FLOAT(name, fname)                                                       \
    llvm::Value *Intrinsics::generate_##name(const std::vector<llvm::Value *> &args)              \
    {                                                                                             \
        if (args.size() != 1)                                                                     \
        {                                                                                         \
            report_error(#name " requires exactly 1 argument");                                   \
            return nullptr;                                                                       \
        }                                                                                         \
                                                                                                  \
        auto &builder = _context_manager.get_builder();                                           \
        auto &context = _context_manager.get_context();                                           \
                                                                                                  \
        llvm::Type *float_type = llvm::Type::getFloatTy(context);                                 \
        llvm::FunctionType *func_type = llvm::FunctionType::get(float_type, {float_type}, false); \
                                                                                                  \
        llvm::Function *func = get_or_create_libc_function(fname, func_type);                     \
        llvm::Value *x = ensure_type(args[0], float_type, #name ".x");                            \
        return builder.CreateCall(func, {x}, #name ".result");                                    \
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
    // Network Intrinsics (Full implementations using libc functions)
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
        return builder.CreateCall(socket_func, {ensure_type(args[0], int_type, "socket.domain"), ensure_type(args[1], int_type, "socket.type"), ensure_type(args[2], int_type, "socket.protocol")}, "socket.result");
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
        return builder.CreateCall(bind_func, {ensure_type(args[0], int_type, "bind.sockfd"),
                                              args[1], // addr
                                              ensure_type(args[2], uint32_type, "bind.addrlen")},
                                  "bind.result");
    }

    // Additional network function implementations

    llvm::Value *Intrinsics::generate_sendto(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 6)
        {
            report_error("sendto requires exactly 6 arguments (sockfd, buf, len, flags, dest_addr, addrlen)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        llvm::Type *int_type = llvm::Type::getInt32Ty(context);
        llvm::Type *void_ptr_type = llvm::PointerType::get(context, 0);
        llvm::FunctionType *sendto_type = llvm::FunctionType::get(
            int_type, {int_type, void_ptr_type, int_type, int_type, void_ptr_type, int_type}, false);

        llvm::Function *sendto_func = get_or_create_libc_function("sendto", sendto_type);
        return builder.CreateCall(sendto_func, {
            ensure_type(args[0], int_type, "sendto.sockfd"),
            args[1], // buf
            ensure_type(args[2], int_type, "sendto.len"),
            ensure_type(args[3], int_type, "sendto.flags"),
            args[4], // dest_addr
            ensure_type(args[5], int_type, "sendto.addrlen")
        }, "sendto.result");
    }

    llvm::Value *Intrinsics::generate_recvfrom(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 6)
        {
            report_error("recvfrom requires exactly 6 arguments (sockfd, buf, len, flags, src_addr, addrlen)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        llvm::Type *int_type = llvm::Type::getInt32Ty(context);
        llvm::Type *void_ptr_type = llvm::PointerType::get(context, 0);
        llvm::FunctionType *recvfrom_type = llvm::FunctionType::get(
            int_type, {int_type, void_ptr_type, int_type, int_type, void_ptr_type, void_ptr_type}, false);

        llvm::Function *recvfrom_func = get_or_create_libc_function("recvfrom", recvfrom_type);
        return builder.CreateCall(recvfrom_func, {
            ensure_type(args[0], int_type, "recvfrom.sockfd"),
            args[1], // buf
            ensure_type(args[2], int_type, "recvfrom.len"),
            ensure_type(args[3], int_type, "recvfrom.flags"),
            args[4], // src_addr
            args[5]  // addrlen (pointer to socklen_t)
        }, "recvfrom.result");
    }

    llvm::Value *Intrinsics::generate_shutdown(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 2)
        {
            report_error("shutdown requires exactly 2 arguments (sockfd, how)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        llvm::Type *int_type = llvm::Type::getInt32Ty(context);
        llvm::FunctionType *shutdown_type = llvm::FunctionType::get(int_type, {int_type, int_type}, false);

        llvm::Function *shutdown_func = get_or_create_libc_function("shutdown", shutdown_type);
        return builder.CreateCall(shutdown_func, {
            ensure_type(args[0], int_type, "shutdown.sockfd"),
            ensure_type(args[1], int_type, "shutdown.how")
        }, "shutdown.result");
    }

    llvm::Value *Intrinsics::generate_getsockopt(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 5)
        {
            report_error("getsockopt requires exactly 5 arguments (sockfd, level, optname, optval, optlen)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        llvm::Type *int_type = llvm::Type::getInt32Ty(context);
        llvm::Type *void_ptr_type = llvm::PointerType::get(context, 0);
        llvm::FunctionType *getsockopt_type = llvm::FunctionType::get(
            int_type, {int_type, int_type, int_type, void_ptr_type, void_ptr_type}, false);

        llvm::Function *getsockopt_func = get_or_create_libc_function("getsockopt", getsockopt_type);
        return builder.CreateCall(getsockopt_func, {
            ensure_type(args[0], int_type, "getsockopt.sockfd"),
            ensure_type(args[1], int_type, "getsockopt.level"),
            ensure_type(args[2], int_type, "getsockopt.optname"),
            args[3], // optval
            args[4]  // optlen
        }, "getsockopt.result");
    }

    llvm::Value *Intrinsics::generate_getsockname(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 3)
        {
            report_error("getsockname requires exactly 3 arguments (sockfd, addr, addrlen)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        llvm::Type *int_type = llvm::Type::getInt32Ty(context);
        llvm::Type *void_ptr_type = llvm::PointerType::get(context, 0);
        llvm::FunctionType *getsockname_type = llvm::FunctionType::get(
            int_type, {int_type, void_ptr_type, void_ptr_type}, false);

        llvm::Function *getsockname_func = get_or_create_libc_function("getsockname", getsockname_type);
        return builder.CreateCall(getsockname_func, {
            ensure_type(args[0], int_type, "getsockname.sockfd"),
            args[1], // addr
            args[2]  // addrlen
        }, "getsockname.result");
    }

    llvm::Value *Intrinsics::generate_getpeername(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 3)
        {
            report_error("getpeername requires exactly 3 arguments (sockfd, addr, addrlen)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        llvm::Type *int_type = llvm::Type::getInt32Ty(context);
        llvm::Type *void_ptr_type = llvm::PointerType::get(context, 0);
        llvm::FunctionType *getpeername_type = llvm::FunctionType::get(
            int_type, {int_type, void_ptr_type, void_ptr_type}, false);

        llvm::Function *getpeername_func = get_or_create_libc_function("getpeername", getpeername_type);
        return builder.CreateCall(getpeername_func, {
            ensure_type(args[0], int_type, "getpeername.sockfd"),
            args[1], // addr
            args[2]  // addrlen
        }, "getpeername.result");
    }

    llvm::Value *Intrinsics::generate_poll(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 3)
        {
            report_error("poll requires exactly 3 arguments (fds, nfds, timeout)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        llvm::Type *int_type = llvm::Type::getInt32Ty(context);
        llvm::Type *i64_type = llvm::Type::getInt64Ty(context);
        llvm::Type *void_ptr_type = llvm::PointerType::get(context, 0);
        // poll(struct pollfd *fds, nfds_t nfds, int timeout)
        llvm::FunctionType *poll_type = llvm::FunctionType::get(
            int_type, {void_ptr_type, i64_type, int_type}, false);

        llvm::Function *poll_func = get_or_create_libc_function("poll", poll_type);
        return builder.CreateCall(poll_func, {
            args[0], // fds
            ensure_type(args[1], i64_type, "poll.nfds"),
            ensure_type(args[2], int_type, "poll.timeout")
        }, "poll.result");
    }

    llvm::Value *Intrinsics::generate_setsockopt(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 5)
        {
            report_error("setsockopt requires exactly 5 arguments (sockfd, level, optname, optval, optlen)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        // int setsockopt(int sockfd, int level, int optname, const void *optval, socklen_t optlen)
        llvm::Type *int_type = llvm::Type::getInt32Ty(context);
        llvm::Type *void_ptr_type = llvm::PointerType::get(context, 0);
        llvm::FunctionType *setsockopt_type = llvm::FunctionType::get(
            int_type, {int_type, int_type, int_type, void_ptr_type, int_type}, false);

        llvm::Function *setsockopt_func = get_or_create_libc_function("setsockopt", setsockopt_type);

        std::vector<llvm::Value *> call_args = {
            ensure_type(args[0], int_type, "setsockopt.sockfd"),
            ensure_type(args[1], int_type, "setsockopt.level"),
            ensure_type(args[2], int_type, "setsockopt.optname"),
            args[3], // optval (pointer)
            ensure_type(args[4], int_type, "setsockopt.optlen")};

        return builder.CreateCall(setsockopt_func, call_args, "setsockopt.result");
    }

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
    // Windows Socket Initialization Intrinsics
    // ========================================

    llvm::Value *Intrinsics::generate_WSAStartup(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 2)
        {
            report_error("WSAStartup requires exactly 2 arguments (wVersionRequired, lpWSAData)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();
        auto *module = _context_manager.get_module();

        llvm::Type *int_type = llvm::Type::getInt32Ty(context);
        llvm::Type *uint16_type = llvm::Type::getInt16Ty(context);
        llvm::Type *ptr_type = llvm::PointerType::get(context, 0);

        // WSAStartup signature: int WSAStartup(WORD wVersionRequired, LPWSADATA lpWSAData)
        llvm::FunctionType *wsa_startup_type = llvm::FunctionType::get(int_type, {uint16_type, ptr_type}, false);

        llvm::Function *wsa_startup_func = module->getFunction("WSAStartup");
        if (!wsa_startup_func)
        {
            wsa_startup_func = llvm::Function::Create(
                wsa_startup_type,
                llvm::Function::ExternalLinkage,
                "WSAStartup",
                module);
        }

        llvm::Value *version_arg = ensure_type(args[0], uint16_type, "WSAStartup.version");
        llvm::Value *data_arg = args[1];

        return builder.CreateCall(wsa_startup_func, {version_arg, data_arg}, "WSAStartup.result");
    }

    llvm::Value *Intrinsics::generate_WSACleanup(const std::vector<llvm::Value *> &args)
    {
        if (!args.empty())
        {
            report_error("WSACleanup requires no arguments");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();
        auto *module = _context_manager.get_module();

        llvm::Type *int_type = llvm::Type::getInt32Ty(context);

        // WSACleanup signature: int WSACleanup(void)
        llvm::FunctionType *wsa_cleanup_type = llvm::FunctionType::get(int_type, false);

        llvm::Function *wsa_cleanup_func = module->getFunction("WSACleanup");
        if (!wsa_cleanup_func)
        {
            wsa_cleanup_func = llvm::Function::Create(
                wsa_cleanup_type,
                llvm::Function::ExternalLinkage,
                "WSACleanup",
                module);
        }

        return builder.CreateCall(wsa_cleanup_func, {}, "WSACleanup.result");
    }

    llvm::Value *Intrinsics::generate_WSAGetLastError(const std::vector<llvm::Value *> &args)
    {
        if (!args.empty())
        {
            report_error("WSAGetLastError requires no arguments");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();
        auto *module = _context_manager.get_module();

        llvm::Type *int_type = llvm::Type::getInt32Ty(context);

        // WSAGetLastError signature: int WSAGetLastError(void)
        llvm::FunctionType *wsa_get_last_error_type = llvm::FunctionType::get(int_type, false);

        llvm::Function *wsa_get_last_error_func = module->getFunction("WSAGetLastError");
        if (!wsa_get_last_error_func)
        {
            wsa_get_last_error_func = llvm::Function::Create(
                wsa_get_last_error_type,
                llvm::Function::ExternalLinkage,
                "WSAGetLastError",
                module);
        }

        return builder.CreateCall(wsa_get_last_error_func, {}, "WSAGetLastError.result");
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
        return builder.CreateCall(clock_gettime_func, {ensure_type(args[0], int_type, "clock_gettime.clk_id"), args[1]}, "clock_gettime.result");
    }

    llvm::Value *Intrinsics::generate_nanosleep(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 2)
        {
            report_error("nanosleep requires exactly 2 arguments (seconds, nanoseconds)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        // The C nanosleep() takes struct timespec* arguments:
        //   struct timespec { time_t tv_sec; long tv_nsec; };
        // We accept integer args (seconds, nanoseconds) and build the struct on the stack.
        llvm::Type *i64_type = llvm::Type::getInt64Ty(context);
        llvm::Type *void_ptr_type = llvm::PointerType::get(context, 0);
        llvm::Type *int_type = llvm::Type::getInt32Ty(context);

        // Create the timespec struct type: { i64, i64 }
        llvm::StructType *timespec_type = llvm::StructType::get(context, {i64_type, i64_type});

        // Allocate timespec on the stack for the request
        llvm::Value *req = builder.CreateAlloca(timespec_type, nullptr, "nanosleep.req");

        // Store seconds and nanoseconds into the struct
        llvm::Value *secs_val = ensure_type(args[0], i64_type, "nanosleep.secs");
        llvm::Value *nsecs_val = ensure_type(args[1], i64_type, "nanosleep.nsecs");

        llvm::Value *sec_ptr = builder.CreateStructGEP(timespec_type, req, 0, "nanosleep.req.sec");
        builder.CreateStore(secs_val, sec_ptr);

        llvm::Value *nsec_ptr = builder.CreateStructGEP(timespec_type, req, 1, "nanosleep.req.nsec");
        builder.CreateStore(nsecs_val, nsec_ptr);

        // Pass null for the remainder pointer (we don't need to know remaining time)
        llvm::Value *rem_null = llvm::ConstantPointerNull::get(llvm::PointerType::get(context, 0));

        // Call the real nanosleep(struct timespec*, struct timespec*)
        llvm::FunctionType *nanosleep_type = llvm::FunctionType::get(int_type, {void_ptr_type, void_ptr_type}, false);
        llvm::Function *nanosleep_func = get_or_create_libc_function("nanosleep", nanosleep_type);
        return builder.CreateCall(nanosleep_func, {req, rem_null}, "nanosleep.result");
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

    // ========================================
    // Threading Intrinsics (pthread)
    // ========================================

    llvm::Value *Intrinsics::generate_pthread_create(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 4)
        {
            report_error("pthread_create requires exactly 4 arguments (thread, attr, start_routine, arg)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        // pthread_create(pthread_t *thread, const pthread_attr_t *attr,
        //                void *(*start_routine)(void*), void *arg)
        llvm::Type *void_ptr_type = llvm::PointerType::get(context, 0);
        llvm::Type *int_type = llvm::Type::getInt32Ty(context);
        llvm::FunctionType *pthread_create_type = llvm::FunctionType::get(
            int_type, {void_ptr_type, void_ptr_type, void_ptr_type, void_ptr_type}, false);

        llvm::Function *pthread_create_func = get_or_create_libc_function("pthread_create", pthread_create_type);
        return builder.CreateCall(pthread_create_func, {args[0], args[1], args[2], args[3]}, "pthread_create.result");
    }

    llvm::Value *Intrinsics::generate_pthread_join(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 2)
        {
            report_error("pthread_join requires exactly 2 arguments (thread, retval)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        // pthread_join(pthread_t thread, void **retval)
        llvm::Type *uint64_type = llvm::Type::getInt64Ty(context); // pthread_t
        llvm::Type *void_ptr_ptr_type = llvm::PointerType::get(llvm::PointerType::get(context, 0), 0);
        llvm::Type *int_type = llvm::Type::getInt32Ty(context);
        llvm::FunctionType *pthread_join_type = llvm::FunctionType::get(
            int_type, {uint64_type, void_ptr_ptr_type}, false);

        llvm::Function *pthread_join_func = get_or_create_libc_function("pthread_join", pthread_join_type);
        llvm::Value *thread = ensure_type(args[0], uint64_type, "pthread_join.thread");
        return builder.CreateCall(pthread_join_func, {thread, args[1]}, "pthread_join.result");
    }

    llvm::Value *Intrinsics::generate_pthread_exit(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 1)
        {
            report_error("pthread_exit requires exactly 1 argument (retval)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        // pthread_exit(void *retval) - never returns
        llvm::Type *void_ptr_type = llvm::PointerType::get(context, 0);
        llvm::Type *void_type = llvm::Type::getVoidTy(context);
        llvm::FunctionType *pthread_exit_type = llvm::FunctionType::get(void_type, {void_ptr_type}, false);

        llvm::Function *pthread_exit_func = get_or_create_libc_function("pthread_exit", pthread_exit_type);
        llvm::Value *call_result = builder.CreateCall(pthread_exit_func, {args[0]});

        // pthread_exit never returns
        builder.CreateUnreachable();
        return call_result;
    }

    llvm::Value *Intrinsics::generate_pthread_detach(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 1)
        {
            report_error("pthread_detach requires exactly 1 argument (thread)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        llvm::Type *uint64_type = llvm::Type::getInt64Ty(context);
        llvm::Type *int_type = llvm::Type::getInt32Ty(context);
        llvm::FunctionType *pthread_detach_type = llvm::FunctionType::get(int_type, {uint64_type}, false);

        llvm::Function *pthread_detach_func = get_or_create_libc_function("pthread_detach", pthread_detach_type);
        llvm::Value *thread = ensure_type(args[0], uint64_type, "pthread_detach.thread");
        return builder.CreateCall(pthread_detach_func, {thread}, "pthread_detach.result");
    }

    llvm::Value *Intrinsics::generate_pthread_self(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 0)
        {
            report_error("pthread_self requires exactly 0 arguments");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        llvm::Type *uint64_type = llvm::Type::getInt64Ty(context);
        llvm::FunctionType *pthread_self_type = llvm::FunctionType::get(uint64_type, {}, false);

        llvm::Function *pthread_self_func = get_or_create_libc_function("pthread_self", pthread_self_type);
        return builder.CreateCall(pthread_self_func, {}, "pthread_self.result");
    }

    llvm::Value *Intrinsics::generate_pthread_equal(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 2)
        {
            report_error("pthread_equal requires exactly 2 arguments (t1, t2)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        llvm::Type *uint64_type = llvm::Type::getInt64Ty(context);
        llvm::Type *int_type = llvm::Type::getInt32Ty(context);
        llvm::FunctionType *pthread_equal_type = llvm::FunctionType::get(int_type, {uint64_type, uint64_type}, false);

        llvm::Function *pthread_equal_func = get_or_create_libc_function("pthread_equal", pthread_equal_type);
        llvm::Value *t1 = ensure_type(args[0], uint64_type, "pthread_equal.t1");
        llvm::Value *t2 = ensure_type(args[1], uint64_type, "pthread_equal.t2");
        return builder.CreateCall(pthread_equal_func, {t1, t2}, "pthread_equal.result");
    }

    llvm::Value *Intrinsics::generate_sched_yield(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 0)
        {
            report_error("sched_yield requires exactly 0 arguments");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        llvm::Type *int_type = llvm::Type::getInt32Ty(context);
        llvm::FunctionType *sched_yield_type = llvm::FunctionType::get(int_type, {}, false);

        llvm::Function *sched_yield_func = get_or_create_libc_function("sched_yield", sched_yield_type);
        return builder.CreateCall(sched_yield_func, {}, "sched_yield.result");
    }

    // ========================================
    // Mutex Intrinsics
    // ========================================

    llvm::Value *Intrinsics::generate_pthread_mutex_init(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 2)
        {
            report_error("pthread_mutex_init requires exactly 2 arguments (mutex, attr)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        llvm::Type *void_ptr_type = llvm::PointerType::get(context, 0);
        llvm::Type *int_type = llvm::Type::getInt32Ty(context);
        llvm::FunctionType *pthread_mutex_init_type = llvm::FunctionType::get(int_type, {void_ptr_type, void_ptr_type}, false);

        llvm::Function *pthread_mutex_init_func = get_or_create_libc_function("pthread_mutex_init", pthread_mutex_init_type);
        return builder.CreateCall(pthread_mutex_init_func, {args[0], args[1]}, "pthread_mutex_init.result");
    }

    llvm::Value *Intrinsics::generate_pthread_mutex_destroy(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 1)
        {
            report_error("pthread_mutex_destroy requires exactly 1 argument (mutex)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        llvm::Type *void_ptr_type = llvm::PointerType::get(context, 0);
        llvm::Type *int_type = llvm::Type::getInt32Ty(context);
        llvm::FunctionType *pthread_mutex_destroy_type = llvm::FunctionType::get(int_type, {void_ptr_type}, false);

        llvm::Function *pthread_mutex_destroy_func = get_or_create_libc_function("pthread_mutex_destroy", pthread_mutex_destroy_type);
        return builder.CreateCall(pthread_mutex_destroy_func, {args[0]}, "pthread_mutex_destroy.result");
    }

    llvm::Value *Intrinsics::generate_pthread_mutex_lock(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 1)
        {
            report_error("pthread_mutex_lock requires exactly 1 argument (mutex)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        llvm::Type *void_ptr_type = llvm::PointerType::get(context, 0);
        llvm::Type *int_type = llvm::Type::getInt32Ty(context);
        llvm::FunctionType *pthread_mutex_lock_type = llvm::FunctionType::get(int_type, {void_ptr_type}, false);

        llvm::Function *pthread_mutex_lock_func = get_or_create_libc_function("pthread_mutex_lock", pthread_mutex_lock_type);
        return builder.CreateCall(pthread_mutex_lock_func, {args[0]}, "pthread_mutex_lock.result");
    }

    llvm::Value *Intrinsics::generate_pthread_mutex_trylock(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 1)
        {
            report_error("pthread_mutex_trylock requires exactly 1 argument (mutex)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        llvm::Type *void_ptr_type = llvm::PointerType::get(context, 0);
        llvm::Type *int_type = llvm::Type::getInt32Ty(context);
        llvm::FunctionType *pthread_mutex_trylock_type = llvm::FunctionType::get(int_type, {void_ptr_type}, false);

        llvm::Function *pthread_mutex_trylock_func = get_or_create_libc_function("pthread_mutex_trylock", pthread_mutex_trylock_type);
        return builder.CreateCall(pthread_mutex_trylock_func, {args[0]}, "pthread_mutex_trylock.result");
    }

    llvm::Value *Intrinsics::generate_pthread_mutex_unlock(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 1)
        {
            report_error("pthread_mutex_unlock requires exactly 1 argument (mutex)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        llvm::Type *void_ptr_type = llvm::PointerType::get(context, 0);
        llvm::Type *int_type = llvm::Type::getInt32Ty(context);
        llvm::FunctionType *pthread_mutex_unlock_type = llvm::FunctionType::get(int_type, {void_ptr_type}, false);

        llvm::Function *pthread_mutex_unlock_func = get_or_create_libc_function("pthread_mutex_unlock", pthread_mutex_unlock_type);
        return builder.CreateCall(pthread_mutex_unlock_func, {args[0]}, "pthread_mutex_unlock.result");
    }

    // ========================================
    // Condition Variable Intrinsics
    // ========================================

    llvm::Value *Intrinsics::generate_pthread_cond_init(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 2)
        {
            report_error("pthread_cond_init requires exactly 2 arguments (cond, attr)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        llvm::Type *void_ptr_type = llvm::PointerType::get(context, 0);
        llvm::Type *int_type = llvm::Type::getInt32Ty(context);
        llvm::FunctionType *pthread_cond_init_type = llvm::FunctionType::get(int_type, {void_ptr_type, void_ptr_type}, false);

        llvm::Function *pthread_cond_init_func = get_or_create_libc_function("pthread_cond_init", pthread_cond_init_type);
        return builder.CreateCall(pthread_cond_init_func, {args[0], args[1]}, "pthread_cond_init.result");
    }

    llvm::Value *Intrinsics::generate_pthread_cond_destroy(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 1)
        {
            report_error("pthread_cond_destroy requires exactly 1 argument (cond)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        llvm::Type *void_ptr_type = llvm::PointerType::get(context, 0);
        llvm::Type *int_type = llvm::Type::getInt32Ty(context);
        llvm::FunctionType *pthread_cond_destroy_type = llvm::FunctionType::get(int_type, {void_ptr_type}, false);

        llvm::Function *pthread_cond_destroy_func = get_or_create_libc_function("pthread_cond_destroy", pthread_cond_destroy_type);
        return builder.CreateCall(pthread_cond_destroy_func, {args[0]}, "pthread_cond_destroy.result");
    }

    llvm::Value *Intrinsics::generate_pthread_cond_wait(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 2)
        {
            report_error("pthread_cond_wait requires exactly 2 arguments (cond, mutex)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        llvm::Type *void_ptr_type = llvm::PointerType::get(context, 0);
        llvm::Type *int_type = llvm::Type::getInt32Ty(context);
        llvm::FunctionType *pthread_cond_wait_type = llvm::FunctionType::get(int_type, {void_ptr_type, void_ptr_type}, false);

        llvm::Function *pthread_cond_wait_func = get_or_create_libc_function("pthread_cond_wait", pthread_cond_wait_type);
        return builder.CreateCall(pthread_cond_wait_func, {args[0], args[1]}, "pthread_cond_wait.result");
    }

    llvm::Value *Intrinsics::generate_pthread_cond_timedwait(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 3)
        {
            report_error("pthread_cond_timedwait requires exactly 3 arguments (cond, mutex, abstime)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        llvm::Type *void_ptr_type = llvm::PointerType::get(context, 0);
        llvm::Type *int_type = llvm::Type::getInt32Ty(context);
        llvm::FunctionType *pthread_cond_timedwait_type = llvm::FunctionType::get(int_type, {void_ptr_type, void_ptr_type, void_ptr_type}, false);

        llvm::Function *pthread_cond_timedwait_func = get_or_create_libc_function("pthread_cond_timedwait", pthread_cond_timedwait_type);
        return builder.CreateCall(pthread_cond_timedwait_func, {args[0], args[1], args[2]}, "pthread_cond_timedwait.result");
    }

    llvm::Value *Intrinsics::generate_pthread_cond_signal(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 1)
        {
            report_error("pthread_cond_signal requires exactly 1 argument (cond)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        llvm::Type *void_ptr_type = llvm::PointerType::get(context, 0);
        llvm::Type *int_type = llvm::Type::getInt32Ty(context);
        llvm::FunctionType *pthread_cond_signal_type = llvm::FunctionType::get(int_type, {void_ptr_type}, false);

        llvm::Function *pthread_cond_signal_func = get_or_create_libc_function("pthread_cond_signal", pthread_cond_signal_type);
        return builder.CreateCall(pthread_cond_signal_func, {args[0]}, "pthread_cond_signal.result");
    }

    llvm::Value *Intrinsics::generate_pthread_cond_broadcast(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 1)
        {
            report_error("pthread_cond_broadcast requires exactly 1 argument (cond)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        llvm::Type *void_ptr_type = llvm::PointerType::get(context, 0);
        llvm::Type *int_type = llvm::Type::getInt32Ty(context);
        llvm::FunctionType *pthread_cond_broadcast_type = llvm::FunctionType::get(int_type, {void_ptr_type}, false);

        llvm::Function *pthread_cond_broadcast_func = get_or_create_libc_function("pthread_cond_broadcast", pthread_cond_broadcast_type);
        return builder.CreateCall(pthread_cond_broadcast_func, {args[0]}, "pthread_cond_broadcast.result");
    }

    // ========================================
    // Read-Write Lock Intrinsics
    // ========================================

#define DEFINE_PTHREAD_RWLOCK_FUNC(name, arg_count)                                                 \
    llvm::Value *Intrinsics::generate_pthread_rwlock_##name(const std::vector<llvm::Value *> &args) \
    {                                                                                               \
        if (args.size() != arg_count)                                                               \
        {                                                                                           \
            report_error("pthread_rwlock_" #name " requires exactly " #arg_count " argument(s)");   \
            return nullptr;                                                                         \
        }                                                                                           \
                                                                                                    \
        auto &builder = _context_manager.get_builder();                                             \
        auto &context = _context_manager.get_context();                                             \
                                                                                                    \
        llvm::Type *void_ptr_type = llvm::PointerType::get(context, 0);                             \
        llvm::Type *int_type = llvm::Type::getInt32Ty(context);                                     \
        std::vector<llvm::Type *> arg_types(arg_count, void_ptr_type);                              \
        llvm::FunctionType *func_type = llvm::FunctionType::get(int_type, arg_types, false);        \
                                                                                                    \
        llvm::Function *func = get_or_create_libc_function("pthread_rwlock_" #name, func_type);     \
        return builder.CreateCall(func, args, "pthread_rwlock_" #name ".result");                   \
    }

    DEFINE_PTHREAD_RWLOCK_FUNC(init, 2)
    DEFINE_PTHREAD_RWLOCK_FUNC(destroy, 1)
    DEFINE_PTHREAD_RWLOCK_FUNC(rdlock, 1)
    DEFINE_PTHREAD_RWLOCK_FUNC(tryrdlock, 1)
    DEFINE_PTHREAD_RWLOCK_FUNC(wrlock, 1)
    DEFINE_PTHREAD_RWLOCK_FUNC(trywrlock, 1)
    DEFINE_PTHREAD_RWLOCK_FUNC(unlock, 1)

    // ========================================
    // Thread-Local Storage Intrinsics
    // ========================================

    llvm::Value *Intrinsics::generate_pthread_key_create(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 2)
        {
            report_error("pthread_key_create requires exactly 2 arguments (key, destructor)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        llvm::Type *void_ptr_type = llvm::PointerType::get(context, 0);
        llvm::Type *int_type = llvm::Type::getInt32Ty(context);
        llvm::FunctionType *pthread_key_create_type = llvm::FunctionType::get(int_type, {void_ptr_type, void_ptr_type}, false);

        llvm::Function *pthread_key_create_func = get_or_create_libc_function("pthread_key_create", pthread_key_create_type);
        return builder.CreateCall(pthread_key_create_func, {args[0], args[1]}, "pthread_key_create.result");
    }

    llvm::Value *Intrinsics::generate_pthread_key_delete(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 1)
        {
            report_error("pthread_key_delete requires exactly 1 argument (key)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        llvm::Type *uint64_type = llvm::Type::getInt64Ty(context);
        llvm::Type *int_type = llvm::Type::getInt32Ty(context);
        llvm::FunctionType *pthread_key_delete_type = llvm::FunctionType::get(int_type, {uint64_type}, false);

        llvm::Function *pthread_key_delete_func = get_or_create_libc_function("pthread_key_delete", pthread_key_delete_type);
        llvm::Value *key = ensure_type(args[0], uint64_type, "pthread_key_delete.key");
        return builder.CreateCall(pthread_key_delete_func, {key}, "pthread_key_delete.result");
    }

    llvm::Value *Intrinsics::generate_pthread_getspecific(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 1)
        {
            report_error("pthread_getspecific requires exactly 1 argument (key)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        llvm::Type *uint64_type = llvm::Type::getInt64Ty(context);
        llvm::Type *void_ptr_type = llvm::PointerType::get(context, 0);
        llvm::FunctionType *pthread_getspecific_type = llvm::FunctionType::get(void_ptr_type, {uint64_type}, false);

        llvm::Function *pthread_getspecific_func = get_or_create_libc_function("pthread_getspecific", pthread_getspecific_type);
        llvm::Value *key = ensure_type(args[0], uint64_type, "pthread_getspecific.key");
        return builder.CreateCall(pthread_getspecific_func, {key}, "pthread_getspecific.result");
    }

    llvm::Value *Intrinsics::generate_pthread_setspecific(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 2)
        {
            report_error("pthread_setspecific requires exactly 2 arguments (key, value)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        llvm::Type *uint64_type = llvm::Type::getInt64Ty(context);
        llvm::Type *void_ptr_type = llvm::PointerType::get(context, 0);
        llvm::Type *int_type = llvm::Type::getInt32Ty(context);
        llvm::FunctionType *pthread_setspecific_type = llvm::FunctionType::get(int_type, {uint64_type, void_ptr_type}, false);

        llvm::Function *pthread_setspecific_func = get_or_create_libc_function("pthread_setspecific", pthread_setspecific_type);
        llvm::Value *key = ensure_type(args[0], uint64_type, "pthread_setspecific.key");
        return builder.CreateCall(pthread_setspecific_func, {key, args[1]}, "pthread_setspecific.result");
    }

    // ========================================
    // Atomic Intrinsics
    // ========================================

    llvm::Value *Intrinsics::generate_atomic_load_8(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 2)
        {
            report_error("atomic_load_8 requires exactly 2 arguments (ptr, order)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        // Convert memory ordering from Cryo to LLVM
        llvm::Value *ptr = args[0];
        llvm::Value *order = args[1];

        if (!ptr->getType()->isPointerTy())
        {
            report_error("atomic_load_8: first argument must be a pointer");
            return nullptr;
        }

        // Create atomic load instruction
        llvm::Type *i8_type = llvm::Type::getInt8Ty(context);
        llvm::LoadInst *load = builder.CreateLoad(i8_type, ptr);
        load->setAtomic(llvm::AtomicOrdering::SequentiallyConsistent);
        load->setAlignment(llvm::Align(1));

        return load;
    }

    llvm::Value *Intrinsics::generate_atomic_load_16(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 2)
        {
            report_error("atomic_load_16 requires exactly 2 arguments (ptr, order)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        llvm::Value *ptr = args[0];
        if (!ptr->getType()->isPointerTy())
        {
            report_error("atomic_load_16: first argument must be a pointer");
            return nullptr;
        }

        llvm::Type *i16_type = llvm::Type::getInt16Ty(context);
        llvm::LoadInst *load = builder.CreateLoad(i16_type, ptr);
        load->setAtomic(llvm::AtomicOrdering::SequentiallyConsistent);
        load->setAlignment(llvm::Align(2));

        return load;
    }

    llvm::Value *Intrinsics::generate_atomic_load_32(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 2)
        {
            report_error("atomic_load_32 requires exactly 2 arguments (ptr, order)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        llvm::Value *ptr = args[0];
        if (!ptr->getType()->isPointerTy())
        {
            report_error("atomic_load_32: first argument must be a pointer");
            return nullptr;
        }

        llvm::Type *i32_type = llvm::Type::getInt32Ty(context);
        llvm::LoadInst *load = builder.CreateLoad(i32_type, ptr);
        load->setAtomic(llvm::AtomicOrdering::SequentiallyConsistent);
        load->setAlignment(llvm::Align(4));

        return load;
    }

    llvm::Value *Intrinsics::generate_atomic_load_64(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 2)
        {
            report_error("atomic_load_64 requires exactly 2 arguments (ptr, order)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        llvm::Value *ptr = args[0];
        if (!ptr->getType()->isPointerTy())
        {
            report_error("atomic_load_64: first argument must be a pointer");
            return nullptr;
        }

        llvm::Type *i64_type = llvm::Type::getInt64Ty(context);
        llvm::LoadInst *load = builder.CreateLoad(i64_type, ptr);
        load->setAtomic(llvm::AtomicOrdering::SequentiallyConsistent);
        load->setAlignment(llvm::Align(8));

        return load;
    }

    llvm::Value *Intrinsics::generate_atomic_store_8(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 3)
        {
            report_error("atomic_store_8 requires exactly 3 arguments (ptr, val, order)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        llvm::Value *ptr = args[0];
        llvm::Value *val = args[1];

        if (!ptr->getType()->isPointerTy())
        {
            report_error("atomic_store_8: first argument must be a pointer");
            return nullptr;
        }

        llvm::Type *i8_type = llvm::Type::getInt8Ty(context);
        llvm::Value *val_i8 = ensure_type(val, i8_type, "atomic_store_8.val");

        llvm::StoreInst *store = builder.CreateStore(val_i8, ptr);
        store->setAtomic(llvm::AtomicOrdering::SequentiallyConsistent);
        store->setAlignment(llvm::Align(1));

        // Return void (null constant)
        return nullptr;
    }

    llvm::Value *Intrinsics::generate_atomic_store_16(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 3)
        {
            report_error("atomic_store_16 requires exactly 3 arguments (ptr, val, order)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        llvm::Value *ptr = args[0];
        llvm::Value *val = args[1];

        if (!ptr->getType()->isPointerTy())
        {
            report_error("atomic_store_16: first argument must be a pointer");
            return nullptr;
        }

        llvm::Type *i16_type = llvm::Type::getInt16Ty(context);
        llvm::Value *val_i16 = ensure_type(val, i16_type, "atomic_store_16.val");

        llvm::StoreInst *store = builder.CreateStore(val_i16, ptr);
        store->setAtomic(llvm::AtomicOrdering::SequentiallyConsistent);
        store->setAlignment(llvm::Align(2));

        return nullptr;
    }

    llvm::Value *Intrinsics::generate_atomic_store_32(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 3)
        {
            report_error("atomic_store_32 requires exactly 3 arguments (ptr, val, order)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        llvm::Value *ptr = args[0];
        llvm::Value *val = args[1];

        if (!ptr->getType()->isPointerTy())
        {
            report_error("atomic_store_32: first argument must be a pointer");
            return nullptr;
        }

        llvm::Type *i32_type = llvm::Type::getInt32Ty(context);
        llvm::Value *val_i32 = ensure_type(val, i32_type, "atomic_store_32.val");

        llvm::StoreInst *store = builder.CreateStore(val_i32, ptr);
        store->setAtomic(llvm::AtomicOrdering::SequentiallyConsistent);
        store->setAlignment(llvm::Align(4));

        return nullptr;
    }

    llvm::Value *Intrinsics::generate_atomic_store_64(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 3)
        {
            report_error("atomic_store_64 requires exactly 3 arguments (ptr, val, order)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        llvm::Value *ptr = args[0];
        llvm::Value *val = args[1];

        if (!ptr->getType()->isPointerTy())
        {
            report_error("atomic_store_64: first argument must be a pointer");
            return nullptr;
        }

        llvm::Type *i64_type = llvm::Type::getInt64Ty(context);
        llvm::Value *val_i64 = ensure_type(val, i64_type, "atomic_store_64.val");

        llvm::StoreInst *store = builder.CreateStore(val_i64, ptr);
        store->setAtomic(llvm::AtomicOrdering::SequentiallyConsistent);
        store->setAlignment(llvm::Align(8));

        return nullptr;
    }

    llvm::Value *Intrinsics::generate_atomic_exchange_32(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 3)
        {
            report_error("atomic_exchange_32 requires exactly 3 arguments (ptr, val, order)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        llvm::Value *ptr = args[0];
        llvm::Value *val = args[1];

        if (!ptr->getType()->isPointerTy())
        {
            report_error("atomic_exchange_32: first argument must be a pointer");
            return nullptr;
        }

        llvm::Type *i32_type = llvm::Type::getInt32Ty(context);
        llvm::Value *val_i32 = ensure_type(val, i32_type, "atomic_exchange_32.val");

        // Create atomic exchange instruction
        return builder.CreateAtomicRMW(llvm::AtomicRMWInst::Xchg, ptr, val_i32,
                                       llvm::MaybeAlign(4), llvm::AtomicOrdering::SequentiallyConsistent);
    }

    llvm::Value *Intrinsics::generate_atomic_exchange_64(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 3)
        {
            report_error("atomic_exchange_64 requires exactly 3 arguments (ptr, val, order)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        llvm::Value *ptr = args[0];
        llvm::Value *val = args[1];

        if (!ptr->getType()->isPointerTy())
        {
            report_error("atomic_exchange_64: first argument must be a pointer");
            return nullptr;
        }

        llvm::Type *i64_type = llvm::Type::getInt64Ty(context);
        llvm::Value *val_i64 = ensure_type(val, i64_type, "atomic_exchange_64.val");

        return builder.CreateAtomicRMW(llvm::AtomicRMWInst::Xchg, ptr, val_i64,
                                       llvm::MaybeAlign(8), llvm::AtomicOrdering::SequentiallyConsistent);
    }

    llvm::Value *Intrinsics::generate_atomic_swap_64(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 3)
        {
            report_error("atomic_swap_64 requires exactly 3 arguments (ptr, val, order)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        llvm::Value *ptr = args[0];
        llvm::Value *val = args[1];

        if (!ptr->getType()->isPointerTy())
        {
            report_error("atomic_swap_64: first argument must be a pointer");
            return nullptr;
        }

        llvm::Type *i64_type = llvm::Type::getInt64Ty(context);
        llvm::Value *val_i64 = ensure_type(val, i64_type, "atomic_swap_64.val");

        return builder.CreateAtomicRMW(llvm::AtomicRMWInst::Xchg, ptr, val_i64,
                                       llvm::MaybeAlign(8), llvm::AtomicOrdering::SequentiallyConsistent);
    }

    llvm::Value *Intrinsics::generate_atomic_compare_exchange_32(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 4)
        {
            report_error("atomic_compare_exchange_32 requires exactly 4 arguments (ptr, expected, desired, order)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        llvm::Value *ptr = args[0];
        llvm::Value *expected_ptr = args[1];
        llvm::Value *desired = args[2];

        if (!ptr->getType()->isPointerTy() || !expected_ptr->getType()->isPointerTy())
        {
            report_error("atomic_compare_exchange_32: first two arguments must be pointers");
            return nullptr;
        }

        llvm::Type *i32_type = llvm::Type::getInt32Ty(context);
        llvm::Value *expected = builder.CreateLoad(i32_type, expected_ptr, "expected_val");
        llvm::Value *desired_i32 = ensure_type(desired, i32_type, "atomic_compare_exchange_32.desired");

        // Create compare exchange instruction
        llvm::AtomicCmpXchgInst *cmpxchg = builder.CreateAtomicCmpXchg(
            ptr, expected, desired_i32, llvm::MaybeAlign(4),
            llvm::AtomicOrdering::SequentiallyConsistent,
            llvm::AtomicOrdering::SequentiallyConsistent);

        // Extract the success flag
        llvm::Value *success = builder.CreateExtractValue(cmpxchg, 1, "success");

        // Store the actual value back to expected pointer
        llvm::Value *actual = builder.CreateExtractValue(cmpxchg, 0, "actual");
        builder.CreateStore(actual, expected_ptr);

        return success;
    }

    llvm::Value *Intrinsics::generate_atomic_compare_exchange_64(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 4)
        {
            report_error("atomic_compare_exchange_64 requires exactly 4 arguments (ptr, expected, desired, order)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        llvm::Value *ptr = args[0];
        llvm::Value *expected_ptr = args[1];
        llvm::Value *desired = args[2];

        if (!ptr->getType()->isPointerTy() || !expected_ptr->getType()->isPointerTy())
        {
            report_error("atomic_compare_exchange_64: first two arguments must be pointers");
            return nullptr;
        }

        llvm::Type *i64_type = llvm::Type::getInt64Ty(context);
        llvm::Value *expected = builder.CreateLoad(i64_type, expected_ptr, "expected_val");
        llvm::Value *desired_i64 = ensure_type(desired, i64_type, "atomic_compare_exchange_64.desired");

        llvm::AtomicCmpXchgInst *cmpxchg = builder.CreateAtomicCmpXchg(
            ptr, expected, desired_i64, llvm::MaybeAlign(8),
            llvm::AtomicOrdering::SequentiallyConsistent,
            llvm::AtomicOrdering::SequentiallyConsistent);

        llvm::Value *success = builder.CreateExtractValue(cmpxchg, 1, "success");
        llvm::Value *actual = builder.CreateExtractValue(cmpxchg, 0, "actual");
        builder.CreateStore(actual, expected_ptr);

        return success;
    }

// Define atomic fetch operations using a macro for efficiency
#define DEFINE_ATOMIC_FETCH_OP(op_name, llvm_op, bit_size)                                                            \
    llvm::Value *Intrinsics::generate_atomic_fetch_##op_name##_##bit_size(const std::vector<llvm::Value *> &args)     \
    {                                                                                                                 \
        if (args.size() != 3)                                                                                         \
        {                                                                                                             \
            report_error("atomic_fetch_" #op_name "_" #bit_size " requires exactly 3 arguments (ptr, val, order)");   \
            return nullptr;                                                                                           \
        }                                                                                                             \
                                                                                                                      \
        auto &builder = _context_manager.get_builder();                                                               \
        auto &context = _context_manager.get_context();                                                               \
                                                                                                                      \
        llvm::Value *ptr = args[0];                                                                                   \
        llvm::Value *val = args[1];                                                                                   \
                                                                                                                      \
        if (!ptr->getType()->isPointerTy())                                                                           \
        {                                                                                                             \
            report_error("atomic_fetch_" #op_name "_" #bit_size ": first argument must be a pointer");                \
            return nullptr;                                                                                           \
        }                                                                                                             \
                                                                                                                      \
        llvm::Type *int_type = llvm::Type::getInt##bit_size##Ty(context);                                             \
        llvm::Value *val_typed = ensure_type(val, int_type, "atomic_fetch_" #op_name "_" #bit_size ".val");           \
                                                                                                                      \
        return builder.CreateAtomicRMW(llvm_op, ptr, val_typed,                                                       \
                                       llvm::MaybeAlign(bit_size / 8), llvm::AtomicOrdering::SequentiallyConsistent); \
    }

    DEFINE_ATOMIC_FETCH_OP(add, llvm::AtomicRMWInst::Add, 32)
    DEFINE_ATOMIC_FETCH_OP(add, llvm::AtomicRMWInst::Add, 64)
    DEFINE_ATOMIC_FETCH_OP(sub, llvm::AtomicRMWInst::Sub, 32)
    DEFINE_ATOMIC_FETCH_OP(sub, llvm::AtomicRMWInst::Sub, 64)
    DEFINE_ATOMIC_FETCH_OP(and, llvm::AtomicRMWInst::And, 32)
    DEFINE_ATOMIC_FETCH_OP(and, llvm::AtomicRMWInst::And, 64)
    DEFINE_ATOMIC_FETCH_OP(or, llvm::AtomicRMWInst::Or, 32)
    DEFINE_ATOMIC_FETCH_OP(or, llvm::AtomicRMWInst::Or, 64)
    DEFINE_ATOMIC_FETCH_OP(xor, llvm::AtomicRMWInst::Xor, 32)
    DEFINE_ATOMIC_FETCH_OP(xor, llvm::AtomicRMWInst::Xor, 64)

    llvm::Value *Intrinsics::generate_atomic_fence(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 1)
        {
            report_error("atomic_fence requires exactly 1 argument (order)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        // Create memory fence instruction
        builder.CreateFence(llvm::AtomicOrdering::SequentiallyConsistent);

        // Return nullptr for void-returning intrinsics
        return nullptr;
    }

    // ========================================
    // Enhanced Network Intrinsics (Full Implementations)
    // ========================================

    llvm::Value *Intrinsics::generate_listen(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 2)
        {
            report_error("listen requires exactly 2 arguments (sockfd, backlog)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        llvm::Type *int_type = llvm::Type::getInt32Ty(context);
        llvm::FunctionType *listen_type = llvm::FunctionType::get(int_type, {int_type, int_type}, false);

        llvm::Function *listen_func = get_or_create_libc_function("listen", listen_type);
        return builder.CreateCall(listen_func, {ensure_type(args[0], int_type, "listen.sockfd"), ensure_type(args[1], int_type, "listen.backlog")}, "listen.result");
    }

    llvm::Value *Intrinsics::generate_accept(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 3)
        {
            report_error("intrinsics::accept requires exactly 3 arguments (sockfd, addr, addrlen), but got " +
                         std::to_string(args.size()) + ". Did you mean to call a method instead?");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        llvm::Type *int_type = llvm::Type::getInt32Ty(context);
        llvm::Type *void_ptr_type = llvm::PointerType::get(context, 0);
        llvm::Type *uint32_ptr_type = llvm::PointerType::get(llvm::Type::getInt32Ty(context), 0);
        llvm::FunctionType *accept_type = llvm::FunctionType::get(
            int_type, {int_type, void_ptr_type, uint32_ptr_type}, false);

        llvm::Function *accept_func = get_or_create_libc_function("accept", accept_type);
        return builder.CreateCall(accept_func, {
                                                   ensure_type(args[0], int_type, "accept.sockfd"),
                                                   args[1], // addr
                                                   args[2]  // addrlen
                                               },
                                  "accept.result");
    }

    llvm::Value *Intrinsics::generate_connect(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 3)
        {
            report_error("connect requires exactly 3 arguments (sockfd, addr, addrlen)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        llvm::Type *int_type = llvm::Type::getInt32Ty(context);
        llvm::Type *void_ptr_type = llvm::PointerType::get(context, 0);
        llvm::Type *uint32_type = llvm::Type::getInt32Ty(context);
        llvm::FunctionType *connect_type = llvm::FunctionType::get(
            int_type, {int_type, void_ptr_type, uint32_type}, false);

        llvm::Function *connect_func = get_or_create_libc_function("connect", connect_type);
        return builder.CreateCall(connect_func, {ensure_type(args[0], int_type, "connect.sockfd"),
                                                 args[1], // addr
                                                 ensure_type(args[2], uint32_type, "connect.addrlen")},
                                  "connect.result");
    }

    llvm::Value *Intrinsics::generate_send(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 4)
        {
            report_error("send requires exactly 4 arguments (sockfd, buf, len, flags)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        llvm::Type *int_type = llvm::Type::getInt32Ty(context);
        llvm::Type *void_ptr_type = llvm::PointerType::get(context, 0);
        // Use i32 for len to match common declarations
        llvm::FunctionType *send_type = llvm::FunctionType::get(
            int_type, {int_type, void_ptr_type, int_type, int_type}, false);

        llvm::Function *send_func = get_or_create_libc_function("send", send_type);
        return builder.CreateCall(send_func, {ensure_type(args[0], int_type, "send.sockfd"),
                                              args[1], // buf
                                              ensure_type(args[2], int_type, "send.len"),
                                              ensure_type(args[3], int_type, "send.flags")},
                                  "send.result");
    }

    llvm::Value *Intrinsics::generate_recv(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 4)
        {
            report_error("recv requires exactly 4 arguments (sockfd, buf, len, flags)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        llvm::Type *int_type = llvm::Type::getInt32Ty(context);
        llvm::Type *void_ptr_type = llvm::PointerType::get(context, 0);
        // Use i32 for len to match common declarations
        llvm::FunctionType *recv_type = llvm::FunctionType::get(
            int_type, {int_type, void_ptr_type, int_type, int_type}, false);

        llvm::Function *recv_func = get_or_create_libc_function("recv", recv_type);
        return builder.CreateCall(recv_func, {ensure_type(args[0], int_type, "recv.sockfd"),
                                              args[1], // buf
                                              ensure_type(args[2], int_type, "recv.len"),
                                              ensure_type(args[3], int_type, "recv.flags")},
                                  "recv.result");
    }

    // All network intrinsics are now fully implemented above

    // ========================================
    // Additional 8-bit Atomic Intrinsics
    // ========================================

    llvm::Value *Intrinsics::generate_atomic_swap_8(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 3)
        {
            report_error("atomic_swap_8 requires exactly 3 arguments (ptr, val, order)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        llvm::Value *ptr = args[0];
        llvm::Value *val = args[1];

        if (!ptr->getType()->isPointerTy())
        {
            report_error("atomic_swap_8: first argument must be a pointer");
            return nullptr;
        }

        llvm::Type *i8_type = llvm::Type::getInt8Ty(context);
        llvm::Value *val_typed = ensure_type(val, i8_type, "atomic_swap_8.val");

        return builder.CreateAtomicRMW(llvm::AtomicRMWInst::Xchg, ptr, val_typed,
                                       llvm::MaybeAlign(1), llvm::AtomicOrdering::SequentiallyConsistent);
    }

    llvm::Value *Intrinsics::generate_atomic_exchange_8(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 3)
        {
            report_error("atomic_exchange_8 requires exactly 3 arguments (ptr, val, order)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        llvm::Value *ptr = args[0];
        llvm::Value *val = args[1];

        if (!ptr->getType()->isPointerTy())
        {
            report_error("atomic_exchange_8: first argument must be a pointer");
            return nullptr;
        }

        llvm::Type *i8_type = llvm::Type::getInt8Ty(context);
        llvm::Value *val_typed = ensure_type(val, i8_type, "atomic_exchange_8.val");

        return builder.CreateAtomicRMW(llvm::AtomicRMWInst::Xchg, ptr, val_typed,
                                       llvm::MaybeAlign(1), llvm::AtomicOrdering::SequentiallyConsistent);
    }

    llvm::Value *Intrinsics::generate_atomic_compare_exchange_8(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 4)
        {
            report_error("atomic_compare_exchange_8 requires exactly 4 arguments (ptr, expected, desired, order)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        llvm::Value *ptr = args[0];
        llvm::Value *expected = args[1];
        llvm::Value *desired = args[2];

        if (!ptr->getType()->isPointerTy())
        {
            report_error("atomic_compare_exchange_8: first argument must be a pointer");
            return nullptr;
        }

        llvm::Type *i8_type = llvm::Type::getInt8Ty(context);
        llvm::Value *expected_val = builder.CreateLoad(i8_type, expected, "expected_val");
        llvm::Value *desired_typed = ensure_type(desired, i8_type, "atomic_compare_exchange_8.desired");

        llvm::Value *result = builder.CreateAtomicCmpXchg(
            ptr, expected_val, desired_typed,
            llvm::MaybeAlign(1),
            llvm::AtomicOrdering::SequentiallyConsistent,
            llvm::AtomicOrdering::SequentiallyConsistent);

        // Extract the success flag (second element)
        return builder.CreateExtractValue(result, 1, "cmpxchg.success");
    }

    // Define 8-bit atomic fetch operations
    DEFINE_ATOMIC_FETCH_OP(and, llvm::AtomicRMWInst::And, 8)
    DEFINE_ATOMIC_FETCH_OP(or, llvm::AtomicRMWInst::Or, 8)
    DEFINE_ATOMIC_FETCH_OP(xor, llvm::AtomicRMWInst::Xor, 8)

    llvm::Value *Intrinsics::generate_atomic_fetch_nand_8(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 3)
        {
            report_error("atomic_fetch_nand_8 requires exactly 3 arguments (ptr, val, order)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        llvm::Value *ptr = args[0];
        llvm::Value *val = args[1];

        if (!ptr->getType()->isPointerTy())
        {
            report_error("atomic_fetch_nand_8: first argument must be a pointer");
            return nullptr;
        }

        llvm::Type *i8_type = llvm::Type::getInt8Ty(context);
        llvm::Value *val_typed = ensure_type(val, i8_type, "atomic_fetch_nand_8.val");

        return builder.CreateAtomicRMW(llvm::AtomicRMWInst::Nand, ptr, val_typed,
                                       llvm::MaybeAlign(1), llvm::AtomicOrdering::SequentiallyConsistent);
    }

    // ========================================
    // Typed Atomic Load/Store Functions (u8)
    // ========================================

    llvm::Value *Intrinsics::generate_atomic_load_u8(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 2)
        {
            report_error("atomic_load_u8 requires exactly 2 arguments (ptr, order)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        llvm::Value *ptr = args[0];
        if (!ptr->getType()->isPointerTy())
        {
            report_error("atomic_load_u8: first argument must be a pointer");
            return nullptr;
        }

        llvm::Type *i8_type = llvm::Type::getInt8Ty(context);
        llvm::LoadInst *load = builder.CreateLoad(i8_type, ptr);
        load->setAtomic(llvm::AtomicOrdering::SequentiallyConsistent);
        load->setAlignment(llvm::Align(1));

        return load;
    }

    llvm::Value *Intrinsics::generate_atomic_store_u8(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 3)
        {
            report_error("atomic_store_u8 requires exactly 3 arguments (ptr, val, order)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        llvm::Value *ptr = args[0];
        llvm::Value *val = args[1];

        if (!ptr->getType()->isPointerTy())
        {
            report_error("atomic_store_u8: first argument must be a pointer");
            return nullptr;
        }

        llvm::Type *i8_type = llvm::Type::getInt8Ty(context);
        llvm::Value *val_typed = ensure_type(val, i8_type, "atomic_store_u8.val");

        llvm::StoreInst *store = builder.CreateStore(val_typed, ptr);
        store->setAtomic(llvm::AtomicOrdering::SequentiallyConsistent);
        store->setAlignment(llvm::Align(1));

        return nullptr;
    }

    llvm::Value *Intrinsics::generate_atomic_swap_u8(const std::vector<llvm::Value *> &args)
    {
        return generate_atomic_swap_8(args); // Same implementation
    }

    llvm::Value *Intrinsics::generate_atomic_cmpxchg_u8(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 5)
        {
            report_error("atomic_cmpxchg_u8 requires exactly 5 arguments (ptr, expected, desired, success_order, failure_order)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        llvm::Value *ptr = args[0];
        llvm::Value *expected = args[1];
        llvm::Value *desired = args[2];

        if (!ptr->getType()->isPointerTy())
        {
            report_error("atomic_cmpxchg_u8: first argument must be a pointer");
            return nullptr;
        }

        llvm::Type *i8_type = llvm::Type::getInt8Ty(context);
        llvm::Value *expected_val = builder.CreateLoad(i8_type, expected, "expected_val");
        llvm::Value *desired_typed = ensure_type(desired, i8_type, "atomic_cmpxchg_u8.desired");

        llvm::Value *result = builder.CreateAtomicCmpXchg(
            ptr, expected_val, desired_typed,
            llvm::MaybeAlign(1),
            llvm::AtomicOrdering::SequentiallyConsistent,
            llvm::AtomicOrdering::SequentiallyConsistent);

        return builder.CreateExtractValue(result, 1, "cmpxchg.success");
    }

    // u8 variants that delegate to the 8-bit versions
    llvm::Value *Intrinsics::generate_atomic_fetch_and_u8(const std::vector<llvm::Value *> &args)
    {
        return generate_atomic_fetch_and_8(args);
    }

    llvm::Value *Intrinsics::generate_atomic_fetch_or_u8(const std::vector<llvm::Value *> &args)
    {
        return generate_atomic_fetch_or_8(args);
    }

    llvm::Value *Intrinsics::generate_atomic_fetch_xor_u8(const std::vector<llvm::Value *> &args)
    {
        return generate_atomic_fetch_xor_8(args);
    }

    llvm::Value *Intrinsics::generate_atomic_fetch_nand_u8(const std::vector<llvm::Value *> &args)
    {
        return generate_atomic_fetch_nand_8(args);
    }

    // ========================================
    // Signed 32-bit Atomic Operations
    // ========================================

    llvm::Value *Intrinsics::generate_atomic_load_i32(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 2)
        {
            report_error("atomic_load_i32 requires exactly 2 arguments (ptr, order)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        llvm::Value *ptr = args[0];
        if (!ptr->getType()->isPointerTy())
        {
            report_error("atomic_load_i32: first argument must be a pointer");
            return nullptr;
        }

        llvm::Type *i32_type = llvm::Type::getInt32Ty(context);
        llvm::LoadInst *load = builder.CreateLoad(i32_type, ptr);
        load->setAtomic(llvm::AtomicOrdering::SequentiallyConsistent);
        load->setAlignment(llvm::Align(4));

        return load;
    }

    llvm::Value *Intrinsics::generate_atomic_store_i32(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 3)
        {
            report_error("atomic_store_i32 requires exactly 3 arguments (ptr, val, order)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        llvm::Value *ptr = args[0];
        llvm::Value *val = args[1];

        if (!ptr->getType()->isPointerTy())
        {
            report_error("atomic_store_i32: first argument must be a pointer");
            return nullptr;
        }

        llvm::Type *i32_type = llvm::Type::getInt32Ty(context);
        llvm::Value *val_typed = ensure_type(val, i32_type, "atomic_store_i32.val");

        llvm::StoreInst *store = builder.CreateStore(val_typed, ptr);
        store->setAtomic(llvm::AtomicOrdering::SequentiallyConsistent);
        store->setAlignment(llvm::Align(4));

        return nullptr;
    }

    llvm::Value *Intrinsics::generate_atomic_swap_i32(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 3)
        {
            report_error("atomic_swap_i32 requires exactly 3 arguments (ptr, val, order)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        llvm::Value *ptr = args[0];
        llvm::Value *val = args[1];

        if (!ptr->getType()->isPointerTy())
        {
            report_error("atomic_swap_i32: first argument must be a pointer");
            return nullptr;
        }

        llvm::Type *i32_type = llvm::Type::getInt32Ty(context);
        llvm::Value *val_typed = ensure_type(val, i32_type, "atomic_swap_i32.val");

        return builder.CreateAtomicRMW(llvm::AtomicRMWInst::Xchg, ptr, val_typed,
                                       llvm::MaybeAlign(4), llvm::AtomicOrdering::SequentiallyConsistent);
    }

    llvm::Value *Intrinsics::generate_atomic_cmpxchg_i32(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 5)
        {
            report_error("atomic_cmpxchg_i32 requires exactly 5 arguments (ptr, expected, desired, success_order, failure_order)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        llvm::Value *ptr = args[0];
        llvm::Value *expected = args[1];
        llvm::Value *desired = args[2];

        if (!ptr->getType()->isPointerTy())
        {
            report_error("atomic_cmpxchg_i32: first argument must be a pointer");
            return nullptr;
        }

        llvm::Type *i32_type = llvm::Type::getInt32Ty(context);
        llvm::Value *expected_val = builder.CreateLoad(i32_type, expected, "expected_val");
        llvm::Value *desired_typed = ensure_type(desired, i32_type, "atomic_cmpxchg_i32.desired");

        llvm::Value *result = builder.CreateAtomicCmpXchg(
            ptr, expected_val, desired_typed,
            llvm::MaybeAlign(4),
            llvm::AtomicOrdering::SequentiallyConsistent,
            llvm::AtomicOrdering::SequentiallyConsistent);

        return builder.CreateExtractValue(result, 1, "cmpxchg.success");
    }

    // Define i32 atomic fetch operations
    llvm::Value *Intrinsics::generate_atomic_fetch_add_i32(const std::vector<llvm::Value *> &args)
    {
        return generate_atomic_fetch_add_32(args); // Delegate to existing implementation
    }

    llvm::Value *Intrinsics::generate_atomic_fetch_sub_i32(const std::vector<llvm::Value *> &args)
    {
        return generate_atomic_fetch_sub_32(args); // Delegate to existing implementation
    }

    llvm::Value *Intrinsics::generate_atomic_fetch_and_i32(const std::vector<llvm::Value *> &args)
    {
        return generate_atomic_fetch_and_32(args); // Delegate to existing implementation
    }

    llvm::Value *Intrinsics::generate_atomic_fetch_or_i32(const std::vector<llvm::Value *> &args)
    {
        return generate_atomic_fetch_or_32(args); // Delegate to existing implementation
    }

    llvm::Value *Intrinsics::generate_atomic_fetch_xor_i32(const std::vector<llvm::Value *> &args)
    {
        return generate_atomic_fetch_xor_32(args); // Delegate to existing implementation
    }

    llvm::Value *Intrinsics::generate_atomic_fetch_max_i32(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 3)
        {
            report_error("atomic_fetch_max_i32 requires exactly 3 arguments (ptr, val, order)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        llvm::Value *ptr = args[0];
        llvm::Value *val = args[1];

        if (!ptr->getType()->isPointerTy())
        {
            report_error("atomic_fetch_max_i32: first argument must be a pointer");
            return nullptr;
        }

        llvm::Type *i32_type = llvm::Type::getInt32Ty(context);
        llvm::Value *val_typed = ensure_type(val, i32_type, "atomic_fetch_max_i32.val");

        return builder.CreateAtomicRMW(llvm::AtomicRMWInst::Max, ptr, val_typed,
                                       llvm::MaybeAlign(4), llvm::AtomicOrdering::SequentiallyConsistent);
    }

    llvm::Value *Intrinsics::generate_atomic_fetch_min_i32(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 3)
        {
            report_error("atomic_fetch_min_i32 requires exactly 3 arguments (ptr, val, order)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        llvm::Value *ptr = args[0];
        llvm::Value *val = args[1];

        if (!ptr->getType()->isPointerTy())
        {
            report_error("atomic_fetch_min_i32: first argument must be a pointer");
            return nullptr;
        }

        llvm::Type *i32_type = llvm::Type::getInt32Ty(context);
        llvm::Value *val_typed = ensure_type(val, i32_type, "atomic_fetch_min_i32.val");

        return builder.CreateAtomicRMW(llvm::AtomicRMWInst::Min, ptr, val_typed,
                                       llvm::MaybeAlign(4), llvm::AtomicOrdering::SequentiallyConsistent);
    }

    // ========================================
    // Unsigned 32-bit Atomic Operations
    // ========================================

    llvm::Value *Intrinsics::generate_atomic_load_u32(const std::vector<llvm::Value *> &args)
    {
        return generate_atomic_load_32(args); // Delegate to existing implementation
    }

    llvm::Value *Intrinsics::generate_atomic_store_u32(const std::vector<llvm::Value *> &args)
    {
        return generate_atomic_store_32(args); // Delegate to existing implementation
    }

    llvm::Value *Intrinsics::generate_atomic_swap_u32(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 3)
        {
            report_error("atomic_swap_u32 requires exactly 3 arguments (ptr, val, order)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        llvm::Value *ptr = args[0];
        llvm::Value *val = args[1];

        if (!ptr->getType()->isPointerTy())
        {
            report_error("atomic_swap_u32: first argument must be a pointer");
            return nullptr;
        }

        llvm::Type *i32_type = llvm::Type::getInt32Ty(context);
        llvm::Value *val_typed = ensure_type(val, i32_type, "atomic_swap_u32.val");

        return builder.CreateAtomicRMW(llvm::AtomicRMWInst::Xchg, ptr, val_typed,
                                       llvm::MaybeAlign(4), llvm::AtomicOrdering::SequentiallyConsistent);
    }

    llvm::Value *Intrinsics::generate_atomic_cmpxchg_u32(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 5)
        {
            report_error("atomic_cmpxchg_u32 requires exactly 5 arguments (ptr, expected, desired, success_order, failure_order)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        llvm::Value *ptr = args[0];
        llvm::Value *expected = args[1];
        llvm::Value *desired = args[2];

        if (!ptr->getType()->isPointerTy())
        {
            report_error("atomic_cmpxchg_u32: first argument must be a pointer");
            return nullptr;
        }

        llvm::Type *i32_type = llvm::Type::getInt32Ty(context);
        llvm::Value *expected_val = builder.CreateLoad(i32_type, expected, "expected_val");
        llvm::Value *desired_typed = ensure_type(desired, i32_type, "atomic_cmpxchg_u32.desired");

        llvm::Value *result = builder.CreateAtomicCmpXchg(
            ptr, expected_val, desired_typed,
            llvm::MaybeAlign(4),
            llvm::AtomicOrdering::SequentiallyConsistent,
            llvm::AtomicOrdering::SequentiallyConsistent);

        return builder.CreateExtractValue(result, 1, "cmpxchg.success");
    }

    llvm::Value *Intrinsics::generate_atomic_fetch_add_u32(const std::vector<llvm::Value *> &args)
    {
        return generate_atomic_fetch_add_32(args); // Delegate to existing implementation
    }

    llvm::Value *Intrinsics::generate_atomic_fetch_sub_u32(const std::vector<llvm::Value *> &args)
    {
        return generate_atomic_fetch_sub_32(args); // Delegate to existing implementation
    }

    llvm::Value *Intrinsics::generate_atomic_fetch_and_u32(const std::vector<llvm::Value *> &args)
    {
        return generate_atomic_fetch_and_32(args); // Delegate to existing implementation
    }

    llvm::Value *Intrinsics::generate_atomic_fetch_or_u32(const std::vector<llvm::Value *> &args)
    {
        return generate_atomic_fetch_or_32(args); // Delegate to existing implementation
    }

    llvm::Value *Intrinsics::generate_atomic_fetch_xor_u32(const std::vector<llvm::Value *> &args)
    {
        return generate_atomic_fetch_xor_32(args); // Delegate to existing implementation
    }

    // ========================================
    // Signed 64-bit Atomic Operations
    // ========================================

    llvm::Value *Intrinsics::generate_atomic_load_i64(const std::vector<llvm::Value *> &args)
    {
        return generate_atomic_load_64(args); // Delegate to existing implementation
    }

    llvm::Value *Intrinsics::generate_atomic_store_i64(const std::vector<llvm::Value *> &args)
    {
        return generate_atomic_store_64(args); // Delegate to existing implementation
    }

    llvm::Value *Intrinsics::generate_atomic_swap_i64(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 3)
        {
            report_error("atomic_swap_i64 requires exactly 3 arguments (ptr, val, order)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        llvm::Value *ptr = args[0];
        llvm::Value *val = args[1];

        if (!ptr->getType()->isPointerTy())
        {
            report_error("atomic_swap_i64: first argument must be a pointer");
            return nullptr;
        }

        llvm::Type *i64_type = llvm::Type::getInt64Ty(context);
        llvm::Value *val_typed = ensure_type(val, i64_type, "atomic_swap_i64.val");

        return builder.CreateAtomicRMW(llvm::AtomicRMWInst::Xchg, ptr, val_typed,
                                       llvm::MaybeAlign(8), llvm::AtomicOrdering::SequentiallyConsistent);
    }

    llvm::Value *Intrinsics::generate_atomic_cmpxchg_i64(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 5)
        {
            report_error("atomic_cmpxchg_i64 requires exactly 5 arguments (ptr, expected, desired, success_order, failure_order)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        llvm::Value *ptr = args[0];
        llvm::Value *expected = args[1];
        llvm::Value *desired = args[2];

        if (!ptr->getType()->isPointerTy())
        {
            report_error("atomic_cmpxchg_i64: first argument must be a pointer");
            return nullptr;
        }

        llvm::Type *i64_type = llvm::Type::getInt64Ty(context);
        llvm::Value *expected_val = builder.CreateLoad(i64_type, expected, "expected_val");
        llvm::Value *desired_typed = ensure_type(desired, i64_type, "atomic_cmpxchg_i64.desired");

        llvm::Value *result = builder.CreateAtomicCmpXchg(
            ptr, expected_val, desired_typed,
            llvm::MaybeAlign(8),
            llvm::AtomicOrdering::SequentiallyConsistent,
            llvm::AtomicOrdering::SequentiallyConsistent);

        return builder.CreateExtractValue(result, 1, "cmpxchg.success");
    }

    llvm::Value *Intrinsics::generate_atomic_fetch_add_i64(const std::vector<llvm::Value *> &args)
    {
        return generate_atomic_fetch_add_64(args); // Delegate to existing implementation
    }

    llvm::Value *Intrinsics::generate_atomic_fetch_sub_i64(const std::vector<llvm::Value *> &args)
    {
        return generate_atomic_fetch_sub_64(args); // Delegate to existing implementation
    }

    // ========================================
    // Unsigned 64-bit Atomic Operations
    // ========================================

    llvm::Value *Intrinsics::generate_atomic_load_u64(const std::vector<llvm::Value *> &args)
    {
        return generate_atomic_load_64(args); // Delegate to existing implementation
    }

    llvm::Value *Intrinsics::generate_atomic_store_u64(const std::vector<llvm::Value *> &args)
    {
        return generate_atomic_store_64(args); // Delegate to existing implementation
    }

    llvm::Value *Intrinsics::generate_atomic_swap_u64(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 3)
        {
            report_error("atomic_swap_u64 requires exactly 3 arguments (ptr, val, order)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        llvm::Value *ptr = args[0];
        llvm::Value *val = args[1];

        if (!ptr->getType()->isPointerTy())
        {
            report_error("atomic_swap_u64: first argument must be a pointer");
            return nullptr;
        }

        llvm::Type *i64_type = llvm::Type::getInt64Ty(context);
        llvm::Value *val_typed = ensure_type(val, i64_type, "atomic_swap_u64.val");

        return builder.CreateAtomicRMW(llvm::AtomicRMWInst::Xchg, ptr, val_typed,
                                       llvm::MaybeAlign(8), llvm::AtomicOrdering::SequentiallyConsistent);
    }

    llvm::Value *Intrinsics::generate_atomic_cmpxchg_u64(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 5)
        {
            report_error("atomic_cmpxchg_u64 requires exactly 5 arguments (ptr, expected, desired, success_order, failure_order)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        llvm::Value *ptr = args[0];
        llvm::Value *expected = args[1];
        llvm::Value *desired = args[2];

        if (!ptr->getType()->isPointerTy())
        {
            report_error("atomic_cmpxchg_u64: first argument must be a pointer");
            return nullptr;
        }

        llvm::Type *i64_type = llvm::Type::getInt64Ty(context);
        llvm::Value *expected_val = builder.CreateLoad(i64_type, expected, "expected_val");
        llvm::Value *desired_typed = ensure_type(desired, i64_type, "atomic_cmpxchg_u64.desired");

        llvm::Value *result = builder.CreateAtomicCmpXchg(
            ptr, expected_val, desired_typed,
            llvm::MaybeAlign(8),
            llvm::AtomicOrdering::SequentiallyConsistent,
            llvm::AtomicOrdering::SequentiallyConsistent);

        return builder.CreateExtractValue(result, 1, "cmpxchg.success");
    }

    llvm::Value *Intrinsics::generate_atomic_fetch_add_u64(const std::vector<llvm::Value *> &args)
    {
        return generate_atomic_fetch_add_64(args); // Delegate to existing implementation
    }

    llvm::Value *Intrinsics::generate_atomic_fetch_sub_u64(const std::vector<llvm::Value *> &args)
    {
        return generate_atomic_fetch_sub_64(args); // Delegate to existing implementation
    }

    // ========================================
    // Integer Type Conversion Intrinsics
    // ========================================

    llvm::Value *Intrinsics::generate_int_conversion(
        const std::vector<llvm::Value *> &args,
        unsigned from_bits, unsigned to_bits,
        bool from_signed, bool to_signed)
    {
        if (args.size() != 1)
        {
            report_error("Integer conversion intrinsic requires exactly 1 argument");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        llvm::Value *val = args[0];
        llvm::Type *from_type = llvm::Type::getIntNTy(context, from_bits);
        llvm::Type *to_type = llvm::Type::getIntNTy(context, to_bits);

        // If the value is a pointer, load it first (handles &this case)
        if (val->getType()->isPointerTy())
        {
            val = builder.CreateLoad(from_type, val, "conv.load");
        }

        // Ensure the input is the expected width
        if (val->getType() != from_type)
        {
            // Try to adapt the value to the expected input type
            if (val->getType()->isIntegerTy())
            {
                unsigned actual_bits = val->getType()->getIntegerBitWidth();
                if (actual_bits < from_bits)
                {
                    // Extend
                    if (from_signed)
                        val = builder.CreateSExt(val, from_type, "conv.sext.in");
                    else
                        val = builder.CreateZExt(val, from_type, "conv.zext.in");
                }
                else if (actual_bits > from_bits)
                {
                    // Truncate
                    val = builder.CreateTrunc(val, from_type, "conv.trunc.in");
                }
            }
        }

        // Same size, different signedness - just a bitcast/reinterpret
        if (from_bits == to_bits)
        {
            return val; // No actual conversion needed at LLVM IR level
        }

        // Widening conversion
        if (from_bits < to_bits)
        {
            if (from_signed)
            {
                return builder.CreateSExt(val, to_type, "conv.sext");
            }
            else
            {
                return builder.CreateZExt(val, to_type, "conv.zext");
            }
        }

        // Narrowing conversion (truncation)
        return builder.CreateTrunc(val, to_type, "conv.trunc");
    }

    // ========================================
    // Variadic Argument Intrinsics
    // ========================================

    llvm::Value *Intrinsics::generate_va_arg_i32(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 1)
        {
            report_error("va_arg_i32 requires exactly 1 argument (va_list pointer)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        llvm::Value *va_list_ptr = args[0];

        // Load the current pointer from the va_list
        llvm::Value *current_ptr = builder.CreateLoad(
            llvm::PointerType::get(context, 0), va_list_ptr, "va.ptr");

        // Load the i32 value from the current position
        llvm::Type *i32_type = llvm::Type::getInt32Ty(context);
        llvm::Value *result = builder.CreateLoad(i32_type, current_ptr, "va.arg.i32");

        // Advance the pointer by 8 bytes (varargs are passed as 64-bit values on x86_64)
        llvm::Value *next_ptr = builder.CreateGEP(
            llvm::Type::getInt8Ty(context), current_ptr,
            llvm::ConstantInt::get(llvm::Type::getInt64Ty(context), 8),
            "va.next");

        // Store the updated pointer back
        builder.CreateStore(next_ptr, va_list_ptr);

        return result;
    }

    llvm::Value *Intrinsics::generate_va_arg_i64(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 1)
        {
            report_error("va_arg_i64 requires exactly 1 argument (va_list pointer)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        llvm::Value *va_list_ptr = args[0];

        // Load the current pointer from the va_list
        llvm::Value *current_ptr = builder.CreateLoad(
            llvm::PointerType::get(context, 0), va_list_ptr, "va.ptr");

        // Load the i64 value from the current position
        llvm::Type *i64_type = llvm::Type::getInt64Ty(context);
        llvm::Value *result = builder.CreateLoad(i64_type, current_ptr, "va.arg.i64");

        // Advance the pointer by 8 bytes
        llvm::Value *next_ptr = builder.CreateGEP(
            llvm::Type::getInt8Ty(context), current_ptr,
            llvm::ConstantInt::get(llvm::Type::getInt64Ty(context), 8),
            "va.next");

        // Store the updated pointer back
        builder.CreateStore(next_ptr, va_list_ptr);

        return result;
    }

    llvm::Value *Intrinsics::generate_va_arg_u64(const std::vector<llvm::Value *> &args)
    {
        // Same implementation as i64 - LLVM doesn't distinguish signed/unsigned at load level
        return generate_va_arg_i64(args);
    }

    llvm::Value *Intrinsics::generate_va_arg_f64(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 1)
        {
            report_error("va_arg_f64 requires exactly 1 argument (va_list pointer)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        llvm::Value *va_list_ptr = args[0];

        // Load the current pointer from the va_list
        llvm::Value *current_ptr = builder.CreateLoad(
            llvm::PointerType::get(context, 0), va_list_ptr, "va.ptr");

        // Load the double value from the current position
        llvm::Type *f64_type = llvm::Type::getDoubleTy(context);
        llvm::Value *result = builder.CreateLoad(f64_type, current_ptr, "va.arg.f64");

        // Advance the pointer by 8 bytes
        llvm::Value *next_ptr = builder.CreateGEP(
            llvm::Type::getInt8Ty(context), current_ptr,
            llvm::ConstantInt::get(llvm::Type::getInt64Ty(context), 8),
            "va.next");

        // Store the updated pointer back
        builder.CreateStore(next_ptr, va_list_ptr);

        return result;
    }

    llvm::Value *Intrinsics::generate_va_arg_ptr(const std::vector<llvm::Value *> &args)
    {
        if (args.size() != 1)
        {
            report_error("va_arg_ptr requires exactly 1 argument (va_list pointer)");
            return nullptr;
        }

        auto &builder = _context_manager.get_builder();
        auto &context = _context_manager.get_context();

        llvm::Value *va_list_ptr = args[0];

        // Load the current pointer from the va_list
        llvm::Value *current_ptr = builder.CreateLoad(
            llvm::PointerType::get(context, 0), va_list_ptr, "va.ptr");

        // Load the pointer value from the current position
        llvm::Type *ptr_type = llvm::PointerType::get(context, 0);
        llvm::Value *result = builder.CreateLoad(ptr_type, current_ptr, "va.arg.ptr");

        // Advance the pointer by 8 bytes (pointer size on 64-bit)
        llvm::Value *next_ptr = builder.CreateGEP(
            llvm::Type::getInt8Ty(context), current_ptr,
            llvm::ConstantInt::get(llvm::Type::getInt64Ty(context), 8),
            "va.next");

        // Store the updated pointer back
        builder.CreateStore(next_ptr, va_list_ptr);

        return result;
    }

} // namespace Cryo::Codegen
