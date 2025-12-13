#include "JsonUtils.hpp"
#include <sstream>
#include <algorithm>
#include <regex>

namespace CryoLSP
{
    namespace JsonUtils
    {

        // JsonObject implementation
        void JsonObject::set(const std::string &key, const std::string &value)
        {
            // Escape quotes in the value
            std::string escaped_value = value;
            std::string::size_type pos = 0;
            while ((pos = escaped_value.find("\"", pos)) != std::string::npos)
            {
                escaped_value.replace(pos, 1, "\\\"");
                pos += 2;
            }
            data[key] = "\"" + escaped_value + "\"";
        }

        void JsonObject::set(const std::string &key, int value)
        {
            data[key] = std::to_string(value);
        }

        void JsonObject::set(const std::string &key, bool value)
        {
            data[key] = value ? "true" : "false";
        }

        std::string JsonObject::get_string(const std::string &key, const std::string &default_value) const
        {
            auto it = data.find(key);
            if (it == data.end())
                return default_value;

            std::string value = it->second;
            // Remove surrounding quotes if present
            if (value.front() == '\"' && value.back() == '\"')
            {
                value = value.substr(1, value.length() - 2);
            }
            return value;
        }

        int JsonObject::get_int(const std::string &key, int default_value) const
        {
            auto it = data.find(key);
            if (it == data.end())
                return default_value;

            try
            {
                return std::stoi(it->second);
            }
            catch (...)
            {
                return default_value;
            }
        }

        bool JsonObject::get_bool(const std::string &key, bool default_value) const
        {
            auto it = data.find(key);
            if (it == data.end())
                return default_value;

            return it->second == "true";
        }

        bool JsonObject::has_key(const std::string &key) const
        {
            return data.find(key) != data.end();
        }

        std::string JsonObject::serialize() const
        {
            if (data.empty())
                return "{}";

            std::stringstream ss;
            ss << "{";

            bool first = true;
            for (const auto &pair : data)
            {
                if (!first)
                    ss << ",";
                ss << "\"" << pair.first << "\":" << pair.second;
                first = false;
            }

            ss << "}";
            return ss.str();
        }

        JsonObject JsonObject::parse(const std::string &json_str)
        {
            JsonObject obj;

            // Very basic JSON parsing - in production use a proper JSON library
            std::regex key_value_regex("\"([^\"]+)\"\\s*:\\s*([^,}]+)");
            std::sregex_iterator iter(json_str.begin(), json_str.end(), key_value_regex);
            std::sregex_iterator end;

            for (; iter != end; ++iter)
            {
                std::smatch match = *iter;
                std::string key = match[1].str();
                std::string value = match[2].str();

                // Trim whitespace
                value.erase(0, value.find_first_not_of(" \t\n\r"));
                value.erase(value.find_last_not_of(" \t\n\r") + 1);

                obj.data[key] = value;
            }

            return obj;
        }

        // LSP serialization functions
        std::string serialize_position(const Position &pos)
        {
            std::stringstream ss;
            ss << "{\"line\":" << pos.line << ",\"character\":" << pos.character << "}";
            return ss.str();
        }

        std::string serialize_range(const Range &range)
        {
            std::stringstream ss;
            ss << "{\"start\":" << serialize_position(range.start)
               << ",\"end\":" << serialize_position(range.end) << "}";
            return ss.str();
        }

        std::string serialize_location(const Location &location)
        {
            std::stringstream ss;
            ss << "{\"uri\":\"" << location.uri << "\",\"range\":" << serialize_range(location.range) << "}";
            return ss.str();
        }

        std::string serialize_diagnostic(const Diagnostic &diagnostic)
        {
            std::stringstream ss;
            ss << "{\"range\":" << serialize_range(diagnostic.range)
               << ",\"message\":\"" << diagnostic.message << "\""
               << ",\"severity\":" << diagnostic.severity
               << ",\"source\":\"" << diagnostic.source << "\"";

            if (!diagnostic.code.empty())
            {
                ss << ",\"code\":\"" << diagnostic.code << "\"";
            }

            ss << "}";
            return ss.str();
        }

        std::string serialize_completion_item(const CompletionItem &item)
        {
            std::stringstream ss;
            ss << "{\"label\":\"" << item.label << "\""
               << ",\"kind\":" << item.kind;

            if (!item.detail.empty())
            {
                ss << ",\"detail\":\"" << item.detail << "\"";
            }

            if (!item.documentation.empty())
            {
                ss << ",\"documentation\":\"" << item.documentation << "\"";
            }

            if (!item.insertText.empty())
            {
                ss << ",\"insertText\":\"" << item.insertText << "\"";
            }

            ss << "}";
            return ss.str();
        }

        std::string serialize_symbol_info(const SymbolInformation &symbol)
        {
            std::stringstream ss;
            ss << "{\"name\":\"" << symbol.name << "\""
               << ",\"kind\":" << symbol.kind
               << ",\"location\":" << serialize_location(symbol.location);

            if (!symbol.containerName.empty())
            {
                ss << ",\"containerName\":\"" << symbol.containerName << "\"";
            }

            ss << "}";
            return ss.str();
        }

        std::string serialize_hover(const Hover &hover)
        {
            std::stringstream ss;
            ss << "{\"contents\":\"" << hover.contents << "\""
               << ",\"range\":" << serialize_range(hover.range) << "}";
            return ss.str();
        }

        // Array serialization
        std::string serialize_diagnostics_array(const std::vector<Diagnostic> &diagnostics)
        {
            std::stringstream ss;
            ss << "[";

            for (size_t i = 0; i < diagnostics.size(); ++i)
            {
                if (i > 0)
                    ss << ",";
                ss << serialize_diagnostic(diagnostics[i]);
            }

            ss << "]";
            return ss.str();
        }

        std::string serialize_completions_array(const std::vector<CompletionItem> &completions)
        {
            std::stringstream ss;
            ss << "[";

            for (size_t i = 0; i < completions.size(); ++i)
            {
                if (i > 0)
                    ss << ",";
                ss << serialize_completion_item(completions[i]);
            }

            ss << "]";
            return ss.str();
        }

        std::string serialize_locations_array(const std::vector<Location> &locations)
        {
            std::stringstream ss;
            ss << "[";

            for (size_t i = 0; i < locations.size(); ++i)
            {
                if (i > 0)
                    ss << ",";
                ss << serialize_location(locations[i]);
            }

            ss << "]";
            return ss.str();
        }

        std::string serialize_symbols_array(const std::vector<SymbolInformation> &symbols)
        {
            std::stringstream ss;
            ss << "[";

            for (size_t i = 0; i < symbols.size(); ++i)
            {
                if (i > 0)
                    ss << ",";
                ss << serialize_symbol_info(symbols[i]);
            }

            ss << "]";
            return ss.str();
        }

        // JSON-RPC utilities
        std::string create_response(const std::string &id, const std::string &result)
        {
            std::stringstream ss;
            ss << "{\"jsonrpc\":\"2.0\",\"id\":\"" << id << "\",\"result\":" << result << "}";
            return ss.str();
        }

        std::string create_error_response(const std::string &id, int code, const std::string &message)
        {
            std::stringstream ss;
            ss << "{\"jsonrpc\":\"2.0\",\"id\":\"" << id
               << "\",\"error\":{\"code\":" << code << ",\"message\":\"" << message << "\"}}";
            return ss.str();
        }

        std::string create_notification(const std::string &method, const std::string &params)
        {
            std::stringstream ss;
            ss << "{\"jsonrpc\":\"2.0\",\"method\":\"" << method << "\",\"params\":" << params << "}";
            return ss.str();
        }

        // Basic JSON value extraction
        std::string extract_json_value(const std::string &json, const std::string &key)
        {
            // Look for the key
            std::string search_pattern = "\"" + key + "\"\\s*:\\s*";
            std::regex key_regex(search_pattern);
            std::smatch match;

            if (!std::regex_search(json, match, key_regex))
            {
                return "";
            }

            // Find the start of the value
            size_t value_start = match.position() + match.length();
            if (value_start >= json.length())
            {
                return "";
            }

            // Handle string values (with proper escaped quote handling)
            if (json[value_start] == '"')
            {
                value_start++; // Skip opening quote
                std::string result;
                size_t pos = value_start;

                while (pos < json.length())
                {
                    char c = json[pos];
                    if (c == '"' && (pos == value_start || json[pos - 1] != '\\'))
                    {
                        // Found unescaped closing quote
                        break;
                    }
                    if (c == '\\' && pos + 1 < json.length())
                    {
                        // Handle escape sequences
                        char next = json[pos + 1];
                        if (next == '"' || next == '\\' || next == '/' || next == 'n' || next == 'r' || next == 't')
                        {
                            result += next == 'n' ? '\n' : (next == 'r' ? '\r' : (next == 't' ? '\t' : next));
                            pos += 2;
                            continue;
                        }
                    }
                    result += c;
                    pos++;
                }
                return result;
            }

            // Handle numeric/boolean values
            std::regex numeric_regex("([^,}\\s]+)");
            std::string remaining = json.substr(value_start);
            if (std::regex_search(remaining, match, numeric_regex))
            {
                return match[1].str();
            }

            return "";
        }

        std::string extract_nested_value(const std::string &json, const std::string &path)
        {
            // Simple implementation for paths like "params.textDocument.uri"
            std::stringstream ss(path);
            std::string segment;
            std::string current_json = json;

            while (std::getline(ss, segment, '.'))
            {
                current_json = extract_json_value(current_json, segment);
                if (current_json.empty())
                    break;
            }

            return current_json;
        }

    } // namespace JsonUtils
} // namespace CryoLSP