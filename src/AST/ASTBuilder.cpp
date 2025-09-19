#include "AST/ASTBuilder.hpp"
#include "AST/ASTNode.hpp"

namespace Cryo
{
    std::unique_ptr<BinaryExpressionNode> ASTBuilder::create_binary_expression(Token op, std::unique_ptr<ExpressionNode> lhs, std::unique_ptr<ExpressionNode> rhs)
    {
        auto node = std::make_unique<BinaryExpressionNode>(
            NodeKind::BinaryExpression,
            op.location(),
            op,
            std::move(lhs),
            std::move(rhs));
        return node;
    }

    std::unique_ptr<StatementNode> ASTBuilder::create_statement_node(NodeKind kind)
    {
        SourceLocation default_loc; // You'll need to handle this properly
        return std::make_unique<StatementNode>(kind, default_loc);
    }

    std::unique_ptr<DeclarationNode> ASTBuilder::create_declaration_node(Token identifier, std::unique_ptr<ExpressionNode> init)
    {
        return std::make_unique<DeclarationNode>(NodeKind::Declaration, identifier.location());
    }

    std::unique_ptr<LiteralNode> ASTBuilder::create_literal_node(Token literal)
    {
        validate_literal_token(literal);
        auto node = std::make_unique<LiteralNode>(
            NodeKind::Literal,
            literal.location(),
            std::string(literal.text()), // Convert string_view to string
            literal.kind());
        return node;
    }

    std::unique_ptr<IdentifierNode> ASTBuilder::create_identifier_node(Token identifier)
    {
        validate_identifier_token(identifier);
        auto node = std::make_unique<IdentifierNode>(
            NodeKind::Identifier,
            identifier.location(),
            std::string(identifier.text()) // Convert string_view to string
        );
        return node;
    }

    std::unique_ptr<ProgramNode> ASTBuilder::create_program_node(SourceLocation loc)
    {
        return std::make_unique<ProgramNode>(loc);
    }

    std::unique_ptr<BlockStatementNode> ASTBuilder::create_block_statement(SourceLocation loc)
    {
        return std::make_unique<BlockStatementNode>(loc);
    }

    std::unique_ptr<ReturnStatementNode> ASTBuilder::create_return_statement(SourceLocation loc, std::unique_ptr<ExpressionNode> expr)
    {
        return std::make_unique<ReturnStatementNode>(loc, std::move(expr));
    }

    std::unique_ptr<VariableDeclarationNode> ASTBuilder::create_variable_declaration(SourceLocation loc,
                                                                                     std::string name,
                                                                                     std::string type_annotation,
                                                                                     std::unique_ptr<ExpressionNode> init,
                                                                                     bool is_mutable)
    {
        return std::make_unique<VariableDeclarationNode>(loc, std::move(name), std::move(type_annotation), std::move(init), is_mutable);
    }

    std::unique_ptr<FunctionDeclarationNode> ASTBuilder::create_function_declaration(SourceLocation loc,
                                                                                     std::string name,
                                                                                     std::string return_type,
                                                                                     bool is_public)
    {
        return std::make_unique<FunctionDeclarationNode>(loc, std::move(name), std::move(return_type), is_public);
    }

    std::unique_ptr<CallExpressionNode> ASTBuilder::create_call_expression(SourceLocation loc, std::unique_ptr<ExpressionNode> callee)
    {
        return std::make_unique<CallExpressionNode>(loc, std::move(callee));
    }

    std::unique_ptr<IfStatementNode> ASTBuilder::create_if_statement(SourceLocation loc, std::unique_ptr<ExpressionNode> condition, std::unique_ptr<StatementNode> then_stmt, std::unique_ptr<StatementNode> else_stmt)
    {
        return std::make_unique<IfStatementNode>(loc, std::move(condition), std::move(then_stmt), std::move(else_stmt));
    }

    std::unique_ptr<WhileStatementNode> ASTBuilder::create_while_statement(SourceLocation loc, std::unique_ptr<ExpressionNode> condition, std::unique_ptr<StatementNode> body)
    {
        return std::make_unique<WhileStatementNode>(loc, std::move(condition), std::move(body));
    }

    std::unique_ptr<ForStatementNode> ASTBuilder::create_for_statement(SourceLocation loc, std::unique_ptr<VariableDeclarationNode> init, std::unique_ptr<ExpressionNode> condition, std::unique_ptr<ExpressionNode> update, std::unique_ptr<StatementNode> body)
    {
        return std::make_unique<ForStatementNode>(loc, std::move(init), std::move(condition), std::move(update), std::move(body));
    }

    std::unique_ptr<BreakStatementNode> ASTBuilder::create_break_statement(SourceLocation loc)
    {
        return std::make_unique<BreakStatementNode>(loc);
    }

    std::unique_ptr<ContinueStatementNode> ASTBuilder::create_continue_statement(SourceLocation loc)
    {
        return std::make_unique<ContinueStatementNode>(loc);
    }

    std::unique_ptr<ExpressionStatementNode> ASTBuilder::create_expression_statement(SourceLocation loc, std::unique_ptr<ExpressionNode> expr)
    {
        return std::make_unique<ExpressionStatementNode>(loc, std::move(expr));
    }

    std::unique_ptr<DeclarationStatementNode> ASTBuilder::create_declaration_statement(SourceLocation loc, std::unique_ptr<DeclarationNode> decl)
    {
        return std::make_unique<DeclarationStatementNode>(loc, std::move(decl));
    }

    // Helper methods
    bool ASTBuilder::is_literal_token(TokenKind kind) const
    {
        return kind == TokenKind::TK_STRING_LITERAL ||
               kind == TokenKind::TK_NUMERIC_CONSTANT ||
               kind == TokenKind::TK_CHAR_CONSTANT ||
               kind == TokenKind::TK_BOOLEAN_LITERAL ||
               kind == TokenKind::TK_KW_TRUE ||
               kind == TokenKind::TK_KW_FALSE;
    }

    void ASTBuilder::validate_identifier_token(const Token &token) const
    {
        if (token.kind() != TokenKind::TK_IDENTIFIER)
        {
            throw std::runtime_error("Expected identifier token");
        }
    }

    void ASTBuilder::validate_literal_token(const Token &token) const
    {
        if (!is_literal_token(token.kind()))
        {
            throw std::runtime_error("Expected literal token");
        }
    }
}