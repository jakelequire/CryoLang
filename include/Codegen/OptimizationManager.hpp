#pragma once

#include "Codegen/TargetConfig.hpp"

#include <llvm/Passes/PassBuilder.h>
#include <llvm/Analysis/TargetTransformInfo.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Function.h>
#include <memory>
#include <vector>
#include <string>

namespace Cryo::Codegen
{
    /**
     * @brief LLVM Optimization Pass Manager for CryoLang
     * 
     * OptimizationManager provides a structured interface to LLVM's
     * optimization infrastructure. It handles:
     * 
     * - Configuration of optimization pipelines
     * - Target-specific optimizations
     * - Debug information preservation
     * - Custom CryoLang-specific optimization passes
     * - Performance profiling and metrics
     * 
     * The manager supports different optimization levels and can be
     * configured for specific compilation scenarios (debug, release, etc.).
     */
    class OptimizationManager
    {
    public:
        /**
         * @brief Optimization preset configurations
         */
        enum class OptimizationPreset
        {
            Debug,      ///< Debug build - minimal optimization, preserve debug info
            Release,    ///< Release build - standard optimizations
            MinSize,    ///< Optimize for minimal size
            MaxSpeed,   ///< Optimize for maximum speed
            Custom      ///< User-defined optimization pipeline
        };

        //===================================================================
        // Construction
        //===================================================================

        /**
         * @brief Construct optimization manager
         * @param target_config Target configuration for optimization
         */
        explicit OptimizationManager(const TargetConfig& target_config);

        ~OptimizationManager() = default;

        //===================================================================
        // Configuration
        //===================================================================

        /**
         * @brief Set optimization level (0-3)
         * @param level Optimization level
         */
        void set_optimization_level(int level);

        /**
         * @brief Get current optimization level
         */
        int get_optimization_level() const { return _optimization_level; }

        /**
         * @brief Set optimization preset
         * @param preset Predefined optimization configuration
         */
        void set_preset(OptimizationPreset preset);

        /**
         * @brief Get current preset
         */
        OptimizationPreset get_preset() const { return _preset; }

        /**
         * @brief Enable/disable debug information preservation
         * @param preserve Whether to preserve debug info during optimization
         */
        void set_preserve_debug_info(bool preserve);

        /**
         * @brief Enable/disable vectorization
         * @param enable Whether to enable auto-vectorization
         */
        void set_vectorization(bool enable);

        /**
         * @brief Enable/disable loop unrolling
         * @param enable Whether to enable loop unrolling
         */
        void set_loop_unrolling(bool enable);

        /**
         * @brief Set inlining threshold
         * @param threshold Cost threshold for function inlining
         */
        void set_inlining_threshold(int threshold);

        //===================================================================
        // Pass Pipeline Management
        //===================================================================

        /**
         * @brief Build optimization pipeline for module
         * @param module Target module
         * @return Module pass manager with configured pipeline
         */
        std::unique_ptr<llvm::ModulePassManager> build_module_pipeline(llvm::Module& module);

        /**
         * @brief Build function-level optimization pipeline
         * @return Function pass manager with configured pipeline
         */
        std::unique_ptr<llvm::FunctionPassManager> build_function_pipeline();

        /**
         * @brief Add custom pass to pipeline
         * @param pass_name Name of the pass to add
         */
        void add_custom_pass(const std::string& pass_name);

        /**
         * @brief Remove pass from pipeline
         * @param pass_name Name of the pass to remove
         */
        void remove_pass(const std::string& pass_name);

        //===================================================================
        // Optimization Execution
        //===================================================================

        /**
         * @brief Run optimization pipeline on module
         * @param module Module to optimize
         * @return true if optimization successful
         */
        bool optimize_module(llvm::Module& module);

        /**
         * @brief Run function-level optimizations
         * @param function Function to optimize
         * @return true if optimization successful
         */
        bool optimize_function(llvm::Function& function);

        /**
         * @brief Run pre-optimization analysis
         * @param module Module to analyze
         */
        void run_pre_optimization_analysis(llvm::Module& module);

        /**
         * @brief Run post-optimization verification
         * @param module Module to verify
         * @return true if module is valid after optimization
         */
        bool verify_optimized_module(llvm::Module& module);

        //===================================================================
        // CryoLang-Specific Optimizations
        //===================================================================

        /**
         * @brief Enable CryoLang-specific optimization passes
         * @param enable Whether to enable custom passes
         */
        void enable_cryo_optimizations(bool enable);

        /**
         * @brief Optimize CryoLang string operations
         * @param module Module containing string operations
         */
        void optimize_string_operations(llvm::Module& module);

        /**
         * @brief Optimize CryoLang array operations
         * @param module Module containing array operations
         */
        void optimize_array_operations(llvm::Module& module);

        /**
         * @brief Optimize CryoLang generic instantiations
         * @param module Module containing generic code
         */
        void optimize_generic_instantiations(llvm::Module& module);

        /**
         * @brief Optimize CryoLang enum pattern matching
         * @param module Module containing pattern match code
         */
        void optimize_pattern_matching(llvm::Module& module);

        //===================================================================
        // Analysis and Profiling
        //===================================================================

        /**
         * @brief Get optimization statistics
         * @return Map of statistic names to values
         */
        std::unordered_map<std::string, double> get_optimization_stats() const;

        /**
         * @brief Enable optimization timing
         * @param enable Whether to collect timing information
         */
        void enable_timing(bool enable);

        /**
         * @brief Get timing information for last optimization run
         * @return Map of pass names to execution times (seconds)
         */
        std::unordered_map<std::string, double> get_timing_info() const;

        /**
         * @brief Print optimization report
         * @param os Output stream
         */
        void print_optimization_report(std::ostream& os) const;

        //===================================================================
        // Target-Specific Optimizations
        //===================================================================

        /**
         * @brief Configure optimizations for specific target
         * @param target_triple Target triple
         */
        void configure_for_target(const std::string& target_triple);

        /**
         * @brief Enable target-specific instruction selection
         * @param enable Whether to enable target-specific optimizations
         */
        void enable_target_optimizations(bool enable);

        /**
         * @brief Set CPU-specific optimization features
         * @param cpu CPU name
         */
        void set_cpu_optimization_features(const std::string& cpu);

        //===================================================================
        // Error Handling
        //===================================================================

        /**
         * @brief Check if manager has errors
         */
        bool has_errors() const { return _has_errors; }

        /**
         * @brief Get last error message
         */
        const std::string& get_last_error() const { return _last_error; }

        /**
         * @brief Get all errors
         */
        const std::vector<std::string>& get_errors() const { return _errors; }

    private:
        //===================================================================
        // Private Implementation
        //===================================================================

        const TargetConfig& _target_config;
        
        // Configuration
        int _optimization_level;
        OptimizationPreset _preset;
        bool _preserve_debug_info;
        bool _vectorization_enabled;
        bool _loop_unrolling_enabled;
        int _inlining_threshold;
        bool _cryo_optimizations_enabled;
        bool _target_optimizations_enabled;
        bool _timing_enabled;

        // Pass configuration
        std::vector<std::string> _custom_passes;
        std::vector<std::string> _disabled_passes;

        // Statistics and timing
        mutable std::unordered_map<std::string, double> _optimization_stats;
        mutable std::unordered_map<std::string, double> _timing_info;

        // Error handling
        bool _has_errors;
        std::string _last_error;
        std::vector<std::string> _errors;

        //===================================================================
        // Private Methods
        //===================================================================

        /**
         * @brief Create pass builder with target configuration
         */
        std::unique_ptr<llvm::PassBuilder> create_pass_builder(llvm::Module& module);

        /**
         * @brief Configure debug optimization pipeline
         */
        void configure_debug_pipeline(llvm::PassBuilder& builder);

        /**
         * @brief Configure release optimization pipeline
         */
        void configure_release_pipeline(llvm::PassBuilder& builder);

        /**
         * @brief Configure size optimization pipeline
         */
        void configure_size_pipeline(llvm::PassBuilder& builder);

        /**
         * @brief Configure speed optimization pipeline
         */
        void configure_speed_pipeline(llvm::PassBuilder& builder);

        /**
         * @brief Add CryoLang-specific passes
         */
        void add_cryo_passes(llvm::PassBuilder& builder);

        /**
         * @brief Report optimization error
         */
        void report_error(const std::string& message);

        /**
         * @brief Update optimization statistics
         */
        void update_stats(const std::string& stat_name, double value);
    };

    //=======================================================================
    // Utility Functions
    //=======================================================================

    /**
     * @brief Get default optimization level for preset
     */
    int get_default_optimization_level(OptimizationManager::OptimizationPreset preset);

    /**
     * @brief Check if optimization level is valid
     */
    bool is_valid_optimization_level(int level);

} // namespace Cryo::Codegen