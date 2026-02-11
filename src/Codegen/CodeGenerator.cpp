#include "Codegen/CodeGenerator.hpp"
#include "Codegen/CodegenVisitor.hpp"
#include "Codegen/TargetConfig.hpp"
#include "AST/ASTNode.hpp"
#include "Utils/Logger.hpp"
#include "Diagnostics/Diag.hpp"
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
        const std::string &namespace_name,
        Cryo::DiagEmitter *diagnostics) : _ast_context(ast_context), _symbol_table(symbol_table), _diagnostics(diagnostics),
                                        _has_errors(false), _debug_enabled(false), _stdlib_compilation_mode(false), _optimization_level(2),
                                        _functions_generated(0), _types_generated(0), _globals_generated(0),
                                        _module_name(namespace_name.empty() ? "cryo_program" : namespace_name)
    {
        // Store the target config
        _target_config = std::move(target_config);

        // Create owned context manager
        _owned_context_manager = std::make_unique<LLVMContextManager>(_module_name);
        _context_manager = _owned_context_manager.get();

        // Components will be initialized later
        _visitor = nullptr;
        _optimization_manager = nullptr;
        _module = nullptr;
        _builder = nullptr;
        _target_machine = nullptr;
    }

    CodeGenerator::CodeGenerator(
        std::unique_ptr<TargetConfig> target_config,
        ASTContext &ast_context, SymbolTable &symbol_table,
        std::shared_ptr<LLVMContextManager> shared_context_manager,
        const std::string &namespace_name, Cryo::DiagEmitter *diagnostics)
        : _ast_context(ast_context), _symbol_table(symbol_table), _diagnostics(diagnostics),
          _has_errors(false), _debug_enabled(false), _stdlib_compilation_mode(false),
          _optimization_level(2), _functions_generated(0), _types_generated(0),
          _globals_generated(0),
          _module_name(namespace_name.empty() ? "cryo_program" : namespace_name)
    {
        _target_config = std::move(target_config);
        _shared_context_manager = shared_context_manager;
        _context_manager = _shared_context_manager.get();
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
                    _symbol_table,
                    _diagnostics);

                // Apply stdlib compilation mode to the newly created visitor
                if (_stdlib_compilation_mode)
                {
                    _visitor->set_stdlib_compilation_mode(true);
                }

                // Apply any pending source info that was set before visitor creation
                if (!_pending_source_file.empty() || !_pending_namespace_context.empty())
                {
                    _visitor->set_source_info(_pending_source_file, _pending_namespace_context);
                }
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

    bool CodeGenerator::generate_imported_ir(ProgramNode *program, const std::string &module_namespace)
    {
        if (!program || !_visitor)
        {
            report_error("Cannot generate imported IR: null program or visitor not initialized");
            return false;
        }

        try
        {
            // Save and set namespace context for the imported module.
            // The multi-pass visitor processes namespace declarations in Pass 4, but
            // struct types and method bodies need the correct namespace in Pass 1/3.
            std::string saved_source_file = _pending_source_file;
            std::string saved_namespace = _pending_namespace_context;

            if (!module_namespace.empty())
            {
                _visitor->set_source_info(_pending_source_file, module_namespace);
            }

            // Visit the imported AST to generate function bodies and struct methods.
            // Types and forward declarations should already exist from earlier passes.
            program->accept(*_visitor);

            // Restore the main module's namespace context
            _visitor->set_source_info(saved_source_file, saved_namespace);

            // Re-validate after adding new function bodies
            return finalize_ir();
        }
        catch (const std::exception &e)
        {
            report_error(std::string("Imported IR generation failed: ") + e.what());
            return false;
        }
    }

    llvm::Module *CodeGenerator::get_module() const
    {
        return _context_manager ? _context_manager->get_module() : nullptr;
    }

    CodegenVisitor *CodeGenerator::get_visitor() const
    {
        return _visitor.get();
    }

    LLVMContextManager *CodeGenerator::get_context_manager() const
    {
        return _context_manager;
    }

    bool CodeGenerator::ensure_visitor_initialized()
    {
        if (_visitor)
        {
            return true; // Already initialized
        }

        try
        {
            // Initialize LLVM if not already done
            if (!initialize_llvm())
            {
                report_error("Failed to initialize LLVM context for visitor initialization");
                return false;
            }

            // Create the CodegenVisitor with the context manager
            _visitor = std::make_unique<CodegenVisitor>(
                *_context_manager,
                _symbol_table,
                _diagnostics);

            // Apply stdlib compilation mode to the newly created visitor
            if (_stdlib_compilation_mode)
            {
                _visitor->set_stdlib_compilation_mode(true);
            }

            // Apply any pending source info that was set before visitor creation
            if (!_pending_source_file.empty() || !_pending_namespace_context.empty())
            {
                _visitor->set_source_info(_pending_source_file, _pending_namespace_context);
            }

            return true;
        }
        catch (const std::exception &e)
        {
            report_error(std::string("Failed to initialize visitor: ") + e.what());
            return false;
        }
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
        // Emit to central diagnostic system
        if (_diagnostics)
        {
            _diagnostics->emit(Diag::error(ErrorCode::E0600_CODEGEN_FAILED, message));
        }
        LOG_ERROR(Cryo::LogComponent::CODEGEN, "CodeGenerator error: {}", message);
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

        // Initialize the context manager (no-op if already initialized in shared mode)
        if (!_context_manager->initialize())
        {
            report_error("Failed to initialize LLVM context manager: " + _context_manager->get_last_error());
            return false;
        }

        // In shared mode, create a new module within the shared context
        if (_shared_context_manager)
        {
            _module = _context_manager->create_module(_module_name);
        }
        else
        {
            _module = _context_manager->get_module();
        }

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

        // Verify the generated IR with detailed error reporting
        std::string verification_errors;

        // CRITICAL: Before verification, fix any unterminated basic blocks that might have been missed
        auto active_module = _context_manager->get_module();
        if (active_module)
        {
            std::vector<std::pair<llvm::BasicBlock *, llvm::Function *>> unterminated_blocks;

            for (llvm::Function &func : *active_module)
            {
                for (llvm::BasicBlock &bb : func)
                {
                    if (!bb.getTerminator())
                    {
                        unterminated_blocks.push_back({&bb, &func});
                    }
                }
            }

            if (!unterminated_blocks.empty())
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN, "Found {} unterminated blocks in module before verification, fixing...",
                          unterminated_blocks.size());

                auto &builder = _context_manager->get_builder();

                for (auto &[bb, func] : unterminated_blocks)
                {
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN, "Fixing unterminated block '{}' in function '{}'",
                              bb->getName().str(), func->getName().str());

                    // Position builder at the end of the unterminated block
                    builder.SetInsertPoint(bb);

                    // Add an unreachable terminator to prevent verification errors
                    // This is safe because an unterminated block means the control flow
                    // should never reach this point in a well-formed program
                    builder.CreateUnreachable();
                }

                LOG_DEBUG(Cryo::LogComponent::CODEGEN, "Fixed all {} unterminated blocks in module", unterminated_blocks.size());
            }
        }

        if (!_context_manager->verify_module_with_details("", verification_errors))
        {
            // Build error message with context
            std::string full_message = "Generated IR failed verification in module '" + _module_name + "'";
            std::string context_info = "";
            ASTNode* current_node = nullptr;

            if (_visitor && _visitor->get_current_node())
            {
                current_node = _visitor->get_current_node();

                // Get additional context about what was being processed
                switch (current_node->kind())
                {
                case NodeKind::FunctionDeclaration:
                    context_info = "while generating function";
                    break;
                case NodeKind::StructDeclaration:
                    context_info = "while generating struct";
                    break;
                case NodeKind::VariableDeclaration:
                    context_info = "while generating variable declaration";
                    break;
                case NodeKind::CallExpression:
                    context_info = "while generating function call";
                    break;
                case NodeKind::BinaryExpression:
                    context_info = "while generating binary expression";
                    break;
                default:
                    context_info = "while generating AST node";
                    break;
                }
            }

            if (!context_info.empty())
            {
                full_message += " " + context_info;
            }
            full_message += ": " + verification_errors;

            // Report the detailed LLVM verification error
            if (_diagnostics)
            {
                auto diag = Diag::error(ErrorCode::E0601_LLVM_ERROR, full_message);
                if (current_node)
                {
                    diag.at(current_node);
                }
                _diagnostics->emit(std::move(diag));
            }

            report_error("Generated IR failed verification");
            return false;
        }

        return true;
    }

    void CodeGenerator::set_source_info(const std::string &source_file, const std::string &namespace_context)
    {
        // Store source info for later application when visitor is created
        _pending_source_file = source_file;
        _pending_namespace_context = namespace_context;

        // Apply immediately if visitor already exists
        if (_visitor)
        {
            _visitor->set_source_info(source_file, namespace_context);
        }
    }

    void CodeGenerator::set_stdlib_compilation_mode(bool enable)
    {
        _stdlib_compilation_mode = enable;
        if (_visitor)
        {
            _visitor->set_stdlib_compilation_mode(enable);
        }
    }

    void CodeGenerator::refresh_module_name()
    {
        if (!_module)
        {
            LOG_DEBUG(Cryo::LogComponent::CODEGEN, "refresh_module_name: No module available");
            return;
        }

        if (_module_name.empty())
        {
            LOG_DEBUG(Cryo::LogComponent::CODEGEN, "refresh_module_name: No stored module name");
            return;
        }

        try
        {
            // Verify module is in a valid state first
            if (!_module->getName().data())
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN, "refresh_module_name: Module name data is null, skipping refresh");
                return;
            }

            // Check if module name is corrupted
            std::string current_name = _module->getName().str();
            if (current_name.empty() || current_name != _module_name)
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN, "Refreshing module name from '{}' to '{}'", current_name, _module_name);

                // For modules with complex import graphs or large symbol tables, skip refresh to prevent corruption
                if (_module_name.find("Intrinsics") != std::string::npos ||
                    _module_name.find("stdio") != std::string::npos ||
                    current_name.empty())
                {
                    LOG_DEBUG(Cryo::LogComponent::CODEGEN, "Skipping refresh for complex module '{}' to prevent memory corruption", _module_name);
                    return;
                }

                _module->setModuleIdentifier(_module_name);

                // Verify it worked
                std::string new_name = _module->getName().str();
                LOG_DEBUG(Cryo::LogComponent::CODEGEN, "Module name after refresh: '{}'", new_name);
            }
        }
        catch (const std::bad_alloc &e)
        {
            LOG_WARN(Cryo::LogComponent::CODEGEN, "Memory allocation failed during module name refresh - skipping to prevent crash");
            // Force early return to prevent further operations on potentially corrupted module
            return;
        }
        catch (const std::exception &e)
        {
            LOG_WARN(Cryo::LogComponent::CODEGEN, "Failed to refresh module name: {} - attempting to continue", e.what());
        }
        catch (...)
        {
            LOG_WARN(Cryo::LogComponent::CODEGEN, "Unknown error during module name refresh - attempting to continue");
        }
    }

    bool CodeGenerator::emit_llvm_ir(const std::string &output_path)
    {
        // MULTI-MODULE FIX: Use main module for emission, not active module
        llvm::Module *target_module = _context_manager ? _context_manager->get_main_module() : nullptr;
        if (!target_module)
        {
            // Fallback to active module if no main module is set
            target_module = _module;
        }

        if (!target_module)
        {
            report_error("No LLVM module available for IR emission");
            return false;
        }

        try
        {
            // Refresh module name before emission
            refresh_module_name();

            // Generate binary bytecode (.bc) file
            std::error_code EC;
            llvm::raw_fd_ostream bc_output_stream(output_path, EC, llvm::sys::fs::OF_None);

            if (EC)
            {
                report_error("Failed to open file for bytecode emission: " + EC.message());
                return false;
            }

            // Write binary bytecode
            llvm::WriteBitcodeToFile(*target_module, bc_output_stream);

            bc_output_stream.flush();

            if (bc_output_stream.has_error())
            {
                report_error("Error during bytecode write: " + bc_output_stream.error().message());
                return false;
            }

            bc_output_stream.close();

            // Generate human-readable LLVM IR (.ll) file
            std::string ll_output_path = output_path;

            // Replace .bc extension with .ll
            size_t pos = ll_output_path.find_last_of('.');
            if (pos != std::string::npos && ll_output_path.substr(pos) == ".bc")
            {
                ll_output_path = ll_output_path.substr(0, pos) + ".ll";
            }
            else
            {
                // If no .bc extension found, just append .ll
                ll_output_path += ".ll";
            }

            std::error_code ll_EC;
            llvm::raw_fd_ostream ll_output_stream(ll_output_path, ll_EC, llvm::sys::fs::OF_None);

            if (ll_EC)
            {
                report_error("Failed to open file for LLVM IR emission: " + ll_EC.message());
                return false;
            }

            // Write human-readable LLVM IR
            target_module->print(ll_output_stream, nullptr);

            ll_output_stream.flush();

            if (ll_output_stream.has_error())
            {
                report_error("Error during LLVM IR write");
                return false;
            }

            ll_output_stream.close();

            return true;
        }
        catch (const std::bad_alloc &e)
        {
            report_error("Failed to emit LLVM IR: memory allocation failed");
            return false;
        }
        catch (const std::exception &e)
        {
            report_error("Failed to emit LLVM IR: " + std::string(e.what()));
            return false;
        }
        catch (...)
        {
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

        return std::make_unique<CodeGenerator>(std::move(target_config), ast_context, symbol_table, namespace_name, nullptr);
    }

    std::unique_ptr<CodeGenerator> create_default_codegen(ASTContext &ast_context, SymbolTable &symbol_table,
                                                          const std::string &namespace_name, DiagEmitter *diagnostics)
    {
        // Create a default target config
        auto target_config = std::make_unique<TargetConfig>();

        return std::make_unique<CodeGenerator>(std::move(target_config), ast_context, symbol_table, namespace_name, diagnostics);
    }

    std::unique_ptr<CodeGenerator> create_shared_context_codegen(
        ASTContext &ast_context, SymbolTable &symbol_table,
        std::shared_ptr<LLVMContextManager> shared_context,
        const std::string &namespace_name, DiagEmitter *diagnostics)
    {
        auto target_config = std::make_unique<TargetConfig>();
        return std::make_unique<CodeGenerator>(
            std::move(target_config), ast_context, symbol_table,
            shared_context, namespace_name, diagnostics);
    }

} // namespace Cryo::Codegen
