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
        _typedefs.clear();
        _enum_names.clear();
        _struct_names.clear();
        _enum_decls.clear();

        tokenize();

        std::vector<CFunctionDecl> result;

        while (!check(CTokKind::Eof))
        {
            // Handle typedef declarations first
            if (check(CTokKind::Typedef))
            {
                parse_typedef();
                continue;
            }

            // Handle standalone enum declarations: enum Name { ... };
            if (check(CTokKind::Enum) && peek().kind != CTokKind::Eof)
            {
                // Could be a standalone enum decl OR a function returning enum type.
                // Peek ahead: enum [Name] { → standalone decl
                // enum [Name] Identifier ( → function returning enum type
                size_t saved = _tok_pos;
                advance(); // consume 'enum'
                if (check(CTokKind::Identifier))
                    advance(); // skip tag name
                if (check(CTokKind::LBrace))
                {
                    // Standalone enum declaration — parse and track it
                    _tok_pos = saved;
                    advance(); // consume 'enum'
                    std::string tag;
                    if (check(CTokKind::Identifier))
                    {
                        tag = current().text;
                        advance();
                    }
                    CEnumDecl edecl;
                    edecl.name = tag;
                    parse_enum_body(edecl);
                    if (!tag.empty())
                        _enum_names.insert(tag);
                    _enum_decls.push_back(std::move(edecl));
                    match(CTokKind::Semicolon);
                    continue;
                }
                _tok_pos = saved; // restore — let try_parse_function_decl handle it
            }

            // Handle standalone struct/union declarations: struct Name { ... };
            if ((check(CTokKind::Struct) || check(CTokKind::Union)) && peek().kind != CTokKind::Eof)
            {
                size_t saved = _tok_pos;
                advance(); // consume 'struct'/'union'
                if (check(CTokKind::Identifier))
                    advance(); // skip tag name
                if (check(CTokKind::LBrace))
                {
                    // Standalone struct/union declaration — track the name, skip the body
                    _tok_pos = saved;
                    advance(); // consume 'struct'/'union'
                    if (check(CTokKind::Identifier))
                    {
                        _struct_names.insert(current().text);
                        advance();
                    }
                    skip_balanced_braces();
                    match(CTokKind::Semicolon);
                    continue;
                }
                _tok_pos = saved;
            }

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
            if (std::isspace(static_cast<unsigned char>(_source[_pos])))
            {
                _pos++;
                continue;
            }

            if (_pos + 1 < _source.size() && _source[_pos] == '/' && _source[_pos + 1] == '/')
            {
                _pos += 2;
                while (_pos < _source.size() && _source[_pos] != '\n')
                    _pos++;
                continue;
            }

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

        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_')
            return lex_identifier_or_keyword();

        if (std::isdigit(static_cast<unsigned char>(c)))
            return lex_number();

        if (c == '"')
            return lex_string_literal();

        if (c == '\'')
        {
            _pos++;
            while (_pos < _source.size() && _source[_pos] != '\'')
            {
                if (_source[_pos] == '\\')
                    _pos++;
                _pos++;
            }
            if (_pos < _source.size())
                _pos++;
            return {CTokKind::NumericConstant, "0"};
        }

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
            _pos++;

        std::string text = _source.substr(start, _pos - start);

        auto it = keyword_map.find(text);
        if (it != keyword_map.end())
            return {it->second, text};

        return {CTokKind::Identifier, text};
    }

    CParser::CToken CParser::lex_number()
    {
        size_t start = _pos;

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
        _pos++;
        while (_pos < _source.size() && _source[_pos] != '"')
        {
            if (_source[_pos] == '\\')
                _pos++;
            _pos++;
        }
        if (_pos < _source.size())
            _pos++;
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

    bool CParser::is_typedef_name(const std::string &name) const
    {
        return _typedefs.find(name) != _typedefs.end();
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

    void CParser::skip_balanced_parens()
    {
        if (!match(CTokKind::LParen))
            return;

        int depth = 1;
        while (!check(CTokKind::Eof) && depth > 0)
        {
            if (check(CTokKind::LParen))
                depth++;
            else if (check(CTokKind::RParen))
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
                // If there's an inline enum/struct body in a param type, skip it
                if (check(CTokKind::LBrace))
                {
                    skip_balanced_braces();
                    has_base_type = true;
                }
            }
            else if (kind == CTokKind::Identifier && !has_base_type)
            {
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
            while (check(CTokKind::Const) || check(CTokKind::Volatile))
                advance();
        }

        return type_str;
    }

    std::string CParser::parse_declarator_name()
    {
        if (check(CTokKind::Identifier))
        {
            std::string name = current().text;
            advance();
            return name;
        }
        return "";
    }

    // ========================================================================
    // Parser: Typedef Declarations
    // ========================================================================

    void CParser::parse_typedef()
    {
        advance(); // consume 'typedef'

        // --- typedef enum { ... } Name; ---
        if (check(CTokKind::Enum))
        {
            advance(); // consume 'enum'

            std::string tag;
            if (check(CTokKind::Identifier))
            {
                tag = current().text;
                advance();
            }

            CEnumDecl edecl;
            edecl.name = tag;

            if (check(CTokKind::LBrace))
            {
                parse_enum_body(edecl);
            }

            // The typedef name(s) follow
            if (check(CTokKind::Identifier))
            {
                std::string alias = current().text;
                advance();
                _typedefs[alias] = "int"; // enums are ints in C ABI
                _enum_names.insert(alias);
                if (tag.empty())
                    edecl.name = alias;
            }
            if (!tag.empty())
                _enum_names.insert(tag);

            _enum_decls.push_back(std::move(edecl));
            match(CTokKind::Semicolon);
            return;
        }

        // --- typedef struct/union [Name] [{...}] [*] Alias; ---
        if (check(CTokKind::Struct) || check(CTokKind::Union))
        {
            std::string kind_str = current().text;
            advance(); // consume 'struct'/'union'

            std::string tag;
            if (check(CTokKind::Identifier))
            {
                tag = current().text;
                _struct_names.insert(tag);
                advance();
            }

            if (check(CTokKind::LBrace))
            {
                skip_balanced_braces();
            }

            // Build the resolved type string
            std::string resolved = kind_str;
            if (!tag.empty())
                resolved += " " + tag;

            // Collect pointer stars
            while (check(CTokKind::Star))
            {
                resolved += " *";
                advance();
                while (check(CTokKind::Const) || check(CTokKind::Volatile))
                    advance();
            }

            // Typedef name
            if (check(CTokKind::Identifier))
            {
                std::string alias = current().text;
                advance();
                _typedefs[alias] = resolved;
                // If it's a pointer to a struct, also track the alias as a struct-related name
            }

            match(CTokKind::Semicolon);
            return;
        }

        // --- General typedef: typedef TYPE NAME; ---
        // e.g., typedef int myint; typedef unsigned long ulong_t;
        std::string type_str = parse_type_specifier();

        // Check for function pointer typedef: typedef RET (*NAME)(PARAMS);
        if (check(CTokKind::LParen))
        {
            // Skip function pointer typedefs entirely
            skip_to_semicolon_or_brace();
            return;
        }

        if (check(CTokKind::Identifier))
        {
            std::string alias = current().text;
            advance();
            if (!type_str.empty())
                _typedefs[alias] = type_str;
        }

        // Handle any trailing array or function declarator junk
        skip_to_semicolon_or_brace();
    }

    // ========================================================================
    // Parser: Enum Body
    // ========================================================================

    void CParser::parse_enum_body(CEnumDecl &out)
    {
        if (!match(CTokKind::LBrace))
            return;

        int64_t next_value = 0;

        while (!check(CTokKind::RBrace) && !check(CTokKind::Eof))
        {
            if (!check(CTokKind::Identifier))
            {
                advance();
                continue;
            }

            CEnumConstant constant;
            constant.name = current().text;
            advance();

            // Optional: = value
            if (match(CTokKind::Equals))
            {
                // Parse integer constant (possibly negative or hex)
                bool negative = false;
                if (check(CTokKind::Unknown) && current().text == "-")
                {
                    negative = true;
                    advance();
                }

                if (check(CTokKind::NumericConstant))
                {
                    try
                    {
                        // Handle hex (0x...) and decimal
                        std::string val_str = current().text;
                        // Strip suffixes
                        while (!val_str.empty() && std::isalpha(val_str.back()))
                            val_str.pop_back();
                        if (val_str.size() > 2 && val_str[0] == '0' && (val_str[1] == 'x' || val_str[1] == 'X'))
                            next_value = std::stoll(val_str, nullptr, 16);
                        else
                            next_value = std::stoll(val_str, nullptr, 10);
                        if (negative)
                            next_value = -next_value;
                    }
                    catch (...)
                    {
                        // If parsing fails, just continue with auto-increment
                    }
                    advance();
                }
                else
                {
                    // Complex expression (e.g., 1 << 3) — skip to comma or brace
                    while (!check(CTokKind::Comma) && !check(CTokKind::RBrace) && !check(CTokKind::Eof))
                        advance();
                }
            }

            constant.value = next_value;
            out.constants.push_back(std::move(constant));
            next_value++;

            // Consume comma between enumerators
            match(CTokKind::Comma);
        }

        match(CTokKind::RBrace);
    }

    // ========================================================================
    // Parser: Function Declarations
    // ========================================================================

    bool CParser::try_parse_function_decl(CFunctionDecl &out)
    {
        size_t saved_pos = _tok_pos;

        // Skip typedef — handled separately
        if (check(CTokKind::Typedef))
            return false;

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
            advance();
        }
        else if (check(CTokKind::LBrace))
        {
            skip_balanced_braces();
        }
        else
        {
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

        if (check(CTokKind::Void) && peek().kind == CTokKind::RParen)
        {
            advance();
            return true;
        }

        if (check(CTokKind::RParen))
            return true;

        while (!check(CTokKind::RParen) && !check(CTokKind::Eof))
        {
            if (check(CTokKind::Ellipsis))
            {
                is_variadic = true;
                advance();
                break;
            }

            std::string param_type = parse_type_specifier();
            if (param_type.empty())
                return false;

            std::string param_name;
            if (check(CTokKind::Identifier) && peek().kind != CTokKind::LParen)
            {
                param_name = current().text;
                advance();
            }

            // Skip array declarators like [N] or []
            while (check(CTokKind::LBracket))
            {
                advance();
                while (!check(CTokKind::RBracket) && !check(CTokKind::Eof))
                    advance();
                match(CTokKind::RBracket);
                param_type += " *";
            }

            params.push_back({param_name, param_type});

            if (check(CTokKind::Comma))
                advance();
            else
                break;
        }

        return true;
    }

} // namespace Cryo
