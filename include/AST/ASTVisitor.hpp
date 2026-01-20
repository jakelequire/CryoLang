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
    class UnaryExpressionNode;
    class TernaryExpressionNode;
    class IfExpressionNode;
    class MatchExpressionNode;
    class ProgramNode;
    class BlockStatementNode;
    class UnsafeBlockStatementNode;
    class ReturnStatementNode;
    class VariableDeclarationNode;
    class FunctionDeclarationNode;
    class IntrinsicDeclarationNode;
    class IntrinsicConstDeclarationNode;
    class ImportDeclarationNode;
    class ModuleDeclarationNode;
    class CallExpressionNode;
    class NewExpressionNode;
    class SizeofExpressionNode;
    class AlignofExpressionNode;
    class CastExpressionNode;
    class StructLiteralNode;
    class ArrayLiteralNode;
    class TupleLiteralNode;
    class LambdaExpressionNode;
    class ArrayAccessNode;
    class MemberAccessNode;
    class ScopeResolutionNode;
    class IfStatementNode;
    class WhileStatementNode;
    class ForStatementNode;
    class MatchStatementNode;
    class SwitchStatementNode;
    class CaseStatementNode;
    class MatchArmNode;
    class PatternNode;
    class EnumPatternNode;
    class BreakStatementNode;
    class ContinueStatementNode;
    class ExpressionStatementNode;
    class DeclarationStatementNode;
    class StructDeclarationNode;
    class ClassDeclarationNode;
    class TraitDeclarationNode;
    class EnumDeclarationNode;
    class EnumVariantNode;
    class TypeAliasDeclarationNode;
    class ImplementationBlockNode;
    class ExternBlockNode;
    class GenericParameterNode;
    class StructFieldNode;
    class StructMethodNode;
    class DirectiveNode;

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
        virtual void visit(UnaryExpressionNode &node) = 0;
        virtual void visit(TernaryExpressionNode &node) = 0;
        virtual void visit(IfExpressionNode &node) = 0;
        virtual void visit(MatchExpressionNode &node) = 0;
        virtual void visit(ProgramNode &node) = 0;
        virtual void visit(BlockStatementNode &node) = 0;
        virtual void visit(UnsafeBlockStatementNode &node) = 0;
        virtual void visit(ReturnStatementNode &node) = 0;
        virtual void visit(VariableDeclarationNode &node) = 0;
        virtual void visit(FunctionDeclarationNode &node) = 0;
        virtual void visit(IntrinsicDeclarationNode &node) = 0;
        virtual void visit(IntrinsicConstDeclarationNode &node) = 0;
        virtual void visit(ImportDeclarationNode &node) = 0;
        virtual void visit(ModuleDeclarationNode &node) = 0;
        virtual void visit(CallExpressionNode &node) = 0;
        virtual void visit(NewExpressionNode &node) = 0;
        virtual void visit(SizeofExpressionNode &node) = 0;
        virtual void visit(AlignofExpressionNode &node) = 0;
        virtual void visit(CastExpressionNode &node) = 0;
        virtual void visit(StructLiteralNode &node) = 0;
        virtual void visit(ArrayLiteralNode &node) = 0;
        virtual void visit(TupleLiteralNode &node) = 0;
        virtual void visit(LambdaExpressionNode &node) = 0;
        virtual void visit(ArrayAccessNode &node) = 0;
        virtual void visit(MemberAccessNode &node) = 0;
        virtual void visit(ScopeResolutionNode &node) = 0;
        virtual void visit(IfStatementNode &node) = 0;
        virtual void visit(WhileStatementNode &node) = 0;
        virtual void visit(ForStatementNode &node) = 0;
        virtual void visit(MatchStatementNode &node) = 0;
        virtual void visit(SwitchStatementNode &node) = 0;
        virtual void visit(CaseStatementNode &node) = 0;
        virtual void visit(MatchArmNode &node) = 0;
        virtual void visit(PatternNode &node) = 0;
        virtual void visit(EnumPatternNode &node) = 0;
        virtual void visit(BreakStatementNode &node) = 0;
        virtual void visit(ContinueStatementNode &node) = 0;
        virtual void visit(ExpressionStatementNode &node) = 0;
        virtual void visit(DeclarationStatementNode &node) = 0;
        virtual void visit(StructDeclarationNode &node) = 0;
        virtual void visit(ClassDeclarationNode &node) = 0;
        virtual void visit(TraitDeclarationNode &node) = 0;
        virtual void visit(EnumDeclarationNode &node) = 0;
        virtual void visit(EnumVariantNode &node) = 0;
        virtual void visit(TypeAliasDeclarationNode &node) = 0;
        virtual void visit(ImplementationBlockNode &node) = 0;
        virtual void visit(ExternBlockNode &node) = 0;
        virtual void visit(GenericParameterNode &node) = 0;
        virtual void visit(StructFieldNode &node) = 0;
        virtual void visit(StructMethodNode &node) = 0;
        virtual void visit(DirectiveNode &node) = 0;
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
        void visit(UnaryExpressionNode &node) override {}
        void visit(TernaryExpressionNode &node) override {}
        void visit(IfExpressionNode &node) override {}
        void visit(MatchExpressionNode &node) override {}
        void visit(ProgramNode &node) override {}
        void visit(BlockStatementNode &node) override {}
        void visit(UnsafeBlockStatementNode &node) override {}
        void visit(ReturnStatementNode &node) override {}
        void visit(VariableDeclarationNode &node) override {}
        void visit(FunctionDeclarationNode &node) override {}
        void visit(IntrinsicDeclarationNode &node) override {}
        void visit(IntrinsicConstDeclarationNode &node) override {}
        void visit(ImportDeclarationNode &node) override {}
        void visit(ModuleDeclarationNode &node) override {}
        void visit(CallExpressionNode &node) override {}
        void visit(NewExpressionNode &node) override {}
        void visit(SizeofExpressionNode &node) override {}
        void visit(AlignofExpressionNode &node) override {}
        void visit(CastExpressionNode &node) override {}
        void visit(StructLiteralNode &node) override {}
        void visit(ArrayLiteralNode &node) override {}
        void visit(TupleLiteralNode &node) override {}
        void visit(LambdaExpressionNode &node) override {}
        void visit(ArrayAccessNode &node) override {}
        void visit(MemberAccessNode &node) override {}
        void visit(ScopeResolutionNode &node) override {}
        void visit(IfStatementNode &node) override {}
        void visit(WhileStatementNode &node) override {}
        void visit(ForStatementNode &node) override {}
        void visit(MatchStatementNode &node) override {}
        void visit(SwitchStatementNode &node) override {}
        void visit(CaseStatementNode &node) override {}
        void visit(MatchArmNode &node) override {}
        void visit(PatternNode &node) override {}
        void visit(EnumPatternNode &node) override {}
        void visit(BreakStatementNode &node) override {}
        void visit(ContinueStatementNode &node) override {}
        void visit(ExpressionStatementNode &node) override {}
        void visit(DeclarationStatementNode &node) override {}
        void visit(StructDeclarationNode &node) override {}
        void visit(ClassDeclarationNode &node) override {}
        void visit(TraitDeclarationNode &node) override {}
        void visit(EnumDeclarationNode &node) override {}
        void visit(EnumVariantNode &node) override {}
        void visit(TypeAliasDeclarationNode &node) override {}
        void visit(ImplementationBlockNode &node) override {}
        void visit(ExternBlockNode &node) override {}
        void visit(GenericParameterNode &node) override {}
        void visit(StructFieldNode &node) override {}
        void visit(StructMethodNode &node) override {}
        void visit(DirectiveNode &node) override {}
    };
}