#include "Codegen/CodegenContext.hpp"
#include "Utils/Logger.hpp"

namespace Cryo::Codegen
{
    //===================================================================
    // Construction
    //===================================================================

    CodegenContext::CodegenContext(
        LLVMContextManager &llvm_ctx,
        Cryo::SymbolTable &symbols,
        Cryo::DiagnosticManager *diagnostics)
        : _llvm(llvm_ctx),
          _symbols(symbols),
          _diagnostics(diagnostics),
          _type_mapper(std::make_unique<TypeMapper>(llvm_ctx, symbols.get_type_context())),
          _value_context(std::make_unique<ValueContext>()),
          _function_registry(std::make_unique<FunctionRegistry>(symbols, *symbols.get_type_context())),
          _intrinsics(std::make_unique<Intrinsics>(llvm_ctx, diagnostics)),
          _diagnostic_builder(diagnostics ? std::make_unique<CodegenDiagnosticBuilder>(diagnostics, "") : nullptr),
          _srm_context(std::make_unique<Cryo::SRM::SymbolResolutionContext>(symbols.get_type_context())),
          _srm_manager(std::make_unique<Cryo::SRM::SymbolResolutionManager>(_srm_context.get())),
          _current_node(nullptr),
          _current_result(nullptr),
          _has_errors(false)
    {
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "CodegenContext initialized");
    }

    //===================================================================
    // Error Reporting
    //===================================================================

    void CodegenContext::report_error(ErrorCode code, Cryo::ASTNode *node, const std::string &msg)
    {
        _has_errors = true;
        _last_error = msg;

        if (_diagnostic_builder)
        {
            _diagnostic_builder->report_error(code, node, msg);
        }
        else
        {
            LOG_ERROR(Cryo::LogComponent::CODEGEN, "Codegen error: {}", msg);
        }
    }

    void CodegenContext::report_error(ErrorCode code, const std::string &msg)
    {
        _has_errors = true;
        _last_error = msg;

        if (_diagnostic_builder)
        {
            _diagnostic_builder->report_error(code, msg);
        }
        else
        {
            LOG_ERROR(Cryo::LogComponent::CODEGEN, "Codegen error: {}", msg);
        }
    }

    void CodegenContext::clear_errors()
    {
        _has_errors = false;
        _last_error.clear();
    }

    //===================================================================
    // Current State Management
    //===================================================================

    void CodegenContext::set_current_function(std::unique_ptr<FunctionContext> fn)
    {
        _current_function = std::move(fn);
    }

    void CodegenContext::clear_current_function()
    {
        _current_function.reset();
    }

    //===================================================================
    // Value Registration
    //===================================================================

    void CodegenContext::register_value(Cryo::ASTNode *node, llvm::Value *value)
    {
        if (node && value)
        {
            _node_values[node] = value;
        }
    }

    llvm::Value *CodegenContext::get_value(Cryo::ASTNode *node)
    {
        if (!node)
            return nullptr;

        auto it = _node_values.find(node);
        return (it != _node_values.end()) ? it->second : nullptr;
    }

    bool CodegenContext::has_value(Cryo::ASTNode *node)
    {
        return node && _node_values.find(node) != _node_values.end();
    }

    //===================================================================
    // Source Context
    //===================================================================

    void CodegenContext::set_source_info(const std::string &source_file, const std::string &namespace_ctx)
    {
        _source_file = source_file;
        _namespace_context = namespace_ctx;

        // Update diagnostic builder with new source file
        if (_diagnostics)
        {
            _diagnostic_builder = std::make_unique<CodegenDiagnosticBuilder>(_diagnostics, source_file);
        }
    }

    //===================================================================
    // Breakable Context Stack
    //===================================================================

    void CodegenContext::push_breakable(BreakableContext ctx)
    {
        _breakable_stack.push(std::move(ctx));
    }

    void CodegenContext::pop_breakable()
    {
        if (!_breakable_stack.empty())
        {
            _breakable_stack.pop();
        }
    }

    BreakableContext *CodegenContext::current_breakable()
    {
        if (_breakable_stack.empty())
            return nullptr;
        return &_breakable_stack.top();
    }

} // namespace Cryo::Codegen
