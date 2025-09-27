#include "Codegen/CodeGenerator.hpp"
#include "Codegen/CodegenVisitor.hpp"
#include "Codegen/TargetConfig.hpp"
#include "AST/ASTNode.hpp"
#include <iostream>
#include <memory>
#include <system_error>

#include "llvm/Support/raw_ostream.h"

namespace Cryo::Codegen
{

    //===================================================================
    // CodeGenerator Implementation
    //===================================================================

    CodeGenerator::CodeGenerator(
        std::unique_ptr<TargetConfig> target_config,
        ASTContext &ast_context,
        SymbolTable &symbol_table) : _ast_context(ast_context), _symbol_table(symbol_table),
                                     _has_errors(false), _debug_enabled(false), _optimization_level(2),
                                     _functions_generated(0), _types_generated(0), _globals_generated(0),
                                     _module_name("cryo_module")
    {
        // Store the target config
        _target_config = std::move(target_config);

        // Initialize LLVM context manager
        _context_manager = std::make_unique<LLVMContextManager>(_module_name);

        // Components will be initialized later
        _visitor = nullptr;
        _optimization_manager = nullptr;
        _module = nullptr;
        _builder = nullptr;
        _target_machine = nullptr;
    }

    CodeGenerator::~CodeGenerator() = default;

    bool CodeGenerator::generate_ir(ProgramNode *program)
    {
        if (!program)
        {
            report_error("Cannot generate IR: null program node");
            return false;
        }

        clear_errors();

        try
        {
            // Initialize LLVM if not already done
            if (!initialize_llvm())
            {
                report_error("Failed to initialize LLVM context");
                return false;
            }

            // Create the CodegenVisitor with the context manager
            if (!_visitor)
            {
                _visitor = std::make_unique<CodegenVisitor>(
                    *_context_manager,
                    _symbol_table);
            }

            // Generate IR by visiting the AST
            program->accept(*_visitor);

            // Perform final IR validation
            return finalize_ir();
        }
        catch (const std::exception &e)
        {
            report_error(std::string("IR generation failed: ") + e.what());
            return false;
        }
    }

    llvm::Module *CodeGenerator::get_module() const
    {
        return _context_manager ? _context_manager->get_module() : nullptr;
    }

    const std::string &CodeGenerator::get_last_error() const
    {
        return _last_error;
    }

    bool CodeGenerator::has_errors() const
    {
        return _has_errors;
    }

    void CodeGenerator::clear_errors()
    {
        _has_errors = false;
        _last_error.clear();
    }

    void CodeGenerator::report_error(const std::string &message)
    {
        _has_errors = true;
        _last_error = message;
    }

    //===================================================================
    // Private Implementation Methods
    //===================================================================

    bool CodeGenerator::initialize_llvm()
    {
        if (_context_manager && _module && _builder)
        {
            return true; // Already initialized
        }

        // Initialize the context manager
        if (!_context_manager->initialize())
        {
            report_error("Failed to initialize LLVM context manager: " + _context_manager->get_last_error());
            return false;
        }

        // Get references to LLVM components from context manager
        _module = _context_manager->get_module();
        _builder = &_context_manager->get_builder();
        _target_machine = _context_manager->get_target_machine();

        return true;
    }

    bool CodeGenerator::finalize_ir()
    {
        if (!_context_manager)
        {
            report_error("Context manager not initialized");
            return false;
        }

        // Verify the generated IR
        if (!_context_manager->verify_module())
        {
            report_error("Generated IR failed verification");
            return false;
        }

        return true;
    }

    void CodeGenerator::set_source_info(const std::string& source_file, const std::string& namespace_context)
    {
        if (_visitor)
        {
            _visitor->set_source_info(source_file, namespace_context);
        }
    }

    bool CodeGenerator::emit_llvm_ir(const std::string& output_path)
    {
        if (!_module)
        {
            report_error("No LLVM module available for IR emission");
            return false;
        }

        try
        {
            std::error_code EC;
            llvm::raw_fd_ostream output_stream(output_path, EC);
            
            if (EC)
            {
                report_error("Failed to open file for IR emission: " + EC.message());
                return false;
            }

            _module->print(output_stream, nullptr);
            output_stream.close();

            return true;
        }
        catch (const std::exception& e)
        {
            report_error("Failed to emit LLVM IR: " + std::string(e.what()));
            return false;
        }
    }

    //===================================================================
    // Factory Functions
    //===================================================================

    std::unique_ptr<CodeGenerator> create_default_codegen(ASTContext &ast_context, SymbolTable &symbol_table)
    {
        // Create a default target config
        auto target_config = std::make_unique<TargetConfig>();

        return std::make_unique<CodeGenerator>(std::move(target_config), ast_context, symbol_table);
    }

} // namespace Cryo::Codegen
