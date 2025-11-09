#include <gtest/gtest.h>
#include "test_utils.hpp"
#include <filesystem>
#include <fstream>

namespace CryoTest {

class E2ELanguageTest : public CryoTestBase {
protected:
    std::filesystem::path test_programs_dir;
    
    void SetUp() override {
        CryoTestBase::SetUp();
        test_programs_dir = get_test_data_dir() / "fixtures";
    }
    
    struct CompilationResult {
        bool success;
        std::string output;
        std::vector<std::string> errors;
        int exit_code;
    };
    
    CompilationResult compile_program(const std::filesystem::path& source_file) {
        CompilationResult result;
        
        if (!std::filesystem::exists(source_file)) {
            result.success = false;
            result.errors.push_back("Source file does not exist: " + source_file.string());
            return result;
        }
        
        try {
            // Use the compiler to compile the file
            auto compilation_result = compiler->compile_file(source_file.string());
            
            result.success = compilation_result.success && !compiler->has_errors();
            result.output = compilation_result.output;
            result.exit_code = compilation_result.exit_code;
            
            // Collect error messages
            if (compiler->has_errors()) {
                for (const auto& error : compiler->get_errors()) {
                    result.errors.push_back(error.message);
                }
            }
            
        } catch (const std::exception& e) {
            result.success = false;
            result.errors.push_back("Compilation exception: " + std::string(e.what()));
        }
        
        return result;
    }
    
    void expect_compilation_success(const std::filesystem::path& source_file) {
        auto result = compile_program(source_file);
        
        EXPECT_TRUE(result.success) 
            << "Expected successful compilation of " << source_file.filename()
            << "\nErrors: " << join_strings(result.errors, "\n");
    }
    
    void expect_compilation_failure(const std::filesystem::path& source_file) {
        auto result = compile_program(source_file);
        
        EXPECT_FALSE(result.success) 
            << "Expected compilation failure for " << source_file.filename()
            << " but compilation succeeded";
    }
    
    std::string join_strings(const std::vector<std::string>& strings, const std::string& delimiter) {
        if (strings.empty()) return "";
        
        std::string result = strings[0];
        for (size_t i = 1; i < strings.size(); ++i) {
            result += delimiter + strings[i];
        }
        return result;
    }
};

// ============================================================================
// Valid Program Compilation Tests
// ============================================================================

class E2EValidProgramsTest : public E2ELanguageTest {};

TEST_F(E2EValidProgramsTest, SimpleFunction) {
    auto source_file = test_programs_dir / "valid_programs" / "simple_function.cryo";
    expect_compilation_success(source_file);
}

TEST_F(E2EValidProgramsTest, BasicStruct) {
    auto source_file = test_programs_dir / "valid_programs" / "struct_basic.cryo";
    expect_compilation_success(source_file);
}

TEST_F(E2EValidProgramsTest, Variables) {
    auto source_file = test_programs_dir / "valid_programs" / "variables.cryo";
    expect_compilation_success(source_file);
}

TEST_F(E2EValidProgramsTest, ArithmeticOperations) {
    auto source_file = test_programs_dir / "valid_programs" / "arithmetic.cryo";
    expect_compilation_success(source_file);
}

TEST_F(E2EValidProgramsTest, ConditionalStatements) {
    auto source_file = test_programs_dir / "valid_programs" / "conditionals.cryo";
    expect_compilation_success(source_file);
}

TEST_F(E2EValidProgramsTest, LoopConstructs) {
    auto source_file = test_programs_dir / "valid_programs" / "loops.cryo";
    expect_compilation_success(source_file);
}

TEST_F(E2EValidProgramsTest, ArrayOperations) {
    auto source_file = test_programs_dir / "valid_programs" / "arrays.cryo";
    expect_compilation_success(source_file);
}

TEST_F(E2EValidProgramsTest, FunctionParameters) {
    auto source_file = test_programs_dir / "valid_programs" / "function_params.cryo";
    expect_compilation_success(source_file);
}

TEST_F(E2EValidProgramsTest, StringHandling) {
    auto source_file = test_programs_dir / "valid_programs" / "strings.cryo";
    expect_compilation_success(source_file);
}

TEST_F(E2EValidProgramsTest, NestedStructures) {
    auto source_file = test_programs_dir / "valid_programs" / "nested_structs.cryo";
    expect_compilation_success(source_file);
}

// ============================================================================
// Invalid Program Compilation Tests
// ============================================================================

class E2EInvalidProgramsTest : public E2ELanguageTest {};

TEST_F(E2EInvalidProgramsTest, MissingBrace) {
    auto source_file = test_programs_dir / "invalid_programs" / "missing_brace.cryo";
    expect_compilation_failure(source_file);
}

TEST_F(E2EInvalidProgramsTest, TypeMismatch) {
    auto source_file = test_programs_dir / "invalid_programs" / "type_error.cryo";
    expect_compilation_failure(source_file);
}

TEST_F(E2EInvalidProgramsTest, UndeclaredVariable) {
    auto source_file = test_programs_dir / "invalid_programs" / "undeclared_var.cryo";
    expect_compilation_failure(source_file);
}

TEST_F(E2EInvalidProgramsTest, InvalidSyntax) {
    auto source_file = test_programs_dir / "invalid_programs" / "syntax_error.cryo";
    expect_compilation_failure(source_file);
}

TEST_F(E2EInvalidProgramsTest, DuplicateFunction) {
    auto source_file = test_programs_dir / "invalid_programs" / "duplicate_function.cryo";
    expect_compilation_failure(source_file);
}

TEST_F(E2EInvalidProgramsTest, WrongReturnType) {
    auto source_file = test_programs_dir / "invalid_programs" / "wrong_return.cryo";
    expect_compilation_failure(source_file);
}

TEST_F(E2EInvalidProgramsTest, InvalidArrayAccess) {
    auto source_file = test_programs_dir / "invalid_programs" / "invalid_array.cryo";
    expect_compilation_failure(source_file);
}

TEST_F(E2EInvalidProgramsTest, UnknownFunction) {
    auto source_file = test_programs_dir / "invalid_programs" / "unknown_function.cryo";
    expect_compilation_failure(source_file);
}

TEST_F(E2EInvalidProgramsTest, InvalidStructField) {
    auto source_file = test_programs_dir / "invalid_programs" / "invalid_field.cryo";
    expect_compilation_failure(source_file);
}

TEST_F(E2EInvalidProgramsTest, ConstantReassignment) {
    auto source_file = test_programs_dir / "invalid_programs" / "const_reassign.cryo";
    expect_compilation_failure(source_file);
}

// ============================================================================
// Language Feature Tests
// ============================================================================

class E2ELanguageFeaturesTest : public E2ELanguageTest {
protected:
    void test_language_feature(const std::string& feature_name, 
                             const std::string& source_code, 
                             bool should_compile = true) {
        // Create temporary test file
        auto temp_file = std::filesystem::temp_directory_path() / ("test_" + feature_name + ".cryo");
        
        std::ofstream file(temp_file);
        ASSERT_TRUE(file.good()) << "Failed to create temporary test file";
        file << source_code;
        file.close();
        
        if (should_compile) {
            expect_compilation_success(temp_file);
        } else {
            expect_compilation_failure(temp_file);
        }
        
        // Clean up
        std::filesystem::remove(temp_file);
    }
};

TEST_F(E2ELanguageFeaturesTest, BasicTypes) {
    std::string source = R"(
        function main() -> int {
            const i: int = 42;
            const f: float = 3.14;
            const b: boolean = true;
            const s: string = "hello";
            return 0;
        }
    )";
    
    test_language_feature("basic_types", source, true);
}

TEST_F(E2ELanguageFeaturesTest, ArithmeticExpressions) {
    std::string source = R"(
        function main() -> int {
            const a: int = 10;
            const b: int = 5;
            const sum: int = a + b;
            const diff: int = a - b;
            const product: int = a * b;
            const quotient: int = a / b;
            const remainder: int = a % b;
            return sum;
        }
    )";
    
    test_language_feature("arithmetic", source, true);
}

TEST_F(E2ELanguageFeaturesTest, ComparisonOperators) {
    std::string source = R"(
        function main() -> int {
            const a: int = 10;
            const b: int = 5;
            const less: boolean = a < b;
            const greater: boolean = a > b;
            const equal: boolean = a == b;
            const not_equal: boolean = a != b;
            const less_equal: boolean = a <= b;
            const greater_equal: boolean = a >= b;
            return 0;
        }
    )";
    
    test_language_feature("comparisons", source, true);
}

TEST_F(E2ELanguageFeaturesTest, LogicalOperators) {
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
    
    test_language_feature("logical_ops", source, true);
}

TEST_F(E2ELanguageFeaturesTest, ConditionalStatements) {
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
    
    test_language_feature("conditionals", source, true);
}

TEST_F(E2ELanguageFeaturesTest, WhileLoops) {
    std::string source = R"(
        function main() -> int {
            mut i: int = 0;
            while (i < 5) {
                i = i + 1;
            }
            return i;
        }
    )";
    
    test_language_feature("while_loop", source, true);
}

TEST_F(E2ELanguageFeaturesTest, ForLoops) {
    std::string source = R"(
        function main() -> int {
            mut sum: int = 0;
            for (mut i: int = 0; i < 5; i = i + 1) {
                sum = sum + i;
            }
            return sum;
        }
    )";
    
    test_language_feature("for_loop", source, true);
}

TEST_F(E2ELanguageFeaturesTest, Arrays) {
    std::string source = R"(
        function main() -> int {
            const numbers: int[] = [1, 2, 3, 4, 5];
            const first: int = numbers[0];
            const last: int = numbers[4];
            return first + last;
        }
    )";
    
    test_language_feature("arrays", source, true);
}

TEST_F(E2ELanguageFeaturesTest, Structs) {
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
    
    test_language_feature("structs", source, true);
}

TEST_F(E2ELanguageFeaturesTest, Functions) {
    std::string source = R"(
        function add(a: int, b: int) -> int {
            return a + b;
        }
        
        function main() -> int {
            return add(5, 10);
        }
    )";
    
    test_language_feature("functions", source, true);
}

TEST_F(E2ELanguageFeaturesTest, MutableVariables) {
    std::string source = R"(
        function main() -> int {
            mut x: int = 10;
            x = 20;
            x = x + 5;
            return x;
        }
    )";
    
    test_language_feature("mutable_vars", source, true);
}

TEST_F(E2ELanguageFeaturesTest, ConstantVariables) {
    std::string source = R"(
        function main() -> int {
            const x: int = 42;
            return x;
        }
    )";
    
    test_language_feature("const_vars", source, true);
}

TEST_F(E2ELanguageFeaturesTest, NestedFunctions) {
    std::string source = R"(
        function outer(x: int) -> int {
            return inner(x * 2);
        }
        
        function inner(y: int) -> int {
            return y + 1;
        }
        
        function main() -> int {
            return outer(5);
        }
    )";
    
    test_language_feature("nested_functions", source, true);
}

TEST_F(E2ELanguageFeaturesTest, RecursiveFunctions) {
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
    
    test_language_feature("recursion", source, true);
}

// ============================================================================
// Error Detection Tests
// ============================================================================

TEST_F(E2ELanguageFeaturesTest, TypeMismatchError) {
    std::string source = R"(
        function main() -> int {
            const x: int = "not a number";
            return x;
        }
    )";
    
    test_language_feature("type_mismatch", source, false);
}

TEST_F(E2ELanguageFeaturesTest, UndeclaredVariableError) {
    std::string source = R"(
        function main() -> int {
            return undeclared_variable;
        }
    )";
    
    test_language_feature("undeclared_var", source, false);
}

TEST_F(E2ELanguageFeaturesTest, ConstantReassignmentError) {
    std::string source = R"(
        function main() -> int {
            const x: int = 10;
            x = 20;
            return x;
        }
    )";
    
    test_language_feature("const_reassign", source, false);
}

TEST_F(E2ELanguageFeaturesTest, InvalidReturnTypeError) {
    std::string source = R"(
        function get_string() -> string {
            return 42;
        }
        
        function main() -> int {
            return 0;
        }
    )";
    
    test_language_feature("invalid_return", source, false);
}

TEST_F(E2ELanguageFeaturesTest, MissingReturnError) {
    std::string source = R"(
        function get_value() -> int {
            // Missing return statement
        }
        
        function main() -> int {
            return 0;
        }
    )";
    
    test_language_feature("missing_return", source, false);
}

TEST_F(E2ELanguageFeaturesTest, InvalidArrayIndexError) {
    std::string source = R"(
        function main() -> int {
            const numbers: int[] = [1, 2, 3];
            const invalid: int = numbers["not_an_index"];
            return invalid;
        }
    )";
    
    test_language_feature("invalid_array_index", source, false);
}

TEST_F(E2ELanguageFeaturesTest, UnknownFunctionError) {
    std::string source = R"(
        function main() -> int {
            return unknown_function(42);
        }
    )";
    
    test_language_feature("unknown_function", source, false);
}

TEST_F(E2ELanguageFeaturesTest, InvalidStructFieldError) {
    std::string source = R"(
        type struct Point {
            x: int;
            y: int;
        }
        
        function main() -> int {
            const p: Point = Point({x: 10, y: 20});
            return p.z; // Field doesn't exist
        }
    )";
    
    test_language_feature("invalid_struct_field", source, false);
}

// ============================================================================
// Performance Tests
// ============================================================================

class E2EPerformanceTest : public E2ELanguageTest {
protected:
    void benchmark_compilation(const std::string& test_name, 
                             const std::string& source_code,
                             int iterations = 10) {
        auto start_time = std::chrono::high_resolution_clock::now();
        
        for (int i = 0; i < iterations; ++i) {
            // Create temporary file for each iteration
            auto temp_file = std::filesystem::temp_directory_path() / 
                           ("bench_" + test_name + "_" + std::to_string(i) + ".cryo");
            
            std::ofstream file(temp_file);
            ASSERT_TRUE(file.good());
            file << source_code;
            file.close();
            
            // Compile and measure
            auto result = compile_program(temp_file);
            EXPECT_TRUE(result.success) << "Benchmark compilation failed for " << test_name;
            
            // Clean up
            std::filesystem::remove(temp_file);
        }
        
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
        
        test_logger.info("Performance benchmark '" + test_name + "': " + 
                        std::to_string(duration.count()) + "ms for " + 
                        std::to_string(iterations) + " iterations");
        test_logger.info("Average: " + std::to_string(duration.count() / iterations) + "ms per compilation");
    }
};

TEST_F(E2EPerformanceTest, SimpleProgram) {
    std::string source = R"(
        function main() -> int {
            return 42;
        }
    )";
    
    benchmark_compilation("simple_program", source, 100);
}

TEST_F(E2EPerformanceTest, ComplexProgram) {
    std::string source = R"(
        type struct Point {
            x: int;
            y: int;
        }
        
        function calculate_distance(p1: Point, p2: Point) -> float {
            const dx: int = p1.x - p2.x;
            const dy: int = p1.y - p2.y;
            return sqrt((dx * dx) + (dy * dy));
        }
        
        function main() -> int {
            const points: Point[] = [
                Point({x: 0, y: 0}),
                Point({x: 3, y: 4}),
                Point({x: 6, y: 8})
            ];
            
            mut total_distance: float = 0.0;
            for (mut i: int = 0; i < 2; i = i + 1) {
                total_distance = total_distance + calculate_distance(points[i], points[i + 1]);
            }
            
            return (int)total_distance;
        }
    )";
    
    benchmark_compilation("complex_program", source, 50);
}

} // namespace CryoTest