#pragma once

#include "CLI.hpp"
#include "Compiler/CompilerInstance.hpp"

namespace Cryo::CLI::Commands
{
    // ================================================================
    // Help Command
    // ================================================================

    /**
     * @brief Displays help information for commands
     */
    class HelpCommand : public Command
    {
    private:
        const CLIRunner *_cli_runner;

    public:
        HelpCommand(const CLIRunner *runner);
        int execute(const ParsedArgs &args) override;
    };

    // ================================================================
    // Version Command
    // ================================================================

    /**
     * @brief Displays version information
     */
    class VersionCommand : public Command
    {
    public:
        VersionCommand();
        int execute(const ParsedArgs &args) override;
    };

    // ================================================================
    // Compile Command
    // ================================================================

    /**
     * @brief Compiles Cryo source files
     */
    class CompileCommand : public Command
    {
    public:
        CompileCommand();
        int execute(const ParsedArgs &args) override;

    private:
        int compile_file(const std::string &input_file, const ParsedArgs &args);
        void print_compilation_summary(bool success, const std::string &file);
    };

    // ================================================================
    // Check Command
    // ================================================================

    /**
     * @brief Type-checks and validates source files without compilation
     */
    class CheckCommand : public Command
    {
    public:
        CheckCommand();
        int execute(const ParsedArgs &args) override;

    private:
        int check_file(const std::string &input_file, const ParsedArgs &args);
    };

    // ================================================================
    // AST Command
    // ================================================================

    /**
     * @brief Displays the Abstract Syntax Tree for a source file
     */
    class ASTCommand : public Command
    {
    public:
        ASTCommand();
        int execute(const ParsedArgs &args) override;

    private:
        int dump_ast(const std::string &input_file, const ParsedArgs &args);
    };

    // ================================================================
    // Tokens Command
    // ================================================================

    /**
     * @brief Displays tokenized output for a source file (lexer output)
     */
    class TokensCommand : public Command
    {
    public:
        TokensCommand();
        int execute(const ParsedArgs &args) override;

    private:
        int dump_tokens(const std::string &input_file, const ParsedArgs &args);
    };

    // ================================================================
    // Symbols Command
    // ================================================================

    /**
     * @brief Displays symbol table information
     */
    class SymbolsCommand : public Command
    {
    public:
        SymbolsCommand();
        int execute(const ParsedArgs &args) override;

    private:
        int dump_symbols(const std::string &input_file, const ParsedArgs &args);
    };

    // ================================================================
    // Info Command
    // ================================================================

    /**
     * @brief Displays compiler and system information
     */
    class InfoCommand : public Command
    {
    public:
        InfoCommand();
        int execute(const ParsedArgs &args) override;

    private:
        void print_compiler_info();
        void print_system_info();
        void print_build_info();
    };

} // namespace Cryo::CLI::Commands
