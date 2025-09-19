#pragma once

#include "Lexer/lexer.hpp"
#include "Parser/Parser.hpp"
#include "AST/ASTContext.hpp"
#include "AST/SymbolTable.hpp"
#include "AST/ASTNode.hpp"
#include "AST/ASTDumper.hpp"
#include "Utils/file.hpp"
#include <memory>
#include <string>
#include <vector>

namespace Cryo
{
    /**
     * @brief Central orchestration class for the Cryo compiler
     *
     * The CompilerInstance manages the lifecycle and coordination of all
     * major compilation components. It provides a unified interface for
     * the compilation process from source code to AST.
     */
    class CompilerInstance
    {
    private:
        // Core components
        std::unique_ptr<Lexer> _lexer;
        std::unique_ptr<Parser> _parser;
        std::unique_ptr<ASTContext> _ast_context;
        std::unique_ptr<SymbolTable> _symbol_table;

        // Compilation state
        std::string _source_file;
        std::vector<std::string> _include_paths;
        bool _debug_mode;

        // Results
        std::unique_ptr<ProgramNode> _ast_root;
        std::vector<std::string> _diagnostics;

    public:
        CompilerInstance();
        ~CompilerInstance() = default;

        // Configuration
        void set_source_file(const std::string &file_path);
        void add_include_path(const std::string &path);
        void set_debug_mode(bool enable) { _debug_mode = enable; }

        // Main compilation phases
        bool compile_file(const std::string &source_file);
        bool parse_source(const std::string &source_code);

        // Phase-by-phase access (for testing/debugging)
        bool tokenize();
        bool parse();
        bool analyze(); // Future: semantic analysis

        // Component access
        Lexer *lexer() const { return _lexer.get(); }
        Parser *parser() const { return _parser.get(); }
        ASTContext *ast_context() const { return _ast_context.get(); }
        SymbolTable *symbol_table() const { return _symbol_table.get(); }

        // Results access
        ProgramNode *ast_root() const { return _ast_root.get(); }
        const std::vector<std::string> &diagnostics() const { return _diagnostics; }
        bool has_errors() const { return !_diagnostics.empty(); }

        // Utility
        void print_ast(std::ostream &os = std::cout, bool use_colors = true) const;
        void dump_ast(std::ostream &os = std::cout, bool use_colors = true) const;
        void print_diagnostics(std::ostream &os = std::cerr) const;
        void clear();

    private:
        void initialize_components();
        void reset_state();
        void report_error(const std::string &message);
        void report_warning(const std::string &message);
        bool parse_source_from_file(std::unique_ptr<File> file);
    };

    /**
     * @brief Factory function for creating configured compiler instances
     */
    std::unique_ptr<CompilerInstance> create_compiler_instance();
}