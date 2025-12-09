#pragma once
#include "Lexer/lexer.hpp"
#include "AST/Type.hpp"
#include <iostream>
#include <string>
#include <vector>
#include <memory>
#include <optional>
#include <algorithm>
#include <unordered_map>

namespace Cryo
{
    // Forward declaration for visitor pattern
    class ASTVisitor;

    // Forward declaration for type system (for migration support)
    class Type;

    // Forward declaration for directive system
    class DirectiveNode;

    // Forward declarations for pattern matching
    class MatchArmNode;
    class PatternNode;
    class EnumPatternNode;
    class GenericParameterNode;

    // Forward declarations for where clauses
    struct TraitBound
    {
        std::string type_parameter; // e.g., "T"
        std::string trait_name;     // e.g., "Default"
        SourceLocation location;

        TraitBound(std::string type_param, std::string trait, SourceLocation loc)
            : type_parameter(std::move(type_param)), trait_name(std::move(trait)), location(loc) {}
    };

    // Forward declarations for switch statements
    class SwitchStatementNode;
    class CaseStatementNode;

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
        SizeofExpression,
        StructLiteral,
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
        SwitchStatement,
        CaseStatement,
        BreakStatement,
        ContinueStatement,
        ExpressionStatement,
        DeclarationStatement,

        // Concrete declaration types
        VariableDeclaration,
        FunctionDeclaration,
        IntrinsicDeclaration,
        ImportDeclaration,
        StructDeclaration,
        ClassDeclaration,
        EnumDeclaration,
        TraitDeclaration,
        TypeAliasDeclaration,
        ImplementationBlock,
        ExternBlock,

        // Top-level
        Program,

        // Pattern matching specific
        MatchArm,
        Pattern,
        EnumPattern,

        // Directive system
        Directive,

        Unknown
    };

    inline std::string NodeKindToString(NodeKind kind)
    {
        switch (kind)
        {
        case NodeKind::Expression:
            return "Expression";
        case NodeKind::Statement:
            return "Statement";
        case NodeKind::Declaration:
            return "Declaration";
        case NodeKind::Literal:
            return "Literal";
        case NodeKind::Identifier:
            return "Identifier";
        case NodeKind::BinaryExpression:
            return "BinaryExpression";
        case NodeKind::UnaryExpression:
            return "UnaryExpression";
        case NodeKind::TernaryExpression:
            return "TernaryExpression";
        case NodeKind::CallExpression:
            return "CallExpression";
        case NodeKind::NewExpression:
            return "NewExpression";
        case NodeKind::SizeofExpression:
            return "SizeofExpression";
        case NodeKind::StructLiteral:
            return "StructLiteral";
        case NodeKind::ArrayLiteral:
            return "ArrayLiteral";
        case NodeKind::ArrayAccess:
            return "ArrayAccess";
        case NodeKind::MemberAccess:
            return "MemberAccess";
        case NodeKind::ScopeResolution:
            return "ScopeResolution";
        case NodeKind::BlockStatement:
            return "BlockStatement";
        case NodeKind::ReturnStatement:
            return "ReturnStatement";
        case NodeKind::IfStatement:
            return "IfStatement";
        case NodeKind::WhileStatement:
            return "WhileStatement";
        case NodeKind::ForStatement:
            return "ForStatement";
        case NodeKind::MatchStatement:
            return "MatchStatement";
        case NodeKind::SwitchStatement:
            return "SwitchStatement";
        case NodeKind::CaseStatement:
            return "CaseStatement";
        case NodeKind::BreakStatement:
            return "BreakStatement";
        case NodeKind::ContinueStatement:
            return "ContinueStatement";
        case NodeKind::ExpressionStatement:
            return "ExpressionStatement";
        case NodeKind::DeclarationStatement:
            return "DeclarationStatement";
        case NodeKind::VariableDeclaration:
            return "VariableDeclaration";
        case NodeKind::FunctionDeclaration:
            return "FunctionDeclaration";
        case NodeKind::IntrinsicDeclaration:
            return "IntrinsicDeclaration";
        case NodeKind::ImportDeclaration:
            return "ImportDeclaration";
        case NodeKind::StructDeclaration:
            return "StructDeclaration";
        case NodeKind::ClassDeclaration:
            return "ClassDeclaration";
        case NodeKind::EnumDeclaration:
            return "EnumDeclaration";
        case NodeKind::TraitDeclaration:
            return "TraitDeclaration";
        case NodeKind::TypeAliasDeclaration:
            return "TypeAliasDeclaration";
        case NodeKind::ImplementationBlock:
            return "ImplementationBlock";
        case NodeKind::ExternBlock:
            return "ExternBlock";
        case NodeKind::Program:
            return "Program";
        case NodeKind::MatchArm:
            return "MatchArm";
        case NodeKind::Pattern:
            return "Pattern";
        case NodeKind::EnumPattern:
            return "EnumPattern";
        case NodeKind::Directive:
            return "Directive";
        default:
            return "Unknown";
        }
    }

    class ASTNode
    {
    protected:
        NodeKind _kind;
        SourceLocation _location;
        mutable bool _has_error = false; // Track if this node already has errors reported

    private:
        struct DirectiveStorage; // PIMPL to avoid circular dependency
        std::unique_ptr<DirectiveStorage> _directive_storage;

    public:
        ASTNode(NodeKind kind, SourceLocation location);
        virtual ~ASTNode();

        // Copy constructor and assignment operator
        ASTNode(const ASTNode &other);
        ASTNode &operator=(const ASTNode &other);

        // Move constructor and assignment operator
        ASTNode(ASTNode &&other) noexcept;
        ASTNode &operator=(ASTNode &&other) noexcept;

        NodeKind kind() const { return _kind; }
        const SourceLocation &location() const { return _location; }

        // Error tracking to prevent duplicate error reports
        bool has_error() const { return _has_error; }
        void mark_error() const { _has_error = true; }
        void clear_error() const { _has_error = false; }

        // Directive support
        void attach_directive(std::unique_ptr<DirectiveNode> directive);
        const std::vector<std::unique_ptr<DirectiveNode>> &get_directives() const;
        bool has_directives() const;

        template <typename T>
        std::vector<T *> get_directives_of_type() const
        {
            std::vector<T *> result;
            for (const auto &directive : get_directives())
            {
                if (auto casted = dynamic_cast<T *>(directive.get()))
                {
                    result.push_back(casted);
                }
            }
            return result;
        }

        virtual void print(std::ostream &os, int indent = 0) const = 0;
        virtual void accept(ASTVisitor &visitor) = 0;
    };

    // Concrete node types
    class ExpressionNode : public ASTNode
    {
    private:
        // Core type system - NO STRING OPERATIONS
        Cryo::Type *_resolved_type = nullptr; // Primary type storage

    public:
        ExpressionNode(NodeKind kind, SourceLocation loc) : ASTNode(kind, loc) {}

        // Core type system access - PREFERRED
        Cryo::Type *get_resolved_type() const { return _resolved_type; }
        void set_resolved_type(Cryo::Type *type) { _resolved_type = type; }
        bool has_resolved_type() const { return _resolved_type != nullptr; }

        // DEPRECATED: String-based type operations - REMOVE THESE
        [[deprecated("Use set_resolved_type() instead - string operations being eliminated")]]
        void set_type(const std::string &type)
        {
            // This should not be used - parser must resolve to Type* directly
        }

        [[deprecated("Use get_resolved_type() instead - string operations being eliminated")]]
        std::optional<std::string> type() const
        {
            if (_resolved_type)
            {
                return _resolved_type->to_string();
            }
            return std::nullopt;
        }

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
    private:
        std::string _documentation; // Documentation comment associated with this declaration
        std::string _source_module; // Track which module this declaration originated from (empty = current module)

    public:
        DeclarationNode(NodeKind kind, SourceLocation loc) : ASTNode(kind, loc) {}

        // Documentation support
        const std::string &documentation() const { return _documentation; }
        void set_documentation(const std::string &doc) { _documentation = doc; }
        bool has_documentation() const { return !_documentation.empty(); }

        // Source module tracking
        const std::string &source_module() const { return _source_module; }
        void set_source_module(const std::string &module) { _source_module = module; }
        bool is_from_import() const { return !_source_module.empty(); }

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
        bool _is_global_reference = false; // whether this identifier references a global variable

    public:
        IdentifierNode(NodeKind kind, SourceLocation loc, std::string name)
            : ExpressionNode(kind, loc), _name(std::move(name)) {}

        const std::string &name() const { return _name; }
        bool is_global_reference() const { return _is_global_reference; }

        // Scope setters
        void set_global_reference(bool is_global) { _is_global_reference = is_global; }

        void print(std::ostream &os, int indent = 0) const override
        {
            os << std::string(indent, ' ') << "Identifier: " << _name;
            if (_is_global_reference)
                os << " (global)";
            os << std::endl;
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
        std::unique_ptr<ExpressionNode> _initializer;
        bool _is_mutable = false; // const vs mut
        bool _is_auto = false;    // auto type inference
        bool _is_global = false;  // whether this variable is declared at global scope

        // Core type system - NO STRING OPERATIONS
        Cryo::Type *_resolved_type = nullptr; // Primary type storage

    public:
        VariableDeclarationNode(SourceLocation loc, std::string name,
                                Cryo::Type *resolved_type = nullptr,
                                std::unique_ptr<ExpressionNode> init = nullptr,
                                bool is_mutable = false,
                                bool is_global = false)
            : DeclarationNode(NodeKind::VariableDeclaration, loc),
              _name(std::move(name)), _initializer(std::move(init)),
              _is_mutable(is_mutable), _is_global(is_global), _resolved_type(resolved_type)
        {
            _is_auto = (resolved_type == nullptr); // Auto if no type provided
        }

        const std::string &name() const { return _name; }
        ExpressionNode *initializer() const { return _initializer.get(); }
        bool is_mutable() const { return _is_mutable; }
        bool is_auto() const { return _is_auto; }
        bool has_initializer() const { return _initializer != nullptr; }
        bool is_global() const { return _is_global; }

        // Core type system access - PREFERRED
        Cryo::Type *get_resolved_type() const { return _resolved_type; }
        void set_resolved_type(Cryo::Type *type)
        {
            _resolved_type = type;
            _is_auto = (type == nullptr);
        }
        bool has_resolved_type() const { return _resolved_type != nullptr; }

        // DEPRECATED: String-based type operations - REMOVE THESE
        [[deprecated("Use get_resolved_type() instead - string operations being eliminated")]]
        const std::string type_annotation() const
        {
            return _resolved_type ? _resolved_type->to_string() : "auto";
        }

        [[deprecated("Use set_resolved_type() instead - string operations being eliminated")]]
        void set_type_annotation(const std::string &type)
        {
            // This should not be used - parser must resolve to Type* directly
            _is_auto = (type == "auto" || type.empty());
        }

        // Scope setters
        void set_global(bool is_global) { _is_global = is_global; }

        void print(std::ostream &os, int indent = 0) const override
        {
            os << std::string(indent, ' ') << "VariableDecl: "
               << (_is_global ? "global " : "")
               << (_is_mutable ? "mut " : "const ") << _name;
            if (_resolved_type)
                os << ": " << _resolved_type->to_string();
            else if (_is_auto)
                os << ": auto";
            os << std::endl;
            if (_initializer)
                _initializer->print(os, indent + 2);
        }

        void accept(ASTVisitor &visitor) override;
    };

    // Intrinsic function declaration
    class IntrinsicDeclarationNode : public DeclarationNode
    {
    private:
        std::string _name;
        std::vector<std::unique_ptr<VariableDeclarationNode>> _parameters;
        std::unique_ptr<BlockStatementNode> _body; // Optional implementation for compiler hints

        // Core type system - NO STRING OPERATIONS
        Cryo::Type *_resolved_return_type = nullptr; // Primary return type storage

    public:
        IntrinsicDeclarationNode(SourceLocation loc, std::string name,
                                 Cryo::Type *return_type = nullptr)
            : DeclarationNode(NodeKind::IntrinsicDeclaration, loc),
              _name(std::move(name)), _resolved_return_type(return_type) {}

        const std::string &name() const { return _name; }
        const std::vector<std::unique_ptr<VariableDeclarationNode>> &parameters() const { return _parameters; }
        BlockStatementNode *body() const { return _body.get(); }
        size_t parameter_count() const { return _parameters.size(); }

        // Core type system access - PREFERRED
        Cryo::Type *get_resolved_return_type() const { return _resolved_return_type; }
        void set_resolved_return_type(Cryo::Type *type) { _resolved_return_type = type; }
        bool has_resolved_return_type() const { return _resolved_return_type != nullptr; }

        const std::string return_type_annotation() const
        {
            return _resolved_return_type ? _resolved_return_type->to_string() : "undefined";
        }

        [[deprecated("Use set_resolved_return_type() instead - this will not do anything when called.")]]
        void set_return_type(const std::string &return_type)
        {
            // This should not be used - parser must resolve to Type* directly
        }

        void add_parameter(std::unique_ptr<VariableDeclarationNode> param)
        {
            _parameters.push_back(std::move(param));
        }

        void set_body(std::unique_ptr<BlockStatementNode> body)
        {
            _body = std::move(body);
        }

        void print(std::ostream &os, int indent = 0) const override
        {
            os << std::string(indent, ' ') << "IntrinsicDecl: " << _name << "(";
            for (size_t i = 0; i < _parameters.size(); ++i)
            {
                if (i > 0)
                    os << ", ";
                if (_parameters[i])
                    os << (_parameters[i]->get_resolved_type() ? _parameters[i]->get_resolved_type()->to_string() : "unknown") << " " << _parameters[i]->name();
            }
            os << ") -> " << (_resolved_return_type ? _resolved_return_type->to_string() : "void") << std::endl;

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
                os << std::string(indent + 2, ' ') << "Implementation Hint:" << std::endl;
                _body->print(os, indent + 4);
            }
        }

        void accept(ASTVisitor &visitor) override;
    };

    // Import declaration node for module importing
    class ImportDeclarationNode : public DeclarationNode
    {
    public:
        enum class ImportType
        {
            Relative, // "./path" or "../path" for relative files
            Absolute  // <path> for standard library (stdlib/ prefix assumed)
        };

        enum class ImportStyle
        {
            WildcardImport, // import <core/types>; or import * from <core/types>;
            SpecificImport  // import IO from <io/stdio>;
        };

    private:
        std::string _path;                          // The import path (file path or module name)
        std::string _alias;                         // Optional alias (for "as" keyword)
        std::vector<std::string> _specific_imports; // Specific symbols to import (for "import X from")
        ImportType _import_type;                    // Type of import
        ImportStyle _import_style;                  // Style of import (wildcard vs specific)
        bool _has_alias;                            // Whether an alias was specified

    public:
        ImportDeclarationNode(SourceLocation loc, std::string path, ImportType type)
            : DeclarationNode(NodeKind::ImportDeclaration, loc),
              _path(std::move(path)), _import_type(type), _import_style(ImportStyle::WildcardImport), _has_alias(false) {}

        ImportDeclarationNode(SourceLocation loc, std::string path, std::string alias, ImportType type)
            : DeclarationNode(NodeKind::ImportDeclaration, loc),
              _path(std::move(path)), _alias(std::move(alias)), _import_type(type), _import_style(ImportStyle::WildcardImport), _has_alias(true) {}

        // Constructor for specific imports (import X from <path>)
        ImportDeclarationNode(SourceLocation loc, std::vector<std::string> specific_imports, std::string path, ImportType type)
            : DeclarationNode(NodeKind::ImportDeclaration, loc),
              _path(std::move(path)), _specific_imports(std::move(specific_imports)), _import_type(type), _import_style(ImportStyle::SpecificImport), _has_alias(false) {}

        const std::string &path() const { return _path; }
        const std::string &alias() const { return _alias; }
        const std::vector<std::string> &specific_imports() const { return _specific_imports; }
        ImportType import_type() const { return _import_type; }
        ImportStyle import_style() const { return _import_style; }
        bool has_alias() const { return _has_alias; }
        bool is_specific_import() const { return _import_style == ImportStyle::SpecificImport; }
        bool is_wildcard_import() const { return _import_style == ImportStyle::WildcardImport; }

        void print(std::ostream &os, int indent = 0) const override
        {
            os << std::string(indent, ' ') << "ImportDecl: ";

            if (_import_style == ImportStyle::SpecificImport)
            {
                for (size_t i = 0; i < _specific_imports.size(); ++i)
                {
                    if (i > 0)
                        os << ", ";
                    os << _specific_imports[i];
                }
                os << " from ";
            }

            if (_import_type == ImportType::Relative)
                os << "\"" << _path << "\"";
            else if (_import_type == ImportType::Absolute)
                os << "<" << _path << ">";

            if (_has_alias)
                os << " as " << _alias;
            os << std::endl;
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

    // Function declaration
    class FunctionDeclarationNode : public DeclarationNode
    {
    private:
        std::string _name;
        std::vector<std::unique_ptr<VariableDeclarationNode>> _parameters;
        std::vector<std::unique_ptr<GenericParameterNode>> _generic_parameters;
        std::vector<TraitBound> _trait_bounds; // Where clause trait bounds
        std::unique_ptr<BlockStatementNode> _body;
        bool _is_public = false; // Visibility
        bool _is_static = false;
        bool _is_inline = false;
        bool _is_variadic = false; // Variadic function (...args)

        // Core type system - NO STRING OPERATIONS
        Cryo::Type *_resolved_return_type = nullptr; // Primary return type storage

    public:
        FunctionDeclarationNode(SourceLocation loc, std::string name,
                                Cryo::Type *return_type = nullptr,
                                bool is_public = false)
            : DeclarationNode(NodeKind::FunctionDeclaration, loc),
              _name(std::move(name)), _resolved_return_type(return_type),
              _is_public(is_public) {}

        const std::string &name() const { return _name; }
        const std::vector<std::unique_ptr<VariableDeclarationNode>> &parameters() const { return _parameters; }
        const std::vector<std::unique_ptr<GenericParameterNode>> &generic_parameters() const { return _generic_parameters; }
        const std::vector<TraitBound> &trait_bounds() const { return _trait_bounds; }
        BlockStatementNode *body() const { return _body.get(); }

        bool is_public() const { return _is_public; }
        bool is_static() const { return _is_static; }
        bool is_inline() const { return _is_inline; }
        bool is_variadic() const { return _is_variadic; }

        // Core type system access - PREFERRED
        Cryo::Type *get_resolved_return_type() const { return _resolved_return_type; }
        void set_resolved_return_type(Cryo::Type *type) { _resolved_return_type = type; }
        bool has_resolved_return_type() const { return _resolved_return_type != nullptr; }

        // DEPRECATED: String-based type operations - REMOVE THESE
        [[deprecated("Use get_resolved_return_type() instead - string operations being eliminated")]]
        const std::string return_type_annotation() const
        {
            if (_resolved_return_type)
            {
                // Apply defensive programming to avoid virtual function crashes
                std::string type_str = "CORRUPTED_TYPE";
                std::string type_name = "CORRUPTED_TYPE";

                try
                {
                    type_str = _resolved_return_type->to_string();
                    type_name = _resolved_return_type->name();
                }
                catch (...)
                {
                    type_str = "void";
                    type_name = "void";
                }

                // Safety check: if to_string() returns empty, fall back to the name
                if (type_str.empty() && !type_name.empty())
                {
                    return type_name;
                }
                // If both are empty, this means the type resolution failed during parsing
                // Return a placeholder that the TypeChecker can properly resolve
                if (type_str.empty())
                {
                    return "UNRESOLVED_TYPE";
                }
                return type_str;
            }
            return "void";
        }
        size_t parameter_count() const { return _parameters.size(); }

        void add_parameter(std::unique_ptr<VariableDeclarationNode> param)
        {
            _parameters.push_back(std::move(param));
        }

        void add_generic_parameter(std::unique_ptr<GenericParameterNode> param)
        {
            _generic_parameters.push_back(std::move(param));
        }

        void add_trait_bound(const TraitBound &bound)
        {
            _trait_bounds.push_back(bound);
        }

        void set_body(std::unique_ptr<BlockStatementNode> body)
        {
            _body = std::move(body);
        }

        [[deprecated("Use set_resolved_return_type() instead - string operations being eliminated")]]
        void set_return_type(const std::string &return_type)
        {
            // This should not be used - parser must resolve to Type* directly
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
                    os << (_parameters[i]->get_resolved_type() ? _parameters[i]->get_resolved_type()->to_string() : "unknown") << " " << _parameters[i]->name();
            }
            if (_is_variadic)
                os << "...";
            os << ") -> " << (_resolved_return_type ? _resolved_return_type->to_string() : "void") << std::endl;

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

    // Struct field declaration
    class StructFieldNode : public DeclarationNode
    {
    private:
        std::string _name;
        Visibility _visibility;
        std::unique_ptr<ExpressionNode> _default_value;

        // Core type system - NO STRING OPERATIONS
        Cryo::Type *_resolved_type = nullptr; // Primary type storage

    public:
        StructFieldNode(SourceLocation loc, std::string name, Cryo::Type *resolved_type,
                        Visibility visibility = Visibility::Public)
            : DeclarationNode(NodeKind::VariableDeclaration, loc),
              _name(std::move(name)), _resolved_type(resolved_type),
              _visibility(visibility) {}

        const std::string &name() const { return _name; }
        Visibility visibility() const { return _visibility; }
        ExpressionNode *default_value() const { return _default_value.get(); }

        // Core type system access - PREFERRED
        Cryo::Type *get_resolved_type() const { return _resolved_type; }
        void set_resolved_type(Cryo::Type *type) { _resolved_type = type; }
        bool has_resolved_type() const { return _resolved_type != nullptr; }

        // DEPRECATED: String-based type operations - REMOVE THESE
        [[deprecated("Use get_resolved_type() instead - string operations being eliminated")]]
        const std::string type_annotation() const
        {
            return _resolved_type ? _resolved_type->to_string() : "unknown";
        }

        [[deprecated("Use get_resolved_type() instead - string operations being eliminated")]]
        const std::string &field_type() const
        {
            static std::string temp = _resolved_type ? _resolved_type->to_string() : "unknown";
            return temp;
        }

        [[deprecated("Use set_resolved_type() instead - string operations being eliminated")]]
        void set_type(const std::string &type)
        {
            // This should not be used - parser must resolve to Type* directly
        }

        bool is_mutable() const { return _visibility != Visibility::Private; }

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
            os << _name << ": " << (_resolved_type ? _resolved_type->to_string() : "unknown");
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
        bool _is_destructor;
        bool _is_static;
        bool _is_default_destructor; // For ~TypeName() default; syntax

    public:
        StructMethodNode(SourceLocation loc, std::string name, Cryo::Type *return_type,
                         Visibility visibility = Visibility::Public, bool is_constructor = false,
                         bool is_destructor = false, bool is_static = false, bool is_default_destructor = false)
            : FunctionDeclarationNode(loc, std::move(name), return_type),
              _visibility(visibility), _is_constructor(is_constructor),
              _is_destructor(is_destructor), _is_static(is_static), _is_default_destructor(is_default_destructor) {}

        Visibility visibility() const { return _visibility; }
        bool is_constructor() const { return _is_constructor; }
        bool is_destructor() const { return _is_destructor; }
        bool is_static() const { return _is_static; }
        bool is_default_destructor() const { return _is_default_destructor; }

        void print(std::ostream &os, int indent = 0) const override
        {
            os << std::string(indent, ' ') << "Method: ";
            if (_visibility == Visibility::Private)
                os << "private ";
            else if (_visibility == Visibility::Protected)
                os << "protected ";
            if (_is_static)
                os << "static ";
            if (_is_constructor)
                os << "constructor ";
            if (_is_destructor)
            {
                os << "destructor ";
                if (_is_default_destructor)
                    os << "(default) ";
            }

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

    // Structure to represent a base trait in inheritance
    struct BaseTraitInfo
    {
        std::string name;
        std::vector<std::string> type_parameters;

        BaseTraitInfo(std::string trait_name, std::vector<std::string> params = {})
            : name(std::move(trait_name)), type_parameters(std::move(params)) {}
    };

    // Trait declaration
    class TraitDeclarationNode : public DeclarationNode
    {
    private:
        std::string _name;
        std::vector<std::unique_ptr<GenericParameterNode>> _generic_parameters;
        std::vector<std::unique_ptr<FunctionDeclarationNode>> _methods;
        std::vector<BaseTraitInfo> _base_traits; // Base traits this trait inherits from

    public:
        TraitDeclarationNode(SourceLocation loc, std::string name)
            : DeclarationNode(NodeKind::TraitDeclaration, loc), _name(std::move(name)) {}

        const std::string &name() const { return _name; }
        const std::vector<std::unique_ptr<GenericParameterNode>> &generic_parameters() const { return _generic_parameters; }
        const std::vector<std::unique_ptr<FunctionDeclarationNode>> &methods() const { return _methods; }
        const std::vector<BaseTraitInfo> &base_traits() const { return _base_traits; }

        void add_generic_parameter(std::unique_ptr<GenericParameterNode> param)
        {
            _generic_parameters.push_back(std::move(param));
        }

        void add_method(std::unique_ptr<FunctionDeclarationNode> method)
        {
            _methods.push_back(std::move(method));
        }

        void add_base_trait(BaseTraitInfo base_trait)
        {
            _base_traits.push_back(std::move(base_trait));
        }

        void print(std::ostream &os, int indent = 0) const override
        {
            os << std::string(indent, ' ') << "TraitDecl: " << _name;
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

            // Display base traits
            if (!_base_traits.empty())
            {
                os << " : ";
                for (size_t i = 0; i < _base_traits.size(); ++i)
                {
                    if (i > 0)
                        os << ", ";
                    os << _base_traits[i].name;
                    if (!_base_traits[i].type_parameters.empty())
                    {
                        os << "<";
                        for (size_t j = 0; j < _base_traits[i].type_parameters.size(); ++j)
                        {
                            if (j > 0)
                                os << ", ";
                            os << _base_traits[i].type_parameters[j];
                        }
                        os << ">";
                    }
                }
            }

            os << std::endl;

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

    // Type alias declaration (type Foo = Bar, type Ptr<T> = T*)
    class TypeAliasDeclarationNode : public DeclarationNode
    {
    private:
        std::string _alias_name;
        std::vector<std::string> _generic_params; // Generic parameters like <T, U>

        // Core type system - NO STRING OPERATIONS
        Cryo::Type *_resolved_target_type = nullptr; // Primary target type storage

    public:
        TypeAliasDeclarationNode(SourceLocation loc, std::string alias_name,
                                 Cryo::Type *target_type = nullptr,
                                 std::vector<std::string> generic_params = {})
            : DeclarationNode(NodeKind::TypeAliasDeclaration, loc),
              _alias_name(std::move(alias_name)), _resolved_target_type(target_type),
              _generic_params(std::move(generic_params)) {}

        const std::string &alias_name() const { return _alias_name; }
        const std::vector<std::string> &generic_params() const { return _generic_params; }
        bool is_generic() const { return !_generic_params.empty(); }

        // Core type system access - PREFERRED
        Cryo::Type *get_resolved_target_type() const { return _resolved_target_type; }
        void set_resolved_target_type(Cryo::Type *type) { _resolved_target_type = type; }
        bool has_resolved_target_type() const { return _resolved_target_type != nullptr; }

        // DEPRECATED: String-based type operations - REMOVE THESE
        [[deprecated("Use get_resolved_target_type() instead - string operations being eliminated")]]
        const std::string target_type() const
        {
            return _resolved_target_type ? _resolved_target_type->to_string() : "unknown";
        }

        [[deprecated("Use set_resolved_target_type() instead - string operations being eliminated")]]
        void set_type(const std::string &type)
        {
            // This should not be used - parser must resolve to Type* directly
        }

        void print(std::ostream &os, int indent = 0) const override
        {
            os << std::string(indent, ' ') << "TypeAlias: " << _alias_name;
            if (!_generic_params.empty())
            {
                os << "<";
                for (size_t i = 0; i < _generic_params.size(); ++i)
                {
                    if (i > 0)
                        os << ", ";
                    os << _generic_params[i];
                }
                os << ">";
            }
            os << " = " << (_resolved_target_type ? _resolved_target_type->to_string() : "unknown") << std::endl;
        }

        void accept(ASTVisitor &visitor) override;
    };

    // Enum variant (for both simple and complex variants)
    class EnumVariantNode : public DeclarationNode
    {
    private:
        std::string _name;
        std::vector<std::string> _associated_types; // Empty for simple variants like NAME_1
        std::optional<int64_t> _explicit_value;     // For C-style enums with explicit values

    public:
        EnumVariantNode(SourceLocation loc, std::string name)
            : DeclarationNode(NodeKind::Declaration, loc), _name(std::move(name)) {}

        EnumVariantNode(SourceLocation loc, std::string name, std::vector<std::string> associated_types)
            : DeclarationNode(NodeKind::Declaration, loc), _name(std::move(name)),
              _associated_types(std::move(associated_types)) {}

        EnumVariantNode(SourceLocation loc, std::string name, int64_t explicit_value)
            : DeclarationNode(NodeKind::Declaration, loc), _name(std::move(name)), _explicit_value(explicit_value) {}

        const std::string &name() const { return _name; }
        const std::vector<std::string> &associated_types() const { return _associated_types; }
        bool is_simple_variant() const { return _associated_types.empty(); }
        bool has_explicit_value() const { return _explicit_value.has_value(); }
        int64_t explicit_value() const { return _explicit_value.value_or(0); }

        void set_explicit_value(int64_t value) { _explicit_value = value; }

        void print(std::ostream &os, int indent = 0) const override
        {
            os << std::string(indent, ' ') << "EnumVariant: " << _name;
            if (has_explicit_value())
            {
                os << " = " << explicit_value();
            }
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
        std::string _linkage_type; // "C" or other linkage types
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

    //===----------------------------------------------------------------------===//
    // SizeofExpressionNode
    //===----------------------------------------------------------------------===//
    class SizeofExpressionNode : public ExpressionNode
    {
    private:
        std::string _type_name;

    public:
        SizeofExpressionNode(SourceLocation loc, std::string type_name)
            : ExpressionNode(NodeKind::SizeofExpression, loc), _type_name(std::move(type_name)) {}

        const std::string &type_name() const { return _type_name; }

        void print(std::ostream &os, int indent = 0) const override
        {
            os << std::string(indent, ' ') << "SizeofExpression: " << _type_name << std::endl;
        }

        void accept(ASTVisitor &visitor) override;
    };

    // Field initializer for struct literals
    class FieldInitializerNode
    {
    private:
        std::string _field_name;
        std::unique_ptr<ExpressionNode> _value;

    public:
        FieldInitializerNode(std::string field_name, std::unique_ptr<ExpressionNode> value)
            : _field_name(std::move(field_name)), _value(std::move(value)) {}

        const std::string &field_name() const { return _field_name; }
        ExpressionNode *value() const { return _value.get(); }

        void print(std::ostream &os, int indent = 0) const
        {
            os << std::string(indent, ' ') << "Field: " << _field_name << " = ";
            if (_value)
                _value->print(os, 0);
            os << std::endl;
        }
    };

    // Struct literal ({field: value, ...})
    class StructLiteralNode : public ExpressionNode
    {
    private:
        std::string _struct_type;
        std::vector<std::string> _generic_args; // For generic types
        std::vector<std::unique_ptr<FieldInitializerNode>> _field_initializers;

    public:
        StructLiteralNode(SourceLocation loc, std::string struct_type)
            : ExpressionNode(NodeKind::StructLiteral, loc), _struct_type(std::move(struct_type)) {}

        const std::string &struct_type() const { return _struct_type; }
        const std::vector<std::string> &generic_args() const { return _generic_args; }
        const std::vector<std::unique_ptr<FieldInitializerNode>> &field_initializers() const { return _field_initializers; }

        void add_generic_arg(const std::string &type) { _generic_args.push_back(type); }

        void add_field_initializer(std::unique_ptr<FieldInitializerNode> initializer)
        {
            _field_initializers.push_back(std::move(initializer));
        }

        void print(std::ostream &os, int indent = 0) const override
        {
            os << std::string(indent, ' ') << "StructLiteral: " << _struct_type;
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
            os << " {" << std::endl;

            for (const auto &init : _field_initializers)
            {
                if (init)
                    init->print(os, indent + 2);
            }

            os << std::string(indent, ' ') << "}" << std::endl;
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
        enum class PatternType
        {
            Literal,
            Identifier,
            Wildcard,
            Enum
        };

    private:
        PatternType _pattern_type;
        std::unique_ptr<LiteralNode> _literal_value;
        std::string _identifier;
        bool _is_wildcard = false;

    public:
        PatternNode(SourceLocation loc) : ASTNode(NodeKind::Pattern, loc), _pattern_type(PatternType::Wildcard) {}
        PatternNode(NodeKind kind, SourceLocation loc) : ASTNode(kind, loc), _pattern_type(PatternType::Enum) {}

        // Setters for different pattern types
        void set_literal_value(std::unique_ptr<LiteralNode> literal)
        {
            _literal_value = std::move(literal);
            _pattern_type = PatternType::Literal;
        }

        void set_identifier(const std::string &identifier)
        {
            _identifier = identifier;
            _pattern_type = PatternType::Identifier;
        }

        void set_wildcard(bool wildcard)
        {
            _is_wildcard = wildcard;
            _pattern_type = PatternType::Wildcard;
        }

        // Getters
        PatternType pattern_type() const { return _pattern_type; }
        LiteralNode *literal_value() const { return _literal_value.get(); }
        const std::string &identifier() const { return _identifier; }
        bool is_wildcard() const { return _is_wildcard; }

        void print(std::ostream &os, int indent = 0) const override
        {
            os << std::string(indent, ' ') << "Pattern";
            switch (_pattern_type)
            {
            case PatternType::Literal:
                os << " (Literal)";
                if (_literal_value)
                {
                    os << std::endl;
                    _literal_value->print(os, indent + 2);
                }
                break;
            case PatternType::Identifier:
                os << " (Identifier: " << _identifier << ")";
                break;
            case PatternType::Wildcard:
                os << " (Wildcard)";
                break;
            case PatternType::Enum:
                os << " (Enum)";
                break;
            }
            os << std::endl;
        }

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

    // Case statement
    class CaseStatementNode : public StatementNode
    {
    private:
        std::unique_ptr<ExpressionNode> _value;                  // Value to match (nullptr for default case)
        std::vector<std::unique_ptr<StatementNode>> _statements; // Statements in this case

    public:
        CaseStatementNode(SourceLocation loc, std::unique_ptr<ExpressionNode> value,
                          std::vector<std::unique_ptr<StatementNode>> statements)
            : StatementNode(NodeKind::CaseStatement, loc), _value(std::move(value)),
              _statements(std::move(statements)) {}

        // Constructor for default case
        CaseStatementNode(SourceLocation loc, std::vector<std::unique_ptr<StatementNode>> statements)
            : StatementNode(NodeKind::CaseStatement, loc), _value(nullptr),
              _statements(std::move(statements)) {}

        ExpressionNode *value() const { return _value.get(); }
        const std::vector<std::unique_ptr<StatementNode>> &statements() const { return _statements; }
        bool is_default() const { return _value == nullptr; }

        void print(std::ostream &os, int indent = 0) const override
        {
            if (is_default())
            {
                os << std::string(indent, ' ') << "Default:" << std::endl;
            }
            else
            {
                os << std::string(indent, ' ') << "Case:" << std::endl;
                if (_value)
                {
                    os << std::string(indent + 2, ' ') << "Value:" << std::endl;
                    _value->print(os, indent + 4);
                }
            }
            os << std::string(indent + 2, ' ') << "Statements:" << std::endl;
            for (const auto &stmt : _statements)
            {
                stmt->print(os, indent + 4);
            }
        }

        void accept(ASTVisitor &visitor) override;
    };

    // Switch statement
    class SwitchStatementNode : public StatementNode
    {
    private:
        std::unique_ptr<ExpressionNode> _expression;
        std::vector<std::unique_ptr<CaseStatementNode>> _cases;

    public:
        SwitchStatementNode(SourceLocation loc, std::unique_ptr<ExpressionNode> expression,
                            std::vector<std::unique_ptr<CaseStatementNode>> cases)
            : StatementNode(NodeKind::SwitchStatement, loc), _expression(std::move(expression)),
              _cases(std::move(cases)) {}

        ExpressionNode *expression() const { return _expression.get(); }
        const std::vector<std::unique_ptr<CaseStatementNode>> &cases() const { return _cases; }

        void print(std::ostream &os, int indent = 0) const override
        {
            os << std::string(indent, ' ') << "Switch:" << std::endl;
            if (_expression)
            {
                os << std::string(indent + 2, ' ') << "Expression:" << std::endl;
                _expression->print(os, indent + 4);
            }
            os << std::string(indent + 2, ' ') << "Cases:" << std::endl;
            for (const auto &case_stmt : _cases)
            {
                case_stmt->print(os, indent + 4);
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

    //===----------------------------------------------------------------------===//
    // Directive System AST Nodes
    //===----------------------------------------------------------------------===//

    // Base class for all directive nodes
    class DirectiveNode : public ASTNode
    {
    protected:
        std::string _directive_name;
        std::unordered_map<std::string, std::string> _arguments;

    public:
        DirectiveNode(SourceLocation loc, const std::string &name)
            : ASTNode(NodeKind::Directive, loc), _directive_name(name) {}

        const std::string &name() const { return _directive_name; }

        void add_argument(const std::string &key, const std::string &value)
        {
            _arguments[key] = value;
        }

        std::optional<std::string> get_argument(const std::string &key) const
        {
            auto it = _arguments.find(key);
            return (it != _arguments.end()) ? std::make_optional(it->second) : std::nullopt;
        }

        const std::unordered_map<std::string, std::string> &arguments() const
        {
            return _arguments;
        }

        void print(std::ostream &os, int indent = 0) const override
        {
            os << std::string(indent, ' ') << "Directive[" << _directive_name << "]";
            if (!_arguments.empty())
            {
                os << " {";
                bool first = true;
                for (const auto &[key, value] : _arguments)
                {
                    if (!first)
                        os << ", ";
                    os << key << "=" << value;
                    first = false;
                }
                os << "}";
            }
            os << std::endl;
        }

        void accept(ASTVisitor &visitor) override;
    };

    // Test directive node - #[test(name="...", category="...")]
    class TestDirectiveNode : public DirectiveNode
    {
    private:
        std::string _test_name;
        std::string _test_category;

    public:
        TestDirectiveNode(SourceLocation loc, const std::string &name = "", const std::string &category = "")
            : DirectiveNode(loc, "test"), _test_name(name), _test_category(category)
        {
            if (!name.empty())
                add_argument("name", name);
            if (!category.empty())
                add_argument("category", category);
        }

        const std::string &test_name() const { return _test_name; }
        const std::string &test_category() const { return _test_category; }

        void set_test_name(const std::string &name)
        {
            _test_name = name;
            add_argument("name", name);
        }

        void set_test_category(const std::string &category)
        {
            _test_category = category;
            add_argument("category", category);
        }

        void print(std::ostream &os, int indent = 0) const override
        {
            os << std::string(indent, ' ') << "TestDirective[name=" << _test_name
               << ", category=" << _test_category << "]" << std::endl;
        }
    };

    // Expect error directive node - #[expect_error("ErrorCode")] or #[expect_errors("E1", "E2")]
    class ExpectErrorDirectiveNode : public DirectiveNode
    {
    private:
        std::vector<std::string> _expected_error_codes;

    public:
        ExpectErrorDirectiveNode(SourceLocation loc, const std::vector<std::string> &error_codes)
            : DirectiveNode(loc, error_codes.size() == 1 ? "expect_error" : "expect_errors"),
              _expected_error_codes(error_codes)
        {

            // Store as arguments for generic access
            for (size_t i = 0; i < error_codes.size(); ++i)
            {
                add_argument("error" + std::to_string(i), error_codes[i]);
            }
        }

        const std::vector<std::string> &expected_errors() const { return _expected_error_codes; }

        void add_expected_error(const std::string &error_code)
        {
            _expected_error_codes.push_back(error_code);
            add_argument("error" + std::to_string(_expected_error_codes.size() - 1), error_code);
        }

        void print(std::ostream &os, int indent = 0) const override
        {
            os << std::string(indent, ' ') << "ExpectErrorDirective[";
            for (size_t i = 0; i < _expected_error_codes.size(); ++i)
            {
                if (i > 0)
                    os << ", ";
                os << _expected_error_codes[i];
            }
            os << "]" << std::endl;
        }
    };

}