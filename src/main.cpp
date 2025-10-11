#include <iostream>
#include <filesystem>
#include "Compiler/CompilerInstance.hpp"
#include "CLI/CLI.hpp"
#include "Utils/Logger.hpp"

int main(int argc, char *argv[])
{
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
