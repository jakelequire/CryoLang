#include "Compiler/CompilerInstance.hpp"
#include "Utils/file.hpp"
#include <iostream>
#include <fstream>
#include <filesystem>

namespace Cryo
{
    CompilerInstance::CompilerInstance()
        : _debug_mode(false)
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
                _diagnostic_manager->report_error(DiagnosticID::Unknown, DiagnosticCategory::Semantic,
                                                  "Analysis failed", SourceRange{}, file_path);
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
        if (!_ast_root)
        {
            _diagnostic_manager->report_error(DiagnosticID::Unknown, DiagnosticCategory::Semantic,
                                              "No AST available for analysis", SourceRange{}, _source_file);
            return false;
        }

        try
        {
            // Phase 1: Type checking
            _type_checker->check_program(*_ast_root);

            // Check if type checking found errors
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

            // Phase 2: Basic symbol table population (for legacy compatibility)
            populate_symbol_table(_ast_root.get());

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
        populate_symbol_table_with_scope(node, _symbol_table.get(), "Global");
    }

    void CompilerInstance::populate_symbol_table_with_scope(ASTNode *node, SymbolTable *current_scope, const std::string &scope_name)
    {
        if (!node || !current_scope)
            return;

        // Handle function declarations
        if (auto func_decl = dynamic_cast<FunctionDeclarationNode *>(node))
        {
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

            // Add function to current (global) scope with proper Type
            current_scope->declare_symbol(func_decl->name(), SymbolKind::Function,
                                          func_decl->location(), function_type, scope_name);

            // Recurse into function body with function name as new scope
            if (func_decl->body())
            {
                populate_symbol_table_with_scope(func_decl->body(), current_scope, func_decl->name());
            }
        }
        // Handle struct declarations
        else if (auto struct_decl = dynamic_cast<StructDeclarationNode *>(node))
        {
            // Get or create struct type
            Type *struct_type = _ast_context->types().get_struct_type(struct_decl->name());

            // Add struct to symbol table as a Type symbol
            current_scope->declare_symbol(struct_decl->name(), SymbolKind::Type,
                                          struct_decl->location(), struct_type, scope_name);
        }
        // Handle variable declarations
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

        std::string signature = "(";

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
        // Parse runtime.cryo file to load extern function declarations
        std::string runtime_cryo_path = "./runtime/runtime.cryo";
        
        // Check if runtime.cryo exists in the current directory
        if (!std::filesystem::exists(runtime_cryo_path))
        {
            std::cerr << "runtime.cryo not found, falling back to empty standard library" << std::endl;
            return;
        }

        try
        {
            // Read the runtime.cryo file
            std::ifstream file(runtime_cryo_path);
            if (!file.is_open())
            {
                std::cerr << "Could not open runtime file: " << runtime_cryo_path << std::endl;
                return;
            }

            std::string content((std::istreambuf_iterator<char>(file)),
                               std::istreambuf_iterator<char>());
            file.close();

            // Create a File object for the Lexer using the factory function
            auto file_obj = make_file_from_string("./runtime/runtime.cryo", content);
            
            // Create lexer and parser
            auto lexer = std::make_unique<Lexer>(std::move(file_obj));
            Parser parser(std::move(lexer), *_ast_context);

            auto program = parser.parse_program();
            if (!program)
            {
                std::cerr << "Failed to parse runtime file: " << runtime_cryo_path << std::endl;
                return;
            }

            // Process extern blocks in the runtime file
            for (const auto& node : program->statements())
            {
                if (auto extern_block = dynamic_cast<ExternBlockNode*>(node.get()))
                {
                    // Process each function declaration in the extern block
                    for (const auto& func_decl : extern_block->function_declarations())
                    {
                        if (auto func = dynamic_cast<FunctionDeclarationNode*>(func_decl.get()))
                        {
                            // Build function signature
                            std::string signature = "(" + func->return_type_annotation() + ")";
                            if (!func->parameters().empty())
                            {
                                signature = "(";
                                for (size_t i = 0; i < func->parameters().size(); ++i)
                                {
                                    if (i > 0) signature += ", ";
                                    signature += func->parameters()[i]->type_annotation();
                                }
                                signature += ") -> " + func->return_type_annotation();
                            }
                            else
                            {
                                signature = "() -> " + func->return_type_annotation();
                            }

                            // Register the function in the symbol table with runtime namespace scope
                            std::string runtime_namespace = "Std::Runtime"; // Use the same namespace as in runtime.cryo
                            _symbol_table->declare_builtin_function(func->name(), signature, _ast_context->types(), runtime_namespace);
                        }
                    }
                }
            }

            std::cout << "Standard library initialized from runtime.cryo" << std::endl;
        }
        catch (const std::exception& e)
        {
            std::cerr << "Error parsing runtime.cryo: " << e.what() << std::endl;
        }
    }

    std::unique_ptr<CompilerInstance> create_compiler_instance()
    {
        return std::make_unique<CompilerInstance>();
    }
}