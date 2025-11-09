#include "test_utils.hpp"
#include <iostream>
#include <vector>
#include <functional>
#include <string>
#include <filesystem>

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
 * @brief Main test runner entry point - simple and clean
 */
int main(int argc, char** argv) {
    std::cout << "CryoLang Test Suite" << std::endl;
    std::cout << "===================" << std::endl;
    
    // Initialize test environment
    CryoTest::initialize_test_environment();
    
    // Run all tests
    int result = CryoTest::TestRegistry::instance().run_all_tests();
    
    // Cleanup
    CryoTest::cleanup_test_environment();
    
    return result;
}