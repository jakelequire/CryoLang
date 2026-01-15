#pragma once

#include "Types/SymbolTable.hpp"
#include <llvm/IR/Function.h>
#include <llvm/IR/Module.h>
#include <memory>
#include <unordered_map>
#include <vector>
#include <string>

namespace Cryo::Linker
{
    /**
     * @brief CryoLang Linker and Object Manager
     *
     * The CryoLinker manages the linking process for CryoLang programs. It handles:
     *
     * - Runtime library integration and symbol resolution
     * - Custom object file linking
     * - Multiple output format generation (executable, shared library, static library)
     * - Cross-platform linking strategies
     * - Symbol table management for linking
     * - External library dependency resolution
     *
     * The linker is designed to be modular and work independently from
     * code generation, allowing for flexible compilation workflows.
     */
    class CryoLinker
    {
    public:
        /**
         * @brief Linking target types
         */
        enum class LinkTarget
        {
            Executable,    ///< Generate executable binary
            SharedLibrary, ///< Generate shared/dynamic library (.so/.dll)
            StaticLibrary, ///< Generate static library (.a/.lib)
            ObjectFile     ///< Generate object file only (.o/.obj)
        };

        /**
         * @brief Linking mode
         */
        enum class LinkMode
        {
            Static,  ///< Static linking (embed all dependencies)
            Dynamic, ///< Dynamic linking (link against shared libraries)
            Mixed    ///< Mixed mode (static for some, dynamic for others)
        };

        /**
         * @brief Runtime function information
         */
        struct RuntimeFunction
        {
            std::string cryo_name;       ///< Name in CryoLang (e.g., "print")
            std::string runtime_name;    ///< Name in runtime library (e.g., "cryo_print")
            llvm::FunctionType *type;    ///< LLVM function type
            llvm::Function *declaration; ///< LLVM function declaration
            bool is_variadic;            ///< Whether function is variadic
            std::string description;     ///< Function description
        };

        //===================================================================
        // Construction and Configuration
        //===================================================================

        /**
         * @brief Construct CryoLinker
         * @param symbol_table Symbol table containing program symbols
         */
        explicit CryoLinker(Cryo::SymbolTable &symbol_table);

        ~CryoLinker() = default;

        // No copy/move for now - manage LLVM resources carefully
        CryoLinker(const CryoLinker &) = delete;
        CryoLinker &operator=(const CryoLinker &) = delete;
        CryoLinker(CryoLinker &&) = delete;
        CryoLinker &operator=(CryoLinker &&) = delete;

        //===================================================================
        // Primary Linking Interface
        //===================================================================

        /**
         * @brief Link LLVM modules and generate output
         * @param modules Vector of LLVM modules to link
         * @param output_path Output file path
         * @param target Link target type
         * @return Success status
         */
        bool link_modules(
            const std::vector<llvm::Module *> &modules,
            const std::string &output_path,
            LinkTarget target = LinkTarget::Executable);

        /**
         * @brief Add object file to link
         * @param object_path Path to object file
         */
        void add_object_file(const std::string &object_path);

        /**
         * @brief Add library to link against
         * @param library_name Library name (without lib prefix or extension)
         * @param is_static Whether to link statically
         */
        void add_library(const std::string &library_name, bool is_static = false);

        /**
         * @brief Add library search path
         * @param path Path to search for libraries
         */
        void add_library_path(const std::string &path);

        //===================================================================
        // Runtime Library Integration
        //===================================================================

        /**
         * @brief Initialize runtime library integration
         * @return Success status
         */
        bool initialize_runtime();

        /**
         * @brief Add runtime library search path
         * @param path Path to runtime library
         */
        void add_runtime_path(const std::string &path);

        /**
         * @brief Set runtime library name (default: "cryoruntime")
         * @param name Library name without extension
         */
        void set_runtime_library_name(const std::string &name);

        /**
         * @brief Enable/disable runtime library linking
         * @param enable Whether to link runtime library
         */
        void enable_runtime_linking(bool enable);

        /**
         * @brief Declare runtime function in module
         * @param module LLVM module to declare function in
         * @param cryo_name CryoLang function name
         * @return LLVM function declaration or nullptr if not found
         */
        llvm::Function *declare_runtime_function(llvm::Module *module, const std::string &cryo_name);

        /**
         * @brief Get runtime function by CryoLang name
         * @param cryo_name CryoLang function name
         * @return Runtime function info or nullptr
         */
        const RuntimeFunction *get_runtime_function(const std::string &cryo_name);

        /**
         * @brief Check if function is a runtime function
         * @param name Function name
         * @return true if function is from runtime library
         */
        bool is_runtime_function(const std::string &name);

        //===================================================================
        // Configuration
        //===================================================================

        /**
         * @brief Set linking mode
         * @param mode Linking mode (static/dynamic/mixed)
         */
        void set_link_mode(LinkMode mode);

        /**
         * @brief Set target triple for linking
         * @param triple Target triple (e.g., "x86_64-pc-linux-gnu")
         */
        void set_target_triple(const std::string &triple);

        /**
         * @brief Enable/disable debug symbol generation
         * @param enable Whether to include debug symbols
         */
        void set_debug_symbols(bool enable);

        /**
         * @brief Set optimization level for linking
         * @param level Optimization level (0-3)
         */
        void set_optimization_level(int level);

        /**
         * @brief Add linker flag
         * @param flag Linker flag (e.g., "-pie", "-static")
         */
        void add_linker_flag(const std::string &flag);

        /**
         * @brief Set entry point function name
         * @param entry_point Entry point name (default: "main")
         */
        void set_entry_point(const std::string &entry_point);

        //===================================================================
        // Output Format Support
        //===================================================================

        /**
         * @brief Generate executable
         * @param module LLVM module to link
         * @param output_path Output executable path
         * @return Success status
         */
        bool generate_executable(llvm::Module *module, const std::string &output_path);

        /**
         * @brief Generate shared library
         * @param module LLVM module to link
         * @param output_path Output shared library path
         * @return Success status
         */
        bool generate_shared_library(llvm::Module *module, const std::string &output_path);

        /**
         * @brief Generate static library
         * @param module LLVM module to link
         * @param output_path Output static library path
         * @return Success status
         */
        bool generate_static_library(llvm::Module *module, const std::string &output_path);

        /**
         * @brief Generate object file
         * @param module LLVM module to compile
         * @param output_path Output object file path
         * @return Success status
         */
        bool generate_object_file(llvm::Module *module, const std::string &output_path);

        //===================================================================
        // Cross-Platform Support
        //===================================================================

        /**
         * @brief Get platform-specific linker executable
         * @return Path to system linker
         */
        std::string get_system_linker();

        /**
         * @brief Get platform-specific library extension
         * @param is_shared Whether to get shared library extension
         * @return Library file extension
         */
        std::string get_library_extension(bool is_shared = true) const;

        /**
         * @brief Get platform-specific executable extension
         * @return Executable file extension
         */
        std::string get_executable_extension();

        //===================================================================
        // Diagnostics and Debugging
        //===================================================================

        /**
         * @brief Print linking information
         * @param os Output stream
         */
        void print_link_info(std::ostream &os) const;

        /**
         * @brief Get linking statistics
         * @param os Output stream for stats
         */
        void print_stats(std::ostream &os) const;

        /**
         * @brief Check if linker has errors
         */
        bool has_errors() const { return _has_errors; }

        /**
         * @brief Get last error message
         */
        const std::string &get_last_error() const { return _last_error; }

        /**
         * @brief Get all generated linker arguments
         * @return Vector of linker arguments
         */
        std::vector<std::string> get_linker_args() const;

    private:
        //===================================================================
        // Private Implementation
        //===================================================================

        Cryo::SymbolTable &_symbol_table;

        // Runtime function registry
        std::unordered_map<std::string, RuntimeFunction> _runtime_functions;

        // Linking configuration
        LinkMode _link_mode;
        std::string _target_triple;
        std::string _entry_point;
        bool _debug_symbols;
        int _optimization_level;
        bool _runtime_linking_enabled;

        // File and library management
        std::vector<std::string> _object_files;
        std::vector<std::pair<std::string, bool>> _libraries; // name, is_static
        std::vector<std::string> _library_paths;
        std::vector<std::string> _runtime_paths;
        std::string _runtime_library_name;
        std::vector<std::string> _linker_flags;

        // Error state
        bool _has_errors;
        std::string _last_error;
        bool _initialized;

        // Statistics
        size_t _modules_linked;
        size_t _objects_linked;
        size_t _libraries_linked;

        //===================================================================
        // Private Methods
        //===================================================================

        /**
         * @brief Initialize runtime function registry
         */
        bool initialize_runtime_functions();

        /**
         * @brief Register runtime function
         */
        void register_runtime_function(
            const std::string &cryo_name,
            const std::string &runtime_name,
            llvm::Type *return_type,
            const std::vector<llvm::Type *> &param_types,
            bool is_variadic = false,
            const std::string &description = "");

        /**
         * @brief Execute system linker command
         */
        bool execute_linker_command(const std::vector<std::string> &args);

        /**
         * @brief Execute ar command for creating static libraries
         */
        bool execute_ar_command(const std::vector<std::string> &args);

        /**
         * @brief Build linker command arguments
         */
        std::vector<std::string> build_linker_args(
            const std::string &output_path,
            LinkTarget target,
            const std::vector<std::string> &input_files);

        /**
         * @brief Find runtime library path
         */
        std::string find_runtime_library() const;

        /**
         * @brief Report linker error
         */
        void report_error(const std::string &message);

        /**
         * @brief Clear error state
         */
        void clear_errors();

        /**
         * @brief Get basic LLVM types for runtime functions
         */
        llvm::Type *get_i8_type();
        llvm::Type *get_i32_type();
        llvm::Type *get_i64_type();
        llvm::Type *get_f64_type();
        llvm::Type *get_void_type();
        llvm::PointerType *get_i8_ptr_type();
    };

    //=======================================================================
    // Factory Functions
    //=======================================================================

    /**
     * @brief Create a CryoLinker with default configuration
     * @param symbol_table Reference to symbol table
     * @return Unique pointer to CryoLinker instance
     */
    std::unique_ptr<CryoLinker> create_default_linker(Cryo::SymbolTable &symbol_table);

    /**
     * @brief Create a CryoLinker for specific target
     * @param target_triple Target triple string
     * @param symbol_table Reference to symbol table
     * @return Unique pointer to CryoLinker instance
     */
    std::unique_ptr<CryoLinker> create_target_linker(
        const std::string &target_triple,
        Cryo::SymbolTable &symbol_table);

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
    bool runtime_library_exists(const std::string &path, const std::string &library_name);

} // namespace Cryo::Linker