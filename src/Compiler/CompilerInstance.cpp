#include "Compiler/CompilerInstance.hpp"
#include "Compiler/StandardPasses.hpp"
#include "Types/TypeMapper.hpp"
#include "Types/GenericTypes.hpp"
#include "Codegen/CodegenVisitor.hpp"
#include "AST/DirectiveProcessors.hpp"
#include "AST/DirectiveWalker.hpp"
#include "Utils/File.hpp"
#include "Utils/Logger.hpp"
#include "Utils/SymbolResolutionManager.hpp"
#include "Utils/SymbolDumper.hpp"
#include <iostream>
#include <fstream>
#include <filesystem>
#include <algorithm>

namespace Cryo
{
    CompilerInstance::CompilerInstance()
        : _debug_mode(false), _show_ast_before_ir(false), _stdlib_linking_enabled(true), _stdlib_compilation_mode(false), _auto_imports_enabled(true), _lsp_mode(false), _frontend_only(false), _dump_symbols(false), _dump_symbols_output_dir(""), _current_namespace("")
    {
        initialize_components();
    }

    void CompilerInstance::initialize_components()
    {
        _ast_context = std::make_unique<ASTContext>();
        _diagnostics = std::make_unique<DiagEmitter>();

        // Initialize Symbol Resolution Manager
        try
        {
            _symbol_resolution_context = std::make_unique<Cryo::SRM::SymbolResolutionContext>(
                &_ast_context->types());
            _symbol_resolution_manager = std::make_unique<Cryo::SRM::SymbolResolutionManager>(
                _symbol_resolution_context.get());
        }
        catch (const std::exception &e)
        {
            LOG_WARN(Cryo::LogComponent::GENERAL, "Failed to initialize Symbol Resolution Manager: {}", e.what());
            _symbol_resolution_context.reset();
            _symbol_resolution_manager.reset();
        }

        // Create Types components in order of dependencies:
        // 1. GenericRegistry (no dependencies)
        _generic_registry = std::make_unique<GenericRegistry>();

        // 2. TypeResolver (needs arena, modules, generics)
        _type_resolver = std::make_unique<TypeResolver>(
            _ast_context->types(),
            _ast_context->modules(),
            *_generic_registry,
            _diagnostics.get());

        // 3. SymbolTable (needs arena, modules)
        _symbol_table = std::make_unique<SymbolTable>(
            _ast_context->types(),
            _ast_context->modules());

        // 4. TypeChecker (needs arena, resolver, modules, generics)
        _type_checker = std::make_unique<TypeChecker>(
            _ast_context->types(),
            *_type_resolver,
            _ast_context->modules(),
            *_generic_registry,
            _diagnostics.get());

        // Create monomorphization pass
        _monomorphization_pass = std::make_unique<Monomorphizer>(
            _ast_context->types(),
            *_generic_registry,
            _ast_context->modules());

        // Create template registry
        _template_registry = std::make_unique<TemplateRegistry>();

        // Create module loader - needs to be created after symbol table and template registry
        // Pass the main ASTContext to ensure all modules use the same TypeArena
        _module_loader = std::make_unique<ModuleLoader>(*_symbol_table, *_template_registry, *_ast_context);
        _module_loader->set_generic_registry(_generic_registry.get());
        _module_loader->set_diagnostics(_diagnostics.get());

        // Note: TypeChecker doesn't have set_template_registry - template handling is done differently

        // Set auto-import callback for runtime dependencies
        _module_loader->set_auto_import_callback([this](SymbolTable *symbol_table, const std::string &scope_name, const std::string &source_file)
                                                 {
            // For runtime dependencies, only import what runtime needs: core/types and intrinsics
            // Don't do full auto-imports to avoid circular dependencies
            LOG_DEBUG(Cryo::LogComponent::GENERAL, "Auto-importing dependencies for runtime module");
            
            // Import core/types for string methods
            auto core_types_import = std::make_unique<ImportDeclarationNode>(
                SourceLocation(0, 0), "core::types");
            auto core_types_result = _module_loader->load_import(*core_types_import);
            if (core_types_result.success && !core_types_result.symbol_map.empty()) {
                LOG_DEBUG(Cryo::LogComponent::GENERAL, "Runtime auto-import: loaded core/types with {} symbols", core_types_result.symbol_map.size());
            }
            
            // Import intrinsics for __malloc__, __free__, etc.
            auto intrinsics_import = std::make_unique<ImportDeclarationNode>(
                SourceLocation(0, 0), "core::intrinsics");
            auto intrinsics_result = _module_loader->load_import(*intrinsics_import);
            if (intrinsics_result.success && !intrinsics_result.symbol_map.empty()) {
                LOG_DEBUG(Cryo::LogComponent::GENERAL, "Runtime auto-import: loaded intrinsics with {} symbols", intrinsics_result.symbol_map.size());
            } });

        // Initialize directive system
        initialize_directive_system();

        // Note: CodeGenerator will be created after parsing when namespace is known

        // Create linker with symbol table reference
        _linker = std::make_unique<Cryo::Linker::CryoLinker>(*_symbol_table);

        // Configure diagnostics
        DiagEmitter::Config diag_config;
        diag_config.colors = true;
        diag_config.unicode = true;
        diag_config.context_lines = 2;
        _diagnostics->configure(diag_config);

        // Initialize standard library built-ins
        initialize_standard_library();

        // Note: TypeChecker doesn't have load_builtin_symbols - symbols are shared via SymbolTable

        // Lexer and Parser will be created when we have a file to work with
    }

    void CompilerInstance::set_source_file(const std::string &file_path)
    {
        _source_file = file_path;

        // Note: TypeChecker doesn't have set_source_file - diagnostics use DiagEmitter
    }

    void CompilerInstance::add_include_path(const std::string &path)
    {
        _include_paths.push_back(path);
    }

    void CompilerInstance::set_namespace_context(const std::string &namespace_name)
    {
        _current_namespace = namespace_name;

        // Note: TypeChecker doesn't have set_current_namespace - context is handled differently
    }

    void CompilerInstance::set_show_stdlib_diagnostics(bool enable)
    {
        if (_diagnostics)
        {
            auto config = _diagnostics->config();
            config.show_stdlib_errors = enable;
            _diagnostics->configure(config);
        }
    }

    bool CompilerInstance::compile_file(const std::string &source_file)
    {
        LOG_DEBUG(LogComponent::GENERAL, "Compiling file: {}", source_file);

        // Step 1: Load the source file
        if (!load_source_file(source_file))
        {
            return false;
        }

        // Step 2: Initialize pass manager if not already done
        if (!_pass_manager)
        {
            initialize_pass_manager();
        }

        // Step 3: Initialize pass context
        initialize_pass_context();

        // Step 4: Run all passes through the PassManager
        bool success = _pass_manager->run_all(*_pass_context);

        if (success)
        {
            LOG_DEBUG(LogComponent::GENERAL, "Compilation completed successfully");
        }
        else
        {
            LOG_ERROR(LogComponent::GENERAL, "Compilation failed");
        }

        // Print AST if requested after compilation
        if (_show_ast_before_ir && _ast_root)
        {
            LOG_INFO(LogComponent::GENERAL, "Generated AST:");
            dump_ast();
        }

        // Return success from PassManager which now correctly tracks module-specific errors
        // (not accumulated errors from previous modules in stdlib compilation mode)
        return success;
    }

    bool CompilerInstance::parse_source(const std::string &source_code)
    {
        // Create an in-memory file
        auto file = make_file_from_string("source", source_code);
        if (!file)
        {
            _diagnostics->emit(Diag::error(ErrorCode::E0000_UNKNOWN, "Failed to create in-memory file"));
            return false;
        }

        return parse_source_from_file(std::move(file));
    }

    bool CompilerInstance::parse_source_from_file(std::unique_ptr<File> file)
    {
        reset_state();

        // Track error count at start to detect new errors (not accumulated from previous modules in stdlib mode)
        size_t errors_at_start = _diagnostics ? _diagnostics->error_count() : 0;

        try
        {
            // Store file information for diagnostics
            std::string file_path = file->path().empty() ? file->name() : file->path();
            std::string file_content = std::string(file->content());

            // Add source for diagnostic rendering
            _diagnostics->add_source_file(file_path);

            // Phase 1: Create lexer with the original file
            _lexer = std::make_unique<Lexer>(std::move(file));

            // Phase 2: Create parser with lexer and AST context
            _parser = std::make_unique<Parser>(std::move(_lexer), *_ast_context, _diagnostics.get(), file_path);
            _parser->set_directive_registry(_directive_registry.get());

            // Phase 3: Parse the program
            if (!parse())
            {
                _diagnostics->emit(Diag::error(ErrorCode::E0000_UNKNOWN, "Failed to parse source file"));
                return false;
            }

            // Phase 4: Basic validation
            if (!_ast_root)
            {
                _diagnostics->emit(Diag::error(ErrorCode::E0000_UNKNOWN, "Failed to create AST root"));
                return false;
            }

            // Phase 5: Semantic analysis (including symbol table population)
            if (!analyze())
            {
                // Errors are reported through DiagEmitter
                std::string namespace_name = _current_namespace.empty() ? "Global" : _current_namespace;
                size_t user_errors = _diagnostics->error_count();
                std::string error_message = "Module contains: " + std::to_string(user_errors) + " errors in namespace '" + namespace_name + "'";
                _diagnostics->emit(Diag::error(ErrorCode::E0805_INTERNAL_ERROR, error_message));
                return false;
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
                _diagnostics->emit(Diag::error(ErrorCode::E0000_UNKNOWN, "Failed to generate IR"));
                return false;
            }

            if (_debug_mode)
            {
                LOG_DEBUG(Cryo::LogComponent::GENERAL, "=== Compilation Completed ===");
            }

            // Check for NEW errors during this compilation (not accumulated from previous modules)
            size_t errors_now = _diagnostics ? _diagnostics->error_count() : 0;
            if (errors_now > errors_at_start)
            {
                return false;
            }

            return true;
        }
        catch (const std::exception &e)
        {
            _diagnostics->emit(Diag::error(ErrorCode::E0000_UNKNOWN, std::string("Exception during compilation: ") + e.what()));
            return false;
        }
    }

    bool CompilerInstance::compile_frontend_only(const std::string &source_file)
    {
        LOG_DEBUG(LogComponent::GENERAL, "Frontend-only compilation: {}", source_file);

        // Skip runtime.cryo in LSP mode
        if (source_file.find("runtime.cryo") != std::string::npos)
        {
            return true;
        }

        // Step 1: Load the source file
        if (!load_source_file(source_file))
        {
            return false;
        }

        // Step 2: Create frontend-only pass manager
        _pass_manager = StandardPassFactory::create_frontend_pipeline(*this);

        if (_debug_mode)
        {
            _pass_manager->set_verbose(true);
        }

        _pass_manager->validate();

        // Step 3: Initialize pass context
        initialize_pass_context();

        // Step 4: Run frontend passes only (stops before codegen)
        bool success = _pass_manager->run_all(*_pass_context);

        if (success)
        {
            LOG_DEBUG(LogComponent::GENERAL, "Frontend-only compilation completed successfully");
        }
        else
        {
            LOG_WARN(LogComponent::GENERAL, "Frontend-only compilation completed with errors");
            // For LSP, we continue even if analysis fails partially
            // This allows hover to work even with some compilation errors
        }

        // For frontend-only mode, we return true even if there are errors
        // to allow LSP features to work with partial information
        return true;
    }

    bool CompilerInstance::tokenize()
    {
        if (_source_file.empty())
        {
            _diagnostics->emit(Diag::error(ErrorCode::E0000_UNKNOWN, "No source file specified"));
            return false;
        }

        auto file = make_file_from_path(_source_file);
        if (!file || !file->load())
        {
            _diagnostics->emit(Diag::error(ErrorCode::E0801_FILE_READ_ERROR, "Failed to read file: " + _source_file));
            return false;
        }

        try
        {
            _lexer = std::make_unique<Lexer>(std::move(file));
            return true;
        }
        catch (const std::exception &e)
        {
            _diagnostics->emit(Diag::error(ErrorCode::E0000_UNKNOWN, std::string("Lexer error: ") + e.what()));
            return false;
        }
    }

    bool CompilerInstance::parse()
    {
        if (!_parser)
        {
            _diagnostics->emit(Diag::error(ErrorCode::E0000_UNKNOWN, "Parser not initialized"));
            return false;
        }

        try
        {
            _ast_root = _parser->parse_program();

            // Note: TypeChecker doesn't have discover_generic_types_from_ast
            // Generic types are now discovered during AST traversal in the symbol table population phase

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
            _codegen = Cryo::Codegen::create_default_codegen(*_ast_context, *_symbol_table, namespace_for_module, _diagnostics.get());

            // Set source info immediately after creating CodeGenerator
            // This ensures namespace context is available for analyze() which may initialize the visitor
            _codegen->set_source_info(_source_file, _current_namespace);

            // Configure stdlib compilation mode if enabled
            if (_stdlib_compilation_mode)
            {
                _codegen->set_stdlib_compilation_mode(true);
                // Note: TypeChecker doesn't have set_stdlib_compilation_mode - stdlib handling is done elsewhere
                if (_debug_mode)
                {
                    LOG_DEBUG(Cryo::LogComponent::GENERAL, "Enabled stdlib compilation mode in CodeGenerator");
                }
            }

            // Set TemplateRegistry for cross-module type resolution
            // This allows codegen to dynamically look up type namespaces from imported templates
            if (_codegen->ensure_visitor_initialized() && _template_registry)
            {
                _codegen->get_visitor()->context().set_template_registry(_template_registry.get());
                LOG_DEBUG(Cryo::LogComponent::GENERAL, "Set TemplateRegistry in CodegenContext for cross-module resolution");
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
            _diagnostics->emit(Diag::error(ErrorCode::E0000_UNKNOWN, std::string("Parse exception: ") + e.what()));
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
            _diagnostics->emit(Diag::error(ErrorCode::E0000_UNKNOWN, "AST root is null"));
            return false;
        }

        // Track error count at start to detect new errors (not accumulated from previous modules)
        size_t errors_at_start = _diagnostics ? _diagnostics->error_count() : 0;

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

            // Note: TypeChecker doesn't have check_imported_modules, load_intrinsic_symbols,
            // load_runtime_symbols, load_user_symbols, or check_program methods.
            // These were part of the old monolithic TypeChecker design.
            // TypeChecker provides type-checking utilities that are used during AST traversal.
            // The actual program checking needs to be done through a separate AST visitor.

            if (_debug_mode)
            {
                LOG_DEBUG(Cryo::LogComponent::GENERAL, "Phase 1: Symbol and type analysis completed via symbol table population");
            }

            // Check for NEW type errors from this module only (not accumulated errors from previous modules)
            size_t errors_now = _diagnostics ? _diagnostics->error_count() : 0;
            if (errors_now > errors_at_start)
            {
                if (_debug_mode)
                {
                    LOG_ERROR(Cryo::LogComponent::GENERAL, "Type checking failed with {} new errors",
                              errors_now - errors_at_start);
                }
                return false;
            }

            // Phase 2: Monomorphization - Generate specialized versions of generic types
            if (_debug_mode)
            {
                LOG_DEBUG(Cryo::LogComponent::GENERAL, "Phase 2: Monomorphization pass...");
            }

            // Import all cached instantiations from type resolution phase
            _monomorphization_pass->import_cached_instantiations();

            if (_debug_mode)
            {
                LOG_DEBUG(Cryo::LogComponent::GENERAL,
                          "Monomorphization: {} instantiations to process",
                          _monomorphization_pass->pending_count());
            }

            // Process all monomorphization requests
            if (_monomorphization_pass->has_pending())
            {
                if (!_monomorphization_pass->process_all())
                {
                    if (_debug_mode)
                    {
                        LOG_ERROR(Cryo::LogComponent::GENERAL, "Monomorphization pass failed");
                    }
                    // Note: Don't return false - monomorphization failures may be non-fatal
                    // depending on whether the unprocessed instantiations are actually used
                }

                if (_debug_mode)
                {
                    LOG_DEBUG(Cryo::LogComponent::GENERAL,
                              "Monomorphization: {} specializations created",
                              _monomorphization_pass->specialization_count());
                }
            }
            else
            {
                if (_debug_mode)
                {
                    LOG_DEBUG(Cryo::LogComponent::GENERAL,
                              "Monomorphization: No generic instantiations to process");
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
            _diagnostics->emit(Diag::error(ErrorCode::E0504_NAMESPACE_CONFLICT, std::string("Namespace conflict: ") + e.what()));
            return false;
        }
        catch (const std::exception &e)
        {
            LOG_ERROR(Cryo::LogComponent::GENERAL, "Exception caught in analyze(): {}", e.what());
            _diagnostics->emit(Diag::error(ErrorCode::E0000_UNKNOWN, std::string("Analysis exception: ") + e.what()));
            return false;
        }
    }

    bool CompilerInstance::generate_ir()
    {
        if (!_ast_root)
        {
            _diagnostics->emit(Diag::error(ErrorCode::E0000_UNKNOWN, "AST root is null for IR generation"));
            return false;
        }

        if (!_codegen)
        {
            _diagnostics->emit(Diag::error(ErrorCode::E0000_UNKNOWN, "CodeGenerator not initialized"));
            return false;
        }

        try
        {
            // Set source file and namespace context before generating IR
            LOG_DEBUG(Cryo::LogComponent::GENERAL, "Setting source info for codegen...");
            _codegen->set_source_info(_source_file, _current_namespace);

            // Sync imported namespaces from compiler's SRM to codegen's SRM
            // This ensures functions like malloc from imported namespaces can be resolved
            if (_symbol_resolution_manager && _codegen->ensure_visitor_initialized())
            {
                auto *compiler_srm_ctx = _symbol_resolution_manager->get_context();
                auto *codegen_srm_ctx = _codegen->get_visitor()->get_srm_context();

                if (compiler_srm_ctx && codegen_srm_ctx)
                {
                    const auto &imported_namespaces = compiler_srm_ctx->get_imported_namespaces();
                    for (const auto &ns : imported_namespaces)
                    {
                        codegen_srm_ctx->add_imported_namespace(ns);
                        LOG_DEBUG(Cryo::LogComponent::GENERAL, "Synced imported namespace to codegen SRM: '{}'", ns);
                    }
                    LOG_DEBUG(Cryo::LogComponent::GENERAL, "Synced {} imported namespaces to codegen SRM", imported_namespaces.size());
                }
            }

            // Note: TypeChecker doesn't have get_srm_context() or specialized method tracking.
            // import_specialized_methods and import_namespace_aliases need reimplementation for Types.
            if (_debug_mode && _codegen->get_visitor())
            {
                LOG_DEBUG(Cryo::LogComponent::GENERAL, "Skipping specialized methods import - requires Types reimplementation");
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
                _diagnostics->emit(Diag::error(ErrorCode::E0000_UNKNOWN, "IR generation failed: " + _codegen->get_last_error()));
            }

            return success;
        }
        catch (const std::exception &e)
        {
            _diagnostics->emit(Diag::error(ErrorCode::E0000_UNKNOWN, std::string("IR generation exception: ") + e.what()));
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
            _diagnostics->emit(Diag::error(ErrorCode::E0000_UNKNOWN, "Linker not initialized"));
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
                _diagnostics->emit(Diag::error(ErrorCode::E0000_UNKNOWN, "No module generated for linking"));
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

                _diagnostics->emit(Diag::error(error_code, custom_message));
            }

            return success;
        }
        catch (const std::exception &e)
        {
            _diagnostics->emit(Diag::error(ErrorCode::E0000_UNKNOWN, std::string("Link exception: ") + e.what()));
            return false;
        }
    }

    bool CompilerInstance::has_errors() const
    {
        return _diagnostics && _diagnostics->has_errors();
    }

    bool CompilerInstance::compile_for_lsp(const std::string &source_file)
    {
        try
        {
            // Set LSP mode
            _lsp_mode = true;
            _frontend_only = true; // LSP doesn't need codegen

            if (_debug_mode)
            {
                LOG_DEBUG(Cryo::LogComponent::GENERAL, "Starting LSP compilation for: {}", source_file);
            }

            // Set source file
            set_source_file(source_file);

            // Parse and analyze (no codegen needed for LSP)
            auto file = make_file_from_path(source_file);
            if (!file || !file->is_loaded())
            {
                if (_debug_mode)
                {
                    LOG_ERROR(Cryo::LogComponent::GENERAL, "LSP: Could not load file {}", source_file);
                }
                return false;
            }

            if (!parse_source_from_file(std::move(file)))
            {
                if (_debug_mode)
                {
                    LOG_ERROR(Cryo::LogComponent::GENERAL, "LSP: Parse failed for {}", source_file);
                }
                return false;
            }

            // Run analysis phase (type checking, symbol resolution)
            if (!analyze())
            {
                if (_debug_mode)
                {
                    LOG_WARN(Cryo::LogComponent::GENERAL, "LSP: Analysis failed for {} - providing partial information", source_file);
                }
                // Don't return false - LSP should provide partial information even with errors
            }

            if (_debug_mode)
            {
                LOG_DEBUG(Cryo::LogComponent::GENERAL, "LSP compilation completed for: {}", source_file);
            }
            return true;
        }
        catch (const std::exception &e)
        {
            if (_debug_mode)
            {
                LOG_ERROR(Cryo::LogComponent::GENERAL, "LSP compilation exception: {}", e.what());
            }
            _diagnostics->emit(Diag::error(ErrorCode::E0000_UNKNOWN, std::string("LSP compilation exception: ") + e.what()));
            return false;
        }
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
            // TODO: SymbolTable print method not yet implemented
            os << "Symbol table has " << _symbol_table->scope_depth() << " scope(s)" << std::endl;
        }
        else
        {
            os << "No symbol table available" << std::endl;
        }
    }

    void CompilerInstance::dump_type_table(std::ostream &os) const
    {
        // Note: TypeChecker doesn't have print_type_table - types are managed by TypeArena
        if (_ast_context)
        {
            os << "=== Types in Arena ===" << std::endl;
            // TypeArena doesn't have a print method, but we can output some info
            os << "(Type table dump requires TypeArena iteration - not yet implemented)" << std::endl;
            os << "===================" << std::endl;
        }
        else
        {
            os << "No AST context available" << std::endl;
        }
    }

    void CompilerInstance::dump_type_errors(std::ostream &os) const
    {
        // TypeChecker doesn't track errors internally - use DiagEmitter
        if (_diagnostics && _diagnostics->has_errors())
        {
            os << "=== Type Errors ===" << std::endl;
            _diagnostics->render_all(os);
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
        if (_diagnostics)
        {
            // Debug: Show what diagnostics we have
            const auto &diagnostics = _diagnostics->diagnostics();
            if (_debug_mode)
            {
                LOG_DEBUG(Cryo::LogComponent::GENERAL, "DiagEmitter has {} total diagnostics", diagnostics.size());
                for (size_t i = 0; i < diagnostics.size(); ++i)
                {
                    const auto &diag = diagnostics[i];
                    LOG_DEBUG(Cryo::LogComponent::GENERAL, "Diagnostic {}: level={}, message='{}'",
                              i, static_cast<int>(diag.level()), diag.message());
                }
            }

            size_t total = _diagnostics->error_count() + _diagnostics->warning_count();
            if (total > 0)
            {
                _diagnostics->render_all(os);
                _diagnostics->print_summary(os);
            }
        }
    }

    void CompilerInstance::clear()
    {
        reset_state();
        _source_file.clear();
        _include_paths.clear();
        if (_diagnostics)
        {
            _diagnostics->clear();
        }
    }

    void CompilerInstance::reset_state()
    {
        // Reset codegen FIRST before parser, as codegen may have references to data
        // that was created during parsing. Order matters for proper cleanup.
        _codegen.reset();

        _ast_root.reset();
        // Don't clear diagnostics during stdlib compilation - we want to accumulate
        // errors from all modules so they can be reported together at the end
        if (_diagnostics && !_stdlib_compilation_mode)
        {
            _diagnostics->clear();
        }
        _lexer.reset();
        _parser.reset();

        // Note: TypeChecker doesn't have reset_state or set_stdlib_compilation_mode.
        // TypeChecker is stateless - it just provides type-checking utilities.
        // State is managed by TypeArena, SymbolTable, and DiagEmitter.
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
                // Enable pre-registration mode to skip method body generation
                _codegen->get_visitor()->set_pre_registration_mode(true);

                // First, process struct declarations to ensure TypeMapper has correct type information
                process_struct_declarations_for_preregistration(node);

                // Disable pre-registration mode
                _codegen->get_visitor()->set_pre_registration_mode(false);

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
            std::vector<TypeRef> param_types;
            for (const auto &param : func_decl->parameters())
            {
                // Use resolved parameter type directly
                TypeRef param_type = param->get_resolved_type();
                param_types.push_back(param_type);
            }

            // Use resolved return type directly
            TypeRef return_type = func_decl->get_resolved_return_type();

            // Handle functions that may not have resolved return types yet
            if (!return_type.is_valid())
            {
                LOG_WARN(Cryo::LogComponent::GENERAL, "Function '{}' has null return type, defaulting to void", func_decl->name());
                return_type = _ast_context->types().get_void(); // Safe fallback
            }

            // Create function type
            TypeRef function_type = _ast_context->types().get_function(return_type, param_types);

            // Build enhanced signature including generic parameters
            std::string enhanced_signature = build_function_signature(func_decl);

            // Add function to symbol table
            _symbol_table->declare_function(func_decl->name(), function_type, func_decl->location());

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
            std::vector<TypeRef> param_types;
            for (const auto &param : intrinsic_decl->parameters())
            {
                // Use resolved parameter type directly
                TypeRef param_type = param->get_resolved_type();
                param_types.push_back(param_type);
            }

            // Use resolved return type directly
            TypeRef return_type = intrinsic_decl->get_resolved_return_type();

            // Handle intrinsics that may not have resolved return types yet
            if (!return_type.is_valid())
            {
                LOG_WARN(Cryo::LogComponent::GENERAL, "Intrinsic '{}' has null return type, defaulting to void", intrinsic_decl->name());
                return_type = _ast_context->types().get_void(); // Safe fallback
            }

            // Create function type
            TypeRef function_type = _ast_context->types().get_function(return_type, param_types);

            // Add intrinsic function to current (global) scope with Intrinsic symbol kind
            // Register intrinsics only in std::Intrinsics namespace for consistency
            _symbol_table->declare_intrinsic(intrinsic_decl->name(), function_type, intrinsic_decl->location());

            LOG_TRACE(Cryo::LogComponent::GENERAL, "Registered intrinsic function '{}' in namespace 'std::Intrinsics'",
                      intrinsic_decl->name());
        }
        // Handle struct declarations
        else if (auto struct_decl = dynamic_cast<StructDeclarationNode *>(node))
        {
            // Check if type was already pre-registered during parsing (for self-referential types)
            TypeRef struct_type = _symbol_table->lookup_struct_type(struct_decl->name());
            if (!struct_type.is_valid())
            {
                // Type wasn't pre-registered, create and register it now
                ModuleID current_module = _symbol_table->current_module();
                struct_type = _ast_context->types().create_struct(QualifiedTypeName{current_module, struct_decl->name()});
                _symbol_table->declare_type(struct_decl->name(), struct_type, struct_decl->location());
            }

            // Register struct methods with fully qualified names
            for (const auto &method : struct_decl->methods())
            {
                if (method)
                {
                    std::string method_name = generate_method_name(scope_name, struct_decl->name(), method->name());
                    LOG_TRACE(Cryo::LogComponent::GENERAL, "Pass 1: Found struct method: {}", method_name);

                    // Create function type for the method
                    std::vector<TypeRef> param_types;
                    for (const auto &param : method->parameters())
                    {
                        TypeRef param_type = param->get_resolved_type();
                        param_types.push_back(param_type);
                    }

                    TypeRef return_type = method->get_resolved_return_type();

                    // Handle constructors that may not have resolved return types yet
                    if (!return_type.is_valid())
                    {
                        if (method->is_constructor())
                        {
                            return_type = _ast_context->types().get_void();
                        }
                        else
                        {
                            LOG_WARN(Cryo::LogComponent::GENERAL, "Struct method '{}' has null return type and is not a constructor", method_name);
                            return_type = _ast_context->types().get_void(); // Safe fallback
                        }
                    }

                    TypeRef function_type = _ast_context->types().get_function(return_type, param_types);

                    // Register method in symbol table with fully qualified name
                    _symbol_table->declare_function(method_name, function_type, method->location());
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

                // Also register in GenericRegistry for the new type system
                if (_generic_registry)
                {
                    std::vector<GenericParam> params;
                    for (size_t i = 0; i < struct_decl->generic_parameters().size(); ++i)
                    {
                        auto &param_node = struct_decl->generic_parameters()[i];
                        TypeRef param_type = _ast_context->types().create_generic_param(param_node->name(), i);
                        params.emplace_back(param_node->name(), i, param_type);
                    }
                    _generic_registry->register_template(struct_type, params,
                        _symbol_table->current_module(), struct_decl, struct_decl->name());
                    LOG_DEBUG(Cryo::LogComponent::GENERAL, "Registered struct '{}' in GenericRegistry with {} params",
                        struct_decl->name(), params.size());
                }

                LOG_TRACE(Cryo::LogComponent::GENERAL, "Registered local generic struct template: {}", struct_decl->name());
            }
        }
        // Handle class declarations
        else if (auto class_decl = dynamic_cast<ClassDeclarationNode *>(node))
        {
            // Check if type was already pre-registered during parsing (for self-referential types)
            TypeRef class_type = _symbol_table->lookup_class_type(class_decl->name());
            if (!class_type.is_valid())
            {
                // Type wasn't pre-registered, create and register it now
                ModuleID current_module = _symbol_table->current_module();
                class_type = _ast_context->types().create_class(QualifiedTypeName{current_module, class_decl->name()});
                _symbol_table->declare_type(class_decl->name(), class_type, class_decl->location());
            }

            // Register class methods with fully qualified names
            for (const auto &method : class_decl->methods())
            {
                if (method)
                {
                    std::string method_name = generate_method_name(scope_name, class_decl->name(), method->name());
                    LOG_TRACE(Cryo::LogComponent::GENERAL, "Pass 1: Found class method: {}", method_name);

                    // Create function type for the method
                    std::vector<TypeRef> param_types;
                    for (const auto &param : method->parameters())
                    {
                        TypeRef param_type = param->get_resolved_type();
                        param_types.push_back(param_type);
                    }

                    TypeRef return_type = method->get_resolved_return_type();

                    // Handle constructors that may not have resolved return types yet
                    if (!return_type.is_valid())
                    {
                        if (method->is_constructor())
                        {
                            return_type = _ast_context->types().get_void();
                        }
                        else
                        {
                            LOG_WARN(Cryo::LogComponent::GENERAL, "Method '{}' has null return type and is not a constructor", method_name);
                            return_type = _ast_context->types().get_void(); // Safe fallback
                        }
                    }

                    TypeRef function_type = _ast_context->types().get_function(return_type, param_types);

                    // Register method in symbol table with fully qualified name
                    _symbol_table->declare_function(method_name, function_type, method->location());
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

                // Also register in GenericRegistry for the new type system
                if (_generic_registry)
                {
                    std::vector<GenericParam> params;
                    for (size_t i = 0; i < class_decl->generic_parameters().size(); ++i)
                    {
                        auto &param_node = class_decl->generic_parameters()[i];
                        TypeRef param_type = _ast_context->types().create_generic_param(param_node->name(), i);
                        params.emplace_back(param_node->name(), i, param_type);
                    }
                    _generic_registry->register_template(class_type, params,
                        _symbol_table->current_module(), class_decl, class_decl->name());
                    LOG_DEBUG(Cryo::LogComponent::GENERAL, "Registered class '{}' in GenericRegistry with {} params",
                        class_decl->name(), params.size());
                }

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
            ModuleID current_module = _symbol_table->current_module();
            TypeRef enum_type = _ast_context->types().create_enum(QualifiedTypeName{current_module, enum_decl->name()});
            _symbol_table->declare_type(enum_decl->name(), enum_type, enum_decl->location());

            // IMMEDIATELY register enum variants in codegen context to prevent early reference issues
            // This ensures variants are available when struct constructors reference them during pre-registration
            if (_codegen && _codegen->ensure_visitor_initialized())
            {
                auto *visitor = _codegen->get_visitor();
                auto &enum_variants_map = visitor->context().enum_variants_map();

                LOG_DEBUG(Cryo::LogComponent::GENERAL, "Pre-registering enum variants for {}", enum_decl->name());

                // For simple enums, register integer constants for each variant
                if (is_simple_enum)
                {
                    for (size_t i = 0; i < variant_names.size(); ++i)
                    {
                        std::string qualified_name = enum_decl->name() + "::" + variant_names[i];

                        // Create integer constant for this variant
                        llvm::Constant *variant_const = llvm::ConstantInt::get(
                            llvm::Type::getInt32Ty(visitor->context().llvm_context()),
                            static_cast<uint64_t>(i));

                        // Register in enum_variants_map
                        enum_variants_map[qualified_name] = variant_const;
                        LOG_DEBUG(Cryo::LogComponent::GENERAL, "Pre-registered enum variant: {} = {}", qualified_name, i);
                    }
                }
            }

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

                // Also register in GenericRegistry for the new type system
                if (_generic_registry)
                {
                    std::vector<GenericParam> params;
                    for (size_t i = 0; i < enum_decl->generic_parameters().size(); ++i)
                    {
                        auto &param_node = enum_decl->generic_parameters()[i];
                        TypeRef param_type = _ast_context->types().create_generic_param(param_node->name(), i);
                        params.emplace_back(param_node->name(), i, param_type);
                    }
                    _generic_registry->register_template(enum_type, params,
                        _symbol_table->current_module(), enum_decl, enum_decl->name());
                    LOG_DEBUG(Cryo::LogComponent::GENERAL, "Registered enum '{}' in GenericRegistry with {} params",
                        enum_decl->name(), params.size());
                }

                LOG_TRACE(Cryo::LogComponent::GENERAL, "Registered local generic enum template: {}", enum_decl->name());
            }
        }
        // Handle trait declarations
        else if (auto trait_decl = dynamic_cast<TraitDeclarationNode *>(node))
        {
            // Create trait type and add to symbol table as a type
            ModuleID current_module = _symbol_table->current_module();
            TypeRef trait_type = _ast_context->types().create_trait(QualifiedTypeName{current_module, trait_decl->name()});
            _symbol_table->declare_type(trait_decl->name(), trait_type, trait_decl->location());

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
        // Handle type alias declarations
        else if (auto alias_decl = dynamic_cast<TypeAliasDeclarationNode *>(node))
        {
            LOG_DEBUG(Cryo::LogComponent::GENERAL, "Processing type alias: {}", alias_decl->alias_name());

            // Type aliases are transparent - the alias name is just another name for the target type
            ModuleID current_module = _symbol_table->current_module();

            // If the alias already has a resolved target type, use it directly
            if (alias_decl->has_resolved_target_type())
            {
                TypeRef target_type = alias_decl->get_resolved_target_type();
                // Register the target type under the alias name
                _symbol_table->declare_type(alias_decl->alias_name(), target_type, alias_decl->location());
                LOG_DEBUG(Cryo::LogComponent::GENERAL, "Registered type alias '{}' -> '{}'",
                          alias_decl->alias_name(), target_type->display_name());
            }
            else if (alias_decl->has_target_type_annotation())
            {
                // Type not resolved yet - create a placeholder struct that will be replaced in TypeResolutionPass
                QualifiedTypeName placeholder_qname{current_module, alias_decl->alias_name()};
                TypeRef placeholder = _ast_context->types().create_struct(placeholder_qname);
                _symbol_table->declare_type(alias_decl->alias_name(), placeholder, alias_decl->location());
                LOG_DEBUG(Cryo::LogComponent::GENERAL, "Registered type alias '{}' with deferred resolution (target: '{}')",
                          alias_decl->alias_name(), alias_decl->target_type_annotation()->to_string());
            }
            else
            {
                // Forward declaration (no target type)
                LOG_DEBUG(Cryo::LogComponent::GENERAL, "Type alias '{}' is a forward declaration (no target)",
                          alias_decl->alias_name());
            }

            // Register generic type alias templates if this alias has generic parameters
            if (alias_decl->is_generic() && _generic_registry)
            {
                LOG_DEBUG(Cryo::LogComponent::GENERAL, "Registering generic type alias template: {}", alias_decl->alias_name());

                // Create a type alias type for the template
                TypeRef alias_type = _ast_context->types().create_type_alias(
                    QualifiedTypeName{_symbol_table->current_module(), alias_decl->alias_name()},
                    TypeRef{}  // Target will be resolved during instantiation
                );

                std::vector<GenericParam> params;
                for (size_t i = 0; i < alias_decl->generic_params().size(); ++i)
                {
                    const std::string &param_name = alias_decl->generic_params()[i];
                    TypeRef param_type = _ast_context->types().create_generic_param(param_name, i);
                    params.emplace_back(param_name, i, param_type);
                }
                _generic_registry->register_template(alias_type, params,
                    _symbol_table->current_module(), alias_decl, alias_decl->alias_name());
                LOG_DEBUG(Cryo::LogComponent::GENERAL, "Registered generic type alias '{}' in GenericRegistry with {} params",
                    alias_decl->alias_name(), params.size());
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

                        _symbol_table->register_namespace(result.namespace_alias, result.symbol_map);
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
                            _symbol_table->declare_symbol(symbol_name, symbol.kind, symbol.location, symbol.type, scope_name);
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
                        _symbol_table->register_namespace(namespace_name, result.symbol_map);
                        _imported_namespaces.push_back(namespace_name); // Track for enhanced resolution

                        // Also register in SRM context for proper wildcard import resolution
                        if (_symbol_resolution_manager && _symbol_resolution_manager->get_context())
                        {
                            _symbol_resolution_manager->get_context()->add_imported_namespace(namespace_name);
                            LOG_DEBUG(Cryo::LogComponent::GENERAL, "Added '{}' to SRM imported namespaces", namespace_name);
                        }

                        // Note: TypeChecker doesn't have get_srm_context - SRM synchronization not available

                        // Also register symbols directly in current scope for unqualified access (e.g., println)
                        // This enables wildcard imports to work with direct function calls
                        for (const auto &[symbol_name, symbol] : result.symbol_map)
                        {
                            // Register each symbol directly in the current scope for unqualified access
                            _symbol_table->declare_symbol(symbol_name, symbol.kind, symbol.location, symbol.type, scope_name);
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
                // Emit diagnostic for import failure (ModuleLoader should have already emitted, but this is a backup)
                if (_diagnostics)
                {
                    _diagnostics->emit(
                        Diag::error(ErrorCode::E0502_INVALID_IMPORT, result.error_message)
                            .at(import_decl));
                }
            }
        }
        // Handle global constants in first pass to prevent early reference issues
        else if (auto var_decl = dynamic_cast<VariableDeclarationNode *>(node))
        {
            // Only handle global constants - regular variables are processed by TypeChecker
            if (var_decl->is_global() && !var_decl->is_mutable() && var_decl->has_initializer())
            {
                LOG_DEBUG(Cryo::LogComponent::GENERAL, "Pre-registering global constant: {}", var_decl->name());

                // IMMEDIATELY register global constant in codegen context to prevent early reference issues
                if (_codegen && _codegen->ensure_visitor_initialized())
                {
                    auto *visitor = _codegen->get_visitor();

                    // Pre-register global constant with a placeholder value
                    // The actual value will be generated later during proper codegen
                    llvm::GlobalVariable *global_var = new llvm::GlobalVariable(
                        *visitor->context().module(),
                        visitor->context().types().map(var_decl->get_resolved_type()),
                        true, // is_constant
                        llvm::GlobalValue::ExternalLinkage,
                        nullptr, // initializer will be set later
                        var_decl->name());

                    // Store in globals map for later retrieval
                    visitor->context().globals_map()[var_decl->name()] = global_var;
                    LOG_DEBUG(Cryo::LogComponent::GENERAL, "Pre-registered global constant: {}", var_decl->name());
                }

                // Still register in symbol table for type checking
                _symbol_table->declare_variable(var_decl->name(), var_decl->get_resolved_type(), var_decl->location(), false);
            }
            // Skip other variable declarations - they will be properly handled by TypeChecker
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
        // Handle constant variable declarations only (const variables need global visibility)
        else if (auto var_decl = dynamic_cast<VariableDeclarationNode *>(node))
        {
            // Only process const variables (not mutable ones) - they need to be available for lookup
            if (!var_decl->is_mutable())
            {
                // Use resolved type from AST node
                TypeRef var_type = var_decl->get_resolved_type();

                // Add constant to current scope for global visibility
                bool success = current_scope->declare_symbol(var_decl->name(), SymbolKind::Variable,
                                                             var_decl->location(), var_type, scope_name);

                if (!success && _debug_mode)
                {
                    LOG_WARN(Cryo::LogComponent::GENERAL, "Constant '{}' already declared in scope '{}'",
                             var_decl->name(), scope_name);
                }
                else if (_debug_mode)
                {
                    LOG_DEBUG(Cryo::LogComponent::GENERAL, "Added constant '{}' to scope '{}' with type '{}'",
                              var_decl->name(), scope_name, var_type.is_valid() ? var_type.get()->display_name() : "null");
                }
            }
            // Skip mutable variables - they will be handled by TypeChecker
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
            auto *type_ann = params[i]->type_annotation();
            signature += type_ann ? type_ann->to_string() : "unknown";
        }

        auto *return_type_ann = func_decl->return_type_annotation();
        signature += ") -> " + (return_type_ann ? return_type_ann->to_string() : "void");
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
        // Special handling for runtime files in stdlib mode - they need access to intrinsics AND core/types
        if (_stdlib_compilation_mode &&
            (_source_file.find("runtime/runtime.cryo") != std::string::npos ||
             _source_file.find("stdlib/runtime/runtime.cryo") != std::string::npos ||
             _source_file.find("runtime\\runtime.cryo") != std::string::npos ||
             _source_file.find("stdlib\\runtime\\runtime.cryo") != std::string::npos ||
             _source_file.find("std::Runtime") != std::string::npos ||
             _source_file == "runtime/runtime" ||
             _source_file == "std::Runtime.cryo"))
        {
            LOG_DEBUG(Cryo::LogComponent::GENERAL, "Loading intrinsics and core/types for runtime compilation");

            // Load intrinsics for runtime compilation by importing the compiled stdlib
            try
            {
                // Create a synthetic ImportDeclarationNode for core/intrinsics
                auto intrinsics_import = std::make_unique<ImportDeclarationNode>(
                    SourceLocation(0, 0), // synthetic location
                    "core::intrinsics"    // module path
                );

                // Temporarily disable stdlib mode for this import to allow loading
                bool original_stdlib_mode = _stdlib_compilation_mode;
                _stdlib_compilation_mode = false;

                // Load the intrinsics import
                auto intrinsics_result = _module_loader->load_import(*intrinsics_import);

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

                // Also load core/types for runtime files to access string.length() and other primitive methods
                auto core_types_import = std::make_unique<ImportDeclarationNode>(
                    SourceLocation(0, 0), // synthetic location
                    "core::types"         // module path
                );

                // Load the core/types import
                auto core_types_result = _module_loader->load_import(*core_types_import);

                // Restore stdlib mode
                _stdlib_compilation_mode = original_stdlib_mode;

                if (core_types_result.success && !core_types_result.symbol_map.empty())
                {
                    current_scope->register_namespace(core_types_result.module_name, core_types_result.symbol_map);
                    LOG_DEBUG(Cryo::LogComponent::GENERAL, "Loaded {} core/types symbols for runtime compilation - string methods should now be available",
                              core_types_result.symbol_map.size());
                }
                else
                {
                    LOG_WARN(Cryo::LogComponent::GENERAL, "Failed to load core/types for runtime: {}", core_types_result.error_message);
                }
            }
            catch (const std::exception &e)
            {
                LOG_ERROR(Cryo::LogComponent::GENERAL, "Exception loading auto-imports for runtime: {}", e.what());
            }

            return; // Don't do other auto-imports in stdlib mode
        }

        // Determine if this is a runtime file that needs special handling
        bool is_runtime_file = (_source_file.find("runtime/runtime.cryo") != std::string::npos ||
                                _source_file.find("stdlib/runtime/runtime.cryo") != std::string::npos ||
                                _source_file.find("runtime\\runtime.cryo") != std::string::npos ||
                                _source_file.find("stdlib\\runtime\\runtime.cryo") != std::string::npos ||
                                _source_file.find("std::Runtime") != std::string::npos ||
                                _source_file == "runtime/runtime" ||
                                _source_file == "std::Runtime.cryo");

        // Skip auto-import if we're in stdlib compilation mode to avoid circular dependencies
        // Exception: runtime files already handled above with intrinsics + core/types
        if (_stdlib_compilation_mode && !is_runtime_file)
        {
            LOG_DEBUG(Cryo::LogComponent::GENERAL, "Skipping auto-imports in stdlib compilation mode");
            return;
        }

        if (!_auto_imports_enabled)
        {
            LOG_DEBUG(Cryo::LogComponent::GENERAL, "Auto-imports disabled");
            return;
        }

        // Skip auto-import if we're compiling the prelude itself or core modules to avoid circular dependencies
        if (_source_file.find("prelude.cryo") != std::string::npos ||
            _source_file.find("stdlib/prelude.cryo") != std::string::npos ||
            _source_file.find("new_stdlib/prelude.cryo") != std::string::npos ||
            _source_file.find("prelude\\prelude.cryo") != std::string::npos ||
            _source_file.find("core/types.cryo") != std::string::npos ||
            _source_file.find("stdlib/core/types.cryo") != std::string::npos ||
            _source_file.find("core\\types.cryo") != std::string::npos ||
            _source_file.find("stdlib\\core\\types.cryo") != std::string::npos ||
            _source_file.find("std::Core") != std::string::npos ||
            _source_file.find("std::prelude") != std::string::npos)
        {
            LOG_DEBUG(Cryo::LogComponent::GENERAL, "Skipping auto-imports when compiling prelude or core modules to avoid circular dependencies");
            return;
        }

        // Check if we're in stdlib mode and only allow core/types for runtime files
        bool stdlib_mode_runtime_exception = _stdlib_compilation_mode && is_runtime_file;

        if (is_runtime_file)
        {
            LOG_DEBUG(Cryo::LogComponent::GENERAL, "Runtime file detected: {}, stdlib_mode: {}, exception_active: {}",
                      _source_file, _stdlib_compilation_mode, stdlib_mode_runtime_exception);
        }

        LOG_DEBUG(Cryo::LogComponent::GENERAL, "Injecting auto-import: prelude");

        // Use the member ModuleLoader instance
        // Auto-detect stdlib root instead of using hardcoded path
        if (!_module_loader->auto_detect_stdlib_root())
        {
            // Fallback to relative paths - try new_stdlib first, then stdlib for compatibility
            if (std::filesystem::exists("./new_stdlib"))
            {
                _module_loader->set_stdlib_root("./new_stdlib");
            }
            else
            {
                _module_loader->set_stdlib_root("./stdlib");
            }
        }
        _module_loader->set_current_file(_source_file);

        // Create a synthetic ImportDeclarationNode for prelude
        // This simulates: import <prelude>;
        auto prelude_import = std::make_unique<ImportDeclarationNode>(
            SourceLocation(0, 0), // synthetic location
            "prelude"             // module path
        );

        // Load the prelude import
        auto result = _module_loader->load_import(*prelude_import);

        if (result.success)
        {
            // Register the namespace and symbols (wildcard import behavior)
            if (!result.symbol_map.empty())
            {
                current_scope->register_namespace(result.module_name, result.symbol_map);
                LOG_DEBUG(Cryo::LogComponent::GENERAL, "Auto-imported prelude: registered namespace '{}' with {} symbols",
                          result.module_name, result.symbol_map.size());
                if (is_runtime_file)
                {
                    LOG_DEBUG(Cryo::LogComponent::GENERAL, "Runtime file successfully auto-imported prelude");
                }
            }
            else
            {
                LOG_WARN(Cryo::LogComponent::GENERAL, "Auto-import succeeded but no symbols found in prelude");
            }
        }
        else
        {
            // If prelude import fails, try fallback to core/types for compatibility
            LOG_WARN(Cryo::LogComponent::GENERAL, "Failed to auto-import prelude: {}, trying core/types fallback", result.error_message);

            auto core_types_import = std::make_unique<ImportDeclarationNode>(
                SourceLocation(0, 0), // synthetic location
                "core::types"         // module path
            );

            auto fallback_result = _module_loader->load_import(*core_types_import);
            if (fallback_result.success && !fallback_result.symbol_map.empty())
            {
                current_scope->register_namespace(fallback_result.module_name, fallback_result.symbol_map);
                LOG_DEBUG(Cryo::LogComponent::GENERAL, "Fallback auto-imported core/types: registered namespace '{}' with {} symbols",
                          fallback_result.module_name, fallback_result.symbol_map.size());
            }
            else if (is_runtime_file)
            {
                LOG_ERROR(Cryo::LogComponent::GENERAL, "CRITICAL: Runtime file failed to auto-import both prelude and core/types!");
            }
        }

        // Skip remaining auto-imports if we're in stdlib mode (except for runtime files which only get prelude)
        if (stdlib_mode_runtime_exception)
        {
            LOG_DEBUG(Cryo::LogComponent::GENERAL, "Runtime file in stdlib mode: only auto-imported prelude, skipping other imports");
            return;
        }

        // The prelude should have already imported all necessary modules (intrinsics, runtime, io, etc.)
        LOG_DEBUG(Cryo::LogComponent::GENERAL, "Prelude auto-injection complete. All required symbols should be available through prelude re-exports.");
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

        TypeMapper *type_mapper = visitor->get_type_mapper();
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

    void CompilerInstance::register_ast_nodes_recursive(ASTNode *node, TypeMapper *type_mapper)
    {
        if (!node || !type_mapper)
            return;

        // Register struct declarations
        if (auto struct_decl = dynamic_cast<StructDeclarationNode *>(node))
        {
            LOG_DEBUG(Cryo::LogComponent::GENERAL, "Registering struct AST node: {}", struct_decl->name());
            // TODO: Implement proper struct registration with AST node
            // type_mapper->register_struct_ast_node(struct_decl);
        }
        // Register class declarations
        else if (auto class_decl = dynamic_cast<ClassDeclarationNode *>(node))
        {
            LOG_DEBUG(Cryo::LogComponent::GENERAL, "Registering class AST node: {}", class_decl->name());
            // TODO: Implement proper class registration with AST node
            // type_mapper->register_class_ast_node(class_decl);
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

    bool CompilerInstance::compile_stdlib(const std::string &source_dir, const std::string &output_path)
    {
        LOG_INFO(LogComponent::GENERAL, "Starting stdlib compilation from directory: {}", source_dir);

        // Discover all .cryo files in the source directory recursively
        std::vector<std::string> source_files;

        try
        {
            for (const auto &entry : std::filesystem::recursive_directory_iterator(source_dir))
            {
                if (entry.is_regular_file() && entry.path().extension() == ".cryo")
                {
                    source_files.push_back(entry.path().string());
                }
            }
        }
        catch (const std::exception &e)
        {
            LOG_ERROR(LogComponent::GENERAL, "Error discovering source files: {}", e.what());
            return false;
        }

        if (source_files.empty())
        {
            LOG_ERROR(LogComponent::GENERAL, "No .cryo files found in source directory: {}", source_dir);
            return false;
        }

        LOG_INFO(LogComponent::GENERAL, "Found {} source files for stdlib compilation", source_files.size());

        // Sort files to ensure consistent compilation order
        std::sort(source_files.begin(), source_files.end());

        // Extract output directory from output_path
        std::string output_dir;
        try
        {
            std::filesystem::path path(output_path);
            output_dir = path.parent_path().string();
        }
        catch (const std::exception &e)
        {
            LOG_ERROR(LogComponent::GENERAL, "Failed to extract output directory from path '{}': {}", output_path, e.what());
            return false;
        }

        if (output_dir.empty())
        {
            LOG_ERROR(LogComponent::GENERAL, "Invalid output path '{}' - cannot determine output directory", output_path);
            return false;
        }

        // Ensure output directory exists
        try
        {
            std::filesystem::create_directories(output_dir);
        }
        catch (const std::exception &e)
        {
            LOG_ERROR(LogComponent::GENERAL, "Failed to create output directory '{}': {}", output_dir, e.what());
            return false;
        }

        LOG_INFO(LogComponent::GENERAL, "Output directory: {}", output_dir);

        // Compile each file individually and generate output files
        bool overall_success = true;
        std::vector<std::string> compiled_modules;
        std::vector<std::string> failed_modules;
        std::vector<std::string> generated_object_files;

        // Track bytes generated
        size_t total_ll_bytes = 0;
        size_t total_bc_bytes = 0;
        size_t total_obj_bytes = 0;

        for (const auto &source_file : source_files)
        {
            std::cout << "Compiling stdlib module: " << source_file << std::endl;
            LOG_INFO(LogComponent::GENERAL, "Compiling stdlib module: {}", source_file);

            // Reset state for each file while maintaining stdlib mode
            reset_state();
            set_stdlib_compilation_mode(true);

            // Generate module-specific output file names preserving directory structure
            std::filesystem::path source_path(source_file);
            std::string relative_path = std::filesystem::relative(source_path, source_dir).string();

            // Remove .cryo extension
            std::string module_path = relative_path;
            if (module_path.size() > 5 && module_path.substr(module_path.size() - 5) == ".cryo")
            {
                module_path = module_path.substr(0, module_path.size() - 5);
            }

            // Create full output paths preserving directory structure
            std::filesystem::path module_output_dir = std::filesystem::path(output_dir) / std::filesystem::path(module_path).parent_path();
            std::string module_name = std::filesystem::path(module_path).filename().string();

            // Ensure the module's output directory exists
            if (!module_output_dir.empty())
            {
                try
                {
                    std::filesystem::create_directories(module_output_dir);
                }
                catch (const std::exception &e)
                {
                    LOG_ERROR(LogComponent::GENERAL, "Failed to create module output directory '{}': {}", module_output_dir.string(), e.what());
                    overall_success = false;
                    continue;
                }
            }

            std::string bc_output = (module_output_dir / (module_name + ".bc")).string();
            std::string ll_output = (module_output_dir / (module_name + ".ll")).string();
            std::string obj_output = (module_output_dir / (module_name + ".o")).string();

            bool module_success = true;

            // Compile the module
            if (!compile_file(source_file))
            {
                LOG_ERROR(LogComponent::GENERAL, "Failed to compile stdlib module: {}", source_file);
                overall_success = false;
                module_success = false;
                failed_modules.push_back(source_file);
            }
            else
            {
                LOG_DEBUG(LogComponent::GENERAL, "Successfully compiled stdlib module: {}", source_file);
            }

            // Dump symbols for this module if enabled (works whether compilation succeeded or failed)
            // Always use module_output_dir to match where .ll and .o files are generated
            if (_dump_symbols && _symbol_table)
            {
                std::string symbol_output_dir = module_output_dir.string();

                if (SymbolDumper::dump_module_symbols(*_symbol_table, module_name, symbol_output_dir))
                {
                    std::cout << "  ✓ Generated symbol dump: " << symbol_output_dir << "/" << module_name << "-symbols.dbg.txt" << std::endl;
                    LOG_INFO(LogComponent::GENERAL, "✓ Generated symbol dump: {}/{}-symbols.dbg.txt", symbol_output_dir, module_name);
                }
                else
                {
                    std::cout << "  ⚠ Failed to dump symbols for module: " << module_name << std::endl;
                    LOG_WARN(LogComponent::GENERAL, "⚠ Failed to dump symbols for module: {}", module_name);
                }
            }

            // Generate LLVM IR for this module (even if compile_file failed, we might have partial IR)
            if (module_success && !generate_ir())
            {
                LOG_ERROR(LogComponent::GENERAL, "Failed to generate IR for stdlib module: {}", source_file);
                overall_success = false;
                module_success = false;
                // Only add if not already in failed list
                if (std::find(failed_modules.begin(), failed_modules.end(), source_file) == failed_modules.end())
                {
                    failed_modules.push_back(source_file);
                }
            }

            // Always try to emit LLVM IR file (for debugging purposes)
            if (_codegen)
            {
                LOG_DEBUG(LogComponent::GENERAL, "Emitting LLVM IR for module '{}' to: {}", module_name, bc_output);
                if (_codegen->emit_llvm_ir(bc_output))
                {
                    // Get file sizes for statistics
                    try
                    {
                        if (std::filesystem::exists(bc_output))
                        {
                            total_bc_bytes += std::filesystem::file_size(bc_output);
                        }
                        if (std::filesystem::exists(ll_output))
                        {
                            total_ll_bytes += std::filesystem::file_size(ll_output);
                        }
                    }
                    catch (const std::exception &e)
                    {
                        LOG_WARN(LogComponent::GENERAL, "Could not get file sizes: {}", e.what());
                    }
                    std::cout << "  ✓ Generated LLVM bitcode: " << bc_output << std::endl;
                    std::cout << "  ✓ Generated LLVM IR: " << ll_output << std::endl;
                    LOG_INFO(LogComponent::GENERAL, "✓ Generated LLVM IR: {}", ll_output);
                }
                else
                {
                    std::cout << "  ⚠ Failed to emit LLVM IR for module: " << module_name << std::endl;
                    LOG_WARN(LogComponent::GENERAL, "⚠ Failed to emit LLVM IR for module: {}", module_name);
                }
            }
            else
            {
                std::cout << "  ⚠ No codegen available for module: " << module_name << std::endl;
            }

            // Generate object file only if compilation was successful
            if (module_success && _codegen && _linker)
            {
                // Get the LLVM module from the code generator
                llvm::Module *module = _codegen->get_module();
                if (module)
                {
                    LOG_DEBUG(LogComponent::GENERAL, "Emitting object file for module '{}' to: {}", module_name, obj_output);
                    if (_linker->generate_object_file(module, obj_output))
                    {
                        // Get object file size for statistics
                        try
                        {
                            if (std::filesystem::exists(obj_output))
                            {
                                total_obj_bytes += std::filesystem::file_size(obj_output);
                            }
                        }
                        catch (const std::exception &e)
                        {
                            LOG_WARN(LogComponent::GENERAL, "Could not get object file size: {}", e.what());
                        }
                        std::cout << "  ✓ Generated object file: " << obj_output << std::endl;
                        LOG_INFO(LogComponent::GENERAL, "✓ Generated object file: {}", obj_output);
                        compiled_modules.push_back(source_file);
                        generated_object_files.push_back(obj_output);
                    }
                    else
                    {
                        std::cout << "  ✗ Failed to emit object file for module: " << module_name << std::endl;
                        // Get the linker's last error if available
                        std::cout << "    Linker error: " << _linker->get_last_error() << std::endl;
                        LOG_ERROR(LogComponent::GENERAL, "Failed to emit object file for module '{}': {}", module_name, _linker->get_last_error());
                        overall_success = false;
                    }
                }
                else
                {
                    std::cout << "  ✗ No LLVM module available for object file generation: " << module_name << std::endl;
                    LOG_ERROR(LogComponent::GENERAL, "No LLVM module available for object file generation: {}", module_name);
                }
            }
            else if (!module_success)
            {
                std::cout << "  ✗ Skipping object file generation due to compilation errors" << std::endl;
            }
            else if (!_linker)
            {
                std::cout << "  ✗ No linker available for object file generation" << std::endl;
            }
        }

        if (compiled_modules.empty())
        {
            std::cout << "✗ No stdlib modules compiled successfully" << std::endl;
            LOG_ERROR(LogComponent::GENERAL, "No stdlib modules compiled successfully");
            return false;
        }

        // Helper lambda to format bytes in human-readable form
        auto format_bytes = [](size_t bytes) -> std::string
        {
            if (bytes < 1024)
            {
                return std::to_string(bytes) + " B";
            }
            else if (bytes < 1024 * 1024)
            {
                return std::to_string(bytes / 1024) + "." + std::to_string((bytes % 1024) * 10 / 1024) + " KB";
            }
            else
            {
                return std::to_string(bytes / (1024 * 1024)) + "." + std::to_string((bytes % (1024 * 1024)) * 10 / (1024 * 1024)) + " MB";
            }
        };

        size_t total_bytes = total_ll_bytes + total_bc_bytes + total_obj_bytes;

        std::cout << "\n========================================" << std::endl;
        std::cout << "       Stdlib Compilation Summary       " << std::endl;
        std::cout << "========================================" << std::endl;
        std::cout << "  Modules:     " << compiled_modules.size() << "/" << source_files.size() << " compiled successfully" << std::endl;
        std::cout << "  Object files: " << generated_object_files.size() << " generated" << std::endl;
        std::cout << "----------------------------------------" << std::endl;
        std::cout << "  Bytes generated:" << std::endl;
        std::cout << "    LLVM IR (.ll):  " << format_bytes(total_ll_bytes) << std::endl;
        std::cout << "    Bitcode (.bc):  " << format_bytes(total_bc_bytes) << std::endl;
        std::cout << "    Objects (.o):   " << format_bytes(total_obj_bytes) << std::endl;
        std::cout << "    Total:          " << format_bytes(total_bytes) << std::endl;
        std::cout << "----------------------------------------" << std::endl;
        std::cout << "  Output directory: " << output_dir << std::endl;

        // Show failed modules if any
        if (!failed_modules.empty())
        {
            std::cout << "----------------------------------------" << std::endl;
            std::cout << "  Failed modules (" << failed_modules.size() << "):" << std::endl;
            for (const auto &failed : failed_modules)
            {
                // Show relative path for readability
                std::filesystem::path failed_path(failed);
                std::string relative = std::filesystem::relative(failed_path, source_dir).string();
                std::cout << "    X " << relative << std::endl;
            }
        }

        // Show completed modules (abbreviated if too many)
        std::cout << "----------------------------------------" << std::endl;
        if (compiled_modules.size() <= 10)
        {
            std::cout << "  Completed modules (" << compiled_modules.size() << "):" << std::endl;
            for (const auto &completed : compiled_modules)
            {
                std::filesystem::path completed_path(completed);
                std::string relative = std::filesystem::relative(completed_path, source_dir).string();
                std::cout << "     " << relative << std::endl;
            }
        }
        else
        {
            std::cout << "  Completed modules (" << compiled_modules.size() << "): [showing first 15]" << std::endl;
            for (size_t i = 0; i < 15 && i < compiled_modules.size(); ++i)
            {
                std::filesystem::path completed_path(compiled_modules[i]);
                std::string relative = std::filesystem::relative(completed_path, source_dir).string();
                std::cout << "     " << relative << std::endl;
            }
            std::cout << "    ... and " << (compiled_modules.size() - 15) << " more" << std::endl;
        }
        std::cout << "========================================\n"
                  << std::endl;

        LOG_INFO(LogComponent::GENERAL, "Successfully compiled {}/{} stdlib modules",
                 compiled_modules.size(), source_files.size());
        LOG_INFO(LogComponent::GENERAL, "Generated {} object files ({} total)",
                 generated_object_files.size(), format_bytes(total_bytes));

        // TODO: Combine object files into final library (.a file) if desired
        // For now, individual object files are sufficient for testing

        return overall_success;
    }

    // ============================================================================
    // Pass Manager Integration
    // ============================================================================

    void CompilerInstance::initialize_pass_manager()
    {
        // Create the standard pass pipeline
        _pass_manager = StandardPassFactory::create_standard_pipeline(*this);

        if (_debug_mode)
        {
            _pass_manager->set_verbose(true);
        }

        // Validate the pipeline
        if (!_pass_manager->validate())
        {
            LOG_ERROR(LogComponent::GENERAL, "Failed to validate pass pipeline");
        }
        else
        {
            LOG_DEBUG(LogComponent::GENERAL, "Pass pipeline validated with {} passes",
                      _pass_manager->passes().size());
        }
    }

    void CompilerInstance::initialize_pass_context()
    {
        _pass_context = std::make_unique<PassContext>(*this);

        // Set context flags from compiler instance
        _pass_context->set_stdlib_mode(_stdlib_compilation_mode);
        _pass_context->set_debug_mode(_debug_mode);
        _pass_context->set_auto_imports_enabled(_auto_imports_enabled);
        _pass_context->set_current_namespace(_current_namespace);
        _pass_context->set_source_file(_source_file);

        // Set AST root if available
        if (_ast_root)
        {
            _pass_context->set_ast_root(_ast_root.get());
        }
    }

    // ============================================================================
    // Pass Implementation Methods
    // ============================================================================

    bool CompilerInstance::load_source_file(const std::string &source_file)
    {
        set_source_file(source_file);

        // Create a file object and load it
        _loaded_file = make_file_from_path(source_file);
        if (!_loaded_file)
        {
            _diagnostics->emit(Diag::error(ErrorCode::E0800_FILE_NOT_FOUND,
                                           "file not found: " + source_file));
            return false;
        }

        if (!_loaded_file->load())
        {
            _diagnostics->emit(Diag::error(ErrorCode::E0801_FILE_READ_ERROR,
                                           "failed to read file: " + source_file));
            return false;
        }

        // Store file info for later phases
        _loaded_file_path = _loaded_file->path().empty() ? _loaded_file->name() : _loaded_file->path();
        _loaded_file_content = std::string(_loaded_file->content());

        // Add source for diagnostic rendering
        _diagnostics->add_source_file(_loaded_file_path);

        // Reset state for new compilation
        reset_state();

        return true;
    }

    bool CompilerInstance::run_lexing_phase()
    {
        if (!_loaded_file)
        {
            _diagnostics->emit(Diag::error(ErrorCode::E0900_INTERNAL_COMPILER_ERROR,
                                           "No source file loaded - call load_source_file first"));
            return false;
        }

        try
        {
            _lexer = std::make_unique<Lexer>(std::move(_loaded_file));
            LOG_DEBUG(LogComponent::GENERAL, "Lexing phase: Lexer created successfully");
            return true;
        }
        catch (const std::exception &e)
        {
            _diagnostics->emit(Diag::error(ErrorCode::E0101_UNEXPECTED_TOKEN,
                                           std::string("Lexer error: ") + e.what()));
            return false;
        }
    }

    bool CompilerInstance::run_parsing_phase()
    {
        if (!_lexer)
        {
            _diagnostics->emit(Diag::error(ErrorCode::E0900_INTERNAL_COMPILER_ERROR,
                                           "Lexer not initialized - run lexing phase first"));
            return false;
        }

        try
        {
            // Create parser with lexer and AST context
            _parser = std::make_unique<Parser>(std::move(_lexer), *_ast_context, _diagnostics.get(), _loaded_file_path);
            _parser->set_directive_registry(_directive_registry.get());

            // Parse the program
            _ast_root = _parser->parse_program();

            if (!_ast_root)
            {
                _diagnostics->emit(Diag::error(ErrorCode::E0116_PARSE_EXCEPTION,
                                               "Failed to create AST root"));
                return false;
            }

            // Capture namespace context from parser after successful parsing
            if (!_parser->current_namespace().empty() && _parser->current_namespace() != "Global")
            {
                set_namespace_context(_parser->current_namespace());
                LOG_DEBUG(LogComponent::GENERAL, "Parsed namespace: {}", _parser->current_namespace());
            }

            // Create CodeGenerator now that we have the namespace information
            std::string namespace_for_module = _current_namespace.empty() ? "cryo_program" : _current_namespace;
            _codegen = Cryo::Codegen::create_default_codegen(*_ast_context, *_symbol_table, namespace_for_module, _diagnostics.get());

            // Set source info immediately after creating CodeGenerator
            _codegen->set_source_info(_source_file, _current_namespace);

            // Configure stdlib compilation mode if enabled
            if (_stdlib_compilation_mode)
            {
                _codegen->set_stdlib_compilation_mode(true);
                LOG_DEBUG(LogComponent::GENERAL, "Enabled stdlib compilation mode in CodeGenerator");
            }

            // Set TemplateRegistry for cross-module type resolution
            if (_codegen->ensure_visitor_initialized() && _template_registry)
            {
                _codegen->get_visitor()->context().set_template_registry(_template_registry.get());
                LOG_DEBUG(LogComponent::GENERAL, "Set TemplateRegistry in CodegenContext for cross-module resolution");
            }

            LOG_DEBUG(LogComponent::GENERAL, "Parsing phase: AST created with module name '{}'", namespace_for_module);

            // Register struct/class AST nodes with TypeMapper immediately after CodeGenerator creation
            register_ast_nodes_with_typemapper();

            return true;
        }
        catch (const std::exception &e)
        {
            _diagnostics->emit(Diag::error(ErrorCode::E0116_PARSE_EXCEPTION,
                                           std::string("Parse exception: ") + e.what()));
            return false;
        }
    }

    void CompilerInstance::run_auto_import_phase()
    {
        if (!_stdlib_compilation_mode && _auto_imports_enabled)
        {
            LOG_DEBUG(LogComponent::GENERAL, "Auto-import phase: Injecting auto-imports");
            inject_auto_imports(_symbol_table.get(), "Global");
        }
        else
        {
            LOG_DEBUG(LogComponent::GENERAL, "Auto-import phase: Skipped (stdlib mode or disabled)");
        }
    }

    void CompilerInstance::run_declaration_collection_phase()
    {
        if (!_ast_root)
        {
            LOG_ERROR(LogComponent::GENERAL, "Declaration collection: AST root is null");
            return;
        }

        LOG_DEBUG(LogComponent::GENERAL, "Declaration collection phase: Collecting declarations");
        collect_declarations_pass(_ast_root.get(), _symbol_table.get(), "Global");
    }

    bool CompilerInstance::run_directive_processing_phase()
    {
        LOG_DEBUG(LogComponent::GENERAL, "Directive processing phase: Processing directives");
        return process_directives();
    }

    bool CompilerInstance::run_function_body_phase()
    {
        if (!_ast_root)
        {
            _diagnostics->emit(Diag::error(ErrorCode::E0900_INTERNAL_COMPILER_ERROR,
                                           "AST root is null for function body processing"));
            return false;
        }

        // Track error count before processing to detect new errors (not accumulated from previous modules)
        size_t errors_before = _diagnostics ? _diagnostics->error_count() : 0;

        LOG_DEBUG(LogComponent::GENERAL, "Function body phase: Processing function bodies and references");
        populate_symbol_table_with_scope(_ast_root.get(), _symbol_table.get(), "Global");

        // Check for NEW type errors from this module only (not accumulated errors from previous modules)
        size_t errors_after = _diagnostics ? _diagnostics->error_count() : 0;
        if (errors_after > errors_before)
        {
            LOG_ERROR(LogComponent::GENERAL, "Function body phase: Type checking failed with {} new errors",
                      errors_after - errors_before);
            return false;
        }

        // Validate directive effects
        LOG_DEBUG(LogComponent::GENERAL, "Function body phase: Validating directive effects");
        if (!validate_directive_effects())
        {
            return false;
        }

        return true;
    }

    void CompilerInstance::run_type_lowering_phase()
    {
        if (!_codegen)
        {
            LOG_WARN(LogComponent::GENERAL, "Type lowering phase: CodeGenerator not available");
            return;
        }

        if (_codegen->ensure_visitor_initialized())
        {
            LOG_DEBUG(LogComponent::GENERAL, "Type lowering phase: Processing struct declarations");

            // Enable pre-registration mode to skip method body generation
            _codegen->get_visitor()->set_pre_registration_mode(true);

            // Process struct declarations to ensure TypeMapper has correct type information
            process_struct_declarations_for_preregistration(_ast_root.get());

            // Disable pre-registration mode
            _codegen->get_visitor()->set_pre_registration_mode(false);
        }
        else
        {
            LOG_WARN(LogComponent::GENERAL, "Type lowering phase: Failed to initialize CodegenVisitor");
        }
    }

    void CompilerInstance::run_function_declaration_phase()
    {
        if (!_codegen)
        {
            LOG_WARN(LogComponent::GENERAL, "Function declaration phase: CodeGenerator not available");
            return;
        }

        if (_codegen->ensure_visitor_initialized())
        {
            LOG_DEBUG(LogComponent::GENERAL, "Function declaration phase: Pre-registering functions in LLVM module");
            _codegen->get_visitor()->pre_register_functions_from_symbol_table();
        }
        else
        {
            LOG_WARN(LogComponent::GENERAL, "Function declaration phase: Failed to initialize CodegenVisitor");
        }
    }

    bool CompilerInstance::run_ir_generation_phase()
    {
        if (!_ast_root)
        {
            _diagnostics->emit(Diag::error(ErrorCode::E0600_CODEGEN_FAILED,
                                           "AST root is null for IR generation"));
            return false;
        }

        if (!_codegen)
        {
            _diagnostics->emit(Diag::error(ErrorCode::E0600_CODEGEN_FAILED,
                                           "CodeGenerator not initialized"));
            return false;
        }

        try
        {
            // Set source file and namespace context before generating IR
            LOG_DEBUG(LogComponent::GENERAL, "IR generation phase: Setting source info");
            _codegen->set_source_info(_source_file, _current_namespace);

            // Pass imported ASTs to CodegenVisitor for dynamic enum variant extraction
            if (_codegen->get_visitor() && _module_loader)
            {
                LOG_DEBUG(LogComponent::GENERAL, "IR generation phase: Setting imported ASTs");
                _codegen->get_visitor()->set_imported_asts(&_module_loader->get_imported_asts());
            }

            LOG_DEBUG(LogComponent::GENERAL, "IR generation phase: Generating LLVM IR");
            bool success = _codegen->generate_ir(_ast_root.get());

            if (!success)
            {
                LOG_ERROR(LogComponent::GENERAL, "IR generation phase: Failed with error: {}",
                          _codegen->get_last_error());
                _diagnostics->emit(Diag::error(ErrorCode::E0600_CODEGEN_FAILED,
                                               "IR generation failed: " + _codegen->get_last_error()));
            }

            return success;
        }
        catch (const std::exception &e)
        {
            _diagnostics->emit(Diag::error(ErrorCode::E0600_CODEGEN_FAILED,
                                           std::string("IR generation exception: ") + e.what()));
            return false;
        }
    }

    void CompilerInstance::dump_pass_order(std::ostream &os) const
    {
        if (_pass_manager)
        {
            _pass_manager->dump_pass_order(os);
        }
        else
        {
            os << "Pass manager not initialized\n";
        }
    }

    std::unique_ptr<CompilerInstance> create_compiler_instance()
    {
        return std::make_unique<CompilerInstance>();
    }
}
