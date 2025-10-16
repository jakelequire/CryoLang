#pragma once

#include "Lexer/lexer.hpp"
#include "Parser/Parser.hpp"
#include "AST/ASTContext.hpp"
#include "AST/SymbolTable.hpp"
#include "AST/ASTNode.hpp"
#include "AST/ASTDumper.hpp"
#include "AST/TypeChecker.hpp"
#include "AST/MonomorphizationPass.hpp"
#include "AST/TemplateRegistry.hpp"
#include "GDM/GDM.hpp"
#include "Codegen/CodeGenerator.hpp"
#include "Linker/CryoLinker.hpp"
#include "Utils/File.hpp"
#include "Utils/ModuleLoader.hpp"
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
    namespace Codegen
    {
        class TypeMapper;
    }

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
        std::unique_ptr<SymbolTable> _symbol_table;
        std::unique_ptr<DiagnosticManager> _diagnostic_manager;
        // TypeChecker must come AFTER ASTContext since it holds a reference to ast_context->types()
        std::unique_ptr<TypeChecker> _type_checker;
        std::unique_ptr<MonomorphizationPass> _monomorphization_pass;
        std::unique_ptr<TemplateRegistry> _template_registry;
        std::unique_ptr<Cryo::Codegen::CodeGenerator> _codegen;
        std::unique_ptr<Cryo::Linker::CryoLinker> _linker;
        // ModuleLoader must be declared AFTER the objects it references (symbol_table, template_registry)
        // This ensures proper destruction order (ModuleLoader destroyed first)
        std::unique_ptr<ModuleLoader> _module_loader;

        // Compilation state
        std::string _source_file;
        std::vector<std::string> _include_paths;
        bool _debug_mode;
        bool _show_ast_before_ir;
        bool _stdlib_linking_enabled;                  // Control whether to link libcryo.a by default
        bool _stdlib_compilation_mode;                 // Control whether to generate full implementations for stdlib compilation
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
        DiagnosticManager *diagnostic_manager() const { return _diagnostic_manager.get(); }
        TypeChecker *type_checker() const { return _type_checker.get(); }
        Cryo::Codegen::CodeGenerator *codegen() const { return _codegen.get(); }
        Cryo::Linker::CryoLinker *linker() const { return _linker.get(); }
        ModuleLoader *module_loader() const { return _module_loader.get(); }

        // Standard library linking control
        void set_stdlib_linking(bool enable) { _stdlib_linking_enabled = enable; }
        bool stdlib_linking_enabled() const { return _stdlib_linking_enabled; }

        // Standard library compilation mode (generates full implementations)
        void set_stdlib_compilation_mode(bool enable) { _stdlib_compilation_mode = enable; }
        bool stdlib_compilation_mode() const { return _stdlib_compilation_mode; }

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
        void register_ast_nodes_recursive(ASTNode *node, Codegen::TypeMapper *type_mapper);
        void process_struct_declarations_for_preregistration(ASTNode *node);
        void process_struct_declarations_recursive(ASTNode *node);
        std::string build_function_signature(FunctionDeclarationNode *func_decl);
        void inject_auto_imports(SymbolTable *current_scope, const std::string &scope_name); // Auto-import core types

        // Dynamic path resolution
        std::string find_bin_directory() const;
    };

    /**
     * @brief Factory function for creating configured compiler instances
     */
    std::unique_ptr<CompilerInstance> create_compiler_instance();
}