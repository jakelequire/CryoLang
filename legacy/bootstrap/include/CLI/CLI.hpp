#pragma once

#include <string>
#include <vector>
#include <map>
#include <functional>
#include <memory>
#include <iostream>

namespace Cryo::CLI
{
    // Forward declarations
    class Command;
    class ArgumentParser;
    class CLIRunner;

    // ================================================================
    // Core CLI Types
    // ================================================================

    /**
     * @brief Represents a command-line argument
     */
    struct CLIArgument
    {
        std::string name;
        std::string description;
        bool required = false;
        bool is_flag = false;
        std::string default_value = "";
        std::vector<std::string> aliases;

        CLIArgument(const std::string &name, const std::string &desc, bool req = false)
            : name(name), description(desc), required(req) {}

        CLIArgument &flag()
        {
            is_flag = true;
            return *this;
        }

        CLIArgument &default_val(const std::string &val)
        {
            default_value = val;
            return *this;
        }

        CLIArgument &alias(const std::string &alias_name)
        {
            aliases.push_back(alias_name);
            return *this;
        }
    };

    /**
     * @brief Contains parsed command-line arguments
     */
    class ParsedArgs
    {
    private:
        std::map<std::string, std::string> _args;
        std::map<std::string, bool> _flags;
        std::map<std::string, std::string> _defines; // For -D flags
        std::vector<std::string> _positional;

    public:
        void set_arg(const std::string &key, const std::string &value) { _args[key] = value; }
        void set_flag(const std::string &key, bool value = true) { _flags[key] = value; }
        void set_define(const std::string &key, const std::string &value) { _defines[key] = value; }
        void add_positional(const std::string &value) { _positional.push_back(value); }

        std::string get_arg(const std::string &key, const std::string &default_val = "") const
        {
            auto it = _args.find(key);
            return (it != _args.end()) ? it->second : default_val;
        }

        bool get_flag(const std::string &key, bool default_val = false) const
        {
            auto it = _flags.find(key);
            return (it != _flags.end()) ? it->second : default_val;
        }

        const std::vector<std::string> &positional() const { return _positional; }
        bool has_arg(const std::string &key) const { return _args.find(key) != _args.end(); }
        bool has_flag(const std::string &key) const { return _flags.find(key) != _flags.end(); }
        bool has_define(const std::string &key) const { return _defines.find(key) != _defines.end(); }
        std::string get_define(const std::string &key, const std::string &default_val = "") const
        {
            auto it = _defines.find(key);
            return (it != _defines.end()) ? it->second : default_val;
        }
        const std::map<std::string, std::string> &defines() const { return _defines; }
        size_t positional_count() const { return _positional.size(); }

        // Enhanced accessors for compilation flags
        bool show_ast() const { return get_flag("ast") || get_flag("show-ast"); }
        bool show_symbols() const { return get_flag("symbols") || get_flag("show-symbols"); }
        bool show_ir() const { return get_flag("ir") || get_flag("show-ir"); }
        bool compile_only() const { return get_flag("c") || get_flag("compile-only"); }
        bool raw_mode() const { return get_flag("raw"); }
        std::string output_file() const { return get_arg("o", get_arg("output", "")); }
        std::string input_file() const { return positional().empty() ? "" : positional()[0]; }
    };

    // ================================================================
    // Command Interface
    // ================================================================

    /**
     * @brief Base class for all CLI commands
     */
    class Command
    {
    protected:
        std::string _name;
        std::string _description;
        std::string _usage;
        std::vector<CLIArgument> _arguments;

    public:
        Command(const std::string &name, const std::string &description)
            : _name(name), _description(description) {}

        virtual ~Command() = default;

        // Command interface
        virtual int execute(const ParsedArgs &args) = 0;
        virtual void print_help() const;

        // Command registration
        Command &usage(const std::string &usage_text)
        {
            _usage = usage_text;
            return *this;
        }

        Command &argument(const CLIArgument &arg)
        {
            _arguments.push_back(arg);
            return *this;
        }

        // Getters
        const std::string &name() const { return _name; }
        const std::string &description() const { return _description; }
        const std::string &usage_text() const { return _usage; }
        const std::vector<CLIArgument> &arguments() const { return _arguments; }
    };

    // ================================================================
    // Argument Parser
    // ================================================================

    /**
     * @brief Parses command-line arguments according to command specifications
     */
    class ArgumentParser
    {
    public:
        static ParsedArgs parse(int argc, char *argv[], const Command *command = nullptr);
        static bool validate_args(const ParsedArgs &args, const Command *command);
        static void print_validation_error(const std::string &error);

    private:
        static bool is_flag(const std::string &arg);
        static std::string normalize_flag(const std::string &flag);
    };

    // ================================================================
    // CLI Runner
    // ================================================================

    /**
     * @brief Main CLI runner that manages commands and execution
     */
    class CLIRunner
    {
    private:
        std::map<std::string, std::unique_ptr<Command>> _commands;
        std::string _program_name;
        std::string _version;
        std::string _description;

    public:
        CLIRunner(const std::string &program_name, const std::string &version);
        ~CLIRunner() = default;

        // Command registration
        template <typename CommandType, typename... Args>
        CLIRunner &register_command(Args &&...args)
        {
            auto command = std::make_unique<CommandType>(std::forward<Args>(args)...);
            std::string name = command->name();
            _commands[name] = std::move(command);
            return *this;
        }

        // Execution
        int run(int argc, char *argv[]);

        // Help and info
        void print_help() const;
        void print_version() const;
        void set_description(const std::string &desc) { _description = desc; }

    private:
        Command *find_command(const std::string &name) const;
        int execute_file_compilation(const ParsedArgs &args);
        int execute_file_compilation(const std::string &file_path); // Legacy overload
        bool is_cryo_file(const std::string &path) const;
        bool has_compilation_flags(const ParsedArgs &args) const;
    };

    // ================================================================
    // Utility Functions
    // ================================================================

    /**
     * @brief Creates and returns a configured CLI runner with default commands
     */
    std::unique_ptr<CLIRunner> create_cli();

} // namespace Cryo::CLI
