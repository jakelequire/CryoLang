#include "Codegen/CodegenContext.hpp"
#include "Diagnostics/Diag.hpp"
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
        Cryo::DiagEmitter *diagnostics)
        : _llvm(llvm_ctx),
          _symbols(symbols),
          _diagnostics(diagnostics),
          _type_mapper(std::make_unique<TypeMapper>(symbols.arena(), llvm_ctx.get_context())),
          _value_context(std::make_unique<ValueContext>()),
          _function_registry(std::make_unique<FunctionRegistry>(symbols, symbols.arena())),
          _intrinsics(std::make_unique<Intrinsics>(llvm_ctx, diagnostics)),
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

    void CodegenContext::emit_diagnostic(Diag diag)
    {
        _has_errors = diag.is_error() || _has_errors;
        if (diag.is_error())
            _last_error = diag.message();

        if (!_diagnostics)
        {
            LOG_ERROR(Cryo::LogComponent::CODEGEN, "Codegen error: {}", diag.message());
            return;
        }

        // Enrich with generic instantiation context
        if (!_current_type_name.empty())
        {
            const std::string &display = _current_type_display_name.empty()
                ? _current_type_name : _current_type_display_name;

            diag.with_note("in instantiation of '" + display + "'");

            // Show the call site as a secondary span with a descriptive label
            // that explains the binding (e.g., "Array<T> instantiated as Array<String> here")
            if (!_instantiation_file.empty())
            {
                // Build a label that shows the generic→concrete binding
                // Extract the base generic name from display (e.g., "Array" from "Array<String>")
                std::string label;
                size_t angle = display.find('<');
                if (angle != std::string::npos)
                {
                    std::string base = display.substr(0, angle);
                    label = base + "<T> instantiated as " + display + " here";
                }
                else
                {
                    label = "'" + display + "' instantiated here";
                }

                Span call_site;
                call_site.file = _instantiation_file;
                call_site.start_line = _instantiation_loc.line();
                call_site.start_col = _instantiation_loc.column();
                call_site.end_line = call_site.start_line;
                call_site.end_col = call_site.start_col + 1;
                call_site.label = label;
                call_site.is_primary = false;
                diag.also_at(std::move(call_site));
            }
            else
            {
                // Fallback: no exact instantiation site, but the error is
                // in a different file from the source context
                auto primary = diag.primary_span();
                if (primary && !_source_file.empty() &&
                    !primary->file.empty() && primary->file != _source_file)
                {
                    Span fallback;
                    fallback.file = _source_file;
                    fallback.start_line = 0;
                    fallback.start_col = 0;
                    fallback.end_line = 0;
                    fallback.end_col = 0;
                    fallback.label = "required from this module";
                    fallback.is_primary = false;
                    diag.also_at(std::move(fallback));
                }
            }
        }
        _diagnostics->emit(std::move(diag));
    }

    void CodegenContext::report_error(ErrorCode code, Cryo::ASTNode *node, const std::string &msg)
    {
        auto diag = Diag::error(code, msg);
        if (node)
        {
            diag.at(node);
        }
        emit_diagnostic(std::move(diag));
    }

    void CodegenContext::report_error(ErrorCode code, const std::string &msg)
    {
        emit_diagnostic(Diag::error(code, msg));
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

    void CodegenContext::unregister_function(llvm::Function *fn)
    {
        if (!fn)
            return;

        // Remove all entries pointing to this function to prevent dangling pointers
        for (auto it = _functions.begin(); it != _functions.end();)
        {
            if (it->second == fn)
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN, "Unregistered function: {}", it->first);
                it = _functions.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    //===================================================================
    // Source Context
    //===================================================================

    void CodegenContext::set_source_info(const std::string &source_file, const std::string &namespace_ctx)
    {
        _source_file = source_file;
        _namespace_context = namespace_ctx;

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
