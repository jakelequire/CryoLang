#include "Parser/Parser.hpp"
#include <iostream>

namespace Cryo
{
    Parser::Parser(std::unique_ptr<Lexer> lexer, ASTContext &context)
        : _lexer(std::move(lexer)), _context(context), _builder(context), _diagnostic_manager(nullptr)
    {
        // Initialize by getting the first token
        advance();
    }

    Parser::Parser(std::unique_ptr<Lexer> lexer, ASTContext &context, DiagnosticManager *diagnostic_manager, const std::string &source_file)
        : _lexer(std::move(lexer)), _context(context), _builder(context), _diagnostic_manager(diagnostic_manager), _source_file(source_file)
    {
        // Initialize by getting the first token
        advance();
    }

    void Parser::report_error(DiagnosticID id, const std::string &message, SourceRange range)
    {
        if (_diagnostic_manager)
        {
            _diagnostic_manager->report_error(id, DiagnosticCategory::Parser, message, range, _source_file);
        }
        else
        {
            // Fallback to old error system
            std::cerr << "Parse Error: " << message << std::endl;
        }
    }

    void Parser::report_warning(DiagnosticID id, const std::string &message, SourceRange range)
    {
        if (_diagnostic_manager)
        {
            _diagnostic_manager->report_warning(id, DiagnosticCategory::Parser, message, range, _source_file);
        }
        else
        {
            // Fallback to old warning system
            std::cerr << "Parse Warning: " << message << std::endl;
        }
    }

    std::unique_ptr<ProgramNode> Parser::parse_program()
    {
        auto program = _builder.create_program_node(SourceLocation{});

        // Parse optional namespace declaration
        if (_current_token.is(TokenKind::TK_KW_NAMESPACE))
        {
            std::string namespace_name = parse_namespace();
            // TODO: Store namespace information in program node or context
        }

        // Parse statements until EOF
        while (!is_at_end())
        {
            try
            {
                auto stmt = parse_statement();
                if (stmt)
                {
                    program->add_statement(std::move(stmt));
                }
            }
            catch (const ParseError &e)
            {
                _errors.push_back(e);
                synchronize(); // Recover from error
            }
        }

        return program;
    }

    // Token management
    void Parser::advance()
    {
        do
        {
            if (_lexer && _lexer->has_more_tokens())
            {
                _current_token = _lexer->next_token();
            }
            else
            {
                // When no more tokens available, set current token to EOF
                _current_token = Token(TokenKind::TK_EOF, "", SourceLocation{});
                break;
            }
        } while (_current_token.is(TokenKind::TK_COMMENT)); // Skip comment tokens
    }

    bool Parser::match(TokenKind kind)
    {
        if (_current_token.is(kind))
        {
            advance();
            return true;
        }
        return false;
    }

    bool Parser::match(std::initializer_list<TokenKind> kinds)
    {
        for (auto kind : kinds)
        {
            if (_current_token.is(kind))
            {
                advance();
                return true;
            }
        }
        return false;
    }

    Token Parser::consume(TokenKind expected, const std::string &error_message)
    {
        if (_current_token.is(expected))
        {
            Token token = _current_token;
            advance();
            return token;
        }

        error(error_message);
        return Token{}; // Return empty token on error
    }

    bool Parser::is_at_end() const
    {
        return _current_token.is_eof();
    }

    // Error handling
    void Parser::error(const std::string &message)
    {
        // Create a source range from the current token location
        SourceRange range;
        range.start = _current_token.location();
        range.end = _current_token.location();

        // Report to GDM if available
        report_error(DiagnosticID::Unknown, message, range);

        // Still throw for old error handling compatibility
        throw ParseError(message, _current_token.location());
    }
    void Parser::synchronize()
    {
        // Skip tokens until we find a statement boundary
        while (!is_at_end())
        {
            if (_current_token.is(TokenKind::TK_SEMICOLON))
            {
                advance();
                return;
            }

            // Check for statement start tokens
            if (match({TokenKind::TK_KW_FUNCTION, TokenKind::TK_KW_CONST, TokenKind::TK_KW_MUT,
                       TokenKind::TK_KW_IF, TokenKind::TK_KW_WHILE, TokenKind::TK_KW_FOR,
                       TokenKind::TK_KW_RETURN, TokenKind::TK_KW_BREAK, TokenKind::TK_KW_CONTINUE}))
            {
                return;
            }

            advance();
        }
    }

    // Type parsing
    std::string Parser::parse_type()
    {
        if (!is_type_token())
        {
            error("Expected type");
            return "";
        }

        Token type_token = _current_token;
        advance();

        std::string base_type = std::string(type_token.text());

        // Handle array types (e.g., i32[], str[][])
        while (_current_token.is(TokenKind::TK_L_SQUARE))
        {
            consume(TokenKind::TK_L_SQUARE, "Expected '['");
            consume(TokenKind::TK_R_SQUARE, "Expected ']'");
            base_type += "[]";
        }

        return base_type;
    }

    Type *Parser::parse_type_annotation()
    {
        if (!is_type_token())
        {
            error("Expected type");
            return _context.types().get_unknown_type();
        }

        Token type_token = _current_token;
        advance();

        // Get base type from token kind
        Type *base_type = _context.types().resolve_type_from_token_kind(static_cast<int>(type_token.kind()));

        // Handle array types (e.g., i32[], str[][])
        while (_current_token.is(TokenKind::TK_L_SQUARE))
        {
            consume(TokenKind::TK_L_SQUARE, "Expected '['");
            consume(TokenKind::TK_R_SQUARE, "Expected ']'");
            base_type = _context.types().create_array_type(base_type);
        }

        return base_type;
    }

    // Namespace parsing
    std::string Parser::parse_namespace()
    {
        consume(TokenKind::TK_KW_NAMESPACE, "Expected 'namespace'");

        std::string namespace_name;

        // Parse namespace identifier (can be dotted like Examples.BinOp)
        if (_current_token.is(TokenKind::TK_IDENTIFIER))
        {
            namespace_name = std::string(_current_token.text());
            advance();

            while (_current_token.is(TokenKind::TK_PERIOD))
            {
                advance(); // consume '.'
                if (_current_token.is(TokenKind::TK_IDENTIFIER))
                {
                    namespace_name += "." + std::string(_current_token.text());
                    advance();
                }
                else
                {
                    error("Expected identifier after '.'");
                    break;
                }
            }
        }
        else
        {
            error("Expected namespace identifier");
        }

        consume(TokenKind::TK_SEMICOLON, "Expected ';' after namespace declaration");
        return namespace_name;
    }

    // Statement parsing
    std::unique_ptr<ASTNode> Parser::parse_statement()
    {
        // Variable declarations
        if (_current_token.is(TokenKind::TK_KW_CONST) || _current_token.is(TokenKind::TK_KW_MUT))
        {
            return parse_variable_declaration();
        }

        // Function declarations
        if (_current_token.is(TokenKind::TK_KW_FUNCTION) || is_visibility_modifier())
        {
            return parse_function_declaration();
        }

        // Control flow statements
        if (_current_token.is(TokenKind::TK_KW_IF))
        {
            return parse_if_statement();
        }

        if (_current_token.is(TokenKind::TK_KW_WHILE))
        {
            return parse_while_statement();
        }

        if (_current_token.is(TokenKind::TK_KW_FOR))
        {
            return parse_for_statement();
        }

        if (_current_token.is(TokenKind::TK_KW_RETURN))
        {
            return parse_return_statement();
        }

        if (_current_token.is(TokenKind::TK_KW_BREAK))
        {
            return parse_break_statement();
        }

        if (_current_token.is(TokenKind::TK_KW_CONTINUE))
        {
            return parse_continue_statement();
        }

        if (_current_token.is(TokenKind::TK_L_BRACE))
        {
            return parse_block_statement();
        }

        // Expression statements
        return parse_expression_statement();
    }

    std::unique_ptr<VariableDeclarationNode> Parser::parse_variable_declaration()
    {
        SourceLocation start_loc = _current_token.location();

        // Parse variable modifier (const or mut)
        bool is_mutable = false;
        if (_current_token.is(TokenKind::TK_KW_CONST))
        {
            advance();
            is_mutable = false;
        }
        else if (_current_token.is(TokenKind::TK_KW_MUT))
        {
            advance();
            is_mutable = true;
        }
        else
        {
            error("Expected 'const' or 'mut'");
        }

        // Parse variable name first (correct syntax: const identifier: type)
        Token name_token = consume(TokenKind::TK_IDENTIFIER, "Expected variable name");
        std::string var_name = std::string(name_token.text());

        // Parse required colon and type annotation
        consume(TokenKind::TK_COLON, "Expected ':' after variable name");
        Type *var_type = parse_type_annotation();

        // Parse optional initializer
        std::unique_ptr<ExpressionNode> initializer = nullptr;
        if (_current_token.is(TokenKind::TK_EQUAL))
        {
            advance(); // consume '='
            initializer = parse_expression();
        }

        consume(TokenKind::TK_SEMICOLON, "Expected ';' after variable declaration");

        auto var_decl = _builder.create_variable_declaration(start_loc, var_name, var_type->to_string(), std::move(initializer), is_mutable);

        return var_decl;
    }

    std::unique_ptr<FunctionDeclarationNode> Parser::parse_function_declaration()
    {
        SourceLocation start_loc = _current_token.location();

        // Parse optional visibility modifier
        bool is_public = false;
        if (_current_token.is(TokenKind::TK_KW_PUBLIC))
        {
            is_public = true;
            advance();
        }
        else if (_current_token.is(TokenKind::TK_KW_PRIVATE))
        {
            is_public = false;
            advance();
        }

        consume(TokenKind::TK_KW_FUNCTION, "Expected 'function'");

        // Parse function name
        Token name_token = consume(TokenKind::TK_IDENTIFIER, "Expected function name");
        std::string func_name = std::string(name_token.text());

        // Parse parameter list first to get parameters
        consume(TokenKind::TK_L_PAREN, "Expected '(' after function name");

        std::vector<std::unique_ptr<VariableDeclarationNode>> params;
        if (!_current_token.is(TokenKind::TK_R_PAREN))
        {
            params = parse_parameter_list();
        }

        consume(TokenKind::TK_R_PAREN, "Expected ')' after parameters");

        // Parse return type
        Type *return_type = _context.types().get_void_type();
        if (_current_token.is(TokenKind::TK_ARROW))
        {
            advance(); // consume '->'
            return_type = parse_type_annotation();
        }

        // Create function declaration with type information
        auto func_decl = _builder.create_function_declaration(start_loc, func_name, return_type->to_string(), is_public);

        // Add parameters to function
        for (auto &param : params)
        {
            func_decl->add_parameter(std::move(param));
        }

        // Parse function body
        auto body = parse_block_statement();
        func_decl->set_body(std::unique_ptr<BlockStatementNode>(
            dynamic_cast<BlockStatementNode *>(body.release())));

        return func_decl;
    }

    std::unique_ptr<ReturnStatementNode> Parser::parse_return_statement()
    {
        SourceLocation start_loc = _current_token.location();
        consume(TokenKind::TK_KW_RETURN, "Expected 'return'");

        std::unique_ptr<ExpressionNode> expr = nullptr;
        if (!_current_token.is(TokenKind::TK_SEMICOLON))
        {
            expr = parse_expression();
        }

        consume(TokenKind::TK_SEMICOLON, "Expected ';' after return statement");

        return _builder.create_return_statement(start_loc, std::move(expr));
    }

    std::unique_ptr<BlockStatementNode> Parser::parse_block_statement()
    {
        SourceLocation start_loc = _current_token.location();
        consume(TokenKind::TK_L_BRACE, "Expected '{'");

        auto block = _builder.create_block_statement(start_loc);

        while (!_current_token.is(TokenKind::TK_R_BRACE) && !is_at_end())
        {
            try
            {
                auto stmt = parse_statement();
                if (stmt)
                {
                    // Convert to StatementNode if needed
                    if (auto statement_node = dynamic_cast<StatementNode *>(stmt.get()))
                    {
                        stmt.release(); // Release ownership
                        block->add_statement(std::unique_ptr<StatementNode>(statement_node));
                    }
                    else if (auto decl_node = dynamic_cast<DeclarationNode *>(stmt.get()))
                    {
                        // Wrap the declaration in a DeclarationStatementNode
                        stmt.release(); // Release ownership
                        auto decl_stmt = _builder.create_declaration_statement(decl_node->location(),
                                                                               std::unique_ptr<DeclarationNode>(decl_node));
                        block->add_statement(std::move(decl_stmt));
                    }
                }
            }
            catch (const ParseError &e)
            {
                _errors.push_back(e);
                synchronize(); // Recover from error and continue parsing block
            }
        }

        consume(TokenKind::TK_R_BRACE, "Expected '}'");
        return block;
    }

    // Expression parsing implementation
    std::unique_ptr<ExpressionNode> Parser::parse_expression()
    {
        return parse_assignment();
    }

    std::unique_ptr<ExpressionNode> Parser::parse_assignment()
    {
        auto expr = parse_conditional();

        if (is_assignment_operator(_current_token.kind()))
        {
            Token op = _current_token;
            advance();
            auto right = parse_assignment();

            // For now, treat assignment as binary expression
            // TODO: Create proper assignment node
            return _builder.create_binary_expression(op, std::move(expr), std::move(right));
        }

        return expr;
    }

    std::unique_ptr<ExpressionNode> Parser::parse_conditional()
    {
        auto expr = parse_logical_or();

        // Check for ternary operator: condition ? true_expr : false_expr
        if (_current_token.is(TokenKind::TK_QUESTION))
        {
            SourceLocation ternary_loc = _current_token.location();
            advance(); // consume '?'

            auto true_expr = parse_expression();

            consume(TokenKind::TK_COLON, "Expected ':' after true expression in ternary operator");

            auto false_expr = parse_conditional(); // Right associative

            return _builder.create_ternary_expression(ternary_loc, std::move(expr), std::move(true_expr), std::move(false_expr));
        }

        return expr;
    }

    std::unique_ptr<ExpressionNode> Parser::parse_logical_or()
    {
        auto expr = parse_logical_and();

        while (_current_token.is(TokenKind::TK_PIPEPIPE))
        {
            Token op = _current_token;
            advance();
            auto right = parse_logical_and();
            expr = _builder.create_binary_expression(op, std::move(expr), std::move(right));
        }

        return expr;
    }

    std::unique_ptr<ExpressionNode> Parser::parse_logical_and()
    {
        auto expr = parse_equality();

        while (_current_token.is(TokenKind::TK_AMPAMP))
        {
            Token op = _current_token;
            advance();
            auto right = parse_equality();
            expr = _builder.create_binary_expression(op, std::move(expr), std::move(right));
        }

        return expr;
    }

    std::unique_ptr<ExpressionNode> Parser::parse_equality()
    {
        auto expr = parse_relational();

        while (_current_token.is(TokenKind::TK_EQUALEQUAL) || _current_token.is(TokenKind::TK_EXCLAIMEQUAL))
        {
            Token op = _current_token;
            advance();
            auto right = parse_relational();
            expr = _builder.create_binary_expression(op, std::move(expr), std::move(right));
        }

        return expr;
    }

    std::unique_ptr<ExpressionNode> Parser::parse_relational()
    {
        auto expr = parse_additive();

        while (_current_token.is(TokenKind::TK_L_ANGLE) || _current_token.is(TokenKind::TK_R_ANGLE) ||
               _current_token.is(TokenKind::TK_LESSEQUAL) || _current_token.is(TokenKind::TK_GREATEREQUAL))
        {
            Token op = _current_token;
            advance();
            auto right = parse_additive();
            expr = _builder.create_binary_expression(op, std::move(expr), std::move(right));
        }

        return expr;
    }

    std::unique_ptr<ExpressionNode> Parser::parse_additive()
    {
        auto expr = parse_multiplicative();

        while (_current_token.is(TokenKind::TK_PLUS) || _current_token.is(TokenKind::TK_MINUS))
        {
            Token op = _current_token;
            advance();
            auto right = parse_multiplicative();
            expr = _builder.create_binary_expression(op, std::move(expr), std::move(right));
        }

        return expr;
    }

    std::unique_ptr<ExpressionNode> Parser::parse_multiplicative()
    {
        auto expr = parse_unary();

        while (_current_token.is(TokenKind::TK_STAR) || _current_token.is(TokenKind::TK_SLASH) || _current_token.is(TokenKind::TK_PERCENT))
        {
            Token op = _current_token;
            advance();
            auto right = parse_unary();
            expr = _builder.create_binary_expression(op, std::move(expr), std::move(right));
        }

        return expr;
    }

    std::unique_ptr<ExpressionNode> Parser::parse_unary()
    {
        if (_current_token.is(TokenKind::TK_MINUS) || _current_token.is(TokenKind::TK_EXCLAIM))
        {
            Token op = _current_token;
            advance();
            auto expr = parse_unary();

            // Create unary expression (treat as binary with null left operand for now)
            // TODO: Create proper unary expression node
            return _builder.create_binary_expression(op, nullptr, std::move(expr));
        }

        return parse_primary();
    }

    std::unique_ptr<ExpressionNode> Parser::parse_primary()
    {
        // Literals
        if (_current_token.is(TokenKind::TK_NUMERIC_CONSTANT))
        {
            return parse_number_literal();
        }

        if (_current_token.is(TokenKind::TK_STRING_LITERAL))
        {
            return parse_string_literal();
        }

        if (_current_token.is(TokenKind::TK_CHAR_CONSTANT))
        {
            return parse_character_literal();
        }

        if (_current_token.is(TokenKind::TK_KW_TRUE) || _current_token.is(TokenKind::TK_KW_FALSE))
        {
            return parse_boolean_literal();
        }

        // Identifiers (can be function calls or variable references)
        if (_current_token.is(TokenKind::TK_IDENTIFIER))
        {
            auto identifier = parse_identifier();

            // Handle postfix expressions (function calls, array access, etc.)
            std::unique_ptr<ExpressionNode> expr = std::move(identifier);

            // Chain multiple postfix operations (e.g., func()[0].method())
            while (true)
            {
                if (_current_token.is(TokenKind::TK_L_PAREN))
                {
                    // Function call
                    expr = parse_call_expression(std::move(expr));
                }
                else if (_current_token.is(TokenKind::TK_L_SQUARE))
                {
                    // Array access
                    expr = parse_array_access(std::move(expr));
                }
                else
                {
                    // No more postfix operations
                    break;
                }
            }

            return expr;
        }

        // Parenthesized expressions
        if (_current_token.is(TokenKind::TK_L_PAREN))
        {
            advance(); // consume '('
            auto expr = parse_expression();
            consume(TokenKind::TK_R_PAREN, "Expected ')' after expression");
            return expr;
        }

        // Array literals
        if (_current_token.is(TokenKind::TK_L_SQUARE))
        {
            return parse_array_literal();
        }

        error("Expected expression");
        return nullptr;
    }

    // Literal parsing
    std::unique_ptr<LiteralNode> Parser::parse_number_literal()
    {
        Token token = consume(TokenKind::TK_NUMERIC_CONSTANT, "Expected number");
        return _builder.create_literal_node(token);
    }

    std::unique_ptr<LiteralNode> Parser::parse_string_literal()
    {
        Token token = consume(TokenKind::TK_STRING_LITERAL, "Expected string");
        return _builder.create_literal_node(token);
    }

    std::unique_ptr<LiteralNode> Parser::parse_boolean_literal()
    {
        Token token = _current_token;
        if (_current_token.is(TokenKind::TK_KW_TRUE) || _current_token.is(TokenKind::TK_KW_FALSE))
        {
            advance();
            return _builder.create_literal_node(token);
        }

        error("Expected boolean literal");
        return nullptr;
    }

    std::unique_ptr<LiteralNode> Parser::parse_character_literal()
    {
        Token token = consume(TokenKind::TK_CHAR_CONSTANT, "Expected character");
        return _builder.create_literal_node(token);
    }

    std::unique_ptr<IdentifierNode> Parser::parse_identifier()
    {
        Token token = consume(TokenKind::TK_IDENTIFIER, "Expected identifier");
        return _builder.create_identifier_node(token);
    }

    // Utility methods
    bool Parser::is_type_token() const
    {
        return _current_token.is(TokenKind::TK_KW_I8) ||
               _current_token.is(TokenKind::TK_KW_I16) ||
               _current_token.is(TokenKind::TK_KW_I32) ||
               _current_token.is(TokenKind::TK_KW_I64) ||
               _current_token.is(TokenKind::TK_KW_INT) ||
               _current_token.is(TokenKind::TK_KW_UINT) ||
               _current_token.is(TokenKind::TK_KW_UINT8) ||
               _current_token.is(TokenKind::TK_KW_UINT16) ||
               _current_token.is(TokenKind::TK_KW_UINT32) ||
               _current_token.is(TokenKind::TK_KW_UINT64) ||
               _current_token.is(TokenKind::TK_KW_FLOAT) ||
               _current_token.is(TokenKind::TK_KW_F32) ||
               _current_token.is(TokenKind::TK_KW_F64) ||
               _current_token.is(TokenKind::TK_KW_DOUBLE) ||
               _current_token.is(TokenKind::TK_KW_BOOLEAN) ||
               _current_token.is(TokenKind::TK_KW_CHAR) ||
               _current_token.is(TokenKind::TK_KW_STRING) ||
               _current_token.is(TokenKind::TK_KW_VOID) ||
               _current_token.is(TokenKind::TK_IDENTIFIER); // For user-defined types
    }

    bool Parser::is_visibility_modifier() const
    {
        return _current_token.is(TokenKind::TK_KW_PUBLIC) ||
               _current_token.is(TokenKind::TK_KW_PRIVATE);
    }

    bool Parser::is_variable_modifier() const
    {
        return _current_token.is(TokenKind::TK_KW_CONST) ||
               _current_token.is(TokenKind::TK_KW_MUT);
    }

    bool Parser::is_assignment_operator(TokenKind kind) const
    {
        return kind == TokenKind::TK_EQUAL ||
               kind == TokenKind::TK_PLUSEQUAL ||
               kind == TokenKind::TK_MINUSEQUAL ||
               kind == TokenKind::TK_STAREQUAL ||
               kind == TokenKind::TK_SLASHEQUAL;
    }

    // Stub implementations for remaining methods
    std::unique_ptr<ASTNode> Parser::parse_if_statement()
    {
        SourceLocation start_loc = _current_token.location();
        consume(TokenKind::TK_KW_IF, "Expected 'if'");

        consume(TokenKind::TK_L_PAREN, "Expected '(' after 'if'");
        auto condition = parse_expression();
        consume(TokenKind::TK_R_PAREN, "Expected ')' after if condition");

        auto then_stmt = parse_statement();

        std::unique_ptr<StatementNode> else_stmt = nullptr;
        if (_current_token.is(TokenKind::TK_KW_ELSE))
        {
            advance(); // consume 'else'
            auto else_node = parse_statement();
            if (auto stmt = dynamic_cast<StatementNode *>(else_node.get()))
            {
                else_node.release();
                else_stmt = std::unique_ptr<StatementNode>(stmt);
            }
        }

        // Convert then_stmt to StatementNode
        if (auto stmt = dynamic_cast<StatementNode *>(then_stmt.get()))
        {
            then_stmt.release();
            auto then_statement = std::unique_ptr<StatementNode>(stmt);
            return _builder.create_if_statement(start_loc, std::move(condition), std::move(then_statement), std::move(else_stmt));
        }

        error("Invalid then statement in if");
        return nullptr;
    }

    std::unique_ptr<ASTNode> Parser::parse_while_statement()
    {
        SourceLocation start_loc = _current_token.location();
        consume(TokenKind::TK_KW_WHILE, "Expected 'while'");

        consume(TokenKind::TK_L_PAREN, "Expected '(' after 'while'");
        auto condition = parse_expression();
        consume(TokenKind::TK_R_PAREN, "Expected ')' after while condition");

        auto body = parse_statement();
        if (auto stmt = dynamic_cast<StatementNode *>(body.get()))
        {
            body.release();
            auto body_stmt = std::unique_ptr<StatementNode>(stmt);
            return _builder.create_while_statement(start_loc, std::move(condition), std::move(body_stmt));
        }

        error("Invalid body statement in while");
        return nullptr;
    }

    std::unique_ptr<ASTNode> Parser::parse_for_statement()
    {
        SourceLocation start_loc = _current_token.location();
        consume(TokenKind::TK_KW_FOR, "Expected 'for'");

        consume(TokenKind::TK_L_PAREN, "Expected '(' after 'for'");

        // Parse initialization (variable declaration)
        auto init = parse_variable_declaration();

        // Parse condition
        auto condition = parse_expression();
        consume(TokenKind::TK_SEMICOLON, "Expected ';' after for condition");

        // Parse update expression
        auto update = parse_expression();
        consume(TokenKind::TK_R_PAREN, "Expected ')' after for clauses");

        // Parse body
        auto body = parse_statement();
        if (auto stmt = dynamic_cast<StatementNode *>(body.get()))
        {
            body.release();
            auto body_stmt = std::unique_ptr<StatementNode>(stmt);
            return _builder.create_for_statement(start_loc, std::move(init), std::move(condition), std::move(update), std::move(body_stmt));
        }

        error("Invalid body statement in for");
        return nullptr;
    }

    std::unique_ptr<ASTNode> Parser::parse_break_statement()
    {
        SourceLocation start_loc = _current_token.location();
        consume(TokenKind::TK_KW_BREAK, "Expected 'break'");
        consume(TokenKind::TK_SEMICOLON, "Expected ';' after 'break'");

        return _builder.create_break_statement(start_loc);
    }

    std::unique_ptr<ASTNode> Parser::parse_continue_statement()
    {
        SourceLocation start_loc = _current_token.location();
        consume(TokenKind::TK_KW_CONTINUE, "Expected 'continue'");
        consume(TokenKind::TK_SEMICOLON, "Expected ';' after 'continue'");

        return _builder.create_continue_statement(start_loc);
    }

    std::unique_ptr<ASTNode> Parser::parse_expression_statement()
    {
        SourceLocation start_loc = _current_token.location();
        auto expr = parse_expression();
        consume(TokenKind::TK_SEMICOLON, "Expected ';' after expression");

        return _builder.create_expression_statement(start_loc, std::move(expr));
    }

    std::vector<std::unique_ptr<VariableDeclarationNode>> Parser::parse_parameter_list()
    {
        std::vector<std::unique_ptr<VariableDeclarationNode>> params;

        params.push_back(parse_parameter());

        while (_current_token.is(TokenKind::TK_COMMA))
        {
            advance(); // consume ','
            params.push_back(parse_parameter());
        }

        return params;
    }

    std::unique_ptr<VariableDeclarationNode> Parser::parse_parameter()
    {
        // Parse parameter name
        Token name_token = consume(TokenKind::TK_IDENTIFIER, "Expected parameter name");
        std::string param_name = std::string(name_token.text());

        // Parse type annotation
        consume(TokenKind::TK_COLON, "Expected ':' after parameter name");
        Type *param_type = parse_type_annotation();

        // Create parameter as variable declaration (without initializer)
        return _builder.create_variable_declaration(name_token.location(), param_name, param_type->to_string());
    }

    std::unique_ptr<ExpressionNode> Parser::parse_call_expression(std::unique_ptr<ExpressionNode> expr)
    {
        SourceLocation call_location = _current_token.location();
        consume(TokenKind::TK_L_PAREN, "Expected '(' to start function call");

        // Create call expression with the callee (function name/expression)
        auto call_expr = _builder.create_call_expression(call_location, std::move(expr));

        // Parse arguments if any
        if (!_current_token.is(TokenKind::TK_R_PAREN))
        {
            do
            {
                auto arg = parse_expression();
                if (arg)
                {
                    call_expr->add_argument(std::move(arg));
                }
            } while (match(TokenKind::TK_COMMA));
        }

        consume(TokenKind::TK_R_PAREN, "Expected ')' after function arguments");

        return call_expr;
    }

    std::unique_ptr<ExpressionNode> Parser::parse_array_access(std::unique_ptr<ExpressionNode> expr)
    {
        SourceLocation access_location = _current_token.location();
        consume(TokenKind::TK_L_SQUARE, "Expected '[' for array access");

        auto index = parse_expression();
        if (!index)
        {
            error("Expected expression for array index");
            return expr;
        }

        consume(TokenKind::TK_R_SQUARE, "Expected ']' after array index");

        return _builder.create_array_access(access_location, std::move(expr), std::move(index));
    }

    std::unique_ptr<ExpressionNode> Parser::parse_array_literal()
    {
        SourceLocation start_loc = _current_token.location();
        consume(TokenKind::TK_L_SQUARE, "Expected '[' for array literal");

        auto array_literal = _builder.create_array_literal(start_loc);

        // Handle empty arrays
        if (_current_token.is(TokenKind::TK_R_SQUARE))
        {
            advance(); // consume ']'
            return array_literal;
        }

        // Parse array elements
        do
        {
            auto element = parse_expression();
            if (element)
            {
                array_literal->add_element(std::move(element));
            }

            if (_current_token.is(TokenKind::TK_COMMA))
            {
                advance(); // consume ','
            }
            else
            {
                break;
            }
        } while (!_current_token.is(TokenKind::TK_R_SQUARE) && !is_at_end());

        consume(TokenKind::TK_R_SQUARE, "Expected ']' after array elements");
        return array_literal;
    }

    Token Parser::peek_next()
    {
        // TODO: Implement lookahead
        return Token{};
    }
}