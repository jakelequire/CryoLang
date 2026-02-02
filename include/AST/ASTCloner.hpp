#pragma once
/******************************************************************************
 * @file ASTCloner.hpp
 * @brief Deep cloning utility for AST nodes
 *
 * ASTCloner provides deep cloning functionality for AST nodes, creating
 * completely independent copies. This is essential for AST specialization
 * during monomorphization, where we need separate AST copies for each
 * generic instantiation.
 ******************************************************************************/

#include "AST/ASTNode.hpp"
#include "AST/ASTVisitor.hpp"
#include "Types/TypeAnnotation.hpp"

#include <memory>
#include <unordered_map>

namespace Cryo
{
    /**************************************************************************
     * @brief Deep cloner for AST nodes
     *
     * Creates independent copies of AST subtrees. Used during monomorphization
     * to create specialized versions of generic method bodies.
     *
     * Usage:
     *   ASTCloner cloner;
     *   auto cloned_block = cloner.clone<BlockStatementNode>(original_block);
     **************************************************************************/
    class ASTCloner : public BaseASTVisitor
    {
    private:
        // The cloned result is stored here after visiting
        std::unique_ptr<ASTNode> _result;

    public:
        ASTCloner() = default;
        ~ASTCloner() = default;

        // Non-copyable
        ASTCloner(const ASTCloner &) = delete;
        ASTCloner &operator=(const ASTCloner &) = delete;

        /**
         * @brief Clone an AST node of a specific type
         * @tparam T The expected node type
         * @param node The node to clone (can be null)
         * @return A unique_ptr to the cloned node, or nullptr if input was null
         */
        template <typename T>
        std::unique_ptr<T> clone(T *node)
        {
            if (!node)
                return nullptr;

            _result.reset();
            node->accept(*this);

            // Transfer ownership and cast to expected type
            return std::unique_ptr<T>(static_cast<T *>(_result.release()));
        }

        /**
         * @brief Clone an expression node
         */
        std::unique_ptr<ExpressionNode> clone_expression(ExpressionNode *node);

        /**
         * @brief Clone a statement node
         */
        std::unique_ptr<StatementNode> clone_statement(StatementNode *node);

        /**
         * @brief Clone a type annotation (deep copy)
         */
        static std::unique_ptr<TypeAnnotation> clone_type_annotation(const TypeAnnotation *annotation);

        // ====================================================================
        // Visitor implementations for all node types
        // ====================================================================

        // Expressions
        void visit(IdentifierNode &node) override;
        void visit(LiteralNode &node) override;
        void visit(ExpressionNode &node) override;
        void visit(BinaryExpressionNode &node) override;
        void visit(UnaryExpressionNode &node) override;
        void visit(TernaryExpressionNode &node) override;
        void visit(IfExpressionNode &node) override;
        void visit(MatchExpressionNode &node) override;
        void visit(CallExpressionNode &node) override;
        void visit(NewExpressionNode &node) override;
        void visit(SizeofExpressionNode &node) override;
        void visit(AlignofExpressionNode &node) override;
        void visit(CastExpressionNode &node) override;
        void visit(StructLiteralNode &node) override;
        void visit(ArrayLiteralNode &node) override;
        void visit(TupleLiteralNode &node) override;
        void visit(LambdaExpressionNode &node) override;
        void visit(ArrayAccessNode &node) override;
        void visit(MemberAccessNode &node) override;
        void visit(ScopeResolutionNode &node) override;

        // Statements
        void visit(StatementNode &node) override;
        void visit(ProgramNode &node) override;
        void visit(BlockStatementNode &node) override;
        void visit(UnsafeBlockStatementNode &node) override;
        void visit(ReturnStatementNode &node) override;
        void visit(IfStatementNode &node) override;
        void visit(WhileStatementNode &node) override;
        void visit(ForStatementNode &node) override;
        void visit(MatchStatementNode &node) override;
        void visit(SwitchStatementNode &node) override;
        void visit(CaseStatementNode &node) override;
        void visit(MatchArmNode &node) override;
        void visit(PatternNode &node) override;
        void visit(EnumPatternNode &node) override;
        void visit(BreakStatementNode &node) override;
        void visit(ContinueStatementNode &node) override;
        void visit(ExpressionStatementNode &node) override;
        void visit(DeclarationStatementNode &node) override;

        // Declarations
        void visit(DeclarationNode &node) override;
        void visit(VariableDeclarationNode &node) override;
        void visit(FunctionDeclarationNode &node) override;
        void visit(IntrinsicDeclarationNode &node) override;
        void visit(IntrinsicConstDeclarationNode &node) override;
        void visit(ImportDeclarationNode &node) override;
        void visit(ModuleDeclarationNode &node) override;
        void visit(StructDeclarationNode &node) override;
        void visit(ClassDeclarationNode &node) override;
        void visit(TraitDeclarationNode &node) override;
        void visit(EnumDeclarationNode &node) override;
        void visit(EnumVariantNode &node) override;
        void visit(TypeAliasDeclarationNode &node) override;
        void visit(ImplementationBlockNode &node) override;
        void visit(ExternBlockNode &node) override;
        void visit(GenericParameterNode &node) override;
        void visit(StructFieldNode &node) override;
        void visit(StructMethodNode &node) override;
        void visit(DirectiveNode &node) override;

    private:
        /**
         * @brief Set the result node, copying base properties from source
         */
        template <typename T>
        void set_result(std::unique_ptr<T> node, const ASTNode &source)
        {
            if (node)
            {
                node->set_source_file(source.source_file());
                if (source.has_error())
                    node->mark_error();
            }
            _result = std::move(node);
        }
    };

} // namespace Cryo
