#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <memory>

namespace Cryo::Codegen
{
    /**
     * @brief Target-specific configuration for code generation
     *
     * TargetConfig encapsulates all target-specific settings needed for
     * LLVM code generation. This includes target triple, CPU features,
     * ABI conventions, optimization preferences, and platform-specific
     * code generation options.
     */
    class TargetConfig
    {
    public:
        /**
         * @brief Code model enumeration
         */
        enum class CodeModel
        {
            Small,  ///< Small code model (default)
            Medium, ///< Medium code model
            Large   ///< Large code model
        };

        /**
         * @brief Relocation model enumeration
         */
        enum class RelocationModel
        {
            Static,      ///< Static relocation
            PIC,         ///< Position Independent Code
            DynamicNoPie ///< Dynamic without PIE
        };

        /**
         * @brief Optimization level enumeration
         */
        enum class OptimizationLevel
        {
            None = 0,      ///< No optimization
            Less = 1,      ///< Minimal optimization
            Default = 2,   ///< Default optimization
            Aggressive = 3 ///< Aggressive optimization
        };

        //===================================================================
        // Construction
        //===================================================================

        /**
         * @brief Construct with default configuration for native target
         */
        TargetConfig();

        /**
         * @brief Construct with specific target triple
         * @param target_triple Target triple string
         */
        explicit TargetConfig(const std::string &target_triple);

        ~TargetConfig() = default;

        //===================================================================
        // Target Configuration
        //===================================================================

        /**
         * @brief Set target triple
         * @param triple Target triple (e.g., "x86_64-pc-windows-msvc")
         */
        void set_target_triple(const std::string &triple);

        /**
         * @brief Get target triple
         */
        const std::string &get_target_triple() const { return _target_triple; }

        /**
         * @brief Set CPU target
         * @param cpu CPU name (e.g., "x86-64", "generic")
         */
        void set_cpu(const std::string &cpu);

        /**
         * @brief Get CPU target
         */
        const std::string &get_cpu() const { return _cpu; }

        /**
         * @brief Set target features
         * @param features Comma-separated feature list
         */
        void set_features(const std::string &features);

        /**
         * @brief Get target features
         */
        const std::string &get_features() const { return _features; }

        /**
         * @brief Add individual feature
         * @param feature Feature to add (with + or - prefix)
         */
        void add_feature(const std::string &feature);

        //===================================================================
        // Code Generation Options
        //===================================================================

        /**
         * @brief Set code model
         */
        void set_code_model(CodeModel model) { _code_model = model; }
        CodeModel get_code_model() const { return _code_model; }

        /**
         * @brief Set relocation model
         */
        void set_relocation_model(RelocationModel model) { _relocation_model = model; }
        RelocationModel get_relocation_model() const { return _relocation_model; }

        /**
         * @brief Set optimization level
         */
        void set_optimization_level(OptimizationLevel level) { _optimization_level = level; }
        OptimizationLevel get_optimization_level() const { return _optimization_level; }

        /**
         * @brief Enable/disable debug information generation
         */
        void set_debug_info(bool enable) { _debug_info = enable; }
        bool get_debug_info() const { return _debug_info; }

        /**
         * @brief Enable/disable frame pointer
         */
        void set_frame_pointer(bool enable) { _frame_pointer = enable; }
        bool get_frame_pointer() const { return _frame_pointer; }

        /**
         * @brief Set stack protection level
         * @param level Protection level (0=none, 1=basic, 2=strong, 3=all)
         */
        void set_stack_protection(int level) { _stack_protection = level; }
        int get_stack_protection() const { return _stack_protection; }

        //===================================================================
        // Platform Detection
        //===================================================================

        /**
         * @brief Check if target is Windows
         */
        bool is_windows() const;

        /**
         * @brief Check if target is Linux
         */
        bool is_linux() const;

        /**
         * @brief Check if target is macOS
         */
        bool is_macos() const;

        /**
         * @brief Check if target is 64-bit
         */
        bool is_64bit() const;

        /**
         * @brief Check if target is WebAssembly
         */
        bool is_wasm() const;

        /**
         * @brief Check if target uses MSVC ABI
         */
        bool is_msvc() const;

        /**
         * @brief Get target architecture (x86_64, aarch64, etc.)
         */
        std::string get_architecture() const;

        /**
         * @brief Get target OS (windows, linux, macos, etc.)
         */
        std::string get_os() const;

        /**
         * @brief Get target environment (msvc, gnu, etc.)
         */
        std::string get_environment() const;

        //===================================================================
        // ABI and Calling Conventions
        //===================================================================

        /**
         * @brief Get default calling convention for target
         */
        std::string get_default_calling_convention() const;

        /**
         * @brief Get pointer size in bits
         */
        int get_pointer_size_bits() const;

        /**
         * @brief Get natural alignment for pointers
         */
        int get_pointer_alignment() const;

        /**
         * @brief Check if target supports thread-local storage
         */
        bool supports_tls() const;

        //===================================================================
        // Runtime Configuration
        //===================================================================

        /**
         * @brief Set runtime library paths
         */
        void set_runtime_paths(const std::vector<std::string> &paths) { _runtime_paths = paths; }
        void add_runtime_path(const std::string &path) { _runtime_paths.push_back(path); }
        const std::vector<std::string> &get_runtime_paths() const { return _runtime_paths; }

        /**
         * @brief Set system library paths
         */
        void set_system_lib_paths(const std::vector<std::string> &paths) { _system_lib_paths = paths; }
        void add_system_lib_path(const std::string &path) { _system_lib_paths.push_back(path); }
        const std::vector<std::string> &get_system_lib_paths() const { return _system_lib_paths; }

        /**
         * @brief Set static linking preference
         */
        void set_static_linking(bool static_link) { _static_linking = static_link; }
        bool get_static_linking() const { return _static_linking; }

        //===================================================================
        // Factory Methods
        //===================================================================

        /**
         * @brief Create configuration for native target
         */
        static std::unique_ptr<TargetConfig> create_native();

        /**
         * @brief Create configuration for Windows x64
         */
        static std::unique_ptr<TargetConfig> create_windows_x64();

        /**
         * @brief Create configuration for Linux x64
         */
        static std::unique_ptr<TargetConfig> create_linux_x64();

        /**
         * @brief Create configuration for macOS x64
         */
        static std::unique_ptr<TargetConfig> create_macos_x64();

        /**
         * @brief Create configuration for macOS ARM64
         */
        static std::unique_ptr<TargetConfig> create_macos_arm64();

        /**
         * @brief Create configuration for WebAssembly (WASM32)
         */
        static std::unique_ptr<TargetConfig> create_wasm32();

        /**
         * @brief Create configuration for WebAssembly (WASM64)
         */
        static std::unique_ptr<TargetConfig> create_wasm64();

        //===================================================================
        // Validation
        //===================================================================

        /**
         * @brief Validate configuration
         * @return true if configuration is valid
         */
        bool validate() const;

        /**
         * @brief Get validation errors
         */
        std::vector<std::string> get_validation_errors() const;

    private:
        //===================================================================
        // Private Implementation
        //===================================================================

        // Core target information
        std::string _target_triple;
        std::string _cpu;
        std::string _features;

        // Code generation options
        CodeModel _code_model;
        RelocationModel _relocation_model;
        OptimizationLevel _optimization_level;
        bool _debug_info;
        bool _frame_pointer;
        int _stack_protection;

        // Runtime configuration
        std::vector<std::string> _runtime_paths;
        std::vector<std::string> _system_lib_paths;
        bool _static_linking;

        //===================================================================
        // Private Methods
        //===================================================================

        /**
         * @brief Initialize default settings
         */
        void initialize_defaults();

        /**
         * @brief Parse target triple components
         */
        void parse_target_triple() const;

        /**
         * @brief Cached triple components
         */
        mutable std::string _cached_arch;
        mutable std::string _cached_os;
        mutable std::string _cached_env;
        mutable bool _triple_parsed = false;
    };

    //=======================================================================
    // Utility Functions
    //=======================================================================

    /**
     * @brief Detect native target triple
     */
    std::string detect_native_target_triple();

    /**
     * @brief Get default features for CPU
     */
    std::string get_default_cpu_features(const std::string &cpu);

    /**
     * @brief Check if target triple is supported by LLVM
     */
    bool is_target_triple_supported(const std::string &triple);

} // namespace Cryo::Codegen