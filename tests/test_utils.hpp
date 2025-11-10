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
 * @brief Custom assertion error for test failures
 */
class AssertionError : public std::runtime_error {
public:
    AssertionError(const std::string& message) : std::runtime_error(message) {}
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
};

// Assertion macros (replacing Google Test assertions)
#define CRYO_ASSERT_TRUE(condition) \
    do { \
        if (!(condition)) { \
            std::stringstream ss; \
            ss << "Assertion failed: " << #condition << " at " << __FILE__ << ":" << __LINE__; \
            throw CryoTest::AssertionError(ss.str()); \
        } \
    } while(0)

#define CRYO_ASSERT_FALSE(condition) \
    do { \
        if (condition) { \
            std::stringstream ss; \
            ss << "Assertion failed: expected false but got true for " << #condition << " at " << __FILE__ << ":" << __LINE__; \
            throw CryoTest::AssertionError(ss.str()); \
        } \
    } while(0)

#define CRYO_ASSERT_EQ(expected, actual) \
    do { \
        if ((expected) != (actual)) { \
            std::stringstream ss; \
            ss << "Assertion failed: expected " << (expected) << " but got " << (actual) << " at " << __FILE__ << ":" << __LINE__; \
            throw CryoTest::AssertionError(ss.str()); \
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
            std::stringstream ss; \
            ss << "String assertion failed: expected \"" << exp_str << "\" but got \"" << act_str << "\" at " << __FILE__ << ":" << __LINE__; \
            throw CryoTest::AssertionError(ss.str()); \
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
    std::function<void()> test_function;
};

class TestRegistry {
public:
    static TestRegistry& instance() {
        static TestRegistry registry;
        return registry;
    }
    
    void register_test(const std::string& suite, const std::string& name, std::function<void()> test_func) {
        tests.push_back({name, suite, test_func});
    }
    
    int run_all_tests();
    
private:
    std::vector<TestCase> tests;
};

class TestRegistrar {
public:
    TestRegistrar(const std::string& suite, const std::string& name, std::function<void()> test_func) {
        TestRegistry::instance().register_test(suite, name, test_func);
    }
};

} // namespace CryoTest

// Macro to define tests
#define CRYO_TEST(suite_name, test_name) \
    void suite_name##_##test_name##_Test(); \
    static CryoTest::TestRegistrar suite_name##_##test_name##_registrar(#suite_name, #test_name, suite_name##_##test_name##_Test); \
    void suite_name##_##test_name##_Test()

// Include test helpers (compiler component helpers)
// Note: This is included separately to avoid circular dependencies
// Include test_helpers.hpp directly in test files that need it