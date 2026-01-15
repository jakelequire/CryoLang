#include "Codegen/CodegenContext.hpp"
#include "Utils/Logger.hpp"
#include "Utils/SymbolResolutionManager.hpp"
#include <sstream>

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
          _type_mapper(std::make_unique<TypeMapper>(symbols.arena(), llvm_ctx.get_context())),
          _value_context(std::make_unique<ValueContext>()),
          _function_registry(std::make_unique<FunctionRegistry>(symbols, symbols.arena())),
          _intrinsics(std::make_unique<Intrinsics>(llvm_ctx, diagnostics)),
          _diagnostic_builder(diagnostics ? std::make_unique<CodegenDiagnosticBuilder>(diagnostics, "") : nullptr),
          _srm_context(std::make_unique<Cryo::SRM::SymbolResolutionContext>(&symbols.arena())),
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
    // Type/Function Registration
    //===================================================================

    void CodegenContext::register_type(const std::string &name, llvm::Type *type)
    {
        if (!name.empty() && type)
        {
            _types[name] = type;
            LOG_DEBUG(Cryo::LogComponent::CODEGEN, "Registered type: {}", name);
        }
    }

    llvm::Type *CodegenContext::get_type(const std::string &name)
    {
        auto it = _types.find(name);
        return (it != _types.end()) ? it->second : nullptr;
    }

    void CodegenContext::register_function(const std::string &name, llvm::Function *fn)
    {
        if (!name.empty() && fn)
        {
            _functions[name] = fn;
            LOG_DEBUG(Cryo::LogComponent::CODEGEN, "Registered function: {}", name);
        }
    }

    llvm::Function *CodegenContext::get_function(const std::string &name)
    {
        auto it = _functions.find(name);
        return (it != _functions.end()) ? it->second : nullptr;
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

        // Sync SRM context with namespace
        // Parse namespace_ctx (e.g., "std::Runtime") and push to SRM stack
        if (!namespace_ctx.empty() && _srm_context)
        {
            // First, clear any existing namespace stack in SRM
            while (!_srm_context->get_current_namespace_path().empty())
            {
                _srm_context->pop_namespace();
            }

            // Parse the namespace context into parts (split on "::")
            std::vector<std::string> ns_parts;
            size_t start = 0;
            size_t end = 0;
            while ((end = namespace_ctx.find("::", start)) != std::string::npos)
            {
                std::string part = namespace_ctx.substr(start, end - start);
                if (!part.empty())
                {
                    ns_parts.push_back(part);
                }
                start = end + 2; // Skip "::"
            }
            // Add the last part
            if (start < namespace_ctx.length())
            {
                std::string last_part = namespace_ctx.substr(start);
                if (!last_part.empty())
                {
                    ns_parts.push_back(last_part);
                }
            }

            // Push each namespace part to SRM
            for (const auto &ns_part : ns_parts)
            {
                _srm_context->push_namespace(ns_part);
            }

            // Also add the full namespace as an imported namespace
            _srm_context->add_imported_namespace(namespace_ctx);

            LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                      "SRM context synced with namespace: '{}' ({} parts)",
                      namespace_ctx, ns_parts.size());
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
