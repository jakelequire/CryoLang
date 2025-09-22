#pragma once

#include <string>
#include <vector>
#include <unordered_map>

namespace Cryo
{

    /**
     * Structure to represent a parsed function signature
     */
    struct RuntimeFunction
    {
        std::string name;                     // Function name (e.g., "cryo_print")
        std::string cryo_name;                // Cryo-facing name (e.g., "print")
        std::string return_type;              // Return type (e.g., "void", "int")
        std::vector<std::string> param_types; // Parameter types
        std::string signature;                // Full signature string (e.g., "(string) -> void")
        std::string description;              // Function description from comments
    };

    /**
     * Structure to represent a parsed type definition
     */
    struct RuntimeType
    {
        std::string name;        // Type name
        std::string description; // Type description
    };

    /**
     * Parses C runtime headers to extract function signatures and types
     * for automatic symbol table registration
     */
    class RuntimeHeaderParser
    {
    public:
        RuntimeHeaderParser() = default;

        /**
         * Parse all runtime header files to extract function signatures
         * @param runtime_header_path Path to the main runtime header file
         * @return True if parsing succeeded, false otherwise
         */
        bool parse_runtime_headers(const std::string &runtime_header_path);

        /**
         * Get all parsed runtime functions
         */
        const std::vector<RuntimeFunction> &get_functions() const { return functions_; }

        /**
         * Get all parsed runtime types
         */
        const std::vector<RuntimeType> &get_types() const { return types_; }

        /**
         * Clear all parsed data
         */
        void clear();

    private:
        std::vector<RuntimeFunction> functions_;
        std::vector<RuntimeType> types_;

        /**
         * Parse a single header file for function declarations
         */
        bool parse_header_file(const std::string &file_path);

        /**
         * Parse a C function declaration line
         * @param line The line containing the function declaration
         * @param comment Optional comment/description
         */
        RuntimeFunction parse_function_declaration(const std::string &line, const std::string &comment = "");

        /**
         * Parse a C typedef or struct declaration
         */
        RuntimeType parse_type_declaration(const std::string &line, const std::string &comment = "");

        /**
         * Convert C type to Cryo type
         */
        std::string c_type_to_cryo_type(const std::string &c_type);

        /**
         * Extract Cryo function name from C function name
         * e.g., "cryo_print" -> "print"
         */
        std::string extract_cryo_name(const std::string &c_name);

        /**
         * Build Cryo function signature from return type and parameters
         */
        std::string build_signature(const std::string &return_type, const std::vector<std::string> &param_types);

        /**
         * Clean up parsed line (remove extra whitespace, etc.)
         */
        std::string clean_line(const std::string &line);

        /**
         * Extract comment from a line or previous line
         */
        std::string extract_comment(const std::string &line);
    };

}