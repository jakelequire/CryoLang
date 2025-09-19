#include <iostream>
#include "Compiler/CompilerInstance.hpp"

int main(int argc, char *argv[])
{
    std::cout << "Cryo Compiler v0.1.0" << std::endl;

    // Create compiler instance
    auto compiler = Cryo::create_compiler_instance();
    compiler->set_debug_mode(true); // Enable debug output

    // Handle command line arguments
    if (argc > 1)
    {
        // User provided a source file
        std::string source_file = argv[1];
        std::cout << "Compiling: " << source_file << std::endl;

        if (compiler->compile_file(source_file))
        {
            std::cout << "\n✓ Compilation successful!" << std::endl;

            // Show the resulting AST with pretty printing
            std::cout << "\nGenerated AST:" << std::endl;
            compiler->dump_ast();
        }
        else
        {
            std::cerr << "\n❌ Compilation failed!" << std::endl;
            compiler->print_diagnostics();
            return 1;
        }
    }
    else
    {
        // No file provided, use the test file
        std::string test_file = "./test/test.cryo";
        std::cout << "No file provided. Testing with: " << test_file << std::endl;

        if (compiler->compile_file(test_file))
        {
            std::cout << "\n✓ Test compilation successful!" << std::endl;
            compiler->dump_ast();
        }
        else
        {
            std::cerr << "\n❌ Test compilation failed!" << std::endl;
            compiler->print_diagnostics();

            // Fallback: show how to use the compiler
            std::cout << "\nUsage: " << argv[0] << " <source_file.cryo>" << std::endl;
            std::cout << "Example: " << argv[0] << " examples/hello.cryo" << std::endl;
            return 1;
        }
    }

    return 0;
}

// Keep the old test function for debugging if needed
int test_lexer_standalone()
{
    Cryo::File file("./test/test.cryo", "test", ".cryo");
    if (!file.load())
    {
        std::cerr << "Failed to load file." << std::endl;
        return 1;
    }
    else
    {
        std::cout << "File loaded successfully." << std::endl;
        std::cout << file.content() << std::endl;
    }
    file.close();

    auto lexer = Cryo::make_lexer(std::make_unique<Cryo::File>(std::move(file)));
    if (!lexer)
    {
        std::cerr << "Failed to create lexer." << std::endl;
        return 1;
    }

    while (lexer->has_more_tokens())
    {
        Cryo::Token token = lexer->next_token();
        std::cout << "Token: `" << token.to_string() << "`"
                  << " (Kind: " << Cryo::get_token_name(token.kind()) << ")"
                  << " at Line: " << token.location().line()
                  << ", Column: " << token.location().column()
                  << std::endl;
        if (token.is_eof())
        {
            break;
        }
    }

    return 0;
}
