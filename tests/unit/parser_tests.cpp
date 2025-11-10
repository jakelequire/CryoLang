#include "test_utils.hpp"
#include "include/test_helpers.hpp"

using namespace CryoTest;

// ============================================================================
// Parser Tests
// ============================================================================

CRYO_TEST_DESC(Parser, BasicVariableDeclaration, "Tests parsing of simple constant variable declarations with type annotations")
{
    ParserTestHelper helper;
    helper.setup();

    std::string source = "const x: int = 42;";
    bool success = helper.parses_successfully(source);

    if (!success) {
        throw CryoTest::AssertionError(__FILE__, __LINE__, "helper.parses_successfully(source)", "true", "false",
                                     "", helper.get_source_context(source, 1, 2), helper.get_diagnostic_summary(), "parsing");
    }
    CRYO_EXPECT_FALSE(helper.has_errors());
    CRYO_EXPECT_TRUE(helper.get_ast() != nullptr);
}

CRYO_TEST_DESC(Parser, FunctionDeclaration, "Tests parsing of basic function declarations with return types and empty parameter lists")
{
    ParserTestHelper helper;
    helper.setup();

    std::string source = R"(
        function main() -> int {
            return 0;
        }
    )";

    bool success = helper.parses_successfully(source);
    if (!success) {
        throw CryoTest::AssertionError(__FILE__, __LINE__, "helper.parses_successfully(source)", "true", "false",
                                     "", helper.get_source_context(source, 2, 2), helper.get_diagnostic_summary(), "parsing");
    }
    CRYO_EXPECT_FALSE(helper.has_errors());
    CRYO_EXPECT_TRUE(helper.get_ast() != nullptr);
}

CRYO_TEST_DESC(Parser, FunctionWithParameters, "Tests parsing of functions with typed parameters and arithmetic expressions in return statements")
{
    ParserTestHelper helper;
    helper.setup();

    std::string source = R"(
        function add(x: int, y: int) -> int {
            return x + y;
        }
    )";

    bool success = helper.parses_successfully(source);
    if (!success) {
        throw CryoTest::AssertionError(__FILE__, __LINE__, "helper.parses_successfully(source)", "true", "false",
                                     "", helper.get_source_context(source, 2, 2), helper.get_diagnostic_summary(), "parsing");
    }
    CRYO_EXPECT_FALSE(helper.has_errors());
}

CRYO_TEST(Parser, NestedExpressions)
{
    ParserTestHelper helper;
    helper.setup();

    std::string source = R"(
        const result: int = (2 + 3) * (4 - 1);
    )";

    bool success = helper.parses_successfully(source);
    CRYO_EXPECT_TRUE(success);
    CRYO_EXPECT_FALSE(helper.has_errors());
}

CRYO_TEST(Parser, InvalidSyntaxHandling)
{
    ParserTestHelper helper;
    helper.setup();

    std::string source = "const x: int = ;"; // Missing value
    bool success = helper.parses_successfully(source);

    // This should fail to parse
    CRYO_EXPECT_FALSE(success);
}

CRYO_TEST(Parser, MissingSemicolon)
{
    ParserTestHelper helper;
    helper.setup();

    std::string source = "const x: int = 42"; // Missing semicolon
    bool success = helper.parses_successfully(source);

    // This might fail depending on language design
    // Adjust expectation based on your parser's behavior
    CRYO_EXPECT_FALSE(success);
}
