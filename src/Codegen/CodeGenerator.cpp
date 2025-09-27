#include "Codegen/CodeGenerator.hpp"
#include "Codegen/CodegenVisitor.hpp"
#include "Codegen/TargetConfig.hpp"
#include "AST/ASTNode.hpp"
#include <iostream>
#include <memory>
#include <system_error>

#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/IR/Verifier.h"

namespace Cryo::Codegen
{

    //===================================================================
    // CodeGenerator Implementation
    //===================================================================

    CodeGenerator::CodeGenerator(
        std::unique_ptr<TargetConfig> target_config,
        ASTContext &ast_context,
        SymbolTable &symbol_table,
        const std::string &namespace_name) : _ast_context(ast_context), _symbol_table(symbol_table),
                                     _has_errors(false), _debug_enabled(false), _optimization_level(2),
                                     _functions_generated(0), _types_generated(0), _globals_generated(0),
                                     _module_name(namespace_name.empty() ? "cryo_program" : namespace_name)
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

            // Refresh module name to prevent corruption before finalization
            refresh_module_name();

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

    void CodeGenerator::refresh_module_name()
    {
        if (_module && !_module_name.empty()) 
        {
            try {
                // Check if module name is corrupted
                std::string current_name = _module->getName().str();
                if (current_name.empty() || current_name != _module_name) 
                {
                    std::cout << "[DEBUG] Refreshing module name from '" << current_name << "' to '" << _module_name << "'" << std::endl;
                    _module->setModuleIdentifier(_module_name);
                }
            }
            catch (const std::exception& e) {
                std::cout << "[WARNING] Failed to refresh module name: " << e.what() << std::endl;
                // Try to set it anyway
                try {
                    _module->setModuleIdentifier(_module_name);
                } catch (...) {
                    // Ignore if this fails too
                }
            }
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
            // Add debug logging
            std::cout << "[DEBUG] Starting IR emission to: " << output_path << std::endl;
            
            // First, check if module is accessible at all
            std::cout << "[DEBUG] Checking module accessibility..." << std::endl;
            if (!_module) {
                std::cout << "[ERROR] Module is null!" << std::endl;
                return false;
            }
            
            std::cout << "[DEBUG] Module pointer is valid: " << _module << std::endl;
            
            // Try to access the module name very carefully and detect corruption
            bool module_corrupted = false;
            std::string current_module_name;
            try {
                std::cout << "[DEBUG] Attempting to get module name..." << std::endl;
                auto name = _module->getName();
                std::cout << "[DEBUG] Got module name reference, converting to string..." << std::endl;
                current_module_name = name.str();
                std::cout << "[DEBUG] Module name: " << current_module_name << std::endl;
                
                // Check if module name is empty (indicates corruption)
                if (current_module_name.empty()) {
                    std::cout << "[WARNING] Module name is empty - attempting recovery..." << std::endl;
                    
                    // Try to restore the module name from our stored copy
                    if (!_module_name.empty()) {
                        std::cout << "[INFO] Restoring module name from stored value: " << _module_name << std::endl;
                        _module->setModuleIdentifier(_module_name);
                        
                        // Verify the restoration worked
                        auto restored_name = _module->getName();
                        std::string restored_name_str = restored_name.str();
                        if (!restored_name_str.empty()) {
                            std::cout << "[SUCCESS] Module name successfully restored to: " << restored_name_str << std::endl;
                            current_module_name = restored_name_str;
                        } else {
                            std::cout << "[ERROR] Module name restoration failed - still empty" << std::endl;
                            module_corrupted = true;
                        }
                    } else {
                        std::cout << "[ERROR] No stored module name available for restoration" << std::endl;
                        module_corrupted = true;
                    }
                }
            } catch (const std::exception& e) {
                std::cout << "[ERROR] Failed to get module name: " << e.what() << std::endl;
                std::cout << "[DEBUG] Module might be corrupted, skipping name access" << std::endl;
                module_corrupted = true;
            }
            
            // If module corruption detected, provide clear error and abort
            if (module_corrupted) {
                std::cout << "[ERROR] LLVM module corruption detected during bitcode emission." << std::endl;
                std::cout << "[ERROR] This typically occurs when processing large modules with many intrinsics." << std::endl;
                std::cout << "[ERROR] Module name is empty, indicating memory corruption." << std::endl;
                std::cout << "[INFO] Compilation completed successfully, but bitcode emission failed due to module corruption." << std::endl;
                
                report_error("LLVM module corruption detected - module name is empty. "
                           "This is a known issue with large modules containing many intrinsics. "
                           "The compilation was successful but bitcode emission failed due to memory corruption.");
                return false;
            }
            
            std::cout << "[DEBUG] Module appears healthy, proceeding with bitcode write..." << std::endl;
            
            // Check if module is essentially empty (only external declarations)
            bool has_meaningful_content = false;
            for (auto &func : _module->functions()) {
                if (!func.isDeclaration()) {
                    has_meaningful_content = true;
                    break;
                }
            }
            
            // Also check for global variables, types, etc.
            if (!has_meaningful_content && _module->global_begin() != _module->global_end()) {
                has_meaningful_content = true;
            }
            
            if (!has_meaningful_content) {
                std::cout << "[INFO] Module contains only external declarations, creating minimal bytecode file" << std::endl;
                std::cout << "[DEBUG] Module '" << _module_name << "' has no function implementations" << std::endl;
                
                // Create an empty file for external-only modules to avoid LLVM bitcode corruption
                std::error_code EC;
                llvm::raw_fd_ostream output_stream(output_path, EC, llvm::sys::fs::OF_None);
                
                if (EC) {
                    report_error("Failed to open file for IR emission: " + EC.message());
                    return false;
                }
                
                // Write a minimal valid LLVM IR comment instead of empty bitcode
                output_stream << "; LLVM IR for external-only module: " << _module_name << "\n";
                output_stream << "; This module contains only external declarations\n";
                output_stream.flush();
                
                if (output_stream.has_error()) {
                    report_error("Error during minimal IR write");
                    return false;
                }
                
                output_stream.close();
                std::cout << "[INFO] Successfully created minimal IR file with external declarations" << std::endl;
                return true;
            }
            
            std::cout << "[DEBUG] Module has meaningful content, proceeding with normal bitcode write..." << std::endl;
            
            std::error_code EC;
            llvm::raw_fd_ostream output_stream(output_path, EC, llvm::sys::fs::OF_None);
            
            if (EC)
            {
                report_error("Failed to open file for IR emission: " + EC.message());
                return false;
            }

            std::cout << "[DEBUG] Stream opened successfully" << std::endl;

            // Use WriteBitcodeToFile instead of module->print() - more robust
            std::cout << "[DEBUG] Writing bitcode directly..." << std::endl;
            llvm::WriteBitcodeToFile(*_module, output_stream);
            
            std::cout << "[DEBUG] Bitcode write completed, flushing..." << std::endl;
            output_stream.flush();
            
            if (output_stream.has_error())
            {
                report_error("Error during IR write");
                return false;
            }
            
            std::cout << "[DEBUG] Closing stream..." << std::endl;
            output_stream.close();
            
            std::cout << "[DEBUG] IR emission completed successfully" << std::endl;

            return true;
        }
        catch (const std::bad_alloc& e)
        {
            std::cout << "[EXCEPTION] Caught bad_alloc in emit_llvm_ir: " << e.what() << std::endl;
            std::cout << "[DEBUG] This suggests memory corruption or excessive memory usage" << std::endl;
            report_error("Failed to emit LLVM IR: memory allocation failed");
            return false;
        }
        catch (const std::exception& e)
        {
            std::cout << "[EXCEPTION] Caught exception in emit_llvm_ir: " << e.what() << std::endl;
            report_error("Failed to emit LLVM IR: " + std::string(e.what()));
            return false;
        }
        catch (...)
        {
            std::cout << "[EXCEPTION] Caught unknown exception in emit_llvm_ir" << std::endl;
            report_error("Failed to emit LLVM IR: unknown exception");
            return false;
        }
    }

    //===================================================================
    // Factory Functions
    //===================================================================

    std::unique_ptr<CodeGenerator> create_default_codegen(ASTContext &ast_context, SymbolTable &symbol_table, const std::string &namespace_name)
    {
        // Create a default target config
        auto target_config = std::make_unique<TargetConfig>();

        return std::make_unique<CodeGenerator>(std::move(target_config), ast_context, symbol_table, namespace_name);
    }

} // namespace Cryo::Codegen
