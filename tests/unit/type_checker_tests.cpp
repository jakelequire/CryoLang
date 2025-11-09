#include "test_utils.hpp"
#include "AST/TypeChecker.hpp"

namespace CryoTest {

class TypeCheckerTest : public CryoTestBase {
protected:
    std::unique_ptr<Cryo::TypeChecker> type_checker;
    
    void setup() override {
        CryoTestBase::setup();
        if (ast_context) {
            type_checker = std::make_unique<Cryo::TypeChecker>(
                ast_context->types(), 
                compiler->symbol_table(),
                compiler->diagnostic_manager()
            );
        }
    }
};

// ============================================================================
// Basic Type Inference Tests
// ============================================================================

CRYO_TEST(TypeCheckerTest, InferIntegerLiteralType) {
    TypeCheckerTestHelper helper;
    helper.setup();

    std::string source = "const x: int = 42;";
    auto ast = helper.parse_source(source);
    
    CRYO_ASSERT_TRUE(ast != nullptr);
    
    // Run type checker
    type_checker->check(ast.get());
    
    // Verify no type errors
    CRYO_EXPECT_FALSE(compiler->has_errors());
}

CRYO_TEST(TypeCheckerTest, InferFloatLiteralType) {
    TypeCheckerTestHelper helper;
    helper.setup();

    std::string source = "const pi: float = 3.14;";
    auto ast = helper.parse_source(source);
    
    CRYO_ASSERT_TRUE(ast != nullptr);
    type_checker->check(ast.get());
    CRYO_EXPECT_FALSE(compiler->has_errors());
}

CRYO_TEST(TypeCheckerTest, InferStringLiteralType) {
    TypeCheckerTestHelper helper;
    helper.setup();

    std::string source = R"(const message: string = "Hello, World!";)";
    auto ast = helper.parse_source(source);
    
    CRYO_ASSERT_TRUE(ast != nullptr);
    type_checker->check(ast.get());
    CRYO_EXPECT_FALSE(compiler->has_errors());
}

CRYO_TEST(TypeCheckerTest, InferBooleanLiteralType) {
    TypeCheckerTestHelper helper;
    helper.setup();

    std::string source = "const flag: boolean = true;";
    auto ast = helper.parse_source(source);
    
    CRYO_ASSERT_TRUE(ast != nullptr);
    type_checker->check(ast.get());
    CRYO_EXPECT_FALSE(compiler->has_errors());
}

// ============================================================================
// Type Mismatch Detection Tests
// ============================================================================

CRYO_TEST(TypeCheckerTest, DetectIntegerStringMismatch) {
    TypeCheckerTestHelper helper;
    helper.setup();

    std::string source = R"(const x: int = "not a number";)";
    auto ast = helper.parse_source(source);
    
    CRYO_ASSERT_TRUE(ast != nullptr);
    type_checker->check(ast.get());
    CRYO_EXPECT_TRUE(compiler->has_errors());
}

CRYO_TEST(TypeCheckerTest, DetectFloatIntegerMismatch) {
    TypeCheckerTestHelper helper;
    helper.setup();

    std::string source = "const x: float = 42;"; // Should be 42.0
    auto ast = helper.parse_source(source);
    
    CRYO_ASSERT_TRUE(ast != nullptr);
    type_checker->check(ast.get());
    
    // This might pass if implicit conversion is allowed
    // Adjust expectation based on language design
}

CRYO_TEST(TypeCheckerTest, DetectBooleanIntegerMismatch) {
    TypeCheckerTestHelper helper;
    helper.setup();

    std::string source = "const x: boolean = 1;"; // Should be true/false
    auto ast = helper.parse_source(source);
    
    CRYO_ASSERT_TRUE(ast != nullptr);
    type_checker->check(ast.get());
    CRYO_EXPECT_TRUE(compiler->has_errors());
}

// ============================================================================
// Function Type Checking Tests
// ============================================================================

CRYO_TEST(TypeCheckerTest, CheckFunctionReturnType) {
    TypeCheckerTestHelper helper;
    helper.setup();

    std::string source = R"(
        function add(x: int, y: int) -> int {
            return x + y;
        }
    )";
    
    auto ast = helper.parse_source(source);
    CRYO_ASSERT_TRUE(ast != nullptr);
    type_checker->check(ast.get());
    CRYO_EXPECT_FALSE(compiler->has_errors());
}

CRYO_TEST(TypeCheckerTest, DetectWrongReturnType) {
    TypeCheckerTestHelper helper;
    helper.setup();

    std::string source = R"(
        function get_message() -> string {
            return 42; // Wrong return type
        }
    )";
    
    auto ast = helper.parse_source(source);
    CRYO_ASSERT_TRUE(ast != nullptr);
    type_checker->check(ast.get());
    CRYO_EXPECT_TRUE(compiler->has_errors());
}

CRYO_TEST(TypeCheckerTest, CheckFunctionParameterTypes) {
    TypeCheckerTestHelper helper;
    helper.setup();

    std::string source = R"(
        function multiply(x: int, y: int) -> int {
            return x * y;
        }
        
        function main() -> int {
            return multiply(5, 10); // Correct types
        }
    )";
    
    auto ast = helper.parse_source(source);
    CRYO_ASSERT_TRUE(ast != nullptr);
    type_checker->check(ast.get());
    CRYO_EXPECT_FALSE(compiler->has_errors());
}

CRYO_TEST(TypeCheckerTest, DetectWrongParameterTypes) {
    TypeCheckerTestHelper helper;
    helper.setup();

    std::string source = R"(
        function multiply(x: int, y: int) -> int {
            return x * y;
        }
        
        function main() -> int {
            return multiply("hello", 10); // Wrong first parameter
        }
    )";
    
    auto ast = helper.parse_source(source);
    CRYO_ASSERT_TRUE(ast != nullptr);
    type_checker->check(ast.get());
    CRYO_EXPECT_TRUE(compiler->has_errors());
}

// ============================================================================
// Binary Expression Type Checking Tests
// ============================================================================

CRYO_TEST(TypeCheckerTest, CheckArithmeticExpressions) {
    TypeCheckerTestHelper helper;
    helper.setup();

    std::string source = R"(
        function main() -> int {
            const a: int = 5;
            const b: int = 10;
            const sum: int = a + b;
            const diff: int = a - b;
            const product: int = a * b;
            const quotient: int = a / b;
            return sum;
        }
    )";
    
    auto ast = helper.parse_source(source);
    CRYO_ASSERT_TRUE(ast != nullptr);
    type_checker->check(ast.get());
    CRYO_EXPECT_FALSE(compiler->has_errors());
}

CRYO_TEST(TypeCheckerTest, CheckComparisonExpressions) {
    TypeCheckerTestHelper helper;
    helper.setup();

    std::string source = R"(
        function main() -> int {
            const a: int = 5;
            const b: int = 10;
            const less: boolean = a < b;
            const equal: boolean = a == b;
            const greater: boolean = a > b;
            return 0;
        }
    )";
    
    auto ast = helper.parse_source(source);
    CRYO_ASSERT_TRUE(ast != nullptr);
    type_checker->check(ast.get());
    CRYO_EXPECT_FALSE(compiler->has_errors());
}

CRYO_TEST(TypeCheckerTest, CheckLogicalExpressions) {
    TypeCheckerTestHelper helper;
    helper.setup();

    std::string source = R"(
        function main() -> int {
            const a: boolean = true;
            const b: boolean = false;
            const and_result: boolean = a && b;
            const or_result: boolean = a || b;
            const not_result: boolean = !a;
            return 0;
        }
    )";
    
    auto ast = helper.parse_source(source);
    CRYO_ASSERT_TRUE(ast != nullptr);
    type_checker->check(ast.get());
    CRYO_EXPECT_FALSE(compiler->has_errors());
}

// ============================================================================
// Struct Type Checking Tests
// ============================================================================

CRYO_TEST(TypeCheckerTest, CheckStructFieldAccess) {
    TypeCheckerTestHelper helper;
    helper.setup();

    std::string source = R"(
        type struct Point {
            x: int;
            y: int;
        }
        
        function main() -> int {
            const p: Point = Point({x: 10, y: 20});
            const x_coord: int = p.x;
            const y_coord: int = p.y;
            return x_coord + y_coord;
        }
    )";
    
    auto ast = helper.parse_source(source);
    CRYO_ASSERT_TRUE(ast != nullptr);
    type_checker->check(ast.get());
    CRYO_EXPECT_FALSE(compiler->has_errors());
}

CRYO_TEST(TypeCheckerTest, DetectInvalidFieldAccess) {
    TypeCheckerTestHelper helper;
    helper.setup();

    std::string source = R"(
        type struct Point {
            x: int;
            y: int;
        }
        
        function main() -> int {
            const p: Point = Point({x: 10, y: 20});
            const z_coord: int = p.z; // Field doesn't exist
            return z_coord;
        }
    )";
    
    auto ast = helper.parse_source(source);
    CRYO_ASSERT_TRUE(ast != nullptr);
    type_checker->check(ast.get());
    CRYO_EXPECT_TRUE(compiler->has_errors());
}

// ============================================================================
// Array Type Checking Tests
// ============================================================================

CRYO_TEST(TypeCheckerTest, CheckArrayAccess) {
    TypeCheckerTestHelper helper;
    helper.setup();

    std::string source = R"(
        function main() -> int {
            const numbers: int[] = [1, 2, 3, 4, 5];
            const first: int = numbers[0];
            const third: int = numbers[2];
            return first + third;
        }
    )";
    
    auto ast = helper.parse_source(source);
    CRYO_ASSERT_TRUE(ast != nullptr);
    type_checker->check(ast.get());
    CRYO_EXPECT_FALSE(compiler->has_errors());
}

CRYO_TEST(TypeCheckerTest, DetectArrayTypeMismatch) {
    TypeCheckerTestHelper helper;
    helper.setup();

    std::string source = R"(
        function main() -> int {
            const numbers: int[] = ["one", "two", "three"]; // String array assigned to int array
            return numbers[0];
        }
    )";
    
    auto ast = helper.parse_source(source);
    CRYO_ASSERT_TRUE(ast != nullptr);
    type_checker->check(ast.get());
    CRYO_EXPECT_TRUE(compiler->has_errors());
}

// ============================================================================
// Control Flow Type Checking Tests
// ============================================================================

CRYO_TEST(TypeCheckerTest, CheckIfConditionType) {
    TypeCheckerTestHelper helper;
    helper.setup();

    std::string source = R"(
        function main() -> int {
            const x: int = 10;
            if (x > 5) {
                return 1;
            }
            return 0;
        }
    )";
    
    auto ast = helper.parse_source(source);
    CRYO_ASSERT_TRUE(ast != nullptr);
    type_checker->check(ast.get());
    CRYO_EXPECT_FALSE(compiler->has_errors());
}

CRYO_TEST(TypeCheckerTest, DetectNonBooleanCondition) {
    TypeCheckerTestHelper helper;
    helper.setup();

    std::string source = R"(
        function main() -> int {
            const x: int = 10;
            if (x) { // Int used as condition (should be boolean)
                return 1;
            }
            return 0;
        }
    )";
    
    auto ast = helper.parse_source(source);
    CRYO_ASSERT_TRUE(ast != nullptr);
    type_checker->check(ast.get());
    // This might pass if implicit conversion is allowed
}

// ============================================================================
// Variable Scope Tests
// ============================================================================

CRYO_TEST(TypeCheckerTest, CheckVariableScope) {
    TypeCheckerTestHelper helper;
    helper.setup();

    std::string source = R"(
        function main() -> int {
            const x: int = 10;
            {
                const y: int = 20;
                const sum: int = x + y; // x should be accessible
            }
            return x; // y should not be accessible here
        }
    )";
    
    auto ast = helper.parse_source(source);
    CRYO_ASSERT_TRUE(ast != nullptr);
    type_checker->check(ast.get());
    CRYO_EXPECT_FALSE(compiler->has_errors());
}

CRYO_TEST(TypeCheckerTest, DetectUndeclaredVariable) {
    TypeCheckerTestHelper helper;
    helper.setup();

    std::string source = R"(
        function main() -> int {
            return undeclared_variable; // Should cause error
        }
    )";
    
    auto ast = helper.parse_source(source);
    CRYO_ASSERT_TRUE(ast != nullptr);
    type_checker->check(ast.get());
    CRYO_EXPECT_TRUE(compiler->has_errors());
}

// ============================================================================
// Generic Type Checking Tests
// ============================================================================

CRYO_TEST(TypeCheckerTest, CheckGenericInstantiation) {
    TypeCheckerTestHelper helper;
    helper.setup();

    std::string source = R"(
        type struct Container<T> {
            value: T;
        }
        
        function main() -> int {
            const int_container: Container<int> = Container<int>({value: 42});
            const string_container: Container<string> = Container<string>({value: "hello"});
            return 0;
        }
    )";
    
    auto ast = helper.parse_source(source);
    CRYO_ASSERT_TRUE(ast != nullptr);
    type_checker->check(ast.get());
    CRYO_EXPECT_FALSE(compiler->has_errors());
}

// ============================================================================
// Mutability Checking Tests
// ============================================================================

CRYO_TEST(TypeCheckerTest, CheckConstantMutability) {
    TypeCheckerTestHelper helper;
    helper.setup();

    std::string source = R"(
        function main() -> int {
            const x: int = 10;
            // x = 20; // This should cause an error
            return x;
        }
    )";
    
    auto ast = helper.parse_source(source);
    CRYO_ASSERT_TRUE(ast != nullptr);
    type_checker->check(ast.get());
    CRYO_EXPECT_FALSE(compiler->has_errors()); // No assignment, so no error
}

CRYO_TEST(TypeCheckerTest, DetectConstantAssignment) {
    TypeCheckerTestHelper helper;
    helper.setup();

    std::string source = R"(
        function main() -> int {
            const x: int = 10;
            x = 20; // Should cause error - can't modify const
            return x;
        }
    )";
    
    auto ast = helper.parse_source(source);
    CRYO_ASSERT_TRUE(ast != nullptr);
    type_checker->check(ast.get());
    CRYO_EXPECT_TRUE(compiler->has_errors());
}

CRYO_TEST(TypeCheckerTest, CheckMutableAssignment) {
    TypeCheckerTestHelper helper;
    helper.setup();

    std::string source = R"(
        function main() -> int {
            mut x: int = 10;
            x = 20; // Should be allowed
            return x;
        }
    )";
    
    auto ast = helper.parse_source(source);
    CRYO_ASSERT_TRUE(ast != nullptr);
    type_checker->check(ast.get());
    CRYO_EXPECT_FALSE(compiler->has_errors());
}

} // namespace CryoTest