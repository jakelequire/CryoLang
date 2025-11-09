#include <gtest/gtest.h>
#include "test_utils.hpp"
#include "Lexer/lexer.hpp"

namespace CryoTest {

class LexerTest : public LexerTestFixture {
};

// ============================================================================
// Basic Token Recognition Tests
// ============================================================================

TEST_F(LexerTest, TokenizeKeywords) {
    expect_token_sequence("function", {Cryo::TokenKind::TK_KW_FUNCTION});
    expect_token_sequence("return", {Cryo::TokenKind::TK_KW_RETURN});
    expect_token_sequence("const", {Cryo::TokenKind::TK_KW_CONST});
    expect_token_sequence("mut", {Cryo::TokenKind::TK_KW_MUT});
    expect_token_sequence("if", {Cryo::TokenKind::TK_KW_IF});
    expect_token_sequence("else", {Cryo::TokenKind::TK_KW_ELSE});
    expect_token_sequence("while", {Cryo::TokenKind::TK_KW_WHILE});
    expect_token_sequence("for", {Cryo::TokenKind::TK_KW_FOR});
    expect_token_sequence("struct", {Cryo::TokenKind::TK_KW_STRUCT});
    expect_token_sequence("class", {Cryo::TokenKind::TK_KW_CLASS});
    expect_token_sequence("enum", {Cryo::TokenKind::TK_KW_ENUM});
    expect_token_sequence("impl", {Cryo::TokenKind::TK_KW_IMPL});
    expect_token_sequence("type", {Cryo::TokenKind::TK_KW_TYPE});
    expect_token_sequence("new", {Cryo::TokenKind::TK_KW_NEW});
    expect_token_sequence("import", {Cryo::TokenKind::TK_KW_IMPORT});
    expect_token_sequence("namespace", {Cryo::TokenKind::TK_KW_NAMESPACE});
    expect_token_sequence("public", {Cryo::TokenKind::TK_KW_PUBLIC});
    expect_token_sequence("private", {Cryo::TokenKind::TK_KW_PRIVATE});
    expect_token_sequence("true", {Cryo::TokenKind::TK_KW_TRUE});
    expect_token_sequence("false", {Cryo::TokenKind::TK_KW_FALSE});
}

TEST_F(LexerTest, TokenizeOperators) {
    expect_token_sequence("+", {Cryo::TokenKind::TK_PLUS});
    expect_token_sequence("-", {Cryo::TokenKind::TK_MINUS});
    expect_token_sequence("*", {Cryo::TokenKind::TK_MULTIPLY});
    expect_token_sequence("/", {Cryo::TokenKind::TK_DIVIDE});
    expect_token_sequence("=", {Cryo::TokenKind::TK_ASSIGN});
    expect_token_sequence("==", {Cryo::TokenKind::TK_EQ});
    expect_token_sequence("!=", {Cryo::TokenKind::TK_NE});
    expect_token_sequence("<", {Cryo::TokenKind::TK_LT});
    expect_token_sequence(">", {Cryo::TokenKind::TK_GT});
    expect_token_sequence("<=", {Cryo::TokenKind::TK_LE});
    expect_token_sequence(">=", {Cryo::TokenKind::TK_GE});
    expect_token_sequence("&&", {Cryo::TokenKind::TK_AND});
    expect_token_sequence("||", {Cryo::TokenKind::TK_OR});
    expect_token_sequence("!", {Cryo::TokenKind::TK_NOT});
    expect_token_sequence("++", {Cryo::TokenKind::TK_INCREMENT});
    expect_token_sequence("--", {Cryo::TokenKind::TK_DECREMENT});
    expect_token_sequence("->", {Cryo::TokenKind::TK_ARROW});
    expect_token_sequence("::", {Cryo::TokenKind::TK_SCOPE});
}

TEST_F(LexerTest, TokenizePunctuation) {
    expect_token_sequence("(", {Cryo::TokenKind::TK_LPAREN});
    expect_token_sequence(")", {Cryo::TokenKind::TK_RPAREN});
    expect_token_sequence("{", {Cryo::TokenKind::TK_LBRACE});
    expect_token_sequence("}", {Cryo::TokenKind::TK_RBRACE});
    expect_token_sequence("[", {Cryo::TokenKind::TK_LBRACKET});
    expect_token_sequence("]", {Cryo::TokenKind::TK_RBRACKET});
    expect_token_sequence(";", {Cryo::TokenKind::TK_SEMICOLON});
    expect_token_sequence(":", {Cryo::TokenKind::TK_COLON});
    expect_token_sequence(",", {Cryo::TokenKind::TK_COMMA});
    expect_token_sequence(".", {Cryo::TokenKind::TK_DOT});
}

// ============================================================================
// Identifier and Literal Tests
// ============================================================================

TEST_F(LexerTest, TokenizeIdentifiers) {
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

TEST_F(LexerTest, TokenizeIntegerLiterals) {
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

TEST_F(LexerTest, TokenizeFloatLiterals) {
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

TEST_F(LexerTest, TokenizeStringLiterals) {
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

TEST_F(LexerTest, TokenizeCharacterLiterals) {
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

TEST_F(LexerTest, TokenizeArithmeticExpression) {
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

TEST_F(LexerTest, TokenizeFunctionDeclaration) {
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

TEST_F(LexerTest, IgnoreLineComments) {
    auto lexer = create_lexer("x // This is a comment\ny");
    
    auto token1 = lexer->next_token();
    EXPECT_EQ(token1.kind(), Cryo::TokenKind::TK_IDENTIFIER);
    EXPECT_EQ(token1.lexeme(), "x");
    
    auto token2 = lexer->next_token();
    EXPECT_EQ(token2.kind(), Cryo::TokenKind::TK_IDENTIFIER);
    EXPECT_EQ(token2.lexeme(), "y");
}

TEST_F(LexerTest, IgnoreBlockComments) {
    auto lexer = create_lexer("x /* This is a\n   multi-line comment */ y");
    
    auto token1 = lexer->next_token();
    EXPECT_EQ(token1.kind(), Cryo::TokenKind::TK_IDENTIFIER);
    EXPECT_EQ(token1.lexeme(), "x");
    
    auto token2 = lexer->next_token();
    EXPECT_EQ(token2.kind(), Cryo::TokenKind::TK_IDENTIFIER);
    EXPECT_EQ(token2.lexeme(), "y");
}

TEST_F(LexerTest, IgnoreWhitespace) {
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

TEST_F(LexerTest, HandleInvalidCharacters) {
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

TEST_F(LexerTest, HandleUnterminatedString) {
    auto lexer = create_lexer(R"("unterminated string)");
    
    auto token = lexer->next_token();
    EXPECT_EQ(token.kind(), Cryo::TokenKind::TK_ERROR);
}

// ============================================================================
// Source Location Tests
// ============================================================================

TEST_F(LexerTest, CorrectSourceLocations) {
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

TEST_F(LexerTest, LexLargeFile) {
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