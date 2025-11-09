#include "test_utils.hpp"
#include <filesystem>
#include <iostream>

namespace CryoTest {

void initialize_test_environment() {
    std::cout << "📁 Initializing test environment..." << std::endl;
    
    // Create necessary test directories
    std::filesystem::create_directories("bin/tests/results");
    std::filesystem::create_directories("bin/tests/temp");
    std::filesystem::create_directories("tests/fixtures/valid_programs");
    std::filesystem::create_directories("tests/fixtures/invalid_programs");
    
    std::cout << "✅ Test environment initialized" << std::endl;
}

void cleanup_test_environment() {
    std::cout << "🧹 Cleaning up test environment..." << std::endl;
    
    // Clean up temporary test files
    if (std::filesystem::exists("bin/tests/temp")) {
        std::filesystem::remove_all("bin/tests/temp");
    }
    
    std::cout << "✅ Test environment cleaned up" << std::endl;
}

int TestRegistry::run_all_tests() {
    int passed = 0;
    int failed = 0;
    
    std::cout << "\n🚀 Starting CryoLang Test Suite (Self-Contained)\n" << std::endl;
    std::cout << "Running " << tests.size() << " tests..." << std::endl;
    std::cout << "========================================\n" << std::endl;
    
    for (const auto& test : tests) {
        std::cout << "[ RUN      ] " << test.suite << "." << test.name;
        
        try {
            test.test_function();
            std::cout << " ... OK" << std::endl;
            passed++;
        } catch (const AssertionError& e) {
            std::cout << " ... FAILED" << std::endl;
            std::cout << "             " << e.what() << std::endl;
            failed++;
        } catch (const std::exception& e) {
            std::cout << " ... FAILED" << std::endl;
            std::cout << "             Exception: " << e.what() << std::endl;
            failed++;
        }
    }
    
    std::cout << "\n========================================" << std::endl;
    std::cout << "Test Results Summary:" << std::endl;
    std::cout << "  Total: " << (passed + failed) << std::endl;
    std::cout << "  Passed: " << passed << std::endl;
    std::cout << "  Failed: " << failed << std::endl;
    
    if (failed == 0) {
        std::cout << "\n✅ All tests passed!" << std::endl;
        return 0;
    } else {
        std::cout << "\n❌ " << failed << " test(s) failed!" << std::endl;
        return 1;
    }
}

} // namespace CryoTest