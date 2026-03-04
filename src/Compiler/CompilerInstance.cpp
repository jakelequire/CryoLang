#include "Compiler/CompilerInstance.hpp"
#include "Compiler/StandardPasses.hpp"
#include "Diagnostics/Diag.hpp"
#include "Types/TypeMapper.hpp"
#include "Types/GenericTypes.hpp"
#include "Types/UserDefinedTypes.hpp"
#include "Codegen/CodegenVisitor.hpp"
#include "AST/DirectiveProcessors.hpp"
#include "AST/DirectiveWalker.hpp"
#include "Utils/File.hpp"
#include "Utils/Logger.hpp"
#include "Utils/SymbolResolutionManager.hpp"
#include "Utils/SymbolDumper.hpp"
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Module.h>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <queue>
#include <unordered_set>

namespace Cryo
{
    CompilerInstance::CompilerInstance()
        : _debug_mode(false), _show_ast_before_ir(false), _stdlib_linking_enabled(true), _stdlib_compilation_mode(false), _auto_imports_enabled(true), _lsp_mode(false), _frontend_only(false), _raw_mode(false), _dump_symbols(false), _dump_symbols_output_dir(""), _current_namespace("")
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

        // 3. SymbolTable - use ASTContext's symbol table to ensure Parser and CompilerInstance
        // share the same symbol table and module context
        _symbol_table = &_ast_context->symbols();

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

        // Create AST specializer and register callback with Monomorphizer
        _ast_specializer = std::make_unique<ASTSpecializer>(*_ast_context, _ast_context->types());

        // Capture specializer by raw pointer for the callback (it outlives the callback)
        ASTSpecializer *specializer = _ast_specializer.get();
        _monomorphization_pass->set_ast_specializer(
            [specializer](const GenericTemplate &tmpl,
                          const TypeSubstitution &subst,
                          const std::string &specialized_name) -> ASTNode *
            {
                return specializer->specialize(tmpl, subst, specialized_name);
            });

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
            
            // Import intrinsics for malloc, free, etc.
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
            _parser->set_raw_mode(_raw_mode);

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

            // In frontend-only mode (LSP), skip semantic analysis and IR generation.
            // The LSP only needs the parsed AST for hover/tokens/diagnostics.
            // Running analyze() on incomplete or raw-mode code can corrupt AST nodes.
            if (_frontend_only)
            {
                if (_debug_mode)
                {
                    LOG_DEBUG(Cryo::LogComponent::GENERAL, "Frontend-only mode: skipping analyze() and generate_ir()");
                }
                return true;
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

            // In frontend-only mode (LSP), we only need the parsed AST.
            // Skip CodeGenerator creation, visitor initialization, and TypeMapper registration
            // which can corrupt AST nodes when run on raw-mode / incomplete code.
            if (_frontend_only)
            {
                return _ast_root != nullptr;
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

            // Set Monomorphizer for access to specialized ASTs during codegen
            if (_codegen->ensure_visitor_initialized() && _monomorphization_pass)
            {
                _codegen->get_visitor()->context().set_monomorphizer(_monomorphization_pass.get());
                LOG_DEBUG(Cryo::LogComponent::GENERAL, "Set Monomorphizer in CodegenContext for specialized AST access");
            }

            // Set GenericRegistry for generic type resolution during codegen
            if (_codegen->ensure_visitor_initialized() && _generic_registry)
            {
                _codegen->get_visitor()->context().set_generic_registry(_generic_registry.get());
                LOG_DEBUG(Cryo::LogComponent::GENERAL, "Set GenericRegistry in CodegenContext for generic type resolution");
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

                    // Sync namespace aliases (e.g., U -> Utils from "import Utils as U")
                    const auto &aliases = compiler_srm_ctx->get_namespace_aliases();
                    for (const auto &[alias, target] : aliases)
                    {
                        codegen_srm_ctx->register_namespace_alias(alias, target);
                        LOG_DEBUG(Cryo::LogComponent::GENERAL, "Synced namespace alias to codegen SRM: '{}' -> '{}'", alias, target);
                    }
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
                std::string runtime_path;
                std::vector<std::string> runtime_candidates = {
                    bin_dir + "/stdlib/runtime.o",
                    Cryo::ModuleLoader::find_stdlib_directory() + "/.bin/runtime.o",
                };
                for (const auto &candidate : runtime_candidates)
                {
                    if (!candidate.empty() && std::filesystem::exists(candidate))
                    {
                        runtime_path = candidate;
                        break;
                    }
                }

                if (!runtime_path.empty())
                {
                    _linker->add_object_file(runtime_path);
                    if (_debug_mode)
                    {
                        LOG_DEBUG(Cryo::LogComponent::GENERAL, "Added runtime.o for runtime linking: {}", runtime_path);
                    }
                }

                // Add libcryo.a to the linker
                // Check multiple possible locations for the pre-built stdlib archive
                std::string libcryo_path;
                std::vector<std::string> libcryo_candidates = {
                    bin_dir + "/stdlib/libcryo.a",
                    Cryo::ModuleLoader::find_stdlib_directory() + "/.bin/libcryo.a",
                };
                for (const auto &candidate : libcryo_candidates)
                {
                    if (!candidate.empty() && std::filesystem::exists(candidate))
                    {
                        libcryo_path = candidate;
                        break;
                    }
                }

                if (!libcryo_path.empty())
                {
                    _linker->add_object_file(libcryo_path);
                    if (_debug_mode)
                    {
                        LOG_DEBUG(Cryo::LogComponent::GENERAL, "Added libcryo.a for standard library linking: {}", libcryo_path);
                    }
                }
                else
                {
                    LOG_WARN(Cryo::LogComponent::GENERAL, "libcryo.a not found, stdlib functions may not link properly. Searched: {}", libcryo_candidates[0]);
                }
            }
            else if (_debug_mode)
            {
                LOG_DEBUG(Cryo::LogComponent::GENERAL, "Standard library linking disabled by --no-std flag");
            }

            // Compile discovered C source files (from CImport blocks) and add to linker
            for (const auto &c_file : _c_source_files)
            {
                // Place the .o next to the .c file to avoid collisions between parallel builds
                std::filesystem::path c_path(c_file);
                std::string obj_path = (c_path.parent_path() / (c_path.stem().string() + ".o")).generic_string();
                std::string c_file_normalized = std::filesystem::path(c_file).generic_string();
#if defined(_WIN32) || defined(_WIN64)
                std::string cmd = "C:/msys64/mingw64/bin/clang -c \"" + c_file_normalized + "\" -o \"" + obj_path + "\" 2>&1";
#else
                std::string cmd = "clang-20 -c \"" + c_file_normalized + "\" -o \"" + obj_path + "\" 2>&1";
#endif
                int ret = system(cmd.c_str());
                if (ret == 0 && std::filesystem::exists(obj_path))
                {
                    _linker->add_object_file(obj_path);
                    LOG_DEBUG(Cryo::LogComponent::GENERAL, "Compiled and added C source for linking: {} -> {}", c_file, obj_path);
                }
                else
                {
                    LOG_WARN(Cryo::LogComponent::GENERAL, "Failed to compile C source file: {}", c_file);
                }
            }

            // Add extra object files from --link-object CLI flag
            for (const auto &obj_file : _extra_object_files)
            {
                if (std::filesystem::exists(obj_file))
                {
                    _linker->add_object_file(obj_file);
                    LOG_DEBUG(Cryo::LogComponent::GENERAL, "Added extra object file for linking: {}", obj_file);
                }
                else
                {
                    LOG_WARN(Cryo::LogComponent::GENERAL, "Extra object file not found: {}", obj_file);
                }
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

    bool CompilerInstance::compile_for_lsp_from_content(const std::string &virtual_path, const std::string &content)
    {
        try
        {
            _lsp_mode = true;
            _frontend_only = true;

            if (_debug_mode)
            {
                LOG_DEBUG(Cryo::LogComponent::LSP, "Starting LSP compilation from content for: {}", virtual_path);
            }

            set_source_file(virtual_path);

            auto file = make_file_from_string(virtual_path, content);
            if (!file)
            {
                _diagnostics->emit(Diag::error(ErrorCode::E0000_UNKNOWN, "LSP: Failed to create in-memory file"));
                return false;
            }

            if (!parse_source_from_file(std::move(file)))
            {
                if (_debug_mode)
                {
                    LOG_ERROR(Cryo::LogComponent::LSP, "LSP: Parse failed for {}", virtual_path);
                }
                // Don't return false - provide partial information even with parse errors
            }

            // Note: analyze() is intentionally skipped here.
            // The LSP only needs the parsed AST for hover/tokens/diagnostics.
            // Semantic analysis is crash-prone on incomplete code and runs separately
            // if needed via the public analyze() method.

            if (_debug_mode)
            {
                LOG_DEBUG(Cryo::LogComponent::LSP, "LSP content compilation completed for: {}", virtual_path);
            }
            return true;
        }
        catch (const std::exception &e)
        {
            if (_debug_mode)
            {
                LOG_ERROR(Cryo::LogComponent::LSP, "LSP content compilation exception: {}", e.what());
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

        // In stdlib compilation mode, preserve the AST instead of destroying it.
        // Templates registered in GenericRegistry have ast_node pointers that
        // reference nodes in the AST. If we destroy the AST, those pointers become
        // dangling, causing crashes when accessing them later (e.g., during type alias resolution).
        if (_stdlib_compilation_mode && _ast_root)
        {
            _compiled_asts.push_back(std::move(_ast_root));

            // Also preserve the source file backing Token string_views in the AST.
            // Token._text is std::string_view into File._content. Destroying the
            // Parser/Lexer/File would free the content and leave dangling string_views
            // in preserved AST nodes (BinaryExpressionNode._operator, etc.).
            if (_parser)
            {
                auto file = _parser->take_lexer_file();
                if (file)
                {
                    _preserved_source_files.push_back(std::move(file));
                }
            }

            LOG_DEBUG(LogComponent::GENERAL, "CompilerInstance: Preserved AST for stdlib mode (total: {} ASTs)",
                      _compiled_asts.size());
        }
        else
        {
            _ast_root.reset();
        }

        // Clear per-module generic instantiation state to prevent specializations
        // from earlier modules leaking into later modules. Template definitions
        // persist (they're needed cross-module), but instantiation caches are per-module.
        if (_generic_registry)
        {
            _generic_registry->clear_module_state();
        }
        if (_monomorphization_pass)
        {
            _monomorphization_pass->clear_module_state();
        }

        // Don't clear diagnostics during stdlib compilation - we want to accumulate
        // errors from all modules so they can be reported together at the end
        if (_diagnostics && !_stdlib_compilation_mode)
        {
            _diagnostics->clear();
        }
        _lexer.reset();
        _parser.reset();
        _local_import_modules.clear();

        // Note: TypeChecker doesn't have reset_state or set_stdlib_compilation_mode.
        // TypeChecker is stateless - it just provides type-checking utilities.
        // State is managed by TypeArena, SymbolTable, and DiagEmitter.
    }

    void CompilerInstance::populate_symbol_table(ASTNode *node)
    {
        // Inject auto-imports before processing user imports
        inject_auto_imports(_symbol_table, "Global");

        // Three-phase approach for proper forward reference resolution:
        // Phase 1a: Collect all declarations - registers type NAMES (functions, structs, enums, traits)
        LOG_DEBUG(Cryo::LogComponent::GENERAL, "Phase 1a: Collecting declarations (type names)...");
        collect_declarations_pass(node, _symbol_table, "Global");

        // Phase 1b: Populate struct/class fields - now that all type names are registered,
        // we can safely resolve field types like Option<ChildStdin> even when ChildStdin
        // is defined later in the file
        LOG_DEBUG(Cryo::LogComponent::GENERAL, "Phase 1b: Populating struct/class field types...");
        populate_type_fields_pass(node);

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

                // Declare functions from previously compiled modules (cross-module resolution)
                if (!_cross_module_functions.empty())
                {
                    declare_cross_module_functions();
                }
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
        populate_symbol_table_with_scope(node, _symbol_table, "Global");
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
            TypeRef function_type = _ast_context->types().get_function(return_type, param_types, func_decl->is_variadic());

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
                    _current_namespace.empty() ? "Main" : _current_namespace,
                    "" // No source file path for main file templates
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
        // Handle struct declarations - Phase 1: Register type NAME only
        // Field types are populated in populate_type_fields_pass after all types are registered
        else if (auto struct_decl = dynamic_cast<StructDeclarationNode *>(node))
        {
            // Check if type was already pre-registered during parsing (for self-referential types)
            TypeRef struct_type = _symbol_table->lookup_struct_type(struct_decl->name());
            if (!struct_type.is_valid())
            {
                // Also check if the type exists in the TypeArena (might have been registered with a different module)
                // This prevents creating duplicate types with different module IDs
                struct_type = _ast_context->types().lookup_type_by_name(struct_decl->name());
            }
            if (!struct_type.is_valid())
            {
                // Type wasn't pre-registered, create and register it now
                ModuleID current_module = _symbol_table->current_module();
                struct_type = _ast_context->types().create_struct(QualifiedTypeName{current_module, struct_decl->name()});
                _symbol_table->declare_type(struct_decl->name(), struct_type, struct_decl->location());
            }

            // NOTE: Field population is deferred to populate_type_fields_pass
            // This ensures all type names are registered before resolving field types,
            // preventing forward reference failures like Option<ChildStdin> when ChildStdin
            // is defined later in the file.

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
                    _current_namespace.empty() ? "Main" : _current_namespace,
                    "" // No source file path for main file templates
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
        // Handle class declarations - Phase 1: Register type NAME only
        // Field types are populated in populate_type_fields_pass after all types are registered
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

            // NOTE: Field population is deferred to populate_type_fields_pass
            // This ensures all type names are registered before resolving field types.

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
                    _current_namespace.empty() ? "Main" : _current_namespace,
                    "" // No source file path for main file templates
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
                        // Create integer constant for this variant
                        llvm::Constant *variant_const = llvm::ConstantInt::get(
                            llvm::Type::getInt32Ty(visitor->context().llvm_context()),
                            static_cast<uint64_t>(i));

                        // Build unqualified name (EnumName::Variant)
                        std::string unqualified_name = enum_decl->name() + "::" + variant_names[i];

                        // Always register with unqualified name for same-module lookups
                        enum_variants_map[unqualified_name] = variant_const;
                        LOG_DEBUG(Cryo::LogComponent::GENERAL, "Pre-registered enum variant: {} = {}", unqualified_name, i);

                        // Also register with fully qualified name for cross-module lookups
                        if (!_current_namespace.empty())
                        {
                            std::string fully_qualified_name = _current_namespace + "::" + unqualified_name;
                            enum_variants_map[fully_qualified_name] = variant_const;
                            LOG_DEBUG(Cryo::LogComponent::GENERAL, "Pre-registered enum variant (qualified): {} = {}", fully_qualified_name, i);
                        }
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
                    _current_namespace.empty() ? "Main" : _current_namespace,
                    "" // No source file path for main file templates
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
                    _current_namespace.empty() ? "Main" : _current_namespace,
                    "" // No source file path for main file templates
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
                    TypeRef{} // Target will be resolved during instantiation
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
                // Track local project imports for IR generation
                if (result.is_local_import)
                {
                    LOG_DEBUG(Cryo::LogComponent::GENERAL, "Tracking local import '{}' for IR generation", result.module_name);
                    _local_import_modules.push_back(result.module_name);
                }

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

                        // Register enum variants from namespace alias imports
                        if (_codegen && _codegen->ensure_visitor_initialized())
                        {
                            auto *visitor = _codegen->get_visitor();
                            auto &enum_variants_map = visitor->context().enum_variants_map();

                            for (const auto &[symbol_name, symbol] : result.symbol_map)
                            {
                                // Check if this is an enum type
                                if (symbol.kind == SymbolKind::Type && symbol.type.is_valid() &&
                                    symbol.type->kind() == TypeKind::Enum)
                                {
                                    auto *enum_type = static_cast<const EnumType *>(symbol.type.get());
                                    if (enum_type->is_complete())
                                    {
                                        for (const auto &variant : enum_type->variants())
                                        {
                                            // Create integer constant for this variant
                                            llvm::Constant *variant_const = llvm::ConstantInt::get(
                                                llvm::Type::getInt32Ty(visitor->context().llvm_context()),
                                                static_cast<uint64_t>(variant.tag_value));

                                            // Register with unqualified name (EnumName::Variant)
                                            std::string unqualified_name = symbol_name + "::" + variant.name;
                                            enum_variants_map[unqualified_name] = variant_const;

                                            // Register with namespace alias (alias::EnumName::Variant)
                                            std::string aliased_name = result.namespace_alias + "::" + unqualified_name;
                                            enum_variants_map[aliased_name] = variant_const;

                                            LOG_DEBUG(Cryo::LogComponent::GENERAL,
                                                      "Registered namespace alias enum variant: {} (also as {})",
                                                      unqualified_name, aliased_name);
                                        }
                                    }
                                }
                            }
                        }
                    }
                    else
                    {
                        // Multiple specific imports - add symbols directly to current scope
                        LOG_DEBUG(Cryo::LogComponent::GENERAL, "Processing specific symbol imports with {} symbols", result.symbol_map.size());

                        for (const auto &[symbol_name, symbol] : result.symbol_map)
                        {
                            // Use the symbol's original scope (the defining module's namespace, e.g., "Baz::Qix")
                            // instead of the importing file's scope ("Global") so that LLVM name generation
                            // produces correct qualified names like "Baz::Qix::Buz::new".
                            std::string sym_scope = symbol.scope.empty() ? scope_name : symbol.scope;
                            _symbol_table->declare_symbol(symbol_name, symbol.kind, symbol.location, symbol.type, sym_scope);
                            LOG_TRACE(Cryo::LogComponent::GENERAL, "Registered specific symbol: {}", symbol_name);
                        }

                        // Register enum variants from specific imports for cross-module enum variant resolution
                        if (_codegen && _codegen->ensure_visitor_initialized())
                        {
                            auto *visitor = _codegen->get_visitor();
                            auto &enum_variants_map = visitor->context().enum_variants_map();

                            for (const auto &[symbol_name, symbol] : result.symbol_map)
                            {
                                // Check if this is an enum type
                                if (symbol.kind == SymbolKind::Type && symbol.type.is_valid() &&
                                    symbol.type->kind() == TypeKind::Enum)
                                {
                                    auto *enum_type = static_cast<const EnumType *>(symbol.type.get());
                                    if (enum_type->is_complete())
                                    {
                                        for (const auto &variant : enum_type->variants())
                                        {
                                            // Create integer constant for this variant
                                            llvm::Constant *variant_const = llvm::ConstantInt::get(
                                                llvm::Type::getInt32Ty(visitor->context().llvm_context()),
                                                static_cast<uint64_t>(variant.tag_value));

                                            // Register with unqualified name (EnumName::Variant)
                                            std::string unqualified_name = symbol_name + "::" + variant.name;
                                            enum_variants_map[unqualified_name] = variant_const;

                                            // Also register with module name for qualified lookups
                                            std::string qualified_name = result.module_name + "::" + unqualified_name;
                                            enum_variants_map[qualified_name] = variant_const;

                                            LOG_DEBUG(Cryo::LogComponent::GENERAL,
                                                      "Registered specific imported enum variant: {} (also as {})",
                                                      unqualified_name, qualified_name);
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                else if (result.resolved_as_specific)
                {
                    // Path resolved as specific symbol import (e.g., "import Baz::Qix::Buz")
                    // Register each symbol directly in current scope (like specific imports)
                    LOG_DEBUG(Cryo::LogComponent::GENERAL,
                              "Processing resolved-as-specific import with {} symbols", result.symbol_map.size());

                    for (const auto &[symbol_name, symbol] : result.symbol_map)
                    {
                        // Use the symbol's original scope (the defining module's namespace, e.g., "Baz::Qix")
                        // instead of the importing file's scope ("Global") so that LLVM name generation
                        // produces correct qualified names like "Baz::Qix::Buz::new".
                        std::string sym_scope = symbol.scope.empty() ? scope_name : symbol.scope;
                        _symbol_table->declare_symbol(symbol_name, symbol.kind, symbol.location, symbol.type, sym_scope);
                        LOG_TRACE(Cryo::LogComponent::GENERAL, "Registered resolved-as-specific symbol: {}", symbol_name);
                    }
                }
                else
                {
                    // For wildcard imports, register the namespace and symbols.
                    // Use the import path (e.g., "Foo") rather than the file's full namespace
                    // (e.g., "Main::Foo") so symbols are accessible as Foo::X from the caller.
                    std::string namespace_name = import_decl->has_alias() ? import_decl->alias() : import_decl->path();

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

                            // Register namespace alias so SRM can resolve U::add → Utils::add
                            if (import_decl->has_alias())
                            {
                                _symbol_resolution_manager->get_context()->register_namespace_alias(
                                    import_decl->alias(), import_decl->path());
                                LOG_DEBUG(Cryo::LogComponent::GENERAL,
                                          "Registered namespace alias '{}' -> '{}' in SRM",
                                          import_decl->alias(), import_decl->path());
                            }
                        }

                        // Note: TypeChecker doesn't have get_srm_context - SRM synchronization not available

                        // Also register symbols directly in current scope for unqualified access (e.g., println)
                        // This enables wildcard imports to work with direct function calls
                        for (const auto &[symbol_name, symbol] : result.symbol_map)
                        {
                            // Register each symbol directly in the current scope for unqualified access
                            // Use the symbol's original scope (the defining module) rather than the
                            // importing file's scope ("Global"), so pre_register_functions generates
                            // correct LLVM names (e.g., "Handler::new" instead of "Global::Handler::new").
                            std::string sym_scope = symbol.scope.empty() ? scope_name : symbol.scope;
                            _symbol_table->declare_symbol(symbol_name, symbol.kind, symbol.location, symbol.type, sym_scope);
                            LOG_TRACE(Cryo::LogComponent::GENERAL, "Registered wildcard symbol for unqualified access: {}", symbol_name);
                        }

                        // Register enum variants from imported enums for cross-module enum variant resolution
                        if (_codegen && _codegen->ensure_visitor_initialized())
                        {
                            auto *visitor = _codegen->get_visitor();
                            auto &enum_variants_map = visitor->context().enum_variants_map();

                            for (const auto &[symbol_name, symbol] : result.symbol_map)
                            {
                                // Check if this is an enum type
                                if (symbol.kind == SymbolKind::Type && symbol.type.is_valid() &&
                                    symbol.type->kind() == TypeKind::Enum)
                                {
                                    auto *enum_type = static_cast<const EnumType *>(symbol.type.get());
                                    if (enum_type->is_complete())
                                    {
                                        for (const auto &variant : enum_type->variants())
                                        {
                                            // Create integer constant for this variant
                                            llvm::Constant *variant_const = llvm::ConstantInt::get(
                                                llvm::Type::getInt32Ty(visitor->context().llvm_context()),
                                                static_cast<uint64_t>(variant.tag_value));

                                            // Register with unqualified name (EnumName::Variant)
                                            std::string unqualified_name = symbol_name + "::" + variant.name;
                                            enum_variants_map[unqualified_name] = variant_const;

                                            // Register with namespace-qualified name (namespace::EnumName::Variant)
                                            std::string qualified_name = namespace_name + "::" + unqualified_name;
                                            enum_variants_map[qualified_name] = variant_const;

                                            LOG_DEBUG(Cryo::LogComponent::GENERAL,
                                                      "Registered imported enum variant: {} (also as {})",
                                                      unqualified_name, qualified_name);
                                        }
                                    }
                                }
                            }
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
        // Handle implementation blocks to register enum impl blocks for monomorphization
        else if (auto impl_block = dynamic_cast<ImplementationBlockNode *>(node))
        {
            std::string type_name = impl_block->target_type();
            // Extract base type name (remove generics like "Option<T>" -> "Option")
            std::string base_type_name = type_name;
            size_t generic_start = type_name.find('<');
            bool is_generic = (generic_start != std::string::npos);
            if (is_generic)
            {
                base_type_name = type_name.substr(0, generic_start);
            }

            LOG_DEBUG(Cryo::LogComponent::GENERAL, "Pass 1: Processing impl block for type: {} (base: {})", type_name, base_type_name);

            // If this is an impl block for a generic enum, register it for monomorphization
            if (is_generic && _template_registry)
            {
                const TemplateRegistry::TemplateInfo *tmpl_info = _template_registry->find_template(base_type_name);
                if (tmpl_info && tmpl_info->enum_template)
                {
                    _template_registry->register_enum_impl_block(base_type_name, impl_block, _current_namespace);
                    LOG_DEBUG(Cryo::LogComponent::GENERAL,
                              "Pass 1: Registered enum impl block for '{}' (base: {}) with {} methods",
                              type_name, base_type_name, impl_block->method_implementations().size());
                }
            }
            // Also register non-generic enum impl blocks so that resolve_method_by_name
            // can look up parameter types when creating forward declarations
            else if (!is_generic && _template_registry && _symbol_table)
            {
                TypeRef enum_type = _symbol_table->lookup_enum_type(base_type_name);
                if (enum_type.is_valid() && enum_type->kind() == Cryo::TypeKind::Enum)
                {
                    _template_registry->register_enum_impl_block(base_type_name, impl_block, _current_namespace);
                    LOG_DEBUG(Cryo::LogComponent::GENERAL,
                              "Pass 1: Registered non-generic enum impl block for '{}' with {} methods",
                              base_type_name, impl_block->method_implementations().size());
                }
            }

            // Also register method return type annotations for cross-module lookups
            std::string qualified_type = _current_namespace.empty() ? base_type_name : _current_namespace + "::" + base_type_name;
            for (const auto &method : impl_block->method_implementations())
            {
                if (method && _template_registry)
                {
                    std::string return_type_str = method->return_type_annotation() ? method->return_type_annotation()->to_string() : "void";
                    std::string qualified_method_name = qualified_type + "::" + method->name();

                    if (!return_type_str.empty() && return_type_str != "void")
                    {
                        _template_registry->register_method_return_type_annotation(qualified_method_name, return_type_str);
                        LOG_DEBUG(Cryo::LogComponent::GENERAL,
                                  "Pass 1: Registered method return type annotation: {} -> {}",
                                  qualified_method_name, return_type_str);
                    }

                    // Register is_static flag
                    _template_registry->register_method_is_static(qualified_method_name, method->is_static());
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
        // Handle extern blocks — register contained function declarations
        else if (auto extern_block = dynamic_cast<ExternBlockNode *>(node))
        {
            std::string ns_alias = extern_block->namespace_alias();
            for (const auto &fn_decl : extern_block->function_declarations())
            {
                if (!fn_decl)
                    continue;

                // Register the function with its bare C name
                collect_declarations_pass(fn_decl.get(), current_scope, scope_name);

                // For CImport blocks with a namespace alias, also register under
                // the qualified name (e.g., "ex::greet") so ScopeResolutionNode resolves it
                if (extern_block->is_c_import() && !ns_alias.empty())
                {
                    std::string qualified_name = ns_alias + "::" + fn_decl->name();

                    std::vector<TypeRef> param_types;
                    for (const auto &param : fn_decl->parameters())
                    {
                        param_types.push_back(param->get_resolved_type());
                    }

                    TypeRef return_type = fn_decl->get_resolved_return_type();
                    if (!return_type.is_valid())
                    {
                        return_type = _ast_context->types().get_void();
                    }

                    TypeRef function_type = _ast_context->types().get_function(return_type, param_types, fn_decl->is_variadic());
                    _symbol_table->declare_function(qualified_name, function_type, fn_decl->location());

                    LOG_TRACE(Cryo::LogComponent::GENERAL,
                              "Pass 1: Registered CImport function: {} (C name: {})",
                              qualified_name, fn_decl->name());
                }
            }
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

    void CompilerInstance::populate_type_fields_pass(ASTNode *node)
    {
        // Phase 2: Now that all type NAMES are registered, we can safely resolve field types
        // This prevents forward reference failures where a field type (like Option<ChildStdin>)
        // references a struct defined later in the file.

        if (!node)
            return;

        // Handle struct declarations - populate fields
        if (auto struct_decl = dynamic_cast<StructDeclarationNode *>(node))
        {
            TypeRef struct_type = _symbol_table->lookup_struct_type(struct_decl->name());
            if (!struct_type.is_valid())
            {
                struct_type = _ast_context->types().lookup_type_by_name(struct_decl->name());
            }

            if (struct_type.is_valid() && !struct_decl->fields().empty())
            {
                auto *struct_ptr = const_cast<StructType *>(dynamic_cast<const StructType *>(struct_type.get()));
                if (struct_ptr && !struct_ptr->is_complete())
                {
                    std::vector<FieldInfo> fields;
                    bool is_generic = !struct_decl->generic_parameters().empty();

                    for (const auto &field : struct_decl->fields())
                    {
                        if (field)
                        {
                            TypeRef field_type = field->get_resolved_type();

                            // For generic structs, field types may contain generic params (T, U, etc.)
                            if (!field_type.is_valid() && field->has_type_annotation())
                            {
                                const TypeAnnotation *ann = field->type_annotation();

                                // Check if this is a generic parameter reference
                                if (is_generic && ann->kind == TypeAnnotationKind::Named)
                                {
                                    for (size_t i = 0; i < struct_decl->generic_parameters().size(); ++i)
                                    {
                                        if (struct_decl->generic_parameters()[i]->name() == ann->name)
                                        {
                                            field_type = _ast_context->types().create_generic_param(ann->name, i);
                                            break;
                                        }
                                    }
                                }

                                // Now all type names should be registered, so resolution should succeed
                                if (!field_type.is_valid())
                                {
                                    ResolutionContext ctx(_symbol_table->current_module());

                                    // For generic structs, bind all generic parameters to the context
                                    // so that complex types like HandlePoolEntry<T>* can resolve T correctly
                                    if (is_generic)
                                    {
                                        for (size_t i = 0; i < struct_decl->generic_parameters().size(); ++i)
                                        {
                                            const std::string &param_name = struct_decl->generic_parameters()[i]->name();
                                            TypeRef param_type = _ast_context->types().create_generic_param(param_name, i);
                                            ctx.bind_generic(param_name, param_type);
                                        }
                                    }

                                    field_type = _type_resolver->resolve(*ann, ctx);
                                    if (field_type.is_error())
                                    {
                                        LOG_WARN(Cryo::LogComponent::GENERAL,
                                                 "CompilerInstance: Field '{}' in struct '{}' still has unresolved type after all registrations (annotation: '{}')",
                                                 field->name(), struct_decl->name(), ann->to_string());
                                        field_type = TypeRef{};
                                    }

                                    // Fallback: search the global TypeArena by name.
                                    // This handles cross-namespace types that the scoped resolver can't find.
                                    if (!field_type.is_valid() && ann->kind == TypeAnnotationKind::Named)
                                    {
                                        field_type = _ast_context->types().lookup_type_by_name(ann->name);
                                        if (field_type.is_valid())
                                        {
                                            LOG_DEBUG(Cryo::LogComponent::GENERAL,
                                                      "CompilerInstance: Resolved field '{}' in struct '{}' via global type lookup (type: '{}')",
                                                      field->name(), struct_decl->name(), ann->name);
                                        }
                                    }
                                }
                            }

                            if (field_type.is_valid())
                            {
                                fields.emplace_back(field->name(), field_type, 0, true, field->is_mutable());
                            }
                            else
                            {
                                LOG_ERROR(Cryo::LogComponent::GENERAL,
                                          "CompilerInstance: Cannot resolve field '{}' in struct '{}' - skipping field",
                                          field->name(), struct_decl->name());
                            }
                        }
                    }

                    if (!fields.empty())
                    {
                        struct_ptr->set_fields(std::move(fields));
                        LOG_DEBUG(Cryo::LogComponent::GENERAL,
                                  "CompilerInstance: Set {} fields on struct '{}' (generic={})",
                                  struct_decl->fields().size(), struct_decl->name(), is_generic);
                    }
                }
            }
        }
        // Handle class declarations - populate fields
        else if (auto class_decl = dynamic_cast<ClassDeclarationNode *>(node))
        {
            TypeRef class_type = _symbol_table->lookup_class_type(class_decl->name());

            if (class_type.is_valid())
            {
                auto *class_ptr = const_cast<ClassType *>(dynamic_cast<const ClassType *>(class_type.get()));
                if (class_ptr && !class_ptr->is_complete())
                {
                    // Resolve base class if specified
                    if (!class_decl->base_class().empty() && !class_ptr->has_base_class())
                    {
                        TypeRef base_type = _symbol_table->lookup_class_type(class_decl->base_class());
                        if (!base_type.is_valid())
                        {
                            base_type = _ast_context->types().lookup_type_by_name(class_decl->base_class());
                        }
                        if (base_type.is_valid())
                        {
                            class_ptr->set_base_class(base_type);
                            LOG_DEBUG(Cryo::LogComponent::GENERAL,
                                      "CompilerInstance: Set base class '{}' for class '{}'",
                                      class_decl->base_class(), class_decl->name());
                        }
                        else
                        {
                            LOG_ERROR(Cryo::LogComponent::GENERAL,
                                      "CompilerInstance: Base class '{}' not found for class '{}'",
                                      class_decl->base_class(), class_decl->name());
                        }
                    }

                    // Register virtual/override method info
                    for (const auto &method : class_decl->methods())
                    {
                        if (method && (method->is_virtual() || method->is_override()))
                        {
                            MethodInfo mi(
                                method->name(),
                                method->get_resolved_return_type(),
                                (method->visibility() == Visibility::Public),
                                method->is_static());
                            mi.is_virtual = method->is_virtual();
                            mi.is_override = method->is_override();
                            class_ptr->add_method(mi);
                            LOG_DEBUG(Cryo::LogComponent::GENERAL,
                                      "CompilerInstance: Registered {} method '{}' on class '{}'",
                                      method->is_virtual() ? "virtual" : "override",
                                      method->name(), class_decl->name());
                        }
                    }

                    // Detect abstract class: any virtual method without a body is pure virtual
                    bool is_abstract = false;
                    for (const auto &method : class_decl->methods())
                    {
                        if (method && method->is_virtual() && !method->body())
                        {
                            is_abstract = true;
                            LOG_DEBUG(Cryo::LogComponent::GENERAL,
                                      "CompilerInstance: Class '{}' is abstract (pure virtual method '{}')",
                                      class_decl->name(), method->name());
                        }
                    }
                    class_ptr->set_abstract(is_abstract);

                    std::vector<FieldInfo> fields;
                    bool is_generic = !class_decl->generic_parameters().empty();

                    // Inherit base class fields
                    if (class_ptr->has_base_class())
                    {
                        auto *base_class = dynamic_cast<const ClassType *>(class_ptr->base_class().get());
                        if (base_class)
                        {
                            for (const auto &base_field : base_class->fields())
                            {
                                fields.push_back(base_field);
                            }
                            LOG_DEBUG(Cryo::LogComponent::GENERAL,
                                      "CompilerInstance: Inherited {} fields from base class '{}' for '{}'",
                                      base_class->fields().size(), class_decl->base_class(), class_decl->name());
                        }
                    }

                    for (const auto &field : class_decl->fields())
                    {
                        if (field)
                        {
                            TypeRef field_type = field->get_resolved_type();

                            // For generic classes, field types may contain generic params
                            if (!field_type.is_valid() && field->has_type_annotation())
                            {
                                const TypeAnnotation *ann = field->type_annotation();

                                if (is_generic && ann->kind == TypeAnnotationKind::Named)
                                {
                                    for (size_t i = 0; i < class_decl->generic_parameters().size(); ++i)
                                    {
                                        if (class_decl->generic_parameters()[i]->name() == ann->name)
                                        {
                                            field_type = _ast_context->types().create_generic_param(ann->name, i);
                                            break;
                                        }
                                    }
                                }

                                if (!field_type.is_valid())
                                {
                                    ResolutionContext ctx(_symbol_table->current_module());

                                    // For generic classes, bind all generic parameters to the context
                                    // so that complex types like Array<T>* can resolve T correctly
                                    if (is_generic)
                                    {
                                        for (size_t i = 0; i < class_decl->generic_parameters().size(); ++i)
                                        {
                                            const std::string &param_name = class_decl->generic_parameters()[i]->name();
                                            TypeRef param_type = _ast_context->types().create_generic_param(param_name, i);
                                            ctx.bind_generic(param_name, param_type);
                                        }
                                    }

                                    field_type = _type_resolver->resolve(*ann, ctx);
                                    if (field_type.is_error())
                                    {
                                        LOG_WARN(Cryo::LogComponent::GENERAL,
                                                 "CompilerInstance: Field '{}' in class '{}' still has unresolved type, trying global lookup",
                                                 field->name(), class_decl->name());
                                        field_type = TypeRef{};
                                    }

                                    // Fallback: search the global TypeArena by name.
                                    // This handles cross-namespace types (e.g., NodeKind from a parent namespace)
                                    // that the scoped resolver can't find.
                                    if (!field_type.is_valid() && ann->kind == TypeAnnotationKind::Named)
                                    {
                                        field_type = _ast_context->types().lookup_type_by_name(ann->name);
                                        if (field_type.is_valid())
                                        {
                                            LOG_DEBUG(Cryo::LogComponent::GENERAL,
                                                      "CompilerInstance: Resolved field '{}' in class '{}' via global type lookup (type: '{}')",
                                                      field->name(), class_decl->name(), ann->name);
                                        }
                                    }
                                }
                            }

                            if (field_type.is_valid())
                            {
                                fields.emplace_back(field->name(), field_type, 0, true, field->is_mutable());
                            }
                            else
                            {
                                LOG_ERROR(Cryo::LogComponent::GENERAL,
                                          "CompilerInstance: Cannot resolve field '{}' in class '{}' - skipping field",
                                          field->name(), class_decl->name());
                            }
                        }
                    }

                    if (!fields.empty())
                    {
                        class_ptr->set_fields(std::move(fields));
                        LOG_DEBUG(Cryo::LogComponent::GENERAL,
                                  "CompilerInstance: Set {} fields on class '{}' (generic={})",
                                  class_decl->fields().size(), class_decl->name(), is_generic);
                    }
                }
            }
        }
        // Handle enum declarations - populate variant payload types
        else if (auto enum_decl = dynamic_cast<EnumDeclarationNode *>(node))
        {
            // Skip generic enums - their variants are handled during monomorphization
            if (!enum_decl->generic_parameters().empty())
            {
                return;
            }

            TypeRef enum_type = _symbol_table->lookup_enum_type(enum_decl->name());
            if (!enum_type.is_valid())
            {
                enum_type = _ast_context->types().lookup_type_by_name(enum_decl->name());
            }

            if (enum_type.is_valid() && enum_type->kind() == TypeKind::Enum)
            {
                auto *enum_ptr = const_cast<EnumType *>(dynamic_cast<const EnumType *>(enum_type.get()));
                if (enum_ptr)
                {
                    // Check if variants need population or payload type resolution.
                    // Variants need population if:
                    // 1. The enum has no variants yet (created without them in collect_declarations_pass)
                    // 2. Any existing variant has unresolved payload types (from ModuleLoader)
                    bool needs_resolution = enum_ptr->variant_count() == 0;
                    if (!needs_resolution)
                    {
                        for (const auto &variant : enum_ptr->variants())
                        {
                            for (const auto &payload_type : variant.payload_types)
                            {
                                if (!payload_type.is_valid())
                                {
                                    needs_resolution = true;
                                    break;
                                }
                            }
                            if (needs_resolution)
                                break;
                        }
                    }

                    if (needs_resolution)
                    {
                        std::vector<EnumVariant> resolved_variants;
                        size_t variant_idx = 0;

                        for (const auto &ast_variant : enum_decl->variants())
                        {
                            std::vector<TypeRef> payload_types;

                            for (const std::string &type_str : ast_variant->associated_types())
                            {
                                ResolutionContext ctx(_symbol_table->current_module());
                                TypeAnnotation ann = TypeAnnotation::named(type_str, ast_variant->location());
                                TypeRef payload_type = _type_resolver->resolve(ann, ctx);

                                if (payload_type.is_valid() && !payload_type.is_error())
                                {
                                    payload_types.push_back(payload_type);
                                    LOG_DEBUG(Cryo::LogComponent::GENERAL,
                                              "populate_type_fields_pass: Resolved enum variant payload '{}' for {}::{}",
                                              type_str, enum_decl->name(), ast_variant->name());
                                }
                                else
                                {
                                    // Still couldn't resolve - use invalid ref
                                    payload_types.push_back(TypeRef{});
                                    LOG_WARN(Cryo::LogComponent::GENERAL,
                                             "populate_type_fields_pass: Could not resolve enum variant payload '{}' for {}::{}",
                                             type_str, enum_decl->name(), ast_variant->name());
                                }
                            }

                            resolved_variants.push_back(EnumVariant(ast_variant->name(), std::move(payload_types), variant_idx++));
                        }

                        enum_ptr->set_variants(std::move(resolved_variants));
                        LOG_DEBUG(Cryo::LogComponent::GENERAL,
                                  "populate_type_fields_pass: Updated {} variants on enum '{}'",
                                  enum_decl->variants().size(), enum_decl->name());
                    }
                }
            }
        }
        // Handle declaration statements - recurse
        else if (auto decl_stmt = dynamic_cast<DeclarationStatementNode *>(node))
        {
            if (decl_stmt->declaration())
            {
                populate_type_fields_pass(decl_stmt->declaration());
            }
        }
        // Handle program nodes - recurse into statements
        else if (auto program = dynamic_cast<ProgramNode *>(node))
        {
            // PHASE 1: Pre-register ALL types in ModuleTypeRegistry FIRST
            // This is critical for forward reference resolution. If we try to resolve
            // Option<ChildStdin> for a field in struct Child, we need ChildStdin to
            // already be registered in the module registry, even if it's defined later.
            auto &module_registry = _ast_context->modules();
            ModuleID current_module = _symbol_table->current_module();

            LOG_DEBUG(LogComponent::GENERAL,
                      "populate_type_fields_pass: Pre-registering all types in module registry (module={})",
                      current_module.id);

            for (const auto &stmt : program->statements())
            {
                // Pre-register struct types
                if (auto *struct_decl = dynamic_cast<StructDeclarationNode *>(stmt.get()))
                {
                    TypeRef struct_type = _symbol_table->lookup_struct_type(struct_decl->name());
                    if (!struct_type.is_valid())
                    {
                        struct_type = _ast_context->types().lookup_type_by_name(struct_decl->name());
                    }
                    if (struct_type.is_valid())
                    {
                        module_registry.register_type(current_module, struct_decl->name(), struct_type);
                        LOG_DEBUG(LogComponent::GENERAL,
                                  "populate_type_fields_pass: Pre-registered struct '{}' in module registry",
                                  struct_decl->name());
                    }
                }
                // Pre-register class types
                else if (auto *class_decl = dynamic_cast<ClassDeclarationNode *>(stmt.get()))
                {
                    TypeRef class_type = _symbol_table->lookup_class_type(class_decl->name());
                    if (class_type.is_valid())
                    {
                        module_registry.register_type(current_module, class_decl->name(), class_type);
                        LOG_DEBUG(LogComponent::GENERAL,
                                  "populate_type_fields_pass: Pre-registered class '{}' in module registry",
                                  class_decl->name());
                    }
                }
                // Pre-register enum types
                else if (auto *enum_decl = dynamic_cast<EnumDeclarationNode *>(stmt.get()))
                {
                    TypeRef enum_type = _symbol_table->lookup_enum_type(enum_decl->name());
                    if (enum_type.is_valid())
                    {
                        module_registry.register_type(current_module, enum_decl->name(), enum_type);
                        LOG_DEBUG(LogComponent::GENERAL,
                                  "populate_type_fields_pass: Pre-registered enum '{}' in module registry",
                                  enum_decl->name());
                    }
                }
            }

            // PHASE 2: Now resolve field types - all types are in the registry
            for (const auto &stmt : program->statements())
            {
                populate_type_fields_pass(stmt.get());
            }
        }
        // Handle block statements - recurse
        else if (auto block = dynamic_cast<BlockStatementNode *>(node))
        {
            for (const auto &stmt : block->statements())
            {
                populate_type_fields_pass(stmt.get());
            }
        }
        // Other node types don't need field population
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
                // Enter a new scope for this function's local variables so that
                // identically-named locals in different functions don't collide.
                current_scope->enter_scope(func_decl->name());
                populate_symbol_table_with_scope(func_decl->body(), current_scope, func_decl->name());
                current_scope->exit_scope();
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
        // Handle variable declarations (both const and mutable)
        else if (auto var_decl = dynamic_cast<VariableDeclarationNode *>(node))
        {
            TypeRef var_type = var_decl->get_resolved_type();

            // Register in scope to detect duplicate declarations
            bool success = current_scope->declare_symbol(var_decl->name(), SymbolKind::Variable,
                                                         var_decl->location(), var_type, scope_name);

            if (!success && scope_name != "Global")
            {
                // Only emit duplicate errors for local scopes, not global scope
                // (global scope gets processed multiple times by different passes)
                if (_diagnostics)
                {
                    std::string err_msg = "variable '" + var_decl->name() + "' is already declared in this scope" +
                                          "Declared in scope: " + scope_name;
                    _diagnostics->emit(
                        Diag::error(ErrorCode::E0205_REDEFINED_SYMBOL, err_msg)
                            .at(var_decl));
                }
            }
            else if (_debug_mode)
            {
                LOG_DEBUG(Cryo::LogComponent::GENERAL, "Added variable '{}' to scope '{}' with type '{}'",
                          var_decl->name(), scope_name, var_type.is_valid() ? var_type.get()->display_name() : "null");
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

    void CompilerInstance::inject_parent_module_import()
    {
        if (_current_namespace.empty() || !_module_loader)
            return;

        // Skip _module.cryo files — they ARE the parent
        std::string filename = std::filesystem::path(_source_file).filename().string();
        if (filename == "_module.cryo")
            return;

        // Compute parent module path from namespace.
        // "std::net::tcp" → strip "std::" → "net::tcp" → strip last segment → "net"
        std::string ns = _current_namespace;
        const std::string std_prefix = "std::";
        if (ns.size() > std_prefix.size() && ns.substr(0, std_prefix.size()) == std_prefix)
            ns = ns.substr(std_prefix.size());

        size_t last_sep = ns.rfind("::");
        if (last_sep == std::string::npos)
            return; // Top-level module, no parent

        std::string parent_module_path = ns.substr(0, last_sep);

        LOG_DEBUG(LogComponent::GENERAL,
                  "Injecting parent module import '{}' for submodule namespace '{}'",
                  parent_module_path, _current_namespace);

        // Set up module loader (same pattern as inject_auto_imports line 2598-2610)
        if (!_module_loader->auto_detect_stdlib_root())
            _module_loader->set_stdlib_root("./stdlib");
        _module_loader->set_current_file(_source_file);

        // Create synthetic wildcard import for parent module
        auto parent_import = std::make_unique<ImportDeclarationNode>(
            SourceLocation(0, 0), parent_module_path);

        // Temporarily disable stdlib mode (same pattern as line 2498)
        bool saved = _stdlib_compilation_mode;
        _stdlib_compilation_mode = false;

        auto result = _module_loader->load_import(*parent_import);

        _stdlib_compilation_mode = saved;

        if (!result.success || result.symbol_map.empty())
            return;

        // Register symbols for unqualified access (sockaddr_in, timeval)
        for (const auto &[name, symbol] : result.symbol_map)
            _symbol_table->declare_symbol(name, symbol.kind, symbol.location, symbol.type, "Global");

        // Also register as namespace for qualified access (net::sockaddr_in)
        _symbol_table->register_namespace(parent_module_path, result.symbol_map);

        LOG_DEBUG(LogComponent::GENERAL,
                  "Injected {} symbols from parent module '{}' for submodule '{}'",
                  result.symbol_map.size(), parent_module_path, _current_namespace);
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
        // Register type alias targets with TypeMapper for placeholder struct resolution
        else if (auto alias_decl = dynamic_cast<TypeAliasDeclarationNode *>(node))
        {
            if (alias_decl->has_resolved_target_type() && !alias_decl->is_generic())
            {
                TypeRef resolved_target = alias_decl->get_resolved_target_type();
                type_mapper->register_type_alias_target(alias_decl->alias_name(), resolved_target);
                LOG_DEBUG(Cryo::LogComponent::GENERAL,
                          "Registered type alias target: '{}' -> '{}'",
                          alias_decl->alias_name(), resolved_target->display_name());
            }
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
        // If this is a type alias declaration, register its target with TypeMapper
        // so that map_struct() can redirect placeholder StructTypes to the real type.
        // DeclarationCollection creates placeholder StructTypes for unresolved aliases
        // (e.g., "AllocResult" as an empty struct). Without this registration,
        // map_struct("AllocResult") creates an opaque LLVM struct instead of
        // redirecting to the concrete Result<void*, AllocError> type.
        else if (auto alias_decl = dynamic_cast<TypeAliasDeclarationNode *>(node))
        {
            if (alias_decl->has_resolved_target_type() && !alias_decl->is_generic())
            {
                TypeRef resolved_target = alias_decl->get_resolved_target_type();
                TypeMapper *type_mapper = _codegen->get_visitor()->get_type_mapper();
                if (type_mapper)
                {
                    type_mapper->register_type_alias_target(alias_decl->alias_name(), resolved_target);
                    LOG_DEBUG(Cryo::LogComponent::GENERAL,
                              "Pre-processing type alias: '{}' -> '{}'",
                              alias_decl->alias_name(), resolved_target->display_name());
                }
            }
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

    CompilerInstance::SimpleTypeDesc CompilerInstance::llvm_type_to_desc(llvm::Type *t)
    {
        SimpleTypeDesc desc;
        if (t->isVoidTy())
        {
            desc.kind = SimpleTypeDesc::Void;
        }
        else if (t->isIntegerTy())
        {
            desc.kind = SimpleTypeDesc::Int;
            desc.int_width = t->getIntegerBitWidth();
        }
        else if (t->isFloatTy())
        {
            desc.kind = SimpleTypeDesc::Float;
        }
        else if (t->isDoubleTy())
        {
            desc.kind = SimpleTypeDesc::Double;
        }
        else if (t->isPointerTy())
        {
            desc.kind = SimpleTypeDesc::Ptr;
        }
        else if (auto *st = llvm::dyn_cast<llvm::StructType>(t))
        {
            if (st->hasName())
            {
                desc.kind = SimpleTypeDesc::NamedStruct;
                desc.struct_name = st->getName().str();
            }
        }
        return desc;
    }

    llvm::Type *CompilerInstance::desc_to_llvm_type(const SimpleTypeDesc &desc,
                                                    llvm::LLVMContext &ctx)
    {
        switch (desc.kind)
        {
        case SimpleTypeDesc::Void:
            return llvm::Type::getVoidTy(ctx);
        case SimpleTypeDesc::Int:
            return llvm::IntegerType::get(ctx, desc.int_width);
        case SimpleTypeDesc::Float:
            return llvm::Type::getFloatTy(ctx);
        case SimpleTypeDesc::Double:
            return llvm::Type::getDoubleTy(ctx);
        case SimpleTypeDesc::Ptr:
            return llvm::PointerType::get(ctx, 0);
        case SimpleTypeDesc::NamedStruct:
        {
            auto *existing = llvm::StructType::getTypeByName(ctx, desc.struct_name);
            if (existing)
                return existing;
            // Struct not yet defined in this context — create an opaque (identified) struct
            // so the function declaration can still be created. The linker will resolve
            // the actual struct body when bitcode files are merged via llvm-link.
            return llvm::StructType::create(ctx, desc.struct_name);
        }
        default:
            // For unrecognized types (arrays, vectors, etc.), use opaque pointer
            // as a safe fallback. Most non-primitive types are passed by pointer
            // in Cryo, and LLVM 20's opaque pointers make this always valid.
            return llvm::PointerType::get(ctx, 0);
        }
    }

    void CompilerInstance::save_cross_module_functions()
    {
        if (!_codegen)
            return;

        llvm::Module *module = _codegen->get_module();
        if (!module)
            return;

        size_t saved = 0;
        for (const llvm::Function &fn : *module)
        {
            if (fn.isDeclaration() || fn.isIntrinsic())
                continue;

            std::string fn_name = fn.getName().str();
            if (fn_name.empty())
                continue;

            // Don't overwrite if already registered from a previous module
            if (_cross_module_functions.count(fn_name))
                continue;

            CrossModuleFnEntry entry;
            entry.calling_conv = fn.getCallingConv();
            entry.is_var_arg = fn.isVarArg();

            llvm::FunctionType *ft = fn.getFunctionType();
            entry.return_type = llvm_type_to_desc(ft->getReturnType());
            for (unsigned i = 0; i < ft->getNumParams(); ++i)
            {
                entry.param_types.push_back(llvm_type_to_desc(ft->getParamType(i)));
            }

            _cross_module_functions[fn_name] = std::move(entry);
            saved++;
        }

        if (saved > 0)
        {
            LOG_DEBUG(LogComponent::GENERAL,
                      "Cross-module: saved {} function signatures (total registry: {})",
                      saved, _cross_module_functions.size());
        }
    }

    void CompilerInstance::declare_cross_module_functions()
    {
        if (!_codegen || _cross_module_functions.empty())
            return;

        if (!_codegen->ensure_visitor_initialized())
            return;

        llvm::Module *current_module = _codegen->get_module();
        if (!current_module)
            return;

        llvm::LLVMContext &ctx = current_module->getContext();
        size_t declared = 0;
        size_t skipped = 0;

        for (const auto &[fn_name, entry] : _cross_module_functions)
        {
            // Check if already declared in current module
            if (llvm::Function *existing = current_module->getFunction(fn_name))
            {
                // If the existing declaration has a different signature than the cross-module
                // definition (e.g., pre_register_functions created it with wrong arg count),
                // remove the incorrect declaration and let us create the correct one.
                llvm::FunctionType *existing_ft = existing->getFunctionType();
                if (existing->isDeclaration() &&
                    existing_ft->getNumParams() != entry.param_types.size())
                {
                    LOG_DEBUG(LogComponent::GENERAL,
                              "Cross-module: replacing mismatched declaration '{}' "
                              "(had {} params, need {})",
                              fn_name, existing_ft->getNumParams(), entry.param_types.size());
                    existing->eraseFromParent();
                }
                else
                {
                    continue;
                }
            }

            // Reconstruct return type
            llvm::Type *ret_type = desc_to_llvm_type(entry.return_type, ctx);
            if (!ret_type)
            {
                skipped++;
                continue;
            }

            // Reconstruct parameter types
            std::vector<llvm::Type *> param_types;
            bool valid = true;
            for (const auto &pd : entry.param_types)
            {
                llvm::Type *pt = desc_to_llvm_type(pd, ctx);
                if (!pt)
                {
                    valid = false;
                    break;
                }
                param_types.push_back(pt);
            }
            if (!valid)
            {
                skipped++;
                continue;
            }

            llvm::FunctionType *ft = llvm::FunctionType::get(ret_type, param_types, entry.is_var_arg);
            llvm::Function *decl = llvm::Function::Create(
                ft,
                llvm::Function::ExternalLinkage,
                fn_name,
                current_module);
            decl->setCallingConv(static_cast<llvm::CallingConv::ID>(entry.calling_conv));

            _codegen->get_visitor()->context().register_function(fn_name, decl);
            declared++;
        }

        if (declared > 0 || skipped > 0)
        {
            LOG_DEBUG(LogComponent::GENERAL,
                      "Cross-module: declared {} functions, skipped {} (incompatible types)",
                      declared, skipped);
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

            // Save cross-module function signatures even from partially failed modules.
            // The LLVM module may contain valid function definitions whose signatures
            // are needed by later modules. Function signatures (types) are set at
            // creation time and remain correct even if the function body has errors.
            if (_codegen && _codegen->get_module())
            {
                save_cross_module_functions();
            }

            // Emit LLVM IR file for successfully compiled modules
            if (module_success && _codegen)
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

        // Clear cross-module function registry after stdlib compilation is complete
        _cross_module_functions.clear();

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

        // Combine object files into final static library archive
        if (!generated_object_files.empty() && _linker)
        {
            std::string archive_path = output_dir + "/libcryo.a";
            std::cout << "  Bundling " << generated_object_files.size()
                      << " object files into " << archive_path << "..." << std::endl;

            if (_linker->create_static_archive(generated_object_files, archive_path))
            {
                try
                {
                    if (std::filesystem::exists(archive_path))
                        std::cout << "  ✓ Created stdlib archive: " << archive_path
                                  << " (" << format_bytes(std::filesystem::file_size(archive_path)) << ")"
                                  << std::endl;
                }
                catch (...)
                {
                }
            }
            else
            {
                std::cout << "  ✗ Failed to create stdlib archive: "
                          << _linker->get_last_error() << std::endl;
                overall_success = false;
            }
        }

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
            _parser->set_raw_mode(_raw_mode);

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

            // Set Monomorphizer for access to specialized ASTs during codegen
            if (_codegen->ensure_visitor_initialized() && _monomorphization_pass)
            {
                _codegen->get_visitor()->context().set_monomorphizer(_monomorphization_pass.get());
                LOG_DEBUG(LogComponent::GENERAL, "Set Monomorphizer in CodegenContext for specialized AST access");
            }

            // Set GenericRegistry for generic type resolution during codegen
            if (_codegen->ensure_visitor_initialized() && _generic_registry)
            {
                _codegen->get_visitor()->context().set_generic_registry(_generic_registry.get());
                LOG_DEBUG(LogComponent::GENERAL, "Set GenericRegistry in CodegenContext for generic type resolution");
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
        // Always inject parent module imports for submodule files.
        // This runs even in stdlib mode so net/tcp.cryo can see net/_module.cryo types.
        inject_parent_module_import();

        if (!_stdlib_compilation_mode && _auto_imports_enabled)
        {
            LOG_DEBUG(LogComponent::GENERAL, "Auto-import phase: Injecting auto-imports");
            inject_auto_imports(_symbol_table, "Global");
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

        LOG_DEBUG(LogComponent::GENERAL, "Declaration collection phase: Collecting declarations (type names)");
        collect_declarations_pass(_ast_root.get(), _symbol_table, "Global");

        // Phase 1b: Populate struct/class fields after all type names are registered
        LOG_DEBUG(LogComponent::GENERAL, "Declaration collection phase: Populating struct/class field types");
        populate_type_fields_pass(_ast_root.get());
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
        populate_symbol_table_with_scope(_ast_root.get(), _symbol_table, "Global");

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

            // Also pre-register imported module structs so they exist during main module codegen
            if (_module_loader && (!_local_import_modules.empty() || !_module_loader->get_all_local_import_names().empty()))
            {
                const auto &imported_asts = _module_loader->get_imported_asts();
                std::unordered_set<std::string> seen;

                // Collect all local import module names (direct + transitive)
                std::vector<std::string> all_local_modules(_local_import_modules.begin(), _local_import_modules.end());
                for (const auto &name : _module_loader->get_all_local_import_names())
                {
                    all_local_modules.push_back(name);
                }

                for (const auto &module_name : all_local_modules)
                {
                    if (seen.insert(module_name).second)
                    {
                        auto it = imported_asts.find(module_name);
                        if (it != imported_asts.end() && it->second)
                            process_struct_declarations_for_preregistration(it->second.get());
                    }
                    std::string prefix = module_name + "::";
                    for (const auto &[ast_name, ast_ptr] : imported_asts)
                    {
                        if (ast_ptr && ast_name.compare(0, prefix.size(), prefix) == 0 &&
                            seen.insert(ast_name).second)
                        {
                            process_struct_declarations_for_preregistration(ast_ptr.get());
                        }
                    }
                }
            }

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

            // Declare functions from previously compiled modules (cross-module resolution)
            if (!_cross_module_functions.empty())
            {
                declare_cross_module_functions();
            }
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

            // Sync imported namespaces and aliases from compiler's SRM to codegen's SRM
            // This ensures functions from imported modules can be resolved by alias
            // (e.g., "import Utils as U" → U::add resolves to Utils::add)
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
                    }
                    LOG_DEBUG(LogComponent::GENERAL, "IR gen phase: Synced {} imported namespaces to codegen SRM", imported_namespaces.size());

                    const auto &aliases = compiler_srm_ctx->get_namespace_aliases();
                    for (const auto &[alias, target] : aliases)
                    {
                        codegen_srm_ctx->register_namespace_alias(alias, target);
                        LOG_DEBUG(LogComponent::GENERAL, "IR gen phase: Synced namespace alias '{}' -> '{}'", alias, target);
                    }
                }
            }

            // Pass imported ASTs to CodegenVisitor for dynamic enum variant extraction
            if (_codegen->get_visitor() && _module_loader)
            {
                LOG_DEBUG(LogComponent::GENERAL, "IR generation phase: Setting imported ASTs");
                _codegen->get_visitor()->set_imported_asts(&_module_loader->get_imported_asts());
            }

            // Generate main module IR FIRST. This sets up:
            // - Prelude functions (ok, err, some, none, etc.)
            // - Intrinsic declarations (fileno, fopen, fclose, etc.)
            // - All main module types and function declarations
            // Main module calls to imported functions will create extern declarations
            // that get resolved when imported module IR is generated afterward.
            LOG_DEBUG(LogComponent::GENERAL, "IR generation phase: Generating main module LLVM IR");
            bool success = _codegen->generate_ir(_ast_root.get());

            // Now generate IR for local project imports AFTER main module.
            // The prelude/intrinsic functions are now available in the LLVM module,
            // so imported method bodies can reference ok(), err(), fileno(), etc.
            if (_module_loader && (!_local_import_modules.empty() || !_module_loader->get_all_local_import_names().empty()))
            {
                const auto &imported_asts = _module_loader->get_imported_asts();

                // Collect all local modules that need IR generation
                std::unordered_set<std::string> all_local_modules;
                for (const auto &m : _module_loader->get_all_local_import_names())
                    all_local_modules.insert(m);
                for (const auto &m : _local_import_modules)
                    all_local_modules.insert(m);

                // Also find submodule ASTs (prefix match: "Baz" → "Baz::Qix", "Baz::Qix::Sub")
                {
                    std::vector<std::string> to_scan(all_local_modules.begin(), all_local_modules.end());
                    for (size_t i = 0; i < to_scan.size(); ++i)
                    {
                        std::string prefix = to_scan[i] + "::";
                        for (const auto &[ast_name, ast_ptr] : imported_asts)
                        {
                            if (ast_ptr && ast_name.compare(0, prefix.size(), prefix) == 0 &&
                                all_local_modules.insert(ast_name).second)
                            {
                                to_scan.push_back(ast_name);
                            }
                        }
                    }
                }

                // Build dependency graph from AST import declarations.
                // For each module, scan its AST for ImportDeclarationNodes and record
                // edges: module -> [modules it depends on]. This lets us topologically
                // sort so dependencies are compiled before dependents.
                std::unordered_map<std::string, std::vector<std::string>> deps;
                std::unordered_map<std::string, int> in_degree;
                for (const auto &mod : all_local_modules)
                {
                    deps[mod]; // ensure entry exists
                    if (in_degree.find(mod) == in_degree.end())
                        in_degree[mod] = 0;
                }

                for (const auto &mod : all_local_modules)
                {
                    auto ast_it = imported_asts.find(mod);
                    if (ast_it == imported_asts.end() || !ast_it->second)
                        continue;

                    for (const auto &stmt : ast_it->second->statements())
                    {
                        if (!stmt || stmt->kind() != NodeKind::ImportDeclaration)
                            continue;
                        auto *import_node = static_cast<ImportDeclarationNode *>(stmt.get());
                        const std::string &import_path = import_node->module_path();

                        // Check if this import refers to another local module
                        if (all_local_modules.count(import_path))
                        {
                            deps[mod].push_back(import_path);
                            in_degree[import_path]; // ensure entry
                            in_degree[mod];         // ensure entry
                        }
                        // Also check for parent namespace imports (e.g., "Utils" when "Utils::File" exists)
                        // and submodule imports (e.g., "Utils::File" when "Utils" is the import)
                        for (const auto &candidate : all_local_modules)
                        {
                            if (candidate == mod)
                                continue;
                            // "Utils::File" starts with "Utils::" (import_path = "Utils")
                            if (candidate.compare(0, import_path.size() + 2, import_path + "::") == 0 ||
                                import_path.compare(0, candidate.size() + 2, candidate + "::") == 0)
                            {
                                // This module depends on the candidate (or vice versa for parent)
                                if (candidate.compare(0, import_path.size() + 2, import_path + "::") == 0)
                                {
                                    // import_path is parent of candidate, mod depends on candidate
                                    deps[mod].push_back(candidate);
                                    in_degree[candidate]; // ensure entry
                                }
                            }
                        }
                    }
                }

                // Add implicit dependency: a disambiguated submodule depends on its parent namespace.
                // e.g., "Compiler::Lex::Lexer" depends on "Compiler::Lex" because the Lexer struct
                // uses types (Token, TokenType, SourceLocation) defined in the parent _module.cryo.
                for (const auto &mod : all_local_modules)
                {
                    // Check if this module name is a child of another module in the set
                    // by seeing if removing the last "::Segment" yields another module in the set.
                    auto last_sep = mod.rfind("::");
                    if (last_sep != std::string::npos)
                    {
                        std::string parent = mod.substr(0, last_sep);
                        if (all_local_modules.count(parent))
                        {
                            deps[mod].push_back(parent);
                            in_degree[parent]; // ensure entry
                        }
                    }
                }

                // Remove self-edges and deduplicate
                for (auto &[mod, dep_list] : deps)
                {
                    std::sort(dep_list.begin(), dep_list.end());
                    dep_list.erase(std::unique(dep_list.begin(), dep_list.end()), dep_list.end());
                    dep_list.erase(std::remove(dep_list.begin(), dep_list.end(), mod), dep_list.end());
                }

                // Recompute in-degrees after dedup
                for (auto &[mod, _] : in_degree)
                    in_degree[mod] = 0;
                for (const auto &[mod, dep_list] : deps)
                {
                    for (const auto &dep : dep_list)
                        in_degree[dep]++;
                }

                // Topological sort (Kahn's algorithm) - modules with no dependents first
                // Note: in_degree here counts how many modules depend ON this module.
                // We want leaf modules (depended on by others, but depend on nothing) first.
                // So we sort by: modules whose own dependencies are all satisfied.
                // Recompute: in_degree = number of deps this module has that haven't been generated yet
                std::unordered_map<std::string, int> remaining_deps;
                for (const auto &mod : all_local_modules)
                    remaining_deps[mod] = static_cast<int>(deps[mod].size());

                std::vector<std::string> modules_to_generate;
                std::queue<std::string> ready;
                for (const auto &mod : all_local_modules)
                {
                    if (remaining_deps[mod] == 0)
                        ready.push(mod);
                }

                while (!ready.empty())
                {
                    std::string mod = ready.front();
                    ready.pop();
                    modules_to_generate.push_back(mod);

                    // For all modules that depend on this one, decrement their remaining count
                    for (const auto &[other, dep_list] : deps)
                    {
                        if (other == mod)
                            continue;
                        for (const auto &dep : dep_list)
                        {
                            if (dep == mod)
                            {
                                remaining_deps[other]--;
                                if (remaining_deps[other] == 0)
                                    ready.push(other);
                                break;
                            }
                        }
                    }
                }

                // Handle any remaining modules (cycles) - add them anyway
                for (const auto &mod : all_local_modules)
                {
                    if (std::find(modules_to_generate.begin(), modules_to_generate.end(), mod) == modules_to_generate.end())
                    {
                        LOG_WARN(LogComponent::GENERAL, "IR generation phase: Module '{}' has circular dependency, adding anyway", mod);
                        modules_to_generate.push_back(mod);
                    }
                }

                LOG_DEBUG(LogComponent::GENERAL, "IR generation phase: Module order after dependency sort:");
                for (size_t i = 0; i < modules_to_generate.size(); ++i)
                    LOG_DEBUG(LogComponent::GENERAL, "  [{}] {}", i, modules_to_generate[i]);

                // Pre-registration pass: create LLVM struct/class/enum types for ALL modules
                // before any module generates method bodies. This ensures cross-module type
                // references (e.g., Checker using Type*) can find the LLVM struct type.
                LOG_DEBUG(LogComponent::GENERAL, "IR generation phase: Pre-registering type declarations for all modules");
                for (const auto &module_name : modules_to_generate)
                {
                    auto it = imported_asts.find(module_name);
                    if (it != imported_asts.end() && it->second)
                    {
                        _codegen->generate_imported_type_declarations(it->second.get(), module_name);
                    }
                }
                LOG_DEBUG(LogComponent::GENERAL, "IR generation phase: Type pre-registration complete");

                for (const auto &module_name : modules_to_generate)
                {
                    auto it = imported_asts.find(module_name);
                    if (it != imported_asts.end() && it->second)
                    {
                        LOG_DEBUG(LogComponent::GENERAL, "IR generation phase: Generating IR for local import '{}'", module_name);
                        bool import_success = _codegen->generate_imported_ir(it->second.get(), module_name);
                        if (!import_success)
                        {
                            // Don't abort compilation for transitive import failures.
                            // Methods that couldn't be generated will surface as linker errors
                            // only if they're actually called.
                            LOG_WARN(LogComponent::GENERAL, "IR generation phase: Partial failure for local import '{}': {} (continuing)",
                                     module_name, _codegen->get_last_error());
                        }
                        LOG_DEBUG(LogComponent::GENERAL, "IR generation phase: Successfully generated IR for local import '{}'", module_name);
                    }
                    else
                    {
                        LOG_WARN(LogComponent::GENERAL, "IR generation phase: AST not found for local import '{}'", module_name);
                    }
                }
            }

            if (!success)
            {
                LOG_ERROR(LogComponent::GENERAL, "IR generation phase: Failed with error: {}",
                          _codegen->get_last_error());
                _diagnostics->emit(Diag::error(ErrorCode::E0600_CODEGEN_FAILED,
                                               "IR generation failed: " + _codegen->get_last_error()));
                return false;
            }

            return true;
        }
        catch (const std::exception &e)
        {
            _diagnostics->emit(Diag::error(ErrorCode::E0600_CODEGEN_FAILED,
                                           std::string("IR generation exception: ") + e.what()));
            return false;
        }
    }

    bool CompilerInstance::init_codegen_for_lsp()
    {
        if (_codegen)
            return true; // Already initialized

        if (!_ast_root)
            return false;

        try
        {
            std::string namespace_for_module = _current_namespace.empty() ? "cryo_program" : _current_namespace;
            _codegen = Cryo::Codegen::create_default_codegen(*_ast_context, *_symbol_table, namespace_for_module, _diagnostics.get());
            _codegen->set_source_info(_source_file, _current_namespace);

            if (_stdlib_compilation_mode)
                _codegen->set_stdlib_compilation_mode(true);

            if (_codegen->ensure_visitor_initialized() && _template_registry)
                _codegen->get_visitor()->context().set_template_registry(_template_registry.get());

            if (_codegen->ensure_visitor_initialized() && _monomorphization_pass)
                _codegen->get_visitor()->context().set_monomorphizer(_monomorphization_pass.get());

            if (_codegen->ensure_visitor_initialized() && _generic_registry)
                _codegen->get_visitor()->context().set_generic_registry(_generic_registry.get());

            register_ast_nodes_with_typemapper();

            LOG_DEBUG(LogComponent::LSP, "LSP: CodeGenerator initialized for extended analysis (module: '{}')", namespace_for_module);
            return true;
        }
        catch (const std::exception &e)
        {
            LOG_WARN(LogComponent::LSP, "LSP: Failed to initialize CodeGenerator: {}", e.what());
            return false;
        }
    }

    bool CompilerInstance::run_lsp_semantic_analysis()
    {
        if (!_ast_root)
            return false;

        // Create the full pipeline (stages 1-8) to support extended analysis.
        // We selectively run stages 3-8, skipping 1-2 which the LSP already handled.
        auto pipeline = StandardPassFactory::create_standard_pipeline(*this);
        PassContext ctx(*this);

        // Pre-seed the context with stage 1-2 provisions since the LSP already
        // handled parsing and import resolution outside the PassManager.
        ctx.mark_provided(PassProvides::TOKENS);
        ctx.mark_provided(PassProvides::AST);
        ctx.mark_provided(PassProvides::AST_VALIDATED);
        ctx.mark_provided(PassProvides::IMPORTS_DISCOVERED);
        ctx.mark_provided(PassProvides::MODULES_LOADED);
        ctx.mark_provided(PassProvides::MODULE_ORDER);

        // Run stages 3-5 (frontend parsing and import resolution already done by LSP)
        size_t errs = _diagnostics ? _diagnostics->error_count() : 0;

        // Stage 3: Declaration Collection (type names, function signatures, templates)
        bool s3 = pipeline->run_stage(ctx, PassStage::DeclarationCollection);
        size_t errs3 = _diagnostics ? _diagnostics->error_count() : 0;
        LOG_DEBUG(LogComponent::LSP, "LSP Stage 3 (DeclarationCollection): {} -> errs={}", s3, errs3 - errs);

        // Stage 4: Type Resolution (resolve type annotations, sync struct fields)
        bool s4 = pipeline->run_stage(ctx, PassStage::TypeResolution);
        size_t errs4 = _diagnostics ? _diagnostics->error_count() : 0;
        LOG_DEBUG(LogComponent::LSP, "LSP Stage 4 (TypeResolution): {} -> errs={}", s4, errs4 - errs3);

        // Stage 5: Semantic Analysis (directive processing, function body type checking)
        bool s5 = pipeline->run_stage(ctx, PassStage::SemanticAnalysis);
        size_t errs5 = _diagnostics ? _diagnostics->error_count() : 0;
        LOG_DEBUG(LogComponent::LSP, "LSP Stage 5 (SemanticAnalysis): {} -> errs={}", s5, errs5 - errs4);

        // Stage 6: Specialization (monomorphization, generic expression resolution)
        // This stage is purely AST-level and doesn't need CodeGenerator.
        try
        {
            bool s6 = pipeline->run_stage(ctx, PassStage::Specialization);
            size_t errs6 = _diagnostics ? _diagnostics->error_count() : 0;
            LOG_DEBUG(LogComponent::LSP, "LSP Stage 6 (Specialization): {} -> errs={}", s6, errs6 - errs5);
        }
        catch (const std::exception &e)
        {
            LOG_DEBUG(LogComponent::LSP, "LSP Stage 6 exception (non-fatal): {}", e.what());
        }
        catch (...)
        {
            LOG_DEBUG(LogComponent::LSP, "LSP Stage 6 exception (non-fatal, unknown)");
        }

        // Stages 7-8 (CodegenPreparation + IRGeneration) are intentionally SKIPPED.
        // The LSP analyzes files individually with a fresh CompilerInstance per file.
        // IR generation requires the full LLVM module populated with ALL imported
        // modules' globals, functions, and types (via generate_imported_ir in
        // topological order). The LSP doesn't do this, so codegen produces false
        // errors like E0607 "Unknown identifier" for any cross-module global or
        // function that exists in an imported module but wasn't generated in the
        // LLVM module. Stages 3-6 already provide all meaningful semantic
        // diagnostics (type errors, unknown types, undeclared variables, etc.).

        // Always return true — diagnostics are accumulated in DiagEmitter regardless
        // of whether individual passes succeed. The LSP will read them via convertDiagnostics.
        return true;
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
