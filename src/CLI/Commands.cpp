#include "CLI/Commands.hpp"
#include "CLI/CLI.hpp"
#include "Compiler/CompilerInstance.hpp"
#include <iostream>
#include <filesystem>
#include <chrono>
#include <iomanip>

namespace Cryo::CLI::Commands
{
    // ================================================================
    // Help Command
    // ================================================================

    HelpCommand::HelpCommand(const CLIRunner *runner)
        : Command("help", "Display help information for commands"), _cli_runner(runner)
    {
        usage("[command]");
        argument(CLIArgument("command", "Show help for specific command", false));
    }

    int HelpCommand::execute(const ParsedArgs &args)
    {
        if (args.positional_count() > 0)
        {
            // Show help for specific command
            std::string command_name = args.positional()[0];
            // Note: In a full implementation, you'd need access to the CLI runner's commands
            std::cout << "Help for '" << command_name << "' command would be displayed here.\n";
            std::cout << "Run 'cryo " << command_name << " --help' for detailed information.\n";
        }
        else
        {
            // Show general help
            _cli_runner->print_help();
        }
        return 0;
    }

    // ================================================================
    // Version Command
    // ================================================================

    VersionCommand::VersionCommand()
        : Command("version", "Display version information")
    {
        argument(CLIArgument("verbose", "Show detailed version information", false).flag().alias("v"));
    }

    int VersionCommand::execute(const ParsedArgs &args)
    {
        std::cout << "Cryo Compiler v0.1.0" << std::endl;

        if (args.get_flag("verbose"))
        {
            std::cout << "Build Information:" << std::endl;
            std::cout << "  Built with: Clang++ (C++23)" << std::endl;
            std::cout << "  LLVM Support: Yes" << std::endl;
            std::cout << "  Platform: "
#ifdef _WIN32
                      << "Windows"
#elif __linux__
                      << "Linux"
#elif __APPLE__
                      << "macOS"
#else
                      << "Unknown"
#endif
                      << std::endl;

            std::cout << "  Debug Mode: "
#ifdef NDEBUG
                      << "No"
#else
                      << "Yes"
#endif
                      << std::endl;

            // Build timestamp
            std::cout << "  Built on: " << __DATE__ << " " << __TIME__ << std::endl;
        }

        return 0;
    }

    // ================================================================
    // Compile Command
    // ================================================================

    CompileCommand::CompileCommand()
        : Command("compile", "Compile Cryo source files")
    {
        usage("<input_file> [options]");
        argument(CLIArgument("input", "Input source file", true));
        argument(CLIArgument("output", "Output file name", false).alias("o"));
        argument(CLIArgument("debug", "Enable debug output", false).flag().alias("d"));
        argument(CLIArgument("verbose", "Show verbose compilation output", false).flag().alias("v"));
        argument(CLIArgument("ast", "Display AST after compilation", false).flag());
        argument(CLIArgument("symbols", "Display symbol table after compilation", false).flag());
        argument(CLIArgument("types", "Display type table after compilation", false).flag());
    }

    int CompileCommand::execute(const ParsedArgs &args)
    {
        if (args.positional_count() == 0)
        {
            std::cerr << "Error: No input file specified" << std::endl;
            print_help();
            return 1;
        }

        std::string input_file = args.positional()[0];
        return compile_file(input_file, args);
    }

    int CompileCommand::compile_file(const std::string &input_file, const ParsedArgs &args)
    {
        bool verbose = args.get_flag("verbose");
        bool debug = args.get_flag("debug");

        if (verbose)
        {
            std::cout << "Cryo Compiler v0.1.0" << std::endl;
            std::cout << "Input file: " << input_file << std::endl;
        }

        // Check if file exists
        if (!std::filesystem::exists(input_file))
        {
            std::cerr << "Error: File '" << input_file << "' not found" << std::endl;
            return 1;
        }

        // Start timing
        auto start_time = std::chrono::high_resolution_clock::now();

        // Create compiler instance
        auto compiler = Cryo::create_compiler_instance();
        compiler->set_debug_mode(debug);

        // Set AST printing flag if requested
        if (args.get_flag("ast"))
        {
            compiler->set_show_ast_before_ir(true);
        }

        // Compile
        bool success = compiler->compile_file(input_file);

        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

        // Print results
        print_compilation_summary(success, input_file);

        if (verbose)
        {
            std::cout << "Compilation time: " << duration.count() << "ms" << std::endl;
        }

        if (success)
        {
            // Show requested outputs
            // Note: AST is already shown before IR generation if --ast flag was used

            if (args.get_flag("symbols"))
            {
                std::cout << "\nSymbol Table:" << std::endl;
                compiler->dump_symbol_table();
                
                std::cout << "\nType Table:" << std::endl;
                compiler->dump_type_table();
            }

            if (args.get_flag("types"))
            {
                std::cout << "\nType Table:" << std::endl;
                compiler->dump_type_table();
            }

            return 0;
        }
        else
        {
            compiler->print_diagnostics();
            return 1;
        }
    }

    void CompileCommand::print_compilation_summary(bool success, const std::string &file)
    {
        if (success)
        {
            std::cout << "✓ Successfully compiled '" << file << "'" << std::endl;
        }
        else
        {
            std::cout << "❌ Failed to compile '" << file << "'" << std::endl;
        }
    }

    // ================================================================
    // Check Command
    // ================================================================

    CheckCommand::CheckCommand()
        : Command("check", "Type-check and validate source files without full compilation")
    {
        usage("<input_file> [options]");
        argument(CLIArgument("input", "Input source file", true));
        argument(CLIArgument("verbose", "Show verbose checking output", false).flag().alias("v"));
    }

    int CheckCommand::execute(const ParsedArgs &args)
    {
        if (args.positional_count() == 0)
        {
            std::cerr << "Error: No input file specified" << std::endl;
            print_help();
            return 1;
        }

        std::string input_file = args.positional()[0];
        return check_file(input_file, args);
    }

    int CheckCommand::check_file(const std::string &input_file, const ParsedArgs &args)
    {
        bool verbose = args.get_flag("verbose");

        if (verbose)
        {
            std::cout << "Checking file: " << input_file << std::endl;
        }

        // Check if file exists
        if (!std::filesystem::exists(input_file))
        {
            std::cerr << "Error: File '" << input_file << "' not found" << std::endl;
            return 1;
        }

        // Create compiler instance
        auto compiler = Cryo::create_compiler_instance();
        compiler->set_debug_mode(false); // Reduce noise for checking

        // Perform compilation up to type checking
        bool success = compiler->compile_file(input_file);

        if (success)
        {
            std::cout << "✓ No errors found in '" << input_file << "'" << std::endl;
            return 0;
        }
        else
        {
            std::cout << "❌ Errors found in '" << input_file << "'" << std::endl;
            compiler->print_diagnostics();
            return 1;
        }
    }

    // ================================================================
    // AST Command
    // ================================================================

    ASTCommand::ASTCommand()
        : Command("ast", "Display the Abstract Syntax Tree for a source file")
    {
        usage("<input_file> [options]");
        argument(CLIArgument("input", "Input source file", true));
        argument(CLIArgument("colors", "Use colored output", false).flag().alias("c"));
    }

    int ASTCommand::execute(const ParsedArgs &args)
    {
        if (args.positional_count() == 0)
        {
            std::cerr << "Error: No input file specified" << std::endl;
            print_help();
            return 1;
        }

        std::string input_file = args.positional()[0];
        return dump_ast(input_file, args);
    }

    int ASTCommand::dump_ast(const std::string &input_file, const ParsedArgs &args)
    {
        // Check if file exists
        if (!std::filesystem::exists(input_file))
        {
            std::cerr << "Error: File '" << input_file << "' not found" << std::endl;
            return 1;
        }

        // Create compiler instance
        auto compiler = Cryo::create_compiler_instance();
        compiler->set_debug_mode(false);
        // Don't set show_ast_before_ir for this command as it handles AST printing specially

        // Parse file
        if (compiler->compile_file(input_file))
        {
            std::cout << "AST for '" << input_file << "':" << std::endl;
            std::cout << std::string(50, '=') << std::endl;

            bool use_colors = args.get_flag("colors");
            compiler->dump_ast(std::cout, use_colors);

            return 0;
        }
        else
        {
            std::cerr << "Error: Failed to parse file '" << input_file << "'" << std::endl;
            compiler->print_diagnostics();
            return 1;
        }
    }

    // ================================================================
    // Tokens Command
    // ================================================================

    TokensCommand::TokensCommand()
        : Command("tokens", "Display tokenized output for a source file (lexer output)")
    {
        usage("<input_file>");
        argument(CLIArgument("input", "Input source file", true));
    }

    int TokensCommand::execute(const ParsedArgs &args)
    {
        if (args.positional_count() == 0)
        {
            std::cerr << "Error: No input file specified" << std::endl;
            print_help();
            return 1;
        }

        std::string input_file = args.positional()[0];
        return dump_tokens(input_file, args);
    }

    int TokensCommand::dump_tokens(const std::string &input_file, const ParsedArgs &args)
    {
        // Check if file exists
        if (!std::filesystem::exists(input_file))
        {
            std::cerr << "Error: File '" << input_file << "' not found" << std::endl;
            return 1;
        }

        std::cout << "Tokens for '" << input_file << "':" << std::endl;
        std::cout << std::string(50, '=') << std::endl;

        // Note: This would require access to just the lexer
        // For now, we'll indicate it's not fully implemented
        std::cout << "Token output is not yet fully implemented." << std::endl;
        std::cout << "This would show the tokenized output from the lexer." << std::endl;

        return 0;
    }

    // ================================================================
    // Symbols Command
    // ================================================================

    SymbolsCommand::SymbolsCommand()
        : Command("symbols", "Display symbol table information")
    {
        usage("<input_file>");
        argument(CLIArgument("input", "Input source file", true));
    }

    int SymbolsCommand::execute(const ParsedArgs &args)
    {
        if (args.positional_count() == 0)
        {
            std::cerr << "Error: No input file specified" << std::endl;
            print_help();
            return 1;
        }

        std::string input_file = args.positional()[0];
        return dump_symbols(input_file, args);
    }

    int SymbolsCommand::dump_symbols(const std::string &input_file, const ParsedArgs &args)
    {
        // Check if file exists
        if (!std::filesystem::exists(input_file))
        {
            std::cerr << "Error: File '" << input_file << "' not found" << std::endl;
            return 1;
        }

        // Create compiler instance
        auto compiler = Cryo::create_compiler_instance();
        compiler->set_debug_mode(false);

        // Parse file
        if (compiler->compile_file(input_file))
        {
            std::cout << "Symbol table for '" << input_file << "':" << std::endl;
            std::cout << std::string(50, '=') << std::endl;
            compiler->dump_symbol_table();

            std::cout << "\nType table:" << std::endl;
            std::cout << std::string(50, '=') << std::endl;
            compiler->dump_type_table();

            return 0;
        }
        else
        {
            std::cerr << "Error: Failed to analyze file '" << input_file << "'" << std::endl;
            compiler->print_diagnostics();
            return 1;
        }
    }

    // ================================================================
    // Info Command
    // ================================================================

    InfoCommand::InfoCommand()
        : Command("info", "Display compiler and system information")
    {
        argument(CLIArgument("system", "Show system information", false).flag().alias("s"));
        argument(CLIArgument("build", "Show build information", false).flag().alias("b"));
    }

    int InfoCommand::execute(const ParsedArgs &args)
    {
        print_compiler_info();

        if (args.get_flag("system"))
        {
            std::cout << std::endl;
            print_system_info();
        }

        if (args.get_flag("build"))
        {
            std::cout << std::endl;
            print_build_info();
        }

        return 0;
    }

    void InfoCommand::print_compiler_info()
    {
        std::cout << "Cryo Compiler Information" << std::endl;
        std::cout << std::string(30, '=') << std::endl;
        std::cout << "Version: 0.1.0" << std::endl;
        std::cout << "Language: C++23" << std::endl;
        std::cout << "LLVM Support: Yes" << std::endl;
        std::cout << "Target Architecture: x86_64" << std::endl;
    }

    void InfoCommand::print_system_info()
    {
        std::cout << "System Information" << std::endl;
        std::cout << std::string(20, '=') << std::endl;

        std::cout << "Platform: "
#ifdef _WIN32
                  << "Windows"
#elif __linux__
                  << "Linux"
#elif __APPLE__
                  << "macOS"
#else
                  << "Unknown"
#endif
                  << std::endl;

        std::cout << "Architecture: "
#ifdef _M_X64 || __x86_64__
                  << "x86_64"
#elif _M_IX86 || __i386__
                  << "x86"
#elif _M_ARM64 || __aarch64__
                  << "ARM64"
#else
                  << "Unknown"
#endif
                  << std::endl;

        // Current working directory
        try
        {
            std::cout << "Working Directory: " << std::filesystem::current_path() << std::endl;
        }
        catch (...)
        {
            std::cout << "Working Directory: <unavailable>" << std::endl;
        }
    }

    void InfoCommand::print_build_info()
    {
        std::cout << "Build Information" << std::endl;
        std::cout << std::string(20, '=') << std::endl;
        std::cout << "Built on: " << __DATE__ << " " << __TIME__ << std::endl;
        std::cout << "Compiler: "
#ifdef __clang__
                  << "Clang " << __clang_major__ << "." << __clang_minor__ << "." << __clang_patchlevel__
#elif defined(__GNUC__)
                  << "GCC " << __GNUC__ << "." << __GNUC_MINOR__ << "." << __GNUC_PATCHLEVEL__
#elif defined(_MSC_VER)
                  << "MSVC " << _MSC_VER
#else
                  << "Unknown"
#endif
                  << std::endl;

        std::cout << "Debug Mode: "
#ifdef NDEBUG
                  << "No"
#else
                  << "Yes"
#endif
                  << std::endl;
    }

} // namespace Cryo::CLI::Commands
