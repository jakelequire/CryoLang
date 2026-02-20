#pragma once

#include "AST/ASTVisitor.hpp"
#include "AST/ASTNode.hpp"
#include "Types/TypeAnnotation.hpp"
#include <string>
#include <optional>

namespace CryoLSP
{

    /**
     * @brief Result of finding a node at a position
     */
    struct FoundNode
    {
        Cryo::ASTNode *node = nullptr;
        std::string identifier_name;
        enum class Kind
        {
            Unknown,
            Literal,
            Identifier,
            FunctionDecl,
            VariableDecl,
            StructDecl,
            ClassDecl,
            EnumDecl,
            EnumVariant,
            FieldAccess,
            FieldInitializer,  // Field name in a struct literal (e.g., `x:` in `Point { x: 1 }`)
            PatternBinding,    // Variable binding in an enum pattern (e.g., `name` in `Expr::Variable(name)`)
            ScopeResolution,
            TypeReference,
            Parameter,
            ImportDecl,
        } kind = Kind::Unknown;
    };

    /**
     * @brief Walks the AST to find the deepest node at a cursor position
     *
     * Uses BaseASTVisitor to traverse the tree and find the most specific
     * node containing the target position.
     */
    class PositionFinder : public Cryo::BaseASTVisitor
    {
    public:
        PositionFinder(size_t target_line, size_t target_col);

        // Run the search on an AST root
        FoundNode find(Cryo::ASTNode *root);

        // Visitor overrides
        void visit(Cryo::ProgramNode &node) override;
        void visit(Cryo::IdentifierNode &node) override;
        void visit(Cryo::LiteralNode &node) override;
        void visit(Cryo::FunctionDeclarationNode &node) override;
        void visit(Cryo::VariableDeclarationNode &node) override;
        void visit(Cryo::StructDeclarationNode &node) override;
        void visit(Cryo::ClassDeclarationNode &node) override;
        void visit(Cryo::EnumDeclarationNode &node) override;
        void visit(Cryo::EnumVariantNode &node) override;
        void visit(Cryo::MemberAccessNode &node) override;
        void visit(Cryo::ScopeResolutionNode &node) override;
        void visit(Cryo::CallExpressionNode &node) override;
        void visit(Cryo::BlockStatementNode &node) override;
        void visit(Cryo::IfStatementNode &node) override;
        void visit(Cryo::WhileStatementNode &node) override;
        void visit(Cryo::ForStatementNode &node) override;
        void visit(Cryo::ReturnStatementNode &node) override;
        void visit(Cryo::ExpressionStatementNode &node) override;
        void visit(Cryo::DeclarationStatementNode &node) override;
        void visit(Cryo::BinaryExpressionNode &node) override;
        void visit(Cryo::UnaryExpressionNode &node) override;
        void visit(Cryo::ImplementationBlockNode &node) override;
        void visit(Cryo::StructMethodNode &node) override;
        void visit(Cryo::ImportDeclarationNode &node) override;
        void visit(Cryo::MatchStatementNode &node) override;
        void visit(Cryo::MatchExpressionNode &node) override;
        void visit(Cryo::MatchArmNode &node) override;
        void visit(Cryo::PatternNode &node) override;
        void visit(Cryo::EnumPatternNode &node) override;
        void visit(Cryo::TypeAliasDeclarationNode &node) override;
        void visit(Cryo::TraitDeclarationNode &node) override;
        void visit(Cryo::SizeofExpressionNode &node) override;
        void visit(Cryo::AlignofExpressionNode &node) override;
        void visit(Cryo::CastExpressionNode &node) override;
        void visit(Cryo::StructFieldNode &node) override;
        void visit(Cryo::StructLiteralNode &node) override;
        void visit(Cryo::SwitchStatementNode &node) override;
        void visit(Cryo::CaseStatementNode &node) override;
        void visit(Cryo::ArrayAccessNode &node) override;
        void visit(Cryo::ModuleDeclarationNode &node) override;

    private:
        size_t _target_line;
        size_t _target_col;
        FoundNode _result;

        // Check if a node's location matches the target
        bool matchesPosition(Cryo::ASTNode &node, size_t name_length = 0);

        // Check if a specific source location matches the target
        bool matchesPosition(const Cryo::SourceLocation &loc, size_t name_length);

        // Check if cursor is on a type annotation (sets result if matched)
        bool checkTypeAnnotation(const Cryo::TypeAnnotation *ann);

        // Visit children of container nodes
        void visitChildren(Cryo::ASTNode &node);
    };

} // namespace CryoLSP
