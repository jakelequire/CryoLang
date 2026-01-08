#ifndef CRYO_CODEGEN_INTRINSICS_HPP
#define CRYO_CODEGEN_INTRINSICS_HPP

#include <llvm/IR/Value.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <string>
#include <vector>

namespace Cryo
{
    class CallExpressionNode;
    class DiagnosticManager;  // Forward declaration
}

namespace Cryo::Codegen
{
    class LLVMContextManager;

    /**
     * @brief Handles generation of intrinsic function implementations
     * 
     * This class provides LLVM IR generation for all CryoLang intrinsic functions,
     * replacing the need for C runtime object files with direct LLVM implementations.
     */
    class Intrinsics
    {
    public:
        explicit Intrinsics(LLVMContextManager& context_manager, Cryo::DiagnosticManager* gdm = nullptr);
        ~Intrinsics() = default;

        // Main entry point for intrinsic call generation
        llvm::Value* generate_intrinsic_call(Cryo::CallExpressionNode* node, 
                                           const std::string& intrinsic_name,
                                           const std::vector<llvm::Value*>& args);

        // Memory allocation intrinsics
        llvm::Value* generate_malloc(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_calloc(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_realloc(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_free(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_aligned_alloc(const std::vector<llvm::Value*>& args);

        // Memory operations intrinsics
        llvm::Value* generate_memcpy(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_memmove(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_memset(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_memcmp(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_memchr(const std::vector<llvm::Value*>& args);

        // String intrinsics
        llvm::Value* generate_strlen(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_strcmp(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_strncmp(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_strcpy(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_strncpy(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_strcat(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_strchr(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_strrchr(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_strstr(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_strdup(const std::vector<llvm::Value*>& args);

        // I/O intrinsics
        llvm::Value* generate_printf(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_snprintf(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_getchar(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_putchar(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_puts(const std::vector<llvm::Value*>& args);

        // File I/O intrinsics
        llvm::Value* generate_fopen(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_fclose(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_fread(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_fwrite(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_fseek(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_ftell(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_fflush(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_feof(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_ferror(const std::vector<llvm::Value*>& args);

        // Low-level file descriptor I/O
        llvm::Value* generate_read(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_write(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_open(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_close(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_lseek(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_dup(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_dup2(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_pipe(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_fcntl(const std::vector<llvm::Value*>& args);

        // Filesystem intrinsics
        llvm::Value* generate_stat(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_fstat(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_lstat(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_access(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_mkdir(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_rmdir(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_unlink(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_rename(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_symlink(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_readlink(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_truncate(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_ftruncate(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_chmod(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_chown(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_getcwd(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_chdir(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_opendir(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_readdir(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_closedir(const std::vector<llvm::Value*>& args);

        // Process intrinsics
        llvm::Value* generate_exit(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_abort(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_fork(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_execvp(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_wait(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_waitpid(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_getpid(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_getppid(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_getuid(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_getgid(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_geteuid(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_getegid(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_kill(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_raise(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_signal(const std::vector<llvm::Value*>& args);

        // Math intrinsics
        llvm::Value* generate_sqrt(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_sqrtf(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_pow(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_powf(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_exp(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_expf(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_exp2(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_expm1(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_log(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_logf(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_log10(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_log2(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_log1p(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_sin(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_sinf(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_cos(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_cosf(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_tan(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_tanf(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_asin(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_acos(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_atan(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_atan2(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_sinh(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_cosh(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_tanh(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_asinh(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_acosh(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_atanh(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_cbrt(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_hypot(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_fabs(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_fabsf(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_floor(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_floorf(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_ceil(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_ceilf(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_round(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_roundf(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_trunc(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_fmod(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_remainder(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_fmin(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_fmax(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_fma(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_copysign(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_nextafter(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_frexp(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_ldexp(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_modf(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_erf(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_erfc(const std::vector<llvm::Value*>& args);

        // Network intrinsics
        llvm::Value* generate_socket(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_bind(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_listen(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_accept(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_connect(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_send(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_recv(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_sendto(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_recvfrom(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_shutdown(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_setsockopt(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_getsockopt(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_getsockname(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_getpeername(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_poll(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_htons(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_ntohs(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_htonl(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_ntohl(const std::vector<llvm::Value*>& args);

        // Time intrinsics
        llvm::Value* generate_time(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_gettimeofday(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_clock_gettime(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_nanosleep(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_sleep(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_usleep(const std::vector<llvm::Value*>& args);

        // Threading intrinsics (stubs for now - not fully implemented)
        // pthread, atomic, etc. would require much more complex implementation

        // Legacy/old intrinsics (for compatibility)
        llvm::Value* generate_mmap(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_munmap(const std::vector<llvm::Value*>& args);

        // Pointer arithmetic intrinsics
        llvm::Value* generate_ptr_add(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_ptr_sub(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_ptr_diff(const std::vector<llvm::Value*>& args);

        // System call intrinsics
        llvm::Value* generate_syscall_write(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_syscall_read(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_syscall_exit(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_syscall_open(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_syscall_close(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_syscall_lseek(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_syscall_unlink(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_syscall_mkdir(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_syscall_rmdir(const std::vector<llvm::Value*>& args);

        // String conversion intrinsics (legacy)
        llvm::Value* generate_sprintf(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_fprintf(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_panic(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_float32_to_string(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_float64_to_string(const std::vector<llvm::Value*>& args);

        // Type conversion intrinsics
        llvm::Value* generate_integer_extension(const std::vector<llvm::Value*>& args, 
                                               unsigned source_bits, unsigned target_bits, bool is_signed);
        llvm::Value* generate_integer_truncation(const std::vector<llvm::Value*>& args,
                                                unsigned source_bits, unsigned target_bits);
        llvm::Value* generate_sign_conversion(const std::vector<llvm::Value*>& args, unsigned bit_width);
        llvm::Value* generate_checked_conversion(const std::vector<llvm::Value*>& args,
                                                unsigned source_bits, unsigned target_bits,
                                                bool source_signed, bool target_signed);

        // Legacy process intrinsics
        llvm::Value* generate_maloc(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_mfree(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_msize(const std::vector<llvm::Value*>& args);

        // Error handling
        bool has_errors() const { return _has_errors; }
        const std::string& get_last_error() const { return _last_error; }
        void clear_errors();

    private:
        LLVMContextManager& _context_manager;
        Cryo::DiagnosticManager* _gdm;
        bool _has_errors;
        std::string _last_error;

        // Helper methods
        void report_error(const std::string& message);
        void report_unimplemented_intrinsic(const std::string& intrinsic_name, 
                                           Cryo::CallExpressionNode* node = nullptr);
        llvm::Value* create_syscall(llvm::Value* syscall_num, 
                                  const std::vector<llvm::Value*>& args);
        llvm::Value* ensure_type(llvm::Value* value, llvm::Type* target_type, 
                               const std::string& name = "");
        llvm::Function* get_or_create_libc_function(const std::string& name,
                                                  llvm::FunctionType* type);
    };

} // namespace Cryo::Codegen

#endif // CRYO_CODEGEN_INTRINSICS_HPP