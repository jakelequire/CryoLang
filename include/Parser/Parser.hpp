#pragma once

#include "Lexer/lexer.hpp"
#include "AST/ASTNode.hpp"
#include "AST/ASTBuilder.hpp"
#include "AST/ASTContext.hpp"
#include "AST/Type.hpp"
#include "GDM/GDM.hpp"
#include <memory>
#include <vector>
#include <stdexcept>

namespace Cryo
{
    class ParseError : public std::runtime_error
    {
    public:
        ParseError(const std::string &message, SourceLocation location)
            : std::runtime_error(message), _location(location) {}

        const SourceLocation &location() const { return _location; }

    private:
        SourceLocation _location;
    };

    class Parser
    {
    private:
        std::unique_ptr<Lexer> _lexer;
        ASTContext &_context;
        ASTBuilder _builder;
        Token _current_token;
        std::vector<ParseError> _errors;
        DiagnosticManager *_diagnostic_manager;
        std::string _source_file;

        // Context tracking
        bool _in_implementation_block = false;
        std::string _current_namespace = "Global"; // Current namespace context

    public:
        Parser(std::unique_ptr<Lexer> lexer, ASTContext &context);
        Parser(std::unique_ptr<Lexer> lexer, ASTContext &context, DiagnosticManager *diagnostic_manager, const std::string &source_file);

        // Main parsing entry point
        std::unique_ptr<ProgramNode> parse_program();

        // Error handling
        const std::vector<ParseError> &errors() const { return _errors; }
        bool has_errors() const { return !_errors.empty(); }

        // Namespace access
        const std::string &current_namespace() const { return _current_namespace; }

    private:
        // Diagnostic reporting
        void report_error(DiagnosticID id, const std::string &message, SourceRange range = SourceRange{});
        void report_warning(DiagnosticID id, const std::string &message, SourceRange range = SourceRange{});

        // Token management
        void advance();
        bool match(TokenKind kind);
        bool match(std::initializer_list<TokenKind> kinds);
        Token consume(TokenKind expected, const std::string &error_message);
        bool is_at_end() const;

        // Error handling
        void error(const std::string &message);
        void synchronize();

        // Type parsing
        std::string parse_type();
        Type *parse_type_annotation();              // New method that returns Type*
        std::vector<std::string> parse_type_list(); // For arrays like i32[][]

        // Namespace parsing
        std::string parse_namespace();

        // Statement parsing
        std::unique_ptr<ASTNode> parse_statement();
        std::unique_ptr<VariableDeclarationNode> parse_variable_declaration();
        std::unique_ptr<FunctionDeclarationNode> parse_function_declaration();
        std::unique_ptr<FunctionDeclarationNode> parse_extern_function_declaration();
        void parse_where_clause(FunctionDeclarationNode* func_decl);
        std::unique_ptr<IntrinsicDeclarationNode> parse_intrinsic_declaration();
        std::unique_ptr<ImportDeclarationNode> parse_import_declaration();
        std::unique_ptr<StructDeclarationNode> parse_struct_declaration();
        std::unique_ptr<ClassDeclarationNode> parse_class_declaration();
        std::unique_ptr<TraitDeclarationNode> parse_trait_declaration();
        std::unique_ptr<EnumDeclarationNode> parse_enum_declaration();
        std::unique_ptr<TypeAliasDeclarationNode> parse_type_alias_declaration();
        std::unique_ptr<ImplementationBlockNode> parse_implementation_block();
        std::unique_ptr<ExternBlockNode> parse_extern_block();
        std::unique_ptr<ReturnStatementNode> parse_return_statement();
        std::unique_ptr<BlockStatementNode> parse_block_statement();
        std::unique_ptr<ASTNode> parse_if_statement();
        std::unique_ptr<ASTNode> parse_while_statement();
        std::unique_ptr<ASTNode> parse_for_statement();
        std::unique_ptr<ASTNode> parse_match_statement();
        std::unique_ptr<ASTNode> parse_switch_statement();
        std::unique_ptr<CaseStatementNode> parse_case_statement();
        std::unique_ptr<MatchArmNode> parse_match_arm();
        std::unique_ptr<PatternNode> parse_pattern();
        std::unique_ptr<ASTNode> parse_break_statement();
        std::unique_ptr<ASTNode> parse_continue_statement();
        std::unique_ptr<ASTNode> parse_expression_statement();

        // Parameter parsing for functions
        std::pair<std::vector<std::unique_ptr<VariableDeclarationNode>>, bool> parse_parameter_list();
        std::unique_ptr<VariableDeclarationNode> parse_parameter();
        bool peek_variadic_parameter();
        std::unique_ptr<VariableDeclarationNode> parse_variadic_parameter();

        // Expression parsing (precedence climbing)
        std::unique_ptr<ExpressionNode> parse_expression();
        std::unique_ptr<ExpressionNode> parse_assignment();
        std::unique_ptr<ExpressionNode> parse_conditional();
        std::unique_ptr<ExpressionNode> parse_logical_or();
        std::unique_ptr<ExpressionNode> parse_logical_and();
        std::unique_ptr<ExpressionNode> parse_equality();
        std::unique_ptr<ExpressionNode> parse_relational();
        std::unique_ptr<ExpressionNode> parse_additive();
        std::unique_ptr<ExpressionNode> parse_multiplicative();
        std::unique_ptr<ExpressionNode> parse_unary();
        std::unique_ptr<ExpressionNode> parse_primary();
        std::unique_ptr<ExpressionNode> parse_call_expression(std::unique_ptr<ExpressionNode> expr);
        std::unique_ptr<ExpressionNode> parse_new_expression();
        std::unique_ptr<ExpressionNode> parse_sizeof_expression();
        std::unique_ptr<ExpressionNode> parse_array_access(std::unique_ptr<ExpressionNode> expr);
        std::unique_ptr<ExpressionNode> parse_member_access(std::unique_ptr<ExpressionNode> expr);

        // Literal parsing
        std::unique_ptr<LiteralNode> parse_number_literal();
        std::unique_ptr<LiteralNode> parse_string_literal();
        std::unique_ptr<LiteralNode> parse_boolean_literal();
        std::unique_ptr<LiteralNode> parse_character_literal();
        std::unique_ptr<LiteralNode> parse_null_literal();
        std::unique_ptr<ExpressionNode> parse_identifier();

        // Array literal parsing
        std::unique_ptr<ExpressionNode> parse_array_literal();

        // Struct/Class parsing helpers
        std::vector<std::unique_ptr<GenericParameterNode>> parse_generic_parameters();
        std::unique_ptr<GenericParameterNode> parse_generic_parameter();
        std::unique_ptr<StructFieldNode> parse_struct_field(Visibility default_visibility = Visibility::Public);
        std::unique_ptr<StructMethodNode> parse_struct_method(const std::string &struct_name = "", Visibility default_visibility = Visibility::Public);
        Visibility parse_visibility_modifier();

        // Enum parsing helpers
        std::unique_ptr<EnumVariantNode> parse_enum_variant();

        // Utility methods
        bool is_type_token() const;
        bool is_visibility_modifier() const;
        bool is_variable_modifier() const; // const, mut
        int get_operator_precedence(TokenKind kind) const;
        bool is_binary_operator(TokenKind kind) const;
        bool is_unary_operator(TokenKind kind) const;
        bool is_assignment_operator(TokenKind kind) const;

        // Look ahead utilities
        Token peek() const { return _current_token; }
        Token peek_next();
    };
}