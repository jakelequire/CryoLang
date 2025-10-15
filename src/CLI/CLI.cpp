#include "CLI/CLI.hpp"
#include "CLI/Commands.hpp"
#include "Compiler/CompilerInstance.hpp"
#include "Linker/CryoLinker.hpp"
#include <iostream>
#include <algorithm>
#include <sstream>
#include <iomanip>

namespace Cryo::CLI
{
    // ================================================================
    // Command Implementation
    // ================================================================

    void Command::print_help() const
    {
        std::cout << "Usage: cryo " << _name;
        if (!_usage.empty())
        {
            std::cout << " " << _usage;
        }
        std::cout << "\n\n";

        std::cout << _description << "\n\n";

        if (!_arguments.empty())
        {
            std::cout << "Arguments:\n";
            for (const auto &arg : _arguments)
            {
                std::cout << "  ";
                if (arg.is_flag)
                {
                    std::cout << "--" << arg.name;
                    if (!arg.aliases.empty())
                    {
                        std::cout << ", -" << arg.aliases[0];
                    }
                }
                else
                {
                    std::cout << (arg.required ? "<" : "[") << arg.name << (arg.required ? ">" : "]");
                }

                std::cout << "\n      " << arg.description;

                if (!arg.default_value.empty())
                {
                    std::cout << " (default: " << arg.default_value << ")";
                }
                std::cout << "\n";
            }
            std::cout << "\n";
        }
    }

    // ================================================================
    // ArgumentParser Implementation
    // ================================================================

    ParsedArgs ArgumentParser::parse(int argc, char *argv[], const Command *command)
    {
        ParsedArgs args;

        for (int i = 1; i < argc; ++i)
        {
            std::string arg = argv[i];

            if (is_flag(arg))
            {
                std::string flag_name = normalize_flag(arg);

                // Check if it's a flag with value (--flag=value)
                size_t eq_pos = flag_name.find('=');
                if (eq_pos != std::string::npos)
                {
                    std::string key = flag_name.substr(0, eq_pos);
                    std::string value = flag_name.substr(eq_pos + 1);
                    args.set_arg(key, value);
                }
                // Handle special compilation flags that expect values
                else if ((flag_name == "o" || flag_name == "output") && i + 1 < argc && !is_flag(argv[i + 1]))
                {
                    args.set_arg(flag_name, argv[++i]);
                }
                // Handle boolean flags
                else if (flag_name == "c" || flag_name == "compile-only" ||
                         flag_name == "ast" || flag_name == "show-ast" ||
                         flag_name == "symbols" || flag_name == "show-symbols" ||
                         flag_name == "ir" || flag_name == "show-ir" ||
                         flag_name == "emit-llvm" ||
                         flag_name == "analyze" ||
                         flag_name == "help" || flag_name == "h" ||
                         flag_name == "version" || flag_name == "v" ||
                         flag_name == "no-std" || flag_name == "stdlib-mode")
                {
                    args.set_flag(flag_name, true);
                }
                else
                {
                    // Check if next argument is not a flag (value for this flag)
                    if (i + 1 < argc && !is_flag(argv[i + 1]))
                    {
                        args.set_arg(flag_name, argv[++i]);
                    }
                    else
                    {
                        args.set_flag(flag_name, true);
                    }
                }
            }
            else
            {
                args.add_positional(arg);
            }
        }

        return args;
    }

    bool ArgumentParser::validate_args(const ParsedArgs &args, const Command *command)
    {
        if (!command)
            return true;

        for (const auto &expected_arg : command->arguments())
        {
            if (expected_arg.required)
            {
                if (expected_arg.is_flag)
                {
                    if (!args.has_flag(expected_arg.name))
                    {
                        print_validation_error("Required flag --" + expected_arg.name + " is missing");
                        return false;
                    }
                }
                else
                {
                    if (!args.has_arg(expected_arg.name) && args.positional_count() == 0)
                    {
                        print_validation_error("Required argument <" + expected_arg.name + "> is missing");
                        return false;
                    }
                }
            }
        }

        return true;
    }

    void ArgumentParser::print_validation_error(const std::string &error)
    {
        std::cerr << "Error: " << error << std::endl;
    }

    bool ArgumentParser::is_flag(const std::string &arg)
    {
        return arg.length() > 1 && arg[0] == '-';
    }

    std::string ArgumentParser::normalize_flag(const std::string &flag)
    {
        if (flag.length() > 2 && flag.substr(0, 2) == "--")
        {
            return flag.substr(2); // Remove --
        }
        else if (flag.length() > 1 && flag[0] == '-')
        {
            return flag.substr(1); // Remove -
        }
        return flag;
    }

    // ================================================================
    // CLIRunner Implementation
    // ================================================================

    CLIRunner::CLIRunner(const std::string &program_name, const std::string &version)
        : _program_name(program_name), _version(version)
    {
    }

    int CLIRunner::run(int argc, char *argv[])
    {
        if (argc < 2)
        {
            print_help();
            return 0;
        }

        // Parse all arguments to check for flags and files
        ParsedArgs args = ArgumentParser::parse(argc, argv);

        // Handle global flags first
        if (args.get_flag("help") || args.get_flag("h"))
        {
            if (args.positional_count() > 0)
            {
                // Help for specific command
                Command *command = find_command(args.positional()[0]);
                if (command)
                {
                    command->print_help();
                    return 0;
                }
            }
            print_help();
            return 0;
        }

        if (args.get_flag("version") || args.get_flag("v"))
        {
            print_version();
            return 0;
        }

        std::string first_arg = argv[1];

        // Check if it's a direct file compilation with or without flags
        if (is_cryo_file(first_arg) ||
            (args.positional_count() > 0 && is_cryo_file(args.input_file())) ||
            (first_arg.find('.') != std::string::npos && first_arg != "help" && find_command(first_arg) == nullptr))
        {
            return execute_file_compilation(args);
        }

        // Handle traditional command-based execution
        Command *command = find_command(first_arg);
        if (!command)
        {
            std::cerr << "Error: Unknown command '" << first_arg << "'" << std::endl;
            std::cerr << "Run 'cryo help' to see available commands." << std::endl;
            return 1;
        }

        // Parse arguments for the specific command (skip command name)
        ParsedArgs cmd_args = ArgumentParser::parse(argc - 1, argv + 1, command);

        // Validate arguments
        if (!ArgumentParser::validate_args(cmd_args, command))
        {
            std::cerr << "\nRun 'cryo help " << first_arg << "' for usage information." << std::endl;
            return 1;
        }

        try
        {
            return command->execute(cmd_args);
        }
        catch (const std::exception &e)
        {
            std::cerr << "Error executing command '" << first_arg << "': " << e.what() << std::endl;
            return 1;
        }
    }

    void CLIRunner::print_help() const
    {
        std::cout << _program_name << " " << _version << "\n";
        if (!_description.empty())
        {
            std::cout << _description << "\n";
        }
        std::cout << "\nUsage:\n";
        std::cout << "  cryo [command] [options]\n";
        std::cout << "  cryo <file.cryo>                    # Direct compilation\n";
        std::cout << "  cryo <file.cryo> [flags]            # Compile with flags\n";
        std::cout << "  cryo <file.cryo> -o <output>        # Specify output file\n";
        std::cout << "  cryo -c <file.cryo>                 # Compile only (no linking)\n\n";

        std::cout << "Compilation flags:\n";
        std::cout << "  --ast                               Show AST during compilation\n";
        std::cout << "  --symbols                           Show symbol table during compilation\n";
        std::cout << "  --ir                                Show generated LLVM IR\n";
        std::cout << "  --emit-llvm                         Emit LLVM bitcode (.bc) file\n";
        std::cout << "  --analyze                           Output detailed analysis in JSON format (for LSP)\n";
        std::cout << "  -c, --compile-only                  Compile only, don't link\n";
        std::cout << "  -o, --output <file>                 Output file name\n\n";

        std::cout << "Available commands:\n";
        for (const auto &[name, command] : _commands)
        {
            std::cout << "  " << std::left << std::setw(12) << name
                      << command->description() << "\n";
        }

        std::cout << "\nGlobal options:\n";
        std::cout << "  --help, -h                 Show this help message\n";
        std::cout << "  --version, -v              Show version information\n";
        std::cout << "\nUse 'cryo help <command>' for more information about a specific command.\n";
    }

    void CLIRunner::print_version() const
    {
        std::cout << _program_name << " " << _version << std::endl;
        std::cout << "Built with LLVM support" << std::endl;
        std::cout << "Copyright (c) 2025 Jake LeQuire" << std::endl;
    }

    Command *CLIRunner::find_command(const std::string &name) const
    {
        auto it = _commands.find(name);
        return (it != _commands.end()) ? it->second.get() : nullptr;
    }

    int CLIRunner::execute_file_compilation(const ParsedArgs &args)
    {
        std::string file_path = args.input_file();

        if (file_path.empty())
        {
            std::cerr << "Error: No input file specified" << std::endl;
            return 1;
        }

        if (!is_cryo_file(file_path))
        {
            std::cerr << "Error: Input file must have .cryo extension" << std::endl;
            return 1;
        }

        std::cout << "Cryo Compiler v" << _version << std::endl;
        std::cout << "Compiling: " << file_path;

        std::string output = args.output_file();
        if (!output.empty())
        {
            std::cout << " -> " << output;
        }
        if (args.compile_only())
        {
            std::cout << " (compile only)";
        }
        std::cout << std::endl;

        auto compiler = Cryo::create_compiler_instance();
        compiler->set_debug_mode(true);

        // Set AST printing flag if requested
        if (args.show_ast())
        {
            compiler->set_show_ast_before_ir(true);
        }

        // Set stdlib linking flag (disabled if --no-std is present)
        if (args.get_flag("no-std"))
        {
            compiler->set_stdlib_linking(false);
            std::cout << "[INFO] Standard library linking disabled by --no-std flag" << std::endl;
        }

        // Set stdlib compilation mode (generates full implementations for imports)
        if (args.get_flag("stdlib-mode"))
        {
            compiler->set_stdlib_compilation_mode(true);
            std::cout << "[INFO] Standard library compilation mode enabled" << std::endl;
        }

        bool compilation_success = compiler->compile_file(file_path);

        if (compilation_success)
        {
            std::cout << "\nCompilation successful!" << std::endl;
        }
        else
        {
            std::cerr << "\n❌ Compilation failed!" << std::endl;
        }

        // Show outputs based on requested flags - regardless of compilation success
        // This is useful for debugging, especially when IR generation fails

        if (args.show_symbols() && compilation_success)
        {
            std::cout << "\nSymbol Table:" << std::endl;
            compiler->dump_symbol_table();
            compiler->dump_type_table();
        }

        if (args.show_ir())
        {
            std::cout << "\nGenerated LLVM IR:" << std::endl;
            compiler->dump_ir();
        }

        if (args.get_flag("analyze"))
        {
            std::cout << "\nAnalysis flag detected - using direct compiler integration for LSP features." << std::endl;
        }

        if (compilation_success)
        {

            // Handle --emit-llvm flag to emit bitcode
            if (args.get_flag("emit-llvm"))
            {
                std::string output_path = args.output_file();

                // If no -o flag specified, use input file path with .bc extension
                if (output_path.empty())
                {
                    output_path = file_path;
                    // Change extension from .cryo to .bc
                    size_t pos = output_path.find_last_of('.');
                    if (pos != std::string::npos)
                    {
                        output_path = output_path.substr(0, pos) + ".bc";
                    }
                    else
                    {
                        output_path += ".bc";
                    }
                }
// If -o flag specified, use it as is (assume .bc extension already included or will be added)

// Normalize path separators for Windows
#ifdef _WIN32
                std::replace(output_path.begin(), output_path.end(), '/', '\\');
#endif

                std::cout << "\nEmitting LLVM bitcode to: " << output_path << std::endl;

                if (compiler->codegen() && compiler->codegen()->emit_llvm_ir(output_path))
                {
                    std::cout << "✓ LLVM bitcode emitted successfully: " << output_path << std::endl;
                }
                else
                {
                    std::cerr << "\n❌ Failed to emit LLVM bitcode" << std::endl;
                    if (compiler->codegen())
                    {
                        std::cerr << "Error: " << compiler->codegen()->get_last_error() << std::endl;
                    }
                    return 1;
                }
            }

            // Generate executable if output file is specified and not compile-only mode
            if (!args.output_file().empty() && !args.compile_only())
            {
                std::cout << "\nGenerating executable: " << args.output_file() << std::endl;

                auto target = Cryo::Linker::CryoLinker::LinkTarget::Executable;

                if (compiler->generate_output(args.output_file(), target))
                {
                    std::cout << "✓ Executable generated successfully: " << args.output_file() << std::endl;
                }
                else
                {
                    std::cerr << "\n❌ Executable generation failed!" << std::endl;
                    compiler->print_diagnostics();
                    return 1;
                }
            }

            // If no specific flags requested, show minimal output for clang-like behavior
            if (!has_compilation_flags(args))
            {
                // Just success message, no extra output for clang-like behavior
            }

            return 0;
        }
        else
        {
            compiler->print_diagnostics();
            return 1;
        }
    }

    int CLIRunner::execute_file_compilation(const std::string &file_path)
    {
        // Legacy method - create ParsedArgs and delegate
        ParsedArgs args;
        args.add_positional(file_path);

        // For backward compatibility, show AST and symbols like before
        args.set_flag("ast", true);
        args.set_flag("symbols", true);

        return execute_file_compilation(args);
    }

    bool CLIRunner::has_compilation_flags(const ParsedArgs &args) const
    {
        return args.show_ast() || args.show_symbols() || args.show_ir() || args.compile_only() || !args.output_file().empty();
    }

    bool CLIRunner::is_cryo_file(const std::string &path) const
    {
        size_t dot_pos = path.find_last_of('.');
        if (dot_pos == std::string::npos)
            return false;

        std::string extension = path.substr(dot_pos + 1);
        std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);
        return extension == "cryo";
    }

    // ================================================================
    // Utility Functions
    // ================================================================

    std::unique_ptr<CLIRunner> create_cli()
    {
        auto cli = std::make_unique<CLIRunner>("Cryo", "0.1.0");
        cli->set_description("A modern systems programming language with safety and performance.");

        // Register commands (will be implemented in Commands.cpp)
        cli->register_command<Commands::VersionCommand>();
        cli->register_command<Commands::CheckCommand>();
        cli->register_command<Commands::ASTCommand>();
        cli->register_command<Commands::InitCommand>();
        cli->register_command<Commands::BuildCommand>();

        return cli;
    }

} // namespace Cryo::CLI
