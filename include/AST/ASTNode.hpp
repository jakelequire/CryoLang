#pragma once
#include "Lexer/lexer.hpp"
#include <iostream>
#include <string>
#include <vector>
#include <memory>
#include <optional>

namespace Cryo
{
    // Forward declaration for visitor pattern
    class ASTVisitor;

    enum class NodeKind
    {
        // Base categories
        Expression,
        Statement,
        Declaration,

        // Concrete expression types
        Literal,
        Identifier,
        BinaryExpression,
        CallExpression,

        // Concrete statement types
        BlockStatement,
        ReturnStatement,
        IfStatement,
        WhileStatement,
        ForStatement,
        BreakStatement,
        ContinueStatement,
        ExpressionStatement,
        DeclarationStatement,

        // Concrete declaration types
        VariableDeclaration,
        FunctionDeclaration,

        // Top-level
        Program,

        Unknown
    };

    class ASTNode
    {
    protected:
        NodeKind _kind;
        SourceLocation _location;

    public:
        ASTNode(NodeKind kind, SourceLocation location)
            : _kind(kind), _location(location) {}

        NodeKind kind() const { return _kind; }
        const SourceLocation &location() const { return _location; }

        virtual void print(std::ostream &os, int indent = 0) const = 0;
        virtual void accept(ASTVisitor &visitor) = 0;
        virtual ~ASTNode() = default;
    };

    // Concrete node types
    class ExpressionNode : public ASTNode
    {
    private:
        std::optional<std::string> _resolved_type; // For future type system

    public:
        ExpressionNode(NodeKind kind, SourceLocation loc) : ASTNode(kind, loc) {}

        // Type system support
        void set_type(const std::string &type) { _resolved_type = type; }
        std::optional<std::string> type() const { return _resolved_type; }

        void print(std::ostream &os, int indent = 0) const override
        {
            os << std::string(indent, ' ') << "Expression" << std::endl;
        }

        void accept(ASTVisitor &visitor) override;
    };

    class StatementNode : public ASTNode
    {
    public:
        StatementNode(NodeKind kind, SourceLocation loc) : ASTNode(kind, loc) {}

        void print(std::ostream &os, int indent = 0) const override
        {
            os << std::string(indent, ' ') << "Statement" << std::endl;
        }

        void accept(ASTVisitor &visitor) override;
    };

    class DeclarationNode : public ASTNode // Separate from StatementNode
    {
    public:
        DeclarationNode(NodeKind kind, SourceLocation loc) : ASTNode(kind, loc) {}

        void print(std::ostream &os, int indent = 0) const override
        {
            os << std::string(indent, ' ') << "Declaration" << std::endl;
        }

        void accept(ASTVisitor &visitor) override;
    };

    class LiteralNode : public ExpressionNode
    {
    private:
        std::string _value;
        TokenKind _literal_kind;

    public:
        LiteralNode(NodeKind kind, SourceLocation loc, std::string value, TokenKind literal_kind)
            : ExpressionNode(kind, loc), _value(std::move(value)), _literal_kind(literal_kind) {}

        const std::string &value() const { return _value; }
        TokenKind literal_kind() const { return _literal_kind; }

        void print(std::ostream &os, int indent = 0) const override
        {
            os << std::string(indent, ' ') << "Literal: " << _value << std::endl;
        }

        void accept(ASTVisitor &visitor) override;
    };

    class IdentifierNode : public ExpressionNode
    {
    private:
        std::string _name;

    public:
        IdentifierNode(NodeKind kind, SourceLocation loc, std::string name)
            : ExpressionNode(kind, loc), _name(std::move(name)) {}

        const std::string &name() const { return _name; }

        void print(std::ostream &os, int indent = 0) const override
        {
            os << std::string(indent, ' ') << "Identifier: " << _name << std::endl;
        }

        void accept(ASTVisitor &visitor) override;
    };

    class BinaryExpressionNode : public ExpressionNode
    {
    private:
        Token _operator;
        std::unique_ptr<ExpressionNode> _left;
        std::unique_ptr<ExpressionNode> _right;

    public:
        BinaryExpressionNode(NodeKind kind, SourceLocation loc, Token op,
                             std::unique_ptr<ExpressionNode> left,
                             std::unique_ptr<ExpressionNode> right)
            : ExpressionNode(kind, loc), _operator(op), _left(std::move(left)), _right(std::move(right)) {}

        const Token &operator_token() const { return _operator; }
        ExpressionNode *left() const { return _left.get(); }
        ExpressionNode *right() const { return _right.get(); }

        void print(std::ostream &os, int indent = 0) const override
        {
            os << std::string(indent, ' ') << "BinaryExpression: " << _operator.to_string() << std::endl;
            if (_left)
                _left->print(os, indent + 2);
            if (_right)
                _right->print(os, indent + 2);
        }

        void accept(ASTVisitor &visitor) override;
    };

    // Program/Module node (root of the AST)
    class ProgramNode : public ASTNode
    {
    private:
        std::vector<std::unique_ptr<ASTNode>> _statements;

    public:
        ProgramNode(SourceLocation loc) : ASTNode(NodeKind::Program, loc) {}

        void add_statement(std::unique_ptr<ASTNode> stmt)
        {
            _statements.push_back(std::move(stmt));
        }

        const std::vector<std::unique_ptr<ASTNode>> &statements() const { return _statements; }

        void print(std::ostream &os, int indent = 0) const override
        {
            os << std::string(indent, ' ') << "Program:" << std::endl;
            for (const auto &stmt : _statements)
            {
                if (stmt)
                    stmt->print(os, indent + 2);
            }
        }

        void accept(ASTVisitor &visitor) override;
    };

    // Block statement
    class BlockStatementNode : public StatementNode
    {
    private:
        std::vector<std::unique_ptr<StatementNode>> _statements;

    public:
        BlockStatementNode(SourceLocation loc) : StatementNode(NodeKind::BlockStatement, loc) {}

        void add_statement(std::unique_ptr<StatementNode> stmt)
        {
            _statements.push_back(std::move(stmt));
        }

        const std::vector<std::unique_ptr<StatementNode>> &statements() const { return _statements; }

        void print(std::ostream &os, int indent = 0) const override
        {
            os << std::string(indent, ' ') << "Block:" << std::endl;
            for (const auto &stmt : _statements)
            {
                if (stmt)
                    stmt->print(os, indent + 2);
            }
        }

        void accept(ASTVisitor &visitor) override;
    };

    // Return statement
    class ReturnStatementNode : public StatementNode
    {
    private:
        std::unique_ptr<ExpressionNode> _expression;

    public:
        ReturnStatementNode(SourceLocation loc, std::unique_ptr<ExpressionNode> expr = nullptr)
            : StatementNode(NodeKind::ReturnStatement, loc), _expression(std::move(expr)) {}

        ExpressionNode *expression() const { return _expression.get(); }

        void print(std::ostream &os, int indent = 0) const override
        {
            os << std::string(indent, ' ') << "Return:" << std::endl;
            if (_expression)
                _expression->print(os, indent + 2);
        }

        void accept(ASTVisitor &visitor) override;
    };

    // Variable declaration
    class VariableDeclarationNode : public DeclarationNode
    {
    private:
        std::string _name;
        std::string _type_annotation; // Type annotation from source code
        std::unique_ptr<ExpressionNode> _initializer;
        bool _is_mutable = false; // const vs mut
        bool _is_auto = false;    // auto type inference

    public:
        VariableDeclarationNode(SourceLocation loc, std::string name,
                                std::string type_annotation = "",
                                std::unique_ptr<ExpressionNode> init = nullptr,
                                bool is_mutable = false)
            : DeclarationNode(NodeKind::VariableDeclaration, loc),
              _name(std::move(name)), _type_annotation(std::move(type_annotation)),
              _initializer(std::move(init)), _is_mutable(is_mutable)
        {
            _is_auto = (_type_annotation == "auto" || _type_annotation.empty());
        }

        const std::string &name() const { return _name; }
        const std::string &type_annotation() const { return _type_annotation; }
        ExpressionNode *initializer() const { return _initializer.get(); }
        bool is_mutable() const { return _is_mutable; }
        bool is_auto() const { return _is_auto; }
        bool has_initializer() const { return _initializer != nullptr; }

        // Type annotation setters
        void set_type_annotation(const std::string &type)
        {
            _type_annotation = type;
            _is_auto = (type == "auto");
        }

        void print(std::ostream &os, int indent = 0) const override
        {
            os << std::string(indent, ' ') << "VariableDecl: "
               << (_is_mutable ? "mut " : "const ") << _name;
            if (!_type_annotation.empty())
                os << ": " << _type_annotation;
            os << std::endl;
            if (_initializer)
                _initializer->print(os, indent + 2);
        }

        void accept(ASTVisitor &visitor) override;
    };

    // Function declaration
    class FunctionDeclarationNode : public DeclarationNode
    {
    private:
        std::string _name;
        std::string _return_type_annotation = "void"; // Return type annotation
        std::vector<std::unique_ptr<VariableDeclarationNode>> _parameters;
        std::unique_ptr<BlockStatementNode> _body;
        bool _is_public = false; // Visibility
        bool _is_static = false;
        bool _is_inline = false;
        bool _is_variadic = false; // Variadic function (...args)

    public:
        FunctionDeclarationNode(SourceLocation loc, std::string name,
                                std::string return_type = "void",
                                bool is_public = false)
            : DeclarationNode(NodeKind::FunctionDeclaration, loc),
              _name(std::move(name)), _return_type_annotation(std::move(return_type)),
              _is_public(is_public) {}

        const std::string &name() const { return _name; }
        const std::string &return_type_annotation() const { return _return_type_annotation; }
        const std::vector<std::unique_ptr<VariableDeclarationNode>> &parameters() const { return _parameters; }
        BlockStatementNode *body() const { return _body.get(); }

        bool is_public() const { return _is_public; }
        bool is_static() const { return _is_static; }
        bool is_inline() const { return _is_inline; }
        bool is_variadic() const { return _is_variadic; }
        size_t parameter_count() const { return _parameters.size(); }

        void add_parameter(std::unique_ptr<VariableDeclarationNode> param)
        {
            _parameters.push_back(std::move(param));
        }

        void set_body(std::unique_ptr<BlockStatementNode> body)
        {
            _body = std::move(body);
        }

        void set_return_type(const std::string &return_type)
        {
            _return_type_annotation = return_type;
        }

        void set_visibility(bool is_public) { _is_public = is_public; }
        void set_static(bool is_static) { _is_static = is_static; }
        void set_inline(bool is_inline) { _is_inline = is_inline; }
        void set_variadic(bool is_variadic) { _is_variadic = is_variadic; }

        void print(std::ostream &os, int indent = 0) const override
        {
            os << std::string(indent, ' ') << "FunctionDecl: "
               << (_is_public ? "public " : "private ") << _name << "(";
            for (size_t i = 0; i < _parameters.size(); ++i)
            {
                if (i > 0)
                    os << ", ";
                if (_parameters[i])
                    os << _parameters[i]->type_annotation() << " " << _parameters[i]->name();
            }
            if (_is_variadic)
                os << "...";
            os << ") -> " << _return_type_annotation << std::endl;

            if (!_parameters.empty())
            {
                os << std::string(indent + 2, ' ') << "Parameters:" << std::endl;
                for (const auto &param : _parameters)
                {
                    if (param)
                        param->print(os, indent + 4);
                }
            }

            if (_body)
            {
                os << std::string(indent + 2, ' ') << "Body:" << std::endl;
                _body->print(os, indent + 4);
            }
        }

        void accept(ASTVisitor &visitor) override;
    };

    // Call expression
    class CallExpressionNode : public ExpressionNode
    {
    private:
        std::unique_ptr<ExpressionNode> _callee;
        std::vector<std::unique_ptr<ExpressionNode>> _arguments;

    public:
        CallExpressionNode(SourceLocation loc, std::unique_ptr<ExpressionNode> callee)
            : ExpressionNode(NodeKind::CallExpression, loc), _callee(std::move(callee)) {}

        ExpressionNode *callee() const { return _callee.get(); }
        const std::vector<std::unique_ptr<ExpressionNode>> &arguments() const { return _arguments; }

        void add_argument(std::unique_ptr<ExpressionNode> arg)
        {
            _arguments.push_back(std::move(arg));
        }

        void print(std::ostream &os, int indent = 0) const override
        {
            os << std::string(indent, ' ') << "Call:" << std::endl;
            if (_callee)
            {
                os << std::string(indent + 2, ' ') << "Callee:" << std::endl;
                _callee->print(os, indent + 4);
            }
            os << std::string(indent + 2, ' ') << "Arguments:" << std::endl;
            for (const auto &arg : _arguments)
            {
                if (arg)
                    arg->print(os, indent + 4);
            }
        }

        void accept(ASTVisitor &visitor) override;
    };

    // If statement
    class IfStatementNode : public StatementNode
    {
    private:
        std::unique_ptr<ExpressionNode> _condition;
        std::unique_ptr<StatementNode> _then_stmt;
        std::unique_ptr<StatementNode> _else_stmt;

    public:
        IfStatementNode(SourceLocation loc, std::unique_ptr<ExpressionNode> condition,
                        std::unique_ptr<StatementNode> then_stmt,
                        std::unique_ptr<StatementNode> else_stmt = nullptr)
            : StatementNode(NodeKind::IfStatement, loc), _condition(std::move(condition)),
              _then_stmt(std::move(then_stmt)), _else_stmt(std::move(else_stmt)) {}

        ExpressionNode *condition() const { return _condition.get(); }
        StatementNode *then_statement() const { return _then_stmt.get(); }
        StatementNode *else_statement() const { return _else_stmt.get(); }

        void print(std::ostream &os, int indent = 0) const override
        {
            os << std::string(indent, ' ') << "If:" << std::endl;
            if (_condition)
            {
                os << std::string(indent + 2, ' ') << "Condition:" << std::endl;
                _condition->print(os, indent + 4);
            }
            if (_then_stmt)
            {
                os << std::string(indent + 2, ' ') << "Then:" << std::endl;
                _then_stmt->print(os, indent + 4);
            }
            if (_else_stmt)
            {
                os << std::string(indent + 2, ' ') << "Else:" << std::endl;
                _else_stmt->print(os, indent + 4);
            }
        }

        void accept(ASTVisitor &visitor) override;
    };

    // While statement
    class WhileStatementNode : public StatementNode
    {
    private:
        std::unique_ptr<ExpressionNode> _condition;
        std::unique_ptr<StatementNode> _body;

    public:
        WhileStatementNode(SourceLocation loc, std::unique_ptr<ExpressionNode> condition,
                           std::unique_ptr<StatementNode> body)
            : StatementNode(NodeKind::WhileStatement, loc), _condition(std::move(condition)),
              _body(std::move(body)) {}

        ExpressionNode *condition() const { return _condition.get(); }
        StatementNode *body() const { return _body.get(); }

        void print(std::ostream &os, int indent = 0) const override
        {
            os << std::string(indent, ' ') << "While:" << std::endl;
            if (_condition)
            {
                os << std::string(indent + 2, ' ') << "Condition:" << std::endl;
                _condition->print(os, indent + 4);
            }
            if (_body)
            {
                os << std::string(indent + 2, ' ') << "Body:" << std::endl;
                _body->print(os, indent + 4);
            }
        }

        void accept(ASTVisitor &visitor) override;
    };

    // For statement
    class ForStatementNode : public StatementNode
    {
    private:
        std::unique_ptr<VariableDeclarationNode> _init;
        std::unique_ptr<ExpressionNode> _condition;
        std::unique_ptr<ExpressionNode> _update;
        std::unique_ptr<StatementNode> _body;

    public:
        ForStatementNode(SourceLocation loc, std::unique_ptr<VariableDeclarationNode> init,
                         std::unique_ptr<ExpressionNode> condition,
                         std::unique_ptr<ExpressionNode> update,
                         std::unique_ptr<StatementNode> body)
            : StatementNode(NodeKind::ForStatement, loc), _init(std::move(init)),
              _condition(std::move(condition)), _update(std::move(update)),
              _body(std::move(body)) {}

        VariableDeclarationNode *init() const { return _init.get(); }
        ExpressionNode *condition() const { return _condition.get(); }
        ExpressionNode *update() const { return _update.get(); }
        StatementNode *body() const { return _body.get(); }

        void print(std::ostream &os, int indent = 0) const override
        {
            os << std::string(indent, ' ') << "For:" << std::endl;
            if (_init)
            {
                os << std::string(indent + 2, ' ') << "Init:" << std::endl;
                _init->print(os, indent + 4);
            }
            if (_condition)
            {
                os << std::string(indent + 2, ' ') << "Condition:" << std::endl;
                _condition->print(os, indent + 4);
            }
            if (_update)
            {
                os << std::string(indent + 2, ' ') << "Update:" << std::endl;
                _update->print(os, indent + 4);
            }
            if (_body)
            {
                os << std::string(indent + 2, ' ') << "Body:" << std::endl;
                _body->print(os, indent + 4);
            }
        }

        void accept(ASTVisitor &visitor) override;
    };

    // Break statement
    class BreakStatementNode : public StatementNode
    {
    public:
        BreakStatementNode(SourceLocation loc)
            : StatementNode(NodeKind::BreakStatement, loc) {}

        void print(std::ostream &os, int indent = 0) const override
        {
            os << std::string(indent, ' ') << "Break" << std::endl;
        }

        void accept(ASTVisitor &visitor) override;
    };

    // Continue statement
    class ContinueStatementNode : public StatementNode
    {
    public:
        ContinueStatementNode(SourceLocation loc)
            : StatementNode(NodeKind::ContinueStatement, loc) {}

        void print(std::ostream &os, int indent = 0) const override
        {
            os << std::string(indent, ' ') << "Continue" << std::endl;
        }

        void accept(ASTVisitor &visitor) override;
    };

    // Expression statement
    class ExpressionStatementNode : public StatementNode
    {
    private:
        std::unique_ptr<ExpressionNode> _expression;

    public:
        ExpressionStatementNode(SourceLocation loc, std::unique_ptr<ExpressionNode> expr)
            : StatementNode(NodeKind::ExpressionStatement, loc), _expression(std::move(expr)) {}

        ExpressionNode *expression() const { return _expression.get(); }

        void print(std::ostream &os, int indent = 0) const override
        {
            os << std::string(indent, ' ') << "ExpressionStatement:" << std::endl;
            if (_expression)
                _expression->print(os, indent + 2);
        }

        void accept(ASTVisitor &visitor) override;
    };

    // Declaration statement - wraps a declaration to be used in statement contexts
    class DeclarationStatementNode : public StatementNode
    {
    private:
        std::unique_ptr<DeclarationNode> _declaration;

    public:
        DeclarationStatementNode(SourceLocation loc, std::unique_ptr<DeclarationNode> decl)
            : StatementNode(NodeKind::DeclarationStatement, loc), _declaration(std::move(decl)) {}

        DeclarationNode *declaration() const { return _declaration.get(); }

        void print(std::ostream &os, int indent = 0) const override
        {
            if (_declaration)
                _declaration->print(os, indent);
        }

        void accept(ASTVisitor &visitor) override;
    };

}