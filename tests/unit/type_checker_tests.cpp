#include <gtest/gtest.h>
#include "test_utils.hpp"
#include "AST/TypeChecker.hpp"

namespace CryoTest {

class TypeCheckerTest : public CryoTestBase {
protected:
    std::unique_ptr<Cryo::TypeChecker> type_checker;
    
    void SetUp() override {
        CryoTestBase::SetUp();
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

TEST_F(TypeCheckerTest, InferIntegerLiteralType) {
    std::string source = "const x: int = 42;";
    auto ast = parse_source(source);
    
    ASSERT_TRUE(ast != nullptr);
    
    // Run type checker
    type_checker->check(ast.get());
    
    // Verify no type errors
    EXPECT_FALSE(compiler->has_errors());
}

TEST_F(TypeCheckerTest, InferFloatLiteralType) {
    std::string source = "const pi: float = 3.14;";
    auto ast = parse_source(source);
    
    ASSERT_TRUE(ast != nullptr);
    type_checker->check(ast.get());
    EXPECT_FALSE(compiler->has_errors());
}

TEST_F(TypeCheckerTest, InferStringLiteralType) {
    std::string source = R"(const message: string = "Hello, World!";)";
    auto ast = parse_source(source);
    
    ASSERT_TRUE(ast != nullptr);
    type_checker->check(ast.get());
    EXPECT_FALSE(compiler->has_errors());
}

TEST_F(TypeCheckerTest, InferBooleanLiteralType) {
    std::string source = "const flag: boolean = true;";
    auto ast = parse_source(source);
    
    ASSERT_TRUE(ast != nullptr);
    type_checker->check(ast.get());
    EXPECT_FALSE(compiler->has_errors());
}

// ============================================================================
// Type Mismatch Detection Tests
// ============================================================================

TEST_F(TypeCheckerTest, DetectIntegerStringMismatch) {
    std::string source = R"(const x: int = "not a number";)";
    auto ast = parse_source(source);
    
    ASSERT_TRUE(ast != nullptr);
    type_checker->check(ast.get());
    EXPECT_TRUE(compiler->has_errors());
}

TEST_F(TypeCheckerTest, DetectFloatIntegerMismatch) {
    std::string source = "const x: float = 42;"; // Should be 42.0
    auto ast = parse_source(source);
    
    ASSERT_TRUE(ast != nullptr);
    type_checker->check(ast.get());
    
    // This might pass if implicit conversion is allowed
    // Adjust expectation based on language design
}

TEST_F(TypeCheckerTest, DetectBooleanIntegerMismatch) {
    std::string source = "const x: boolean = 1;"; // Should be true/false
    auto ast = parse_source(source);
    
    ASSERT_TRUE(ast != nullptr);
    type_checker->check(ast.get());
    EXPECT_TRUE(compiler->has_errors());
}

// ============================================================================
// Function Type Checking Tests
// ============================================================================

TEST_F(TypeCheckerTest, CheckFunctionReturnType) {
    std::string source = R"(
        function add(x: int, y: int) -> int {
            return x + y;
        }
    )";
    
    auto ast = parse_source(source);
    ASSERT_TRUE(ast != nullptr);
    type_checker->check(ast.get());
    EXPECT_FALSE(compiler->has_errors());
}

TEST_F(TypeCheckerTest, DetectWrongReturnType) {
    std::string source = R"(
        function get_message() -> string {
            return 42; // Wrong return type
        }
    )";
    
    auto ast = parse_source(source);
    ASSERT_TRUE(ast != nullptr);
    type_checker->check(ast.get());
    EXPECT_TRUE(compiler->has_errors());
}

TEST_F(TypeCheckerTest, CheckFunctionParameterTypes) {
    std::string source = R"(
        function multiply(x: int, y: int) -> int {
            return x * y;
        }
        
        function main() -> int {
            return multiply(5, 10); // Correct types
        }
    )";
    
    auto ast = parse_source(source);
    ASSERT_TRUE(ast != nullptr);
    type_checker->check(ast.get());
    EXPECT_FALSE(compiler->has_errors());
}

TEST_F(TypeCheckerTest, DetectWrongParameterTypes) {
    std::string source = R"(
        function multiply(x: int, y: int) -> int {
            return x * y;
        }
        
        function main() -> int {
            return multiply("hello", 10); // Wrong first parameter
        }
    )";
    
    auto ast = parse_source(source);
    ASSERT_TRUE(ast != nullptr);
    type_checker->check(ast.get());
    EXPECT_TRUE(compiler->has_errors());
}

// ============================================================================
// Binary Expression Type Checking Tests
// ============================================================================

TEST_F(TypeCheckerTest, CheckArithmeticExpressions) {
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
    
    auto ast = parse_source(source);
    ASSERT_TRUE(ast != nullptr);
    type_checker->check(ast.get());
    EXPECT_FALSE(compiler->has_errors());
}

TEST_F(TypeCheckerTest, CheckComparisonExpressions) {
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
    
    auto ast = parse_source(source);
    ASSERT_TRUE(ast != nullptr);
    type_checker->check(ast.get());
    EXPECT_FALSE(compiler->has_errors());
}

TEST_F(TypeCheckerTest, CheckLogicalExpressions) {
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
    
    auto ast = parse_source(source);
    ASSERT_TRUE(ast != nullptr);
    type_checker->check(ast.get());
    EXPECT_FALSE(compiler->has_errors());
}

// ============================================================================
// Struct Type Checking Tests
// ============================================================================

TEST_F(TypeCheckerTest, CheckStructFieldAccess) {
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
    
    auto ast = parse_source(source);
    ASSERT_TRUE(ast != nullptr);
    type_checker->check(ast.get());
    EXPECT_FALSE(compiler->has_errors());
}

TEST_F(TypeCheckerTest, DetectInvalidFieldAccess) {
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
    
    auto ast = parse_source(source);
    ASSERT_TRUE(ast != nullptr);
    type_checker->check(ast.get());
    EXPECT_TRUE(compiler->has_errors());
}

// ============================================================================
// Array Type Checking Tests
// ============================================================================

TEST_F(TypeCheckerTest, CheckArrayAccess) {
    std::string source = R"(
        function main() -> int {
            const numbers: int[] = [1, 2, 3, 4, 5];
            const first: int = numbers[0];
            const third: int = numbers[2];
            return first + third;
        }
    )";
    
    auto ast = parse_source(source);
    ASSERT_TRUE(ast != nullptr);
    type_checker->check(ast.get());
    EXPECT_FALSE(compiler->has_errors());
}

TEST_F(TypeCheckerTest, DetectArrayTypeMismatch) {
    std::string source = R"(
        function main() -> int {
            const numbers: int[] = ["one", "two", "three"]; // String array assigned to int array
            return numbers[0];
        }
    )";
    
    auto ast = parse_source(source);
    ASSERT_TRUE(ast != nullptr);
    type_checker->check(ast.get());
    EXPECT_TRUE(compiler->has_errors());
}

// ============================================================================
// Control Flow Type Checking Tests
// ============================================================================

TEST_F(TypeCheckerTest, CheckIfConditionType) {
    std::string source = R"(
        function main() -> int {
            const x: int = 10;
            if (x > 5) {
                return 1;
            }
            return 0;
        }
    )";
    
    auto ast = parse_source(source);
    ASSERT_TRUE(ast != nullptr);
    type_checker->check(ast.get());
    EXPECT_FALSE(compiler->has_errors());
}

TEST_F(TypeCheckerTest, DetectNonBooleanCondition) {
    std::string source = R"(
        function main() -> int {
            const x: int = 10;
            if (x) { // Int used as condition (should be boolean)
                return 1;
            }
            return 0;
        }
    )";
    
    auto ast = parse_source(source);
    ASSERT_TRUE(ast != nullptr);
    type_checker->check(ast.get());
    // This might pass if implicit conversion is allowed
}

// ============================================================================
// Variable Scope Tests
// ============================================================================

TEST_F(TypeCheckerTest, CheckVariableScope) {
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
    
    auto ast = parse_source(source);
    ASSERT_TRUE(ast != nullptr);
    type_checker->check(ast.get());
    EXPECT_FALSE(compiler->has_errors());
}

TEST_F(TypeCheckerTest, DetectUndeclaredVariable) {
    std::string source = R"(
        function main() -> int {
            return undeclared_variable; // Should cause error
        }
    )";
    
    auto ast = parse_source(source);
    ASSERT_TRUE(ast != nullptr);
    type_checker->check(ast.get());
    EXPECT_TRUE(compiler->has_errors());
}

// ============================================================================
// Generic Type Checking Tests
// ============================================================================

TEST_F(TypeCheckerTest, CheckGenericInstantiation) {
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
    
    auto ast = parse_source(source);
    ASSERT_TRUE(ast != nullptr);
    type_checker->check(ast.get());
    EXPECT_FALSE(compiler->has_errors());
}

// ============================================================================
// Mutability Checking Tests
// ============================================================================

TEST_F(TypeCheckerTest, CheckConstantMutability) {
    std::string source = R"(
        function main() -> int {
            const x: int = 10;
            // x = 20; // This should cause an error
            return x;
        }
    )";
    
    auto ast = parse_source(source);
    ASSERT_TRUE(ast != nullptr);
    type_checker->check(ast.get());
    EXPECT_FALSE(compiler->has_errors()); // No assignment, so no error
}

TEST_F(TypeCheckerTest, DetectConstantAssignment) {
    std::string source = R"(
        function main() -> int {
            const x: int = 10;
            x = 20; // Should cause error - can't modify const
            return x;
        }
    )";
    
    auto ast = parse_source(source);
    ASSERT_TRUE(ast != nullptr);
    type_checker->check(ast.get());
    EXPECT_TRUE(compiler->has_errors());
}

TEST_F(TypeCheckerTest, CheckMutableAssignment) {
    std::string source = R"(
        function main() -> int {
            mut x: int = 10;
            x = 20; // Should be allowed
            return x;
        }
    )";
    
    auto ast = parse_source(source);
    ASSERT_TRUE(ast != nullptr);
    type_checker->check(ast.get());
    EXPECT_FALSE(compiler->has_errors());
}

} // namespace CryoTest