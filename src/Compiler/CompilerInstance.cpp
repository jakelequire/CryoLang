#include "Compiler/CompilerInstance.hpp"
#include "Utils/file.hpp"
#include <iostream>
#include <fstream>
#include <filesystem>

namespace Cryo
{
    CompilerInstance::CompilerInstance()
        : _debug_mode(false), _show_ast_before_ir(false), _stdlib_linking_enabled(true), _stdlib_compilation_mode(false), _current_namespace("")
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

        // Create type checker with the AST context's type context
        _type_checker = std::make_unique<TypeChecker>(_ast_context->types());

        // Create monomorphization pass
        _monomorphization_pass = std::make_unique<MonomorphizationPass>();

        // Create template registry
        _template_registry = std::make_unique<TemplateRegistry>();

        // Create module loader - needs to be created after symbol table and template registry
        _module_loader = std::make_unique<ModuleLoader>(*_symbol_table, *_template_registry);

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

        // Create a file object and load it
        auto file = make_file_from_path(source_file);
        if (!file)
        {
            _diagnostic_manager->report_error(DiagnosticID::FileNotFound, DiagnosticCategory::System,
                                              "Failed to create file object for: " + source_file,
                                              SourceRange{}, source_file);
            return false;
        }

        if (!file->load())
        {
            _diagnostic_manager->report_error(DiagnosticID::FileReadError, DiagnosticCategory::System,
                                              "Failed to load source file: " + source_file,
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
            _diagnostic_manager->report_error(DiagnosticID::Unknown, DiagnosticCategory::System,
                                              "Failed to create in-memory file",
                                              SourceRange{}, "source");
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

            // Phase 3: Parse the program
            if (!parse())
            {
                _diagnostic_manager->report_error(DiagnosticID::Unknown, DiagnosticCategory::Parser,
                                                  "Parsing failed", SourceRange{}, file_path);
                return false;
            }

            // Phase 4: Basic validation
            if (!_ast_root)
            {
                _diagnostic_manager->report_error(DiagnosticID::Unknown, DiagnosticCategory::Parser,
                                                  "No AST generated", SourceRange{}, file_path);
                return false;
            }

            // Phase 5: Semantic analysis (including symbol table population)
            if (!analyze())
            {
                std::string namespace_name = _current_namespace.empty() ? "Global" : _current_namespace;
                _diagnostic_manager->report_error(DiagnosticID::Unknown, DiagnosticCategory::Semantic,
                                                  "Analysis failed in namespace '" + namespace_name + "'", SourceRange{}, file_path);
                return false;
            }

            // Phase 6: Generate IR (for testing and integration)
            if (_debug_mode)
            {
                std::cout << "Generating IR..." << std::endl;
            }

            // Print AST if requested before IR generation
            if (_show_ast_before_ir)
            {
                std::cout << "\nGenerated AST:" << std::endl;
                dump_ast();
            }

            if (!generate_ir())
            {
                _diagnostic_manager->report_error(DiagnosticID::Unknown, DiagnosticCategory::CodeGen,
                                                  "IR generation failed", SourceRange{}, file_path);
                return false;
            }

            if (_debug_mode)
            {
                std::cout << "=== Compilation Completed ===" << std::endl;
            }

            // Print any diagnostics that were collected
            print_diagnostics();

            return true;
        }
        catch (const std::exception &e)
        {
            _diagnostic_manager->report_error(DiagnosticID::Unknown, DiagnosticCategory::System,
                                              std::string("Compilation exception: ") + e.what(),
                                              SourceRange{}, _source_file);
            return false;
        }
    }

    bool CompilerInstance::tokenize()
    {
        if (_source_file.empty())
        {
            _diagnostic_manager->report_error(DiagnosticID::Unknown, DiagnosticCategory::System,
                                              "No source file specified", SourceRange{}, "");
            return false;
        }

        auto file = make_file_from_path(_source_file);
        if (!file || !file->load())
        {
            _diagnostic_manager->report_error(DiagnosticID::FileReadError, DiagnosticCategory::System,
                                              "Failed to load source file", SourceRange{}, _source_file);
            return false;
        }

        try
        {
            _lexer = std::make_unique<Lexer>(std::move(file));
            return true;
        }
        catch (const std::exception &e)
        {
            _diagnostic_manager->report_error(DiagnosticID::Unknown, DiagnosticCategory::Lexer,
                                              std::string("Lexer error: ") + e.what(),
                                              SourceRange{}, _source_file);
            return false;
        }
    }

    bool CompilerInstance::parse()
    {
        if (!_parser)
        {
            _diagnostic_manager->report_error(DiagnosticID::Unknown, DiagnosticCategory::Parser,
                                              "Parser not initialized", SourceRange{}, _source_file);
            return false;
        }

        try
        {
            _ast_root = _parser->parse_program();

            // Capture namespace context from parser after successful parsing
            if (_ast_root && !_parser->current_namespace().empty() && _parser->current_namespace() != "Global")
            {
                set_namespace_context(_parser->current_namespace());
                if (_debug_mode)
                {
                    std::cout << "Parsed namespace: " << _parser->current_namespace() << std::endl;
                }
            }

            // Create CodeGenerator now that we have the namespace information
            std::string namespace_for_module = _current_namespace.empty() ? "cryo_program" : _current_namespace;
            _codegen = Cryo::Codegen::create_default_codegen(*_ast_context, *_symbol_table, namespace_for_module, _diagnostic_manager.get());

            // Configure stdlib compilation mode if enabled
            if (_stdlib_compilation_mode)
            {
                _codegen->set_stdlib_compilation_mode(true);
                if (_debug_mode)
                {
                    std::cout << "[DEBUG] Enabled stdlib compilation mode in CodeGenerator" << std::endl;
                }
            }

            if (_debug_mode)
            {
                std::cout << "[DEBUG] Created CodeGenerator with module name: '" << namespace_for_module << "'" << std::endl;
            }

            return _ast_root != nullptr;
        }
        catch (const std::exception &e)
        {
            _diagnostic_manager->report_error(DiagnosticID::Unknown, DiagnosticCategory::Parser,
                                              std::string("Parser error: ") + e.what(),
                                              SourceRange{}, _source_file);
            return false;
        }
    }

    bool CompilerInstance::analyze()
    {
        std::cout << "[CompilerInstance] Starting analysis phase..." << std::endl;

        if (!_ast_root)
        {
            _diagnostic_manager->report_error(DiagnosticID::Unknown, DiagnosticCategory::Semantic,
                                              "No AST available for analysis", SourceRange{}, _source_file);
            return false;
        }

        try
        {
            std::cout << "[CompilerInstance] Phase 0: Symbol table population..." << std::endl;
            // Phase 0: Symbol table population (must happen before type checking)
            populate_symbol_table(_ast_root.get());

            std::cout << "[CompilerInstance] Loading intrinsic symbols into type checker..." << std::endl;
            // Load only newly added intrinsic symbols into type checker
            _type_checker->load_intrinsic_symbols(*_symbol_table);

            std::cout << "[CompilerInstance] Loading user-defined symbols into type checker..." << std::endl;
            // Load user-defined function symbols that were registered during Pass 1
            _type_checker->load_user_symbols(*_symbol_table);

            std::cout << "[CompilerInstance] Phase 1: Type checking..." << std::endl;
            // Phase 1: Type checking
            _type_checker->check_program(*_ast_root);

            std::cout << "[CompilerInstance] Checking for type errors..." << std::endl;
            if (_type_checker->has_errors())
            {
                // Convert type errors to diagnostic manager errors
                for (const auto &type_error : _type_checker->errors())
                {
                    DiagnosticID diag_id = DiagnosticID::Unknown;
                    switch (type_error.kind)
                    {
                    case TypeError::ErrorKind::TypeMismatch:
                        diag_id = DiagnosticID::TypeMismatch;
                        break;
                    case TypeError::ErrorKind::UndefinedVariable:
                        diag_id = DiagnosticID::UndefinedVariable;
                        break;
                    case TypeError::ErrorKind::UndefinedFunction:
                        diag_id = DiagnosticID::UndefinedFunction;
                        break;
                    case TypeError::ErrorKind::RedefinedSymbol:
                        diag_id = DiagnosticID::RedefinedSymbol;
                        break;
                    default:
                        diag_id = DiagnosticID::Unknown;
                        break;
                    }

                    SourceRange range(type_error.location);
                    _diagnostic_manager->report_error(diag_id, DiagnosticCategory::Semantic,
                                                      type_error.message, range, _source_file);
                }

                if (_debug_mode)
                {
                    std::cout << "Type checking failed with " << _type_checker->error_count() << " errors." << std::endl;
                }
                return false;
            }

            // Phase 2: Monomorphization - Generate specialized versions of generic types
            if (_debug_mode)
            {
                std::cout << "Starting monomorphization pass..." << std::endl;
            }

            const auto &required_instantiations = _type_checker->get_required_instantiations();
            if (!required_instantiations.empty())
            {
                bool monomorphization_success = _monomorphization_pass->monomorphize(*_ast_root, required_instantiations, *_template_registry);
                if (!monomorphization_success)
                {
                    _diagnostic_manager->report_error(DiagnosticID::Unknown, DiagnosticCategory::Semantic,
                                                      "Monomorphization failed", SourceRange{}, _source_file);
                    return false;
                }

                if (_debug_mode)
                {
                    std::cout << "Monomorphization completed successfully." << std::endl;
                }
            }
            else
            {
                if (_debug_mode)
                {
                    std::cout << "No generic instantiations required, skipping monomorphization." << std::endl;
                }
            }

            // Phase 3: Future semantic analysis phases would go here
            // - Control flow analysis
            // - Dead code elimination
            // - etc.

            if (_debug_mode)
            {
                std::cout << "Type checking completed successfully." << std::endl;
            }

            return true;
        }
        catch (const std::exception &e)
        {
            _diagnostic_manager->report_error(DiagnosticID::Unknown, DiagnosticCategory::Semantic,
                                              std::string("Analysis error: ") + e.what(),
                                              SourceRange{}, _source_file);
            return false;
        }
    }

    bool CompilerInstance::generate_ir()
    {
        if (!_ast_root)
        {
            _diagnostic_manager->report_error(DiagnosticID::Unknown, DiagnosticCategory::CodeGen,
                                              "No AST available for code generation", SourceRange{}, _source_file);
            return false;
        }

        if (!_codegen)
        {
            _diagnostic_manager->report_error(DiagnosticID::Unknown, DiagnosticCategory::CodeGen,
                                              "CodeGenerator not initialized", SourceRange{}, _source_file);
            return false;
        }

        try
        {
            // Set source file and namespace context before generating IR
            _codegen->set_source_info(_source_file, _current_namespace);

            bool success = _codegen->generate_ir(_ast_root.get());

            if (!success)
            {
                _diagnostic_manager->report_error(DiagnosticID::Unknown, DiagnosticCategory::CodeGen,
                                                  "Code generation failed: " + _codegen->get_last_error(),
                                                  SourceRange{}, _source_file);
            }

            return success;
        }
        catch (const std::exception &e)
        {
            _diagnostic_manager->report_error(DiagnosticID::Unknown, DiagnosticCategory::CodeGen,
                                              std::string("Code generation exception: ") + e.what(),
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
            _diagnostic_manager->report_error(DiagnosticID::Unknown, DiagnosticCategory::System,
                                              "Linker not initialized", SourceRange{}, _source_file);
            return false;
        }

        try
        {
            // Configure linker with runtime and stdlib if linking is enabled
            if (_stdlib_linking_enabled)
            {
                // Add runtime first (contains true main function)
                std::string runtime_path = "./bin/stdlib/runtime.o";
                if (std::filesystem::exists(runtime_path))
                {
                    _linker->add_object_file(runtime_path);
                    if (_debug_mode)
                    {
                        std::cout << "[DEBUG] Added runtime.o for runtime linking: " << runtime_path << std::endl;
                    }
                }
                else
                {
                    std::cerr << "Warning: runtime.o not found at " << runtime_path << ", runtime functions may not link properly" << std::endl;
                }

                // Add libcryo.a to the linker
                std::string libcryo_path = "./bin/stdlib/libcryo.a";
                if (std::filesystem::exists(libcryo_path))
                {
                    _linker->add_object_file(libcryo_path);
                    if (_debug_mode)
                    {
                        std::cout << "[DEBUG] Added libcryo.a for standard library linking: " << libcryo_path << std::endl;
                    }
                }
                else
                {
                    std::cerr << "Warning: libcryo.a not found at " << libcryo_path << ", stdlib functions may not link properly" << std::endl;
                }
            }
            else if (_debug_mode)
            {
                std::cout << "[DEBUG] Standard library linking disabled by --no-std flag" << std::endl;
            }

            // Get generated module and link
            llvm::Module *module = _codegen->get_module();
            if (!module)
            {
                _diagnostic_manager->report_error(DiagnosticID::Unknown, DiagnosticCategory::CodeGen,
                                                  "No LLVM module generated", SourceRange{}, _source_file);
                return false;
            }

            bool success = _linker->link_modules({module}, output_path, target);

            if (!success)
            {
                std::string linker_error = _linker->get_last_error();
                std::cerr << "[DEBUG] Linker error string length: " << linker_error.length() << std::endl;
                std::cerr << "[DEBUG] Linker error content: '" << linker_error << "'" << std::endl;
                std::cerr << "[DEBUG] Full error message will be: 'Linking failed: " << linker_error << "'" << std::endl;

                _diagnostic_manager->report_error(DiagnosticID::Unknown, DiagnosticCategory::System,
                                                  "Linking failed: " + linker_error,
                                                  SourceRange{}, _source_file);
            }

            return success;
        }
        catch (const std::exception &e)
        {
            _diagnostic_manager->report_error(DiagnosticID::Unknown, DiagnosticCategory::System,
                                              std::string("Linking exception: ") + e.what(),
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
        std::cout << "[CompilerInstance] Pass 1: Collecting declarations..." << std::endl;
        collect_declarations_pass(node, _symbol_table.get(), "Global");

        // Pre-register functions in LLVM to prevent forward declaration conflicts
        if (_codegen)
        {
            std::cout << "[CompilerInstance] Ensuring visitor is initialized for function pre-registration..." << std::endl;
            if (_codegen->ensure_visitor_initialized())
            {
                std::cout << "[CompilerInstance] Processing struct declarations before function pre-registration..." << std::endl;
                // First, process struct declarations to ensure TypeMapper has correct type information
                process_struct_declarations_for_preregistration(node);
                
                std::cout << "[CompilerInstance] Pre-registering functions in LLVM module..." << std::endl;
                _codegen->get_visitor()->pre_register_functions_from_symbol_table();
            }
            else
            {
                std::cout << "[WARNING] Failed to initialize CodegenVisitor for function pre-registration" << std::endl;
            }
        }
        else
        {
            std::cout << "[WARNING] CodeGenerator not available for function pre-registration" << std::endl;
        }

        // Pass 2: Process function bodies and other code that can reference the symbols
        std::cout << "[CompilerInstance] Pass 2: Processing function bodies and references..." << std::endl;
        populate_symbol_table_with_scope(node, _symbol_table.get(), "Global");
    }

    void CompilerInstance::collect_declarations_pass(ASTNode *node, SymbolTable *current_scope, const std::string &scope_name)
    {
        if (!node || !current_scope)
            return;

        // Handle function declarations - only collect signature, not body
        if (auto func_decl = dynamic_cast<FunctionDeclarationNode *>(node))
        {
            std::cout << "[CompilerInstance] Pass 1: Found function declaration: " << func_decl->name() << std::endl;
            
            // Create function type from return type and parameters
            std::vector<Type *> param_types;
            for (const auto &param : func_decl->parameters())
            {
                // Parse parameter type from string annotation
                Type *param_type = _ast_context->types().parse_type_from_string(param->type_annotation());
                param_types.push_back(param_type);
            }

            // Parse return type
            Type *return_type = _ast_context->types().parse_type_from_string(func_decl->return_type_annotation());

            // Create function type
            Type *function_type = _ast_context->types().create_function_type(return_type, param_types);

            // Build enhanced signature including generic parameters
            std::string enhanced_signature = build_function_signature(func_decl);

            // Add function to current (global) scope with enhanced display for generics
            current_scope->declare_symbol(func_decl->name(), SymbolKind::Function,
                                          func_decl->location(), function_type, scope_name,
                                          func_decl->generic_parameters().empty() ? "" : enhanced_signature);

            std::cout << "[CompilerInstance] Pass 1: Registered function: " << func_decl->name() << std::endl;

            // Register generic function templates if this function has generic parameters
            if (!func_decl->generic_parameters().empty())
            {
                std::cout << "[CompilerInstance] Registering local generic function template: " << func_decl->name() << std::endl;
                _template_registry->register_function_template(
                    func_decl->name(),
                    func_decl,
                    "Main", // Use "Main" as the module name for local templates
                    ""      // No source file path for main file templates
                );
                std::cout << "[CompilerInstance] Registered local generic function template: " << func_decl->name() << std::endl;
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
                // Parse parameter type from string annotation
                Type *param_type = _ast_context->types().parse_type_from_string(param->type_annotation());
                param_types.push_back(param_type);
            }

            // Parse return type
            Type *return_type = _ast_context->types().parse_type_from_string(intrinsic_decl->return_type_annotation());

            // Create function type
            Type *function_type = _ast_context->types().create_function_type(return_type, param_types);

            // Add intrinsic function to current (global) scope with Intrinsic symbol kind
            current_scope->declare_symbol(intrinsic_decl->name(), SymbolKind::Intrinsic,
                                          intrinsic_decl->location(), function_type, scope_name);

            std::cout << "[CompilerInstance] Registered intrinsic function '" << intrinsic_decl->name() << "' in symbol table" << std::endl;
        }
        // Handle struct declarations
        else if (auto struct_decl = dynamic_cast<StructDeclarationNode *>(node))
        {
            // Get or create struct type
            Type *struct_type = _ast_context->types().get_struct_type(struct_decl->name());

            // Add struct to symbol table as a Type symbol
            current_scope->declare_symbol(struct_decl->name(), SymbolKind::Type,
                                          struct_decl->location(), struct_type, scope_name);

            // Register generic struct templates if this struct has generic parameters
            if (!struct_decl->generic_parameters().empty())
            {
                std::cout << "[CompilerInstance] Registering local generic struct template: " << struct_decl->name() << std::endl;
                _template_registry->register_struct_template(
                    struct_decl->name(),
                    struct_decl,
                    "Main", // Use "Main" as the module name for local templates
                    ""      // No source file path for main file templates
                );
                std::cout << "[CompilerInstance] Registered local generic struct template: " << struct_decl->name() << std::endl;
            }
        }
        // Handle enum declarations
        else if (auto enum_decl = dynamic_cast<EnumDeclarationNode *>(node))
        {
            // Create the enum type during symbol table population for proper type resolution
            std::vector<std::string> variant_names;
            for (const auto& variant : enum_decl->variants()) {
                variant_names.push_back(variant->name());
            }
            
            // Check if this is a simple enum (all variants without associated types)
            bool is_simple_enum = true;
            for (const auto& variant : enum_decl->variants()) {
                if (!variant->associated_types().empty()) {
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
                std::cout << "[CompilerInstance] Registering local generic enum template: " << enum_decl->name() << std::endl;
                _template_registry->register_enum_template(
                    enum_decl->name(),
                    enum_decl,
                    "Main", // Use "Main" as the module name for local templates
                    ""      // No source file path for main file templates
                );
                std::cout << "[CompilerInstance] Registered local generic enum template: " << enum_decl->name() << std::endl;
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
                std::cout << "[CompilerInstance] Registering local generic trait template: " << trait_decl->name() << std::endl;
                _template_registry->register_trait_template(
                    trait_decl->name(),
                    trait_decl,
                    "Main", // Use "Main" as the module name for local templates
                    ""      // No source file path for main file templates
                );
                std::cout << "[CompilerInstance] Registered local generic trait template: " << trait_decl->name() << std::endl;
            }
        }
        // Handle import declarations (process in first pass since they affect symbol resolution)
        else if (auto import_decl = dynamic_cast<ImportDeclarationNode *>(node))
        {
            std::cout << "[CompilerInstance] Processing import: " << import_decl->path() << std::endl;

            // Use the member ModuleLoader instance
            _module_loader->set_stdlib_root("./stdlib");
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
                        std::cout << "[CompilerInstance] Processing namespace alias '" << result.namespace_alias
                                  << "' for module '" << result.module_name << "' with " << result.symbol_map.size() << " symbols" << std::endl;

                        current_scope->register_namespace(result.namespace_alias, result.symbol_map);
                        _imported_namespaces.push_back(result.namespace_alias); // Track for enhanced resolution
                        std::cout << "[CompilerInstance] Registered namespace alias '" << result.namespace_alias << "' with "
                                  << result.symbol_map.size() << " symbols" << std::endl;
                    }
                    else
                    {
                        // Multiple specific imports - add symbols directly to current scope
                        std::cout << "[CompilerInstance] Processing specific symbol imports with " << result.symbol_map.size() << " symbols" << std::endl;

                        for (const auto &[symbol_name, symbol] : result.symbol_map)
                        {
                            // Register each symbol directly in the current scope
                            current_scope->declare_symbol(symbol_name, symbol.kind, symbol.declaration_location, symbol.data_type, scope_name);
                            std::cout << "[CompilerInstance] Registered specific symbol: " << symbol_name << std::endl;
                        }
                    }
                }
                else
                {
                    // For wildcard imports, register the namespace and symbols
                    std::string namespace_name = import_decl->has_alias() ? import_decl->alias() : result.module_name;

                    if (!result.symbol_map.empty())
                    {
                        current_scope->register_namespace(namespace_name, result.symbol_map);
                        _imported_namespaces.push_back(namespace_name); // Track for enhanced resolution
                        std::cout << "[CompilerInstance] Registered namespace '" << namespace_name << "' with "
                                  << result.symbol_map.size() << " symbols" << std::endl;
                    }
                    else
                    {
                        std::cout << "[CompilerInstance] Warning: Import succeeded but no symbols found in " << import_decl->path() << std::endl;
                    }
                }
            }
            else
            {
                std::cout << "[CompilerInstance] Failed to load import '" << import_decl->path() << "': " << result.error_message << std::endl;
            }
        }
        // Handle variable declarations (collect them in first pass too)
        else if (auto var_decl = dynamic_cast<VariableDeclarationNode *>(node))
        {
            // Parse variable type from string annotation
            Type *var_type = _ast_context->types().parse_type_from_string(var_decl->type_annotation());

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
        // Handle variable declarations - skip, already processed in first pass
        else if (auto var_decl = dynamic_cast<VariableDeclarationNode *>(node))
        {
            // Already processed in first pass, nothing to do
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

        std::cout << "Standard library initialized from libcryo" << std::endl;
    }

    void CompilerInstance::inject_auto_imports(SymbolTable *current_scope, const std::string &scope_name)
    {
        // Skip auto-import if we're in stdlib compilation mode to avoid circular dependencies
        if (_stdlib_compilation_mode)
        {
            std::cout << "[CompilerInstance] Skipping auto-imports in stdlib compilation mode" << std::endl;
            return;
        }

        // Skip auto-import if we're compiling the core/types module itself
        if (_source_file.find("core/types.cryo") != std::string::npos ||
            _source_file.find("stdlib/core/types.cryo") != std::string::npos)
        {
            std::cout << "[CompilerInstance] Skipping auto-imports when compiling core/types module" << std::endl;
            return;
        }

        std::cout << "[CompilerInstance] Injecting auto-import: core/types" << std::endl;

        // Use the member ModuleLoader instance
        _module_loader->set_stdlib_root("./stdlib");
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
                std::cout << "[CompilerInstance] Auto-imported core/types: registered namespace '" << result.module_name
                          << "' with " << result.symbol_map.size() << " symbols" << std::endl;
            }
            else
            {
                std::cout << "[CompilerInstance] Warning: Auto-import succeeded but no symbols found in core/types" << std::endl;
            }
        }
        else
        {
            std::cout << "[CompilerInstance] Warning: Failed to auto-import core/types: " << result.error_message << std::endl;
        }
    }

    void CompilerInstance::process_struct_declarations_for_preregistration(ASTNode *node)
    {
        if (!node || !_codegen || !_codegen->get_visitor())
            return;

        std::cout << "[CompilerInstance] Processing struct declarations for pre-registration..." << std::endl;

        // Process the AST to find and register struct declarations with the TypeMapper
        process_struct_declarations_recursive(node);
    }

    void CompilerInstance::process_struct_declarations_recursive(ASTNode *node)
    {
        if (!node)
            return;

        // If this is a struct declaration, process it
        if (auto struct_decl = dynamic_cast<StructDeclarationNode*>(node))
        {
            std::cout << "[CompilerInstance] Pre-processing struct: " << struct_decl->name() << std::endl;
            
            // Visit the struct declaration to register it with TypeMapper
            struct_decl->accept(*_codegen->get_visitor());
        }
        // If this is a program node, process all its statements
        else if (auto program = dynamic_cast<ProgramNode*>(node))
        {
            for (const auto& statement : program->statements())
            {
                process_struct_declarations_recursive(statement.get());
            }
        }
        // If this is a block statement, process its statements
        else if (auto block = dynamic_cast<BlockStatementNode*>(node))
        {
            for (const auto& statement : block->statements())
            {
                process_struct_declarations_recursive(statement.get());
            }
        }
        // For other nodes, we might need to recurse into their children
        // but for now, we're mainly interested in top-level struct declarations
    }

    std::unique_ptr<CompilerInstance> create_compiler_instance()
    {
        return std::make_unique<CompilerInstance>();
    }
}