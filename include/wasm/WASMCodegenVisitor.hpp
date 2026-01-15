#pragma once

#include "Codegen/CodegenVisitor.hpp"
#include "wasm/WASMRuntimeAdapter.hpp"
#include <llvm/IR/Module.h>
#include <memory>

namespace Cryo::WASM
{
    /**
     * @brief WebAssembly-specific code generation visitor
     * 
     * This visitor extends the base CodegenVisitor to handle WASM-specific
     * code generation requirements, including:
     * - WASM memory model (linear memory)
     * - JavaScript interop function calls
     * - WASM-specific intrinsics
     * - Export declarations for JavaScript visibility
     */
    class WASMCodegenVisitor : public Cryo::Codegen::CodegenVisitor
    {
    public:
        //===================================================================
        // Construction
        //===================================================================

        WASMCodegenVisitor(
            Cryo::Codegen::LLVMContextManager& context_manager,
            Cryo::SymbolTable& symbol_table,
            Cryo::DiagEmitter* diagnostics = nullptr
        );

        ~WASMCodegenVisitor() override = default;

        //===================================================================
        // WASM-specific Configuration
        //===================================================================

        /**
         * @brief Enable JavaScript interop code generation
         */
        void enable_js_interop(bool enable) { _js_interop_enabled = enable; }

        /**
         * @brief Set the list of functions to export to JavaScript
         */
        void set_exported_functions(const std::vector<std::string>& functions);

        /**
         * @brief Register JavaScript function for calling from Cryo
         */
        void register_js_function(const std::string& name, const std::string& signature);

        //===================================================================
        // AST Visitor Overrides for WASM-specific handling
        //===================================================================

        void visit(Cryo::FunctionDeclarationNode& node) override;
        void visit(Cryo::CallExpressionNode& node) override;
        void visit(Cryo::ProgramNode& node) override;

        //===================================================================
        // WASM-specific Code Generation
        //===================================================================

        /**
         * @brief Generate WASM module initialization code
         */
        void generate_wasm_module_init();

        /**
         * @brief Generate JavaScript interop functions
         */
        void generate_js_interop_functions();

        /**
         * @brief Generate WASM memory management functions
         */
        void generate_wasm_memory_functions();

        /**
         * @brief Export function to JavaScript with WASM calling convention
         */
        void export_function_to_js(llvm::Function* func, const std::string& js_name);

    protected:
        //===================================================================
        // WASM-specific Helper Methods
        //===================================================================

        /**
         * @brief Create WASM-compatible function signature
         */
        llvm::FunctionType* create_wasm_function_type(
            llvm::Type* return_type,
            const std::vector<llvm::Type*>& param_types
        );

        /**
         * @brief Generate call to JavaScript function
         */
        llvm::Value* generate_js_call(
            const std::string& js_function_name,
            const std::vector<llvm::Value*>& args,
            llvm::Type* return_type
        );

        /**
         * @brief Convert Cryo type to WASM-compatible LLVM type
         */
        llvm::Type* convert_to_wasm_type(Cryo::Type* cryo_type);

        /**
         * @brief Generate WASM linear memory access
         */
        llvm::Value* generate_wasm_memory_access(
            llvm::Value* offset,
            llvm::Type* element_type,
            bool is_store = false,
            llvm::Value* store_value = nullptr
        );

    private:
        //===================================================================
        // WASM-specific State
        //===================================================================

        bool _js_interop_enabled = true;
        std::vector<std::string> _exported_functions;
        std::unordered_map<std::string, std::string> _js_function_signatures;
        
        // WASM-specific LLVM types
        llvm::Type* _wasm_ptr_type = nullptr;  // i32 for WASM32, i64 for WASM64
        llvm::Type* _wasm_size_type = nullptr;

        //===================================================================
        // Internal Methods
        //===================================================================

        void initialize_wasm_types();
        void setup_wasm_runtime_functions();
        llvm::Function* get_or_create_wasm_alloc_function();
        llvm::Function* get_or_create_wasm_free_function();
    };

} // namespace Cryo::WASM