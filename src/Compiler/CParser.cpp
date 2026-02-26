#include "Compiler/CParser.hpp"
#include <cctype>
#include <unordered_map>

namespace Cryo
{
    // ========================================================================
    // Public API
    // ========================================================================

    std::vector<CFunctionDecl> CParser::parse(const std::string &source)
    {
        _source = source;
        _pos = 0;
        _tokens.clear();
        _tok_pos = 0;

        tokenize();

        std::vector<CFunctionDecl> result;

        while (!check(CTokKind::Eof))
        {
            CFunctionDecl decl;
            if (try_parse_function_decl(decl))
            {
                result.push_back(std::move(decl));
            }
            else
            {
                // Skip whatever we couldn't parse
                skip_to_semicolon_or_brace();
            }
        }

        return result;
    }

    // ========================================================================
    // Embedded C Lexer
    // ========================================================================

    static const std::unordered_map<std::string, CParser::CTokKind> keyword_map = {
        {"void", CParser::CTokKind::Void},
        {"int", CParser::CTokKind::Int},
        {"char", CParser::CTokKind::Char},
        {"short", CParser::CTokKind::Short},
        {"long", CParser::CTokKind::Long},
        {"float", CParser::CTokKind::Float},
        {"double", CParser::CTokKind::Double},
        {"unsigned", CParser::CTokKind::Unsigned},
        {"signed", CParser::CTokKind::Signed},
        {"const", CParser::CTokKind::Const},
        {"volatile", CParser::CTokKind::Volatile},
        {"static", CParser::CTokKind::Static},
        {"extern", CParser::CTokKind::Extern},
        {"inline", CParser::CTokKind::Inline},
        {"__inline", CParser::CTokKind::Inline},
        {"__inline__", CParser::CTokKind::Inline},
        {"struct", CParser::CTokKind::Struct},
        {"union", CParser::CTokKind::Union},
        {"enum", CParser::CTokKind::Enum},
        {"typedef", CParser::CTokKind::Typedef},
        {"_Bool", CParser::CTokKind::Bool},
        {"size_t", CParser::CTokKind::SizeT},
        {"int8_t", CParser::CTokKind::Int8T},
        {"int16_t", CParser::CTokKind::Int16T},
        {"int32_t", CParser::CTokKind::Int32T},
        {"int64_t", CParser::CTokKind::Int64T},
        {"uint8_t", CParser::CTokKind::UInt8T},
        {"uint16_t", CParser::CTokKind::UInt16T},
        {"uint32_t", CParser::CTokKind::UInt32T},
        {"uint64_t", CParser::CTokKind::UInt64T},
    };

    void CParser::tokenize()
    {
        while (_pos < _source.size())
        {
            skip_whitespace_and_comments();
            if (_pos >= _source.size())
                break;

            CToken tok = next_token();
            if (tok.kind != CTokKind::Unknown || !tok.text.empty())
            {
                _tokens.push_back(std::move(tok));
            }
        }

        _tokens.push_back({CTokKind::Eof, ""});
    }

    void CParser::skip_whitespace_and_comments()
    {
        while (_pos < _source.size())
        {
            // Skip whitespace
            if (std::isspace(static_cast<unsigned char>(_source[_pos])))
            {
                _pos++;
                continue;
            }

            // Skip // line comments
            if (_pos + 1 < _source.size() && _source[_pos] == '/' && _source[_pos + 1] == '/')
            {
                _pos += 2;
                while (_pos < _source.size() && _source[_pos] != '\n')
                    _pos++;
                continue;
            }

            // Skip /* block comments */
            if (_pos + 1 < _source.size() && _source[_pos] == '/' && _source[_pos + 1] == '*')
            {
                _pos += 2;
                while (_pos + 1 < _source.size() && !(_source[_pos] == '*' && _source[_pos + 1] == '/'))
                    _pos++;
                if (_pos + 1 < _source.size())
                    _pos += 2;
                continue;
            }

            break;
        }
    }

    CParser::CToken CParser::next_token()
    {
        if (_pos >= _source.size())
            return {CTokKind::Eof, ""};

        char c = _source[_pos];

        // Identifiers and keywords
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_')
        {
            return lex_identifier_or_keyword();
        }

        // Numbers
        if (std::isdigit(static_cast<unsigned char>(c)))
        {
            return lex_number();
        }

        // String literals
        if (c == '"')
        {
            return lex_string_literal();
        }

        // Character literals
        if (c == '\'')
        {
            _pos++; // skip opening '
            while (_pos < _source.size() && _source[_pos] != '\'')
            {
                if (_source[_pos] == '\\')
                    _pos++; // skip escape
                _pos++;
            }
            if (_pos < _source.size())
                _pos++; // skip closing '
            return {CTokKind::NumericConstant, "0"}; // treat char literals as numeric
        }

        // Punctuation
        _pos++;
        switch (c)
        {
        case '*':
            return {CTokKind::Star, "*"};
        case '(':
            return {CTokKind::LParen, "("};
        case ')':
            return {CTokKind::RParen, ")"};
        case '{':
            return {CTokKind::LBrace, "{"};
        case '}':
            return {CTokKind::RBrace, "}"};
        case '[':
            return {CTokKind::LBracket, "["};
        case ']':
            return {CTokKind::RBracket, "]"};
        case ',':
            return {CTokKind::Comma, ","};
        case ';':
            return {CTokKind::Semicolon, ";"};
        case '=':
            return {CTokKind::Equals, "="};
        case '.':
            // Check for ellipsis
            if (_pos + 1 < _source.size() && _source[_pos] == '.' && _source[_pos + 1] == '.')
            {
                _pos += 2;
                return {CTokKind::Ellipsis, "..."};
            }
            return {CTokKind::Unknown, "."};
        default:
            return {CTokKind::Unknown, std::string(1, c)};
        }
    }

    CParser::CToken CParser::lex_identifier_or_keyword()
    {
        size_t start = _pos;
        while (_pos < _source.size() &&
               (std::isalnum(static_cast<unsigned char>(_source[_pos])) || _source[_pos] == '_'))
        {
            _pos++;
        }

        std::string text = _source.substr(start, _pos - start);

        // Check for keywords
        auto it = keyword_map.find(text);
        if (it != keyword_map.end())
        {
            return {it->second, text};
        }

        return {CTokKind::Identifier, text};
    }

    CParser::CToken CParser::lex_number()
    {
        size_t start = _pos;

        // Handle hex (0x...), octal (0...), or decimal
        if (_source[_pos] == '0' && _pos + 1 < _source.size())
        {
            char next = _source[_pos + 1];
            if (next == 'x' || next == 'X')
            {
                _pos += 2;
                while (_pos < _source.size() && std::isxdigit(static_cast<unsigned char>(_source[_pos])))
                    _pos++;
            }
        }

        while (_pos < _source.size() && (std::isdigit(static_cast<unsigned char>(_source[_pos])) || _source[_pos] == '.'))
            _pos++;

        // Skip suffixes (u, l, ul, ull, f, etc.)
        while (_pos < _source.size() && (std::isalpha(static_cast<unsigned char>(_source[_pos]))))
            _pos++;

        return {CTokKind::NumericConstant, _source.substr(start, _pos - start)};
    }

    CParser::CToken CParser::lex_string_literal()
    {
        size_t start = _pos;
        _pos++; // skip opening "
        while (_pos < _source.size() && _source[_pos] != '"')
        {
            if (_source[_pos] == '\\')
                _pos++; // skip escape
            _pos++;
        }
        if (_pos < _source.size())
            _pos++; // skip closing "
        return {CTokKind::StringLiteral, _source.substr(start, _pos - start)};
    }

    // ========================================================================
    // Parser Helpers
    // ========================================================================

    const CParser::CToken &CParser::current() const
    {
        static const CToken eof_token{CTokKind::Eof, ""};
        if (_tok_pos < _tokens.size())
            return _tokens[_tok_pos];
        return eof_token;
    }

    const CParser::CToken &CParser::peek(size_t offset) const
    {
        static const CToken eof_token{CTokKind::Eof, ""};
        size_t idx = _tok_pos + offset;
        if (idx < _tokens.size())
            return _tokens[idx];
        return eof_token;
    }

    void CParser::advance()
    {
        if (_tok_pos < _tokens.size())
            _tok_pos++;
    }

    bool CParser::match(CTokKind kind)
    {
        if (check(kind))
        {
            advance();
            return true;
        }
        return false;
    }

    bool CParser::check(CTokKind kind) const
    {
        return current().kind == kind;
    }

    void CParser::skip_to_semicolon_or_brace()
    {
        while (!check(CTokKind::Eof))
        {
            if (check(CTokKind::Semicolon))
            {
                advance();
                return;
            }
            if (check(CTokKind::LBrace))
            {
                skip_balanced_braces();
                // After a closing brace, optionally consume a semicolon
                match(CTokKind::Semicolon);
                return;
            }
            advance();
        }
    }

    void CParser::skip_balanced_braces()
    {
        if (!match(CTokKind::LBrace))
            return;

        int depth = 1;
        while (!check(CTokKind::Eof) && depth > 0)
        {
            if (check(CTokKind::LBrace))
                depth++;
            else if (check(CTokKind::RBrace))
                depth--;
            advance();
        }
    }

    bool CParser::is_type_keyword(CTokKind kind) const
    {
        switch (kind)
        {
        case CTokKind::Void:
        case CTokKind::Int:
        case CTokKind::Char:
        case CTokKind::Short:
        case CTokKind::Long:
        case CTokKind::Float:
        case CTokKind::Double:
        case CTokKind::Unsigned:
        case CTokKind::Signed:
        case CTokKind::Bool:
        case CTokKind::SizeT:
        case CTokKind::Int8T:
        case CTokKind::Int16T:
        case CTokKind::Int32T:
        case CTokKind::Int64T:
        case CTokKind::UInt8T:
        case CTokKind::UInt16T:
        case CTokKind::UInt32T:
        case CTokKind::UInt64T:
            return true;
        default:
            return false;
        }
    }

    bool CParser::is_type_qualifier(CTokKind kind) const
    {
        switch (kind)
        {
        case CTokKind::Const:
        case CTokKind::Volatile:
        case CTokKind::Static:
        case CTokKind::Extern:
        case CTokKind::Inline:
        case CTokKind::Signed:
        case CTokKind::Unsigned:
            return true;
        default:
            return false;
        }
    }

    // ========================================================================
    // Parser: Type Specifiers
    // ========================================================================

    std::string CParser::parse_type_specifier()
    {
        std::string type_str;

        // Collect qualifiers and type keywords
        bool has_base_type = false;
        while (!check(CTokKind::Eof))
        {
            CTokKind kind = current().kind;

            if (is_type_qualifier(kind) || is_type_keyword(kind))
            {
                if (!type_str.empty())
                    type_str += " ";
                type_str += current().text;

                if (is_type_keyword(kind))
                    has_base_type = true;

                advance();
            }
            else if (kind == CTokKind::Struct || kind == CTokKind::Union || kind == CTokKind::Enum)
            {
                // struct/union/enum tag — skip the whole thing as an opaque type
                if (!type_str.empty())
                    type_str += " ";
                type_str += current().text;
                advance();
                // Consume the tag name if present
                if (check(CTokKind::Identifier))
                {
                    type_str += " " + current().text;
                    advance();
                    has_base_type = true;
                }
            }
            else if (kind == CTokKind::Identifier && !has_base_type)
            {
                // Treat an unknown identifier as a type name (e.g., FILE, va_list, etc.)
                if (!type_str.empty())
                    type_str += " ";
                type_str += current().text;
                has_base_type = true;
                advance();
            }
            else
            {
                break;
            }
        }

        // Collect pointer stars
        while (check(CTokKind::Star))
        {
            type_str += " *";
            advance();
            // Collect post-pointer qualifiers (const T * const)
            while (check(CTokKind::Const) || check(CTokKind::Volatile))
            {
                advance(); // skip post-pointer qualifiers
            }
        }

        return type_str;
    }

    std::string CParser::parse_declarator_name()
    {
        // A declarator can have leading *'s (for function pointers we skip, but
        // for normal declarators the *'s are part of the type), then an identifier.
        // Since we already parsed pointer *'s in parse_type_specifier, here we
        // just look for an identifier.

        if (check(CTokKind::Identifier))
        {
            std::string name = current().text;
            advance();
            return name;
        }
        return "";
    }

    // ========================================================================
    // Parser: Function Declarations
    // ========================================================================

    bool CParser::try_parse_function_decl(CFunctionDecl &out)
    {
        // Save position for backtracking
        size_t saved_pos = _tok_pos;

        // Skip typedef — we don't produce function decls from typedefs
        if (check(CTokKind::Typedef))
        {
            return false;
        }

        // Parse return type
        std::string return_type = parse_type_specifier();
        if (return_type.empty())
        {
            _tok_pos = saved_pos;
            return false;
        }

        // Parse function name
        std::string func_name = parse_declarator_name();
        if (func_name.empty())
        {
            _tok_pos = saved_pos;
            return false;
        }

        // Expect '(' for parameter list
        if (!check(CTokKind::LParen))
        {
            _tok_pos = saved_pos;
            return false;
        }
        advance(); // consume '('

        // Parse parameter list
        std::vector<CParam> params;
        bool is_variadic = false;

        if (!parse_parameter_list(params, is_variadic))
        {
            _tok_pos = saved_pos;
            return false;
        }

        // Expect ')'
        if (!match(CTokKind::RParen))
        {
            _tok_pos = saved_pos;
            return false;
        }

        // Expect ';' for a prototype, or '{' for a definition
        if (check(CTokKind::Semicolon))
        {
            advance(); // consume ';'
        }
        else if (check(CTokKind::LBrace))
        {
            // Function definition — skip the body, still capture the declaration
            skip_balanced_braces();
        }
        else
        {
            // Could be something like __attribute__ — skip
            _tok_pos = saved_pos;
            return false;
        }

        out.name = func_name;
        out.return_type = return_type;
        out.params = std::move(params);
        out.is_variadic = is_variadic;
        return true;
    }

    bool CParser::parse_parameter_list(std::vector<CParam> &params, bool &is_variadic)
    {
        is_variadic = false;

        // Check for void parameter (void as sole parameter means no params)
        if (check(CTokKind::Void) && peek().kind == CTokKind::RParen)
        {
            advance(); // consume 'void'
            return true;
        }

        // Empty parameter list
        if (check(CTokKind::RParen))
        {
            return true;
        }

        while (!check(CTokKind::RParen) && !check(CTokKind::Eof))
        {
            // Check for variadic
            if (check(CTokKind::Ellipsis))
            {
                is_variadic = true;
                advance();
                break;
            }

            // Parse parameter type
            std::string param_type = parse_type_specifier();
            if (param_type.empty())
                return false;

            // Parse optional parameter name
            std::string param_name;
            if (check(CTokKind::Identifier) && peek().kind != CTokKind::LParen)
            {
                param_name = current().text;
                advance();
            }

            // Skip array declarators like [N] or []
            while (check(CTokKind::LBracket))
            {
                advance(); // '['
                while (!check(CTokKind::RBracket) && !check(CTokKind::Eof))
                    advance();
                match(CTokKind::RBracket);
                // Array parameters decay to pointers in C
                param_type += " *";
            }

            params.push_back({param_name, param_type});

            // Expect ',' or ')'
            if (check(CTokKind::Comma))
            {
                advance();
            }
            else
            {
                break;
            }
        }

        return true;
    }

} // namespace Cryo
