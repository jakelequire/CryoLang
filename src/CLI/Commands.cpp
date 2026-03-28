#include "CLI/Commands.hpp"
#include "CLI/CLI.hpp"
#include "CLI/ConfigParser.hpp"
#include "Compiler/CompilerInstance.hpp"
#include "Utils/Logger.hpp"
#include "Utils/OS.hpp"
#include "Utils/SymbolDumper.hpp"
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
            std::cout << "  Platform: " << Cryo::Utils::OS::instance().get_platform_name() << std::endl;

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
        argument(CLIArgument("target", "Target triple (e.g., wasm32-unknown-emscripten)", false));
        argument(CLIArgument("emit-wasm", "Compile to WebAssembly (.wasm)", false).flag());
        argument(CLIArgument("wasm-exports", "Comma-separated list of functions to export to JavaScript", false));
        argument(CLIArgument("enable-js-interop", "Enable JavaScript interoperability", false).flag());
        argument(CLIArgument("lsp", "LSP compilation mode (frontend only, no executable generation)", false).flag());
        argument(CLIArgument("raw", "Compile without standard library/runtime, no main->_user_main_ transform", false).flag());
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

        // Debug: Print what flags we detected
        if (debug || verbose || trace)
        {
            std::cout << "[DEBUG] Logging flags detected: debug=" << debug
                      << ", verbose=" << verbose << ", trace=" << trace << std::endl;
        }

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
            config.enable_colors = !args.get_flag("no-color");
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

            // Test that logging is working
            LOG_DEBUG(Cryo::LogComponent::CLI, "Debug logging enabled successfully!");
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

        // Check for raw mode
        bool raw_mode = args.get_flag("raw");
        if (raw_mode)
        {
            compiler->set_raw_mode(true);
            if (verbose || debug)
            {
                std::cout << "[RAW] Raw mode enabled - no stdlib linking or main transform" << std::endl;
            }
        }

        // Check for LSP mode
        bool lsp_mode = args.get_flag("lsp");

        // Compile
        bool success;
        if (lsp_mode)
        {
            if (verbose || debug)
            {
                std::cout << "[LSP] Compiling in LSP mode (frontend only)..." << std::endl;
            }
            compiler->set_lsp_mode(true);
            success = compiler->compile_for_lsp(input_file);
        }
        else
        {
            success = compiler->compile_file(input_file);
        }

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
            // LSP mode: show LSP-specific information
            if (lsp_mode)
            {
                if (verbose || debug)
                {
                    std::cout << "\n[LSP] Compilation completed successfully" << std::endl;

                    // Show LSP diagnostics
                    if (compiler->diagnostics())
                    {
                        auto lsp_diagnostics = compiler->diagnostics()->to_lsp();
                        std::cout << "[LSP] Found " << lsp_diagnostics.size() << " diagnostics" << std::endl;
                        for (const auto &diag : lsp_diagnostics)
                        {
                            std::cout << "  " << diag.severity << " at " << diag.start_line << ":" << diag.start_col << ": " << diag.message << std::endl;
                        }
                    }

                    // Show symbol count
                    if (compiler->symbol_table())
                    {
                        auto symbols = compiler->symbol_table()->get_all_symbols_for_lsp();
                        std::cout << "[LSP] Found " << symbols.size() << " symbols" << std::endl;
                    }
                }
                return 0; // LSP mode doesn't need executable generation
            }

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

            // Handle raw mode output generation
            if (raw_mode)
            {
                std::string output_path = args.output_file();

                if (!output_path.empty())
                {
                    // Determine output type based on extension
                    std::filesystem::path path_obj(output_path);
                    std::string extension = path_obj.extension().string();
                    if (!extension.empty() && extension[0] == '.')
                    {
                        extension = extension.substr(1); // Remove the leading dot
                    }
                    std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);

                    std::cout << "\n[RAW] Generating raw output to: " << output_path << std::endl;

                    bool output_success = false;
                    if (extension == "ll")
                    {
                        // Generate LLVM IR text
                        output_success = compiler->codegen() && compiler->codegen()->emit_llvm_ir(output_path);
                    }
                    else if (extension == "bc")
                    {
                        // Generate LLVM bitcode
                        output_success = compiler->codegen() && compiler->codegen()->emit_llvm_ir(output_path);
                    }
                    else if (extension == "o" || extension == "obj")
                    {
                        // Generate object file using linker
                        auto target = Cryo::Linker::CryoLinker::LinkTarget::ObjectFile;
                        output_success = compiler->generate_output(output_path, target);
                    }
                    else if (extension == "asm" || extension == "s")
                    {
                        // Generate assembly file using linker
                        output_success = compiler->linker() && compiler->linker()->generate_assembly_file(compiler->codegen()->get_module(), output_path);
                    }
                    else if (extension == "exe" || extension == "out" || extension == "")
                    {
                        // Generate executable binary
                        auto target = Cryo::Linker::CryoLinker::LinkTarget::Executable;
                        output_success = compiler->generate_output(output_path, target);
                    }
                    else
                    {
                        // Default to object file for unknown extensions
                        std::cout << "[RAW] Unknown extension '" << extension << "', defaulting to object file" << std::endl;
                        auto target = Cryo::Linker::CryoLinker::LinkTarget::ObjectFile;
                        output_success = compiler->generate_output(output_path, target);
                    }

                    if (output_success)
                    {
                        std::cout << "✓ Raw output generated successfully: " << output_path << std::endl;
                    }
                    else
                    {
                        std::cerr << "❌ Failed to generate raw output" << std::endl;
                        if (compiler->codegen())
                        {
                            std::cerr << "Error: " << compiler->codegen()->get_last_error() << std::endl;
                        }
                        return 1;
                    }
                }
                else
                {
                    std::cout << "\n[RAW] Raw mode enabled but no output file specified. Use -o flag to specify output file." << std::endl;
                    std::cout << "[RAW] Supported extensions: .o/.obj (object file), .ll (LLVM IR), .bc (LLVM bitcode), .asm/.s (assembly)" << std::endl;
                }

                return 0; // Raw mode doesn't need executable generation, exit early
            }

            return 0;
        }
        else
        {
            compiler->print_diagnostics();

            // Even on failure, attempt to emit partial LLVM IR if --emit-llvm was requested
            if (args.get_flag("emit-llvm") && compiler->codegen())
            {
                std::string output_path = args.output_file();
                if (output_path.empty())
                {
                    output_path = input_file;
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

                std::cout << "\nAttempting to emit partial LLVM IR despite errors..." << std::endl;
                if (compiler->codegen()->emit_llvm_ir(output_path))
                {
                    std::cout << "✓ Partial LLVM IR emitted: " << output_path << std::endl;
                }
                else
                {
                    std::cerr << "⚠ Could not emit partial LLVM IR (no IR available)" << std::endl;
                }
            }

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

        std::cout << "Platform: " << Cryo::Utils::OS::instance().get_platform_name() << std::endl;
        std::cout << "Architecture: " << Cryo::Utils::OS::instance().get_architecture_name() << std::endl;

        // Current working directory
        std::cout << "Working Directory: " << Cryo::Utils::OS::instance().get_working_directory() << std::endl;
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
        if (!ConfigParser::create_config(project_name, "cryoconfig"))
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
            main_file << "\n";
            main_file << "function main() -> int {\n";
            main_file << "    println(\"Hello, world!\");\n";
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
            // Create src directory using OS utility
            auto &os = Cryo::Utils::OS::instance();
            os.create_directories("src");
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
        argument(CLIArgument("emit-llvm", "Emit LLVM IR files (.ll and .bc) to build directory", false).flag());
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
        CryoConfig config;
        if (!parse_cryoconfig(config_path, config))
        {
            std::cerr << "Error: Failed to parse cryoconfig file" << std::endl;
            return 1;
        }

        // Check for debug flag in config args array
        bool config_has_debug = config.debug;
        for (const auto &arg : config.args)
        {
            if (arg == "--debug" || arg == "-d")
            {
                config_has_debug = true;
                break;
            }
        }

        // Determine final debug/verbose state (CLI flags override config)
        bool use_debug = debug || config_has_debug;
        bool use_verbose = verbose || use_debug; // Debug implies verbose

        // Configure logger based on debug/verbose flags
        if (use_debug || use_verbose)
        {
            Cryo::LogLevel log_level = use_debug ? Cryo::LogLevel::DEBUG : Cryo::LogLevel::INFO;

            Cryo::LoggerConfig logger_config;
            logger_config.console_level = log_level;
            logger_config.file_level = log_level;
            logger_config.log_file_path = ""; // No file logging by default
            logger_config.enable_colors = !args.get_flag("no-color");
            logger_config.enable_timestamps = true;
            logger_config.enable_component_tags = true;

            Cryo::Logger::instance().initialize(logger_config);

            // Test that logging is working
            if (use_debug)
            {
                LOG_DEBUG(Cryo::LogComponent::CLI, "Debug logging enabled for build command!");
            }
            else
            {
                LOG_INFO(Cryo::LogComponent::CLI, "Verbose logging enabled for build command!");
            }
        }

        // Use entry_point from config (or default)
        std::string main_file = config.entry_point;
        std::string project_name = config.project_name.empty() ? "app" : config.project_name;
        bool is_library = (config.target_type == "static_library" || config.target_type == "shared_library");
        bool is_stdlib = (config.target_type == "stdlib");

        if (use_verbose)
        {
            std::cout << "Project name: " << project_name << std::endl;
            std::cout << "Target type: " << config.target_type << std::endl;
            std::cout << "Entry point: " << main_file << std::endl;
            std::cout << "Source directory: " << config.source_dir << std::endl;
            std::cout << "Output directory: " << config.output_dir << std::endl;
            std::cout << "Debug mode: " << (use_debug ? "enabled" : "disabled") << std::endl;
            std::cout << "Config debug: " << (config.debug ? "enabled" : "disabled") << std::endl;
            std::cout << "Optimization: " << (config.optimize ? "enabled" : "disabled") << std::endl;
            std::cout << "Stdlib mode: " << (config.stdlib_mode ? "enabled" : "disabled") << std::endl;
            std::cout << "No-std: " << (config.no_std ? "enabled" : "disabled") << std::endl;
            if (!config.args.empty())
            {
                std::cout << "Compiler args: ";
                for (const auto &arg : config.args)
                {
                    std::cout << arg << " ";
                }
                std::cout << std::endl;
            }
        }

        // Check if entry point file exists (skip for stdlib compilation)
        if (!is_stdlib && !std::filesystem::exists(main_file))
        {
            std::cerr << "Error: Entry point file '" << main_file << "' not found" << std::endl;
            return 1;
        }

        // Ensure build directory exists
        auto &os = Cryo::Utils::OS::instance();
        if (!os.path_exists(config.output_dir))
        {
            try
            {
                os.create_directories(config.output_dir);
            }
            catch (...)
            {
                std::cerr << "Error: Failed to create output directory: " << config.output_dir << std::endl;
                return 1;
            }
        }

        // Clean if requested
        if (clean && os.path_exists(config.output_dir))
        {
            if (use_verbose)
            {
                std::cout << "Cleaning " << config.output_dir << " directory..." << std::endl;
            }
            try
            {
                std::filesystem::remove_all(config.output_dir);
                os.create_directories(config.output_dir);
            }
            catch (...)
            {
                std::cerr << "Warning: Failed to clean " << config.output_dir << " directory" << std::endl;
            }
        }

        // Build the project using the compiler
        std::string output_path;
        if (is_stdlib)
        {
            // Standard library: lib<name>.a
            output_path = os.join_path(config.output_dir, "lib" + project_name + ".a");
        }
        else if (is_library)
        {
            // Static library: lib<name>.a
            output_path = os.join_path(config.output_dir, "lib" + project_name + ".a");
        }
        else
        {
            // Executable: <name> (with platform extension)
            std::string exe_name_with_ext = os.make_executable_name(project_name);
            output_path = os.join_path(config.output_dir, exe_name_with_ext);
        }

        auto compiler = Cryo::create_compiler_instance();

        // Use config settings for debug mode (but allow CLI override)
        compiler->set_debug_mode(use_debug);

        // Handle stdlib_mode: compile as stdlib component
        if (config.stdlib_mode)
        {
            compiler->set_stdlib_compilation_mode(true);
            if (use_verbose)
            {
                std::cout << "Compiling in stdlib mode" << std::endl;
            }
        }

        // Configure symbol dumping from config
        if (config.dump_symbols)
        {
            compiler->set_dump_symbols(true, config.output_dir);
            if (use_verbose)
            {
                std::cout << "Symbol table dumps enabled" << std::endl;
            }
        }

        // When building stdlib itself, show all stdlib diagnostics
        if (is_stdlib)
        {
            compiler->set_show_stdlib_diagnostics(true);
        }

        // Handle no_std: disable stdlib linking, auto-imports, and main transform
        if (config.no_std)
        {
            compiler->set_stdlib_linking(false);
            compiler->set_auto_imports_enabled(false);
            compiler->set_raw_mode(true);
            if (use_verbose)
            {
                std::cout << "Stdlib linking disabled (no-std mode)" << std::endl;
            }
        }
        else
        {
            // Enable standard library linking by default
            compiler->set_stdlib_linking(true);

            // Use auto-detection to find stdlib location
            if (!compiler->module_loader()->auto_detect_stdlib_root())
            {
                std::cerr << "Warning: Could not auto-detect stdlib location, using fallback path" << std::endl;
            }
        }

        bool compilation_success;
        if (is_stdlib)
        {
            // For stdlib compilation, use directory-based compilation
            if (use_verbose)
            {
                std::cout << "Compiling stdlib from " << config.source_dir << " -> " << output_path << std::endl;
            }
            compilation_success = compiler->compile_stdlib(config.source_dir, output_path);
        }
        else
        {
            // For regular files, use single-file compilation
            compiler->module_loader()->set_current_file(std::filesystem::absolute(main_file).string());
            // Set project root for module resolution (directory containing cryoconfig)
            compiler->module_loader()->set_project_root(
                std::filesystem::absolute(config_path).parent_path().string());
            // Set entry point directory as additional search root (for when entry_point
            // is in a subdirectory like src/main.cryo — imports resolve relative to it)
            compiler->module_loader()->set_entry_dir(
                std::filesystem::absolute(main_file).parent_path().string());
            if (use_verbose)
            {
                std::cout << "Compiling " << main_file << " -> " << output_path << std::endl;
            }
            compilation_success = compiler->compile_file(main_file);
        }

        // Dump symbols if enabled in config (for non-stdlib compilations only)
        // For stdlib compilations, symbols are dumped per-module inside compile_stdlib
        if (config.dump_symbols && !is_stdlib && compiler->symbol_table())
        {
            if (use_verbose)
            {
                std::cout << "Dumping symbol table to " << config.output_dir << "..." << std::endl;
            }

            std::string module_name = project_name;
            if (Cryo::SymbolDumper::dump_module_symbols(*compiler->symbol_table(), module_name, config.output_dir))
            {
                if (use_verbose)
                {
                    std::cout << "✓ Symbol table dumped: " << config.output_dir << "/" << module_name << "-symbols.dbg.txt" << std::endl;
                }
            }
            else
            {
                std::cerr << "Warning: Failed to dump symbol table" << std::endl;
            }
        }

        if (!compilation_success)
        {
            std::cerr << "X Compilation failed!" << std::endl;

            // Even on failure, attempt to emit partial LLVM IR if --emit-llvm was requested
            bool emit_llvm_on_fail = args.get_flag("emit-llvm");
            if (!emit_llvm_on_fail)
            {
                for (const auto &arg : config.args)
                {
                    if (arg == "--emit-llvm")
                    {
                        emit_llvm_on_fail = true;
                        break;
                    }
                }
            }

            compiler->print_diagnostics();

            if (emit_llvm_on_fail && compiler->codegen())
            {
                std::string bc_path = os.join_path(config.output_dir, project_name + ".bc");
                std::cout << "\nAttempting to emit partial LLVM IR despite errors..." << std::endl;
                if (compiler->codegen()->emit_llvm_ir(bc_path))
                {
                    std::cout << "✓ Partial LLVM IR emitted: " << bc_path << std::endl;
                    std::string ll_path = os.join_path(config.output_dir, project_name + ".ll");
                    if (std::filesystem::exists(ll_path))
                    {
                        std::cout << "✓ Partial LLVM IR text emitted: " << ll_path << std::endl;
                    }
                }
                else
                {
                    std::cerr << "⚠ Could not emit partial LLVM IR (no IR available)" << std::endl;
                }
            }

            return 1;
        }

        // For stdlib compilation, individual files are already generated,
        // so skip the additional LLVM emission and linking steps
        if (is_stdlib)
        {
            if (compilation_success)
            {
                std::cout << "✓ Stdlib compilation completed successfully" << std::endl;
                std::cout << "  Individual module files generated in: " << config.output_dir << std::endl;
            }
            else
            {
                std::cout << "⚠ Stdlib compilation completed with errors" << std::endl;
                std::cout << "  Partial results available in: " << config.output_dir << std::endl;
            }
            return compilation_success ? 0 : 1;
        }

        // Handle --emit-llvm flag from CLI args or config file (non-stdlib builds only)
        bool emit_llvm = args.get_flag("emit-llvm");

        // Check if --emit-llvm is specified in config args
        if (!emit_llvm)
        {
            for (const auto &arg : config.args)
            {
                if (arg == "--emit-llvm")
                {
                    emit_llvm = true;
                    break;
                }
            }
        }

        if (emit_llvm)
        {
            std::string bc_path = os.join_path(config.output_dir, project_name + ".bc");

            if (use_verbose)
            {
                std::cout << "Emitting LLVM IR files to " << config.output_dir << " directory..." << std::endl;
            }

            if (compiler->codegen() && compiler->codegen()->emit_llvm_ir(bc_path))
            {
                std::cout << "✓ LLVM bitcode emitted: " << bc_path << std::endl;

                // The .ll file is automatically generated by emit_llvm_ir
                std::string ll_path = os.join_path(config.output_dir, project_name + ".ll");
                if (std::filesystem::exists(ll_path))
                {
                    std::cout << "✓ LLVM IR text emitted: " << ll_path << std::endl;
                }
            }
            else
            {
                std::cerr << "❌ Failed to emit LLVM IR files" << std::endl;
                if (compiler->codegen())
                {
                    std::cerr << "Error: " << compiler->codegen()->get_last_error() << std::endl;
                }
            }
        }

        // Determine the link target based on target_type
        Cryo::Linker::CryoLinker::LinkTarget target;
        if (config.target_type == "static_library")
        {
            target = Cryo::Linker::CryoLinker::LinkTarget::StaticLibrary;
        }
        else if (config.target_type == "shared_library")
        {
            target = Cryo::Linker::CryoLinker::LinkTarget::SharedLibrary;
        }
        else
        {
            target = Cryo::Linker::CryoLinker::LinkTarget::Executable;
        }

        if (compiler->generate_output(output_path, target))
        {
            if (is_library)
            {
                std::cout << "✓ Library built successfully: " << output_path << std::endl;
            }
            else
            {
                std::cout << "✓ Build successful: " << output_path << std::endl;
            }
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

    bool BuildCommand::parse_cryoconfig(const std::string &config_path, CryoConfig &config)
    {
        return ConfigParser::parse_config(config_path, config);
    }

    bool BuildCommand::ensure_build_directory()
    {
        try
        {
            auto &os = Cryo::Utils::OS::instance();
            os.create_directories("build");
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    // ================================================================
    // Run Command
    // ================================================================

    RunCommand::RunCommand()
        : Command("run", "Run a built Cryo project")
    {
        usage("[options]");
        argument(CLIArgument("verbose", "Show verbose run output", false).flag().alias("v"));
        argument(CLIArgument("args", "Arguments to pass to the executable", false));
    }

    int RunCommand::execute(const ParsedArgs &args)
    {
        std::cout << "Running Cryo project..." << std::endl;

        return run_project(args);
    }

    int RunCommand::run_project(const ParsedArgs &args)
    {
        bool verbose = args.get_flag("verbose");

        // Collect positional arguments to forward to the executable
        std::string exe_args;
        for (const auto &pos_arg : args.positional())
        {
            if (!exe_args.empty())
                exe_args += " ";
            exe_args += pos_arg;
        }

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
        CryoConfig config;
        if (!parse_cryoconfig(config_path, config))
        {
            std::cerr << "Error: Failed to parse cryoconfig file" << std::endl;
            return 1;
        }

        std::string exe_name = config.project_name.empty() ? "app" : config.project_name;

        if (verbose)
        {
            std::cout << "Project name: " << exe_name << std::endl;
            std::cout << "Output directory: " << config.output_dir << std::endl;
        }

        // Check if executable exists
        auto &os = Cryo::Utils::OS::instance();
        std::string exe_name_with_ext = os.make_executable_name(exe_name);
        std::string exe_path = os.join_path(config.output_dir, exe_name_with_ext);
        if (!os.path_exists(exe_path))
        {
            std::cerr << "Error: Executable '" << exe_path << "' not found. Run 'cryo build' first." << std::endl;
            return 1;
        }

        // Run the executable
        std::string command = os.make_executable_command(exe_path);
        if (!exe_args.empty())
        {
            command += " " + exe_args;
        }

        if (verbose)
        {
            std::cout << "Executing: " << command << std::endl;
        }

        int result = std::system(command.c_str());

        if (verbose)
        {
            std::cout << "Process exited with code: " << result << std::endl;
        }

        return result;
    }

    bool RunCommand::find_cryoconfig_file(std::string &config_path)
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

    bool RunCommand::parse_cryoconfig(const std::string &config_path, CryoConfig &config)
    {
        return ConfigParser::parse_config(config_path, config);
    }

} // namespace Cryo::CLI::Commands
