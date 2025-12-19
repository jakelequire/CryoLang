#ifndef LEXER_HPP
#define LEXER_HPP

#include <string>
#include <string_view>
#include <memory>
#include <fstream>
#include <unordered_map>
#include <optional>
#include <vector>
#include <cstdint>

#include "Utils/File.hpp"

namespace Cryo
{

    // Forward declarations
    class Token;
    class SourceLocation;
    class Lexer;
    class DiagnosticManager;
    class Diagnostic;
    enum class ErrorCode : uint32_t;

    // ================================================================
    // Token Kind Enumeration
    // ================================================================

    enum class TokenKind
    {
        TK_ERROR = -1,

    // Generate token enum from tokens.def
#define TOK(X) TK_##X,
#define KEYWORD(X) TK_KW_##X,
#define PUNCTUATOR(X, Y) TK_##X,
#define DIRECTIVE(X) TK_PP_##X,
#define ATTRIBUTE(X) TK_ATTR_##X,
#include "tokens.def"
#undef TOK
#undef KEYWORD
#undef PUNCTUATOR
#undef DIRECTIVE
#undef ATTRIBUTE

        NUM_TOKENS // Count of total tokens
    };

    static std::string TokenKindToString(TokenKind kind)
    {
        switch (kind)
        {
#define TOK(X) \
    case TokenKind::TK_##X: \
        return #X;
#define KEYWORD(X) \
    case TokenKind::TK_KW_##X: \
        return "KW_" #X;
#define PUNCTUATOR(X, Y) \ 
    case TokenKind::TK_##X: \
        return #X;
#define DIRECTIVE(X) \
    case TokenKind::TK_PP_##X: \
        return "PP_" #X;
#define ATTRIBUTE(X) \
    case TokenKind::TK_ATTR_##X: \
        return "ATTR_" #X;
#include "tokens.def"
#undef TOK
#undef KEYWORD
#undef PUNCTUATOR
#undef DIRECTIVE
#undef ATTRIBUTE
        default:
            return "UNKNOWN_TOKEN";
        }
    }

    // ================================================================
    // Source Location Class
    // ================================================================

    class SourceLocation
    {
    private:
        size_t _line;
        size_t _column;

    public:
        SourceLocation(size_t line = 1, size_t column = 1);

        // Getters
        size_t line() const { return _line; }
        size_t column() const { return _column; }

        // Modifiers
        void increment(size_t count = 1);
        void increment_for_char(char c); // New: handles tabs properly
        void newline();
        void reset(size_t line = 1, size_t column = 1);
        void set(size_t line, size_t column);

        // Operators
        bool operator==(const SourceLocation &other) const;
        bool operator!=(const SourceLocation &other) const;
    };

    // ================================================================
    // Token Class
    // ================================================================

    class Token
    {
    private:
        TokenKind _kind;
        std::string_view _text;
        SourceLocation _location;

    public:
        Token(TokenKind kind = TokenKind::TK_ERROR,
              std::string_view text = "",
              SourceLocation location = SourceLocation{});

        // Getters
        TokenKind kind() const { return _kind; }
        std::string_view text() const { return _text; }
        const SourceLocation &location() const { return _location; }
        size_t length() const { return _text.length(); }

        // Utility methods
        bool is(TokenKind kind) const { return kind == _kind; }
        bool is_not(TokenKind kind) const { return kind != _kind; }
        bool is_eof() const { return _kind == TokenKind::TK_EOF; }
        bool is_error() const { return _kind == TokenKind::TK_ERROR; }
        bool is_keyword() const;
        bool is_identifier() const { return _kind == TokenKind::TK_IDENTIFIER; }
        bool is_literal() const;

        // String representation
        std::string to_string() const;

        // Operators
        bool operator==(const Token &other) const;
        bool operator!=(const Token &other) const;
    };

    // ================================================================
    // Lexer Class
    // ================================================================

    class Lexer
    {
    private:
        std::unique_ptr<File> _file;
        std::string_view _buffer;
        const char *_current;
        const char *_buffer_start;
        const char *_buffer_end;

        // For spot lexing - holds string content when not using a file
        std::string _spot_content;

        SourceLocation _current_location;
        Token _current_token;
        Token _previous_token;
        size_t _token_count;

        // Peek token state
        std::optional<Token> _peeked_token;

        // Diagnostic reporting
        DiagnosticManager *_diagnostic_manager;
        std::string _source_file;

        // String pool for processed string literals
        std::vector<std::string> _string_pool;

        // Static keyword lookup table
        static const std::unordered_map<std::string_view, TokenKind> _keywords;

    public:
        // Constructors
        explicit Lexer(std::unique_ptr<File> file);
        Lexer(std::unique_ptr<File> file, DiagnosticManager *diagnostic_manager, const std::string &source_file);

        // Constructor for spot lexing from string content
        explicit Lexer(const std::string &content);
        Lexer(const std::string &content, DiagnosticManager *diagnostic_manager, const std::string &source_file);

        // Destructor
        ~Lexer() = default;

        // Move semantics only
        Lexer(Lexer &&) = default;
        Lexer &operator=(Lexer &&) = default;

        // Delete copy semantics
        Lexer(const Lexer &) = delete;
        Lexer &operator=(const Lexer &) = delete;

        // Main lexing interface
        Token next_token();
        Token peek_token();
        bool has_more_tokens() const;
        void reset();

        // State access
        const Token &current_token() const { return _current_token; }
        const Token &previous_token() const { return _previous_token; }
        const SourceLocation &location() const { return _current_location; }
        size_t token_count() const { return _token_count; }

        // File access
        const File &file() const { return *_file; }

    private:
        // Core lexing methods
        Token lex_token();
        Token lex_identifier();
        Token lex_number();
        Token lex_string(char quote);
        Token lex_character();
        Token lex_comment();
        Token lex_doc_comment();
        TokenKind lex_punctuator(char c);

        // Helper methods
        void skip_whitespace();
        char advance();
        char peek(size_t offset = 0) const;
        bool at_end() const;
        Token make_token(TokenKind kind, const char *start) const;
        Token make_error_token(const std::string &message) const;

        // Enhanced error reporting
        void report_lexer_error(const std::string &message, const SourceLocation &location);

        // String processing helpers
        std::string process_escape_sequences(const std::string &raw_string);
        std::string_view store_processed_string(std::string processed_string);

        // Character classification
        static bool is_alpha(char c);
        static bool is_digit(char c);
        static bool is_hex_digit(char c);
        static bool is_alnum(char c);
        static bool is_whitespace(char c);

        // Keyword lookup
        static TokenKind lookup_keyword(std::string_view text);
        static void init_keywords();
    };

    // ================================================================
    // Utility Functions
    // ================================================================

    // Token name and spelling utilities
    const char *get_token_name(TokenKind kind);
    const char *get_token_spelling(TokenKind kind);

    // Factory functions
    std::unique_ptr<Lexer> make_lexer(std::unique_ptr<File> file);

} // namespace CryoLang

#endif // LEXER_HPP