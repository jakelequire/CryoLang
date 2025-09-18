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
            report_error("Failed to create file object for: " + source_file);
            return false;
        }

        if (!file->load())
        {
            report_error("Failed to load source file: " + source_file);
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
            report_error("Failed to create in-memory file");
            return false;
        }

        return parse_source_from_file(std::move(file));
    }

    bool CompilerInstance::parse_source_from_file(std::unique_ptr<File> file)
    {
        reset_state();

        try
        {
            // Phase 1: Create lexer with the file
            _lexer = std::make_unique<Lexer>(std::move(file));

            // Phase 2: Create parser with lexer and AST context
            _parser = std::make_unique<Parser>(std::move(_lexer), *_ast_context);

            // Phase 3: Parse the program
            if (!parse())
            {
                report_error("Parsing failed");
                return false;
            }

            // Phase 4: Basic validation
            if (!_ast_root)
            {
                report_error("No AST generated");
                return false;
            }

            if (_debug_mode)
            {
                std::cout << "=== Compilation successful ===" << std::endl;
                print_ast();
            }

            return true;
        }
        catch (const std::exception &e)
        {
            report_error(std::string("Compilation exception: ") + e.what());
            return false;
        }
    }

    bool CompilerInstance::tokenize()
    {
        if (_source_file.empty())
        {
            report_error("No source file specified");
            return false;
        }

        auto file = make_file_from_path(_source_file);
        if (!file || !file->load())
        {
            report_error("Failed to load source file");
            return false;
        }

        try
        {
            _lexer = std::make_unique<Lexer>(std::move(file));
            return true;
        }
        catch (const std::exception &e)
        {
            report_error(std::string("Lexer error: ") + e.what());
            return false;
        }
    }

    bool CompilerInstance::parse()
    {
        if (!_parser)
        {
            report_error("Parser not initialized");
            return false;
        }

        try
        {
            _ast_root = _parser->parse_program();
            return _ast_root != nullptr;
        }
        catch (const std::exception &e)
        {
            report_error(std::string("Parser error: ") + e.what());
            return false;
        }
    }

    bool CompilerInstance::analyze()
    {
        // Future: Implement semantic analysis
        if (!_ast_root)
        {
            report_error("No AST available for analysis");
            return false;
        }

        // Placeholder for future semantic analysis
        // - Type checking
        // - Symbol resolution
        // - Control flow analysis
        // - etc.

        return true;
    }

    void CompilerInstance::print_ast(std::ostream &os) const
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

    void CompilerInstance::print_diagnostics(std::ostream &os) const
    {
        if (!_diagnostics.empty())
        {
            os << "=== Diagnostics ===" << std::endl;
            for (const auto &diagnostic : _diagnostics)
            {
                os << diagnostic << std::endl;
            }
            os << "==================" << std::endl;
        }
    }

    void CompilerInstance::clear()
    {
        reset_state();
        _source_file.clear();
        _include_paths.clear();
    }

    void CompilerInstance::reset_state()
    {
        _ast_root.reset();
        _diagnostics.clear();
        _lexer.reset();
        _parser.reset();
    }

    void CompilerInstance::report_error(const std::string &message)
    {
        _diagnostics.push_back("Error: " + message);
        if (_debug_mode)
        {
            std::cerr << "Error: " << message << std::endl;
        }
    }

    void CompilerInstance::report_warning(const std::string &message)
    {
        _diagnostics.push_back("Warning: " + message);
        if (_debug_mode)
        {
            std::cerr << "Warning: " << message << std::endl;
        }
    }

    std::unique_ptr<CompilerInstance> create_compiler_instance()
    {
        return std::make_unique<CompilerInstance>();
    }
}