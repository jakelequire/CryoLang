#include <iostream>
#include <filesystem>
#include "Compiler/CompilerInstance.hpp"
#include "CLI/CLI.hpp"
#include "Utils/Logger.hpp"
#include "Utils/ModuleLoader.hpp"

int main(int argc, char *argv[])
{
    // Set the global executable path for stdlib auto-detection
    if (argc > 0 && argv[0])
    {
        Cryo::ModuleLoader::set_global_executable_path(argv[0]);
    }

    // Initialize logger with NONE by default (will be configured by CLI flags)
    Cryo::LoggerConfig config;
    config.console_level = Cryo::LogLevel::NONE; // Disabled by default
    config.file_level = Cryo::LogLevel::NONE;    // No file logging by default
    config.log_file_path = "";                   // No log file
    config.enable_colors = true;
    config.enable_timestamps = true;
    config.enable_component_tags = true;

    Cryo::Logger::instance().initialize(config);

    auto cli = Cryo::CLI::create_cli();
    return cli->run(argc, argv);
}
