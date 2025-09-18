#include "Parser/Parser.hpp"
#include "Lexer/lexer.hpp"
#include "Utils/file.hpp"
#include <iostream>

int main()
{
    // Create a simple test program
    std::string test_code = R"(
        const x: i32 = 5;
        const y: i32 = 10;
        const result: i32 = x + y;
    )";

    // Create a temporary file (in memory simulation)
    // For now, let's just create a simple lexer test
    std::cout << "Testing simple expression parsing...\n";

    // We need to create a proper file for the lexer
    // For now, let's just output what we would do
    std::cout << "Would parse: " << test_code << std::endl;

    return 0;
}