#include "Codegen/ValueContext.hpp"
#include <iostream>

namespace Cryo::Codegen
{
    void ValueContext::enter_scope(const std::string &scope_name)
    {
        _scope_stack.emplace_back(scope_name);
    }

    void ValueContext::exit_scope()
    {
        if (!_scope_stack.empty())
        {
            _scope_stack.pop_back();
        }
    }

    void ValueContext::set_value(const std::string &name, llvm::Value *value, llvm::AllocaInst *alloca)
    {
        auto &current_scope = get_current_scope();
        current_scope.values[name] = value;
        if (alloca)
        {
            current_scope.allocas[name] = alloca;
        }
    }

    llvm::Value *ValueContext::get_value(const std::string &name)
    {
        auto result = search_scopes(name);
        return result.first;
    }

    llvm::AllocaInst *ValueContext::get_alloca(const std::string &name)
    {
        auto result = search_scopes(name);
        return result.second;
    }

    bool ValueContext::has_value(const std::string &name)
    {
        return get_value(name) != nullptr;
    }

    const std::unordered_map<std::string, llvm::Value *> &ValueContext::get_current_scope_values()
    {
        return get_current_scope().values;
    }

    void ValueContext::set_global_value(const std::string &name, llvm::Value *value)
    {
        _global_values[name] = value;
    }

    llvm::Value *ValueContext::get_global_value(const std::string &name)
    {
        auto it = _global_values.find(name);
        return (it != _global_values.end()) ? it->second : nullptr;
    }

    bool ValueContext::has_global_value(const std::string &name)
    {
        return _global_values.find(name) != _global_values.end();
    }

    std::string ValueContext::create_temp_name(const std::string &prefix)
    {
        return prefix + std::to_string(_temp_counter++);
    }

    std::string ValueContext::register_temp_value(llvm::Value *value, const std::string &prefix)
    {
        std::string temp_name = create_temp_name(prefix);
        set_value(temp_name, value);
        return temp_name;
    }

    bool ValueContext::is_lvalue(llvm::Value *value)
    {
        return llvm::isa<llvm::AllocaInst>(value) ||
               llvm::isa<llvm::GlobalVariable>(value);
    }

    llvm::AllocaInst *ValueContext::get_value_alloca(llvm::Value *value)
    {
        return llvm::dyn_cast<llvm::AllocaInst>(value);
    }

    void ValueContext::mark_constant(const std::string &name)
    {
        _constants[name] = true;
    }

    bool ValueContext::is_constant(const std::string &name)
    {
        auto it = _constants.find(name);
        return (it != _constants.end()) && it->second;
    }

    void ValueContext::print_scope_stack(std::ostream &os) const
    {
        os << "Scope Stack (depth: " << _scope_stack.size() << "):\n";
        for (size_t i = 0; i < _scope_stack.size(); ++i)
        {
            const auto &scope = _scope_stack[i];
            os << "  [" << i << "] " << scope.scope_name << " (" << scope.values.size() << " values)\n";
        }
    }

    std::string ValueContext::get_current_scope_name() const
    {
        if (_scope_stack.empty())
        {
            return "";
        }
        return _scope_stack.back().scope_name;
    }

    size_t ValueContext::get_total_value_count() const
    {
        size_t count = _global_values.size();
        for (const auto &scope : _scope_stack)
        {
            count += scope.values.size();
        }
        return count;
    }

    ValueContext::ScopeFrame &ValueContext::get_current_scope()
    {
        if (_scope_stack.empty())
        {
            static ScopeFrame global_frame("global");
            return global_frame;
        }
        return _scope_stack.back();
    }

    std::pair<llvm::Value *, llvm::AllocaInst *> ValueContext::search_scopes(const std::string &name)
    {
        for (auto it = _scope_stack.rbegin(); it != _scope_stack.rend(); ++it)
        {
            auto value_it = it->values.find(name);
            if (value_it != it->values.end())
            {
                llvm::AllocaInst *alloca = nullptr;
                auto alloca_it = it->allocas.find(name);
                if (alloca_it != it->allocas.end())
                {
                    alloca = alloca_it->second;
                }
                return {value_it->second, alloca};
            }
        }

        auto global_it = _global_values.find(name);
        if (global_it != _global_values.end())
        {
            return {global_it->second, nullptr};
        }

        return {nullptr, nullptr};
    }

} // namespace Cryo::Codegen
