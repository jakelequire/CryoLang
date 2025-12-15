#pragma once

#include "json.hpp"
#include <string>
#include <string_view>
#include <cctype>
#include <cstdint>

namespace cjson
{

    class JsonParser
    {
    public:
        explicit JsonParser(const std::string &input);
        explicit JsonParser(std::string_view input);

        JsonValue parse();

        // Configuration options
        void set_allow_comments(bool allow) { allow_comments_ = allow; }
        void set_allow_trailing_commas(bool allow) { allow_trailing_commas_ = allow; }
        void set_allow_single_quotes(bool allow) { allow_single_quotes_ = allow; }
        void set_allow_unquoted_keys(bool allow) { allow_unquoted_keys_ = allow; }

    private:
        std::string_view input_;
        size_t pos_;
        size_t line_;
        size_t column_;
        bool allow_comments_;
        bool allow_trailing_commas_;
        bool allow_single_quotes_;
        bool allow_unquoted_keys_;

        // Core parsing methods
        JsonValue parse_value();
        JsonValue parse_object();
        JsonValue parse_array();
        JsonValue parse_string();
        JsonValue parse_number();
        JsonValue parse_literal();

        // Utility methods
        void skip_whitespace_and_comments();
        void skip_whitespace();
        void skip_comment();
        char peek() const;
        char peek(size_t offset) const;
        char consume();
        bool consume_if(char expected);
        bool consume_string(const std::string &str);
        void expect(char expected);
        bool is_at_end() const;

        // String parsing helpers
        std::string parse_string_content(char quote_char);
        std::string parse_escape_sequence();
        uint32_t parse_unicode_escape();
        std::string unicode_to_utf8(uint32_t codepoint);

        // Number parsing helpers
        double parse_numeric_value();
        bool is_digit(char c) const;
        bool is_hex_digit(char c) const;

        // Error reporting
        [[noreturn]] void throw_parse_error(const std::string &message);
        [[noreturn]] void throw_unexpected_character(char expected, char actual);
        [[noreturn]] void throw_unexpected_end();

        // Position tracking
        void advance_position(char c);
        void save_position();
        void restore_position();

        struct Position
        {
            size_t pos;
            size_t line;
            size_t column;
        };

        Position saved_pos_;
    };

} // namespace cjson