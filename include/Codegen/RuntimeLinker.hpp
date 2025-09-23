#pragma once

#include "AST/SymbolTable.hpp"
#include "Codegen/LLVMContext.hpp"

#include <llvm/IR/Function.h>
#include <llvm/IR/Module.h>
#include <memory>
#include <unordered_map>
#include <vector>
#include <string>

namespace Cryo::Codegen
{
    /**
     * @brief Runtime Library Integration Manager
     * 
     * The RuntimeLinker manages the integration between generated LLVM IR
     * and the CryoLang runtime library. It handles:
     * 
     * - Declaration of runtime functions in LLVM IR
     * - Mapping CryoLang standard library calls to runtime functions
     * - Runtime type system integration
     * - Memory management function calls
     * - Library linking and symbol resolution
     * 
     * The linker works with the existing runtime library built in C,
     * providing seamless integration for standard library functionality.
     */
    class RuntimeLinker
    {
    public:
        /**
         * @brief Runtime function information
         */
        struct RuntimeFunction
        {
            std::string cryo_name;      ///< Name in CryoLang (e.g., "print")
            std::string runtime_name;   ///< Name in runtime library (e.g., "cryo_print")
            llvm::FunctionType* type;   ///< LLVM function type
            llvm::Function* declaration; ///< LLVM function declaration
            bool is_variadic;           ///< Whether function is variadic
            std::string description;    ///< Function description
        };

        //===================================================================
        // Construction
        //===================================================================

        /**
         * @brief Construct runtime linker
         * @param context_manager LLVM context manager
         * @param symbol_table Symbol table containing runtime functions
         */
        RuntimeLinker(LLVMContextManager& context_manager, Cryo::SymbolTable& symbol_table);

        ~RuntimeLinker() = default;

        //===================================================================
        // Initialization and Setup
        //===================================================================

        /**
         * @brief Initialize runtime function declarations
         * @return true if initialization successful
         */
        bool initialize();

        /**
         * @brief Add runtime library search path
         * @param path Path to runtime library
         */
        void add_runtime_path(const std::string& path);

        /**
         * @brief Set runtime library name (default: "cryoruntime")
         * @param name Library name without extension
         */
        void set_runtime_library_name(const std::string& name);

        //===================================================================
        // Function Declaration and Lookup
        //===================================================================

        /**
         * @brief Declare runtime function in current module
         * @param cryo_name CryoLang function name
         * @return LLVM function declaration or nullptr if not found
         */
        llvm::Function* declare_runtime_function(const std::string& cryo_name);

        /**
         * @brief Get runtime function by CryoLang name
         * @param cryo_name CryoLang function name
         * @return Runtime function info or nullptr
         */
        const RuntimeFunction* get_runtime_function(const std::string& cryo_name);

        /**
         * @brief Check if function is a runtime function
         * @param name Function name
         * @return true if function is from runtime library
         */
        bool is_runtime_function(const std::string& name);

        /**
         * @brief Get all declared runtime functions
         * @return Vector of runtime functions
         */
        std::vector<llvm::Function*> get_declared_functions();

        //===================================================================
        // Standard Library Integration
        //===================================================================

        /**
         * @brief Declare I/O functions (print, println, read_line, etc.)
         * @return Number of functions declared
         */
        size_t declare_io_functions();

        /**
         * @brief Declare string manipulation functions
         * @return Number of functions declared
         */
        size_t declare_string_functions();

        /**
         * @brief Declare math functions
         * @return Number of functions declared
         */
        size_t declare_math_functions();

        /**
         * @brief Declare memory management functions
         * @return Number of functions declared
         */
        size_t declare_memory_functions();

        /**
         * @brief Declare system functions
         * @return Number of functions declared
         */
        size_t declare_system_functions();

        /**
         * @brief Declare collection functions
         * @return Number of functions declared
         */
        size_t declare_collection_functions();

        /**
         * @brief Declare all standard library functions
         * @return Number of functions declared
         */
        size_t declare_all_functions();

        //===================================================================
        // Memory Management Integration
        //===================================================================

        /**
         * @brief Get malloc function for heap allocation
         * @return LLVM malloc function
         */
        llvm::Function* get_malloc_function();

        /**
         * @brief Get free function for heap deallocation
         * @return LLVM free function
         */
        llvm::Function* get_free_function();

        /**
         * @brief Get realloc function for heap reallocation
         * @return LLVM realloc function
         */
        llvm::Function* get_realloc_function();

        /**
         * @brief Generate heap allocation call
         * @param size Size to allocate
         * @param name Optional name for instruction
         * @return LLVM value representing allocated pointer
         */
        llvm::Value* generate_malloc_call(llvm::Value* size, const std::string& name = "");

        /**
         * @brief Generate heap deallocation call
         * @param ptr Pointer to deallocate
         */
        void generate_free_call(llvm::Value* ptr);

        //===================================================================
        // String Runtime Integration
        //===================================================================

        /**
         * @brief Create string literal with runtime integration
         * @param value String value
         * @return LLVM global string constant
         */
        llvm::Constant* create_string_literal(const std::string& value);

        /**
         * @brief Generate string concatenation call
         * @param left Left operand
         * @param right Right operand
         * @return Result value
         */
        llvm::Value* generate_string_concat(llvm::Value* left, llvm::Value* right);

        /**
         * @brief Generate string comparison call
         * @param left Left operand
         * @param right Right operand
         * @return Comparison result (i32)
         */
        llvm::Value* generate_string_compare(llvm::Value* left, llvm::Value* right);

        //===================================================================
        // Linking and Library Management
        //===================================================================

        /**
         * @brief Generate linker arguments for runtime library
         * @param target_triple Target triple for linking
         * @return Vector of linker arguments
         */
        std::vector<std::string> get_linker_args(const std::string& target_triple);

        /**
         * @brief Get library dependencies
         * @return Vector of library names to link against
         */
        std::vector<std::string> get_library_dependencies();

        /**
         * @brief Set static linking mode
         * @param static_link Whether to link statically
         */
        void set_static_linking(bool static_link);

        /**
         * @brief Get runtime library path
         * @return Path to runtime library
         */
        std::string get_runtime_library_path();

        //===================================================================
        // Error Handling
        //===================================================================

        /**
         * @brief Check if linker has errors
         */
        bool has_errors() const { return _has_errors; }

        /**
         * @brief Get last error message
         */
        const std::string& get_last_error() const { return _last_error; }

    private:
        //===================================================================
        // Private Implementation
        //===================================================================

        LLVMContextManager& _context_manager;
        Cryo::SymbolTable& _symbol_table;

        // Runtime function registry
        std::unordered_map<std::string, RuntimeFunction> _runtime_functions;
        std::unordered_map<std::string, llvm::Function*> _declared_functions;

        // Configuration
        std::vector<std::string> _runtime_paths;
        std::string _runtime_library_name;
        bool _static_linking;
        bool _initialized;

        // Error state
        bool _has_errors;
        std::string _last_error;

        //===================================================================
        // Private Methods
        //===================================================================

        /**
         * @brief Register runtime function
         * @param cryo_name CryoLang name
         * @param runtime_name Runtime library name
         * @param return_type Return type
         * @param param_types Parameter types
         * @param is_variadic Whether function is variadic
         * @param description Function description
         */
        void register_runtime_function(
            const std::string& cryo_name,
            const std::string& runtime_name,
            llvm::Type* return_type,
            const std::vector<llvm::Type*>& param_types,
            bool is_variadic = false,
            const std::string& description = ""
        );

        /**
         * @brief Create function type from signature
         */
        llvm::FunctionType* create_function_type(
            llvm::Type* return_type,
            const std::vector<llvm::Type*>& param_types,
            bool is_variadic = false
        );

        /**
         * @brief Initialize I/O function registry
         */
        void initialize_io_functions();

        /**
         * @brief Initialize string function registry
         */
        void initialize_string_functions();

        /**
         * @brief Initialize math function registry
         */
        void initialize_math_functions();

        /**
         * @brief Initialize memory function registry
         */
        void initialize_memory_functions();

        /**
         * @brief Initialize system function registry
         */
        void initialize_system_functions();

        /**
         * @brief Initialize collection function registry
         */
        void initialize_collection_functions();

        /**
         * @brief Report error
         */
        void report_error(const std::string& message);

        /**
         * @brief Get basic LLVM types for runtime functions
         */
        llvm::Type* get_i8_type();
        llvm::Type* get_i32_type();
        llvm::Type* get_i64_type();
        llvm::Type* get_f64_type();
        llvm::Type* get_void_type();
        llvm::PointerType* get_i8_ptr_type();
    };

    //=======================================================================
    // Utility Functions
    //=======================================================================

    /**
     * @brief Get default runtime library paths for platform
     * @return Vector of default search paths
     */
    std::vector<std::string> get_default_runtime_paths();

    /**
     * @brief Check if runtime library exists at path
     * @param path Path to check
     * @param library_name Library name
     * @return true if library exists
     */
    bool runtime_library_exists(const std::string& path, const std::string& library_name);

} // namespace Cryo::Codegen