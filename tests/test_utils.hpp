#pragma once
/**
 * @file test_utils.hpp
 * @brief Self-contained testing utilities for CryoLang (no third-party dependencies)
 * 
 * This header provides all the necessary utilities for testing the Cryo compiler
 * without relying on any external testing frameworks.
 */

#include <iostream>
#include <string>
#include <memory>
#include <filesystem>
#include <sstream>
#include <stdexcept>
#include <chrono>
#include <fstream>
#include <vector>
#include <functional>

namespace CryoTest {

/**
 * @brief Enhanced assertion error with detailed context
 */
class AssertionError : public std::runtime_error {
public:
    std::string file_name;
    int line_number;
    std::string condition;
    std::string expected_value;
    std::string actual_value;
    std::string context;
    
    AssertionError(const std::string& message) : std::runtime_error(message) {}
    
    AssertionError(const std::string& file, int line, const std::string& cond, 
                   const std::string& expected = "", const std::string& actual = "",
                   const std::string& ctx = "") 
        : std::runtime_error("Assertion failed"), file_name(file), line_number(line), 
          condition(cond), expected_value(expected), actual_value(actual), context(ctx) {}
    
    std::string get_detailed_message() const {
        std::stringstream ss;
        ss << "Assertion Failed\n";
        ss << "  Location: " << file_name << ":" << line_number << "\n";
        ss << "  Condition: " << condition << "\n";
        if (!expected_value.empty() && !actual_value.empty()) {
            ss << "  Expected: " << expected_value << "\n";
            ss << "  Actual:   " << actual_value << "\n";
        }
        if (!context.empty()) {
            ss << "  Context: " << context << "\n";
        }
        return ss.str();
    }
};

/**
 * @brief Simple test logger for debugging
 */
class TestLogger {
public:
    enum Level { INFO, WARNING, ERROR };
    
    void info(const std::string& message) {
        log(INFO, message);
    }
    
    void warning(const std::string& message) {
        log(WARNING, message);
    }
    
    void error(const std::string& message) {
        log(ERROR, message);
    }
    
private:
    void log(Level level, const std::string& message) {
        const char* level_str[] = {"INFO", "WARN", "ERROR"};
        std::cout << "[" << level_str[level] << "] " << message << std::endl;
    }
};

/**
 * @brief Base test class providing common functionality
 */
class CryoTestBase {
protected:
    TestLogger test_logger;
    
public:
    virtual ~CryoTestBase() = default;
    
    virtual void SetUp() {
        test_logger.info("Setting up test environment");
    }
    
    virtual void TearDown() {
        test_logger.info("Tearing down test environment");
    }
    
    std::filesystem::path get_test_data_dir() const {
        return std::filesystem::current_path() / "tests" / "fixtures";
    }
    
    std::string read_file_content(const std::filesystem::path& file_path) {
        if (!std::filesystem::exists(file_path)) {
            throw std::runtime_error("Test file not found: " + file_path.string());
        }
        
        std::ifstream file(file_path);
        if (!file.is_open()) {
            throw std::runtime_error("Cannot open test file: " + file_path.string());
        }
        
        std::stringstream buffer;
        buffer << file.rdbuf();
        return buffer.str();
    }
    
    /**
     * @brief Get source code context around a specific line
     */
    std::string get_source_context(const std::string& filename, int line_number, int context_lines = 2) {
        std::ifstream file(filename);
        if (!file.is_open()) {
            return "Source file not available";
        }
        
        std::vector<std::string> lines;
        std::string line;
        while (std::getline(file, line)) {
            lines.push_back(line);
        }
        
        if (line_number <= 0 || line_number > static_cast<int>(lines.size())) {
            return "Invalid line number";
        }
        
        std::stringstream ss;
        int start = std::max(1, line_number - context_lines);
        int end = std::min(static_cast<int>(lines.size()), line_number + context_lines);
        
        for (int i = start; i <= end; ++i) {
            if (i == line_number) {
                ss << "  > " << i << " | " << lines[i-1] << "\n";  // Highlight failure line
            } else {
                ss << "    " << i << " | " << lines[i-1] << "\n";
            }
        }
        
        return ss.str();
    }
};

// Assertion macros (replacing Google Test assertions)
#define CRYO_ASSERT_TRUE(condition) \
    do { \
        if (!(condition)) { \
            throw CryoTest::AssertionError(__FILE__, __LINE__, #condition, "true", "false"); \
        } \
    } while(0)

#define CRYO_ASSERT_FALSE(condition) \
    do { \
        if (condition) { \
            throw CryoTest::AssertionError(__FILE__, __LINE__, #condition, "false", "true"); \
        } \
    } while(0)

#define CRYO_ASSERT_EQ(expected, actual) \
    do { \
        auto exp_val = (expected); \
        auto act_val = (actual); \
        if (exp_val != act_val) { \
            std::stringstream exp_ss, act_ss; \
            exp_ss << exp_val; \
            act_ss << act_val; \
            throw CryoTest::AssertionError(__FILE__, __LINE__, #expected " == " #actual, exp_ss.str(), act_ss.str()); \
        } \
    } while(0)

#define CRYO_ASSERT_NE(expected, actual) \
    do { \
        if ((expected) == (actual)) { \
            std::stringstream ss; \
            ss << "Assertion failed: expected " << (expected) << " != " << (actual) << " at " << __FILE__ << ":" << __LINE__; \
            throw CryoTest::AssertionError(ss.str()); \
        } \
    } while(0)

#define CRYO_ASSERT_STREQ(expected, actual) \
    do { \
        std::string exp_str = (expected); \
        std::string act_str = (actual); \
        if (exp_str != act_str) { \
            throw CryoTest::AssertionError(__FILE__, __LINE__, #expected " == " #actual, "\"" + exp_str + "\"", "\"" + act_str + "\""); \
        } \
    } while(0)

#define CRYO_EXPECT_TRUE(condition) CRYO_ASSERT_TRUE(condition)
#define CRYO_EXPECT_FALSE(condition) CRYO_ASSERT_FALSE(condition)
#define CRYO_EXPECT_EQ(expected, actual) CRYO_ASSERT_EQ(expected, actual)
#define CRYO_EXPECT_NE(expected, actual) CRYO_ASSERT_NE(expected, actual)
#define CRYO_EXPECT_STREQ(expected, actual) CRYO_ASSERT_STREQ(expected, actual)

// Compatibility aliases for existing tests
#define ASSERT_TRUE(condition) CRYO_ASSERT_TRUE(condition)
#define ASSERT_FALSE(condition) CRYO_ASSERT_FALSE(condition)
#define ASSERT_EQ(expected, actual) CRYO_ASSERT_EQ(expected, actual)
#define ASSERT_NE(expected, actual) CRYO_ASSERT_NE(expected, actual)
#define EXPECT_TRUE(condition) CRYO_EXPECT_TRUE(condition)
#define EXPECT_FALSE(condition) CRYO_EXPECT_FALSE(condition)
#define EXPECT_EQ(expected, actual) CRYO_EXPECT_EQ(expected, actual)
#define EXPECT_NE(expected, actual) CRYO_EXPECT_NE(expected, actual)
#define EXPECT_STREQ(expected, actual) CRYO_EXPECT_STREQ(expected, actual)

/**
 * @brief Performance measurement utilities
 */
class PerformanceMeasure {
private:
    std::chrono::steady_clock::time_point start_time;
    
public:
    void start() {
        start_time = std::chrono::steady_clock::now();
    }
    
    long long elapsed_ms() const {
        auto end_time = std::chrono::steady_clock::now();
        return std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
    }
    
    long long elapsed_us() const {
        auto end_time = std::chrono::steady_clock::now();
        return std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time).count();
    }
};

/**
 * @brief Test environment setup/teardown functions
 */
void initialize_test_environment();
void cleanup_test_environment();

/**
 * @brief Memory usage tracking for performance tests
 */
class MemoryTracker {
public:
    static size_t get_current_memory_usage() {
        // Platform-specific memory usage tracking
        // For now, return 0 as placeholder
        return 0;
    }
    
    static size_t get_peak_memory_usage() {
        // Platform-specific peak memory usage
        return 0;
    }
};

/**
 * @brief Test framework infrastructure
 */
struct TestCase {
    std::string name;
    std::string suite;
    std::string description;
    std::function<void()> test_function;
};

class TestRegistry {
public:
    static TestRegistry& instance() {
        static TestRegistry registry;
        return registry;
    }
    
    void register_test(const std::string& suite, const std::string& name, std::function<void()> test_func, const std::string& description = "") {
        tests.push_back({name, suite, description, test_func});
    }
    
    int run_all_tests();
    
private:
    std::vector<TestCase> tests;
};

class TestRegistrar {
public:
    TestRegistrar(const std::string& suite, const std::string& name, std::function<void()> test_func, const std::string& description = "") {
        TestRegistry::instance().register_test(suite, name, test_func, description);
    }
};

} // namespace CryoTest

// Macro to define tests
#define CRYO_TEST(suite_name, test_name) \
    void suite_name##_##test_name##_Test(); \
    static CryoTest::TestRegistrar suite_name##_##test_name##_registrar(#suite_name, #test_name, suite_name##_##test_name##_Test); \
    void suite_name##_##test_name##_Test()

// Macro to define tests with descriptions
#define CRYO_TEST_DESC(suite_name, test_name, description) \
    void suite_name##_##test_name##_Test(); \
    static CryoTest::TestRegistrar suite_name##_##test_name##_registrar(#suite_name, #test_name, suite_name##_##test_name##_Test, description); \
    void suite_name##_##test_name##_Test()

// Include test helpers (compiler component helpers)
// Note: This is included separately to avoid circular dependencies
// Include test_helpers.hpp directly in test files that need it