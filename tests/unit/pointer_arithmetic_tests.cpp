#include "test_utils.hpp"
#include "include/test_helpers.hpp"

using namespace CryoTest;

/**
 * @file pointer_arithmetic_tests.cpp
 * @brief Comprehensive tests for pointer arithmetic operations in CryoLang
 * 
 * Tests all pointer arithmetic operations, array indexing through pointers,
 * pointer increment/decrement, and pointer difference calculations.
 */

// ============================================================================
// Basic Pointer Operations
// ============================================================================

CRYO_TEST_DESC(PointerArithmetic, basic_addition_with_integer_array, 
    "Test pointer addition with integer array elements") {
    
    StdlibIntegrationTestHelper helper;
    helper.setup();

    std::string source = R"(
        function test_pointer_add() -> int {
            mut arr: int[] = [10, 20, 30, 40, 50];
            mut ptr: int* = &arr[0];
            
            // Test basic pointer addition
            const val1: int = *(ptr + 1);  // Should be 20
            const val2: int = *(ptr + 2);  // Should be 30
            const val3: int = *(ptr + 4);  // Should be 50
            
            return val1 + val2 + val3;     // 20 + 30 + 50 = 100
        }
    )";
    
    bool success = helper.compiles_to_ir(source);
    CRYO_EXPECT_TRUE(success);
    CRYO_EXPECT_FALSE(helper.has_errors());
}

CRYO_TEST_DESC(PointerArithmetic, basic_subtraction_with_integer_array,
    "Test pointer subtraction with integer array elements") {
    
    StdlibIntegrationTestHelper helper;
    helper.setup();

    std::string source = R"(
        function test_pointer_sub() -> int {
            mut arr: int[] = [10, 20, 30, 40, 50];
            mut ptr: int* = &arr[4];  // Start at index 4 (value 50)
            
            // Test basic pointer subtraction
            const val1: int = *(ptr - 1);  // Should be 40
            const val2: int = *(ptr - 2);  // Should be 30  
            const val3: int = *(ptr - 4);  // Should be 10
            
            return val1 + val2 + val3;     // 40 + 30 + 10 = 80
        }
    )";
    
    bool success = helper.compiles_to_ir(source);
    CRYO_EXPECT_TRUE(success);
    CRYO_EXPECT_FALSE(helper.has_errors());
}

CRYO_TEST_DESC(PointerArithmetic, pre_increment_with_dereference,
    "Test pre-increment operator on pointers with immediate dereference") {
    
    StdlibIntegrationTestHelper helper;
    helper.setup();

    std::string source = R"(
        function test_pre_increment() -> int {
            mut arr: int[] = [5, 15, 25, 35];
            mut ptr: int* = &arr[0];
            
            // Test pre-increment
            const original: int = *ptr;     // 5
            ++ptr;
            const after_inc: int = *ptr;    // 15
            
            return original + after_inc;    // 5 + 15 = 20
        }
    )";
    
    bool success = helper.compiles_to_ir(source);
    CRYO_EXPECT_TRUE(success);
    CRYO_EXPECT_FALSE(helper.has_errors());
}

CRYO_TEST_DESC(PointerArithmetic, post_increment_with_dereference,
    "Test post-increment operator on pointers with value capture") {
    
    StdlibIntegrationTestHelper helper;
    helper.setup();

    std::string source = R"(
        function test_post_increment() -> int {
            mut arr: int[] = [7, 17, 27, 37];
            mut ptr: int* = &arr[0];
            
            // Test post-increment behavior
            const original: int = *ptr;     // 7
            ptr++;  
            const after_inc: int = *ptr;    // 17
            
            return original + after_inc;    // 7 + 17 = 24
        }
    )";
    
    bool success = helper.compiles_to_ir(source);
    CRYO_EXPECT_TRUE(success);
    CRYO_EXPECT_FALSE(helper.has_errors());
}

CRYO_TEST_DESC(PointerArithmetic, pre_decrement_with_dereference,
    "Test pre-decrement operator on pointers") {
    
    StdlibIntegrationTestHelper helper;
    helper.setup();

    std::string source = R"(
        function test_pre_decrement() -> int {
            mut arr: int[] = [100, 200, 300, 400];
            mut ptr: int* = &arr[3];  // Start at index 3 (400)
            
            const original: int = *ptr;     // 400
            --ptr;
            const after_dec: int = *ptr;    // 300
            
            return original - after_dec;    // 400 - 300 = 100
        }
    )";
    
    bool success = helper.compiles_to_ir(source);
    CRYO_EXPECT_TRUE(success);
    CRYO_EXPECT_FALSE(helper.has_errors());
}

CRYO_TEST_DESC(PointerArithmetic, post_decrement_with_dereference,
    "Test post-decrement operator on pointers") {
    
    StdlibIntegrationTestHelper helper;
    helper.setup();

    std::string source = R"(
        function test_post_decrement() -> int {
            mut arr: int[] = [11, 22, 33, 44];
            mut ptr: int* = &arr[2];  // Start at index 2 (33)
            
            const original: int = *ptr;     // 33
            ptr--;
            const after_dec: int = *ptr;    // 22
            
            return original + after_dec;    // 33 + 22 = 55
        }
    )";
    
    bool success = helper.compiles_to_ir(source);
    CRYO_EXPECT_TRUE(success);
    CRYO_EXPECT_FALSE(helper.has_errors());
}

// ============================================================================
// Advanced Pointer Operations
// ============================================================================

CRYO_TEST_DESC(PointerArithmetic, chained_arithmetic_operations,
    "Test multiple pointer arithmetic operations in sequence") {
    
    StdlibIntegrationTestHelper helper;
    helper.setup();

    std::string source = R"(
        function test_chained_ops() -> int {
            mut arr: int[] = [1, 3, 5, 7, 9, 11, 13];
            mut ptr: int* = &arr[0];
            
            // Chain multiple operations
            ptr = ptr + 2;              // Now at index 2 (value 5)
            const val1: int = *ptr;     // 5
            
            ptr = ptr + 3;              // Now at index 5 (value 11) 
            const val2: int = *ptr;     // 11
            
            ptr = ptr - 1;              // Now at index 4 (value 9)
            const val3: int = *ptr;     // 9
            
            return val1 + val2 + val3;  // 5 + 11 + 9 = 25
        }
    )";
    
    bool success = helper.compiles_to_ir(source);
    CRYO_EXPECT_TRUE(success);
    CRYO_EXPECT_FALSE(helper.has_errors());
}

CRYO_TEST_DESC(PointerArithmetic, mixed_increment_decrement_operations,
    "Test mixing increment and decrement operations on same pointer") {
    
    StdlibIntegrationTestHelper helper;
    helper.setup();

    std::string source = R"(
        function test_mixed_ops() -> int {
            mut arr: int[] = [2, 4, 6, 8, 10];
            mut ptr: int* = &arr[2];  // Start at index 2 (value 6)
            
            const start: int = *ptr;    // 6
            
            ++ptr;                      // Move to index 3 (value 8)
            const after_inc: int = *ptr; // 8
            
            --ptr;                      // Move back to index 2 (value 6)
            const after_dec: int = *ptr; // 6
            
            ptr++;                      // Move to index 3 again (value 8)
            ptr--;                      // Move back to index 2 (value 6)
            const final: int = *ptr;     // 6
            
            return start + after_inc + after_dec + final; // 6+8+6+6 = 26
        }
    )";
    
    bool success = helper.compiles_to_ir(source);
    CRYO_EXPECT_TRUE(success);
    CRYO_EXPECT_FALSE(helper.has_errors());
}

// ============================================================================
// Different Data Types
// ============================================================================

CRYO_TEST_DESC(PointerArithmetic, float_array_pointer_operations,
    "Test pointer arithmetic with float arrays") {
    
    StdlibIntegrationTestHelper helper;
    helper.setup();

    std::string source = R"(
        function test_float_pointers() -> int {
            mut arr: float[] = [1.5, 2.5, 3.5, 4.5];
            mut ptr: float* = &arr[0];
            
            const val1: float = *(ptr + 1);  // 2.5
            const val2: float = *(ptr + 3);  // 4.5
            
            // Convert to int for return (simple test)
            return int(val1 + val2);         // int(7.0) = 7
        }
    )";
    
    bool success = helper.compiles_to_ir(source);
    CRYO_EXPECT_TRUE(success);
    CRYO_EXPECT_FALSE(helper.has_errors());
}

CRYO_TEST_DESC(PointerArithmetic, boolean_array_pointer_operations,
    "Test pointer arithmetic with boolean arrays") {
    
    StdlibIntegrationTestHelper helper;
    helper.setup();

    std::string source = R"(
        function test_boolean_pointers() -> int {
            mut arr: boolean[] = [true, false, true, false];
            mut ptr: boolean* = &arr[0];
            
            const val1: boolean = *(ptr + 0);  // true
            const val2: boolean = *(ptr + 2);  // true
            const val3: boolean = *(ptr + 1);  // false
            
            // Convert booleans to integers for testing
            const result: int = (val1 ? 1 : 0) + (val2 ? 1 : 0) + (val3 ? 1 : 0);
            return result;  // 1 + 1 + 0 = 2
        }
    )";
    
    bool success = helper.compiles_to_ir(source);
    CRYO_EXPECT_TRUE(success);
    CRYO_EXPECT_FALSE(helper.has_errors());
}

// ============================================================================
// Boundary and Edge Cases
// ============================================================================

CRYO_TEST_DESC(PointerArithmetic, zero_offset_operations,
    "Test pointer arithmetic with zero offset (should be no-op)") {
    
    StdlibIntegrationTestHelper helper;
    helper.setup();

    std::string source = R"(
        function test_zero_offset() -> int {
            mut arr: int[] = [42, 84, 126];
            mut ptr: int* = &arr[1];  // Start at index 1 (value 84)
            
            const original: int = *ptr;       // 84
            const zero_add: int = *(ptr + 0); // Should still be 84
            const zero_sub: int = *(ptr - 0); // Should still be 84
            
            return original + zero_add + zero_sub; // 84 + 84 + 84 = 252
        }
    )";
    
    bool success = helper.compiles_to_ir(source);
    CRYO_EXPECT_TRUE(success);
    CRYO_EXPECT_FALSE(helper.has_errors());
}

CRYO_TEST_DESC(PointerArithmetic, single_element_array_operations,
    "Test pointer operations on single-element arrays") {
    
    StdlibIntegrationTestHelper helper;
    helper.setup();

    std::string source = R"(
        function test_single_element() -> int {
            mut arr: int[] = [99];
            mut ptr: int* = &arr[0];
            
            const value: int = *ptr;          // 99
            const same_value: int = *(ptr + 0); // Still 99
            
            return value + same_value;        // 99 + 99 = 198
        }
    )";
    
    bool success = helper.compiles_to_ir(source);
    CRYO_EXPECT_TRUE(success);
    CRYO_EXPECT_FALSE(helper.has_errors());
}
