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
#include "Types/GenericRegistry.hpp"
#include "Types/TypeResolver.hpp"
#include "Diagnostics/Diag.hpp"
#include "Codegen/CodeGenerator.hpp"
#include "Utils/SymbolResolutionManager.hpp"
#include "Linker/CryoLinker.hpp"
#include "Utils/File.hpp"
#include "Compiler/ModuleLoader.hpp"
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
        std::unique_ptr<Cryo::SRM::SymbolResolutionManager> _symbol_resolution_manager;
        // Types components - GenericRegistry and TypeResolver must come before TypeChecker
        std::unique_ptr<GenericRegistry> _generic_registry;
        std::unique_ptr<TypeResolver> _type_resolver;
        // SymbolTable needs arena and modules from ASTContext
        std::unique_ptr<SymbolTable> _symbol_table;
        // TypeChecker needs arena, resolver, modules, generics
        std::unique_ptr<TypeChecker> _type_checker;
        std::unique_ptr<Monomorphizer> _monomorphization_pass;
        std::unique_ptr<TemplateRegistry> _template_registry;
        std::unique_ptr<Cryo::Codegen::CodeGenerator> _codegen;
        std::unique_ptr<Cryo::Linker::CryoLinker> _linker;
        // ModuleLoader must be declared AFTER the objects it references (symbol_table, template_registry)
        // This ensures proper destruction order (ModuleLoader destroyed first)
        std::unique_ptr<ModuleLoader> _module_loader;

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
        std::string _current_namespace;                // Current namespace context
        std::vector<std::string> _imported_namespaces; // Track imported namespaces for enhanced resolution

        // Results
        std::unique_ptr<ProgramNode> _ast_root;

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
        SymbolTable *symbol_table() const { return _symbol_table.get(); }
        DiagEmitter *diagnostics() const { return _diagnostics.get(); }
        TypeChecker *type_checker() const { return _type_checker.get(); }
        Cryo::Codegen::CodeGenerator *codegen() const { return _codegen.get(); }
        Cryo::Linker::CryoLinker *linker() const { return _linker.get(); }
        ModuleLoader *module_loader() const { return _module_loader.get(); }

        // Directive system access
        DirectiveRegistry *directive_registry() const { return _directive_registry.get(); }
        CompilationContext &compilation_context() { return _compilation_context; }
        const CompilationContext &compilation_context() const { return _compilation_context; }

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

    private:
        void initialize_components();
        void initialize_standard_library(); // Initialize built-in functions and types
        void reset_state();
        bool parse_source_from_file(std::unique_ptr<File> file);
        void populate_symbol_table(ASTNode *node);
        void populate_symbol_table_with_scope(ASTNode *node, SymbolTable *current_scope, const std::string &scope_name);
        void collect_declarations_pass(ASTNode *node, SymbolTable *current_scope, const std::string &scope_name);
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

        // Dynamic path resolution
        std::string find_bin_directory() const;
    };

    /**
     * @brief Factory function for creating configured compiler instances
     */
    std::unique_ptr<CompilerInstance> create_compiler_instance();
}