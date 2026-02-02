#pragma once
/******************************************************************************
 * @file ASTTypeSubstituter.hpp
 * @brief Type substitution for AST nodes during monomorphization
 *
 * ASTTypeSubstituter walks an AST and substitutes generic type parameters
 * with concrete types. This is used after cloning to update all resolved
 * types in method bodies for specialized generic types.
 ******************************************************************************/

#include "AST/ASTNode.hpp"
#include "AST/ASTVisitor.hpp"
#include "Types/GenericRegistry.hpp"
#include "Types/TypeArena.hpp"

namespace Cryo
{
    /**************************************************************************
     * @brief Substitutes types in AST nodes during monomorphization
     *
     * Walks an AST tree and replaces all type references according to a
     * TypeSubstitution map. This ensures that method bodies in specialized
     * generic types have concrete types instead of type parameters.
     *
     * Usage:
     *   ASTTypeSubstituter substituter(substitution, arena);
     *   substituter.substitute(cloned_method_body);
     **************************************************************************/
    class ASTTypeSubstituter : public BaseASTVisitor
    {
    private:
        const TypeSubstitution &_substitution;
        TypeArena &_arena;
        // Map from type parameter name to concrete type (for annotation resolution)
        std::unordered_map<std::string, TypeRef> _param_name_map;

    public:
        ASTTypeSubstituter(const TypeSubstitution &substitution, TypeArena &arena)
            : _substitution(substitution), _arena(arena) {}

        /**
         * @brief Constructor with template info for name-based lookup
         */
        ASTTypeSubstituter(const TypeSubstitution &substitution, TypeArena &arena,
                           const GenericTemplate &tmpl, const std::vector<TypeRef> &type_args)
            : _substitution(substitution), _arena(arena)
        {
            // Build name-to-type map for annotation resolution
            for (size_t i = 0; i < tmpl.params.size() && i < type_args.size(); ++i)
            {
                _param_name_map[tmpl.params[i].name] = type_args[i];
            }
        }

        /**
         * @brief Constructor that derives name map from template and substitution
         *
         * This constructor extracts the concrete types from the substitution
         * using the type parameter TypeRefs from the template.
         */
        ASTTypeSubstituter(const TypeSubstitution &substitution, TypeArena &arena,
                           const GenericTemplate &tmpl)
            : _substitution(substitution), _arena(arena)
        {
            // Build name-to-type map by looking up each param's concrete type
            for (const auto &param : tmpl.params)
            {
                auto concrete = substitution.get(param.type);
                if (concrete)
                {
                    _param_name_map[param.name] = *concrete;
                }
            }
            // Debug: Log what was populated
            // (Will be visible when debug logging is enabled)
        }

        ~ASTTypeSubstituter() = default;

        // Non-copyable
        ASTTypeSubstituter(const ASTTypeSubstituter &) = delete;
        ASTTypeSubstituter &operator=(const ASTTypeSubstituter &) = delete;

        /**
         * @brief Substitute types in an AST node and all its children
         * @param node The root node to process
         */
        void substitute(ASTNode *node);

        /**
         * @brief Substitute a single type reference
         * @param type The type to substitute
         * @return The substituted type (may be same as input if no substitution needed)
         */
        TypeRef substitute_type(TypeRef type);

        // ====================================================================
        // Visitor implementations
        // ====================================================================

        // Expressions - all have resolved_type
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
         * @brief Substitute type on an expression node if it has one
         */
        void substitute_expression_type(ExpressionNode &node);

        /**
         * @brief Resolve a type from a TypeAnnotation, substituting type parameters
         *
         * This handles the case where resolved_type isn't set (common in generic
         * method bodies where TypeResolver runs before monomorphization).
         *
         * @param annotation The type annotation to resolve
         * @return Resolved type with type parameters substituted
         */
        TypeRef resolve_from_annotation(const TypeAnnotation *annotation);

        /**
         * @brief Check if a type name is a type parameter in our substitution
         */
        TypeRef lookup_type_param(const std::string &name);

        /**
         * @brief Parse a type string with modifiers (e.g., "T*", "T[]", "&T")
         *
         * Handles type strings stored as Named annotations that actually contain
         * modifier syntax. Returns the resolved type with all modifiers applied.
         *
         * @param type_str The type string to parse
         * @return Resolved type with modifiers applied, or invalid TypeRef if failed
         */
        TypeRef resolve_type_string_with_modifiers(const std::string &type_str);
    };

} // namespace Cryo
