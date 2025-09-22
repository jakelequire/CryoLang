#include "Utils/RuntimeHeaderParser.hpp"
#include <fstream>
#include <sstream>
#include <regex>
#include <iostream>
#include <algorithm>

namespace Cryo
{

    bool RuntimeHeaderParser::parse_runtime_headers(const std::string &runtime_header_path)
    {
        clear();

        if (!parse_header_file(runtime_header_path))
        {
            std::cerr << "Failed to parse runtime header: " << runtime_header_path << std::endl;
            return false;
        }

        std::cout << "Parsed " << functions_.size() << " runtime functions and "
                  << types_.size() << " runtime types" << std::endl;

        return true;
    }

    bool RuntimeHeaderParser::parse_header_file(const std::string &file_path)
    {
        std::ifstream file(file_path);
        if (!file.is_open())
        {
            std::cerr << "Could not open header file: " << file_path << std::endl;
            return false;
        }

        std::string line;
        std::string current_comment;

        while (std::getline(file, line))
        {
            line = clean_line(line);

            // Skip empty lines
            if (line.empty())
            {
                current_comment.clear();
                continue;
            }

            // Collect comments
            if (line.find("//") == 0 || line.find("/*") != std::string::npos)
            {
                current_comment = extract_comment(line);
                continue;
            }

            // Parse function declarations
            // Look for pattern: return_type function_name(params);
            std::regex func_pattern(R"(^([a-zA-Z_][a-zA-Z0-9_*\s]*)\s+([a-zA-Z_][a-zA-Z0-9_]*)\s*\((.*?)\)\s*;)");
            std::smatch func_match;

            if (std::regex_search(line, func_match, func_pattern))
            {
                RuntimeFunction func = parse_function_declaration(line, current_comment);
                if (!func.name.empty())
                {
                    functions_.push_back(func);
                }
                current_comment.clear();
                continue;
            }

            // Parse typedef declarations
            std::regex typedef_pattern(R"(^typedef\s+.*\s+([a-zA-Z_][a-zA-Z0-9_]*)\s*;)");
            std::smatch typedef_match;

            if (std::regex_search(line, typedef_match, typedef_pattern))
            {
                RuntimeType type = parse_type_declaration(line, current_comment);
                if (!type.name.empty())
                {
                    types_.push_back(type);
                }
                current_comment.clear();
                continue;
            }

            // Reset comment if we didn't match anything
            current_comment.clear();
        }

        return true;
    }

    RuntimeFunction RuntimeHeaderParser::parse_function_declaration(const std::string &line, const std::string &comment)
    {
        RuntimeFunction func;

        // Regex to parse: return_type function_name(param1, param2, ...);
        std::regex pattern(R"(^([a-zA-Z_][a-zA-Z0-9_*\s]*)\s+([a-zA-Z_][a-zA-Z0-9_]*)\s*\((.*?)\)\s*;)");
        std::smatch match;

        if (!std::regex_search(line, match, pattern))
        {
            return func; // Return empty function
        }

        std::string return_type = clean_line(match[1].str());
        func.name = match[2].str();
        std::string params_str = match[3].str();

        // Convert C types to Cryo types
        func.return_type = c_type_to_cryo_type(return_type);
        func.cryo_name = extract_cryo_name(func.name);
        func.description = comment;

        // Parse parameters
        if (!params_str.empty() && params_str != "void")
        {
            std::stringstream ss(params_str);
            std::string param;

            while (std::getline(ss, param, ','))
            {
                param = clean_line(param);
                if (!param.empty())
                {
                    // Extract type from parameter (ignore parameter name)
                    // Look for pattern: [const] type [parameter_name]
                    std::regex param_pattern(R"(^(?:const\s+)?([a-zA-Z_][a-zA-Z0-9_*]*)\s+[a-zA-Z_][a-zA-Z0-9_]*\s*$)");
                    std::smatch param_match;
                    if (std::regex_search(param, param_match, param_pattern))
                    {
                        std::string param_type = clean_line(param_match[1].str());
                        std::string converted_type = c_type_to_cryo_type(param_type);
                        func.param_types.push_back(converted_type);
                    }
                }
            }
        }

        // Build signature
        func.signature = build_signature(func.return_type, func.param_types);

        return func;
    }

    RuntimeType RuntimeHeaderParser::parse_type_declaration(const std::string &line, const std::string &comment)
    {
        RuntimeType type;

        std::regex pattern(R"(^typedef\s+.*\s+([a-zA-Z_][a-zA-Z0-9_]*)\s*;)");
        std::smatch match;

        if (std::regex_search(line, match, pattern))
        {
            type.name = match[1].str();
            type.description = comment.empty() ? "Runtime type" : comment;
        }

        return type;
    }

    std::string RuntimeHeaderParser::c_type_to_cryo_type(const std::string &c_type)
    {
        std::string type = clean_line(c_type);

        // Remove const, volatile, etc.
        type = std::regex_replace(type, std::regex(R"(\bconst\s+)"), "");
        type = std::regex_replace(type, std::regex(R"(\bvolatile\s+)"), "");
        type = clean_line(type);

        // Map CryoLang runtime types to Cryo language types
        if (type == "cryo_int")
            return "int";
        if (type == "cryo_float")
            return "float";
        if (type == "cryo_bool")
            return "boolean";
        if (type == "cryo_char")
            return "char";
        if (type == "cryo_string")
            return "string";
        if (type == "cryo_array")
            return "array"; // Generic array, might need special handling

        // Map standard C types to Cryo types
        if (type == "void")
            return "void";
        if (type == "int" || type == "int32_t")
            return "int";
        if (type == "float" || type == "double")
            return "float";
        if (type == "char")
            return "char";
        if (type == "char*" || type == "const char*")
            return "string";
        if (type == "bool" || type == "_Bool")
            return "boolean";

        // Handle pointers and arrays (basic mapping)
        if (type.find("*") != std::string::npos)
        {
            if (type.find("char") != std::string::npos)
                return "string";
            // For other pointers, just remove the * for now
            type = std::regex_replace(type, std::regex(R"(\s*\*\s*)"), "");
            return c_type_to_cryo_type(type);
        }

        // Return as-is for unknown types (might be custom types)
        return type;
    }

    std::string RuntimeHeaderParser::extract_cryo_name(const std::string &c_name)
    {
        // Remove "cryo_" prefix if present
        if (c_name.find("cryo_") == 0)
        {
            return c_name.substr(5); // Remove "cryo_"
        }
        return c_name;
    }

    std::string RuntimeHeaderParser::build_signature(const std::string &return_type, const std::vector<std::string> &param_types)
    {
        std::stringstream ss;
        ss << "(";

        for (size_t i = 0; i < param_types.size(); ++i)
        {
            if (i > 0)
                ss << ", ";
            ss << param_types[i];
        }

        ss << ") -> " << return_type;
        return ss.str();
    }

    std::string RuntimeHeaderParser::clean_line(const std::string &line)
    {
        std::string cleaned = line;

        // Remove leading and trailing whitespace
        cleaned = std::regex_replace(cleaned, std::regex(R"(^\s+)"), "");
        cleaned = std::regex_replace(cleaned, std::regex(R"(\s+$)"), "");

        // Replace multiple spaces with single space
        cleaned = std::regex_replace(cleaned, std::regex(R"(\s+)"), " ");

        return cleaned;
    }

    std::string RuntimeHeaderParser::extract_comment(const std::string &line)
    {
        std::string comment;

        // Extract single-line comment
        size_t pos = line.find("//");
        if (pos != std::string::npos)
        {
            comment = line.substr(pos + 2);
            comment = clean_line(comment);
        }

        // TODO: Handle multi-line comments /* ... */

        return comment;
    }

    void RuntimeHeaderParser::clear()
    {
        functions_.clear();
        types_.clear();
    }

}
