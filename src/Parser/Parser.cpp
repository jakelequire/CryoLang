#include "Parser/Parser.hpp"
#include "Utils/Logger.hpp"
#include "GDM/GDM.hpp"
#include "GDM/DiagnosticBuilders.hpp"
#include <iostream>
#include <cctype>
#include <algorithm>

namespace Cryo
{
    Parser::Parser(std::unique_ptr<Lexer> lexer, ASTContext &context)
        : _lexer(std::move(lexer)), _context(context), _builder(context), _diagnostic_manager(nullptr), _diagnostic_builder(nullptr)
    {
        // Initialize by getting the first token
        advance();
    }

    Parser::Parser(std::unique_ptr<Lexer> lexer, ASTContext &context, DiagnosticManager *diagnostic_manager, const std::string &source_file)
        : _lexer(std::move(lexer)), _context(context), _builder(context), _diagnostic_manager(diagnostic_manager), _source_file(source_file)
    {
        // Initialize diagnostic builder if diagnostic manager is available
        if (_diagnostic_manager)
        {
            _diagnostic_builder = std::make_unique<ParserDiagnosticBuilder>(_diagnostic_manager, std::string(_source_file));
        }

        // Initialize by getting the first token
        advance();
    }

    void Parser::report_error(ErrorCode error_code, const std::string &message, SourceRange range)
    {
        if (_diagnostic_manager)
        {
            _diagnostic_manager->create_error(error_code, range, _source_file);
        }
        else
        {
            // Fallback to old error system
            LOG_ERROR(LogComponent::PARSER, "Parse Error: {}", message);
        }
    }

    void Parser::report_warning(ErrorCode error_code, const std::string &message, SourceRange range)
    {
        if (_diagnostic_manager)
        {
            _diagnostic_manager->create_error(error_code, range, _source_file); // Note: warnings are handled by ErrorCode automatically
        }
        else
        {
            // Fallback to old warning system
            LOG_WARN(LogComponent::PARSER, "Parse Warning: {}", message);
        }
    }

    void Parser::report_enhanced_token_error(TokenKind expected, const std::string &context_message)
    {
        if (!_diagnostic_manager || !_diagnostic_builder)
        {
            // Fallback to basic error reporting
            error(context_message);
            return;
        }

        // Use member ParserDiagnosticBuilder for better error reporting

        // Check for specific token patterns and use appropriate diagnostic methods
        if (_current_token.is_eof())
        {
            // Handle unexpected EOF
            _diagnostic_builder->create_missing_token_error(expected, _current_token.location(), context_message);
        }
        else if (is_delimiter_token(expected))
        {
            // Handle delimiter-related errors with better context
            if (is_delimiter_token(_current_token.kind()))
            {
                // Mismatched delimiters
                char expected_char = get_delimiter_char(expected);
                char found_char = get_delimiter_char(_current_token.kind());
                _diagnostic_builder->create_mismatched_delimiter_error(found_char, expected_char,
                                                                       _current_token.location());
            }
            else
            {
                // Missing delimiter
                _diagnostic_builder->create_missing_token_error(expected, _current_token.location(), context_message);
            }
        }
        else
        {
            // Use general unexpected token error
            _diagnostic_builder->create_unexpected_token_error(_current_token, expected, context_message);
        }
    }

    ErrorCode Parser::get_token_error_code(TokenKind expected)
    {
        switch (expected)
        {
        case TokenKind::TK_SEMICOLON:
            return ErrorCode::E0106_EXPECTED_SEMICOLON;
        case TokenKind::TK_L_PAREN:
        case TokenKind::TK_R_PAREN:
            return ErrorCode::E0107_EXPECTED_PAREN;
        case TokenKind::TK_L_BRACE:
        case TokenKind::TK_R_BRACE:
            return ErrorCode::E0108_EXPECTED_BRACE;
        case TokenKind::TK_L_SQUARE:
        case TokenKind::TK_R_SQUARE:
            return ErrorCode::E0109_EXPECTED_BRACKET;
        case TokenKind::TK_IDENTIFIER:
            return ErrorCode::E0105_EXPECTED_IDENTIFIER;
        default:
            return ErrorCode::E0100_EXPECTED_TOKEN;
        }
    }

    std::string Parser::get_token_name(TokenKind kind)
    {
        switch (kind)
        {
        case TokenKind::TK_SEMICOLON:
            return ";";
        case TokenKind::TK_L_PAREN:
            return "(";
        case TokenKind::TK_R_PAREN:
            return ")";
        case TokenKind::TK_L_BRACE:
            return "{";
        case TokenKind::TK_R_BRACE:
            return "}";
        case TokenKind::TK_L_SQUARE:
            return "[";
        case TokenKind::TK_R_SQUARE:
            return "]";
        case TokenKind::TK_IDENTIFIER:
            return "identifier";
        case TokenKind::TK_EOF:
            return "end of file";
        case TokenKind::TK_ERROR:
            return "error";
        case TokenKind::TK_KW_FUNCTION:
            return "function";
        case TokenKind::TK_KW_CONST:
            return "const";
        case TokenKind::TK_KW_MUT:
            return "mut";
        case TokenKind::TK_KW_IF:
            return "if";
        case TokenKind::TK_KW_ELSE:
            return "else";
        case TokenKind::TK_KW_WHILE:
            return "while";
        case TokenKind::TK_KW_FOR:
            return "for";
        case TokenKind::TK_KW_RETURN:
            return "return";
        case TokenKind::TK_COLON:
            return ":";
        case TokenKind::TK_COMMA:
            return ",";
        case TokenKind::TK_PERIOD:
            return ".";
        case TokenKind::TK_ARROW:
            return "->";
        default:
            if (_current_token.text().empty())
                return "unknown";
            return std::string(_current_token.text());
        }
    }

    void Parser::add_token_mismatch_suggestions(Diagnostic &diagnostic, TokenKind expected, TokenKind actual, const std::string &context)
    {
        SourceRange current_range(_current_token.location(), _current_token.location());

        switch (expected)
        {
        case TokenKind::TK_SEMICOLON:
            diagnostic.add_suggestion(CodeSuggestion(
                "add a semicolon",
                SourceSpan(current_range, _source_file),
                ";",
                SuggestionApplicability::MachineApplicable,
                SuggestionStyle::ShowCode));
            diagnostic.add_note("statements must be terminated with a semicolon");
            break;

        case TokenKind::TK_R_PAREN:
            if (actual == TokenKind::TK_COMMA)
            {
                diagnostic.add_suggestion(CodeSuggestion(
                    "replace comma with closing parenthesis",
                    SourceSpan(current_range, _source_file),
                    ")",
                    SuggestionApplicability::MaybeIncorrect,
                    SuggestionStyle::ShowCode));
                diagnostic.add_note("function parameter lists must be closed with `)`");
            }
            else
            {
                diagnostic.add_suggestion(CodeSuggestion(
                    "add closing parenthesis",
                    SourceSpan(current_range, _source_file),
                    ")",
                    SuggestionApplicability::MachineApplicable,
                    SuggestionStyle::ShowCode));
                diagnostic.add_note("opened parenthesis must be closed");
            }
            break;

        case TokenKind::TK_R_BRACE:
            diagnostic.add_suggestion(CodeSuggestion(
                "add closing brace",
                SourceSpan(current_range, _source_file),
                "}",
                SuggestionApplicability::MachineApplicable,
                SuggestionStyle::ShowCode));
            diagnostic.add_note("code blocks must be closed with `}`");
            break;

        case TokenKind::TK_IDENTIFIER:
            if (actual == TokenKind::TK_KW_FUNCTION || actual == TokenKind::TK_KW_CONST)
            {
                diagnostic.add_note("expected an identifier (variable or function name) here");
                diagnostic.add_note("help: identifiers must start with a letter or underscore");
            }
            break;

        default:
            // Generic suggestion
            diagnostic.add_note("help: check the syntax and ensure proper token placement");
            break;
        }

        // Add context-specific help
        if (!context.empty() && context != get_token_name(expected))
        {
            diagnostic.add_note("context: " + context);
        }
    }

    void Parser::add_context_spans(Diagnostic &diagnostic, TokenKind expected)
    {
        // For closing delimiters, try to find the matching opening delimiter
        if (expected == TokenKind::TK_R_PAREN || expected == TokenKind::TK_R_BRACE || expected == TokenKind::TK_R_SQUARE)
        {
            // This is a simplified implementation - a more sophisticated version would
            // track delimiter pairs during parsing
            diagnostic.add_note("help: check for matching opening delimiter");
        }
    }

    std::unique_ptr<ProgramNode> Parser::parse_program()
    {
        auto program = _builder.create_program_node(SourceLocation{});

        // Parse file-level directives before anything else
        while (is_directive_start())
        {
            auto directive = parse_directive();
            if (directive)
            {
                program->add_statement(std::move(directive));
            }
        }

        // Parse optional namespace declaration
        if (_current_token.is(TokenKind::TK_KW_NAMESPACE))
        {
            std::string namespace_name = parse_namespace();
            _current_namespace = namespace_name; // Store the namespace in parser state
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

            // Collect documentation comments for later attachment
            if (_current_token.is(TokenKind::TK_DOC_COMMENT_BLOCK) ||
                _current_token.is(TokenKind::TK_DOC_COMMENT_LINE))
            {
                _pending_doc_comments.push_back(std::string(_current_token.text()));
            }

        } while (_current_token.is(TokenKind::TK_COMMENT) ||
                 _current_token.is(TokenKind::TK_DOC_COMMENT_BLOCK) ||
                 _current_token.is(TokenKind::TK_DOC_COMMENT_LINE)); // Skip all comment tokens
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

        // Enhanced error reporting with sophisticated diagnostics
        report_enhanced_token_error(expected, error_message);
        return Token{}; // Return empty token on error
    }

    Token Parser::consume_right_angle()
    {
        // Handle >> token splitting for nested generics like AllocResult<Box<int>>
        if (_current_token.is(TokenKind::TK_GREATERGREATER))
        {
            // We need to split >> into two > tokens
            // Return the first > and leave the second > in the stream
            Token first_angle = Token(TokenKind::TK_R_ANGLE, ">", _current_token.location());

            // Replace current token with the second >
            _current_token = Token(TokenKind::TK_R_ANGLE, ">", _current_token.location());

            return first_angle;
        }
        else if (_current_token.is(TokenKind::TK_R_ANGLE))
        {
            Token token = _current_token;
            advance();
            return token;
        }
        else
        {
            error("Expected '>' after generic type arguments");
            return Token{};
        }
    }

    bool Parser::is_delimiter_token(TokenKind kind) const
    {
        switch (kind)
        {
        case TokenKind::TK_L_PAREN:
        case TokenKind::TK_R_PAREN:
        case TokenKind::TK_L_BRACE:
        case TokenKind::TK_R_BRACE:
        case TokenKind::TK_L_SQUARE:
        case TokenKind::TK_R_SQUARE:
        case TokenKind::TK_SEMICOLON:
            return true;
        default:
            return false;
        }
    }

    char Parser::get_delimiter_char(TokenKind kind) const
    {
        switch (kind)
        {
        case TokenKind::TK_L_PAREN:
            return '(';
        case TokenKind::TK_R_PAREN:
            return ')';
        case TokenKind::TK_L_BRACE:
            return '{';
        case TokenKind::TK_R_BRACE:
            return '}';
        case TokenKind::TK_L_SQUARE:
            return '[';
        case TokenKind::TK_R_SQUARE:
            return ']';
        case TokenKind::TK_SEMICOLON:
            return ';';
        default:
            return '?'; // Unknown delimiter
        }
    }

    bool Parser::is_at_end() const
    {
        return _current_token.is_eof();
    }

    // Error handling
    void Parser::error(const std::string &message)
    {
        if (_diagnostic_manager)
        {
            // Use enhanced error reporting
            SourceRange range(_current_token.location(), _current_token.location());

            auto &diagnostic = _diagnostic_manager->create_error(ErrorCode::E0111_INVALID_SYNTAX, range, _source_file);
            SourceSpan primary_span(range.start, range.end, _source_file, true);
            primary_span.set_label(message);
            diagnostic.with_primary_span(primary_span);

            // Add contextual suggestions for common parsing errors
            add_generic_parsing_suggestions(diagnostic, message);

            // Note: Diagnostic is automatically stored by create_error
        }
        else
        {
            // Create a source range from the current token location
            SourceRange range;
            range.start = _current_token.location();
            range.end = _current_token.location();

            // Report to GDM if available
            report_error(ErrorCode::E0111_INVALID_SYNTAX, message, range);
        }

        // Still throw for old error handling compatibility
        throw ParseError(message, _current_token.location());
    }

    void Parser::add_generic_parsing_suggestions(Diagnostic &diagnostic, const std::string &message)
    {
        std::string lower_message = message;
        std::transform(lower_message.begin(), lower_message.end(), lower_message.begin(), ::tolower);

        if (lower_message.find("expression") != std::string::npos)
        {
            diagnostic.add_note("help: expressions include variables, function calls, literals, and operators");
            diagnostic.add_note("examples: `x`, `foo()`, `42`, `x + y`");
        }
        else if (lower_message.find("statement") != std::string::npos)
        {
            diagnostic.add_note("help: statements include variable declarations, assignments, and function calls");
            diagnostic.add_note("note: statements must end with a semicolon `;`");
        }
        else if (lower_message.find("type") != std::string::npos)
        {
            diagnostic.add_note("help: types include built-in types like `int`, `string`, `bool` and user-defined types");
            diagnostic.add_note("examples: `int`, `string`, `MyStruct`, `Array<int>`");
        }
        else if (lower_message.find("unexpected") != std::string::npos)
        {
            diagnostic.add_note("help: check for missing semicolons, braces, or parentheses");
            diagnostic.add_note("tip: ensure proper nesting of code blocks and expressions");
        }
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
                       TokenKind::TK_KW_TYPE, TokenKind::TK_KW_CLASS, TokenKind::TK_KW_IMPLEMENT,
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
        std::string type_prefix = "";

        // Handle type modifiers: const, unsigned
        if (_current_token.is(TokenKind::TK_KW_CONST))
        {
            type_prefix += "const ";
            advance();
        }

        if (_current_token.is(TokenKind::TK_KW_UNSIGNED))
        {
            type_prefix += "unsigned ";
            advance();
        }

        // Handle generic function types: <T>(params) -> return_type
        if (_current_token.is(TokenKind::TK_L_ANGLE))
        {
            return parse_generic_function_type_syntax(type_prefix);
        }

        // Handle reference types (&type, &mut type)
        if (_current_token.is(TokenKind::TK_AMP))
        {
            advance(); // consume '&'

            // Check for 'mut' after '&'
            std::string ref_prefix = "&";
            if (_current_token.is(TokenKind::TK_KW_MUT))
            {
                ref_prefix += "mut ";
                advance(); // consume 'mut'
            }

            std::string base_type = parse_type(); // recursive call
            return type_prefix + ref_prefix + base_type;
        }

        // Handle function types (() -> type) and tuple types (type1, type2, ...)
        if (_current_token.is(TokenKind::TK_L_PAREN))
        {
            // Look ahead to determine if this is a function type or tuple type
            // We need to scan ahead to see if there's an '->' after the closing ')'
            if (is_function_type_ahead())
            {
                return parse_function_type_syntax(type_prefix);
            }
            else
            {
                // This is a tuple type
                std::string tuple_type = "(";
                advance(); // consume '('

                // Parse tuple element types
                if (!_current_token.is(TokenKind::TK_R_PAREN))
                {
                    do
                    {
                        std::string element_type = parse_type();
                        tuple_type += element_type;

                        if (_current_token.is(TokenKind::TK_COMMA))
                        {
                            tuple_type += ", ";
                            advance(); // consume ','
                        }
                        else
                        {
                            break;
                        }
                    } while (true);
                }

                consume(TokenKind::TK_R_PAREN, "Expected ')' after tuple types");
                tuple_type += ")";
                return type_prefix + tuple_type;
            }
        }

        if (!is_type_token())
        {
            error("Expected type");
            return "";
        }

        Token type_token = _current_token;
        advance();

        std::string base_type = type_prefix + std::string(type_token.text());

        // Handle scope resolution (e.g., This::Error, T::Output)
        if (_current_token.is(TokenKind::TK_COLONCOLON))
        {
            advance(); // consume '::'
            if (!_current_token.is(TokenKind::TK_IDENTIFIER))
            {
                error("Expected identifier after '::'");
                return base_type;
            }

            std::string member_name = std::string(_current_token.text());
            advance();

            base_type += "::" + member_name;
        }

        // Handle generic instantiation (e.g., SimpleGeneric<int>)
        if (_current_token.is(TokenKind::TK_L_ANGLE))
        {
            base_type += "<";
            advance(); // consume '<'

            // Parse type arguments
            do
            {
                std::string arg_type = parse_type();
                base_type += arg_type;

                if (_current_token.is(TokenKind::TK_COMMA))
                {
                    base_type += ",";
                    advance(); // consume ','
                }
                else
                {
                    break;
                }
            } while (true);

            consume(TokenKind::TK_R_ANGLE, "Expected '>' after generic type arguments");
            base_type += ">";
        }

        // Handle array types (e.g., i32[], str[][])
        while (_current_token.is(TokenKind::TK_L_SQUARE))
        {
            consume(TokenKind::TK_L_SQUARE, "Expected '['");
            consume(TokenKind::TK_R_SQUARE, "Expected ']'");
            base_type += "[]";
        }

        // Handle pointer types (type *)
        while (_current_token.is(TokenKind::TK_STAR))
        {
            advance(); // consume '*'
            base_type += "*";
        }

        return base_type;
    }

    Type *Parser::parse_type_annotation()
    {

        // Handle reference types (&type, &mut type) - keep existing logic
        if (_current_token.is(TokenKind::TK_AMP))
        {
            advance(); // consume '&'

            // Check for 'mut' after '&'
            bool is_mutable = false;
            if (_current_token.is(TokenKind::TK_KW_MUT))
            {
                is_mutable = true;
                advance(); // consume 'mut'
            }

            Type *base_type = parse_type_annotation(); // recursive call

            // For now, create reference type (could be enhanced later for mut references)
            return _context.types().create_reference_type(base_type);
        }

        // Use the new comprehensive token-based parsing system
        return parse_type_annotation_with_tokens();
    }

    // Namespace parsing
    std::string Parser::parse_namespace()
    {
        consume(TokenKind::TK_KW_NAMESPACE, "Expected 'namespace'");

        std::string namespace_name;

        // Parse namespace identifier (can be scoped like Std::Runtime)
        if (_current_token.is(TokenKind::TK_IDENTIFIER))
        {
            namespace_name = std::string(_current_token.text());
            advance();

            // Support :: separator for C++ style namespace scoping
            while (_current_token.is(TokenKind::TK_COLONCOLON))
            {
                advance(); // consume '::'
                if (_current_token.is(TokenKind::TK_IDENTIFIER))
                {
                    namespace_name += "::" + std::string(_current_token.text());
                    advance();
                }
                else
                {
                    error("Expected identifier after '::'");
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
        // First, check for and parse any leading directives
        std::vector<std::unique_ptr<DirectiveNode>> leading_directives = parse_leading_directives();

        // Now parse the actual statement
        std::unique_ptr<ASTNode> statement = nullptr;

        // Variable declarations
        if (_current_token.is(TokenKind::TK_KW_CONST) || _current_token.is(TokenKind::TK_KW_MUT))
        {
            statement = parse_variable_declaration();
        }
        // Import declarations
        else if (_current_token.is(TokenKind::TK_KW_IMPORT))
        {
            statement = parse_import_declaration();
        }

        // Type declarations - struct, class, type alias, enum
        else if (_current_token.is(TokenKind::TK_KW_TYPE))
        {
            // Look ahead to see if it's "type struct", "type class", or "type alias"
            Token next = peek_next();
            if (next.is(TokenKind::TK_KW_STRUCT))
            {
                statement = parse_struct_declaration();
            }
            else if (next.is(TokenKind::TK_KW_CLASS))
            {
                statement = parse_class_declaration();
            }
            else if (next.is(TokenKind::TK_KW_ENUM))
            {
                statement = parse_enum_declaration();
            }
            else
            {
                statement = parse_type_alias_declaration();
            }
        }
        // Standalone enum declaration
        else if (_current_token.is(TokenKind::TK_KW_ENUM))
        {
            statement = parse_enum_declaration();
        }
        // Class declaration
        else if (_current_token.is(TokenKind::TK_KW_CLASS))
        {
            statement = parse_class_declaration();
        }
        // Trait declaration
        else if (_current_token.is(TokenKind::TK_KW_TRAIT))
        {
            statement = parse_trait_declaration();
        }
        // Implementation block
        else if (_current_token.is(TokenKind::TK_KW_IMPLEMENT))
        {
            statement = parse_implementation_block();
        }

        // Extern block or extern function
        else if (_current_token.is(TokenKind::TK_KW_EXTERN))
        {
            // Look ahead to see if this is "extern function" or "extern block"
            if (peek_next().is(TokenKind::TK_KW_FUNCTION))
            {
                // This is "extern function ..." syntax
                advance(); // consume 'extern'
                statement = parse_extern_function_declaration();
            }
            else
            {
                // This is "extern \"C\" { ... }" syntax
                statement = parse_extern_block();
            }
        }
        // Intrinsic function declarations
        else if (_current_token.is(TokenKind::TK_KW_INTRINSIC))
        {
            statement = parse_intrinsic_declaration();
        }
        // Function declarations
        else if (_current_token.is(TokenKind::TK_KW_FUNCTION) || is_visibility_modifier())
        {
            statement = parse_function_declaration();
        }
        // Control flow statements
        else if (_current_token.is(TokenKind::TK_KW_IF))
        {
            statement = parse_if_statement();
        }
        else if (_current_token.is(TokenKind::TK_KW_WHILE))
        {
            statement = parse_while_statement();
        }
        else if (_current_token.is(TokenKind::TK_KW_FOR))
        {
            statement = parse_for_statement();
        }
        else if (_current_token.is(TokenKind::TK_KW_MATCH))
        {
            statement = parse_match_statement();
        }
        else if (_current_token.is(TokenKind::TK_KW_SWITCH))
        {
            statement = parse_switch_statement();
        }
        else if (_current_token.is(TokenKind::TK_KW_RETURN))
        {
            statement = parse_return_statement();
        }
        else if (_current_token.is(TokenKind::TK_KW_BREAK))
        {
            statement = parse_break_statement();
        }
        else if (_current_token.is(TokenKind::TK_KW_CONTINUE))
        {
            statement = parse_continue_statement();
        }
        else if (_current_token.is(TokenKind::TK_L_BRACE))
        {
            statement = parse_block_statement();
        }
        else
        {
            // Expression statements
            statement = parse_expression_statement();
        }

        // Attach any directives that were parsed
        if (statement && !leading_directives.empty())
        {
            for (auto &directive : leading_directives)
            {
                statement->attach_directive(std::move(directive));
            }
        }

        return statement;
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

        // Determine if this is a global variable based on current scope
        bool is_global = is_global_scope();

        auto var_decl = _builder.create_variable_declaration(start_loc, var_name, var_type, std::move(initializer), is_mutable, is_global);

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

        // Runtime function name transformation: main -> _user_main_
        // Only transform if this is not a stdlib module (no "std::" in current namespace)
        if (func_name == "main" && _current_namespace.find("std::") == std::string::npos)
        {
            func_name = "_user_main_";
            if (_diagnostic_manager)
            {
                // Optional: Report the transformation for debugging
                // std::cout << "[DEBUG] Transformed main() -> _user_main_()" << std::endl;
            }
        }

        // Create function declaration early so we can add generic parameters
        Type *void_type = resolve_type_from_string("void");
        auto func_decl = _builder.create_function_declaration(start_loc, func_name, void_type, is_public);

        // Attach documentation if available
        attach_documentation(func_decl.get());

        // Parse optional generic parameters
        if (_current_token.is(TokenKind::TK_L_ANGLE))
        {
            auto generics = parse_generic_parameters();
            for (auto &generic : generics)
            {
                func_decl->add_generic_parameter(std::move(generic));
            }
        }

        // Parse parameter list first to get parameters
        consume(TokenKind::TK_L_PAREN, "Expected '(' after function name");

        std::vector<std::unique_ptr<VariableDeclarationNode>> params;
        bool is_variadic = false;
        if (!_current_token.is(TokenKind::TK_R_PAREN))
        {
            auto param_result = parse_parameter_list();
            params = std::move(param_result.first);
            is_variadic = param_result.second;
        }

        consume(TokenKind::TK_R_PAREN, "Expected ')' after parameters");

        // Parse return type
        Type *return_type = _context.types().get_void_type();
        if (_current_token.is(TokenKind::TK_ARROW))
        {
            advance(); // consume '->'
            return_type = parse_type_annotation();
        }

        // Update return type in the already created function declaration
        func_decl->set_resolved_return_type(return_type);

        // Parse optional where clause
        if (_current_token.is(TokenKind::TK_KW_WHERE))
        {
            parse_where_clause(func_decl.get());
        }

        // Set variadic flag if detected
        func_decl->set_variadic(is_variadic);

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

    std::unique_ptr<FunctionDeclarationNode> Parser::parse_extern_function_declaration()
    {
        SourceLocation start_loc = _current_token.location();

        consume(TokenKind::TK_KW_FUNCTION, "Expected 'function'");

        // Parse function name
        Token name_token = consume(TokenKind::TK_IDENTIFIER, "Expected function name");
        std::string func_name = std::string(name_token.text());

        // Parse parameter list
        consume(TokenKind::TK_L_PAREN, "Expected '(' after function name");

        std::vector<std::unique_ptr<VariableDeclarationNode>> params;
        bool is_variadic = false;
        if (!_current_token.is(TokenKind::TK_R_PAREN))
        {
            auto param_result = parse_parameter_list();
            params = std::move(param_result.first);
            is_variadic = param_result.second;
        }

        consume(TokenKind::TK_R_PAREN, "Expected ')' after parameters");

        // Parse return type
        Type *return_type = _context.types().get_void_type();
        if (_current_token.is(TokenKind::TK_ARROW))
        {
            advance(); // consume '->'
            return_type = parse_type_annotation();
        }

        // Create function declaration with type information (extern functions are public by default)
        auto func_decl = _builder.create_function_declaration(start_loc, func_name, return_type, true);

        // Attach documentation if available
        attach_documentation(func_decl.get());

        // Set variadic flag if detected
        func_decl->set_variadic(is_variadic);

        // Add parameters to function
        for (auto &param : params)
        {
            func_decl->add_parameter(std::move(param));
        }

        // Extern functions don't have bodies, they end with semicolon
        consume(TokenKind::TK_SEMICOLON, "Expected ';' after extern function declaration");

        return func_decl;
    }

    void Parser::parse_where_clause(FunctionDeclarationNode *func_decl)
    {
        // Consume 'where' keyword
        consume(TokenKind::TK_KW_WHERE, "Expected 'where'");

        // Parse trait bounds separated by commas
        do
        {
            // Parse type parameter (e.g., "T")
            Token type_param_token = consume(TokenKind::TK_IDENTIFIER, "Expected type parameter");
            std::string type_param = std::string(type_param_token.text());

            // Consume ':'
            consume(TokenKind::TK_COLON, "Expected ':' after type parameter");

            // Parse trait name (e.g., "Default")
            Token trait_token = consume(TokenKind::TK_IDENTIFIER, "Expected trait name");
            std::string trait_name = std::string(trait_token.text());

            // Add the trait bound to the function
            func_decl->add_trait_bound(TraitBound(type_param, trait_name, type_param_token.location()));

            // If there's a comma, continue parsing more bounds
            if (_current_token.is(TokenKind::TK_COMMA))
            {
                advance(); // consume comma
            }
            else
            {
                break;
            }
        } while (true);
    }

    std::unique_ptr<IntrinsicDeclarationNode> Parser::parse_intrinsic_declaration()
    {
        SourceLocation start_loc = _current_token.location();

        // Parse 'intrinsic' keyword
        consume(TokenKind::TK_KW_INTRINSIC, "Expected 'intrinsic'");

        // Parse 'function' keyword
        consume(TokenKind::TK_KW_FUNCTION, "Expected 'function' after 'intrinsic'");

        // Parse function name
        Token name_token = consume(TokenKind::TK_IDENTIFIER, "Expected function name");
        std::string func_name = std::string(name_token.text());

        // Parse parameter list
        consume(TokenKind::TK_L_PAREN, "Expected '(' after function name");

        std::vector<std::unique_ptr<VariableDeclarationNode>> params;
        bool is_variadic = false;
        if (!_current_token.is(TokenKind::TK_R_PAREN))
        {
            auto param_result = parse_parameter_list();
            params = std::move(param_result.first);
            is_variadic = param_result.second;
        }

        consume(TokenKind::TK_R_PAREN, "Expected ')' after parameters");

        // Parse return type
        Type *return_type = _context.types().get_void_type();
        if (_current_token.is(TokenKind::TK_ARROW))
        {
            advance(); // consume '->'
            return_type = parse_type_annotation();
        }

        // Create intrinsic declaration
        auto intrinsic_decl = std::make_unique<IntrinsicDeclarationNode>(start_loc, func_name, return_type);

        // Set variadic flag if detected (though intrinsics might not need this)
        // intrinsic_decl->set_variadic(is_variadic);  // Commented out unless IntrinsicDeclarationNode supports it

        // Add parameters to intrinsic
        for (auto &param : params)
        {
            intrinsic_decl->add_parameter(std::move(param));
        }

        // Parse optional implementation body for compiler hints
        if (_current_token.is(TokenKind::TK_L_BRACE))
        {
            auto body = parse_block_statement();
            intrinsic_decl->set_body(std::unique_ptr<BlockStatementNode>(
                dynamic_cast<BlockStatementNode *>(body.release())));
        }
        else
        {
            // No body, just end with semicolon
            consume(TokenKind::TK_SEMICOLON, "Expected ';' after intrinsic function declaration");
        }

        return intrinsic_decl;
    }

    std::unique_ptr<ImportDeclarationNode> Parser::parse_import_declaration()
    {
        SourceLocation start_loc = _current_token.location();

        // Parse 'import' keyword
        consume(TokenKind::TK_KW_IMPORT, "Expected 'import'");

        // Check for different import patterns:
        // 1. import * from <path>;            (wildcard with explicit from)
        // 2. import IO from <path>;           (specific import with from)
        // 3. import IO, Function from <path>; (multiple specific imports)
        // 4. import <path>;                   (traditional wildcard)
        // 5. import <path> as alias;          (traditional with alias)

        std::vector<std::string> specific_imports;
        std::string module_path;
        std::string alias;
        ImportDeclarationNode::ImportType import_type;
        bool has_alias = false;
        bool using_from_syntax = false;

        // Check if we have a wildcard (*) or specific imports before 'from'
        if (_current_token.is(TokenKind::TK_STAR))
        {
            // import * from <path>;
            advance(); // consume '*'

            if (!_current_token.is(TokenKind::TK_KW_FROM))
            {
                throw ParseError("Expected 'from' after '*' in import statement", _current_token.location());
            }

            advance(); // consume 'from'
            using_from_syntax = true;
            // This is still a wildcard import, just with explicit 'from' syntax
        }
        else if (_current_token.is(TokenKind::TK_IDENTIFIER))
        {
            // Could be: import IO from <path>; or import IO, Function from <path>; or import <path>;
            // We need to look ahead to see if there's a 'from' keyword

            // Parse the first identifier
            std::string first_identifier = std::string(_current_token.text());
            advance(); // consume identifier

            // Check if we have a comma (multiple imports) or 'from' (specific import) or something else (traditional import path)
            if (_current_token.is(TokenKind::TK_COMMA) || _current_token.is(TokenKind::TK_KW_FROM))
            {
                // This is specific import syntax: import IO from <path>; or import IO, Function from <path>;
                specific_imports.push_back(first_identifier);

                // Parse additional imports if comma-separated
                while (_current_token.is(TokenKind::TK_COMMA))
                {
                    advance(); // consume ','
                    Token import_name = consume(TokenKind::TK_IDENTIFIER, "Expected identifier after ',' in import list");
                    specific_imports.push_back(std::string(import_name.text()));
                }

                // Now we should have 'from'
                consume(TokenKind::TK_KW_FROM, "Expected 'from' after import list");
                using_from_syntax = true;
            }
            else
            {
                // This might be traditional import syntax where the identifier is part of the path
                // We need to backtrack and parse this as a traditional import path
                // For now, let's handle this as an error since identifiers without <> or "" are ambiguous
                throw ParseError("Ambiguous import syntax. Use 'import <path>' for stdlib or 'import \"path\"' for relative imports, or 'import Symbol from <path>' for specific imports", _current_token.location());
            }
        }

        // Now parse the module path (required for all import types)
        if (_current_token.is(TokenKind::TK_STRING_LITERAL))
        {
            // String literal import for relative files (e.g., import "./relative/path.cryo")
            module_path = std::string(_current_token.text());
            // Remove quotes
            if (module_path.size() >= 2 && module_path.front() == '"' && module_path.back() == '"')
            {
                module_path = module_path.substr(1, module_path.size() - 2);
            }

            import_type = ImportDeclarationNode::ImportType::Relative;
            advance(); // consume string literal
        }
        else if (_current_token.is(TokenKind::TK_L_ANGLE))
        {
            // Angle bracket import for standard library (e.g., import <core/intrinsics>)
            advance(); // consume '<'

            // Build the path from identifiers and forward slashes
            if (!_current_token.is(TokenKind::TK_IDENTIFIER))
            {
                throw ParseError("Expected module path after '<' in import statement", _current_token.location());
            }

            module_path = std::string(_current_token.text());
            advance(); // consume first identifier

            // Handle path segments separated by '/'
            while (_current_token.is(TokenKind::TK_SLASH))
            {
                advance(); // consume '/'
                Token ident = consume(TokenKind::TK_IDENTIFIER, "Expected identifier after '/' in import path");
                module_path += "/" + std::string(ident.text());
            }

            consume(TokenKind::TK_R_ANGLE, "Expected '>' to close standard library import");

            import_type = ImportDeclarationNode::ImportType::Absolute;
        }
        else
        {
            throw ParseError("Expected import path: use \"./relative/path.cryo\" for relative imports or <core/module> for standard library", _current_token.location());
        }

        // Parse optional 'as' alias (only for traditional wildcard imports)
        if (_current_token.is(TokenKind::TK_KW_AS) && !using_from_syntax)
        {
            advance(); // consume 'as'
            Token alias_token = consume(TokenKind::TK_IDENTIFIER, "Expected identifier after 'as'");
            alias = std::string(alias_token.text());
            has_alias = true;
        }

        // Parse optional ';' or newline
        if (_current_token.is(TokenKind::TK_SEMICOLON))
        {
            advance(); // consume ';'
        }

        // Create the appropriate import node based on the syntax used
        if (!specific_imports.empty())
        {
            // Specific import: import IO from <path>;
            return std::make_unique<ImportDeclarationNode>(start_loc, std::move(specific_imports), module_path, import_type);
        }
        else if (has_alias)
        {
            // Traditional import with alias: import <path> as alias;
            return std::make_unique<ImportDeclarationNode>(start_loc, module_path, alias, import_type);
        }
        else
        {
            // Wildcard import: import <path>; or import * from <path>;
            return std::make_unique<ImportDeclarationNode>(start_loc, module_path, import_type);
        }
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

        // Enter new scope for this block
        enter_scope();

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

        // Exit scope after parsing the block
        exit_scope();

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
        auto expr = parse_bitwise_or();

        while (_current_token.is(TokenKind::TK_EQUALEQUAL) || _current_token.is(TokenKind::TK_EXCLAIMEQUAL))
        {
            Token op = _current_token;
            advance();
            auto right = parse_bitwise_or();
            expr = _builder.create_binary_expression(op, std::move(expr), std::move(right));
        }

        return expr;
    }

    std::unique_ptr<ExpressionNode> Parser::parse_bitwise_or()
    {
        auto expr = parse_bitwise_xor();

        while (_current_token.is(TokenKind::TK_PIPE))
        {
            Token op = _current_token;
            advance();
            auto right = parse_bitwise_xor();
            expr = _builder.create_binary_expression(op, std::move(expr), std::move(right));
        }

        return expr;
    }

    std::unique_ptr<ExpressionNode> Parser::parse_bitwise_xor()
    {
        auto expr = parse_bitwise_and();

        while (_current_token.is(TokenKind::TK_CARET))
        {
            Token op = _current_token;
            advance();
            auto right = parse_bitwise_and();
            expr = _builder.create_binary_expression(op, std::move(expr), std::move(right));
        }

        return expr;
    }

    std::unique_ptr<ExpressionNode> Parser::parse_bitwise_and()
    {
        auto expr = parse_relational();

        while (_current_token.is(TokenKind::TK_AMP))
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
        auto expr = parse_shift();

        while (_current_token.is(TokenKind::TK_L_ANGLE) || _current_token.is(TokenKind::TK_R_ANGLE) ||
               _current_token.is(TokenKind::TK_LESSEQUAL) || _current_token.is(TokenKind::TK_GREATEREQUAL))
        {
            Token op = _current_token;
            advance();
            auto right = parse_shift();
            expr = _builder.create_binary_expression(op, std::move(expr), std::move(right));
        }

        return expr;
    }

    std::unique_ptr<ExpressionNode> Parser::parse_shift()
    {
        auto expr = parse_additive();

        while (_current_token.is(TokenKind::TK_LESSLESS) || _current_token.is(TokenKind::TK_GREATERGREATER))
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
        if (_current_token.is(TokenKind::TK_MINUS) || _current_token.is(TokenKind::TK_EXCLAIM) ||
            _current_token.is(TokenKind::TK_AMP) || _current_token.is(TokenKind::TK_STAR) ||
            _current_token.is(TokenKind::TK_TILDE) ||
            _current_token.is(TokenKind::TK_PLUSPLUS) || _current_token.is(TokenKind::TK_MINUSMINUS))
        {
            Token op = _current_token;
            advance();
            auto expr = parse_unary();

            // Create proper unary expression node
            return _builder.create_unary_expression(op, std::move(expr));
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
            std::unique_ptr<ExpressionNode> expr = parse_string_literal();

            // Handle postfix expressions on string literals (like "hello".length())
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
                else if (_current_token.is(TokenKind::TK_PERIOD))
                {
                    // Member access
                    expr = parse_member_access(std::move(expr));
                }
                else if (_current_token.is(TokenKind::TK_PLUSPLUS) || _current_token.is(TokenKind::TK_MINUSMINUS))
                {
                    // Postfix increment/decrement
                    Token op = _current_token;
                    advance();
                    expr = _builder.create_unary_expression(op, std::move(expr));
                }
                else
                {
                    // No more postfix operations
                    break;
                }
            }

            return expr;
        }

        if (_current_token.is(TokenKind::TK_CHAR_CONSTANT))
        {
            return parse_character_literal();
        }

        if (_current_token.is(TokenKind::TK_KW_TRUE) || _current_token.is(TokenKind::TK_KW_FALSE))
        {
            return parse_boolean_literal();
        }

        if (_current_token.is(TokenKind::TK_KW_NULL))
        {
            return parse_null_literal();
        }

        // 'this' keyword - temporarily disabled while fixing implementation
        if (_current_token.is(TokenKind::TK_KW_THIS))
        {
            Token this_token = _current_token;
            advance(); // consume 'this'

            // Create a new token with TK_IDENTIFIER kind but same text/location
            Token this_identifier_token(TokenKind::TK_IDENTIFIER,
                                        this_token.text(),
                                        this_token.location());

            auto this_expr = _builder.create_identifier_node(this_identifier_token);

            // Handle postfix expressions on 'this' (like this.member)
            std::unique_ptr<ExpressionNode> expr = std::move(this_expr);

            // Chain multiple postfix operations
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
                else if (_current_token.is(TokenKind::TK_PERIOD))
                {
                    // Member access
                    expr = parse_member_access(std::move(expr));
                }
                else
                {
                    // No more postfix operations
                    break;
                }
            }

            return std::move(expr);
        }

        // Primitive type constructors (e.g., i64(value), i32(value), f64(value))
        if (is_primitive_type_token())
        {
            // Look ahead to see if this is followed by parentheses
            if (peek_next().is(TokenKind::TK_L_PAREN))
            {
                // This is a primitive constructor call like i64(value) or f64(value)
                std::string type_name = std::string(_current_token.text());
                SourceLocation type_location = _current_token.location();
                advance(); // consume the type name (e.g., 'i64', 'f64')

                // Create an identifier node with the type name
                Token type_token(TokenKind::TK_IDENTIFIER, type_name, type_location);
                auto type_identifier = _builder.create_identifier_node(type_token);

                // Parse as a function call
                auto constructor_call = parse_call_expression(std::move(type_identifier));

                return constructor_call;
            }
        }

        // Identifiers (can be function calls, variable references, or scope resolution)
        if (_current_token.is(TokenKind::TK_IDENTIFIER))
        {
            std::unique_ptr<ExpressionNode> expr = parse_identifier();

            // Check if this might be a generic type constructor like Pair<int, string>
            if (_current_token.is(TokenKind::TK_L_ANGLE))
            {
                // Look ahead to see if this is followed by parentheses (constructor call)
                // We need to peek ahead to see if there are parentheses after the generic args
                if (auto identifier_node = dynamic_cast<IdentifierNode *>(expr.get()))
                {
                    // Use a simple heuristic: only try to parse generics if the identifier starts with uppercase
                    // This avoids conflicts with comparison operators in most cases
                    std::string type_name = identifier_node->name();
                    SourceLocation type_location = identifier_node->location();

                    if (!type_name.empty() && std::isupper(type_name[0]))
                    {
                        // Parse generic arguments
                        advance(); // consume '<'
                        std::vector<std::string> generic_args;

                        do
                        {
                            if (!_current_token.is(TokenKind::TK_IDENTIFIER) && !is_type_token())
                            {
                                error("Expected type name in generic arguments");
                                return nullptr;
                            }

                            generic_args.push_back(std::string(_current_token.text()));
                            advance(); // consume type argument

                        } while (match(TokenKind::TK_COMMA));

                        consume(TokenKind::TK_R_ANGLE, "Expected '>' after generic arguments");

                        // Check if this is followed by parentheses (constructor call) or scope resolution
                        if (_current_token.is(TokenKind::TK_L_PAREN))
                        {
                            // Parse the constructor call with arguments
                            advance(); // consume '('

                            // Check if this is a struct literal ({field: value})
                            if (_current_token.is(TokenKind::TK_L_BRACE))
                            {
                                // This is a struct literal
                                advance(); // consume '{'

                                auto struct_literal = _builder.create_struct_literal(type_location, type_name);

                                // Add generic arguments to struct literal
                                for (const auto &generic_arg : generic_args)
                                {
                                    struct_literal->add_generic_arg(generic_arg);
                                }

                                // Parse field initializers
                                if (!_current_token.is(TokenKind::TK_R_BRACE))
                                {
                                    do
                                    {
                                        // Parse field name
                                        if (!_current_token.is(TokenKind::TK_IDENTIFIER))
                                        {
                                            error("Expected field name in struct literal");
                                            return nullptr;
                                        }

                                        std::string field_name = std::string(_current_token.text());
                                        advance(); // consume field name

                                        consume(TokenKind::TK_COLON, "Expected ':' after field name");

                                        // Parse field value
                                        auto field_value = parse_expression();
                                        if (!field_value)
                                        {
                                            error("Expected expression for field value");
                                            return nullptr;
                                        }

                                        // Create field initializer
                                        auto field_init = std::make_unique<FieldInitializerNode>(field_name, std::move(field_value));
                                        struct_literal->add_field_initializer(std::move(field_init));

                                    } while (match(TokenKind::TK_COMMA));
                                }

                                consume(TokenKind::TK_R_BRACE, "Expected '}' after struct literal fields");
                                consume(TokenKind::TK_R_PAREN, "Expected ')' after struct literal");

                                expr = std::move(struct_literal);
                            }
                            else
                            {
                                // Regular constructor call - create NewExpressionNode and parse arguments
                                auto new_expr = _builder.create_new_expression(type_location, type_name);

                                // Add generic arguments
                                for (const auto &generic_arg : generic_args)
                                {
                                    new_expr->add_generic_arg(generic_arg);
                                }

                                // Parse constructor arguments (if any)
                                if (!_current_token.is(TokenKind::TK_R_PAREN))
                                {
                                    do
                                    {
                                        auto arg = parse_expression();
                                        if (!arg)
                                        {
                                            error("Expected expression in constructor arguments");
                                            return nullptr;
                                        }
                                        new_expr->add_argument(std::move(arg));

                                    } while (match(TokenKind::TK_COMMA));
                                }

                                consume(TokenKind::TK_R_PAREN, "Expected ')' after constructor arguments");
                                expr = std::move(new_expr);
                            }
                        }
                        else if (_current_token.is(TokenKind::TK_COLONCOLON))
                        {
                            // Generic type with scope resolution: Option<T>::None
                            // Create a generic type identifier that can be used in scope resolution
                            std::string generic_type_name = type_name + "<";
                            for (size_t i = 0; i < generic_args.size(); ++i)
                            {
                                if (i > 0)
                                    generic_type_name += ",";
                                generic_type_name += generic_args[i];
                            }
                            generic_type_name += ">";

                            // Create identifier with generic type name for scope resolution
                            Token generic_token(TokenKind::TK_IDENTIFIER, generic_type_name, type_location);
                            expr = _builder.create_identifier_node(generic_token);
                        }
                        else
                        {
                            // Not a constructor call or scope resolution, this is an error for now
                            error("Generic type expressions must be followed by constructor call '()' or scope resolution '::'");
                            return nullptr;
                        }
                    }
                }
            }

            // Handle multi-level scope resolution (e.g., Std::Runtime::function)
            while (_current_token.is(TokenKind::TK_COLONCOLON))
            {
                advance(); // consume '::'

                if (!_current_token.is(TokenKind::TK_IDENTIFIER))
                {
                    error("Expected identifier after '::'");
                    return nullptr;
                }

                std::string member_name = std::string(_current_token.text());
                SourceLocation loc;

                // If expr is already a ScopeResolutionNode, build on it
                if (auto scope_node = dynamic_cast<ScopeResolutionNode *>(expr.get()))
                {
                    // For multi-level resolution like Std::Runtime::function
                    // Combine the existing scope with the new member
                    std::string full_scope = scope_node->scope_name() + "::" + scope_node->member_name();
                    loc = scope_node->location();
                    expr = _builder.create_scope_resolution(loc, full_scope, member_name);
                }
                else if (auto identifier_node = dynamic_cast<IdentifierNode *>(expr.get()))
                {
                    // First level scope resolution like Std::Runtime
                    std::string scope_name = identifier_node->name();
                    loc = identifier_node->location();
                    expr = _builder.create_scope_resolution(loc, scope_name, member_name);
                }
                else
                {
                    error("Invalid scope resolution expression");
                    return nullptr;
                }

                advance(); // consume member identifier
            }

            // Handle postfix expressions (function calls, array access, etc.)
            // expr is already the right type, so we can continue with postfix operations

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
                else if (_current_token.is(TokenKind::TK_PERIOD))
                {
                    // Member access
                    expr = parse_member_access(std::move(expr));
                }
                else if (_current_token.is(TokenKind::TK_PLUSPLUS) || _current_token.is(TokenKind::TK_MINUSMINUS))
                {
                    // Postfix increment/decrement
                    Token op = _current_token;
                    advance();
                    expr = _builder.create_unary_expression(op, std::move(expr));
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

        // New expressions (constructor calls)
        if (_current_token.is(TokenKind::TK_KW_NEW))
        {
            return parse_new_expression();
        }

        // Sizeof expressions
        if (_current_token.is(TokenKind::TK_KW_SIZEOF))
        {
            return parse_sizeof_expression();
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

    std::unique_ptr<LiteralNode> Parser::parse_null_literal()
    {
        Token token = consume(TokenKind::TK_KW_NULL, "Expected null");
        return _builder.create_literal_node(token);
    }

    std::unique_ptr<ExpressionNode> Parser::parse_identifier()
    {
        Token token = consume(TokenKind::TK_IDENTIFIER, "Expected identifier");
        std::string base_name = std::string(token.text());
        SourceLocation base_loc = token.location();

        // Check for scope resolution (e.g., T::get_default)
        if (_current_token.is(TokenKind::TK_COLONCOLON))
        {
            advance(); // consume '::'

            if (!_current_token.is(TokenKind::TK_IDENTIFIER))
            {
                error("Expected identifier after '::'");
                return _builder.create_identifier_node(token);
            }

            Token member_token = consume(TokenKind::TK_IDENTIFIER, "Expected identifier after '::'");
            std::string member_name = std::string(member_token.text());

            return _builder.create_scope_resolution(base_loc, base_name, member_name);
        }

        // Simple identifier without scope resolution
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
               _current_token.is(TokenKind::TK_KW_THIS_TYPE) || // Support for 'This'
               _current_token.is(TokenKind::TK_IDENTIFIER);     // For user-defined types
    }

    bool Parser::is_primitive_type_token() const
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
               _current_token.is(TokenKind::TK_KW_VOID);
    }

    bool Parser::is_integer_primitive_type_token() const
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
               _current_token.is(TokenKind::TK_KW_UINT64);
    }

    bool Parser::is_visibility_modifier() const
    {
        return _current_token.is(TokenKind::TK_KW_PUBLIC) ||
               _current_token.is(TokenKind::TK_KW_PRIVATE) ||
               _current_token.is(TokenKind::TK_KW_PROTECTED);
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

    std::unique_ptr<ASTNode> Parser::parse_match_statement()
    {
        SourceLocation start_loc = _current_token.location();
        consume(TokenKind::TK_KW_MATCH, "Expected 'match'");

        // Parse the expression to match on
        auto expr = parse_expression();

        consume(TokenKind::TK_L_BRACE, "Expected '{' after match expression");

        // Create the match statement
        auto match_stmt = std::make_unique<MatchStatementNode>(start_loc, std::move(expr));

        // Parse match arms
        while (!_current_token.is(TokenKind::TK_R_BRACE) && !is_at_end())
        {
            auto arm = parse_match_arm();
            if (arm)
            {
                match_stmt->add_arm(std::move(arm));
            }
        }

        consume(TokenKind::TK_R_BRACE, "Expected '}' after match arms");

        return std::move(match_stmt);
    }

    std::unique_ptr<ASTNode> Parser::parse_switch_statement()
    {
        SourceLocation start_loc = _current_token.location();
        consume(TokenKind::TK_KW_SWITCH, "Expected 'switch'");

        consume(TokenKind::TK_L_PAREN, "Expected '(' after switch");
        auto expr = parse_expression();
        consume(TokenKind::TK_R_PAREN, "Expected ')' after switch expression");

        consume(TokenKind::TK_L_BRACE, "Expected '{' after switch expression");

        std::vector<std::unique_ptr<CaseStatementNode>> cases;

        // Parse case statements
        while (!_current_token.is(TokenKind::TK_R_BRACE) && !is_at_end())
        {
            auto case_stmt = parse_case_statement();
            if (case_stmt)
            {
                cases.push_back(std::move(case_stmt));
            }
        }

        consume(TokenKind::TK_R_BRACE, "Expected '}' after switch cases");

        return std::make_unique<SwitchStatementNode>(start_loc, std::move(expr), std::move(cases));
    }

    std::unique_ptr<CaseStatementNode> Parser::parse_case_statement()
    {
        SourceLocation start_loc = _current_token.location();
        std::unique_ptr<ExpressionNode> value = nullptr;

        if (_current_token.is(TokenKind::TK_KW_CASE))
        {
            advance(); // consume 'case'
            value = parse_expression();
            consume(TokenKind::TK_COLON, "Expected ':' after case value");
        }
        else if (_current_token.is(TokenKind::TK_KW_DEFAULT))
        {
            advance(); // consume 'default'
            consume(TokenKind::TK_COLON, "Expected ':' after default");
            // value remains nullptr for default case
        }
        else
        {
            error("Expected 'case' or 'default'");
            return nullptr;
        }

        // Parse statements until we hit another case, default, or closing brace
        std::vector<std::unique_ptr<StatementNode>> statements;
        while (!_current_token.is(TokenKind::TK_KW_CASE) &&
               !_current_token.is(TokenKind::TK_KW_DEFAULT) &&
               !_current_token.is(TokenKind::TK_R_BRACE) &&
               !is_at_end())
        {
            auto stmt = parse_statement();
            if (auto statement_node = dynamic_cast<StatementNode *>(stmt.release()))
            {
                statements.push_back(std::unique_ptr<StatementNode>(statement_node));
            }
        }

        return std::make_unique<CaseStatementNode>(start_loc, std::move(value), std::move(statements));
    }

    std::unique_ptr<MatchArmNode> Parser::parse_match_arm()
    {
        SourceLocation start_loc = _current_token.location();

        // Parse the pattern
        auto pattern = parse_pattern();

        consume(TokenKind::TK_FATARROW, "Expected '=>' after pattern");

        // Parse the body - can be either a block statement or a single expression
        std::unique_ptr<StatementNode> body;

        if (_current_token.is(TokenKind::TK_L_BRACE))
        {
            // Block statement: { ... }
            body = parse_block_statement();
        }
        else if (_current_token.is(TokenKind::TK_KW_RETURN))
        {
            // Special handling for return statements in match arms - no semicolon expected
            SourceLocation return_loc = _current_token.location();
            consume(TokenKind::TK_KW_RETURN, "Expected 'return'");

            std::unique_ptr<ExpressionNode> expr = nullptr;
            if (!_current_token.is(TokenKind::TK_COMMA) && !_current_token.is(TokenKind::TK_R_BRACE))
            {
                expr = parse_expression();
            }

            body = _builder.create_return_statement(return_loc, std::move(expr));
        }
        else
        {
            // Expression statement
            auto expr = parse_expression();
            body = std::make_unique<ExpressionStatementNode>(start_loc, std::move(expr));
        }

        // Expect comma after match arm (optional for last arm)
        if (_current_token.is(TokenKind::TK_COMMA))
        {
            advance();
        }

        return std::make_unique<MatchArmNode>(start_loc, std::move(pattern), std::move(body));
    }

    std::unique_ptr<PatternNode> Parser::parse_pattern()
    {
        SourceLocation start_loc = _current_token.location();

        // For now, only handle enum patterns like Shape::Circle(radius)
        if (_current_token.is(TokenKind::TK_IDENTIFIER))
        {
            std::string enum_name{_current_token.text()};
            advance();

            if (_current_token.is(TokenKind::TK_COLONCOLON))
            {
                advance();

                if (_current_token.is(TokenKind::TK_IDENTIFIER))
                {
                    std::string variant_name{_current_token.text()};
                    advance();

                    auto pattern = std::make_unique<EnumPatternNode>(start_loc, enum_name, variant_name);

                    // Parse bound variables if present
                    if (_current_token.is(TokenKind::TK_L_PAREN))
                    {
                        advance();

                        while (!_current_token.is(TokenKind::TK_R_PAREN) && !is_at_end())
                        {
                            if (_current_token.is(TokenKind::TK_IDENTIFIER))
                            {
                                pattern->add_bound_variable(std::string{_current_token.text()});
                                advance();

                                if (_current_token.is(TokenKind::TK_COMMA))
                                {
                                    advance();
                                }
                            }
                            else
                            {
                                error("Expected identifier in pattern");
                                break;
                            }
                        }

                        consume(TokenKind::TK_R_PAREN, "Expected ')' after pattern variables");
                    }

                    return std::move(pattern);
                }
            }
        }

        error("Expected enum pattern");
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

    std::pair<std::vector<std::unique_ptr<VariableDeclarationNode>>, bool> Parser::parse_parameter_list()
    {
        std::vector<std::unique_ptr<VariableDeclarationNode>> params;
        bool is_variadic = false;

        // Check if the first parameter is variadic
        if (peek_variadic_parameter())
        {
            params.push_back(parse_variadic_parameter());
            is_variadic = true;
        }
        else
        {
            params.push_back(parse_parameter());
        }

        while (_current_token.is(TokenKind::TK_COMMA))
        {
            advance(); // consume ','

            // Check if next parameter is variadic (ends with ...)
            if (peek_variadic_parameter())
            {
                params.push_back(parse_variadic_parameter());
                is_variadic = true;
                break; // Variadic parameter must be last
            }
            else
            {
                params.push_back(parse_parameter());
            }
        }

        return std::make_pair(std::move(params), is_variadic);
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
        return _builder.create_variable_declaration(name_token.location(), param_name, param_type);
    }

    bool Parser::peek_variadic_parameter()
    {
        // Check if we have an identifier followed by ellipsis: "args..."
        if (_current_token.is(TokenKind::TK_IDENTIFIER))
        {
            // Look ahead to see if there's an ellipsis after the identifier
            Token next = peek_next();
            return next.is(TokenKind::TK_ELLIPSIS);
        }
        return false;
    }

    std::unique_ptr<VariableDeclarationNode> Parser::parse_variadic_parameter()
    {
        // Parse parameter name
        Token name_token = consume(TokenKind::TK_IDENTIFIER, "Expected parameter name");
        std::string param_name = std::string(name_token.text());

        // Consume the ellipsis
        consume(TokenKind::TK_ELLIPSIS, "Expected '...' for variadic parameter");

        // For variadic parameters, we'll use a special type to indicate it's variadic
        // The actual type will be handled by the codegen later
        Type *variadic_type = resolve_type_from_string("...");
        return _builder.create_variable_declaration(name_token.location(), param_name, variadic_type);
    }

    std::unique_ptr<ExpressionNode> Parser::parse_call_expression(std::unique_ptr<ExpressionNode> expr)
    {
        SourceLocation call_location = _current_token.location();
        consume(TokenKind::TK_L_PAREN, "Expected '(' to start function call");

        // Check if this could be a non-generic struct literal
        if (_current_token.is(TokenKind::TK_L_BRACE))
        {
            // Check if expr is an identifier (potential struct name)
            if (auto identifier = dynamic_cast<IdentifierNode *>(expr.get()))
            {
                // This looks like a struct literal: StructName({field: value})
                advance(); // consume '{'

                std::string struct_name = identifier->name();
                auto struct_literal = _builder.create_struct_literal(call_location, struct_name);

                // Parse field initializers
                if (!_current_token.is(TokenKind::TK_R_BRACE))
                {
                    do
                    {
                        // Parse field name
                        if (!_current_token.is(TokenKind::TK_IDENTIFIER))
                        {
                            error("Expected field name in struct literal");
                            return nullptr;
                        }

                        std::string field_name = std::string(_current_token.text());
                        advance(); // consume field name

                        consume(TokenKind::TK_COLON, "Expected ':' after field name");

                        // Parse field value
                        auto field_value = parse_expression();
                        if (!field_value)
                        {
                            error("Expected expression for field value");
                            return nullptr;
                        }

                        // Create field initializer
                        auto field_init = std::make_unique<FieldInitializerNode>(field_name, std::move(field_value));
                        struct_literal->add_field_initializer(std::move(field_init));

                    } while (match(TokenKind::TK_COMMA));
                }

                consume(TokenKind::TK_R_BRACE, "Expected '}' after struct literal fields");
                consume(TokenKind::TK_R_PAREN, "Expected ')' after struct literal");

                return struct_literal;
            }
        }

        // Regular function call - create call expression with the callee (function name/expression)
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

    std::unique_ptr<ExpressionNode> Parser::parse_new_expression()
    {
        SourceLocation new_location = _current_token.location();
        consume(TokenKind::TK_KW_NEW, "Expected 'new' keyword");

        // Parse type name
        if (!_current_token.is(TokenKind::TK_IDENTIFIER))
        {
            error("Expected type name after 'new'");
            return nullptr;
        }

        std::string type_name = std::string(_current_token.text());
        advance(); // consume type name

        auto new_expr = _builder.create_new_expression(new_location, type_name);

        // Handle generic types like GenericStruct<string>
        if (_current_token.is(TokenKind::TK_L_ANGLE))
        {
            advance(); // consume '<'

            do
            {
                // Accept both identifiers and type keywords (like string, int, float, etc.)
                if (!_current_token.is(TokenKind::TK_IDENTIFIER) && !is_type_token())
                {
                    error("Expected type name in generic arguments");
                    return nullptr;
                }

                new_expr->add_generic_arg(std::string(_current_token.text()));
                advance(); // consume type argument

            } while (match(TokenKind::TK_COMMA));

            consume(TokenKind::TK_R_ANGLE, "Expected '>' after generic arguments");
        }

        // Parse constructor arguments
        consume(TokenKind::TK_L_PAREN, "Expected '(' after type name");

        // Check if this is a struct literal ({field: value})
        if (_current_token.is(TokenKind::TK_L_BRACE))
        {
            // This is invalid syntax: 'new' keyword cannot be used with struct literal syntax
            if (_diagnostic_builder)
            {
                _diagnostic_builder->create_invalid_instantiation_error(
                    type_name,
                    new_location,
                    "cannot use 'new' keyword with struct literal syntax");
            }

            // Error recovery: skip the invalid struct literal syntax
            // Consume the opening brace
            advance(); // consume '{'

            // Skip until we find the closing brace
            int brace_count = 1;
            while (!_current_token.is(TokenKind::TK_EOF) && brace_count > 0)
            {
                if (_current_token.is(TokenKind::TK_L_BRACE))
                {
                    brace_count++;
                }
                else if (_current_token.is(TokenKind::TK_R_BRACE))
                {
                    brace_count--;
                }
                advance();
            }

            // Consume the closing parenthesis if present
            if (_current_token.is(TokenKind::TK_R_PAREN))
            {
                advance();
            }

            // Return a valid new expression node without arguments for error recovery
            return new_expr;
        }
        else
        {
            // Regular constructor call with positional arguments
            if (!_current_token.is(TokenKind::TK_R_PAREN))
            {
                do
                {
                    // Parse positional arguments (no named parameter support)
                    auto arg = parse_expression();
                    if (arg)
                    {
                        new_expr->add_argument(std::move(arg));
                    }
                } while (match(TokenKind::TK_COMMA));
            }

            consume(TokenKind::TK_R_PAREN, "Expected ')' after constructor arguments");

            return new_expr;
        }
    }

    std::unique_ptr<ExpressionNode> Parser::parse_sizeof_expression()
    {
        SourceLocation sizeof_location = _current_token.location();
        consume(TokenKind::TK_KW_SIZEOF, "Expected 'sizeof' keyword");

        consume(TokenKind::TK_L_PAREN, "Expected '(' after 'sizeof'");

        // Parse type name
        if (!_current_token.is(TokenKind::TK_IDENTIFIER) && !is_type_token())
        {
            error("Expected type name in sizeof expression");
            return nullptr;
        }

        std::string type_name = std::string(_current_token.text());
        advance(); // consume type name

        consume(TokenKind::TK_R_PAREN, "Expected ')' after type name");

        return _builder.create_sizeof_expression(sizeof_location, type_name);
    }

    std::unique_ptr<ExpressionNode> Parser::parse_member_access(std::unique_ptr<ExpressionNode> expr)
    {
        SourceLocation access_location = _current_token.location();
        consume(TokenKind::TK_PERIOD, "Expected '.' for member access");

        if (!_current_token.is(TokenKind::TK_IDENTIFIER) && !_current_token.is_keyword())
        {
            error("Expected member name after '.'");
            return expr;
        }

        std::string member_name = std::string(_current_token.text());
        advance(); // consume member name

        // Check if this is a method call (member access followed by function call)
        if (_current_token.is(TokenKind::TK_L_PAREN))
        {
            // Create member access node first
            auto member_access = _builder.create_member_access(access_location, std::move(expr), member_name);

            // Then parse the function call with the member access as the callee
            return parse_call_expression(std::move(member_access));
        }
        else
        {
            // Simple member access (field access)
            return _builder.create_member_access(access_location, std::move(expr), member_name);
        }
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

    // Struct and Class parsing methods
    std::unique_ptr<StructDeclarationNode> Parser::parse_struct_declaration()
    {
        SourceLocation start_loc = _current_token.location();

        consume(TokenKind::TK_KW_TYPE, "Expected 'type'");
        consume(TokenKind::TK_KW_STRUCT, "Expected 'struct'");

        Token name_token = consume(TokenKind::TK_IDENTIFIER, "Expected struct name");
        std::string struct_name = std::string(name_token.text());

        auto struct_decl = _builder.create_struct_declaration(start_loc, struct_name);

        // Attach documentation if available
        attach_documentation(struct_decl.get());

        // Parse optional generic parameters
        if (_current_token.is(TokenKind::TK_L_ANGLE))
        {
            auto generics = parse_generic_parameters();
            for (auto &generic : generics)
            {
                struct_decl->add_generic_parameter(std::move(generic));
            }
        }

        consume(TokenKind::TK_L_BRACE, "Expected '{' after struct name");

        // Parse struct members (fields and methods)
        Visibility current_visibility = Visibility::Public; // Default for structs

        while (!_current_token.is(TokenKind::TK_R_BRACE) && !is_at_end())
        {
            try
            {
                // Check for visibility modifiers
                if (is_visibility_modifier())
                {
                    current_visibility = parse_visibility_modifier(); // Store the parsed visibility
                    consume(TokenKind::TK_COLON, "Expected ':' after visibility modifier");
                    continue;
                }

                // Check if it's a method or field
                Token next = peek_next();

                bool is_method = false;

                // Case 1: regular method - identifier followed by (
                if (_current_token.is(TokenKind::TK_IDENTIFIER) && next.is(TokenKind::TK_L_PAREN))
                {
                    is_method = true;
                }
                // Case 2: static method - static followed by identifier
                else if (_current_token.is(TokenKind::TK_KW_STATIC) && next.is(TokenKind::TK_IDENTIFIER))
                {
                    // For static methods, we assume it's a method if static is followed by identifier
                    // The method parsing will handle the rest
                    is_method = true;
                }
                // Case 3: destructor - ~ followed by identifier
                else if (_current_token.is(TokenKind::TK_TILDE) && next.is(TokenKind::TK_IDENTIFIER))
                {
                    is_method = true;
                }
                // Case 4: constructor with explicit constructor keyword
                else if (_current_token.is(TokenKind::TK_KW_CONSTRUCTOR))
                {
                    is_method = true;
                }

                if (is_method)
                {
                    // It's a method
                    auto method = parse_struct_method(struct_name, current_visibility);
                    struct_decl->add_method(std::move(method));
                }
                else
                {
                    // It's a field
                    auto field = parse_struct_field(current_visibility);
                    struct_decl->add_field(std::move(field));
                }
            }
            catch (const ParseError &e)
            {
                _errors.push_back(e);
                synchronize();
            }
        }

        consume(TokenKind::TK_R_BRACE, "Expected '}' after struct body");
        return struct_decl;
    }

    std::unique_ptr<ClassDeclarationNode> Parser::parse_class_declaration()
    {
        SourceLocation start_loc = _current_token.location();

        // Check if it's "type class" or just "class"
        if (_current_token.is(TokenKind::TK_KW_TYPE))
        {
            consume(TokenKind::TK_KW_TYPE, "Expected 'type'");
        }

        consume(TokenKind::TK_KW_CLASS, "Expected 'class'");

        Token name_token = consume(TokenKind::TK_IDENTIFIER, "Expected class name");
        std::string class_name = std::string(name_token.text());

        auto class_decl = _builder.create_class_declaration(start_loc, class_name);

        // Attach documentation if available
        attach_documentation(class_decl.get());

        // Parse optional generic parameters
        if (_current_token.is(TokenKind::TK_L_ANGLE))
        {
            auto generics = parse_generic_parameters();
            for (auto &generic : generics)
            {
                class_decl->add_generic_parameter(std::move(generic));
            }
        }

        // Parse optional base class
        if (_current_token.is(TokenKind::TK_COLON))
        {
            advance(); // consume ':'
            Token base_token = consume(TokenKind::TK_IDENTIFIER, "Expected base class name");
            class_decl->set_base_class(std::string(base_token.text()));
        }

        consume(TokenKind::TK_L_BRACE, "Expected '{' after class declaration");

        // Parse class members
        Visibility current_visibility = Visibility::Private; // Default for classes

        while (!_current_token.is(TokenKind::TK_R_BRACE) && !is_at_end())
        {
            try
            {
                // Check for visibility modifiers
                if (is_visibility_modifier())
                {
                    current_visibility = parse_visibility_modifier();
                    consume(TokenKind::TK_COLON, "Expected ':' after visibility modifier");
                    continue;
                }

                // Check if it's a method or field
                Token next = peek_next();

                bool is_method = false;

                // Case 1: regular method - identifier followed by (
                if (_current_token.is(TokenKind::TK_IDENTIFIER) && next.is(TokenKind::TK_L_PAREN))
                {
                    is_method = true;
                }
                // Case 2: static method - static followed by identifier
                else if (_current_token.is(TokenKind::TK_KW_STATIC) && next.is(TokenKind::TK_IDENTIFIER))
                {
                    // For static methods, we assume it's a method if static is followed by identifier
                    // The method parsing will handle the rest
                    is_method = true;
                }
                // Case 3: destructor - ~ followed by identifier
                else if (_current_token.is(TokenKind::TK_TILDE) && next.is(TokenKind::TK_IDENTIFIER))
                {
                    is_method = true;
                }
                // Case 4: constructor with explicit constructor keyword
                else if (_current_token.is(TokenKind::TK_KW_CONSTRUCTOR))
                {
                    is_method = true;
                }

                if (is_method)
                {
                    // It's a method
                    auto method = parse_struct_method(class_name, current_visibility);
                    // Note: We reuse StructMethodNode for class methods
                    class_decl->add_method(std::move(method));
                }
                else
                {
                    // It's a field
                    auto field = parse_struct_field(current_visibility);
                    class_decl->add_field(std::move(field));
                }
            }
            catch (const ParseError &e)
            {
                _errors.push_back(e);
                synchronize();
            }
        }

        consume(TokenKind::TK_R_BRACE, "Expected '}' after class body");
        return class_decl;
    }

    std::unique_ptr<TraitDeclarationNode> Parser::parse_trait_declaration()
    {
        SourceLocation start_loc = _current_token.location();

        consume(TokenKind::TK_KW_TRAIT, "Expected 'trait'");

        Token name_token = consume(TokenKind::TK_IDENTIFIER, "Expected trait name");
        std::string trait_name = std::string(name_token.text());

        auto trait_decl = _builder.create_trait_declaration(start_loc, trait_name);

        // Attach documentation if available
        attach_documentation(trait_decl.get());

        // Parse optional generic parameters
        if (_current_token.is(TokenKind::TK_L_ANGLE))
        {
            auto generics = parse_generic_parameters();
            for (auto &generic : generics)
            {
                trait_decl->add_generic_parameter(std::move(generic));
            }
        }

        // Parse optional trait inheritance (: BaseTrait<T>)
        if (_current_token.is(TokenKind::TK_COLON))
        {
            advance(); // consume ':'

            do
            {
                // Parse base trait name
                Token base_trait_name = consume(TokenKind::TK_IDENTIFIER, "Expected base trait name");
                std::string base_name = std::string(base_trait_name.text());

                std::vector<std::string> type_params;

                // Parse optional generic parameters for base trait
                if (_current_token.is(TokenKind::TK_L_ANGLE))
                {
                    advance(); // consume '<'

                    if (!_current_token.is(TokenKind::TK_R_ANGLE))
                    {
                        do
                        {
                            Token type_param = consume(TokenKind::TK_IDENTIFIER, "Expected type parameter");
                            type_params.push_back(std::string(type_param.text()));

                            if (_current_token.is(TokenKind::TK_COMMA))
                            {
                                advance(); // consume ','
                            }
                            else
                            {
                                break;
                            }
                        } while (!is_at_end());
                    }

                    consume(TokenKind::TK_R_ANGLE, "Expected '>' after base trait type parameters");
                }

                // Add base trait to the declaration
                trait_decl->add_base_trait(BaseTraitInfo(base_name, type_params));

                // Check for multiple inheritance (comma-separated base traits)
                if (_current_token.is(TokenKind::TK_COMMA))
                {
                    advance(); // consume ','
                }
                else
                {
                    break;
                }
            } while (!is_at_end());
        }

        consume(TokenKind::TK_L_BRACE, "Expected '{' after trait declaration");

        // Parse trait methods (only signatures, no implementations)
        while (!_current_token.is(TokenKind::TK_R_BRACE) && !is_at_end())
        {
            try
            {
                // Parse method signature
                if (_current_token.is(TokenKind::TK_IDENTIFIER) || _current_token.is(TokenKind::TK_KW_FROM))
                {
                    Token method_name_token = _current_token;
                    std::string method_name = std::string(method_name_token.text());
                    advance(); // consume method name token

                    consume(TokenKind::TK_L_PAREN, "Expected '(' after method name");

                    // Parse parameters
                    std::vector<std::unique_ptr<VariableDeclarationNode>> params;
                    if (!_current_token.is(TokenKind::TK_R_PAREN))
                    {
                        do
                        {
                            auto param = parse_parameter();
                            params.push_back(std::move(param));

                            if (_current_token.is(TokenKind::TK_COMMA))
                            {
                                advance(); // consume comma
                            }
                            else
                            {
                                break;
                            }
                        } while (!is_at_end());
                    }

                    consume(TokenKind::TK_R_PAREN, "Expected ')' after parameters");

                    // Parse return type
                    Type *return_type = _context.types().get_void_type(); // Default to void
                    if (_current_token.is(TokenKind::TK_ARROW))
                    {
                        advance(); // consume '->'
                        return_type = parse_type_annotation();
                    }

                    consume(TokenKind::TK_SEMICOLON, "Expected ';' after trait method signature");

                    // Create a function declaration (trait methods are just signatures)
                    auto method_decl = _builder.create_function_declaration(
                        _current_token.location(), method_name, return_type, true); // traits are public

                    // Add parameters to the function declaration
                    for (auto &param : params)
                    {
                        method_decl->add_parameter(std::move(param));
                    }

                    trait_decl->add_method(std::move(method_decl));
                }
                else
                {
                    error("Expected method signature in trait");
                    synchronize();
                }
            }
            catch (const ParseError &e)
            {
                _errors.push_back(e);
                synchronize();
            }
        }

        consume(TokenKind::TK_R_BRACE, "Expected '}' after trait body");
        return trait_decl;
    }

    std::unique_ptr<TypeAliasDeclarationNode> Parser::parse_type_alias_declaration()
    {
        SourceLocation start_loc = _current_token.location();

        consume(TokenKind::TK_KW_TYPE, "Expected 'type'");

        // Handle both identifiers and reserved keywords as type names
        std::string alias_name;
        if (_current_token.is(TokenKind::TK_IDENTIFIER))
        {
            alias_name = std::string(_current_token.text());
            advance();
        }
        else if (_current_token.is(TokenKind::TK_KW_INT) ||
                 _current_token.is(TokenKind::TK_KW_FLOAT) ||
                 _current_token.is(TokenKind::TK_KW_DOUBLE) ||
                 _current_token.is(TokenKind::TK_KW_STRING) ||
                 _current_token.is(TokenKind::TK_KW_BOOLEAN) ||
                 _current_token.is(TokenKind::TK_KW_I8) ||
                 _current_token.is(TokenKind::TK_KW_I16) ||
                 _current_token.is(TokenKind::TK_KW_I32) ||
                 _current_token.is(TokenKind::TK_KW_I64) ||
                 _current_token.is(TokenKind::TK_KW_F32) ||
                 _current_token.is(TokenKind::TK_KW_F64))
        {
            // Allow reserved keywords as type alias names
            alias_name = std::string(_current_token.text());
            advance();
        }
        else
        {
            error("Expected type alias name");
            return nullptr;
        }

        // Parse optional generic parameters <T, U, V>
        std::vector<std::string> generic_params;
        if (_current_token.is(TokenKind::TK_L_ANGLE))
        {
            advance(); // consume '<'

            do
            {
                if (!_current_token.is(TokenKind::TK_IDENTIFIER))
                {
                    error("Expected generic parameter name");
                    return nullptr;
                }

                generic_params.push_back(std::string(_current_token.text()));
                advance();

                if (_current_token.is(TokenKind::TK_COMMA))
                {
                    advance(); // consume ','
                }
                else if (_current_token.is(TokenKind::TK_R_ANGLE))
                {
                    break;
                }
                else
                {
                    error("Expected ',' or '>' in generic parameter list");
                    return nullptr;
                }
            } while (true);

            consume(TokenKind::TK_R_ANGLE, "Expected '>' after generic parameters");
        }

        // Check if this is a forward declaration (type i8;) or alias (type int = i64;)
        if (_current_token.is(TokenKind::TK_SEMICOLON))
        {
            // Forward declaration - for now, generic forward declarations are not supported
            if (!generic_params.empty())
            {
                error("Generic forward declarations are not yet supported");
                return nullptr;
            }
            advance();                                                                           // consume semicolon
            auto type_alias = _builder.create_type_alias_declaration(start_loc, alias_name, ""); // Empty target for forward decl

            // Attach documentation if available
            attach_documentation(type_alias.get());

            return type_alias;
        }

        consume(TokenKind::TK_EQUAL, "Expected '=' in type alias");

        // Parse target type
        std::string target_type = parse_type();

        consume(TokenKind::TK_SEMICOLON, "Expected ';' after type alias");

        auto type_alias = _builder.create_type_alias_declaration(start_loc, alias_name, target_type, generic_params);

        // Attach documentation if available
        attach_documentation(type_alias.get());

        return type_alias;
    }

    std::unique_ptr<EnumDeclarationNode> Parser::parse_enum_declaration()
    {
        SourceLocation start_loc = _current_token.location();

        // Handle both "enum" and "type enum" syntax
        if (_current_token.is(TokenKind::TK_KW_TYPE))
        {
            consume(TokenKind::TK_KW_TYPE, "Expected 'type'");
        }

        consume(TokenKind::TK_KW_ENUM, "Expected 'enum'");

        Token name_token = consume(TokenKind::TK_IDENTIFIER, "Expected enum name");
        std::string enum_name = std::string(name_token.text());

        auto enum_decl = _builder.create_enum_declaration(start_loc, enum_name);

        // Attach documentation if available
        attach_documentation(enum_decl.get());

        // Parse generic parameters if present
        if (_current_token.is(TokenKind::TK_L_ANGLE))
        {
            auto generic_params = parse_generic_parameters();
            for (auto &param : generic_params)
            {
                enum_decl->add_generic_parameter(std::move(param));
            }
        }

        consume(TokenKind::TK_L_BRACE, "Expected '{' after enum name");

        // Parse enum variants
        while (!_current_token.is(TokenKind::TK_R_BRACE) && !is_at_end())
        {
            auto variant = parse_enum_variant();
            if (variant)
            {
                enum_decl->add_variant(std::move(variant));
            }

            // Optional comma between variants
            if (_current_token.is(TokenKind::TK_COMMA))
            {
                advance();
            }
        }

        consume(TokenKind::TK_R_BRACE, "Expected '}' after enum body");

        // CRITICAL FIX: Register enum type immediately during parsing
        // This allows later type annotations to resolve to this enum
        // Determine if this is a simple enum (all variants have no associated data)
        bool is_simple_enum = true;
        std::vector<std::string> variant_names;

        for (const auto &variant : enum_decl->variants())
        {
            if (variant)
            {
                variant_names.push_back(variant->name());
                // If any variant has associated types, it's not a simple enum
                if (!variant->associated_types().empty())
                {
                    is_simple_enum = false;
                }
            }
        }

        // Register the enum type in TypeContext so future type resolution can find it
        // Skip registration for generic enums (they'll be handled by template system)
        if (enum_decl->generic_parameters().empty())
        {
            _context.types().get_enum_type(enum_name, variant_names, is_simple_enum);
            LOG_DEBUG(LogComponent::PARSER, "Registered enum type: {} (simple={}, variants={})", enum_name, is_simple_enum, variant_names.size());
        }

        return enum_decl;
    }

    std::unique_ptr<EnumVariantNode> Parser::parse_enum_variant()
    {
        SourceLocation start_loc = _current_token.location();

        Token name_token = consume(TokenKind::TK_IDENTIFIER, "Expected enum variant name");
        std::string variant_name = std::string(name_token.text());

        // Check if this is a simple variant (C-style) or complex variant (Rust-style)
        if (_current_token.is(TokenKind::TK_L_PAREN))
        {
            // Complex variant with associated data: Bar(Baz)
            consume(TokenKind::TK_L_PAREN, "Expected '('");

            std::vector<std::string> associated_types;

            // Parse associated types
            if (!_current_token.is(TokenKind::TK_R_PAREN))
            {
                do
                {
                    std::string type = parse_type();
                    associated_types.push_back(type);

                    if (_current_token.is(TokenKind::TK_COMMA))
                    {
                        advance();
                    }
                    else
                    {
                        break;
                    }
                } while (!is_at_end());
            }

            consume(TokenKind::TK_R_PAREN, "Expected ')' after variant types");

            return _builder.create_enum_variant(start_loc, variant_name, associated_types);
        }
        else
        {
            // Simple variant: NAME_1, NAME_2
            return _builder.create_enum_variant(start_loc, variant_name);
        }
    }

    std::unique_ptr<ImplementationBlockNode> Parser::parse_implementation_block()
    {
        SourceLocation start_loc = _current_token.location();

        consume(TokenKind::TK_KW_IMPLEMENT, "Expected 'implement'");

        // Optional 'struct', 'enum', or 'trait' keyword for different target types
        bool is_enum_impl = false;
        bool is_trait_impl = false;
        bool is_primitive_impl = false;

        if (_current_token.is(TokenKind::TK_KW_STRUCT))
        {
            advance(); // consume 'struct'
        }
        else if (_current_token.is(TokenKind::TK_KW_ENUM))
        {
            advance(); // consume 'enum'
            is_enum_impl = true;
        }
        else if (_current_token.is(TokenKind::TK_KW_TRAIT))
        {
            advance(); // consume 'trait'
            is_trait_impl = true;
        }
        else if (is_primitive_type_token())
        {
            // Handle primitive types like 'string', 'int', etc.
            is_primitive_impl = true;
        }

        Token target_token;
        if (is_primitive_impl)
        {
            // For primitive types, the current token is the type itself
            target_token = _current_token;
            advance();
        }
        else
        {
            target_token = consume(TokenKind::TK_IDENTIFIER, "Expected target type name");
        }
        std::string target_type = std::string(target_token.text());

        // Handle generic parameters for the target type (e.g., implement enum Option<T>)
        if (_current_token.is(TokenKind::TK_L_ANGLE))
        {
            target_type += "<";
            advance(); // consume '<'

            // Parse generic arguments
            do
            {
                if (_current_token.is(TokenKind::TK_IDENTIFIER))
                {
                    target_type += std::string(_current_token.text());
                    advance();
                }

                if (_current_token.is(TokenKind::TK_COMMA))
                {
                    target_type += ",";
                    advance();
                }
                else
                {
                    break;
                }
            } while (!is_at_end() && !_current_token.is(TokenKind::TK_R_ANGLE));

            consume(TokenKind::TK_R_ANGLE, "Expected '>' after generic parameters");
            target_type += ">";
        }

        auto impl_block = _builder.create_implementation_block(start_loc, target_type);

        consume(TokenKind::TK_L_BRACE, "Expected '{' after implement declaration");

        // TODO: Add implementation block context tracking

        while (!_current_token.is(TokenKind::TK_R_BRACE) && !is_at_end())
        {
            try
            {
                // Check if it's a field implementation or method implementation
                Token next = peek_next();
                if (_current_token.is(TokenKind::TK_IDENTIFIER) && next.is(TokenKind::TK_EQUAL))
                {
                    // Field implementation: field_name = value;
                    Token field_token = consume(TokenKind::TK_IDENTIFIER, "Expected field name");
                    consume(TokenKind::TK_EQUAL, "Expected '=' in field implementation");

                    auto value = parse_expression();
                    consume(TokenKind::TK_SEMICOLON, "Expected ';' after field implementation");

                    // Create a field node with the default value
                    Type *auto_type = resolve_type_from_string("auto"); // Type will be inferred
                    auto field = _builder.create_struct_field(field_token.location(),
                                                              std::string(field_token.text()),
                                                              auto_type,
                                                              Visibility::Public);
                    field->set_default_value(std::move(value));
                    impl_block->add_field_implementation(std::move(field));
                }
                else if ((_current_token.is(TokenKind::TK_IDENTIFIER) || _current_token.is_keyword()) &&
                         (next.is(TokenKind::TK_L_PAREN) || next.is(TokenKind::TK_KW_CONSTRUCTOR)))
                {
                    // Method implementation
                    auto method = parse_struct_method(target_type);
                    impl_block->add_method_implementation(std::move(method));
                }
                else
                {
                    error("Expected field implementation or method implementation");
                    synchronize();
                }
            }
            catch (const ParseError &e)
            {
                _errors.push_back(e);
                synchronize();
            }
        }

        consume(TokenKind::TK_R_BRACE, "Expected '}' after implementation block");

        return impl_block;
    }

    std::unique_ptr<ExternBlockNode> Parser::parse_extern_block()
    {
        SourceLocation start_loc = _current_token.location();

        consume(TokenKind::TK_KW_EXTERN, "Expected 'extern'");

        // Parse linkage specifier (e.g., "C")
        Token linkage_token = consume(TokenKind::TK_STRING_LITERAL, "Expected linkage specifier (e.g., \"C\")");
        std::string linkage_type = std::string(linkage_token.text());

        // Remove quotes from linkage type
        if (linkage_type.length() >= 2 && linkage_type.front() == '"' && linkage_type.back() == '"')
        {
            linkage_type = linkage_type.substr(1, linkage_type.length() - 2);
        }

        auto extern_block = _builder.create_extern_block(start_loc, linkage_type);

        consume(TokenKind::TK_L_BRACE, "Expected '{' after extern linkage");

        // Parse function declarations within the extern block
        while (!_current_token.is(TokenKind::TK_R_BRACE) && !is_at_end())
        {
            try
            {
                // Parse function declaration (should be function signatures only)
                if (_current_token.is(TokenKind::TK_KW_FUNCTION))
                {
                    auto func_decl = parse_extern_function_declaration();
                    extern_block->add_function_declaration(std::move(func_decl));
                }
                else
                {
                    error("Expected function declaration in extern block");
                    synchronize();
                }
            }
            catch (const ParseError &e)
            {
                _errors.push_back(e);
                synchronize();
            }
        }

        consume(TokenKind::TK_R_BRACE, "Expected '}' after extern block");

        return extern_block;
    }

    // Struct/Class helper parsing methods
    std::vector<std::unique_ptr<GenericParameterNode>> Parser::parse_generic_parameters()
    {
        std::vector<std::unique_ptr<GenericParameterNode>> generics;

        consume(TokenKind::TK_L_ANGLE, "Expected '<' for generics");

        if (!_current_token.is(TokenKind::TK_R_ANGLE))
        {
            do
            {
                auto generic = parse_generic_parameter();
                generics.push_back(std::move(generic));

                if (_current_token.is(TokenKind::TK_COMMA))
                {
                    advance(); // consume ','
                }
                else
                {
                    break;
                }
            } while (!_current_token.is(TokenKind::TK_R_ANGLE) && !is_at_end());
        }

        consume(TokenKind::TK_R_ANGLE, "Expected '>' after generic parameters");
        return generics;
    }

    std::unique_ptr<GenericParameterNode> Parser::parse_generic_parameter()
    {
        Token name_token = consume(TokenKind::TK_IDENTIFIER, "Expected generic parameter name");
        auto generic = _builder.create_generic_parameter(name_token.location(),
                                                         std::string(name_token.text()));

        // Parse optional constraints (T: Trait1 + Trait2)
        if (_current_token.is(TokenKind::TK_COLON))
        {
            advance(); // consume ':'

            do
            {
                Token constraint_token = consume(TokenKind::TK_IDENTIFIER, "Expected constraint type");
                generic->add_constraint(std::string(constraint_token.text()));

                if (_current_token.is(TokenKind::TK_PLUS))
                {
                    advance(); // consume '+'
                }
                else
                {
                    break;
                }
            } while (!is_at_end());
        }

        return generic;
    }

    std::unique_ptr<StructFieldNode> Parser::parse_struct_field(Visibility default_visibility)
    {
        SourceLocation start_loc = _current_token.location();

        // Use the visibility passed from the calling context
        // Don't try to parse visibility again if it was already parsed in the outer scope
        // WORKAROUND: Force struct fields to always be public to avoid LLVM codegen crash
        // TODO: Fix the root cause in TypeMapper/CodegenVisitor for private struct members
        Visibility visibility = Visibility::Public;

        Token field_token = consume(TokenKind::TK_IDENTIFIER, "Expected field name");
        std::string field_name = std::string(field_token.text());

        consume(TokenKind::TK_COLON, "Expected ':' after field name");

        Type *field_type = parse_type_annotation();

        auto field = _builder.create_struct_field(start_loc, field_name, field_type, visibility);

        // Parse optional default value
        if (_current_token.is(TokenKind::TK_EQUAL))
        {
            advance(); // consume '='
            auto default_value = parse_expression();
            field->set_default_value(std::move(default_value));
        }

        consume(TokenKind::TK_SEMICOLON, "Expected ';' after field declaration");
        return field;
    }

    std::unique_ptr<StructMethodNode> Parser::parse_struct_method(const std::string &struct_name, Visibility default_visibility)
    {
        SourceLocation start_loc = _current_token.location();

        // Use the visibility passed from the calling context
        // This respects the private:/public: sections in classes and structs
        Visibility visibility = default_visibility;

        // Check for static keyword
        bool is_static = false;
        if (_current_token.is(TokenKind::TK_KW_STATIC))
        {
            is_static = true;
            advance(); // consume 'static'
        }

        // Check for constructor
        bool is_constructor = false;
        if (_current_token.is(TokenKind::TK_KW_CONSTRUCTOR))
        {
            is_constructor = true;
            advance();
        }

        // Check for destructor
        bool is_destructor = false;
        if (_current_token.is(TokenKind::TK_TILDE))
        {
            is_destructor = true;
            advance(); // consume '~'
        }

        Token method_token;
        std::string method_name;

        // For destructors, method name should match the struct/class name
        if (is_destructor)
        {
            if (_current_token.is(TokenKind::TK_IDENTIFIER))
            {
                method_token = _current_token;
                method_name = "~" + std::string(method_token.text());
                advance();
            }
            else
            {
                error("Expected class/struct name after '~' in destructor");
                return nullptr;
            }
        }
        // Accept both identifiers and keywords as method names
        else if (_current_token.is(TokenKind::TK_IDENTIFIER) || _current_token.is_keyword())
        {
            method_token = _current_token;
            method_name = std::string(method_token.text());
            advance();
        }
        else
        {
            error("Expected method name");
            return nullptr;
        }

        // Check for C++-style constructor (method name matches class/struct name)
        if (!is_constructor && !struct_name.empty() && method_name == struct_name)
        {
            is_constructor = true;
        }

        consume(TokenKind::TK_L_PAREN, "Expected '(' after method name");

        // Parse parameters
        std::vector<std::unique_ptr<VariableDeclarationNode>> params;
        bool is_variadic = false;
        if (!_current_token.is(TokenKind::TK_R_PAREN))
        {
            auto param_result = parse_parameter_list();
            params = std::move(param_result.first);
            is_variadic = param_result.second;
        }

        consume(TokenKind::TK_R_PAREN, "Expected ')' after parameters");

        // Parse return type (optional for constructors)
        Type *return_type = _context.types().get_void_type(); // Default to void
        if (_current_token.is(TokenKind::TK_ARROW))
        {
            advance(); // consume '->'
            return_type = parse_type_annotation();
        }
        else if (is_constructor)
        {
            return_type = _context.types().get_void_type(); // Constructors don't have explicit return types
        }

        // Check for default destructor syntax: ~TypeName() default;
        bool is_default_destructor = false;
        if (is_destructor && _current_token.is(TokenKind::TK_KW_DEFAULT))
        {
            is_default_destructor = true;
            advance(); // consume 'default'
        }

        auto method = _builder.create_struct_method(start_loc, method_name, return_type, visibility, is_constructor, is_destructor, is_static, is_default_destructor);

        // Set variadic flag if detected
        method->set_variadic(is_variadic);

        // Add parameters
        for (auto &param : params)
        {
            method->add_parameter(std::move(param));
        }

        // Parse optional body (for method implementations)
        if (_current_token.is(TokenKind::TK_L_BRACE))
        {
            auto body = parse_block_statement();
            method->set_body(std::unique_ptr<BlockStatementNode>(
                dynamic_cast<BlockStatementNode *>(body.release())));
        }
        else if (is_default_destructor)
        {
            // Default destructors don't have a body - the compiler will generate one
            consume(TokenKind::TK_SEMICOLON, "Expected ';' after default destructor");
        }
        else
        {
            // Method signature only
            consume(TokenKind::TK_SEMICOLON, "Expected ';' after method signature");
        }

        return method;
    }

    Visibility Parser::parse_visibility_modifier()
    {
        if (_current_token.is(TokenKind::TK_KW_PUBLIC))
        {
            advance();
            return Visibility::Public;
        }
        else if (_current_token.is(TokenKind::TK_KW_PRIVATE))
        {
            advance();
            return Visibility::Private;
        }
        else if (_current_token.is(TokenKind::TK_KW_PROTECTED))
        {
            advance();
            return Visibility::Protected;
        }

        error("Expected visibility modifier");
        return Visibility::Public; // Default fallback
    }

    Token Parser::peek_next()
    {
        // Use the lexer's built-in peek functionality
        return _lexer->peek_token();
    }

    // Function type parsing helpers
    bool Parser::is_function_type_ahead()
    {
        // Simple heuristic: if we're at '(' and the peeked token suggests function parameters,
        // we'll tentatively treat it as a function type and let the parsing fail gracefully if wrong

        // For now, let's use a simpler approach: look at context clues
        // If we're in a type alias context (which we are), () followed by -> is likely a function type
        // We can check if the next few tokens after '(' look like parameter syntax

        // This is a simplified heuristic - in practice, we might need to be more sophisticated
        return true; // For function types in type alias context, assume it's a function type
    }

    std::string Parser::parse_function_type_syntax(const std::string &type_prefix)
    {
        std::string function_type = type_prefix;

        // Consume '('
        consume(TokenKind::TK_L_PAREN, "Expected '('");
        function_type += "(";

        // Parse parameter types
        if (!_current_token.is(TokenKind::TK_R_PAREN))
        {
            do
            {
                std::string param_type = parse_type();
                function_type += param_type;

                if (_current_token.is(TokenKind::TK_COMMA))
                {
                    function_type += ", ";
                    advance(); // consume ','
                }
                else
                {
                    break;
                }
            } while (true);
        }

        // Consume ')'
        consume(TokenKind::TK_R_PAREN, "Expected ')' after function parameters");
        function_type += ")";

        // Consume '->'
        consume(TokenKind::TK_ARROW, "Expected '->' after function parameters");
        function_type += " -> ";

        // Parse return type
        std::string return_type = parse_type();
        function_type += return_type;

        return function_type;
    }

    std::string Parser::parse_generic_function_type_syntax(const std::string &type_prefix)
    {
        std::string function_type = type_prefix;

        // Consume '<'
        consume(TokenKind::TK_L_ANGLE, "Expected '<'");
        function_type += "<";

        // Parse generic parameters
        do
        {
            if (!_current_token.is(TokenKind::TK_IDENTIFIER))
            {
                error("Expected generic parameter name");
                return function_type;
            }

            function_type += std::string(_current_token.text());
            advance(); // consume parameter name

            if (_current_token.is(TokenKind::TK_COMMA))
            {
                function_type += ", ";
                advance(); // consume ','
            }
            else
            {
                break;
            }
        } while (true);

        // Consume '>'
        consume(TokenKind::TK_R_ANGLE, "Expected '>' after generic parameters");
        function_type += ">";

        // Now parse the function signature: (params) -> return_type
        // Consume '('
        consume(TokenKind::TK_L_PAREN, "Expected '(' after generic parameters");
        function_type += "(";

        // Parse parameter types (may include named parameters like 'arg: T')
        if (!_current_token.is(TokenKind::TK_R_PAREN))
        {
            do
            {
                // Handle named parameters: arg: T or just T
                if (_current_token.is(TokenKind::TK_IDENTIFIER))
                {
                    Token next = peek_next();
                    if (next.is(TokenKind::TK_COLON))
                    {
                        // Named parameter: arg: T
                        function_type += std::string(_current_token.text());
                        advance(); // consume parameter name

                        consume(TokenKind::TK_COLON, "Expected ':' after parameter name");
                        function_type += ": ";

                        std::string param_type = parse_type();
                        function_type += param_type;
                    }
                    else
                    {
                        // Just type: T
                        std::string param_type = parse_type();
                        function_type += param_type;
                    }
                }
                else
                {
                    // Just type
                    std::string param_type = parse_type();
                    function_type += param_type;
                }

                if (_current_token.is(TokenKind::TK_COMMA))
                {
                    function_type += ", ";
                    advance(); // consume ','
                }
                else
                {
                    break;
                }
            } while (true);
        }

        // Consume ')'
        consume(TokenKind::TK_R_PAREN, "Expected ')' after function parameters");
        function_type += ")";

        // Consume '->'
        consume(TokenKind::TK_ARROW, "Expected '->' after function parameters");
        function_type += " -> ";

        // Parse return type
        std::string return_type = parse_type();
        function_type += return_type;

        return function_type;
    }

    // ================================================================
    // Documentation Comment Handling
    // ================================================================

    void Parser::collect_documentation_comments()
    {
        // This method is called automatically by advance() now
        // Documentation comments are collected in _pending_doc_comments
    }

    std::string Parser::extract_documentation_text()
    {
        if (_pending_doc_comments.empty())
        {
            return "";
        }

        std::string result;
        for (const auto &comment : _pending_doc_comments)
        {
            std::string processed = comment;

            // Process block comments (/** ... */)
            if (processed.substr(0, 3) == "/**")
            {
                // Remove /** and */ and clean up formatting
                processed = processed.substr(3);
                if (processed.size() >= 2 && processed.substr(processed.size() - 2) == "*/")
                {
                    processed = processed.substr(0, processed.size() - 2);
                }

                // Clean up whitespace and asterisks at line beginnings
                std::istringstream iss(processed);
                std::string line;
                std::vector<std::string> lines;

                while (std::getline(iss, line))
                {
                    // Trim leading whitespace
                    size_t start = line.find_first_not_of(" \t");
                    if (start != std::string::npos)
                    {
                        line = line.substr(start);
                        // Remove leading * if present
                        if (!line.empty() && line[0] == '*')
                        {
                            line = line.substr(1);
                            // Remove one space after * if present
                            if (!line.empty() && line[0] == ' ')
                            {
                                line = line.substr(1);
                            }
                        }
                    }
                    else
                    {
                        line = "";
                    }
                    lines.push_back(line);
                }

                // Join lines with newlines, but remove empty lines at start/end
                while (!lines.empty() && lines.front().empty())
                {
                    lines.erase(lines.begin());
                }
                while (!lines.empty() && lines.back().empty())
                {
                    lines.pop_back();
                }

                for (size_t i = 0; i < lines.size(); ++i)
                {
                    if (i > 0)
                        result += "\n";
                    result += lines[i];
                }
            }
            // Process line comments (/// ...)
            else if (processed.substr(0, 3) == "///")
            {
                // Remove /// and optional space
                processed = processed.substr(3);
                if (!processed.empty() && processed[0] == ' ')
                {
                    processed = processed.substr(1);
                }

                if (!result.empty())
                {
                    result += "\n";
                }
                result += processed;
            }
        }

        // Clear processed comments
        _pending_doc_comments.clear();

        return result;
    }

    void Parser::attach_documentation(DeclarationNode *node)
    {
        if (node && !_pending_doc_comments.empty())
        {
            std::string doc_text = extract_documentation_text();
            if (!doc_text.empty())
            {
                node->set_documentation(doc_text);
            }
        }
    }

    Type *Parser::parse_type_annotation_with_tokens()
    {
        // Create a string stream to build type tokens
        std::string type_string = "";
        std::vector<Token> collected_tokens;

        // Collect tokens that form the complete type expression
        // This handles complex types like: const int**, Option<Result<T, E>>, etc.
        while (is_type_token() ||
               _current_token.is(TokenKind::TK_L_ANGLE) ||
               _current_token.is(TokenKind::TK_R_ANGLE) ||
               _current_token.is(TokenKind::TK_L_SQUARE) ||
               _current_token.is(TokenKind::TK_R_SQUARE) ||
               _current_token.is(TokenKind::TK_STAR) ||
               _current_token.is(TokenKind::TK_AMP) ||
               _current_token.is(TokenKind::TK_COMMA) ||
               _current_token.is(TokenKind::TK_COLONCOLON) ||
               _current_token.is(TokenKind::TK_KW_CONST) ||
               _current_token.is(TokenKind::TK_KW_MUT) ||
               _current_token.is(TokenKind::TK_IDENTIFIER))
        {
            collected_tokens.push_back(_current_token);
            type_string += std::string(_current_token.text()) + " ";
            advance();

            // Break on certain terminators
            if (_current_token.is(TokenKind::TK_SEMICOLON) ||
                _current_token.is(TokenKind::TK_COMMA) ||
                _current_token.is(TokenKind::TK_R_PAREN) ||
                _current_token.is(TokenKind::TK_R_BRACE) ||
                _current_token.is(TokenKind::TK_EQUAL) ||
                _current_token.is_eof())
            {
                break;
            }
        }

        if (collected_tokens.empty())
        {
            error("Expected type annotation");
            return _context.types().get_unknown_type();
        }

        // Use the token-based parsing system
        size_t index = 0;
        Type *parsed_type = _context.types().parse_type_from_token_stream(collected_tokens, index);

        if (!parsed_type)
        {
            // Fallback to string-based parsing if token parsing fails
            LOG_DEBUG(LogComponent::PARSER, "Token-based parsing failed for '{}' (collected {} tokens), falling back to string parsing", type_string, collected_tokens.size());
            if (!collected_tokens.empty())
            {
                LOG_DEBUG(LogComponent::PARSER, "First token: {} ({})", static_cast<int>(collected_tokens[0].kind()), std::string(collected_tokens[0].text()));
            }

            // Trim whitespace
            type_string.erase(type_string.find_last_not_of(" \t\n\r\f\v") + 1);
            parsed_type = resolve_type_from_string(type_string);
        }

        return parsed_type ? parsed_type : _context.types().get_unknown_type();
    }

    Type *Parser::resolve_type_from_string(const std::string &type_str)
    {
        // Handle basic built-in types first
        if (type_str == "void")
            return _context.types().get_void_type();
        if (type_str == "int" || type_str == "i32")
            return _context.types().get_i32_type();
        if (type_str == "i8")
            return _context.types().get_i8_type();
        if (type_str == "i16")
            return _context.types().get_i16_type();
        if (type_str == "i64")
            return _context.types().get_i64_type();
        if (type_str == "f32")
            return _context.types().get_f32_type();
        if (type_str == "f64")
            return _context.types().get_f64_type();
        if (type_str == "float")
            return _context.types().get_f32_type();
        if (type_str == "double")
            return _context.types().get_f64_type();
        if (type_str == "boolean")
            return _context.types().get_boolean_type();
        if (type_str == "char")
            return _context.types().get_char_type();
        if (type_str == "string")
            return _context.types().get_string_type();
        if (type_str == "auto")
            return _context.types().get_auto_type(); // Auto type inference
        if (type_str == "...")
            return _context.types().get_variadic_type(); // Variadic type

        // Check for generic types first (e.g., "Array<int>")
        if (type_str.find('<') != std::string::npos && type_str.find('>') != std::string::npos)
        {
            LOG_DEBUG(LogComponent::PARSER, "Parser detected generic type syntax: '{}'", type_str);
            // Use TypeRegistry's parse_and_instantiate for proper generic type resolution
            ParameterizedType *generic_type = _context.types().get_type_registry()->parse_and_instantiate(type_str);
            if (generic_type && generic_type->kind() != TypeKind::Unknown)
            {
                LOG_DEBUG(LogComponent::PARSER, "Successfully resolved generic type '{}' via TypeRegistry", type_str);
                return generic_type;
            }
            else
            {
                LOG_DEBUG(LogComponent::PARSER, "Failed to resolve generic type '{}' via TypeRegistry", type_str);
            }
        }

        // Try to resolve as a struct/class type
        Type *struct_type = _context.types().lookup_struct_type(type_str);
        if (struct_type && struct_type->kind() != TypeKind::Unknown)
        {
            return struct_type;
        }

        // Try to resolve as a class type
        Type *class_type = _context.types().get_class_type(type_str);
        if (class_type && class_type->kind() != TypeKind::Unknown)
        {
            return class_type;
        }

        // Try to resolve as an enum type
        Type *enum_type = _context.types().get_enum_type(type_str, {}, false);
        if (enum_type && enum_type->kind() != TypeKind::Unknown)
        {
            return enum_type;
        }

        // Could be a generic parameter
        Type *generic_type = _context.types().get_generic_type(type_str);
        if (generic_type)
        {
            return generic_type;
        }

        // For unresolved types that might be enums or structs defined later in the file,
        // we'll return an unknown type and let the TypeChecker resolve it properly
        // when all types are available during type checking phase
        LOG_DEBUG(LogComponent::PARSER, "Could not resolve '{}' during parsing, deferring to type checker", type_str);

        // Return unknown type - the TypeChecker will attempt to resolve it again during type checking
        // when all enum and struct declarations have been processed
        return _context.types().get_unknown_type();
    }

    //===----------------------------------------------------------------------===//
    // Directive Parsing Implementation
    //===----------------------------------------------------------------------===//

    bool Parser::is_directive_start()
    {
        return _current_token.is(TokenKind::TK_HASH) && peek_next().is(TokenKind::TK_L_SQUARE);
    }

    std::vector<std::unique_ptr<DirectiveNode>> Parser::parse_leading_directives()
    {
        std::vector<std::unique_ptr<DirectiveNode>> directives;

        // Parse any leading directives
        while (is_directive_start())
        {
            auto directive = parse_directive();
            if (directive)
            {
                directives.push_back(std::move(directive));
            }
            else
            {
                // If we failed to parse a directive, break to avoid infinite loop
                break;
            }
        }

        return directives;
    }

    std::unique_ptr<DirectiveNode> Parser::parse_directive()
    {
        if (!is_directive_start())
        {
            error("Expected directive starting with '#['");
            return nullptr;
        }

        // Consume '#' and '['
        consume(TokenKind::TK_HASH, "Expected '#'");
        consume(TokenKind::TK_L_SQUARE, "Expected '[' after '#'");

        // Parse directive name
        if (!_current_token.is(TokenKind::TK_IDENTIFIER))
        {
            error("Expected directive name after '#['");
            return nullptr;
        }

        Token name_token = consume(TokenKind::TK_IDENTIFIER, "Expected directive name");
        std::string directive_name = std::string(name_token.text());

        // Check if we have a processor for this directive
        if (!_directive_registry || !_directive_registry->has_processor(directive_name))
        {
            error("Unknown directive: " + directive_name);
            return nullptr;
        }

        // Get the processor and let it parse the arguments
        auto processor = _directive_registry->get_processor(directive_name);
        if (!processor)
        {
            error("Failed to get processor for directive: " + directive_name);
            return nullptr;
        }

        // Let the processor parse the directive arguments
        // The processor should consume tokens up to but NOT including the closing ']'
        auto directive_node = processor->parse_directive_arguments(*this);

        if (!directive_node)
        {
            error("Failed to parse directive arguments for: " + directive_name);
            return nullptr;
        }

        // Consume the closing ']'
        consume(TokenKind::TK_R_SQUARE, "Expected ']' after directive");

        return directive_node;
    }

    //===----------------------------------------------------------------------===//
    // Directive Parsing Utilities
    //===----------------------------------------------------------------------===//

    bool Parser::match_token(TokenKind kind)
    {
        return match(kind);
    }

    void Parser::advance_token()
    {
        advance();
    }

    Token Parser::consume_token(TokenKind expected, const std::string &error_message)
    {
        return consume(expected, error_message);
    }

    void Parser::report_error(const std::string &message)
    {
        error(message);
    }

    bool Parser::is_parser_at_end() const
    {
        return is_at_end();
    }
}