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

        // Memory management intrinsics
        llvm::Value* generate_malloc(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_free(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_realloc(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_calloc(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_mmap(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_munmap(const std::vector<llvm::Value*>& args);

        // Memory operations intrinsics
        llvm::Value* generate_memcpy(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_memset(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_memcmp(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_memmove(const std::vector<llvm::Value*>& args);

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

        // String operations intrinsics
        llvm::Value* generate_strlen(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_strcmp(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_strcpy(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_strcat(const std::vector<llvm::Value*>& args);

        // I/O operations intrinsics
        llvm::Value* generate_printf(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_sprintf(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_fprintf(const std::vector<llvm::Value*>& args);

        // String conversion intrinsics
        llvm::Value* generate_float32_to_string(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_float64_to_string(const std::vector<llvm::Value*>& args);

        // Math operations intrinsics
        llvm::Value* generate_sqrt(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_pow(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_sin(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_cos(const std::vector<llvm::Value*>& args);

        // Type conversion intrinsics
        llvm::Value* generate_integer_extension(const std::vector<llvm::Value*>& args, 
                                               unsigned source_bits, unsigned target_bits, bool is_signed);
        llvm::Value* generate_integer_truncation(const std::vector<llvm::Value*>& args,
                                                unsigned source_bits, unsigned target_bits);
        llvm::Value* generate_sign_conversion(const std::vector<llvm::Value*>& args, unsigned bit_width);
        llvm::Value* generate_checked_conversion(const std::vector<llvm::Value*>& args,
                                                unsigned source_bits, unsigned target_bits,
                                                bool source_signed, bool target_signed);

        // Process management intrinsics
        llvm::Value* generate_getpid(const std::vector<llvm::Value*>& args);
        llvm::Value* generate_fork(const std::vector<llvm::Value*>& args);

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