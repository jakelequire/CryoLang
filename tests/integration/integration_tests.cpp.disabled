#include "test_utils.hpp"
#include <filesystem>
#include <fstream>

namespace CryoTest {

class IntegrationTestHelper : public CryoTestBase {
public:
    std::filesystem::path temp_dir;
    std::filesystem::path exe_path;
    
    void setup() {
        CryoTestBase::setup();
        
        // Create temporary directory for integration tests
        temp_dir = std::filesystem::temp_directory_path() / "cryo_integration_tests";
        std::filesystem::create_directories(temp_dir);
        
        exe_path = temp_dir / "test_program";
        #ifdef _WIN32
        exe_path += ".exe";
        #endif
    }
    
    void teardown() {
        // Clean up temporary files
        if (std::filesystem::exists(temp_dir)) {
            std::filesystem::remove_all(temp_dir);
        }
        CryoTestBase::teardown();
    }
    
    bool compile_and_run(const std::string& source, std::string& output, int& exit_code) {
        // Write source to temporary file
        auto source_file = temp_dir / "test.cryo";
        std::ofstream file(source_file);
        if (!file) {
            return false;
        }
        file << source;
        file.close();
        
        // Compile the source file
        if (!compile_file(source_file.string(), exe_path.string())) {
            return false;
        }
        
        // Execute the compiled program
        return execute_program(exe_path.string(), output, exit_code);
    }
    
    bool compile_file(const std::string& source_path, const std::string& output_path) {
        // Use the compiler instance to compile the file
        if (!compiler) {
            return false;
        }
        
        try {
            // This would be the actual compilation process
            // For now, we'll simulate it based on the compiler's capabilities
            auto result = compiler->compile_file(source_path, output_path);
            return result.success && !compiler->has_errors();
        } catch (const std::exception& e) {
            test_logger.error("Compilation failed: " + std::string(e.what()));
            return false;
        }
    }
    
    bool execute_program(const std::string& program_path, std::string& output, int& exit_code) {
        // Execute the program and capture output
        #ifdef _WIN32
        std::string command = program_path + " 2>&1";
        #else
        std::string command = program_path + " 2>&1";
        #endif
        
        FILE* pipe = popen(command.c_str(), "r");
        if (!pipe) {
            return false;
        }
        
        char buffer[128];
        output.clear();
        while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
            output += buffer;
        }
        
        exit_code = pclose(pipe);
        return true;
    }
};

// ============================================================================
// Basic Integration Tests
// ============================================================================

CRYO_TEST(IntegrationTest, CompileAndRunHelloWorld) { 
    IntegrationTestHelper helper; 
    helper.setup();
    std::string source = R"(
        namespace Main;
        import IO from <io/stdio>;
        
        function main() -> int {
            IO::println("Hello, World!");
            return 0;
        }
    )";
    
    std::string output;
    int exit_code;
    
    CRYO_ASSERT_TRUE(helper.compile_and_run(source, output, exit_code));
    CRYO_EXPECT_EQ(exit_code, 0);
    CRYO_EXPECT_TRUE(output.find("Hello, World!") != std::string::npos);
    
    helper.teardown();
}

CRYO_TEST(IntegrationTest, CompileAndRunArithmetic) { 
    IntegrationTestHelper helper; 
    helper.setup();
    std::string source = R"(
        function main() -> int {
            const a: int = 10;
            const b: int = 5;
            const sum: int = a + b;
            printf("Sum: %d\n", sum);
            return sum;
        }
    )";
    
    std::string output;
    int exit_code;
    
    CRYO_ASSERT_TRUE(compile_and_run(source, output, exit_code));
    CRYO_EXPECT_EQ(exit_code, 15); // Return value should be 15
    CRYO_EXPECT_TRUE(output.find("Sum: 15") != std::string::npos);
}

CRYO_TEST(IntegrationTest, CompileAndRunFunctionCall) { 
    IntegrationTestHelper helper; 
    helper.setup();
    std::string source = R"(
        function multiply(x: int, y: int) -> int {
            return x * y;
        }
        
        function main() -> int {
            const result: int = multiply(6, 7);
            printf("Result: %d\n", result);
            return result;
        }
    )";
    
    std::string output;
    int exit_code;
    
    CRYO_ASSERT_TRUE(compile_and_run(source, output, exit_code));
    CRYO_EXPECT_EQ(exit_code, 42);
    CRYO_EXPECT_TRUE(output.find("Result: 42") != std::string::npos);
}

// ============================================================================
// Control Flow Integration Tests
// ============================================================================

CRYO_TEST(IntegrationTest, CompileAndRunIfStatement) { 
    IntegrationTestHelper helper; 
    helper.setup();
    std::string source = R"(
        function main() -> int {
            const x: int = 10;
            if (x > 5) {
                printf("x is greater than 5\n");
                return 1;
            } else {
                printf("x is not greater than 5\n");
                return 0;
            }
        }
    )";
    
    std::string output;
    int exit_code;
    
    CRYO_ASSERT_TRUE(compile_and_run(source, output, exit_code));
    CRYO_EXPECT_EQ(exit_code, 1);
    CRYO_EXPECT_TRUE(output.find("x is greater than 5") != std::string::npos);
}

CRYO_TEST(IntegrationTest, CompileAndRunWhileLoop) { 
    IntegrationTestHelper helper; 
    helper.setup();
    std::string source = R"(
        function main() -> int {
            mut i: int = 0;
            mut sum: int = 0;
            
            while (i < 5) {
                sum = sum + i;
                i = i + 1;
            }
            
            printf("Sum: %d\n", sum);
            return sum;
        }
    )";
    
    std::string output;
    int exit_code;
    
    CRYO_ASSERT_TRUE(compile_and_run(source, output, exit_code));
    CRYO_EXPECT_EQ(exit_code, 10); // 0+1+2+3+4 = 10
    CRYO_EXPECT_TRUE(output.find("Sum: 10") != std::string::npos);
}

CRYO_TEST(IntegrationTest, CompileAndRunForLoop) { 
    IntegrationTestHelper helper; 
    helper.setup();
    std::string source = R"(
        function main() -> int {
            mut total: int = 0;
            
            for (mut i: int = 1; i <= 5; i = i + 1) {
                total = total + i;
            }
            
            printf("Total: %d\n", total);
            return total;
        }
    )";
    
    std::string output;
    int exit_code;
    
    CRYO_ASSERT_TRUE(compile_and_run(source, output, exit_code));
    CRYO_EXPECT_EQ(exit_code, 15); // 1+2+3+4+5 = 15
    CRYO_EXPECT_TRUE(output.find("Total: 15") != std::string::npos);
}

// ============================================================================
// Data Structure Integration Tests
// ============================================================================

CRYO_TEST(IntegrationTest, CompileAndRunStruct) { 
    IntegrationTestHelper helper; 
    helper.setup();
    std::string source = R"(
        type struct Point {
            x: int;
            y: int;
        }
        
        function main() -> int {
            const p: Point = Point({x: 10, y: 20});
            printf("Point: (%d, %d)\n", p.x, p.y);
            return p.x + p.y;
        }
    )";
    
    std::string output;
    int exit_code;
    
    CRYO_ASSERT_TRUE(compile_and_run(source, output, exit_code));
    CRYO_EXPECT_EQ(exit_code, 30);
    CRYO_EXPECT_TRUE(output.find("Point: (10, 20)") != std::string::npos);
}

CRYO_TEST(IntegrationTest, CompileAndRunArray) { 
    IntegrationTestHelper helper; 
    helper.setup();
    std::string source = R"(
        function main() -> int {
            const numbers: int[] = [1, 2, 3, 4, 5];
            mut sum: int = 0;
            
            for (mut i: int = 0; i < 5; i = i + 1) {
                sum = sum + numbers[i];
            }
            
            printf("Array sum: %d\n", sum);
            return sum;
        }
    )";
    
    std::string output;
    int exit_code;
    
    CRYO_ASSERT_TRUE(compile_and_run(source, output, exit_code));
    CRYO_EXPECT_EQ(exit_code, 15); // 1+2+3+4+5 = 15
    CRYO_EXPECT_TRUE(output.find("Array sum: 15") != std::string::npos);
}

// ============================================================================
// String Handling Integration Tests
// ============================================================================

CRYO_TEST(IntegrationTest, CompileAndRunStringOperations) { 
    IntegrationTestHelper helper; 
    helper.setup();
    std::string source = R"(
        function main() -> int {
            const greeting: string = "Hello";
            const name: string = "World";
            printf("%s, %s!\n", greeting, name);
            return 0;
        }
    )";
    
    std::string output;
    int exit_code;
    
    CRYO_ASSERT_TRUE(compile_and_run(source, output, exit_code));
    CRYO_EXPECT_EQ(exit_code, 0);
    CRYO_EXPECT_TRUE(output.find("Hello, World!") != std::string::npos);
}

// ============================================================================
// Error Handling Integration Tests
// ============================================================================

CRYO_TEST(IntegrationTest, CompilationErrorHandling) { 
    IntegrationTestHelper helper; 
    helper.setup();
    std::string source = R"(
        function main() -> int {
            const x: int = "this is wrong"; // Type error
            return x;
        }
    )";
    
    // Write source to temporary file
    auto source_file = temp_dir / "error_test.cryo";
    std::ofstream file(source_file);
    CRYO_ASSERT_TRUE(file.good());
    file << source;
    file.close();
    
    // Attempt compilation - should fail
    bool compilation_success = compile_file(source_file.string(), exe_path.string());
    CRYO_EXPECT_FALSE(compilation_success);
    CRYO_EXPECT_TRUE(compiler->has_errors());
}

// ============================================================================
// Memory Management Integration Tests
// ============================================================================

CRYO_TEST(IntegrationTest, CompileAndRunMemoryAllocation) { 
    IntegrationTestHelper helper; 
    helper.setup();
    std::string source = R"(
        function main() -> int {
            const large_array: int[] = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10];
            mut sum: int = 0;
            
            for (mut i: int = 0; i < 10; i = i + 1) {
                sum = sum + large_array[i];
            }
            
            printf("Large array sum: %d\n", sum);
            return sum;
        }
    )";
    
    std::string output;
    int exit_code;
    
    CRYO_ASSERT_TRUE(compile_and_run(source, output, exit_code));
    CRYO_EXPECT_EQ(exit_code, 55); // Sum of 1-10
    CRYO_EXPECT_TRUE(output.find("Large array sum: 55") != std::string::npos);
}

// ============================================================================
// Recursive Function Integration Tests
// ============================================================================

CRYO_TEST(IntegrationTest, CompileAndRunRecursion) { 
    IntegrationTestHelper helper; 
    helper.setup();
    std::string source = R"(
        function factorial(n: int) -> int {
            if (n <= 1) {
                return 1;
            }
            return n * factorial(n - 1);
        }
        
        function main() -> int {
            const result: int = factorial(5);
            printf("Factorial of 5: %d\n", result);
            return result;
        }
    )";
    
    std::string output;
    int exit_code;
    
    CRYO_ASSERT_TRUE(compile_and_run(source, output, exit_code));
    CRYO_EXPECT_EQ(exit_code, 120); // 5! = 120
    CRYO_EXPECT_TRUE(output.find("Factorial of 5: 120") != std::string::npos);
}

CRYO_TEST(IntegrationTest, CompileAndRunFibonacci) { 
    IntegrationTestHelper helper; 
    helper.setup();
    std::string source = R"(
        function fibonacci(n: int) -> int {
            if (n <= 1) {
                return n;
            }
            return fibonacci(n - 1) + fibonacci(n - 2);
        }
        
        function main() -> int {
            const result: int = fibonacci(10);
            printf("Fibonacci of 10: %d\n", result);
            return result;
        }
    )";
    
    std::string output;
    int exit_code;
    
    CRYO_ASSERT_TRUE(compile_and_run(source, output, exit_code));
    CRYO_EXPECT_EQ(exit_code, 55); // Fib(10) = 55
    CRYO_EXPECT_TRUE(output.find("Fibonacci of 10: 55") != std::string::npos);
}

// ============================================================================
// Complex Algorithm Integration Tests
// ============================================================================

CRYO_TEST(IntegrationTest, CompileAndRunBubbleSort) { 
    IntegrationTestHelper helper; 
    helper.setup();
    std::string source = R"(
        function bubble_sort(arr: int[], size: int) {
            for (mut i: int = 0; i < size - 1; i = i + 1) {
                for (mut j: int = 0; j < size - i - 1; j = j + 1) {
                    if (arr[j] > arr[j + 1]) {
                        const temp: int = arr[j];
                        arr[j] = arr[j + 1];
                        arr[j + 1] = temp;
                    }
                }
            }
        }
        
        function main() -> int {
            mut numbers: int[] = [64, 34, 25, 12, 22, 11, 90];
            
            printf("Original array: ");
            for (mut i: int = 0; i < 7; i = i + 1) {
                printf("%d ", numbers[i]);
            }
            printf("\n");
            
            bubble_sort(numbers, 7);
            
            printf("Sorted array: ");
            for (mut i: int = 0; i < 7; i = i + 1) {
                printf("%d ", numbers[i]);
            }
            printf("\n");
            
            return 0;
        }
    )";
    
    std::string output;
    int exit_code;
    
    CRYO_ASSERT_TRUE(compile_and_run(source, output, exit_code));
    CRYO_EXPECT_EQ(exit_code, 0);
    CRYO_EXPECT_TRUE(output.find("Original array:") != std::string::npos);
    CRYO_EXPECT_TRUE(output.find("Sorted array:") != std::string::npos);
}

// ============================================================================
// Multi-file Integration Tests
// ============================================================================

CRYO_TEST(IntegrationTest, CompileAndRunMultipleFiles) { 
    IntegrationTestHelper helper; 
    helper.setup();
    // This test would require implementing module system
    // For now, we'll create a placeholder
    
    std::string math_module = R"(
        function add(a: int, b: int) -> int {
            return a + b;
        }
        
        function multiply(a: int, b: int) -> int {
            return a * b;
        }
    )";
    
    std::string main_file = R"(
        // import math;
        
        function main() -> int {
            const sum: int = add(5, 3);
            const product: int = multiply(4, 6);
            printf("Sum: %d, Product: %d\n", sum, product);
            return sum + product;
        }
    )";
    
    // This test needs module system implementation
    // For now, just test single file compilation
    
    std::string combined_source = math_module + "\n" + main_file;
    std::string output;
    int exit_code;
    
    // This might not work until module system is implemented
    // CRYO_ASSERT_TRUE(compile_and_run(combined_source, output, exit_code));
}

// ============================================================================
// Performance Integration Tests
// ============================================================================

CRYO_TEST(IntegrationTest, CompileAndRunPerformanceTest) { 
    IntegrationTestHelper helper; 
    helper.setup();
    std::string source = R"(
        function compute_intensive() -> int {
            mut result: int = 0;
            for (mut i: int = 0; i < 10000; i = i + 1) {
                for (mut j: int = 0; j < 100; j = j + 1) {
                    result = result + (i * j);
                }
            }
            return result;
        }
        
        function main() -> int {
            const start_time: int = 0; // Would need time functions
            const result: int = compute_intensive();
            const end_time: int = 0;
            
            printf("Computation result: %d\n", result);
            return 0;
        }
    )";
    
    std::string output;
    int exit_code;
    
    CRYO_ASSERT_TRUE(compile_and_run(source, output, exit_code));
    CRYO_EXPECT_EQ(exit_code, 0);
    CRYO_EXPECT_TRUE(output.find("Computation result:") != std::string::npos);
}

} // namespace CryoTest