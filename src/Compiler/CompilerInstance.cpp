#include "Compiler/CompilerInstance.hpp"
#include "Utils/file.hpp"
#include <iostream>
#include <fstream>

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

        // Configure diagnostic manager
        _diagnostic_manager->set_formatter_options(true, true, 2);

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
        // Future: Implement semantic analysis
        if (!_ast_root)
        {
            _diagnostic_manager->report_error(DiagnosticID::Unknown, DiagnosticCategory::Semantic,
                                              "No AST available for analysis", SourceRange{}, _source_file);
            return false;
        }

        // Placeholder for future semantic analysis
        // - Type checking
        // - Symbol resolution
        // - Control flow analysis
        // - etc.

        return true;
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

    std::unique_ptr<CompilerInstance> create_compiler_instance()
    {
        return std::make_unique<CompilerInstance>();
    }
}