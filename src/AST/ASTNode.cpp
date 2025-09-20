#include "AST/ASTNode.hpp"
#include "AST/ASTVisitor.hpp"

namespace Cryo
{
    void CallExpressionNode::accept(ASTVisitor &visitor)
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

    void DeclarationStatementNode::accept(ASTVisitor &visitor)
    {
        visitor.visit(*this);
    }
}