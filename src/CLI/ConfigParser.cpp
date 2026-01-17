#include "CLI/ConfigParser.hpp"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <iostream>

namespace Cryo::CLI
{

    bool ConfigParser::parse_config(const std::string &config_path, CryoConfig &config)
    {
        try
        {
            std::ifstream config_file(config_path);
            if (!config_file.is_open())
            {
                return false;
            }

            std::string line;
            std::string current_section;

            while (std::getline(config_file, line))
            {
                // Skip empty lines and comments
                line = trim_whitespace(line);
                if (line.empty() || line[0] == '#')
                {
                    continue;
                }

                // Check for section headers [section_name]
                if (line[0] == '[' && line.back() == ']')
                {
                    current_section = line.substr(1, line.length() - 2);
                    continue;
                }

                // Parse key = value pairs
                size_t eq_pos = line.find('=');
                if (eq_pos == std::string::npos)
                {
                    continue; // Skip lines without '='
                }

                std::string key = trim_whitespace(line.substr(0, eq_pos));
                std::string value = trim_string(line.substr(eq_pos + 1));

                // Parse based on current section
                if (current_section == "project")
                {
                    if (key == "project_name")
                    {
                        config.project_name = value;
                    }
                    else if (key == "output_dir")
                    {
                        config.output_dir = value;
                    }
                    else if (key == "target_type")
                    {
                        config.target_type = value;
                    }
                    else if (key == "entry_point")
                    {
                        config.entry_point = value;
                    }
                    else if (key == "source_dir")
                    {
                        config.source_dir = value;
                    }
                }
                else if (current_section == "compiler")
                {
                    if (key == "debug")
                    {
                        config.debug = parse_bool(value);
                    }
                    else if (key == "optimize")
                    {
                        config.optimize = parse_bool(value);
                    }
                    else if (key == "stdlib_mode")
                    {
                        config.stdlib_mode = parse_bool(value);
                    }
                    else if (key == "no_std")
                    {
                        config.no_std = parse_bool(value);
                    }
                    else if (key == "dump_symbols")
                    {
                        config.dump_symbols = parse_bool(value);
                    }
                    else if (key == "args")
                    {
                        config.args = parse_array(value);
                    }
                }
                else if (current_section == "dependencies")
                {
                    // Future feature: parse dependencies
                    config.dependencies[key] = value;
                }
            }

            config_file.close();
            return true;
        }
        catch (const std::exception &e)
        {
            std::cerr << "Error parsing config file: " << e.what() << std::endl;
            return false;
        }
        catch (...)
        {
            std::cerr << "Unknown error parsing config file" << std::endl;
            return false;
        }
    }

    bool ConfigParser::create_config(const std::string &project_name, const std::string &config_path)
    {
        try
        {
            std::ofstream config_file(config_path);
            if (!config_file.is_open())
            {
                return false;
            }

            config_file << "# Cryo Project Configuration\n";
            config_file << "# This file defines the build configuration for your Cryo project\n\n";

            config_file << "[project]\n";
            config_file << "project_name = \"" << project_name << "\"\n";
            config_file << "output_dir = \"build\"\n";
            config_file << "target_type = \"executable\"\n\n";

            config_file << "# Compiler options\n";
            config_file << "[compiler]\n";
            config_file << "debug = false\n";
            config_file << "optimize = true\n";
            config_file << "args = []\n\n";

            config_file << "# Dependencies (future feature)\n";
            config_file << "[dependencies]\n";
            config_file << "# Add dependencies here\n";

            config_file.close();
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    bool ConfigParser::parse_bool(const std::string &value)
    {
        std::string lower_value = value;
        std::transform(lower_value.begin(), lower_value.end(), lower_value.begin(), ::tolower);
        return (lower_value == "true" || lower_value == "yes" || lower_value == "1");
    }

    std::vector<std::string> ConfigParser::parse_array(const std::string &value)
    {
        std::vector<std::string> result;

        // Check if value is in array format [item1, item2, ...]
        std::string trimmed = trim_whitespace(value);
        if (trimmed.empty())
        {
            return result;
        }

        // Handle empty array []
        if (trimmed == "[]")
        {
            return result;
        }

        // Remove brackets if present
        if (trimmed.front() == '[' && trimmed.back() == ']')
        {
            trimmed = trimmed.substr(1, trimmed.length() - 2);
            trimmed = trim_whitespace(trimmed);
        }

        // If still empty after removing brackets, return empty vector
        if (trimmed.empty())
        {
            return result;
        }

        // Split by comma
        std::stringstream ss(trimmed);
        std::string item;

        while (std::getline(ss, item, ','))
        {
            item = trim_string(item);
            if (!item.empty())
            {
                result.push_back(item);
            }
        }

        return result;
    }

    std::string ConfigParser::trim_string(const std::string &str)
    {
        std::string result = trim_whitespace(str);

        // Remove quotes if present
        if (result.length() >= 2)
        {
            if ((result.front() == '"' && result.back() == '"') ||
                (result.front() == '\'' && result.back() == '\''))
            {
                result = result.substr(1, result.length() - 2);
            }
        }

        return result;
    }

    std::string ConfigParser::trim_whitespace(const std::string &str)
    {
        size_t start = str.find_first_not_of(" \t\n\r");
        if (start == std::string::npos)
        {
            return "";
        }

        size_t end = str.find_last_not_of(" \t\n\r");
        return str.substr(start, end - start + 1);
    }

} // namespace Cryo::CLI