#include "test_utils.hpp"
#include <iostream>
#include <vector>
#include <functional>
#include <string>
#include <filesystem>
#include <exception>
#include <cstdlib>

/**
 * @file test_main.cpp
 * @brief Self-contained test framework for CryoLang (no third-party dependencies)
 * 
 * This file provides a simple, lightweight test framework using only standard C++.
 * It replaces Google Test with a minimal but effective testing solution.
 */

namespace CryoTest {
    // Main implementation is in test_utils.hpp/cpp
} // namespace CryoTest

/**
 * @brief Main test runner entry point - supports both full suite and single test execution
 */
int main(int argc, char** argv) {
    // Check if we're running a single test (for process isolation)
    if (argc >= 3 && std::string(argv[1]) == "--run-single-test") {
        std::string test_name = argv[2];
        
        // SELECTIVE SUPPRESSION: Only suppress stderr (terminate messages), keep stdout for test output
        #ifdef _WIN32
        freopen("NUL", "w", stderr);  // Suppress std::terminate messages only
        #else
        freopen("/dev/null", "w", stderr);
        #endif
        
        // SILENT TERMINATE: Set custom terminate handler that exits quietly
        std::set_terminate([]() {
            exit(999);  // Use distinctive exit code for terminated processes
        });
        
        // Initialize test environment
        CryoTest::initialize_test_environment();
        
        // Run single test
        int result = CryoTest::TestRegistry::instance().run_single_test(test_name);
        
        // Cleanup
        CryoTest::cleanup_test_environment();
        
        return result;
    }
    
    // Normal full test suite execution
    std::cout << "CryoLang Test Suite" << std::endl;
    std::cout << "===================" << std::endl;
    
    // Store executable path for process isolation
    CryoTest::set_test_executable_path(argv[0]);
    
    // Initialize test environment
    CryoTest::initialize_test_environment();
    
    // Run ALL tests - no exceptions allowed to stop this
    int result = CryoTest::TestRegistry::instance().run_all_tests();
    
    // Cleanup
    CryoTest::cleanup_test_environment();
    
    return 0; // Always return 0 to ensure test suite completion
}