#include "test_utils.hpp"
#include "Lexer/lexer.hpp"
#include <sstream>
#include <memory>

namespace CryoTest {

// Helper class for lexer testing
class LexerTestHelper : public CryoTestBase {
public:
    std::unique_ptr<Cryo::Lexer> create_lexer(const std::string& source) {
        return std::make_unique<Cryo::Lexer>(source);
    }
    
    void expect_token_kind(const std::string& source, Cryo::TokenKind expected_kind) {
        auto lexer = create_lexer(source);
        auto token = lexer->next_token();
        CRYO_ASSERT_EQ(static_cast<int>(expected_kind), static_cast<int>(token.kind()));
    }
    
    void expect_token_sequence(const std::string& source, const std::vector<Cryo::TokenKind>& expected_tokens) {
        auto lexer = create_lexer(source);
        for (size_t i = 0; i < expected_tokens.size(); ++i) {
            auto token = lexer->next_token();
            CRYO_ASSERT_EQ(static_cast<int>(expected_tokens[i]), static_cast<int>(token.kind()));
        }
    }
};

// ============================================================================
// Basic Token Recognition Tests
// ============================================================================

CRYO_TEST(LexerTests, TokenizeKeywords) {
    LexerTestHelper helper;
    helper.expect_token_kind("function", Cryo::TokenKind::TK_KW_FUNCTION);
    helper.expect_token_kind("return", Cryo::TokenKind::TK_KW_RETURN);
    helper.expect_token_kind("const", Cryo::TokenKind::TK_KW_CONST);
    helper.expect_token_kind("mut", Cryo::TokenKind::TK_KW_MUT);
    helper.expect_token_kind("if", Cryo::TokenKind::TK_KW_IF);
    helper.expect_token_kind("else", Cryo::TokenKind::TK_KW_ELSE);
    helper.expect_token_kind("while", Cryo::TokenKind::TK_KW_WHILE);
    helper.expect_token_kind("for", Cryo::TokenKind::TK_KW_FOR);
    helper.expect_token_kind("struct", Cryo::TokenKind::TK_KW_STRUCT);
    helper.expect_token_kind("class", Cryo::TokenKind::TK_KW_CLASS);
    helper.expect_token_kind("enum", Cryo::TokenKind::TK_KW_ENUM);
    helper.expect_token_kind("impl", Cryo::TokenKind::TK_KW_IMPL);
    helper.expect_token_kind("type", Cryo::TokenKind::TK_KW_TYPE);
    helper.expect_token_kind("new", Cryo::TokenKind::TK_KW_NEW);
    helper.expect_token_kind("import", Cryo::TokenKind::TK_KW_IMPORT);
    helper.expect_token_kind("namespace", Cryo::TokenKind::TK_KW_NAMESPACE);
    helper.expect_token_kind("public", Cryo::TokenKind::TK_KW_PUBLIC);
    helper.expect_token_kind("private", Cryo::TokenKind::TK_KW_PRIVATE);
    helper.expect_token_kind("true", Cryo::TokenKind::TK_KW_TRUE);
    helper.expect_token_kind("false", Cryo::TokenKind::TK_KW_FALSE);
}

CRYO_TEST(LexerTests, TokenizeOperators) {
    LexerTestHelper helper;
    helper.expect_token_kind("+", Cryo::TokenKind::TK_PLUS);
    helper.expect_token_kind("-", Cryo::TokenKind::TK_MINUS);
    helper.expect_token_kind("*", Cryo::TokenKind::TK_MULTIPLY);
    helper.expect_token_kind("/", Cryo::TokenKind::TK_DIVIDE);
    helper.expect_token_kind("=", Cryo::TokenKind::TK_ASSIGN);
    helper.expect_token_kind("==", Cryo::TokenKind::TK_EQ);
    helper.expect_token_kind("!=", Cryo::TokenKind::TK_NE);
    helper.expect_token_kind("<", Cryo::TokenKind::TK_LT);
    helper.expect_token_kind(">", Cryo::TokenKind::TK_GT);
    helper.expect_token_kind("<=", Cryo::TokenKind::TK_LE);
    helper.expect_token_kind(">=", Cryo::TokenKind::TK_GE);
    helper.expect_token_kind("&&", Cryo::TokenKind::TK_AND);
    helper.expect_token_kind("||", Cryo::TokenKind::TK_OR);
    helper.expect_token_kind("!", Cryo::TokenKind::TK_NOT);
    helper.expect_token_kind("++", Cryo::TokenKind::TK_INCREMENT);
    helper.expect_token_kind("--", Cryo::TokenKind::TK_DECREMENT);
    helper.expect_token_kind("->", Cryo::TokenKind::TK_ARROW);
    helper.expect_token_kind("::", Cryo::TokenKind::TK_SCOPE);
}

CRYO_TEST(LexerTests, TokenizePunctuation) {
    LexerTestHelper helper;
    helper.expect_token_kind("(", Cryo::TokenKind::TK_LPAREN);
    helper.expect_token_kind(")", Cryo::TokenKind::TK_RPAREN);
    helper.expect_token_kind("{", Cryo::TokenKind::TK_LBRACE);
    helper.expect_token_kind("}", Cryo::TokenKind::TK_RBRACE);
    helper.expect_token_kind("[", Cryo::TokenKind::TK_LBRACKET);
    helper.expect_token_kind("]", Cryo::TokenKind::TK_RBRACKET);
    helper.expect_token_kind(";", Cryo::TokenKind::TK_SEMICOLON);
    helper.expect_token_kind(":", Cryo::TokenKind::TK_COLON);
    helper.expect_token_kind(",", Cryo::TokenKind::TK_COMMA);
    helper.expect_token_kind(".", Cryo::TokenKind::TK_DOT);
}

// ============================================================================
// Identifier and Literal Tests
// ============================================================================

CRYO_TEST(LexerTest, TokenizeIdentifiers) {
    auto lexer = create_lexer("variable_name camelCase PascalCase _underscore");
    
    auto token1 = lexer->next_token();
    EXPECT_EQ(token1.kind(), Cryo::TokenKind::TK_IDENTIFIER);
    EXPECT_EQ(token1.lexeme(), "variable_name");
    
    auto token2 = lexer->next_token();
    EXPECT_EQ(token2.kind(), Cryo::TokenKind::TK_IDENTIFIER);
    EXPECT_EQ(token2.lexeme(), "camelCase");
    
    auto token3 = lexer->next_token();
    EXPECT_EQ(token3.kind(), Cryo::TokenKind::TK_IDENTIFIER);
    EXPECT_EQ(token3.lexeme(), "PascalCase");
    
    auto token4 = lexer->next_token();
    EXPECT_EQ(token4.kind(), Cryo::TokenKind::TK_IDENTIFIER);
    EXPECT_EQ(token4.lexeme(), "_underscore");
}

CRYO_TEST(LexerTest, TokenizeIntegerLiterals) {
    auto lexer = create_lexer("42 0 123456 -789");
    
    auto token1 = lexer->next_token();
    EXPECT_EQ(token1.kind(), Cryo::TokenKind::TK_INTEGER_LITERAL);
    EXPECT_EQ(token1.lexeme(), "42");
    
    auto token2 = lexer->next_token();
    EXPECT_EQ(token2.kind(), Cryo::TokenKind::TK_INTEGER_LITERAL);
    EXPECT_EQ(token2.lexeme(), "0");
    
    auto token3 = lexer->next_token();
    EXPECT_EQ(token3.kind(), Cryo::TokenKind::TK_INTEGER_LITERAL);
    EXPECT_EQ(token3.lexeme(), "123456");
    
    auto token4 = lexer->next_token();
    EXPECT_EQ(token4.kind(), Cryo::TokenKind::TK_MINUS);
    
    auto token5 = lexer->next_token();
    EXPECT_EQ(token5.kind(), Cryo::TokenKind::TK_INTEGER_LITERAL);
    EXPECT_EQ(token5.lexeme(), "789");
}

CRYO_TEST(LexerTest, TokenizeFloatLiterals) {
    auto lexer = create_lexer("3.14 0.5 123.456 .5 5.");
    
    auto token1 = lexer->next_token();
    EXPECT_EQ(token1.kind(), Cryo::TokenKind::TK_FLOAT_LITERAL);
    EXPECT_EQ(token1.lexeme(), "3.14");
    
    auto token2 = lexer->next_token();
    EXPECT_EQ(token2.kind(), Cryo::TokenKind::TK_FLOAT_LITERAL);
    EXPECT_EQ(token2.lexeme(), "0.5");
    
    auto token3 = lexer->next_token();
    EXPECT_EQ(token3.kind(), Cryo::TokenKind::TK_FLOAT_LITERAL);
    EXPECT_EQ(token3.lexeme(), "123.456");
}

CRYO_TEST(LexerTest, TokenizeStringLiterals) {
    auto lexer = create_lexer(R"("hello world" "escape \"quote\"" "")");
    
    auto token1 = lexer->next_token();
    EXPECT_EQ(token1.kind(), Cryo::TokenKind::TK_STRING_LITERAL);
    EXPECT_EQ(token1.lexeme(), "\"hello world\"");
    
    auto token2 = lexer->next_token();
    EXPECT_EQ(token2.kind(), Cryo::TokenKind::TK_STRING_LITERAL);
    EXPECT_EQ(token2.lexeme(), "\"escape \\\"quote\\\"\"");
    
    auto token3 = lexer->next_token();
    EXPECT_EQ(token3.kind(), Cryo::TokenKind::TK_STRING_LITERAL);
    EXPECT_EQ(token3.lexeme(), "\"\"");
}

CRYO_TEST(LexerTest, TokenizeCharacterLiterals) {
    auto lexer = create_lexer(R"('a' 'Z' '1' '\n' '\t' '\'')");
    
    auto token1 = lexer->next_token();
    EXPECT_EQ(token1.kind(), Cryo::TokenKind::TK_CHAR_LITERAL);
    EXPECT_EQ(token1.lexeme(), "'a'");
    
    auto token2 = lexer->next_token();
    EXPECT_EQ(token2.kind(), Cryo::TokenKind::TK_CHAR_LITERAL);
    EXPECT_EQ(token2.lexeme(), "'Z'");
    
    auto token3 = lexer->next_token();
    EXPECT_EQ(token3.kind(), Cryo::TokenKind::TK_CHAR_LITERAL);
    EXPECT_EQ(token3.lexeme(), "'1'");
}

// ============================================================================
// Complex Expression Tests
// ============================================================================

CRYO_TEST(LexerTest, TokenizeArithmeticExpression) {
    std::string source = "x + y * 2 - (a / b)";
    std::vector<Cryo::TokenKind> expected = {
        Cryo::TokenKind::TK_IDENTIFIER,    // x
        Cryo::TokenKind::TK_PLUS,          // +
        Cryo::TokenKind::TK_IDENTIFIER,    // y
        Cryo::TokenKind::TK_MULTIPLY,      // *
        Cryo::TokenKind::TK_INTEGER_LITERAL, // 2
        Cryo::TokenKind::TK_MINUS,         // -
        Cryo::TokenKind::TK_LPAREN,        // (
        Cryo::TokenKind::TK_IDENTIFIER,    // a
        Cryo::TokenKind::TK_DIVIDE,        // /
        Cryo::TokenKind::TK_IDENTIFIER,    // b
        Cryo::TokenKind::TK_RPAREN         // )
    };
    
    expect_token_sequence(source, expected);
}

CRYO_TEST(LexerTest, TokenizeFunctionDeclaration) {
    std::string source = "function add(x: int, y: int) -> int { return x + y; }";
    std::vector<Cryo::TokenKind> expected = {
        Cryo::TokenKind::TK_KW_FUNCTION,
        Cryo::TokenKind::TK_IDENTIFIER,
        Cryo::TokenKind::TK_LPAREN,
        Cryo::TokenKind::TK_IDENTIFIER,
        Cryo::TokenKind::TK_COLON,
        Cryo::TokenKind::TK_IDENTIFIER,
        Cryo::TokenKind::TK_COMMA,
        Cryo::TokenKind::TK_IDENTIFIER,
        Cryo::TokenKind::TK_COLON,
        Cryo::TokenKind::TK_IDENTIFIER,
        Cryo::TokenKind::TK_RPAREN,
        Cryo::TokenKind::TK_ARROW,
        Cryo::TokenKind::TK_IDENTIFIER,
        Cryo::TokenKind::TK_LBRACE,
        Cryo::TokenKind::TK_KW_RETURN,
        Cryo::TokenKind::TK_IDENTIFIER,
        Cryo::TokenKind::TK_PLUS,
        Cryo::TokenKind::TK_IDENTIFIER,
        Cryo::TokenKind::TK_SEMICOLON,
        Cryo::TokenKind::TK_RBRACE
    };
    
    expect_token_sequence(source, expected);
}

// ============================================================================
// Comment and Whitespace Tests
// ============================================================================

CRYO_TEST(LexerTest, IgnoreLineComments) {
    auto lexer = create_lexer("x // This is a comment\ny");
    
    auto token1 = lexer->next_token();
    EXPECT_EQ(token1.kind(), Cryo::TokenKind::TK_IDENTIFIER);
    EXPECT_EQ(token1.lexeme(), "x");
    
    auto token2 = lexer->next_token();
    EXPECT_EQ(token2.kind(), Cryo::TokenKind::TK_IDENTIFIER);
    EXPECT_EQ(token2.lexeme(), "y");
}

CRYO_TEST(LexerTest, IgnoreBlockComments) {
    auto lexer = create_lexer("x /* This is a\n   multi-line comment */ y");
    
    auto token1 = lexer->next_token();
    EXPECT_EQ(token1.kind(), Cryo::TokenKind::TK_IDENTIFIER);
    EXPECT_EQ(token1.lexeme(), "x");
    
    auto token2 = lexer->next_token();
    EXPECT_EQ(token2.kind(), Cryo::TokenKind::TK_IDENTIFIER);
    EXPECT_EQ(token2.lexeme(), "y");
}

CRYO_TEST(LexerTest, IgnoreWhitespace) {
    auto lexer = create_lexer("  \t\n  x   \n\t  y  ");
    
    auto token1 = lexer->next_token();
    EXPECT_EQ(token1.kind(), Cryo::TokenKind::TK_IDENTIFIER);
    EXPECT_EQ(token1.lexeme(), "x");
    
    auto token2 = lexer->next_token();
    EXPECT_EQ(token2.kind(), Cryo::TokenKind::TK_IDENTIFIER);
    EXPECT_EQ(token2.lexeme(), "y");
}

// ============================================================================
// Error Handling Tests
// ============================================================================

CRYO_TEST(LexerTest, HandleInvalidCharacters) {
    auto lexer = create_lexer("x @ y");
    
    auto token1 = lexer->next_token();
    EXPECT_EQ(token1.kind(), Cryo::TokenKind::TK_IDENTIFIER);
    EXPECT_EQ(token1.lexeme(), "x");
    
    auto token2 = lexer->next_token();
    EXPECT_EQ(token2.kind(), Cryo::TokenKind::TK_ERROR);
    
    auto token3 = lexer->next_token();
    EXPECT_EQ(token3.kind(), Cryo::TokenKind::TK_IDENTIFIER);
    EXPECT_EQ(token3.lexeme(), "y");
}

CRYO_TEST(LexerTest, HandleUnterminatedString) {
    auto lexer = create_lexer(R"("unterminated string)");
    
    auto token = lexer->next_token();
    EXPECT_EQ(token.kind(), Cryo::TokenKind::TK_ERROR);
}

// ============================================================================
// Source Location Tests
// ============================================================================

CRYO_TEST(LexerTest, CorrectSourceLocations) {
    auto lexer = create_lexer("x\ny\n  z");
    
    auto token1 = lexer->next_token();
    EXPECT_EQ(token1.location().line, 1);
    EXPECT_EQ(token1.location().column, 1);
    
    auto token2 = lexer->next_token();
    EXPECT_EQ(token2.location().line, 2);
    EXPECT_EQ(token2.location().column, 1);
    
    auto token3 = lexer->next_token();
    EXPECT_EQ(token3.location().line, 3);
    EXPECT_EQ(token3.location().column, 3);
}

// ============================================================================
// Performance Tests
// ============================================================================

CRYO_TEST(LexerTest, LexLargeFile) {
    // Generate a large source file for performance testing
    std::string large_source;
    for (int i = 0; i < 1000; ++i) {
        large_source += "function test" + std::to_string(i) + 
                       "(x: int, y: int) -> int { return x + y; }\n";
    }
    
    auto lexer = create_lexer(large_source);
    
    PerformanceTimer timer;
    timer.start();
    
    int token_count = 0;
    while (lexer->has_more_tokens()) {
        auto token = lexer->next_token();
        if (token.is(Cryo::TokenKind::TK_EOF)) break;
        token_count++;
    }
    
    double elapsed = timer.elapsed_ms();
    
    EXPECT_GT(token_count, 10000) << "Should have tokenized many tokens";
    EXPECT_LT(elapsed, 1000.0) << "Lexing should complete within 1 second";
    
    std::cout << "Lexed " << token_count << " tokens in " << elapsed << "ms\n";
}

} // namespace CryoTest