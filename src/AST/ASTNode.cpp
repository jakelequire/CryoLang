#include "AST/ASTNode.hpp"
#include "AST/ASTVisitor.hpp"

namespace Cryo
{
    //===----------------------------------------------------------------------===//
    // DirectiveStorage - PIMPL implementation for directive support
    //===----------------------------------------------------------------------===//

    struct ASTNode::DirectiveStorage
    {
        std::vector<std::unique_ptr<DirectiveNode>> attached_directives;
    };

    //===----------------------------------------------------------------------===//
    // ASTNode Implementation
    //===----------------------------------------------------------------------===//

    ASTNode::ASTNode(NodeKind kind, SourceLocation location)
        : _kind(kind), _location(location), _directive_storage(std::make_unique<DirectiveStorage>())
    {
    }

    ASTNode::~ASTNode() = default;

    ASTNode::ASTNode(const ASTNode &other)
        : _kind(other._kind), _location(other._location), _has_error(other._has_error),
          _directive_storage(std::make_unique<DirectiveStorage>())
    {
        // Note: We don't copy directives as they are typically tied to specific parse locations
    }

    ASTNode &ASTNode::operator=(const ASTNode &other)
    {
        if (this != &other)
        {
            _kind = other._kind;
            _location = other._location;
            _has_error = other._has_error;
            _directive_storage = std::make_unique<DirectiveStorage>();
            // Note: We don't copy directives as they are typically tied to specific parse locations
        }
        return *this;
    }

    ASTNode::ASTNode(ASTNode &&other) noexcept
        : _kind(other._kind), _location(other._location), _has_error(other._has_error),
          _directive_storage(std::move(other._directive_storage))
    {
    }

    ASTNode &ASTNode::operator=(ASTNode &&other) noexcept
    {
        if (this != &other)
        {
            _kind = other._kind;
            _location = other._location;
            _has_error = other._has_error;
            _directive_storage = std::move(other._directive_storage);
        }
        return *this;
    }

    void ASTNode::attach_directive(std::unique_ptr<DirectiveNode> directive)
    {
        if (directive)
        {
            _directive_storage->attached_directives.push_back(std::move(directive));
        }
    }

    const std::vector<std::unique_ptr<DirectiveNode>> &ASTNode::get_directives() const
    {
        return _directive_storage->attached_directives;
    }

    bool ASTNode::has_directives() const
    {
        return !_directive_storage->attached_directives.empty();
    }

    //===----------------------------------------------------------------------===//
    // AST Node accept() implementations
    //===----------------------------------------------------------------------===//
    void CallExpressionNode::accept(ASTVisitor &visitor)
    {
        visitor.visit(*this);
    }

    void NewExpressionNode::accept(ASTVisitor &visitor)
    {
        visitor.visit(*this);
    }

    void SizeofExpressionNode::accept(ASTVisitor &visitor)
    {
        visitor.visit(*this);
    }

    void StructLiteralNode::accept(ASTVisitor &visitor)
    {
        visitor.visit(*this);
    }

    void ArrayLiteralNode::accept(ASTVisitor &visitor)
    {
        visitor.visit(*this);
    }

    void ArrayAccessNode::accept(ASTVisitor &visitor)
    {
        visitor.visit(*this);
    }

    void MemberAccessNode::accept(ASTVisitor &visitor)
    {
        visitor.visit(*this);
    }

    void ScopeResolutionNode::accept(ASTVisitor &visitor)
    {
        visitor.visit(*this);
    }

    // Control flow statement visitor implementations
    void IfStatementNode::accept(ASTVisitor &visitor)
    {
        visitor.visit(*this);
    }

    void WhileStatementNode::accept(ASTVisitor &visitor)
    {
        visitor.visit(*this);
    }

    void ForStatementNode::accept(ASTVisitor &visitor)
    {
        visitor.visit(*this);
    }

    void MatchStatementNode::accept(ASTVisitor &visitor)
    {
        visitor.visit(*this);
    }

    void MatchArmNode::accept(ASTVisitor &visitor)
    {
        visitor.visit(*this);
    }

    void PatternNode::accept(ASTVisitor &visitor)
    {
        visitor.visit(*this);
    }

    void EnumPatternNode::accept(ASTVisitor &visitor)
    {
        visitor.visit(*this);
    }

    void BreakStatementNode::accept(ASTVisitor &visitor)
    {
        visitor.visit(*this);
    }

    void ContinueStatementNode::accept(ASTVisitor &visitor)
    {
        visitor.visit(*this);
    }

    void ExpressionStatementNode::accept(ASTVisitor &visitor)
    {
        visitor.visit(*this);
    } // ExpressionNode visitor implementation
    void ExpressionNode::accept(ASTVisitor &visitor)
    {
        visitor.visit(*this);
    }

    // StatementNode visitor implementation
    void StatementNode::accept(ASTVisitor &visitor)
    {
        visitor.visit(*this);
    }

    // DeclarationNode visitor implementation
    void DeclarationNode::accept(ASTVisitor &visitor)
    {
        visitor.visit(*this);
    }

    // LiteralNode visitor implementation
    void LiteralNode::accept(ASTVisitor &visitor)
    {
        visitor.visit(*this);
    }

    // IdentifierNode visitor implementation
    void IdentifierNode::accept(ASTVisitor &visitor)
    {
        visitor.visit(*this);
    }

    // BinaryExpressionNode visitor implementation
    void BinaryExpressionNode::accept(ASTVisitor &visitor)
    {
        visitor.visit(*this);
    }

    // UnaryExpressionNode visitor implementation
    void UnaryExpressionNode::accept(ASTVisitor &visitor)
    {
        visitor.visit(*this);
    }

    // TernaryExpressionNode visitor implementation
    void TernaryExpressionNode::accept(ASTVisitor &visitor)
    {
        visitor.visit(*this);
    }

    // ProgramNode visitor implementation
    void ProgramNode::accept(ASTVisitor &visitor)
    {
        visitor.visit(*this);
    }

    // BlockStatementNode visitor implementation
    void BlockStatementNode::accept(ASTVisitor &visitor)
    {
        visitor.visit(*this);
    }

    // ReturnStatementNode visitor implementation
    void ReturnStatementNode::accept(ASTVisitor &visitor)
    {
        visitor.visit(*this);
    }

    // VariableDeclarationNode visitor implementation
    void VariableDeclarationNode::accept(ASTVisitor &visitor)
    {
        visitor.visit(*this);
    }

    // FunctionDeclarationNode visitor implementation
    void FunctionDeclarationNode::accept(ASTVisitor &visitor)
    {
        visitor.visit(*this);
    }

    // IntrinsicDeclarationNode visitor implementation
    void IntrinsicDeclarationNode::accept(ASTVisitor &visitor)
    {
        visitor.visit(*this);
    }

    // IntrinsicConstDeclarationNode visitor implementation
    void IntrinsicConstDeclarationNode::accept(ASTVisitor &visitor)
    {
        visitor.visit(*this);
    }

    void ImportDeclarationNode::accept(ASTVisitor &visitor)
    {
        visitor.visit(*this);
    }

    void DeclarationStatementNode::accept(ASTVisitor &visitor)
    {
        visitor.visit(*this);
    }

    // GenericParameterNode visitor implementation
    void GenericParameterNode::accept(ASTVisitor &visitor)
    {
        visitor.visit(*this);
    }

    // StructFieldNode visitor implementation
    void StructFieldNode::accept(ASTVisitor &visitor)
    {
        visitor.visit(*this);
    }

    // StructMethodNode visitor implementation
    void StructMethodNode::accept(ASTVisitor &visitor)
    {
        visitor.visit(*this);
    }

    // StructDeclarationNode visitor implementation
    void StructDeclarationNode::accept(ASTVisitor &visitor)
    {
        visitor.visit(*this);
    }

    // ClassDeclarationNode visitor implementation
    void ClassDeclarationNode::accept(ASTVisitor &visitor)
    {
        visitor.visit(*this);
    }

    // TraitDeclarationNode visitor implementation
    void TraitDeclarationNode::accept(ASTVisitor &visitor)
    {
        visitor.visit(*this);
    }

    // TypeAliasDeclarationNode visitor implementation
    void TypeAliasDeclarationNode::accept(ASTVisitor &visitor)
    {
        visitor.visit(*this);
    }

    // EnumDeclarationNode visitor implementation
    void EnumDeclarationNode::accept(ASTVisitor &visitor)
    {
        visitor.visit(*this);
    }

    // EnumVariantNode visitor implementation
    void EnumVariantNode::accept(ASTVisitor &visitor)
    {
        visitor.visit(*this);
    }

    // ImplementationBlockNode visitor implementation
    void ImplementationBlockNode::accept(ASTVisitor &visitor)
    {
        visitor.visit(*this);
    }

    // ExternBlockNode visitor implementation
    void ExternBlockNode::accept(ASTVisitor &visitor)
    {
        visitor.visit(*this);
    }

    // SwitchStatementNode visitor implementation
    void SwitchStatementNode::accept(ASTVisitor &visitor)
    {
        visitor.visit(*this);
    }

    // CaseStatementNode visitor implementation
    void CaseStatementNode::accept(ASTVisitor &visitor)
    {
        visitor.visit(*this);
    }

    // DirectiveNode visitor implementation
    void DirectiveNode::accept(ASTVisitor &visitor)
    {
        visitor.visit(*this);
    }
}