#ifndef CRYOLSP_JSON_UTILS_HPP
#define CRYOLSP_JSON_UTILS_HPP

#include <string>
#include <unordered_map>
#include <vector>
#include "Server.hpp"

namespace CryoLSP
{
    namespace JsonUtils
    {

        // Basic JSON parsing and serialization utilities
        // Note: This is a minimal implementation for LSP needs
        // In a production environment, you'd use a proper JSON library

        class JsonObject
        {
        private:
            std::unordered_map<std::string, std::string> data;

        public:
            void set(const std::string &key, const std::string &value);
            void set(const std::string &key, int value);
            void set(const std::string &key, bool value);

            std::string get_string(const std::string &key, const std::string &default_value = "") const;
            int get_int(const std::string &key, int default_value = 0) const;
            bool get_bool(const std::string &key, bool default_value = false) const;

            bool has_key(const std::string &key) const;
            std::string serialize() const;

            static JsonObject parse(const std::string &json_str);
        };

        // Helper functions for LSP-specific JSON serialization
        std::string serialize_position(const Position &pos);
        std::string serialize_range(const Range &range);
        std::string serialize_location(const Location &location);
        std::string serialize_diagnostic(const Diagnostic &diagnostic);
        std::string serialize_completion_item(const CompletionItem &item);
        std::string serialize_symbol_info(const SymbolInformation &symbol);
        std::string serialize_hover(const Hover &hover);

        std::string serialize_diagnostics_array(const std::vector<Diagnostic> &diagnostics);
        std::string serialize_completions_array(const std::vector<CompletionItem> &completions);
        std::string serialize_locations_array(const std::vector<Location> &locations);
        std::string serialize_symbols_array(const std::vector<SymbolInformation> &symbols);

        // Helper functions for LSP-specific JSON parsing
        Position parse_position(const std::string &json_str);
        Range parse_range(const std::string &json_str);
        Location parse_location(const std::string &json_str);

        // JSON-RPC specific utilities
        std::string create_response(const std::string &id, const std::string &result);
        std::string create_error_response(const std::string &id, int code, const std::string &message);
        std::string create_notification(const std::string &method, const std::string &params);

        // Extract values from JSON strings (basic implementation)
        std::string extract_json_value(const std::string &json, const std::string &key);
        std::string extract_nested_value(const std::string &json, const std::string &path);

    } // namespace JsonUtils
} // namespace CryoLSP

#endif // CRYOLSP_JSON_UTILS_HPP