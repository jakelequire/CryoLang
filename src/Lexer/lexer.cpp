#include "Lexer/lexer.hpp"
#include "Utils/File.hpp"
#include "Diagnostics/Diag.hpp"
#include <iostream>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <cctype>

namespace Cryo
{

    // ================================================================
    // Character Classification Helpers
    // ================================================================

    bool Lexer::is_alpha(char c)
    {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
    }

    bool Lexer::is_digit(char c)
    {
        return c >= '0' && c <= '9';
    }

    bool Lexer::is_hex_digit(char c)
    {
        return is_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
    }

    bool Lexer::is_alnum(char c)
    {
        return is_alpha(c) || is_digit(c);
    }

    bool Lexer::is_whitespace(char c)
    {
        return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f';
    }

    // ================================================================
    // Keyword Lookup Table Implementation
    // ================================================================

    // Static initialization of keyword map
    const std::unordered_map<std::string_view, TokenKind> Lexer::_keywords = {
        // Map lowercase keyword strings to ALL_CAPS token kinds
        {"if", TokenKind::TK_KW_IF},
        {"else", TokenKind::TK_KW_ELSE},
        {"elif", TokenKind::TK_KW_ELIF},
        {"switch", TokenKind::TK_KW_SWITCH},
        {"case", TokenKind::TK_KW_CASE},
        {"default", TokenKind::TK_KW_DEFAULT},
        {"while", TokenKind::TK_KW_WHILE},
        {"for", TokenKind::TK_KW_FOR},
        {"loop", TokenKind::TK_KW_LOOP},
        {"do", TokenKind::TK_KW_DO},
        {"break", TokenKind::TK_KW_BREAK},
        {"continue", TokenKind::TK_KW_CONTINUE},
        {"return", TokenKind::TK_KW_RETURN},
        {"match", TokenKind::TK_KW_MATCH},
        {"yield", TokenKind::TK_KW_YIELD},

        // Declarations
        {"const", TokenKind::TK_KW_CONST},
        {"static", TokenKind::TK_KW_STATIC},
        {"extern", TokenKind::TK_KW_EXTERN},
        {"auto", TokenKind::TK_KW_AUTO},
        {"function", TokenKind::TK_KW_FUNCTION},
        {"class", TokenKind::TK_KW_CLASS},
        {"struct", TokenKind::TK_KW_STRUCT},
        {"enum", TokenKind::TK_KW_ENUM},
        {"trait", TokenKind::TK_KW_TRAIT},
        {"type", TokenKind::TK_KW_TYPE},
        {"namespace", TokenKind::TK_KW_NAMESPACE},
        {"module", TokenKind::TK_KW_MODULE},
        {"import", TokenKind::TK_KW_IMPORT},
        {"export", TokenKind::TK_KW_EXPORT},
        {"from", TokenKind::TK_KW_FROM},
        {"as", TokenKind::TK_KW_AS},
        {"implement", TokenKind::TK_KW_IMPLEMENT},
        {"intrinsic", TokenKind::TK_KW_INTRINSIC},

        // Type System
        {"void", TokenKind::TK_KW_VOID},
        {"boolean", TokenKind::TK_KW_BOOLEAN},
        {"int", TokenKind::TK_KW_INT},
        {"i8", TokenKind::TK_KW_I8},
        {"i16", TokenKind::TK_KW_I16},
        {"i32", TokenKind::TK_KW_I32},
        {"i64", TokenKind::TK_KW_I64},
        {"uint", TokenKind::TK_KW_UINT},
        {"uint8", TokenKind::TK_KW_UINT8},
        {"uint16", TokenKind::TK_KW_UINT16},
        {"uint32", TokenKind::TK_KW_UINT32},
        {"uint64", TokenKind::TK_KW_UINT64},
        {"float", TokenKind::TK_KW_FLOAT},
        {"f32", TokenKind::TK_KW_F32},
        {"f64", TokenKind::TK_KW_F64},
        {"double", TokenKind::TK_KW_DOUBLE},
        {"char", TokenKind::TK_KW_CHAR},
        {"string", TokenKind::TK_KW_STRING},
        {"tuple", TokenKind::TK_KW_TUPLE},
        {"optional", TokenKind::TK_KW_OPTIONAL},
        {"any", TokenKind::TK_KW_ANY},
        {"generic", TokenKind::TK_KW_GENERIC},

        // Access Control & Modifiers
        {"public", TokenKind::TK_KW_PUBLIC},
        {"private", TokenKind::TK_KW_PRIVATE},
        {"protected", TokenKind::TK_KW_PROTECTED},
        {"virtual", TokenKind::TK_KW_VIRTUAL},
        {"override", TokenKind::TK_KW_OVERRIDE},
        {"mutable", TokenKind::TK_KW_MUTABLE},
        {"inline", TokenKind::TK_KW_INLINE},
        {"async", TokenKind::TK_KW_ASYNC},
        {"await", TokenKind::TK_KW_AWAIT},
        {"unsafe", TokenKind::TK_KW_UNSAFE},

        // Memory & Ownership
        {"mut", TokenKind::TK_KW_MUT},

        // Special Values
        {"true", TokenKind::TK_KW_TRUE},
        {"false", TokenKind::TK_KW_FALSE},
        {"null", TokenKind::TK_KW_NULL},
        {"this", TokenKind::TK_KW_THIS},
        {"This", TokenKind::TK_KW_THIS_TYPE},

        // Operators as Keywords
        {"in", TokenKind::TK_KW_IN},
        {"typeof", TokenKind::TK_KW_TYPEOF},
        {"sizeof", TokenKind::TK_KW_SIZEOF},
        {"alignof", TokenKind::TK_KW_ALIGNOF},
        {"new", TokenKind::TK_KW_NEW},
        {"delete", TokenKind::TK_KW_DELETE},
        {"with", TokenKind::TK_KW_WITH},
        {"where", TokenKind::TK_KW_WHERE}}; // Where clause for trait bounds

    TokenKind Lexer::lookup_keyword(std::string_view text)
    {
        auto it = _keywords.find(text);
        return (it != _keywords.end()) ? it->second : TokenKind::TK_IDENTIFIER;
    }

    // ================================================================
    // SourceLocation Implementation
    // ================================================================

    SourceLocation::SourceLocation(size_t line, size_t column)
        : _line(line), _column(column) {}

    void SourceLocation::increment(size_t count)
    {
        _column += count;
    }

    void SourceLocation::increment_for_char(char c)
    {
        if (c == '\t')
        {
            // Tab expands to next tab stop (typically 8-character boundaries)
            // Formula: next_tab_stop = ((current_column - 1) / 8 + 1) * 8 + 1
            size_t current_0_based = _column - 1; // Convert to 0-based
            size_t next_tab_stop = ((current_0_based / 8) + 1) * 8;
            _column = next_tab_stop + 1; // Convert back to 1-based
        }
        else
        {
            _column++;
        }
    }

    void SourceLocation::newline()
    {
        _line++;
        _column = 1;
    }

    void SourceLocation::reset(size_t line, size_t column)
    {
        _line = line;
        _column = column;
    }

    void SourceLocation::set(size_t line, size_t column)
    {
        _line = line;
        _column = column;
    }

    bool SourceLocation::operator==(const SourceLocation &other) const
    {
        return _line == other._line && _column == other._column;
    }

    bool SourceLocation::operator!=(const SourceLocation &other) const
    {
        return !(*this == other);
    }

    // ================================================================
    // Token Implementation
    // ================================================================

    Token::Token(TokenKind kind, std::string_view text, SourceLocation location)
        : _kind(kind), _text(text), _location(location) {}

    bool Token::is_keyword() const
    {
        // Check if token kind is in keyword range
        return static_cast<int>(_kind) >= static_cast<int>(TokenKind::TK_KW_IF) &&
               static_cast<int>(_kind) <= static_cast<int>(TokenKind::TK_KW_WITH);
    }

    bool Token::is_literal() const
    {
        return _kind == TokenKind::TK_NUMERIC_CONSTANT ||
               _kind == TokenKind::TK_CHAR_CONSTANT ||
               _kind == TokenKind::TK_STRING_LITERAL ||
               _kind == TokenKind::TK_RAW_STRING_LITERAL ||
               _kind == TokenKind::TK_BOOLEAN_LITERAL;
    }

    std::string Token::to_string() const
    {
        return std::string(_text);
    }

    bool Token::operator==(const Token &other) const
    {
        return _kind == other._kind && _text == other._text && _location == other._location;
    }

    bool Token::operator!=(const Token &other) const
    {
        return !(*this == other);
    }

    // ================================================================
    // Lexer Implementation
    // ================================================================

    Lexer::Lexer(std::unique_ptr<File> file)
        : _file(std::move(file)), _current_location(1, 1), _current_token(TokenKind::TK_ERROR), _previous_token(TokenKind::TK_ERROR),
          _token_count(0), _diagnostics(nullptr)
    {

        if (!_file || !_file->is_loaded())
        {
            if (_file)
            {
                _file->load();
            }
        }

        if (_file && _file->is_loaded())
        {
            _buffer = _file->view();
            _buffer_start = _buffer.data();
            _buffer_end = _buffer_start + _buffer.size();
            _current = _buffer_start;
        }
        else
        {
            _buffer_start = _buffer_end = _current = nullptr;
        }
    }

    Lexer::Lexer(std::unique_ptr<File> file, DiagEmitter *diagnostics, const std::string &source_file)
        : _file(std::move(file)), _current_location(1, 1), _current_token(TokenKind::TK_ERROR), _previous_token(TokenKind::TK_ERROR),
          _token_count(0), _diagnostics(diagnostics), _source_file(source_file)
    {

        if (!_file || !_file->is_loaded())
        {
            if (_file)
            {
                _file->load();
            }
        }

        if (_file && _file->is_loaded())
        {
            _buffer = _file->view();
            _buffer_start = _buffer.data();
            _buffer_end = _buffer_start + _buffer.size();
            _current = _buffer_start;
        }
        else
        {
            _buffer_start = _buffer_end = _current = nullptr;
        }
    }

    Lexer::Lexer(const std::string &content)
        : _file(nullptr), _spot_content(content), _current_location(1, 1), _current_token(TokenKind::TK_ERROR), _previous_token(TokenKind::TK_ERROR),
          _token_count(0), _diagnostics(nullptr)
    {
        // Set up buffer pointers to reference the spot content
        _buffer = std::string_view(_spot_content);
        _buffer_start = _buffer.data();
        _buffer_end = _buffer_start + _buffer.size();
        _current = _buffer_start;
    }

    Lexer::Lexer(const std::string &content, DiagEmitter *diagnostics, const std::string &source_file)
        : _file(nullptr), _spot_content(content), _current_location(1, 1), _current_token(TokenKind::TK_ERROR), _previous_token(TokenKind::TK_ERROR),
          _token_count(0), _diagnostics(diagnostics), _source_file(source_file)
    {
        // Set up buffer pointers to reference the spot content
        _buffer = std::string_view(_spot_content);
        _buffer_start = _buffer.data();
        _buffer_end = _buffer_start + _buffer.size();
        _current = _buffer_start;
    }

    Token Lexer::next_token()
    {
        if (_peeked_token.has_value())
        {
            Token token = *_peeked_token;
            _peeked_token.reset();
            _previous_token = _current_token;
            _current_token = token;
            _token_count++;

            // Restore buffer position to where the peeked lex_token() left off.
            // This avoids re-lexing (which would duplicate string pool entries
            // and corrupt string_view data for string literal tokens).
            _current = _peek_saved_current;
            _current_location = _peek_saved_location;

            return _current_token;
        }

        _previous_token = _current_token;
        _current_token = lex_token();
        _token_count++;
        return _current_token;
    }

    Token Lexer::peek_token()
    {
        if (_peeked_token.has_value())
        {
            return *_peeked_token;
        }

        // Save current state (pre-lex position)
        const char *saved_current = _current;
        SourceLocation saved_location = _current_location;

        // Lex the next token
        _peeked_token = lex_token();

        // Save the post-lex position so next_token() can restore it
        // without needing to re-lex (which would corrupt string pool entries)
        _peek_saved_current = _current;
        _peek_saved_location = _current_location;

        // Restore state to pre-lex position (peek doesn't consume)
        _current = saved_current;
        _current_location = saved_location;

        return *_peeked_token;
    }

    bool Lexer::has_more_tokens() const
    {
        if (_peeked_token.has_value() && _peeked_token->is_eof())
        {
            return false;
        }
        return _current < _buffer_end;
    }

    void Lexer::reset()
    {
        _current = _buffer_start;
        _current_location.reset(1, 1);
        _current_token = Token(TokenKind::TK_ERROR);
        _previous_token = Token(TokenKind::TK_ERROR);
        _token_count = 0;
        _peeked_token.reset();
        _peek_saved_current = nullptr;
    }

    // ================================================================
    // Core Lexing Methods
    // ================================================================

    Token Lexer::lex_token()
    {
        skip_whitespace();

        if (at_end())
        {
            return make_token(TokenKind::TK_EOF, _current);
        }

        const char *token_start = _current;
        char c = advance();

        // Identifier or keyword
        if (is_alpha(c))
        {
            return lex_identifier();
        }

        // Number
        if (is_digit(c))
        {
            return lex_number();
        }

        // String literals
        if (c == '"')
        {
            return lex_string(c);
        }

        // Character literals
        if (c == '\'')
        {
            return lex_character();
        }

        // Comments and documentation comments
        if (c == '/' && peek() == '/')
        {
            // Check if it's a documentation comment (///)
            if (peek(1) == '/')
            {
                return lex_doc_comment();
            }
            return lex_comment();
        }
        if (c == '/' && peek() == '*')
        {
            // Check if it's a documentation comment (/**)
            if (peek(1) == '*')
            {
                return lex_doc_comment();
            }
            return lex_comment();
        }

        // Punctuators
        TokenKind punct_kind = lex_punctuator(c);
        if (punct_kind != TokenKind::TK_ERROR)
        {
            return make_token(punct_kind, token_start);
        }

        if (_diagnostics)
        {
            report_lexer_error("unexpected character", _current_location);
        }
        return make_error_token("Unexpected character");
    }

    Token Lexer::lex_identifier()
    {
        const char *start = _current - 1; // Back up to include first character

        while (!at_end() && is_alnum(peek()))
        {
            advance();
        }

        std::string_view text(start, _current - start);
        TokenKind kind = lookup_keyword(text);

        return Token(kind, text, SourceLocation(_current_location.line(), _current_location.column() - text.length()));
    }

    Token Lexer::lex_number()
    {
        const char *start = _current - 1;

        // Handle special number formats (hex, binary, octal) - but only if we have '0' followed by a format specifier
        if (*(_current - 1) == '0' && !at_end())
        {
            char prefix = peek();
            if (prefix == 'x' || prefix == 'X')
            {
                // Hexadecimal
                advance(); // consume 'x' or 'X'
                const char *hex_start = _current;

                while (!at_end() && (is_hex_digit(peek()) || peek() == '_'))
                {
                    // Skip underscores but don't count them as digits
                    if (peek() == '_')
                    {
                        advance();
                        continue;
                    }
                    advance();
                }

                // Check if we actually consumed any hex digits
                if (_current == hex_start)
                {
                    if (_diagnostics)
                    {
                        std::string hex_text(start, _current - start);
                        _diagnostics->emit(
                            Diag::error(ErrorCode::E0004_INVALID_NUMBER, "Invalid hexadecimal number: " + hex_text)
                                .at(Span::at(_current_location, _source_file)));
                    }
                    return make_error_token("Invalid hexadecimal number");
                }

                // Skip decimal and exponent parsing for hex numbers
                goto handle_suffixes;
            }
            else if (prefix == 'b' || prefix == 'B')
            {
                // Binary
                advance(); // consume 'b' or 'B'
                const char *bin_start = _current;

                while (!at_end() && (peek() == '0' || peek() == '1' || peek() == '_'))
                {
                    // Skip underscores but don't count them as digits
                    if (peek() == '_')
                    {
                        advance();
                        continue;
                    }
                    advance();
                }

                // Check if we actually consumed any binary digits
                if (_current == bin_start)
                {
                    if (_diagnostics)
                    {
                        std::string bin_text(start, _current - start);
                        _diagnostics->emit(
                            Diag::error(ErrorCode::E0004_INVALID_NUMBER, "Invalid binary number: " + bin_text)
                                .at(Span::at(_current_location, _source_file)));
                    }
                    return make_error_token("Invalid binary number");
                }

                // Skip decimal and exponent parsing for binary numbers
                goto handle_suffixes;
            }
            else if (prefix == 'o' || prefix == 'O')
            {
                // Octal
                advance(); // consume 'o' or 'O'
                const char *oct_start = _current;

                while (!at_end() && ((peek() >= '0' && peek() <= '7') || peek() == '_'))
                {
                    // Skip underscores but don't count them as digits
                    if (peek() == '_')
                    {
                        advance();
                        continue;
                    }
                    advance();
                }

                // Check if we actually consumed any octal digits
                if (_current == oct_start)
                {
                    if (_diagnostics)
                    {
                        std::string oct_text(start, _current - start);
                        _diagnostics->emit(
                            Diag::error(ErrorCode::E0004_INVALID_NUMBER, "Invalid octal number: " + oct_text)
                                .at(Span::at(_current_location, _source_file)));
                    }
                    return make_error_token("Invalid octal number");
                }

                // Skip decimal and exponent parsing for octal numbers
                goto handle_suffixes;
            }
        }

        // Regular decimal number - consume remaining digits (for all numbers, including those starting with 0)
        // Also allow underscores as visual separators (e.g., 1_000_000)
        while (!at_end() && (is_digit(peek()) || peek() == '_'))
        {
            // Skip underscores but don't count them as digits
            if (peek() == '_')
            {
                advance();
                continue;
            }
            advance();
        }

        // Decimal point - check if we have a decimal number like 1.23 or 0.5
        if (!at_end() && peek() == '.')
        {
            // Look ahead to see if there's a digit after the decimal point
            if ((_current + 1) < _buffer_end)
            {
                char next_char = *(_current + 1);
                if (is_digit(next_char))
                {
                    advance(); // consume '.'
                    while (!at_end() && (is_digit(peek()) || peek() == '_'))
                    {
                        if (peek() == '_')
                        {
                            advance();
                            continue;
                        }
                        advance();
                    }
                }
            }
        }

        // Exponent
        if (!at_end() && (peek() == 'e' || peek() == 'E'))
        {
            advance();
            if (!at_end() && (peek() == '+' || peek() == '-'))
            {
                advance();
            }
            while (!at_end() && (is_digit(peek()) || peek() == '_'))
            {
                if (peek() == '_')
                {
                    advance();
                    continue;
                }
                advance();
            }
        }

    handle_suffixes:

        // Handle type suffixes like u64, i32, f32, etc.
        if (!at_end() && is_alpha(peek()))
        {
            // Look for common type suffixes
            const char *suffix_start = _current;
            while (!at_end() && is_alnum(peek()))
            {
                advance();
            }

            // Validate that this is a valid type suffix
            std::string_view suffix(suffix_start, _current - suffix_start);
            if (suffix == "u8" || suffix == "u16" || suffix == "u32" || suffix == "u64" ||
                suffix == "i8" || suffix == "i16" || suffix == "i32" || suffix == "i64" ||
                suffix == "f32" || suffix == "f64" || suffix == "usize" || suffix == "isize")
            {
                // Valid suffix, include it in the numeric constant
            }
            else
            {
                // Not a valid suffix, backtrack
                _current = suffix_start;
            }
        }

        std::string_view text(start, _current - start);
        return Token(TokenKind::TK_NUMERIC_CONSTANT, text,
                     SourceLocation(_current_location.line(),
                                    _current_location.column() - text.length()));
    }

    Token Lexer::lex_string(char quote)
    {
        const char *start = _current - 1;

        while (!at_end() && peek() != quote)
        {
            if (peek() == '\\' && _current + 1 < _buffer_end)
            {
                advance(); // consume backslash
                if (!at_end())
                    advance(); // consume escaped character
            }
            else
            {
                if (peek() == '\n')
                {
                    _current_location.newline();
                }
                advance();
            }
        }

        if (at_end())
        {
            if (_diagnostics)
            {
                report_lexer_error("unterminated string literal", _current_location);
            }
            return make_error_token("Unterminated string literal");
        }

        advance(); // consume closing quote

        // Extract the string content without quotes
        std::string_view raw_text(start + 1, _current - start - 2);

        // Process escape sequences during lexical analysis
        std::string processed_content = process_escape_sequences(std::string(raw_text));

        // Store the processed string in the string pool and get a string_view to it
        std::string_view processed_text = store_processed_string(std::move(processed_content));

        return Token(TokenKind::TK_STRING_LITERAL, processed_text,
                     SourceLocation(_current_location.line(),
                                    _current_location.column() - ((_current - start))));
    }

    Token Lexer::lex_character()
    {
        const char *start = _current - 1;

        if (at_end())
        {
            if (_diagnostics)
            {
                report_lexer_error("unterminated character literal", _current_location);
            }
            return make_error_token("Unterminated character literal");
        }

        if (peek() == '\\' && _current + 1 < _buffer_end)
        {
            advance(); // consume backslash
            if (!at_end())
            {
                char escape_char = peek();
                advance(); // consume escape character (n, t, x, etc.)

                // Handle hex escape sequences (\xHH)
                if (escape_char == 'x')
                {
                    // Consume up to 2 hex digits
                    for (int i = 0; i < 2 && !at_end() && is_hex_digit(peek()); i++)
                    {
                        advance();
                    }
                }
            }
        }
        else
        {
            // Handle multi-byte UTF-8 characters
            unsigned char first_byte = (unsigned char)peek();
            if (first_byte < 0x80)
            {
                // ASCII character (0xxxxxxx)
                advance();
            }
            else if ((first_byte & 0xE0) == 0xC0)
            {
                // 2-byte UTF-8 character (110xxxxx 10xxxxxx)
                advance(); // first byte
                if (!at_end() && ((unsigned char)peek() & 0xC0) == 0x80)
                    advance(); // second byte
            }
            else if ((first_byte & 0xF0) == 0xE0)
            {
                // 3-byte UTF-8 character (1110xxxx 10xxxxxx 10xxxxxx)
                advance(); // first byte
                if (!at_end() && ((unsigned char)peek() & 0xC0) == 0x80)
                {
                    advance(); // second byte
                    if (!at_end() && ((unsigned char)peek() & 0xC0) == 0x80)
                        advance(); // third byte
                }
            }
            else if ((first_byte & 0xF8) == 0xF0)
            {
                // 4-byte UTF-8 character (11110xxx 10xxxxxx 10xxxxxx 10xxxxxx)
                advance(); // first byte
                if (!at_end() && ((unsigned char)peek() & 0xC0) == 0x80)
                {
                    advance(); // second byte
                    if (!at_end() && ((unsigned char)peek() & 0xC0) == 0x80)
                    {
                        advance(); // third byte
                        if (!at_end() && ((unsigned char)peek() & 0xC0) == 0x80)
                            advance(); // fourth byte
                    }
                }
            }
            else
            {
                // Invalid UTF-8 sequence, just advance one byte
                advance();
            }
        }

        if (at_end() || peek() != '\'')
        {
            if (_diagnostics)
            {
                report_lexer_error("unterminated character literal", _current_location);
            }
            return make_error_token("Unterminated character literal");
        }

        advance(); // consume closing quote

        std::string_view text(start, _current - start);
        return Token(TokenKind::TK_CHAR_CONSTANT, text,
                     SourceLocation(_current_location.line(),
                                    _current_location.column() - text.length()));
    }

    Token Lexer::lex_comment()
    {
        const char *start = _current - 1;

        if (*(_current - 1) == '/' && *_current == '/')
        {
            // Line comment
            advance(); // consume second '/'
            while (!at_end() && peek() != '\n')
            {
                advance();
            }
        }
        else if (*(_current - 1) == '/' && *_current == '*')
        {
            // Block comment
            advance(); // consume '*'

            while (_current + 1 < _buffer_end)
            {
                if (peek() == '*' && *(_current + 1) == '/')
                {
                    advance(); // consume '*'
                    advance(); // consume '/'
                    break;
                }
                if (peek() == '\n')
                {
                    _current_location.newline();
                }
                advance();
            }
        }

        std::string_view text(start, _current - start);
        return Token(TokenKind::TK_COMMENT, text,
                     SourceLocation(_current_location.line(),
                                    _current_location.column() - text.length()));
    }

    Token Lexer::lex_doc_comment()
    {
        const char *start = _current - 1;
        TokenKind doc_token_kind;

        if (peek() == '/' && peek(1) == '/')
        {
            // Line documentation comment (///)
            doc_token_kind = TokenKind::TK_DOC_COMMENT_LINE;
            advance(); // consume second '/'
            advance(); // consume third '/'

            // Skip optional space after ///
            if (peek() == ' ')
            {
                advance();
            }

            while (!at_end() && peek() != '\n')
            {
                advance();
            }
        }
        else if (peek() == '*' && peek(1) == '*')
        {
            // Block documentation comment (/**)
            doc_token_kind = TokenKind::TK_DOC_COMMENT_BLOCK;
            advance(); // consume '*'
            advance(); // consume second '*'

            // Skip optional space or newline after /**
            if (peek() == ' ' || peek() == '\n')
            {
                if (peek() == '\n')
                {
                    _current_location.newline();
                }
                advance();
            }

            while (_current + 1 < _buffer_end)
            {
                if (peek() == '*' && *(_current + 1) == '/')
                {
                    advance(); // consume '*'
                    advance(); // consume '/'
                    break;
                }
                if (peek() == '\n')
                {
                    _current_location.newline();
                }
                advance();
            }
        }
        else
        {
            // Fallback to regular comment if detection fails
            return lex_comment();
        }

        std::string_view text(start, _current - start);
        return Token(doc_token_kind, text,
                     SourceLocation(_current_location.line(),
                                    _current_location.column() - text.length()));
    }

    TokenKind Lexer::lex_punctuator(char c)
    {
        switch (c)
        {
        case '(':
            return TokenKind::TK_L_PAREN;
        case ')':
            return TokenKind::TK_R_PAREN;
        case '{':
            return TokenKind::TK_L_BRACE;
        case '}':
            return TokenKind::TK_R_BRACE;
        case '[':
            return TokenKind::TK_L_SQUARE;
        case ']':
            return TokenKind::TK_R_SQUARE;
        case ';':
            return TokenKind::TK_SEMICOLON;
        case ',':
            return TokenKind::TK_COMMA;
        case '.':
            if (peek() == '.')
            {
                if (_current + 1 < _buffer_end && *(_current + 1) == '.')
                {
                    advance();
                    advance();                     // consume ".."
                    return TokenKind::TK_ELLIPSIS; // "..."
                }
                advance();                   // consume second '.'
                return TokenKind::TK_DOTDOT; // ".."
            }
            return TokenKind::TK_PERIOD;
        case '+':
            if (!at_end())
            {
                if (peek() == '+')
                {
                    advance();
                    return TokenKind::TK_PLUSPLUS;
                }
                else if (peek() == '=')
                {
                    advance();
                    return TokenKind::TK_PLUSEQUAL;
                }
            }
            return TokenKind::TK_PLUS;
        case '-':
            if (!at_end())
            {
                if (peek() == '-')
                {
                    advance();
                    return TokenKind::TK_MINUSMINUS;
                }
                else if (peek() == '=')
                {
                    advance();
                    return TokenKind::TK_MINUSEQUAL;
                }
                else if (peek() == '>')
                {
                    advance();
                    return TokenKind::TK_ARROW;
                }
            }
            return TokenKind::TK_MINUS;
        case '*':
            if (!at_end() && peek() == '=')
            {
                advance();
                return TokenKind::TK_STAREQUAL;
            }
            return TokenKind::TK_STAR;
        case '/':
            if (!at_end() && peek() == '=')
            {
                advance();
                return TokenKind::TK_SLASHEQUAL;
            }
            return TokenKind::TK_SLASH;
        case '=':
            if (!at_end())
            {
                if (peek() == '=')
                {
                    advance();
                    return TokenKind::TK_EQUALEQUAL;
                }
                else if (peek() == '>')
                {
                    advance();
                    return TokenKind::TK_FATARROW;
                }
            }
            return TokenKind::TK_EQUAL;
        case '<':
            if (!at_end())
            {
                if (peek() == '=')
                {
                    advance();
                    if (!at_end() && peek() == '>')
                    {
                        advance();
                        return TokenKind::TK_SPACESHIP;
                    }
                    return TokenKind::TK_LESSEQUAL;
                }
                else if (peek() == '<')
                {
                    advance();
                    if (!at_end() && peek() == '=')
                    {
                        advance();
                        return TokenKind::TK_LESSLESSEQUAL;
                    }
                    return TokenKind::TK_LESSLESS;
                }
            }
            return TokenKind::TK_L_ANGLE;
        case '>':
            if (!at_end())
            {
                if (peek() == '=')
                {
                    advance();
                    return TokenKind::TK_GREATEREQUAL;
                }
                else if (peek() == '>')
                {
                    advance();
                    if (!at_end() && peek() == '=')
                    {
                        advance();
                        return TokenKind::TK_GREATERGREATEREQUAL;
                    }
                    return TokenKind::TK_GREATERGREATER;
                }
            }
            return TokenKind::TK_R_ANGLE;
        case '!':
            if (!at_end() && peek() == '=')
            {
                advance();
                return TokenKind::TK_EXCLAIMEQUAL;
            }
            return TokenKind::TK_EXCLAIM;
        case '&':
            if (!at_end())
            {
                if (peek() == '&')
                {
                    advance();
                    return TokenKind::TK_AMPAMP;
                }
                else if (peek() == '=')
                {
                    advance();
                    return TokenKind::TK_AMPEQUAL;
                }
            }
            return TokenKind::TK_AMP;
        case '|':
            if (!at_end())
            {
                if (peek() == '|')
                {
                    advance();
                    return TokenKind::TK_PIPEPIPE;
                }
                else if (peek() == '=')
                {
                    advance();
                    return TokenKind::TK_PIPEEQUAL;
                }
                else if (peek() == '>')
                {
                    advance();
                    return TokenKind::TK_PIPEGT;
                }
            }
            return TokenKind::TK_PIPE;
        case '?':
            if (!at_end())
            {
                if (peek() == '?')
                {
                    advance();
                    return TokenKind::TK_QUESTION2;
                }
                else if (peek() == '.')
                {
                    advance();
                    return TokenKind::TK_QUESTIONDOT;
                }
            }
            return TokenKind::TK_QUESTION;
        case ':':
            if (!at_end() && peek() == ':')
            {
                advance();
                return TokenKind::TK_COLONCOLON;
            }
            return TokenKind::TK_COLON;
        case '#':
            if (!at_end() && peek() == '#')
            {
                advance();
                return TokenKind::TK_HASHHASH;
            }
            return TokenKind::TK_HASH;
        case '%':
            return TokenKind::TK_PERCENT;
        case '^':
            return TokenKind::TK_CARET;
        case '~':
            return TokenKind::TK_TILDE;
        case '@':
            return TokenKind::TK_AT;
        case '$':
            return TokenKind::TK_DOLLAR;
        case '\\':
            return TokenKind::TK_BACKSLASH;
        default:
            return TokenKind::TK_ERROR;
        }
    }

    // ================================================================
    // Helper Methods
    // ================================================================

    void Lexer::skip_whitespace()
    {
        while (!at_end() && is_whitespace(peek()))
        {
            char c = peek();
            if (c == '\n')
            {
                _current++;                  // Advance pointer manually
                _current_location.newline(); // Handle newline properly
            }
            else
            {
                advance(); // This will properly handle tabs and other whitespace
            }
        }
    }

    char Lexer::advance()
    {
        if (at_end())
            return '\0';

        char c = *_current;
        _current++;

        // Use proper tab-aware column tracking
        _current_location.increment_for_char(c);

        return c;
    }

    char Lexer::peek(size_t offset) const
    {
        if (_current + offset >= _buffer_end)
        {
            return '\0';
        }
        return *(_current + offset);
    }

    bool Lexer::at_end() const
    {
        return _current >= _buffer_end;
    }

    Token Lexer::make_token(TokenKind kind, const char *start) const
    {
        std::string_view text(start, _current - start);
        return Token(kind, text, SourceLocation(_current_location.line(), _current_location.column() - text.length()));
    }

    Token Lexer::make_error_token(const std::string &message) const
    {
        // For error tokens, we'll store the error message in the text
        static std::string error_storage = message; // Simple approach for demo
        return Token(TokenKind::TK_ERROR, error_storage, _current_location);
    }

    // ================================================================
    // Utility Functions
    // ================================================================

    const char *get_token_name(TokenKind kind)
    {
        switch (kind)
        {
        case TokenKind::TK_ERROR:
            return "ERROR";

#define TOK(X)              \
    case TokenKind::TK_##X: \
        return #X;
#define KEYWORD(X)             \
    case TokenKind::TK_KW_##X: \
        return "KW_" #X;
#define PUNCTUATOR(X, Y)    \
    case TokenKind::TK_##X: \
        return #X;
#define DIRECTIVE(X)           \
    case TokenKind::TK_PP_##X: \
        return "PP_" #X;
#define ATTRIBUTE(X)             \
    case TokenKind::TK_ATTR_##X: \
        return "ATTR_" #X;
#include "Lexer/tokens.def"
#undef TOK
#undef KEYWORD
#undef PUNCTUATOR
#undef DIRECTIVE
#undef ATTRIBUTE

        default:
            return "UNKNOWN";
        }
    }

    const char *get_token_spelling(TokenKind kind)
    {
        switch (kind)
        {
#define TOK(X)
#define KEYWORD(X)             \
    case TokenKind::TK_KW_##X: \
        return #X;
#define PUNCTUATOR(X, Y)    \
    case TokenKind::TK_##X: \
        return Y;
#define DIRECTIVE(X)
#define ATTRIBUTE(X)
#include "Lexer/tokens.def"
#undef TOK
#undef KEYWORD
#undef PUNCTUATOR
#undef DIRECTIVE
#undef ATTRIBUTE

        default:
            return "";
        }
    }

    // ================================================================
    // Factory Functions
    // ================================================================

    std::unique_ptr<Lexer> make_lexer(std::unique_ptr<File> file)
    {
        if (!file)
        {
            return nullptr;
        }
        return std::make_unique<Lexer>(std::move(file));
    }

    // ================================================================
    // Enhanced Error Reporting Methods
    // ================================================================

    void Lexer::report_lexer_error(const std::string &message, const SourceLocation &location)
    {
        if (!_diagnostics)
        {
            return;
        }

        Span span = Span::at(location, _source_file);

        // Use appropriate error code based on message content
        if (message.find("unexpected character") != std::string::npos)
        {
            char character = (_current > _buffer_start) ? *(_current - 1) : '?';
            std::string msg = "Unexpected character '";
            msg += character;
            msg += "'";
            _diagnostics->emit(Diag::error(ErrorCode::E0001_UNEXPECTED_CHARACTER, msg).at(span));
        }
        else if (message.find("unterminated string") != std::string::npos)
        {
            _diagnostics->emit(Diag::error(ErrorCode::E0002_UNTERMINATED_STRING, "Unterminated string literal").at(span));
        }
        else if (message.find("unterminated character") != std::string::npos)
        {
            _diagnostics->emit(Diag::error(ErrorCode::E0003_UNTERMINATED_CHAR, "Unterminated character literal").at(span));
        }
        else
        {
            _diagnostics->emit(Diag::error(ErrorCode::E0001_UNEXPECTED_CHARACTER, message).at(span));
        }
    }

    // ================================================================
    // String Processing Helper Methods
    // ================================================================

    std::string Lexer::process_escape_sequences(const std::string &str)
    {
        std::string result;
        result.reserve(str.length());

        for (size_t i = 0; i < str.length(); ++i)
        {
            if (str[i] == '\\' && i + 1 < str.length())
            {
                char next = str[i + 1];
                switch (next)
                {
                case 'n':
                    result += '\n';
                    break;
                case 't':
                    result += '\t';
                    break;
                case 'r':
                    result += '\r';
                    break;
                case '\\':
                    result += '\\';
                    break;
                case '\'':
                    result += '\'';
                    break;
                case '\"':
                    result += '\"';
                    break;
                case 'a':
                    result += '\a';
                    break;
                case 'b':
                    result += '\b';
                    break;
                case 'f':
                    result += '\f';
                    break;
                case 'v':
                    result += '\v';
                    break;
                case 'x':
                    // Handle hex escape sequences (\xHH)
                    {
                        std::string hex_str;
                        size_t hex_pos = i + 2; // Start after \x

                        // Collect up to 2 hex digits
                        for (int digit_count = 0; digit_count < 2 && hex_pos < str.length() && is_hex_digit(str[hex_pos]); ++digit_count, ++hex_pos)
                        {
                            hex_str += str[hex_pos];
                        }

                        if (!hex_str.empty())
                        {
                            // Convert hex string to integer
                            int hex_value = std::stoi(hex_str, nullptr, 16);
                            result += static_cast<char>(hex_value);
                            // Skip the hex digits (first char 'x' is handled by normal ++i)
                            i += hex_str.length();
                        }
                        else
                        {
                            // No hex digits after \x, treat as literal
                            result += '\\';
                            result += 'x';
                        }
                    }
                    break;
                case '0':
                case '1':
                case '2':
                case '3':
                case '4':
                case '5':
                case '6':
                case '7':
                    // Handle octal escape sequences (\000 to \377)
                    {
                        std::string octal_str;
                        octal_str += next;        // Include the first digit we already found
                        size_t octal_pos = i + 2; // Start from the next position after the first digit

                        // Collect up to 2 more octal digits (we already have 1)
                        for (int digit_count = 1; digit_count < 3 && octal_pos < str.length() && str[octal_pos] >= '0' && str[octal_pos] <= '7'; ++digit_count, ++octal_pos)
                        {
                            octal_str += str[octal_pos];
                        }

                        // Special case: if we only have \0 and no following octal digits, treat as null terminator
                        if (octal_str == "0" && (i + 2 >= str.length() || str[i + 2] < '0' || str[i + 2] > '7'))
                        {
                            result += '\0';
                        }
                        else
                        {
                            // Convert octal string to integer
                            int octal_value = std::stoi(octal_str, nullptr, 8);
                            if (octal_value >= 0 && octal_value <= 255)
                            {
                                result += static_cast<char>(octal_value);
                                // Skip the remaining octal digits (first digit is handled by normal ++i)
                                i += octal_str.length() - 1;
                            }
                            else
                            {
                                // Invalid octal value, treat as literal
                                result += '\\';
                                result += next;
                            }
                        }
                    }
                    break;
                default:
                    // If unknown escape sequence, just include the character as-is
                    result += next;
                    break;
                }
                ++i; // Skip the next character since we processed it
            }
            else
            {
                result += str[i];
            }
        }

        return result;
    }

    std::string_view Lexer::store_processed_string(std::string processed_string)
    {
        _string_pool.emplace_back(std::move(processed_string));
        return std::string_view(_string_pool.back());
    }

} // namespace Cryo
