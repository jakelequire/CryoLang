#include <iostream>

#include "Lexer/lexer.hpp"
#include "Utils/file.hpp"


int test_lexer() {
    Cryo::File file("./test/test.cryo", "test", ".cryo");
    if (!file.load()) {
        std::cerr << "Failed to load file." << std::endl;
        return 1;
    } else {
        std::cout << "File loaded successfully." << std::endl;
        std::cout << file.content() << std::endl;
    }
    file.close();

    auto lexer = Cryo::make_lexer(std::make_unique<Cryo::File>(std::move(file)));
    if (!lexer) {
        std::cerr << "Failed to create lexer." << std::endl;
        return 1;
    }

    while (lexer->has_more_tokens()) {
        Cryo::Token token = lexer->next_token();
        std::cout << "Token: `" << token.to_string() << "`"
                  << " (Kind: " << Cryo::get_token_name(token.kind()) << ")"
                  << " at Line: " << token.location().line()
                  << ", Column: " << token.location().column()
                  << std::endl;
        if (token.is_eof()) {
            break;
        }
    }

    return 0;
}

int main(int argc, char* argv[]) {
    std::cout << "Hello, World!" << std::endl;

    test_lexer();

    return 0;
}
