#include "CLI/Commands.hpp"
#include "CLI/CLI.hpp"
#include "Compiler/CompilerInstance.hpp"
#include "Utils/Logger.hpp"
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
        argument(CLIArgument("trace", "Enable trace-level logging (most verbose)", false).flag());
        argument(CLIArgument("log-file", "Write logs to specified file", false));
        argument(CLIArgument("log-component", "Filter logs by component (e.g., CODEGEN, PARSER)", false));
        argument(CLIArgument("ast", "Display AST after compilation", false).flag());
        argument(CLIArgument("symbols", "Display symbol table after compilation", false).flag());
        argument(CLIArgument("types", "Display type table after compilation", false).flag());
        argument(CLIArgument("emit-llvm", "Emit LLVM bitcode (.bc) instead of executable", false).flag());
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
        bool trace = args.get_flag("trace");
        std::string log_file = args.get_arg("log-file");
        std::string log_component_str = args.get_arg("log-component");

        // Configure logger based on CLI flags
        Cryo::LogLevel log_level = Cryo::LogLevel::NONE;
        if (trace)
        {
            log_level = Cryo::LogLevel::TRACE;
        }
        else if (debug)
        {
            log_level = Cryo::LogLevel::DEBUG;
        }
        else if (verbose)
        {
            log_level = Cryo::LogLevel::INFO;
        }

        // Parse component filter if provided
        Cryo::LogComponent log_component = Cryo::LogComponent::ALL;
        if (!log_component_str.empty())
        {
            // Convert string to LogComponent enum
            if (log_component_str == "CODEGEN")
                log_component = Cryo::LogComponent::CODEGEN;
            else if (log_component_str == "PARSER")
                log_component = Cryo::LogComponent::PARSER;
            else if (log_component_str == "LEXER")
                log_component = Cryo::LogComponent::LEXER;
            else if (log_component_str == "AST")
                log_component = Cryo::LogComponent::AST;
            else if (log_component_str == "TYPECHECKER")
                log_component = Cryo::LogComponent::TYPECHECKER;
            else if (log_component_str == "LINKER")
                log_component = Cryo::LogComponent::LINKER;
            else if (log_component_str == "OPTIMIZER")
                log_component = Cryo::LogComponent::OPTIMIZER;
            else if (log_component_str == "GENERAL")
                log_component = Cryo::LogComponent::GENERAL;
            // Add others as needed
        }

        // Reconfigure logger if any logging is enabled
        if (log_level != Cryo::LogLevel::NONE)
        {
            Cryo::LoggerConfig config;
            config.console_level = log_level;
            config.file_level = log_level;
            config.log_file_path = log_file; // Empty string = no file
            config.enable_colors = true;
            config.enable_timestamps = true;
            config.enable_component_tags = true;

            // Set component filter if specified (leave empty for ALL)
            if (log_component != Cryo::LogComponent::ALL)
            {
                // Only enable the requested component
                config.component_filters[log_component] = true;
            }
            // If ALL, leave component_filters empty (logs all components)

            Cryo::Logger::instance().initialize(config);
        }

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

            // Handle --emit-llvm flag to emit bitcode
            if (args.get_flag("emit-llvm"))
            {
                std::string output_path = args.output_file();

                // If no -o flag specified, use input file path with .bc extension
                if (output_path.empty())
                {
                    output_path = input_file;
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

                std::cout << "\nEmitting LLVM bitcode to: " << output_path << std::endl;

                if (compiler->codegen() && compiler->codegen()->emit_llvm_ir(output_path))
                {
                    std::cout << "✓ LLVM bitcode emitted successfully: " << output_path << std::endl;
                }
                else
                {
                    std::cerr << "❌ Failed to emit LLVM bitcode" << std::endl;
                    if (compiler->codegen())
                    {
                        std::cerr << "Error: " << compiler->codegen()->get_last_error() << std::endl;
                    }
                    return 1;
                }
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

    // ================================================================
    // Init Command
    // ================================================================

    InitCommand::InitCommand()
        : Command("init", "Initialize a new Cryo project")
    {
        usage("[project_name]");
        argument(CLIArgument("project_name", "Name of the project to create", false));
        argument(CLIArgument("force", "Overwrite existing files if they exist", false).flag().alias("f"));
    }

    int InitCommand::execute(const ParsedArgs &args)
    {
        std::string project_name = "hello_world";

        if (args.positional_count() > 0)
        {
            project_name = args.positional()[0];
        }

        std::cout << "Initializing Cryo project: " << project_name << std::endl;

        return create_project(project_name, args);
    }

    int InitCommand::create_project(const std::string &project_name, const ParsedArgs &args)
    {
        bool force = args.get_flag("force");

        // Check if cryoconfig already exists
        if (!force && std::filesystem::exists("cryoconfig"))
        {
            std::cerr << "Error: cryoconfig already exists. Use --force to overwrite." << std::endl;
            return 1;
        }

        // Create directory structure
        if (!create_directory_structure())
        {
            std::cerr << "Error: Failed to create directory structure" << std::endl;
            return 1;
        }

        // Create cryoconfig file
        if (!create_cryoconfig_file(project_name))
        {
            std::cerr << "Error: Failed to create cryoconfig file" << std::endl;
            return 1;
        }

        // Create main.cryo file
        if (!create_main_cryo_file())
        {
            std::cerr << "Error: Failed to create src/main.cryo file" << std::endl;
            return 1;
        }

        std::cout << "✓ Project '" << project_name << "' initialized successfully!" << std::endl;
        std::cout << "  - Created cryoconfig" << std::endl;
        std::cout << "  - Created src/main.cryo" << std::endl;
        std::cout << "\nTo build your project, run: cryo build" << std::endl;

        return 0;
    }

    bool InitCommand::create_cryoconfig_file(const std::string &project_name)
    {
        try
        {
            std::ofstream config_file("cryoconfig");
            if (!config_file.is_open())
            {
                return false;
            }

            config_file << "# Cryo Project Configuration\n";
            config_file << "# This file defines the build configuration for your Cryo project\n\n";
            config_file << "project_name = \"" << project_name << "\"\n";
            config_file << "main_file = \"src/main.cryo\"\n";
            config_file << "output_dir = \"build\"\n";
            config_file << "target_type = \"executable\"\n\n";
            config_file << "# Compiler options\n";
            config_file << "[compiler]\n";
            config_file << "debug = false\n";
            config_file << "optimize = true\n";
            config_file << "emit_llvm = false\n\n";
            config_file << "# Dependencies (future feature)\n";
            config_file << "[dependencies]\n";
            config_file << "# Add dependencies here\n";

            config_file.close();
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    bool InitCommand::create_main_cryo_file()
    {
        try
        {
            std::ofstream main_file("src/main.cryo");
            if (!main_file.is_open())
            {
                return false;
            }

            main_file << "namespace Main;\n\n";
            main_file << "import IO from <io/stdio>\n\n";
            main_file << "\n";
            main_file << "function main() -> int {\n";
            main_file << "    IO::println(\"Hello, world!\");\n";
            main_file << "    return 0;\n";
            main_file << "}\n";

            main_file.close();
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    bool InitCommand::create_directory_structure()
    {
        try
        {
            // Create src directory
            std::filesystem::create_directories("src");
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    // ================================================================
    // Build Command
    // ================================================================

    BuildCommand::BuildCommand()
        : Command("build", "Build a Cryo project")
    {
        usage("[options]");
        argument(CLIArgument("debug", "Build in debug mode", false).flag().alias("d"));
        argument(CLIArgument("release", "Build in release mode (optimized)", false).flag().alias("r"));
        argument(CLIArgument("verbose", "Show verbose build output", false).flag().alias("v"));
        argument(CLIArgument("clean", "Clean build artifacts before building", false).flag().alias("c"));
    }

    int BuildCommand::execute(const ParsedArgs &args)
    {
        std::cout << "Building Cryo project..." << std::endl;

        return build_project(args);
    }

    int BuildCommand::build_project(const ParsedArgs &args)
    {
        bool verbose = args.get_flag("verbose");
        bool debug = args.get_flag("debug");
        bool clean = args.get_flag("clean");

        // Find cryoconfig file
        std::string config_path;
        if (!find_cryoconfig_file(config_path))
        {
            std::cerr << "Error: No cryoconfig file found. Run 'cryo init' to create a new project." << std::endl;
            return 1;
        }

        if (verbose)
        {
            std::cout << "Found cryoconfig: " << config_path << std::endl;
        }

        // Parse cryoconfig
        std::string exe_name, main_file;
        if (!parse_cryoconfig(config_path, exe_name, main_file))
        {
            std::cerr << "Error: Failed to parse cryoconfig file" << std::endl;
            return 1;
        }

        if (verbose)
        {
            std::cout << "Project name: " << exe_name << std::endl;
            std::cout << "Main file: " << main_file << std::endl;
        }

        // Check if main file exists
        if (!std::filesystem::exists(main_file))
        {
            std::cerr << "Error: Main file '" << main_file << "' not found" << std::endl;
            return 1;
        }

        // Ensure build directory exists
        if (!ensure_build_directory())
        {
            std::cerr << "Error: Failed to create build directory" << std::endl;
            return 1;
        }

        // Clean if requested
        if (clean && std::filesystem::exists("build"))
        {
            if (verbose)
            {
                std::cout << "Cleaning build directory..." << std::endl;
            }
            try
            {
                std::filesystem::remove_all("build");
                std::filesystem::create_directories("build");
            }
            catch (...)
            {
                std::cerr << "Warning: Failed to clean build directory" << std::endl;
            }
        }

        // Build the project using the compiler
        std::string output_path = "build/" + exe_name;

        auto compiler = Cryo::create_compiler_instance();
        compiler->set_debug_mode(debug);

        // Enable standard library linking by default
        compiler->set_stdlib_linking(true);
        
        // Use auto-detection to find stdlib location
        if (!compiler->module_loader()->auto_detect_stdlib_root())
        {
            std::cerr << "Warning: Could not auto-detect stdlib location, using fallback path" << std::endl;
        }
        
        compiler->module_loader()->set_current_file(std::filesystem::absolute(main_file).string());        if (verbose)
        {
            std::cout << "Compiling " << main_file << " -> " << output_path << std::endl;
        }

        bool compilation_success = compiler->compile_file(main_file);

        if (!compilation_success)
        {
            std::cerr << "❌ Compilation failed!" << std::endl;
            compiler->print_diagnostics();
            return 1;
        }

        // Generate executable
        auto target = Cryo::Linker::CryoLinker::LinkTarget::Executable;

        if (compiler->generate_output(output_path, target))
        {
            std::cout << "✓ Build successful: " << output_path << std::endl;
            return 0;
        }
        else
        {
            std::cerr << "❌ Build failed during linking!" << std::endl;
            compiler->print_diagnostics();
            return 1;
        }
    }

    bool BuildCommand::find_cryoconfig_file(std::string &config_path)
    {
        // Look for cryoconfig in current directory
        if (std::filesystem::exists("cryoconfig"))
        {
            config_path = "cryoconfig";
            return true;
        }

        // Could extend this to search parent directories in the future
        return false;
    }

    bool BuildCommand::parse_cryoconfig(const std::string &config_path, std::string &exe_name, std::string &main_file)
    {
        try
        {
            std::ifstream config_file(config_path);
            if (!config_file.is_open())
            {
                return false;
            }

            std::string line;
            exe_name = "app";            // default
            main_file = "src/main.cryo"; // default

            while (std::getline(config_file, line))
            {
                // Skip comments and empty lines
                if (line.empty() || line[0] == '#' || line[0] == '[')
                {
                    continue;
                }

                // Simple key = value parsing
                size_t eq_pos = line.find('=');
                if (eq_pos != std::string::npos)
                {
                    std::string key = line.substr(0, eq_pos);
                    std::string value = line.substr(eq_pos + 1);

                    // Trim whitespace and quotes
                    key.erase(0, key.find_first_not_of(" \t"));
                    key.erase(key.find_last_not_of(" \t") + 1);

                    value.erase(0, value.find_first_not_of(" \t\""));
                    value.erase(value.find_last_not_of(" \t\"") + 1);

                    if (key == "project_name")
                    {
                        exe_name = value;
                    }
                    else if (key == "main_file")
                    {
                        main_file = value;
                    }
                }
            }

            config_file.close();
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    bool BuildCommand::ensure_build_directory()
    {
        try
        {
            std::filesystem::create_directories("build");
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

} // namespace Cryo::CLI::Commands
