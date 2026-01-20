#pragma once

#include "Lexer/lexer.hpp"
#include "AST/ASTNode.hpp"
#include "AST/ASTBuilder.hpp"
#include "AST/ASTContext.hpp"
#include "Types/Types.hpp"
#include "AST/DirectiveSystem.hpp"
#include "Diagnostics/Diag.hpp"
#include "Utils/SymbolResolutionManager.hpp"
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
        DiagEmitter *_diagnostics;
        std::string _source_file;

        // Context tracking
        bool _in_implementation_block = false;
        bool _parsing_method_body = false;         // Track if we're inside a method body (for synchronize context)
        bool _parsing_class_members = false;       // Track if we're parsing class/struct members (for synchronize context)
        std::string _current_namespace = "Global"; // Current namespace context
        std::string _current_parsing_type_name;    // Track current struct/class being parsed (for self-referential types)
        ModuleID _current_module_id;               // Current module ID for proper type qualification
        int _scope_depth = 0;                      // Track nesting depth (0 = global scope)
        std::vector<std::string> _current_generic_params; // Track generic params of current type being parsed (e.g., T, U in struct Foo<T, U>)

        // Bracket depth tracking for improved error recovery
        int _brace_depth = 0;        // Track { } nesting
        int _paren_depth = 0;        // Track ( ) nesting
        int _bracket_depth = 0;      // Track [ ] nesting
        size_t _tokens_consumed = 0; // Track total tokens processed for diagnostics

        // Token lookahead buffer for multi-token lookahead
        std::vector<Token> _lookahead_buffer; // Buffered tokens for peek_next_n

        // Documentation comment collection
        std::vector<std::string> _pending_doc_comments; // Collected doc comments waiting to be attached

        // Directive system
        DirectiveRegistry *_directive_registry = nullptr; // Will be set during initialization

        // Symbol Resolution Manager (SRM) for standardized naming
        std::unique_ptr<Cryo::SRM::SymbolResolutionContext> _srm_context;
        std::unique_ptr<Cryo::SRM::SymbolResolutionManager> _srm_manager;

    public:
        Parser(std::unique_ptr<Lexer> lexer, ASTContext &context);
        Parser(std::unique_ptr<Lexer> lexer, ASTContext &context, DiagEmitter *diagnostics, const std::string &source_file);

        // Main parsing entry point
        std::unique_ptr<ProgramNode> parse_program();

        // Error handling
        const std::vector<ParseError> &errors() const { return _errors; }
        bool has_errors() const { return !_errors.empty(); }

        // Namespace and module access
        const std::string &current_namespace() const { return _current_namespace; }
        ModuleID current_module() const { return _current_module_id; }

        // Scope tracking
        bool is_global_scope() const { return _scope_depth == 0; }
        void enter_scope() { _scope_depth++; }
        void exit_scope()
        {
            if (_scope_depth > 0)
                _scope_depth--;
        }

        // Bracket depth tracking (for error recovery)
        int brace_depth() const { return _brace_depth; }
        int paren_depth() const { return _paren_depth; }
        int bracket_depth() const { return _bracket_depth; }
        size_t tokens_consumed() const { return _tokens_consumed; }

        // Directive system
        void set_directive_registry(DirectiveRegistry *registry) { _directive_registry = registry; }

        // SRM Helper Methods for standardized naming during parsing
        std::string generate_qualified_namespace_name(const std::string &namespace_name);
        std::string generate_qualified_type_name(const std::string &base_name, const std::string &member_name);
        std::string generate_scope_resolution_name(const std::string &scope_name, const std::string &member_name);
        std::vector<std::string> get_current_namespace_parts() const;

    private:
        // Diagnostic reporting
        void report_error(ErrorCode error_code, const std::string &message, SourceRange range = SourceRange{});
        void report_warning(ErrorCode error_code, const std::string &message, SourceRange range = SourceRange{});

        // Enhanced diagnostic reporting
        void report_enhanced_token_error(TokenKind expected, const std::string &context_message);
        ErrorCode get_token_error_code(TokenKind expected);
        std::string get_token_name(TokenKind kind);
        void add_token_mismatch_suggestions(Diag &diagnostic, TokenKind expected, TokenKind actual, const std::string &context);
        void add_context_spans(Diag &diagnostic, TokenKind expected);
        void add_generic_parsing_suggestions(Diag &diagnostic, const std::string &message);

        // Token management
        void advance();
        bool match(TokenKind kind);
        bool match(std::initializer_list<TokenKind> kinds);
        Token consume(TokenKind expected, const std::string &error_message);
        Token consume_right_angle(); // Special handler for > in generic contexts
        bool is_at_end() const;

        // Error handling
        void error(const std::string &message);
        void synchronize();

        // Type parsing
        std::string parse_type();
        // If out_type_string is provided, the raw type string will be stored there
        TypeRef parse_type_annotation(std::string *out_type_string = nullptr);
        std::vector<std::string> parse_type_list(); // For arrays like i32[][]

        // Documentation comment handling
        void collect_documentation_comments();
        std::string extract_documentation_text();
        void attach_documentation(DeclarationNode *node);

        // Namespace parsing
        std::string parse_namespace();

        // Statement parsing
        std::unique_ptr<ASTNode> parse_statement();
        std::unique_ptr<VariableDeclarationNode> parse_variable_declaration();
        std::unique_ptr<FunctionDeclarationNode> parse_function_declaration();
        std::unique_ptr<FunctionDeclarationNode> parse_extern_function_declaration();
        void parse_where_clause(FunctionDeclarationNode *func_decl);
        std::unique_ptr<DeclarationNode> parse_intrinsic_declaration();
        std::unique_ptr<IntrinsicConstDeclarationNode> parse_intrinsic_const_declaration(SourceLocation start_loc);
        std::unique_ptr<ImportDeclarationNode> parse_import_declaration();
        std::unique_ptr<ModuleDeclarationNode> parse_module_declaration();
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
        std::unique_ptr<ExpressionNode> parse_if_expression();
        std::unique_ptr<ASTNode> parse_while_statement();
        std::unique_ptr<ASTNode> parse_loop_statement();
        std::unique_ptr<ASTNode> parse_for_statement();
        std::unique_ptr<ASTNode> parse_match_statement();
        std::unique_ptr<ASTNode> parse_switch_statement();
        std::unique_ptr<CaseStatementNode> parse_case_statement();
        std::unique_ptr<MatchArmNode> parse_match_arm();
        std::unique_ptr<PatternNode> parse_pattern();
        std::unique_ptr<ASTNode> parse_break_statement();
        std::unique_ptr<ASTNode> parse_continue_statement();
        std::unique_ptr<ASTNode> parse_unsafe_block_statement();
        std::unique_ptr<ASTNode> parse_expression_statement();

        // Parameter parsing for functions
        std::pair<std::vector<std::unique_ptr<VariableDeclarationNode>>, bool> parse_parameter_list();
        std::unique_ptr<VariableDeclarationNode> parse_parameter();
        bool is_this_parameter();
        std::unique_ptr<VariableDeclarationNode> parse_this_parameter();
        bool peek_variadic_parameter();
        std::unique_ptr<VariableDeclarationNode> parse_variadic_parameter();

        // Expression parsing (precedence climbing)
        std::unique_ptr<ExpressionNode> parse_expression();
        std::unique_ptr<ExpressionNode> parse_assignment();
        std::unique_ptr<ExpressionNode> parse_conditional();
        std::unique_ptr<ExpressionNode> parse_logical_or();
        std::unique_ptr<ExpressionNode> parse_logical_and();
        std::unique_ptr<ExpressionNode> parse_equality();
        std::unique_ptr<ExpressionNode> parse_bitwise_or();
        std::unique_ptr<ExpressionNode> parse_bitwise_xor();
        std::unique_ptr<ExpressionNode> parse_bitwise_and();
        std::unique_ptr<ExpressionNode> parse_cast();
        std::unique_ptr<ExpressionNode> parse_relational();
        std::unique_ptr<ExpressionNode> parse_shift();
        std::unique_ptr<ExpressionNode> parse_additive();
        std::unique_ptr<ExpressionNode> parse_multiplicative();
        std::unique_ptr<ExpressionNode> parse_unary();
        std::unique_ptr<ExpressionNode> parse_primary();
        std::unique_ptr<ExpressionNode> parse_call_expression(std::unique_ptr<ExpressionNode> expr);
        std::unique_ptr<ExpressionNode> parse_new_expression();
        std::unique_ptr<ExpressionNode> parse_sizeof_expression();
        std::unique_ptr<ExpressionNode> parse_alignof_expression();
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

        // Directive parsing
        std::unique_ptr<DirectiveNode> parse_directive();
        bool is_directive_start(); // Check for # followed by [
        std::vector<std::unique_ptr<DirectiveNode>> parse_leading_directives();

    public:
        // Directive parsing utilities (public for directive processors)
        Token peek_current() const { return _current_token; }
        bool match_token(TokenKind kind);
        void advance_token();
        Token consume_token(TokenKind expected, const std::string &error_message);
        void report_error(const std::string &message);
        bool is_parser_at_end() const;

    private:
        // Utility methods
        bool is_type_token() const;
        bool is_primitive_type_token() const;
        bool is_integer_primitive_type_token() const;
        bool is_visibility_modifier() const;
        bool is_variable_modifier() const; // const, mut
        int get_operator_precedence(TokenKind kind) const;
        bool is_binary_operator(TokenKind kind) const;
        bool is_unary_operator(TokenKind kind) const;
        bool is_assignment_operator(TokenKind kind) const;

        // Function type parsing helpers
        bool is_function_type_ahead();
        std::string parse_function_type_syntax(const std::string &type_prefix);
        std::string parse_generic_function_type_syntax(const std::string &type_prefix);

        // Look ahead utilities
        Token peek() const { return _current_token; }
        Token peek_next();
        Token peek_next_n(int n);

        // Generic call detection - scans ahead to determine if '<' starts a generic call
        // Returns true if pattern matches: identifier<type_args>(
        bool is_generic_call_ahead();

        // Type resolution helper
        TypeRef resolve_type_from_string(const std::string &type_str);

        // Enhanced type parsing using tokens
        // If out_type_string is provided, the raw type string will be stored there
        TypeRef parse_type_annotation_with_tokens(std::string *out_type_string = nullptr);

        // Type parsing helpers for cast expressions
        std::string parse_generic_type_suffix();

        // Enhanced diagnostic helper methods
        bool is_delimiter_token(TokenKind kind) const;
        char get_delimiter_char(TokenKind kind) const;

        // Error recovery helper methods
        bool is_top_level_declaration_start(TokenKind kind) const;
        bool is_statement_start(TokenKind kind) const;
        bool is_forced_recovery_point(TokenKind kind) const;

        // Bracket depth management
        void update_bracket_depth(TokenKind kind);
        void reset_parsing_state();
    };
}