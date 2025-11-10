#include "test_utils.hpp"
#include "include/test_helpers.hpp"
#include <cstdlib>

using namespace CryoTest;

// ============================================================================
// Type Checker Tests
// ============================================================================

CRYO_TEST(TypeChecker, BasicIntegerLiteral) {
    TypeCheckerTestHelper helper;
    helper.setup();

    std::string source = "const test_var_" + std::to_string(rand()) + ": int = 42;";
    bool success = helper.parse_and_type_check(source);
    
    CRYO_EXPECT_TRUE(success);
    CRYO_EXPECT_FALSE(helper.has_errors());
}

CRYO_TEST(TypeChecker, BasicStringLiteral) {
    try {
        TypeCheckerTestHelper helper;
        helper.setup();

        std::string source = R"(const test_str_)" + std::to_string(rand()) + R"(: string = "Hello, World!";)";
        
        // Debug: print the source we're testing
        std::cout << "\n[DEBUG] Testing source: " << source << std::endl;
        
        bool success = helper.parse_and_type_check(source);
        
        CRYO_EXPECT_TRUE(success);
        CRYO_EXPECT_FALSE(helper.has_errors());
    } catch (const std::exception& e) {
        std::cout << "\n[ERROR] Exception in BasicStringLiteral test: " << e.what() << std::endl;
        throw; // Re-throw to let the test framework handle it
    } catch (...) {
        std::cout << "\n[ERROR] Unknown exception in BasicStringLiteral test" << std::endl;
        throw;
    }
}

CRYO_TEST(TypeChecker, BasicBooleanLiteral) {
    TypeCheckerTestHelper helper;
    helper.setup();

    std::string source = "const test_bool_" + std::to_string(rand()) + ": boolean = true;";
    bool success = helper.parse_and_type_check(source);
    
    CRYO_EXPECT_TRUE(success);
    CRYO_EXPECT_FALSE(helper.has_errors());
}

CRYO_TEST(TypeChecker, TypeMismatchDetection) {
    TypeCheckerTestHelper helper;
    helper.setup();

    std::string source = R"(const unique_bad_var_jkl012: int = "not a number";)";
    
    // This should fail type checking
    bool success = helper.parse_and_type_check(source);
    CRYO_EXPECT_FALSE(success);
    CRYO_EXPECT_TRUE(helper.has_errors());
}

CRYO_TEST(TypeChecker, SimpleFunctionDeclaration) {
    TypeCheckerTestHelper helper;
    helper.setup();

    std::string source = R"(
        function unique_add_func_mno345(param1: int, param2: int) -> int {
            return param1 + param2;
        }
    )";
    
    bool success = helper.parse_and_type_check(source);
    CRYO_EXPECT_TRUE(success);
    CRYO_EXPECT_FALSE(helper.has_errors());
}

CRYO_TEST(TypeChecker, FunctionReturnTypeMismatch) {
    TypeCheckerTestHelper helper;
    helper.setup();

    std::string source = R"(
        function unique_bad_func_pqr678() -> string {
            return 42;
        }
    )";
    
    // This should fail type checking
    bool success = helper.parse_and_type_check(source);
    CRYO_EXPECT_FALSE(success);
    CRYO_EXPECT_TRUE(helper.has_errors());
}

CRYO_TEST(TypeChecker, ValidVariableDeclaration) {
    TypeCheckerTestHelper helper;
    helper.setup();

    std::string source = "var unique_var_decl_stu901: int;";
    
    // Should parse successfully
    bool parse_success = helper.parse_source(source);
    CRYO_EXPECT_TRUE(parse_success);
    CRYO_EXPECT_FALSE(helper.has_errors());
    
    // AST should be created
    CRYO_EXPECT_TRUE(helper.get_ast() != nullptr);
}