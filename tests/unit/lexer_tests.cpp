#include "test_utils.hpp"
#include "include/test_helpers.hpp"

using namespace CryoTest;

// ============================================================================
// Lexer Tests
// ============================================================================

CRYO_TEST(Lexer, BasicTokenization) {
    LexerTestHelper helper;
    helper.setup();

    std::string source = "const x = 42;";
    bool success = helper.tokenizes_successfully(source);
    
    CRYO_EXPECT_TRUE(success);
    CRYO_EXPECT_FALSE(helper.has_errors());
}

CRYO_TEST(Lexer, StringLiteralTokenization) {
    LexerTestHelper helper;
    helper.setup();

    std::string source = R"(const message = "Hello, World!";)";
    bool success = helper.tokenizes_successfully(source);
    
    CRYO_EXPECT_TRUE(success);
    CRYO_EXPECT_FALSE(helper.has_errors());
}

CRYO_TEST(Lexer, FunctionTokenization) {
    LexerTestHelper helper;
    helper.setup();

    std::string source = R"(
        function main() -> int {
            return 0;
        }
    )";
    
    bool success = helper.tokenizes_successfully(source);
    CRYO_EXPECT_TRUE(success);
    CRYO_EXPECT_FALSE(helper.has_errors());
}

CRYO_TEST(Lexer, TokenCount) {
    LexerTestHelper helper;
    helper.setup();

    std::string source = "const x = 42;";
    size_t token_count = helper.count_tokens(source);
    
    // Should have at least: const, x, =, 42, ;
    CRYO_EXPECT_TRUE(token_count >= 5);
}

CRYO_TEST(Lexer, EmptySourceHandling) {
    LexerTestHelper helper;
    helper.setup();

    std::string source = "";
    bool success = helper.tokenizes_successfully(source);
    
    CRYO_EXPECT_TRUE(success);
}

CRYO_TEST(Lexer, WhitespaceHandling) {
    LexerTestHelper helper;
    helper.setup();

    std::string source = "   const   x   =   42   ;   ";
    bool success = helper.tokenizes_successfully(source);
    
    CRYO_EXPECT_TRUE(success);
    CRYO_EXPECT_FALSE(helper.has_errors());
}