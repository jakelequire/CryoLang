#include "Utils/SyntaxHighlighter.hpp"
#include <algorithm>
#include <cctype>
#include <regex>

namespace Cryo
{
    // ========== LanguageHighlighter Base Implementation ==========
    
    std::vector<SyntaxToken> LanguageHighlighter::basic_tokenize(const std::string& code, const LanguageRules& rules) 
    {
        std::vector<SyntaxToken> tokens;
        if (code.empty()) return tokens;
        
        size_t pos = 0;
        size_t length = code.length();
        
        while (pos < length) {
            // Skip whitespace
            if (std::isspace(code[pos])) {
                pos++;
                continue;
            }
            
            // Check for single-line comments
            if (!rules.comment_single_line.empty() && 
                pos + rules.comment_single_line.length() <= length &&
                code.substr(pos, rules.comment_single_line.length()) == rules.comment_single_line) {
                
                size_t comment_start = pos;
                pos += rules.comment_single_line.length();
                
                // Find end of line
                while (pos < length && code[pos] != '\n') {
                    pos++;
                }
                
                auto color_it = rules.color_scheme.find(TokenType::Comment);
                std::string color = (color_it != rules.color_scheme.end()) ? color_it->second : ColorCodes::GREY;
                tokens.emplace_back(comment_start, pos, TokenType::Comment, color);
                continue;
            }
            
            // Check for multi-line comments
            if (!rules.comment_multi_line_start.empty() && 
                pos + rules.comment_multi_line_start.length() <= length &&
                code.substr(pos, rules.comment_multi_line_start.length()) == rules.comment_multi_line_start) {
                
                size_t comment_start = pos;
                pos += rules.comment_multi_line_start.length();
                
                // Find end of comment
                while (pos + rules.comment_multi_line_end.length() <= length) {
                    if (code.substr(pos, rules.comment_multi_line_end.length()) == rules.comment_multi_line_end) {
                        pos += rules.comment_multi_line_end.length();
                        break;
                    }
                    pos++;
                }
                
                auto color_it = rules.color_scheme.find(TokenType::Comment);
                std::string color = (color_it != rules.color_scheme.end()) ? color_it->second : ColorCodes::GREY;
                tokens.emplace_back(comment_start, pos, TokenType::Comment, color);
                continue;
            }
            
            // Check for strings
            if (rules.string_delimiters.find(code[pos]) != std::string::npos) {
                char delimiter = code[pos];
                size_t string_start = pos;
                pos++; // Skip opening delimiter
                
                // Find closing delimiter, handling escape sequences
                while (pos < length) {
                    if (code[pos] == '\\' && pos + 1 < length) {
                        pos += 2; // Skip escape sequence
                    } else if (code[pos] == delimiter) {
                        pos++; // Include closing delimiter
                        break;
                    } else {
                        pos++;
                    }
                }
                
                auto color_it = rules.color_scheme.find(TokenType::String);
                std::string color = (color_it != rules.color_scheme.end()) ? color_it->second : ColorCodes::GREEN;
                tokens.emplace_back(string_start, pos, TokenType::String, color);
                continue;
            }
            
            // Check for numbers (improved to handle hex, binary, octal)
            if (std::isdigit(code[pos]) || (code[pos] == '.' && pos + 1 < length && std::isdigit(code[pos + 1]))) {
                size_t number_start = pos;
                
                // Check for hexadecimal (0x...)
                if (pos + 1 < length && code[pos] == '0' && (code[pos + 1] == 'x' || code[pos + 1] == 'X')) {
                    pos += 2; // Skip '0x'
                    while (pos < length && std::isxdigit(code[pos])) {
                        pos++;
                    }
                }
                // Check for binary (0b...)
                else if (pos + 1 < length && code[pos] == '0' && (code[pos + 1] == 'b' || code[pos + 1] == 'B')) {
                    pos += 2; // Skip '0b'
                    while (pos < length && (code[pos] == '0' || code[pos] == '1')) {
                        pos++;
                    }
                }
                // Check for octal (0o...)
                else if (pos + 1 < length && code[pos] == '0' && (code[pos + 1] == 'o' || code[pos + 1] == 'O')) {
                    pos += 2; // Skip '0o'
                    while (pos < length && code[pos] >= '0' && code[pos] <= '7') {
                        pos++;
                    }
                }
                // Regular decimal number or float
                else {
                    bool has_dot = false;
                    while (pos < length) {
                        if (std::isdigit(code[pos])) {
                            pos++;
                        } else if (code[pos] == '.' && !has_dot) {
                            has_dot = true;
                            pos++;
                        } else if ((code[pos] == 'e' || code[pos] == 'E') && pos + 1 < length) {
                            pos++; // Skip 'e' or 'E'
                            if (pos < length && (code[pos] == '+' || code[pos] == '-')) {
                                pos++; // Skip optional sign
                            }
                            while (pos < length && std::isdigit(code[pos])) {
                                pos++;
                            }
                            break;
                        } else if (code[pos] == 'f' || code[pos] == 'F' || 
                                   code[pos] == 'd' || code[pos] == 'D' ||
                                   code[pos] == 'l' || code[pos] == 'L' ||
                                   code[pos] == 'u' || code[pos] == 'U') {
                            pos++; // Include type suffix
                            break;
                        } else {
                            break;
                        }
                    }
                }
                
                auto color_it = rules.color_scheme.find(TokenType::Number);
                std::string color = (color_it != rules.color_scheme.end()) ? color_it->second : ColorCodes::CYAN;
                tokens.emplace_back(number_start, pos, TokenType::Number, color);
                continue;
            }
            
            // Check for identifiers and keywords
            if (std::isalpha(code[pos]) || code[pos] == '_') {
                size_t identifier_start = pos;
                
                while (pos < length && (std::isalnum(code[pos]) || code[pos] == '_')) {
                    pos++;
                }
                
                std::string word = code.substr(identifier_start, pos - identifier_start);
                TokenType token_type = TokenType::Identifier;
                std::string color;
                
                // Check if it's a keyword
                if (std::find(rules.keywords.begin(), rules.keywords.end(), word) != rules.keywords.end()) {
                    token_type = TokenType::Keyword;
                    
                    // Apply custom colors based on keyword type
                    if (word == "if" || word == "else" || word == "while" || word == "for" || 
                        word == "switch" || word == "loop" || word == "break" || word == "continue" ||
                        word == "return" || word == "match" || word == "case" || word == "default" ||
                        word == "new" || word == "delete") {
                        // Control statements get bright magenta
                        color = ColorCodes::MAGENTA;
                    } else if (word == "const" || word == "mut") {
                        // const and mut stay dark blue
                        color = ColorCodes::BLUE;
                    } else {
                        // Default keyword color (dark blue) - includes type, class, struct, enum
                        auto color_it = rules.color_scheme.find(TokenType::Keyword);
                        color = (color_it != rules.color_scheme.end()) ? color_it->second : ColorCodes::BLUE;
                    }
                } 
                // Check if it's a type
                else if (std::find(rules.types.begin(), rules.types.end(), word) != rules.types.end()) {
                    token_type = TokenType::Type;
                    auto color_it = rules.color_scheme.find(TokenType::Type);
                    color = (color_it != rules.color_scheme.end()) ? color_it->second : ColorCodes::YELLOW;
                } 
                // Check if it's a literal (true, false, null, undefined)
                else if (std::find(rules.literals.begin(), rules.literals.end(), word) != rules.literals.end()) {
                    token_type = TokenType::Literal;
                    auto color_it = rules.color_scheme.find(TokenType::Literal);
                    color = (color_it != rules.color_scheme.end()) ? color_it->second : ColorCodes::BRIGHT_YELLOW;
                } 
                // Check if it's likely a user-defined type (starts with capital letter)
                else if (!word.empty() && std::isupper(word[0])) {
                    token_type = TokenType::Type;
                    auto color_it = rules.color_scheme.find(TokenType::Type);
                    color = (color_it != rules.color_scheme.end()) ? color_it->second : ColorCodes::CYAN;
                }
                // Default to identifier
                else {
                    auto color_it = rules.color_scheme.find(TokenType::Identifier);
                    color = (color_it != rules.color_scheme.end()) ? color_it->second : ColorCodes::WHITE;
                }
                
                tokens.emplace_back(identifier_start, pos, token_type, color);
                continue;
            }
            
            // Check for operators and punctuation
            size_t op_start = pos;
            std::string potential_op;
            
            // Try to match multi-character operators first
            for (size_t len = 3; len >= 1 && pos + len <= length; len--) {
                potential_op = code.substr(pos, len);
                if (std::find(rules.operators.begin(), rules.operators.end(), potential_op) != rules.operators.end()) {
                    pos += len;
                    auto color_it = rules.color_scheme.find(TokenType::Operator);
                    std::string color = (color_it != rules.color_scheme.end()) ? color_it->second : ColorCodes::MAGENTA;
                    tokens.emplace_back(op_start, pos, TokenType::Operator, color);
                    break;
                }
            }
            
            // If no operator matched, treat as punctuation
            if (pos == op_start) {
                pos++;
                auto color_it = rules.color_scheme.find(TokenType::Punctuation);
                std::string color = (color_it != rules.color_scheme.end()) ? color_it->second : ColorCodes::WHITE;
                tokens.emplace_back(op_start, pos, TokenType::Punctuation, color);
            }
        }
        
        return tokens;
    }

    // ========== CryoHighlighter Implementation ==========
    
    CryoHighlighter::CryoHighlighter() 
    {
        initialize_cryo_rules();
    }
    
    void CryoHighlighter::initialize_cryo_rules() 
    {
        // Cryo keywords based on grammar.md and cryo.tmLanguage.json
        _rules.keywords = {
            // Control flow
            "if", "else", "while", "for", "switch", "loop", "break", "continue", 
            "return", "match", "case", "default",
            
            // Declaration keywords
            "function", "fn", "const", "mut", "type", "struct", "class", "enum", 
            "implement", "trait", "module", "namespace",
            
            // Visibility and modifiers
            "public", "private", "static", "extern", "volatile", "intrinsic",
            
            // Memory and type operations
            "new", "delete", "sizeof", "alignof",
            
            // Import/export
            "import", "export", "using", "from", "as", "in",
            
            // Special keywords
            "this", "super", "where", "property", "param", "method"
        };
        
        // Cryo types from grammar.md
        _rules.types = {
            // Basic types from grammar
            "int", "float", "double", "boolean", "string", "char", "void",
            
            // Specific integer types
            "i1", "i8", "i16", "i32", "i64", 
            "u8", "u16", "u32", "u64",
            "uint8", "uint16", "uint32", "uint64",
            
            // Floating point types
            "f32", "f64",
            
            // Pointer types
            "ptr", "const_ptr"
        };
        
        // Cryo operators from grammar and tmLanguage
        _rules.operators = {
            // Assignment operators
            "=", "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "<<=", ">>=",
            
            // Comparison operators
            "==", "!=", "<", "<=", ">", ">=",
            
            // Logical operators
            "&&", "||", "!",
            
            // Arithmetic operators
            "+", "-", "*", "/", "%",
            
            // Bitwise operators
            "&", "|", "^", "~", "<<", ">>",
            
            // Arrow operators
            "->", "=>",
            
            // Scope resolution
            "::",
            
            // Other operators and punctuation
            "?", ":", ".", ";"
        };
        
        // Cryo literals and constants
        _rules.literals = {
            "true", "false", "null", "undefined"
        };
        
        _rules.string_delimiters = "\"'";  // Removed backticks, not in Cryo grammar
        _rules.comment_single_line = "//";
        _rules.comment_multi_line_start = "/*";
        _rules.comment_multi_line_end = "*/";
        
        // Enhanced color scheme for Cryo with user-preferred colors
        _rules.color_scheme = {
            {TokenType::Keyword, ColorCodes::BLUE},           // const, mut (dark blue)
            {TokenType::Type, ColorCodes::CYAN},              // Data types (teal)
            {TokenType::String, ColorCodes::YELLOW},          // String literals (light orange)
            {TokenType::Number, ColorCodes::CYAN},            // Numeric literals
            {TokenType::Comment, ColorCodes::GREY},           // Comments
            {TokenType::Operator, ColorCodes::MAGENTA},       // Operators (bright magenta)
            {TokenType::Identifier, ColorCodes::WHITE},       // Variable names
            {TokenType::Punctuation, ColorCodes::MAGENTA},    // Brackets, semicolons (bright magenta)
            {TokenType::Function, ColorCodes::BRIGHT_CYAN},   // Function names
            {TokenType::Variable, ColorCodes::WHITE},         // Variables
            {TokenType::Constant, ColorCodes::BRIGHT_YELLOW}, // Constants like true, false, null
            {TokenType::Literal, ColorCodes::BRIGHT_YELLOW}   // Boolean and null literals
        };
    }
    
    std::vector<SyntaxToken> CryoHighlighter::tokenize(const std::string& code) 
    {
        return basic_tokenize(code, _rules);
    }
    
    LanguageRules CryoHighlighter::get_language_rules() const 
    {
        return _rules;
    }

    // ========== SyntaxHighlighter Implementation ==========
    
    SyntaxHighlighter::SyntaxHighlighter() 
        : _default_language("cryo"), _use_colors(true)
    {
        // Register the Cryo highlighter by default
        register_language("cryo", std::make_unique<CryoHighlighter>());
    }
    
    std::string SyntaxHighlighter::highlight_code(const std::string& code, const std::string& language) const 
    {
        if (!should_colorize() || code.empty()) {
            return code;
        }
        
        auto* highlighter = get_highlighter(language);
        if (!highlighter) {
            return code; // Return original code if language not supported
        }
        
        auto tokens = highlighter->tokenize(code);
        return apply_tokens_to_string(code, tokens);
    }
    
    std::string SyntaxHighlighter::highlight_line(const std::string& line, const std::string& language) const 
    {
        return highlight_code(line, language);
    }
    
    std::string SyntaxHighlighter::apply_syntax_highlighting(const std::string& source_line) const 
    {
        return highlight_line(source_line, _default_language);
    }
    
    void SyntaxHighlighter::register_language(const std::string& name, std::unique_ptr<LanguageHighlighter> highlighter) 
    {
        _highlighters[name] = std::move(highlighter);
    }
    
    bool SyntaxHighlighter::supports_language(const std::string& language) const 
    {
        return _highlighters.find(language) != _highlighters.end();
    }
    
    std::vector<std::string> SyntaxHighlighter::get_supported_languages() const 
    {
        std::vector<std::string> languages;
        for (const auto& pair : _highlighters) {
            languages.push_back(pair.first);
        }
        return languages;
    }
    
    std::string SyntaxHighlighter::apply_tokens_to_string(const std::string& code, const std::vector<SyntaxToken>& tokens) const 
    {
        if (tokens.empty()) {
            return code;
        }
        
        std::string result;
        size_t last_pos = 0;
        
        for (const auto& token : tokens) {
            // Add any unhighlighted text before this token
            if (token.start_pos > last_pos) {
                result += code.substr(last_pos, token.start_pos - last_pos);
            }
            
            // Add the highlighted token
            std::string token_text = code.substr(token.start_pos, token.end_pos - token.start_pos);
            result += colorize_token(token_text, token.color_code);
            
            last_pos = token.end_pos;
        }
        
        // Add any remaining unhighlighted text
        if (last_pos < code.length()) {
            result += code.substr(last_pos);
        }
        
        return result;
    }
    
    std::string SyntaxHighlighter::colorize_token(const std::string& text, const std::string& color_code) const 
    {
        if (!should_colorize() || color_code.empty()) {
            return text;
        }
        
        return color_code + text + get_reset_code();
    }
    
    LanguageHighlighter* SyntaxHighlighter::get_highlighter(const std::string& language) const 
    {
        auto it = _highlighters.find(language);
        return (it != _highlighters.end()) ? it->second.get() : nullptr;
    }
    
    std::string SyntaxHighlighter::get_reset_code() const 
    {
        return ColorCodes::RESET;
    }
    
    bool SyntaxHighlighter::should_colorize() const 
    {
        return _use_colors;
    }
}