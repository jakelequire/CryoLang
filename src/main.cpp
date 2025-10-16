#include <iostream>
#include <filesystem>
#include "Compiler/CompilerInstance.hpp"
#include "CLI/CLI.hpp"
#include "Utils/Logger.hpp"
#include "Utils/ModuleLoader.hpp"
#include "Utils/OS.hpp"

int main(int argc, char *argv[])
{
    if (!Cryo::Utils::OS::initialize(argc > 0 ? argv[0] : ""))
    {
        std::cerr << "Fatal error: Failed to initialize OS Utility Module" << std::endl;
        return 1;
    }

    // Initialize logger with default configuration (will be overridden by CLI flags)
    Cryo::Logger::instance().initialize(Cryo::Logger::create_default_config());

    auto cli = Cryo::CLI::create_cli();
    return cli->run(argc, argv);
}
