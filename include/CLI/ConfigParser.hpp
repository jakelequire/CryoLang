#pragma once

#include <string>
#include <vector>
#include <unordered_map>

namespace Cryo::CLI
{

    /**
     * @brief Configuration structure for Cryo projects
     */
    struct CryoConfig
    {
        // [project] section
        std::string project_name;
        std::string output_dir = "build";
        std::string target_type = "executable";

        // [compiler] section
        bool debug = false;
        bool optimize = true;
        std::vector<std::string> args;

        // [dependencies] section (future feature)
        std::unordered_map<std::string, std::string> dependencies;
    };

    /**
     * @brief Parser for Cryo configuration files (cryoconfig)
     */
    class ConfigParser
    {
    public:
        /**
         * @brief Parse a cryoconfig file
         * @param config_path Path to the configuration file
         * @param config Output configuration structure
         * @return true if parsing was successful, false otherwise
         */
        static bool parse_config(const std::string &config_path, CryoConfig &config);

        /**
         * @brief Create a new cryoconfig file with default values
         * @param project_name Name of the project
         * @param config_path Path where to create the config file (default: "cryoconfig")
         * @return true if creation was successful, false otherwise
         */
        static bool create_config(const std::string &project_name, const std::string &config_path = "cryoconfig");

    private:
        /**
         * @brief Parse a boolean value from string
         * @param value String value to parse
         * @return Parsed boolean value
         */
        static bool parse_bool(const std::string &value);

        /**
         * @brief Parse an array value from string (e.g., ["item1", "item2"])
         * @param value String value to parse
         * @return Vector of parsed string values
         */
        static std::vector<std::string> parse_array(const std::string &value);

        /**
         * @brief Trim whitespace and quotes from a string
         * @param str String to trim
         * @return Trimmed string
         */
        static std::string trim_string(const std::string &str);

        /**
         * @brief Trim whitespace from a string
         * @param str String to trim
         * @return Trimmed string
         */
        static std::string trim_whitespace(const std::string &str);
    };

} // namespace Cryo::CLI