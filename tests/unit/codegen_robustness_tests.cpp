#include "test_utils.hpp"
#include "include/test_helpers.hpp"
#include <cstdlib>

using namespace CryoTest;

// ============================================================================
// Code Generation Robustness Tests
// Specifically targets issues found in the main test suite
// ============================================================================

/**
 * Test unary operators that are currently causing crashes
 * Error: "Unsupported unary operator: %s"
 */
CRYO_TEST_DESC(Codegen, UnaryOperatorSupport, "Tests all unary operators for proper codegen support") {
    CodegenTestHelper helper;
    helper.setup();

    std::string source = R"(
        function test_unary_operators() -> void {
            mut x: i32 = 10;
            mut ptr: i32* = &x;
            
            // Test unary minus
            const neg: i32 = -x;
            
            // Test logical NOT
            const flag: boolean = true;
            const not_flag: boolean = !flag;
            
            // Test dereference
            const deref_val: i32 = *ptr;
            
            // Test address-of (already used above)
            const addr: i32* = &x;
            
            return;
        }
    )";
    
    bool success = helper.compile_source(source);
    if (!success) {
        std::cout << "Unary operator test failed: " << helper.get_diagnostic_summary() << std::endl;
    }
    CRYO_EXPECT_TRUE(success);
    CRYO_EXPECT_FALSE(helper.has_errors());
}

/**
 * Test pre/post increment operators separately
 * These are causing specific crashes in pointer arithmetic tests
 */
CRYO_TEST_DESC(Codegen, IncrementDecrementOperators, "Tests increment and decrement operators") {
    CodegenTestHelper helper;
    helper.setup();

    std::string source = R"(
        function test_increment_decrement() -> void {
            mut x: i32 = 5;
            
            // Pre-increment
            const pre_inc: i32 = ++x; // x becomes 6, pre_inc = 6
            
            // Post-increment 
            const post_inc: i32 = x++; // post_inc = 6, x becomes 7
            
            // Pre-decrement
            const pre_dec: i32 = --x; // x becomes 6, pre_dec = 6
            
            // Post-decrement
            const post_dec: i32 = x--; // post_dec = 6, x becomes 5
            
            return;
        }
    )";
    
    bool success = helper.compile_source(source);
    if (!success) {
        std::cout << "Increment/Decrement test failed: " << helper.get_diagnostic_summary() << std::endl;
    }
    CRYO_EXPECT_TRUE(success);
    CRYO_EXPECT_FALSE(helper.has_errors());
}

/**
 * Test pointer arithmetic that's currently crashing
 * Error patterns suggest issues with pointer increment/decrement
 */
CRYO_TEST_DESC(Codegen, BasicPointerArithmetic, "Tests basic pointer arithmetic operations") {
    CodegenTestHelper helper;
    helper.setup();

    std::string source = R"(
        function test_pointer_arithmetic() -> void {
            mut arr: i32[3] = [1, 2, 3];
            mut ptr: i32* = &arr[0];
            
            // Basic pointer access
            const first: i32 = *ptr;
            
            // Pointer arithmetic without increment (safer test)
            const second_ptr: i32* = ptr + 1;
            const second: i32 = *second_ptr;
            
            return;
        }
    )";
    
    bool success = helper.compile_source(source);
    if (!success) {
        std::cout << "Basic pointer arithmetic test failed: " << helper.get_diagnostic_summary() << std::endl;
    }
    CRYO_EXPECT_TRUE(success);
    CRYO_EXPECT_FALSE(helper.has_errors());
}

/**
 * Test string handling to prevent access violations
 * Error: "Exit Code: -1073741819 (0xC0000005 - Access Violation)"
 */
CRYO_TEST_DESC(Codegen, StringHandlingSafety, "Tests string compilation without memory access violations") {
    CodegenTestHelper helper;
    helper.setup();

    std::string source = R"(
        function test_string_safety() -> boolean {
            const simple: string = "test";
            const empty: string = "";
            const longer: string = "This is a longer test string";
            
            return true;
        }
    )";
    
    bool success = helper.compile_source(source);
    if (!success) {
        std::cout << "String safety test failed: " << helper.get_diagnostic_summary() << std::endl;
    }
    CRYO_EXPECT_TRUE(success);
    CRYO_EXPECT_FALSE(helper.has_errors());
}

/**
 * Test struct method calls that are failing IR generation
 * Error: "generate_function_call returned NULL"
 */
CRYO_TEST_DESC(Codegen, StructMethodCallGeneration, "Tests struct method call IR generation") {
    CodegenTestHelper helper;
    helper.setup();

    std::string source = R"(
        type struct Calculator {
            value: i32;
            
            Calculator(initial: i32) {
                this.value = initial;
            }
            
            add(amount: i32) -> i32 {
                this.value = this.value + amount;
                return this.value;
            }
            
            get_value() -> i32 {
                return this.value;
            }
        }
        
        function test_method_calls() -> i32 {
            const calc: Calculator = Calculator(10);
            const result: i32 = calc.add(5);
            return calc.get_value();
        }
    )";
    
    bool success = helper.compile_source(source);
    if (!success) {
        std::cout << "Method call generation test failed: " << helper.get_diagnostic_summary() << std::endl;
    }
    CRYO_EXPECT_TRUE(success);
    CRYO_EXPECT_FALSE(helper.has_errors());
}

/**
 * Test array operations that are causing crashes
 * Error: "Unsupported unary operator" in array contexts
 */
CRYO_TEST_DESC(Codegen, ArrayOperationsSafety, "Tests array operations without unary operator issues") {
    CodegenTestHelper helper;
    helper.setup();

    std::string source = R"(
        function test_array_safety() -> i32 {
            const arr: i32[3] = [10, 20, 30];
            
            // Basic array access
            const first: i32 = arr[0];
            const second: i32 = arr[1];
            const third: i32 = arr[2];
            
            // Array element arithmetic
            const sum: i32 = first + second + third;
            
            return sum;
        }
    )";
    
    bool success = helper.compile_source(source);
    if (!success) {
        std::cout << "Array safety test failed: " << helper.get_diagnostic_summary() << std::endl;
    }
    CRYO_EXPECT_TRUE(success);
    CRYO_EXPECT_FALSE(helper.has_errors());
}

/**
 * Test class constructor issues that cause IR verification failures
 * Error: "Generated IR failed verification"
 */
CRYO_TEST_DESC(Codegen, ClassConstructorIR, "Tests class constructor IR generation and verification") {
    CodegenTestHelper helper;
    helper.setup();

    std::string source = R"(
        type class Container {
        private:
            data: i32;
            
        public:
            Container(value: i32) {
                this.data = value;
            }
            
            get_data() -> i32 {
                return this.data;
            }
            
            set_data(new_value: i32) -> void {
                this.data = new_value;
                return;
            }
        }
        
        function test_class_constructor() -> i32 {
            const container: Container = Container(42);
            return container.get_data();
        }
    )";
    
    bool success = helper.compile_source(source);
    if (!success) {
        std::cout << "Class constructor IR test failed: " << helper.get_diagnostic_summary() << std::endl;
    }
    CRYO_EXPECT_TRUE(success);
    CRYO_EXPECT_FALSE(helper.has_errors());
}

/**
 * Test proper function return handling
 * Addresses "Function return type does not match operand type of return inst" errors
 */
CRYO_TEST_DESC(Codegen, FunctionReturnTypeMatching, "Tests proper function return type handling") {
    CodegenTestHelper helper;
    helper.setup();

    std::string source = R"(
        // Function returning boolean
        function test_bool() -> boolean {
            return true;
        }
        
        // Function returning int
        function test_int() -> i32 {
            return 42;
        }
        
        // Function returning void
        function test_void() -> void {
            const x: i32 = 10;
            return;
        }
        
        // Function with conditional returns
        function test_conditional(flag: boolean) -> i32 {
            if (flag) {
                return 1;
            } else {
                return 0;
            }
        }
    )";
    
    bool success = helper.compile_source(source);
    if (!success) {
        std::cout << "Function return type test failed: " << helper.get_diagnostic_summary() << std::endl;
    }
    CRYO_EXPECT_TRUE(success);
    CRYO_EXPECT_FALSE(helper.has_errors());
}