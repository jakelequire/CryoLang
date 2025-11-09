#include "test_utils.hpp"
#include "Compiler/CompilerInstance.hpp"

namespace CryoTest {

class CompilationTest : public CryoTestBase {
protected:
    void expect_successful_compilation_and_execution(const std::string& source, 
                                                   const std::string& expected_output = "") {
        // Compile the source
        expect_compilation_success(source);
        
        // If expected output is provided, we could run the executable and check
        // This would require platform-specific execution logic
        if (!expected_output.empty()) {
            // TODO: Implement executable execution and output capture
        }
    }
};

// ============================================================================
// Basic Compilation Tests
// ============================================================================

CRYO_TEST(CompilationTest, CompileSimpleProgram) { 
    CompilationTestHelper helper; 
    helper.setup();
    std::string source = R"(
        import IO from <io/stdio>;
        
        function main() -> int {
            IO::println("Hello, World!");
            return 0;
        }
    )";
    
    expect_compilation_success(source);
}

CRYO_TEST(CompilationTest, CompileWithArithmetic) { 
    CompilationTestHelper helper; 
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
    
    expect_compilation_success(source);
}

CRYO_TEST(CompilationTest, CompileWithVariables) { 
    CompilationTestHelper helper; 
    helper.setup();
    std::string source = R"(
        function main() -> int {
            const x: int = 42;
            mut y: int = 10;
            y = y + x;
            return y;
        }
    )";
    
    expect_compilation_success(source);
}

// ============================================================================
// Control Flow Compilation Tests
// ============================================================================

CRYO_TEST(CompilationTest, CompileIfStatement) { 
    CompilationTestHelper helper; 
    helper.setup();
    std::string source = R"(
        function main() -> int {
            const x: int = 10;
            if (x > 5) {
                return 1;
            } else {
                return 0;
            }
        }
    )";
    
    expect_compilation_success(source);
}

CRYO_TEST(CompilationTest, CompileWhileLoop) { 
    CompilationTestHelper helper; 
    helper.setup();
    std::string source = R"(
        function main() -> int {
            mut counter: int = 0;
            while (counter < 5) {
                counter = counter + 1;
            }
            return counter;
        }
    )";
    
    expect_compilation_success(source);
}

CRYO_TEST(CompilationTest, CompileForLoop) { 
    CompilationTestHelper helper; 
    helper.setup();
    std::string source = R"(
        function main() -> int {
            mut sum: int = 0;
            for (mut i: int = 0; i < 10; i++) {
                sum = sum + i;
            }
            return sum;
        }
    )";
    
    expect_compilation_success(source);
}

// ============================================================================
// Struct Compilation Tests
// ============================================================================

CRYO_TEST(CompilationTest, CompileStructDeclaration) { 
    CompilationTestHelper helper; 
    helper.setup();
    std::string source = R"(
        type struct Point {
            x: int;
            y: int;
        }
        
        function main() -> int {
            const p: Point = Point({x: 10, y: 20});
            return p.x + p.y;
        }
    )";
    
    expect_compilation_success(source);
}

CRYO_TEST(CompilationTest, CompileStructWithMethods) { 
    CompilationTestHelper helper; 
    helper.setup();
    std::string source = R"(
        type struct Point {
            x: int;
            y: int;
            
            Point(x: int, y: int);
            get_sum() -> int;
        }
        
        implement Point {
            Point(x: int, y: int) {
                this.x = x;
                this.y = y;
            }
            
            get_sum() -> int {
                return this.x + this.y;
            }
        }
        
        function main() -> int {
            const p: Point = new Point(5, 15);
            return p.get_sum();
        }
    )";
    
    expect_compilation_success(source);
}

// ============================================================================
// Array Compilation Tests
// ============================================================================

CRYO_TEST(CompilationTest, CompileArrayOperations) { 
    CompilationTestHelper helper; 
    helper.setup();
    std::string source = R"(
        function main() -> int {
            const numbers: int[] = [1, 2, 3, 4, 5];
            const first: int = numbers[0];
            const last: int = numbers[4];
            return first + last;
        }
    )";
    
    expect_compilation_success(source);
}

// ============================================================================
// Function Compilation Tests
// ============================================================================

CRYO_TEST(CompilationTest, CompileRecursiveFunction) { 
    CompilationTestHelper helper; 
    helper.setup();
    std::string source = R"(
        function factorial(n: int) -> int {
            if (n <= 1) {
                return 1;
            }
            return n * factorial(n - 1);
        }
        
        function main() -> int {
            return factorial(5);
        }
    )";
    
    expect_compilation_success(source);
}

CRYO_TEST(CompilationTest, CompileOverloadedFunctions) { 
    CompilationTestHelper helper; 
    helper.setup();
    std::string source = R"(
        function add(x: int, y: int) -> int {
            return x + y;
        }
        
        function add(x: float, y: float) -> float {
            return x + y;
        }
        
        function main() -> int {
            const int_result: int = add(5, 10);
            const float_result: float = add(3.14, 2.86);
            return int_result;
        }
    )";
    
    expect_compilation_success(source);
}

// ============================================================================
// Enum Compilation Tests
// ============================================================================

CRYO_TEST(CompilationTest, CompileSimpleEnum) { 
    CompilationTestHelper helper; 
    helper.setup();
    std::string source = R"(
        enum Color {
            RED,
            GREEN,
            BLUE
        }
        
        function main() -> int {
            const color: Color = Color::RED;
            return 0;
        }
    )";
    
    expect_compilation_success(source);
}

CRYO_TEST(CompilationTest, CompileComplexEnum) { 
    CompilationTestHelper helper; 
    helper.setup();
    std::string source = R"(
        enum Shape {
            Circle(float),
            Rectangle(float, float),
            Point
        }
        
        function get_area(shape: Shape) -> float {
            match shape {
                Shape::Circle(r) => {
                    return 3.14 * r * r;
                }
                Shape::Rectangle(w, h) => {
                    return w * h;
                }
                Shape::Point => {
                    return 0.0;
                }
            }
        }
        
        function main() -> int {
            const circle: Shape = Shape::Circle(5.0);
            const area: float = get_area(circle);
            return 0;
        }
    )";
    
    expect_compilation_success(source);
}

// ============================================================================
// Generic Compilation Tests
// ============================================================================

CRYO_TEST(CompilationTest, CompileGenericStruct) { 
    CompilationTestHelper helper; 
    helper.setup();
    std::string source = R"(
        type struct Box<T> {
            value: T;
            
            Box(value: T);
            get_value() -> T;
        }
        
        implement Box<T> {
            Box(value: T) {
                this.value = value;
            }
            
            get_value() -> T {
                return this.value;
            }
        }
        
        function main() -> int {
            const int_box: Box<int> = new Box<int>(42);
            const value: int = int_box.get_value();
            return value;
        }
    )";
    
    expect_compilation_success(source);
}

// ============================================================================
// Error Compilation Tests
// ============================================================================

CRYO_TEST(CompilationTest, RejectUndeclaredVariable) { 
    CompilationTestHelper helper; 
    helper.setup();
    std::string source = R"(
        function main() -> int {
            return undeclared_variable;
        }
    )";
    
    expect_compilation_error(source, "undeclared");
}

CRYO_TEST(CompilationTest, RejectTypeMismatch) { 
    CompilationTestHelper helper; 
    helper.setup();
    std::string source = R"(
        function main() -> int {
            const x: int = "hello";
            return x;
        }
    )";
    
    expect_compilation_error(source, "type");
}

CRYO_TEST(CompilationTest, RejectInvalidFunctionCall) { 
    CompilationTestHelper helper; 
    helper.setup();
    std::string source = R"(
        function add(x: int, y: int) -> int {
            return x + y;
        }
        
        function main() -> int {
            return add(5); // Wrong number of arguments
        }
    )";
    
    expect_compilation_error(source, "argument");
}

// ============================================================================
// Module System Compilation Tests
// ============================================================================

CRYO_TEST(CompilationTest, CompileWithNamespace) { 
    CompilationTestHelper helper; 
    helper.setup();
    std::string source = R"(
        namespace MyModule;
        
        function helper() -> int {
            return 42;
        }
        
        function main() -> int {
            return helper();
        }
    )";
    
    expect_compilation_success(source);
}

CRYO_TEST(CompilationTest, CompileWithImports) { 
    CompilationTestHelper helper; 
    helper.setup();
    std::string source = R"(
        import IO from <io/stdio>;
        import <core/types>;
        
        function main() -> int {
            IO::println("Testing imports");
            return 0;
        }
    )";
    
    expect_compilation_success(source);
}

// ============================================================================
// Memory Management Compilation Tests
// ============================================================================

CRYO_TEST(CompilationTest, CompileWithPointers) { 
    CompilationTestHelper helper; 
    helper.setup();
    std::string source = R"(
        function main() -> int {
            mut x: int = 42;
            const ptr: int* = &x;
            *ptr = 24;
            return x;
        }
    )";
    
    expect_compilation_success(source);
}

CRYO_TEST(CompilationTest, CompileWithReferences) { 
    CompilationTestHelper helper; 
    helper.setup();
    std::string source = R"(
        function modify(ref: int&) -> void {
            ref = ref + 10;
        }
        
        function main() -> int {
            mut x: int = 5;
            modify(x);
            return x;
        }
    )";
    
    expect_compilation_success(source);
}

// ============================================================================
// Performance Compilation Tests
// ============================================================================

CRYO_TEST(CompilationTest, CompileLargeProgram) { 
    CompilationTestHelper helper; 
    helper.setup();
    // Generate a large program to test compilation performance
    std::string large_source = R"(
        namespace PerformanceTest;
        
        type struct DataPoint {
            x: float;
            y: float;
            z: float;
        }
    )";
    
    // Add many functions
    for (int i = 0; i < 50; ++i) {
        large_source += "\nfunction process_data_" + std::to_string(i) + 
                       "(data: DataPoint) -> DataPoint {\n";
        large_source += "    mut result: DataPoint = data;\n";
        large_source += "    result.x = result.x + " + std::to_string(i) + ".0;\n";
        large_source += "    result.y = result.y * 2.0;\n";
        large_source += "    result.z = result.z - 1.0;\n";
        large_source += "    return result;\n";
        large_source += "}\n";
    }
    
    large_source += R"(
        function main() -> int {
            const data: DataPoint = DataPoint({x: 1.0, y: 2.0, z: 3.0});
            mut result: DataPoint = data;
            
            for (mut i: int = 0; i < 10; i++) {
                result = process_data_0(result);
            }
            
            return 0;
        }
    )";
    
    PerformanceTimer timer;
    timer.start();
    
    expect_compilation_success(large_source);
    
    double elapsed = timer.elapsed_ms();
    EXPECT_LT(elapsed, 10000.0) << "Large program compilation should complete within 10 seconds";
    
    std::cout << "Compiled large program in " << elapsed << "ms\n";
}

// ============================================================================
// Stress Tests
// ============================================================================

CRYO_TEST(CompilationTest, CompileDeepNestedStructures) { 
    CompilationTestHelper helper; 
    helper.setup();
    std::string source = R"(
        function deeply_nested() -> int {
            if (true) {
                if (true) {
                    if (true) {
                        if (true) {
                            if (true) {
                                return 42;
                            }
                        }
                    }
                }
            }
            return 0;
        }
        
        function main() -> int {
            return deeply_nested();
        }
    )";
    
    expect_compilation_success(source);
}

CRYO_TEST(CompilationTest, CompileComplexExpressions) { 
    CompilationTestHelper helper; 
    helper.setup();
    std::string source = R"(
        function complex_calculation() -> int {
            const a: int = 1;
            const b: int = 2;
            const c: int = 3;
            const d: int = 4;
            
            const result: int = (a + b) * (c - d) + (a * b) / (c + d) - (a - b) * (c + d);
            return result;
        }
        
        function main() -> int {
            return complex_calculation();
        }
    )";
    
    expect_compilation_success(source);
}

} // namespace CryoTest