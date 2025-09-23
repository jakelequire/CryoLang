#pragma once
#include "AST/ASTContext.hpp"
#include "AST/ASTNode.hpp"

#include <memory>

namespace Cryo
{
    class ASTBuilder
    {
    private:
        ASTContext &_context;

    public:
        ASTBuilder(ASTContext &ctx) : _context(ctx) {}

        // Factory methods for creating various AST nodes
        std::unique_ptr<BinaryExpressionNode> create_binary_expression(Token op, std::unique_ptr<ExpressionNode> lhs, std::unique_ptr<ExpressionNode> rhs);
        std::unique_ptr<UnaryExpressionNode> create_unary_expression(Token op, std::unique_ptr<ExpressionNode> operand);
        std::unique_ptr<TernaryExpressionNode> create_ternary_expression(SourceLocation loc, std::unique_ptr<ExpressionNode> condition, std::unique_ptr<ExpressionNode> true_expr, std::unique_ptr<ExpressionNode> false_expr);
        std::unique_ptr<StatementNode> create_statement_node(NodeKind kind); // Simplified signature
        std::unique_ptr<DeclarationNode> create_declaration_node(Token identifier, std::unique_ptr<ExpressionNode> init = nullptr);
        std::unique_ptr<LiteralNode> create_literal_node(Token literal);
        std::unique_ptr<IdentifierNode> create_identifier_node(Token identifier);

        // Additional node creation methods
        std::unique_ptr<ProgramNode> create_program_node(SourceLocation loc);
        std::unique_ptr<BlockStatementNode> create_block_statement(SourceLocation loc);
        std::unique_ptr<ReturnStatementNode> create_return_statement(SourceLocation loc, std::unique_ptr<ExpressionNode> expr = nullptr);
        std::unique_ptr<VariableDeclarationNode> create_variable_declaration(SourceLocation loc,
                                                                             std::string name,
                                                                             std::string type_annotation,
                                                                             std::unique_ptr<ExpressionNode> init = nullptr,
                                                                             bool is_mutable = false);
        std::unique_ptr<FunctionDeclarationNode> create_function_declaration(SourceLocation loc,
                                                                             std::string name,
                                                                             std::string return_type,
                                                                             bool is_public = false);
        std::unique_ptr<CallExpressionNode> create_call_expression(SourceLocation loc, std::unique_ptr<ExpressionNode> callee);
        std::unique_ptr<NewExpressionNode> create_new_expression(SourceLocation loc, std::string type_name);
        std::unique_ptr<ArrayLiteralNode> create_array_literal(SourceLocation loc);
        std::unique_ptr<ArrayAccessNode> create_array_access(SourceLocation loc, std::unique_ptr<ExpressionNode> array, std::unique_ptr<ExpressionNode> index);
        std::unique_ptr<MemberAccessNode> create_member_access(SourceLocation loc, std::unique_ptr<ExpressionNode> object, std::string member);
        std::unique_ptr<ScopeResolutionNode> create_scope_resolution(SourceLocation loc, std::string scope_name, std::string member_name);
        std::unique_ptr<IfStatementNode> create_if_statement(SourceLocation loc, std::unique_ptr<ExpressionNode> condition, std::unique_ptr<StatementNode> then_stmt, std::unique_ptr<StatementNode> else_stmt = nullptr);
        std::unique_ptr<WhileStatementNode> create_while_statement(SourceLocation loc, std::unique_ptr<ExpressionNode> condition, std::unique_ptr<StatementNode> body);
        std::unique_ptr<ForStatementNode> create_for_statement(SourceLocation loc, std::unique_ptr<VariableDeclarationNode> init, std::unique_ptr<ExpressionNode> condition, std::unique_ptr<ExpressionNode> update, std::unique_ptr<StatementNode> body);
        std::unique_ptr<BreakStatementNode> create_break_statement(SourceLocation loc);
        std::unique_ptr<ContinueStatementNode> create_continue_statement(SourceLocation loc);
        std::unique_ptr<ExpressionStatementNode> create_expression_statement(SourceLocation loc, std::unique_ptr<ExpressionNode> expr);
        std::unique_ptr<DeclarationStatementNode> create_declaration_statement(SourceLocation loc, std::unique_ptr<DeclarationNode> decl);

        // Struct and Class creation methods
        std::unique_ptr<GenericParameterNode> create_generic_parameter(SourceLocation loc, std::string name);
        std::unique_ptr<StructFieldNode> create_struct_field(SourceLocation loc, std::string name, std::string type_annotation, Visibility visibility = Visibility::Public);
        std::unique_ptr<StructMethodNode> create_struct_method(SourceLocation loc, std::string name, std::string return_type, Visibility visibility = Visibility::Public, bool is_constructor = false);
        std::unique_ptr<StructDeclarationNode> create_struct_declaration(SourceLocation loc, std::string name);
        std::unique_ptr<ClassDeclarationNode> create_class_declaration(SourceLocation loc, std::string name);
        std::unique_ptr<EnumDeclarationNode> create_enum_declaration(SourceLocation loc, std::string name);
        std::unique_ptr<EnumVariantNode> create_enum_variant(SourceLocation loc, std::string name);
        std::unique_ptr<EnumVariantNode> create_enum_variant(SourceLocation loc, std::string name, std::vector<std::string> associated_types);
        std::unique_ptr<TypeAliasDeclarationNode> create_type_alias_declaration(SourceLocation loc, std::string alias_name, std::string target_type);
        std::unique_ptr<ImplementationBlockNode> create_implementation_block(SourceLocation loc, std::string target_type);
        std::unique_ptr<ExternBlockNode> create_extern_block(SourceLocation loc, std::string linkage_type);

        // Helper methods
    private:
        bool is_literal_token(TokenKind kind) const;
        void validate_identifier_token(const Token &token) const;
        void validate_literal_token(const Token &token) const;
    };
}