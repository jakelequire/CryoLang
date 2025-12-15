#include "../include/cjson/parser.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace cjson
{

    JsonParser::JsonParser(const std::string &input)
        : input_(input), pos_(0), line_(1), column_(1),
          allow_comments_(false), allow_trailing_commas_(false),
          allow_single_quotes_(false), allow_unquoted_keys_(false) {}

    JsonParser::JsonParser(std::string_view input)
        : input_(input), pos_(0), line_(1), column_(1),
          allow_comments_(false), allow_trailing_commas_(false),
          allow_single_quotes_(false), allow_unquoted_keys_(false) {}

    JsonValue JsonParser::parse()
    {
        skip_whitespace_and_comments();
        if (is_at_end())
        {
            throw_parse_error("Empty input");
        }

        JsonValue result = parse_value();

        skip_whitespace_and_comments();
        if (!is_at_end())
        {
            throw_parse_error("Unexpected content after JSON value");
        }

        return result;
    }

    JsonValue JsonParser::parse_value()
    {
        skip_whitespace_and_comments();

        if (is_at_end())
        {
            throw_unexpected_end();
        }

        char c = peek();

        switch (c)
        {
        case 'n':
            return parse_literal();
        case 't':
        case 'f':
            return parse_literal();
        case '"':
            return parse_string();
        case '\'':
            if (allow_single_quotes_)
            {
                return parse_string();
            }
            throw_parse_error("Single quotes not allowed (use set_allow_single_quotes(true))");
        case '[':
            return parse_array();
        case '{':
            return parse_object();
        case '-':
        case '0':
        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
        case '6':
        case '7':
        case '8':
        case '9':
            return parse_number();
        default:
            throw_parse_error("Expected valid JSON value but got '" + std::string(1, c) + "'");
        }
    }

    JsonValue JsonParser::parse_object()
    {
        expect('{');
        skip_whitespace_and_comments();

        JsonObject obj;

        if (peek() == '}')
        {
            consume();
            return JsonValue(std::move(obj));
        }

        bool first = true;
        while (!is_at_end())
        {
            if (!first)
            {
                skip_whitespace_and_comments();
                if (peek() == '}')
                {
                    if (allow_trailing_commas_)
                    {
                        consume();
                        return JsonValue(std::move(obj));
                    }
                    else
                    {
                        throw_parse_error("Trailing comma not allowed");
                    }
                }
                expect(',');
                skip_whitespace_and_comments();

                // Check for trailing comma after consuming comma and whitespace
                if (peek() == '}')
                {
                    if (allow_trailing_commas_)
                    {
                        consume();
                        return JsonValue(std::move(obj));
                    }
                    else
                    {
                        throw_parse_error("Trailing comma not allowed");
                    }
                }
            }
            first = false;

            // Parse key
            std::string key;
            char key_char = peek();
            if (key_char == '"' || (allow_single_quotes_ && key_char == '\''))
            {
                JsonValue key_val = parse_string();
                key = key_val.as_string();
            }
            else if (allow_unquoted_keys_ && (std::isalpha(key_char) || key_char == '_' || key_char == '$'))
            {
                // Parse unquoted key
                while (!is_at_end() && (std::isalnum(peek()) || peek() == '_' || peek() == '$'))
                {
                    key += consume();
                }
            }
            else
            {
                throw_parse_error("Expected string key");
            }

            skip_whitespace_and_comments();
            expect(':');
            skip_whitespace_and_comments();

            // Parse value
            JsonValue value = parse_value();
            obj[key] = std::move(value);

            skip_whitespace_and_comments();
            if (peek() == '}')
            {
                consume();
                return JsonValue(std::move(obj));
            }
        }

        throw_unexpected_end();
    }

    JsonValue JsonParser::parse_array()
    {
        expect('[');
        skip_whitespace_and_comments();

        JsonArray arr;

        if (peek() == ']')
        {
            consume();
            return JsonValue(std::move(arr));
        }

        bool first = true;
        while (!is_at_end())
        {
            if (!first)
            {
                skip_whitespace_and_comments();
                if (peek() == ']')
                {
                    if (allow_trailing_commas_)
                    {
                        consume();
                        return JsonValue(std::move(arr));
                    }
                    else
                    {
                        throw_parse_error("Trailing comma not allowed");
                    }
                }
                expect(',');
                skip_whitespace_and_comments();

                // Check for trailing comma after consuming comma and whitespace
                if (peek() == ']')
                {
                    if (allow_trailing_commas_)
                    {
                        consume();
                        return JsonValue(std::move(arr));
                    }
                    else
                    {
                        throw_parse_error("Trailing comma not allowed");
                    }
                }
            }
            first = false;

            JsonValue value = parse_value();
            arr.push_back(std::move(value));

            skip_whitespace_and_comments();
            if (peek() == ']')
            {
                consume();
                return JsonValue(std::move(arr));
            }
        }

        throw_unexpected_end();
    }

    JsonValue JsonParser::parse_string()
    {
        char quote = peek();
        if (quote != '"' && !(allow_single_quotes_ && quote == '\''))
        {
            throw_parse_error("Expected string delimiter");
        }

        consume(); // consume opening quote
        std::string result = parse_string_content(quote);
        expect(quote); // consume closing quote

        return JsonValue(std::move(result));
    }

    std::string JsonParser::parse_string_content(char quote_char)
    {
        std::string result;

        while (!is_at_end() && peek() != quote_char)
        {
            char c = peek();

            if (c == '\\')
            {
                consume(); // consume backslash
                result += parse_escape_sequence();
            }
            else if (static_cast<unsigned char>(c) < 0x20)
            {
                throw_parse_error("Unescaped control character in string");
            }
            else
            {
                result += consume();
            }
        }

        return result;
    }

    std::string JsonParser::parse_escape_sequence()
    {
        if (is_at_end())
        {
            throw_unexpected_end();
        }

        char c = consume();
        switch (c)
        {
        case '"':
            return "\"";
        case '\'':
            return "'";
        case '\\':
            return "\\";
        case '/':
            return "/";
        case 'b':
            return "\b";
        case 'f':
            return "\f";
        case 'n':
            return "\n";
        case 'r':
            return "\r";
        case 't':
            return "\t";
        case 'u':
        {
            uint32_t codepoint = parse_unicode_escape();
            return unicode_to_utf8(codepoint);
        }
        default:
            throw_parse_error("Invalid escape sequence: \\" + std::string(1, c));
        }
    }

    uint32_t JsonParser::parse_unicode_escape()
    {
        uint32_t result = 0;

        for (int i = 0; i < 4; ++i)
        {
            if (is_at_end())
            {
                throw_unexpected_end();
            }

            char c = consume();
            if (!is_hex_digit(c))
            {
                throw_parse_error("Invalid unicode escape sequence");
            }

            result = result * 16 + (c >= 'a' ? c - 'a' + 10 : c >= 'A' ? c - 'A' + 10
                                                                       : c - '0');
        }

        return result;
    }

    std::string JsonParser::unicode_to_utf8(uint32_t codepoint)
    {
        std::string result;

        if (codepoint <= 0x7F)
        {
            result += static_cast<char>(codepoint);
        }
        else if (codepoint <= 0x7FF)
        {
            result += static_cast<char>(0xC0 | (codepoint >> 6));
            result += static_cast<char>(0x80 | (codepoint & 0x3F));
        }
        else if (codepoint <= 0xFFFF)
        {
            result += static_cast<char>(0xE0 | (codepoint >> 12));
            result += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
            result += static_cast<char>(0x80 | (codepoint & 0x3F));
        }
        else if (codepoint <= 0x10FFFF)
        {
            result += static_cast<char>(0xF0 | (codepoint >> 18));
            result += static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));
            result += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
            result += static_cast<char>(0x80 | (codepoint & 0x3F));
        }
        else
        {
            throw_parse_error("Invalid unicode codepoint");
        }

        return result;
    }

    JsonValue JsonParser::parse_number()
    {
        size_t start_pos = pos_;

        // Handle negative sign
        if (peek() == '-')
        {
            consume();
        }

        // Parse integer part
        if (peek() == '0')
        {
            consume();
        }
        else if (is_digit(peek()))
        {
            while (!is_at_end() && is_digit(peek()))
            {
                consume();
            }
        }
        else
        {
            throw_parse_error("Invalid number format");
        }

        // Parse fractional part
        if (!is_at_end() && peek() == '.')
        {
            consume();
            if (!is_digit(peek()))
            {
                throw_parse_error("Expected digit after decimal point");
            }
            while (!is_at_end() && is_digit(peek()))
            {
                consume();
            }
        }

        // Parse exponent part
        if (!is_at_end() && (peek() == 'e' || peek() == 'E'))
        {
            consume();
            if (!is_at_end() && (peek() == '+' || peek() == '-'))
            {
                consume();
            }
            if (!is_digit(peek()))
            {
                throw_parse_error("Expected digit in exponent");
            }
            while (!is_at_end() && is_digit(peek()))
            {
                consume();
            }
        }

        std::string number_str(input_.substr(start_pos, pos_ - start_pos));
        double value = std::stod(number_str);

        if (!std::isfinite(value))
        {
            throw_parse_error("Number out of range");
        }

        return JsonValue(value);
    }

    JsonValue JsonParser::parse_literal()
    {
        if (consume_string("null"))
        {
            return JsonValue();
        }
        else if (consume_string("true"))
        {
            return JsonValue(true);
        }
        else if (consume_string("false"))
        {
            return JsonValue(false);
        }
        else
        {
            throw_parse_error("Invalid literal");
        }
    }

    void JsonParser::skip_whitespace_and_comments()
    {
        while (!is_at_end())
        {
            char c = peek();
            if (std::isspace(c))
            {
                skip_whitespace();
            }
            else if (allow_comments_ && c == '/')
            {
                skip_comment();
            }
            else
            {
                break;
            }
        }
    }

    void JsonParser::skip_whitespace()
    {
        while (!is_at_end() && std::isspace(peek()))
        {
            consume();
        }
    }

    void JsonParser::skip_comment()
    {
        if (peek() != '/')
        {
            return;
        }

        consume(); // consume first '/'
        if (is_at_end())
        {
            throw_parse_error("Incomplete comment");
        }

        char next = consume();
        if (next == '/')
        {
            // Line comment
            while (!is_at_end() && peek() != '\n')
            {
                consume();
            }
        }
        else if (next == '*')
        {
            // Block comment
            while (!is_at_end())
            {
                if (peek() == '*' && peek(1) == '/')
                {
                    consume(); // consume '*'
                    consume(); // consume '/'
                    break;
                }
                consume();
            }
        }
        else
        {
            throw_parse_error("Invalid comment syntax");
        }
    }

    char JsonParser::peek() const
    {
        return peek(0);
    }

    char JsonParser::peek(size_t offset) const
    {
        size_t pos = pos_ + offset;
        return pos < input_.size() ? input_[pos] : '\0';
    }

    char JsonParser::consume()
    {
        if (is_at_end())
        {
            return '\0';
        }

        char c = input_[pos_++];
        advance_position(c);
        return c;
    }

    bool JsonParser::consume_if(char expected)
    {
        if (!is_at_end() && peek() == expected)
        {
            consume();
            return true;
        }
        return false;
    }

    bool JsonParser::consume_string(const std::string &str)
    {
        if (pos_ + str.length() > input_.size())
        {
            return false;
        }

        if (input_.substr(pos_, str.length()) == str)
        {
            for (size_t i = 0; i < str.length(); ++i)
            {
                consume();
            }
            return true;
        }

        return false;
    }

    void JsonParser::expect(char expected)
    {
        if (is_at_end())
        {
            throw_unexpected_end();
        }

        char actual = consume();
        if (actual != expected)
        {
            throw_unexpected_character(expected, actual);
        }
    }

    bool JsonParser::is_at_end() const
    {
        return pos_ >= input_.size();
    }

    bool JsonParser::is_digit(char c) const
    {
        return c >= '0' && c <= '9';
    }

    bool JsonParser::is_hex_digit(char c) const
    {
        return is_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
    }

    void JsonParser::advance_position(char c)
    {
        if (c == '\n')
        {
            line_++;
            column_ = 1;
        }
        else
        {
            column_++;
        }
    }

    void JsonParser::throw_parse_error(const std::string &message)
    {
        throw JsonParseException(message, line_, column_);
    }

    void JsonParser::throw_unexpected_character(char expected, char actual)
    {
        std::string message = "Expected '" + std::string(1, expected) + "', got '" + std::string(1, actual) + "'";
        throw_parse_error(message);
    }

    void JsonParser::throw_unexpected_end()
    {
        throw_parse_error("Unexpected end of input");
    }

} // namespace cjson