#pragma once

namespace Cryo
{
    // Forward declarations
    class IdentifierNode;
    class LiteralNode;
    class ExpressionNode;
    class StatementNode;
    class DeclarationNode;
    class BinaryExpressionNode;
    class TernaryExpressionNode;
    class ProgramNode;
    class BlockStatementNode;
    class ReturnStatementNode;
    class VariableDeclarationNode;
    class FunctionDeclarationNode;
    class CallExpressionNode;
    class ArrayLiteralNode;
    class ArrayAccessNode;
    class IfStatementNode;
    class WhileStatementNode;
    class ForStatementNode;
    class BreakStatementNode;
    class ContinueStatementNode;
    class ExpressionStatementNode;
    class DeclarationStatementNode;

    class ASTVisitor
    {
    public:
        virtual ~ASTVisitor() = default;

        // Visit methods for each node type
        virtual void visit(IdentifierNode &node) = 0;
        virtual void visit(LiteralNode &node) = 0;
        virtual void visit(ExpressionNode &node) = 0;
        virtual void visit(StatementNode &node) = 0;
        virtual void visit(DeclarationNode &node) = 0;
        virtual void visit(BinaryExpressionNode &node) = 0;
        virtual void visit(TernaryExpressionNode &node) = 0;
        virtual void visit(ProgramNode &node) = 0;
        virtual void visit(BlockStatementNode &node) = 0;
        virtual void visit(ReturnStatementNode &node) = 0;
        virtual void visit(VariableDeclarationNode &node) = 0;
        virtual void visit(FunctionDeclarationNode &node) = 0;
        virtual void visit(CallExpressionNode &node) = 0;
        virtual void visit(ArrayLiteralNode &node) = 0;
        virtual void visit(ArrayAccessNode &node) = 0;
        virtual void visit(IfStatementNode &node) = 0;
        virtual void visit(WhileStatementNode &node) = 0;
        virtual void visit(ForStatementNode &node) = 0;
        virtual void visit(BreakStatementNode &node) = 0;
        virtual void visit(ContinueStatementNode &node) = 0;
        virtual void visit(ExpressionStatementNode &node) = 0;
        virtual void visit(DeclarationStatementNode &node) = 0;
    };

    // Base visitor with default implementations
    class BaseASTVisitor : public ASTVisitor
    {
    public:
        void visit(IdentifierNode &node) override {}
        void visit(LiteralNode &node) override {}
        void visit(ExpressionNode &node) override {}
        void visit(StatementNode &node) override {}
        void visit(DeclarationNode &node) override {}
        void visit(BinaryExpressionNode &node) override {}
        void visit(TernaryExpressionNode &node) override {}
        void visit(ProgramNode &node) override {}
        void visit(BlockStatementNode &node) override {}
        void visit(ReturnStatementNode &node) override {}
        void visit(VariableDeclarationNode &node) override {}
        void visit(FunctionDeclarationNode &node) override {}
        void visit(CallExpressionNode &node) override {}
        void visit(ArrayLiteralNode &node) override {}
        void visit(ArrayAccessNode &node) override {}
        void visit(IfStatementNode &node) override {}
        void visit(WhileStatementNode &node) override {}
        void visit(ForStatementNode &node) override {}
        void visit(BreakStatementNode &node) override {}
        void visit(ContinueStatementNode &node) override {}
        void visit(ExpressionStatementNode &node) override {}
        void visit(DeclarationStatementNode &node) override {}
    };
}