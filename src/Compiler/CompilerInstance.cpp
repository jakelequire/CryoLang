#include "Compiler/CompilerInstance.hpp"
#include "Codegen/TypeMapper.hpp"
#include "Codegen/CodegenVisitor.hpp"
#include "AST/DirectiveProcessors.hpp"
#include "AST/DirectiveWalker.hpp"
#include "Utils/File.hpp"
#include "Utils/Logger.hpp"
#include "Utils/SymbolResolutionManager.hpp"
#include <iostream>
#include <fstream>
#include <filesystem>

namespace Cryo
{
    CompilerInstance::CompilerInstance()
        : _debug_mode(false), _show_ast_before_ir(false), _stdlib_linking_enabled(true), _stdlib_compilation_mode(false), _auto_imports_enabled(true), _current_namespace("")
    {
        initialize_components();
    }

    void CompilerInstance::initialize_components()
    {
        _ast_context = std::make_unique<ASTContext>();
        _symbol_table = std::make_unique<SymbolTable>();
        _diagnostic_manager = std::make_unique<DiagnosticManager>();

        // Set the type context in symbol table so it can create proper function types
        _symbol_table->set_type_context(&_ast_context->types());

        // Initialize Symbol Resolution Manager
        try
        {
            auto context = std::make_unique<Cryo::SRM::SymbolResolutionContext>(
                &_ast_context->types());
            _symbol_resolution_manager = std::make_unique<Cryo::SRM::SymbolResolutionManager>(context.release());
        }
        catch (const std::exception &e)
        {
            LOG_WARN(Cryo::LogComponent::GENERAL, "Failed to initialize Symbol Resolution Manager: {}", e.what());
            _symbol_resolution_manager = nullptr;
        }

        // Create type checker with the AST context's type context and diagnostic manager
        _type_checker = std::make_unique<TypeChecker>(_ast_context->types(), _diagnostic_manager.get());

        // Create monomorphization pass
        _monomorphization_pass = std::make_unique<MonomorphizationPass>();

        // Create template registry
        _template_registry = std::make_unique<TemplateRegistry>();

        // Create module loader - needs to be created after symbol table and template registry
        // Pass the main ASTContext to ensure all modules use the same TypeContext
        _module_loader = std::make_unique<ModuleLoader>(*_symbol_table, *_template_registry, *_ast_context);

        // Initialize directive system
        initialize_directive_system();

        // Note: CodeGenerator will be created after parsing when namespace is known

        // Create linker with symbol table reference
        _linker = std::make_unique<Cryo::Linker::CryoLinker>(*_symbol_table);

        // Configure diagnostic manager
        _diagnostic_manager->set_formatter_options(true, true, 2);

        // Initialize standard library built-ins
        initialize_standard_library();

        // Copy built-in symbols to TypeChecker's symbol table
        _type_checker->load_builtin_symbols(*_symbol_table);

        // Lexer and Parser will be created when we have a file to work with
    }

    void CompilerInstance::set_source_file(const std::string &file_path)
    {
        _source_file = file_path;

        // Update TypeChecker with the source file for proper error reporting
        if (_type_checker)
        {
            _type_checker->set_source_file(file_path);
        }
    }

    void CompilerInstance::add_include_path(const std::string &path)
    {
        _include_paths.push_back(path);
    }

    void CompilerInstance::set_namespace_context(const std::string &namespace_name)
    {
        _current_namespace = namespace_name;

        // Also set the namespace context in the TypeChecker if it exists
        if (_type_checker)
        {
            _type_checker->set_current_namespace(namespace_name);
        }
    }

    bool CompilerInstance::compile_file(const std::string &source_file)
    {
        set_source_file(source_file);

        // Update TypeChecker with source file information
        if (_type_checker)
        {
            _type_checker->set_source_file(source_file);
        }

        // Create a file object and load it
        auto file = make_file_from_path(source_file);
        if (!file)
        {
            _diagnostic_manager->create_error(ErrorCode::E0800_FILE_NOT_FOUND,
                                              SourceRange{}, source_file);
            return false;
        }

        if (!file->load())
        {
            _diagnostic_manager->create_error(ErrorCode::E0801_FILE_READ_ERROR,
                                              SourceRange{}, source_file);
            return false;
        }

        return parse_source_from_file(std::move(file));
    }

    bool CompilerInstance::parse_source(const std::string &source_code)
    {
        // Create an in-memory file
        auto file = make_file_from_string("source", source_code);
        if (!file)
        {
            _diagnostic_manager->create_error(ErrorCode::E0000_UNKNOWN, SourceRange{}, "source");
            return false;
        }

        return parse_source_from_file(std::move(file));
    }

    bool CompilerInstance::parse_source_from_file(std::unique_ptr<File> file)
    {
        reset_state();

        try
        {
            // Store file information for diagnostics
            std::string file_path = file->path().empty() ? file->name() : file->path();
            std::string file_content = std::string(file->content());

            // Create a shared file copy for the diagnostic manager
            auto diagnostic_file = make_file_from_string(file->name(), file_content);
            std::shared_ptr<File> shared_diagnostic_file(diagnostic_file.release());
            _diagnostic_manager->add_source_file(file_path, shared_diagnostic_file);

            // Phase 1: Create lexer with the original file
            _lexer = std::make_unique<Lexer>(std::move(file));

            // Phase 2: Create parser with lexer and AST context
            _parser = std::make_unique<Parser>(std::move(_lexer), *_ast_context, _diagnostic_manager.get(), file_path);
            _parser->set_directive_registry(_directive_registry.get());

            // Phase 3: Parse the program
            if (!parse())
            {
                _diagnostic_manager->create_error(ErrorCode::E0000_UNKNOWN, SourceRange{}, file_path);
                return false;
            }

            // Phase 4: Basic validation
            if (!_ast_root)
            {
                _diagnostic_manager->create_error(ErrorCode::E0000_UNKNOWN, SourceRange{}, file_path);
                return false;
            }

            // Phase 5: Semantic analysis (including symbol table population)
            if (!analyze())
            {
                // If TypeChecker has its own diagnostic manager, the real errors are already reported
                // Don't create a misleading E0805 error that hides the actual type errors
                if (_type_checker && _type_checker->has_diagnostic_manager())
                {
                    // Real errors are already in the diagnostic manager, just return false
                    return false;
                }
                else
                {
                    // Fallback: create E0805 error with correct error count if TypeChecker doesn't have diagnostic manager
                    std::string namespace_name = _current_namespace.empty() ? "Global" : _current_namespace;
                    size_t user_errors = _diagnostic_manager->user_error_count();
                    std::string error_message = "Module contains: " + std::to_string(user_errors) + " errors in namespace '" + namespace_name + "'";
                    _diagnostic_manager->create_error(ErrorCode::E0805_INTERNAL_ERROR,
                                                      SourceRange{}, file_path,
                                                      error_message);
                    return false;
                }
            }

            // Phase 6: Generate IR (for testing and integration)
            if (_debug_mode)
            {
                LOG_DEBUG(Cryo::LogComponent::GENERAL, "Generating IR...");
            }

            // Print AST if requested before IR generation
            if (_show_ast_before_ir)
            {
                LOG_INFO(Cryo::LogComponent::GENERAL, "Generated AST:");
                dump_ast();
            }

            if (!generate_ir())
            {
                _diagnostic_manager->create_error(ErrorCode::E0000_UNKNOWN, SourceRange{}, file_path);
                return false;
            }

            if (_debug_mode)
            {
                LOG_DEBUG(Cryo::LogComponent::GENERAL, "=== Compilation Completed ===");
            }

            // Check if there were any errors during compilation
            if (_diagnostic_manager->has_errors())
            {
                return false;
            }

            return true;
        }
        catch (const std::exception &e)
        {
            _diagnostic_manager->create_error(ErrorCode::E0000_UNKNOWN,
                                              SourceRange{}, _source_file);
            return false;
        }
    }

    bool CompilerInstance::compile_frontend_only(const std::string &source_file)
    {
        set_source_file(source_file);

        // Create a file object and load it
        auto file = make_file_from_path(source_file);
        if (!file)
        {
            _diagnostic_manager->create_error(ErrorCode::E0800_FILE_NOT_FOUND,
                                              SourceRange{}, source_file);
            return false;
        }

        if (!file->load())
        {
            _diagnostic_manager->create_error(ErrorCode::E0801_FILE_READ_ERROR,
                                              SourceRange{}, source_file);
            return false;
        }

        // Frontend-only parsing - stops before IR generation and codegen
        reset_state();

        try
        {
            // Store file information for diagnostics
            std::string file_path = file->path();

            if (file_path.find("runtime.cryo") != std::string::npos)
            {
                return true; // Skip parsing runtime.cryo in LSP mode
            }

            // Phase 1: Create lexer
            _lexer = std::make_unique<Lexer>(std::move(file));

            // Phase 2: Create parser with lexer and AST context
            _parser = std::make_unique<Parser>(std::move(_lexer), *_ast_context, _diagnostic_manager.get(), file_path);
            _parser->set_directive_registry(_directive_registry.get());

            // Phase 3: Parse the program
            if (!parse())
            {
                _diagnostic_manager->create_error(ErrorCode::E0000_UNKNOWN, SourceRange{}, file_path);
                return false;
            }

            // Phase 4: Basic validation
            if (!_ast_root)
            {
                _diagnostic_manager->create_error(ErrorCode::E0000_UNKNOWN, SourceRange{}, file_path);
                return false;
            }

            // Phase 5: Semantic analysis (including symbol table population)
            if (!analyze())
            {
                std::string namespace_name = _current_namespace.empty() ? "Global" : _current_namespace;
                _diagnostic_manager->create_error(ErrorCode::E0000_UNKNOWN,
                                                  SourceRange{}, file_path);
                // For LSP, we continue even if analysis fails partially
                // This allows hover to work even with some compilation errors
            }

            // Stop here - no IR generation or codegen for LSP
            // Frontend-only compilation completed successfully

            return true;
        }
        catch (const std::exception &e)
        {
            _diagnostic_manager->create_error(ErrorCode::E0000_UNKNOWN,
                                              SourceRange{}, _source_file);
            return false;
        }
    }

    bool CompilerInstance::tokenize()
    {
        if (_source_file.empty())
        {
            _diagnostic_manager->create_error(ErrorCode::E0000_UNKNOWN, SourceRange{}, "");
            return false;
        }

        auto file = make_file_from_path(_source_file);
        if (!file || !file->load())
        {
            _diagnostic_manager->create_error(ErrorCode::E0801_FILE_READ_ERROR, SourceRange{}, _source_file);
            return false;
        }

        try
        {
            _lexer = std::make_unique<Lexer>(std::move(file));
            return true;
        }
        catch (const std::exception &e)
        {
            _diagnostic_manager->create_error(ErrorCode::E0000_UNKNOWN,
                                              SourceRange{}, _source_file);
            return false;
        }
    }

    bool CompilerInstance::parse()
    {
        if (!_parser)
        {
            _diagnostic_manager->create_error(ErrorCode::E0000_UNKNOWN, SourceRange{}, _source_file);
            return false;
        }

        try
        {
            _ast_root = _parser->parse_program();

            // Immediately discover and register generic types from the AST 
            // This must happen before any type resolution attempts
            if (_ast_root) {
                _type_checker->discover_generic_types_from_ast(*_ast_root);
            }

            // Capture namespace context from parser after successful parsing
            if (_ast_root && !_parser->current_namespace().empty() && _parser->current_namespace() != "Global")
            {
                set_namespace_context(_parser->current_namespace());
                if (_debug_mode)
                {
                    LOG_DEBUG(Cryo::LogComponent::GENERAL, "Parsed namespace: {}", _parser->current_namespace());
                }
            }

            // Create CodeGenerator now that we have the namespace information
            std::string namespace_for_module = _current_namespace.empty() ? "cryo_program" : _current_namespace;
            _codegen = Cryo::Codegen::create_default_codegen(*_ast_context, *_symbol_table, namespace_for_module, _diagnostic_manager.get());

            // Configure stdlib compilation mode if enabled
            if (_stdlib_compilation_mode)
            {
                _codegen->set_stdlib_compilation_mode(true);
                _type_checker->set_stdlib_compilation_mode(true);
                if (_debug_mode)
                {
                    LOG_DEBUG(Cryo::LogComponent::GENERAL, "Enabled stdlib compilation mode in CodeGenerator and TypeChecker");
                }
            }

            if (_debug_mode)
            {
                LOG_DEBUG(Cryo::LogComponent::GENERAL, "Created CodeGenerator with module name: '{}'", namespace_for_module);
            }

            // Register struct/class AST nodes with TypeMapper immediately after CodeGenerator creation
            // This ensures AST nodes are registered before any TypeMapper operations during analysis
            register_ast_nodes_with_typemapper();

            return _ast_root != nullptr;
        }
        catch (const std::exception &e)
        {
            _diagnostic_manager->create_error(ErrorCode::E0000_UNKNOWN,
                                              SourceRange{}, _source_file);
            return false;
        }
    }

    bool CompilerInstance::analyze()
    {
        if (_debug_mode)
        {
            LOG_DEBUG(Cryo::LogComponent::GENERAL, "Starting analysis phase...");
        }

        if (!_ast_root)
        {
            _diagnostic_manager->create_error(ErrorCode::E0000_UNKNOWN, SourceRange{}, _source_file);
            return false;
        }

        try
        {
            // Phase -1: Process directives first
            if (_debug_mode)
            {
                LOG_DEBUG(Cryo::LogComponent::GENERAL, "Phase -1: Processing directives...");
            }
            if (!process_directives())
            {
                return false;
            }

            if (_debug_mode)
            {
                LOG_DEBUG(Cryo::LogComponent::GENERAL, "Phase 0: Symbol table population...");
            }
            // Phase 0: Symbol table population (must happen before type checking)
            populate_symbol_table(_ast_root.get());

            if (_debug_mode)
            {
                LOG_DEBUG(Cryo::LogComponent::GENERAL, "Loading intrinsic symbols into type checker...");
            }
            // Load only newly added intrinsic symbols into type checker
            _type_checker->load_intrinsic_symbols(*_symbol_table);

            if (_debug_mode)
            {
                LOG_DEBUG(Cryo::LogComponent::GENERAL, "Loading runtime symbols into type checker...");
            }
            // Load runtime function symbols and make them available globally
            _type_checker->load_runtime_symbols(*_symbol_table);

            if (_debug_mode)
            {
                LOG_DEBUG(Cryo::LogComponent::GENERAL, "Loading user-defined symbols into type checker...");
            }
            // Load user-defined function symbols that were registered during Pass 1
            _type_checker->load_user_symbols(*_symbol_table);

            if (_debug_mode)
            {
                LOG_DEBUG(Cryo::LogComponent::GENERAL, "Processing imported modules for AST node updates...");
            }
            // Process imported modules to update symbols with AST node references
            _type_checker->check_imported_modules(_module_loader->get_imported_asts());

            if (_debug_mode)
            {
                LOG_DEBUG(Cryo::LogComponent::GENERAL, "Phase 1: Type checking...");
            }
            // Phase 1: Type checking
            _type_checker->check_program(*_ast_root);

            if (_debug_mode)
            {
                LOG_DEBUG(Cryo::LogComponent::GENERAL, "Checking for type errors...");
            }
            if (_debug_mode)
            {
                LOG_DEBUG(Cryo::LogComponent::GENERAL, "TypeChecker has {} errors, has_errors() = {}",
                          _type_checker->error_count(), _type_checker->has_errors());
            }
            if (_type_checker->has_errors())
            {
                // Only convert type errors to diagnostic manager errors if TypeChecker doesn't have its own diagnostic manager
                // This prevents duplicate error reporting
                if (!_type_checker->has_diagnostic_manager())
                {
                    // Convert type errors to diagnostic manager errors
                    for (const auto &type_error : _type_checker->errors())
                    {
                        ErrorCode error_code = ErrorCode::E0000_UNKNOWN;
                        switch (type_error.kind)
                        {
                        case TypeError::ErrorKind::TypeMismatch:
                            error_code = ErrorCode::E0200_TYPE_MISMATCH;
                            break;
                        case TypeError::ErrorKind::UndefinedVariable:
                            error_code = ErrorCode::E0201_UNDEFINED_VARIABLE;
                            break;
                        case TypeError::ErrorKind::UndefinedFunction:
                            error_code = ErrorCode::E0202_UNDEFINED_FUNCTION;
                            break;
                        case TypeError::ErrorKind::RedefinedSymbol:
                            error_code = ErrorCode::E0205_REDEFINED_SYMBOL;
                            break;
                        case TypeError::ErrorKind::InvalidOperation:
                            error_code = ErrorCode::E0209_INVALID_OPERATION;
                            break;
                        default:
                            error_code = ErrorCode::E0000_UNKNOWN;
                            break;
                        }

                        // Create better ranges for specific error types
                        // For now, use single-position ranges - the diagnostic formatter will handle the display
                        SourceRange range = SourceRange(type_error.location);
                        _diagnostic_manager->create_error(error_code,
                                                          range, _source_file, type_error.message);
                    }
                }

                if (_debug_mode)
                {
                    LOG_ERROR(Cryo::LogComponent::GENERAL, "Type checking failed with {} errors", _type_checker->error_count());
                    // Print type errors for debugging
                    for (const auto &type_error : _type_checker->errors())
                    {
                        LOG_ERROR(Cryo::LogComponent::GENERAL, "Type Error: {}", type_error.to_string());
                    }
                }
                return false;
            }

            // Phase 2: Monomorphization - Generate specialized versions of generic types
            if (_debug_mode)
            {
                LOG_DEBUG(Cryo::LogComponent::GENERAL, "Starting monomorphization pass...");
            }

            const auto &required_instantiations = _type_checker->get_required_instantiations();
            if (!required_instantiations.empty())
            {
                bool monomorphization_success = _monomorphization_pass->monomorphize(*_ast_root, required_instantiations, *_template_registry, *_type_checker);
                if (!monomorphization_success)
                {
                    _diagnostic_manager->create_error(ErrorCode::E0000_UNKNOWN, SourceRange{}, _source_file);
                    return false;
                }

                if (_debug_mode)
                {
                    LOG_DEBUG(Cryo::LogComponent::GENERAL, "Monomorphization completed successfully");
                }
            }
            else
            {
                if (_debug_mode)
                {
                    LOG_DEBUG(Cryo::LogComponent::GENERAL, "No generic instantiations required, skipping monomorphization");
                }
            }

            // Phase 3: Future semantic analysis phases would go here
            // - Control flow analysis
            // - Dead code elimination
            // - etc.

            if (_debug_mode)
            {
                LOG_DEBUG(Cryo::LogComponent::GENERAL, "Type checking completed successfully");
            }

            // Final phase: Validate directive effects
            if (_debug_mode)
            {
                LOG_DEBUG(Cryo::LogComponent::GENERAL, "Validating directive effects...");
            }
            if (!validate_directive_effects())
            {
                return false;
            }

            return true;
        }
        catch (const std::invalid_argument &e)
        {
            LOG_ERROR(Cryo::LogComponent::GENERAL, "Invalid argument in analyze(): {}", e.what());
            _diagnostic_manager->create_error(ErrorCode::E0504_NAMESPACE_CONFLICT,
                                              SourceRange{}, _source_file, e.what());
            return false;
        }
        catch (const std::exception &e)
        {
            LOG_ERROR(Cryo::LogComponent::GENERAL, "Exception caught in analyze(): {}", e.what());
            _diagnostic_manager->create_error(ErrorCode::E0000_UNKNOWN,
                                              SourceRange{}, _source_file, e.what());
            return false;
        }
    }

    bool CompilerInstance::generate_ir()
    {
        if (!_ast_root)
        {
            _diagnostic_manager->create_error(ErrorCode::E0000_UNKNOWN, SourceRange{}, _source_file);
            return false;
        }

        if (!_codegen)
        {
            _diagnostic_manager->create_error(ErrorCode::E0000_UNKNOWN, SourceRange{}, _source_file);
            return false;
        }

        try
        {
            // Set source file and namespace context before generating IR
            LOG_DEBUG(Cryo::LogComponent::GENERAL, "Setting source info for codegen...");
            _codegen->set_source_info(_source_file, _current_namespace);

            // Import specialized methods from TypeChecker to CodeGen after monomorphization
            if (_codegen->get_visitor() && _type_checker)
            {
                LOG_DEBUG(Cryo::LogComponent::GENERAL, "Importing specialized methods from TypeChecker to CodeGen...");
                _codegen->get_visitor()->import_specialized_methods(*_type_checker);
                
                LOG_DEBUG(Cryo::LogComponent::GENERAL, "Importing namespace aliases from TypeChecker to CodeGen...");
                _codegen->get_visitor()->import_namespace_aliases(*_type_checker);
            }

            // Pass imported ASTs to CodegenVisitor for dynamic enum variant extraction
            if (_codegen->get_visitor() && _module_loader)
            {
                LOG_DEBUG(Cryo::LogComponent::GENERAL, "Setting imported ASTs for dynamic enum variant extraction...");
                _codegen->get_visitor()->set_imported_asts(&_module_loader->get_imported_asts());
            }

            LOG_DEBUG(Cryo::LogComponent::GENERAL, "Starting IR generation...");
            bool success = _codegen->generate_ir(_ast_root.get());
            LOG_DEBUG(Cryo::LogComponent::GENERAL, "IR generation completed with result: {}", (success ? "success" : "failure"));

            if (!success)
            {
                LOG_ERROR(Cryo::LogComponent::GENERAL, "IR generation failed with error: {}", _codegen->get_last_error());
                _diagnostic_manager->create_error(ErrorCode::E0000_UNKNOWN,
                                                  SourceRange{}, _source_file);
            }

            return success;
        }
        catch (const std::exception &e)
        {
            _diagnostic_manager->create_error(ErrorCode::E0000_UNKNOWN,
                                              SourceRange{}, _source_file);
            return false;
        }
    }

    bool CompilerInstance::generate_output(const std::string &output_path,
                                           Cryo::Linker::CryoLinker::LinkTarget target)
    {
        // First generate IR if not already done
        if (!_codegen->get_module())
        {
            if (!generate_ir())
            {
                return false;
            }
        }

        if (!_linker)
        {
            _diagnostic_manager->create_error(ErrorCode::E0000_UNKNOWN, SourceRange{}, _source_file);
            return false;
        }

        try
        {
            // Configure linker with runtime and stdlib if linking is enabled
            if (_stdlib_linking_enabled)
            {
                // Find runtime files using dynamic resolution
                std::string bin_dir = find_bin_directory();

                // Add runtime first (contains true main function)
                std::string runtime_path = bin_dir + "/stdlib/runtime.o";
                if (std::filesystem::exists(runtime_path))
                {
                    _linker->add_object_file(runtime_path);
                    if (_debug_mode)
                    {
                        LOG_DEBUG(Cryo::LogComponent::GENERAL, "Added runtime.o for runtime linking: {}", runtime_path);
                    }
                }
                else
                {
                    LOG_WARN(Cryo::LogComponent::GENERAL, "runtime.o not found at {}, runtime functions may not link properly", runtime_path);
                }

                // Add libcryo.a to the linker
                std::string libcryo_path = bin_dir + "/stdlib/libcryo.a";
                if (std::filesystem::exists(libcryo_path))
                {
                    _linker->add_object_file(libcryo_path);
                    if (_debug_mode)
                    {
                        LOG_DEBUG(Cryo::LogComponent::GENERAL, "Added libcryo.a for standard library linking: {}", libcryo_path);
                    }
                }
                else
                {
                    LOG_WARN(Cryo::LogComponent::GENERAL, "libcryo.a not found at {}, stdlib functions may not link properly", libcryo_path);
                }
            }
            else if (_debug_mode)
            {
                LOG_DEBUG(Cryo::LogComponent::GENERAL, "Standard library linking disabled by --no-std flag");
            }

            // Get generated module and link
            llvm::Module *module = _codegen->get_module();
            if (!module)
            {
                _diagnostic_manager->create_error(ErrorCode::E0000_UNKNOWN, SourceRange{}, _source_file);
                return false;
            }

            bool success = _linker->link_modules({module}, output_path, target);

            if (!success)
            {
                std::string linker_error = _linker->get_last_error();
                LOG_DEBUG(Cryo::LogComponent::GENERAL, "Linker error string length: {}", linker_error.length());
                LOG_DEBUG(Cryo::LogComponent::GENERAL, "Linker error content: '{}'", linker_error);

                // Detect specific linker error types and use appropriate error codes
                ErrorCode error_code = ErrorCode::E0700_LINK_ERROR;
                std::string custom_message = linker_error;

                if (linker_error.find("undefined reference") != std::string::npos)
                {
                    error_code = ErrorCode::E0701_UNDEFINED_SYMBOL_LINK;

                    // Special case for WinMain/main issues (common runtime missing issue)
                    if (linker_error.find("WinMain") != std::string::npos ||
                        linker_error.find("main") != std::string::npos)
                    {
                        custom_message = "Missing runtime library. The CryoLang runtime (runtime.o) is required to provide the main entry point. " + linker_error;
                    }
                }
                else if (linker_error.find("duplicate symbol") != std::string::npos)
                {
                    error_code = ErrorCode::E0702_DUPLICATE_SYMBOL_LINK;
                }

                _diagnostic_manager->create_error(error_code,
                                                  SourceRange{}, _source_file,
                                                  custom_message);
            }

            return success;
        }
        catch (const std::exception &e)
        {
            _diagnostic_manager->create_error(ErrorCode::E0000_UNKNOWN,
                                              SourceRange{}, _source_file);
            return false;
        }
    }

    bool CompilerInstance::has_errors() const
    {
        return _diagnostic_manager && _diagnostic_manager->has_errors();
    }

    void CompilerInstance::print_ast(std::ostream &os, bool use_colors) const
    {
        if (_ast_root)
        {
            os << "=== AST Structure ===" << std::endl;
            _ast_root->print(os);
            os << "===================" << std::endl;
        }
        else
        {
            os << "No AST available" << std::endl;
        }
    }

    void CompilerInstance::dump_ast(std::ostream &os, bool use_colors) const
    {
        if (_ast_root)
        {
            ASTDumper dumper(os, use_colors);
            dumper.dump(_ast_root.get());
        }
        else
        {
            os << "No AST available" << std::endl;
        }
    }

    void CompilerInstance::dump_symbol_table(std::ostream &os) const
    {
        if (_symbol_table)
        {
            _symbol_table->print_pretty(os);
        }
        else
        {
            os << "No symbol table available" << std::endl;
        }
    }

    void CompilerInstance::dump_type_table(std::ostream &os) const
    {
        if (_type_checker)
        {
            _type_checker->print_type_table(os);
        }
        else
        {
            os << "No type checker available" << std::endl;
        }
    }

    void CompilerInstance::dump_type_errors(std::ostream &os) const
    {
        if (_type_checker && _type_checker->has_errors())
        {
            os << "=== Type Errors ===" << std::endl;
            for (const auto &error : _type_checker->errors())
            {
                os << error.to_string() << std::endl;
            }
            os << "=== End Type Errors ===" << std::endl;
        }
        else
        {
            os << "No type errors found." << std::endl;
        }
    }

    void CompilerInstance::dump_ir(std::ostream &os) const
    {
        if (_codegen && _codegen->get_module())
        {
            llvm::Module *module = _codegen->get_module();

            // Use LLVM's raw_ostream to print to string, then output to our stream
            std::string ir_str;
            llvm::raw_string_ostream string_stream(ir_str);
            module->print(string_stream, nullptr);
            os << string_stream.str();
        }
        else
        {
            os << "No LLVM IR available (module not generated)" << std::endl;
        }
    }

    void CompilerInstance::print_diagnostics(std::ostream &os) const
    {
        if (_diagnostic_manager)
        {
            // Debug: Show what diagnostics we have
            const auto &diagnostics = _diagnostic_manager->diagnostics();
            if (_debug_mode)
            {
                LOG_DEBUG(Cryo::LogComponent::GENERAL, "DiagnosticManager has {} total diagnostics", diagnostics.size());
                for (size_t i = 0; i < diagnostics.size(); ++i)
                {
                    const auto &diag = diagnostics[i];
                    bool is_stdlib = _diagnostic_manager->is_stdlib_diagnostic(diag);
                    LOG_DEBUG(Cryo::LogComponent::GENERAL, "Diagnostic {}: severity={}, filename='{}', is_stdlib={}, message='{}'", 
                              i, static_cast<int>(diag.severity()), diag.filename(), is_stdlib, diag.message());
                }
            }
            
            if (_diagnostic_manager->total_count() > 0)
            {
                _diagnostic_manager->print_all(os);
                _diagnostic_manager->print_summary(os);
            }
        }
    }

    void CompilerInstance::clear()
    {
        reset_state();
        _source_file.clear();
        _include_paths.clear();
        if (_diagnostic_manager)
        {
            _diagnostic_manager->clear();
        }
    }

    void CompilerInstance::reset_state()
    {
        _ast_root.reset();
        if (_diagnostic_manager)
        {
            _diagnostic_manager->clear();
        }
        _lexer.reset();
        _parser.reset();
    }

    void CompilerInstance::populate_symbol_table(ASTNode *node)
    {
        // Inject auto-imports before processing user imports
        inject_auto_imports(_symbol_table.get(), "Global");

        // Two-pass approach for proper forward reference resolution:
        // Pass 1: Collect all declarations (functions, structs, enums, traits) first
        LOG_DEBUG(Cryo::LogComponent::GENERAL, "Pass 1: Collecting declarations...");
        collect_declarations_pass(node, _symbol_table.get(), "Global");

        // Pre-register functions in LLVM to prevent forward declaration conflicts
        if (_codegen)
        {
            LOG_DEBUG(Cryo::LogComponent::GENERAL, "Ensuring visitor is initialized for function pre-registration...");
            if (_codegen->ensure_visitor_initialized())
            {
                LOG_DEBUG(Cryo::LogComponent::GENERAL, "Processing struct declarations before function pre-registration...");
                // First, process struct declarations to ensure TypeMapper has correct type information
                process_struct_declarations_for_preregistration(node);

                LOG_DEBUG(Cryo::LogComponent::GENERAL, "Pre-registering functions in LLVM module...");
                _codegen->get_visitor()->pre_register_functions_from_symbol_table();
            }
            else
            {
                LOG_WARN(Cryo::LogComponent::GENERAL, "Failed to initialize CodegenVisitor for function pre-registration");
            }
        }
        else
        {
            LOG_WARN(Cryo::LogComponent::GENERAL, "CodeGenerator not available for function pre-registration");
        }

        // Pass 2: Process function bodies and other code that can reference the symbols
        LOG_DEBUG(Cryo::LogComponent::GENERAL, "Pass 2: Processing function bodies and references...");
        populate_symbol_table_with_scope(node, _symbol_table.get(), "Global");
    }

    void CompilerInstance::collect_declarations_pass(ASTNode *node, SymbolTable *current_scope, const std::string &scope_name)
    {
        if (!node || !current_scope)
            return;

        // Handle function declarations - only collect signature, not body
        if (auto func_decl = dynamic_cast<FunctionDeclarationNode *>(node))
        {
            LOG_TRACE(Cryo::LogComponent::GENERAL, "Pass 1: Found function declaration: {}", func_decl->name());

            // Create function type from return type and parameters
            std::vector<Type *> param_types;
            for (const auto &param : func_decl->parameters())
            {
                // Use resolved parameter type directly
                Type *param_type = param->get_resolved_type();
                param_types.push_back(param_type);
            }

            // Use resolved return type directly
            Type *return_type = func_decl->get_resolved_return_type();

            // Handle functions that may not have resolved return types yet
            if (return_type == nullptr)
            {
                LOG_WARN(Cryo::LogComponent::GENERAL, "Function '{}' has null return type, defaulting to void", func_decl->name());
                return_type = _ast_context->types().get_void_type(); // Safe fallback
            }

            // Create function type
            Type *function_type = _ast_context->types().create_function_type(return_type, param_types);

            // Build enhanced signature including generic parameters
            std::string enhanced_signature = build_function_signature(func_decl);

            // Add function to current (global) scope with enhanced display for generics
            current_scope->declare_symbol(func_decl->name(), SymbolKind::Function,
                                          func_decl->location(), function_type, scope_name,
                                          func_decl->generic_parameters().empty() ? "" : enhanced_signature);

            LOG_TRACE(Cryo::LogComponent::GENERAL, "Pass 1: Registered function: {}", func_decl->name());

            // Register generic function templates if this function has generic parameters
            if (!func_decl->generic_parameters().empty())
            {
                LOG_DEBUG(Cryo::LogComponent::GENERAL, "Registering local generic function template: {}", func_decl->name());
                _template_registry->register_function_template(
                    func_decl->name(),
                    func_decl,
                    "Main", // Use "Main" as the module name for local templates
                    ""      // No source file path for main file templates
                );
                LOG_TRACE(Cryo::LogComponent::GENERAL, "Registered local generic function template: {}", func_decl->name());
            }

            // Do NOT process function body in this pass
        }
        // Handle intrinsic function declarations
        else if (auto intrinsic_decl = dynamic_cast<IntrinsicDeclarationNode *>(node))
        {
            // Create function type from return type and parameters
            std::vector<Type *> param_types;
            for (const auto &param : intrinsic_decl->parameters())
            {
                // Use resolved parameter type directly
                Type *param_type = param->get_resolved_type();
                param_types.push_back(param_type);
            }

            // Use resolved return type directly
            Type *return_type = intrinsic_decl->get_resolved_return_type();

            // Handle intrinsics that may not have resolved return types yet
            if (return_type == nullptr)
            {
                LOG_WARN(Cryo::LogComponent::GENERAL, "Intrinsic '{}' has null return type, defaulting to void", intrinsic_decl->name());
                return_type = _ast_context->types().get_void_type(); // Safe fallback
            }

            // Create function type
            Type *function_type = _ast_context->types().create_function_type(return_type, param_types);

            // Add intrinsic function to current (global) scope with Intrinsic symbol kind
            // Register intrinsics only in std::Intrinsics namespace for consistency
            current_scope->declare_symbol(intrinsic_decl->name(), SymbolKind::Intrinsic,
                                          intrinsic_decl->location(), function_type, "std::Intrinsics");

            LOG_TRACE(Cryo::LogComponent::GENERAL, "Registered intrinsic function '{}' in namespace 'std::Intrinsics'", 
                     intrinsic_decl->name());
        }
        // Handle struct declarations
        else if (auto struct_decl = dynamic_cast<StructDeclarationNode *>(node))
        {
            // Get or create struct type
            Type *struct_type = _ast_context->types().get_struct_type(struct_decl->name());

            // Add struct to symbol table as a Type symbol
            current_scope->declare_symbol(struct_decl->name(), SymbolKind::Type,
                                          struct_decl->location(), struct_type, scope_name);

            // Register struct methods with fully qualified names
            for (const auto &method : struct_decl->methods())
            {
                if (method)
                {
                    std::string method_name = generate_method_name(scope_name, struct_decl->name(), method->name());
                    LOG_TRACE(Cryo::LogComponent::GENERAL, "Pass 1: Found struct method: {}", method_name);

                    // Create function type for the method
                    std::vector<Type *> param_types;
                    for (const auto &param : method->parameters())
                    {
                        Type *param_type = param->get_resolved_type();
                        param_types.push_back(param_type);
                    }

                    Type *return_type = method->get_resolved_return_type();

                    // Handle constructors that may not have resolved return types yet
                    if (return_type == nullptr)
                    {
                        if (method->is_constructor())
                        {
                            return_type = _ast_context->types().get_void_type();
                        }
                        else
                        {
                            LOG_WARN(Cryo::LogComponent::GENERAL, "Struct method '{}' has null return type and is not a constructor", method_name);
                            return_type = _ast_context->types().get_void_type(); // Safe fallback
                        }
                    }

                    Type *function_type = _ast_context->types().create_function_type(return_type, param_types);

                    // Register method in symbol table with fully qualified name
                    current_scope->declare_symbol(method_name, SymbolKind::Function,
                                                  method->location(), function_type, scope_name);
                }
            }

            // Register generic struct templates if this struct has generic parameters
            if (!struct_decl->generic_parameters().empty())
            {
                LOG_DEBUG(Cryo::LogComponent::GENERAL, "Registering local generic struct template: {}", struct_decl->name());
                _template_registry->register_struct_template(
                    struct_decl->name(),
                    struct_decl,
                    "Main", // Use "Main" as the module name for local templates
                    ""      // No source file path for main file templates
                );
                LOG_TRACE(Cryo::LogComponent::GENERAL, "Registered local generic struct template: {}", struct_decl->name());
            }
        }
        // Handle class declarations
        else if (auto class_decl = dynamic_cast<ClassDeclarationNode *>(node))
        {
            // Get or create class type
            Type *class_type = _ast_context->types().get_class_type(class_decl->name());

            // Add class to symbol table as a Type symbol
            current_scope->declare_symbol(class_decl->name(), SymbolKind::Type,
                                          class_decl->location(), class_type, scope_name);

            // Register class methods with fully qualified names
            for (const auto &method : class_decl->methods())
            {
                if (method)
                {
                    std::string method_name = generate_method_name(scope_name, class_decl->name(), method->name());
                    LOG_TRACE(Cryo::LogComponent::GENERAL, "Pass 1: Found class method: {}", method_name);

                    // Create function type for the method
                    std::vector<Type *> param_types;
                    for (const auto &param : method->parameters())
                    {
                        Type *param_type = param->get_resolved_type();
                        param_types.push_back(param_type);
                    }

                    Type *return_type = method->get_resolved_return_type();

                    // Handle constructors that may not have resolved return types yet
                    if (return_type == nullptr)
                    {
                        if (method->is_constructor())
                        {
                            return_type = _ast_context->types().get_void_type();
                        }
                        else
                        {
                            LOG_WARN(Cryo::LogComponent::GENERAL, "Method '{}' has null return type and is not a constructor", method_name);
                            return_type = _ast_context->types().get_void_type(); // Safe fallback
                        }
                    }

                    Type *function_type = _ast_context->types().create_function_type(return_type, param_types);

                    // Register method in symbol table with fully qualified name
                    current_scope->declare_symbol(method_name, SymbolKind::Function,
                                                  method->location(), function_type, scope_name);
                }
            }

            // Register generic class templates if this class has generic parameters
            if (!class_decl->generic_parameters().empty())
            {
                LOG_DEBUG(Cryo::LogComponent::GENERAL, "Registering local generic class template: {}", class_decl->name());
                _template_registry->register_class_template(
                    class_decl->name(),
                    class_decl,
                    "Main", // Use "Main" as the module name for local templates
                    ""      // No source file path for main file templates
                );
                LOG_TRACE(Cryo::LogComponent::GENERAL, "Registered local generic class template: {}", class_decl->name());
            }
        }
        // Handle enum declarations
        else if (auto enum_decl = dynamic_cast<EnumDeclarationNode *>(node))
        {
            // Create the enum type during symbol table population for proper type resolution
            std::vector<std::string> variant_names;
            for (const auto &variant : enum_decl->variants())
            {
                variant_names.push_back(variant->name());
            }

            // Check if this is a simple enum (all variants without associated types)
            bool is_simple_enum = true;
            for (const auto &variant : enum_decl->variants())
            {
                if (!variant->associated_types().empty())
                {
                    is_simple_enum = false;
                    break;
                }
            }

            // Create enum type and register it
            Type *enum_type = _ast_context->types().get_enum_type(enum_decl->name(), variant_names, is_simple_enum);
            current_scope->declare_symbol(enum_decl->name(), SymbolKind::Type,
                                          enum_decl->location(), enum_type, scope_name);

            // Register generic enum templates if this enum has generic parameters
            if (!enum_decl->generic_parameters().empty())
            {
                LOG_DEBUG(Cryo::LogComponent::GENERAL, "Registering local generic enum template: {}", enum_decl->name());
                _template_registry->register_enum_template(
                    enum_decl->name(),
                    enum_decl,
                    "Main", // Use "Main" as the module name for local templates
                    ""      // No source file path for main file templates
                );
                LOG_TRACE(Cryo::LogComponent::GENERAL, "Registered local generic enum template: {}", enum_decl->name());
            }
        }
        // Handle trait declarations
        else if (auto trait_decl = dynamic_cast<TraitDeclarationNode *>(node))
        {
            // Add trait to symbol table as a type
            current_scope->declare_symbol(trait_decl->name(), SymbolKind::Type,
                                          trait_decl->location(), nullptr, scope_name);

            // Register generic trait templates if this trait has generic parameters
            if (!trait_decl->generic_parameters().empty())
            {
                LOG_DEBUG(Cryo::LogComponent::GENERAL, "Registering local generic trait template: {}", trait_decl->name());
                _template_registry->register_trait_template(
                    trait_decl->name(),
                    trait_decl,
                    "Main", // Use "Main" as the module name for local templates
                    ""      // No source file path for main file templates
                );
                LOG_TRACE(Cryo::LogComponent::GENERAL, "Registered local generic trait template: {}", trait_decl->name());
            }
        }
        // Handle import declarations (process in first pass since they affect symbol resolution)
        else if (auto import_decl = dynamic_cast<ImportDeclarationNode *>(node))
        {
            LOG_DEBUG(Cryo::LogComponent::GENERAL, "Processing import: {}", import_decl->path());

            // Use the member ModuleLoader instance
            // Auto-detect stdlib root instead of using hardcoded path
            if (!_module_loader->auto_detect_stdlib_root())
            {
                // Fallback to relative path if auto-detection fails
                _module_loader->set_stdlib_root("./stdlib");
            }
            _module_loader->set_current_file(_source_file);

            // Load the import
            auto result = _module_loader->load_import(*import_decl);

            if (result.success)
            {
                if (import_decl->is_specific_import())
                {
                    // Check if this is a namespace alias (single specific import) or symbol imports (multiple)
                    if (!result.namespace_alias.empty())
                    {
                        // Single specific import treated as namespace alias
                        LOG_DEBUG(Cryo::LogComponent::GENERAL, "Processing namespace alias '{}' for module '{}' with {} symbols",
                                  result.namespace_alias, result.module_name, result.symbol_map.size());

                        current_scope->register_namespace(result.namespace_alias, result.symbol_map);
                        _imported_namespaces.push_back(result.namespace_alias); // Track for enhanced resolution
                        LOG_TRACE(Cryo::LogComponent::GENERAL, "Registered namespace alias '{}' with {} symbols",
                                  result.namespace_alias, result.symbol_map.size());
                    }
                    else
                    {
                        // Multiple specific imports - add symbols directly to current scope
                        LOG_DEBUG(Cryo::LogComponent::GENERAL, "Processing specific symbol imports with {} symbols", result.symbol_map.size());

                        for (const auto &[symbol_name, symbol] : result.symbol_map)
                        {
                            // Register each symbol directly in the current scope
                            current_scope->declare_symbol(symbol_name, symbol.kind, symbol.declaration_location, symbol.data_type, scope_name);
                            LOG_TRACE(Cryo::LogComponent::GENERAL, "Registered specific symbol: {}", symbol_name);
                        }
                    }
                }
                else
                {
                    // For wildcard imports, register the namespace and symbols
                    std::string namespace_name = import_decl->has_alias() ? import_decl->alias() : result.module_name;

                    if (!result.symbol_map.empty())
                    {
                        // Register namespace for qualified access (e.g., IO::println)
                        current_scope->register_namespace(namespace_name, result.symbol_map);
                        _imported_namespaces.push_back(namespace_name); // Track for enhanced resolution
                        
                        // Also register in SRM context for proper wildcard import resolution
                        if (_symbol_resolution_manager && _symbol_resolution_manager->get_context()) {
                            _symbol_resolution_manager->get_context()->add_imported_namespace(namespace_name);
                            LOG_DEBUG(Cryo::LogComponent::GENERAL, "Added '{}' to SRM imported namespaces", namespace_name);
                        }
                        
                        // TODO: Re-enable TypeChecker SRM synchronization after fixing crashes
                        // Temporarily disabled due to compilation issues
                        /*
                        // TODO: Re-enable TypeChecker SRM synchronization after fixing crashes
                        // Temporarily disabled due to compilation issues
                        /*
                        // Also register in TypeChecker's SRM context for proper symbol resolution during type checking
                        if (_type_checker && _type_checker->get_srm_context())
                        {
                            _type_checker->get_srm_context()->add_imported_namespace(namespace_name);
                            LOG_DEBUG(Cryo::LogComponent::GENERAL, "Added '{}' to TypeChecker SRM imported namespaces", namespace_name);
                        }
                        */
                        
                        // Also register symbols directly in current scope for unqualified access (e.g., println)
                        // This enables wildcard imports to work with direct function calls
                        for (const auto &[symbol_name, symbol] : result.symbol_map)
                        {
                            // Register each symbol directly in the current scope for unqualified access
                            current_scope->declare_symbol(symbol_name, symbol.kind, symbol.declaration_location, symbol.data_type, scope_name);
                            LOG_TRACE(Cryo::LogComponent::GENERAL, "Registered wildcard symbol for unqualified access: {}", symbol_name);
                        }
                        
                        LOG_DEBUG(Cryo::LogComponent::GENERAL, "Registered namespace '{}' with {} symbols (qualified and unqualified access)",
                                  namespace_name, result.symbol_map.size());
                    }
                    else
                    {
                        LOG_WARN(Cryo::LogComponent::GENERAL, "Import succeeded but no symbols found in {}", import_decl->path());
                    }
                }
            }
            else
            {
                LOG_ERROR(Cryo::LogComponent::GENERAL, "Failed to load import '{}': {}", import_decl->path(), result.error_message);
            }
        }
        // Handle variable declarations (collect them in first pass too)
        else if (auto var_decl = dynamic_cast<VariableDeclarationNode *>(node))
        {
            // Use resolved variable type directly
            Type *var_type = var_decl->get_resolved_type();

            current_scope->declare_symbol(var_decl->name(), SymbolKind::Variable,
                                          var_decl->location(), var_type, scope_name);
        }
        // Handle declaration statements (our wrapper)
        else if (auto decl_stmt = dynamic_cast<DeclarationStatementNode *>(node))
        {
            if (decl_stmt->declaration())
            {
                collect_declarations_pass(decl_stmt->declaration(), current_scope, scope_name);
            }
        }
        // Handle program nodes - recurse into statements
        else if (auto program = dynamic_cast<ProgramNode *>(node))
        {
            const auto &statements = program->statements();
            for (const auto &stmt : statements)
            {
                collect_declarations_pass(stmt.get(), current_scope, scope_name);
            }
        }
        // Handle block statements - recurse into statements
        else if (auto block = dynamic_cast<BlockStatementNode *>(node))
        {
            const auto &statements = block->statements();
            for (const auto &stmt : statements)
            {
                collect_declarations_pass(stmt.get(), current_scope, scope_name);
            }
        }
        // Skip all other node types in first pass (expressions, function calls, etc.)
    }

    void CompilerInstance::populate_symbol_table_with_scope(ASTNode *node, SymbolTable *current_scope, const std::string &scope_name)
    {
        if (!node || !current_scope)
            return;

        // Handle function declarations - skip redeclaring, only process body
        if (auto func_decl = dynamic_cast<FunctionDeclarationNode *>(node))
        {
            // Function declaration was already processed in first pass
            // Only process the function body in this second pass
            if (func_decl->body())
            {
                populate_symbol_table_with_scope(func_decl->body(), current_scope, func_decl->name());
            }
        }
        // Handle intrinsic function declarations - skip, already processed in first pass
        else if (auto intrinsic_decl = dynamic_cast<IntrinsicDeclarationNode *>(node))
        {
            // Already processed in first pass, nothing to do
        }
        // Handle import declarations - skip, already processed in first pass
        else if (auto import_decl = dynamic_cast<ImportDeclarationNode *>(node))
        {
            // Already processed in first pass, nothing to do
        }
        // Handle struct declarations - skip, already processed in first pass
        else if (auto struct_decl = dynamic_cast<StructDeclarationNode *>(node))
        {
            // Already processed in first pass, nothing to do
        }
        // Handle enum declarations - skip, already processed in first pass
        else if (auto enum_decl = dynamic_cast<EnumDeclarationNode *>(node))
        {
            // Already processed in first pass, nothing to do
        }
        // Handle trait declarations - skip, already processed in first pass
        else if (auto trait_decl = dynamic_cast<TraitDeclarationNode *>(node))
        {
            // Already processed in first pass, nothing to do
        }
        // Handle variable declarations - add to current scope
        else if (auto var_decl = dynamic_cast<VariableDeclarationNode *>(node))
        {
            // Use resolved type from AST node
            Type *var_type = var_decl->get_resolved_type();

            // Add variable to current scope
            bool success = current_scope->declare_symbol(var_decl->name(), SymbolKind::Variable,
                                                         var_decl->location(), var_type, scope_name);

            if (!success && _debug_mode)
            {
                LOG_WARN(Cryo::LogComponent::GENERAL, "Variable '{}' already declared in scope '{}'",
                         var_decl->name(), scope_name);
            }
            else if (_debug_mode)
            {
                LOG_DEBUG(Cryo::LogComponent::GENERAL, "Added variable '{}' to scope '{}' with type '{}'",
                          var_decl->name(), scope_name, var_type->to_string());
            }
        }
        // Handle declaration statements (our wrapper)
        else if (auto decl_stmt = dynamic_cast<DeclarationStatementNode *>(node))
        {
            if (decl_stmt->declaration())
            {
                populate_symbol_table_with_scope(decl_stmt->declaration(), current_scope, scope_name);
            }
        }
        // Handle program nodes
        else if (auto program = dynamic_cast<ProgramNode *>(node))
        {
            const auto &statements = program->statements();
            for (const auto &stmt : statements)
            {
                populate_symbol_table_with_scope(stmt.get(), current_scope, scope_name);
            }
        }
        // Handle block statements
        else if (auto block = dynamic_cast<BlockStatementNode *>(node))
        {
            const auto &statements = block->statements();
            for (const auto &stmt : statements)
            {
                populate_symbol_table_with_scope(stmt.get(), current_scope, scope_name);
            }
        }
    }

    std::string CompilerInstance::build_function_signature(FunctionDeclarationNode *func_decl)
    {
        if (!func_decl)
            return "unknown";

        std::string signature;

        // Add generic parameters if present
        const auto &generic_params = func_decl->generic_parameters();
        if (!generic_params.empty())
        {
            signature += "<";
            for (size_t i = 0; i < generic_params.size(); ++i)
            {
                if (i > 0)
                    signature += ", ";
                signature += generic_params[i]->name();
            }
            signature += ">";
        }

        signature += "(";

        // Add parameter types
        const auto &params = func_decl->parameters();
        for (size_t i = 0; i < params.size(); ++i)
        {
            if (i > 0)
                signature += ", ";
            signature += params[i]->type_annotation();
        }

        signature += ") -> " + func_decl->return_type_annotation();
        return signature;
    }

    void CompilerInstance::initialize_standard_library()
    {
        // Initialize standard library from built-in intrinsics
        // The actual stdlib modules will be loaded dynamically through imports
        // The libcryo.a linking happens during final linking phase

        // Note: We don't pre-load stdlib symbols here anymore since they're loaded on-demand
        // through the import system. This allows for better modularity and avoids duplicate definitions.

        // LOG_INFO(Cryo::LogComponent::GENERAL, "Standard library initialized from libcryo");
    }

    void CompilerInstance::inject_auto_imports(SymbolTable *current_scope, const std::string &scope_name)
    {
        // Special handling for runtime files in stdlib mode - they need access to intrinsics
        if (_stdlib_compilation_mode &&
            (_source_file.find("runtime/runtime.cryo") != std::string::npos ||
             _source_file.find("stdlib/runtime/runtime.cryo") != std::string::npos))
        {
            LOG_DEBUG(Cryo::LogComponent::GENERAL, "Loading intrinsics for runtime compilation");

            // Load intrinsics for runtime compilation by importing the compiled stdlib
            try
            {
                // Create a synthetic ImportDeclarationNode for core/intrinsics
                auto intrinsics_import = std::make_unique<ImportDeclarationNode>(
                    SourceLocation(0, 0),                       // synthetic location
                    "core/intrinsics",                          // path
                    ImportDeclarationNode::ImportType::Absolute // absolute import (stdlib)
                );

                // Temporarily disable stdlib mode for this import to allow loading
                bool original_stdlib_mode = _stdlib_compilation_mode;
                _stdlib_compilation_mode = false;

                // Load the intrinsics import
                auto intrinsics_result = _module_loader->load_import(*intrinsics_import);

                // Restore stdlib mode
                _stdlib_compilation_mode = original_stdlib_mode;

                if (intrinsics_result.success && !intrinsics_result.symbol_map.empty())
                {
                    current_scope->register_namespace(intrinsics_result.module_name, intrinsics_result.symbol_map);
                    LOG_DEBUG(Cryo::LogComponent::GENERAL, "Loaded {} intrinsic symbols for runtime compilation",
                              intrinsics_result.symbol_map.size());
                }
                else
                {
                    LOG_WARN(Cryo::LogComponent::GENERAL, "Failed to load intrinsics for runtime: {}", intrinsics_result.error_message);
                }
            }
            catch (const std::exception &e)
            {
                LOG_ERROR(Cryo::LogComponent::GENERAL, "Exception loading intrinsics for runtime: {}", e.what());
            }

            return; // Don't do other auto-imports in stdlib mode
        }

        // Skip auto-import if we're in stdlib compilation mode to avoid circular dependencies
        if (_stdlib_compilation_mode)
        {
            LOG_DEBUG(Cryo::LogComponent::GENERAL, "Skipping auto-imports in stdlib compilation mode");
            return;
        }
        
        if (!_auto_imports_enabled)
        {
            LOG_DEBUG(Cryo::LogComponent::GENERAL, "Auto-imports disabled");
            return;
        }

        // Skip auto-import if we're compiling the core/types module itself
        if (_source_file.find("core/types.cryo") != std::string::npos ||
            _source_file.find("stdlib/core/types.cryo") != std::string::npos ||
            _source_file.find("runtime/runtime.cryo") != std::string::npos)
        {
            LOG_DEBUG(Cryo::LogComponent::GENERAL, "Skipping auto-imports when compiling core/types module");
            return;
        }

        LOG_DEBUG(Cryo::LogComponent::GENERAL, "Injecting auto-import: core/types");

        // Use the member ModuleLoader instance
        // Auto-detect stdlib root instead of using hardcoded path
        if (!_module_loader->auto_detect_stdlib_root())
        {
            // Fallback to relative path if auto-detection fails
            _module_loader->set_stdlib_root("./stdlib");
        }
        _module_loader->set_current_file(_source_file);

        // Create a synthetic ImportDeclarationNode for core/types
        // This simulates: import <core/types>;
        auto core_types_import = std::make_unique<ImportDeclarationNode>(
            SourceLocation(0, 0),                       // synthetic location
            "core/types",                               // path
            ImportDeclarationNode::ImportType::Absolute // absolute import (stdlib)
        );

        // Load the import
        auto result = _module_loader->load_import(*core_types_import);

        if (result.success)
        {
            // Register the namespace and symbols (wildcard import behavior)
            if (!result.symbol_map.empty())
            {
                current_scope->register_namespace(result.module_name, result.symbol_map);
                LOG_DEBUG(Cryo::LogComponent::GENERAL, "Auto-imported core/types: registered namespace '{}' with {} symbols",
                          result.module_name, result.symbol_map.size());
            }
            else
            {
                LOG_WARN(Cryo::LogComponent::GENERAL, "Auto-import succeeded but no symbols found in core/types");
            }
        }
        else
        {
            LOG_WARN(Cryo::LogComponent::GENERAL, "Failed to auto-import core/types: {}", result.error_message);
        }

        // Auto-import intrinsic functions for user projects
        LOG_DEBUG(Cryo::LogComponent::GENERAL, "Injecting auto-import: core/intrinsics");

        // Create a synthetic ImportDeclarationNode for core/intrinsics
        // This simulates: import <core/intrinsics>;
        auto intrinsics_import = std::make_unique<ImportDeclarationNode>(
            SourceLocation(0, 0),                       // synthetic location
            "core/intrinsics",                          // path
            ImportDeclarationNode::ImportType::Absolute // absolute import (stdlib)
        );

        // Load the intrinsics import
        auto intrinsics_result = _module_loader->load_import(*intrinsics_import);

        if (intrinsics_result.success)
        {
            // Register the namespace and symbols (wildcard import behavior)
            if (!intrinsics_result.symbol_map.empty())
            {
                current_scope->register_namespace(intrinsics_result.module_name, intrinsics_result.symbol_map);
                LOG_DEBUG(Cryo::LogComponent::GENERAL, "Auto-imported intrinsics: registered namespace '{}' with {} symbols",
                          intrinsics_result.module_name, intrinsics_result.symbol_map.size());

                // Also register under the short name "Intrinsics" for convenience
                // This allows both Intrinsics::__printf__ and std::Intrinsics::__printf__ to work
                current_scope->register_namespace("Intrinsics", intrinsics_result.symbol_map);
                LOG_DEBUG(Cryo::LogComponent::GENERAL, "Auto-imported intrinsics: also registered under short name 'Intrinsics' with {} symbols",
                          intrinsics_result.symbol_map.size());
            }
            else
            {
                LOG_WARN(Cryo::LogComponent::GENERAL, "Auto-import succeeded but no symbols found in intrinsics");
            }
        }
        else
        {
            LOG_WARN(Cryo::LogComponent::GENERAL, "Failed to auto-import intrinsics: {}", intrinsics_result.error_message);
        }

        // Auto-import runtime functions
        LOG_DEBUG(Cryo::LogComponent::GENERAL, "Injecting auto-import: runtime/runtime");

        // Create a synthetic ImportDeclarationNode for runtime/runtime
        // This simulates: import <runtime/runtime>;
        auto runtime_import = std::make_unique<ImportDeclarationNode>(
            SourceLocation(0, 0),                       // synthetic location
            "runtime/runtime",                          // path
            ImportDeclarationNode::ImportType::Absolute // absolute import (stdlib)
        );

        // Load the runtime import
        auto runtime_result = _module_loader->load_import(*runtime_import);

        if (runtime_result.success)
        {
            // Register the namespace and symbols (wildcard import behavior)
            if (!runtime_result.symbol_map.empty())
            {
                current_scope->register_namespace(runtime_result.module_name, runtime_result.symbol_map);
                LOG_DEBUG(Cryo::LogComponent::GENERAL, "Auto-imported runtime: registered namespace '{}' with {} symbols",
                          runtime_result.module_name, runtime_result.symbol_map.size());

                // Also register under the short name "Runtime" for convenience
                // This allows both Runtime::cryo_alloc and std::Runtime::cryo_alloc to work
                current_scope->register_namespace("Runtime", runtime_result.symbol_map);
                LOG_DEBUG(Cryo::LogComponent::GENERAL, "Auto-imported runtime: also registered under short name 'Runtime' with {} symbols",
                          runtime_result.symbol_map.size());
            }
            else
            {
                LOG_WARN(Cryo::LogComponent::GENERAL, "Auto-import succeeded but no symbols found in runtime");
            }
        }
        else
        {
            LOG_WARN(Cryo::LogComponent::GENERAL, "Failed to auto-import runtime: {}", runtime_result.error_message);
        }

        // Auto-import IO functions (printf, etc.)
        LOG_DEBUG(Cryo::LogComponent::GENERAL, "Injecting auto-import: io/stdio");

        // Create a synthetic ImportDeclarationNode for io/stdio
        // This simulates: import <io/stdio>;
        auto io_import = std::make_unique<ImportDeclarationNode>(
            SourceLocation(0, 0),                       // synthetic location
            "io/stdio",                                 // path
            ImportDeclarationNode::ImportType::Absolute // absolute import (stdlib)
        );

        // Load the io/stdio import
        auto io_result = _module_loader->load_import(*io_import);

        if (io_result.success)
        {
            // Register the namespace and symbols (wildcard import behavior)
            if (!io_result.symbol_map.empty())
            {
                current_scope->register_namespace(io_result.module_name, io_result.symbol_map);
                LOG_DEBUG(Cryo::LogComponent::GENERAL, "Auto-imported IO: registered namespace '{}' with {} symbols",
                          io_result.module_name, io_result.symbol_map.size());

                // Also register under the short name "IO" for convenience
                // This allows both IO::printf and std::IO::printf to work
                current_scope->register_namespace("IO", io_result.symbol_map);
                LOG_DEBUG(Cryo::LogComponent::GENERAL, "Auto-imported IO: also registered under short name 'IO' with {} symbols",
                          io_result.symbol_map.size());
            }
            else
            {
                LOG_WARN(Cryo::LogComponent::GENERAL, "Auto-import succeeded but no symbols found in io/stdio");
            }
        }
        else
        {
            LOG_WARN(Cryo::LogComponent::GENERAL, "Failed to auto-import io/stdio: {}", io_result.error_message);
        }
    }

    void CompilerInstance::process_struct_declarations_for_preregistration(ASTNode *node)
    {
        if (!node || !_codegen || !_codegen->get_visitor())
            return;

        LOG_DEBUG(Cryo::LogComponent::GENERAL, "Processing struct declarations for pre-registration...");

        // Process the AST to find and register struct declarations with the TypeMapper
        process_struct_declarations_recursive(node);
    }

    void CompilerInstance::register_ast_nodes_with_typemapper()
    {
        if (!_codegen || !_ast_root)
        {
            return;
        }

        // Ensure the visitor is initialized first
        if (!_codegen->ensure_visitor_initialized())
        {
            LOG_ERROR(Cryo::LogComponent::GENERAL, "Failed to initialize CodegenVisitor for AST node registration");
            return;
        }

        Codegen::CodegenVisitor *visitor = _codegen->get_visitor();
        if (!visitor)
        {
            LOG_ERROR(Cryo::LogComponent::GENERAL, "Failed to get CodegenVisitor for AST node registration");
            return;
        }

        Codegen::TypeMapper *type_mapper = visitor->get_type_mapper();
        if (!type_mapper)
        {
            LOG_ERROR(Cryo::LogComponent::GENERAL, "Failed to get TypeMapper for AST node registration");
            return;
        }

        LOG_DEBUG(Cryo::LogComponent::GENERAL, "Registering struct/class AST nodes with TypeMapper...");

        // Recursively find and register all struct/class declarations
        register_ast_nodes_recursive(_ast_root.get(), type_mapper);

        LOG_DEBUG(Cryo::LogComponent::GENERAL, "Completed AST node registration with TypeMapper");
    }

    void CompilerInstance::register_ast_nodes_recursive(ASTNode *node, Codegen::TypeMapper *type_mapper)
    {
        if (!node || !type_mapper)
            return;

        // Register struct declarations
        if (auto struct_decl = dynamic_cast<StructDeclarationNode *>(node))
        {
            LOG_DEBUG(Cryo::LogComponent::GENERAL, "Registering struct AST node: {}", struct_decl->name());
            type_mapper->register_struct_ast_node(struct_decl);
        }
        // Register class declarations
        else if (auto class_decl = dynamic_cast<ClassDeclarationNode *>(node))
        {
            LOG_DEBUG(Cryo::LogComponent::GENERAL, "Registering class AST node: {}", class_decl->name());
            type_mapper->register_class_ast_node(class_decl);
        }
        // Recurse into program nodes
        else if (auto program = dynamic_cast<ProgramNode *>(node))
        {
            for (const auto &statement : program->statements())
            {
                register_ast_nodes_recursive(statement.get(), type_mapper);
            }
        }
        // Recurse into block statements
        else if (auto block = dynamic_cast<BlockStatementNode *>(node))
        {
            for (const auto &statement : block->statements())
            {
                register_ast_nodes_recursive(statement.get(), type_mapper);
            }
        }
    }

    void CompilerInstance::process_struct_declarations_recursive(ASTNode *node)
    {
        if (!node)
            return;

        // If this is a struct declaration, process it
        if (auto struct_decl = dynamic_cast<StructDeclarationNode *>(node))
        {
            LOG_DEBUG(Cryo::LogComponent::GENERAL, "Pre-processing struct: {}", struct_decl->name());

            // Visit the struct declaration to register it with TypeMapper
            struct_decl->accept(*_codegen->get_visitor());
        }
        // If this is a program node, process all its statements
        else if (auto program = dynamic_cast<ProgramNode *>(node))
        {
            for (const auto &statement : program->statements())
            {
                process_struct_declarations_recursive(statement.get());
            }
        }
        
        // If this is a block statement, process its statements
        else if (auto block = dynamic_cast<BlockStatementNode *>(node))
        {
            for (const auto &statement : block->statements())
            {
                process_struct_declarations_recursive(statement.get());
            }
        }
        // For other nodes, we might need to recurse into their children
        // but for now, we're mainly interested in top-level struct declarations
    }

    std::string CompilerInstance::find_bin_directory() const
    {
        // Use the same approach as ModuleLoader to find the stdlib directory
        std::string stdlib_dir = Cryo::ModuleLoader::find_stdlib_directory();
        if (!stdlib_dir.empty())
        {
            // Convert stdlib path to bin path
            // stdlib_dir is something like "/workspaces/CryoLang/stdlib"
            // We need "/workspaces/CryoLang/bin"
            std::filesystem::path stdlib_path(stdlib_dir);
            std::filesystem::path cryo_root = stdlib_path.parent_path(); // Go up to CryoLang root
            std::filesystem::path bin_path = cryo_root / "bin";
            return bin_path.string();
        }

        // Fallback to relative path
        return "./bin";
    }

    //===----------------------------------------------------------------------===//
    // Directive System Implementation
    //===----------------------------------------------------------------------===//

    void CompilerInstance::initialize_directive_system()
    {
        _directive_registry = std::make_unique<DirectiveRegistry>();

        // Register built-in directive processors
        _directive_registry->register_processor(std::make_unique<TestDirectiveProcessor>());
        _directive_registry->register_processor(std::make_unique<ExpectErrorDirectiveProcessor>());
        _directive_registry->register_processor(std::make_unique<ExpectErrorsDirectiveProcessor>());

        LOG_DEBUG(LogComponent::GENERAL, "Initialized directive system with {} processors",
                  _directive_registry->get_available_directives().size());
    }

    bool CompilerInstance::process_directives()
    {
        if (!_ast_root || !_directive_registry)
        {
            return true; // No directives to process
        }

        LOG_DEBUG(LogComponent::GENERAL, "Processing directives in AST");

        // Create and use DirectiveWalker to process all directives in the AST
        DirectiveWalker walker(_directive_registry.get(), _compilation_context);

        // Walk the entire AST and process directives
        _ast_root->accept(walker);

        if (walker.has_errors())
        {
            LOG_ERROR(LogComponent::GENERAL, "Errors occurred while processing directives");
            return false;
        }

        LOG_DEBUG(LogComponent::GENERAL, "Successfully processed all directives");
        return true;
    }

    bool CompilerInstance::validate_directive_effects()
    {
        bool all_valid = _compilation_context.validate_all_effects();

        if (!all_valid)
        {
            LOG_ERROR(LogComponent::GENERAL, "Directive validation failed");
        }

        return all_valid;
    }

    std::string CompilerInstance::generate_method_name(const std::string &scope_name, const std::string &class_name, const std::string &method_name)
    {
        if (_symbol_resolution_manager)
        {
            // Use SRM to generate qualified method name
            std::vector<std::string> class_parts = {class_name};
            Cryo::SRM::QualifiedIdentifier qualified_id(class_parts, method_name, Cryo::SymbolKind::Function);
            return qualified_id.get_qualified_name();
        }
        else
        {
            // Fallback to manual concatenation
            std::vector<std::string> scope_parts = {scope_name, class_name};
            return Cryo::SRM::Utils::build_qualified_name(scope_parts, method_name);
        }
    }

    std::unique_ptr<CompilerInstance> create_compiler_instance()
    {
        return std::make_unique<CompilerInstance>();
    }
}
