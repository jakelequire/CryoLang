#include <iostream>
#include <filesystem>
#include "Compiler/CompilerInstance.hpp"
#include "CLI/CLI.hpp"

int main(int argc, char *argv[])
{
    // Create and run the CLI system
    auto cli = Cryo::CLI::create_default_cli();
    return cli->run(argc, argv);
}
