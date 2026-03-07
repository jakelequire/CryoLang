#include "Parser/Parser.hpp"
#include "Types/TypeAnnotation.hpp"
#include "Utils/Logger.hpp"
#include "Diagnostics/Diag.hpp"
#include <iostream>
#include <cctype>
#include <algorithm>
#include <optional>

namespace Cryo
{
    Parser::Parser(std::unique_ptr<Lexer> lexer, ASTContext &context)
        : _lexer(std::move(lexer)), _context(context), _builder(context), _diagnostics(nullptr)
    {
        // Set source file on the builder from the lexer's file path
        if (_lexer && &_lexer->file())
        {
            _source_file = _lexer->file().path();
            _builder.set_source_file(_source_file);
        }

        // Initialize current module to "Global" by default
        _current_module_id = _context.modules().get_or_create_module("Global", _source_file);
        _context.symbols().set_current_module(_current_module_id);

        // Initialize Symbol Resolution Manager (SRM) - only if symbol table is valid
        try
        {
            if (&context.symbols())
            {
                _srm_context = std::make_unique<Cryo::SRM::SymbolResolutionContext>(&context.types());
                _srm_manager = std::make_unique<Cryo::SRM::SymbolResolutionManager>(_srm_context.get());
            }
        }
        catch (const std::exception &e)
        {
            // SRM initialization failed, continue without it (fallback to manual naming)
            _srm_context.reset();
            _srm_manager.reset();
            LOG_WARN(LogComponent::PARSER, "Symbol Resolution Manager (SRM) initialization failed: {}", e.what());
        }

        // Initialize by getting the first token
        advance();
    }

    Parser::Parser(std::unique_ptr<Lexer> lexer, ASTContext &context, DiagEmitter *diagnostics, const std::string &source_file)
        : _lexer(std::move(lexer)), _context(context), _builder(context), _diagnostics(diagnostics), _source_file(source_file)
    {
        // Set source file on the builder so created nodes know their origin
        _builder.set_source_file(source_file);

        // Initialize current module to "Global" by default
        _current_module_id = _context.modules().get_or_create_module("Global", source_file);
        _context.symbols().set_current_module(_current_module_id);

        // Initialize Symbol Resolution Manager (SRM) - only if symbol table is valid
        try
        {
            if (&context.symbols())
            {
                _srm_context = std::make_unique<Cryo::SRM::SymbolResolutionContext>(&context.types());
                _srm_manager = std::make_unique<Cryo::SRM::SymbolResolutionManager>(_srm_context.get());
            }
        }
        catch (const std::exception &e)
        {
            // SRM initialization failed, continue without it (fallback to manual naming)
            _srm_context.reset();
            _srm_manager.reset();
            LOG_WARN(LogComponent::PARSER, "Symbol Resolution Manager (SRM) initialization failed: {}", e.what());
        }

        // Initialize by getting the first token
        advance();
    }

    void Parser::report_error(ErrorCode error_code, const std::string &message, SourceRange range)
    {
        if (_diagnostics)
        {
            Span span(_source_file, range.start.line(), range.start.column(),
                      range.end.line(), range.end.column());
            _diagnostics->emit(Diag::error(error_code, message).at(span));
        }
        else
        {
            // Fallback to old error system
            LOG_ERROR(LogComponent::PARSER, "Parse Error: {}", message);
        }
    }

    void Parser::report_warning(ErrorCode error_code, const std::string &message, SourceRange range)
    {
        if (_diagnostics)
        {
            Span span(_source_file, range.start.line(), range.start.column(),
                      range.end.line(), range.end.column());
            _diagnostics->emit(Diag::warning(error_code, message).at(span));
        }
        else
        {
            // Fallback to old warning system
            LOG_WARN(LogComponent::PARSER, "Parse Warning: {}", message);
        }
    }

    void Parser::report_enhanced_token_error(TokenKind expected, const std::string &context_message)
    {
        if (!_diagnostics)
        {
            // Fallback to basic error reporting
            error(context_message);
            return;
        }

        // Get error code based on expected token
        ErrorCode code = get_token_error_code(expected);

        // Build diagnostic with location
        Span span = Span::at(_current_token.location(), _source_file);

        // Create message based on context
        std::string message;
        if (_current_token.is_eof())
        {
            message = "unexpected end of file, expected " + get_token_name(expected);
        }
        else if (is_delimiter_token(expected) && is_delimiter_token(_current_token.kind()))
        {
            char expected_char = get_delimiter_char(expected);
            char found_char = get_delimiter_char(_current_token.kind());
            message = std::string("mismatched delimiter: expected '") + expected_char +
                      "', found '" + found_char + "'";
            code = ErrorCode::E0110_MISMATCHED_DELIMITERS;
        }
        else
        {
            message = "expected " + get_token_name(expected) + ", found " +
                      TokenKindToString(_current_token.kind());
        }

        auto diag = Diag::error(code, message).at(span);
        if (!context_message.empty())
        {
            diag.with_note(context_message);
        }

        _diagnostics->emit(std::move(diag));
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

    void Parser::add_token_mismatch_suggestions(Diag &diagnostic, TokenKind expected, TokenKind actual, const std::string &context)
    {
        Span span = Span::at(_current_token.location(), _source_file);

        switch (expected)
        {
        case TokenKind::TK_SEMICOLON:
            diagnostic.suggest(Suggestion::insert_after(_current_token.location(), ";", "add a semicolon", _source_file));
            diagnostic.with_note("statements must be terminated with a semicolon");
            break;

        case TokenKind::TK_R_PAREN:
            if (actual == TokenKind::TK_COMMA)
            {
                diagnostic.suggest(Suggestion::replace(span, ")", "replace comma with closing parenthesis"));
                diagnostic.with_note("function parameter lists must be closed with `)`");
            }
            else
            {
                diagnostic.suggest(Suggestion::insert_after(_current_token.location(), ")", "add closing parenthesis", _source_file));
                diagnostic.with_note("opened parenthesis must be closed");
            }
            break;

        case TokenKind::TK_R_BRACE:
            diagnostic.suggest(Suggestion::insert_after(_current_token.location(), "}", "add closing brace", _source_file));
            diagnostic.with_note("code blocks must be closed with `}`");
            break;

        case TokenKind::TK_IDENTIFIER:
            if (actual == TokenKind::TK_KW_FUNCTION || actual == TokenKind::TK_KW_CONST)
            {
                diagnostic.with_note("expected an identifier (variable or function name) here");
                diagnostic.help("identifiers must start with a letter or underscore");
            }
            break;

        default:
            // Generic suggestion
            diagnostic.help("check the syntax and ensure proper token placement");
            break;
        }

        // Add context-specific help
        if (!context.empty() && context != get_token_name(expected))
        {
            diagnostic.with_note("context: " + context);
        }
    }

    void Parser::add_context_spans(Diag &diagnostic, TokenKind expected)
    {
        // For closing delimiters, try to find the matching opening delimiter
        if (expected == TokenKind::TK_R_PAREN || expected == TokenKind::TK_R_BRACE || expected == TokenKind::TK_R_SQUARE)
        {
            // This is a simplified implementation - a more sophisticated version would
            // track delimiter pairs during parsing
            diagnostic.help("check for matching opening delimiter");
        }
    }

    std::unique_ptr<ProgramNode> Parser::parse_program()
    {
        // Reset parser state for clean parsing session
        reset_parsing_state();

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
            auto ns_node = parse_namespace();
            std::string namespace_name = ns_node->module_path();
            program->add_statement(std::move(ns_node));
            _current_namespace = namespace_name; // Store the namespace in parser state

            // Get or create a ModuleID for this namespace and update context
            _current_module_id = _context.modules().get_or_create_module(namespace_name, _source_file);
            _context.symbols().set_current_module(_current_module_id);
            LOG_DEBUG(LogComponent::PARSER, "Set current module to '{}' (id={})", namespace_name, _current_module_id.id);

            // Update SRM context with the new namespace
            if (_srm_context)
            {
                // Clear and rebuild namespace stack from the full namespace name
                // First, clear existing stack
                while (!_srm_context->get_namespace_stack().empty())
                {
                    _srm_context->pop_namespace();
                }

                // Parse and push each part of the namespace
                auto parts = get_current_namespace_parts();
                for (const auto &part : parts)
                {
                    _srm_context->push_namespace(part);
                }
            }
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
        // First, check if we have buffered tokens from lookahead
        if (!_lookahead_buffer.empty())
        {
            _current_token = _lookahead_buffer.front();
            _lookahead_buffer.erase(_lookahead_buffer.begin());
            update_bracket_depth(_current_token.kind());
            _tokens_consumed++;
            return;
        }

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

        // Update bracket depth tracking and token count
        update_bracket_depth(_current_token.kind());
        _tokens_consumed++;
    }

    bool Parser::match(TokenKind kind)
    {
        if (_current_token.is(kind))
        {
            advance(); // This will automatically update bracket depth
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
        if (_diagnostics)
        {
            // Use enhanced error reporting
            Span span = Span::at(_current_token.location(), _source_file);
            span.label = message;

            auto diag = Diag::error(ErrorCode::E0111_INVALID_SYNTAX, message).at(span);

            // Add contextual suggestions for common parsing errors
            add_generic_parsing_suggestions(diag, message);

            _diagnostics->emit(std::move(diag));
        }
        else
        {
            // Create a source range from the current token location
            SourceRange range;
            range.start = _current_token.location();
            range.end = _current_token.location();

            // Report using our method
            report_error(ErrorCode::E0111_INVALID_SYNTAX, message, range);
        }

        // Still throw for old error handling compatibility
        throw ParseError(message, _current_token.location());
    }

    void Parser::add_generic_parsing_suggestions(Diag &diagnostic, const std::string &message)
    {
        std::string lower_message = message;
        std::transform(lower_message.begin(), lower_message.end(), lower_message.begin(), ::tolower);

        if (lower_message.find("expression") != std::string::npos)
        {
            diagnostic.with_note("expressions include variables, function calls, literals, and operators");
            diagnostic.help("examples: `x`, `foo()`, `42`, `x + y`");
        }
        else if (lower_message.find("statement") != std::string::npos)
        {
            diagnostic.with_note("statements include variable declarations, assignments, and function calls");
            diagnostic.with_note("statements must end with a semicolon `;`");
        }
        else if (lower_message.find("type") != std::string::npos)
        {
            diagnostic.with_note("types include built-in types like `int`, `string`, `bool` and user-defined types");
            diagnostic.help("examples: `int`, `string`, `MyStruct`, `Array<int>`");
        }
        else if (lower_message.find("unexpected") != std::string::npos)
        {
            diagnostic.help("check for missing semicolons, braces, or parentheses");
            diagnostic.with_note("ensure proper nesting of code blocks and expressions");
        }
    }
    void Parser::synchronize()
    {
        LOG_DEBUG(LogComponent::PARSER, "Parser synchronization initiated at {}:{} (file: {})(brace_depth={}, paren_depth={}, bracket_depth={})",
                  _current_token.location().line(), _current_token.location().column(), _source_file,
                  _brace_depth, _paren_depth, _bracket_depth);

        // Track starting position for diagnostic purposes
        SourceLocation sync_start = _current_token.location();
        size_t tokens_skipped = 0;
        size_t sync_start_token_count = _tokens_consumed;

        // Prevent infinite loops by ensuring we always make progress
        Token previous_token = _current_token;

        // Enhanced error recovery with context-aware synchronization
        while (!is_at_end())
        {
            TokenKind current_kind = _current_token.kind();

            // ================================================================
            // METHOD BODY CONTEXT - Prevent escaping method boundaries (CHECK FIRST - most specific)
            // ================================================================

            if (_parsing_method_body)
            {
                // When inside method body, only synchronize on method-safe recovery points
                // Never escape to class/struct level declarations

                LOG_DEBUG(LogComponent::PARSER, "[SYNC METHOD] Entered METHOD BODY CONTEXT at token '{}' (line {})",
                          std::string(_current_token.text()), _current_token.location().line());

                // Semicolon is a reliable statement boundary
                if (current_kind == TokenKind::TK_SEMICOLON)
                {
                    advance();
                    LOG_DEBUG(LogComponent::PARSER, "Method body synchronized on semicolon after skipping {} tokens", tokens_skipped);
                    return;
                }

                // Keywords that start new statements - good recovery points
                if (current_kind == TokenKind::TK_KW_IF || current_kind == TokenKind::TK_KW_WHILE ||
                    current_kind == TokenKind::TK_KW_FOR || current_kind == TokenKind::TK_KW_RETURN ||
                    current_kind == TokenKind::TK_KW_CONST || current_kind == TokenKind::TK_KW_MUT ||
                    current_kind == TokenKind::TK_KW_BREAK || current_kind == TokenKind::TK_KW_CONTINUE)
                {
                    // IMPORTANT: If we hit a statement keyword immediately without skipping any tokens,
                    // the caller likely can't parse this statement. Skip past it to make progress.
                    if (tokens_skipped == 0)
                    {
                        LOG_DEBUG(LogComponent::PARSER,
                                  "[SYNC METHOD] Found statement keyword '{}' immediately - skipping to prevent infinite loop",
                                  std::string(_current_token.text()));
                        advance();
                        tokens_skipped++;
                        continue;
                    }
                    LOG_DEBUG(LogComponent::PARSER, "Method body synchronized on statement keyword after skipping {} tokens", tokens_skipped);
                    return;
                }

                // Closing brace - this likely ends our method block, so return immediately
                // Let the parse_block_statement() that owns this brace consume it properly
                if (current_kind == TokenKind::TK_R_BRACE)
                {
                    LOG_DEBUG(LogComponent::PARSER, "Method body synchronized on closing brace after skipping {} tokens", tokens_skipped);
                    return;
                }

                // CRITICAL: Check if we've accidentally reached the NEXT method definition
                // Pattern: identifier '(' at method boundary is common, but we should be careful
                // about distinguishing method definitions from method calls.
                // For now, we'll be conservative and not try to detect method boundaries here
                // since we don't have easy access to the previous token context.

                // Continue consuming tokens within the method
                advance();
                tokens_skipped++;
                continue;
            }

            // ================================================================
            // CLASS/STRUCT MEMBER CONTEXT - Prevent escaping class boundaries
            // ================================================================

            if (_parsing_class_members && !_parsing_method_body)
            {
                // When inside class/struct member parsing (but NOT inside a method body),
                // synchronize on member-safe recovery points. Never escape to global scope.
                // Skip this entire section if we're inside a method body - the METHOD BODY CONTEXT
                // above already handled it.

                // CRITICAL: If we're inside parentheses (paren_depth > 0) or brackets (bracket_depth > 0),
                // we're likely in a method signature parsing parameters or array type. Don't stop at
                // identifier: patterns since those are parameters, not fields. Keep consuming until
                // we exit the nested context.
                if (_paren_depth > 0 || _bracket_depth > 0)
                {
                    // We're inside parentheses or brackets - likely in a method parameter list or array type
                    // Skip tokens until we're out of the nested context
                    if (current_kind == TokenKind::TK_R_PAREN && _paren_depth > 0)
                    {
                        // Don't consume the closing paren - let the caller handle it
                        LOG_DEBUG(LogComponent::PARSER, "Class member synchronized on ')' (end of params) after skipping {} tokens",
                                  tokens_skipped);
                        return;
                    }
                    if (current_kind == TokenKind::TK_R_SQUARE && _bracket_depth > 0)
                    {
                        // Don't consume the closing bracket - let the caller handle it
                        LOG_DEBUG(LogComponent::PARSER, "Class member synchronized on ']' (end of array) after skipping {} tokens",
                                  tokens_skipped);
                        return;
                    }
                    // Continue consuming tokens within the nested context
                    advance();
                    tokens_skipped++;
                    continue;
                }

                // Visibility modifiers are safe recovery points
                if (current_kind == TokenKind::TK_KW_PUBLIC ||
                    current_kind == TokenKind::TK_KW_PRIVATE ||
                    current_kind == TokenKind::TK_KW_PROTECTED)
                {
                    LOG_DEBUG(LogComponent::PARSER, "Class member synchronized on visibility modifier '{}' after skipping {} tokens",
                              std::string(_current_token.text()), tokens_skipped);
                    return;
                }

                // Closing brace likely ends the class/struct - don't consume it, let the loop handle it
                if (current_kind == TokenKind::TK_R_BRACE)
                {
                    LOG_DEBUG(LogComponent::PARSER, "Class member synchronized on closing brace (class end) after skipping {} tokens",
                              tokens_skipped);
                    return;
                }

                // Static keyword might start a static method
                if (current_kind == TokenKind::TK_KW_STATIC)
                {
                    LOG_DEBUG(LogComponent::PARSER, "Class member synchronized on 'static' keyword after skipping {} tokens",
                              tokens_skipped);
                    return;
                }

                // Tilde might start a destructor
                if (current_kind == TokenKind::TK_TILDE)
                {
                    LOG_DEBUG(LogComponent::PARSER, "Class member synchronized on destructor '~' after skipping {} tokens",
                              tokens_skipped);
                    return;
                }

                // Identifier might start a method or field - but need to look ahead
                // Method: identifier '(' or identifier ':' type '('
                // Field: identifier ':' type
                if (current_kind == TokenKind::TK_IDENTIFIER)
                {
                    // Look ahead to see if this looks like a method/field declaration
                    Token next_token = peek_next();

                    if (next_token.is(TokenKind::TK_L_PAREN))
                    {
                        // Pattern: identifier '(' - likely a method
                        LOG_DEBUG(LogComponent::PARSER, "Class member synchronized on method-like identifier '{}(' after skipping {} tokens",
                                  std::string(_current_token.text()), tokens_skipped);
                        return;
                    }
                    else if (next_token.is(TokenKind::TK_COLON))
                    {
                        // Pattern: identifier ':' - likely a field or method with type annotation
                        LOG_DEBUG(LogComponent::PARSER, "Class member synchronized on member-like identifier '{}:' after skipping {} tokens",
                                  std::string(_current_token.text()), tokens_skipped);
                        return;
                    }
                }

                // Continue consuming tokens within the class
                advance();
                tokens_skipped++;
                continue;
            }

            // ================================================================
            // GENERAL/GLOBAL CONTEXT - Original synchronization logic
            // ================================================================
            // Primary Recovery Points - Natural Statement Boundaries (Non-Method Context)
            // ================================================================

            // Statement terminators
            if (current_kind == TokenKind::TK_SEMICOLON)
            {
                advance(); // Consume the semicolon
                LOG_DEBUG(LogComponent::PARSER, "Synchronized on semicolon after skipping {} tokens", tokens_skipped);
                return;
            }

            // Block boundaries - handle closing braces carefully to avoid infinite loops
            if (current_kind == TokenKind::TK_R_BRACE)
            {
                if (tokens_skipped == 0)
                {
                    // We hit a closing brace immediately without consuming any tokens.
                    // This means the calling parser can't handle this brace, so we must consume it
                    // to prevent infinite loops, regardless of brace depth.
                    advance();
                    tokens_skipped++;
                    LOG_DEBUG(LogComponent::PARSER, "Consumed problematic closing brace to prevent infinite loop after skipping {} tokens", tokens_skipped);
                    continue;
                }
                else
                {
                    // We've consumed some tokens before hitting this brace - safe to return
                    LOG_DEBUG(LogComponent::PARSER, "Synchronized on closing brace after skipping {} tokens", tokens_skipped);
                    return;
                }
            }

            // ================================================================
            // Declaration Recovery Points
            // ================================================================

            // Top-level declarations (safe recovery points)
            if (is_top_level_declaration_start(current_kind))
            {
                LOG_DEBUG(LogComponent::PARSER, "Synchronized on top-level declaration '{}' after skipping {} tokens",
                          std::string(_current_token.text()), tokens_skipped);
                return;
            }

            // ================================================================
            // Statement Recovery Points (Context-Aware)
            // ================================================================

            // Control flow statements (safe within blocks)
            if (is_statement_start(current_kind))
            {
                // Only synchronize on statement starts if we're not deeply nested in expressions
                if (_paren_depth <= 0 && _bracket_depth <= 0)
                {
                    // Special case: don't synchronize on 'default' if it appears to be part of a destructor
                    // or other declaration (i.e., followed by a semicolon)
                    if (current_kind == TokenKind::TK_KW_DEFAULT)
                    {
                        // Look ahead to see if this is "default;" (part of destructor) vs standalone default
                        Token next_token = peek_next();
                        if (next_token.kind() == TokenKind::TK_SEMICOLON)
                        {
                            // This is likely "default;" in a destructor - don't synchronize here, keep advancing
                            advance();
                            tokens_skipped++;
                            continue;
                        }
                    }

                    // IMPORTANT: If we hit a statement start immediately without skipping any tokens,
                    // the caller likely can't parse this statement. Consuming and retrying will cause
                    // an infinite loop. Instead, skip past this statement start to make progress.
                    if (tokens_skipped == 0)
                    {
                        LOG_DEBUG(LogComponent::PARSER,
                                  "Synchronization found statement start '{}' immediately - skipping to prevent infinite loop",
                                  std::string(_current_token.text()));
                        advance();
                        tokens_skipped++;
                        // Continue looking for a better recovery point (like a semicolon)
                        continue;
                    }

                    LOG_DEBUG(LogComponent::PARSER, "Synchronized on statement start '{}' after skipping {} tokens",
                              std::string(_current_token.text()), tokens_skipped);
                    return;
                }
            }

            // ================================================================
            // Special Context Recovery
            // ================================================================

            // Implementation block boundaries
            if (_in_implementation_block && current_kind == TokenKind::TK_KW_IMPLEMENT)
            {
                LOG_DEBUG(LogComponent::PARSER, "Synchronized on implement block boundary after skipping {} tokens", tokens_skipped);
                return;
            }

            // Namespace boundaries (global scope recovery)
            if (is_global_scope() && current_kind == TokenKind::TK_KW_NAMESPACE)
            {
                LOG_DEBUG(LogComponent::PARSER, "Synchronized on namespace declaration after skipping {} tokens", tokens_skipped);
                return;
            }

            // ================================================================
            // Bracket Depth Recovery
            // ================================================================

            // This check is now handled in the closing brace section above

            // ================================================================
            // Emergency Recovery - Prevent Infinite Loops
            // ================================================================

            // Prevent excessive token skipping (likely indicates a deeper issue)
            if (tokens_skipped > 50)
            {
                LOG_WARN(LogComponent::PARSER, "Emergency synchronization: skipped {} tokens without finding recovery point", tokens_skipped);

                // Report a diagnostic about potential structural issues
                if (_diagnostics)
                {
                    Span span(_source_file, sync_start.line(), sync_start.column(),
                              _current_token.location().line(), _current_token.location().column());
                    _diagnostics->emit(
                        Diag::error(ErrorCode::E0115_PARSE_RECOVERY_FAILED, "parser unable to recover from syntax error")
                            .at(span)
                            .with_note("possible structural issue in code")
                            .help("check for unmatched braces, parentheses, or missing semicolons")
                            .with_note("Current nesting: " + std::to_string(_brace_depth) + " braces, " +
                                       std::to_string(_paren_depth) + " parentheses, " +
                                       std::to_string(_bracket_depth) + " brackets"));
                }

                // Force recovery at next reasonable boundary
                while (!is_at_end() && !is_forced_recovery_point(_current_token.kind()))
                {
                    advance();
                }
                return;
            }

            // ================================================================
            // Ensure Progress to Prevent Infinite Loops
            // ================================================================

            // Store current position before advancing
            Token current_before_advance = _current_token;

            // Continue advancing and tracking
            advance();
            tokens_skipped++;

            // Critical check: Ensure we're making progress compared to previous iteration
            if (_current_token.location().line() == current_before_advance.location().line() &&
                _current_token.location().column() == current_before_advance.location().column() &&
                _current_token.kind() == current_before_advance.kind())
            {
                LOG_ERROR(LogComponent::PARSER, "CRITICAL: Parser synchronization stuck at same token - forcing emergency exit");
                LOG_ERROR(LogComponent::PARSER, "Stuck token: '{}' at {}:{}",
                          std::string(_current_token.text()),
                          _current_token.location().line(),
                          _current_token.location().column());
                return; // Emergency exit to prevent infinite loop
            }

            // Update previous token for next iteration
            previous_token = current_before_advance;
        }

        // EOF reached during synchronization
        LOG_DEBUG(LogComponent::PARSER, "Synchronization reached EOF after skipping {} tokens (final depths: brace={}, paren={}, bracket={})",
                  tokens_skipped, _brace_depth, _paren_depth, _bracket_depth);
    }

    // ================================================================
    // Bracket Depth Management Methods
    // ================================================================

    void Parser::update_bracket_depth(TokenKind kind)
    {
        switch (kind)
        {
        case TokenKind::TK_L_BRACE:
            _brace_depth++;
            break;
        case TokenKind::TK_R_BRACE:
            _brace_depth--;
            break;
        case TokenKind::TK_L_PAREN:
            _paren_depth++;
            break;
        case TokenKind::TK_R_PAREN:
            _paren_depth--;
            break;
        case TokenKind::TK_L_SQUARE:
            _bracket_depth++;
            break;
        case TokenKind::TK_R_SQUARE:
            _bracket_depth--;
            break;
        default:
            // No bracket depth change for other tokens
            break;
        }

        // Log significant depth changes for debugging
        if (kind == TokenKind::TK_L_BRACE || kind == TokenKind::TK_R_BRACE)
        {
            LOG_TRACE(LogComponent::PARSER, "Brace depth changed to {} at {}:{}",
                      _brace_depth, _current_token.location().line(), _current_token.location().column());
        }
    }

    void Parser::reset_parsing_state()
    {
        _brace_depth = 0;
        _paren_depth = 0;
        _bracket_depth = 0;
        _tokens_consumed = 0;
        _scope_depth = 0;
        _in_implementation_block = false;
        _parsing_method_body = false;

        LOG_DEBUG(LogComponent::PARSER, "Parser state reset - all depths and counters cleared");
    }

    // ================================================================
    // Error Recovery Helper Methods
    // ================================================================

    bool Parser::is_top_level_declaration_start(TokenKind kind) const
    {
        // Context-aware check: private/public are only top-level declarations when at global scope
        if (kind == TokenKind::TK_KW_PUBLIC || kind == TokenKind::TK_KW_PRIVATE)
        {
            // If we're inside braces (struct/class/function), these are visibility modifiers, not top-level declarations
            return _brace_depth == 0;
        }

        // Context-aware check: static is only a top-level declaration when at global scope
        // Inside implementation blocks, static methods are not top-level declarations
        if (kind == TokenKind::TK_KW_STATIC)
        {
            return _brace_depth == 0;
        }

        return kind == TokenKind::TK_KW_FUNCTION ||
               kind == TokenKind::TK_KW_TYPE ||
               kind == TokenKind::TK_KW_CLASS ||
               kind == TokenKind::TK_KW_STRUCT ||
               kind == TokenKind::TK_KW_ENUM ||
               kind == TokenKind::TK_KW_TRAIT ||
               kind == TokenKind::TK_KW_IMPLEMENT ||
               kind == TokenKind::TK_KW_EXTERN ||
               kind == TokenKind::TK_KW_INTRINSIC ||
               kind == TokenKind::TK_KW_IMPORT ||
               kind == TokenKind::TK_KW_NAMESPACE ||
               kind == TokenKind::TK_KW_MODULE;
    }

    bool Parser::is_statement_start(TokenKind kind) const
    {
        return kind == TokenKind::TK_KW_CONST ||
               kind == TokenKind::TK_KW_MUT ||
               kind == TokenKind::TK_KW_IF ||
               kind == TokenKind::TK_KW_ELIF ||
               kind == TokenKind::TK_KW_ELSE ||
               kind == TokenKind::TK_KW_WHILE ||
               kind == TokenKind::TK_KW_FOR ||
               kind == TokenKind::TK_KW_MATCH ||
               kind == TokenKind::TK_KW_SWITCH ||
               kind == TokenKind::TK_KW_CASE ||
               kind == TokenKind::TK_KW_DEFAULT ||
               kind == TokenKind::TK_KW_RETURN ||
               kind == TokenKind::TK_KW_BREAK ||
               kind == TokenKind::TK_KW_CONTINUE ||
               kind == TokenKind::TK_KW_UNSAFE ||
               kind == TokenKind::TK_KW_WITH ||
               kind == TokenKind::TK_KW_YIELD;
    }

    bool Parser::is_forced_recovery_point(TokenKind kind) const
    {
        return kind == TokenKind::TK_KW_FUNCTION ||
               kind == TokenKind::TK_KW_CLASS ||
               kind == TokenKind::TK_KW_NAMESPACE ||
               kind == TokenKind::TK_KW_IMPORT ||
               kind == TokenKind::TK_EOF;
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
        // Keywords like 'string' are allowed as path segments
        if (_current_token.is(TokenKind::TK_COLONCOLON))
        {
            advance(); // consume '::'
            if (!_current_token.is_identifier() && !_current_token.is_keyword())
            {
                error("Expected identifier after '::'");
                return base_type;
            }

            std::string member_name = std::string(_current_token.text());
            advance();

            base_type = generate_scope_resolution_name(base_type, member_name);
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

        // Handle array types (e.g., i32[], str[][], u32[10], void[8])
        while (_current_token.is(TokenKind::TK_L_SQUARE))
        {
            consume(TokenKind::TK_L_SQUARE, "Expected '['");

            // Check for fixed-size array syntax [N]
            if (_current_token.is(TokenKind::TK_NUMERIC_CONSTANT))
            {
                std::string size = std::string(_current_token.text());
                advance(); // consume the number
                base_type += "[" + size + "]";
            }
            else
            {
                // Dynamic array []
                base_type += "[]";
            }

            consume(TokenKind::TK_R_SQUARE, "Expected ']'");
        }

        // Handle pointer types (type *)
        while (_current_token.is(TokenKind::TK_STAR))
        {
            advance(); // consume '*'
            base_type += "*";
        }

        return base_type;
    }

    TypeRef Parser::parse_type_annotation(std::string *out_type_string)
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

            std::string inner_type_string;
            TypeRef base_type = parse_type_annotation(&inner_type_string); // recursive call

            // Build the full type string including the reference
            if (out_type_string)
            {
                *out_type_string = is_mutable ? ("&mut " + inner_type_string) : ("&" + inner_type_string);
            }

            // For now, create reference type (could be enhanced later for mut references)
            return _context.types().get_reference_to(base_type);
        }

        // Use the new comprehensive token-based parsing system
        return parse_type_annotation_with_tokens(out_type_string);
    }

    // Namespace parsing
    std::unique_ptr<ModuleDeclarationNode> Parser::parse_namespace()
    {
        SourceLocation start_loc = _current_token.location();
        consume(TokenKind::TK_KW_NAMESPACE, "Expected 'namespace'");

        std::string namespace_name;
        SourceLocation name_loc = _current_token.location(); // Location of the first identifier

        // Parse namespace identifier (can be scoped like Std::Runtime)
        // Keywords like 'string' are allowed as namespace segments
        if (_current_token.is_identifier() || _current_token.is_keyword())
        {
            namespace_name = std::string(_current_token.text());
            advance();

            // Support :: separator for C++ style namespace scoping
            while (_current_token.is(TokenKind::TK_COLONCOLON))
            {
                advance(); // consume '::'
                // Allow both identifiers and keywords as namespace segments
                if (_current_token.is_identifier() || _current_token.is_keyword())
                {
                    namespace_name = generate_scope_resolution_name(namespace_name, std::string(_current_token.text()));
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

        auto node = std::make_unique<ModuleDeclarationNode>(start_loc, namespace_name, false);
        node->set_name_location(name_loc);
        return node;
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
        // Module declarations (public module syntax)
        else if (_current_token.is(TokenKind::TK_KW_PUBLIC) && peek_next().is(TokenKind::TK_KW_MODULE))
        {
            statement = parse_module_declaration();
        }
        else if (_current_token.is(TokenKind::TK_KW_MODULE))
        {
            statement = parse_module_declaration();
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
        else if (_current_token.is(TokenKind::TK_KW_LOOP))
        {
            statement = parse_loop_statement();
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
        else if (_current_token.is(TokenKind::TK_KW_UNSAFE))
        {
            statement = parse_unsafe_block_statement();
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

        // Debug: Log variable name parsing
        LOG_DEBUG(Cryo::LogComponent::PARSER, "VARDECL_DEBUG: Parsing variable declaration for name='{}', location={}:{}",
                  var_name, name_token.location().line(), name_token.location().column());

        // Parse required colon and type annotation
        consume(TokenKind::TK_COLON, "Expected ':' after variable name");
        SourceLocation type_loc = _current_token.location();
        std::string type_string;
        TypeRef var_type = parse_type_annotation(&type_string);

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

        // Create the variable declaration with the TypeAnnotation to preserve type info for resolution
        auto type_annotation = std::make_unique<TypeAnnotation>(
            TypeAnnotation::named(type_string, type_loc));
        auto var_decl = _builder.create_variable_declaration(start_loc, var_name, std::move(type_annotation), std::move(initializer), is_mutable, is_global);
        var_decl->set_name_location(name_token.location());
        attach_documentation(var_decl.get());

        // Also set the resolved type (may be an error type for generic types, will be resolved later)
        var_decl->set_resolved_type(var_type);

        // Debug: Log created variable declaration
        LOG_DEBUG(Cryo::LogComponent::PARSER, "VARDECL_DEBUG: Created VariableDeclarationNode for name='{}', node_ptr={}, stored_name='{}'",
                  var_name, static_cast<void *>(var_decl.get()), var_decl->name());

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

        // Parse function name - allow keywords as function names (e.g., some, none)
        Token name_token = _current_token;
        std::string func_name;
        if (_current_token.is(TokenKind::TK_IDENTIFIER))
        {
            func_name = std::string(name_token.text());
            advance();
        }
        else if (_current_token.is_keyword())
        {
            // Allow keywords as function names (e.g., some, none, as, etc.)
            func_name = std::string(name_token.text());
            advance();
        }
        else
        {
            error("Expected function name");
            return nullptr;
        }

        // Runtime function name transformation: main -> _user_main_
        // Only transform if this is not a stdlib module (not in "std" namespace) AND not in raw mode
        bool is_in_std_namespace = false;
        if (_srm_context)
        {
            // Check if we're in "std" namespace exactly OR any namespace starting with "std::"
            is_in_std_namespace = _srm_context->is_in_namespace("std") ||
                                  _current_namespace.starts_with("std::");
        }
        else
        {
            // Fallback to manual check
            is_in_std_namespace = _current_namespace.find("std::") != std::string::npos;
        }

        // Runtime function name transformation: main -> _user_main_
        // Currently disabled — Cryo Runtime is not yet implemented.
        // When the runtime is ready, re-enable this so the runtime's main()
        // can call _user_main_() after initialization.
        // if (func_name == "main" && !is_in_std_namespace && !_raw_mode)
        // {
        //     func_name = "_user_main_";
        // }

        // Create function declaration early so we can add generic parameters
        TypeRef void_type = resolve_type_from_string("void");
        auto func_decl = _builder.create_function_declaration(start_loc, func_name, void_type, is_public);
        func_decl->set_name_location(name_token.location());

        // Attach documentation if available
        attach_documentation(func_decl.get());

        // Parse optional generic parameters
        size_t func_generic_count = 0; // Track how many generics we add for cleanup
        if (_current_token.is(TokenKind::TK_L_ANGLE))
        {
            auto generics = parse_generic_parameters();
            for (auto &generic : generics)
            {
                // Track generic params so parameter/return type parsing can recognize them
                _current_generic_params.push_back(generic->name());
                func_generic_count++;
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
        TypeRef return_type = _context.types().get_void();
        std::string return_type_string = "void"; // Track the type string for TypeAnnotation
        SourceLocation return_type_loc;
        if (_current_token.is(TokenKind::TK_ARROW))
        {
            advance(); // consume '->'
            return_type_loc = _current_token.location();
            return_type = parse_type_annotation(&return_type_string);
        }

        // Update return type in the already created function declaration
        func_decl->set_resolved_return_type(return_type);

        // Set the return type annotation from the captured type string
        // This preserves the original type annotation (e.g., "Result<i8,ConversionError>") for later phases
        if (!return_type_string.empty())
        {
            auto annotation = std::make_unique<TypeAnnotation>(
                TypeAnnotation::named(return_type_string, return_type_loc));
            func_decl->set_return_type_annotation(std::move(annotation));
        }

        // Parse optional where clause
        if (_current_token.is(TokenKind::TK_KW_WHERE))
        {
            parse_where_clause(func_decl.get());
        }

        // Set variadic flag if detected
        func_decl->set_variadic(is_variadic);

        // For _user_main_, inject argc and argv parameters to match runtime's expected signature
        // The runtime declares: declare i32 @_user_main_(i32, ptr)
        if (func_name == "_user_main_")
        {
            // Create argc: i32 parameter
            TypeRef i32_type = _context.types().get_i32();
            auto argc_param = _builder.create_variable_declaration(start_loc, "argc", i32_type);
            func_decl->add_parameter(std::move(argc_param));

            // Create argv: ptr parameter (pointer to string array)
            TypeRef ptr_type = _context.types().get_pointer_to(_context.types().get_i8());
            auto argv_param = _builder.create_variable_declaration(start_loc, "argv", ptr_type);
            func_decl->add_parameter(std::move(argv_param));
        }

        // Add user-defined parameters to function
        for (auto &param : params)
        {
            func_decl->add_parameter(std::move(param));
        }

        // Parse function body
        auto body = parse_block_statement();
        func_decl->set_body(std::unique_ptr<BlockStatementNode>(
            dynamic_cast<BlockStatementNode *>(body.release())));

        // Clean up function's generic parameters from tracking
        for (size_t i = 0; i < func_generic_count; ++i)
        {
            if (!_current_generic_params.empty())
            {
                _current_generic_params.pop_back();
            }
        }

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
        TypeRef return_type = _context.types().get_void();
        std::string return_type_string = "void"; // Track the type string for TypeAnnotation
        SourceLocation return_type_loc;
        if (_current_token.is(TokenKind::TK_ARROW))
        {
            advance(); // consume '->'
            return_type_loc = _current_token.location();
            return_type = parse_type_annotation(&return_type_string);
        }

        // Create function declaration with type information (extern functions are public by default)
        auto func_decl = _builder.create_function_declaration(start_loc, func_name, return_type, true);

        // Set the return type annotation from the captured type string
        // This preserves the original type annotation (e.g., "Result<i8,ConversionError>") for later phases
        if (!return_type_string.empty())
        {
            auto annotation = std::make_unique<TypeAnnotation>(
                TypeAnnotation::named(return_type_string, return_type_loc));
            func_decl->set_return_type_annotation(std::move(annotation));
        }

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

    std::unique_ptr<DeclarationNode> Parser::parse_intrinsic_declaration()
    {
        SourceLocation start_loc = _current_token.location();

        // Parse 'intrinsic' keyword
        consume(TokenKind::TK_KW_INTRINSIC, "Expected 'intrinsic'");

        // Check if this is 'intrinsic const' or 'intrinsic function'
        if (_current_token.is(TokenKind::TK_KW_CONST))
        {
            return parse_intrinsic_const_declaration(start_loc);
        }
        else if (_current_token.is(TokenKind::TK_KW_FUNCTION))
        {
            // Parse 'function' keyword
            consume(TokenKind::TK_KW_FUNCTION, "Expected 'function' after 'intrinsic'");
        }
        else
        {
            throw ParseError("Expected 'function' or 'const' after 'intrinsic'", _current_token.location());
        }

        // Parse function name - allow keywords as intrinsic names since they map to C functions
        // which may have names that conflict with language keywords (e.g., raise, signal, read, write)
        Token name_token = _current_token;
        std::string func_name;
        if (_current_token.is(TokenKind::TK_IDENTIFIER))
        {
            func_name = std::string(name_token.text());
            advance();
        }
        else if (_current_token.is_keyword())
        {
            // Allow keywords as intrinsic function names (e.g., raise, signal, read, write, open, close)
            func_name = std::string(name_token.text());
            advance();
        }
        else
        {
            error("Expected function name");
            return nullptr;
        }

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
        TypeRef return_type = _context.types().get_void();
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

        return std::move(intrinsic_decl);
    }

    std::unique_ptr<IntrinsicConstDeclarationNode> Parser::parse_intrinsic_const_declaration(SourceLocation start_loc)
    {
        // Parse 'const' keyword
        consume(TokenKind::TK_KW_CONST, "Expected 'const' after 'intrinsic'");

        // Parse constant name
        Token name_token = consume(TokenKind::TK_IDENTIFIER, "Expected constant name");
        std::string const_name = std::string(name_token.text());

        // Parse type annotation
        consume(TokenKind::TK_COLON, "Expected ':' after intrinsic constant name");
        TypeRef const_type = parse_type_annotation();

        // Create intrinsic const declaration
        auto intrinsic_const = std::make_unique<IntrinsicConstDeclarationNode>(start_loc, const_name, const_type);

        // End with semicolon
        consume(TokenKind::TK_SEMICOLON, "Expected ';' after intrinsic constant declaration");

        return intrinsic_const;
    }

    std::unique_ptr<ImportDeclarationNode> Parser::parse_import_declaration()
    {
        SourceLocation start_loc = _current_token.location();

        // Parse 'import' keyword
        consume(TokenKind::TK_KW_IMPORT, "Expected 'import'");

        // Check for different import patterns:
        // 1. import * from core::stdio;            (wildcard with explicit from)
        // 2. import IO from core::stdio;           (specific import with from)
        // 3. import IO, Function from core::stdio; (multiple specific imports)
        // 4. import core::option;                  (wildcard module import)
        // 5. import core::option as Option;        (wildcard with alias)

        std::vector<std::string> specific_imports;
        std::string module_path;
        std::string alias;
        bool has_alias = false;
        bool using_from_syntax = false;
        SourceLocation module_path_loc; // Track start of the module path for LSP

        // Check if we have a wildcard (*) or specific imports before 'from'
        if (_current_token.is(TokenKind::TK_STAR))
        {
            // import * from core::option;
            advance(); // consume '*'

            if (!_current_token.is(TokenKind::TK_KW_FROM))
            {
                throw ParseError("Expected 'from' after '*' in import statement", _current_token.location());
            }

            advance(); // consume 'from'
            using_from_syntax = true;
        }
        else if (_current_token.is(TokenKind::TK_IDENTIFIER))
        {
            // Could be: import IO from core::stdio; or import IO, Function from core::stdio; or import core::option;
            SourceLocation first_ident_loc = _current_token.location();
            std::string first_identifier = std::string(_current_token.text());
            advance(); // consume identifier

            // Check if we have a comma (multiple imports) or 'from' (specific import) or '::' (module path)
            if (_current_token.is(TokenKind::TK_COMMA) || _current_token.is(TokenKind::TK_KW_FROM))
            {
                // This is specific import syntax: import IO from core::stdio;
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
            else if (_current_token.is(TokenKind::TK_COLONCOLON))
            {
                // This is module path syntax: import core::option;
                module_path = first_identifier;
                module_path_loc = first_ident_loc;
                // Continue parsing the module path
            }
            else if (_current_token.is(TokenKind::TK_SEMICOLON) || _current_token.is(TokenKind::TK_KW_AS))
            {
                // Simple bare module import: import Foo;  or  import Foo as Bar;
                module_path = first_identifier;
                module_path_loc = first_ident_loc;
            }
            else
            {
                throw ParseError("Expected '::' for module path, 'from' for specific imports, or ',' for multiple imports", _current_token.location());
            }
        }
        else
        {
            throw ParseError("Expected module path or symbol name after 'import'", _current_token.location());
        }

        // Parse module path (required for all imports)
        if (module_path.empty())
        {
            // We need to parse a module path starting with identifier
            if (!_current_token.is(TokenKind::TK_IDENTIFIER))
            {
                throw ParseError("Expected module path after 'from' or 'import'", _current_token.location());
            }

            module_path_loc = _current_token.location();
            module_path = std::string(_current_token.text());
            advance(); // consume first identifier
        }

        // Parse remaining module path segments (identifier::identifier::...)
        // Note: Keywords like 'string' are allowed as module path segments
        while (_current_token.is(TokenKind::TK_COLONCOLON))
        {
            advance(); // consume '::'

            // Check for multi-import syntax: import Foo::Bar::{A, B}
            if (_current_token.is(TokenKind::TK_L_BRACE))
            {
                advance(); // consume '{'
                std::vector<std::string> multi_imports;

                while (!_current_token.is(TokenKind::TK_R_BRACE))
                {
                    if (!_current_token.is_identifier() && !_current_token.is_keyword())
                    {
                        throw ParseError("Expected identifier in import list", _current_token.location());
                    }
                    multi_imports.push_back(std::string(_current_token.text()));
                    advance();
                    if (_current_token.is(TokenKind::TK_COMMA))
                        advance(); // consume comma
                }
                advance(); // consume '}'

                // Parse optional ';'
                if (_current_token.is(TokenKind::TK_SEMICOLON))
                    advance();

                // Create SpecificImport node with module_path as the base path
                auto import_node = std::make_unique<ImportDeclarationNode>(
                    start_loc, std::move(multi_imports), module_path);
                import_node->set_name_location(module_path_loc);
                return import_node;
            }

            // Accept both identifiers and keywords as valid path segments
            if (!_current_token.is_identifier() && !_current_token.is_keyword())
            {
                throw ParseError("Expected identifier after '::' in module path", _current_token.location());
            }
            module_path += "::" + std::string(_current_token.text());
            advance(); // consume the identifier/keyword
        }

        // Parse optional 'as' alias (only for wildcard imports)
        if (_current_token.is(TokenKind::TK_KW_AS) && !using_from_syntax)
        {
            advance(); // consume 'as'
            Token alias_token = consume(TokenKind::TK_IDENTIFIER, "Expected identifier after 'as'");
            alias = std::string(alias_token.text());
            has_alias = true;
        }

        // Parse optional ';'
        if (_current_token.is(TokenKind::TK_SEMICOLON))
        {
            advance(); // consume ';'
        }

        // Create the appropriate import node
        if (!specific_imports.empty())
        {
            // Specific import: import IO from core::stdio;
            auto import_node = std::make_unique<ImportDeclarationNode>(start_loc, std::move(specific_imports), module_path);
            import_node->set_name_location(module_path_loc);
            return import_node;
        }
        else if (has_alias)
        {
            // Import with alias: import core::option as Option;
            auto import_node = std::make_unique<ImportDeclarationNode>(start_loc, module_path, alias);
            import_node->set_name_location(module_path_loc);
            return import_node;
        }
        else
        {
            // Wildcard import: import core::option;
            auto import_node = std::make_unique<ImportDeclarationNode>(start_loc, module_path);
            import_node->set_name_location(module_path_loc);
            return import_node;
        }
    }

    std::unique_ptr<ModuleDeclarationNode> Parser::parse_module_declaration()
    {
        SourceLocation start_loc = _current_token.location();
        bool is_public = false;

        // Check for 'public' keyword
        if (_current_token.is(TokenKind::TK_KW_PUBLIC))
        {
            is_public = true;
            advance(); // consume 'public'
        }

        // Parse 'module' keyword
        consume(TokenKind::TK_KW_MODULE, "Expected 'module'");

        // Parse module path (e.g., alloc::global)
        // Note: Keywords like 'string' are allowed as module path segments
        if (!_current_token.is_identifier() && !_current_token.is_keyword())
        {
            throw ParseError("Expected module path after 'module'", _current_token.location());
        }

        SourceLocation module_path_loc = _current_token.location();
        std::string module_path = std::string(_current_token.text());
        advance(); // consume first identifier/keyword

        // Parse remaining module path segments (identifier::identifier::...)
        // Keywords are allowed as path segments (e.g., collections::string)
        while (_current_token.is(TokenKind::TK_COLONCOLON))
        {
            advance(); // consume '::'
            // Accept both identifiers and keywords as valid path segments
            if (!_current_token.is_identifier() && !_current_token.is_keyword())
            {
                throw ParseError("Expected identifier after '::' in module path", _current_token.location());
            }
            module_path += "::" + std::string(_current_token.text());
            advance(); // consume the identifier/keyword
        }

        // Parse optional ';'
        if (_current_token.is(TokenKind::TK_SEMICOLON))
        {
            advance(); // consume ';'
        }

        auto module_node = std::make_unique<ModuleDeclarationNode>(start_loc, module_path, is_public);
        module_node->set_name_location(module_path_loc);
        return module_node;
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

        // Semicolon is optional if the expression ended with '}' (struct literal, block, match, etc.)
        // This allows: return StructName { field: value }
        // The closing } of the struct literal is followed by } of the function body
        if (_current_token.is(TokenKind::TK_SEMICOLON))
        {
            advance(); // consume optional ';'
        }
        else if (!_current_token.is(TokenKind::TK_R_BRACE))
        {
            // If not followed by ; or }, that's an error
            error("Expected ';' after return statement");
        }

        return _builder.create_return_statement(start_loc, std::move(expr));
    }

    std::unique_ptr<BlockStatementNode> Parser::parse_block_statement()
    {
        SourceLocation start_loc = _current_token.location();
        LOG_DEBUG(LogComponent::PARSER, "[BLOCK] Starting parse_block_statement at line {}, token '{}'",
                  start_loc.line(), std::string(_current_token.text()));
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

                // Emit recovery details to help debug truncated method bodies
                LOG_WARN(LogComponent::PARSER,
                         "[RECOVERY] parse_block_statement caught error: {} at line {} (token='{}', _parsing_method_body={})",
                         e.what(), _current_token.location().line(), std::string(_current_token.text()),
                         _parsing_method_body);
                // Enhanced error recovery: skip to next statement boundary
                // Try to find the end of the current statement (semicolon) or a statement keyword
                synchronize();

                // Additional recovery: if we're still in the middle of something,
                // skip tokens until we find a semicolon (statement terminator)
                while (!is_at_end() && !_current_token.is(TokenKind::TK_SEMICOLON) &&
                       !_current_token.is(TokenKind::TK_R_BRACE) && !_current_token.is(TokenKind::TK_KW_IF) &&
                       !_current_token.is(TokenKind::TK_KW_WHILE) && !_current_token.is(TokenKind::TK_KW_FOR) &&
                       !_current_token.is(TokenKind::TK_KW_RETURN) && !_current_token.is(TokenKind::TK_KW_CONST) &&
                       !_current_token.is(TokenKind::TK_KW_MUT))
                {
                    advance();
                }

                // Consume the semicolon if we found it
                if (_current_token.is(TokenKind::TK_SEMICOLON))
                {
                    advance();
                }

                // Method-body specific recovery: if we still haven't reached a closing brace,
                // aggressively seek to the matching '}' so we don't treat the next method
                // signature as a statement inside the current method.
                if (_parsing_method_body && !_current_token.is(TokenKind::TK_R_BRACE))
                {
                    int brace_balance = 0;
                    size_t tokens_scanned = 0;

                    while (!is_at_end() && tokens_scanned < 2048)
                    {
                        if (_current_token.is(TokenKind::TK_L_BRACE))
                        {
                            brace_balance++;
                        }
                        else if (_current_token.is(TokenKind::TK_R_BRACE))
                        {
                            if (brace_balance == 0)
                            {
                                break; // Found the closing brace for the current method body
                            }
                            brace_balance--;
                        }

                        advance();
                        tokens_scanned++;
                    }
                }
            }
        }

        LOG_DEBUG(LogComponent::PARSER, "[BLOCK] About to consume closing brace, current token: '{}' at line {}",
                  std::string(_current_token.text()), _current_token.location().line());
        consume(TokenKind::TK_R_BRACE, "Expected '}'");
        LOG_DEBUG(LogComponent::PARSER, "[BLOCK] Successfully consumed closing brace, now at token: '{}' at line {}",
                  std::string(_current_token.text()), _current_token.location().line());

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
        auto expr = parse_binary_expression(0);

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

    // Precedence table for binary operators (higher = tighter binding).
    // Returns -1 for tokens that are not binary operators.
    int Parser::get_binary_precedence(TokenKind kind)
    {
        switch (kind)
        {
        case TokenKind::TK_PIPEPIPE:        return 1;  // ||
        case TokenKind::TK_AMPAMP:          return 2;  // &&
        case TokenKind::TK_PIPE:            return 3;  // |
        case TokenKind::TK_CARET:           return 4;  // ^
        case TokenKind::TK_AMP:             return 5;  // &
        case TokenKind::TK_EQUALEQUAL:      return 6;  // ==
        case TokenKind::TK_EXCLAIMEQUAL:    return 6;  // !=
        case TokenKind::TK_L_ANGLE:         return 7;  // <
        case TokenKind::TK_R_ANGLE:         return 7;  // >
        case TokenKind::TK_LESSEQUAL:       return 7;  // <=
        case TokenKind::TK_GREATEREQUAL:    return 7;  // >=
        case TokenKind::TK_LESSLESS:        return 8;  // <<
        case TokenKind::TK_GREATERGREATER:  return 8;  // >>
        case TokenKind::TK_PLUS:            return 9;  // +
        case TokenKind::TK_MINUS:           return 9;  // -
        case TokenKind::TK_STAR:            return 10; // *
        case TokenKind::TK_SLASH:           return 10; // /
        case TokenKind::TK_PERCENT:         return 10; // %
        default:                            return -1; // not a binary operator
        }
    }

    // Pratt (precedence-climbing) parser for all binary operators.
    // Replaces the 10 separate recursive-descent functions (parse_logical_or
    // through parse_multiplicative), collapsing them into a single iterative
    // loop. This reduces per-expression stack depth from ~16 frames to ~4.
    std::unique_ptr<ExpressionNode> Parser::parse_binary_expression(int min_precedence)
    {
        auto expr = parse_cast();

        while (true)
        {
            int prec = get_binary_precedence(_current_token.kind());
            if (prec < min_precedence)
                break;

            Token op = _current_token;
            advance();

            // All binary operators are left-associative, so the right operand
            // binds at one precedence level higher than the current operator.
            auto right = parse_binary_expression(prec + 1);
            expr = _builder.create_binary_expression(op, std::move(expr), std::move(right));
        }

        return expr;
    }

    std::unique_ptr<ExpressionNode> Parser::parse_cast()
    {
        auto expr = parse_unary();

        // Handle type casts: expression as TargetType
        while (_current_token.is(TokenKind::TK_KW_AS))
        {
            SourceLocation cast_loc = _current_token.location();
            advance(); // consume 'as'

            // Parse the target type with better error recovery
            if (!_current_token.is(TokenKind::TK_IDENTIFIER) && !is_type_token())
            {
                // Instead of returning nullptr, create an error recovery cast node
                // This prevents cascading parser failures in method bodies
                error("Expected type name after 'as' in cast expression");

                // Error recovery: create a cast to 'unknown' type to allow parsing to continue
                auto unknown_annotation = std::make_unique<TypeAnnotation>(TypeAnnotation::named("unknown", cast_loc));
                expr = _builder.create_cast_expression(cast_loc, std::move(expr), std::move(unknown_annotation));
                break; // Exit the while loop to prevent further parsing issues
            }

            std::string target_type = std::string(_current_token.text());
            advance(); // consume type name

            // Handle generic types like Array<T> or Map<K, V> with error recovery
            if (_current_token.is(TokenKind::TK_L_ANGLE))
            {
                try
                {
                    target_type += parse_generic_type_suffix();
                }
                catch (const ParseError &e)
                {
                    // If generic type parsing fails, continue with just the base type
                    // This prevents method body parsing from completely failing
                    error("Failed to parse generic type suffix in cast expression");
                }
            }

            // Handle pointer types like int* or char**
            while (_current_token.is(TokenKind::TK_STAR))
            {
                target_type += "*";
                advance();
            }

            // Create TypeAnnotation from the target type string
            auto target_annotation = std::make_unique<TypeAnnotation>(TypeAnnotation::named(target_type, cast_loc));
            expr = _builder.create_cast_expression(cast_loc, std::move(expr), std::move(target_annotation));
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
                else if (_current_token.is(TokenKind::TK_PERIOD) || _current_token.is(TokenKind::TK_ARROW))
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

        // If-expression: if (condition) { expr } else { expr }
        if (_current_token.is(TokenKind::TK_KW_IF))
        {
            return parse_if_expression();
        }

        // Match-expression: match (expr) { pattern => { expr } ... }
        if (_current_token.is(TokenKind::TK_KW_MATCH))
        {
            return parse_match_expression();
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
                else if (_current_token.is(TokenKind::TK_PERIOD) || _current_token.is(TokenKind::TK_ARROW))
                {
                    // Member access
                    expr = parse_member_access(std::move(expr));
                }
                else if (_current_token.is(TokenKind::TK_PLUSPLUS) || _current_token.is(TokenKind::TK_MINUSMINUS))
                {
                    // Postfix increment/decrement (e.g., this.value++)
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

            return std::move(expr);
        }

        // Primitive type constructors (e.g., i64(value), i32(value), f64(value))
        // or primitive type scope resolution (e.g., i64::max_value(), u8::MIN)
        if (is_primitive_type_token())
        {
            Token next_tok = peek_next();
            // Look ahead to see if this is followed by parentheses or scope resolution
            if (next_tok.is(TokenKind::TK_L_PAREN) || next_tok.is(TokenKind::TK_COLONCOLON))
            {
                // This is a primitive constructor call like i64(value) or scope resolution like i64::max_value()
                std::string type_name = std::string(_current_token.text());
                SourceLocation type_location = _current_token.location();
                advance(); // consume the type name (e.g., 'i64', 'f64')

                // Create an identifier node with the type name
                Token type_token(TokenKind::TK_IDENTIFIER, type_name, type_location);
                std::unique_ptr<ExpressionNode> expr = _builder.create_identifier_node(type_token);

                // Handle scope resolution (e.g., i64::max_value())
                while (_current_token.is(TokenKind::TK_COLONCOLON))
                {
                    advance(); // consume '::'

                    if (!_current_token.is(TokenKind::TK_IDENTIFIER) && !_current_token.is_keyword())
                    {
                        error("Expected identifier after '::'");
                        return nullptr;
                    }

                    std::string member_name = std::string(_current_token.text());
                    SourceLocation loc;

                    if (auto scope_node = dynamic_cast<ScopeResolutionNode *>(expr.get()))
                    {
                        std::string full_scope = generate_scope_resolution_name(scope_node->scope_name(), scope_node->member_name());
                        loc = scope_node->location();
                        expr = _builder.create_scope_resolution(loc, full_scope, member_name);
                    }
                    else if (auto identifier_node = dynamic_cast<IdentifierNode *>(expr.get()))
                    {
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

                // Handle postfix operations (function calls, etc.)
                while (true)
                {
                    if (_current_token.is(TokenKind::TK_L_PAREN))
                    {
                        expr = parse_call_expression(std::move(expr));
                    }
                    else if (_current_token.is(TokenKind::TK_L_SQUARE))
                    {
                        expr = parse_array_access(std::move(expr));
                    }
                    else if (_current_token.is(TokenKind::TK_PERIOD) || _current_token.is(TokenKind::TK_ARROW))
                    {
                        expr = parse_member_access(std::move(expr));
                    }
                    else
                    {
                        break;
                    }
                }

                return expr;
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
                    std::string type_name = identifier_node->name();
                    SourceLocation type_location = identifier_node->location();

                    // Use lookahead-based detection: scan ahead to see if pattern matches <...>(
                    // This properly handles cases like:
                    //   func<T>()  -> generic call (pattern: <type_args>()
                    //   i < n      -> comparison (no matching > followed by ()
                    bool looks_like_generic = is_generic_call_ahead();

                    if (looks_like_generic)
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

                            std::string type_arg = std::string(_current_token.text());
                            advance(); // consume type argument

                            // Handle nested generic types (e.g., HashMap<string, Option<T>>)
                            if (_current_token.is(TokenKind::TK_L_ANGLE))
                            {
                                type_arg += "<";
                                advance();
                                int nested_depth = 1;
                                while (nested_depth > 0 && !is_at_end())
                                {
                                    if (_current_token.is(TokenKind::TK_L_ANGLE))
                                        nested_depth++;
                                    else if (_current_token.is(TokenKind::TK_R_ANGLE))
                                        nested_depth--;
                                    else if (_current_token.is(TokenKind::TK_GREATERGREATER))
                                    {
                                        // >> is two closing angle brackets
                                        nested_depth -= 2;
                                    }
                                    if (nested_depth > 0)
                                    {
                                        type_arg += std::string(_current_token.text());
                                        advance();
                                    }
                                    else
                                    {
                                        // Consume the final '>'
                                        type_arg += ">";
                                        advance();
                                    }
                                }
                            }

                            // Handle pointer modifiers (e.g., Array<Expr*>)
                            while (_current_token.is(TokenKind::TK_STAR))
                            {
                                type_arg += "*";
                                advance();
                            }

                            // Handle array modifiers (e.g., HashMap<string, SymbolID[]>)
                            while (_current_token.is(TokenKind::TK_L_SQUARE))
                            {
                                type_arg += "[";
                                advance();
                                // Optionally collect a size (e.g., Type[10])
                                if (_current_token.is(TokenKind::TK_NUMERIC_CONSTANT))
                                {
                                    type_arg += std::string(_current_token.text());
                                    advance();
                                }
                                if (_current_token.is(TokenKind::TK_R_SQUARE))
                                {
                                    type_arg += "]";
                                    advance();
                                }
                            }

                            generic_args.push_back(type_arg);

                        } while (match(TokenKind::TK_COMMA));

                        consume(TokenKind::TK_R_ANGLE, "Expected '>' after generic arguments");

                        // Check if this is followed by parentheses (call) or scope resolution
                        if (_current_token.is(TokenKind::TK_L_PAREN))
                        {
                            // Determine if this is a struct constructor (uppercase) or function call (lowercase)
                            bool is_type_constructor = !type_name.empty() && std::isupper(type_name[0]);

                            if (is_type_constructor)
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
                                            // Check for trailing comma - if we see '}' after comma, break out
                                            if (_current_token.is(TokenKind::TK_R_BRACE))
                                            {
                                                break;
                                            }

                                            // Parse field name (allow keywords as field names)
                                            if (!_current_token.is(TokenKind::TK_IDENTIFIER) && !_current_token.is_keyword())
                                            {
                                                error("Expected field name in struct literal");
                                                return nullptr;
                                            }

                                            std::string field_name = std::string(_current_token.text());
                                            auto field_name_loc = _current_token.location();
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
                                            auto field_init = std::make_unique<FieldInitializerNode>(field_name, std::move(field_value), field_name_loc);
                                            struct_literal->add_field_initializer(std::move(field_init));

                                            // Continue if comma or another field (identifier:)
                                        } while (match(TokenKind::TK_COMMA) ||
                                                 ((_current_token.is(TokenKind::TK_IDENTIFIER) || _current_token.is_keyword()) &&
                                                  peek_next().is(TokenKind::TK_COLON)));
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
                            else
                            {
                                // Generic function call (lowercase identifier) - create CallExpressionNode
                                auto call_expr = _builder.create_call_expression(type_location, std::move(expr));

                                // Add generic arguments to call expression
                                for (const auto &generic_arg : generic_args)
                                {
                                    call_expr->add_generic_arg(generic_arg);
                                }

                                advance(); // consume '('

                                // Parse call arguments (if any)
                                if (!_current_token.is(TokenKind::TK_R_PAREN))
                                {
                                    do
                                    {
                                        auto arg = parse_expression();
                                        if (!arg)
                                        {
                                            error("Expected expression in function call arguments");
                                            return nullptr;
                                        }
                                        call_expr->add_argument(std::move(arg));

                                    } while (match(TokenKind::TK_COMMA));
                                }

                                consume(TokenKind::TK_R_PAREN, "Expected ')' after function call arguments");
                                expr = std::move(call_expr);
                            }
                        }
                        else if (_current_token.is(TokenKind::TK_COLONCOLON))
                        {
                            // Generic type with scope resolution: Array<String>::new()
                            advance(); // consume '::'

                            if (!_current_token.is(TokenKind::TK_IDENTIFIER) && !_current_token.is_keyword())
                            {
                                error("Expected identifier after '::'");
                                return nullptr;
                            }

                            std::string member_name = std::string(_current_token.text());
                            advance();

                            // Create ScopeResolutionNode with generic_args for proper type resolution
                            expr = _builder.create_scope_resolution(type_location, type_name, member_name, generic_args);
                        }
                        else if (_current_token.is(TokenKind::TK_L_BRACE))
                        {
                            // Generic struct literal: Type<T> { field: value, ... }
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
                                    // Check for trailing comma - if we see '}' after comma, break out
                                    if (_current_token.is(TokenKind::TK_R_BRACE))
                                    {
                                        break;
                                    }

                                    // Parse field name (allow keywords as field names)
                                    if (!_current_token.is(TokenKind::TK_IDENTIFIER) && !_current_token.is_keyword())
                                    {
                                        error("Expected field name in struct literal");
                                        return nullptr;
                                    }

                                    std::string field_name = std::string(_current_token.text());
                                    auto field_name_loc = _current_token.location();
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
                                    auto field_init = std::make_unique<FieldInitializerNode>(field_name, std::move(field_value), field_name_loc);
                                    struct_literal->add_field_initializer(std::move(field_init));

                                    // Continue if comma or another field (identifier:)
                                } while (match(TokenKind::TK_COMMA) ||
                                         ((_current_token.is(TokenKind::TK_IDENTIFIER) || _current_token.is_keyword()) &&
                                          peek_next().is(TokenKind::TK_COLON)));
                            }

                            consume(TokenKind::TK_R_BRACE, "Expected '}' after struct literal fields");

                            expr = std::move(struct_literal);
                        }
                        else
                        {
                            // Not a constructor call, struct literal, or scope resolution, this is an error for now
                            error("Generic type expressions must be followed by constructor call '()', struct literal '{...}', or scope resolution '::'");
                            return nullptr;
                        }
                    }
                    // else: not a generic pattern, treat '<' as less-than operator (handled by caller)
                }
                else if (auto scope_node = dynamic_cast<ScopeResolutionNode *>(expr.get()))
                {
                    // Handle qualified generic types like Containers::Box<int>::new(10)
                    std::string type_name = generate_scope_resolution_name(scope_node->scope_name(), scope_node->member_name());
                    SourceLocation type_location = scope_node->location();

                    bool looks_like_generic = is_generic_call_ahead();

                    if (looks_like_generic)
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

                            std::string type_arg = std::string(_current_token.text());
                            advance(); // consume type argument

                            // Handle nested generic types (e.g., HashMap<string, Option<T>>)
                            if (_current_token.is(TokenKind::TK_L_ANGLE))
                            {
                                type_arg += "<";
                                advance();
                                int nested_depth = 1;
                                while (nested_depth > 0 && !is_at_end())
                                {
                                    if (_current_token.is(TokenKind::TK_L_ANGLE))
                                        nested_depth++;
                                    else if (_current_token.is(TokenKind::TK_R_ANGLE))
                                        nested_depth--;
                                    else if (_current_token.is(TokenKind::TK_GREATERGREATER))
                                        nested_depth -= 2;
                                    if (nested_depth > 0)
                                    {
                                        type_arg += std::string(_current_token.text());
                                        advance();
                                    }
                                    else
                                    {
                                        type_arg += ">";
                                        advance();
                                    }
                                }
                            }

                            // Handle pointer modifiers (e.g., Array<Expr*>)
                            while (_current_token.is(TokenKind::TK_STAR))
                            {
                                type_arg += "*";
                                advance();
                            }

                            // Handle array modifiers (e.g., HashMap<string, SymbolID[]>)
                            while (_current_token.is(TokenKind::TK_L_SQUARE))
                            {
                                type_arg += "[";
                                advance();
                                if (_current_token.is(TokenKind::TK_NUMERIC_CONSTANT))
                                {
                                    type_arg += std::string(_current_token.text());
                                    advance();
                                }
                                if (_current_token.is(TokenKind::TK_R_SQUARE))
                                {
                                    type_arg += "]";
                                    advance();
                                }
                            }

                            generic_args.push_back(type_arg);

                        } while (match(TokenKind::TK_COMMA));

                        consume(TokenKind::TK_R_ANGLE, "Expected '>' after generic arguments");

                        // Check if this is followed by parentheses (call) or scope resolution
                        if (_current_token.is(TokenKind::TK_L_PAREN))
                        {
                            // Generic constructor call: Containers::Box<int>(10)
                            bool is_type_constructor = !type_name.empty() && std::isupper(scope_node->member_name()[0]);

                            if (is_type_constructor)
                            {
                                advance(); // consume '('
                                auto new_expr = _builder.create_new_expression(type_location, type_name);
                                for (const auto &generic_arg : generic_args)
                                {
                                    new_expr->add_generic_arg(generic_arg);
                                }

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
                            else
                            {
                                // Generic function call
                                auto call_expr = _builder.create_call_expression(type_location, std::move(expr));
                                for (const auto &generic_arg : generic_args)
                                {
                                    call_expr->add_generic_arg(generic_arg);
                                }
                                advance(); // consume '('
                                if (!_current_token.is(TokenKind::TK_R_PAREN))
                                {
                                    do
                                    {
                                        auto arg = parse_expression();
                                        if (!arg)
                                        {
                                            error("Expected expression in function call arguments");
                                            return nullptr;
                                        }
                                        call_expr->add_argument(std::move(arg));
                                    } while (match(TokenKind::TK_COMMA));
                                }
                                consume(TokenKind::TK_R_PAREN, "Expected ')' after function call arguments");
                                expr = std::move(call_expr);
                            }
                        }
                        else if (_current_token.is(TokenKind::TK_COLONCOLON))
                        {
                            // Generic type with scope resolution: Containers::Box<int>::new()
                            advance(); // consume '::'

                            if (!_current_token.is(TokenKind::TK_IDENTIFIER) && !_current_token.is_keyword())
                            {
                                error("Expected identifier after '::'");
                                return nullptr;
                            }

                            std::string member_name = std::string(_current_token.text());
                            advance();

                            // Create ScopeResolutionNode with the full qualified type name and generic_args
                            expr = _builder.create_scope_resolution(type_location, type_name, member_name, generic_args);
                        }
                        else
                        {
                            error("Generic type expressions must be followed by constructor call '()', struct literal '{...}', or scope resolution '::'");
                            return nullptr;
                        }
                    }
                    // else: not a generic pattern, treat '<' as less-than operator
                }
            }

            // Handle multi-level scope resolution (e.g., Std::Runtime::function)
            while (_current_token.is(TokenKind::TK_COLONCOLON))
            {
                advance(); // consume '::'

                // Allow both identifiers and keywords after :: (for things like T::default())
                if (!_current_token.is(TokenKind::TK_IDENTIFIER) && !_current_token.is_keyword())
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
                    std::string full_scope = generate_scope_resolution_name(scope_node->scope_name(), scope_node->member_name());
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
                else if (_current_token.is(TokenKind::TK_PERIOD) || _current_token.is(TokenKind::TK_ARROW))
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
                else if (_current_token.is(TokenKind::TK_L_BRACE))
                {
                    // Struct literal initialization: TypeName { field: value, ... }
                    // Only valid if expr is an identifier or scope resolution
                    std::string struct_name;
                    SourceLocation literal_location;

                    if (auto identifier = dynamic_cast<IdentifierNode *>(expr.get()))
                    {
                        struct_name = identifier->name();
                        literal_location = identifier->location();
                    }
                    else if (auto scope_res = dynamic_cast<ScopeResolutionNode *>(expr.get()))
                    {
                        // For generic types like MaybeUninit<T>
                        struct_name = scope_res->scope_name() + "::" + scope_res->member_name();
                        literal_location = scope_res->location();
                    }
                    else
                    {
                        // Not a valid struct literal context
                        break;
                    }

                    advance(); // consume '{'

                    auto struct_literal = _builder.create_struct_literal(literal_location, struct_name);

                    // Parse field initializers
                    if (!_current_token.is(TokenKind::TK_R_BRACE))
                    {
                        do
                        {
                            // Check for trailing comma - if we see '}' after comma, break out
                            if (_current_token.is(TokenKind::TK_R_BRACE))
                            {
                                break;
                            }

                            // Parse field name (allow keywords as field names)
                            if (!_current_token.is(TokenKind::TK_IDENTIFIER) && !_current_token.is_keyword())
                            {
                                error("Expected field name in struct literal");
                                return nullptr;
                            }

                            std::string field_name = std::string(_current_token.text());
                            auto field_name_loc = _current_token.location();
                            advance();

                            // Expect ':'
                            consume(TokenKind::TK_COLON, "Expected ':' after field name");

                            // Parse field value
                            auto field_value = parse_expression();

                            // Create field initializer
                            auto field_init = std::make_unique<FieldInitializerNode>(field_name, std::move(field_value), field_name_loc);
                            struct_literal->add_field_initializer(std::move(field_init));

                            // Continue if we see comma, or if we see another field (identifier:)
                            // This allows struct literals without commas after match/block expressions
                        } while (match(TokenKind::TK_COMMA) ||
                                 ((_current_token.is(TokenKind::TK_IDENTIFIER) || _current_token.is_keyword()) &&
                                  peek_next().is(TokenKind::TK_COLON)));
                    }

                    consume(TokenKind::TK_R_BRACE, "Expected '}' after struct literal fields");

                    expr = std::move(struct_literal);
                }
                else
                {
                    // No more postfix operations
                    break;
                }
            }

            return expr;
        }

        // Parenthesized expressions, tuple literals, void literal (), or lambda expressions
        if (_current_token.is(TokenKind::TK_L_PAREN))
        {
            SourceLocation paren_loc = _current_token.location();
            advance(); // consume '('

            // Check for empty parentheses () - could be void or lambda with no params
            if (_current_token.is(TokenKind::TK_R_PAREN))
            {
                advance(); // consume ')'

                // Check if this is a lambda: () -> T { ... }
                if (_current_token.is(TokenKind::TK_ARROW))
                {
                    return parse_lambda_body(paren_loc, {});
                }

                // Create a void literal token and return it as a literal node
                Token void_token(TokenKind::TK_KW_VOID, "void", paren_loc);
                return _builder.create_literal_node(void_token);
            }

            // Check if this looks like lambda parameters: (name: Type, ...) -> ...
            // Lambda parameters have the form: identifier COLON type
            if (_current_token.is(TokenKind::TK_IDENTIFIER) && peek_next().is(TokenKind::TK_COLON))
            {
                // This looks like a lambda parameter list
                std::vector<std::pair<std::string, TypeRef>> params;

                do
                {
                    // Parse parameter name
                    std::string param_name = std::string(_current_token.text());
                    advance(); // consume identifier

                    consume(TokenKind::TK_COLON, "Expected ':' after parameter name");

                    // Parse parameter type
                    TypeRef param_type = parse_type_annotation();

                    params.push_back({param_name, param_type});

                } while (match(TokenKind::TK_COMMA) && _current_token.is(TokenKind::TK_IDENTIFIER));

                consume(TokenKind::TK_R_PAREN, "Expected ')' after lambda parameters");

                // Must have -> after parameter list
                if (!_current_token.is(TokenKind::TK_ARROW))
                {
                    error("Expected '->' after lambda parameters");
                    return nullptr;
                }

                return parse_lambda_body(paren_loc, std::move(params));
            }

            // Parse first expression
            auto first_expr = parse_expression();

            // Check if this is a tuple (has comma) or just a parenthesized expression
            if (_current_token.is(TokenKind::TK_COMMA))
            {
                // This is a tuple literal
                auto tuple = _builder.create_tuple_literal(paren_loc);
                tuple->add_element(std::move(first_expr));

                // Parse remaining elements
                while (_current_token.is(TokenKind::TK_COMMA))
                {
                    advance(); // consume ','

                    // Handle trailing comma before )
                    if (_current_token.is(TokenKind::TK_R_PAREN))
                    {
                        break;
                    }

                    auto element = parse_expression();
                    tuple->add_element(std::move(element));
                }

                consume(TokenKind::TK_R_PAREN, "Expected ')' after tuple elements");
                return tuple;
            }

            // Just a parenthesized expression
            consume(TokenKind::TK_R_PAREN, "Expected ')' after expression");

            std::unique_ptr<ExpressionNode> expr = std::move(first_expr);

            // Handle postfix expressions after parentheses (like (*ptr).method())
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
                else if (_current_token.is(TokenKind::TK_PERIOD) || _current_token.is(TokenKind::TK_ARROW))
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

        // Alignof expressions
        if (_current_token.is(TokenKind::TK_KW_ALIGNOF))
        {
            return parse_alignof_expression();
        }

        // Keywords used as variable names (e.g., ref, default, some, none)
        // This handles cases where a keyword is used as a parameter name and then referenced
        if (_current_token.is_keyword())
        {
            // Treat the keyword as an identifier
            Token keyword_token = _current_token;
            advance();

            // Create identifier node from the keyword
            Token identifier_token(TokenKind::TK_IDENTIFIER,
                                   keyword_token.text(),
                                   keyword_token.location());
            auto keyword_expr = _builder.create_identifier_node(identifier_token);

            // Handle postfix expressions
            std::unique_ptr<ExpressionNode> expr = std::move(keyword_expr);
            while (true)
            {
                if (_current_token.is(TokenKind::TK_L_PAREN))
                {
                    expr = parse_call_expression(std::move(expr));
                }
                else if (_current_token.is(TokenKind::TK_L_SQUARE))
                {
                    expr = parse_array_access(std::move(expr));
                }
                else if (_current_token.is(TokenKind::TK_PERIOD) || _current_token.is(TokenKind::TK_ARROW))
                {
                    expr = parse_member_access(std::move(expr));
                }
                else
                {
                    break;
                }
            }

            return expr;
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

            // Allow both identifiers and keywords after :: (for things like T::default())
            if (!_current_token.is(TokenKind::TK_IDENTIFIER) && !_current_token.is_keyword())
            {
                error("Expected identifier after '::'");
                return _builder.create_identifier_node(token);
            }

            std::string member_name = std::string(_current_token.text());
            advance(); // consume the identifier/keyword

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

    std::unique_ptr<ExpressionNode> Parser::parse_if_expression()
    {
        SourceLocation start_loc = _current_token.location();
        consume(TokenKind::TK_KW_IF, "Expected 'if'");

        // Parse condition in parentheses
        consume(TokenKind::TK_L_PAREN, "Expected '(' after 'if' in if-expression");
        auto condition = parse_expression();
        consume(TokenKind::TK_R_PAREN, "Expected ')' after condition in if-expression");

        // Parse then branch: { expression }
        consume(TokenKind::TK_L_BRACE, "Expected '{' for then branch in if-expression");
        auto then_expr = parse_expression();
        consume(TokenKind::TK_R_BRACE, "Expected '}' after then expression");

        // Require else branch for if-expressions
        consume(TokenKind::TK_KW_ELSE, "Expected 'else' in if-expression (both branches required)");

        // Parse else branch: { expression }
        consume(TokenKind::TK_L_BRACE, "Expected '{' for else branch in if-expression");
        auto else_expr = parse_expression();
        consume(TokenKind::TK_R_BRACE, "Expected '}' after else expression");

        return _builder.create_if_expression(start_loc, std::move(condition), std::move(then_expr), std::move(else_expr));
    }

    std::unique_ptr<ExpressionNode> Parser::parse_match_expression()
    {
        SourceLocation start_loc = _current_token.location();
        consume(TokenKind::TK_KW_MATCH, "Expected 'match'");

        // Parse the expression to match on
        auto expr = parse_expression();

        consume(TokenKind::TK_L_BRACE, "Expected '{' after match expression");

        // Create the match expression
        auto match_expr = _builder.create_match_expression(start_loc, std::move(expr));

        // Parse match arms
        while (!_current_token.is(TokenKind::TK_R_BRACE) && !is_at_end())
        {
            auto arm = parse_match_arm();
            if (arm)
            {
                match_expr->add_arm(std::move(arm));
            }
        }

        consume(TokenKind::TK_R_BRACE, "Expected '}' after match arms");

        return match_expr;
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

    std::unique_ptr<ASTNode> Parser::parse_loop_statement()
    {
        SourceLocation start_loc = _current_token.location();
        consume(TokenKind::TK_KW_LOOP, "Expected 'loop'");

        // loop { body } is equivalent to while(true) { body }
        // Create a synthetic "true" token for the condition
        Token true_token(TokenKind::TK_KW_TRUE, "true", start_loc);
        auto condition = _builder.create_literal_node(true_token);

        auto body = parse_statement();
        if (auto stmt = dynamic_cast<StatementNode *>(body.get()))
        {
            body.release();
            auto body_stmt = std::unique_ptr<StatementNode>(stmt);
            return _builder.create_while_statement(start_loc, std::move(condition), std::move(body_stmt));
        }

        error("Invalid body statement in loop");
        return nullptr;
    }

    std::unique_ptr<ASTNode> Parser::parse_for_statement()
    {
        SourceLocation start_loc = _current_token.location();
        consume(TokenKind::TK_KW_FOR, "Expected 'for'");

        consume(TokenKind::TK_L_PAREN, "Expected '(' after 'for'");

        // Parse initialization - can be either:
        // 1. Standard variable declaration: const/mut name: type = value;
        // 2. Simplified for-loop syntax: name: type = value; (implicitly mutable)
        std::unique_ptr<VariableDeclarationNode> init = nullptr;

        if (_current_token.is(TokenKind::TK_KW_CONST) || _current_token.is(TokenKind::TK_KW_MUT))
        {
            // Standard variable declaration
            init = parse_variable_declaration();
        }
        else if (_current_token.is(TokenKind::TK_IDENTIFIER))
        {
            // Simplified for-loop syntax: i: u32 = 0
            SourceLocation var_loc = _current_token.location();
            Token name_token = consume(TokenKind::TK_IDENTIFIER, "Expected variable name");
            std::string var_name = std::string(name_token.text());

            consume(TokenKind::TK_COLON, "Expected ':' after variable name");
            SourceLocation type_loc = _current_token.location();
            std::string type_string;
            TypeRef var_type = parse_type_annotation(&type_string);

            std::unique_ptr<ExpressionNode> initializer = nullptr;
            if (_current_token.is(TokenKind::TK_EQUAL))
            {
                advance(); // consume '='
                initializer = parse_expression();
            }

            consume(TokenKind::TK_SEMICOLON, "Expected ';' after for-loop initializer");

            // For-loop variables are implicitly mutable
            // Create with TypeAnnotation to preserve type info for resolution
            auto type_annotation = std::make_unique<TypeAnnotation>(
                TypeAnnotation::named(type_string, type_loc));
            init = _builder.create_variable_declaration(var_loc, var_name, std::move(type_annotation), std::move(initializer), true, false);
            // Also set the resolved type (may be an error type for generic types)
            init->set_resolved_type(var_type);
        }
        else
        {
            error("Expected variable declaration in for-loop initializer");
            return nullptr;
        }

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

        // Parse patterns - supports multiple patterns with '|' syntax
        // e.g., ' ' | '\r' | '\t' => { ... }
        std::vector<std::unique_ptr<PatternNode>> patterns;
        patterns.push_back(parse_pattern());

        // Check for alternative patterns using '|'
        while (_current_token.is(TokenKind::TK_PIPE))
        {
            advance(); // consume '|'
            patterns.push_back(parse_pattern());
        }

        consume(TokenKind::TK_FATARROW, "Expected '=>' after pattern");

        // Parse the body - can be either a block statement or a single expression
        std::unique_ptr<StatementNode> body;

        if (_current_token.is(TokenKind::TK_L_BRACE))
        {
            // Match arm block body
            // Handles: empty blocks {}, single expressions { expr }, multi-statement { stmt; stmt; }
            SourceLocation block_loc = _current_token.location();
            advance(); // consume '{'

            // Handle empty block
            if (_current_token.is(TokenKind::TK_R_BRACE))
            {
                advance(); // consume '}'
                // Create an empty block statement
                auto block = _builder.create_block_statement(block_loc);
                body = std::move(block);
            }
            else
            {
                // Parse block content - can be statements or expressions
                auto block = _builder.create_block_statement(block_loc);
                enter_scope();

                while (!_current_token.is(TokenKind::TK_R_BRACE) && !is_at_end())
                {
                    // Check if this is a statement keyword that needs special handling
                    if (_current_token.is(TokenKind::TK_KW_RETURN))
                    {
                        // Parse return statement
                        SourceLocation return_loc = _current_token.location();
                        advance(); // consume 'return'

                        std::unique_ptr<ExpressionNode> expr = nullptr;
                        if (!_current_token.is(TokenKind::TK_SEMICOLON) && !_current_token.is(TokenKind::TK_R_BRACE))
                        {
                            expr = parse_expression();
                        }

                        auto return_stmt = _builder.create_return_statement(return_loc, std::move(expr));

                        // Consume optional semicolon
                        if (_current_token.is(TokenKind::TK_SEMICOLON))
                        {
                            advance();
                        }

                        block->add_statement(std::move(return_stmt));
                    }
                    else if (_current_token.is(TokenKind::TK_KW_CONST) ||
                             _current_token.is(TokenKind::TK_KW_MUT))
                    {
                        // Variable declaration - parse as statement
                        auto stmt = parse_statement();
                        if (stmt)
                        {
                            if (auto statement_node = dynamic_cast<StatementNode *>(stmt.get()))
                            {
                                stmt.release();
                                block->add_statement(std::unique_ptr<StatementNode>(statement_node));
                            }
                            else if (auto decl_node = dynamic_cast<DeclarationNode *>(stmt.get()))
                            {
                                stmt.release();
                                auto decl_stmt = _builder.create_declaration_statement(decl_node->location(),
                                                                                       std::unique_ptr<DeclarationNode>(decl_node));
                                block->add_statement(std::move(decl_stmt));
                            }
                        }
                    }
                    else if (_current_token.is(TokenKind::TK_KW_IF) ||
                             _current_token.is(TokenKind::TK_KW_WHILE) ||
                             _current_token.is(TokenKind::TK_KW_FOR) ||
                             _current_token.is(TokenKind::TK_KW_LOOP) ||
                             _current_token.is(TokenKind::TK_KW_BREAK) ||
                             _current_token.is(TokenKind::TK_KW_CONTINUE))
                    {
                        // Control flow statement - parse as statement
                        auto stmt = parse_statement();
                        if (stmt)
                        {
                            if (auto statement_node = dynamic_cast<StatementNode *>(stmt.get()))
                            {
                                stmt.release();
                                block->add_statement(std::unique_ptr<StatementNode>(statement_node));
                            }
                        }
                    }
                    else
                    {
                        // Parse as expression
                        auto expr = parse_expression();

                        // Check what follows
                        if (_current_token.is(TokenKind::TK_SEMICOLON))
                        {
                            // Expression statement with semicolon - more statements may follow
                            advance(); // consume ';'
                            auto expr_stmt = std::make_unique<ExpressionStatementNode>(expr->location(), std::move(expr));
                            block->add_statement(std::move(expr_stmt));
                        }
                        else if (_current_token.is(TokenKind::TK_R_BRACE))
                        {
                            // Final expression without semicolon - this is the block's value
                            auto expr_stmt = std::make_unique<ExpressionStatementNode>(expr->location(), std::move(expr));
                            block->add_statement(std::move(expr_stmt));
                            // Don't advance, let the outer loop handle the }
                        }
                        else
                        {
                            // Error - expected ; or }
                            error("Expected ';' or '}' after expression in match arm block");
                            break;
                        }
                    }
                }

                exit_scope();
                consume(TokenKind::TK_R_BRACE, "Expected '}' after match arm block");
                body = std::move(block);
            }
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

        return std::make_unique<MatchArmNode>(start_loc, std::move(patterns), std::move(body));
    }

    std::unique_ptr<PatternNode> Parser::parse_pattern()
    {
        SourceLocation start_loc = _current_token.location();

        // Handle literal patterns (character, string, integer literals)
        // Also handles range patterns like 'a'..'z' or 0..100
        if (_current_token.is(TokenKind::TK_CHAR_CONSTANT) ||
            _current_token.is(TokenKind::TK_STRING_LITERAL) ||
            _current_token.is(TokenKind::TK_NUMERIC_CONSTANT))
        {
            // Create a literal pattern - for now we can reuse the literal node structure
            auto literal = _builder.create_literal_node(_current_token);
            advance();

            // Check for range pattern (e.g., 'a'..'z', 0..100)
            if (_current_token.is(TokenKind::TK_DOTDOT))
            {
                advance(); // consume '..'

                // Parse the end of the range
                if (!_current_token.is(TokenKind::TK_CHAR_CONSTANT) &&
                    !_current_token.is(TokenKind::TK_STRING_LITERAL) &&
                    !_current_token.is(TokenKind::TK_NUMERIC_CONSTANT))
                {
                    report_error("Expected literal after '..' in range pattern");
                    return nullptr;
                }

                auto end_literal = _builder.create_literal_node(_current_token);
                advance();

                // Create a range pattern
                auto pattern = std::make_unique<PatternNode>(start_loc);
                pattern->set_range(std::move(literal), std::move(end_literal));
                return std::move(pattern);
            }

            // Create a pattern node that wraps the literal
            auto pattern = std::make_unique<PatternNode>(start_loc);
            pattern->set_literal_value(std::move(literal));
            return std::move(pattern);
        }

        // Handle wildcard pattern
        if (_current_token.is(TokenKind::TK_IDENTIFIER) && _current_token.text() == "_")
        {
            advance();
            auto pattern = std::make_unique<PatternNode>(start_loc);
            pattern->set_wildcard(true);
            return std::move(pattern);
        }

        // Handle enum patterns like Shape::Circle(radius) or Colors::Color::Red
        if (_current_token.is(TokenKind::TK_IDENTIFIER))
        {
            // Collect all path segments: A::B::C::D → segments = [A, B, C, D]
            std::vector<std::string> segments;
            segments.push_back(std::string{_current_token.text()});
            advance();

            while (_current_token.is(TokenKind::TK_COLONCOLON))
            {
                advance(); // consume ::
                if (_current_token.is_identifier() || _current_token.is_keyword())
                {
                    segments.push_back(std::string{_current_token.text()});
                    advance();
                }
                else
                {
                    break;
                }
            }

            if (segments.size() >= 2)
            {
                // Last segment is variant, everything before is the enum qualified name
                std::string variant_name = segments.back();
                segments.pop_back();
                std::string enum_name;
                for (size_t i = 0; i < segments.size(); ++i)
                {
                    if (i > 0) enum_name += "::";
                    enum_name += segments[i];
                }

                auto pattern = std::make_unique<EnumPatternNode>(start_loc, enum_name, variant_name);

                // Parse pattern elements if present (bindings, wildcards, or literals)
                if (_current_token.is(TokenKind::TK_L_PAREN))
                {
                    advance();

                    while (!_current_token.is(TokenKind::TK_R_PAREN) && !is_at_end())
                    {
                        // Handle identifier (variable binding) or wildcard (_)
                        if (_current_token.is(TokenKind::TK_IDENTIFIER))
                        {
                            std::string text{_current_token.text()};
                            auto elem_loc = _current_token.location();
                            if (text == "_")
                            {
                                // Wildcard pattern
                                pattern->add_pattern_element(PatternElement::make_wildcard(elem_loc));
                            }
                            else
                            {
                                // Variable binding
                                pattern->add_pattern_element(PatternElement::make_binding(text, elem_loc));
                            }
                            advance();

                            if (_current_token.is(TokenKind::TK_COMMA))
                            {
                                advance();
                            }
                        }
                        // Handle numeric literals (e.g., 0, 42, -1)
                        else if (_current_token.is(TokenKind::TK_NUMERIC_CONSTANT))
                        {
                            std::string num_text{_current_token.text()};
                            num_text.erase(std::remove(num_text.begin(), num_text.end(), '_'), num_text.end()); // Strip underscore separators
                            // Parse as integer (for simplicity, floating point patterns can be added later)
                            int64_t value = 0;
                            try
                            {
                                value = std::stoll(num_text);
                            }
                            catch (...)
                            {
                                // Try parsing as float if integer parsing fails
                                try
                                {
                                    double fval = std::stod(num_text);
                                    pattern->add_pattern_element(PatternElement::make_literal_float(fval));
                                    advance();
                                    if (_current_token.is(TokenKind::TK_COMMA))
                                    {
                                        advance();
                                    }
                                    continue;
                                }
                                catch (...)
                                {
                                    error("Invalid numeric literal in pattern");
                                    break;
                                }
                            }
                            pattern->add_pattern_element(PatternElement::make_literal_int(value));
                            advance();

                            if (_current_token.is(TokenKind::TK_COMMA))
                            {
                                advance();
                            }
                        }
                        // Handle boolean literals (true/false)
                        else if (_current_token.is(TokenKind::TK_KW_TRUE))
                        {
                            pattern->add_pattern_element(PatternElement::make_literal_bool(true));
                            advance();

                            if (_current_token.is(TokenKind::TK_COMMA))
                            {
                                advance();
                            }
                        }
                        else if (_current_token.is(TokenKind::TK_KW_FALSE))
                        {
                            pattern->add_pattern_element(PatternElement::make_literal_bool(false));
                            advance();

                            if (_current_token.is(TokenKind::TK_COMMA))
                            {
                                advance();
                            }
                        }
                        // Handle string literals
                        else if (_current_token.is(TokenKind::TK_STRING_LITERAL))
                        {
                            std::string str_text{_current_token.text()};
                            // Remove quotes if present
                            if (str_text.length() >= 2 && str_text.front() == '"' && str_text.back() == '"')
                            {
                                str_text = str_text.substr(1, str_text.length() - 2);
                            }
                            pattern->add_pattern_element(PatternElement::make_literal_string(str_text));
                            advance();

                            if (_current_token.is(TokenKind::TK_COMMA))
                            {
                                advance();
                            }
                        }
                        else
                        {
                            error("Expected identifier, wildcard (_), or literal in pattern");
                            break;
                        }
                    }

                    consume(TokenKind::TK_R_PAREN, "Expected ')' after pattern elements");
                }

                return std::move(pattern);
            }
            else
            {
                // Simple identifier pattern (variable binding, single segment with no ::)
                auto pattern = std::make_unique<PatternNode>(start_loc);
                pattern->set_identifier(segments[0]);
                return std::move(pattern);
            }
        }

        error("Expected pattern (literal, identifier, enum pattern, or wildcard)");
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

    std::unique_ptr<ASTNode> Parser::parse_unsafe_block_statement()
    {
        SourceLocation start_loc = _current_token.location();
        consume(TokenKind::TK_KW_UNSAFE, "Expected 'unsafe'");

        auto block = parse_block_statement();
        if (auto block_stmt = dynamic_cast<BlockStatementNode *>(block.get()))
        {
            block.release(); // Release ownership
            auto unsafe_block = std::unique_ptr<BlockStatementNode>(block_stmt);
            return _builder.create_unsafe_block_statement(start_loc, std::move(unsafe_block));
        }

        error("Expected block statement after 'unsafe'");
        return nullptr;
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
        // Check for explicit 'this' parameter (e.g., &this, mut &this, this)
        if (is_this_parameter())
        {
            return parse_this_parameter();
        }

        // Parse parameter name - allow identifiers, 'this', and other keywords as parameter names
        Token name_token;
        std::string param_name;
        if (_current_token.is(TokenKind::TK_IDENTIFIER))
        {
            name_token = _current_token;
            param_name = std::string(name_token.text());
            advance();
        }
        else if (_current_token.is(TokenKind::TK_KW_THIS))
        {
            // Allow 'this' as a parameter name when followed by ':'
            name_token = _current_token;
            param_name = "this";
            advance();
        }
        else if (_current_token.is_keyword())
        {
            // Allow keywords as parameter names (e.g., default, type, as)
            name_token = _current_token;
            param_name = std::string(name_token.text());
            advance();
        }
        else
        {
            error("Expected parameter name");
            return nullptr;
        }

        // Parse type annotation
        consume(TokenKind::TK_COLON, "Expected ':' after parameter name");
        SourceLocation type_loc = _current_token.location();
        std::string type_string;
        TypeRef param_type = parse_type_annotation(&type_string);

        // Create parameter as variable declaration (without initializer)
        // Use TypeAnnotation to preserve type info for resolution
        auto type_annotation = std::make_unique<TypeAnnotation>(
            TypeAnnotation::named(type_string, type_loc));
        // Parameters: loc, name, type_annotation, init (nullptr), is_mutable (true for params), is_global (false)
        auto param_decl = _builder.create_variable_declaration(name_token.location(), param_name, std::move(type_annotation), nullptr, true, false);
        // Also set the resolved type (may be an error type for generic types)
        param_decl->set_resolved_type(param_type);
        return param_decl;
    }

    bool Parser::is_this_parameter()
    {
        // Check for patterns: &this, mut &this, this (but NOT this: Type)
        if (_current_token.is(TokenKind::TK_KW_THIS))
        {
            // Check if followed by ':' - if so, it's a regular named parameter like "this: T"
            Token next = peek_next();
            if (next.is(TokenKind::TK_COLON))
            {
                return false; // this: Type is a regular named parameter, not special 'this' syntax
            }
            return true; // this (by value) without explicit type
        }
        else if (_current_token.is(TokenKind::TK_AMP))
        {
            // Look ahead to see if it's &this
            Token next = peek_next();
            return next.is(TokenKind::TK_KW_THIS);
        }
        else if (_current_token.is(TokenKind::TK_KW_MUT))
        {
            // For 'mut &this', we'll handle this in parse_this_parameter
            // For now, just return true if we see 'mut' as first token in parameter
            return true;
        }
        return false;
    }

    std::unique_ptr<VariableDeclarationNode> Parser::parse_this_parameter()
    {
        SourceLocation start_loc = _current_token.location();
        bool is_mutable = false;
        bool is_reference = false;

        // Parse mut keyword if present
        if (_current_token.is(TokenKind::TK_KW_MUT))
        {
            is_mutable = true;
            advance(); // consume 'mut'

            // After 'mut', we expect '&this'
            if (!_current_token.is(TokenKind::TK_AMP))
            {
                error("Expected '&' after 'mut' in this parameter");
                return nullptr;
            }
        }

        // Parse & if present
        if (_current_token.is(TokenKind::TK_AMP))
        {
            is_reference = true;
            advance(); // consume '&'
        }

        // Parse 'this'
        if (!_current_token.is(TokenKind::TK_KW_THIS))
        {
            error("Expected 'this' in this parameter");
            return nullptr;
        }
        advance(); // consume 'this'

        // Resolve the 'this' type from the enclosing struct/class context
        TypeRef this_type;

        if (!_current_parsing_type_name.empty())
        {
            // For generic types like "Option<T>", extract the base type name "Option"
            // The symbol table registers the base type, not the parameterized version
            std::string lookup_name = _current_parsing_type_name;
            size_t angle_pos = lookup_name.find('<');
            if (angle_pos != std::string::npos)
            {
                lookup_name = lookup_name.substr(0, angle_pos);
            }

            // First, check if this is a primitive type (e.g., impl i32 { ... })
            // Primitive types aren't in the symbol table - they're built-in to TypeArena
            TypeRef base_type;
            if (lookup_name == "void")
                base_type = _context.types().get_void();
            else if (lookup_name == "boolean")
                base_type = _context.types().get_bool();
            else if (lookup_name == "i8")
                base_type = _context.types().get_i8();
            else if (lookup_name == "i16")
                base_type = _context.types().get_i16();
            else if (lookup_name == "i32")
                base_type = _context.types().get_i32();
            else if (lookup_name == "i64")
                base_type = _context.types().get_i64();
            else if (lookup_name == "i128")
                base_type = _context.types().get_i128();
            else if (lookup_name == "int")
                base_type = _context.types().get_i32();
            else if (lookup_name == "u8")
                base_type = _context.types().get_u8();
            else if (lookup_name == "u16")
                base_type = _context.types().get_u16();
            else if (lookup_name == "u32")
                base_type = _context.types().get_u32();
            else if (lookup_name == "u64")
                base_type = _context.types().get_u64();
            else if (lookup_name == "u128")
                base_type = _context.types().get_u128();
            else if (lookup_name == "uint")
                base_type = _context.types().get_u32();
            else if (lookup_name == "f32")
                base_type = _context.types().get_f32();
            else if (lookup_name == "f64")
                base_type = _context.types().get_f64();
            else if (lookup_name == "float")
                base_type = _context.types().get_f32();
            else if (lookup_name == "double")
                base_type = _context.types().get_f64();
            else if (lookup_name == "char")
                base_type = _context.types().get_char();
            else if (lookup_name == "string")
                base_type = _context.types().get_string();
            else if (lookup_name == "never")
                base_type = _context.types().get_never();
            else
            {
                // Not a primitive - try multiple lookup methods

                // 1. Try symbol table first (for types pre-registered during parsing)
                const Symbol *sym = _context.symbols().lookup(lookup_name);
                base_type = sym ? sym->type : TypeRef{};

                // 2. If not found, try TypeArena lookup (for types registered by ModuleLoader)
                if (!base_type.is_valid())
                {
                    base_type = _context.types().lookup_type_by_name(lookup_name);
                    if (base_type.is_valid())
                    {
                        LOG_DEBUG(LogComponent::PARSER, "Resolved 'this' type via TypeArena lookup: '{}'",
                                  lookup_name);
                    }
                }
            }

            if (base_type.is_valid() && !base_type.is_error())
            {
                if (is_reference)
                {
                    // Create a reference to the struct/class type
                    this_type = _context.types().get_reference_to(base_type);
                }
                else
                {
                    // By value - use the struct/class type directly
                    this_type = base_type;
                }
                LOG_DEBUG(LogComponent::PARSER, "Resolved 'this' parameter type to '{}'",
                          this_type.is_valid() ? this_type->display_name() : "<invalid>");
            }
            else
            {
                // Type not found - create error with informative message
                std::string err_msg = "unresolved_this:" + _current_parsing_type_name;
                this_type = _context.types().create_error(err_msg, start_loc);
                LOG_DEBUG(LogComponent::PARSER, "Could not resolve 'this' type for '{}'",
                          _current_parsing_type_name);
            }
        }
        else
        {
            // Not inside a struct/class - should not have 'this' parameter
            error("'this' parameter can only be used inside struct or class methods");
            this_type = _context.types().create_error("orphan_this_parameter", start_loc);
        }

        // Create the 'this' parameter with the resolved type
        auto param = _builder.create_variable_declaration(start_loc, "this", this_type);

        // TODO: Add metadata to indicate this is a 'this' parameter and whether it's mutable

        return param;
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
        TypeRef variadic_type = resolve_type_from_string("...");
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
                        // Parse field name (allow keywords as field names)
                        if (!_current_token.is(TokenKind::TK_IDENTIFIER) && !_current_token.is_keyword())
                        {
                            error("Expected field name in struct literal");
                            return nullptr;
                        }

                        std::string field_name = std::string(_current_token.text());
                        auto field_name_loc = _current_token.location();
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
                        auto field_init = std::make_unique<FieldInitializerNode>(field_name, std::move(field_value), field_name_loc);
                        struct_literal->add_field_initializer(std::move(field_init));

                        // Continue if comma or another field (identifier:)
                    } while (match(TokenKind::TK_COMMA) ||
                             ((_current_token.is(TokenKind::TK_IDENTIFIER) || _current_token.is_keyword()) &&
                              peek_next().is(TokenKind::TK_COLON)));
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

    std::unique_ptr<ExpressionNode> Parser::parse_lambda_body(
        SourceLocation loc,
        std::vector<std::pair<std::string, TypeRef>> params)
    {
        // At this point we've already parsed the parameters and are at '->'
        consume(TokenKind::TK_ARROW, "Expected '->' in lambda expression");

        // Parse return type
        TypeRef return_type = parse_type_annotation();

        // Create lambda node
        auto lambda = _builder.create_lambda_expression(loc);

        // Add parameters
        for (auto &[name, type] : params)
        {
            lambda->add_parameter(name, type);
        }

        // Set return type
        lambda->set_return_type(return_type);

        // Parse body (must be a block)
        if (!_current_token.is(TokenKind::TK_L_BRACE))
        {
            error("Expected '{' for lambda body");
            return nullptr;
        }

        auto body = parse_block_statement();
        lambda->set_body(std::move(body));

        return lambda;
    }

    std::unique_ptr<ExpressionNode> Parser::parse_new_expression()
    {
        SourceLocation new_location = _current_token.location();
        consume(TokenKind::TK_KW_NEW, "Expected 'new' keyword");

        // Parse type name - accept both identifiers and type keywords (char, int, etc.)
        if (!_current_token.is(TokenKind::TK_IDENTIFIER) && !is_type_token())
        {
            error("Expected type name after 'new'");
            return nullptr;
        }

        std::string type_name = std::string(_current_token.text());
        advance(); // consume type name

        // Handle qualified type names: new Module::Type or new EnumType::Variant
        while (_current_token.is(TokenKind::TK_COLONCOLON))
        {
            advance(); // consume '::'
            if (!_current_token.is(TokenKind::TK_IDENTIFIER) && !is_type_token())
            {
                error("Expected type name after '::'");
                return nullptr;
            }
            type_name += "::" + std::string(_current_token.text());
            advance(); // consume the next segment
        }

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

                std::string type_arg = std::string(_current_token.text());
                advance(); // consume type argument

                // Handle nested generic types (e.g., Box<Option<T>>)
                if (_current_token.is(TokenKind::TK_L_ANGLE))
                {
                    type_arg += "<";
                    advance();
                    int nested_depth = 1;
                    while (nested_depth > 0 && !is_at_end())
                    {
                        if (_current_token.is(TokenKind::TK_L_ANGLE))
                            nested_depth++;
                        else if (_current_token.is(TokenKind::TK_R_ANGLE))
                            nested_depth--;
                        else if (_current_token.is(TokenKind::TK_GREATERGREATER))
                            nested_depth -= 2;
                        if (nested_depth > 0)
                        {
                            type_arg += std::string(_current_token.text());
                            advance();
                        }
                        else
                        {
                            type_arg += ">";
                            advance();
                        }
                    }
                }

                // Handle pointer modifiers (e.g., Box<Expr*>)
                while (_current_token.is(TokenKind::TK_STAR))
                {
                    type_arg += "*";
                    advance();
                }

                // Handle array modifiers (e.g., Box<SymbolID[]>)
                while (_current_token.is(TokenKind::TK_L_SQUARE))
                {
                    type_arg += "[";
                    advance();
                    if (_current_token.is(TokenKind::TK_NUMERIC_CONSTANT))
                    {
                        type_arg += std::string(_current_token.text());
                        advance();
                    }
                    if (_current_token.is(TokenKind::TK_R_SQUARE))
                    {
                        type_arg += "]";
                        advance();
                    }
                }

                new_expr->add_generic_arg(type_arg);

            } while (match(TokenKind::TK_COMMA));

            consume(TokenKind::TK_R_ANGLE, "Expected '>' after generic arguments");
        }

        // Check for array allocation syntax: new Type[size]
        if (_current_token.is(TokenKind::TK_L_SQUARE))
        {
            advance(); // consume '['

            // Parse the array size expression
            auto size_expr = parse_expression();
            if (!size_expr)
            {
                error("Expected array size expression");
                return nullptr;
            }

            new_expr->set_array_size(std::move(size_expr));

            consume(TokenKind::TK_R_SQUARE, "Expected ']' after array size");

            return new_expr;
        }

        // Check for heap-allocated struct literal syntax: new StructName { field: value, ... }
        if (_current_token.is(TokenKind::TK_L_BRACE))
        {
            advance(); // consume '{'

            auto struct_literal = _builder.create_struct_literal(new_location, type_name);
            struct_literal->set_heap_allocated(true);

            // Copy generic arguments from new_expr to struct_literal
            for (const auto &generic_arg : new_expr->generic_args())
            {
                struct_literal->add_generic_arg(generic_arg);
            }

            // Parse field initializers
            if (!_current_token.is(TokenKind::TK_R_BRACE))
            {
                do
                {
                    if (_current_token.is(TokenKind::TK_R_BRACE))
                    {
                        break;
                    }

                    if (!_current_token.is(TokenKind::TK_IDENTIFIER) && !_current_token.is_keyword())
                    {
                        error("Expected field name in struct literal");
                        return nullptr;
                    }

                    std::string field_name = std::string(_current_token.text());
                    auto field_name_loc = _current_token.location();
                    advance(); // consume field name

                    consume(TokenKind::TK_COLON, "Expected ':' after field name");

                    auto field_value = parse_expression();
                    if (!field_value)
                    {
                        error("Expected expression for field value");
                        return nullptr;
                    }

                    auto field_init = std::make_unique<FieldInitializerNode>(field_name, std::move(field_value), field_name_loc);
                    struct_literal->add_field_initializer(std::move(field_init));

                } while (match(TokenKind::TK_COMMA) ||
                         ((_current_token.is(TokenKind::TK_IDENTIFIER) || _current_token.is_keyword()) &&
                          peek_next().is(TokenKind::TK_COLON)));
            }

            consume(TokenKind::TK_R_BRACE, "Expected '}' after struct literal fields");

            return struct_literal;
        }

        // Parse constructor arguments: new Type(arg1, arg2, ...)
        consume(TokenKind::TK_L_PAREN, "Expected '(' or '{' for constructor/struct literal");

        // Regular constructor call with positional arguments
        if (!_current_token.is(TokenKind::TK_R_PAREN))
        {
            do
            {
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

    std::unique_ptr<ExpressionNode> Parser::parse_sizeof_expression()
    {
        SourceLocation sizeof_location = _current_token.location();
        consume(TokenKind::TK_KW_SIZEOF, "Expected 'sizeof' keyword");

        consume(TokenKind::TK_L_PAREN, "Expected '(' after 'sizeof'");

        // Capture location of the type name token for LSP hover
        SourceLocation type_loc = _current_token.location();

        // Parse type annotation - use out_type_string to get the original annotation
        // instead of display_name(), which would include error messages for unresolved generics.
        // For sizeof(HashSetEntry<T>), we need to preserve "HashSetEntry<T>" so that
        // during codegen, the type parameter T can be properly substituted.
        std::string type_name;
        TypeRef resolved = parse_type_annotation(&type_name);

        // If we didn't get a type string (shouldn't happen), fall back to display_name
        if (type_name.empty() && resolved.is_valid())
        {
            type_name = resolved->display_name();
        }

        if (type_name.empty())
        {
            error("Expected type name in sizeof expression");
            return nullptr;
        }

        consume(TokenKind::TK_R_PAREN, "Expected ')' after type name");

        auto node = _builder.create_sizeof_expression(sizeof_location, type_name);
        if (node)
        {
            auto *sizeof_node = static_cast<SizeofExpressionNode *>(node.get());
            sizeof_node->set_type_location(type_loc);
        }
        return node;
    }

    std::unique_ptr<ExpressionNode> Parser::parse_alignof_expression()
    {
        SourceLocation alignof_location = _current_token.location();
        consume(TokenKind::TK_KW_ALIGNOF, "Expected 'alignof' keyword");

        consume(TokenKind::TK_L_PAREN, "Expected '(' after 'alignof'");

        // Capture location of the type name token for LSP hover
        SourceLocation type_loc = _current_token.location();

        // Parse type annotation - use out_type_string to get the original annotation
        // instead of display_name(), which would include error messages for unresolved generics.
        std::string type_name;
        TypeRef resolved = parse_type_annotation(&type_name);

        // If we didn't get a type string (shouldn't happen), fall back to display_name
        if (type_name.empty() && resolved.is_valid())
        {
            type_name = resolved->display_name();
        }

        if (type_name.empty())
        {
            error("Expected type name in alignof expression");
            return nullptr;
        }

        consume(TokenKind::TK_R_PAREN, "Expected ')' after type name");

        auto node = _builder.create_alignof_expression(alignof_location, type_name);
        if (node)
        {
            auto *alignof_node = static_cast<AlignofExpressionNode *>(node.get());
            alignof_node->set_type_location(type_loc);
        }
        return node;
    }

    std::string Parser::parse_generic_type_suffix()
    {
        std::string result;

        if (_current_token.is(TokenKind::TK_L_ANGLE))
        {
            result += "<";
            advance(); // consume '<'

            // Parse generic type arguments
            if (!_current_token.is(TokenKind::TK_R_ANGLE))
            {
                do
                {
                    // Parse type argument
                    if (_current_token.is(TokenKind::TK_IDENTIFIER) || is_type_token())
                    {
                        result += std::string(_current_token.text());
                        advance();

                        // Handle nested generics like Array<Map<K, V>>
                        if (_current_token.is(TokenKind::TK_L_ANGLE))
                        {
                            result += parse_generic_type_suffix();
                        }

                        // Handle pointers in generic arguments like Array<int*>
                        while (_current_token.is(TokenKind::TK_STAR))
                        {
                            result += "*";
                            advance();
                        }
                    }
                    else
                    {
                        error("Expected type name in generic type arguments");
                        break;
                    }

                    if (_current_token.is(TokenKind::TK_COMMA))
                    {
                        result += ", ";
                        advance(); // consume comma
                    }
                    else
                    {
                        break;
                    }
                } while (true);
            }

            if (_current_token.is(TokenKind::TK_R_ANGLE))
            {
                result += ">";
                advance(); // consume '>'
            }
            else
            {
                error("Expected '>' to close generic type arguments");
            }
        }

        return result;
    }

    std::unique_ptr<ExpressionNode> Parser::parse_member_access(std::unique_ptr<ExpressionNode> expr)
    {
        SourceLocation access_location = _current_token.location();

        // Support both object member access (.) and pointer member access (->)
        if (!_current_token.is(TokenKind::TK_PERIOD) && !_current_token.is(TokenKind::TK_ARROW))
        {
            error("Expected '.' or '->' for member access");
            return expr;
        }

        bool is_arrow = _current_token.is(TokenKind::TK_ARROW);
        advance(); // consume '.' or '->'

        // Handle postfix dereference: expr.*
        // This dereferences a pointer stored in expr
        if (_current_token.is(TokenKind::TK_STAR) && !is_arrow)
        {
            advance(); // consume '*'
            // Create a dereference expression
            Token deref_token(TokenKind::TK_STAR, "*", access_location);
            return _builder.create_unary_expression(deref_token, std::move(expr));
        }

        if (!_current_token.is(TokenKind::TK_IDENTIFIER) && !_current_token.is_keyword())
        {
            error("Expected member name after member access operator");
            return expr;
        }

        std::string member_name = std::string(_current_token.text());
        SourceLocation member_name_loc = _current_token.location();
        advance(); // consume member name

        // Check for generic method call: method_name<T>(args)
        // Use lookahead to determine if < starts generic args followed by (
        std::vector<std::string> generic_args;
        if (_current_token.is(TokenKind::TK_L_ANGLE))
        {
            // Use lookahead to check if this is generic args followed by '('
            // Pattern: <type_args>() where type_args can contain nested <>
            bool is_generic_call = false;
            int lookahead_idx = 1;
            int angle_depth = 1;

            // Scan ahead to find matching '>' and check if followed by '('
            while (angle_depth > 0)
            {
                Token peek_tok = peek_next_n(lookahead_idx);
                if (peek_tok.is_eof())
                    break;

                if (peek_tok.is(TokenKind::TK_L_ANGLE))
                {
                    angle_depth++;
                }
                else if (peek_tok.is(TokenKind::TK_R_ANGLE))
                {
                    angle_depth--;
                }
                else if (peek_tok.is(TokenKind::TK_GREATERGREATER))
                {
                    angle_depth -= 2;
                }
                else if (!peek_tok.is_identifier() && !peek_tok.is_keyword() &&
                         !peek_tok.is(TokenKind::TK_COMMA) && !peek_tok.is(TokenKind::TK_STAR) &&
                         !peek_tok.is(TokenKind::TK_COLONCOLON) && !peek_tok.is(TokenKind::TK_AMP))
                {
                    // Invalid token for generic args - not a generic call
                    break;
                }
                lookahead_idx++;
            }

            // Check if the token after '>' is '('
            if (angle_depth == 0)
            {
                Token after_close = peek_next_n(lookahead_idx);
                if (after_close.is(TokenKind::TK_L_PAREN))
                {
                    is_generic_call = true;
                }
            }

            if (is_generic_call)
            {
                // Parse generic arguments
                advance(); // consume '<'

                while (!_current_token.is(TokenKind::TK_R_ANGLE) &&
                       !_current_token.is(TokenKind::TK_GREATERGREATER) && !is_at_end())
                {
                    if (_current_token.is(TokenKind::TK_COMMA))
                    {
                        advance();
                        continue;
                    }

                    // Build up the type string (handle pointers, qualified names, nested generics)
                    std::string type_arg;
                    int nested_angle = 0;

                    while (!is_at_end())
                    {
                        if (_current_token.is(TokenKind::TK_L_ANGLE))
                        {
                            nested_angle++;
                            type_arg += "<";
                            advance();
                        }
                        else if (_current_token.is(TokenKind::TK_R_ANGLE))
                        {
                            if (nested_angle > 0)
                            {
                                nested_angle--;
                                type_arg += ">";
                                advance();
                            }
                            else
                            {
                                break;
                            }
                        }
                        else if (_current_token.is(TokenKind::TK_GREATERGREATER))
                        {
                            if (nested_angle > 1)
                            {
                                nested_angle -= 2;
                                type_arg += ">>";
                                advance();
                            }
                            else
                            {
                                break;
                            }
                        }
                        else if (_current_token.is(TokenKind::TK_COMMA) && nested_angle == 0)
                        {
                            break;
                        }
                        else if (_current_token.is_identifier() || _current_token.is_keyword())
                        {
                            type_arg += std::string(_current_token.text());
                            advance();
                        }
                        else if (_current_token.is(TokenKind::TK_STAR))
                        {
                            type_arg += "*";
                            advance();
                        }
                        else if (_current_token.is(TokenKind::TK_AMP))
                        {
                            type_arg += "&";
                            advance();
                        }
                        else if (_current_token.is(TokenKind::TK_COLONCOLON))
                        {
                            type_arg += "::";
                            advance();
                        }
                        else
                        {
                            break;
                        }
                    }

                    if (!type_arg.empty())
                    {
                        generic_args.push_back(type_arg);
                    }
                }

                // Consume closing '>' or '>>'
                if (_current_token.is(TokenKind::TK_R_ANGLE))
                {
                    advance();
                }
                else if (_current_token.is(TokenKind::TK_GREATERGREATER))
                {
                    advance();
                }
            }
        }

        // Check if this is a method call (member access followed by function call)
        if (_current_token.is(TokenKind::TK_L_PAREN))
        {
            // Create member access node first
            auto member_access = _builder.create_member_access(access_location, std::move(expr), member_name);
            member_access->set_member_location(member_name_loc);

            // Then parse the function call with the member access as the callee
            auto call_expr = parse_call_expression(std::move(member_access));

            // Add generic arguments if present
            if (!generic_args.empty() && call_expr)
            {
                auto *call_node = dynamic_cast<CallExpressionNode *>(call_expr.get());
                if (call_node)
                {
                    for (const auto &arg : generic_args)
                    {
                        call_node->add_generic_arg(arg);
                    }
                }
            }

            return call_expr;
        }
        else
        {
            // Simple member access (field access)
            auto member_access = _builder.create_member_access(access_location, std::move(expr), member_name);
            member_access->set_member_location(member_name_loc);
            return member_access;
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

        // Parse first element
        auto first_element = parse_expression();
        if (first_element)
        {
            array_literal->add_element(std::move(first_element));
        }

        // Check for [value; count] repeat syntax
        if (_current_token.is(TokenKind::TK_SEMICOLON))
        {
            advance(); // consume ';'

            // Parse the count expression
            auto count_expr = parse_expression();
            if (!count_expr)
            {
                report_error(ErrorCode::E0102_EXPECTED_EXPRESSION, "Expected count expression after ';' in array repeat syntax");
                // Try to recover by consuming up to ']'
                while (!_current_token.is(TokenKind::TK_R_SQUARE) && !is_at_end())
                {
                    advance();
                }
                if (_current_token.is(TokenKind::TK_R_SQUARE))
                {
                    advance();
                }
                return array_literal;
            }

            // Check if it's a compile-time constant (integer literal)
            if (count_expr->kind() == NodeKind::Literal)
            {
                auto *literal = static_cast<LiteralNode *>(count_expr.get());
                if (literal->literal_kind() == TokenKind::TK_NUMERIC_CONSTANT)
                {
                    // Parse the string value as integer
                    size_t repeat_count = static_cast<size_t>(std::stoull(literal->value()));

                    if (repeat_count == 0)
                    {
                        report_error(ErrorCode::E0111_INVALID_SYNTAX, "Array repeat count must be greater than 0");
                        consume(TokenKind::TK_R_SQUARE, "Expected ']' after array repeat count");
                        return array_literal;
                    }

                    array_literal->set_repeat_count(repeat_count);
                    consume(TokenKind::TK_R_SQUARE, "Expected ']' after array repeat count");
                    return array_literal;
                }
            }

            // Not a literal - use expression-based count (runtime size)
            array_literal->set_repeat_count_expr(std::move(count_expr));

            consume(TokenKind::TK_R_SQUARE, "Expected ']' after array repeat count");
            return array_literal;
        }

        // Normal comma-separated array elements
        while (_current_token.is(TokenKind::TK_COMMA) && !is_at_end())
        {
            advance(); // consume ','

            // Allow trailing comma before ]
            if (_current_token.is(TokenKind::TK_R_SQUARE))
            {
                break;
            }

            auto element = parse_expression();
            if (element)
            {
                array_literal->add_element(std::move(element));
            }
        }

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
        struct_decl->set_name_location(name_token.location());

        // Attach documentation if available
        attach_documentation(struct_decl.get());

        // Parse optional generic parameters
        if (_current_token.is(TokenKind::TK_L_ANGLE))
        {
            auto generics = parse_generic_parameters();
            for (auto &generic : generics)
            {
                // Track generic param names so field type parsing can recognize them
                _current_generic_params.push_back(generic->name());
                struct_decl->add_generic_parameter(std::move(generic));
            }
        }

        // PRE-REGISTER the struct type in the symbol table BEFORE parsing fields.
        // This enables self-referential types like: type struct Node { next: Node*; }
        // Without this, the field type resolution fails because the struct isn't known yet.
        TypeRef struct_type = _context.types().create_struct(QualifiedTypeName{_current_module_id, struct_name});
        _context.symbols().declare_type(struct_name, struct_type, start_loc);
        LOG_DEBUG(LogComponent::PARSER, "Pre-registered struct type '{}' in module {} for self-referential field support",
                  struct_name, _current_module_id.id);

        consume(TokenKind::TK_L_BRACE, "Expected '{' after struct name");

        // Parse struct members (fields and methods)
        Visibility current_visibility = Visibility::Public; // Default for structs

        // Set flags to indicate we're parsing struct members
        _parsing_class_members = true;
        _current_parsing_type_name = struct_name;

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

                // Case 1: regular method - identifier or keyword followed by ( or < (for generic methods)
                // This allows method names like 'new', 'default', etc.
                if ((_current_token.is(TokenKind::TK_IDENTIFIER) || _current_token.is_keyword()) &&
                    (next.is(TokenKind::TK_L_PAREN) || next.is(TokenKind::TK_L_ANGLE)))
                {
                    is_method = true;
                }
                // Case 2: static method - static followed by identifier or keyword (like 'new')
                else if (_current_token.is(TokenKind::TK_KW_STATIC) &&
                         (next.is(TokenKind::TK_IDENTIFIER) || next.is_keyword()))
                {
                    // For static methods, we assume it's a method if static is followed by identifier or keyword
                    // The method parsing will handle the rest
                    is_method = true;
                }
                // Case 3: destructor - ~ followed by identifier
                else if (_current_token.is(TokenKind::TK_TILDE) && next.is(TokenKind::TK_IDENTIFIER))
                {
                    is_method = true;
                }

                if (is_method)
                {
                    auto method = parse_struct_method(struct_name, current_visibility);
                    LOG_DEBUG(LogComponent::PARSER, "[STRUCT_PARSE] {} :: Added method '{}' (total methods: {})",
                              struct_name, method ? method->name() : "null", struct_decl->methods().size() + 1);
                    struct_decl->add_method(std::move(method));
                }
                else
                {
                    auto field = parse_struct_field(current_visibility);
                    LOG_DEBUG(LogComponent::PARSER, "[STRUCT_PARSE] {} :: Added field '{}' of type '{}' (total fields: {})",
                              struct_name, field ? field->name() : "null",
                              field && field->get_resolved_type() ? field->get_resolved_type()->display_name() : "unknown",
                              struct_decl->fields().size() + 1);
                    struct_decl->add_field(std::move(field));
                }
            }
            catch (const ParseError &e)
            {
                _errors.push_back(e);
                synchronize();
            }
        }

        // Clear flags after parsing struct members
        _parsing_class_members = false;
        _current_parsing_type_name.clear();
        _current_generic_params.clear();

        consume(TokenKind::TK_R_BRACE, "Expected '}' after struct body");

        LOG_DEBUG(LogComponent::PARSER, "[STRUCT_PARSE] {} :: COMPLETE - {} fields, {} methods",
                  struct_name, struct_decl->fields().size(), struct_decl->methods().size());
        for (const auto &f : struct_decl->fields())
        {
            LOG_DEBUG(LogComponent::PARSER, "[STRUCT_PARSE] {} :: Field summary: '{}' : '{}'",
                      struct_name, f->name(),
                      f->get_resolved_type() ? f->get_resolved_type()->display_name() : "unknown");
        }

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
        class_decl->set_name_location(name_token.location());

        // Attach documentation if available
        attach_documentation(class_decl.get());

        // Parse optional generic parameters
        if (_current_token.is(TokenKind::TK_L_ANGLE))
        {
            auto generics = parse_generic_parameters();
            for (auto &generic : generics)
            {
                // Track generic param names so field type parsing can recognize them
                _current_generic_params.push_back(generic->name());
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

        // PRE-REGISTER the class type in the symbol table BEFORE parsing fields.
        // This enables self-referential types like: type class Node { next: Node*; }
        TypeRef class_type = _context.types().create_class(QualifiedTypeName{_current_module_id, class_name});
        _context.symbols().declare_type(class_name, class_type, start_loc);
        LOG_DEBUG(LogComponent::PARSER, "Pre-registered class type '{}' in module {} for self-referential field support",
                  class_name, _current_module_id.id);

        consume(TokenKind::TK_L_BRACE, "Expected '{' after class declaration");

        // Parse class members
        Visibility current_visibility = Visibility::Private; // Default for classes

        // Set flags to indicate we're parsing class members
        _parsing_class_members = true;
        _current_parsing_type_name = class_name;

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

                // Case 1: regular method - identifier or keyword followed by ( or < (for generic methods)
                // This allows method names like 'new', 'default', etc.
                if ((_current_token.is(TokenKind::TK_IDENTIFIER) || _current_token.is_keyword()) &&
                    (next.is(TokenKind::TK_L_PAREN) || next.is(TokenKind::TK_L_ANGLE)))
                {
                    // Additional validation: identifiers starting with __ are likely function calls, not methods
                    // Also exclude primitive type names that are likely type casts
                    std::string identifier_text = std::string(_current_token.text());
                    if (identifier_text.length() >= 2 && identifier_text.substr(0, 2) == "__" ||
                        identifier_text == "u8" || identifier_text == "u16" || identifier_text == "u32" || identifier_text == "u64" ||
                        identifier_text == "i8" || identifier_text == "i16" || identifier_text == "i32" || identifier_text == "i64" ||
                        identifier_text == "f32" || identifier_text == "f64" || identifier_text == "bool" || identifier_text == "char")
                    {
                        LOG_DEBUG(LogComponent::PARSER, "[CLASS PARSE] Skipping potential function call/type cast '{}' at line {}",
                                  identifier_text, _current_token.location().line());
                        is_method = false;
                    }
                    else
                    {
                        is_method = true;
                    }
                }
                // Case 2: static method - static followed by identifier or keyword (like 'new')
                else if (_current_token.is(TokenKind::TK_KW_STATIC) &&
                         (next.is(TokenKind::TK_IDENTIFIER) || next.is_keyword()))
                {
                    // For static methods, we assume it's a method if static is followed by identifier or keyword
                    // The method parsing will handle the rest
                    is_method = true;
                }
                // Case 3: destructor - ~ followed by identifier
                else if (_current_token.is(TokenKind::TK_TILDE) && next.is(TokenKind::TK_IDENTIFIER))
                {
                    is_method = true;
                }
                // Case 4: virtual method - virtual followed by identifier
                else if (_current_token.is(TokenKind::TK_KW_VIRTUAL) &&
                         (next.is(TokenKind::TK_IDENTIFIER) || next.is_keyword()))
                {
                    is_method = true;
                }
                // Case 5: override method - override followed by identifier
                else if (_current_token.is(TokenKind::TK_KW_OVERRIDE) &&
                         (next.is(TokenKind::TK_IDENTIFIER) || next.is_keyword()))
                {
                    is_method = true;
                }

                if (is_method)
                {
                    std::string method_name = std::string(_current_token.text());
                    LOG_DEBUG(LogComponent::PARSER, "[CLASS PARSE] About to parse method: '{}' at line {} (_parsing_method_body={})",
                              method_name, _current_token.location().line(), _parsing_method_body);
                    auto method = parse_struct_method(class_name, current_visibility);
                    // Note: We reuse StructMethodNode for class methods
                    class_decl->add_method(std::move(method));
                    LOG_DEBUG(LogComponent::PARSER, "[CLASS PARSE] Finished parsing method: '{}'", method_name);
                }
                else
                {
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

        // Clear flags after parsing class members
        _parsing_class_members = false;
        _current_parsing_type_name.clear();
        _current_generic_params.clear();

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
        trait_decl->set_name_location(name_token.location());

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
                    TypeRef return_type = _context.types().get_void(); // Default to void
                    std::string return_type_string = "void";           // Track the type string for TypeAnnotation
                    SourceLocation return_type_loc;
                    if (_current_token.is(TokenKind::TK_ARROW))
                    {
                        advance(); // consume '->'
                        return_type_loc = _current_token.location();
                        return_type = parse_type_annotation(&return_type_string);
                    }

                    consume(TokenKind::TK_SEMICOLON, "Expected ';' after trait method signature");

                    // Create a function declaration (trait methods are just signatures)
                    auto method_decl = _builder.create_function_declaration(
                        _current_token.location(), method_name, return_type, true); // traits are public

                    // Set the return type annotation from the captured type string
                    // This preserves the original type annotation for later phases
                    if (!return_type_string.empty())
                    {
                        auto annotation = std::make_unique<TypeAnnotation>(
                            TypeAnnotation::named(return_type_string, return_type_loc));
                        method_decl->set_return_type_annotation(std::move(annotation));
                    }

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
        SourceLocation alias_name_loc = _current_token.location();
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
            type_alias->set_name_location(alias_name_loc);

            // Attach documentation if available
            attach_documentation(type_alias.get());

            return type_alias;
        }

        consume(TokenKind::TK_EQUAL, "Expected '=' in type alias");

        // Parse target type
        std::string target_type = parse_type();

        consume(TokenKind::TK_SEMICOLON, "Expected ';' after type alias");

        auto type_alias = _builder.create_type_alias_declaration(start_loc, alias_name, target_type, generic_params);
        type_alias->set_name_location(alias_name_loc);

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
        enum_decl->set_name_location(name_token.location());

        // Attach documentation if available
        attach_documentation(enum_decl.get());

        // Parse generic parameters if present
        if (_current_token.is(TokenKind::TK_L_ANGLE))
        {
            auto generic_params = parse_generic_parameters();
            for (auto &param : generic_params)
            {
                // Track generic param names so variant type parsing can recognize them
                _current_generic_params.push_back(param->name());
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

            // Optional comma or semicolon between variants
            if (_current_token.is(TokenKind::TK_COMMA) || _current_token.is(TokenKind::TK_SEMICOLON))
            {
                advance();
            }
        }

        consume(TokenKind::TK_R_BRACE, "Expected '}' after enum body");

        // Clear generic params after parsing enum body
        _current_generic_params.clear();

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

        // Register the enum type in TypeContext AND SymbolTable so future type resolution can find it
        // Skip registration for generic enums (they'll be handled by template system)
        if (enum_decl->generic_parameters().empty())
        {
            QualifiedTypeName qname{_current_module_id, enum_name};
            TypeRef enum_type = _context.types().create_enum(qname);
            // Register in symbol table so other types in the same file can reference this enum
            _context.symbols().declare_type(enum_name, enum_type, start_loc);
            // TODO: Populate enum variants on the type - currently handled in TypeChecker
            LOG_DEBUG(LogComponent::PARSER, "Registered enum type: {} in module {} (simple={}, variants={})",
                      enum_name, _current_module_id.id, is_simple_enum, variant_names.size());
        }

        return enum_decl;
    }

    std::unique_ptr<EnumVariantNode> Parser::parse_enum_variant()
    {
        SourceLocation start_loc = _current_token.location();

        Token name_token = consume(TokenKind::TK_IDENTIFIER, "Expected enum variant name");
        std::string variant_name = std::string(name_token.text());

        // Check for explicit value assignment (C-style enum with values)
        if (_current_token.is(TokenKind::TK_EQUAL))
        {
            advance(); // consume '='

            if (_current_token.is(TokenKind::TK_NUMERIC_CONSTANT))
            {
                std::string enum_val_text = std::string(_current_token.text());
                enum_val_text.erase(std::remove(enum_val_text.begin(), enum_val_text.end(), '_'), enum_val_text.end()); // Strip underscore separators
                int64_t explicit_value = std::stoll(enum_val_text);
                advance(); // consume the number
                auto variant = _builder.create_enum_variant_with_value(start_loc, variant_name, explicit_value);
                attach_documentation(variant.get());
                return variant;
            }
            else
            {
                error("Expected integer value after '=' in enum variant");
                auto variant = _builder.create_enum_variant(start_loc, variant_name);
                attach_documentation(variant.get());
                return variant;
            }
        }
        // Check if this is a complex variant with associated data
        else if (_current_token.is(TokenKind::TK_L_PAREN))
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

            auto variant = _builder.create_enum_variant(start_loc, variant_name, associated_types);
            attach_documentation(variant.get());
            return variant;
        }
        else
        {
            // Simple variant: NAME_1, NAME_2
            auto variant = _builder.create_enum_variant(start_loc, variant_name);
            attach_documentation(variant.get());
            return variant;
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

            // Parse generic arguments and track them for method type resolution
            do
            {
                if (_current_token.is(TokenKind::TK_IDENTIFIER))
                {
                    std::string param_name = std::string(_current_token.text());
                    target_type += param_name;
                    // Track generic param so method return types can reference it
                    _current_generic_params.push_back(param_name);
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
        impl_block->set_name_location(target_token.location());

        consume(TokenKind::TK_L_BRACE, "Expected '{' after implement declaration");

        // Set context for 'this' parameter resolution in implementation block methods
        _current_parsing_type_name = target_type;

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
                    TypeRef auto_type = resolve_type_from_string("auto"); // Type will be inferred
                    auto field = _builder.create_struct_field(field_token.location(),
                                                              std::string(field_token.text()),
                                                              auto_type,
                                                              Visibility::Public);
                    field->set_default_value(std::move(value));
                    impl_block->add_field_implementation(std::move(field));
                }
                else if (_current_token.is(TokenKind::TK_KW_STATIC))
                {
                    // Static method implementation: static method_name() -> T { ... }
                    auto method = parse_struct_method(target_type);
                    impl_block->add_method_implementation(std::move(method));
                }
                else if ((_current_token.is(TokenKind::TK_IDENTIFIER) || _current_token.is_keyword()) &&
                         (next.is(TokenKind::TK_L_PAREN) || next.is(TokenKind::TK_L_ANGLE)))
                {
                    // Method implementation (including generic methods with <T> syntax)
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

        // Clear context after parsing implementation block
        _current_parsing_type_name.clear();
        _current_generic_params.clear();

        consume(TokenKind::TK_R_BRACE, "Expected '}' after implementation block");

        return impl_block;
    }

    std::unique_ptr<ExternBlockNode> Parser::parse_extern_block()
    {
        SourceLocation start_loc = _current_token.location();

        consume(TokenKind::TK_KW_EXTERN, "Expected 'extern'");

        // Parse linkage specifier (e.g., "C" or "CImport")
        // NOTE: Must capture text BEFORE advance() — Token holds a string_view
        // into lexer memory that advance() can invalidate.
        if (!_current_token.is(TokenKind::TK_STRING_LITERAL))
        {
            error("Expected linkage specifier (e.g., \"C\")");
            return nullptr;
        }
        std::string linkage_type = std::string(_current_token.text());
        advance(); // consume the string literal

        // Remove quotes from linkage type (lexer strips quotes, but be safe)
        if (linkage_type.length() >= 2 && linkage_type.front() == '"' && linkage_type.back() == '"')
        {
            linkage_type = linkage_type.substr(1, linkage_type.length() - 2);
        }

        // Parse optional namespace alias (e.g., "ex" in extern "CImport" ex { ... })
        // Same pattern: capture text before advance.
        std::string namespace_alias;
        if (_current_token.is(TokenKind::TK_IDENTIFIER))
        {
            namespace_alias = std::string(_current_token.text());
            advance();
        }

        auto extern_block = _builder.create_extern_block(start_loc, linkage_type, namespace_alias);

        consume(TokenKind::TK_L_BRACE, "Expected '{' after extern linkage");

        if (linkage_type == "CImport")
        {
            // Parse #include directives within the CImport block
            while (!_current_token.is(TokenKind::TK_R_BRACE) && !is_at_end())
            {
                try
                {
                    // Expect: # include "path"
                    if (_current_token.is(TokenKind::TK_HASH))
                    {
                        advance(); // consume '#'

                        // Expect 'include' identifier
                        if (!_current_token.is(TokenKind::TK_IDENTIFIER) ||
                            std::string(_current_token.text()) != "include")
                        {
                            error("Expected 'include' after '#' in CImport block");
                            synchronize();
                            continue;
                        }
                        advance(); // consume 'include'

                        // Parse the include path string
                        // Capture text before advance to avoid dangling string_view
                        if (!_current_token.is(TokenKind::TK_STRING_LITERAL))
                        {
                            error("Expected string path after #include");
                            synchronize();
                            continue;
                        }
                        std::string include_path = std::string(_current_token.text());
                        advance(); // consume the string literal

                        // Remove quotes from include path (lexer strips quotes, but be safe)
                        if (include_path.length() >= 2 && include_path.front() == '"' && include_path.back() == '"')
                        {
                            include_path = include_path.substr(1, include_path.length() - 2);
                        }

                        extern_block->add_include_path(include_path);
                    }
                    else
                    {
                        error("Expected '#include' directive in CImport block");
                        synchronize();
                    }
                }
                catch (const ParseError &e)
                {
                    _errors.push_back(e);
                    synchronize();
                }
            }
        }
        else
        {
            // Parse function declarations within the extern block (existing behavior)
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
        // Use the visibility passed from the calling context
        // Don't try to parse visibility again if it was already parsed in the outer scope
        // WORKAROUND: Force struct fields to always be public to avoid LLVM codegen crash
        // TODO: Fix the root cause in TypeMapper/CodegenVisitor for private struct members
        Visibility visibility = Visibility::Public;

        Token field_token = consume(TokenKind::TK_IDENTIFIER, "Expected field name");
        std::string field_name = std::string(field_token.text());
        SourceLocation field_loc = field_token.location();

        consume(TokenKind::TK_COLON, "Expected ':' after field name");

        // Capture location of the type token for LSP hover and deferred resolution
        SourceLocation type_loc = _current_token.location();

        // Capture the type string for deferred resolution
        std::string type_string;
        TypeRef field_type = parse_type_annotation(&type_string);

        std::unique_ptr<StructFieldNode> field;

        // If type resolution failed, create a TypeAnnotation for deferred resolution
        if (field_type.is_error())
        {
            LOG_DEBUG(LogComponent::PARSER, "Struct field '{}' type '{}' deferred to type resolution phase",
                      field_name, type_string);
            auto annotation = std::make_unique<TypeAnnotation>(TypeAnnotation::named(type_string, type_loc));
            field = _builder.create_struct_field(field_loc, field_name, std::move(annotation), visibility);
        }
        else
        {
            // Even with resolved type, store annotation for LSP hover on the type name
            auto annotation = std::make_unique<TypeAnnotation>(TypeAnnotation::named(type_string, type_loc));
            field = _builder.create_struct_field(field_loc, field_name, std::move(annotation), visibility);
            field->set_resolved_type(field_type);
        }

        // Parse optional default value
        if (_current_token.is(TokenKind::TK_EQUAL))
        {
            advance(); // consume '='
            auto default_value = parse_expression();
            field->set_default_value(std::move(default_value));
        }

        attach_documentation(field.get());
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

        // Check for virtual keyword
        bool is_virtual = false;
        if (_current_token.is(TokenKind::TK_KW_VIRTUAL))
        {
            is_virtual = true;
            advance(); // consume 'virtual'
        }

        // Check for override keyword
        bool is_override = false;
        if (_current_token.is(TokenKind::TK_KW_OVERRIDE))
        {
            is_override = true;
            advance(); // consume 'override'
        }

        // Check for constructor
        bool is_constructor = false;

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
            LOG_DEBUG(LogComponent::PARSER, "[CONSTRUCTOR] Detected constructor: method_name='{}' matches struct_name='{}'",
                      method_name, struct_name);
            is_constructor = true;
        }

        // Parse optional generic parameters (e.g., <T> or <T, U>)
        std::vector<std::unique_ptr<GenericParameterNode>> method_generics;
        size_t method_generic_count = 0; // Track how many we add for cleanup
        if (_current_token.is(TokenKind::TK_L_ANGLE))
        {
            method_generics = parse_generic_parameters();
            LOG_DEBUG(LogComponent::PARSER, "[METHOD] Parsed {} generic parameters for method '{}'",
                      method_generics.size(), method_name);
            // Track method's generic params so param/return type parsing can recognize them
            for (const auto &generic : method_generics)
            {
                _current_generic_params.push_back(generic->name());
                method_generic_count++;
            }
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

        // Parse constructor initialization list (for inheritance)
        std::string base_ctor_name;
        std::vector<std::unique_ptr<ExpressionNode>> base_ctor_args;
        if (is_constructor && _current_token.is(TokenKind::TK_COLON))
        {
            advance(); // consume ':'
            // Parse base class constructor call: BaseClass(args)
            if (_current_token.is(TokenKind::TK_IDENTIFIER) || _current_token.is_keyword())
            {
                base_ctor_name = std::string(_current_token.text());
                advance(); // consume base class name
                if (_current_token.is(TokenKind::TK_L_PAREN))
                {
                    advance(); // consume '('
                    // Parse arguments as expressions
                    while (!_current_token.is(TokenKind::TK_R_PAREN) && !is_at_end())
                    {
                        base_ctor_args.push_back(parse_expression());
                        if (_current_token.is(TokenKind::TK_COMMA))
                            advance(); // consume ','
                    }
                    consume(TokenKind::TK_R_PAREN, "Expected ')' after base constructor args");
                }
            }
        }

        // Parse return type (optional for constructors)
        TypeRef return_type = _context.types().get_void(); // Default to void
        std::string return_type_string = "void";           // Track the type string for TypeAnnotation
        SourceLocation return_type_loc;
        if (_current_token.is(TokenKind::TK_ARROW))
        {
            advance(); // consume '->'
            return_type_loc = _current_token.location();
            return_type = parse_type_annotation(&return_type_string);
        }
        else if (is_constructor)
        {
            // Constructors return void - they operate on a 'this' pointer passed as first arg
            return_type = _context.types().get_void();
            return_type_string = "void";
        }

        // Check for default destructor syntax: ~TypeName() default;
        bool is_default_destructor = false;
        if (is_destructor && _current_token.is(TokenKind::TK_KW_DEFAULT))
        {
            is_default_destructor = true;
            advance(); // consume 'default'
        }

        auto method = _builder.create_struct_method(start_loc, method_name, return_type, visibility, is_constructor, is_destructor, is_static, is_default_destructor);
        method->set_name_location(method_token.location());
        method->set_virtual(is_virtual);
        method->set_override(is_override);
        attach_documentation(method.get());

        // Set base constructor call info (for inheritance)
        if (!base_ctor_name.empty())
        {
            method->set_base_ctor_name(base_ctor_name);
            for (auto &arg : base_ctor_args)
            {
                method->add_base_ctor_arg(std::move(arg));
            }
        }

        // Set the return type annotation from the captured type string
        // This preserves the original type annotation (e.g., "Option<u64>") for later phases
        if (!return_type_string.empty())
        {
            auto annotation = std::make_unique<TypeAnnotation>(
                TypeAnnotation::named(return_type_string, return_type_loc));
            method->set_return_type_annotation(std::move(annotation));
            LOG_DEBUG(LogComponent::PARSER, "PARSER: Set return type annotation '{}' for struct method '{}'",
                      return_type_string, method_name);
        }
        else
        {
            LOG_ERROR(LogComponent::PARSER, "PARSER: No return type string for struct method '{}' (empty string)",
                      method_name);
        }

        // Set variadic flag if detected
        method->set_variadic(is_variadic);

        // Add generic parameters
        for (auto &generic : method_generics)
        {
            method->add_generic_parameter(std::move(generic));
        }

        // Add parameters
        for (auto &param : params)
        {
            method->add_parameter(std::move(param));
        }

        // Parse optional body (for method implementations)
        if (_current_token.is(TokenKind::TK_L_BRACE))
        {
            // Parse method body properly
            try
            {
                // Set flag to indicate we're parsing a method body
                // This affects synchronize() behavior to prevent escaping method boundaries
                _parsing_method_body = true;

                // Reset brace depth and set to 1 since we'll consume the opening brace
                // This ensures clean brace tracking for this method
                _brace_depth = 0; // Reset first
                _brace_depth = 1; // Then set to 1 for the method's opening brace

                auto body = parse_block_statement();
                method->set_body(std::unique_ptr<BlockStatementNode>(
                    dynamic_cast<BlockStatementNode *>(body.release())));

                // Clear flag after method body is parsed
                LOG_DEBUG(LogComponent::PARSER, "[METHOD] Clearing _parsing_method_body flag after method '{}' at line {}",
                          method_name, _current_token.location().line());
                _parsing_method_body = false;
                _brace_depth = 0;
            }
            catch (const ParseError &e)
            {
                _parsing_method_body = false; // Ensure flag is cleared even on error
                _brace_depth = 0;             // Reset brace depth
                // Don't re-throw - just log the error and continue
                // The method was already created with a partial body, which is better than
                // not adding the method to the class at all
                _errors.push_back(e);
                LOG_DEBUG(LogComponent::PARSER, "Error parsing method body for '{}': {}. Attempting recovery...", method_name, e.what());

                // Recovery: try to find the end of this method body by counting braces
                // synchronize() has already positioned us at a recovery point (likely a closing brace)
                // We need to count braces to find the matching closing brace of this method
                int brace_count = 0;

                // CRITICAL FIX: Check if we're ALREADY at the closing brace of the method
                // This happens when synchronize() returns without consuming the brace
                if (_current_token.is(TokenKind::TK_R_BRACE))
                {
                    // We're already at what should be the closing brace of the method
                    // Consume it and we're done - this is the normal case
                    advance();
                    LOG_DEBUG(LogComponent::PARSER, "Recovery: Already positioned at method closing brace, consumed it");
                    // Clean up method-level generic params before early return
                    for (size_t i = 0; i < method_generic_count; ++i)
                    {
                        if (!_current_generic_params.empty())
                        {
                            _current_generic_params.pop_back();
                        }
                    }
                    return method;
                }

                // If we're NOT at a brace, we need to scan forward
                // Start with brace_count = 1 assuming we're inside the method body
                brace_count = 1;

                // Skip tokens until we find the matching closing brace
                int tokens_consumed = 0;
                while (!is_at_end() && brace_count > 0 && tokens_consumed < 1000)
                {
                    if (_current_token.is(TokenKind::TK_L_BRACE))
                    {
                        brace_count++;
                    }
                    else if (_current_token.is(TokenKind::TK_R_BRACE))
                    {
                        brace_count--;
                        if (brace_count == 0)
                        {
                            // Found the closing brace of the method - consume it
                            advance();
                            LOG_DEBUG(LogComponent::PARSER, "Recovery: Found method closing brace after {} tokens", tokens_consumed);
                            break;
                        }
                    }
                    advance();
                    tokens_consumed++;
                }

                if (brace_count > 0)
                {
                    LOG_DEBUG(LogComponent::PARSER, "Recovery: Could not find method closing brace after {} tokens", tokens_consumed);
                }
            }

            // OLD COMPLEX PARSING - Replaced with simple token consumption above
            /*
            try {
                auto body = parse_block_statement();
                method->set_body(std::unique_ptr<BlockStatementNode>(
                    dynamic_cast<BlockStatementNode *>(body.release())));
            } catch (const ParseError &e) {
                _errors.push_back(e);

                // IMPROVED: Method-body-specific error recovery
                // Don't use synchronize() here as it's too aggressive for method bodies
                int brace_count = 0;
                SourceLocation recovery_start = _current_token.location();

                // Count opening brace
                if (_current_token.is(TokenKind::TK_L_BRACE)) {
                    brace_count = 1;
                    advance();
                } else {
                    // If we're not at an opening brace, we need to find the start of the method body
                    while (!is_at_end() && !_current_token.is(TokenKind::TK_L_BRACE)) {
                        advance();
                    }
                    if (_current_token.is(TokenKind::TK_L_BRACE)) {
                        brace_count = 1;
                        advance();
                    }
                }

                // Skip tokens until we find ONLY the matching closing brace for this method
                // Use brace depth tracking to avoid consuming private: sections
                size_t tokens_consumed = 0;
                while (!is_at_end() && brace_count > 0 && tokens_consumed < 1000) { // Prevent infinite loops
                    if (_current_token.is(TokenKind::TK_L_BRACE)) {
                        brace_count++;
                    } else if (_current_token.is(TokenKind::TK_R_BRACE)) {
                        brace_count--;
                        // CRITICAL: If we've consumed the method's closing brace, stop here
                        // Don't continue to the private: section
                        if (brace_count == 0) {
                            advance(); // Consume the final closing brace
                            break;
                        }
                    }
                    advance();
                    tokens_consumed++;
                }

                LOG_DEBUG(LogComponent::PARSER, "Method body error recovery: consumed {} tokens, final brace_count={}",
                         tokens_consumed, brace_count);

                // Create an empty block as fallback
                auto empty_block = _builder.create_block_statement(body_start);
                method->set_body(std::move(empty_block));
            }
            */
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

        // Clean up method-level generic params from the tracking list
        for (size_t i = 0; i < method_generic_count; ++i)
        {
            if (!_current_generic_params.empty())
            {
                _current_generic_params.pop_back();
            }
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
        // Check if we have buffered tokens from previous lookahead
        // This ensures synchronization with peek_next_n() which uses _lookahead_buffer
        if (!_lookahead_buffer.empty())
        {
            return _lookahead_buffer.front();
        }
        // Use the lexer's built-in peek functionality
        return _lexer->peek_token();
    }

    Token Parser::peek_next_n(int n)
    {
        // Multi-token lookahead using buffer
        if (n <= 0)
        {
            return _current_token;
        }

        // Ensure buffer has enough tokens (skip comments when buffering)
        while (static_cast<int>(_lookahead_buffer.size()) < n)
        {
            if (!_lexer->has_more_tokens())
            {
                // Fill remaining with EOF
                _lookahead_buffer.push_back(Token(TokenKind::TK_EOF, "", SourceLocation{}));
            }
            else
            {
                Token tok = _lexer->next_token();
                // Skip comment tokens in lookahead (consistent with advance())
                while ((tok.is(TokenKind::TK_COMMENT) ||
                        tok.is(TokenKind::TK_DOC_COMMENT_BLOCK) ||
                        tok.is(TokenKind::TK_DOC_COMMENT_LINE)) &&
                       _lexer->has_more_tokens())
                {
                    tok = _lexer->next_token();
                }
                _lookahead_buffer.push_back(tok);
            }
        }

        return _lookahead_buffer[n - 1];
    }

    bool Parser::is_generic_call_ahead()
    {
        // Scans ahead from current position (at '<') to determine if this is a generic expression.
        // A generic expression follows one of these patterns:
        //   - <type_args>(  - generic function call or constructor
        //   - <type_args>{  - generic struct literal
        //   - <type_args>:: - generic type with scope resolution
        // A comparison follows the pattern: < expr (without closing > followed by one of the above)
        //
        // Strategy: Track angle bracket depth, looking for matching '>' followed by '(', '{', or '::'.
        // If we find one of those patterns, it's a generic expression.
        // If we hit certain tokens that can't appear in type arguments, it's a comparison.

        int pos = 1; // Start looking after '<' (peek_next_n(1) = first token after current '<')
        int angle_depth = 1;
        const int max_lookahead = 50; // Reasonable limit to prevent infinite scanning

        while (pos <= max_lookahead)
        {
            Token tok = peek_next_n(pos);

            if (tok.is(TokenKind::TK_EOF))
            {
                // Hit end of file - definitely not a valid generic call
                return false;
            }

            if (tok.is(TokenKind::TK_L_ANGLE))
            {
                angle_depth++;
            }
            else if (tok.is(TokenKind::TK_R_ANGLE))
            {
                angle_depth--;
                if (angle_depth == 0)
                {
                    // Found matching '>'. Check if next token indicates a generic expression
                    Token next_tok = peek_next_n(pos + 1);
                    return next_tok.is(TokenKind::TK_L_PAREN) ||  // Generic call: Type<T>()
                           next_tok.is(TokenKind::TK_L_BRACE) ||  // Generic struct literal: Type<T> { ... }
                           next_tok.is(TokenKind::TK_COLONCOLON); // Generic scope resolution: Type<T>::method()
                }
            }
            else if (tok.is(TokenKind::TK_SEMICOLON) ||
                     tok.is(TokenKind::TK_R_BRACE) ||
                     tok.is(TokenKind::TK_R_PAREN) ||
                     tok.is(TokenKind::TK_EQUAL) ||
                     tok.is(TokenKind::TK_EXCLAIMEQUAL) ||
                     tok.is(TokenKind::TK_PIPE) ||
                     tok.is(TokenKind::TK_PLUS) ||
                     tok.is(TokenKind::TK_MINUS) ||
                     tok.is(TokenKind::TK_SLASH) ||
                     tok.is(TokenKind::TK_PERCENT) ||
                     tok.is(TokenKind::TK_KW_IF) ||
                     tok.is(TokenKind::TK_KW_WHILE) ||
                     tok.is(TokenKind::TK_KW_FOR) ||
                     tok.is(TokenKind::TK_KW_RETURN))
            {
                // These tokens cannot appear inside generic type arguments
                // If we see them before finding a valid generic suffix, it's a comparison
                // Note: TK_STAR is not included since * can be a pointer type modifier
                // Note: TK_L_BRACE, TK_L_ANGLE, TK_AMP removed - they can appear in nested generics or types
                return false;
            }

            // Tokens that ARE valid in generic type args:
            // - Identifiers (type names)
            // - Type keywords (i32, u64, string, etc.)
            // - Comma (separating type args)
            // - Star (pointer types)
            // - Ampersand (reference types)
            // - Square brackets (array types)
            // - Colons (for scope resolution like std::Vec)
            // - L_ANGLE (nested generics)
            // - L_BRACE (should not appear but let the matching > check handle it)

            pos++;
        }

        // Exceeded lookahead limit without finding definitive answer
        // Default to treating as comparison (safer)
        return false;
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

    TypeRef Parser::parse_type_annotation_with_tokens(std::string *out_type_string)
    {
        // Create a string stream to build type tokens
        std::string type_string = "";
        std::vector<Token> collected_tokens;
        int angle_bracket_depth = 0;

        LOG_DEBUG(LogComponent::PARSER, "parse_type_annotation_with_tokens() starting, current token: {} ({})",
                  static_cast<int>(_current_token.kind()), std::string(_current_token.text()));

        // Track parenthesis depth for function types like () -> T or (T, U) -> V
        int paren_depth = 0;

        // Collect tokens that form the complete type expression
        // This handles complex types like: const int**, Option<Result<T, E>>, u64[5], () -> T, etc.
        while (is_type_token() ||
               _current_token.is(TokenKind::TK_L_ANGLE) ||
               _current_token.is(TokenKind::TK_R_ANGLE) ||
               _current_token.is(TokenKind::TK_GREATERGREATER) || // Handle >> in nested generics
               _current_token.is(TokenKind::TK_L_SQUARE) ||
               _current_token.is(TokenKind::TK_R_SQUARE) ||
               _current_token.is(TokenKind::TK_NUMERIC_CONSTANT) ||
               _current_token.is(TokenKind::TK_STAR) ||
               _current_token.is(TokenKind::TK_AMP) ||
               _current_token.is(TokenKind::TK_COMMA) ||
               _current_token.is(TokenKind::TK_COLONCOLON) ||
               _current_token.is(TokenKind::TK_KW_CONST) ||
               _current_token.is(TokenKind::TK_KW_MUT) ||
               _current_token.is(TokenKind::TK_IDENTIFIER) ||
               _current_token.is(TokenKind::TK_L_PAREN) || // Function types: () -> T
               _current_token.is(TokenKind::TK_R_PAREN) || // Function types: (T) -> U
               _current_token.is(TokenKind::TK_ARROW))     // Function types: T -> U
        {
            // Track angle bracket depth
            if (_current_token.is(TokenKind::TK_L_ANGLE))
            {
                angle_bracket_depth++;
            }
            else if (_current_token.is(TokenKind::TK_R_ANGLE))
            {
                angle_bracket_depth--;
            }
            else if (_current_token.is(TokenKind::TK_GREATERGREATER))
            {
                // >> represents two closing angle brackets in generic types
                angle_bracket_depth -= 2;
            }

            // Track parenthesis depth for function types
            if (_current_token.is(TokenKind::TK_L_PAREN))
            {
                paren_depth++;
            }
            else if (_current_token.is(TokenKind::TK_R_PAREN))
            {
                paren_depth--;
                // If we close more parens than we opened, this ) belongs to outer context
                if (paren_depth < 0)
                {
                    break;
                }
            }

            LOG_DEBUG(LogComponent::PARSER, "Collecting token: {} ({})",
                      static_cast<int>(_current_token.kind()), std::string(_current_token.text()));

            collected_tokens.push_back(_current_token);
            type_string += std::string(_current_token.text());
            advance();

            // Break on certain terminators, but only if we're not inside angle brackets or parentheses
            if (angle_bracket_depth == 0 && paren_depth == 0 &&
                (_current_token.is(TokenKind::TK_SEMICOLON) ||
                 _current_token.is(TokenKind::TK_COMMA) ||
                 _current_token.is(TokenKind::TK_R_PAREN) ||
                 _current_token.is(TokenKind::TK_R_BRACE) ||
                 _current_token.is(TokenKind::TK_EQUAL) ||
                 _current_token.is_eof()))
            {
                break;
            }
        }

        LOG_DEBUG(LogComponent::PARSER, "Collected {} tokens for type: '{}'", collected_tokens.size(), type_string);

        if (collected_tokens.empty())
        {
            // Instead of throwing an error, handle the case gracefully
            LOG_DEBUG(LogComponent::PARSER, "No type tokens collected, current token is: {} ({})",
                      static_cast<int>(_current_token.kind()), std::string(_current_token.text()));

            // We must advance at least one token to prevent infinite loops
            SourceLocation error_location = _current_token.location();
            TokenKind error_token = _current_token.kind();
            advance(); // Always advance to prevent infinite loops

            // Report appropriate error based on what we encountered
            if (is_statement_start(error_token) || error_token == TokenKind::TK_EOF)
            {
                LOG_DEBUG(LogComponent::PARSER, "Advanced past statement start or EOF that appeared in type context");
                if (_diagnostics)
                {
                    Span span = Span::at(error_location, _source_file);
                    _diagnostics->emit(
                        Diag::error(ErrorCode::E0111_INVALID_SYNTAX, "expected type annotation")
                            .at(span)
                            .with_note("found statement keyword - possible missing type or misplaced statement"));
                }
            }
            else
            {
                // For other unexpected tokens, report generic error
                if (_diagnostics)
                {
                    Span span = Span::at(error_location, _source_file);
                    _diagnostics->emit(
                        Diag::error(ErrorCode::E0111_INVALID_SYNTAX, "expected valid type annotation")
                            .at(span));
                }
            }

            return _context.types().create_error("invalid_type_annotation", error_location);
        }

        // Use string-based parsing
        // Trim whitespace
        type_string.erase(type_string.find_last_not_of(" \t\n\r\f\v") + 1);

        // Store the type string if caller requested it
        // This preserves the original type annotation for later use (e.g., in struct methods)
        if (out_type_string)
        {
            *out_type_string = type_string;
        }

        TypeRef parsed_type = resolve_type_from_string(type_string);

        if (!parsed_type.is_valid())
        {
            return _context.types().create_error("unresolved_type: " + type_string, SourceLocation{});
        }

        return parsed_type;
    }

    TypeRef Parser::resolve_type_from_string(const std::string &type_str)
    {
        // Handle reference types - check if type starts with '&'
        // This handles types like &T, &mut T, &i32, &mut String, etc.
        if (!type_str.empty() && type_str[0] == '&')
        {
            bool is_mutable = false;
            std::string base_type_str;

            // Check for &mut T pattern
            if (type_str.length() > 4 && type_str.substr(0, 4) == "&mut")
            {
                is_mutable = true;
                // Skip "&mut" and any whitespace
                size_t start = 4;
                while (start < type_str.length() && (type_str[start] == ' ' || type_str[start] == '\t'))
                {
                    start++;
                }
                base_type_str = type_str.substr(start);
            }
            else
            {
                // Just & (immutable reference)
                // Skip "&" and any whitespace
                size_t start = 1;
                while (start < type_str.length() && (type_str[start] == ' ' || type_str[start] == '\t'))
                {
                    start++;
                }
                base_type_str = type_str.substr(start);
            }

            LOG_DEBUG(LogComponent::PARSER, "Resolving reference type '{}': base='{}', mutable={}",
                      type_str, base_type_str, is_mutable);

            // Recursively resolve the base type
            TypeRef base_type = resolve_type_from_string(base_type_str);

            // If base type failed to resolve, propagate the error
            if (!base_type.is_valid() || base_type.is_error())
            {
                LOG_DEBUG(LogComponent::PARSER, "Base type '{}' could not be resolved, deferring reference type '{}'",
                          base_type_str, type_str);
                return _context.types().create_error("unresolved type: " + type_str, SourceLocation{});
            }

            // Create the reference type
            TypeRef result = is_mutable ? _context.types().get_mut_reference_to(base_type)
                                        : _context.types().get_reference_to(base_type);

            LOG_DEBUG(LogComponent::PARSER, "Successfully resolved reference type '{}'", type_str);
            return result;
        }

        // Handle pointer types - check if type ends with '*'
        // This handles types like u8*, ArenaChunk*, int**, etc.
        if (!type_str.empty() && type_str.back() == '*')
        {
            // Count trailing asterisks to handle multi-level pointers
            size_t ptr_count = 0;
            size_t pos = type_str.size();
            while (pos > 0 && type_str[pos - 1] == '*')
            {
                ptr_count++;
                pos--;
            }

            // Get the base type string (without the asterisks)
            std::string base_type_str = type_str.substr(0, pos);

            LOG_DEBUG(LogComponent::PARSER, "Resolving pointer type '{}': base='{}', ptr_levels={}",
                      type_str, base_type_str, ptr_count);

            // Recursively resolve the base type
            TypeRef base_type = resolve_type_from_string(base_type_str);

            // If base type failed to resolve, propagate the error
            if (!base_type.is_valid() || base_type.is_error())
            {
                LOG_DEBUG(LogComponent::PARSER, "Base type '{}' could not be resolved, deferring pointer type '{}'",
                          base_type_str, type_str);
                return _context.types().create_error("unresolved type: " + type_str, SourceLocation{});
            }

            // Wrap the base type in pointer types
            TypeRef result = base_type;
            for (size_t i = 0; i < ptr_count; i++)
            {
                result = _context.types().get_pointer_to(result);
            }

            LOG_DEBUG(LogComponent::PARSER, "Successfully resolved pointer type '{}'", type_str);
            return result;
        }

        // Handle fixed-size array types - e.g., u8[4], i32[10], etc.
        // Array types end with [size] where size is a number
        size_t bracket_pos = type_str.find('[');
        if (bracket_pos != std::string::npos && type_str.back() == ']')
        {
            std::string base_type_str = type_str.substr(0, bracket_pos);
            std::string size_str = type_str.substr(bracket_pos + 1, type_str.size() - bracket_pos - 2);

            LOG_DEBUG(LogComponent::PARSER, "Resolving array type '{}': base='{}', size_str='{}'",
                      type_str, base_type_str, size_str);

            // Parse the size
            std::optional<size_t> array_size;
            if (!size_str.empty())
            {
                try
                {
                    array_size = std::stoull(size_str);
                }
                catch (...)
                {
                    LOG_DEBUG(LogComponent::PARSER, "Failed to parse array size '{}' for type '{}'",
                              size_str, type_str);
                    return _context.types().create_error("invalid array size: " + type_str, SourceLocation{});
                }
            }

            // Recursively resolve the base type
            TypeRef base_type = resolve_type_from_string(base_type_str);

            // If base type failed to resolve, propagate the error
            if (!base_type.is_valid() || base_type.is_error())
            {
                LOG_DEBUG(LogComponent::PARSER, "Base type '{}' could not be resolved, deferring array type '{}'",
                          base_type_str, type_str);
                return _context.types().create_error("unresolved type: " + type_str, SourceLocation{});
            }

            // Create the array type
            TypeRef result = _context.types().get_array_of(base_type, array_size);
            LOG_DEBUG(LogComponent::PARSER, "Successfully resolved array type '{}'", type_str);
            return result;
        }

        // Check if this is a generic type parameter (e.g., T, U, E)
        // These should be recognized within the context of a generic struct/class/enum/impl
        for (size_t i = 0; i < _current_generic_params.size(); ++i)
        {
            if (type_str == _current_generic_params[i])
            {
                LOG_DEBUG(LogComponent::PARSER, "Resolved '{}' as generic parameter at index {}", type_str, i);
                return _context.types().create_generic_param(type_str, i);
            }
        }

        // Handle basic built-in types first
        if (type_str == "void")
            return _context.types().get_void();
        if (type_str == "int" || type_str == "i32")
            return _context.types().get_i32();
        if (type_str == "i8")
            return _context.types().get_i8();
        if (type_str == "i16")
            return _context.types().get_i16();
        if (type_str == "i64")
            return _context.types().get_i64();
        if (type_str == "i128")
            return _context.types().get_i128();
        // Unsigned integer types
        if (type_str == "u8")
            return _context.types().get_u8();
        if (type_str == "u16")
            return _context.types().get_u16();
        if (type_str == "u32")
            return _context.types().get_u32();
        if (type_str == "u64")
            return _context.types().get_u64();
        if (type_str == "u128")
            return _context.types().get_u128();
        if (type_str == "f32")
            return _context.types().get_f32();
        if (type_str == "f64")
            return _context.types().get_f64();
        if (type_str == "float")
            return _context.types().get_f32();
        if (type_str == "double")
            return _context.types().get_f64();
        if (type_str == "boolean")
            return _context.types().get_bool();
        if (type_str == "char")
            return _context.types().get_char();
        if (type_str == "string")
            return _context.types().get_string();
        // For 'auto' and variadic types, these will be resolved during type inference
        // Return void as placeholder for now - the type annotation will carry the info
        if (type_str == "auto")
            return _context.types().get_void(); // Placeholder for auto type inference
        if (type_str == "...")
            return _context.types().get_void(); // Placeholder for variadic type

        // For generic types (e.g., "Array<int>"), defer to type resolution phase
        // The TypeAnnotation stored on the node will contain the full type info
        // Exclude function types like "()->Maybe<T>" or "(int)->Array<T>" which start with '(' and contain '->'
        if (type_str.find('<') != std::string::npos && type_str.find('>') != std::string::npos
            && !(type_str[0] == '(' && type_str.find("->") != std::string::npos))
        {
            LOG_DEBUG(LogComponent::PARSER, "Parser detected generic type syntax: '{}' - deferring to type resolution", type_str);
            // Return an error type with the type string - TypeResolver will handle it
            return _context.types().create_error("unresolved generic: " + type_str, SourceLocation{});
        }

        // For tuple types (e.g., "(i32, i32)", "(string, i64)"), parse properly
        // Tuple types start with '(' and don't contain '->' (which would make it a function type)
        if (!type_str.empty() && type_str[0] == '(' && type_str.back() == ')' && type_str.find("->") == std::string::npos)
        {
            LOG_DEBUG(LogComponent::PARSER, "Parser parsing tuple type syntax: '{}'", type_str);

            // Extract the content inside parentheses
            std::string inner = type_str.substr(1, type_str.size() - 2);

            // If inner is empty, this is the unit type ()
            // The unit type is distinct from void - it's a real type that can be passed/stored
            if (inner.empty())
            {
                return _context.types().get_unit();
            }

            // Parse comma-separated types
            std::vector<TypeRef> element_types;
            size_t start = 0;
            int paren_depth = 0;
            int angle_depth = 0;

            for (size_t i = 0; i <= inner.size(); ++i)
            {
                if (i < inner.size())
                {
                    char c = inner[i];
                    if (c == '(')
                        paren_depth++;
                    else if (c == ')')
                        paren_depth--;
                    else if (c == '<')
                        angle_depth++;
                    else if (c == '>')
                        angle_depth--;
                }

                // Split on comma only when not inside nested parens or angle brackets
                if (i == inner.size() || (inner[i] == ',' && paren_depth == 0 && angle_depth == 0))
                {
                    std::string elem_str = inner.substr(start, i - start);
                    // Trim whitespace
                    size_t elem_start = elem_str.find_first_not_of(" \t");
                    size_t elem_end = elem_str.find_last_not_of(" \t");
                    if (elem_start != std::string::npos)
                    {
                        elem_str = elem_str.substr(elem_start, elem_end - elem_start + 1);
                    }

                    if (!elem_str.empty())
                    {
                        TypeRef elem_type = resolve_type_from_string(elem_str);
                        if (!elem_type.is_valid() || elem_type.is_error())
                        {
                            LOG_DEBUG(LogComponent::PARSER, "Failed to resolve tuple element type: '{}'", elem_str);
                            return _context.types().create_error("unresolved tuple element: " + elem_str, SourceLocation{});
                        }
                        element_types.push_back(elem_type);
                    }

                    start = i + 1;
                }
            }

            // If only one element and no trailing comma, treat as parenthesized type
            if (element_types.size() == 1)
            {
                return element_types[0];
            }

            // Create tuple type
            return _context.types().get_tuple(element_types);
        }

        // For function types (e.g., "()->R", "(T)->U", "(A,B)->R"), parse properly
        // Function types are identified by starting with '(' and containing '->'
        if (!type_str.empty() && type_str[0] == '(' && type_str.find("->") != std::string::npos)
        {
            LOG_DEBUG(LogComponent::PARSER, "Parser parsing function type syntax: '{}'", type_str);

            // Find the closing paren that matches the opening one
            int depth = 0;
            size_t params_end = 0;
            for (size_t i = 0; i < type_str.size(); ++i)
            {
                if (type_str[i] == '(')
                    depth++;
                else if (type_str[i] == ')')
                {
                    depth--;
                    if (depth == 0)
                    {
                        params_end = i;
                        break;
                    }
                }
            }

            if (params_end == 0)
            {
                LOG_DEBUG(LogComponent::PARSER, "Function type '{}' has unbalanced parens", type_str);
                return _context.types().create_error("invalid function type: " + type_str, SourceLocation{});
            }

            // Extract parameter list and return type
            std::string params_str = type_str.substr(1, params_end - 1); // Content inside ()
            std::string rest = type_str.substr(params_end + 1);

            // Find -> and extract return type
            size_t arrow_pos = rest.find("->");
            if (arrow_pos == std::string::npos)
            {
                LOG_DEBUG(LogComponent::PARSER, "Function type '{}' missing arrow", type_str);
                return _context.types().create_error("invalid function type: " + type_str, SourceLocation{});
            }

            std::string return_type_str = rest.substr(arrow_pos + 2);
            // Trim whitespace from return type
            while (!return_type_str.empty() && std::isspace(static_cast<unsigned char>(return_type_str.front())))
                return_type_str.erase(0, 1);
            while (!return_type_str.empty() && std::isspace(static_cast<unsigned char>(return_type_str.back())))
                return_type_str.pop_back();

            LOG_DEBUG(LogComponent::PARSER, "Function type params='{}', return='{}'", params_str, return_type_str);

            // Parse parameter types
            std::vector<TypeRef> param_types;
            if (!params_str.empty())
            {
                // Split by comma, respecting nested types
                int nested_depth = 0;
                size_t start = 0;
                for (size_t i = 0; i <= params_str.size(); ++i)
                {
                    if (i == params_str.size() || (params_str[i] == ',' && nested_depth == 0))
                    {
                        std::string param_type_str = params_str.substr(start, i - start);
                        // Trim whitespace
                        while (!param_type_str.empty() && std::isspace(static_cast<unsigned char>(param_type_str.front())))
                            param_type_str.erase(0, 1);
                        while (!param_type_str.empty() && std::isspace(static_cast<unsigned char>(param_type_str.back())))
                            param_type_str.pop_back();

                        if (!param_type_str.empty())
                        {
                            TypeRef param_type = resolve_type_from_string(param_type_str);
                            if (param_type.is_error())
                            {
                                LOG_DEBUG(LogComponent::PARSER, "Function type param '{}' failed to resolve", param_type_str);
                                return param_type; // Propagate error
                            }
                            param_types.push_back(param_type);
                        }
                        start = i + 1;
                    }
                    else if (params_str[i] == '<' || params_str[i] == '(')
                    {
                        nested_depth++;
                    }
                    else if (params_str[i] == '>' || params_str[i] == ')')
                    {
                        nested_depth--;
                    }
                }
            }

            // Parse return type
            TypeRef return_type = resolve_type_from_string(return_type_str);
            if (return_type.is_error())
            {
                // Don't propagate the error — create the FunctionType with the unresolved return type.
                // All function types lower to opaque ptr in LLVM, so the return type doesn't affect
                // LLVM type lowering. TypeResolution will resolve the return type later.
                LOG_DEBUG(LogComponent::PARSER, "Function type return type '{}' is unresolved, creating function type with placeholder", return_type_str);
            }
            else
            {
                LOG_DEBUG(LogComponent::PARSER, "Successfully parsed function type: {} params, return='{}'",
                          param_types.size(), return_type->display_name());
            }

            return _context.types().get_function(return_type, std::move(param_types), false);
        }

        // Try to resolve as a struct type
        TypeRef struct_type = _context.symbols().lookup_struct_type(type_str);
        if (struct_type.is_valid())
        {
            return struct_type;
        }

        // Try to resolve as a class type
        TypeRef class_type = _context.symbols().lookup_class_type(type_str);
        if (class_type.is_valid())
        {
            return class_type;
        }

        // Try to resolve as an enum type
        TypeRef enum_type = _context.symbols().lookup_enum_type(type_str);
        if (enum_type.is_valid())
        {
            return enum_type;
        }

        // For unresolved types that might be enums, structs, or generic parameters
        // defined later in the file, create an error type with the type name
        // The TypeResolver will resolve it properly during type resolution phase
        LOG_DEBUG(LogComponent::PARSER, "Could not resolve '{}' during parsing, deferring to type resolution phase", type_str);

        // Return an error type - the TypeResolver will resolve the actual type from annotation
        return _context.types().create_error("unresolved type: " + type_str, SourceLocation{});
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

    // ========================================
    // Symbol Resolution Manager (SRM) Helper Methods
    // ========================================

    std::string Parser::generate_qualified_namespace_name(const std::string &namespace_name)
    {
        if (!_srm_manager)
        {
            return namespace_name;
        }

        // Convert single namespace name to parts and generate qualified name
        auto namespace_parts = get_current_namespace_parts();

        auto qualified_id = std::make_unique<Cryo::SRM::QualifiedIdentifier>(
            namespace_parts, namespace_name, Cryo::SymbolKind::Type);

        return qualified_id->to_string();
    }

    std::string Parser::generate_qualified_type_name(const std::string &base_name, const std::string &member_name)
    {
        if (!_srm_manager)
        {
            return base_name + "::" + member_name;
        }

        auto namespace_parts = get_current_namespace_parts();
        namespace_parts.push_back(base_name);

        auto type_id = std::make_unique<Cryo::SRM::TypeIdentifier>(
            namespace_parts, member_name, Cryo::TypeKind::Struct);

        return type_id->to_string();
    }

    std::string Parser::generate_scope_resolution_name(const std::string &scope_name, const std::string &member_name)
    {
        if (!_srm_manager)
        {
            return scope_name + "::" + member_name;
        }

        // Create a qualified identifier for scope resolution
        std::vector<std::string> parts = {scope_name};
        auto qualified_id = std::make_unique<Cryo::SRM::QualifiedIdentifier>(
            parts, member_name, Cryo::SymbolKind::Variable);

        return qualified_id->to_string();
    }

    std::vector<std::string> Parser::get_current_namespace_parts() const
    {
        if (!_srm_context)
        {
            // Fallback to manual namespace tracking if SRM is not available
            std::vector<std::string> parts;
            if (!_current_namespace.empty())
            {
                // Simple split by "::" - this is a fallback for legacy compatibility
                std::string delimiter = "::";
                std::string ns = _current_namespace;
                size_t pos = 0;
                std::string token;
                while ((pos = ns.find(delimiter)) != std::string::npos)
                {
                    token = ns.substr(0, pos);
                    if (!token.empty())
                    {
                        parts.push_back(token);
                    }
                    ns.erase(0, pos + delimiter.length());
                }
                if (!ns.empty())
                {
                    parts.push_back(ns);
                }
            }
            return parts;
        }

        return _srm_context->get_namespace_stack();
    }
}