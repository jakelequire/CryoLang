#include "../include/cjson/writer.hpp"
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <cmath>

namespace cjson
{

    std::string JsonWriter::write(const JsonValue &value)
    {
        std::ostringstream os;
        write(value, os);
        return os.str();
    }

    void JsonWriter::write(const JsonValue &value, std::ostream &os)
    {
        write_value(value, os, 0);
        if (options_.trailing_newline)
        {
            write_newline(os);
        }
    }

    void JsonWriter::write_value(const JsonValue &value, std::ostream &os, int current_indent)
    {
        switch (value.type())
        {
        case JsonType::Null:
            write_null(os);
            break;
        case JsonType::Boolean:
            write_boolean(value.as_boolean(), os);
            break;
        case JsonType::Number:
            write_number(value.as_number(), os);
            break;
        case JsonType::String:
            write_string(value.as_string(), os);
            break;
        case JsonType::Array:
            write_array(value.as_array(), os, current_indent);
            break;
        case JsonType::Object:
            write_object(value.as_object(), os, current_indent);
            break;
        }
    }

    void JsonWriter::write_null(std::ostream &os)
    {
        os << "null";
    }

    void JsonWriter::write_boolean(bool value, std::ostream &os)
    {
        os << (value ? "true" : "false");
    }

    void JsonWriter::write_number(double value, std::ostream &os)
    {
        if (!is_finite_number(value))
        {
            if (options_.allow_nan_inf)
            {
                if (std::isnan(value))
                {
                    os << "NaN";
                }
                else if (std::isinf(value))
                {
                    os << (value > 0 ? "Infinity" : "-Infinity");
                }
            }
            else
            {
                throw JsonException("NaN and infinity values are not allowed");
            }
            return;
        }

        // Use a separate stringstream for number formatting to avoid affecting the main stream
        std::ostringstream temp_os;

        // Check if it's effectively an integer
        if (std::floor(value) == value && value >= -9007199254740992.0 && value <= 9007199254740992.0)
        {
            temp_os << std::fixed << std::setprecision(0) << value;
        }
        else
        {
            // For non-integers, use a reasonable precision and remove trailing zeros
            temp_os << std::fixed << std::setprecision(15) << value;
            std::string str = temp_os.str();

            // Remove trailing zeros and decimal point if not needed
            size_t decimal_pos = str.find('.');
            if (decimal_pos != std::string::npos)
            {
                size_t end = str.length() - 1;
                while (end > decimal_pos && str[end] == '0')
                {
                    end--;
                }
                if (str[end] == '.')
                {
                    end--;
                }
                str = str.substr(0, end + 1);
            }

            os << str;
            return;
        }

        // Write the formatted string to the output stream without affecting its state
        os << temp_os.str();
    }

    void JsonWriter::write_string(const std::string &value, std::ostream &os)
    {
        if (options_.validate_utf8 && !is_valid_utf8(value))
        {
            throw JsonException("Invalid UTF-8 sequence in string");
        }

        os << '"';
        os << escape_string(value);
        os << '"';
    }

    void JsonWriter::write_array(const JsonArray &array, std::ostream &os, int current_indent)
    {
        os << '[';

        if (array.empty())
        {
            os << ']';
            return;
        }

        bool pretty_print = options_.indent >= 0;
        bool first = true;

        for (const auto &element : array)
        {
            if (!first)
            {
                os << ',';
            }
            first = false;

            if (pretty_print)
            {
                write_newline(os);
                write_indent(os, current_indent + 1);
            }

            write_value(element, os, current_indent + 1);
        }

        if (pretty_print)
        {
            write_newline(os);
            write_indent(os, current_indent);
        }

        os << ']';
    }

    void JsonWriter::write_object(const JsonObject &object, std::ostream &os, int current_indent)
    {
        os << '{';

        if (object.empty())
        {
            os << '}';
            return;
        }

        bool pretty_print = options_.indent >= 0;
        bool first = true;

        std::vector<std::string> keys;
        if (options_.sort_keys)
        {
            keys = get_sorted_keys(object);
        }
        else
        {
            for (const auto &pair : object)
            {
                keys.push_back(pair.first);
            }
        }

        for (const auto &key : keys)
        {
            if (!first)
            {
                os << ',';
            }
            first = false;

            if (pretty_print)
            {
                write_newline(os);
                write_indent(os, current_indent + 1);
            }

            write_string(key, os);
            os << ':';

            if (pretty_print)
            {
                os << ' ';
            }

            write_value(object.at(key), os, current_indent + 1);
        }

        if (pretty_print)
        {
            write_newline(os);
            write_indent(os, current_indent);
        }

        os << '}';
    }

    void JsonWriter::write_indent(std::ostream &os, int level)
    {
        if (options_.indent >= 0)
        {
            for (int i = 0; i < level * options_.indent; ++i)
            {
                os << options_.indent_char;
            }
        }
    }

    void JsonWriter::write_newline(std::ostream &os)
    {
        if (options_.indent >= 0)
        {
            os << options_.line_separator;
        }
    }

    std::string JsonWriter::escape_string(const std::string &str)
    {
        std::string result;
        result.reserve(str.length() * 2); // Reserve some extra space

        for (char c : str)
        {
            if (needs_escaping(c))
            {
                result += escape_character(c);
            }
            else if (options_.escape_unicode && static_cast<unsigned char>(c) > 127)
            {
                result += unicode_escape(static_cast<unsigned char>(c));
            }
            else if (options_.ensure_ascii && static_cast<unsigned char>(c) > 127)
            {
                // Convert multi-byte UTF-8 to unicode escapes
                auto codepoints = utf8_to_codepoints(std::string(1, c));
                for (uint32_t cp : codepoints)
                {
                    result += unicode_escape(cp);
                }
            }
            else
            {
                result += c;
            }
        }

        return result;
    }

    std::string JsonWriter::escape_character(char c)
    {
        switch (c)
        {
        case '"':
            return "\\\"";
        case '\\':
            return "\\\\";
        case '\b':
            return "\\b";
        case '\f':
            return "\\f";
        case '\n':
            return "\\n";
        case '\r':
            return "\\r";
        case '\t':
            return "\\t";
        default:
            if (static_cast<unsigned char>(c) < 0x20)
            {
                return unicode_escape(static_cast<unsigned char>(c));
            }
            return std::string(1, c);
        }
    }

    std::string JsonWriter::unicode_escape(uint32_t codepoint)
    {
        std::ostringstream os;
        os << "\\u" << std::hex << std::setfill('0') << std::setw(4) << codepoint;
        return os.str();
    }

    bool JsonWriter::needs_escaping(char c)
    {
        return c == '"' || c == '\\' || static_cast<unsigned char>(c) < 0x20;
    }

    bool JsonWriter::is_finite_number(double value)
    {
        return std::isfinite(value);
    }

    bool JsonWriter::is_valid_utf8(const std::string &str)
    {
        // Basic UTF-8 validation
        for (size_t i = 0; i < str.length();)
        {
            unsigned char c = str[i];

            if (c <= 0x7F)
            {
                // ASCII character
                i++;
            }
            else if ((c >> 5) == 0x06)
            {
                // 110xxxxx - 2 byte sequence
                if (i + 1 >= str.length() || (str[i + 1] & 0xC0) != 0x80)
                {
                    return false;
                }
                i += 2;
            }
            else if ((c >> 4) == 0x0E)
            {
                // 1110xxxx - 3 byte sequence
                if (i + 2 >= str.length() ||
                    (str[i + 1] & 0xC0) != 0x80 ||
                    (str[i + 2] & 0xC0) != 0x80)
                {
                    return false;
                }
                i += 3;
            }
            else if ((c >> 3) == 0x1E)
            {
                // 11110xxx - 4 byte sequence
                if (i + 3 >= str.length() ||
                    (str[i + 1] & 0xC0) != 0x80 ||
                    (str[i + 2] & 0xC0) != 0x80 ||
                    (str[i + 3] & 0xC0) != 0x80)
                {
                    return false;
                }
                i += 4;
            }
            else
            {
                return false;
            }
        }

        return true;
    }

    std::vector<uint32_t> JsonWriter::utf8_to_codepoints(const std::string &str)
    {
        std::vector<uint32_t> codepoints;

        for (size_t i = 0; i < str.length();)
        {
            uint32_t codepoint = 0;
            unsigned char c = str[i];

            if (c <= 0x7F)
            {
                codepoint = c;
                i++;
            }
            else if ((c >> 5) == 0x06)
            {
                codepoint = ((c & 0x1F) << 6) | (str[i + 1] & 0x3F);
                i += 2;
            }
            else if ((c >> 4) == 0x0E)
            {
                codepoint = ((c & 0x0F) << 12) | ((str[i + 1] & 0x3F) << 6) | (str[i + 2] & 0x3F);
                i += 3;
            }
            else if ((c >> 3) == 0x1E)
            {
                codepoint = ((c & 0x07) << 18) | ((str[i + 1] & 0x3F) << 12) |
                            ((str[i + 2] & 0x3F) << 6) | (str[i + 3] & 0x3F);
                i += 4;
            }
            else
            {
                i++; // Skip invalid byte
            }

            codepoints.push_back(codepoint);
        }

        return codepoints;
    }

    std::vector<std::string> JsonWriter::get_sorted_keys(const JsonObject &object)
    {
        std::vector<std::string> keys;
        for (const auto &pair : object)
        {
            keys.push_back(pair.first);
        }
        std::sort(keys.begin(), keys.end());
        return keys;
    }

    // Convenience functions
    std::string to_json(const JsonValue &value, int indent)
    {
        JsonWriter writer;
        writer.set_indent(indent);
        return writer.write(value);
    }

    std::string to_json_pretty(const JsonValue &value, int indent)
    {
        JsonWriter writer;
        writer.set_indent(indent);
        return writer.write(value);
    }

    std::string to_json_compact(const JsonValue &value)
    {
        JsonWriter writer;
        writer.set_indent(-1);
        return writer.write(value);
    }

} // namespace cjson