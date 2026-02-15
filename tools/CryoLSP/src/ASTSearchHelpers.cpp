#include "LSP/ASTSearchHelpers.hpp"

namespace CryoLSP
{

    // ============================================================================
    // DeclarationFinder
    // ============================================================================

    void DeclarationFinder::visit(Cryo::ProgramNode &node)
    {
        for (const auto &child : node.statements())
            if (child && !_result)
                child->accept(*this);
    }

    void DeclarationFinder::visit(Cryo::DeclarationStatementNode &node)
    {
        if (node.declaration() && !_result)
            node.declaration()->accept(*this);
    }

    void DeclarationFinder::visit(Cryo::FunctionDeclarationNode &node)
    {
        if (node.name() == _target)
            _result = &node;
    }

    void DeclarationFinder::visit(Cryo::StructDeclarationNode &node)
    {
        if (node.name() == _target)
            _result = &node;
    }

    void DeclarationFinder::visit(Cryo::ClassDeclarationNode &node)
    {
        if (node.name() == _target)
            _result = &node;
    }

    void DeclarationFinder::visit(Cryo::EnumDeclarationNode &node)
    {
        if (node.name() == _target)
            _result = &node;
    }

    void DeclarationFinder::visit(Cryo::TraitDeclarationNode &node)
    {
        if (node.name() == _target)
            _result = &node;
    }

    void DeclarationFinder::visit(Cryo::TypeAliasDeclarationNode &node)
    {
        if (node.alias_name() == _target)
            _result = &node;
    }

    void DeclarationFinder::visit(Cryo::IntrinsicDeclarationNode &node)
    {
        if (node.name() == _target)
            _result = &node;
    }

    // ============================================================================
    // MemberFinder
    // ============================================================================

    void MemberFinder::visit(Cryo::ProgramNode &node)
    {
        for (const auto &child : node.statements())
            if (child && !_result)
                child->accept(*this);
    }

    void MemberFinder::visit(Cryo::DeclarationStatementNode &node)
    {
        if (node.declaration() && !_result)
            node.declaration()->accept(*this);
    }

    void MemberFinder::visit(Cryo::StructDeclarationNode &node)
    {
        if (_result)
            return;

        for (const auto &method : node.methods())
        {
            if (method && method->name() == _target)
            {
                _result = method.get();
                _owner_name = node.name();
                return;
            }
        }

        for (const auto &field : node.fields())
        {
            if (field && field->name() == _target)
            {
                _result = field.get();
                _owner_name = node.name();
                return;
            }
        }
    }

    void MemberFinder::visit(Cryo::ClassDeclarationNode &node)
    {
        if (_result)
            return;

        for (const auto &method : node.methods())
        {
            if (method && method->name() == _target)
            {
                _result = method.get();
                _owner_name = node.name();
                return;
            }
        }

        for (const auto &field : node.fields())
        {
            if (field && field->name() == _target)
            {
                _result = field.get();
                _owner_name = node.name();
                return;
            }
        }
    }

    void MemberFinder::visit(Cryo::ImplementationBlockNode &node)
    {
        if (_result)
            return;

        for (const auto &method : node.method_implementations())
        {
            if (method && method->name() == _target)
            {
                _result = method.get();
                _owner_name = node.target_type();
                return;
            }
        }

        for (const auto &field : node.field_implementations())
        {
            if (field && field->name() == _target)
            {
                _result = field.get();
                _owner_name = node.target_type();
                return;
            }
        }
    }

    // ============================================================================
    // ScopedMemberFinder
    // ============================================================================

    void ScopedMemberFinder::visit(Cryo::ProgramNode &node)
    {
        for (const auto &child : node.statements())
            if (child && !_result)
                child->accept(*this);
    }

    void ScopedMemberFinder::visit(Cryo::DeclarationStatementNode &node)
    {
        if (node.declaration() && !_result)
            node.declaration()->accept(*this);
    }

    void ScopedMemberFinder::visit(Cryo::StructDeclarationNode &node)
    {
        if (_result || node.name() != _owner)
            return;

        for (const auto &method : node.methods())
        {
            if (method && method->name() == _member)
            {
                _result = method.get();
                return;
            }
        }

        for (const auto &field : node.fields())
        {
            if (field && field->name() == _member)
            {
                _result = field.get();
                return;
            }
        }
    }

    void ScopedMemberFinder::visit(Cryo::ClassDeclarationNode &node)
    {
        if (_result || node.name() != _owner)
            return;

        for (const auto &method : node.methods())
        {
            if (method && method->name() == _member)
            {
                _result = method.get();
                return;
            }
        }

        for (const auto &field : node.fields())
        {
            if (field && field->name() == _member)
            {
                _result = field.get();
                return;
            }
        }
    }

    void ScopedMemberFinder::visit(Cryo::EnumDeclarationNode &node)
    {
        if (_result || node.name() != _owner)
            return;

        for (const auto &variant : node.variants())
        {
            if (variant && variant->name() == _member)
            {
                _result = variant.get();
                return;
            }
        }
    }

    void ScopedMemberFinder::visit(Cryo::ImplementationBlockNode &node)
    {
        if (_result || node.target_type() != _owner)
            return;

        for (const auto &method : node.method_implementations())
        {
            if (method && method->name() == _member)
            {
                _result = method.get();
                return;
            }
        }

        for (const auto &field : node.field_implementations())
        {
            if (field && field->name() == _member)
            {
                _result = field.get();
                return;
            }
        }
    }

    // ============================================================================
    // VariableRefResolver
    // ============================================================================

    void VariableRefResolver::visit(Cryo::ProgramNode &node)
    {
        for (const auto &child : node.statements())
            if (child && !_result)
                child->accept(*this);
    }

    void VariableRefResolver::visit(Cryo::DeclarationStatementNode &node)
    {
        if (node.declaration() && !_result)
            node.declaration()->accept(*this);
    }

    void VariableRefResolver::visit(Cryo::BlockStatementNode &node)
    {
        for (const auto &stmt : node.statements())
            if (stmt && !_result)
                stmt->accept(*this);
    }

    void VariableRefResolver::visit(Cryo::ExpressionStatementNode &node)
    {
        // Expression statements don't contain variable declarations
    }

    void VariableRefResolver::visit(Cryo::VariableDeclarationNode &node)
    {
        if (node.name() == _target)
            _result = &node;
    }

    void VariableRefResolver::visit(Cryo::FunctionDeclarationNode &node)
    {
        for (const auto &param : node.parameters())
        {
            if (param && !_result && param->name() == _target)
                _result = param.get();
        }
        if (node.body() && !_result)
            node.body()->accept(*this);
    }

    void VariableRefResolver::visit(Cryo::StructMethodNode &node)
    {
        for (const auto &param : node.parameters())
        {
            if (param && !_result && param->name() == _target)
                _result = param.get();
        }
        if (node.body() && !_result)
            node.body()->accept(*this);
    }

    void VariableRefResolver::visit(Cryo::StructDeclarationNode &node)
    {
        for (const auto &method : node.methods())
            if (method && !_result)
                method->accept(*this);
    }

    void VariableRefResolver::visit(Cryo::ClassDeclarationNode &node)
    {
        for (const auto &method : node.methods())
            if (method && !_result)
                method->accept(*this);
    }

    void VariableRefResolver::visit(Cryo::ImplementationBlockNode &node)
    {
        for (const auto &method : node.method_implementations())
            if (method && !_result)
                method->accept(*this);
    }

    void VariableRefResolver::visit(Cryo::IfStatementNode &node)
    {
        if (node.then_statement() && !_result)
            node.then_statement()->accept(*this);
        if (node.else_statement() && !_result)
            node.else_statement()->accept(*this);
    }

    void VariableRefResolver::visit(Cryo::ForStatementNode &node)
    {
        if (node.init() && !_result)
            node.init()->accept(*this);
        if (node.body() && !_result)
            node.body()->accept(*this);
    }

    void VariableRefResolver::visit(Cryo::WhileStatementNode &node)
    {
        if (node.body() && !_result)
            node.body()->accept(*this);
    }

    // ============================================================================
    // Utility functions
    // ============================================================================

    std::string stripGenericArgs(const std::string &name)
    {
        auto pos = name.find('<');
        if (pos == std::string::npos)
            return name;
        return name.substr(0, pos);
    }

    std::vector<std::string> parseGenericArgs(const std::string &name)
    {
        auto open = name.find('<');
        if (open == std::string::npos)
            return {};
        auto close = name.rfind('>');
        if (close == std::string::npos || close <= open)
            return {};

        std::string inner = name.substr(open + 1, close - open - 1);
        std::vector<std::string> args;
        int depth = 0;
        size_t start = 0;

        for (size_t i = 0; i < inner.size(); ++i)
        {
            if (inner[i] == '<')
                depth++;
            else if (inner[i] == '>')
                depth--;
            else if (inner[i] == ',' && depth == 0)
            {
                std::string arg = inner.substr(start, i - start);
                size_t first = arg.find_first_not_of(" \t");
                if (first != std::string::npos)
                    arg = arg.substr(first);
                size_t last = arg.find_last_not_of(" \t");
                if (last != std::string::npos)
                    arg = arg.substr(0, last + 1);
                args.push_back(arg);
                start = i + 1;
            }
        }
        // Last argument
        std::string last_arg = inner.substr(start);
        size_t first = last_arg.find_first_not_of(" \t");
        if (first != std::string::npos)
            last_arg = last_arg.substr(first);
        size_t lastpos = last_arg.find_last_not_of(" \t");
        if (lastpos != std::string::npos)
            last_arg = last_arg.substr(0, lastpos + 1);
        if (!last_arg.empty())
            args.push_back(last_arg);

        return args;
    }

} // namespace CryoLSP
