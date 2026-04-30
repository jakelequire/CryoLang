#pragma once

#include "json.hpp"
#include <string>
#include <ostream>
#include <sstream>
#include <cstdint>

namespace cjson
{

    class JsonWriter
    {
    public:
        struct Options
        {
            int indent = -1;                   // -1 for compact, >= 0 for pretty print
            bool escape_unicode = false;       // Escape non-ASCII characters
            bool ensure_ascii = false;         // Ensure output is ASCII
            bool sort_keys = false;            // Sort object keys
            std::string indent_char = " ";     // Character(s) to use for indentation
            std::string line_separator = "\n"; // Line separator for pretty printing
            bool trailing_newline = false;     // Add trailing newline

            // Validation options
            bool validate_utf8 = true;  // Validate UTF-8 strings
            bool allow_nan_inf = false; // Allow NaN and infinity values
        };

        JsonWriter() = default;
        explicit JsonWriter(const Options &options) : options_(options) {}

        // Main serialization methods
        std::string write(const JsonValue &value);
        void write(const JsonValue &value, std::ostream &os);

        // Configuration
        void set_options(const Options &options) { options_ = options; }
        const Options &get_options() const { return options_; }

        // Individual option setters
        void set_indent(int indent) { options_.indent = indent; }
        void set_escape_unicode(bool escape) { options_.escape_unicode = escape; }
        void set_ensure_ascii(bool ensure) { options_.ensure_ascii = ensure; }
        void set_sort_keys(bool sort) { options_.sort_keys = sort; }

    private:
        Options options_;

        // Internal writing methods
        void write_value(const JsonValue &value, std::ostream &os, int current_indent = 0);
        void write_null(std::ostream &os);
        void write_boolean(bool value, std::ostream &os);
        void write_number(double value, std::ostream &os);
        void write_string(const std::string &value, std::ostream &os);
        void write_array(const JsonArray &array, std::ostream &os, int current_indent = 0);
        void write_object(const JsonObject &object, std::ostream &os, int current_indent = 0);

        // Utility methods
        void write_indent(std::ostream &os, int level);
        void write_newline(std::ostream &os);
        std::string escape_string(const std::string &str);
        std::string escape_character(char c);
        std::string unicode_escape(uint32_t codepoint);
        bool is_valid_utf8(const std::string &str);
        std::vector<uint32_t> utf8_to_codepoints(const std::string &str);
        bool needs_escaping(char c);
        bool is_finite_number(double value);

        // Key sorting for objects
        std::vector<std::string> get_sorted_keys(const JsonObject &object);
    };

    // Convenience functions
    std::string to_json(const JsonValue &value, int indent = -1);
    std::string to_json_pretty(const JsonValue &value, int indent = 2);
    std::string to_json_compact(const JsonValue &value);

} // namespace cjson