#include <iostream>
#include "Compiler/CompilerInstance.hpp"

int main(int argc, char *argv[])
{
    std::cout << "Cryo Compiler" << std::endl;

    // Create compiler instance
    auto compiler = Cryo::create_compiler_instance();
    compiler->set_debug_mode(true);

    // Option 1: Compile a file directly
    if (argc > 1)
    {
        std::string source_file = argv[1];
        std::cout << "Compiling file: " << source_file << std::endl;

        if (compiler->compile_file(source_file))
        {
            std::cout << "Compilation successful!" << std::endl;
            compiler->print_ast();
        }
        else
        {
            std::cerr << "Compilation failed!" << std::endl;
            compiler->print_diagnostics();
            return 1;
        }
    }
    // Option 2: Test with the default test file
    else
    {
        std::cout << "Testing with default file: ./test/test.cryo" << std::endl;

        if (compiler->compile_file("./test/test.cryo"))
        {
            std::cout << "Test compilation successful!" << std::endl;
            compiler->print_ast();
        }
        else
        {
            std::cerr << "Test compilation failed!" << std::endl;
            compiler->print_diagnostics();
            return 1;
        }
    }

    return 0;
}

// Keep the old test_lexer function for reference/debugging
int test_lexer_standalone()
{
    // Your original lexer test code...
    // This could be moved to a separate test file
    return 0;
}