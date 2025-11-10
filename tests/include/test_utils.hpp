#pragma once

#include <memory>
#include <string>
#include <vector>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <functional>

// Test framework macros (self-contained, no external dependencies)
namespace CryoTest {

class AssertionError : public std::runtime_error {
public:
    AssertionError(const std::string& message) : std::runtime_error(message) {}
};

}

// Simple assertion macros
#define CRYO_ASSERT(condition, message) \
    do { \
        if (!(condition)) { \
            throw CryoTest::AssertionError("ASSERTION FAILED: " + std::string(message) + \
                                         " at " + __FILE__ + ":" + std::to_string(__LINE__)); \
        } \
    } while(0)

#define CRYO_ASSERT_EQ(expected, actual) \
    do { \
        if ((expected) != (actual)) { \
            std::ostringstream oss; \
            oss << "ASSERTION FAILED: Expected equality but values differ at " << __FILE__ << ":" << __LINE__; \
            throw CryoTest::AssertionError(oss.str()); \
        } \
    } while(0)

#define CRYO_ASSERT_NE(expected, actual) \
    do { \
        if ((expected) == (actual)) { \
            std::ostringstream oss; \
            oss << "ASSERTION FAILED: Values should not be equal at " << __FILE__ << ":" << __LINE__; \
            throw CryoTest::AssertionError(oss.str()); \
        } \
    } while(0)

#define CRYO_ASSERT_TRUE(condition) CRYO_ASSERT(condition, "Condition should be true")
#define CRYO_ASSERT_FALSE(condition) CRYO_ASSERT(!(condition), "Condition should be false")

// String equality macro
#define CRYO_ASSERT_STREQ(expected, actual) \
    do { \
        if (std::string(expected) != std::string(actual)) { \
            throw CryoTest::AssertionError("ASSERTION FAILED: String mismatch - Expected: '" + \
                                         std::string(expected) + "', Got: '" + std::string(actual) + "'" + \
                                         " at " + __FILE__ + ":" + std::to_string(__LINE__)); \
        } \
    } while(0)

// Test registration and execution macros
#define CRYO_TEST(test_class, test_name) \
    class test_class##_##test_name : public test_class { \
    public: \
        void run_test(); \
    }; \
    void test_class##_##test_name::run_test()

// CryoLang includes
#include "Compiler/CompilerInstance.hpp"
#include "Lexer/lexer.hpp"
#include "Parser/Parser.hpp"
#include "AST/ASTContext.hpp"
#include "AST/TypeChecker.hpp"
#include "Codegen/CodeGenerator.hpp"
#include "Utils/File.hpp"
#include "Utils/Logger.hpp"

namespace CryoTest {

/**
 * @brief Base class for all CryoLang tests
 * 
 * Provides common utilities and setup/teardown for compiler component testing.
 * All test classes should inherit from this to get access to compiler infrastructure.
 */
class CryoTestBase {
protected:
    std::unique_ptr<Cryo::CompilerInstance> compiler;
    std::unique_ptr<Cryo::ASTContext> ast_context;
    std::filesystem::path temp_dir;
    
    void SetUp() {
        // Create temporary directory for test files
        temp_dir = std::filesystem::temp_directory_path() / "cryo_tests" / 
                   std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        std::filesystem::create_directories(temp_dir);
        
        // Initialize compiler instance
        compiler = std::make_unique<Cryo::CompilerInstance>();
        compiler->set_debug_mode(false); // Disable debug output during tests
        
        // Initialize AST context
        ast_context = std::make_unique<Cryo::ASTContext>();
    }
    
    void TearDown() {
        // Clean up temporary files
        if (std::filesystem::exists(temp_dir)) {
            std::filesystem::remove_all(temp_dir);
        }
        
        // Reset compiler state
        compiler.reset();
        ast_context.reset();
    }
    
    /**
     * @brief Create a temporary file with given content
     */
    std::filesystem::path create_temp_file(const std::string& filename, const std::string& content) {
        auto file_path = temp_dir / filename;
        std::ofstream file(file_path);
        file << content;
        file.close();
        return file_path;
    }
    
    /**
     * @brief Compile source code and return success status
     */
    bool compile_source(const std::string& source) {
        auto temp_file = create_temp_file("test.cryo", source);
        return compiler->compile_file(temp_file.string());
    }
    
    /**
     * @brief Parse source code and return AST
     */
    std::unique_ptr<Cryo::ProgramNode> parse_source(const std::string& source) {
        auto file = Cryo::make_file_from_string("test.cryo", source);
        if (!file) return nullptr;
        
        auto lexer = std::make_unique<Cryo::Lexer>(std::move(file));
        auto parser = std::make_unique<Cryo::Parser>(std::move(lexer), *ast_context);
        
        return parser->parse_program();
    }
    
    /**
     * @brief Tokenize source code and return tokens
     */
    std::vector<Cryo::Token> tokenize_source(const std::string& source) {
        auto file = Cryo::make_file_from_string("test.cryo", source);
        if (!file) return {};
        
        auto lexer = std::make_unique<Cryo::Lexer>(std::move(file));
        std::vector<Cryo::Token> tokens;
        
        while (lexer->has_more_tokens()) {
            auto token = lexer->next_token();
            if (token.is(Cryo::TokenKind::TK_EOF)) break;
            tokens.push_back(token);
        }
        
        return tokens;
    }
    
    /**
     * @brief Assert that compilation fails with specific error
     */
    void expect_compilation_error(const std::string& source, const std::string& expected_error = "") {
        bool compiled = compile_source(source);
        CRYO_ASSERT(!compiled, "Expected compilation to fail, but it succeeded");
        
        if (!expected_error.empty() && compiler->diagnostic_manager()) {
            bool found_error = false;
            for (const auto& diagnostic : compiler->diagnostic_manager()->diagnostics()) {
                if (diagnostic.message().find(expected_error) != std::string::npos) {
                    found_error = true;
                    break;
                }
            }
            CRYO_ASSERT(found_error, "Expected error message containing: " + expected_error);
        }
    }
    
    /**
     * @brief Assert that compilation succeeds
     */
    void expect_compilation_success(const std::string& source) {
        bool compiled = compile_source(source);
        CRYO_ASSERT(compiled, "Expected compilation to succeed, but it failed");
        
        if (!compiled && compiler->diagnostic_manager()) {
            std::cout << "Compilation errors:\n";
            for (const auto& diagnostic : compiler->diagnostic_manager()->diagnostics()) {
                std::cout << "  " << diagnostic.message() << "\n";
            }
        }
    }
};

/**
 * @brief Test fixture for lexer tests
 */
class LexerTestFixture : public CryoTestBase {
protected:
    std::unique_ptr<Cryo::Lexer> create_lexer(const std::string& source) {
        auto file = Cryo::make_file_from_string("test.cryo", source);
        return std::make_unique<Cryo::Lexer>(std::move(file));
    }
    
    void expect_token_sequence(const std::string& source, 
                              const std::vector<Cryo::TokenKind>& expected_tokens) {
        auto lexer = create_lexer(source);
        
        for (size_t i = 0; i < expected_tokens.size(); ++i) {
            CRYO_ASSERT(lexer->has_more_tokens(), "Expected more tokens at position " + std::to_string(i));
            auto token = lexer->next_token();
            CRYO_ASSERT_EQ(token.kind(), expected_tokens[i]);
        }
    }
};

/**
 * @brief Test fixture for parser tests
 */
class ParserTestFixture : public CryoTestBase {
protected:
    std::unique_ptr<Cryo::Parser> create_parser(const std::string& source) {
        auto file = Cryo::make_file_from_string("test.cryo", source);
        auto lexer = std::make_unique<Cryo::Lexer>(std::move(file));
        return std::make_unique<Cryo::Parser>(std::move(lexer), *ast_context);
    }
    
    void expect_parse_success(const std::string& source) {
        auto parser = create_parser(source);
        auto ast = parser->parse_program();
        CRYO_ASSERT(ast != nullptr, "Parsing failed for: " + source);
        CRYO_ASSERT(!parser->has_errors(), "Parser reported errors");
    }
    
    void expect_parse_error(const std::string& source) {
        auto parser = create_parser(source);
        auto ast = parser->parse_program();
        CRYO_ASSERT(parser->has_errors(), "Expected parse error for: " + source);
    }
};

/**
 * @brief Performance measurement utilities
 */
class PerformanceTimer {
private:
    std::chrono::high_resolution_clock::time_point start_time;
    
public:
    void start() {
        start_time = std::chrono::high_resolution_clock::now();
    }
    
    double elapsed_ms() {
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
        return duration.count() / 1000.0;
    }
};

/**
 * @brief Benchmark test fixture
 */
class BenchmarkTestFixture : public CryoTestBase {
protected:
    static constexpr int DEFAULT_ITERATIONS = 100;
    
    template<typename Func>
    double benchmark(Func&& func, int iterations = DEFAULT_ITERATIONS) {
        PerformanceTimer timer;
        timer.start();
        
        for (int i = 0; i < iterations; ++i) {
            func();
        }
        
        return timer.elapsed_ms() / iterations; // Average time per iteration
    }
    
    void expect_performance_within(double actual_ms, double expected_ms, double tolerance = 0.1) {
        double lower_bound = expected_ms * (1.0 - tolerance);
        double upper_bound = expected_ms * (1.0 + tolerance);
        
        CRYO_ASSERT(actual_ms >= lower_bound, 
            "Performance better than expected (might indicate measurement error)");
        CRYO_ASSERT(actual_ms <= upper_bound, 
            "Performance worse than expected");
    }
};

/**
 * @brief Memory leak detection utilities
 */
class MemoryTestFixture : public CryoTestBase {
protected:
    size_t initial_memory_usage = 0;
    
    void SetUp() {
        CryoTestBase::SetUp();
        initial_memory_usage = get_memory_usage();
    }
    
    void TearDown() {
        size_t final_memory_usage = get_memory_usage();
        size_t memory_diff = final_memory_usage - initial_memory_usage;
        
        // Allow some tolerance for normal allocations
        CRYO_ASSERT(memory_diff < 1024 * 1024, // 1MB tolerance
            "Potential memory leak detected: " + std::to_string(memory_diff) + " bytes");
            
        CryoTestBase::TearDown();
    }
    
private:
    size_t get_memory_usage() {
        // Platform-specific memory usage detection
#ifdef _WIN32
        // Windows implementation would go here
        return 0;
#else
        // Linux/Unix implementation would go here
        return 0;
#endif
    }
};

/**
 * @brief Test data provider utilities
 */
namespace TestData {
    
    inline std::vector<std::string> valid_programs() {
        return {
            R"(
                function main() -> int {
                    return 0;
                }
            )",
            R"(
                function add(x: int, y: int) -> int {
                    return x + y;
                }
                
                function main() -> int {
                    const result: int = add(5, 10);
                    return result;
                }
            )",
            R"(
                type struct Point {
                    x: int;
                    y: int;
                }
                
                function main() -> int {
                    const p: Point = Point({x: 10, y: 20});
                    return 0;
                }
            )"
        };
    }
    
    inline std::vector<std::string> invalid_programs() {
        return {
            "function ( ) {",  // Invalid syntax
            "const x: int = \"hello\";",  // Type mismatch
            "undeclared_function();",  // Undeclared identifier
        };
    }
    
    inline std::vector<std::pair<std::string, std::vector<Cryo::TokenKind>>> lexer_test_cases() {
        return {
            {
                "function add(x: int) -> int { return x; }",
                {
                    Cryo::TokenKind::TK_KW_FUNCTION,
                    Cryo::TokenKind::TK_IDENTIFIER,
                    Cryo::TokenKind::TK_L_PAREN,
                    Cryo::TokenKind::TK_IDENTIFIER,
                    Cryo::TokenKind::TK_COLON,
                    Cryo::TokenKind::TK_IDENTIFIER,
                    Cryo::TokenKind::TK_R_PAREN,
                    Cryo::TokenKind::TK_ARROW,
                    Cryo::TokenKind::TK_IDENTIFIER,
                    Cryo::TokenKind::TK_L_BRACE,
                    Cryo::TokenKind::TK_KW_RETURN,
                    Cryo::TokenKind::TK_IDENTIFIER,
                    Cryo::TokenKind::TK_SEMICOLON,
                    Cryo::TokenKind::TK_R_BRACE
                }
            }
        };
    }
}

} // namespace CryoTest