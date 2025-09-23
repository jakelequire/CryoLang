#include <iostream>
#include <filesystem>
#include "Compiler/CompilerInstance.hpp"
#include "CLI/CLI.hpp"

int main(int argc, char *argv[])
{
    auto cli = Cryo::CLI::create_cli();
    return cli->run(argc, argv);
}
