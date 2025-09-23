#pragma once

#include "AST/ASTNode.hpp"
#include "AST/ASTContext.hpp"
#include "AST/SymbolTable.hpp"
#include "AST/TypeChecker.hpp"
#include "GDM/GDM.hpp"
#include "Codegen/LLVMContext.hpp"
#include "Codegen/CodegenVisitor.hpp"
#include "Codegen/RuntimeLinker.hpp"
#include "Codegen/TargetConfig.hpp"
#include "Codegen/OptimizationManager.hpp"

#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/Target/TargetMachine.h>
#include <memory>
#include <string>
#include <unordered_map>

namespace Cryo::Codegen
{
    /**
     * @brief Central code generation orchestrator for CryoLang
     * 
     * The CodeGenerator is the primary interface for converting CryoLang AST
     * into LLVM IR and eventually machine code. It coordinates all codegen
     * components and maintains compilation state.
     * 
     * Key responsibilities:
     * - AST to LLVM IR translation
     * - Symbol resolution and linking
     * - Target-specific code generation
     * - Runtime library integration
     * - Optimization pipeline management
     * - Debug information generation
     * 
     * Architecture:
     * ┌─────────────────────────────────────────────────────────────────┐
     * │                      CodeGenerator                              │
     * ├─────────────────────────────────────────────────────────────────┤
     * │  • LLVMContextManager    • CodegenVisitor                      │
     * │  • RuntimeLinker         • TargetConfig                        │
     * │  • OptimizationManager   • DebugInfoGenerator                  │
     * │  • ObjectFileEmitter     • ModuleManager                       │
     * └─────────────────────────────────────────────────────────────────┘
     */
    class CodeGenerator
    {
    public:
        //===================================================================
        // Construction & Configuration
        //===================================================================
        
        /**
         * @brief Construct CodeGenerator with target configuration
         * @param target_config Target-specific compilation settings
         * @param ast_context Reference to the AST context from frontend
         * @param symbol_table Reference to populated symbol table
         */
        CodeGenerator(
            std::unique_ptr<TargetConfig> target_config,
            Cryo::ASTContext& ast_context,
            Cryo::SymbolTable& symbol_table
        );
        
        ~CodeGenerator();

        // No copy/move for now - manage LLVM resources carefully
        CodeGenerator(const CodeGenerator&) = delete;
        CodeGenerator& operator=(const CodeGenerator&) = delete;
        CodeGenerator(CodeGenerator&&) = delete;
        CodeGenerator& operator=(CodeGenerator&&) = delete;

        //===================================================================
        // Core Code Generation Interface
        //===================================================================

        /**
         * @brief Generate LLVM IR from AST
         * @param program_node Root AST node to generate code for
         * @return Success status
         */
        bool generate_ir(Cryo::ProgramNode* program_node);

        /**
         * @brief Generate optimized LLVM IR
         * @param optimization_level Optimization level (0-3)
         * @return Success status
         */
        bool optimize(int optimization_level = 2);

        /**
         * @brief Emit object file
         * @param output_path Path for generated object file
         * @return Success status
         */
        bool emit_object_file(const std::string& output_path);

        /**
         * @brief Emit executable
         * @param output_path Path for generated executable
         * @param link_runtime Whether to link CryoLang runtime
         * @return Success status
         */
        bool emit_executable(const std::string& output_path, bool link_runtime = true);

        /**
         * @brief Emit LLVM IR to file (for debugging/inspection)
         * @param output_path Path for IR file
         * @return Success status
         */
        bool emit_llvm_ir(const std::string& output_path);

        //===================================================================
        // Configuration & State Management
        //===================================================================

        /**
         * @brief Set target triple (e.g., "x86_64-pc-windows-msvc")
         */
        void set_target_triple(const std::string& triple);

        /**
         * @brief Set CPU target (e.g., "x86-64", "generic")
         */
        void set_cpu_target(const std::string& cpu);

        /**
         * @brief Enable/disable debug information generation
         */
        void set_debug_info(bool enable);

        /**
         * @brief Set optimization level (0-3)
         */
        void set_optimization_level(int level);

        /**
         * @brief Add runtime library search path
         */
        void add_runtime_path(const std::string& path);

        /**
         * @brief Set code model (small, medium, large)
         */
        void set_code_model(const std::string& model);

        //===================================================================
        // Component Access (for advanced usage)
        //===================================================================

        /**
         * @brief Get LLVM module (for inspection/advanced manipulation)
         */
        llvm::Module* get_module() const;

        /**
         * @brief Get LLVM context manager
         */
        LLVMContextManager* get_context_manager() const;

        /**
         * @brief Get runtime linker
         */
        RuntimeLinker* get_runtime_linker() const;

        /**
         * @brief Get target configuration
         */
        TargetConfig* get_target_config() const;

        //===================================================================
        // Diagnostic & Debugging
        //===================================================================

        /**
         * @brief Verify generated LLVM IR
         * @return true if IR is valid
         */
        bool verify_ir() const;

        /**
         * @brief Print LLVM IR to stream
         */
        void print_ir(std::ostream& os) const;

        /**
         * @brief Print compilation statistics
         */
        void print_stats(std::ostream& os) const;

        /**
         * @brief Get last error message
         */
        const std::string& get_last_error() const;

        /**
         * @brief Check if codegen has errors
         */
        bool has_errors() const;

    private:
        //===================================================================
        // Private Implementation
        //===================================================================

        // Core components
        std::unique_ptr<LLVMContextManager> _context_manager;
        std::unique_ptr<CodegenVisitor> _visitor;
        std::unique_ptr<RuntimeLinker> _runtime_linker;
        std::unique_ptr<TargetConfig> _target_config;
        std::unique_ptr<OptimizationManager> _optimization_manager;

        // LLVM components (managed by context manager)
        llvm::Module* _module;
        llvm::IRBuilder<>* _builder;
        llvm::TargetMachine* _target_machine;

        // Frontend context references
        Cryo::ASTContext& _ast_context;
        Cryo::SymbolTable& _symbol_table;

        // Compilation state
        std::string _module_name;
        std::string _last_error;
        bool _debug_enabled;
        int _optimization_level;
        bool _has_errors;

        // Statistics
        size_t _functions_generated;
        size_t _types_generated;
        size_t _globals_generated;

        //===================================================================
        // Private Methods
        //===================================================================

        /**
         * @brief Initialize LLVM components
         */
        bool initialize_llvm();

        /**
         * @brief Setup target machine
         */
        bool setup_target_machine();

        /**
         * @brief Initialize module with metadata
         */
        bool initialize_module(const std::string& module_name);

        /**
         * @brief Setup runtime function declarations
         */
        bool setup_runtime_functions();

        /**
         * @brief Perform final IR validation and cleanup
         */
        bool finalize_ir();

        /**
         * @brief Report codegen error
         */
        void report_error(const std::string& message);

        /**
         * @brief Clear error state
         */
        void clear_errors();
    };

    //=======================================================================
    // Factory Functions
    //=======================================================================

    /**
     * @brief Create a CodeGenerator with default configuration
     * @param ast_context Reference to AST context
     * @param symbol_table Reference to symbol table
     * @return Unique pointer to CodeGenerator instance
     */
    std::unique_ptr<CodeGenerator> create_default_codegen(
        Cryo::ASTContext& ast_context,
        Cryo::SymbolTable& symbol_table
    );

    /**
     * @brief Create a CodeGenerator for specific target
     * @param target_triple Target triple string
     * @param ast_context Reference to AST context
     * @param symbol_table Reference to symbol table
     * @return Unique pointer to CodeGenerator instance
     */
    std::unique_ptr<CodeGenerator> create_target_codegen(
        const std::string& target_triple,
        Cryo::ASTContext& ast_context,
        Cryo::SymbolTable& symbol_table
    );

} // namespace Cryo::Codegen