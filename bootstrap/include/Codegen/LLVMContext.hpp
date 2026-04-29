#pragma once

#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/DIBuilder.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Support/TargetSelect.h>
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>

// Forward declaration
namespace llvm { class DICompileUnit; }

namespace Cryo::Codegen
{
    /**
     * @brief LLVM Context and Resource Manager for CryoLang
     * 
     * Manages LLVM context, modules, and provides centralized access to
     * LLVM infrastructure components. Handles initialization of LLVM
     * targets and ensures proper resource cleanup.
     * 
     * This class encapsulates all LLVM-specific setup and teardown,
     * providing a clean interface for the rest of the codegen system.
     */
    class LLVMContextManager
    {
    public:
        //===================================================================
        // Construction & Initialization
        //===================================================================

        /**
         * @brief Construct LLVM context manager
         * @param module_name Name for the LLVM module
         */
        explicit LLVMContextManager(const std::string& module_name);

        /**
         * @brief Destructor - ensures proper LLVM cleanup
         */
        ~LLVMContextManager();

        // No copy/move - LLVM resources are not trivially copyable
        LLVMContextManager(const LLVMContextManager&) = delete;
        LLVMContextManager& operator=(const LLVMContextManager&) = delete;
        LLVMContextManager(LLVMContextManager&&) = delete;
        LLVMContextManager& operator=(LLVMContextManager&&) = delete;

        //===================================================================
        // Initialization & Configuration
        //===================================================================

        /**
         * @brief Initialize LLVM targets and components
         * @return true if initialization successful
         */
        bool initialize();

        /**
         * @brief Set target triple for code generation
         * @param triple Target triple (e.g., "x86_64-pc-windows-msvc")
         * @return true if target is supported
         */
        bool set_target_triple(const std::string& triple);

        /**
         * @brief Set CPU target
         * @param cpu CPU target name (e.g., "x86-64", "generic")
         */
        void set_cpu_target(const std::string& cpu);

        /**
         * @brief Set target features
         * @param features Comma-separated list of features
         */
        void set_target_features(const std::string& features);

        /**
         * @brief Create target machine with current configuration
         * @return true if target machine created successfully
         */
        bool create_target_machine();

        //===================================================================
        // Component Access
        //===================================================================

        /**
         * @brief Get LLVM context
         */
        llvm::LLVMContext& get_context() { return *_context; }
        const llvm::LLVMContext& get_context() const { return *_context; }

        /**
         * @brief Get IR builder
         */
        llvm::IRBuilder<>& get_builder() { return *_builder; }
        const llvm::IRBuilder<>& get_builder() const { return *_builder; }

        /**
         * @brief Get target machine (may be null)
         */
        llvm::TargetMachine* get_target_machine() const { return _target_machine.get(); }

        /**
         * @brief Get debug info builder (created on demand)
         */
        llvm::DIBuilder* get_debug_builder();

        //===================================================================
        // Module Management
        //===================================================================

        /**
         * @brief Create a new module with given name (multi-module support)
         * @param name Module name
         * @return Pointer to new module (does not overwrite existing modules)
         */
        llvm::Module* create_module(const std::string& name);

        /**
         * @brief Get module by name
         * @param name Module name  
         * @return Pointer to module or nullptr if not found
         */
        llvm::Module* get_module(const std::string& name) const;

        /**
         * @brief Get active module (current module for IR generation)
         * @return Pointer to active module
         */
        llvm::Module* get_module() const { return _active_module; }

        /**
         * @brief Set active module for IR generation
         * @param name Module name to make active
         * @return true if module exists and was set as active
         */
        bool set_active_module(const std::string& name);

        /**
         * @brief Get main module (the primary compilation target)
         * @return Pointer to main module or nullptr if not set
         */
        llvm::Module* get_main_module() const { return _main_module; }

        /**
         * @brief Get main module name
         * @return Main module name or empty string if not set
         */
        const std::string& get_main_module_name() const { return _main_module_name; }

        /**
         * @brief Set main module (the primary compilation target)
         * @param name Module name to set as main
         * @return true if module exists and was set as main
         */
        bool set_main_module(const std::string& name);

        /**
         * @brief Check if module exists
         * @param name Module name
         * @return true if module exists
         */
        bool has_module(const std::string& name) const;

        /**
         * @brief Get list of all module names
         * @return Vector of module names
         */
        std::vector<std::string> get_module_names() const;

        /**
         * @brief Remove module (careful - this will invalidate pointers)
         * @param name Module name to remove
         * @return true if module was removed
         */
        bool remove_module(const std::string& name);

        /**
         * @brief Set module metadata (source file, compile flags, etc.)
         * @param source_file Original source file path
         * @param compile_flags Compilation flags used
         * @param module_name Specific module name (defaults to active module)
         */
        void set_module_metadata(const std::string& source_file, const std::string& compile_flags, 
                                const std::string& module_name = "");

        /**
         * @brief Setup binary metadata for CryoLang identification
         * @param source_file Original source file path
         * @param module_name Specific module name (defaults to active module)
         * @return DICompileUnit for further debug info generation
         */
        llvm::DICompileUnit* setup_binary_metadata(const std::string& source_file, 
                                                   const std::string& module_name = "");

        /**
         * @brief Verify module integrity
         * @param module_name Specific module name (defaults to active module)
         * @return true if module is valid
         */
        bool verify_module(const std::string& module_name = "") const;

        /**
         * @brief Verify module and get detailed error information
         * @param module_name Specific module name (defaults to active module)
         * @param error_details Output parameter for detailed error information
         * @return true if module is valid
         */
        bool verify_module_with_details(const std::string& module_name, std::string& error_details) const;

        /**
         * @brief Print module IR to stream
         * @param os Output stream
         * @param module_name Specific module name (defaults to active module)
         */
        void print_module(std::ostream& os, const std::string& module_name = "") const;

        //===================================================================
        // Target Information
        //===================================================================

        /**
         * @brief Get target triple string
         */
        const std::string& get_target_triple() const { return _target_triple; }

        /**
         * @brief Get CPU target string
         */
        const std::string& get_cpu_target() const { return _cpu_target; }

        /**
         * @brief Get target features string
         */
        const std::string& get_target_features() const { return _target_features; }

        /**
         * @brief Check if target is 64-bit
         */
        bool is_64bit() const;

        /**
         * @brief Get pointer size in bytes for target
         */
        size_t get_pointer_size() const;

        /**
         * @brief Get natural alignment for type
         */
        size_t get_type_alignment(llvm::Type* type) const;

        //===================================================================
        // Error Handling
        //===================================================================

        /**
         * @brief Check if context manager has errors
         */
        bool has_errors() const { return _has_errors; }

        /**
         * @brief Get last error message
         */
        const std::string& get_last_error() const { return _last_error; }

        /**
         * @brief Clear error state
         */
        void clear_errors();

    private:
        //===================================================================
        // Private Implementation
        //===================================================================

        // LLVM Core Components
        std::unique_ptr<llvm::LLVMContext> _context;
        std::unique_ptr<llvm::IRBuilder<>> _builder;
        std::unique_ptr<llvm::TargetMachine> _target_machine;
        std::unique_ptr<llvm::DIBuilder> _debug_builder;

        // Multi-Module Management
        std::unordered_map<std::string, std::unique_ptr<llvm::Module>> _modules;
        llvm::Module* _active_module;  // Current module for IR generation
        llvm::Module* _main_module;    // Primary compilation target module
        std::string _active_module_name;
        std::string _main_module_name;

        // Configuration
        std::string _module_name;      // Default/initial module name
        std::string _target_triple;
        std::string _cpu_target;
        std::string _target_features;

        // State
        bool _initialized;
        bool _target_machine_created;
        bool _has_errors;
        std::string _last_error;

        //===================================================================
        // Private Methods
        //===================================================================

        /**
         * @brief Initialize LLVM native target
         */
        bool initialize_native_target();

        /**
         * @brief Initialize all targets (for cross-compilation)
         */
        void initialize_all_targets();

        /**
         * @brief Detect native target triple
         */
        std::string detect_native_target_triple();

        /**
         * @brief Set error state
         */
        void set_error(const std::string& message);
    };

    //=======================================================================
    // Utility Functions
    //=======================================================================

    /**
     * @brief Initialize LLVM for CryoLang (call once per process)
     * @return true if initialization successful
     */
    bool initialize_llvm_for_cryo();

    /**
     * @brief Shutdown LLVM (cleanup - call at process exit)
     */
    void shutdown_llvm_for_cryo();

    /**
     * @brief Get supported target triples
     */
    std::vector<std::string> get_supported_targets();

    /**
     * @brief Check if target triple is supported
     */
    bool is_target_supported(const std::string& triple);

} // namespace Cryo::Codegen