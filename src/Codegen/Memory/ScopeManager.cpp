#include "Codegen/Memory/ScopeManager.hpp"
#include "Utils/Logger.hpp"

namespace Cryo::Codegen
{
    //===================================================================
    // Construction
    //===================================================================

    ScopeManager::ScopeManager(CodegenContext &ctx)
        : ICodegenComponent(ctx), _destructor_callback(nullptr)
    {
    }

    //===================================================================
    // ScopeGuard Implementation
    //===================================================================

    ScopeManager::ScopeGuard::ScopeGuard(ScopeManager &mgr, llvm::BasicBlock *entry,
                                          llvm::BasicBlock *exit)
        : _mgr(mgr), _exited(false)
    {
        _mgr.enter_scope(entry, exit);
    }

    ScopeManager::ScopeGuard::~ScopeGuard()
    {
        if (!_exited)
        {
            _mgr.exit_scope();
        }
    }

    void ScopeManager::ScopeGuard::exit_early()
    {
        if (!_exited)
        {
            _mgr.exit_scope();
            _exited = true;
        }
    }

    //===================================================================
    // Manual Scope Management
    //===================================================================

    void ScopeManager::enter_scope(llvm::BasicBlock *entry, llvm::BasicBlock *exit)
    {
        _scope_stack.emplace_back(entry, exit);
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "Entered scope (depth: {})", _scope_stack.size());
    }

    void ScopeManager::exit_scope()
    {
        if (_scope_stack.empty())
        {
            LOG_WARN(Cryo::LogComponent::CODEGEN, "exit_scope called with empty scope stack");
            return;
        }

        // Run destructors for this scope
        run_scope_destructors();

        // Pop the scope
        _scope_stack.pop_back();
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "Exited scope (depth: {})", _scope_stack.size());
    }

    size_t ScopeManager::scope_depth() const
    {
        return _scope_stack.size();
    }

    bool ScopeManager::is_global_scope() const
    {
        return _scope_stack.empty();
    }

    ScopeContext &ScopeManager::current_scope()
    {
        if (_scope_stack.empty())
        {
            LOG_ERROR(Cryo::LogComponent::CODEGEN, "current_scope called with empty scope stack");
            throw std::runtime_error("No current scope");
        }
        return _scope_stack.back();
    }

    bool ScopeManager::has_scope() const
    {
        return !_scope_stack.empty();
    }

    //===================================================================
    // Variable Tracking
    //===================================================================

    void ScopeManager::register_local(const std::string &name, llvm::Value *value,
                                       llvm::AllocaInst *alloca)
    {
        if (_scope_stack.empty())
        {
            LOG_WARN(Cryo::LogComponent::CODEGEN, "register_local called with no active scope");
            return;
        }

        ScopeContext &scope = _scope_stack.back();
        scope.local_values[name] = value;
        if (alloca)
        {
            scope.local_allocas[name] = alloca;
        }

        // Also register in the ValueContext for broader lookup
        values().set_value(name, value, alloca);

        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "Registered local '{}' in scope", name);
    }

    llvm::Value *ScopeManager::lookup_local(const std::string &name)
    {
        // Search from innermost to outermost scope
        for (auto it = _scope_stack.rbegin(); it != _scope_stack.rend(); ++it)
        {
            auto found = it->local_values.find(name);
            if (found != it->local_values.end())
            {
                return found->second;
            }
        }

        // Fallback to ValueContext
        return values().get_value(name);
    }

    llvm::AllocaInst *ScopeManager::lookup_alloca(const std::string &name)
    {
        // Search from innermost to outermost scope
        for (auto it = _scope_stack.rbegin(); it != _scope_stack.rend(); ++it)
        {
            auto found = it->local_allocas.find(name);
            if (found != it->local_allocas.end())
            {
                return found->second;
            }
        }

        // Fallback to ValueContext
        return values().get_alloca(name);
    }

    bool ScopeManager::has_local(const std::string &name)
    {
        for (const auto &scope : _scope_stack)
        {
            if (scope.local_values.find(name) != scope.local_values.end())
            {
                return true;
            }
        }
        return values().has_value(name);
    }

    //===================================================================
    // Destructor Management
    //===================================================================

    void ScopeManager::register_destructor(const std::string &var_name, llvm::Value *value,
                                            const std::string &type_name, bool is_heap)
    {
        if (_scope_stack.empty())
        {
            LOG_WARN(Cryo::LogComponent::CODEGEN, "register_destructor called with no active scope");
            return;
        }

        // Only register if the type has a destructor
        if (!has_destructor(type_name))
        {
            return;
        }

        ScopeContext &scope = _scope_stack.back();
        scope.destructors.emplace_back(var_name, value, type_name, is_heap);

        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "Registered destructor for '{}' of type '{}'",
                  var_name, type_name);
    }

    void ScopeManager::run_scope_destructors()
    {
        if (_scope_stack.empty())
            return;

        ScopeContext &scope = _scope_stack.back();

        // Run destructors in reverse order (LIFO)
        for (auto it = scope.destructors.rbegin(); it != scope.destructors.rend(); ++it)
        {
            const DestructorInfo &info = *it;

            if (_destructor_callback)
            {
                _destructor_callback(info.variable_name, info.variable_value,
                                     info.type_name, info.is_heap_allocated);
            }
            else
            {
                LOG_DEBUG(Cryo::LogComponent::CODEGEN,
                          "Would run destructor for '{}' (no callback set)", info.variable_name);
            }
        }
    }

    void ScopeManager::run_all_destructors()
    {
        // Run destructors from innermost to outermost scope
        for (auto scope_it = _scope_stack.rbegin(); scope_it != _scope_stack.rend(); ++scope_it)
        {
            ScopeContext &scope = *scope_it;

            for (auto it = scope.destructors.rbegin(); it != scope.destructors.rend(); ++it)
            {
                const DestructorInfo &info = *it;

                if (_destructor_callback)
                {
                    _destructor_callback(info.variable_name, info.variable_value,
                                         info.type_name, info.is_heap_allocated);
                }
            }
        }
    }

    void ScopeManager::set_destructor_callback(DestructorCallback callback)
    {
        _destructor_callback = std::move(callback);
    }

    bool ScopeManager::has_destructor(const std::string &type_name)
    {
        // Check if the type has a destructor method
        // This searches the symbol table for a destructor

        // Common types without destructors
        static const std::unordered_set<std::string> no_destructor_types = {
            "void", "bool", "i8", "i16", "i32", "i64",
            "u8", "u16", "u32", "u64", "f32", "f64",
            "int", "uint", "float", "double", "char", "string"};

        if (no_destructor_types.count(type_name) > 0)
        {
            return false;
        }

        // Check symbol table for destructor method
        std::string destructor_name = type_name + "::~" + type_name;
        // For now, we'll assume complex types may have destructors
        // The actual check would query the symbol table

        return false; // Conservative default - expand this based on actual type info
    }

    //===================================================================
    // Breakable Context Management
    //===================================================================

    void ScopeManager::push_loop(llvm::BasicBlock *condition, llvm::BasicBlock *body,
                                  llvm::BasicBlock *continue_target, llvm::BasicBlock *break_target)
    {
        _breakable_stack.push(BreakableContext(condition, body, continue_target, break_target));
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "Pushed loop context");
    }

    void ScopeManager::push_switch(llvm::BasicBlock *break_target)
    {
        _breakable_stack.push(BreakableContext(break_target));
        LOG_DEBUG(Cryo::LogComponent::CODEGEN, "Pushed switch context");
    }

    void ScopeManager::pop_breakable()
    {
        if (!_breakable_stack.empty())
        {
            _breakable_stack.pop();
            LOG_DEBUG(Cryo::LogComponent::CODEGEN, "Popped breakable context");
        }
    }

    BreakableContext *ScopeManager::current_breakable()
    {
        if (_breakable_stack.empty())
            return nullptr;
        return &_breakable_stack.top();
    }

    bool ScopeManager::in_loop() const
    {
        if (_breakable_stack.empty())
            return false;
        return _breakable_stack.top().context_type == BreakableContext::Loop;
    }

    bool ScopeManager::in_switch() const
    {
        if (_breakable_stack.empty())
            return false;
        return _breakable_stack.top().context_type == BreakableContext::Switch;
    }

    bool ScopeManager::in_breakable() const
    {
        return !_breakable_stack.empty();
    }

    bool ScopeManager::generate_break()
    {
        BreakableContext *breakable = current_breakable();
        if (!breakable || !breakable->break_block)
        {
            report_error(ErrorCode::E0600_CODEGEN_FAILED, "break statement outside of loop or switch");
            return false;
        }

        // Run destructors for scopes we're exiting
        // (This is a simplified version - full implementation would track scope depth)
        run_scope_destructors();

        // Branch to break target
        builder().CreateBr(breakable->break_block);

        return true;
    }

    bool ScopeManager::generate_continue()
    {
        BreakableContext *breakable = current_breakable();
        if (!breakable || breakable->context_type != BreakableContext::Loop)
        {
            report_error(ErrorCode::E0600_CODEGEN_FAILED, "continue statement outside of loop");
            return false;
        }

        if (!breakable->continue_block)
        {
            report_error(ErrorCode::E0600_CODEGEN_FAILED, "continue target not set");
            return false;
        }

        // Run destructors for scopes we're exiting
        run_scope_destructors();

        // Branch to continue target
        builder().CreateBr(breakable->continue_block);

        return true;
    }

} // namespace Cryo::Codegen
