#pragma once

#include "Lexer/lexer.hpp"
#include "Parser/Parser.hpp"
#include "AST/ASTContext.hpp"
#include "Types/SymbolTable.hpp"
#include "AST/ASTNode.hpp"
#include "AST/ASTDumper.hpp"
#include "Types/TypeChecker.hpp"
#include "Types/Monomorphizer.hpp"
#include "AST/TemplateRegistry.hpp"
#include "AST/DirectiveSystem.hpp"
#include "AST/DirectiveProcessors.hpp"
#include "AST/ASTSpecializer.hpp"
#include "Types/GenericRegistry.hpp"
#include "Types/TypeResolver.hpp"
#include "Diagnostics/Diag.hpp"
#include "Codegen/CodeGenerator.hpp"
#include "Codegen/LLVMContext.hpp"
#include <unordered_map>
#include "Utils/SymbolResolutionManager.hpp"
#include "Linker/CryoLinker.hpp"
#include "Utils/File.hpp"
#include "Compiler/ModuleLoader.hpp"
#include "Compiler/PassManager.hpp"
#include <memory>
#include <string>
#include <vector>

namespace Cryo
{
    // Forward declarations
    namespace Codegen
    {
        class CodegenVisitor;
    }
    class TypeMapper;

    /**
     * @brief Central orchestration class for the Cryo compiler
     *
     * The CompilerInstance manages the lifecycle and coordination of all
     * major compilation components. It provides a unified interface for
     * the compilation process from source code to final executable.
     */
    class CompilerInstance
    {
    private:
        // Core components (order matters for destruction!)
        std::unique_ptr<Lexer> _lexer;
        std::unique_ptr<Parser> _parser;
        std::unique_ptr<ASTContext> _ast_context;
        std::unique_ptr<DiagEmitter> _diagnostics;
        // SRM context must be declared before manager (destruction order)
        std::unique_ptr<Cryo::SRM::SymbolResolutionContext> _symbol_resolution_context;
        std::unique_ptr<Cryo::SRM::SymbolResolutionManager> _symbol_resolution_manager;
        // Types components - GenericRegistry and TypeResolver must come before TypeChecker
        std::unique_ptr<GenericRegistry> _generic_registry;
        std::unique_ptr<TypeResolver> _type_resolver;
        // SymbolTable - use ASTContext's symbol table to ensure consistency with Parser
        // This is a non-owning pointer to _ast_context->symbols()
        SymbolTable* _symbol_table;
        // TypeChecker needs arena, resolver, modules, generics
        std::unique_ptr<TypeChecker> _type_checker;
        std::unique_ptr<Monomorphizer> _monomorphization_pass;
        std::unique_ptr<ASTSpecializer> _ast_specializer;
        std::unique_ptr<TemplateRegistry> _template_registry;
        std::unique_ptr<Cryo::Codegen::CodeGenerator> _codegen;
        std::unique_ptr<Cryo::Linker::CryoLinker> _linker;
        // ModuleLoader must be declared AFTER the objects it references (symbol_table, template_registry)
        // This ensures proper destruction order (ModuleLoader destroyed first)
        std::unique_ptr<ModuleLoader> _module_loader;

        // Pass Manager - manages the compilation pipeline
        std::unique_ptr<PassManager> _pass_manager;
        std::unique_ptr<PassContext> _pass_context;

        // Directive system
        std::unique_ptr<DirectiveRegistry> _directive_registry;
        CompilationContext _compilation_context;

        // Compilation state
        std::string _source_file;
        std::vector<std::string> _include_paths;
        bool _debug_mode;
        bool _show_ast_before_ir;
        bool _stdlib_linking_enabled;                  // Control whether to link libcryo.a by default
        bool _stdlib_compilation_mode;                 // Control whether to generate full implementations for stdlib compilation
        bool _auto_imports_enabled;                    // Control whether to automatically import core types
        bool _lsp_mode;                                // LSP compilation mode
        bool _frontend_only;                           // Frontend-only compilation mode
        bool _raw_mode;                                // Raw mode compilation (no stdlib, no main transform)
        bool _dump_symbols;                            // Dump symbol tables for each module to debug files
        std::string _dump_symbols_output_dir;          // Output directory for symbol dumps
        std::string _current_namespace;                // Current namespace context
        std::vector<std::string> _imported_namespaces;     // Track imported namespaces for enhanced resolution
        std::vector<std::string> _local_import_modules;    // Modules from local project files that need IR generation

        // Results
        std::unique_ptr<ProgramNode> _ast_root;

        // Storage for compiled ASTs to prevent dangling ast_node pointers in GenericRegistry
        // When in stdlib compilation mode, ASTs are moved here instead of destroyed
        // so that templates registered with ast_node pointers remain valid
        std::vector<std::unique_ptr<ProgramNode>> _compiled_asts;

        // Cross-module function registry for stdlib compilation.
        // Stores lightweight type descriptors from completed modules so that
        // later modules can create ExternalLinkage declarations without sharing
        // the LLVMContext (which causes struct type collisions).
        struct SimpleTypeDesc
        {
            enum Kind : uint8_t { Void, Int, Float, Double, Ptr, NamedStruct, Other };
            Kind kind = Other;
            unsigned int_width = 0;    // for Int
            std::string struct_name;   // for NamedStruct
        };
        struct CrossModuleFnEntry
        {
            SimpleTypeDesc return_type;
            std::vector<SimpleTypeDesc> param_types;
            bool is_var_arg = false;
            unsigned calling_conv = 0;
        };
        std::unordered_map<std::string, CrossModuleFnEntry> _cross_module_functions;

        // Loaded source file (for pass-based compilation)
        std::unique_ptr<File> _loaded_file;
        std::string _loaded_file_path;
        std::string _loaded_file_content;

    public:
        CompilerInstance();
        ~CompilerInstance() = default;

        // Configuration
        void set_source_file(const std::string &file_path);
        void add_include_path(const std::string &path);
        void set_debug_mode(bool enable) { _debug_mode = enable; }
        void set_show_ast_before_ir(bool show) { _show_ast_before_ir = show; }

        // Main compilation phases
        bool compile_file(const std::string &source_file);
        bool compile_stdlib(const std::string &source_dir, const std::string &output_path);
        bool parse_source(const std::string &source_code);

        // LSP-specific frontend-only compilation (no codegen)
        bool compile_frontend_only(const std::string &source_file);

        // Code generation and linking
        bool generate_ir();
        bool generate_output(const std::string &output_path,
                             Cryo::Linker::CryoLinker::LinkTarget target = Cryo::Linker::CryoLinker::LinkTarget::Executable);

        // Phase-by-phase access (for testing/debugging)
        bool tokenize();
        bool parse();
        bool analyze(); // Future: semantic analysis

        // Component access
        Lexer *lexer() const { return _lexer.get(); }
        Parser *parser() const { return _parser.get(); }
        ASTContext *ast_context() const { return _ast_context.get(); }
        SymbolTable *symbol_table() const { return _symbol_table; }
        DiagEmitter *diagnostics() const { return _diagnostics.get(); }
        TypeChecker *type_checker() const { return _type_checker.get(); }
        TypeResolver *type_resolver() const { return _type_resolver.get(); }
        GenericRegistry *generic_registry() const { return _generic_registry.get(); }
        Monomorphizer *monomorphizer() const { return _monomorphization_pass.get(); }
        ASTSpecializer *ast_specializer() const { return _ast_specializer.get(); }
        TemplateRegistry *template_registry() const { return _template_registry.get(); }
        Cryo::SRM::SymbolResolutionManager *symbol_resolution_manager() const { return _symbol_resolution_manager.get(); }
        Cryo::Codegen::CodeGenerator *codegen() const { return _codegen.get(); }
        Cryo::Linker::CryoLinker *linker() const { return _linker.get(); }
        ModuleLoader *module_loader() const { return _module_loader.get(); }

        // Directive system access
        DirectiveRegistry *directive_registry() const { return _directive_registry.get(); }
        CompilationContext &compilation_context() { return _compilation_context; }
        const CompilationContext &compilation_context() const { return _compilation_context; }

        // Pass Manager access
        PassManager *pass_manager() const { return _pass_manager.get(); }
        PassContext *pass_context() const { return _pass_context.get(); }

        // Initialize pass pipeline
        void initialize_pass_manager();
        void initialize_pass_context();

        // Dump pass execution order for debugging
        void dump_pass_order(std::ostream &os = std::cout) const;

        // Standard library linking control
        void set_stdlib_linking(bool enable) { _stdlib_linking_enabled = enable; }
        bool stdlib_linking_enabled() const { return _stdlib_linking_enabled; }

        // Standard library compilation mode (generates full implementations)
        void set_stdlib_compilation_mode(bool enable) { _stdlib_compilation_mode = enable; }
        bool stdlib_compilation_mode() const { return _stdlib_compilation_mode; }

        // Show stdlib diagnostics (for building stdlib itself)
        void set_show_stdlib_diagnostics(bool enable);

        void set_auto_imports_enabled(bool enable) { _auto_imports_enabled = enable; }
        bool auto_imports_enabled() const { return _auto_imports_enabled; }

        // Raw mode control (no stdlib, no main transform)
        void set_raw_mode(bool enable) { 
            _raw_mode = enable; 
            if (enable) {
                // Raw mode implies no stdlib linking and no auto imports
                _stdlib_linking_enabled = false;
                _auto_imports_enabled = false;
            }
        }
        bool raw_mode() const { return _raw_mode; }

        // Symbol dump control
        void set_dump_symbols(bool enable, const std::string &output_dir = "") { _dump_symbols = enable; _dump_symbols_output_dir = output_dir; }
        bool dump_symbols_enabled() const { return _dump_symbols; }
        const std::string &dump_symbols_output_dir() const { return _dump_symbols_output_dir; }

        // Namespace context
        void set_namespace_context(const std::string &namespace_name);
        const std::string &get_namespace_context() const { return _current_namespace; }

        // Results access
        ProgramNode *ast_root() const { return _ast_root.get(); }
        bool has_errors() const;

        // Utility
        void print_ast(std::ostream &os = std::cout, bool use_colors = true) const;
        void dump_ast(std::ostream &os = std::cout, bool use_colors = true) const;
        void dump_symbol_table(std::ostream &os = std::cout) const;
        void dump_type_table(std::ostream &os = std::cout) const;
        void dump_type_errors(std::ostream &os = std::cout) const;
        void dump_ir(std::ostream &os = std::cout) const;
        void print_diagnostics(std::ostream &os = std::cerr) const;

        // LSP-specific compilation mode
        bool compile_for_lsp(const std::string &source_file);
        void set_lsp_mode(bool enabled) { _lsp_mode = enabled; }
        bool is_lsp_mode() const { return _lsp_mode; }
        void clear();

        // ============================================================================
        // Pass Implementation Methods
        // These methods are called by the standard passes and encapsulate
        // the actual compilation logic for each pass.
        // ============================================================================

        /// Load a source file and prepare for compilation (used before running passes)
        bool load_source_file(const std::string &source_file);

        /// Run lexing phase - creates lexer from loaded file
        bool run_lexing_phase();

        /// Run parsing phase - creates parser and parses to AST
        bool run_parsing_phase();

        /// Inject auto-imports for core types
        void run_auto_import_phase();

        /// Collect all declarations (types, functions, signatures)
        void run_declaration_collection_phase();

        /// Process compiler directives
        bool run_directive_processing_phase();

        /// Type-check function bodies and validate directive effects
        bool run_function_body_phase();

        /// Process struct declarations for LLVM type registration
        void run_type_lowering_phase();

        /// Pre-register functions in LLVM module
        void run_function_declaration_phase();

        /// Generate LLVM IR
        bool run_ir_generation_phase();

    private:
        void initialize_components();
        void initialize_standard_library(); // Initialize built-in functions and types
        void reset_state();
        bool parse_source_from_file(std::unique_ptr<File> file);
        void populate_symbol_table(ASTNode *node);
        void populate_symbol_table_with_scope(ASTNode *node, SymbolTable *current_scope, const std::string &scope_name);
        void collect_declarations_pass(ASTNode *node, SymbolTable *current_scope, const std::string &scope_name);
        void populate_type_fields_pass(ASTNode *node); // Phase 2: populate struct/class fields after all types are registered
        void register_ast_nodes_with_typemapper();
        void register_ast_nodes_recursive(ASTNode *node, TypeMapper *type_mapper);
        void process_struct_declarations_for_preregistration(ASTNode *node);
        void process_struct_declarations_recursive(ASTNode *node);
        std::string build_function_signature(FunctionDeclarationNode *func_decl);
        void inject_auto_imports(SymbolTable *current_scope, const std::string &scope_name); // Auto-import core types

        // SRM helper methods
        std::string generate_method_name(const std::string &scope_name, const std::string &class_name, const std::string &method_name);

        // Directive system
        void initialize_directive_system();
        bool process_directives();
        bool validate_directive_effects();

        // Cross-module function resolution for stdlib compilation
        void save_cross_module_functions();
        void declare_cross_module_functions();
        static SimpleTypeDesc llvm_type_to_desc(llvm::Type *t);
        static llvm::Type *desc_to_llvm_type(const SimpleTypeDesc &desc, llvm::LLVMContext &ctx);

        // Dynamic path resolution
        std::string find_bin_directory() const;
    };

    /**
     * @brief Factory function for creating configured compiler instances
     */
    std::unique_ptr<CompilerInstance> create_compiler_instance();
}