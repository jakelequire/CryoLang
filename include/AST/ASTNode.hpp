#pragma once
#include "Lexer/lexer.hpp"
#include <iostream>
#include <string>
#include <vector>
#include <memory>
#include <optional>
#include <algorithm>

namespace Cryo
{
    // Forward declaration for visitor pattern
    class ASTVisitor;

    // Forward declarations for pattern matching
    class MatchArmNode;
    class PatternNode;
    class EnumPatternNode;

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
        UnaryExpression,
        TernaryExpression,
        CallExpression,
        NewExpression,
        ArrayLiteral,
        ArrayAccess,
        MemberAccess,
        ScopeResolution,

        // Concrete statement types
        BlockStatement,
        ReturnStatement,
        IfStatement,
        WhileStatement,
        ForStatement,
        MatchStatement,
        BreakStatement,
        ContinueStatement,
        ExpressionStatement,
        DeclarationStatement,

        // Concrete declaration types
        VariableDeclaration,
        FunctionDeclaration,
        StructDeclaration,
        ClassDeclaration,
        EnumDeclaration,
        TypeAliasDeclaration,
        ImplementationBlock,
        ExternBlock,

        // Top-level
        Program,

        // Pattern matching specific
        MatchArm,
        Pattern,
        EnumPattern,

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

    class UnaryExpressionNode : public ExpressionNode
    {
    private:
        Token _operator;
        std::unique_ptr<ExpressionNode> _operand;

    public:
        UnaryExpressionNode(NodeKind kind, SourceLocation loc, Token op,
                            std::unique_ptr<ExpressionNode> operand)
            : ExpressionNode(kind, loc), _operator(op), _operand(std::move(operand)) {}

        const Token &operator_token() const { return _operator; }
        ExpressionNode *operand() const { return _operand.get(); }

        void print(std::ostream &os, int indent = 0) const override
        {
            os << std::string(indent, ' ') << "UnaryExpression: " << _operator.to_string() << std::endl;
            if (_operand)
                _operand->print(os, indent + 2);
        }

        void accept(ASTVisitor &visitor) override;
    };

    class TernaryExpressionNode : public ExpressionNode
    {
    private:
        std::unique_ptr<ExpressionNode> _condition;
        std::unique_ptr<ExpressionNode> _true_expr;
        std::unique_ptr<ExpressionNode> _false_expr;

    public:
        TernaryExpressionNode(NodeKind kind, SourceLocation loc,
                              std::unique_ptr<ExpressionNode> condition,
                              std::unique_ptr<ExpressionNode> true_expr,
                              std::unique_ptr<ExpressionNode> false_expr)
            : ExpressionNode(kind, loc), _condition(std::move(condition)),
              _true_expr(std::move(true_expr)), _false_expr(std::move(false_expr)) {}

        ExpressionNode *condition() const { return _condition.get(); }
        ExpressionNode *true_expression() const { return _true_expr.get(); }
        ExpressionNode *false_expression() const { return _false_expr.get(); }

        void print(std::ostream &os, int indent = 0) const override
        {
            os << std::string(indent, ' ') << "TernaryExpression:" << std::endl;
            if (_condition)
            {
                os << std::string(indent + 2, ' ') << "Condition:" << std::endl;
                _condition->print(os, indent + 4);
            }
            if (_true_expr)
            {
                os << std::string(indent + 2, ' ') << "TrueExpr:" << std::endl;
                _true_expr->print(os, indent + 4);
            }
            if (_false_expr)
            {
                os << std::string(indent + 2, ' ') << "FalseExpr:" << std::endl;
                _false_expr->print(os, indent + 4);
            }
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

    // Visibility enum for struct/class members
    enum class Visibility
    {
        Public,
        Private,
        Protected
    };

    // Forward declarations for struct/class components
    class StructFieldNode;
    class StructMethodNode;
    class GenericParameterNode;

    // Generic parameter for types
    class GenericParameterNode : public ASTNode
    {
    private:
        std::string _name;
        std::vector<std::string> _constraints; // Type constraints

    public:
        GenericParameterNode(SourceLocation loc, std::string name)
            : ASTNode(NodeKind::Declaration, loc), _name(std::move(name)) {}

        const std::string &name() const { return _name; }
        const std::vector<std::string> &constraints() const { return _constraints; }

        void add_constraint(const std::string &constraint)
        {
            _constraints.push_back(constraint);
        }

        void set_type(const std::string &type) { /* Generic parameters have implicit types */ }

        void print(std::ostream &os, int indent = 0) const override
        {
            os << std::string(indent, ' ') << "GenericParam: " << _name;
            if (!_constraints.empty())
            {
                os << " : ";
                for (size_t i = 0; i < _constraints.size(); ++i)
                {
                    if (i > 0)
                        os << " + ";
                    os << _constraints[i];
                }
            }
            os << std::endl;
        }

        void accept(ASTVisitor &visitor) override;
    };

    // Struct field declaration
    class StructFieldNode : public DeclarationNode
    {
    private:
        std::string _name;
        std::string _type_annotation;
        Visibility _visibility;
        std::unique_ptr<ExpressionNode> _default_value;

    public:
        StructFieldNode(SourceLocation loc, std::string name, std::string type_annotation,
                        Visibility visibility = Visibility::Public)
            : DeclarationNode(NodeKind::VariableDeclaration, loc),
              _name(std::move(name)), _type_annotation(std::move(type_annotation)),
              _visibility(visibility) {}

        const std::string &name() const { return _name; }
        const std::string &type_annotation() const { return _type_annotation; }
        Visibility visibility() const { return _visibility; }
        ExpressionNode *default_value() const { return _default_value.get(); }

        // Compatibility methods for TypeChecker
        const std::string &field_type() const { return _type_annotation; }
        bool is_mutable() const { return _visibility != Visibility::Private; }
        void set_type(const std::string &type) { _type_annotation = type; }

        void set_default_value(std::unique_ptr<ExpressionNode> value)
        {
            _default_value = std::move(value);
        }

        void print(std::ostream &os, int indent = 0) const override
        {
            os << std::string(indent, ' ') << "Field: ";
            if (_visibility == Visibility::Private)
                os << "private ";
            else if (_visibility == Visibility::Protected)
                os << "protected ";
            os << _name << ": " << _type_annotation;
            if (_default_value)
            {
                os << " = ";
                _default_value->print(os, 0);
            }
            os << std::endl;
        }

        void accept(ASTVisitor &visitor) override;
    };

    // Struct method declaration (can be signature only or with body)
    class StructMethodNode : public FunctionDeclarationNode
    {
    private:
        Visibility _visibility;
        bool _is_constructor;

    public:
        StructMethodNode(SourceLocation loc, std::string name, std::string return_type,
                         Visibility visibility = Visibility::Public, bool is_constructor = false)
            : FunctionDeclarationNode(loc, std::move(name), std::move(return_type)),
              _visibility(visibility), _is_constructor(is_constructor) {}

        Visibility visibility() const { return _visibility; }
        bool is_constructor() const { return _is_constructor; }

        void print(std::ostream &os, int indent = 0) const override
        {
            os << std::string(indent, ' ') << "Method: ";
            if (_visibility == Visibility::Private)
                os << "private ";
            else if (_visibility == Visibility::Protected)
                os << "protected ";
            if (_is_constructor)
                os << "constructor ";

            // Call parent print method
            FunctionDeclarationNode::print(os, 0);
        }

        void accept(ASTVisitor &visitor) override;
    };

    // Struct declaration
    class StructDeclarationNode : public DeclarationNode
    {
    private:
        std::string _name;
        std::vector<std::unique_ptr<GenericParameterNode>> _generic_parameters;
        std::vector<std::unique_ptr<StructFieldNode>> _fields;
        std::vector<std::unique_ptr<StructMethodNode>> _methods;

    public:
        StructDeclarationNode(SourceLocation loc, std::string name)
            : DeclarationNode(NodeKind::StructDeclaration, loc), _name(std::move(name)) {}

        const std::string &name() const { return _name; }
        const std::vector<std::unique_ptr<GenericParameterNode>> &generic_parameters() const { return _generic_parameters; }
        const std::vector<std::unique_ptr<StructFieldNode>> &fields() const { return _fields; }
        const std::vector<std::unique_ptr<StructMethodNode>> &methods() const { return _methods; }

        void set_type(const std::string &type_name) { /* For type checker compatibility */ }

        void add_generic_parameter(std::unique_ptr<GenericParameterNode> param)
        {
            _generic_parameters.push_back(std::move(param));
        }

        void add_field(std::unique_ptr<StructFieldNode> field)
        {
            _fields.push_back(std::move(field));
        }

        void add_method(std::unique_ptr<StructMethodNode> method)
        {
            _methods.push_back(std::move(method));
        }

        void print(std::ostream &os, int indent = 0) const override
        {
            os << std::string(indent, ' ') << "StructDecl: " << _name;
            if (!_generic_parameters.empty())
            {
                os << "<";
                for (size_t i = 0; i < _generic_parameters.size(); ++i)
                {
                    if (i > 0)
                        os << ", ";
                    os << _generic_parameters[i]->name();
                }
                os << ">";
            }
            os << std::endl;

            if (!_fields.empty())
            {
                os << std::string(indent + 2, ' ') << "Fields:" << std::endl;
                for (const auto &field : _fields)
                {
                    if (field)
                        field->print(os, indent + 4);
                }
            }

            if (!_methods.empty())
            {
                os << std::string(indent + 2, ' ') << "Methods:" << std::endl;
                for (const auto &method : _methods)
                {
                    if (method)
                        method->print(os, indent + 4);
                }
            }
        }

        void accept(ASTVisitor &visitor) override;
    };

    // Class declaration (similar to struct but defaults to private)
    class ClassDeclarationNode : public DeclarationNode
    {
    private:
        std::string _name;
        std::vector<std::unique_ptr<GenericParameterNode>> _generic_parameters;
        std::vector<std::unique_ptr<StructFieldNode>> _fields;
        std::vector<std::unique_ptr<StructMethodNode>> _methods;
        std::string _base_class; // Optional base class

    public:
        ClassDeclarationNode(SourceLocation loc, std::string name)
            : DeclarationNode(NodeKind::ClassDeclaration, loc), _name(std::move(name)) {}

        const std::string &name() const { return _name; }
        const std::vector<std::unique_ptr<GenericParameterNode>> &generic_parameters() const { return _generic_parameters; }
        const std::vector<std::unique_ptr<StructFieldNode>> &fields() const { return _fields; }
        const std::vector<std::unique_ptr<StructMethodNode>> &methods() const { return _methods; }
        const std::string &base_class() const { return _base_class; }

        void set_type(const std::string &type_name) { /* For type checker compatibility */ }

        void add_generic_parameter(std::unique_ptr<GenericParameterNode> param)
        {
            _generic_parameters.push_back(std::move(param));
        }

        void add_field(std::unique_ptr<StructFieldNode> field)
        {
            _fields.push_back(std::move(field));
        }

        void add_method(std::unique_ptr<StructMethodNode> method)
        {
            _methods.push_back(std::move(method));
        }

        void set_base_class(const std::string &base)
        {
            _base_class = base;
        }

        void print(std::ostream &os, int indent = 0) const override
        {
            os << std::string(indent, ' ') << "ClassDecl: " << _name;
            if (!_generic_parameters.empty())
            {
                os << "<";
                for (size_t i = 0; i < _generic_parameters.size(); ++i)
                {
                    if (i > 0)
                        os << ", ";
                    os << _generic_parameters[i]->name();
                }
                os << ">";
            }
            if (!_base_class.empty())
            {
                os << " : " << _base_class;
            }
            os << std::endl;

            if (!_fields.empty())
            {
                os << std::string(indent + 2, ' ') << "Fields:" << std::endl;
                for (const auto &field : _fields)
                {
                    if (field)
                        field->print(os, indent + 4);
                }
            }

            if (!_methods.empty())
            {
                os << std::string(indent + 2, ' ') << "Methods:" << std::endl;
                for (const auto &method : _methods)
                {
                    if (method)
                        method->print(os, indent + 4);
                }
            }
        }

        void accept(ASTVisitor &visitor) override;
    };

    // Type alias declaration (type Foo = Bar)
    class TypeAliasDeclarationNode : public DeclarationNode
    {
    private:
        std::string _alias_name;
        std::string _target_type;

    public:
        TypeAliasDeclarationNode(SourceLocation loc, std::string alias_name, std::string target_type)
            : DeclarationNode(NodeKind::TypeAliasDeclaration, loc),
              _alias_name(std::move(alias_name)), _target_type(std::move(target_type)) {}

        const std::string &alias_name() const { return _alias_name; }
        const std::string &target_type() const { return _target_type; }

        void set_type(const std::string &type) { _target_type = type; }

        void print(std::ostream &os, int indent = 0) const override
        {
            os << std::string(indent, ' ') << "TypeAlias: " << _alias_name
               << " = " << _target_type << std::endl;
        }

        void accept(ASTVisitor &visitor) override;
    };

    // Enum variant (for both simple and complex variants)
    class EnumVariantNode : public DeclarationNode
    {
    private:
        std::string _name;
        std::vector<std::string> _associated_types; // Empty for simple variants like NAME_1

    public:
        EnumVariantNode(SourceLocation loc, std::string name)
            : DeclarationNode(NodeKind::Declaration, loc), _name(std::move(name)) {}

        EnumVariantNode(SourceLocation loc, std::string name, std::vector<std::string> associated_types)
            : DeclarationNode(NodeKind::Declaration, loc), _name(std::move(name)),
              _associated_types(std::move(associated_types)) {}

        const std::string &name() const { return _name; }
        const std::vector<std::string> &associated_types() const { return _associated_types; }
        bool is_simple_variant() const { return _associated_types.empty(); }

        void print(std::ostream &os, int indent = 0) const override
        {
            os << std::string(indent, ' ') << "EnumVariant: " << _name;
            if (!_associated_types.empty())
            {
                os << "(";
                for (size_t i = 0; i < _associated_types.size(); ++i)
                {
                    if (i > 0)
                        os << ", ";
                    os << _associated_types[i];
                }
                os << ")";
            }
            os << std::endl;
        }

        void accept(ASTVisitor &visitor) override;
    };

    // Enum declaration (supports both C-style and Rust-style)
    class EnumDeclarationNode : public DeclarationNode
    {
    private:
        std::string _name;
        std::vector<std::unique_ptr<EnumVariantNode>> _variants;
        std::vector<std::unique_ptr<GenericParameterNode>> _generic_parameters;

    public:
        EnumDeclarationNode(SourceLocation loc, std::string name)
            : DeclarationNode(NodeKind::EnumDeclaration, loc), _name(std::move(name)) {}

        const std::string &name() const { return _name; }
        const std::vector<std::unique_ptr<EnumVariantNode>> &variants() const { return _variants; }
        const std::vector<std::unique_ptr<GenericParameterNode>> &generic_parameters() const { return _generic_parameters; }

        void add_variant(std::unique_ptr<EnumVariantNode> variant)
        {
            _variants.push_back(std::move(variant));
        }

        void add_generic_parameter(std::unique_ptr<GenericParameterNode> param)
        {
            _generic_parameters.push_back(std::move(param));
        }

        // Check if this is a simple C-style enum (all variants are simple)
        bool is_simple_enum() const
        {
            return std::all_of(_variants.begin(), _variants.end(),
                               [](const auto &variant)
                               { return variant->is_simple_variant(); });
        }

        void print(std::ostream &os, int indent = 0) const override
        {
            os << std::string(indent, ' ') << "EnumDeclaration: " << _name;

            if (!_generic_parameters.empty())
            {
                os << "<";
                for (size_t i = 0; i < _generic_parameters.size(); ++i)
                {
                    if (i > 0)
                        os << ", ";
                    os << "T" << i; // Simplified for now
                }
                os << ">";
            }

            os << " {" << std::endl;

            for (const auto &variant : _variants)
            {
                variant->print(os, indent + 4);
            }

            os << std::string(indent, ' ') << "}" << std::endl;
        }

        void accept(ASTVisitor &visitor) override;
    };

    // Implementation block (impl StructName { ... })
    class ImplementationBlockNode : public DeclarationNode
    {
    private:
        std::string _target_type;
        std::vector<std::unique_ptr<StructFieldNode>> _field_implementations;
        std::vector<std::unique_ptr<StructMethodNode>> _method_implementations;

    public:
        ImplementationBlockNode(SourceLocation loc, std::string target_type)
            : DeclarationNode(NodeKind::ImplementationBlock, loc), _target_type(std::move(target_type)) {}

        const std::string &target_type() const { return _target_type; }
        const std::vector<std::unique_ptr<StructFieldNode>> &field_implementations() const { return _field_implementations; }
        const std::vector<std::unique_ptr<StructMethodNode>> &method_implementations() const { return _method_implementations; }

        void add_field_implementation(std::unique_ptr<StructFieldNode> field)
        {
            _field_implementations.push_back(std::move(field));
        }

        void add_method_implementation(std::unique_ptr<StructMethodNode> method)
        {
            _method_implementations.push_back(std::move(method));
        }

        void print(std::ostream &os, int indent = 0) const override
        {
            os << std::string(indent, ' ') << "ImplementationBlock for " << _target_type << std::endl;

            if (!_field_implementations.empty())
            {
                os << std::string(indent + 2, ' ') << "Field Implementations:" << std::endl;
                for (const auto &field : _field_implementations)
                {
                    if (field)
                        field->print(os, indent + 4);
                }
            }

            if (!_method_implementations.empty())
            {
                os << std::string(indent + 2, ' ') << "Method Implementations:" << std::endl;
                for (const auto &method : _method_implementations)
                {
                    if (method)
                        method->print(os, indent + 4);
                }
            }
        }

        void accept(ASTVisitor &visitor) override;
    };

    // External linkage block (extern "C" { ... })
    class ExternBlockNode : public DeclarationNode
    {
    private:
        std::string _linkage_type;  // "C" or other linkage types
        std::vector<std::unique_ptr<FunctionDeclarationNode>> _function_declarations;

    public:
        ExternBlockNode(SourceLocation loc, std::string linkage_type)
            : DeclarationNode(NodeKind::ExternBlock, loc), _linkage_type(std::move(linkage_type)) {}

        const std::string &linkage_type() const { return _linkage_type; }
        const std::vector<std::unique_ptr<FunctionDeclarationNode>> &function_declarations() const { return _function_declarations; }

        void add_function_declaration(std::unique_ptr<FunctionDeclarationNode> func_decl)
        {
            _function_declarations.push_back(std::move(func_decl));
        }

        void print(std::ostream &os, int indent = 0) const override
        {
            os << std::string(indent, ' ') << "ExternBlock linkage=\"" << _linkage_type << "\"" << std::endl;

            if (!_function_declarations.empty())
            {
                os << std::string(indent + 2, ' ') << "Function Declarations:" << std::endl;
                for (const auto &func : _function_declarations)
                {
                    if (func)
                        func->print(os, indent + 4);
                }
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

    // New expression (constructor call)
    class NewExpressionNode : public ExpressionNode
    {
    private:
        std::string _type_name;
        std::vector<std::string> _generic_args; // For generic types like GenericStruct<string>
        std::vector<std::unique_ptr<ExpressionNode>> _arguments;

    public:
        NewExpressionNode(SourceLocation loc, std::string type_name)
            : ExpressionNode(NodeKind::NewExpression, loc), _type_name(std::move(type_name)) {}

        const std::string &type_name() const { return _type_name; }
        const std::vector<std::string> &generic_args() const { return _generic_args; }
        const std::vector<std::unique_ptr<ExpressionNode>> &arguments() const { return _arguments; }

        void add_generic_arg(const std::string &type) { _generic_args.push_back(type); }

        void add_argument(std::unique_ptr<ExpressionNode> arg)
        {
            _arguments.push_back(std::move(arg));
        }

        void print(std::ostream &os, int indent = 0) const override
        {
            os << std::string(indent, ' ') << "NewExpression: " << _type_name;
            if (!_generic_args.empty())
            {
                os << "<";
                for (size_t i = 0; i < _generic_args.size(); ++i)
                {
                    if (i > 0)
                        os << ", ";
                    os << _generic_args[i];
                }
                os << ">";
            }
            os << std::endl;

            if (!_arguments.empty())
            {
                os << std::string(indent + 2, ' ') << "Arguments:" << std::endl;
                for (const auto &arg : _arguments)
                {
                    if (arg)
                        arg->print(os, indent + 4);
                }
            }
        }

        void accept(ASTVisitor &visitor) override;
    };

    // Array literal
    class ArrayLiteralNode : public ExpressionNode
    {
    private:
        std::vector<std::unique_ptr<ExpressionNode>> _elements;
        std::string _element_type; // Type of array elements

    public:
        ArrayLiteralNode(SourceLocation loc)
            : ExpressionNode(NodeKind::ArrayLiteral, loc) {}

        const std::vector<std::unique_ptr<ExpressionNode>> &elements() const { return _elements; }
        const std::string &element_type() const { return _element_type; }
        size_t size() const { return _elements.size(); }

        void add_element(std::unique_ptr<ExpressionNode> element)
        {
            _elements.push_back(std::move(element));
        }

        void set_element_type(const std::string &type)
        {
            _element_type = type;
        }

        void print(std::ostream &os, int indent = 0) const override
        {
            os << std::string(indent, ' ') << "ArrayLiteral[" << _elements.size() << "]";
            if (!_element_type.empty())
                os << " (" << _element_type << ")";
            os << ":" << std::endl;
            for (const auto &element : _elements)
            {
                if (element)
                    element->print(os, indent + 2);
            }
        }

        void accept(ASTVisitor &visitor) override;
    };

    // Array access
    class ArrayAccessNode : public ExpressionNode
    {
    private:
        std::unique_ptr<ExpressionNode> _array;
        std::unique_ptr<ExpressionNode> _index;

    public:
        ArrayAccessNode(SourceLocation loc, std::unique_ptr<ExpressionNode> array,
                        std::unique_ptr<ExpressionNode> index)
            : ExpressionNode(NodeKind::ArrayAccess, loc), _array(std::move(array)),
              _index(std::move(index)) {}

        ExpressionNode *array() const { return _array.get(); }
        ExpressionNode *index() const { return _index.get(); }

        void print(std::ostream &os, int indent = 0) const override
        {
            os << std::string(indent, ' ') << "ArrayAccess:" << std::endl;
            if (_array)
            {
                os << std::string(indent + 2, ' ') << "Array:" << std::endl;
                _array->print(os, indent + 4);
            }
            if (_index)
            {
                os << std::string(indent + 2, ' ') << "Index:" << std::endl;
                _index->print(os, indent + 4);
            }
        }

        void accept(ASTVisitor &visitor) override;
    };

    // Member access (dot notation like obj.member)
    class MemberAccessNode : public ExpressionNode
    {
    private:
        std::unique_ptr<ExpressionNode> _object;
        std::string _member;

    public:
        MemberAccessNode(SourceLocation loc, std::unique_ptr<ExpressionNode> object,
                         std::string member)
            : ExpressionNode(NodeKind::MemberAccess, loc), _object(std::move(object)),
              _member(std::move(member)) {}

        ExpressionNode *object() const { return _object.get(); }
        const std::string &member() const { return _member; }

        void print(std::ostream &os, int indent = 0) const override
        {
            os << std::string(indent, ' ') << "MemberAccess:" << std::endl;
            if (_object)
            {
                os << std::string(indent + 2, ' ') << "Object:" << std::endl;
                _object->print(os, indent + 4);
            }
            os << std::string(indent + 2, ' ') << "Member: " << _member << std::endl;
        }

        void accept(ASTVisitor &visitor) override;
    };

    // Scope resolution (for enum access like Color::RED)
    class ScopeResolutionNode : public ExpressionNode
    {
    private:
        std::string _scope_name;  // e.g., "Color"
        std::string _member_name; // e.g., "RED"

    public:
        ScopeResolutionNode(SourceLocation loc, std::string scope_name, std::string member_name)
            : ExpressionNode(NodeKind::ScopeResolution, loc),
              _scope_name(std::move(scope_name)), _member_name(std::move(member_name)) {}

        const std::string &scope_name() const { return _scope_name; }
        const std::string &member_name() const { return _member_name; }

        void print(std::ostream &os, int indent = 0) const override
        {
            os << std::string(indent, ' ') << "ScopeResolution: " << _scope_name
               << "::" << _member_name << std::endl;
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

    // Base pattern class
    class PatternNode : public ASTNode
    {
    public:
        PatternNode(NodeKind kind, SourceLocation loc) : ASTNode(kind, loc) {}
        void accept(ASTVisitor &visitor) override;
    };

    // Enum pattern (e.g., Shape::Circle(radius))
    class EnumPatternNode : public PatternNode
    {
    private:
        std::string _enum_name;
        std::string _variant_name;
        std::vector<std::string> _bound_variables;

    public:
        EnumPatternNode(SourceLocation loc, const std::string &enum_name, const std::string &variant_name)
            : PatternNode(NodeKind::EnumPattern, loc), _enum_name(enum_name), _variant_name(variant_name) {}

        void add_bound_variable(const std::string &var_name)
        {
            _bound_variables.push_back(var_name);
        }

        const std::string &enum_name() const { return _enum_name; }
        const std::string &variant_name() const { return _variant_name; }
        const std::vector<std::string> &bound_variables() const { return _bound_variables; }

        void print(std::ostream &os, int indent = 0) const override
        {
            os << std::string(indent, ' ') << "EnumPattern " << _enum_name << "::" << _variant_name;
            if (!_bound_variables.empty())
            {
                os << " (";
                for (size_t i = 0; i < _bound_variables.size(); ++i)
                {
                    if (i > 0)
                        os << ", ";
                    os << _bound_variables[i];
                }
                os << ")";
            }
            os << std::endl;
        }

        void accept(ASTVisitor &visitor) override;
    };

    // Match arm
    class MatchArmNode : public ASTNode
    {
    private:
        std::unique_ptr<PatternNode> _pattern;
        std::unique_ptr<StatementNode> _body;

    public:
        MatchArmNode(SourceLocation loc, std::unique_ptr<PatternNode> pattern, std::unique_ptr<StatementNode> body)
            : ASTNode(NodeKind::MatchArm, loc), _pattern(std::move(pattern)), _body(std::move(body)) {}

        PatternNode *pattern() const { return _pattern.get(); }
        StatementNode *body() const { return _body.get(); }

        void print(std::ostream &os, int indent = 0) const override
        {
            os << std::string(indent, ' ') << "MatchArm" << std::endl;
            if (_pattern)
            {
                os << std::string(indent + 2, ' ') << "Pattern:" << std::endl;
                _pattern->print(os, indent + 4);
            }
            if (_body)
            {
                os << std::string(indent + 2, ' ') << "Body:" << std::endl;
                _body->print(os, indent + 4);
            }
        }

        void accept(ASTVisitor &visitor) override;
    };

    // Match statement
    class MatchStatementNode : public StatementNode
    {
    private:
        std::unique_ptr<ExpressionNode> _expr;
        std::vector<std::unique_ptr<MatchArmNode>> _arms;

    public:
        MatchStatementNode(SourceLocation loc, std::unique_ptr<ExpressionNode> expr)
            : StatementNode(NodeKind::MatchStatement, loc), _expr(std::move(expr)) {}

        void add_arm(std::unique_ptr<MatchArmNode> arm)
        {
            _arms.push_back(std::move(arm));
        }

        ExpressionNode *expr() const { return _expr.get(); }
        const std::vector<std::unique_ptr<MatchArmNode>> &arms() const { return _arms; }

        void print(std::ostream &os, int indent = 0) const override
        {
            os << std::string(indent, ' ') << "Match" << std::endl;
            if (_expr)
            {
                os << std::string(indent + 2, ' ') << "Expression:" << std::endl;
                _expr->print(os, indent + 4);
            }
            for (const auto &arm : _arms)
            {
                os << std::string(indent + 2, ' ') << "Arm:" << std::endl;
                arm->print(os, indent + 4);
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