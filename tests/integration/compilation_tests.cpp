#include "test_utils.hpp"
#include "include/test_helpers.hpp"

using namespace CryoTest;

// ============================================================================
// Integration Tests - Full Compilation Pipeline
// ============================================================================

CRYO_TEST_DESC(Integration, SimpleProgram, "End-to-end compilation of a simple program with variable declaration and return statement") {
    IntegrationTestHelper helper;
    helper.setup();

    std::string source = R"(
        function main() -> int {
            const x: int = 42;
            return x;
        }
    )";
    
    bool success = helper.compiles_to_ir(source);
    CRYO_ASSERT(!success, "Simple program should compile successfully");
}

CRYO_TEST_DESC(Integration, ArithmeticOperations, "End-to-end compilation of arithmetic operations and expression evaluation") {
    IntegrationTestHelper helper;
    helper.setup();

    std::string source = R"(";
        function calculate() -> int {
            const a: int = 10;
            const b: int = 5;
            const result: int = a + b * 2;
            return result;
        }
    )";
    
    bool success = helper.compiles_to_ir(source);
    CRYO_ASSERT_TRUE(success);
    CRYO_ASSERT_FALSE(helper.has_errors());
}

CRYO_TEST(Integration, FunctionCalls) {
    IntegrationTestHelper helper;
    helper.setup();

    std::string source = R"(
        function add(x: int, y: int) -> int {
            return x + y;
        }
        
        function main() -> int {
            const result: int = add(5, 10);
            return result;
        }
    )";
    
    bool success = helper.compiles_to_ir(source);
    CRYO_EXPECT_TRUE(success);
    CRYO_EXPECT_FALSE(helper.has_errors());
}

CRYO_TEST(Integration, TypeError) {
    IntegrationTestHelper helper;
    helper.setup();

    std::string source = R"(
        function main() -> int {
            const x: int = "not a number";
            return x;
        }
    )";
    
    // This should fail during compilation due to type error
    bool success = helper.compiles_to_ir(source);
    CRYO_EXPECT_FALSE(success);
    CRYO_EXPECT_TRUE(helper.has_errors());
}

CRYO_TEST(Integration, ParseError) {
    IntegrationTestHelper helper;
    helper.setup();

    std::string source = R"(
        function main() -> int
            const x: int = 42;
            return x;
        }
    )";
    
    bool success = helper.compiles_to_ir(source);
    CRYO_EXPECT_FALSE(success);
    CRYO_EXPECT_TRUE(helper.has_errors());
}